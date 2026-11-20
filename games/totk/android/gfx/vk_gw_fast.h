#ifndef TOTK_VK_GW_FAST_H
#define TOTK_VK_GW_FAST_H
/* Wave 16 (Opt 18): 4-way g_gw seen dedup, wait-free g_gw seqlock summary + 4-way lag_original_pending memo (TOTK_VK_GW_FAST=1). */
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef TOTK_KTGL_OPT_TEST
extern int g_fault_mode;
#define TOTK_GW_FAULT(n) (g_fault_mode == (n))
#else
#define TOTK_GW_FAULT(n) 0
#endif

static inline int totk_gw_fast_on(void) {
    static _Atomic int cached = -1;
    int v = atomic_load_explicit(&cached, memory_order_relaxed);
    if (v < 0) {
        const char *e = getenv("TOTK_VK_GW_FAST");
        v = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&cached, v, memory_order_relaxed);
    }
    return v;
}

static inline uint32_t totk_gw_bit_idx(uint64_t p) {
    uint64_t x = (p ^ (p >> 7) ^ (p >> 17)) * UINT64_C(0x9e3779b97f4a7c15);
    return (uint32_t)(x >> 58) & 63u;
}

static inline uint64_t totk_gw_bloom_mask(uint64_t lo, uint64_t hi) {
    if (hi < lo) return ~UINT64_C(0);
    if (hi == lo) return 0u;
    uint64_t p0 = lo >> 16, p1 = (hi - 1u) >> 16;
    if (TOTK_GW_FAULT(45)) p1 = p0;
    if (p0 == p1) return UINT64_C(1) << totk_gw_bit_idx(p0);
    if (p1 - p0 >= 64u) return ~UINT64_C(0);
    uint64_t m = 0;
    for (uint64_t k = 0, n = p1 - p0 + 1u; k < n && k < 64u; k++)
        m |= UINT64_C(1) << totk_gw_bit_idx(p0 + k);
    return m;
}

enum { TOTK_GWS_SETS = 64u, TOTK_GWS_WAYS = 4u };

typedef struct {
    uint64_t a, n, sub, physical, spans;
    int guarded, pad_;
} totk_gw_seen_ent;

typedef struct {
    totk_gw_seen_ent w[TOTK_GWS_WAYS];
    uint8_t hand, pad[7];
} totk_gw_seen_set;

typedef struct {
    totk_gw_seen_set s[TOTK_GWS_SETS];
} totk_gw_seen4;

static inline uint32_t totk_gw_seen_idx(uint64_t a, uint64_t n) {
    uint64_t x = ((a >> 6) ^ (a >> 18) ^ (n * UINT64_C(0x9e3779b97f4a7c15))) * UINT64_C(0xbf58476d1ce4e5b9);
    return (uint32_t)(x >> 58) & (TOTK_GWS_SETS - 1u);
}

static inline int totk_gw_seen4_probe(totk_gw_seen4 *s, uint64_t a, uint64_t n,
                                      uint64_t sub, uint64_t physical, uint64_t spans,
                                      int guard, int *need_guard) {
    assert(s != NULL && need_guard != NULL && n > 0);
    totk_gw_seen_set *set = &s->s[totk_gw_seen_idx(a, n)];
    int stale = -1;
    for (uint32_t i = 0; i < TOTK_GWS_WAYS; i++) {
        totk_gw_seen_ent *e = &set->w[i];
        if (!e->n || e->sub != sub) { if (stale < 0) stale = (int)i; continue; }
        if (e->a == a && e->n == n) {
            if (e->physical == physical && e->spans == spans) {
                if (!TOTK_GW_FAULT(44) && guard && !e->guarded) { e->guarded = 1; *need_guard = 1; }
                else *need_guard = 0;
                return 1;
            }
            stale = (int)i;
        }
    }
    uint32_t slot = stale >= 0 ? (uint32_t)stale : (set->hand & (TOTK_GWS_WAYS - 1u));
    if (stale < 0) set->hand = (uint8_t)((set->hand + 1u) & (TOTK_GWS_WAYS - 1u));
    set->w[slot] = (totk_gw_seen_ent){a, n, sub, physical, spans, guard ? 1 : 0, 0};
    *need_guard = guard ? 1 : 0;
    return 0;
}

