#ifndef TEST_VK_KTGL_OPT_W7_H
#define TEST_VK_KTGL_OPT_W7_H

#include "vk_res_fast.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern uint64_t host_now_ns(void);
int g_w7_aoc_fault = 0;
static _Atomic uint64_t w7_samp_epoch = 1;
static pthread_mutex_t w7_samp_mu = PTHREAD_MUTEX_INITIALIZER;
static uint64_t w7_samp_table[2048];
static volatile uint64_t w7_sink;

static uint64_t w7_slow_resolve(int tid, uint32_t ver) {
    pthread_mutex_lock(&w7_samp_mu);
    uint64_t v = ((uint64_t)(uint32_t)tid << 32) | (ver + 1u);
    pthread_mutex_unlock(&w7_samp_mu);
    return v;
}

typedef struct { int n_threads, ops_per_thread; } w7_sc_arg;

static void *w7_sc_worker(void *arg) {
    const w7_sc_arg *a = (const w7_sc_arg *)arg;
    totk_scache4 *sc = calloc(1, sizeof *sc);
    if (!sc) return (void *)1;
    for (int i = 0; i < a->ops_per_thread; i++) {
        int sid = 17 + ((i & 3) * 512);
        uint32_t ep = (uint32_t)atomic_load_explicit(&w7_samp_epoch, memory_order_acquire);
        uint64_t v = totk_scache4_get(sc, sid, ep);
        if (!v) {
            pthread_mutex_lock(&w7_samp_mu);
            v = w7_samp_table[sid];
            pthread_mutex_unlock(&w7_samp_mu);
            totk_scache4_put(sc, sid, ep, v);
        }
        if (v != (uint64_t)(sid * 17u + 1u)) { free(sc); return (void *)2; }
    }
    free(sc);
    return NULL;
}

static int test_fmemo_and_tvic(void) {
    totk_fmemo *fm = calloc(1, sizeof *fm);
    totk_tidc_vic *vc = calloc(1, sizeof *vc);
    if (!fm || !vc) { free(fm); free(vc); return 1; }
    fm->gen = 1;
    int tids[4] = { 100, (1 << 12) | (100 ^ 1), (2 << 12) | (100 ^ 2), (3 << 12) | (100 ^ 3) };
    for (int i = 0; i < 4; i++) {
        uint32_t key = (uint32_t)tids[i] | (3u << 20);
        totk_fmemo_put(fm, key, 10, 0x1000u + (uint64_t)tids[i], (void *)(uintptr_t)(0x5000u + (unsigned)i));
    }
    for (int i = 0; i < 4; i++) {
        uint32_t key = (uint32_t)tids[i] | (3u << 20);
        const totk_fm_ent *h = totk_fmemo_get(fm, key);
        if (!h || h->v != 0x1000u + (uint64_t)tids[i]) { free(fm); free(vc); return 2; }
    }
    uint32_t nkey = 99u | (3u << 20);
    totk_fmemo_put(fm, nkey, 10, 0, NULL);
    const totk_fm_ent *ne = totk_fmemo_get(fm, nkey);
    if (!ne || totk_fmemo_sub_ok(ne, 11)) { free(fm); free(vc); return 3; }
    unsigned chash = (unsigned)tids[0] ^ ((unsigned)tids[0] >> 12) ^ 3u * 0x9e3779b9u;
    for (int i = 1; i < 4; i++) {
        totk_tvic_ent ve = { .tid = tids[i], .want = 1, .in_pool = 1, .gen = 1, .view_u = 0x2000u + (uint64_t)tids[i], .x = (void *)1 };
        totk_tvic_put(vc, chash, &ve);
    }
    for (int i = 1; i < 4; i++) {
        totk_tvic_ent *got = totk_tvic_find(vc, chash, tids[i], 1, 1);
        if (!got || got->view_u != 0x2000u + (uint64_t)tids[i]) { free(fm); free(vc); return 4; }
    }
    if (totk_tvic_find(vc, chash, tids[1], 1, 2) != NULL) { free(fm); free(vc); return 5; }
    free(fm); free(vc);
    return 0;
}

