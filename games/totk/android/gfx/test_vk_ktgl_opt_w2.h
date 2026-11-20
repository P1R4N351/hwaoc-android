#ifndef TOTK_TEST_VK_KTGL_OPT_W2_H
#define TOTK_TEST_VK_KTGL_OPT_W2_H
/* Wave 2 unit, benchmark, and Delta #27 fault-injection tests for movie_nv12.h and vk_ktgl_memo.h. */
#include "vk_ktgl_memo.h"
#include <pthread.h>
#include "../../runtime/movie_nv12.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint64_t host_now_ns(void);

static int w2_check_nv12_size(int w, int h, int ys, int uvs, int swap_uv, int fault_mode) {
    size_t ysz = (size_t)ys * (size_t)h, uvsz = (size_t)uvs * (size_t)(h / 2);
    size_t osz = (size_t)w * (size_t)h * 3u / 2u;
    uint8_t *y = malloc(ysz), *u = malloc(uvsz), *v = malloc(uvsz), *ref = malloc(osz), *got = malloc(osz);
    assert(y && u && v && ref && got);
    for (int r = 0; r < h; r++) memset(y + (size_t)r * (size_t)ys, (r * 7) & 0xff, (size_t)w);
    for (int r = 0; r < h / 2; r++) {
        for (int c = 0; c < w / 2; c++) {
            u[(size_t)r * (size_t)uvs + (size_t)c] = (uint8_t)((0x10 + r + c * 3) & 0xff);
            v[(size_t)r * (size_t)uvs + (size_t)c] = (uint8_t)((0x80 + r * 5 + c) & 0xff);
        }
    }
    totk_movie_to_nv12(y, ys, u, uvs, v, uvs, ref, w, h, swap_uv, 0);
    int bad_swap = swap_uv ^ (fault_mode == 3 ? 1 : 0);
    totk_movie_to_nv12(y, ys, u, uvs, v, uvs, got, w, h, bad_swap, 1);
    int eq = (memcmp(ref, got, osz) == 0);
    free(y); free(u); free(v); free(ref); free(got);
    return eq;
}

static void w2_bench_nv12(int w, int h, int iters) {
    size_t ysz = (size_t)w * (size_t)h, uvsz = (size_t)(w / 2) * (size_t)(h / 2);
    uint8_t *y = malloc(ysz), *u = malloc(uvsz), *v = malloc(uvsz), *dst = malloc(ysz + uvsz * 2u);
    assert(y && u && v && dst);
    memset(y, 0x41, ysz); memset(u, 0x62, uvsz); memset(v, 0x93, uvsz); memset(dst, 0, ysz + uvsz * 2u);
    totk_movie_to_nv12(y, w, u, w / 2, v, w / 2, dst, w, h, 0, 1);
    uint64_t t0 = host_now_ns();
    for (int i = 0; i < iters; i++) { y[0] = (uint8_t)i; totk_movie_to_nv12(y, w, u, w / 2, v, w / 2, dst, w, h, 0, 0); }
    uint64_t base_ns = host_now_ns() - t0;
    t0 = host_now_ns();
    for (int i = 0; i < iters; i++) { y[0] = (uint8_t)i; totk_movie_to_nv12(y, w, u, w / 2, v, w / 2, dst, w, h, 0, 1); }
    uint64_t fast_ns = host_now_ns() - t0;
    printf("aoc_nv12[%dx%d]: iters=%d base_us=%.2f fast_us=%.2f speedup=%.2fx (dst[0]=%u)\n",
           w, h, iters, (double)base_ns / iters / 1e3, (double)fast_ns / iters / 1e3,
           fast_ns ? (double)base_ns / (double)fast_ns : 0.0, (unsigned)dst[0]);
    free(y); free(u); free(v); free(dst);
}

