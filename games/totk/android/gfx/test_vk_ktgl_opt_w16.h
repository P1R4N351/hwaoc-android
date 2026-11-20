#ifndef TEST_VK_KTGL_OPT_W16_H
#define TEST_VK_KTGL_OPT_W16_H

#include "vk_gw_fast.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern uint64_t host_now_ns(void);
static volatile uint64_t w16_sink;

static int test_w16_gw_seen4_fault44(void) {
    totk_gw_seen4 s = {0};
    uint64_t ssbo[4] = {0, 0, 0, 0}, base = 0x10000000ull;
    int got = 0;
    for (uint64_t k = 0; k < 2048u && got < 4; k++) {
        uint64_t cand = base + k * 0x1000ull;
        if (totk_gw_seen_idx(cand, 4096u) == totk_gw_seen_idx(base, 4096u)) ssbo[got++] = cand;
    }
    assert(got == 4);
    for (int i = 0; i < 4; i++) assert(totk_gw_seen_idx(ssbo[i], 4096u) == totk_gw_seen_idx(ssbo[0], 4096u));
    int ng = 0;
    for (int i = 0; i < 4; i++) {
        int hit = totk_gw_seen4_probe(&s, ssbo[i], 4096u, 10u, ssbo[i], 1u, i > 0, &ng);
        if (hit || ng != (i > 0)) return 1;
    }
    int hit0 = totk_gw_seen4_probe(&s, ssbo[0], 4096u, 10u, ssbo[0], 1u, 1, &ng);
    if (!hit0 || !ng) return 1;
    int hit1 = totk_gw_seen4_probe(&s, ssbo[0], 4096u, 10u, ssbo[0], 1u, 1, &ng);
    if (!hit1 || ng) return 1;
    int hit_sp = totk_gw_seen4_probe(&s, ssbo[1], 4096u, 10u, ssbo[1], 2u, 1, &ng);
    if (hit_sp || !ng) return 1;
    return 0;
}

static int test_w16_gw_sum_fault45(void) {
    totk_gw_summary s = {0};
    assert(totk_gw_may_overlap(&s, 0x34000000ull, 0x34010000ull) == 0);
    totk_gw_sum_add(&s, 0x3400f000ull, 0x34021000ull);
    if (!totk_gw_may_overlap(&s, 0x34010000ull, 0x34010100ull)) return 1;
    if (!totk_gw_hit_locked(&s, 0x3401f000ull, 0x34020000ull)) return 1;
    assert(totk_gw_may_overlap(&s, 0x35000000ull, 0x35001000ull) == 0);
    totk_gw_sum_publish(&s, 1u, 0x36000000ull, 0x36001000ull,
                        totk_gw_bloom_mask(0x36000000ull, 0x36001000ull));
    if (totk_gw_may_overlap(&s, 0x34010000ull, 0x34010100ull)) return 1;
    return 0;
}

static int test_w16_orig_pend4_fault46(void) {
    totk_orig_pend_memo4 m = {0};
    uint64_t ubo[4] = {0, 0, 0, 0}, base = 0x40000000ull;
    int got = 0;
    for (uint64_t k = 0; k < 8192u && got < 4; k++) {
        uint64_t cand = base + k * 0x1000ull;
        if (totk_orig_pend_idx(cand, 256u) == totk_orig_pend_idx(base, 256u)) ubo[got++] = cand;
    }
    assert(got == 4);
    for (int i = 0; i < 4; i++) assert(totk_orig_pend_idx(ubo[i], 256u) == totk_orig_pend_idx(ubo[0], 256u));
    for (int i = 0; i < 4; i++)
        totk_orig_pend_insert(&m, ubo[i], 256u, 5u, 11u, 1u, 1u, 20u, i == 0 ? 1 : 0);
    if (totk_orig_pend_find(&m, ubo[0], 256u, 1u, 1u, 21u) != NULL) return 1;
    for (int i = 1; i < 4; i++) {
        totk_orig_pend_ent *e = totk_orig_pend_find(&m, ubo[i], 256u, 1u, 1u, 21u);
        if (!e || e->pending != 0) return 1;
    }
    return 0;
}

