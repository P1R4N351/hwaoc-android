#ifndef TEST_VK_KTGL_OPT_W52_H
#define TEST_VK_KTGL_OPT_W52_H
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "vk_bind_wb_fast.h"
#ifdef TOTK_W52_STANDALONE
int g_fault_mode = 0;
#endif

_Atomic int g_totk_vk_bind_wb_fast_override = -1;
_Atomic int g_totk_w52_fault = -1;
static int g_w52_fail = 0;

typedef struct { uint32_t dtype, kind, count; int32_t reg; uint8_t rw; } w52_bind_t;
typedef struct { uint64_t buf, base, identity, lo, size; uint8_t own; } w52_pool_t;

#define W52_EXPECT_EQ(m, w, g) do { \
    uint64_t wa_ = (uint64_t)(w), ga_ = (uint64_t)(g); \
    if (wa_ != ga_) { \
        g_w52_fail++; \
        fprintf(stderr, "FAIL %s:%d %s want=%llu got=%llu\n", __FILE__, __LINE__, m, \
                (unsigned long long)wa_, (unsigned long long)ga_); \
    } \
} while (0)

static uint64_t w52_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static void w52_test_bind_sum(void) {
    totk_sh_bind_sum_t s0 = {0}, s1 = {0};
    w52_bind_t bs[4] = {{6u, 0u, 2u, 3, 0u}, {6u, 0u, 1u, 11, 0u}, {1u, 1u, 4u, 5, 0u}, {6u, 0u, 1u, 3, 1u}};
    for (int i = 0; i < 4; i++) {
        totk_sh_bind_sum_add(&s0, bs[i].count, bs[i].rw, bs[i].dtype == 7u, bs[i].dtype == 6u && bs[i].kind <= 1u, bs[i].reg, bs[i].dtype == 1u);
    }
    totk_sh_bind_sum_finish(&s0);
    W52_EXPECT_EQ("sum_cnt", 8u, s0.desc_total_count);
    W52_EXPECT_EQ("sum_rw", 1u, s0.has_any_rw);
    W52_EXPECT_EQ("sum_blk0", 0u, s0.ubo_unchanged_blocked);
    W52_EXPECT_EQ("sum_mask", (1u << 3) | (1u << 11), s0.ubo_check_mask);
    W52_EXPECT_EQ("sum_img", 1u, s0.has_pso_life_img);
    totk_sh_bind_sum_add(&s1, 1u, 0, 1, 0, 2, 0);
    totk_sh_bind_sum_finish(&s1);
    W52_EXPECT_EQ("sum_blk1", 1u, s1.ubo_unchanged_blocked);
    W52_EXPECT_EQ("sum_noimg", 0u, s1.has_pso_life_img);
}

static void w52_test_wb_tile_and_pool(void) {
    totk_wb_tile_idx_t *ti = calloc(1u, sizeof *ti);
    totk_pool_map_t *pm = calloc(1u, sizeof *pm);
    assert(ti && pm);
    typedef struct { uint64_t addr, pool, ver; } tr_t;
    tr_t ring[512];
    for (uint32_t i = 0u; i < 768u; i++) {
        uint32_t s = i % 512u;
        if (i >= 512u) {
            totk_wb_tile_del_ap(ti, ring[s].addr, ring[s].pool);
            totk_wb_tile_del_ver(ti, ring[s].ver);
        }
        ring[s] = (tr_t){UINT64_C(0x1000000) + (uint64_t)i * 65536u, 100u + (i & 31u), (uint64_t)i + 1u};
        totk_wb_tile_put_ap(ti, ring[s].addr, ring[s].pool, (uint16_t)s);
        totk_wb_tile_put_ver(ti, ring[s].ver, (uint16_t)s);
    }
    uint16_t gs = 0u;
    W52_EXPECT_EQ("ti_evicted_ap", 0, totk_wb_tile_find_ap(ti, UINT64_C(0x1000000), 100u, &gs));
    W52_EXPECT_EQ("ti_evicted_v", 0, totk_wb_tile_find_ver(ti, 1u, &gs));
    W52_EXPECT_EQ("ti_live_ap", 1, totk_wb_tile_find_ap(ti, ring[42].addr, ring[42].pool, &gs));
    W52_EXPECT_EQ("ti_live_ap_s", 42u, gs);
    W52_EXPECT_EQ("ti_live_v", 1, totk_wb_tile_find_ver(ti, ring[42].ver, &gs));
    W52_EXPECT_EQ("ti_live_v_s", 42u, gs);

    totk_pool_map_clear(pm);
    totk_pool_map_put_buf(pm, UINT64_C(0x8800), UINT64_C(0xA0000000));
    totk_pool_map_put_ls(pm, UINT64_C(0xB0000000), UINT64_C(0x10000), 3);
    totk_pool_map_put_ls(pm, UINT64_C(0xB0000000), UINT64_C(0x10000), 19);
    totk_pool_map_put_id(pm, UINT64_C(77), 19);
    W52_EXPECT_EQ("pm_buf", UINT64_C(0xA0000000), totk_pool_map_find_buf(pm, UINT64_C(0x8800)));
    W52_EXPECT_EQ("pm_ls_hi", 19, totk_pool_map_find_ls(pm, UINT64_C(0xB0000000), UINT64_C(0x10000)));
    W52_EXPECT_EQ("pm_id", 19, totk_pool_map_find_id(pm, UINT64_C(77)));
    totk_pool_map_clear(pm);
    W52_EXPECT_EQ("pm_clr", 0u, totk_pool_map_find_buf(pm, UINT64_C(0x8800)));
    free(ti); free(pm);
}

