#ifndef TEST_VK_KTGL_OPT_W48_H
#define TEST_VK_KTGL_OPT_W48_H
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "vk_pso_key_fast.h"
#ifdef TOTK_W48_STANDALONE
int g_fault_mode = 0;
#endif

_Atomic int g_totk_vk_pso_key_fast_override = -1;
_Atomic int g_totk_w48_fault = -1;
static int g_w48_fail = 0;

#define W48_EXPECT_EQ(m, w, g) do { \
    uint64_t wa_ = (uint64_t)(w), ga_ = (uint64_t)(g); \
    if (wa_ != ga_) { \
        g_w48_fail++; \
        fprintf(stderr, "FAIL %s:%d %s want=%llu got=%llu\n", __FILE__, __LINE__, m, \
                (unsigned long long)wa_, (unsigned long long)ga_); \
    } \
} while (0)

static uint64_t w48_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static uint32_t w48_ref_write_mask(uint32_t chmask, uint32_t outputs) {
    uint32_t mask = chmask;
    for (unsigned i = 0; i < 8u; i++) if (!(outputs >> i & 1u)) mask &= ~(0xfu << (4u * i));
    return mask;
}

static uint32_t w48_ref_blend_en(uint32_t raw_be, const int *dclass, uint32_t ncolor) {
    uint32_t be = raw_be;
    for (uint32_t i = 0; i < ncolor && i < 8u; i++) if (dclass[i]) be &= ~(1u << i);
    return be & (ncolor >= 8u ? 0xffu : (1u << ncolor) - 1u);
}

static int w48_ref_topo_class(int prim) {
    return prim == 0 ? 0 : prim == 1 || prim == 2 || prim == 3 ? 1 : 2;
}

static int w48_ref_topology(int prim) {
    switch (prim) {
    case 0: return 0; case 1: return 1; case 3: return 2;
    case 5: return 4; case 6: return 5;
    }
    return 3;
}

static int w48_ref_blend_op(int e) {
    switch (e) { case 2: return 1; case 3: return 2; case 4: return 3; case 5: return 4; }
    return 0;
}

static int w48_ref_stencil_op(int o) {
    switch (o) {
    case 2: return 1; case 3: return 2; case 4: return 3; case 5: return 4;
    case 6: return 5; case 7: return 6; case 8: return 7;
    }
    return 0;
}

static int w48_ref_wrap(int w) {
    switch (w) {
    case 0: case 7: return 2; case 1: return 0; case 2: case 3: return 4;
    case 4: case 5: return 3; case 6: return 1;
    }
    return 0;
}

static int w48_ref_swizzle_sel(int s) {
    switch (s) {
    case 0: return 1; case 1: return 2; case 2: return 3;
    case 3: return 4; case 4: return 5; case 5: return 6;
    }
    return 0;
}

static uint32_t w48_ref_aspect(int f) {
    if (f == 0x31) return 4u;
    if (f == 0x35 || f == 0x36) return 6u;
    if (f >= 0x32 && f <= 0x34) return 2u;
    return 1u;
}

static void w48_test_luts_and_masks(void) {
    for (int v = -4; v <= 0x40; v++) {
        W48_EXPECT_EQ("topo_class", w48_ref_topo_class(v), totk_topo_class_fast(v));
        W48_EXPECT_EQ("topology", w48_ref_topology(v), totk_topology_fast(v));
        W48_EXPECT_EQ("blend_op", w48_ref_blend_op(v), totk_blend_op_fast(v));
        W48_EXPECT_EQ("stencil_op", w48_ref_stencil_op(v), totk_stencil_op_fast(v));
        W48_EXPECT_EQ("wrap", w48_ref_wrap(v), totk_wrap_fast(v));
        W48_EXPECT_EQ("swizzle", w48_ref_swizzle_sel(v), totk_swizzle_sel_fast(v));
        W48_EXPECT_EQ("aspect", w48_ref_aspect(v), totk_fmt_aspect_fast(v));
    }
    for (uint32_t out = 0u; out < 256u; out++) {
        uint32_t cm = 0xf1e2d3c4u ^ (out * 0x01010101u);
        W48_EXPECT_EQ("write_mask", w48_ref_write_mask(cm, out), totk_pipe_write_mask_fast(cm, out));
    }
    int dclass[8] = {0, 1, 0, 2, 0, 1, 0, 0};
    for (uint32_t nc = 0u; nc <= 8u; nc++) {
        for (uint32_t be = 0u; be < 256u; be += 19u) {
            W48_EXPECT_EQ("blend_en", w48_ref_blend_en(be, dclass, nc), totk_blend_en_fast(be, dclass, nc));
        }
    }
}

