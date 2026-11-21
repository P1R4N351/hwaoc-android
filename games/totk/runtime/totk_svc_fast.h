/* Wave 24 (Opt 26): Lock-free thread/IPC/event/QueryMemory/alias SVC fast paths. */
#pragma once
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    TOTK_SVC_MAX_H = 4096,
    TOTK_SVC_EV_R = 2,
    TOTK_SVC_EV_W = 3,
    TOTK_SVC_SEQ_RETRIES = 8,
    TOTK_SVC_CAS_RETRIES = 4,
    TOTK_SVC_QMEM_SLOTS = 16
};

#ifdef TOTK_SVC_FAST_TEST_HOOKS
static _Atomic int g_totk_svc_fast_ovr = -1;
static inline void totk_svc_fast_reset_for_test(int v) {
    atomic_store_explicit(&g_totk_svc_fast_ovr, v, memory_order_release);
}
static inline int totk_svc_w24_fault(void) {
    static _Atomic int cached = -1;
    int f = atomic_load_explicit(&cached, memory_order_acquire);
    if (f < 0) {
        const char *e = getenv("TOTK_KTGL_OPT_FAULT");
        f = e ? atoi(e) : 0;
        atomic_store_explicit(&cached, f, memory_order_release);
    }
    return f;
}
#else
static inline int totk_svc_w24_fault(void) { return 0; }
#endif

static inline int totk_svc_fast_enabled(void) {
#ifdef TOTK_SVC_FAST_TEST_HOOKS
    int o = atomic_load_explicit(&g_totk_svc_fast_ovr, memory_order_acquire);
    if (o >= 0) return o != 0;
#endif
    static _Atomic int cached = -1;
    int v = atomic_load_explicit(&cached, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("TOTK_SVC_FAST");
        v = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&cached, v, memory_order_release);
    }
    return v;
}

typedef struct {
    _Atomic uint32_t seq;
    _Atomic uint32_t handle;
    _Atomic int32_t prio;
    _Atomic int32_t core;
    _Atomic uint64_t mask;
    _Atomic uint64_t tid;
} totk_svc_thr_meta;

typedef struct {
    totk_svc_thr_meta slots[TOTK_SVC_MAX_H];
} totk_svc_thr_table;

static inline void totk_svc_thr_write(
    totk_svc_thr_meta *m, uint32_t h, int32_t prio, int32_t core, uint64_t mask, uint64_t tid) {
    assert(m != NULL);
    uint32_t q = atomic_fetch_add_explicit(&m->seq, 1u, memory_order_acq_rel);
    atomic_store_explicit(&m->handle, h, memory_order_relaxed);
    atomic_store_explicit(&m->prio, prio, memory_order_relaxed);
    atomic_store_explicit(&m->core, core, memory_order_relaxed);
    atomic_store_explicit(&m->mask, mask, memory_order_relaxed);
    atomic_store_explicit(&m->tid, tid, memory_order_relaxed);
    atomic_store_explicit(&m->seq, q + 2u, memory_order_release);
}

static inline int totk_svc_thr_read(
    const totk_svc_thr_meta *m, uint32_t want_h,
    int32_t *prio, int32_t *core, uint64_t *mask, uint64_t *tid) {
    assert(m != NULL);
    for (int i = 0; i < TOTK_SVC_SEQ_RETRIES; i++) {
        uint32_t s1 = atomic_load_explicit(&m->seq, memory_order_acquire);
        if (s1 & 1u) continue;
        uint32_t h = atomic_load_explicit(&m->handle, memory_order_acquire);
        int32_t p = atomic_load_explicit(&m->prio, memory_order_acquire);
        int32_t c = atomic_load_explicit(&m->core, memory_order_acquire);
        uint64_t mk = atomic_load_explicit(&m->mask, memory_order_acquire);
        uint64_t id = atomic_load_explicit(&m->tid, memory_order_acquire);
        uint32_t s2 = atomic_load_explicit(&m->seq, memory_order_acquire);
        if (s1 == s2 && h && h == want_h) {
            if (totk_svc_w24_fault() == 72) p += 7;
            if (prio) *prio = p;
            if (core) *core = c;
            if (mask) *mask = mk;
            if (tid) *tid = id;
            return 1;
        }
        if (s1 == s2) return 0;
    }
    return 0;
}

