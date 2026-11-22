#ifndef TEST_VK_KTGL_OPT_W50_H
#define TEST_VK_KTGL_OPT_W50_H
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "vk_sig_fast.h"
#ifdef TOTK_W50_STANDALONE
int g_fault_mode = 0;
#endif

_Atomic int g_totk_vk_sig_fast_override = -1;
_Atomic int g_totk_w50_fault = -1;
static int g_w50_fail = 0;

#define W50_EXPECT_EQ(m, w, g) do { \
    uint64_t wa_ = (uint64_t)(w), ga_ = (uint64_t)(g); \
    if (wa_ != ga_) { \
        g_w50_fail++; \
        fprintf(stderr, "FAIL %s:%d %s want=%llu got=%llu\n", __FILE__, __LINE__, m, \
                (unsigned long long)wa_, (unsigned long long)ga_); \
    } \
} while (0)

static uint64_t w50_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

typedef struct {
    int ok, f, tg;
} w50_mock_reg;

static uint64_t w50_ref_stage_tex_dm(const uint64_t tex[128], int hi, const w50_mock_reg *reg, int nreg) {
    uint64_t dm = 0u;
    for (int r = 0; r < hi && r < 128; r++) if (tex[r]) {
        int tid = (int)(tex[r] & 0xfffffu);
        if (tid <= 0 || tid >= nreg || !reg[tid].ok) continue;
        int f = reg[tid].f;
        if (f >= 0x32 && f <= 0x36) dm ^= UINT64_C(0x9e3779b97f4a7c15) * (uint64_t)(r + 1);
    }
    return dm;
}

static uint64_t w50_fast_stage_tex_dm(const uint64_t tex[128], const uint64_t nz[2], int hi,
                                      totk_treg_depth_cache_t *c, uint64_t rev, const w50_mock_reg *reg, int nreg) {
    uint64_t w[2], dm = 0u;
    totk_stage_tex_hi_words(nz, hi, w);
    for (int half = 0; half < 2; half++) {
        uint64_t m = w[half];
        while (m) {
            unsigned r = (unsigned)half * 64u + (unsigned)__builtin_ctzll(m);
            m &= m - UINT64_C(1);
            int tid = (int)(tex[r] & 0xfffffu), d = 0;
            if (!totk_treg_depth_peek(c, tid, rev, &d)) {
                d = (tid > 0 && tid < nreg && reg[tid].ok && totk_is_depth_fmt_fast(reg[tid].f)) ? 1 : 0;
                totk_treg_depth_put(c, tid, d);
            }
            if (d) dm ^= UINT64_C(0x9e3779b97f4a7c15) * (uint64_t)(r + 1u);
        }
    }
    return dm;
}

static void w50_test_sig_and_cache(void) {
    static w50_mock_reg reg[128];
    uint64_t tex[128] = {0}, nz[2] = {0};
    uint8_t ready = 1u;
    for (int i = 1; i < 64; i++) reg[i] = (w50_mock_reg){1, (i & 7) == 0 ? 0x36 : (0x10 + (i & 7)), 1};
    uint64_t b0[4] = {2u, 0u, 8u, 11u}, b1[4] = {16u, 5u, 0u, 24u};
    memcpy(&tex[2], b0, sizeof b0);
    totk_tex_nz_update(nz, &ready, 2, b0, 4);
    memcpy(&tex[96], b1, sizeof b1);
    totk_tex_nz_update(nz, &ready, 96, b1, 4);
    totk_treg_depth_cache_t cache;
    memset(&cache, 0, sizeof cache);
    W50_EXPECT_EQ("dm_hi128", w50_ref_stage_tex_dm(tex, 128, reg, 128), w50_fast_stage_tex_dm(tex, nz, 128, &cache, 1u, reg, 128));
    W50_EXPECT_EQ("dm_hi64", w50_ref_stage_tex_dm(tex, 64, reg, 128), w50_fast_stage_tex_dm(tex, nz, 64, &cache, 1u, reg, 128));
    reg[11].f = 0x33;
    W50_EXPECT_EQ("dm_rev2", w50_ref_stage_tex_dm(tex, 128, reg, 128), w50_fast_stage_tex_dm(tex, nz, 128, &cache, 2u, reg, 128));
    uint64_t z0[2] = {0u, 0u};
    memcpy(&tex[96], z0, sizeof z0);
    totk_tex_nz_update(nz, &ready, 96, z0, 2);
    W50_EXPECT_EQ("dm_clear96", w50_ref_stage_tex_dm(tex, 128, reg, 128), w50_fast_stage_tex_dm(tex, nz, 128, &cache, 2u, reg, 128));
}

static void w50_test_alt_and_vin(void) {
    totk_alt_map_t m;
    totk_alt_map_clear(&m);
    uint64_t sigs[24];
    for (int i = 0; i < 24; i++) {
        sigs[i] = i == 0 ? 0u : (UINT64_C(0x12340000) << 20) | (uint64_t)(i * 0x101u);
        totk_alt_map_put(&m, sigs[i], (uint16_t)(i + 1));
    }
    W50_EXPECT_EQ("alt_hit_0", 0, totk_alt_map_find(&m, sigs[0]));
    W50_EXPECT_EQ("alt_hit_23", 23, totk_alt_map_find(&m, sigs[23]));
    uint64_t high_diff = sigs[5];
    uint32_t h5 = totk_alt_hash(sigs[5]);
    for (uint64_t k = 1u; k < 4096u; k++) {
        uint64_t cand = sigs[5] ^ (k << 32);
        if (totk_alt_hash(cand) == h5) { high_diff = cand; break; }
    }
    W50_EXPECT_EQ("alt_coll_found", 1, high_diff != sigs[5] && totk_alt_hash(high_diff) == h5);
    W50_EXPECT_EQ("alt_miss_high", -1, totk_alt_map_find(&m, high_diff));
    uint32_t c = totk_vin_cand_mask_fast(0xffffu, 14);
    W50_EXPECT_EQ("vin_cand_14", 0x3fffu, c);
}