typedef struct {
    _Atomic uint64_t seq;
    _Atomic uint32_t count;
    _Atomic uint64_t min_lo;
    _Atomic uint64_t max_hi;
    _Atomic uint64_t bloom;
} totk_gw_summary;

static inline void totk_gw_sum_clear(totk_gw_summary *s) {
    assert(s != NULL);
    uint64_t seq = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_store_explicit(&s->seq, seq + 1u, memory_order_release);
    atomic_thread_fence(memory_order_seq_cst);
    atomic_store_explicit(&s->count, 0u, memory_order_relaxed);
    atomic_store_explicit(&s->min_lo, 0u, memory_order_relaxed);
    atomic_store_explicit(&s->max_hi, 0u, memory_order_relaxed);
    atomic_store_explicit(&s->bloom, 0u, memory_order_relaxed);
    atomic_store_explicit(&s->seq, seq + 2u, memory_order_release);
}

static inline void totk_gw_sum_add(totk_gw_summary *s, uint64_t lo, uint64_t hi) {
    assert(s != NULL && hi > lo);
    uint64_t seq = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_store_explicit(&s->seq, seq + 1u, memory_order_release);
    atomic_thread_fence(memory_order_seq_cst);
    uint32_t cnt = atomic_load_explicit(&s->count, memory_order_relaxed);
    uint64_t mlo = cnt ? atomic_load_explicit(&s->min_lo, memory_order_relaxed) : lo;
    uint64_t mhi = cnt ? atomic_load_explicit(&s->max_hi, memory_order_relaxed) : hi;
    uint64_t blm = atomic_load_explicit(&s->bloom, memory_order_relaxed) | totk_gw_bloom_mask(lo, hi);
    atomic_store_explicit(&s->min_lo, lo < mlo ? lo : mlo, memory_order_relaxed);
    atomic_store_explicit(&s->max_hi, hi > mhi ? hi : mhi, memory_order_relaxed);
    atomic_store_explicit(&s->bloom, blm, memory_order_relaxed);
    atomic_store_explicit(&s->count, cnt + 1u, memory_order_relaxed);
    atomic_store_explicit(&s->seq, seq + 2u, memory_order_release);
}

static inline void totk_gw_sum_publish(totk_gw_summary *s, uint32_t cnt,
                                       uint64_t mlo, uint64_t mhi, uint64_t blm) {
    assert(s != NULL);
    uint64_t seq = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_store_explicit(&s->seq, seq + 1u, memory_order_release);
    atomic_thread_fence(memory_order_seq_cst);
    atomic_store_explicit(&s->min_lo, mlo, memory_order_relaxed);
    atomic_store_explicit(&s->max_hi, mhi, memory_order_relaxed);
    atomic_store_explicit(&s->bloom, blm, memory_order_relaxed);
    atomic_store_explicit(&s->count, cnt, memory_order_relaxed);
    atomic_store_explicit(&s->seq, seq + 2u, memory_order_release);
}

static inline int totk_gw_hit_locked(const totk_gw_summary *s, uint64_t lo, uint64_t hi) {
    assert(s != NULL);
    if (hi < lo) return 1;
    if (hi == lo) return 0;
    uint32_t cnt = atomic_load_explicit(&s->count, memory_order_relaxed);
    if (!cnt) return 0;
    uint64_t mlo = atomic_load_explicit(&s->min_lo, memory_order_relaxed);
    uint64_t mhi = atomic_load_explicit(&s->max_hi, memory_order_relaxed);
    if (hi <= mlo || lo >= mhi) return 0;
    uint64_t blm = atomic_load_explicit(&s->bloom, memory_order_relaxed);
    return (blm & totk_gw_bloom_mask(lo, hi)) != 0u;
}