static void w48_test_key_eq_and_dyn(void) {
    uint8_t a[464], b[464];
    for (size_t i = 0; i < sizeof a; i++) a[i] = (uint8_t)((i * 29u + 7u) & 0xffu);
    memcpy(b, a, sizeof b);
    W48_EXPECT_EQ("pipe_eq_same", 1, totk_pipe_key_eq_fast(a, b, sizeof a));
    for (size_t i = 0; i < sizeof a; i += 23u) {
        b[i] ^= 0x10u;
        W48_EXPECT_EQ("pipe_eq_flip", 0, totk_pipe_key_eq_fast(a, b, sizeof a));
        b[i] ^= 0x10u;
    }
    b[sizeof b - 1u] ^= 0x01u;
    W48_EXPECT_EQ("pipe_eq_tail", 0, totk_pipe_key_eq_fast(a, b, sizeof b));

    static const uint32_t dyn[19] = {
        TOTK_DYN_VIEWPORT, TOTK_DYN_SCISSOR, TOTK_DYN_DEPTH_BIAS, TOTK_DYN_BLEND_CONSTANTS,
        TOTK_DYN_STENCIL_COMPARE_MASK, TOTK_DYN_STENCIL_WRITE_MASK, TOTK_DYN_STENCIL_REFERENCE,
        TOTK_DYN_CULL_MODE, TOTK_DYN_FRONT_FACE, TOTK_DYN_PRIMITIVE_TOPOLOGY,
        TOTK_DYN_DEPTH_TEST_ENABLE, TOTK_DYN_DEPTH_WRITE_ENABLE, TOTK_DYN_DEPTH_COMPARE_OP,
        TOTK_DYN_STENCIL_TEST_ENABLE, TOTK_DYN_STENCIL_OP, TOTK_DYN_DEPTH_BIAS_ENABLE,
        TOTK_DYN_PRIMITIVE_RESTART_ENABLE, TOTK_DYN_VERTEX_INPUT_BINDING_STRIDE, TOTK_DYN_LINE_WIDTH
    };
    for (int part = 0; part < 4; part++) {
        uint32_t ref[19], rn = 0u, fn = 0u;
        for (unsigned i = 0; i < 19u; i++) {
            uint32_t d = dyn[i];
            int owner = (d == TOTK_DYN_PRIMITIVE_TOPOLOGY || d == TOTK_DYN_PRIMITIVE_RESTART_ENABLE ||
                         d == TOTK_DYN_VERTEX_INPUT_BINDING_STRIDE) ? 0 :
                        (d == TOTK_DYN_BLEND_CONSTANTS) ? 3 :
                        (d == TOTK_DYN_STENCIL_COMPARE_MASK || d == TOTK_DYN_STENCIL_WRITE_MASK ||
                         d == TOTK_DYN_STENCIL_REFERENCE || d == TOTK_DYN_DEPTH_TEST_ENABLE ||
                         d == TOTK_DYN_DEPTH_WRITE_ENABLE || d == TOTK_DYN_DEPTH_COMPARE_OP ||
                         d == TOTK_DYN_STENCIL_TEST_ENABLE || d == TOTK_DYN_STENCIL_OP) ? 2 : 1;
            if (owner == part) ref[rn++] = d;
        }
        const uint32_t *fsl = totk_gpl_dyn_slice_fast(part, &fn);
        W48_EXPECT_EQ("gpl_dyn_n", rn, fn);
        for (uint32_t i = 0; i < rn; i++) W48_EXPECT_EQ("gpl_dyn_item", ref[i], fsl[i]);
    }
}

typedef struct { uint32_t tid; uint32_t errs; } w48_thr_arg_t;