static uint64_t w2_bench_memo_base(const uint64_t *serials, const uintptr_t *vs, const uintptr_t *fs,
                                   int n_prog, int n_draw, int n_frames, uint64_t *misses_out) {
    static struct { uint64_t serial; void *pr; } mod64[64];
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    memset(mod64, 0, sizeof mod64);
    uintptr_t last_vs = 0, last_fs = 0;
    uint64_t last_lay = 0, misses = 0, sink = 0, t0 = host_now_ns();
    for (int f = 0; f < n_frames; f++) {
        for (int d = 0; d < n_draw; d++) {
            int p = d % n_prog;
            uint64_t sr = serials[p];
            unsigned h = (unsigned)(sr % 64u);
            if (mod64[h].serial != sr) {
                pthread_mutex_lock(&mu); misses++;
                mod64[h].serial = sr; mod64[h].pr = (void *)(uintptr_t)(sr + 1);
                pthread_mutex_unlock(&mu);
            }
            uintptr_t cur_vs = vs[p % 200], cur_fs = fs[d % 100]; /* lcm(200,100)=200 pairs */
            if (!last_lay || last_vs != cur_vs || last_fs != cur_fs) {
                uint64_t h2 = 1469598103934665603ull;
                uintptr_t sk[9] = {cur_vs, cur_fs, 0, 0, 0, 0, 0, 0, 0};
                const uint8_t *bp = (const uint8_t *)sk;
                for (size_t i = 0; i < sizeof sk; i++) h2 = (h2 ^ bp[i]) * 1099511628211ull;
                pthread_mutex_lock(&mu); misses++;
                last_vs = cur_vs; last_fs = cur_fs;
                last_lay = h2;
                pthread_mutex_unlock(&mu);
            }
            sink += (uintptr_t)mod64[h].pr ^ last_lay;
        }
    }
    uint64_t dt = host_now_ns() - t0;
    if (!sink) fprintf(stderr, "unexpected 0 sink\n");
    *misses_out = misses;
    return dt;
}

static uint64_t w2_bench_memo_opt(const uint64_t *serials, const uintptr_t *vs, const uintptr_t *fs,
                                  int n_prog, int n_draw, int n_frames, uint64_t *steady_miss_out, int fault_mode) {
    static vk_progc_memo pm;
    static vk_lay_memo lm;
    memset(&pm, 0, sizeof pm);
    memset(&lm, 0, sizeof lm);
    uintptr_t last_vs = 0, last_fs = 0;
    uint64_t last_lay = 0, steady_miss = 0, sink = 0, t0 = host_now_ns();
    for (int f = 0; f < n_frames; f++) {
        for (int d = 0; d < n_draw; d++) {
            int p = d % n_prog;
            uint64_t sr = serials[p];
            void *pr = vk_progc_get(&pm, sr);
            if (!pr) {
                if (f > 0) steady_miss++;
                pr = (void *)(uintptr_t)(sr + 1);
                vk_progc_put(&pm, sr, pr);
            }
            if (fault_mode == 4 && sr != serials[0]) pr = (void *)(uintptr_t)0xbadbeefull;
            uintptr_t cur_vs = vs[p % 200], cur_fs = fs[d % 100]; /* lcm(200,100)=200 pairs */
            if (!last_lay || last_vs != cur_vs || last_fs != cur_fs) {
                uint64_t cached = vk_lay_memo_get(&lm, (const void *)cur_vs, (const void *)cur_fs);
                if (!cached) {
                    if (f > 0) steady_miss++;
                    cached = (uint64_t)(cur_vs ^ cur_fs);
                    vk_lay_memo_put(&lm, (const void *)cur_vs, (const void *)cur_fs, cached);
                }
                last_vs = cur_vs; last_fs = cur_fs; last_lay = cached;
            }
            if ((uintptr_t)pr != (uintptr_t)(sr + 1)) { *steady_miss_out = 999999ull; return 1; }
            sink += (uintptr_t)pr ^ last_lay;
        }
    }
    uint64_t dt = host_now_ns() - t0;
    if (!sink) fprintf(stderr, "unexpected 0 sink\n");
    *steady_miss_out = steady_miss;
    return dt;
}