static inline int totk_gw_may_overlap(const totk_gw_summary *s, uint64_t lo, uint64_t hi) {
    assert(s != NULL);
    if (hi < lo) return 1;
    if (hi == lo) return 0;
    uint64_t s1 = atomic_load_explicit(&s->seq, memory_order_acquire);
    if (s1 & 1u) return 1;
    uint32_t cnt = atomic_load_explicit(&s->count, memory_order_relaxed);
    uint64_t mlo = atomic_load_explicit(&s->min_lo, memory_order_relaxed);
    uint64_t mhi = atomic_load_explicit(&s->max_hi, memory_order_relaxed);
    uint64_t blm = atomic_load_explicit(&s->bloom, memory_order_relaxed);
    atomic_thread_fence(memory_order_acquire);
    uint64_t s2 = atomic_load_explicit(&s->seq, memory_order_relaxed);
    if (s1 != s2) return 1;
    if (!cnt || hi <= mlo || lo >= mhi) return 0;
    return (blm & totk_gw_bloom_mask(lo, hi)) != 0u;
}

enum { TOTK_ORIG_SETS = 256u, TOTK_ORIG_WAYS = 4u };

typedef struct {
    uint64_t address, size, revision, ticket, spans, lifetime, done;
    int pending, valid;
} totk_orig_pend_ent;

typedef struct {
    totk_orig_pend_ent w[TOTK_ORIG_WAYS];
    uint8_t hand, pad[7];
} totk_orig_pend_set;

typedef struct {
    totk_orig_pend_set s[TOTK_ORIG_SETS];
} totk_orig_pend_memo4;

static inline uint32_t totk_orig_pend_idx(uint64_t address, uint64_t size) {
    uint64_t x = (address >> 4) ^ (address >> 16) ^ (address >> 32) ^ size;
    x ^= x >> 23;
    x *= UINT64_C(0x9e3779b1);
    x ^= x >> 16;
    return (uint32_t)x & (TOTK_ORIG_SETS - 1u);
}

static inline totk_orig_pend_ent *totk_orig_pend_find(
        totk_orig_pend_memo4 *m, uint64_t address, uint64_t size,
        uint64_t spans, uint64_t lifetime, uint64_t cur_done) {
    assert(m != NULL && size > 0);
    totk_orig_pend_set *set = &m->s[totk_orig_pend_idx(address, size)];
    for (uint32_t i = 0; i < TOTK_ORIG_WAYS; i++) {
        totk_orig_pend_ent *e = &set->w[i];
        if (!e->valid || e->address != address || e->size != size) continue;
        if (e->spans != spans || e->lifetime != lifetime) continue;
        if (!TOTK_GW_FAULT(46) && e->pending && e->done != cur_done) continue;
        return e;
    }
    return NULL;
}

static inline void totk_orig_pend_insert(
        totk_orig_pend_memo4 *m, uint64_t address, uint64_t size,
        uint64_t rev, uint64_t ticket, uint64_t spans,
        uint64_t lifetime, uint64_t done, int pending) {
    assert(m != NULL && size > 0);
    totk_orig_pend_set *set = &m->s[totk_orig_pend_idx(address, size)];
    totk_orig_pend_ent ent = {address, size, rev, ticket, spans, lifetime, done, pending, 1};
    for (uint32_t i = 0; i < TOTK_ORIG_WAYS; i++) {
        totk_orig_pend_ent *e = &set->w[i];
        if (!e->valid || (e->address == address && e->size == size)) { *e = ent; return; }
    }
    uint32_t v = set->hand & (TOTK_ORIG_WAYS - 1u);
    set->hand = (uint8_t)((set->hand + 1u) & (TOTK_ORIG_WAYS - 1u));
    set->w[v] = ent;
}

#endif