static void *w48_tsan_worker(void *arg) {
    w48_thr_arg_t *t = (w48_thr_arg_t *)arg;
    uint8_t k1[464], k2[464];
    memset(k1, (int)(t->tid + 3u), sizeof k1);
    memcpy(k2, k1, sizeof k2);
    for (uint32_t i = 0u; i < 200u; i++) {
        if (!totk_pipe_key_eq_fast(k1, k2, sizeof k1)) t->errs++;
        if (totk_pipe_write_mask_fast(0xffffffffu, i & 0xffu) != w48_ref_write_mask(0xffffffffu, i & 0xffu)) t->errs++;
        uint32_t fn = 0u;
        const uint32_t *sl = totk_gpl_dyn_slice_fast((int)(i & 3u), &fn);
        if (!fn || !sl) t->errs++;
    }
    return NULL;
}

static void w48_test_tsan(void) {
    pthread_t th[4];
    w48_thr_arg_t args[4];
    for (uint32_t t = 0u; t < 4u; t++) {
        args[t] = (w48_thr_arg_t){t, 0u};
        pthread_create(&th[t], NULL, w48_tsan_worker, &args[t]);
    }
    for (uint32_t t = 0u; t < 4u; t++) {
        pthread_join(th[t], NULL);
        W48_EXPECT_EQ("tsan_worker_errs", 0, args[t].errs);
    }
}

static uint64_t w48_bench_pass(int fast, uint32_t iters) {
    totk_vk_pso_key_fast_set_for_test(fast);
    static const uint32_t dyn[19] = {
        TOTK_DYN_VIEWPORT, TOTK_DYN_SCISSOR, TOTK_DYN_DEPTH_BIAS, TOTK_DYN_BLEND_CONSTANTS,
        TOTK_DYN_STENCIL_COMPARE_MASK, TOTK_DYN_STENCIL_WRITE_MASK, TOTK_DYN_STENCIL_REFERENCE,
        TOTK_DYN_CULL_MODE, TOTK_DYN_FRONT_FACE, TOTK_DYN_PRIMITIVE_TOPOLOGY,
        TOTK_DYN_DEPTH_TEST_ENABLE, TOTK_DYN_DEPTH_WRITE_ENABLE, TOTK_DYN_DEPTH_COMPARE_OP,
        TOTK_DYN_STENCIL_TEST_ENABLE, TOTK_DYN_STENCIL_OP, TOTK_DYN_DEPTH_BIAS_ENABLE,
        TOTK_DYN_PRIMITIVE_RESTART_ENABLE, TOTK_DYN_VERTEX_INPUT_BINDING_STRIDE, TOTK_DYN_LINE_WIDTH
    };
    uint8_t ka[464], kb[464];
    uint32_t sub_dyn[19];
    for (size_t i = 0; i < sizeof ka; i++) ka[i] = (uint8_t)((i * 13u + 5u) & 0xffu);
    memcpy(kb, ka, sizeof kb);
    int dclass[8] = {0, 0, 1, 0, 0, 2, 0, 0};
    uint32_t vin_attrs[32];
    for (uint32_t i = 0; i < 32u; i++) vin_attrs[i] = i * 17u + 3u;
    uint64_t sink = 0u;
    static const size_t m_off[4] = {0u, 128u, 16u, 8u};
    for (uint32_t it = 0u; it < iters; it++) {
        uint32_t sel = it & 3u;
        size_t mo = m_off[sel];
        uint8_t x = (sel != 0u) ? 1u : 0u;
        kb[mo] ^= x;
        uint32_t outputs = (it * 37u) & 0xffu, cm = 0xf0e0d0c0u + it;
        uint32_t raw_be = (it & 1u) ? 0u : 3u, vm = 0x00000013u;
        int op = (int)(it & 7u), part = (int)sel;
        if (fast) {
            sink += (uint64_t)totk_pipe_key_eq_fast(ka, kb, sizeof ka);
            uint32_t dn = 0u;
            const uint32_t *sl = totk_gpl_dyn_slice_fast(part, &dn);
            memcpy(sub_dyn, sl, (size_t)dn * sizeof sub_dyn[0]);
            sink += dn + sub_dyn[0] + totk_pipe_write_mask_fast(cm, outputs);
            uint32_t be = totk_blend_en_fast(raw_be, dclass, 8u);
            while (be) { unsigned i = (unsigned)__builtin_ctz(be); be &= be - 1u; sink += i + 1u; }
            while (vm) { int a = __builtin_ctz(vm); vm &= vm - 1u; sink += vin_attrs[a]; }
            sink += (uint64_t)(totk_blend_op_fast(op) + totk_stencil_op_fast(op + 2) + totk_wrap_fast(op) + totk_swizzle_sel_fast(op)) + totk_fmt_aspect_fast(0x31 + (op & 7));
        } else {
            sink += (uint64_t)(memcmp(ka, kb, sizeof ka) == 0);
            uint32_t dn = 0u;
            for (unsigned i = 0; i < 19u; i++) {
                uint32_t d = dyn[i];
                int owner = (d == TOTK_DYN_PRIMITIVE_TOPOLOGY || d == TOTK_DYN_PRIMITIVE_RESTART_ENABLE || d == TOTK_DYN_VERTEX_INPUT_BINDING_STRIDE) ? 0 :
                            (d == TOTK_DYN_BLEND_CONSTANTS) ? 3 :
                            (d == TOTK_DYN_STENCIL_COMPARE_MASK || d == TOTK_DYN_STENCIL_WRITE_MASK || d == TOTK_DYN_STENCIL_REFERENCE || d == TOTK_DYN_DEPTH_TEST_ENABLE || d == TOTK_DYN_DEPTH_WRITE_ENABLE || d == TOTK_DYN_DEPTH_COMPARE_OP || d == TOTK_DYN_STENCIL_TEST_ENABLE || d == TOTK_DYN_STENCIL_OP) ? 2 : 1;
                if (owner == part) sub_dyn[dn++] = d;
            }
            sink += dn + sub_dyn[0] + w48_ref_write_mask(cm, outputs);
            uint32_t be = w48_ref_blend_en(raw_be, dclass, 8u);
            for (uint32_t i = 0; i < 8u; i++) if ((be >> i) & 1u) sink += i + 1u;
            for (int a = 0; a < 32; a++) if ((vm >> a) & 1u) sink += vin_attrs[a];
            sink += (uint64_t)(w48_ref_blend_op(op) + w48_ref_stencil_op(op + 2) + w48_ref_wrap(op) + w48_ref_swizzle_sel(op)) + w48_ref_aspect(0x31 + (op & 7));
        }
        kb[mo] ^= x;
    }
    return sink;
}

