/* Fast O(log N) UB flush/forget/lag-iv index, O(1) gw_seal skip & bind/transfer masks (Opt 57). */
#ifndef VK_UB_GW_FAST_H
#define VK_UB_GW_FAST_H
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TOTK_W55_TEST_HOOKS
extern _Atomic int g_totk_vk_ub_gw_fast_override;
extern _Atomic int g_totk_w55_fault;
static inline void totk_vk_ub_gw_fast_set_for_test(int v) {
    atomic_store_explicit(&g_totk_vk_ub_gw_fast_override, v, memory_order_release);
}
static inline void totk_w55_set_fault(int f) {
    atomic_store_explicit(&g_totk_w55_fault, f, memory_order_release);
}
static inline int totk_w55_parse_fault(void) {
    const char *e = getenv("TOTK_W55_FAULT");
    if (e && *e) return atoi(e);
    const char *k = getenv("TOTK_KTGL_OPT_FAULT");
    if (k && *k) {
        int kv = atoi(k);
        if (kv >= 221 && kv <= 225) return kv - 220;
    }
    return 0;
}
static inline int totk_w55_fault_mode(void) {
    int f = atomic_load_explicit(&g_totk_w55_fault, memory_order_relaxed);
    if (f >= 0) return f;
    int v = totk_w55_parse_fault();
    atomic_store_explicit(&g_totk_w55_fault, v, memory_order_relaxed);
    return v;
}
#else
static inline int totk_w55_fault_mode(void) { return 0; }
#endif

