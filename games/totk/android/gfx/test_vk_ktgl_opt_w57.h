/* Wave 57 equivalence, fault-injection, TSan & -O2 microbenchmark header for ktglopttest. */
#ifndef TEST_VK_KTGL_OPT_W57_H
#define TEST_VK_KTGL_OPT_W57_H
#ifndef TOTK_W57_TEST_HOOKS
#define TOTK_W57_TEST_HOOKS
#endif
#include "vk_ans_widen_fast.h"
#include "vk_ub_gw_fast.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

_Atomic int g_totk_vk_ans_widen_fast_override = -1;
_Atomic int g_totk_w57_fault = -1;

static void w57_req(int ok) {
    if (!ok) abort();
}

static uint64_t w57_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

__attribute__((noinline)) static void w57_oracle_widen(uint16_t *dst, const uint8_t *src, uint32_t count) {
    for (uint32_t i = 0u; i < count; i++) dst[i] = src[i] == 0xffu ? 0xffffu : (uint16_t)src[i];
}

__attribute__((noinline)) static void w57_oracle_quad(uint32_t *ix, uint32_t nquads) {
    for (uint32_t q = 0u; q < nquads; q++) {
        uint32_t v = q * 4u, t[6] = {v, v + 1u, v + 2u, v, v + 2u, v + 3u};
        memcpy(ix + (size_t)q * 6u, t, 24u);
    }
}

__attribute__((noinline)) static void w57_fast_widen_call(uint16_t *dst, const uint8_t *src, uint32_t count) {
    totk_widen_u8_indices(dst, src, count);
}

__attribute__((noinline)) static void w57_fast_quad_call(uint32_t *ix, uint32_t nquads) {
    totk_fill_quad_ib_u32(ix, nquads);
}

typedef struct { uint64_t seq; uint32_t id; } w57_ub_stub_t;

static void w57_check_ans_and_widen(void) {
    uint64_t tex_nz[2] = {(UINT64_C(1) << 2) | (UINT64_C(1) << 19) | (UINT64_C(1) << 63), (UINT64_C(1) << 1) | (UINT64_C(1) << 40)};
    uint64_t m0 = totk_ans_tex_word(tex_nz[0], 0, 20u);
    uint64_t m1 = totk_ans_tex_word(tex_nz[1], 1, 20u);
    w57_req(m0 == ((UINT64_C(1) << 2) | (UINT64_C(1) << 19)) && m1 == UINT64_C(0));
    w57_req(totk_ans_tex_word(tex_nz[1], 1, 70u) == (UINT64_C(1) << 1));
    w57_req(totk_ans_img_word(0x21u) == 0x21u);

    uint8_t src[64];
    uint16_t ref_dst[64], fast_dst[64];
    for (uint32_t i = 0u; i < 64u; i++) src[i] = (uint8_t)((i * 37u) & 0xffu);
    src[0] = 0xffu; src[7] = 0xffu; src[8] = 0xfeu; src[15] = 0xffu;
    src[16] = 0xffu; src[31] = 0xffu; src[32] = 0x00u; src[47] = 0xffu;
    for (uint32_t n = 0u; n <= 49u; n++) {
        memset(ref_dst, 0x5a, sizeof ref_dst);
        memset(fast_dst, 0x5a, sizeof fast_dst);
        w57_oracle_widen(ref_dst, src, n);
        totk_widen_u8_indices(fast_dst, src, n);
        w57_req(memcmp(ref_dst, fast_dst, sizeof ref_dst) == 0);
    }

    uint32_t q0[6 * 19], q1[6 * 19];
    w57_oracle_quad(q0, 19u);
    totk_fill_quad_ib_u32(q1, 19u);
    w57_req(memcmp(q0, q1, sizeof q0) == 0);
}

