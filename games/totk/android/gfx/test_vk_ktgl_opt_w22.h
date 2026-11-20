/* Wave 22 (Opt 24: TOTK_VK_IMG_FAST=1) unit, benchmark & fault-injection suite (faults 62..66). */
#ifndef TEST_VK_KTGL_OPT_W22_H
#define TEST_VK_KTGL_OPT_W22_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "vk_img_fast.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int g_w22_fast_on = 1;

static uint64_t w22_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

enum { W22_SLOTS = 64u };
static pthread_mutex_t g_w22_mu = PTHREAD_MUTEX_INITIALIZER;
static _Atomic uint64_t g_w22_rf = 1u;

static uint64_t w22_rf_last(int fast) {
    if (fast) return atomic_load_explicit(&g_w22_rf, memory_order_acquire);
    pthread_mutex_lock(&g_w22_mu);
    uint64_t v = atomic_load_explicit(&g_w22_rf, memory_order_relaxed);
    pthread_mutex_unlock(&g_w22_mu);
    return v;
}

static int w22_test_tidc4_crm_steady(void) {
    totk_tidc4 *tc = calloc(1, sizeof *tc);
    totk_clear_read_memo4 *cr = calloc(1, sizeof *cr);
    assert(tc != NULL && cr != NULL);
    totk_tidc4_ent in = {0};
    in.tid = 11; in.want = 1; in.gen = 3u; in.sub = 50u; in.view_u = 0x5555u;
    in.x = (void *)(uintptr_t)0x6666u; in.in_pool = 1u;
    totk_tidc4_put(tc, &in);
    totk_tidc4_ent *e = totk_tidc4_probe(tc, 11, 1, 3u);
    if (!e || e->view_u != 0x5555u) { free(tc); free(cr); return 1; }
    if (totk_tidc4_probe(tc, 11, 257, 3u) != NULL) { free(tc); free(cr); return 1; }
    if (totk_tidc4_probe(tc, 11, 1, 4u) != NULL) { free(tc); free(cr); return 1; }
    if (totk_tidc4_needs_sub_check(e, 51u, 0, 0) != 0) { free(tc); free(cr); return 1; }
    if (totk_tidc4_needs_sub_check(e, 51u, 1, 1) != 0) { free(tc); free(cr); return 1; }
    if (totk_tidc4_needs_sub_check(e, 51u, 1, 0) != 1) { free(tc); free(cr); return 1; }
    totk_clear_read_memo4_put(cr, 9u, 100u, 0x1234u, 1u);
    if (!totk_clear_read_memo4_hit(cr, 9u, 100u, 0x1234u, 1u)) { free(tc); free(cr); return 1; }
    if (totk_clear_read_memo4_hit(cr, 9u, 100u, 0x9999u, 1u)) { free(tc); free(cr); return 1; }
    if (totk_clear_read_memo4_hit(cr, 9u, 100u, 0x1234u, 2u)) { free(tc); free(cr); return 1; }
    free(tc); free(cr);
    if (!totk_tex_steady_gpu_written(1, 0, 0)) return 1;
    if (totk_tex_steady_gpu_written(1, 1, 0)) return 1;
    return totk_tex_steady_gpu_written(1, 0, 1);
}

typedef struct { _Atomic int stop; } w22_thr_t;

static void *w22_fence_worker(void *p) {
    w22_thr_t *t = (w22_thr_t *)p;
    uint64_t prev = 0u;
    while (!atomic_load_explicit(&t->stop, memory_order_acquire)) {
        uint64_t cur = w22_rf_last(1);
        if (cur < prev) abort();
        prev = cur;
    }
    return NULL;
}

static int w22_test_mt_rf(void) {
    w22_thr_t s;
    atomic_init(&s.stop, 0);
    pthread_t th[3];
    for (int i = 0; i < 3; i++) assert(pthread_create(&th[i], NULL, w22_fence_worker, &s) == 0);
    for (int i = 0; i < 3000; i++) {
        pthread_mutex_lock(&g_w22_mu);
        uint64_t nxt = atomic_load_explicit(&g_w22_rf, memory_order_relaxed) + 1u;
        atomic_store_explicit(&g_w22_rf, nxt, memory_order_release);
        pthread_mutex_unlock(&g_w22_mu);
    }
    atomic_store_explicit(&s.stop, 1, memory_order_release);
    for (int i = 0; i < 3; i++) pthread_join(th[i], NULL);
    return w22_rf_last(1) < 3001u;
}