static int test_scache4_mt(void) {
    totk_scache4 *sc = calloc(1, sizeof *sc);
    if (!sc) return 1;
    for (int i = 0; i < 2048; i++) w7_samp_table[i] = (uint64_t)(i * 17u + 1u);
    totk_scache4_put(sc, 17, 1, 0xAAAAu);
    if (totk_scache4_get(sc, 17, 2) != 0) { free(sc); return 2; }
    free(sc);
    pthread_t th[8];
    w7_sc_arg arg = { 8, 50000 };
    for (int i = 0; i < 8; i++) if (pthread_create(&th[i], NULL, w7_sc_worker, &arg) != 0) return 3;
    for (int i = 0; i < 8; i++) { void *ret = NULL; pthread_join(th[i], &ret); if (ret != NULL) return 4; }
    return 0;
}

static uint64_t w7_bench_tidc(int use_fast, uint64_t *misses_out) {
    typedef struct { int tid, want; uint32_t gen; uint64_t v; } sim_c1;
    sim_c1 *c1 = calloc(4096, sizeof *c1);
    totk_tidc_vic *vc = calloc(1, sizeof *vc);
    assert(c1 != NULL && vc != NULL);
    int tids[4] = { 100, (1 << 12) | (100 ^ 1), (2 << 12) | (100 ^ 2), (3 << 12) | (100 ^ 3) };
    uint64_t t0 = host_now_ns(), sum = 0, misses = 0;
    for (int it = 0; it < 200000; it++) {
        int tid = tids[it & 3];
        unsigned hash = (unsigned)tid ^ ((unsigned)tid >> 12) ^ (3u * 0x9e3779b9u);
        sim_c1 *c = &c1[hash & 4095u];
        if (c->tid == tid && c->want == 1 && c->gen == 1) { sum += c->v; continue; }
        if (use_fast) {
            totk_tvic_ent *ve = totk_tvic_find(vc, hash, tid, 1, 1);
            if (ve) { sum += ve->view_u; continue; }
            if (c->tid > 0) {
                totk_tvic_ent ev = { .tid = c->tid, .want = c->want, .gen = c->gen, .view_u = c->v };
                totk_tvic_put(vc, hash, &ev);
            }
        }
        uint64_t v = w7_slow_resolve(tid, 1);
        misses++;
        *c = (sim_c1){ tid, 1, 1, v };
        sum += v;
    }
    uint64_t dt = host_now_ns() - t0;
    w7_sink ^= sum;
    *misses_out = misses;
    free(c1); free(vc);
    return dt;
}

static int run_ktgl_w7_tests(int fault_mode) {
    g_w7_aoc_fault = fault_mode;
    int r1 = test_fmemo_and_tvic();
    if (r1) { fprintf(stderr, "FAIL w7 test_fmemo_and_tvic rc=%d fault=%d\n", r1, fault_mode); return 1; }
    int r2 = test_scache4_mt();
    if (r2) { fprintf(stderr, "FAIL w7 test_scache4_mt rc=%d fault=%d\n", r2, fault_mode); return 1; }
    uint64_t lm = 0, fm = 0;
    uint64_t lns = w7_bench_tidc(0, &lm), fns = w7_bench_tidc(1, &fm);
    double spd = fns ? (double)lns / (double)fns : 0.0;
    printf("aoc_w7_res_fast: fault=%d tidc_200k legacy=%.2fms (misses=%llu) fast=%.2fms (misses=%llu) speedup=%.2fx sink=%llu\n",
           fault_mode, (double)lns / 1e6, (unsigned long long)lm, (double)fns / 1e6, (unsigned long long)fm, spd, (unsigned long long)w7_sink);
    if (fault_mode == 0 && (lm != 200000 || fm != 4)) { fprintf(stderr, "FAIL w7 misses lm=%llu fm=%llu\n", (unsigned long long)lm, (unsigned long long)fm); return 1; }
    return 0;
}

#endif
