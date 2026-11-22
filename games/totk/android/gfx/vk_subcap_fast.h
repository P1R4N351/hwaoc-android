#ifndef TOTK_VK_SUBCAP_FAST_H
#define TOTK_VK_SUBCAP_FAST_H
/* Wave 43 (Opt 45): Lock-free cpend seqlock+buckets, epoch UBO dedup, TLS radix sort,
   elementary-segment span window, and single-slab atomic ucap pool (TOTK_VK_SUBCAP_FAST=1). */
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { TOTK_CPEND_SLOTS = 4096u, TOTK_CPEND_BUCKETS = 512u, TOTK_UBO_SEEN_N = 1024u, TOTK_UCAP_POOL_N = 4u };

typedef struct { uint64_t lo, hi; } totk_rng_pair;

typedef struct {
    _Atomic uint64_t addr;
    _Atomic uint64_t val;
    _Atomic uint64_t prev1;
    _Atomic int known;
} totk_cpend_slot_t;

typedef struct {
    _Atomic uint64_t seq;
    _Atomic uint64_t head;
    _Atomic uint64_t tail;
    _Atomic int heap_overflow;
    _Atomic uint64_t bucket_latest[TOTK_CPEND_BUCKETS];
    totk_cpend_slot_t slots[TOTK_CPEND_SLOTS];
} totk_cpend_fast_t;

typedef struct {
    uint32_t gen;
    uint32_t tag[TOTK_UBO_SEEN_N];
    uint64_t lo[TOTK_UBO_SEEN_N];
    uint64_t hi[TOTK_UBO_SEEN_N];
} totk_ubo_seen_tls;

typedef struct {
    uintptr_t seg_lo, seg_hi, best_end;
    int seg_idx, best_valid;
} totk_span_win_t;

typedef struct {
    _Atomic(void *) slots[TOTK_UCAP_POOL_N];
    _Atomic uint32_t evict_cursor;
} totk_ucap_pool_t;

#ifdef TOTK_W43_TEST_HOOKS
extern _Atomic int g_totk_subcap_fast_override;
extern _Atomic int g_totk_w43_fault;
#endif

