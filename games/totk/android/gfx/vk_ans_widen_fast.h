/* Pure C11 helpers for bitwalked answers_of/native_bound_sync, 16-wide SIMD/scalar
   u8->u16 widener, streaming SIMD quad IB fill, attachment image bloom, RT span
   filter, MRU depth-size dedup & atomic live-WB gate (TOTK_VK_ANS_WIDEN_FAST=1). */
#pragma once
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#endif

#ifdef TOTK_W57_TEST_HOOKS
extern _Atomic int g_totk_vk_ans_widen_fast_override;
extern _Atomic int g_totk_w57_fault;
static inline void totk_vk_ans_widen_fast_set_for_test(int v) {
    atomic_store_explicit(&g_totk_vk_ans_widen_fast_override, v, memory_order_release);
}
static inline void totk_w57_set_fault(int f) {
    atomic_store_explicit(&g_totk_w57_fault, f, memory_order_release);
}
static inline int totk_w57_parse_fault(void) {
    const char *e = getenv("TOTK_W57_FAULT");
    if (e && *e) return atoi(e);
    const char *k = getenv("TOTK_KTGL_OPT_FAULT");
    if (k && *k) {
        int kv = atoi(k);
        if (kv >= 231 && kv <= 235) return kv - 230;
    }
    return 0;
}
static inline int totk_w57_fault_mode(void) {
    int f = atomic_load_explicit(&g_totk_w57_fault, memory_order_relaxed);
    if (f >= 0) return f;
    int v = totk_w57_parse_fault();
    atomic_store_explicit(&g_totk_w57_fault, v, memory_order_relaxed);
    return v;
}
#else
static inline int totk_w57_fault_mode(void) { return 0; }
#endif

