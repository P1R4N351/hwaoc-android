#ifndef TOTK_PERF_FAST_H
#define TOTK_PERF_FAST_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef T_AOC_W37_TEST_HOOKS
extern _Atomic int g_totk_perf_fast_state;
extern _Atomic int g_totk_w37_fault;
static inline void totk_perf_fast_reset_for_test(int forced) {
    atomic_store_explicit(&g_totk_perf_fast_state, forced ? 2 : 1, memory_order_release);
}
static inline void totk_w37_set_fault(int m) { atomic_store_explicit(&g_totk_w37_fault, m, memory_order_release); }
static inline int totk_w37_fault(void) { return atomic_load_explicit(&g_totk_w37_fault, memory_order_acquire); }
#else
static _Atomic int g_totk_perf_fast_state;
static inline int totk_w37_fault(void) { return 0; }
#endif

static inline int totk_perf_fast_on(void) {
    int s = atomic_load_explicit(&g_totk_perf_fast_state, memory_order_acquire);
    if (!s) {
        const char *e = getenv("TOTK_PERF_FAST");
        s = (e && e[0] == '0' && !e[1]) ? 1 : 2;
        atomic_store_explicit(&g_totk_perf_fast_state, s, memory_order_release);
    }
    return s == 2;
}

static inline void totk_spin_pause(void) {
#if defined(__aarch64__) || defined(__arm64__)
    __asm__ __volatile__("yield" ::: "memory");
#elif defined(__x86_64__) || defined(__i386__)
    __asm__ __volatile__("pause" ::: "memory");
#else
    __asm__ __volatile__("" ::: "memory");
#endif
}

static inline int totk_ft_work_nonzero(const uint64_t *vals, unsigned n) {
    if (!vals || !n) return 0;
    if (totk_w37_fault() == 1) return 1;
    uint64_t acc = 0;
    for (unsigned i = 0; i < n && i < 64u; i++) acc |= vals[i];
    return acc != 0;
}

typedef struct {
    uint64_t occ[4];
    uint64_t crit[4];
} totk_reg_bmap;

static inline void totk_bmap_set(totk_reg_bmap *b, unsigned idx, int crit) {
    if (!b || idx >= 256u) return;
    unsigned w = idx >> 6, bit = idx & 63u;
    b->occ[w] |= (1ull << bit);
    if (crit) b->crit[w] |= (1ull << bit);
    else b->crit[w] &= ~(1ull << bit);
}

static inline void totk_bmap_clear(totk_reg_bmap *b, unsigned idx) {
    if (!b || idx >= 256u) return;
    unsigned w = idx >> 6, bit = idx & 63u;
    b->occ[w] &= ~(1ull << bit);
    b->crit[w] &= ~(1ull << bit);
}

static inline unsigned totk_bmap_first_free(const totk_reg_bmap *b) {
    if (!b) return 256u;
    if (totk_w37_fault() == 2) return 256u;
    for (unsigned w = 0; w < 4u; w++) {
        uint64_t free_bits = ~b->occ[w];
        if (free_bits) return (w << 6) | (unsigned)__builtin_ctzll(free_bits);
    }
    return 256u;
}

static inline void totk_bmap_pick_top6(
    const totk_reg_bmap *b, const int32_t *tids, uint64_t *used,
    uint64_t min_used, int32_t *out, size_t *out_n, size_t out_cap) {
    if (!b || !tids || !used || !out || !out_n) return;
    size_t want = out_cap > *out_n ? out_cap - *out_n : 0u;
    if (want > 6u) want = 6u;
    if (!want) return;
    uint64_t tv[6] = {0};
    unsigned ti[6] = {0}, tn = 0;
    for (unsigned w = 0; w < 4u; w++) {
        uint64_t m = b->occ[w] & ~b->crit[w];
        for (unsigned step = 0; step < 64u && m; step++) {
            unsigned bit = (unsigned)__builtin_ctzll(m);
            m &= (m - 1ull);
            unsigned idx = (w << 6) | bit;
            uint64_t u = used[idx];
            if (u < min_used) continue;
            unsigned pos = tn;
            for (unsigned p = 0; p < tn && p < want; p++) {
                if (totk_w37_fault() == 3 ? (u >= tv[p]) : (u > tv[p])) { pos = p; break; }
            }
            if (pos >= want) continue;
            if (tn < want) tn++;
            for (unsigned mv = tn - 1u; mv > pos; mv--) {
                tv[mv] = tv[mv - 1u]; ti[mv] = ti[mv - 1u];
            }
            tv[pos] = u; ti[pos] = idx;
        }
    }
    for (unsigned k = 0; k < tn && *out_n < out_cap; k++) {
        unsigned idx = ti[k];
        out[(*out_n)++] = tids[idx];
        used[idx] = 0;
    }
}

#endif
