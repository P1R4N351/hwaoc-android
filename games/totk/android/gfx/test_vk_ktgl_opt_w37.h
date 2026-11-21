#ifndef TEST_VK_KTGL_OPT_W37_H
#define TEST_VK_KTGL_OPT_W37_H

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define T_AOC_W37_TEST_HOOKS 1
#include "../../runtime/totk_perf_fast.h"

_Atomic int g_totk_perf_fast_state;
_Atomic int g_totk_w37_fault;

static uint64_t w37_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void w37_legacy_top6(
    const int32_t *tids, const int *crit, uint64_t *used,
    uint64_t min_used, int32_t *out, size_t *out_n, size_t out_cap) {
    for (unsigned k = 0; k < 6u && *out_n < out_cap; k++) {
        int best = -1;
        for (unsigned i = 0; i < 256u; i++) {
            if (tids[i] && !crit[i] && used[i] >= min_used &&
                (best < 0 || used[i] > used[best]))
                best = (int)i;
        }
        if (best < 0) break;
        out[(*out_n)++] = tids[best];
        used[best] = 0;
    }
}

static int w37_test_parity(int fault_mode) {
    totk_w37_set_fault(fault_mode ? (fault_mode <= 3 ? fault_mode : 3) : 0);
    int32_t tids[256] = {0}, lo[64] = {0}, fo[64] = {0};
    int crit[256] = {0};
    uint64_t lu[256] = {0}, fu[256] = {0};
    totk_reg_bmap bm = {{0}, {0}};
    assert(totk_bmap_first_free(&bm) == 0u);
    for (unsigned i = 0; i < 200u; i++) {
        unsigned idx = (i * 11u) % 256u;
        if (!tids[idx]) {
            unsigned free_i = totk_bmap_first_free(&bm);
            if (fault_mode == 2 && free_i == 256u) return 1;
            idx = free_i;
        }
        tids[idx] = 5000 + (int32_t)idx;
        crit[idx] = (idx % 7u == 0u);
        uint64_t val = (idx % 5u == 0u) ? 500ull : (uint64_t)((idx * 17u) % 900u);
        lu[idx] = fu[idx] = val;
        totk_bmap_set(&bm, idx, crit[idx]);
    }
    size_t ln = 0, fn = 0;
    for (unsigned w = 0; w < 4u; w++) {
        uint64_t m = bm.crit[w];
        for (unsigned s = 0; s < 64u && m && fn < 64u; s++) {
            unsigned b = (unsigned)__builtin_ctzll(m); m &= (m - 1ull);
            fo[fn++] = tids[(w << 6) | b];
        }
    }
    for (unsigned i = 0; i < 256u && ln < 64u; i++) if (tids[i] && crit[i]) lo[ln++] = tids[i];
    totk_bmap_pick_top6(&bm, tids, fu, 300ull, fo, &fn, 64u);
    w37_legacy_top6(tids, crit, lu, 300ull, lo, &ln, 64u);
    if (ln != fn || memcmp(lo, fo, ln * sizeof(int32_t)) != 0 || memcmp(lu, fu, sizeof lu) != 0) return 1;
    uint64_t zeros[8] = {0};
    if (totk_ft_work_nonzero(zeros, 8u) != 0) return 1;
    zeros[5] = 1ull;
    if (totk_ft_work_nonzero(zeros, 8u) != 1) return 1;
    totk_bmap_clear(&bm, 3u);
    if (totk_bmap_first_free(&bm) > 3u) return 1;
    return 0;
}

static double w37_bench_mode(int fast_on, uint64_t *sink) {
    static int32_t tids[256], out[64];
    static int crit[256];
    static uint64_t used[256], work[8];
    totk_reg_bmap bm = {{0}, {0}};
    for (unsigned i = 0; i < 120u; i++) {
        unsigned idx = i * 2u;
        tids[idx] = 8000 + (int32_t)idx;
        crit[idx] = i < 8u;
        totk_bmap_set(&bm, idx, crit[idx]);
    }
    uint64_t t0 = w37_now_ns(), acc = 0;
    for (int r = 0; r < 10000; r++) {
        for (unsigned w = 0; w < 4u; w++) {
            uint64_t m = bm.occ[w];
            for (unsigned s = 0; s < 64u && m; s++) {
                unsigned b = (unsigned)__builtin_ctzll(m); m &= (m - 1ull);
                used[(w << 6) | b] = (uint64_t)(((w << 6 | b) + (unsigned)r) * 19u % 1000u);
            }
        }
        size_t n = 0;
        if (fast_on) {
            for (unsigned w = 0; w < 4u; w++) {
                uint64_t m = bm.crit[w];
                for (unsigned s = 0; s < 64u && m && n < 64u; s++) {
                    unsigned b = (unsigned)__builtin_ctzll(m); m &= (m - 1ull);
                    out[n++] = tids[(w << 6) | b];
                }
            }
            totk_bmap_pick_top6(&bm, tids, used, 250ull, out, &n, 64u);
            acc += (uint64_t)totk_ft_work_nonzero(work, 8u);
        } else {
            for (unsigned i = 0; i < 256u; i++) if (tids[i]) acc += 1u;
            for (unsigned i = 0; i < 256u && n < 64u; i++) if (tids[i] && crit[i]) out[n++] = tids[i];
            w37_legacy_top6(tids, crit, used, 250ull, out, &n, 64u);
            acc -= 120u;
            uint64_t tmp[8];
            memset(tmp, 0, sizeof tmp);
            for (unsigned i = 0; i < 8u; i++) acc += tmp[i];
        }
        for (size_t i = 0; i < n; i++) acc += (uint64_t)out[i];
    }
    *sink += acc;
    return (double)(w37_now_ns() - t0) / 1e6;
}

static int run_ktgl_w37_tests(int fault_mode) {
    totk_perf_fast_reset_for_test(1);
    totk_spin_pause();
    if (fault_mode > 0) {
        int m = (fault_mode - 1) % 3 + 1;
        return w37_test_parity(m) != 0 ? 1 : 0;
    }
    assert(w37_test_parity(0) == 0);
    const char *env = getenv("TOTK_PERF_FAST");
    int req_off = (env && env[0] == '0' && !env[1]);
    totk_perf_fast_reset_for_test(1);
    uint64_t s0 = 0, s1 = 0;
    double ms0 = w37_bench_mode(0, &s0);
    double ms1 = w37_bench_mode(1, &s1);
    assert(s0 == s1);
    printf("ktglopttest w37: req_off=%d off_ms=%.3f on_ms=%.3f ratio=%.2fx sink=%llu\n",
           req_off, ms0, ms1, ms1 > 0 ? ms0 / ms1 : 0.0, (unsigned long long)s1);
    return 0;
}

#endif
