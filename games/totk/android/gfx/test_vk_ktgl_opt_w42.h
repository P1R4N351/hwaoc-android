/* Wave 42 (Optimization 44): NVN/Vulkan format LUT, Tegra X1 layout, mip-walk elision, and seqlock vpool tests. */
#ifndef TEST_VK_KTGL_OPT_W42_H
#define TEST_VK_KTGL_OPT_W42_H
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "nvn_tex.h"
#include "nvn_fmt_mtl.h"
#include "vk_fmt.h"

_Atomic int g_totk_tex_layout_fast_override = -1;
_Atomic int g_totk_w42_fault = -1;

static inline uint64_t w42_aoc_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static inline int w42_layout_eq(const nvn_texlayout *a, const nvn_texlayout *b) {
    return a->size == b->size && a->layer_size == b->layer_size &&
           a->align == b->align && a->layers == b->layers &&
           a->bpb == b->bpb && a->tileH == b->tileH && a->tileD == b->tileD;
}

static inline int w42_check_fmt_and_vk_parity(void) {
    vk_fmt_set_depth(VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT);
    for (int f = -16; f <= 512; f++) {
        totk_tex_layout_fast_set_for_test(0);
        int bw0 = -1, bh0 = -1, b0 = nvn_fmt_bpb(f, &bw0, &bh0);
        uint32_t m0 = 777u; uint8_t sw0[4] = {9, 9, 9, 9}; int km0 = nvn_fmt_to_mtl(f, &m0, sw0);
        VkFormat vf0 = VK_FORMAT_MAX_ENUM; VkComponentMapping vm0 = {0}; int kv0 = vk_fmt(f, &vf0, &vm0);
        int cl0 = vk_fmt_class(f); VkFormat vx0 = vk_vertex_fmt(f); VkBlendFactor bf0 = vk_blend_factor(f);

        totk_tex_layout_fast_set_for_test(1);
        int bw1 = -2, bh1 = -2, b1 = nvn_fmt_bpb(f, &bw1, &bh1);
        uint32_t m1 = 666u; uint8_t sw1[4] = {8, 8, 8, 8}; int km1 = nvn_fmt_to_mtl(f, &m1, sw1);
        VkFormat vf1 = VK_FORMAT_MAX_ENUM; VkComponentMapping vm1 = {0}; int kv1 = vk_fmt(f, &vf1, &vm1);
        int cl1 = vk_fmt_class(f); VkFormat vx1 = vk_vertex_fmt(f); VkBlendFactor bf1 = vk_blend_factor(f);

        if (b0 != b1 || bw0 != bw1 || bh0 != bh1 || km0 != km1 || m0 != m1 || memcmp(sw0, sw1, 4u) != 0) return 1;
        if (kv0 != kv1 || vf0 != vf1 || vm0.r != vm1.r || vm0.a != vm1.a || cl0 != cl1 || vx0 != vx1 || bf0 != bf1) return 1;
    }
    vk_fmt_set_depth(VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D24_UNORM_S8_UINT);
    return 0;
}