static inline int totk_vk_ans_widen_fast_on(void) {
#ifdef TOTK_W57_TEST_HOOKS
    int ov = atomic_load_explicit(&g_totk_vk_ans_widen_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
#endif
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("TOTK_VK_ANS_WIDEN_FAST");
        v = (e && *e && atoi(e) == 0) ? 0 : 1;
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
}

/* 1. Bounded O(popcount) word extraction for answers_of & native_bound_sync. */
static inline uint64_t totk_ans_tex_word(uint64_t w, int wi, unsigned tex_hi) {
    unsigned base = (unsigned)wi * 64u;
    if (tex_hi <= base) return UINT64_C(0);
    unsigned rem = tex_hi - base;
    uint64_t m = rem >= 64u ? w : (w & ((UINT64_C(1) << rem) - UINT64_C(1)));
    if (totk_w57_fault_mode() == 1 && m) m &= m - UINT64_C(1);
    return m;
}

static inline uint32_t totk_ans_img_word(uint32_t m) {
    return m;
}

/* 2a. 16-wide NEON/SSE2 + scalar tail uint8 -> uint16 index widener (0xff -> 0xffff). */
static inline void totk_widen_u8_indices(uint16_t *dst, const uint8_t *src, uint32_t count) {
    uint32_t i = 0u;
    int f2 = (totk_w57_fault_mode() == 2);
#if defined(__ARM_NEON) && defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8x16_t ff = vdupq_n_u8(0xffu), zr = vdupq_n_u8(0u);
    for (; i + 16u <= count; i += 16u) {
        uint8x16_t v = vld1q_u8(src + i);
        uint8x16_t eq = f2 ? zr : vceqq_u8(v, ff);
        vst1q_u16(dst + i, vreinterpretq_u16_u8(vzip1q_u8(v, eq)));
        vst1q_u16(dst + i + 8u, vreinterpretq_u16_u8(vzip2q_u8(v, eq)));
    }
#elif defined(__SSE2__)
    __m128i ff = _mm_set1_epi8((char)0xff), zr = _mm_setzero_si128();
    for (; i + 16u <= count; i += 16u) {
        __m128i v, lo, hi;
        memcpy(&v, src + i, 16u);
        __m128i eq = f2 ? zr : _mm_cmpeq_epi8(v, ff);
        lo = _mm_unpacklo_epi8(v, eq);
        hi = _mm_unpackhi_epi8(v, eq);
        memcpy(dst + i, &lo, 16u);
        memcpy(dst + i + 8u, &hi, 16u);
    }
#endif
    for (; i < count; i++) {
        uint16_t b = (uint16_t)src[i];
        dst[i] = (!f2 && b == 0xffu) ? 0xffffu : b;
    }
}

/* 2b. Streaming 2-quad SIMD + scalar tail quad index buffer generator. */
static inline void totk_fill_quad_ib_u32(uint32_t *ix, uint32_t nquads) {
    uint32_t q = 0u;
#if defined(__ARM_NEON)
    static const uint32_t k0[12] = {0u, 1u, 2u, 0u, 2u, 3u, 4u, 5u, 6u, 4u, 6u, 7u};
    uint32x4_t c0 = vld1q_u32(k0), c1 = vld1q_u32(k0 + 4), c2 = vld1q_u32(k0 + 8);
    uint32x4_t step = vdupq_n_u32(8u);
    for (; q + 2u <= nquads; q += 2u) {
        vst1q_u32(ix + (size_t)q * 6u, c0);
        vst1q_u32(ix + (size_t)q * 6u + 4u, c1);
        vst1q_u32(ix + (size_t)q * 6u + 8u, c2);
        c0 = vaddq_u32(c0, step); c1 = vaddq_u32(c1, step); c2 = vaddq_u32(c2, step);
    }
#elif defined(__SSE2__)
    __m128i c0 = _mm_setr_epi32(0, 1, 2, 0), c1 = _mm_setr_epi32(2, 3, 4, 5), c2 = _mm_setr_epi32(6, 4, 6, 7);
    __m128i step = _mm_set1_epi32(8);
    for (; q + 2u <= nquads; q += 2u) {
        memcpy(ix + (size_t)q * 6u, &c0, 16u);
        memcpy(ix + (size_t)q * 6u + 4u, &c1, 16u);
        memcpy(ix + (size_t)q * 6u + 8u, &c2, 16u);
        c0 = _mm_add_epi32(c0, step); c1 = _mm_add_epi32(c1, step); c2 = _mm_add_epi32(c2, step);
    }
#endif
    for (; q < nquads; q++) {
        uint32_t v = q * 4u;
        ix[(size_t)q * 6u + 0u] = v; ix[(size_t)q * 6u + 1u] = v + 1u; ix[(size_t)q * 6u + 2u] = v + 2u;
        ix[(size_t)q * 6u + 3u] = v; ix[(size_t)q * 6u + 4u] = v + 2u; ix[(size_t)q * 6u + 5u] = v + 3u;
    }
}

/* 3a. 64-bit attachment VkImage bloom filter for feedback_cut. */
typedef struct {
    uint64_t bits;
    uint8_t count;
    uint8_t ready;
} totk_rt_fb_bloom_t;

static inline uint64_t totk_rt_ptr_bloom(uintptr_t p) {
    if (!p) return UINT64_C(0);
    uint64_t h = (uint64_t)p * UINT64_C(0x9e3779b97f4a7c15);
    h ^= h >> 30;
    return (UINT64_C(1) << ((h >> 58) & 63u)) | (UINT64_C(1) << ((h >> 52) & 63u));
}

static inline void totk_rt_fb_reset(totk_rt_fb_bloom_t *b) {
    b->bits = UINT64_C(0);
    b->count = 0u;
    b->ready = 0u;
}

static inline void totk_rt_fb_add(totk_rt_fb_bloom_t *b, uintptr_t img) {
    uint64_t m = totk_rt_ptr_bloom(img);
    if (m) { b->bits |= m; b->count++; }
}

static inline int totk_rt_fb_may_contain(const totk_rt_fb_bloom_t *b, uintptr_t img) {
    if (totk_w57_fault_mode() == 3) return 0;
    if (!b->count) return 0;
    uint64_t m = totk_rt_ptr_bloom(img);
    return (b->bits & m) == m;
}

/* 3b. Render-target memory span + serial bloom pre-filter for pass_merge_draw_hazard. */
typedef struct {
    uint64_t min_lo;
    uint64_t max_hi;
    uint64_t sbloom;
    uint8_t count;
    uint8_t bad;
    uint8_t built;
} totk_pm_rt_span_t;

static inline uint64_t totk_pm_ser_bloom(uint64_t serial) {
    uint64_t h = (serial + UINT64_C(1)) * UINT64_C(0xbf58476d1ce4e5b9);
    return (UINT64_C(1) << ((h >> 58) & 63u)) | (UINT64_C(1) << ((h >> 52) & 63u));
}

static inline void totk_pm_rt_add(totk_pm_rt_span_t *sp, uint64_t alo, uint64_t asz, uint64_t serial) {
    uint64_t ahi = alo + asz;
    sp->sbloom |= totk_pm_ser_bloom(serial);
    if (!asz || ahi < alo) { sp->bad = 1u; sp->count++; return; }
    if (!sp->count || alo < sp->min_lo) sp->min_lo = alo;
    if (!sp->count || ahi > sp->max_hi) sp->max_hi = ahi;
    sp->count++;
}

static inline int totk_pm_rt_may_hit(const totk_pm_rt_span_t *sp, uint64_t lo, uint64_t hi, uint64_t serial) {
    if (totk_w57_fault_mode() == 3) return 0;
    if (!sp->count) return 0;
    if (sp->bad) return 1;
    if (lo < sp->max_hi && sp->min_lo < hi) return 1;
    uint64_t sm = totk_pm_ser_bloom(serial);
    return (sp->sbloom & sm) == sm;
}

/* 4. MRU depth-size recorder & inline Ciura u32 sort for g_fr[16]. */
static inline void totk_fr_record_u32(uint32_t fr[16], int *n_p, uint32_t key) {
    int n = *n_p;
    if (n > 0 && (fr[n - 1] == key || (n > 1 && fr[0] == key))) return;
    for (int k = 1; k < n - 1; k++) if (fr[k] == key) return;
    if (n < 16) { fr[n] = key; *n_p = n + 1; }
}

static inline void totk_sort_u32_small(uint32_t *a, int n) {
    for (int i = 1; i < n; i++) {
        uint32_t v = a[i];
        int j = i;
        while (j > 0 && a[j - 1] > v) { a[j] = a[j - 1]; j--; }
        a[j] = v;
    }
    if (totk_w57_fault_mode() == 4 && n >= 2) {
        uint32_t t = a[0]; a[0] = a[n - 1]; a[n - 1] = t;
    }
}

/* 5. Atomic live write-back gate for vk_wb_record. */
static inline int totk_wb_has_live(int nwb) {
    if (totk_w57_fault_mode() == 5) return 0;
    return nwb > 0;
}