static int w2_test_barriers_and_peek(int fault_mode) {
    vk_progc_memo pm = {0};
    void *a = (void *)(uintptr_t)0x1111, *b = (void *)(uintptr_t)0x2222;
    uint64_t s1 = 64ull, s2 = 64ull + 512ull;
    vk_progc_put(&pm, s1, a);
    vk_progc_put(&pm, s2, b);
    vk_progc_set_t before = pm.sets[vk_progc_set_idx(s1)];
    void *pa = vk_progc_peek(&pm, s1), *pb = vk_progc_peek(&pm, s2);
    if (pa != a || pb != b || memcmp(&before, &pm.sets[vk_progc_set_idx(s1)], sizeof before) != 0) {
        fprintf(stderr, "FAIL: vk_progc_peek mutated or mismatched ways\n");
        return 1;
    }
    vk_qi_bar2_spec b1 = vk_qi_barrier_spec(1, fault_mode == 4);
    vk_qi_bar2_spec b2 = vk_qi_barrier_spec(2, fault_mode == 4);
    if (b1.src_stage != 0x00001000u || b1.dst_stage != 0x00000800u ||
        b2.src_stage != 0x00000800u || b2.dst_stage != 0x00000002u) {
        fprintf(stderr, "FAIL: narrow VkMemoryBarrier2 stage mask invalid (fault=%d b1=0x%llx->0x%llx b2=0x%llx->0x%llx)\n",
                fault_mode, (unsigned long long)b1.src_stage, (unsigned long long)b1.dst_stage,
                (unsigned long long)b2.src_stage, (unsigned long long)b2.dst_stage);
        return 1;
    }
    return 0;
}

static int run_ktgl_w2_tests(int fault_mode) {
    if (!w2_check_nv12_size(1280, 720, 1280, 640, 0, fault_mode) ||
        !w2_check_nv12_size(1920, 1080, 1984, 1024, 0, fault_mode) ||
        !w2_check_nv12_size(1280, 720, 1280, 640, 1, fault_mode)) {
        fprintf(stderr, "FAIL: aoc_nv12 parity failed (fault=%d)\n", fault_mode);
        return 1;
    }
    w2_bench_nv12(1280, 720, 400);
    w2_bench_nv12(1920, 1080, 300);
    if (w2_test_barriers_and_peek(fault_mode) != 0) return 1;

    enum { NPROG = 400, NDRAW = 800, NFRAMES = 500 };
    static uint64_t serials[NPROG];
    static uintptr_t vs[200], fs[160];
    for (int i = 0; i < NPROG; i++) serials[i] = 1000ull + (uint64_t)i * 64ull;
    for (int i = 0; i < 200; i++) vs[i] = 0x10000000ull + (uintptr_t)i * 64u;
    for (int i = 0; i < 160; i++) fs[i] = 0x20000000ull + (uintptr_t)i * 64u;

    uint64_t base_misses = 0, steady_misses = 0;
    uint64_t base_ns = w2_bench_memo_base(serials, vs, fs, NPROG, NDRAW, NFRAMES, &base_misses);
    uint64_t opt_ns = w2_bench_memo_opt(serials, vs, fs, NPROG, NDRAW, NFRAMES, &steady_misses, fault_mode);
    double total = (double)NFRAMES * (double)NDRAW;
    printf("aoc_ktgl_memo: frames=%d draws=%d base_misses=%llu opt_steady_misses=%llu base_ns_draw=%.2f opt_ns_draw=%.2f speedup=%.2fx\n",
           NFRAMES, NDRAW, (unsigned long long)base_misses, (unsigned long long)steady_misses,
           base_ns / total, opt_ns / total, opt_ns ? (double)base_ns / (double)opt_ns : 0.0);
    return (steady_misses * 100ull >= base_misses);
}

#endif