static inline void totk_svc_thr_pub(
    totk_svc_thr_table *tb, totk_svc_thr_meta *self_m,
    uint32_t h, int32_t prio, int32_t core, uint64_t mask, uint64_t tid) {
    assert(tb != NULL);
    if (self_m) totk_svc_thr_write(self_m, h, prio, core, mask, tid);
    uint32_t slot = h & (TOTK_SVC_MAX_H - 1u);
    if (slot) totk_svc_thr_write(&tb->slots[slot], h, prio, core, mask, tid);
}

static inline void totk_svc_thr_drop(totk_svc_thr_table *tb, uint32_t h) {
    assert(tb != NULL);
    uint32_t slot = h & (TOTK_SVC_MAX_H - 1u);
    if (slot) totk_svc_thr_write(&tb->slots[slot], 0, 0, 0, 0, 0);
}

static inline int totk_svc_thr_lookup(
    const totk_svc_thr_table *tb, uint32_t h,
    int32_t *prio, int32_t *core, uint64_t *mask, uint64_t *tid) {
    assert(tb != NULL);
    uint32_t slot = h & (TOTK_SVC_MAX_H - 1u);
    if (!slot || !h) return 0;
    return totk_svc_thr_read(&tb->slots[slot], h, prio, core, mask, tid);
}

typedef struct {
    _Atomic uint32_t seq;
    _Atomic uint32_t handle;
    _Atomic int32_t type;
    _Atomic uintptr_t obj;
} totk_svc_h_slot;

typedef struct {
    totk_svc_h_slot slots[TOTK_SVC_MAX_H];
} totk_svc_h_table;

static inline void totk_svc_h_bind(
    totk_svc_h_table *tb, uint32_t h, int32_t type, void *obj) {
    assert(tb != NULL && obj != NULL);
    uint32_t slot = h & (TOTK_SVC_MAX_H - 1u);
    if (!slot) return;
    totk_svc_h_slot *e = &tb->slots[slot];
    uint32_t q = atomic_fetch_add_explicit(&e->seq, 1u, memory_order_acq_rel);
    atomic_store_explicit(&e->handle, h, memory_order_relaxed);
    atomic_store_explicit(&e->type, type, memory_order_relaxed);
    atomic_store_explicit(&e->obj, (uintptr_t)obj, memory_order_relaxed);
    atomic_store_explicit(&e->seq, q + 2u, memory_order_release);
}

static inline void totk_svc_h_unbind(totk_svc_h_table *tb, uint32_t h) {
    assert(tb != NULL);
    uint32_t slot = h & (TOTK_SVC_MAX_H - 1u);
    if (!slot) return;
    totk_svc_h_slot *e = &tb->slots[slot];
    uint32_t q = atomic_fetch_add_explicit(&e->seq, 1u, memory_order_acq_rel);
    atomic_store_explicit(&e->handle, 0u, memory_order_relaxed);
    atomic_store_explicit(&e->type, 0, memory_order_relaxed);
    atomic_store_explicit(&e->obj, 0u, memory_order_relaxed);
    atomic_store_explicit(&e->seq, q + 2u, memory_order_release);
}

static inline void *totk_svc_h_lookup(
    const totk_svc_h_table *tb, uint32_t h, int32_t want_type) {
    assert(tb != NULL);
    uint32_t slot = h & (TOTK_SVC_MAX_H - 1u);
    if (!slot || !h || h == 0xFFFF8000u) return NULL;
    const totk_svc_h_slot *e = &tb->slots[slot];
    int f = totk_svc_w24_fault();
    for (int i = 0; i < TOTK_SVC_SEQ_RETRIES; i++) {
        uint32_t s1 = atomic_load_explicit(&e->seq, memory_order_acquire);
        if (s1 & 1u) continue;
        uint32_t hh = atomic_load_explicit(&e->handle, memory_order_acquire);
        int32_t tp = atomic_load_explicit(&e->type, memory_order_acquire);
        uintptr_t p = atomic_load_explicit(&e->obj, memory_order_acquire);
        uint32_t s2 = atomic_load_explicit(&e->seq, memory_order_acquire);
        if (s1 == s2 && (hh == h || f == 73) && tp == want_type && p) return (void *)p;
        if (s1 == s2) return NULL;
    }
    return NULL;
}

typedef struct {
    _Atomic uint64_t state_gen;
    _Atomic int signaled;
    int refs;
    int in_use;
} totk_svc_ev;

typedef struct {
    _Atomic uint32_t seq;
    _Atomic uint32_t handle;
    _Atomic int32_t type;
    _Atomic uint32_t pool_idx;
    _Atomic uint64_t ev_gen;
} totk_svc_ev_h;

