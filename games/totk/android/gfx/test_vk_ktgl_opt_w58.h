/* Wave 58 equivalence, fault-injection, TSan & -O2 microbenchmark header for ktglopttest. */
#ifndef TEST_VK_KTGL_OPT_W58_H
#define TEST_VK_KTGL_OPT_W58_H
#ifndef TOTK_W58_TEST_HOOKS
#define TOTK_W58_TEST_HOOKS
#endif
#include "vk_copy_span_fast.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

_Atomic int g_totk_vk_copy_span_fast_override = -1;
_Atomic int g_totk_w58_fault = -1;

static void w58_req(int ok) {
    if (!ok) abort();
}

static uint64_t w58_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static int w58_oracle_span_cmp(const void *x, const void *y) {
    const totk_span32_t *a = (const totk_span32_t *)x, *b = (const totk_span32_t *)y;
    if (a->lo != b->lo) return a->lo < b->lo ? -1 : 1;
    return a->off < b->off ? -1 : (a->off > b->off ? 1 : 0);
}

static int w58_oracle_ep_cmp(const void *x, const void *y) {
    const totk_span_ep_t *a = (const totk_span_ep_t *)x, *b = (const totk_span_ep_t *)y;
    return a->lo < b->lo ? -1 : (a->lo > b->lo ? 1 : 0);
}

static int w58_oracle_bsearch(const totk_span_ep_t *seg, int nseg, uintptr_t a) {
    int lo = 0, hi = nseg;
    while (lo < hi) {
        int m = (lo + hi) >> 1;
        if (seg[m].lo <= a) lo = m + 1;
        else hi = m;
    }
    return lo;
}

static void w58_check_copy_and_low32(void) {
    uint8_t src[512], dst0[512], dst1[512];
    for (size_t i = 0u; i < sizeof src; i++) src[i] = (uint8_t)(i * 11u + 5u);
    memset(dst0, 0x33, sizeof dst0); memset(dst1, 0x33, sizeof dst1);
    for (size_t z = 0u; z < 4u; z++) for (size_t y = 0u; y < 8u; y++)
        memcpy(dst0 + z * 128u + y * 16u, src + z * 128u + y * 16u, 16u);
    totk_memcpy_3d(dst1, 16u, 128u, src, 16u, 128u, 16u, 8u, 4u);
    w58_req(memcmp(dst0, dst1, sizeof dst0) == 0);

    memset(dst0, 0x44, sizeof dst0); memset(dst1, 0x44, sizeof dst1);
    for (size_t y = 0u; y < 6u; y++) memcpy(dst0 + y * 24u, src + y * 32u, 20u);
    totk_memcpy_2d(dst1, 24u, src, 32u, 20u, 6u);
    w58_req(memcmp(dst0, dst1, sizeof dst0) == 0);

    uint64_t old_h[40], new_h[40];
    for (int n = 0; n <= 37; n++) {
        for (int i = 0; i < 40; i++) {
            old_h[i] = ((uint64_t)i << 32) | (uint32_t)(0x1000 + i);
            new_h[i] = ((uint64_t)(i + 99) << 32) | (uint32_t)(0x1000 + i);
        }
        w58_req(totk_u64_low32_changed(old_h, new_h, n) == 0);
        if (n > 0) {
            int pos[3] = {0, n >> 1, n - 1};
            for (int k = 0; k < 3; k++) {
                new_h[pos[k]] ^= UINT64_C(1);
                w58_req(totk_u64_low32_changed(old_h, new_h, n) == 1);
                new_h[pos[k]] ^= UINT64_C(1);
            }
        }
    }
}