static void bench_w16_gw_seen(void) {
    enum { NW = 200000 };
    uint64_t ssbo[4] = {0, 0, 0, 0}, base = 0x70000000ull;
    int got = 0;
    unsigned direct_h = (unsigned)((base >> 6) ^ (base >> 20) ^ 4096u) % 256u;
    for (uint64_t k = 0; k < 65536u && got < 4; k++) {
        uint64_t cand = base + k * 0x1000ull;
        if (((unsigned)((cand >> 6) ^ (cand >> 20) ^ 4096u) % 256u) == direct_h) ssbo[got++] = cand;
    }
    assert(got == 4);
    struct { uint64_t a, n, sub, physical, spans; int guarded; } seen1[256] = {{0}};
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    uint64_t t0 = host_now_ns(), miss1 = 0;
    for (int i = 0; i < NW; i++) {
        uint64_t a = ssbo[i & 3];
        unsigned h = (unsigned)((a >> 6) ^ (a >> 20) ^ 4096u) % 256u;
        if (!(seen1[h].a == a && seen1[h].n == 4096u && seen1[h].sub == 1u)) {
            seen1[h].a = a; seen1[h].n = 4096u; seen1[h].sub = 1u;
            pthread_mutex_lock(&mu); miss1++; pthread_mutex_unlock(&mu);
        }
    }
    uint64_t ns1 = host_now_ns() - t0;
    totk_gw_seen4 seen4 = {0};
    uint64_t t1 = host_now_ns(), miss4 = 0;
    for (int i = 0; i < NW; i++) {
        uint64_t a = ssbo[i & 3];
        int ng = 0;
        if (!totk_gw_seen4_probe(&seen4, a, 4096u, 1u, a, 1u, 1, &ng)) {
            pthread_mutex_lock(&mu); miss4++; pthread_mutex_unlock(&mu);
        }
    }
    uint64_t ns4 = host_now_ns() - t1;
    w16_sink += miss1 + miss4;
    double sb0 = (double)ns1 * 1e-6, sf0 = (double)ns4 * 1e-6;
    printf("aoc_w16_gw_seen: base=%.3f ms fast=%.3f ms speedup=%.2fx (misses=%llu->%llu)\n",
           sb0, sf0, sb0 / (sf0 > 0.001 ? sf0 : 0.001), (unsigned long long)miss1, (unsigned long long)miss4);
}

static void bench_w16_gw_pend(void) {
    enum { NW = 200000 };
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    static totk_gw_summary gs;
    totk_gw_sum_clear(&gs);
    for (int i = 0; i < 64; i++) totk_gw_sum_add(&gs, 0x90000000ull + (uint64_t)i * 0x20000ull, 0x90001000ull + (uint64_t)i * 0x20000ull);
    uint64_t tp0 = host_now_ns(), hits_b = 0;
    for (int i = 0; i < NW; i++) {
        uint64_t lo = 0x10000000ull + (((uint64_t)i ^ (w16_sink & 0u)) & 3u) * 0x1000ull;
        pthread_mutex_lock(&mu); hits_b += (uint64_t)totk_gw_hit_locked(&gs, lo, lo + 256u); pthread_mutex_unlock(&mu);
    }
    uint64_t nsp0 = host_now_ns() - tp0;
    uint64_t tp1 = host_now_ns(), hits_f = 0;
    for (int i = 0; i < NW; i++) {
        uint64_t lo = 0x10000000ull + (((uint64_t)i ^ (w16_sink & 0u)) & 3u) * 0x1000ull;
        hits_f += (uint64_t)totk_gw_may_overlap(&gs, lo, lo + 256u);
    }
    uint64_t nsp1 = host_now_ns() - tp1;
    w16_sink += hits_b + hits_f;
    double pb0 = (double)nsp0 * 1e-6, pf0 = (double)nsp1 * 1e-6;
    printf("aoc_w16_gw_pend: base=%.3f ms fast=%.3f ms speedup=%.2fx\n",
           pb0, pf0, pb0 / (pf0 > 0.001 ? pf0 : 0.001));
}

static int run_ktgl_w16_tests(int fault_mode) {
    (void)fault_mode;
    if (test_w16_gw_seen4_fault44() != 0) { fprintf(stderr, "FAIL w16 seen4\n"); return 1; }
    if (test_w16_gw_sum_fault45() != 0) { fprintf(stderr, "FAIL w16 gw_sum\n"); return 1; }
    if (test_w16_orig_pend4_fault46() != 0) { fprintf(stderr, "FAIL w16 orig_pend4\n"); return 1; }
    if (!fault_mode) { bench_w16_gw_seen(); bench_w16_gw_pend(); }
    return 0;
}

#endif