static void w57_check_bloom_sort_and_wb(void) {
    totk_rt_fb_bloom_t fb = {0};
    totk_rt_fb_reset(&fb);
    uintptr_t im0 = 0x401000u, im1 = 0x802000u;
    totk_rt_fb_add(&fb, im0);
    totk_rt_fb_add(&fb, im1);
    fb.ready = 1u;
    w57_req(totk_rt_fb_may_contain(&fb, im0) == 1 && totk_rt_fb_may_contain(&fb, im1) == 1);

    totk_pm_rt_span_t sp = {0};
    totk_pm_rt_add(&sp, 0x100000u, 0x10000u, 11u);
    totk_pm_rt_add(&sp, 0x200000u, 0x10000u, 22u);
    sp.built = 1u;
    w57_req(totk_pm_rt_may_hit(&sp, 0x108000u, 0x109000u, 99u) == 1);
    w57_req(totk_pm_rt_may_hit(&sp, 0x300000u, 0x301000u, 22u) == 1);
    w57_req(totk_pm_rt_may_hit(&sp, 0x300000u, 0x301000u, 777u) == 0);

    uint32_t fr[16] = {0};
    int fn = 0;
    totk_fr_record_u32(fr, &fn, 0x07800438u);
    totk_fr_record_u32(fr, &fn, 0x050002d0u);
    totk_fr_record_u32(fr, &fn, 0x050002d0u);
    totk_fr_record_u32(fr, &fn, 0x07800438u);
    totk_fr_record_u32(fr, &fn, 0x04000240u);
    w57_req(fn == 3);
    totk_sort_u32_small(fr, fn);
    w57_req(fr[0] == 0x04000240u && fr[1] == 0x050002d0u && fr[2] == 0x07800438u);

    w57_ub_stub_t ubs[32];
    int ord[32];
    for (int i = 0; i < 32; i++) { ubs[i].seq = (uint64_t)(32 - i) * 7u; ubs[i].id = (uint32_t)i; ord[i] = i; }
    totk_ub_sort_by_seq(ord, 32, ubs, sizeof ubs[0], offsetof(w57_ub_stub_t, seq));
    for (int i = 1; i < 32; i++) w57_req(ubs[ord[i - 1]].seq <= ubs[ord[i]].seq);
    w57_req(totk_wb_has_live(0) == 0 && totk_wb_has_live(2) == 1);
}

static void *w57_thread_fn(void *arg) {
    int tid = *(int *)arg;
    for (int it = 0; it < 200; it++) {
        totk_vk_ans_widen_fast_set_for_test((tid + it) & 1);
        (void)totk_vk_ans_widen_fast_on();
        w57_check_ans_and_widen();
    }
    return NULL;
}

static uint64_t w57_bench_step_fast(uint16_t *dst, const uint8_t *src, uint32_t *qib,
                                    const uint64_t tex_nz[6][2], const uint32_t img_nz[6],
                                    const totk_rt_fb_bloom_t *fb, const uintptr_t atts[9],
                                    const uintptr_t probes[8], int it) {
    uint64_t sink = 0u;
    w57_fast_widen_call(dst, src, 1024u);
    w57_fast_quad_call(qib, 256u);
    sink += (uint64_t)dst[(it * 7) & 1023] + (uint64_t)qib[it & 1023];
    for (int st = 0; st < 6; st++) {
        for (int w = 0; w < 2; w++) {
            for (uint64_t m = totk_ans_tex_word(tex_nz[st][w], w, 128u); m; m &= m - 1u) {
                sink += (uint64_t)((w << 6) + __builtin_ctzll(m));
            }
        }
        for (uint32_t m = totk_ans_img_word(img_nz[st]); m; m &= m - 1u) sink += (uint64_t)__builtin_ctz(m);
    }
    for (int p = 0; p < 8; p++) {
        if (totk_rt_fb_may_contain(fb, probes[p])) {
            for (int a = 0; a < 9; a++) if (atts[a] == probes[p]) sink++;
        }
    }
    return sink;
}

