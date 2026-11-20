#ifndef TOTK_NVN_DESWIZZLE_FAST_H
#define TOTK_NVN_DESWIZZLE_FAST_H
/* Wave 6 (TOTK_DESWIZZLE_FAST=1): Odometer full-GOB block-linear kernels & hoisted
 * region-row block addressing. Zero divisions inside the GOB loop and region row loop. */
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static _Atomic int g_totk_deswz_fast_cached;

static inline int totk_deswz_fast_on(void) {
    int v = atomic_load_explicit(&g_totk_deswz_fast_cached, memory_order_relaxed);
    if (!v) {
        const char *e = getenv("TOTK_DESWIZZLE_FAST");
        v = (e && !strcmp(e, "0")) ? -1 : 1;
        atomic_store_explicit(&g_totk_deswz_fast_cached, v, memory_order_relaxed);
    }
    return v > 0;
}

static inline int totk_deswz_w6_fault(void) {
#ifdef W6_TEST_HOOKS
    const char *e = getenv("TOTK_KTGL_OPT_FAULT");
    return e ? atoi(e) : 0;
#else
    return 0;
#endif
}

static inline void totk_cp16_dir(uint8_t *bl_p, uint8_t *lin_p, int to_linear) {
    if (to_linear) memcpy(lin_p, bl_p, 16);
    else memcpy(bl_p, lin_p, 16);
}

static inline void totk_gob_x1_full(uint8_t *gob, uint8_t *lin0, size_t pitch, int to_linear) {
    for (unsigned r = 0; r < 4u; r++) {
        uint8_t *g0 = gob + ((size_t)r << 6);
        uint8_t *g1 = g0 + 256u;
        uint8_t *re = lin0 + (size_t)(2u * r) * pitch;
        uint8_t *ro = re + pitch;
        totk_cp16_dir(g0 + 0u, re + 0u, to_linear);
        totk_cp16_dir(g0 + 32u, re + 16u, to_linear);
        totk_cp16_dir(g1 + 0u, re + 32u, to_linear);
        totk_cp16_dir(g1 + 32u, re + 48u, to_linear);
        totk_cp16_dir(g0 + 16u, ro + 0u, to_linear);
        totk_cp16_dir(g0 + 48u, ro + 16u, to_linear);
        totk_cp16_dir(g1 + 16u, ro + 32u, to_linear);
        totk_cp16_dir(g1 + 48u, ro + 48u, to_linear);
    }
}

static inline void totk_gob_x1_edge(uint8_t *gob, uint8_t *lin0, size_t pitch,
                                    uint32_t rem_x, uint32_t rem_y, int to_linear, int fault) {
    static const uint16_t kox[4] = {0, 32, 256, 288};
    static const uint16_t koy[8] = {0, 16, 64, 80, 128, 144, 192, 208};
    uint32_t xmax = rem_x < 64u ? rem_x : 64u;
    uint32_t ymax = rem_y < 8u ? rem_y : 8u;
    uint32_t nfull = xmax >> 4, tail = xmax & 15u;
    for (uint32_t yy = 0; yy < ymax; yy++) {
        uint8_t *row = lin0 + (size_t)yy * pitch;
        uint8_t *gb = gob + koy[yy];
        for (uint32_t j = 0; j < nfull; j++) totk_cp16_dir(gb + kox[j], row + (j << 4), to_linear);
        if (tail && fault != 13) {
            uint8_t *p = gb + kox[nfull];
            if (to_linear) memcpy(row + (nfull << 4), p, tail);
            else memcpy(p, row + (nfull << 4), tail);
        }
    }
}