typedef struct {
    totk_svc_ev pool[TOTK_SVC_MAX_H];
    uint32_t free_stack[TOTK_SVC_MAX_H];
    uint32_t free_top;
    uint32_t init_cursor;
    totk_svc_ev_h hslots[TOTK_SVC_MAX_H];
    _Atomic uint32_t wsync_count;
} totk_svc_ev_sys;

static inline totk_svc_ev *totk_svc_ev_alloc(totk_svc_ev_sys *s, int refs) {
    assert(s != NULL && refs > 0);
    uint32_t idx = 0;
    if (s->free_top > 0) idx = s->free_stack[--s->free_top];
    else {
        if (s->init_cursor == 0) s->init_cursor = 1;
        if (s->init_cursor < TOTK_SVC_MAX_H) idx = s->init_cursor++;
    }
    if (!idx || idx >= TOTK_SVC_MAX_H) return NULL;
    totk_svc_ev *e = &s->pool[idx];
    uint64_t g = (atomic_load_explicit(&e->state_gen, memory_order_relaxed) >> 1) + 1ULL;
    atomic_store_explicit(&e->state_gen, g << 1, memory_order_seq_cst);
    atomic_store_explicit(&e->signaled, 0, memory_order_relaxed);
    e->refs = refs;
    e->in_use = 1;
    return e;
}

static inline void totk_svc_ev_free(totk_svc_ev_sys *s, totk_svc_ev *e) {
    assert(s != NULL && e != NULL && e->in_use);
    uintptr_t idx = (uintptr_t)(e - s->pool);
    assert(idx > 0 && idx < TOTK_SVC_MAX_H);
    uint64_t g = (atomic_load_explicit(&e->state_gen, memory_order_relaxed) >> 1) + 1ULL;
    atomic_store_explicit(&e->state_gen, g << 1, memory_order_seq_cst);
    atomic_store_explicit(&e->signaled, 0, memory_order_relaxed);
    e->in_use = 0;
    if (s->free_top < TOTK_SVC_MAX_H) s->free_stack[s->free_top++] = (uint32_t)idx;
}

static inline int totk_svc_ev_is_sig(const totk_svc_ev *e) {
    assert(e != NULL);
    return (int)(atomic_load_explicit(&e->state_gen, memory_order_seq_cst) & 1ULL);
}

static inline void totk_svc_ev_set_sig_locked(totk_svc_ev *e, int sig) {
    assert(e != NULL);
    uint64_t cur = atomic_load_explicit(&e->state_gen, memory_order_relaxed);
    uint64_t next = (cur & ~1ULL) | (uint64_t)(sig ? 1ULL : 0ULL);
    atomic_store_explicit(&e->state_gen, next, memory_order_seq_cst);
    atomic_store_explicit(&e->signaled, sig ? 1 : 0, memory_order_relaxed);
}

static inline void totk_svc_ev_h_bind(
    totk_svc_ev_sys *s, uint32_t h, int32_t type, const totk_svc_ev *e) {
    assert(s != NULL && e != NULL);
    uint32_t slot = h & (TOTK_SVC_MAX_H - 1u);
    if (!slot) return;
    totk_svc_ev_h *he = &s->hslots[slot];
    uint32_t idx = (uint32_t)(e - s->pool);
    uint64_t gen = atomic_load_explicit(&e->state_gen, memory_order_relaxed) >> 1;
    uint32_t q = atomic_fetch_add_explicit(&he->seq, 1u, memory_order_acq_rel);
    atomic_store_explicit(&he->handle, h, memory_order_relaxed);
    atomic_store_explicit(&he->type, type, memory_order_relaxed);
    atomic_store_explicit(&he->pool_idx, idx, memory_order_relaxed);
    atomic_store_explicit(&he->ev_gen, gen, memory_order_relaxed);
    atomic_store_explicit(&he->seq, q + 2u, memory_order_release);
}

static inline void totk_svc_ev_h_unbind(totk_svc_ev_sys *s, uint32_t h) {
    assert(s != NULL);
    uint32_t slot = h & (TOTK_SVC_MAX_H - 1u);
    if (!slot) return;
    totk_svc_ev_h *he = &s->hslots[slot];
    uint32_t q = atomic_fetch_add_explicit(&he->seq, 1u, memory_order_acq_rel);
    atomic_store_explicit(&he->handle, 0u, memory_order_relaxed);
    atomic_store_explicit(&he->type, 0, memory_order_relaxed);
    atomic_store_explicit(&he->pool_idx, 0u, memory_order_relaxed);
    atomic_store_explicit(&he->ev_gen, 0ULL, memory_order_relaxed);
    atomic_store_explicit(&he->seq, q + 2u, memory_order_release);
}

