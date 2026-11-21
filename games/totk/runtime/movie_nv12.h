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
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

static inline uint64_t totk_nv12_zip4(uint32_t u4, uint32_t v4, int fault) {
    uint64_t ux = (uint64_t)u4, vx = (uint64_t)v4;
    ux = (ux | (ux << 16)) & UINT64_C(0x0000ffff0000ffff);
    ux = (ux | (ux << 8)) & UINT64_C(0x00ff00ff00ff00ff);
    vx = (vx | (vx << 16)) & UINT64_C(0x0000ffff0000ffff);
    vx = (vx | (vx << 8)) & UINT64_C(0x00ff00ff00ff00ff);
    return fault ? (ux | (vx << 16)) : (ux | (vx << 8));
}
static inline int totk_nv12_interleave_simd(const uint8_t * restrict u, const uint8_t * restrict v,
                                            uint8_t * restrict o, int w2, int fault) {
    int x = 0;
#if defined(__aarch64__)
    for (; x + 16 <= w2; x += 16) {
        uint8x16_t uu = vld1q_u8(u + x), vv = vld1q_u8(v + x);
        uint8x16x2_t p = fault ? (uint8x16x2_t){{vv, uu}} : (uint8x16x2_t){{uu, vv}};
        vst2q_u8(o + 2 * x, p);
    }
#elif defined(__SSE2__)
    for (; x + 16 <= w2; x += 16) {
        __m128i uu = _mm_loadu_si128((const __m128i *)(u + x));
        __m128i vv = _mm_loadu_si128((const __m128i *)(v + x));
        if (fault) { __m128i tmp = uu; uu = vv; vv = tmp; }
        _mm_storeu_si128((__m128i *)(o + 2 * x), _mm_unpacklo_epi8(uu, vv));
        _mm_storeu_si128((__m128i *)(o + 2 * x + 16), _mm_unpackhi_epi8(uu, vv));
    }
#else
    (void)u; (void)v; (void)o; (void)w2; (void)fault;
#endif
    return x;
}
static inline void totk_nv12_interleave_row_ex(const uint8_t * restrict u, const uint8_t * restrict v,
                                              uint8_t * restrict o, int w2, int fast, int fault) {
    int x = fast ? totk_nv12_interleave_simd(u, v, o, w2, fault) : 0;
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (fast) {
        for (; x + 8 <= w2; x += 8) {
            uint32_t u0 = 0u, u1 = 0u, v0 = 0u, v1 = 0u;
            memcpy(&u0, u + x, sizeof u0); memcpy(&u1, u + x + 4, sizeof u1);
            memcpy(&v0, v + x, sizeof v0); memcpy(&v1, v + x + 4, sizeof v1);
            uint64_t q0 = totk_nv12_zip4(u0, v0, fault), q1 = totk_nv12_zip4(u1, v1, fault);
            memcpy(o + 2 * x, &q0, sizeof q0); memcpy(o + 2 * x + 8, &q1, sizeof q1);
        }
        if (x + 4 <= w2) {
            uint32_t u0 = 0u, v0 = 0u;
            memcpy(&u0, u + x, sizeof u0); memcpy(&v0, v + x, sizeof v0);
            uint64_t q0 = totk_nv12_zip4(u0, v0, fault);
            memcpy(o + 2 * x, &q0, sizeof q0);
            x += 4;
        }
    }
#else
    (void)fast; (void)fault;
#endif
    for (; x < w2; x++) {
        int sw = fast && fault;
        o[2 * x] = sw ? v[x] : u[x];
        o[2 * x + 1] = sw ? u[x] : v[x];
    }
}
static inline void totk_nv12_interleave_row(const uint8_t * restrict u, const uint8_t * restrict v,
                                           uint8_t * restrict o, int w2, int fast) {
    totk_nv12_interleave_row_ex(u, v, o, w2, fast, 0);
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
