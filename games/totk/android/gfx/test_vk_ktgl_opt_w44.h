#ifndef TEST_VK_KTGL_OPT_W44_H
#define TEST_VK_KTGL_OPT_W44_H
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include "vk_gpuprof_fast.h"
_Atomic int g_totk_vk_gpuprof_fast_override = -1;
_Atomic int g_totk_w44_fault = -1;
static int g_w44_fail = 0;
#define W44_EXPECT_EQ(m, w, g) do { uint64_t wa_=(uint64_t)(w), ga_=(uint64_t)(g); if (wa_!=ga_) { g_w44_fail++; fprintf(stderr,"FAIL %s:%d %s want=%llu got=%llu\n",__FILE__,__LINE__,m,(unsigned long long)wa_,(unsigned long long)ga_); } } while(0)
static uint64_t w44_now_ns(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec; }
static uint64_t ref_fnv_bytes(uint64_t h, const void *d, size_t n) { const uint8_t *p = (const uint8_t *)d; for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= UINT64_C(1099511628211); } return h; }
static uint64_t ref_key_hash(uint64_t sh, uint32_t w, uint32_t h, uint32_t smp, uint32_t col, uint32_t cmp, const vk_gp_att_t a[10]) {
    uint64_t ac = ref_fnv_bytes(UINT64_C(14695981039346656037), &sh, 8);
    const uint32_t f[5] = {w, h, smp, col, cmp}; ac = ref_fnv_bytes(ac, f, sizeof f);
    for (unsigned j = 0; j < 10; j++) { const uint32_t v[5] = {a[j].format, a[j].w, a[j].h, a[j].load, a[j].store}; ac = ref_fnv_bytes(ac, v, sizeof v); }
    return ac;
}
static int ref_key_eq(uint64_t as, uint32_t aw, uint32_t ah, uint32_t asmp, uint32_t ac, uint32_t acmp, const vk_gp_att_t aa[10],
        uint64_t bs, uint32_t bw, uint32_t bh, uint32_t bsmp, uint32_t bc, uint32_t bcmp, const vk_gp_att_t ba[10]) {
    if (aw!=bw || ah!=bh || as!=bs || asmp!=bsmp || ac!=bc || acmp!=bcmp) return 0;
    for (unsigned j=0; j<10; j++) if (aa[j].format!=ba[j].format||aa[j].w!=ba[j].w||aa[j].h!=ba[j].h||aa[j].load!=ba[j].load||aa[j].store!=ba[j].store) return 0;
    return 1;
}
static uint32_t ref_texel_bytes(uint32_t f) {
    static const uint8_t r[17][3] = {
        {2,8,2},{9,15,1},{16,22,2},{23,36,3},{37,69,4},{70,76,2},{77,83,4},{84,90,6},
        {91,97,8},{98,100,4},{101,103,8},{104,106,12},{107,109,16},{122,123,4},{124,124,2},{125,126,4},{127,127,1}
    };
    for (size_t i = 0u; i < 17u; i++) if (f >= r[i][0] && f <= r[i][1]) return r[i][2];
    return 0u;
}

