/* Fast paths for compute pipeline history/queue/waiter gating, prepare memo, and xlate reject cache (Opt 47). */
#ifndef VK_COMPUTE_FAST_H
#define VK_COMPUTE_FAST_H
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TOTK_W45_TEST_HOOKS
extern _Atomic int g_totk_vk_compute_fast_override;
extern _Atomic int g_totk_w45_fault;
static inline void totk_vk_compute_fast_set_for_test(int v) {
    atomic_store_explicit(&g_totk_vk_compute_fast_override, v, memory_order_release);
}
static inline void totk_w45_set_fault(int f) {
    atomic_store_explicit(&g_totk_w45_fault, f, memory_order_release);
}
static inline int totk_w45_fault_mode(void) {
    int f = atomic_load_explicit(&g_totk_w45_fault, memory_order_relaxed);
    if (f >= 0) return f;
    const char *e = getenv("TOTK_W45_FAULT");
    int v = (e && *e) ? atoi(e) : 0;
    atomic_store_explicit(&g_totk_w45_fault, v, memory_order_relaxed);
    return v;
}
#else
static inline int totk_w45_fault_mode(void) { return 0; }
#endif

static inline int totk_vk_compute_fast_on(void) {
#ifdef TOTK_W45_TEST_HOOKS
    int ov = atomic_load_explicit(&g_totk_vk_compute_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
#endif
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("TOTK_VK_COMPUTE_FAST");
        v = !(e && strcmp(e, "0") == 0);
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
}

static inline uint64_t totk_cp_mix64(uint64_t x) {
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

static inline uint64_t totk_cp_key_hash80(const char key[80]) {
    uint64_t w[10];
    memcpy(w, key, 80u);
    uint64_t h = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < 10u; i++) {
        h ^= totk_cp_mix64(w[i] + (uint64_t)i * UINT64_C(0x9e3779b97f4a7c15));
        h *= UINT64_C(1099511628211);
    }
    return h ? h : 1ULL;
}

enum { TOTK_CP_HIST_CAP = 4096, TOTK_CP_HIST_MAP = 8192 };
typedef struct {
    uint64_t hash[TOTK_CP_HIST_MAP];
    int16_t slot[TOTK_CP_HIST_MAP];
    uint32_t head;
    uint32_t count;
    char (*keys)[80];
} totk_cp_hist_t;

static inline void totk_cp_hist_reset(totk_cp_hist_t *h, char (*keys)[80]) {
    h->keys = keys;
    h->head = 0u;
    h->count = 0u;
    memset(h->hash, 0, sizeof h->hash);
    for (size_t i = 0; i < TOTK_CP_HIST_MAP; i++) h->slot[i] = -1;
}

static inline void totk_cp_hist_put_slot(totk_cp_hist_t *h, uint64_t kh, int16_t ring_idx) {
    unsigned base = (unsigned)(kh & (TOTK_CP_HIST_MAP - 1));
    for (unsigned step = 0; step < TOTK_CP_HIST_MAP; step++) {
        unsigned p = (base + step) & (TOTK_CP_HIST_MAP - 1);
        if (h->slot[p] < 0) { h->hash[p] = kh; h->slot[p] = ring_idx; return; }
    }
}

static inline void totk_cp_hist_del_slot(totk_cp_hist_t *h, uint64_t kh, int16_t ring_idx) {
    const unsigned mask = TOTK_CP_HIST_MAP - 1;
    unsigned i = (unsigned)(kh & mask);
    for (; h->slot[i] >= 0; i = (i + 1u) & mask) {
        if (h->hash[i] == kh && h->slot[i] == ring_idx) break;
    }
    if (h->slot[i] < 0) return;
    for (unsigned j = i;;) {
        j = (j + 1u) & mask;
        if (h->slot[j] < 0) break;
        unsigned home = (unsigned)(h->hash[j] & mask);
        if (i <= j ? (i < home && home <= j) : (i < home || home <= j)) continue;
        h->hash[i] = h->hash[j];
        h->slot[i] = h->slot[j];
        i = j;
    }
    h->hash[i] = 0ULL;
    h->slot[i] = -1;
}

static inline int totk_cp_hist_find(const totk_cp_hist_t *h, const char key[80], uint64_t kh) {
    if (!h->keys) return -1;
    if (totk_w45_fault_mode() == 1 && h->count > 0u) return -1;
    unsigned base = (unsigned)(kh & (TOTK_CP_HIST_MAP - 1));
    for (unsigned step = 0; step < TOTK_CP_HIST_MAP; step++) {
        unsigned p = (base + step) & (TOTK_CP_HIST_MAP - 1);
        int16_t s = h->slot[p];
        if (s < 0) break;
        if (h->hash[p] == kh && memcmp(h->keys[s], key, 80u) == 0) return (int)s;
    }
    return -1;
}

static inline void totk_cp_hist_load_bulk(totk_cp_hist_t *h, uint32_t n) {
    totk_cp_hist_reset(h, h->keys);
    uint32_t cap = n > TOTK_CP_HIST_CAP ? TOTK_CP_HIST_CAP : n;
    h->count = cap;
    for (uint32_t i = 0; i < cap; i++) {
        totk_cp_hist_put_slot(h, totk_cp_key_hash80(h->keys[i]), (int16_t)i);
    }
}

static inline int totk_cp_hist_learn(totk_cp_hist_t *h, const char key[80]) {
    if (!h->keys) return 0;
    uint64_t kh = totk_cp_key_hash80(key);
    if (totk_cp_hist_find(h, key, kh) >= 0) return 0;
    uint32_t pos = 0u;
    if (h->count == TOTK_CP_HIST_CAP) {
        pos = (totk_w45_fault_mode() == 2) ? ((h->head + h->count - 1u) & (TOTK_CP_HIST_CAP - 1u)) : h->head;
        totk_cp_hist_del_slot(h, totk_cp_key_hash80(h->keys[pos]), (int16_t)pos);
        memcpy(h->keys[pos], key, 80u);
        totk_cp_hist_put_slot(h, kh, (int16_t)pos);
        if (totk_w45_fault_mode() != 2) h->head = (h->head + 1u) & (TOTK_CP_HIST_CAP - 1u);
    } else {
        pos = (h->head + h->count) & (TOTK_CP_HIST_CAP - 1u);
        memcpy(h->keys[pos], key, 80u);
        totk_cp_hist_put_slot(h, kh, (int16_t)pos);
        h->count++;
    }
    return 1;
}

static inline void totk_cp_hist_snapshot(const totk_cp_hist_t *h, char (*dst)[80]) {
    if (!h->keys || !dst || h->count == 0u) return;
    uint32_t first = TOTK_CP_HIST_CAP - h->head;
    if (first > h->count) first = h->count;
    memcpy(dst, h->keys + h->head, (size_t)first * 80u);
    if (first < h->count) {
        memcpy(dst + first, h->keys, (size_t)(h->count - first) * 80u);
    }
}

typedef struct totk_cp_q_node {
    struct totk_cp_q_node *next;
    struct totk_cp_q_node *prev;
} totk_cp_q_node_t;

typedef struct {
    totk_cp_q_node_t *head;
    totk_cp_q_node_t *tail;
} totk_cp_queue_t;

static inline void totk_cp_q_push(totk_cp_queue_t *q, totk_cp_q_node_t *n) {
    n->next = NULL;
    n->prev = q->tail;
    if (q->tail) q->tail->next = n; else q->head = n;
    q->tail = n;
}

static inline totk_cp_q_node_t *totk_cp_q_pop(totk_cp_queue_t *q) {
    totk_cp_q_node_t *n = q->head;
    if (!n) return NULL;
    q->head = n->next;
    if (q->head) q->head->prev = NULL; else q->tail = NULL;
    n->next = NULL;
    n->prev = NULL;
    return n;
}

static inline void totk_cp_q_promote(totk_cp_queue_t *q, totk_cp_q_node_t *n) {
    if (totk_w45_fault_mode() == 3) return;
    if (!q->head || q->head == n || !n->prev) return;
    n->prev->next = n->next;
    if (n->next) n->next->prev = n->prev; else q->tail = n->prev;
    n->prev = NULL;
    n->next = q->head;
    q->head->prev = n;
    q->head = n;
}

static inline void totk_cp_wake_enqueue(pthread_cond_t *cv, const _Atomic uint32_t *waiters,
                                        const _Atomic uint32_t *sleeping, int fast) {
    if (!fast) { pthread_cond_broadcast(cv); return; }
    if (atomic_load_explicit(waiters, memory_order_relaxed) != 0u) pthread_cond_broadcast(cv);
    else if (atomic_load_explicit(sleeping, memory_order_relaxed) != 0u) pthread_cond_signal(cv);
}

static inline void totk_cp_wake_built(pthread_cond_t *cv, const _Atomic uint32_t *waiters, int fast) {
    if (totk_w45_fault_mode() == 4) return;
    if (!fast || atomic_load_explicit(waiters, memory_order_relaxed) != 0u) pthread_cond_broadcast(cv);
}

enum { TOTK_CP_PREP_CAP = 256 };
typedef struct {
    _Atomic uint32_t seq;
    _Atomic uint64_t k1[TOTK_CP_PREP_CAP];
    _Atomic uint64_t k2[TOTK_CP_PREP_CAP];
} totk_cp_prep_memo_t;

static inline void totk_cp_prep_memo_reset(totk_cp_prep_memo_t *m) {
    atomic_store_explicit(&m->seq, 0u, memory_order_release);
    for (size_t i = 0; i < TOTK_CP_PREP_CAP; i++) {
        atomic_store_explicit(&m->k1[i], 0ULL, memory_order_relaxed);
        atomic_store_explicit(&m->k2[i], 0ULL, memory_order_relaxed);
    }
}

static inline void totk_cp_key_fp(const char key[80], uint64_t *h1, uint64_t *h2) {
    uint64_t a = totk_cp_key_hash80(key);
    uint64_t b = totk_cp_mix64(a ^ UINT64_C(0xa0761d6478bd642f));
    *h1 = a;
    *h2 = b ? b : 1ULL;
}

static inline int totk_cp_prep_memo_seen(const totk_cp_prep_memo_t *m, const char key[80]) {
    uint64_t h1 = 0, h2 = 0;
    totk_cp_key_fp(key, &h1, &h2);
    unsigned b = (unsigned)((h1 & 127u) << 1);
    for (int tries = 0; tries < 2; tries++) {
        uint32_t s1 = atomic_load_explicit(&m->seq, memory_order_acquire);
        if (s1 & 1u) continue;
        uint64_t a0 = atomic_load_explicit(&m->k1[b], memory_order_acquire);
        uint64_t b0 = atomic_load_explicit(&m->k2[b], memory_order_acquire);
        uint64_t a1 = atomic_load_explicit(&m->k1[b + 1u], memory_order_acquire);
        uint64_t b1 = atomic_load_explicit(&m->k2[b + 1u], memory_order_acquire);
        uint32_t s2 = atomic_load_explicit(&m->seq, memory_order_acquire);
        if (s1 == s2) return (a0 == h1 && b0 == h2) || (a1 == h1 && b1 == h2);
    }
    return 0;
}

static inline void totk_cp_prep_memo_put_locked(totk_cp_prep_memo_t *m, const char key[80]) {
    uint64_t h1 = 0, h2 = 0;
    totk_cp_key_fp(key, &h1, &h2);
    unsigned b = (unsigned)((h1 & 127u) << 1);
    uint64_t a0 = atomic_load_explicit(&m->k1[b], memory_order_relaxed);
    uint64_t b0 = atomic_load_explicit(&m->k2[b], memory_order_relaxed);
    uint64_t a1 = atomic_load_explicit(&m->k1[b + 1u], memory_order_relaxed);
    uint64_t b1 = atomic_load_explicit(&m->k2[b + 1u], memory_order_relaxed);
    if ((a0 == h1 && b0 == h2) || (a1 == h1 && b1 == h2)) return;
    atomic_fetch_add_explicit(&m->seq, 1u, memory_order_acq_rel);
    atomic_store_explicit(&m->k1[b + 1u], a0, memory_order_relaxed);
    atomic_store_explicit(&m->k2[b + 1u], b0, memory_order_relaxed);
    atomic_store_explicit(&m->k1[b], h1, memory_order_release);
    atomic_store_explicit(&m->k2[b], h2, memory_order_release);
    atomic_fetch_add_explicit(&m->seq, 1u, memory_order_acq_rel);
}

enum { TOTK_XLATE_BAD_CAP = 512, TOTK_XLATE_BAD_MAX = 256 };
typedef struct { _Atomic uint64_t hash; _Atomic uint64_t w[10]; } totk_xlate_bad_slot_t;
typedef struct {
    _Atomic uint32_t seq;
    _Atomic uint32_t count;
    _Atomic uint32_t overflow;
    totk_xlate_bad_slot_t slots[TOTK_XLATE_BAD_CAP];
} totk_xlate_bad_fast_t;

static inline uint64_t totk_xlate_pack80(const char *s, uint64_t w[10]) {
    memset(w, 0, 80u);
    if (!s) return 1ULL;
    size_t n = strnlen(s, 79u);
    if (n) memcpy(w, s, n);
    return totk_cp_key_hash80((const char *)w);
}

static inline void totk_xlate_bad_add_locked(totk_xlate_bad_fast_t *t, const char *variant) {
    if (!variant || atomic_load_explicit(&t->overflow, memory_order_relaxed)) return;
    if (strnlen(variant, 80u) >= 80u || atomic_load_explicit(&t->count, memory_order_relaxed) >= TOTK_XLATE_BAD_MAX) {
        atomic_store_explicit(&t->overflow, 1u, memory_order_release);
        return;
    }
    uint64_t w[10], h = totk_xlate_pack80(variant, w);
    unsigned base = (unsigned)(h & (TOTK_XLATE_BAD_CAP - 1));
    for (unsigned step = 0; step < TOTK_XLATE_BAD_CAP; step++) {
        unsigned p = (base + step) & (TOTK_XLATE_BAD_CAP - 1);
        uint64_t sh = atomic_load_explicit(&t->slots[p].hash, memory_order_relaxed);
        if (sh == 0ULL) {
            atomic_fetch_add_explicit(&t->seq, 1u, memory_order_acq_rel);
            for (size_t i = 0; i < 10u; i++) atomic_store_explicit(&t->slots[p].w[i], w[i], memory_order_relaxed);
            atomic_store_explicit(&t->slots[p].hash, h, memory_order_release);
            atomic_fetch_add_explicit(&t->count, 1u, memory_order_release);
            atomic_fetch_add_explicit(&t->seq, 1u, memory_order_acq_rel);
            return;
        }
        if (sh == h) {
            int eq = 1;
            for (size_t i = 0; i < 10u; i++) {
                if (atomic_load_explicit(&t->slots[p].w[i], memory_order_relaxed) != w[i]) { eq = 0; break; }
            }
            if (eq) return;
        }
    }
}

static inline int totk_xlate_bad_probe(const totk_xlate_bad_fast_t *t, uint64_t h, const uint64_t w[10]) {
    unsigned base = (unsigned)(h & (TOTK_XLATE_BAD_CAP - 1));
    for (unsigned step = 0; step < TOTK_XLATE_BAD_CAP; step++) {
        unsigned p = (base + step) & (TOTK_XLATE_BAD_CAP - 1);
        uint64_t sh = atomic_load_explicit(&t->slots[p].hash, memory_order_acquire);
        if (sh == 0ULL) return 0;
        if (sh != h) continue;
        int match = 1;
        for (size_t i = 0; i < 10u; i++) {
            uint64_t wi = atomic_load_explicit(&t->slots[p].w[i], memory_order_acquire);
            if (wi != w[i]) match = 0;
        }
        if (match) return 1;
    }
    return 0;
}

static inline int totk_xlate_bad_check(const totk_xlate_bad_fast_t *t, const char *variant) {
    if (totk_w45_fault_mode() == 5) return 0;
    if (!variant || strnlen(variant, 80u) >= 80u) return -1;
    if (atomic_load_explicit(&t->overflow, memory_order_acquire)) return -1;
    if (atomic_load_explicit(&t->count, memory_order_acquire) == 0u) return 0;
    uint64_t w[10], h = totk_xlate_pack80(variant, w);
    for (int tries = 0; tries < 4; tries++) {
        uint32_t s1 = atomic_load_explicit(&t->seq, memory_order_acquire);
        if (s1 & 1u) continue;
        int hit = totk_xlate_bad_probe(t, h, w);
        uint32_t s2 = atomic_load_explicit(&t->seq, memory_order_acquire);
        if (s1 == s2) return hit;
    }
    return -1;
}
#endif
