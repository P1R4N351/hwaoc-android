/* Wave 55 (Opt 57 / TOTK_VK_UB_GW_FAST=1) unit tests, fault injection & benchmark. */
#ifndef TEST_VK_KTGL_OPT_W55_H
#define TEST_VK_KTGL_OPT_W55_H
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "vk_ub_gw_fast.h"
#ifdef TOTK_W55_STANDALONE
int g_fault_mode = 0;
#endif

_Atomic int g_totk_vk_ub_gw_fast_override = -1;
_Atomic int g_totk_w55_fault = -1;
static int g_w55_fail = 0;

typedef struct { uint64_t lo, hi, seq, fence; int flushed; uint8_t *data; } w55_ub_t;

#define W55_EQ(m, w, g) do { \
    uint64_t wa_ = (uint64_t)(w), ga_ = (uint64_t)(g); \
    if (wa_ != ga_) { \
        g_w55_fail++; \
        fprintf(stderr, "FAIL %s:%d %s want=%llu got=%llu\n", __FILE__, __LINE__, m, \
                (unsigned long long)wa_, (unsigned long long)ga_); \
    } \
} while (0)

static uint64_t w55_ns(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static void w55_ub_build(w55_ub_t *u, uint64_t *mh, int n, uint64_t *lo, uint64_t *hi, uint64_t *ml) {
    *lo = UINT64_MAX; *hi = 0u; *ml = 0u;
    uint64_t pm = 0u;
    for (int i = 0; i < n; i++) {
        if (u[i].lo < *lo) *lo = u[i].lo;
        if (u[i].hi > *hi) *hi = u[i].hi;
        if (u[i].hi - u[i].lo > *ml) *ml = u[i].hi - u[i].lo;
        if (u[i].hi > pm) pm = u[i].hi;
        mh[i] = pm;
    }
}

static void w55_test_ub_collect_and_forget(void) {
    enum { N = 128 };
    static w55_ub_t u[N];
    static uint64_t mh[N];
    for (int i = 0; i < N; i++) {
        uint64_t a = UINT64_C(0x200000) + (uint64_t)i * 512u;
        u[i] = (w55_ub_t){a, a + ((i % 7 == 0) ? 4096u : 256u), (uint64_t)(N - i), 0u, (i % 3 == 0), NULL};
    }
    uint64_t ulo = 0u, uhi = 0u, uml = 0u;
    w55_ub_build(u, mh, N, &ulo, &uhi, &uml);
    int *ord1 = NULL, cap1 = 0, ref[N], nr = 0;
    uint64_t qlo = UINT64_C(0x202000), qhi = UINT64_C(0x206000);
    for (int i = 0; i < N; i++) if (!u[i].flushed && u[i].lo < qhi && qlo < u[i].hi) ref[nr++] = i;
    for (int i = 1; i < nr; i++) for (int j = i; j > 0 && u[ref[j - 1]].seq > u[ref[j]].seq; j--) {
        int t = ref[j]; ref[j] = ref[j - 1]; ref[j - 1] = t;
    }
    int n1 = totk_ub_collect_unflushed(u, sizeof *u, offsetof(w55_ub_t, lo), offsetof(w55_ub_t, hi),
                                       offsetof(w55_ub_t, flushed), mh, N, ulo, uhi, uml, qlo, qhi, &ord1, &cap1);
    totk_ub_sort_by_seq(ord1, n1, u, sizeof *u, offsetof(w55_ub_t, seq));
    W55_EQ("ub_collect_n", nr, n1);
    for (int i = 0; i < nr && i < n1; i++) W55_EQ("ub_collect_i", ref[i], ord1[i]);
    free(ord1);
    int first = 0, end = 0;
    int hit = totk_ub_forget_window(u, sizeof *u, offsetof(w55_ub_t, lo), mh, N, ulo, uhi, uml, qlo, qhi - qlo, &first, &end);
    W55_EQ("ub_forget_hit", 1, hit);
    for (int i = 0; i < N; i++) if (u[i].lo < qhi && qlo < u[i].hi) W55_EQ("ub_forget_bounds", 1, i >= first && i < end);
}

static void w55_lag_iv_oracle(const totk_lag_iv_alias_t *al, uint32_t nal, const totk_lag_iv_pool_t *pl,
                              uint32_t npl, uint64_t addr, uint64_t *olo, uint64_t *ohi, uint64_t *oev, uint8_t *obd) {
    uint64_t iv_lo = 0u, iv_hi = UINT64_MAX, phys = addr, end_va = UINT64_MAX;
    uint8_t bd = 0u;
    for (uint32_t i = 0u; i < nal; i++) {
        uint64_t va = al[i].va, end = al[i].end;
        if (addr >= va && addr < end) {
            if (va > iv_lo) iv_lo = va;
            if (end < iv_hi) iv_hi = end;
            phys = al[i].phys_base + (addr - va);
            end_va = end; bd = 1u; break;
        }
        if (end <= addr) { if (end > iv_lo) iv_lo = end; }
        else if (va > addr && va < iv_hi) iv_hi = va;
    }
    for (uint32_t i = 0u; i < npl; i++) {
        uint64_t plo = pl[i].lo, phi = pl[i].hi;
        if (phi <= plo) continue;
        if (phi <= phys) { uint64_t t = totk_lag_va_thresh(addr, phys, phi); if (t > iv_lo) iv_lo = t; }
        else if (plo > phys) { uint64_t t = totk_lag_va_thresh(addr, phys, plo); if (t < iv_hi) iv_hi = t; }
        else {
            uint64_t tl = totk_lag_va_thresh(addr, phys, plo), th = totk_lag_va_thresh(addr, phys, phi);
            if (tl > iv_lo) iv_lo = tl;
            if (th < iv_hi) iv_hi = th;
            if (!bd || th < end_va) { end_va = th; bd = 1u; }
        }
    }
    *olo = iv_lo; *ohi = iv_hi; *oev = end_va; *obd = bd;
}

static void w55_test_gw_lag_and_sh(void) {
    totk_gw_unsealed_t gu = {0};
    totk_gw_unsealed_add(&gu, 0x3000u); totk_gw_unsealed_add(&gu, 0x1000u);
    W55_EQ("gw_unsealed_has", 1, totk_gw_unsealed_has(&gu));
    W55_EQ("gw_min_lo", 0x1000u, gu.min_lo); W55_EQ("gw_max_lo", 0x3000u, gu.max_lo);
    totk_lag_iv_idx_t *idx = (totk_lag_iv_idx_t *)calloc(1, sizeof *idx);
    assert(idx != NULL);
    totk_lag_iv_begin(idx, 1u, 5u);
    totk_lag_iv_alias_t raw_a[3] = {{0x9000u, 0xb000u, 0x5000u}, {0x1000u, 0x2000u, 0x6000u}, {0x3000u, 0x4500u, 0x8200u}};
    totk_lag_iv_pool_t raw_p[4] = {{0x5000u, 0xc000u}, {0x6000u, 0x9000u}, {0x8000u, 0xa000u}, {0xd000u, 0xe000u}};
    for (int i = 0; i < 3; i++) idx->aliases[idx->n_alias++] = raw_a[i];
    for (int i = 0; i < 4; i++) idx->pools[idx->n_pool++] = raw_p[i];
    totk_lag_iv_finish(idx);
    for (uint64_t a = 0u; a <= 0xf000u; a += 0x400u) {
        uint64_t lo0 = 0u, hi0 = 0u, ev0 = 0u, lo1 = 0u, hi1 = 0u, ev1 = 0u; uint8_t b0 = 0u, b1 = 0u;
        w55_lag_iv_oracle(raw_a, 3u, raw_p, 4u, a, &lo0, &hi0, &ev0, &b0);
        totk_lag_iv_query(idx, a, &lo1, &hi1, &ev1, &b1);
        W55_EQ("iv_lo", lo0, lo1); W55_EQ("iv_hi", hi0, hi1); W55_EQ("iv_ev", ev0, ev1); W55_EQ("iv_bd", b0, b1);
    }
    free(idx);
    totk_sh_ub_gw_sum_t sh = {0};
    totk_sh_ub_gw_sum_add(&sh, 3, 1, 0); totk_sh_ub_gw_sum_add(&sh, 9, 1, 1);
    totk_sh_ub_gw_sum_finish(&sh, 16);
    W55_EQ("sh_img3", (UINT64_C(1) << 3) | (UINT64_C(1) << 9), sh.img3_mask);
    W55_EQ("sh_cis", UINT64_C(1) << 9, sh.cis_tex128_mask);
}

static void *w55_tsan_worker(void *arg) {
    uint64_t s = (uint64_t)(uintptr_t)arg;
    totk_sh_ub_gw_sum_t sh = {0};
    totk_sh_ub_gw_sum_add(&sh, (int)(s & 31u), 1, 1);
    totk_sh_ub_gw_sum_finish(&sh, 32);
    return (void *)(uintptr_t)(sh.img3_mask + (uint64_t)totk_vk_ub_gw_fast_on());
}

static uint64_t w55_bench(const totk_lag_iv_idx_t *idx, const totk_lag_iv_alias_t *al, uint32_t nal,
                          const totk_lag_iv_pool_t *pl, uint32_t npl, int fast) {
    uint64_t sink = 0u;
    for (uint32_t i = 0u; i < 40000u; i++) {
        uint64_t a = UINT64_C(0x10000000) + (uint64_t)(i % 1024u) * UINT64_C(0x1000) + 0x200u;
        uint64_t lo = 0u, hi = 0u, ev = 0u; uint8_t bd = 0u;
        if (fast) totk_lag_iv_query(idx, a, &lo, &hi, &ev, &bd);
        else w55_lag_iv_oracle(al, nal, pl, npl, a, &lo, &hi, &ev, &bd);
        sink ^= lo + hi + ev + bd;
    }
    return sink;
}

static int run_ktgl_w55_tests(int fault_mode) {
    (void)fault_mode; g_w55_fail = 0;
    w55_test_ub_collect_and_forget();
    w55_test_gw_lag_and_sh();
    pthread_t th[4];
    for (int i = 0; i < 4; i++) assert(pthread_create(&th[i], NULL, w55_tsan_worker, (void *)(uintptr_t)i) == 0);
    for (int i = 0; i < 4; i++) pthread_join(th[i], NULL);
    enum { NA = 256, NP = 512 };
    static totk_lag_iv_alias_t al[NA];
    static totk_lag_iv_pool_t pl[NP];
    totk_lag_iv_idx_t *idx = (totk_lag_iv_idx_t *)calloc(1, sizeof *idx);
    assert(idx != NULL);
    totk_lag_iv_begin(idx, 2u, NP);
    for (uint32_t i = 0u; i < NA; i++) {
        uint64_t va = UINT64_C(0x10000000) + (uint64_t)i * UINT64_C(0x4000);
        al[i] = (totk_lag_iv_alias_t){va, va + UINT64_C(0x2000), UINT64_C(0x40000000) + i * UINT64_C(0x2000)};
        idx->aliases[idx->n_alias++] = al[i];
    }
    for (uint32_t i = 0u; i < NP; i++) {
        uint64_t plo = UINT64_C(0x38000000) + (uint64_t)i * UINT64_C(0x10000);
        pl[i] = (totk_lag_iv_pool_t){plo, plo + UINT64_C(0x40000)};
        idx->pools[idx->n_pool++] = pl[i];
    }
    totk_lag_iv_finish(idx);
    uint64_t t0 = w55_ns(), s0 = w55_bench(idx, al, NA, pl, NP, 0), t1 = w55_ns(), s1 = w55_bench(idx, al, NA, pl, NP, 1), t2 = w55_ns();
    W55_EQ("bench_sink", s0, s1);
    free(idx);
    double ms0 = (double)(t1 - t0) / 1e6, ms1 = (double)(t2 - t1) / 1e6;
    printf("ktglopttest w55: baseline=%.3f ms fast=%.3f ms speedup=%.2fx sink=%llu failures=%d\n",
           ms0, ms1, ms1 > 0.0 ? ms0 / ms1 : 0.0, (unsigned long long)s1, g_w55_fail);
    return g_w55_fail != 0;
}

#ifdef TOTK_W55_STANDALONE
int main(void) { return run_ktgl_w55_tests(0); }
#endif
#endif
