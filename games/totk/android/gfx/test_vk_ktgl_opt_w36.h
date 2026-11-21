#ifndef TEST_VK_KTGL_OPT_W36_H
#define TEST_VK_KTGL_OPT_W36_H
#include "vk_post_fast.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned w36_legacy_dset_mask(
    totk_post_dset_state *st, unsigned stage, uint64_t src_rev, uint64_t aux_rev, uint64_t lut_rev) {
    (void)st; (void)stage; (void)src_rev; (void)aux_rev; (void)lut_rev;
    return 0xFu;
}

static void w36_check_post_dset_and_cv(int fault_mode) {
    totk_post_dset_state st[6];
    memset(st, 0, sizeof st);
    uint64_t lut = (UINT64_C(2) << 32) | 1u;
    assert(totk_post_dset_mask(&st[TOTK_POST_EDGE], TOTK_POST_EDGE, 0u, 0u, lut, fault_mode) == 0xFu);
    assert(totk_post_dset_mask(&st[TOTK_POST_EDGE], TOTK_POST_EDGE, 0u, 0u, lut, fault_mode) == 0x3u);
    assert(totk_post_dset_mask(&st[TOTK_POST_WEIGHTS], TOTK_POST_WEIGHTS, 0u, 11u, lut, fault_mode) == 0xFu);
    assert(totk_post_dset_mask(&st[TOTK_POST_WEIGHTS], TOTK_POST_WEIGHTS, 0u, 11u, lut, fault_mode) == 0x1u);
    assert(totk_post_dset_mask(&st[TOTK_POST_WEIGHTS], TOTK_POST_WEIGHTS, 0u, 12u, lut, fault_mode) == 0x3u);
    assert(totk_post_dset_mask(&st[TOTK_POST_RCAS], TOTK_POST_RCAS, 21u, 0u, 0u, fault_mode) == 0xFu);
    assert(totk_post_dset_mask(&st[TOTK_POST_RCAS], TOTK_POST_RCAS, 21u, 0u, 0u, fault_mode) == 0x0u);
    assert(totk_post_dset_mask(&st[TOTK_POST_RCAS], TOTK_POST_RCAS, 22u, 0u, 0u, fault_mode) == 0xFu);
    assert(totk_post_dset_mask(&st[TOTK_POST_SGSR], TOTK_POST_SGSR, 0u, 0u, 0u, fault_mode) == 0xFu);
    assert(totk_post_dset_mask(&st[TOTK_POST_SGSR], TOTK_POST_SGSR, 0u, 0u, 0u, fault_mode) == 0xFu);
    assert(totk_post_dset_mask(&st[TOTK_POST_EASU], TOTK_POST_EASU, 31u, 0u, 0u, fault_mode) == 0xFu);
    assert(totk_post_dset_mask(&st[TOTK_POST_EASU], TOTK_POST_EASU, 31u, 0u, 0u, fault_mode) == 0x0u);
    assert(totk_cv_should_wake(0u, 1, fault_mode) == 0);
    assert(totk_cv_should_wake(0u, 0, fault_mode) == 1);
    assert(totk_cv_should_wake(2u, 1, fault_mode) == 1);
}

static void w36_check_ready_ring(int fault_mode) {
    totk_present_ready_ring r;
    memset(&r, 0, sizeof r);
    for (uint64_t tk = 1u; tk <= 1200u; tk++) totk_present_ready_note(&r, tk, UINT64_C(5000000) + tk * 16u);
    for (uint64_t tk = 1u; tk <= 176u; tk++) {
        assert(totk_present_ready_read(&r, tk, fault_mode) == 0u);
    }
    for (uint64_t tk = 177u; tk <= 1200u; tk++) {
        assert(totk_present_ready_read(&r, tk, fault_mode) == UINT64_C(5000000) + tk * 16u);
    }
}

typedef struct {
    totk_present_ready_ring *r;
} w36_tsan_ctx;

static void *w36_tsan_writer(void *arg) {
    w36_tsan_ctx *ctx = (w36_tsan_ctx *)arg;
    for (uint64_t tk = 1u; tk <= 25000u; tk++) {
        totk_present_ready_note(ctx->r, tk, UINT64_C(1000000) + tk * 8u);
    }
    return NULL;
}

