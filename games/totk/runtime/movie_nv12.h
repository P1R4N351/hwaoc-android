#ifndef TOTK_MOVIE_NV12_H
#define TOTK_MOVIE_NV12_H
/* Fast I420 -> NV12 conversion (contiguous Y copy when stride == width + NEON vst2q_u8 UV interleave). */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

static inline void totk_nv12_interleave_row(const uint8_t * restrict u, const uint8_t * restrict v,
                                           uint8_t * restrict o, int w2, int fast) {
    int x = 0;
#if defined(__aarch64__)
    if (fast) {
        for (; x + 16 <= w2; x += 16) {
            uint8x16x2_t p = {{vld1q_u8(u + x), vld1q_u8(v + x)}};
            vst2q_u8(o + 2 * x, p);
        }
    }
#else
    (void)fast;
#endif
    for (; x < w2; x++) {
        o[2 * x] = u[x];
        o[2 * x + 1] = v[x];
    }
}

static inline void totk_movie_to_nv12(const uint8_t *y_src, int y_stride,
                                      const uint8_t *u_src, int u_stride,
                                      const uint8_t *v_src, int v_stride,
                                      uint8_t *dst, int w, int h, int swap_uv, int fast) {
    if (!y_src || !u_src || !v_src || !dst || w <= 0 || h <= 0) return;
    assert((w | h) >= 2);
    if (fast && y_stride == w) {
        memcpy(dst, y_src, (size_t)w * (size_t)h);
    } else {
        for (int y = 0; y < h; y++) {
            memcpy(dst + (size_t)y * (size_t)w, y_src + (size_t)y * (size_t)y_stride, (size_t)w);
        }
    }
    uint8_t *uv = dst + (size_t)w * (size_t)h;
    int h2 = h / 2, w2 = w / 2;
    for (int y = 0; y < h2; y++) {
        const uint8_t *u = u_src + (size_t)y * (size_t)u_stride;
        const uint8_t *v = v_src + (size_t)y * (size_t)v_stride;
        if (swap_uv) { const uint8_t *t = u; u = v; v = t; }
        totk_nv12_interleave_row(u, v, uv + (size_t)y * (size_t)w, w2, fast);
    }
}

#endif