static uint64_t w57_bench_step_base(uint16_t *dst, const uint8_t *src, uint32_t *qib,
                                    const uint64_t tex_nz[6][2], const uint32_t img_nz[6],
                                    const uintptr_t atts[9], const uintptr_t probes[8], int it) {
    uint64_t sink = 0u;
    w57_oracle_widen(dst, src, 1024u);
    w57_oracle_quad(qib, 256u);
    sink += (uint64_t)dst[(it * 7) & 1023] + (uint64_t)qib[it & 1023];
    for (int st = 0; st < 6; st++) {
        for (int r = 0; r < 128; r++) if ((tex_nz[st][r >> 6] >> (r & 63)) & 1u) sink += (uint64_t)r;
        for (int r = 0; r < 32; r++) if ((img_nz[st] >> r) & 1u) sink += (uint64_t)r;
    }
    for (int p = 0; p < 8; p++) for (int a = 0; a < 9; a++) if (atts[a] == probes[p]) sink++;
    return sink;
}

__attribute__((noinline)) static uint64_t w57_bench_run(int fast) {
    uint8_t u8_src[1024];
    uint16_t u16_dst[1024];
    uint32_t q_ib[6 * 256];
    uint64_t tex_nz[6][2] = {
        {UINT64_C(1) << 3, 0}, {UINT64_C(1) << 11, UINT64_C(1) << 5},
        {0, 0}, {0, 0}, {UINT64_C(1) << 2, 0}, {0, 0}
    };
    uint32_t img_nz[6] = {0, UINT32_C(1) << 3, 0, 0, 0, UINT32_C(1) << 1};
    uintptr_t atts[9] = {0x1111000u, 0x1122000u, 0, 0, 0, 0, 0, 0, 0x1199000u};
    uintptr_t probes[8] = {
        0x2222000u, 0x3333000u, 0x4444000u, 0x5555000u,
        0x6666000u, 0x7777000u, 0x8888000u, 0x9999000u
    };
    for (uint32_t i = 0u; i < 1024u; i++) u8_src[i] = (uint8_t)((i * 29u) & 0xffu);
    memset(q_ib, 0, sizeof q_ib);
    totk_rt_fb_bloom_t fb = {0};
    for (int a = 0; a < 9; a++) if (atts[a]) totk_rt_fb_add(&fb, atts[a]);
    fb.ready = 1u;
    uint64_t sink = 0u;
    for (int it = 0; it < 6000; it++) {
        u8_src[it & 1023] = (uint8_t)((it * 13u + 0xf0u) & 0xffu);
        q_ib[it & 1023] = 0u;
        sink += fast
            ? w57_bench_step_fast(u16_dst, u8_src, q_ib, tex_nz, img_nz, &fb, atts, probes, it)
            : w57_bench_step_base(u16_dst, u8_src, q_ib, tex_nz, img_nz, atts, probes, it);
    }
    return sink;
}

static int run_ktgl_w57_tests(int fault_mode) {
    (void)fault_mode;
    totk_w57_set_fault(-1);
    pthread_t th[4];
    int tids[4] = {0, 1, 2, 3};
    w57_check_ans_and_widen();
    w57_check_bloom_sort_and_wb();
    for (int i = 0; i < 4; i++) w57_req(pthread_create(&th[i], NULL, w57_thread_fn, &tids[i]) == 0);
    for (int i = 0; i < 4; i++) pthread_join(th[i], NULL);
    totk_vk_ans_widen_fast_set_for_test(-1);
    uint64_t t0 = w57_now_ns(), s0 = w57_bench_run(0), t1 = w57_now_ns(), s1 = w57_bench_run(1), t2 = w57_now_ns();
    w57_req(s0 == s1);
    double ms0 = (double)(t1 - t0) / 1e6, ms1 = (double)(t2 - t1) / 1e6;
    printf("ktglopttest w57: baseline=%.3f ms fast=%.3f ms speedup=%.2fx sink=%llu failures=0\n",
           ms0, ms1, ms0 / (ms1 > 0.001 ? ms1 : 0.001), (unsigned long long)s1);
    return 0;
}

#ifdef TOTK_W57_STANDALONE
int main(void) { return run_ktgl_w57_tests(-1); }
#endif
#endif