static inline int totk_vk_ub_gw_fast_on(void) {
#ifdef TOTK_W55_TEST_HOOKS
    int ov = atomic_load_explicit(&g_totk_vk_ub_gw_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
#endif
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("TOTK_VK_UB_GW_FAST");
        v = !(e && strcmp(e, "0") == 0);
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
}

static inline uint64_t totk_ug_u64_at(const void *base, size_t stride, int idx, size_t off) {
    uint64_t v = 0u;
    memcpy(&v, (const uint8_t *)base + (size_t)idx * stride + off, sizeof v);
    return v;
}

static inline int totk_ug_int_at(const void *base, size_t stride, int idx, size_t off) {
    int v = 0;
    memcpy(&v, (const uint8_t *)base + (size_t)idx * stride + off, sizeof v);
    return v;
}

/* 1. Binary-searched UB unflushed overlap collector + Ciura shell-sort by seq. */
static inline int totk_ub_lower_lo(const void *base, size_t stride, size_t off_lo, int nub, uint64_t key) {
    int a = 0, b = nub;
    while (a < b) {
        int m = (a + b) >> 1;
        if (totk_ug_u64_at(base, stride, m, off_lo) < key) a = m + 1;
        else b = m;
    }
    return a;
}

static inline int totk_ub_collect_unflushed(const void *base, size_t stride, size_t off_lo, size_t off_hi,
                                            size_t off_flushed, const uint64_t *maxhi, int nub,
                                            uint64_t ub_lo, uint64_t ub_hi, uint64_t ub_maxlen,
                                            uint64_t lo, uint64_t hi, int **order_p, int *cap_p) {
    if (!nub || !base || !maxhi || hi <= ub_lo || lo >= ub_hi || lo >= hi) return 0;
    int end = totk_ub_lower_lo(base, stride, off_lo, nub, hi);
    uint64_t floor = lo > ub_maxlen ? lo - ub_maxlen : 0u;
    int n = 0;
    for (int i = end - 1; i >= 0 && maxhi[i] > lo; i--) {
        uint64_t elo = totk_ug_u64_at(base, stride, i, off_lo);
        if (elo <= floor && floor) break;
        if (!totk_ug_int_at(base, stride, i, off_flushed) && totk_ug_u64_at(base, stride, i, off_hi) > lo) {
            if (n >= *cap_p) {
                int nc = *cap_p ? *cap_p * 2 : 64;
                int *np = (int *)realloc(*order_p, (size_t)nc * sizeof *np);
                if (!np) return -1;
                *order_p = np; *cap_p = nc;
            }
            (*order_p)[n++] = i;
        }
        if (totk_w55_fault_mode() == 1) break;
    }
    return n;
}

static inline void totk_ub_sort_by_seq(int *order, int n, const void *base, size_t stride, size_t off_seq) {
    static const int gaps[8] = {701, 301, 132, 57, 23, 10, 4, 1};
    if (!order || n <= 1) return;
    for (int gi = 0; gi < 8; gi++) {
        int g = gaps[gi];
        for (int i = g; i < n; i++) {
            int ti = order[i];
            uint64_t ts = totk_ug_u64_at(base, stride, ti, off_seq);
            int j = i;
            for (; j >= g && totk_ug_u64_at(base, stride, order[j - g], off_seq) > ts; j -= g)
                order[j] = order[j - g];
            order[j] = ti;
        }
    }
}

/* 2. O(log N) overlap window [first, end) for ub_forget. */
static inline int totk_ub_forget_window(const void *base, size_t stride, size_t off_lo, const uint64_t *maxhi,
                                        int nub, uint64_t ub_lo, uint64_t ub_hi, uint64_t ub_maxlen,
                                        uint64_t a, uint64_t n, int *first_out, int *end_out) {
    *first_out = 0; *end_out = 0;
    if (totk_w55_fault_mode() == 2) return 0;
    if (!nub || !n || !base || !maxhi || a > UINT64_MAX - n || a + n <= ub_lo || a >= ub_hi) return 0;
    uint64_t hi = a + n, floor = a > ub_maxlen ? a - ub_maxlen : 0u;
    int end = totk_ub_lower_lo(base, stride, off_lo, nub, hi);
    int i = end - 1;
    for (; i >= 0 && maxhi[i] > a; i--) {
        uint64_t elo = totk_ug_u64_at(base, stride, i, off_lo);
        if (elo <= floor && floor) break;
    }
    int first = i + 1;
    if (first >= end) return 0;
    *first_out = first; *end_out = end;
    return 1;
}

/* 3. O(1) / address-bounded gw_seal tracker. */
typedef struct {
    uint32_t count;
    uint64_t min_lo, max_lo;
} totk_gw_unsealed_t;

static inline void totk_gw_unsealed_reset(totk_gw_unsealed_t *u) {
    if (!u) return;
    u->count = 0u; u->min_lo = 0u; u->max_lo = 0u;
}

static inline void totk_gw_unsealed_add(totk_gw_unsealed_t *u, uint64_t lo) {
    if (!u) return;
    if (!u->count || lo < u->min_lo) u->min_lo = lo;
    if (!u->count || lo > u->max_lo) u->max_lo = lo;
    u->count++;
}

static inline int totk_gw_unsealed_has(const totk_gw_unsealed_t *u) {
    if (totk_w55_fault_mode() == 3) return 0;
    return u && u->count != 0u;
}

/* 4. O(log N) alias & pool interval/boundary index for lag_iv_scan. */
enum { TOTK_LAG_IV_MAX_ALIAS = 2048u, TOTK_LAG_IV_MAX_POOL = 2048u, TOTK_LAG_IV_MAX_EDGE = 4096u };
typedef struct { uint64_t va, end, phys_base; } totk_lag_iv_alias_t;
typedef struct { uint64_t lo, hi; } totk_lag_iv_pool_t;

typedef struct {
    totk_lag_iv_alias_t aliases[TOTK_LAG_IV_MAX_ALIAS];
    totk_lag_iv_pool_t pools[TOTK_LAG_IV_MAX_POOL];
    uint64_t edges[TOTK_LAG_IV_MAX_EDGE];
    uint64_t seg_min_hi[TOTK_LAG_IV_MAX_EDGE];
    uint64_t heap[TOTK_LAG_IV_MAX_POOL];
    uint64_t gen;
    uint32_t n_alias, n_pool, n_pool_raw, n_edge;
    uint8_t seg_has[TOTK_LAG_IV_MAX_EDGE];
    uint8_t ready;
} totk_lag_iv_idx_t;

static inline uint64_t totk_lag_va_thresh(uint64_t addr, uint64_t phys, uint64_t edge) {
    if (edge <= phys) { uint64_t d = phys - edge; return d <= addr ? addr - d : 0u; }
    uint64_t d = edge - phys;
    return d <= UINT64_MAX - addr ? addr + d : UINT64_MAX;
}

static inline void totk_lag_iv_begin(totk_lag_iv_idx_t *x, uint64_t gen, uint32_t n_pool_raw) {
    x->gen = gen; x->n_alias = 0u; x->n_pool = 0u; x->n_pool_raw = n_pool_raw;
    x->n_edge = 0u; x->ready = 0u;
}

static inline void totk_lag_iv_heap_push(uint64_t *h, uint32_t *hn, uint64_t v) {
    uint32_t i = (*hn)++;
    while (i > 0u) {
        uint32_t p = (i - 1u) >> 1u;
        if (h[p] <= v) break;
        h[i] = h[p]; i = p;
    }
    h[i] = v;
}

static inline void totk_lag_iv_heap_pop(uint64_t *h, uint32_t *hn) {
    uint32_t n = --(*hn);
    uint64_t v = h[n];
    uint32_t i = 0u;
    while ((i * 2u + 1u) < n) {
        uint32_t c = i * 2u + 1u;
        if (c + 1u < n && h[c + 1u] < h[c]) c++;
        if (v <= h[c]) break;
        h[i] = h[c]; i = c;
    }
    h[i] = v;
}

static inline void totk_lag_iv_sort_ap(totk_lag_iv_idx_t *x) {
    static const int gaps[8] = {701, 301, 132, 57, 23, 10, 4, 1};
    for (int gi = 0; gi < 8; gi++) {
        uint32_t g = (uint32_t)gaps[gi];
        for (uint32_t i = g; i < x->n_alias; i++) {
            totk_lag_iv_alias_t t = x->aliases[i]; uint32_t j = i;
            for (; j >= g && x->aliases[j - g].va > t.va; j -= g) x->aliases[j] = x->aliases[j - g];
            x->aliases[j] = t;
        }
        for (uint32_t i = g; i < x->n_pool; i++) {
            totk_lag_iv_pool_t t = x->pools[i]; uint32_t j = i;
            for (; j >= g && x->pools[j - g].lo > t.lo; j -= g) x->pools[j] = x->pools[j - g];
            x->pools[j] = t;
        }
    }
}

static inline void totk_lag_iv_sort_edges(totk_lag_iv_idx_t *x) {
    static const int gaps[8] = {1750, 701, 301, 132, 57, 23, 10, 4};
    uint32_t ne = 0u;
    for (uint32_t i = 0u; i < x->n_pool; i++) {
        x->edges[ne++] = x->pools[i].lo;
        x->edges[ne++] = x->pools[i].hi;
    }
    for (int gi = 0; gi < 9; gi++) {
        uint32_t g = gi < 8 ? (uint32_t)gaps[gi] : 1u;
        for (uint32_t i = g; i < ne; i++) {
            uint64_t t = x->edges[i]; uint32_t j = i;
            for (; j >= g && x->edges[j - g] > t; j -= g) x->edges[j] = x->edges[j - g];
            x->edges[j] = t;
        }
    }
    uint32_t u = 0u;
    for (uint32_t i = 0u; i < ne; i++) if (!u || x->edges[i] != x->edges[u - 1u]) x->edges[u++] = x->edges[i];
    x->n_edge = u;
}

static inline void totk_lag_iv_finish(totk_lag_iv_idx_t *x) {
    totk_lag_iv_sort_ap(x);
    totk_lag_iv_sort_edges(x);
    uint32_t p = 0u, hn = 0u;
    for (uint32_t k = 0u; k + 1u < x->n_edge; k++) {
        uint64_t e = x->edges[k];
        while (p < x->n_pool && x->pools[p].lo <= e) totk_lag_iv_heap_push(x->heap, &hn, x->pools[p++].hi);
        while (hn > 0u && x->heap[0] <= e) totk_lag_iv_heap_pop(x->heap, &hn);
        x->seg_has[k] = hn > 0u ? 1u : 0u;
        x->seg_min_hi[k] = hn > 0u ? x->heap[0] : 0u;
    }
    if (x->n_edge > 0u) x->seg_has[x->n_edge - 1u] = 0u;
    x->ready = 1u;
}

static inline void totk_lag_iv_lookup_alias(const totk_lag_iv_idx_t *x, uint64_t addr, uint64_t *iv_lo,
                                            uint64_t *iv_hi, uint64_t *phys, uint64_t *end_va, uint8_t *bounded) {
    uint32_t a = 0u, b = x->n_alias;
    while (a < b) {
        uint32_t m = (a + b) >> 1u;
        if (x->aliases[m].va <= addr) a = m + 1u; else b = m;
    }
    if (a > 0u) {
        const totk_lag_iv_alias_t *al = &x->aliases[a - 1u];
        if (addr < al->end) {
            if (al->va > *iv_lo) *iv_lo = al->va;
            if (al->end < *iv_hi) *iv_hi = al->end;
            *phys = al->phys_base + (addr - al->va);
            *end_va = al->end; *bounded = 1u;
            return;
        }
        if (al->end > *iv_lo) *iv_lo = al->end;
    }
    if (a < x->n_alias && x->aliases[a].va < *iv_hi) *iv_hi = x->aliases[a].va;
}

static inline void totk_lag_iv_query(const totk_lag_iv_idx_t *x, uint64_t addr, uint64_t *iv_lo_out,
                                     uint64_t *iv_hi_out, uint64_t *end_va_out, uint8_t *bounded_out) {
    uint64_t iv_lo = 0u, iv_hi = UINT64_MAX, phys = addr, end_va = UINT64_MAX;
    uint8_t bounded = 0u;
    totk_lag_iv_lookup_alias(x, addr, &iv_lo, &iv_hi, &phys, &end_va, &bounded);
    uint32_t a = 0u, b = x->n_edge;
    while (a < b) {
        uint32_t m = (a + b) >> 1u;
        if (x->edges[m] <= phys) a = m + 1u; else b = m;
    }
    if (a > 0u) {
        uint64_t tl = totk_lag_va_thresh(addr, phys, x->edges[a - 1u]);
        if (tl > iv_lo) iv_lo = tl;
        if (a < x->n_edge && x->seg_has[a - 1u]) {
            uint64_t th = totk_lag_va_thresh(addr, phys, x->seg_min_hi[a - 1u]);
            if (totk_w55_fault_mode() != 4 && (!bounded || th < end_va)) { end_va = th; bounded = 1u; }
        }
    }
    if (a < x->n_edge) {
        uint64_t tr = totk_lag_va_thresh(addr, phys, x->edges[a]);
        if (tr < iv_hi) iv_hi = tr;
    }
    if (totk_w55_fault_mode() == 4) { end_va = UINT64_MAX; bounded = 0u; }
    *iv_lo_out = iv_lo; *iv_hi_out = iv_hi; *end_va_out = end_va; *bounded_out = bounded;
}

/* 5. Shader binding-index masks for native_reads & canvas_draw_prepare. */
typedef struct {
    uint64_t img3_mask;
    uint64_t cis_tex128_mask;
    uint8_t fast;
} totk_sh_ub_gw_sum_t;

static inline void totk_sh_ub_gw_sum_add(totk_sh_ub_gw_sum_t *s, int i, int is_img3, int is_cis_tex128) {
    if (!s || i < 0 || i >= 64) return;
    uint64_t bit = UINT64_C(1) << (unsigned)i;
    if (is_img3) s->img3_mask |= bit;
    if (is_cis_tex128) s->cis_tex128_mask |= bit;
}

static inline void totk_sh_ub_gw_sum_finish(totk_sh_ub_gw_sum_t *s, int nb) {
    if (!s) return;
    if (totk_w55_fault_mode() == 5) { s->img3_mask = 0u; s->cis_tex128_mask = 0u; }
    s->fast = (nb >= 0 && nb <= 64) ? 1u : 0u;
}

#endif