static int run_ktgl_w48_tests(int fault_mode) {
    g_w48_fail = 0;
    const char *ef = getenv("TOTK_W48_FAULT");
    if (ef && *ef) totk_w48_set_fault(atoi(ef));
    if (fault_mode >= 186 && fault_mode <= 190) totk_w48_set_fault(fault_mode - 185);
    totk_vk_pso_key_fast_set_for_test(1);
    w48_test_luts_and_masks();
    w48_test_key_eq_and_dyn();
    w48_test_tsan();
    uint32_t iters = 150000u;
    uint64_t t0 = w48_now_ns();
    uint64_t s_slow = w48_bench_pass(0, iters);
    uint64_t dt_slow = w48_now_ns() - t0;
    uint64_t t1 = w48_now_ns();
    uint64_t s_fast = w48_bench_pass(1, iters);
    uint64_t dt_fast = w48_now_ns() - t1;
    W48_EXPECT_EQ("bench_sink", s_slow, s_fast);
    printf("aoc_w48_pso_key_fast: slow=%.3f ms fast=%.3f ms speedup=%.2fx sink=%llu\n",
           (double)dt_slow / 1e6, (double)dt_fast / 1e6,
           dt_fast ? (double)dt_slow / (double)dt_fast : 0.0, (unsigned long long)s_fast);
    totk_w48_set_fault(0);
    return g_w48_fail != 0;
}

#ifdef TOTK_W48_STANDALONE
int main(void) {
    const char *f = getenv("TOTK_KTGL_OPT_FAULT");
    if (f && *f) g_fault_mode = atoi(f);
    int rc = run_ktgl_w48_tests(g_fault_mode);
    printf("w48_standalone: fault=%d failures=%d\n", g_fault_mode, rc);
    return rc;
}
#endif
#endif