static void *w36_tsan_reader(void *arg) {
    w36_tsan_ctx *ctx = (w36_tsan_ctx *)arg;
    uint64_t acc = 0u;
    for (uint64_t tk = 1u; tk <= 25000u; tk++) {
        uint64_t v = totk_present_ready_read(ctx->r, tk, 0);
        acc += v ? (v & 0xFFu) : 0u;
        acc += (uint64_t)totk_vk_post_fast_on();
    }
    return (void *)(uintptr_t)acc;
}

static uint64_t w36_bench_post(int fast) {
    totk_post_dset_state st[4][6];
    memset(st, 0, sizeof st);
    totk_present_ready_ring r;
    memset(&r, 0, sizeof r);
    for (uint64_t tk = 1u; tk <= 1024u; tk++) totk_present_ready_note(&r, tk, UINT64_C(5000000) + tk * 16u);
    pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    uint64_t lut = (UINT64_C(2) << 32) | 1u;
    uint64_t desc[4][6][4];
    memset(desc, 0, sizeof desc);
    uint64_t sink = 0u;
    for (uint32_t f = 0u; f < 100000u; f++) {
        unsigned s = f & 3u;
        uint64_t b0 = 0x100u + (f & 15u);
        uint64_t vals[3][4] = {{b0, b0, 1u, 2u}, {b0, 11u + s, 1u, 2u}, {31u + s, 31u + s, 31u + s, 31u + s}};
        unsigned stages[3] = {TOTK_POST_EDGE, TOTK_POST_WEIGHTS, TOTK_POST_RCAS};
        uint64_t aux[3] = {0u, 11u + s, 0u};
        uint64_t sr[3] = {0u, 0u, 31u + s};
        for (unsigned j = 0u; j < 3u; j++) {
            unsigned stg = stages[j];
            unsigned m = fast ? totk_post_dset_mask(&st[s][stg], stg, sr[j], aux[j], lut, 0)
                              : w36_legacy_dset_mask(&st[s][stg], stg, sr[j], aux[j], lut);
            for (unsigned b = 0u; b < 4u; b++) if (m & (1u << b)) desc[s][stg][b] = vals[j][b];
            sink += desc[s][stg][0] + desc[s][stg][1];
        }
        uint64_t tk = 1u + (f & 1023u), ready = 0u;
        if (fast) ready = totk_present_ready_read(&r, tk, 0);
        else { pthread_mutex_lock(&mu); ready = atomic_load_explicit(&r.s[tk % TOTK_PRESENT_READY_RING].ready_ns, memory_order_relaxed); pthread_mutex_unlock(&mu); }
        sink += ready & 0xFFu;
    }
    pthread_mutex_destroy(&mu);
    return sink;
}

static int run_ktgl_w36_tests(int fault_mode) {
    const char *env = getenv("TOTK_VK_POST_FAST");
    int init_on = totk_vk_post_fast_on();
    if (env && !strcmp(env, "0")) assert(!init_on); else assert(init_on);
    totk_w36_set_fault(fault_mode);
    w36_check_post_dset_and_cv(fault_mode);
    w36_check_ready_ring(fault_mode);

    totk_present_ready_ring r;
    memset(&r, 0, sizeof r);
    w36_tsan_ctx ctx = {&r};
    pthread_t th[3];
    assert(pthread_create(&th[0], NULL, w36_tsan_writer, &ctx) == 0);
    assert(pthread_create(&th[1], NULL, w36_tsan_reader, &ctx) == 0);
    assert(pthread_create(&th[2], NULL, w36_tsan_reader, &ctx) == 0);
    for (int i = 0; i < 3; i++) { void *ret = NULL; assert(pthread_join(th[i], &ret) == 0); }

    uint64_t t0 = host_now_ns(), s0 = w36_bench_post(0), off_ns = host_now_ns() - t0;
    uint64_t t1 = host_now_ns(), s1 = w36_bench_post(1), on_ns = host_now_ns() - t1;
    assert(s0 == s1);
    printf("aoc_post_w36: off=%.3f ms on=%.3f ms sink=%llu\n", off_ns / 1e6, on_ns / 1e6, (unsigned long long)s0);
    return 0;
}
#endif