static inline int w42_check_tex_layout_parity(void) {
    static const nvn_texdesc cases[] = {
        {.target=1, .format=0x25, .flags=0x0, .samples=1, .width=1920, .height=1080, .depth=1, .levels=11},
        {.target=8, .format=0x7d, .flags=0x4, .samples=1, .width=1024, .height=1024, .depth=6, .levels=8, .pkg_block_h=4},
        {.target=9, .format=0x25, .flags=0x0, .samples=1, .width=64, .height=64, .depth=12, .levels=7},
        {.target=2, .format=0x49, .flags=0x0, .samples=1, .width=64, .height=64, .depth=64, .levels=7, .pkg_block_h=0x41},
        {.target=1, .format=0x25, .flags=0x40, .samples=1, .width=260, .height=260, .depth=1, .levels=1},
        {.target=1, .format=0x42, .flags=0x10, .samples=1, .width=65, .height=65, .depth=1, .levels=1},
        {.target=1, .format=0x0d, .flags=0x200, .samples=1, .width=260, .height=260, .depth=1, .levels=9},
        {.target=1, .format=0x3c, .flags=0x200, .samples=1, .width=320, .height=320, .depth=1, .levels=1},
        {.target=1, .format=0x25, .flags=0x200, .samples=1, .width=64, .height=128, .depth=1, .levels=1},
        {.target=1, .format=0x79, .flags=0x10, .samples=1, .width=250, .height=250, .depth=1, .levels=1},
        {.target=1, .format=0x7e, .flags=0x10, .samples=1, .width=250, .height=250, .depth=1, .levels=1},
        {.target=0, .format=0x01, .flags=0x0, .samples=1, .width=256, .height=1, .depth=1, .levels=5},
        {.target=10, .format=0x25, .flags=0x0, .samples=1, .width=4096, .height=1, .depth=1, .levels=1},
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const nvn_texdesc *d = &cases[i];
        totk_tex_layout_fast_set_for_test(0);
        nvn_texlayout l0; nvn_tex_layout(d, &l0);
        uint32_t ls0 = nvn_tex_linear_stride(d);
        totk_tex_layout_fast_set_for_test(1);
        nvn_texlayout l1; nvn_tex_layout(d, &l1);
        uint32_t ls1 = nvn_tex_linear_stride(d);
        if (!w42_layout_eq(&l0, &l1) || ls0 != ls1) return 1;
        unsigned nlev = d->levels ? d->levels : 1u;
        for (unsigned lv = 0u; lv < nlev && lv < 16u; lv++) {
            totk_tex_layout_fast_set_for_test(0);
            uint64_t o0 = nvn_tex_level_offset(d, lv); unsigned th0 = 9u, td0 = 9u; nvn_tex_level_tiles(d, lv, &th0, &td0);
            totk_tex_layout_fast_set_for_test(1);
            uint64_t o1 = nvn_tex_level_offset(d, lv); unsigned th1 = 8u, td1 = 8u; nvn_tex_level_tiles(d, lv, &th1, &td1);
            if (o0 != o1 || th0 != th1 || td0 != td1) return 1;
        }
    }
    for (unsigned sh = 0u; sh <= 6u; sh++) {
        for (uint32_t dm = 0u; dm <= 70u; dm++) {
            uint32_t th0 = sh, x0 = 8u << (sh ? sh - 1u : 0u);
            if (!sh) th0 = 0u;
            else if (x0 >= dm) { while (--th0) { x0 >>= 1; if (x0 < dm) break; } }
            if (th0 != totk_adj_tile_fast(sh, 8u, dm)) return 1;
        }
    }
    return 0;
}

typedef struct {
    totk_vmap_fast_t vm;
    _Atomic int stop;
    _Atomic int err;
} w42_vmap_tsan_t;

static inline void *w42_vmap_reader(void *arg) {
    w42_vmap_tsan_t *ctx = (w42_vmap_tsan_t *)arg;
    for (int i = 0; i < 8000 && !atomic_load_explicit(&ctx->stop, memory_order_acquire); i++) {
        uintptr_t cpu = 0u;
        int r = totk_vmap_fast_lookup(&ctx->vm, (uintptr_t)UINT64_C(0x10020000), 4096u, &cpu);
        if (r > 0 && cpu != (uintptr_t)UINT64_C(0x20020000) && cpu != (uintptr_t)UINT64_C(0x30020000)) {
            atomic_store_explicit(&ctx->err, 1, memory_order_release);
        }
    }
    return NULL;
}

