#ifndef TEST_VK_KTGL_OPT_W6_H
#define TEST_VK_KTGL_OPT_W6_H
/* Wave 6 unit, benchmark, and Delta #27 fault-injection checks:
 *   1) TOTK_DESWIZZLE_FAST=1 (nvn_deswizzle / nvn_swizzle / _span / _region)
 *   2) TOTK_DIRTY_FAST=1 (64-page superpage cmax[] in real nvn_dirty_since) */
#include "../../third_party/d2r_gfx/gfx/tex/nvn_deswizzle.h"
#include "nvn_dirty.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

uint64_t host_now_ns(void);
void nvn_deswizzle_w6_set_fast(int on);
void nvn_dirty_w6_set_fast(int on);

static void w6_fill(uint8_t *buf, size_t n, uint32_t seed) {
    uint32_t x = seed ? seed : 0xA5A55A5Au;
    for (size_t i = 0; i < n; i++) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; buf[i] = (uint8_t)x; }
}

static int w6_check_deswz_case(const nvn_texdesc *d) {
    nvn_texlayout L;
    nvn_tex_layout(d, &L);
    uint8_t *st0 = calloc(1u, (size_t)L.size), *st1 = calloc(1u, (size_t)L.size);
    if (!st0 || !st1) { free(st0); free(st1); return 1; }
    int fail = 0;
    for (unsigned l = 0; l < d->levels && !fail; l++) {
        uint32_t rb, rows, sl, bpb; uint64_t base; unsigned th, td;
        if (nvn_deswizzle_geom(d, l, 0, &rb, &rows, &sl, &base, &th, &td, &bpb)) { fail = 1; break; }
        size_t pitch = (size_t)rb + 16u, sz = pitch * rows * sl;
        uint8_t *src = malloc(sz), *out0 = calloc(1u, sz), *out1 = calloc(1u, sz);
        w6_fill(src, sz, d->width * 19u + l);
        nvn_deswizzle_w6_set_fast(0);
        if (nvn_swizzle(d, l, 0, src, pitch, st0) || nvn_deswizzle(d, l, 0, st0, out0, pitch)) fail++;
        nvn_deswizzle_w6_set_fast(1);
        if (nvn_swizzle(d, l, 0, src, pitch, st1) || nvn_deswizzle(d, l, 0, st1, out1, pitch)) fail++;
        if (memcmp(st0, st1, (size_t)L.size) || memcmp(out0, out1, sz)) fail++;
        if ((size_t)L.size >= 4096u && base + 4096u <= L.size) {
            uint8_t *sp0 = calloc(1u, (size_t)L.size), *sp1 = calloc(1u, (size_t)L.size);
            memset(out0, 0, sz); memset(out1, 0, sz);
            nvn_deswizzle_w6_set_fast(0);
            if (nvn_swizzle_span(d, l, 0, src, pitch, sp0, 512u, 2048u) ||
                nvn_deswizzle_span(d, l, 0, sp0, out0, pitch, 512u, 2048u)) fail++;
            nvn_deswizzle_w6_set_fast(1);
            if (nvn_swizzle_span(d, l, 0, src, pitch, sp1, 512u, 2048u) ||
                nvn_deswizzle_span(d, l, 0, sp1, out1, pitch, 512u, 2048u)) fail++;
            if (memcmp(sp0, sp1, (size_t)L.size) || memcmp(out0, out1, sz)) fail++;
            free(sp0); free(sp1);
        }
        if (rb >= 80u && rows >= 8u && l == 0u) {
            uint32_t rw = 64u < rb - 5u ? 64u : rb - 5u, rh = 4u < rows - 2u ? 4u : rows - 2u;
            size_t rsz = pitch * rh * sl;
            uint8_t *r0 = malloc(rsz), *r1 = malloc(rsz);
            w6_fill(r0, rsz, rw + 7u);
            memcpy(r1, r0, rsz);
            nvn_deswizzle_w6_set_fast(0);
            if (nvn_swizzle_region(d, 0, 0, 0u, 2u, 0u, rw, rh, 1u, r0, pitch, pitch * rh, st0) ||
                nvn_deswizzle_region(d, 0, 0, 0u, 2u, 0u, rw, rh, 1u, st0, r0, pitch, pitch * rh)) fail++;
            nvn_deswizzle_w6_set_fast(1);
            if (nvn_swizzle_region(d, 0, 0, 0u, 2u, 0u, rw, rh, 1u, r1, pitch, pitch * rh, st1) ||
                nvn_deswizzle_region(d, 0, 0, 0u, 2u, 0u, rw, rh, 1u, st1, r1, pitch, pitch * rh)) fail++;
            if (memcmp(st0, st1, (size_t)L.size) || memcmp(r0, r1, rsz)) fail++;
            free(r0); free(r1);
        }
        free(src); free(out0); free(out1);
    }
    free(st0); free(st1);
    return fail;
}