static void *w50_tsan_worker(void *arg) {
    const uint64_t *sigs = (const uint64_t *)arg;
    totk_alt_map_t local;
    totk_alt_map_clear(&local);
    for (int i = 0; i < 16; i++) totk_alt_map_put(&local, sigs[i], (uint16_t)(i + 1));
    for (int rep = 0; rep < 2000; rep++) {
        W50_EXPECT_EQ("tsan_alt", rep & 15, totk_alt_map_find(&local, sigs[rep & 15]));
        W50_EXPECT_EQ("tsan_vin", 0x0fffu, totk_vin_cand_mask_fast(0xffffu, 12));
    }
    return NULL;
}

static void w50_test_tsan(void) {
    uint64_t sigs[16];
    for (int i = 0; i < 16; i++) sigs[i] = UINT64_C(0xabc0000000000001) ^ ((uint64_t)i * UINT64_C(0x9e3779b97f4a7c15));
    pthread_t th[4];
    for (int t = 0; t < 4; t++) pthread_create(&th[t], NULL, w50_tsan_worker, sigs);
    for (int t = 0; t < 4; t++) pthread_join(th[t], NULL);
}

static uint64_t w50_bench_pass(int fast, double *ms_out, const uint64_t tex[2][128], const uint64_t nz[2][2],
                               const w50_mock_reg *reg, const uint64_t alts[32], const totk_alt_map_t *amap) {
    enum { ITERS = 120000u };
    totk_treg_depth_cache_t cache;
    memset(&cache, 0, sizeof cache);
    uint64_t sink = 0u, t0 = w50_now_ns();
    for (uint32_t i = 0u; i < ITERS; i++) {
        uint32_t row = i & 1u, ai = (i * 5u) & 31u;
        if (fast) {
            sink += w50_fast_stage_tex_dm(tex[row], nz[row], 128, &cache, 7u, reg, 128);
            int idx = totk_alt_map_find(amap, alts[ai]);
            if (idx >= 0) sink += (uint64_t)idx;
        } else {
            sink += w50_ref_stage_tex_dm(tex[row], 128, reg, 128);
            for (int k = 0; k < 32; k++) if (alts[k] == alts[ai]) { sink += (uint64_t)k; break; }
        }
    }
    *ms_out = (double)(w50_now_ns() - t0) / 1e6;
    return sink;
}

static int run_ktgl_w50_tests(int fault_mode) {
    if (fault_mode >= 196 && fault_mode <= 200) totk_w50_set_fault(fault_mode - 195);
    else if (fault_mode >= 1 && fault_mode <= 5) totk_w50_set_fault(fault_mode);
    g_w50_fail = 0;
    totk_vk_sig_fast_set_for_test(1); W50_EXPECT_EQ("w50_sw1", 1, totk_vk_sig_fast_on());
    totk_vk_sig_fast_set_for_test(0); W50_EXPECT_EQ("w50_sw0", 0, totk_vk_sig_fast_on());
    totk_vk_sig_fast_set_for_test(1);
    w50_test_sig_and_cache();
    w50_test_alt_and_vin();
    w50_test_tsan();
    static w50_mock_reg reg[128];
    uint64_t tex[2][128] = {{0}}, nz[2][2] = {{0}}, alts[32];
    for (int i = 1; i < 128; i++) reg[i] = (w50_mock_reg){1, (i == 90) ? 0x34 : 0x19, 1};
    uint8_t r0 = 1u, r1 = 1u;
    uint64_t h0[4] = {12u, 22u, 90u, 33u}, h1[4] = {15u, 25u, 45u, 90u};
    memcpy(&tex[0][4], h0, sizeof h0); totk_tex_nz_update(nz[0], &r0, 4, h0, 4);
    memcpy(&tex[1][72], h1, sizeof h1); totk_tex_nz_update(nz[1], &r1, 72, h1, 4);
    totk_alt_map_t amap;
    totk_alt_map_clear(&amap);
    for (int i = 0; i < 32; i++) {
        alts[i] = UINT64_C(0x55aa000000000001) + (uint64_t)i * UINT64_C(0x100000007);
        totk_alt_map_put(&amap, alts[i], (uint16_t)(i + 1));
    }
    double s_ms = 0.0, f_ms = 0.0;
    uint64_t s_sink = w50_bench_pass(0, &s_ms, tex, nz, reg, alts, &amap);
    uint64_t f_sink = w50_bench_pass(1, &f_ms, tex, nz, reg, alts, &amap);
    W50_EXPECT_EQ("w50_bench_sink", s_sink, f_sink);
    printf("aoc_w50_sig_fast: slow=%.3f ms fast=%.3f ms speedup=%.2fx sink=%llu failures=%d\n",
           s_ms, f_ms, f_ms > 0.0 ? s_ms / f_ms : 0.0, (unsigned long long)f_sink, g_w50_fail);
    return g_w50_fail ? 1 : 0;
}

#ifdef TOTK_W50_STANDALONE
int main(void) {
    int fm = totk_w50_fault_mode();
    return run_ktgl_w50_tests(fm);
}
#endif
#endif