static inline int w42_check_vmap_and_tsan(void) {
    totk_vmap_fast_t vm = {0};
    uintptr_t out = 0u;
    if (totk_vmap_fast_lookup(&vm, (uintptr_t)UINT64_C(0x10000000), 4096u, &out) != 0) return 1;
    totk_vmap_fast_note(&vm, (uintptr_t)UINT64_C(0x10000000), 0x40000u, (uintptr_t)UINT64_C(0x50000000));
    totk_vmap_fast_note(&vm, (uintptr_t)UINT64_C(0x10000000), 0x40000u, (uintptr_t)UINT64_C(0x60000000));
    if (totk_vmap_fast_lookup(&vm, (uintptr_t)UINT64_C(0x10010000), 4096u, &out) != 1 || out != (uintptr_t)UINT64_C(0x60010000)) return 1;
    totk_vmap_fast_note(&vm, (uintptr_t)UINT64_C(0x10000000), 0x40000u, 0u);
    if (totk_vmap_fast_lookup(&vm, (uintptr_t)UINT64_C(0x10010000), 4096u, &out) != 0) return 1;

    w42_vmap_tsan_t ctx = {.vm = {0}, .stop = 0, .err = 0};
    pthread_t th[3];
    for (int i = 0; i < 3; i++) pthread_create(&th[i], NULL, w42_vmap_reader, &ctx);
    for (int step = 0; step < 1500; step++) {
        totk_vmap_fast_note(&ctx.vm, (uintptr_t)UINT64_C(0x10000000), 0x80000u, (step & 1) ? (uintptr_t)UINT64_C(0x20000000) : (uintptr_t)UINT64_C(0x30000000));
    }
    atomic_store_explicit(&ctx.stop, 1, memory_order_release);
    for (int i = 0; i < 3; i++) pthread_join(th[i], NULL);
    return atomic_load_explicit(&ctx.err, memory_order_acquire) != 0;
}

static inline uint64_t w42_bench_aoc(int fast_mode) {
    totk_tex_layout_fast_set_for_test(fast_mode);
    nvn_texdesc d = {.target=1, .format=0x7d, .flags=0x4, .samples=1, .width=2048, .height=2048, .depth=1, .levels=8, .pkg_block_h=4};
    uint64_t sink = 0u;
    for (uint32_t i = 0u; i < 12000u; i++) {
        int fm = 0x79 + (int)(i % 28u);
        uint32_t mtl = 0u; uint8_t sw[4] = {0};
        sink += (uint64_t)nvn_fmt_to_mtl(fm, &mtl, sw) + mtl;
        VkFormat vf = VK_FORMAT_UNDEFINED; VkComponentMapping cm = {0};
        sink += (uint64_t)vk_fmt(fm, &vf, &cm) + (uint64_t)vf;
        sink += (uint64_t)vk_fmt_class(i & 0x31u) + (uint64_t)vk_vertex_fmt((int)(i & 0x68u)) + (uint64_t)vk_blend_factor((int)(i & 0x64u));
        sink += nvn_tex_level_offset(&d, (i & 7u));
        unsigned th = 0u, td = 0u;
        nvn_tex_level_tiles(&d, (i & 7u), &th, &td);
        sink += th + td;
    }
    totk_tex_layout_fast_set_for_test(-1);
    return sink;
}

static inline int run_ktgl_w42_tests(int fault_mode) {
    int prev = atomic_load_explicit(&g_totk_w42_fault, memory_order_acquire);
    if (fault_mode >= 156 && fault_mode <= 160) totk_w42_set_fault(fault_mode - 155);
    int err = 0;
    if (w42_check_fmt_and_vk_parity() != 0) err = 1;
    if (w42_check_tex_layout_parity() != 0) err = 1;
    if (w42_check_vmap_and_tsan() != 0) err = 1;
    uint64_t t0 = w42_aoc_ns(), s0 = w42_bench_aoc(0), t1 = w42_aoc_ns();
    uint64_t t2 = w42_aoc_ns(), s1 = w42_bench_aoc(1), t3 = w42_aoc_ns();
    if (s0 != s1) err = 1;
    double slow_ms = (double)(t1 - t0) / 1e6, fast_ms = (double)(t3 - t2) / 1e6;
    printf("aoc_w42_tex_layout_fast: slow_ms=%.3f fast_ms=%.3f speedup=%.2fx sink=%llu %s\n",
           slow_ms, fast_ms, fast_ms > 0.0 ? slow_ms / fast_ms : 0.0,
           (unsigned long long)s1, err ? "FAIL" : "PASS");
    totk_w42_set_fault(prev);
    return err;
}

#ifdef TOTK_W42_STANDALONE
int main(void) {
    const char *f = getenv("TOTK_W42_FAULT");
    return run_ktgl_w42_tests(f && *f ? (155 + atoi(f)) : 0);
}
#endif
#endif