static int w6_bench_deswz(const char *tag, int fmt, uint32_t w, uint32_t h, int iters) {
    nvn_texdesc d = {.target = 1, .format = fmt, .width = w, .height = h, .depth = 1, .levels = 1};
    nvn_texlayout L;
    nvn_tex_layout(&d, &L);
    uint32_t rb, rows, sl, bpb; uint64_t base; unsigned th, td;
    if (nvn_deswizzle_geom(&d, 0, 0, &rb, &rows, &sl, &base, &th, &td, &bpb)) return 1;
    size_t sz = (size_t)rb * rows;
    uint8_t *st = calloc(1u, (size_t)L.size), *lin = malloc(sz);
    w6_fill(lin, sz, w ^ (uint32_t)fmt);
    nvn_deswizzle_w6_set_fast(1);
    if (nvn_swizzle(&d, 0, 0, lin, rb, st)) { free(st); free(lin); return 1; }
    volatile uint8_t sink = 0;

    nvn_deswizzle_w6_set_fast(0);
    uint64_t t0 = host_now_ns();
    for (int i = 0; i < iters; i++) { nvn_deswizzle(&d, 0, 0, st, lin, rb); sink ^= lin[(size_t)i & (sz - 1u)]; }
    uint64_t dt0 = host_now_ns() - t0;

    nvn_deswizzle_w6_set_fast(1);
    t0 = host_now_ns();
    for (int i = 0; i < iters; i++) { nvn_deswizzle(&d, 0, 0, st, lin, rb); sink ^= lin[(size_t)i & (sz - 1u)]; }
    uint64_t dt1 = host_now_ns() - t0;
    (void)sink;
    printf("aoc_deswz[%s]: iters=%d bytes=%zu legacy_ms=%.2f fast_ms=%.2f speedup=%.2fx\n",
           tag, iters, sz, dt0 / 1e6, dt1 / 1e6, dt1 ? (double)dt0 / (double)dt1 : 0.0);
    free(st); free(lin);
    return 0;
}

static int w6_bench_dirty_cmax(void) {
    enum { POOL_MB = 16, FAR_OFF_MB = 14, TEX_MB = 2, NQ = 100000 };
    size_t pg = (size_t)sysconf(_SC_PAGESIZE), sz = (size_t)POOL_MB << 20;
    uint8_t *pool = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (pool == MAP_FAILED) return 1;
    nvn_dirty_add_range(pool, sz);
    nvn_dirty_protect(pool, sz);
    uint64_t stamp = nvn_dirty_now();
    ((volatile uint8_t *)pool)[0] = 1u; /* force r->latest > stamp without touching far texture */

    const void *far_tex = pool + ((size_t)FAR_OFF_MB << 20);
    size_t far_sz = (size_t)TEX_MB << 20;
    volatile int sink = 0;

    nvn_dirty_w6_set_fast(0);
    uint64_t t0 = host_now_ns();
    for (int i = 0; i < NQ; i++) {
        int d = nvn_dirty_since(far_tex, far_sz, stamp);
        if (d) { nvn_dirty_remove_range(pool); munmap(pool, sz); return 1; }
        sink += d;
    }
    uint64_t dt0 = host_now_ns() - t0;

    nvn_dirty_w6_set_fast(1);
    t0 = host_now_ns();
    for (int i = 0; i < NQ; i++) {
        int d = nvn_dirty_since(far_tex, far_sz, stamp);
        if (d) { nvn_dirty_remove_range(pool); munmap(pool, sz); return 1; }
        sink += d;
    }
    uint64_t dt1 = host_now_ns() - t0;

    ((volatile uint8_t *)pool)[sz - pg] = 2u; /* write last page of far_tex -> caught under fault 15 */
    int dirty_after = nvn_dirty_since(far_tex, far_sz, stamp);
    nvn_dirty_remove_range(pool);
    munmap(pool, sz);
    (void)sink;
    printf("aoc_dirty_cmax: pool_mb=%d pg=%zu queries=%d leg_ms=%.2f fast_ms=%.2f speedup=%.2fx dirty_after=%d\n",
           POOL_MB, pg, NQ, dt0 / 1e6, dt1 / 1e6, dt1 ? (double)dt0 / (double)dt1 : 0.0, dirty_after);
    return dirty_after != 1;
}

static int run_ktgl_w6_tests(int fault) {
    (void)fault;
    const nvn_texdesc cases[] = {
        {.target = 1, .format = 0x25, .width = 197u, .height = 149u, .depth = 1u, .levels = 4u},
        {.target = 1, .format = 0x19, .width = 83u,  .height = 37u,  .depth = 1u, .levels = 2u},
        {.target = 1, .format = 0x4D, .width = 259u, .height = 131u, .depth = 1u, .levels = 3u},
        {.target = 2, .format = 0x25, .width = 64u,  .height = 33u,  .depth = 5u, .levels = 2u},
    };
    int fail = 0;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) fail += w6_check_deswz_case(&cases[i]);
    fail += w6_bench_deswz("1024_rgba8", 0x25, 1024u, 1024u, 200);
    fail += w6_bench_deswz("1024_bc7  ", 0x4D, 1024u, 1024u, 600);
    fail += w6_bench_dirty_cmax();
    return fail;
}
#endif