static void w58_check_spans_and_gates(void) {
    totk_span32_t ref[130], fast[130], tmp[130];
    totk_span_ep_t ep_ref[260], ep_fast[260];
    int sizes[5] = {0, 1, 17, 64, 129};
    for (int si = 0; si < 5; si++) {
        int n = sizes[si];
        uint32_t rng = 0x1234567u + (uint32_t)n;
        for (int i = 0; i < n; i++) {
            rng = rng * 1664525u + 1013904223u;
            uintptr_t lo = ((uintptr_t)(rng & 0x1fu) << 12) + UINT64_C(0x10000);
            ref[i] = (totk_span32_t){lo, lo + 0x2000u, (uintptr_t)i, (uint64_t)i};
            fast[i] = ref[i];
        }
        if (n > 1) qsort(ref, (size_t)n, sizeof *ref, w58_oracle_span_cmp);
        totk_sort_spans_stable(fast, tmp, n);
        w58_req(memcmp(ref, fast, (size_t)n * sizeof *ref) == 0);

        for (int i = 0; i < n; i++) {
            ep_ref[2 * i] = (totk_span_ep_t){ref[i].lo, -1};
            ep_ref[2 * i + 1] = (totk_span_ep_t){ref[i].end, -1};
            ep_fast[2 * i] = ep_ref[2 * i];
            ep_fast[2 * i + 1] = ep_ref[2 * i + 1];
        }
        if (n > 0) qsort(ep_ref, (size_t)(2 * n), sizeof *ep_ref, w58_oracle_ep_cmp);
        int nr = 0;
        for (int i = 0; i < 2 * n; i++) if (!nr || ep_ref[i].lo != ep_ref[nr - 1].lo) ep_ref[nr++] = ep_ref[i];
        int nf = totk_sort_dedup_ep(ep_fast, 2 * n);
        w58_req(nf == nr);
        for (int q = 0; q < nr; q++) w58_req(ep_ref[q].lo == ep_fast[q].lo && ep_ref[q].best == ep_fast[q].best);
        for (int q = 0; q < nr; q++) {
            uintptr_t a = ep_ref[q].lo;
            w58_req(totk_span_bsearch_ep(ep_fast, nf, a - 1u) == w58_oracle_bsearch(ep_ref, nr, a - 1u));
            w58_req(totk_span_bsearch_ep(ep_fast, nf, a) == w58_oracle_bsearch(ep_ref, nr, a));
            w58_req(totk_span_bsearch_ep(ep_fast, nf, a + 1u) == w58_oracle_bsearch(ep_ref, nr, a + 1u));
        }
    }
    uint64_t mf = UINT64_MAX;
    w58_req(totk_fence_can_prune(0, mf, 99u) == 0);
    totk_fence_min_note(&mf, 15u);
    totk_fence_min_note(&mf, 10u);
    totk_fence_min_note(&mf, 0u);
    w58_req(mf == 10u && totk_fence_can_prune(4, mf, 9u) == 0 && totk_fence_can_prune(4, mf, 10u) == 1);
}

static void *w58_thread_fn(void *arg) {
    int tid = *(const int *)arg;
    totk_span32_t s[64], tmp[64];
    uint64_t oh[32], nh[32];
    for (int it = 0; it < 200; it++) {
        for (int i = 0; i < 64; i++) {
            uintptr_t lo = (uintptr_t)((63 - i + it + tid) & 31) << 12;
            s[i] = (totk_span32_t){lo, lo + 4096u, 0u, (uint64_t)i};
        }
        totk_sort_spans_stable(s, tmp, 64);
        for (int i = 1; i < 64; i++) w58_req(s[i - 1].lo <= s[i].lo);
        for (int i = 0; i < 32; i++) { oh[i] = (uint64_t)i; nh[i] = ((uint64_t)(it + tid) << 32) | (uint32_t)i; }
        w58_req(totk_u64_low32_changed(oh, nh, 32) == 0);
    }
    return NULL;
}

__attribute__((noinline)) static uint64_t w58_bench_base(totk_span32_t *s, totk_span_ep_t *ep, int n,
                                                         const uint64_t *oh, const uint64_t *nh) {
    uint64_t acc = 0u;
    qsort(s, (size_t)n, sizeof *s, w58_oracle_span_cmp);
    for (int i = 0; i < n; i++) {
        ep[2 * i] = (totk_span_ep_t){s[i].lo, -1};
        ep[2 * i + 1] = (totk_span_ep_t){s[i].end, -1};
    }
    qsort(ep, (size_t)(2 * n), sizeof *ep, w58_oracle_ep_cmp);
    int k = 0;
    for (int i = 0; i < 2 * n; i++) if (!k || ep[i].lo != ep[k - 1].lo) ep[k++] = ep[i];
    for (int q = 0; q < 64; q++) acc += (uint64_t)w58_oracle_bsearch(ep, k, ep[(q * 3) % k].lo + 7u);
    int diff = 0;
    for (int i = 0; i < 64; i++) if ((uint32_t)oh[i] != (uint32_t)nh[i]) { diff = 1; break; }
    return acc + (uint64_t)diff + (uint64_t)s[0].lo;
}