static inline int totk_vk_subcap_fast_on(void) {
#ifdef TOTK_W43_TEST_HOOKS
    int ov = atomic_load_explicit(&g_totk_subcap_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
    const char *e = getenv("TOTK_VK_SUBCAP_FAST");
    return !(e && strcmp(e, "0") == 0);
#else
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("TOTK_VK_SUBCAP_FAST");
        v = !(e && strcmp(e, "0") == 0);
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
#endif
}

static inline int totk_w43_fault_mode(void) {
#ifdef TOTK_W43_TEST_HOOKS
    int f = atomic_load_explicit(&g_totk_w43_fault, memory_order_relaxed);
    if (f >= 0) return f;
    const char *e = getenv("TOTK_W43_FAULT");
    int v = (e && *e) ? atoi(e) : 0;
    if (!v) {
        const char *k = getenv("TOTK_KTGL_OPT_FAULT");
        if (k && *k) { int kv = atoi(k); if (kv >= 161 && kv <= 165) v = kv - 160; }
    }
    atomic_store_explicit(&g_totk_w43_fault, v, memory_order_relaxed);
    return v;
#else
    return 0;
#endif
}

#ifdef TOTK_W43_TEST_HOOKS
static inline void totk_vk_subcap_fast_set_for_test(int v) {
    atomic_store_explicit(&g_totk_subcap_fast_override, v, memory_order_release);
}
static inline void totk_w43_set_fault(int f) {
    atomic_store_explicit(&g_totk_w43_fault, f, memory_order_release);
}
#endif

static inline uint32_t totk_cpend_bucket(uint64_t addr) {
    uint64_t x = ((addr >> 4) ^ (addr >> 15)) * UINT64_C(0x9e3779b97f4a7c15);
    return (uint32_t)(x >> 55) & (TOTK_CPEND_BUCKETS - 1u);
}

static inline void totk_cpend_fast_push(totk_cpend_fast_t *c, uint64_t addr, uint64_t val, int known) {
    uint64_t h = atomic_load_explicit(&c->head, memory_order_relaxed);
    uint64_t t = atomic_load_explicit(&c->tail, memory_order_relaxed);
    if (atomic_load_explicit(&c->heap_overflow, memory_order_relaxed) || (h - t) >= TOTK_CPEND_SLOTS) {
        atomic_store_explicit(&c->heap_overflow, 1, memory_order_release);
        return;
    }
    uint64_t seq = atomic_load_explicit(&c->seq, memory_order_relaxed);
    atomic_store_explicit(&c->seq, seq + 1u, memory_order_release);
    uint32_t s = (uint32_t)(h & (TOTK_CPEND_SLOTS - 1u));
    uint32_t b = totk_cpend_bucket(addr);
    uint64_t old_pos1 = atomic_load_explicit(&c->bucket_latest[b], memory_order_relaxed);
    totk_cpend_slot_t *e = &c->slots[s];
    atomic_store_explicit(&e->addr, addr, memory_order_release);
    atomic_store_explicit(&e->val, val, memory_order_release);
    atomic_store_explicit(&e->known, known, memory_order_release);
    atomic_store_explicit(&e->prev1, (old_pos1 > t) ? old_pos1 : 0u, memory_order_release);
    atomic_store_explicit(&c->bucket_latest[b], h + 1u, memory_order_release);
    atomic_store_explicit(&c->head, h + 1u, memory_order_release);
    atomic_store_explicit(&c->seq, seq + 2u, memory_order_release);
}

static inline void totk_cpend_fast_pop(totk_cpend_fast_t *c, int now_empty) {
    uint64_t seq = atomic_load_explicit(&c->seq, memory_order_relaxed);
    atomic_store_explicit(&c->seq, seq + 1u, memory_order_release);
    uint64_t t = atomic_load_explicit(&c->tail, memory_order_relaxed);
    uint64_t h = atomic_load_explicit(&c->head, memory_order_relaxed);
    if (atomic_load_explicit(&c->heap_overflow, memory_order_relaxed)) {
        if (now_empty) {
            for (uint32_t i = 0u; i < TOTK_CPEND_BUCKETS; i++) {
                atomic_store_explicit(&c->bucket_latest[i], 0u, memory_order_release);
            }
            atomic_store_explicit(&c->tail, h, memory_order_release);
            atomic_store_explicit(&c->heap_overflow, 0, memory_order_release);
        }
    } else if (t != h) {
        atomic_store_explicit(&c->tail, t + 1u, memory_order_release);
    }
    atomic_store_explicit(&c->seq, seq + 2u, memory_order_release);
}

static inline int totk_cpend_scan_bucket(const totk_cpend_fast_t *c, uint64_t addr, uint64_t t,
                                         int *found, uint64_t *val, int *known) {
    *found = 0; *val = 0u; *known = 1;
    uint32_t b = totk_cpend_bucket(addr);
    uint64_t pos1 = atomic_load_explicit(&c->bucket_latest[b], memory_order_acquire);
    int fault1 = (totk_w43_fault_mode() == 1);
    for (uint32_t step = 0u; pos1 > t && step < TOTK_CPEND_SLOTS; step++) {
        const totk_cpend_slot_t *e = &c->slots[(pos1 - 1u) & (TOTK_CPEND_SLOTS - 1u)];
        uint64_t ea = atomic_load_explicit(&e->addr, memory_order_acquire);
        if (ea == addr) {
            uint64_t ev = atomic_load_explicit(&e->val, memory_order_relaxed);
            int ek = atomic_load_explicit(&e->known, memory_order_acquire);
            *found = 1; *val = ev; *known = (totk_w43_fault_mode() == 2) ? 1 : ek;
            if (!fault1) return 1;
        }
        uint64_t ep = atomic_load_explicit(&e->prev1, memory_order_relaxed);
        if (ep >= pos1) return 0;
        pos1 = ep;
    }
    return pos1 <= t;
}

static inline int totk_cpend_fast_lookup(const totk_cpend_fast_t *c, uint64_t addr,
                                         int *found_out, uint64_t *val_out, int *known_out) {
    for (int attempt = 0; attempt < 3; attempt++) {
        uint64_t s1 = atomic_load_explicit(&c->seq, memory_order_acquire);
        if (s1 & 1u) continue;
        if (atomic_load_explicit(&c->heap_overflow, memory_order_relaxed)) return -1;
        uint64_t h = atomic_load_explicit(&c->head, memory_order_relaxed);
        uint64_t t = atomic_load_explicit(&c->tail, memory_order_acquire);
        int f = 0, k = 1; uint64_t v = 0u;
        int ok = (h == t) ? 1 : totk_cpend_scan_bucket(c, addr, t, &f, &v, &k);
        if (ok && atomic_load_explicit(&c->seq, memory_order_acquire) == s1) {
            *found_out = f; *val_out = v; *known_out = k;
            return 0;
        }
    }
    return -1;
}

static inline int totk_cpend_fast_pair(const totk_cpend_fast_t *c, uint64_t a0, uint64_t a1,
                                       int f[2], uint64_t v[2], int k[2]) {
    for (int attempt = 0; attempt < 3; attempt++) {
        uint64_t s1 = atomic_load_explicit(&c->seq, memory_order_acquire);
        if (s1 & 1u) continue;
        if (atomic_load_explicit(&c->heap_overflow, memory_order_relaxed)) return -1;
        uint64_t h = atomic_load_explicit(&c->head, memory_order_relaxed);
        uint64_t t = atomic_load_explicit(&c->tail, memory_order_acquire);
        int ok0 = 1, ok1 = 1;
        f[0] = f[1] = 0; v[0] = v[1] = 0u; k[0] = k[1] = 1;
        if (h != t) {
            ok0 = totk_cpend_scan_bucket(c, a0, t, &f[0], &v[0], &k[0]);
            ok1 = totk_cpend_scan_bucket(c, a1, t, &f[1], &v[1], &k[1]);
        }
        if (ok0 && ok1 && atomic_load_explicit(&c->seq, memory_order_acquire) == s1) return 0;
    }
    return -1;
}

static inline void totk_ubo_seen_reset(totk_ubo_seen_tls *s) {
    uint32_t g = s->gen + 1u;
    if (g == 0u) { memset(s->tag, 0, sizeof s->tag); g = 1u; }
    s->gen = g;
}

static inline int totk_ubo_seen_hit(totk_ubo_seen_tls *s, uint32_t slot, uint64_t lo, uint64_t hi) {
    uint32_t sl = slot & (TOTK_UBO_SEEN_N - 1u);
    int check_tag = (totk_w43_fault_mode() != 3);
    if ((!check_tag || s->tag[sl] == s->gen) && s->lo[sl] == lo && s->hi[sl] == hi) return 1;
    s->tag[sl] = s->gen; s->lo[sl] = lo; s->hi[sl] = hi;
    return 0;
}

static inline void totk_radix_sort_rng(totk_rng_pair *r, int n, totk_rng_pair *tmp_inline, int inline_cap) {
    if (n < 2) return;
    if (n <= 32) {
        for (int i = 1; i < n; i++) {
            totk_rng_pair x = r[i]; int j = i;
            for (; j > 0 && r[j - 1].lo > x.lo; j--) r[j] = r[j - 1];
            r[j] = x;
        }
        return;
    }
    uint64_t any = 0u, all = UINT64_MAX;
    for (int i = 0; i < n; i++) { any |= r[i].lo; all &= r[i].lo; }
    uint64_t vary = any ^ all;
    totk_rng_pair *heap_tmp = (tmp_inline && n <= inline_cap) ? NULL : (totk_rng_pair *)malloc((size_t)n * sizeof *heap_tmp);
    totk_rng_pair *src = r, *dst = heap_tmp ? heap_tmp : tmp_inline;
    if (!dst) return;
    unsigned count[2048];
    for (unsigned sh = 0u; sh < 64u; sh += 11u) {
        if (!((vary >> sh) & 2047u)) continue;
        memset(count, 0, sizeof count);
        for (int i = 0; i < n; i++) count[(src[i].lo >> sh) & 2047u]++;
        unsigned sum = 0u;
        for (unsigned b = 0u; b < 2048u; b++) { unsigned c = count[b]; count[b] = sum; sum += c; }
        for (int i = 0; i < n; i++) dst[count[(src[i].lo >> sh) & 2047u]++] = src[i];
        totk_rng_pair *sw = src; src = dst; dst = sw;
    }
    if (src != r) memcpy(r, src, (size_t)n * sizeof *r);
    free(heap_tmp);
}

static inline uintptr_t totk_rd_uptr(const void *base, size_t stride, int idx, size_t off) {
    uintptr_t v; memcpy(&v, (const uint8_t *)base + (size_t)idx * stride + off, sizeof v); return v;
}
static inline int totk_rd_int(const void *base, size_t stride, int idx, size_t off) {
    int v; memcpy(&v, (const uint8_t *)base + (size_t)idx * stride + off, sizeof v); return v;
}

static inline void totk_span_win_seek(totk_span_win_t *w, const void *spans, size_t sp_stride, size_t off_end,
                                      const void *segs, size_t sg_stride, size_t off_slo, size_t off_best,
                                      int nseg, uintptr_t a) {
    if (w->seg_idx >= 0 && a >= w->seg_lo && a < w->seg_hi) return;
    int lo = (w->seg_idx >= 0 && a >= w->seg_hi) ? (w->seg_idx + 1) : 0, hi = nseg;
    if (w->seg_idx >= 0 && a >= w->seg_hi) {
        for (int k = 0; k < 4 && lo < hi; k++) {
            if (totk_rd_uptr(segs, sg_stride, lo, off_slo) > a) { hi = lo; break; }
            lo++;
        }
    }
    while (lo < hi) {
        int m = (lo + hi) >> 1;
        if (totk_rd_uptr(segs, sg_stride, m, off_slo) <= a) lo = m + 1;
        else hi = m;
    }
    if (lo <= 0) {
        w->seg_idx = -1; w->seg_lo = 0u; w->seg_hi = nseg > 0 ? totk_rd_uptr(segs, sg_stride, 0, off_slo) : UINTPTR_MAX;
        w->best_valid = 0; w->best_end = 0u;
        return;
    }
    int idx = lo - 1;
    w->seg_idx = idx;
    w->seg_lo = totk_rd_uptr(segs, sg_stride, idx, off_slo);
    w->seg_hi = (lo < nseg) ? totk_rd_uptr(segs, sg_stride, lo, off_slo) : UINTPTR_MAX;
    int b = totk_rd_int(segs, sg_stride, idx, off_best);
    w->best_valid = (b >= 0);
    w->best_end = w->best_valid ? totk_rd_uptr(spans, sp_stride, b, off_end) : 0u;
}

static inline int totk_span_win_contains(const totk_span_win_t *w, uintptr_t b) {
    if (totk_w43_fault_mode() == 4) return w->best_valid;
    return w->best_valid && b <= w->best_end;
}

static inline size_t totk_ucap_slab_need(size_t hdr_sz, int cnt, int copy_bytes, size_t total) {
    size_t h_al = (hdr_sz + 15u) & ~(size_t)15u;
    size_t u64_bytes = (size_t)(cnt + 1) * 2u * sizeof(uint64_t);
    size_t sz_bytes = copy_bytes ? (size_t)(cnt + 1) * sizeof(size_t) : 0u;
    size_t arr_al = (h_al + u64_bytes + sz_bytes + 15u) & ~(size_t)15u;
    return arr_al + (copy_bytes ? (total ? total : 1u) : 0u);
}

static inline void *totk_ucap_pool_acquire(totk_ucap_pool_t *pool, size_t need, size_t *got_cap) {
    for (uint32_t i = 0u; i < TOTK_UCAP_POOL_N; i++) {
        void *p = atomic_exchange_explicit(&pool->slots[i], NULL, memory_order_acquire);
        if (!p) continue;
        size_t cap = *(const size_t *)p;
        if (cap >= need) { *got_cap = cap; return p; }
        void *exp_null = NULL;
        if (!atomic_compare_exchange_strong_explicit(&pool->slots[i], &exp_null, p,
                                                     memory_order_release, memory_order_relaxed)) {
            free(p);
        }
    }
    size_t cap = 4096u;
    while (cap < need && cap <= ((size_t)2u << 20)) cap <<= 1;
    if (cap < need) cap = need;
    void *p = malloc(cap);
    if (!p) { *got_cap = 0u; return NULL; }
    *got_cap = cap;
    return p;
}

static inline void totk_ucap_pool_release(totk_ucap_pool_t *pool, void *ptr, size_t slab_cap) {
    if (!ptr) return;
    if (slab_cap > ((size_t)2u << 20)) { free(ptr); return; }
    *(size_t *)ptr = slab_cap;
    for (uint32_t i = 0u; i < TOTK_UCAP_POOL_N; i++) {
        void *exp_null = NULL;
        if (atomic_compare_exchange_strong_explicit(&pool->slots[i], &exp_null, ptr,
                                                    memory_order_release, memory_order_relaxed)) {
            return;
        }
    }
    uint32_t k = atomic_fetch_add_explicit(&pool->evict_cursor, 1u, memory_order_relaxed) & (TOTK_UCAP_POOL_N - 1u);
    void *old = atomic_exchange_explicit(&pool->slots[k], ptr, memory_order_acq_rel);
    free(old);
}

static inline void totk_ucap_pool_drain(totk_ucap_pool_t *pool) {
    for (uint32_t i = 0u; i < TOTK_UCAP_POOL_N; i++) {
        void *p = atomic_exchange_explicit(&pool->slots[i], NULL, memory_order_acquire);
        free(p);
    }
}

static inline void totk_ucap_slice(void *base, size_t hdr_sz, int cnt, int copy_bytes,
                                   uint64_t **lo_out, uint64_t **hi_out,
                                   size_t **off_out, uint8_t **bytes_out) {
    uint8_t *raw = (uint8_t *)base + ((hdr_sz + 15u) & ~(size_t)15u);
    *lo_out = (uint64_t *)raw;
    *hi_out = (totk_w43_fault_mode() == 5) ? *lo_out : (*lo_out + (size_t)(cnt + 1));
    raw += (size_t)(cnt + 1) * 2u * sizeof(uint64_t);
    if (copy_bytes) {
        *off_out = (size_t *)raw;
        raw += (size_t)(cnt + 1) * sizeof(size_t);
        uintptr_t p = ((uintptr_t)raw + 15u) & ~(uintptr_t)15u;
        *bytes_out = (uint8_t *)p;
    } else {
        *off_out = NULL; *bytes_out = NULL;
    }
}
#endif