static inline int totk_svc_ev_h_lookup(
    const totk_svc_ev_sys *s, uint32_t h, int32_t *out_type,
    uint32_t *out_idx, uint64_t *out_gen) {
    assert(s != NULL && out_type && out_idx && out_gen);
    uint32_t slot = h & (TOTK_SVC_MAX_H - 1u);
    if (!slot || !h) return 0;
    const totk_svc_ev_h *he = &s->hslots[slot];
    for (int i = 0; i < TOTK_SVC_SEQ_RETRIES; i++) {
        uint32_t s1 = atomic_load_explicit(&he->seq, memory_order_acquire);
        if (s1 & 1u) continue;
        uint32_t hh = atomic_load_explicit(&he->handle, memory_order_acquire);
        int32_t tp = atomic_load_explicit(&he->type, memory_order_acquire);
        uint32_t idx = atomic_load_explicit(&he->pool_idx, memory_order_acquire);
        uint64_t gen = atomic_load_explicit(&he->ev_gen, memory_order_acquire);
        uint32_t s2 = atomic_load_explicit(&he->seq, memory_order_acquire);
        if (s1 == s2 && hh == h && idx > 0 && idx < TOTK_SVC_MAX_H && gen) {
            *out_type = tp; *out_idx = idx; *out_gen = gen;
            return 1;
        }
        if (s1 == s2) return 0;
    }
    return 0;
}

static inline int totk_svc_ev_clear(totk_svc_ev_sys *s, uint32_t h, int64_t *out_res) {
    assert(s != NULL && out_res != NULL);
    int32_t tp = 0; uint32_t idx = 0; uint64_t gen = 0;
    if (!totk_svc_ev_h_lookup(s, h, &tp, &idx, &gen)) return 0;
    if (tp != TOTK_SVC_EV_R && tp != TOTK_SVC_EV_W) return 0;
    if (totk_svc_w24_fault() == 74) { *out_res = 0; return 1; }
    totk_svc_ev *e = &s->pool[idx];
    uint64_t cur = atomic_load_explicit(&e->state_gen, memory_order_acquire);
    for (int i = 0; i < TOTK_SVC_CAS_RETRIES; i++) {
        if ((cur >> 1) != gen) return 0;
        if (atomic_compare_exchange_weak_explicit(
                &e->state_gen, &cur, cur & ~1ULL, memory_order_seq_cst, memory_order_acquire)) {
            *out_res = 0;
            return 1;
        }
    }
    return 0;
}

static inline int totk_svc_ev_reset(
    totk_svc_ev_sys *s, uint32_t h, uint32_t r_bad_state, int64_t *out_res) {
    assert(s != NULL && out_res != NULL);
    int32_t tp = 0; uint32_t idx = 0; uint64_t gen = 0;
    if (!totk_svc_ev_h_lookup(s, h, &tp, &idx, &gen) || tp != TOTK_SVC_EV_R) return 0;
    totk_svc_ev *e = &s->pool[idx];
    uint64_t cur = atomic_load_explicit(&e->state_gen, memory_order_acquire);
    for (int i = 0; i < TOTK_SVC_CAS_RETRIES; i++) {
        if ((cur >> 1) != gen) return 0;
        if (!(cur & 1ULL)) { *out_res = (int64_t)r_bad_state; return 1; }
        if (atomic_compare_exchange_weak_explicit(
                &e->state_gen, &cur, cur & ~1ULL, memory_order_seq_cst, memory_order_acquire)) {
            *out_res = 0;
            return 1;
        }
    }
    return 0;
}

static inline int totk_svc_ev_signal_try(
    totk_svc_ev_sys *s, uint32_t h, int *already_set, int64_t *out_res) {
    assert(s != NULL && already_set != NULL && out_res != NULL);
    *already_set = 0;
    int32_t tp = 0; uint32_t idx = 0; uint64_t gen = 0;
    if (!totk_svc_ev_h_lookup(s, h, &tp, &idx, &gen) || tp != TOTK_SVC_EV_W) return 0;
    totk_svc_ev *e = &s->pool[idx];
    uint64_t cur = atomic_load_explicit(&e->state_gen, memory_order_acquire);
    int ok = 0;
    for (int i = 0; i < TOTK_SVC_CAS_RETRIES; i++) {
        if ((cur >> 1) != gen) return 0;
        if (atomic_compare_exchange_weak_explicit(
                &e->state_gen, &cur, cur | 1ULL, memory_order_seq_cst, memory_order_acquire)) {
            ok = 1; break;
        }
    }
    if (!ok) return 0;
    uint32_t wc = atomic_load_explicit(&s->wsync_count, memory_order_seq_cst);
    if (wc == 0 || totk_svc_w24_fault() == 75) { *out_res = 0; return 1; }
    *already_set = 1;
    return 0;
}