static int w44_ref_rank_cmp(const void *a, const void *b) {
    const vk_gp_rank_item_t *x = (const vk_gp_rank_item_t *)a, *y = (const vk_gp_rank_item_t *)b;
    return x->ms < y->ms ? 1 : x->ms > y->ms ? -1 : x->hash < y->hash ? -1 : x->hash > y->hash;
}
static uint32_t ref_pipe_pop(uint32_t prev, uint32_t obs) { while (prev < obs && prev < (UINT32_C(1) << 31)) prev <<= 1; return prev; }
static int ref_pipe_prim(int p, uint32_t c, uint32_t inst, int idx, uint64_t *out) {
    if (idx && (p == 3 || p == 5)) return 0;
    uint64_t n = p==0 ? c : p==1 ? c/2 : p==3 ? (c>1?c-1:0) : (p==5||p==6) ? (c>2?c-2:0) : p==7 ? (idx?c/3:c/4*2) : c/3;
    *out = n * inst; return 1;
}
static void w44_test_hash_and_texel(void) {
    vk_gp_att_t a[10], b[10]; uint64_t rng = UINT64_C(0x123456789abcdef1);
    for (unsigned tc = 0; tc < 64u; tc++) {
        memset(a, 0, sizeof a); unsigned active = tc % 4u;
        for (unsigned j = 0; j < active; j++) {
            rng ^= rng >> 13; rng ^= rng << 7; rng ^= rng >> 17;
            a[j] = (vk_gp_att_t){37u + (uint32_t)(tc % 90u), 1280u, 720u, tc & 3u, (tc >> 1) & 3u, 0u};
        }
        if (tc & 8u) a[8] = (vk_gp_att_t){126u, 1280u, 720u, 1u, 0u, 0u};
        if (tc & 16u) a[9] = (vk_gp_att_t){127u, 640u, 360u, 0u, 2u, 0u};
        W44_EXPECT_EQ("key_hash", ref_key_hash(rng, 1280u, 720u, 1u, active, tc == 0u, a),
            vk_gp_key_hash_fast(rng, 1280u, 720u, 1u, active, tc == 0u, a));
        memcpy(b, a, sizeof b);
        W44_EXPECT_EQ("key_eq same", 1u, (uint64_t)vk_gp_key_eq_fast(rng, 1280u, 720u, 1u, active, 0u, a, rng, 1280u, 720u, 1u, active, 0u, b));
        b[8].store ^= 1u;
        W44_EXPECT_EQ("key_eq depth diff", (uint64_t)ref_key_eq(rng, 1280u, 720u, 1u, active, 0u, a, rng, 1280u, 720u, 1u, active, 0u, b),
            (uint64_t)vk_gp_key_eq_fast(rng, 1280u, 720u, 1u, active, 0u, a, rng, 1280u, 720u, 1u, active, 0u, b));
    }
    for (uint32_t f = 0u; f < 160u; f++) W44_EXPECT_EQ("texel", ref_texel_bytes(f), vk_gp_texel_bytes_fast(f));
    W44_EXPECT_EQ("texel big", 0u, vk_gp_texel_bytes_fast(1000000001u));
}

static void w44_test_rank_draw_pipe(void) {
    vk_gp_rank_item_t ref[120], got[120], top[64];
    for (size_t i = 0; i < 120u; i++) { ref[i] = (vk_gp_rank_item_t){(double)(i % 15u) * 0.25, UINT64_C(0x1000) - (uint64_t)(i * 7u), (uint32_t)i, 0u}; got[i] = ref[i]; }
    qsort(ref, 120u, sizeof ref[0], w44_ref_rank_cmp); vk_gp_rank_sort_fast(got, 120u);
    for (size_t i = 0; i < 120u; i++) W44_EXPECT_EQ("rank idx", ref[i].idx, got[i].idx);
    W44_EXPECT_EQ("top64 count", 64u, vk_gp_rank_top64_fast(ref, 120u, top));
    for (size_t i = 0; i < 64u; i++) W44_EXPECT_EQ("top64 idx", ref[i].idx, top[i].idx);
    const char *keys[6] = {"vs_main_hash0", "fs_alpha_test", NULL, NULL, NULL, "cs_post"};
    uint64_t rh = UINT64_C(14695981039346656037);
    for (int i = 0; i < 6; i++) if (keys[i]) { rh = ref_fnv_bytes(rh, &i, sizeof i); rh = ref_fnv_bytes(rh, keys[i], strlen(keys[i]) + 1u); }
    W44_EXPECT_EQ("draw shaders", rh, vk_gp_draw_shaders_hash_fast(keys));
    static const uint32_t obs[6] = {1024u, 2048u, 2049u, 4096u, 8193u, UINT32_C(0x90000000)};
    for (size_t i = 0; i < 6u; i++) W44_EXPECT_EQ("pipe pop", ref_pipe_pop(2048u, obs[i]), vk_gp_pipe_population_fast(2048u, obs[i]));
    for (int p = -1; p <= 9; p++) for (int idx = 0; idx <= 1; idx++) {
        uint64_t o0 = 99u, o1 = 99u, w0 = 99u, w1 = 99u;
        int r0 = ref_pipe_prim(p, 1003u, 3u, idx, &o0), r1 = vk_gp_pipe_primitives_fast(p, 1003u, 3u, idx, &o1);
        int q0 = ref_pipe_prim(p, UINT32_MAX, 0x10000u, idx, &w0), q1 = vk_gp_pipe_primitives_fast(p, UINT32_MAX, 0x10000u, idx, &w1);
        W44_EXPECT_EQ("prim rc", (uint64_t)r0, (uint64_t)r1); W44_EXPECT_EQ("prim out", o0, o1);
        W44_EXPECT_EQ("prim64 rc", (uint64_t)q0, (uint64_t)q1); W44_EXPECT_EQ("prim64 out", w0, w1);
    }
}
static void *w44_tsan_fn(void *arg) {
    const vk_gp_att_t *a = (const vk_gp_att_t *)arg;
    const char *keys[6] = {"vs_tsan", "fs_tsan", NULL, NULL, NULL, NULL}; uint64_t s = 0u;
    for (uint32_t i = 0; i < 1000u; i++) {
        s += vk_gp_key_hash_fast(i, 1280u, 720u, 1u, 1u, 0u, a) + vk_gp_draw_shaders_hash_fast(keys) + vk_gp_texel_bytes_fast(37u + (i & 63u));
    }
    return (void *)(uintptr_t)s;
}

