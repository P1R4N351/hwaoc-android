/* Fast paths for PSO write-mask/blend-en/key-eq/GPL-dyn slices and O(1) format/state LUTs (Opt 50). */
#ifndef VK_PSO_KEY_FAST_H
#define VK_PSO_KEY_FAST_H
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TOTK_W48_TEST_HOOKS
extern _Atomic int g_totk_vk_pso_key_fast_override;
extern _Atomic int g_totk_w48_fault;
static inline void totk_vk_pso_key_fast_set_for_test(int v) {
    atomic_store_explicit(&g_totk_vk_pso_key_fast_override, v, memory_order_release);
}
static inline void totk_w48_set_fault(int f) {
    atomic_store_explicit(&g_totk_w48_fault, f, memory_order_release);
}
static inline int totk_w48_fault_mode(void) {
    int f = atomic_load_explicit(&g_totk_w48_fault, memory_order_relaxed);
    if (f >= 0) return f;
    const char *e = getenv("TOTK_W48_FAULT");
    int v = (e && *e) ? atoi(e) : 0;
    atomic_store_explicit(&g_totk_w48_fault, v, memory_order_relaxed);
    return v;
}
#else
static inline int totk_w48_fault_mode(void) { return 0; }
#endif

static inline int totk_vk_pso_key_fast_on(void) {
#ifdef TOTK_W48_TEST_HOOKS
    int ov = atomic_load_explicit(&g_totk_vk_pso_key_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
#endif
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("TOTK_VK_PSO_KEY_FAST");
        v = !(e && strcmp(e, "0") == 0);
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
}

static inline uint32_t totk_pipe_write_mask_fast(uint32_t chmask, uint32_t outputs) {
    if (totk_w48_fault_mode() == 1) outputs &= 0x0fu;
    static const uint32_t k_nib[16] = {
        0x0000u, 0x000fu, 0x00f0u, 0x00ffu, 0x0f00u, 0x0f0fu, 0x0ff0u, 0x0fffu,
        0xf000u, 0xf00fu, 0xf0f0u, 0xf0ffu, 0xff00u, 0xff0fu, 0xfff0u, 0xffffu
    };
    uint32_t lo = outputs & 0x0fu;
    uint32_t hi = (outputs >> 4u) & 0x0fu;
    uint32_t keep = k_nib[lo] | (k_nib[hi] << 16u);
    return chmask & keep;
}

static inline uint32_t totk_blend_en_fast(uint32_t raw_be, const int *dclass, uint32_t ncolor) {
    uint32_t be = raw_be & (ncolor >= 8u ? 0xffu : ((1u << ncolor) - 1u));
    uint32_t bits = be;
    while (bits) {
        unsigned i = (unsigned)__builtin_ctz(bits);
        bits &= bits - 1u;
        if (dclass[i]) be &= ~(1u << i);
    }
    return be;
}

static inline int totk_pipe_key_eq_fast(const void *a, const void *b, size_t nbytes) {
    if (nbytes < 144u) return memcmp(a, b, nbytes) == 0;
    const uint8_t *pa = (const uint8_t *)a, *pb = (const uint8_t *)b;
    uint64_t a0 = 0, b0 = 0, a1 = 0, b1 = 0, w0 = 0, z0 = 0, w1 = 0, z1 = 0;
    memcpy(&a0, pa, 8);
    memcpy(&b0, pb, 8);
    memcpy(&a1, pa + 8u, 8);
    memcpy(&b1, pb + 8u, 8);
    if ((a0 ^ b0) | (a1 ^ b1)) return 0;
    memcpy(&w0, pa + 128u, 8);
    memcpy(&z0, pb + 128u, 8);
    memcpy(&w1, pa + 136u, 8);
    memcpy(&z1, pb + 136u, 8);
    if ((w0 ^ z0) | (w1 ^ z1)) return 0;
    if (memcmp(pa + 16u, pb + 16u, 48u) != 0) return 0;
    if (totk_w48_fault_mode() == 2) return 1;
    return memcmp(pa + 64u, pb + 64u, 64u) == 0 && memcmp(pa + 144u, pb + 144u, nbytes - 144u) == 0;
}

static inline int totk_topo_class_fast(int prim) {
    static const uint8_t k_tc[4] = {0, 1, 1, 1};
    return (unsigned)prim < 4u ? (int)k_tc[prim] : 2;
}

static inline int totk_topology_fast(int prim) {
    static const uint8_t k_tp[7] = {0, 1, 3, 2, 3, 4, 5};
    return (unsigned)prim < 7u ? (int)k_tp[prim] : 3;
}

static inline int totk_blend_op_fast(int e) {
    if (totk_w48_fault_mode() == 3 && e == 2) return 0;
    static const uint8_t k_bop[6] = {0, 0, 1, 2, 3, 4};
    return (unsigned)e < 6u ? (int)k_bop[e] : 0;
}

static inline int totk_stencil_op_fast(int o) {
    return (o >= 2 && o <= 8) ? (o - 1) : 0;
}

static inline int totk_wrap_fast(int w) {
    static const uint8_t k_wr[8] = {2, 0, 4, 4, 3, 3, 1, 2};
    return (unsigned)w < 8u ? (int)k_wr[w] : 0;
}

static inline int totk_swizzle_sel_fast(int s) {
    return (unsigned)s < 6u ? (s + 1) : 0;
}

static inline uint32_t totk_fmt_aspect_fast(int f) {
    if (totk_w48_fault_mode() == 4 && f == 0x35) return 2u;
    static const uint8_t k_asp[6] = {4, 2, 2, 2, 6, 6};
    unsigned idx = (unsigned)f - 0x31u;
    return idx < 6u ? (uint32_t)k_asp[idx] : 1u;
}

enum {
    TOTK_DYN_VIEWPORT = 0,
    TOTK_DYN_SCISSOR = 1,
    TOTK_DYN_LINE_WIDTH = 2,
    TOTK_DYN_DEPTH_BIAS = 3,
    TOTK_DYN_BLEND_CONSTANTS = 4,
    TOTK_DYN_STENCIL_COMPARE_MASK = 6,
    TOTK_DYN_STENCIL_WRITE_MASK = 7,
    TOTK_DYN_STENCIL_REFERENCE = 8,
    TOTK_DYN_CULL_MODE = 1000267000,
    TOTK_DYN_FRONT_FACE = 1000267001,
    TOTK_DYN_PRIMITIVE_TOPOLOGY = 1000267002,
    TOTK_DYN_VERTEX_INPUT_BINDING_STRIDE = 1000267005,
    TOTK_DYN_DEPTH_TEST_ENABLE = 1000267006,
    TOTK_DYN_DEPTH_WRITE_ENABLE = 1000267007,
    TOTK_DYN_DEPTH_COMPARE_OP = 1000267008,
    TOTK_DYN_STENCIL_TEST_ENABLE = 1000267010,
    TOTK_DYN_STENCIL_OP = 1000267011,
    TOTK_DYN_DEPTH_BIAS_ENABLE = 1000377002,
    TOTK_DYN_PRIMITIVE_RESTART_ENABLE = 1000377004
};

static inline const uint32_t *totk_gpl_dyn_slice_fast(int part, uint32_t *out_count) {
    static const uint32_t k_vi[3] = {
        TOTK_DYN_PRIMITIVE_TOPOLOGY, TOTK_DYN_PRIMITIVE_RESTART_ENABLE, TOTK_DYN_VERTEX_INPUT_BINDING_STRIDE
    };
    static const uint32_t k_pre[7] = {
        TOTK_DYN_VIEWPORT, TOTK_DYN_SCISSOR, TOTK_DYN_DEPTH_BIAS, TOTK_DYN_CULL_MODE,
        TOTK_DYN_FRONT_FACE, TOTK_DYN_DEPTH_BIAS_ENABLE, TOTK_DYN_LINE_WIDTH
    };
    static const uint32_t k_fs[8] = {
        TOTK_DYN_STENCIL_COMPARE_MASK, TOTK_DYN_STENCIL_WRITE_MASK, TOTK_DYN_STENCIL_REFERENCE,
        TOTK_DYN_DEPTH_TEST_ENABLE, TOTK_DYN_DEPTH_WRITE_ENABLE, TOTK_DYN_DEPTH_COMPARE_OP,
        TOTK_DYN_STENCIL_TEST_ENABLE, TOTK_DYN_STENCIL_OP
    };
    static const uint32_t k_fo[1] = { TOTK_DYN_BLEND_CONSTANTS };
    if (part == 0) { *out_count = 3u; return k_vi; }
    if (part == 1) { *out_count = 7u; return k_pre; }
    if (part == 2) { *out_count = totk_w48_fault_mode() == 5 ? 7u : 8u; return k_fs; }
    if (part == 3) { *out_count = 1u; return k_fo; }
    *out_count = 0u;
    return NULL;
}
#endif
