#ifndef TEST_VK_KTGL_OPT_W30_H
#define TEST_VK_KTGL_OPT_W30_H
#include "vk_upload_pool.h"
#include "vk_upload_fast.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint64_t host_now_ns(void);
void totk_w30_pool_test_reset(int fast, int fault);

static int w30_ref_plan(const nvn_texdesc *d, const upload_sub *subs, unsigned nsubs,
                        uint64_t *texels, uint64_t *nruns, upload_run *out) {
    uint64_t bytes = 0u, n = 0u;
    for (unsigned i = 0u; i < nsubs; i++) {
        uint32_t row_bytes, rows, slices, bpb; uint64_t base; unsigned th, td;
        if (nvn_deswizzle_geom(d, subs[i].level, subs[i].layer, &row_bytes, &rows, &slices, &base, &th, &td, &bpb) ||
            subs[i].pitch < row_bytes)
            return -1;
        bytes += (uint64_t)row_bytes * rows * slices;
        uint64_t block_row = (row_bytes + 63) / 64 * (UINT64_C(512) << (th + td));
        uint64_t block_rows = ((uint64_t)rows + (8u << th) - 1) / (8u << th) * (((uint64_t)slices + (1u << td) - 1) >> td);
        uint64_t per_run = (UPLOAD_POOL_RUN_BYTES + block_row - 1) / block_row, total = block_row * block_rows;
        for (uint64_t first = 0u; first < total; first += per_run * block_row, n++) {
            if (out) {
                uint64_t cnt = (total - first < per_run * block_row) ? (total - first) : (per_run * block_row);
                out[n] = (upload_run){subs[i].level, subs[i].layer, subs[i].dst, subs[i].pitch, first, cnt};
            }
        }
    }
    *texels = bytes;
    *nruns = n;
    return 0;
}

static void w30_verify_math_identities(void) {
    static const uint32_t widths[8] = {1u, 13u, 63u, 64u, 65u, 255u, 1024u, 4096u};
    static const uint32_t heights[8] = {1u, 7u, 15u, 16u, 33u, 128u, 513u, 2048u};
    for (unsigned ti = 0u; ti <= 4u; ti++) {
        for (unsigned td = 0u; td <= 2u; td++) {
            for (unsigned wi = 0u; wi < 8u; wi++) {
                for (unsigned hi = 0u; hi < 8u; hi++) {
                    for (uint32_t bpb = 1u; bpb <= 16u; bpb *= 4u) {
                        nvn_texdesc d = {.target = 2, .format = 0x25, .width = widths[wi], .height = heights[hi],
                                         .depth = 1u + td * 4u, .levels = 1, .pkg_block_h = (int)((td << 4u) | ti) + 1};
                        upload_sub sub = {0u, 0u, (void *)(uintptr_t)0x1000u, (size_t)widths[wi] * 16u};
                        uint64_t tb0 = 0u, rn0 = 0u, tb1 = 0u, rn1 = 0u;
                        assert(w30_ref_plan(&d, &sub, 1u, &tb0, &rn0, NULL) == 0);
                        totk_upload_sub_geom g;
                        assert(totk_upload_geom_pass(&d, &sub, 1u, &tb1, &rn1, &g, 1u) == 0);
                        assert(tb0 == tb1 && rn0 == rn1);
                    }
                }
            }
        }
    }
}

static uint32_t w30_mip(uint32_t v, unsigned l) { uint32_t m = v >> l; return m ? m : 1u; }

static void w30_verify_upload_texture(nvn_texdesc d, int helpers) {
    nvn_texlayout L;
    nvn_tex_layout(&d, &L);
    uint8_t *src = (uint8_t *)malloc((size_t)L.size);
    assert(src != NULL);
    for (size_t i = 0u; i < L.size; i++) src[i] = (uint8_t)((i * 131u + 17u) & 0xffu);
    int bw = 0, bh = 0, bpb = nvn_fmt_bpb(d.format, &bw, &bh);
    upload_sub subs[128];
    size_t offs[128], tot = 0u;
    unsigned ns = 0u;
    uint32_t nlay = d.target == 2 ? 1u : L.layers;
    for (unsigned l = 0u; l < (unsigned)d.levels; l++) {
        uint32_t w = w30_mip(d.width, l), h = w30_mip(d.height, l), z = d.target == 2 ? w30_mip(d.depth, l) : 1u;
        size_t pitch = (size_t)((w + (unsigned)bw - 1u) / (unsigned)bw) * (unsigned)bpb + 5u;
        size_t rows = ((size_t)h + (unsigned)bh - 1u) / (unsigned)bh;
        for (unsigned y = 0u; y < nlay && ns < 128u; y++) {
            subs[ns] = (upload_sub){l, y, NULL, pitch};
            offs[ns++] = tot;
            tot += (pitch * rows * z + 15u) & ~(size_t)15u;
        }
    }
    uint8_t *out0 = (uint8_t *)malloc(tot + 32u), *out1 = (uint8_t *)malloc(tot + 32u);
    assert(out0 != NULL && out1 != NULL);
    memset(out0, 0xc7, tot + 32u); memset(out1, 0xc7, tot + 32u);
    for (unsigned i = 0u; i < ns; i++) subs[i].dst = out0 + offs[i];
    totk_upload_sub_geom geoms[128];
    uint64_t tb0 = 0u, rn0 = 0u, tb1 = 0u, rn1 = 0u;
    assert(w30_ref_plan(&d, subs, ns, &tb0, &rn0, NULL) == 0);
    assert(totk_upload_geom_pass(&d, subs, ns, &tb1, &rn1, geoms, 128u) == 0);
    assert(tb0 == tb1 && rn0 == rn1);
    upload_run *r0 = (upload_run *)malloc((size_t)(rn0 ? rn0 : 1u) * sizeof(r0[0]));
    upload_run *r1 = (upload_run *)malloc((size_t)(rn1 ? rn1 : 1u) * sizeof(r1[0]));
    assert(r0 != NULL && r1 != NULL);
    assert(w30_ref_plan(&d, subs, ns, &tb0, &rn0, r0) == 0);
    totk_upload_emit_runs(subs, ns, geoms, r1, rn1);
    if (rn0 > 0u) assert(memcmp(r0, r1, (size_t)rn0 * sizeof(r0[0])) == 0);
    int fmode = totk_w30_fault_mode();
    upload_pool_configure(helpers);
    assert(upload_pool_helpers() == helpers);
    totk_w30_pool_test_reset(0, 0);
    assert(upload_pool_deswizzle(&d, src, subs, ns, 0u) == 0);
    for (unsigned i = 0u; i < ns; i++) subs[i].dst = out1 + offs[i];
    totk_w30_pool_test_reset(1, fmode);
    assert(upload_pool_deswizzle(&d, src, subs, ns, 0u) == 0);
    assert(memcmp(out0, out1, tot + 32u) == 0);
    free(r0); free(r1); free(out0); free(out1); free(src);
}