static inline void totk_deswz_move_fast(uint8_t *bl, uint8_t *lin, size_t pitch,
                                        uint32_t row_bytes, uint32_t rows, uint32_t slices,
                                        unsigned th, unsigned td, int to_linear,
                                        uint64_t first, uint64_t end) {
    uint32_t bh = 1u << th, bd = 1u << td;
    uint64_t gx = ((uint64_t)row_bytes + 63u) >> 6;
    uint64_t by = ((uint64_t)rows + (8u << th) - 1u) >> (3u + th);
    uint64_t gob = first >> 9, gob_end = end >> 9;
    uint64_t block = gob >> (th + td), within = gob & (((uint64_t)bh * bd) - 1u);
    uint32_t wy = (uint32_t)(within & (bh - 1u)), wz = (uint32_t)(within >> th);
    uint64_t row_blk = block / gx, bz_i = row_blk / by;
    uint32_t bx = (uint32_t)(block - row_blk * gx), by_i = (uint32_t)(row_blk - bz_i * by);
    int fault = totk_deswz_w6_fault();
    for (; gob < gob_end; gob++) {
        uint32_t x = bx << 6, y = ((by_i << th) + wy) << 3, z = ((uint32_t)bz_i << td) + wz;
        if (z < slices && x < row_bytes && y < rows) {
            uint8_t *gp = bl + (gob << 9);
            uint8_t *lp = lin + ((size_t)z * rows + y) * pitch + x;
            if (x + 64u <= row_bytes && y + 8u <= rows) totk_gob_x1_full(gp, lp, pitch, to_linear);
            else totk_gob_x1_edge(gp, lp, pitch, row_bytes - x, rows - y, to_linear, fault);
        }
        if (++wy == bh) {
            wy = 0;
            if (++wz == bd) {
                wz = 0;
                if (++bx == gx) {
                    bx = (fault == 12 && gx > 1u) ? 1u : 0u;
                    if (++by_i == by) { by_i = 0; bz_i++; }
                }
            }
        }
    }
}

static inline void totk_deswz_region_row(uint8_t *gob_row, uint8_t *row, uint64_t block_bytes,
                                         uint32_t x, uint32_t width, int to_linear, int fault) {
    size_t f = (fault == 14) ? 16u : 0u;
    for (uint32_t xx = 0; xx < width;) {
        uint32_t bx = x + xx, rem = width - xx;
        uint8_t *blk = gob_row + (uint64_t)(bx >> 6) * block_bytes;
        if (!(bx & 63u) && rem >= 64u) {
            totk_cp16_dir(blk + f, row + xx, to_linear);
            totk_cp16_dir(blk + 32u, row + xx + 16u, to_linear);
            totk_cp16_dir(blk + 256u, row + xx + 32u, to_linear);
            totk_cp16_dir(blk + 288u, row + xx + 48u, to_linear);
            xx += 64u;
        } else if (!(bx & 15u) && rem >= 16u) {
            totk_cp16_dir(blk + ((bx & 32u) << 3) + ((bx & 16u) << 1), row + xx, to_linear);
            xx += 16u;
        } else {
            uint32_t n = 16u - (bx & 15u);
            if (n > rem) n = rem;
            uint8_t *p = blk + ((bx & 32u) << 3) + ((bx & 16u) << 1) + (bx & 15u);
            if (to_linear) memcpy(row + xx, p, n); else memcpy(p, row + xx, n);
            xx += n;
        }
    }
}

static inline void totk_deswz_region_bl(uint8_t *bl, uint8_t *lin, size_t pitch, size_t image_pitch,
                                        uint32_t row_bytes, uint32_t rows, unsigned th, unsigned td,
                                        uint32_t x, uint32_t y, uint32_t z,
                                        uint32_t width, uint32_t height, uint32_t depth, int to_linear) {
    static const uint16_t koy[8] = {0, 16, 64, 80, 128, 144, 192, 208};
    uint32_t bd_mask = (1u << td) - 1u, bh_mask = (8u << th) - 1u;
    uint64_t block_bytes = UINT64_C(512) << (th + td);
    uint64_t row_blk = ((uint64_t)(row_bytes + 63u) >> 6) * block_bytes;
    uint64_t slice_blk = ((uint64_t)(rows + bh_mask) >> (3u + th)) * row_blk;
    int fault = totk_deswz_w6_fault();
    for (uint32_t zz = 0; zz < depth; zz++) {
        uint32_t gz = z + zz;
        uint64_t z_off = (uint64_t)(gz >> td) * slice_blk + (uint64_t)((gz & bd_mask) << th) * 512u;
        for (uint32_t yy = 0; yy < height; yy++) {
            uint32_t gy = y + yy;
            uint64_t y_off = z_off + (uint64_t)(gy >> (3u + th)) * row_blk +
                             (uint64_t)((gy & bh_mask) >> 3) * 512u + koy[gy & 7u];
            uint8_t *row = lin + (size_t)zz * image_pitch + (size_t)yy * pitch;
            totk_deswz_region_row(bl + y_off, row, block_bytes, x, width, to_linear, fault);
        }
    }
}
#endif