static uint64_t w52_bench_slow(const w52_pool_t *pl, int np, const uint64_t *vers, int nv) {
    uint64_t sink = 0u;
    for (int rep = 0; rep < 200; rep++) {
        for (int q = 0; q < 256; q++) {
            uint64_t id = pl[q].identity, b = pl[q].buf, v = vers[q & 255];
            for (int i = np - 1; i >= 0; i--) { if (pl[i].lo == pl[q].lo && pl[i].size == pl[q].size) { sink += pl[i].identity; break; } }
            for (int i = 0; i < np; i++) { if (pl[i].own && pl[i].buf == b) { sink += pl[i].base; break; } }
            for (int i = 0; i < np; i++) { if (pl[i].identity == id) { sink += (uint64_t)i; break; } }
            for (int i = 0; i < nv; i++) { if (vers[i] == v) { sink += (uint64_t)i; break; } }
        }
    }
    return sink;
}

static uint64_t w52_bench_fast(const w52_pool_t *pl, const totk_pool_map_t *pm,
                               const uint64_t *vers, const totk_wb_tile_idx_t *ti) {
    uint64_t sink = 0u;
    for (int rep = 0; rep < 200; rep++) {
        for (int q = 0; q < 256; q++) {
            int32_t li = totk_pool_map_find_ls(pm, pl[q].lo, pl[q].size);
            if (li >= 0) sink += pl[li].identity;
            sink += totk_pool_map_find_buf(pm, pl[q].buf);
            int32_t ii = totk_pool_map_find_id(pm, pl[q].identity);
            if (ii >= 0) sink += (uint64_t)ii;
            uint16_t slot = 0u;
            if (totk_wb_tile_find_ver(ti, vers[q & 255], &slot)) sink += slot;
        }
    }
    return sink;
}

static void w52_bench_run(void) {
    enum { NP = 512, NV = 256 };
    w52_pool_t *pl = calloc((size_t)NP, sizeof *pl);
    uint64_t *vers = calloc((size_t)NV, sizeof *vers);
    totk_pool_map_t *pm = calloc(1u, sizeof *pm);
    totk_wb_tile_idx_t *ti = calloc(1u, sizeof *ti);
    assert(pl && vers && pm && ti);
    totk_pool_map_clear(pm);
    for (int i = 0; i < NP; i++) {
        pl[i] = (w52_pool_t){UINT64_C(0x10000) + (uint64_t)i * 8u, UINT64_C(0x80000000) + (uint64_t)i * 65536u,
                             UINT64_C(1000) + (uint64_t)i, UINT64_C(0x40000000) + (uint64_t)i * 4096u, 4096u, 1u};
        totk_pool_map_put_buf(pm, pl[i].buf, pl[i].base);
        totk_pool_map_put_id(pm, pl[i].identity, i);
        totk_pool_map_put_ls(pm, pl[i].lo, pl[i].size, i);
    }
    for (int i = 0; i < NV; i++) {
        vers[i] = UINT64_C(500) + (uint64_t)i;
        totk_wb_tile_put_ver(ti, vers[i], (uint16_t)i);
    }
    uint64_t bs = UINT64_MAX, bf = UINT64_MAX, s0 = 0u, s1 = 0u;
    for (int r = 0; r < 5; r++) {
        uint64_t t0 = w52_now_ns();
        s0 = w52_bench_slow(pl, NP, vers, NV);
        uint64_t t1 = w52_now_ns();
        s1 = w52_bench_fast(pl, pm, vers, ti);
        uint64_t t2 = w52_now_ns();
        if (t1 - t0 < bs) bs = t1 - t0;
        if (t2 - t1 < bf) bf = t2 - t1;
    }
    W52_EXPECT_EQ("bench_sink", s0, s1);
    double sms = (double)bs / 1e6, fms = (double)bf / 1e6;
    printf("ktglopttest w52: slow=%.3f ms fast=%.3f ms speedup=%.2fx sink=%llu failures=%d\n",
           sms, fms, sms / (fms > 0.001 ? fms : 0.001), (unsigned long long)s1, g_w52_fail);
    free(pl); free(vers); free(pm); free(ti);
}

static int run_ktgl_w52_tests(int fault_mode) {
    if (fault_mode > 0) totk_w52_set_fault(fault_mode);
    g_w52_fail = 0;
    totk_vk_bind_wb_fast_set_for_test(1);
    w52_test_bind_sum();
    w52_test_wb_tile_and_pool();
    w52_bench_run();
    return g_w52_fail != 0;
}
#ifdef TOTK_W52_STANDALONE
int main(void) {
    return run_ktgl_w52_tests(totk_w52_fault_mode());
}
#endif
#endif