static void w30_run_bench(void) {
    nvn_texdesc d = {.target = 1, .format = 0x25, .width = 1024, .height = 1024, .depth = 1, .levels = 11};
    upload_sub subs[11];
    totk_upload_sub_geom geoms[TOTK_UPLOAD_STACK_SUBS] = {0};
    upload_run stack_runs[TOTK_UPLOAD_STACK_RUNS] = {0};
    uint8_t dummy_buf[4096] = {0};
    for (unsigned l = 0u; l < 11u; l++) subs[l] = (upload_sub){l, 0u, dummy_buf, (size_t)(4096u >> l) > 4u ? (size_t)(4096u >> l) : 4u};
    uint64_t ref_sink = 0u;
    for (int fast = 0; fast < 2; fast++) {
        totk_vk_upload_fast_reset_for_test(fast, 0);
        totk_w30_pool_test_reset(fast, 0);
        upload_pool_configure(3);
        volatile uint64_t sink = 0u;
        uint64_t t0 = host_now_ns();
        for (int it = 0; it < 4000; it++) {
            uint64_t texels = 0u, n = 0u;
            int h = upload_pool_helpers();
            if (fast) {
                assert(totk_upload_geom_pass(&d, subs, 11u, &texels, &n, geoms, TOTK_UPLOAD_STACK_SUBS) == 0);
                totk_upload_emit_runs(subs, 11u, geoms, stack_runs, n);
                sink += (uint64_t)h + stack_runs[0].count + stack_runs[n - 1u].count;
            } else {
                assert(w30_ref_plan(&d, subs, 11u, &texels, &n, NULL) == 0);
                upload_run *dyn = (upload_run *)malloc((size_t)n * sizeof(*dyn));
                assert(dyn != NULL);
                assert(w30_ref_plan(&d, subs, 11u, &texels, &n, dyn) == 0);
                sink += (uint64_t)h + dyn[0].count + dyn[n - 1u].count;
                free(dyn);
            }
        }
        double ms = (double)(host_now_ns() - t0) / 1e6;
        uint64_t got = sink;
        if (fast == 0) ref_sink = got;
        else assert(got == ref_sink);
        printf("aoc_vk_upload_w30(%s): elapsed=%.3f ms sink=%llu\n", fast ? "on" : "off",
               ms, (unsigned long long)got);
    }
    upload_pool_configure(0);
}

static int run_ktgl_w30_tests(int fault_mode) {
    totk_vk_upload_fast_reset_for_test(1, fault_mode);
    totk_w30_pool_test_reset(1, fault_mode);
    if (fault_mode == 102) {
        upload_pool_configure(3);
        int h = upload_pool_helpers();
        upload_pool_configure(0);
        return h == 3 ? 0 : 1;
    }
    w30_verify_math_identities();
    w30_verify_upload_texture((nvn_texdesc){.target = 8, .format = 0x25, .width = 128, .height = 128, .depth = 6, .levels = 8}, 3);
    w30_verify_upload_texture((nvn_texdesc){.target = 2, .format = 0x25, .width = 128, .height = 96, .depth = 24, .levels = 4}, 1);
    w30_verify_upload_texture((nvn_texdesc){.target = 1, .format = 0x25, .width = 2048, .height = 2048, .depth = 1, .levels = 12}, 3);
    w30_verify_upload_texture((nvn_texdesc){.target = 4, .format = 0x25, .width = 1024, .height = 1024, .depth = 4, .levels = 1, .pkg_block_h = 1}, 3);
    upload_pool_configure(0);
    if (fault_mode < 102 || fault_mode > 106) w30_run_bench();
    return 0;
}

#endif