static uint64_t w44_bench_arm(int fast, double *ms_out) {
    vk_gp_att_t a[10], b[10]; memset(a, 0, sizeof a); memset(b, 0, sizeof b);
    a[0] = (vk_gp_att_t){37u, 1280u, 720u, 1u, 0u, 0u}; a[8] = (vk_gp_att_t){126u, 1280u, 720u, 1u, 0u, 0u}; memcpy(b, a, sizeof b);
    vk_gp_rank_item_t items[64]; uint64_t acc = 0u;
    const char *keys[6] = {"vs_ktgl_shadow_pass_01", "fs_ktgl_pbr_deferred_02", NULL, NULL, NULL, NULL};
    uint64_t t0 = w44_now_ns();
    for (uint32_t it = 0; it < 20000u; it++) {
        a[0].w = 1280u + (it & 15u); b[0].w = a[0].w;
        if (fast) {
            acc += vk_gp_key_hash_fast(it, 1280u, 720u, 1u, 1u, 0u, a) + (uint64_t)vk_gp_key_eq_fast(it, 1280u, 720u, 1u, 1u, 0u, a, it, 1280u, 720u, 1u, 1u, 0u, b);
            acc += vk_gp_texel_bytes_fast(37u + (it & 63u)) + vk_gp_draw_shaders_hash_fast(keys) + vk_gp_pipe_population_fast(2048u, 2049u + (it & 8191u));
        } else {
            acc += ref_key_hash(it, 1280u, 720u, 1u, 1u, 0u, a) + (uint64_t)ref_key_eq(it, 1280u, 720u, 1u, 1u, 0u, a, it, 1280u, 720u, 1u, 1u, 0u, b);
            acc += ref_texel_bytes(37u + (it & 63u)) + ref_pipe_pop(2048u, 2049u + (it & 8191u));
            uint64_t h = UINT64_C(14695981039346656037);
            for (int i = 0; i < 6; i++) if (keys[i]) { h = ref_fnv_bytes(h, &i, sizeof i); h = ref_fnv_bytes(h, keys[i], strlen(keys[i]) + 1u); }
            acc += h;
        }
        if ((it & 255u) == 0u) {
            for (size_t k = 0; k < 64u; k++) items[k] = (vk_gp_rank_item_t){(double)((it + k) & 31u), k, (uint32_t)k, 0u};
            if (fast) vk_gp_rank_sort_fast(items, 64u); else qsort(items, 64u, sizeof items[0], w44_ref_rank_cmp);
            acc += items[0].idx;
        }
    }
    *ms_out = (double)(w44_now_ns() - t0) / 1e6;
    return acc;
}

static int run_ktgl_w44_tests(int fault_mode) {
    g_w44_fail = 0;
    int f = fault_mode; const char *envf = getenv("TOTK_W44_FAULT");
    if (envf && *envf) f = atoi(envf); else if (f >= 166 && f <= 170) f = f - 165; else f = 0;
    totk_w44_set_fault(f); totk_vk_gpuprof_fast_set_for_test(1);
    w44_test_hash_and_texel(); w44_test_rank_draw_pipe();
    vk_gp_att_t a[10]; memset(a, 0, sizeof a); a[0] = (vk_gp_att_t){37u, 1280u, 720u, 1u, 0u, 0u};
    pthread_t th[4]; for (int i = 0; i < 4; i++) pthread_create(&th[i], NULL, w44_tsan_fn, a);
    for (int i = 0; i < 4; i++) pthread_join(th[i], NULL);
    double bms = 0.0, fms = 0.0; uint64_t s0 = w44_bench_arm(0, &bms), s1 = w44_bench_arm(1, &fms);
    W44_EXPECT_EQ("bench sink eq", s0, s1);
    totk_vk_gpuprof_fast_set_for_test(-1); totk_w44_set_fault(-1);
    printf("aoc_w44_gpuprof_fast: base_ms=%.3f fast_ms=%.3f speedup=%.2fx sink=%llu %s\n",
           bms, fms, bms / (fms > 0.0 ? fms : 1e-6), (unsigned long long)s1, g_w44_fail ? "FAIL" : "PASS");
    return g_w44_fail != 0;
}
#if defined(TOTK_W44_STANDALONE)
int main(void) { return run_ktgl_w44_tests(0); }
#endif
#endif