__attribute__((noinline)) static uint64_t w58_bench_fast(totk_span32_t *s, totk_span32_t *tmp, totk_span_ep_t *ep,
                                                         int n, const uint64_t *oh, const uint64_t *nh) {
    uint64_t acc = 0u;
    totk_sort_spans_stable(s, tmp, n);
    for (int i = 0; i < n; i++) {
        ep[2 * i] = (totk_span_ep_t){s[i].lo, -1};
        ep[2 * i + 1] = (totk_span_ep_t){s[i].end, -1};
    }
    int k = totk_sort_dedup_ep(ep, 2 * n);
    for (int q = 0; q < 64; q++) acc += (uint64_t)totk_span_bsearch_ep(ep, k, ep[(q * 3) % k].lo + 7u);
    int diff = totk_u64_low32_changed(oh, nh, 64);
    return acc + (uint64_t)diff + (uint64_t)s[0].lo;
}

static void w58_bench_run(void) {
    totk_span32_t s0[128], s1[128], tmp[128];
    totk_span_ep_t ep[256];
    uint64_t oh[64], nh[64];
    for (int i = 0; i < 64; i++) { oh[i] = (uint64_t)i; nh[i] = ((uint64_t)i << 32) | (uint32_t)i; }
    enum { N = 128, ITERS = 1000 };
    uint64_t sink0 = 0u, sink1 = 0u;
    uint64_t t0 = w58_now_ns();
    for (int it = 0; it < ITERS; it++) {
        for (int i = 0; i < N; i++) {
            uintptr_t lo = (uintptr_t)(((N - 1 - i) * 7 + it) & 255) << 12;
            s0[i] = (totk_span32_t){lo, lo + 8192u, 0u, (uint64_t)i};
        }
        oh[(unsigned)it & 63u] ^= UINT64_C(1) << 33;
        sink0 += w58_bench_base(s0, ep, N, oh, nh);
    }
    uint64_t dt0 = w58_now_ns() - t0;
    for (int i = 0; i < 64; i++) { oh[i] = (uint64_t)i; }
    uint64_t t1 = w58_now_ns();
    for (int it = 0; it < ITERS; it++) {
        for (int i = 0; i < N; i++) {
            uintptr_t lo = (uintptr_t)(((N - 1 - i) * 7 + it) & 255) << 12;
            s1[i] = (totk_span32_t){lo, lo + 8192u, 0u, (uint64_t)i};
        }
        oh[(unsigned)it & 63u] ^= UINT64_C(1) << 33;
        sink1 += w58_bench_fast(s1, tmp, ep, N, oh, nh);
    }
    uint64_t dt1 = w58_now_ns() - t1;
    w58_req(sink0 == sink1);
    double bms = (double)dt0 / 1e6, fms = (double)dt1 / 1e6;
    printf("ktglopttest w58: base=%.3f ms fast=%.3f ms speedup=%.2fx sink=%llu\n",
           bms, fms, fms > 0.0 ? bms / fms : 1.0, (unsigned long long)sink1);
}

static int run_ktgl_w58_tests(int fault_mode) {
    (void)fault_mode;
    totk_vk_copy_span_fast_set_for_test(1);
    totk_w58_set_fault(-1);
    w58_check_copy_and_low32();
    w58_check_spans_and_gates();
    pthread_t th[4];
    int tids[4] = {0, 1, 2, 3};
    for (int i = 0; i < 4; i++) w58_req(pthread_create(&th[i], NULL, w58_thread_fn, &tids[i]) == 0);
    for (int i = 0; i < 4; i++) pthread_join(th[i], NULL);
    w58_req(totk_vk_copy_span_fast_on() == 1);
    totk_vk_copy_span_fast_set_for_test(0);
    w58_req(totk_vk_copy_span_fast_on() == 0);
    totk_vk_copy_span_fast_set_for_test(1);
    w58_bench_run();
    return 0;
}

#ifdef TOTK_W58_STANDALONE
int main(void) {
    return run_ktgl_w58_tests(0);
}
#endif
#endif