typedef struct {
    uint64_t base, size;
    uint32_t state, attr, perm, ipc, device, pad;
} totk_svc_qmem_info;

_Static_assert(sizeof(totk_svc_qmem_info) == 40, "MemoryInfo ABI size");

typedef struct {
    uint32_t seq;
    uint32_t valid;
    totk_svc_qmem_info info;
} totk_qmem_slot;

typedef struct {
    uint32_t next;
    totk_qmem_slot slots[TOTK_SVC_QMEM_SLOTS];
} totk_qmem_tls;

static inline void totk_svc_seq_begin(_Atomic uint32_t *seq) {
    assert(seq != NULL);
    atomic_fetch_add_explicit(seq, 1u, memory_order_acq_rel);
}

static inline void totk_svc_seq_end(_Atomic uint32_t *seq) {
    assert(seq != NULL);
    atomic_fetch_add_explicit(seq, 1u, memory_order_release);
}

static inline int totk_qmem_lookup(
    const totk_qmem_tls *tls, uint64_t addr, uint32_t seq, totk_svc_qmem_info *out) {
    assert(tls != NULL && out != NULL);
    if (!seq || (seq & 1u)) return 0;
    for (int i = 0; i < TOTK_SVC_QMEM_SLOTS; i++) {
        const totk_qmem_slot *e = &tls->slots[i];
        if (!e->valid || e->seq != seq) continue;
        if (addr >= e->info.base && addr - e->info.base < e->info.size) {
            *out = e->info;
            return 1;
        }
    }
    return 0;
}

static inline void totk_qmem_store(
    totk_qmem_tls *tls, uint32_t seq, const totk_svc_qmem_info *info) {
    assert(tls != NULL && info != NULL);
    if (!seq || (seq & 1u) || !info->size) return;
    for (int i = 0; i < TOTK_SVC_QMEM_SLOTS; i++) {
        totk_qmem_slot *e = &tls->slots[i];
        if (e->valid && e->info.base == info->base && e->info.size == info->size) {
            e->seq = seq; e->info = *info; return;
        }
    }
    uint32_t idx = tls->next % TOTK_SVC_QMEM_SLOTS;
    tls->next = (idx + 1u) % TOTK_SVC_QMEM_SLOTS;
    tls->slots[idx].seq = seq;
    tls->slots[idx].valid = 1;
    tls->slots[idx].info = *info;
}

static inline void totk_alias_scan_run(
    const unsigned char *pages, size_t total_pages, unsigned p,
    unsigned *out_low, unsigned *out_high) {
    assert(pages != NULL && out_low != NULL && out_high != NULL && p < total_pages);
    unsigned allocated = pages[p];
    unsigned low = p, high = p + 1u;
    uint64_t target = (uint64_t)allocated * 0x0101010101010101ULL;
    while (low > 0 && ((uintptr_t)(pages + low) & 7u)) {
        if (pages[low - 1u] != allocated) break;
        low--;
    }
    if (!((uintptr_t)(pages + low) & 7u)) {
        while (low >= 8u) {
            uint64_t w;
            memcpy(&w, pages + low - 8u, sizeof w);
            if (w != target) break;
            low -= 8u;
        }
    }
    while (low > 0 && pages[low - 1u] == allocated) low--;
    while (high < total_pages && ((uintptr_t)(pages + high) & 7u)) {
        if (pages[high] != allocated) break;
        high++;
    }
    if (!((uintptr_t)(pages + high) & 7u)) {
        while ((size_t)high + 8u <= total_pages) {
            uint64_t w;
            memcpy(&w, pages + high, sizeof w);
            if (w != target) break;
            high += 8u;
        }
    }
    while (high < total_pages && pages[high] == allocated) high++;
    if (totk_svc_w24_fault() == 76 && high > p + 1u) high--;
    *out_low = low;
    *out_high = high;
}
