/* Wave 53 (Opt 55 / TOTK_VK_FG_WB_FAST=1) unit tests, fault injection and benchmark. */
#ifndef TEST_VK_KTGL_OPT_W53_H
#define TEST_VK_KTGL_OPT_W53_H
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "vk_fg_wb_fast.h"
#ifdef TOTK_W53_STANDALONE
int g_fault_mode = 0;
#endif

_Atomic int g_totk_vk_fg_wb_fast_override = -1;
_Atomic int g_totk_w53_fault = -1;
static int g_w53_fail = 0;

typedef struct { uint32_t dtype, kind; int32_t reg; uint8_t rw; } w53_bind_t;
typedef struct { uint64_t back, addr, size; uint32_t mip, lay, asp; } w53_watch_t;

#define W53_EXPECT_EQ(m, w, g) do { \
    uint64_t wa_ = (uint64_t)(w), ga_ = (uint64_t)(g); \
    if (wa_ != ga_) { \
        g_w53_fail++; \
        fprintf(stderr, "FAIL %s:%d %s want=%llu got=%llu\n", __FILE__, __LINE__, m, \
                (unsigned long long)wa_, (unsigned long long)ga_); \
    } \
} while (0)

static uint64_t w53_ns(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static void w53_test_fg_sum_watch_scene(void) {
    w53_bind_t bs[8] = {
        {6u, 0u, 1, 0}, {8u, 1u, 1, 0}, {7u, 2u, 0, 1}, {3u, 3u, 2, 1},
        {7u, 2u, 3, 0}, {3u, 3u, 4, 1}, {7u, 2u, 5, 1}, {2u, 1u, 7, 1}
    };
    totk_sh_fg_sum_t fs = {0};
    for (int i = 0; i < 8; i++) {
        int sbuf = bs[i].dtype == 7u, simg = bs[i].dtype == 3u;
        int cam = (bs[i].dtype == 6u || bs[i].dtype == 7u) && (bs[i].kind == 0u || bs[i].kind == 1u);
        totk_sh_fg_sum_add(&fs, i, bs[i].rw, simg, sbuf, cam, bs[i].reg);
    }
    totk_sh_fg_sum_finish(&fs, 8);
    W53_EXPECT_EQ("cam1", 1u, totk_sh_fg_reads_cam(&fs));
    W53_EXPECT_EQ("rw_st0", (1u << 2) | (1u << 3) | (1u << 5) | (1u << 6), totk_sh_fg_rw_mask(&fs, 0u));
    W53_EXPECT_EQ("rw_st5", (1u << 2) | (1u << 6), totk_sh_fg_rw_mask(&fs, 5u));

    totk_fgx_watch_idx_t *wi = calloc(1, sizeof *wi);
    totk_fgx_scene_idx_t *si = calloc(1, sizeof *si);
    assert(wi && si);
    totk_fgx_watch_begin(wi);
    w53_watch_t ws[16];
    for (int i = 0; i < 16; i++) {
        ws[i] = (w53_watch_t){0x1000u + (uint64_t)(i & 7), 0x40000000u + (uint64_t)i * 0x100000u, 0x10000u, (uint32_t)(i >> 3), 0u, 1u};
        totk_fgx_watch_add_entry(wi, (uint32_t)i, ws[i].back, ws[i].mip, ws[i].lay, ws[i].asp, ws[i].addr, ws[i].size);
    }
    wi->synced_n = 16u;
    W53_EXPECT_EQ("wfind_0", 0, totk_fgx_watch_find_key(wi, ws[0].back, ws[0].mip, ws[0].lay, ws[0].asp));
    W53_EXPECT_EQ("wfind_11", 11, totk_fgx_watch_find_key(wi, ws[11].back, ws[11].mip, ws[11].lay, ws[11].asp));
    W53_EXPECT_EQ("w_ovl_hit", 1, totk_fgx_watch_may_overlap(wi, ws[5].addr + 4096u, 4096u));
    W53_EXPECT_EQ("w_ovl_miss", 0, totk_fgx_watch_may_overlap(wi, 0x90000000u, 4096u));
    int h = totk_fgx_watch_head(wi, ws[10].back);
    W53_EXPECT_EQ("whead_has", h >= 0 ? 1u : 0u, 1u);

    totk_fgx_scene_begin(si);
    for (int i = 0; i < 8; i++) totk_fgx_scene_add(si, (uint32_t)i, 0x5000u + (uint64_t)(i & 3));
    si->synced_n = 8u;
    int sh = totk_fgx_scene_head(si, 0x5002u);
    W53_EXPECT_EQ("shead_top", 6, sh);
    free(wi); free(si);
}

static void w53_test_wb_and_arena(void) {
    totk_wb_bucket_dir_t *wd = calloc(1, sizeof *wd);
    assert(wd != NULL);
    totk_wb_dir_landing_reset(wd);
    for (uint32_t i = 0; i < 12; i++) {
        totk_wb_dir_set(wd, i, UINT64_C(0x30000000) + (uint64_t)i * UINT64_C(0x200000), 4096u, i + 1u);
    }
    uint64_t out[4] = {0};
    W53_EXPECT_EQ("wb_q_ok", 1, totk_wb_dir_query(wd, UINT64_C(0x30000000) + UINT64_C(5) * UINT64_C(0x200000), 4096u, out));
    W53_EXPECT_EQ("wb_q_bit", 1u, (out[0] >> 5) & 1u);
    totk_wb_dir_remove_swap(wd, 2u, 11u);
    memset(out, 0, sizeof out);
    W53_EXPECT_EQ("wb_swapped_ok", 1, totk_wb_dir_query(wd, UINT64_C(0x30000000) + UINT64_C(11) * UINT64_C(0x200000), 4096u, out));
    W53_EXPECT_EQ("wb_swapped_slot2", 1u, (out[0] >> 2) & 1u);
    totk_wb_dir_landing_add(wd, 0x400000u, 8192u);
    W53_EXPECT_EQ("land_lo", 0x400000u, wd->landing_lo);
    free(wd);

    totk_arena_buf_map_t am;
    totk_arena_bmap_clear(&am);
    for (int i = 0; i < 8; i++) totk_arena_bmap_put(&am, 0x8000u + (uint64_t)i * 16u, i);
    totk_arena_bmap_swap(&am, 0x8020u, 7, 0x8070u, 2);
    int ov = 0;
    W53_EXPECT_EQ("abm_2", 7, totk_arena_bmap_find(&am, 0x8020u, &ov));
    W53_EXPECT_EQ("abm_7", 2, totk_arena_bmap_find(&am, 0x8070u, &ov));
    W53_EXPECT_EQ("abm_miss", -1, totk_arena_bmap_find(&am, 0x9999u, &ov));
}

static void w53_bench_run(void) {
    enum { NQ = 15000, REP = 3 };
    totk_sh_fg_sum_t fs = {0};
    w53_bind_t bs[32];
    for (int i = 0; i < 32; i++) {
        bs[i] = (w53_bind_t){i == 0 ? 6u : (i & 3) ? 8u : 7u, 0u, i == 0 ? 1 : i, (i % 7) == 0};
        totk_sh_fg_sum_add(&fs, i, bs[i].rw, 0, bs[i].dtype == 7u, bs[i].dtype == 6u, bs[i].reg);
    }
    totk_sh_fg_sum_finish(&fs, 32);
    totk_fgx_watch_idx_t *wi = calloc(1, sizeof *wi);
    totk_wb_bucket_dir_t *wd = calloc(1, sizeof *wd);
    w53_watch_t *ws = calloc(128, sizeof *ws);
    assert(wi && wd && ws);
    totk_fgx_watch_begin(wi);
    for (uint32_t i = 0; i < 128u; i++) {
        ws[i] = (w53_watch_t){0x10000u + i, UINT64_C(0x10000000) + i * UINT64_C(0x100000), 4096u, 0u, 0u, 1u};
        totk_fgx_watch_add_entry(wi, i, ws[i].back, 0u, 0u, 1u, ws[i].addr, 4096u);
        totk_wb_dir_set(wd, i, ws[i].addr, 4096u, i + 1u);
    }
    wi->synced_n = 128u;
    totk_arena_buf_map_t am;
    totk_arena_bmap_clear(&am);
    uint64_t abufs[16];
    for (int i = 0; i < 16; i++) { abufs[i] = 0xa0000u + (uint64_t)i * 64u; totk_arena_bmap_put(&am, abufs[i], i); }

    uint64_t s0 = 0u, s1 = 0u, bs_ns = UINT64_MAX, bf_ns = UINT64_MAX;
    for (int r = 0; r < REP; r++) {
        uint64_t t0 = w53_ns(), a0 = 0u;
        for (int q = 0; q < NQ; q++) {
            for (int i = 0; i < 32; i++) if (bs[i].rw && bs[i].dtype == 7u) a0 += (uint64_t)i;
            uint64_t qa = UINT64_C(0x50000000) + (uint64_t)(q & 63) * 4096u;
            for (int i = 0; i < 128; i++) if (ws[i].addr < qa + 4096u && qa < ws[i].addr + 4096u) a0++;
            uint64_t wantb = abufs[q & 15];
            for (int i = 0; i < 16; i++) if (abufs[i] == wantb) { a0 += (uint64_t)i; break; }
        }
        uint64_t t1 = w53_ns(), a1 = 0u;
        for (int q = 0; q < NQ; q++) {
            uint64_t m = totk_sh_fg_rw_mask(&fs, 0u);
            while (m) { unsigned b = (unsigned)__builtin_ctzll(m); m &= m - 1u; a1 += b; }
            uint64_t qa = UINT64_C(0x50000000) + (uint64_t)(q & 63) * 4096u;
            if (totk_fgx_watch_may_overlap(wi, qa, 4096u)) a1++;
            int ov = 0; int ci = totk_arena_bmap_find(&am, abufs[q & 15], &ov);
            if (ci >= 0) a1 += (uint64_t)(unsigned)ci;
        }
        uint64_t t2 = w53_ns();
        s0 = a0; s1 = a1;
        if (t1 - t0 < bs_ns) bs_ns = t1 - t0;
        if (t2 - t1 < bf_ns) bf_ns = t2 - t1;
    }
    W53_EXPECT_EQ("bench_sink", s0, s1);
    double sms = (double)bs_ns / 1e6, fms = (double)bf_ns / 1e6;
    printf("ktglopttest w53: slow=%.3f ms fast=%.3f ms speedup=%.2fx sink=%llu failures=%d\n",
           sms, fms, sms / (fms > 0.001 ? fms : 0.001), (unsigned long long)s1, g_w53_fail);
    free(wi); free(wd); free(ws);
}

static int run_ktgl_w53_tests(int fault_mode) {
    if (fault_mode > 0) totk_w53_set_fault(fault_mode);
    g_w53_fail = 0;
    totk_vk_fg_wb_fast_set_for_test(1);
    w53_test_fg_sum_watch_scene();
    w53_test_wb_and_arena();
    w53_bench_run();
    return g_w53_fail != 0;
}
#ifdef TOTK_W53_STANDALONE
int main(void) {
    return run_ktgl_w53_tests(totk_w53_fault_mode());
}
#endif
#endif