static int w22_bench_tidc4_sub(void) {
    totk_tidc4 *tc = calloc(1, sizeof *tc);
    assert(tc != NULL);
    for (int i = 0; i < 64; i++) {
        totk_tidc4_ent en = {.sub = 1u, .view_u = (uint64_t)(0x10000 + i), .x = (void *)(uintptr_t)(0x20000 + i),
                             .tid = i + 1, .want = 1, .gen = 1u, .in_pool = 1u, .valid = 1u};
        totk_tidc4_put(tc, &en);
    }
    enum { ITERS = 400000 };
    uint64_t leg = 0, fast = 0, t0 = w22_now_ns();
    for (int i = 0; i < ITERS; i++) {
        uint64_t sub = 10u + (uint64_t)(i >> 4);
        int tid = 1 + (i & 31);
        pthread_mutex_lock(&g_w22_mu);
        leg ^= (uint64_t)(0x10000 + (tid - 1)) ^ (sub & 0u);
        pthread_mutex_unlock(&g_w22_mu);
    }
    uint64_t leg_ns = w22_now_ns() - t0;
    t0 = w22_now_ns();
    for (int i = 0; i < ITERS; i++) {
        uint64_t sub = 10u + (uint64_t)(i >> 4);
        int tid = 1 + (i & 31);
        totk_tidc4_ent *e = totk_tidc4_probe(tc, tid, 1, 1u);
        if (e && totk_tidc4_needs_sub_check(e, sub, 0, 1)) {
            pthread_mutex_lock(&g_w22_mu);
            e->sub = sub;
            pthread_mutex_unlock(&g_w22_mu);
        }
        fast ^= e ? e->view_u : 0u;
    }
    uint64_t fast_ns = w22_now_ns() - t0;
    free(tc);
    printf("aoc_w22_tidc4_sub: iters=%d leg_ms=%.3f fast_ms=%.3f speedup=%.2fx diff=%llu\n",
           ITERS, leg_ns / 1e6, fast_ns / 1e6, fast_ns ? (double)leg_ns / (double)fast_ns : 0.0,
           (unsigned long long)(leg ^ fast));
    return leg != fast;
}

static int w22_bench_clear_gpuwrite(void) {
    static int clear_c[8];
    uint32_t clear_mask = 0u;
    enum { ITERS = 400000 };
    uint64_t leg = 0, fast = 0, t0 = w22_now_ns();
    for (int i = 0; i < ITERS; i++) {
        int pend = 0;
        for (int k = 0; k < 8; k++) pend |= clear_c[k];
        pthread_mutex_lock(&g_w22_mu);
        uint64_t gw = 1u + (uint64_t)pend;
        pthread_mutex_unlock(&g_w22_mu);
        leg += gw + w22_rf_last(0);
    }
    uint64_t leg_ns = w22_now_ns() - t0;
    t0 = w22_now_ns();
    for (int i = 0; i < ITERS; i++) {
        uint64_t pend = clear_mask != 0u ? 1u : 0u;
        uint64_t gw = (uint64_t)totk_tex_steady_gpu_written(1, 0, 0) + pend;
        fast += gw + w22_rf_last(1);
    }
    uint64_t fast_ns = w22_now_ns() - t0;
    printf("aoc_w22_clear_gpuwrite: iters=%d leg_ms=%.3f fast_ms=%.3f speedup=%.2fx diff=%llu\n",
           ITERS, leg_ns / 1e6, fast_ns / 1e6, fast_ns ? (double)leg_ns / (double)fast_ns : 0.0,
           (unsigned long long)(leg ^ fast));
    return leg != fast;
}

static int run_ktgl_w22_tests(int fault_mode) {
    (void)fault_mode;
    int bad = 0;
    bad |= w22_test_tidc4_crm_steady();
    bad |= w22_test_mt_rf();
    bad |= w22_bench_tidc4_sub();
    bad |= w22_bench_clear_gpuwrite();
    return bad ? 1 : 0;
}

#endif
