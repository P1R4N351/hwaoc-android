/* Pure C11 helpers for 2D/3D copy coalescing, stable bottom-up span merge sort &
   endpoint Ciura shell-sort, branchless span_bsearch, min-fence ub/gw prune gates
   & 4-lane SIMD low-32 handle diffing (TOTK_VK_COPY_SPAN_FAST=1). */
#pragma once
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if defined(__ARM_NEON) && defined(__aarch64__)
#include <arm_neon.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#endif

#ifdef TOTK_W58_TEST_HOOKS
extern _Atomic int g_totk_vk_copy_span_fast_override;
extern _Atomic int g_totk_w58_fault;
static inline void totk_vk_copy_span_fast_set_for_test(int v) {
    atomic_store_explicit(&g_totk_vk_copy_span_fast_override, v, memory_order_release);
}
static inline void totk_w58_set_fault(int f) {
    atomic_store_explicit(&g_totk_w58_fault, f, memory_order_release);
}
static inline int totk_w58_parse_fault(void) {
    const char *e = getenv("TOTK_W58_FAULT");
    if (e && *e) return atoi(e);
    const char *k = getenv("TOTK_KTGL_OPT_FAULT");
    if (k && *k) {
        int kv = atoi(k);
        if (kv >= 236 && kv <= 240) return kv - 235;
    }
    return 0;
}
static inline int totk_w58_fault_mode(void) {
    int f = atomic_load_explicit(&g_totk_w58_fault, memory_order_relaxed);
    if (f >= 0) return f;
    int v = totk_w58_parse_fault();
    atomic_store_explicit(&g_totk_w58_fault, v, memory_order_relaxed);
    return v;
}
#else
static inline int totk_w58_fault_mode(void) { return 0; }
#endif

static inline int totk_vk_copy_span_fast_on(void) {
#ifdef TOTK_W58_TEST_HOOKS
    int ov = atomic_load_explicit(&g_totk_vk_copy_span_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
#endif
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("TOTK_VK_COPY_SPAN_FAST");
        v = (e && *e && atoi(e) == 0) ? 0 : 1;
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
}

/* 1. 2D/3D contiguous texel memcpy coalescing (upload_staged, read_texels, write_texels). */
static inline void totk_memcpy_2d(uint8_t *dst, size_t drow, const uint8_t *src, size_t srow,
                                  size_t wb, size_t rows) {
    if (!wb || !rows) return;
    if (rows == 1u || (srow == wb && drow == wb)) {
        size_t total = wb * rows;
        if (totk_w58_fault_mode() == 1 && total >= 2u) total >>= 1;
        memcpy(dst, src, total);
        return;
    }
    for (size_t y = 0u; y < rows; y++) memcpy(dst + y * drow, src + y * srow, wb);
}

static inline void totk_memcpy_3d(uint8_t *dst, size_t drow, size_t dimg, const uint8_t *src,
                                  size_t srow, size_t simg, size_t wb, size_t rows, size_t nz) {
    if (!wb || !rows || !nz) return;
    int row_c = (rows == 1u) || (srow == wb && drow == wb);
    size_t slice_b = wb * rows;
    if (row_c && (nz == 1u || (simg == slice_b && dimg == slice_b))) {
        size_t total = slice_b * nz;
        if (totk_w58_fault_mode() == 1 && total >= 2u) total >>= 1;
        memcpy(dst, src, total);
        return;
    }
    if (row_c) {
        for (size_t z = 0u; z < nz; z++) memcpy(dst + z * dimg, src + z * simg, slice_b);
        return;
    }
    for (size_t z = 0u; z < nz; z++) for (size_t y = 0u; y < rows; y++)
        memcpy(dst + z * dimg + y * drow, src + z * simg + y * srow, wb);
}

/* 2. Layout-verified 32B span stable merge-sort & 16B endpoint Ciura shell-sort + dedup. */
typedef struct {
    uintptr_t lo;
    uintptr_t end;
    uintptr_t p2;
    uint64_t off;
} totk_span32_t;

typedef struct {
    uintptr_t lo;
    int best;
} totk_span_ep_t;

static inline uintptr_t totk_span_lo_at(const void *base, int idx) {
    uintptr_t lo;
    memcpy(&lo, (const uint8_t *)base + (size_t)idx * sizeof(totk_span32_t), sizeof lo);
    return lo;
}

static inline void totk_ins_sort_spans(void *base, int lo, int hi) {
    uint8_t *p = (uint8_t *)base;
    for (int i = lo + 1; i < hi; i++) {
        totk_span32_t key;
        memcpy(&key, p + (size_t)i * sizeof key, sizeof key);
        int j = i;
        for (; j > lo && totk_span_lo_at(p, j - 1) > key.lo; j--) {
            memcpy(p + (size_t)j * sizeof key, p + (size_t)(j - 1) * sizeof key, sizeof key);
        }
        memcpy(p + (size_t)j * sizeof key, &key, sizeof key);
    }
}

static inline void totk_merge_span_runs(const void *src_p, void *dst_p, int n, int width) {
    const uint8_t *src = (const uint8_t *)src_p;
    uint8_t *dst = (uint8_t *)dst_p;
    const size_t sz = sizeof(totk_span32_t);
    for (int lo = 0; lo < n; lo += width * 2) {
        int mid = lo + width < n ? lo + width : n;
        int hi = lo + width * 2 < n ? lo + width * 2 : n;
        int i = lo, j = mid, k = lo;
        for (; i < mid && j < hi; k++) {
            int take_left = totk_span_lo_at(src, i) <= totk_span_lo_at(src, j);
            memcpy(dst + (size_t)k * sz, src + (size_t)(take_left ? i++ : j++) * sz, sz);
        }
        if (i < mid) memcpy(dst + (size_t)k * sz, src + (size_t)i * sz, (size_t)(mid - i) * sz);
        if (j < hi) memcpy(dst + (size_t)k * sz, src + (size_t)j * sz, (size_t)(hi - j) * sz);
    }
}

static inline void totk_sort_spans_stable(void *base, void *tmp_buf, int n) {
    int f2 = (totk_w58_fault_mode() == 2);
    if (n <= 1) return;
    for (int b = 0; b < n; b += 16) totk_ins_sort_spans(base, b, b + 16 < n ? b + 16 : n);
    int src_is_base = 1;
    for (int step = 0, w = 16; step < 28 && w < n; step++, w <<= 1) {
        if (src_is_base) totk_merge_span_runs(base, tmp_buf, n, w);
        else totk_merge_span_runs(tmp_buf, base, n, w);
        src_is_base = !src_is_base;
    }
    if (!src_is_base) memcpy(base, tmp_buf, (size_t)n * sizeof(totk_span32_t));
    if (f2) {
        totk_span32_t a, b;
        uint8_t *p = (uint8_t *)base;
        memcpy(&a, p, sizeof a);
        memcpy(&b, p + (size_t)(n - 1) * sizeof b, sizeof b);
        memcpy(p, &b, sizeof b);
        memcpy(p + (size_t)(n - 1) * sizeof a, &a, sizeof a);
    }
}

static inline void totk_uptr_set(uint8_t *p, int i, uintptr_t v) {
    memcpy(p + (size_t)i * sizeof v, &v, sizeof v);
}

static inline uintptr_t totk_uptr_at(const uint8_t *p, int i) {
    uintptr_t v;
    memcpy(&v, p + (size_t)i * sizeof v, sizeof v);
    return v;
}

static inline void totk_ins_sort_uptr(uint8_t *k, int lo, int hi) {
    for (int i = lo + 1; i < hi; i++) {
        uintptr_t v = totk_uptr_at(k, i);
        int j = i;
        for (; j > lo && totk_uptr_at(k, j - 1) > v; j--) totk_uptr_set(k, j, totk_uptr_at(k, j - 1));
        totk_uptr_set(k, j, v);
    }
}

static inline void totk_merge_uptr_runs(const uint8_t *src, uint8_t *dst, int n, int w) {
    for (int lo = 0; lo < n; lo += w * 2) {
        int mid = lo + w < n ? lo + w : n, hi = lo + w * 2 < n ? lo + w * 2 : n;
        int i = lo, j = mid, k = lo;
        for (; i < mid && j < hi; k++) {
            uintptr_t a = totk_uptr_at(src, i), b = totk_uptr_at(src, j);
            if (a <= b) { totk_uptr_set(dst, k, a); i++; }
            else { totk_uptr_set(dst, k, b); j++; }
        }
        if (i < mid) memcpy(dst + (size_t)k * sizeof(uintptr_t), src + (size_t)i * sizeof(uintptr_t), (size_t)(mid - i) * sizeof(uintptr_t));
        if (j < hi) memcpy(dst + (size_t)k * sizeof(uintptr_t), src + (size_t)j * sizeof(uintptr_t), (size_t)(hi - j) * sizeof(uintptr_t));
    }
}

static inline int totk_sort_dedup_ep(void *ep_buf, int n) {
    int f2 = (totk_w58_fault_mode() == 2);
    if (n <= 0) return 0;
    uint8_t *k0 = (uint8_t *)ep_buf, *k1 = k0 + (size_t)n * sizeof(uintptr_t);
    for (int i = 0; i < n; i++) {
        uintptr_t lo;
        memcpy(&lo, k0 + (size_t)i * sizeof(totk_span_ep_t), sizeof lo);
        memcpy(k0 + (size_t)i * sizeof lo, &lo, sizeof lo);
    }
    for (int b = 0; b < n; b += 16) totk_ins_sort_uptr(k0, b, b + 16 < n ? b + 16 : n);
    int in_k0 = 1;
    for (int step = 0, w = 16; step < 28 && w < n; step++, w <<= 1) {
        if (in_k0) totk_merge_uptr_runs(k0, k1, n, w);
        else totk_merge_uptr_runs(k1, k0, n, w);
        in_k0 = !in_k0;
    }
    if (in_k0) memcpy(k1, k0, (size_t)n * sizeof(uintptr_t));
    int out_n = 0;
    uintptr_t prev_lo = 0;
    for (int i = 0; i < n; i++) {
        uintptr_t v = totk_uptr_at(k1, i);
        if (!out_n || v != prev_lo) {
            totk_span_ep_t out = {v, -1};
            memcpy(k0 + (size_t)out_n * sizeof out, &out, sizeof out);
            prev_lo = v;
            out_n++;
        }
    }
    return (f2 && out_n >= 2) ? out_n - 1 : out_n;
}

/* 3. Branchless upper-bound binary search for span_bsearch. */
static inline int totk_span_bsearch_ep(const void *seg_buf, int nseg, uintptr_t a) {
    int f3 = (totk_w58_fault_mode() == 3);
    if (nseg <= 0) return f3 ? 1 : 0;
    const uint8_t *p = (const uint8_t *)seg_buf;
    const size_t sz = sizeof(totk_span_ep_t);
    int base = 0, len = nseg;
    for (int step = 0; step < 31 && len > 1; step++) {
        int half = len >> 1;
        uintptr_t lo;
        memcpy(&lo, p + (size_t)(base + half) * sz, sizeof lo);
        base += (lo <= a) ? half : 0;
        len -= half;
    }
    uintptr_t lo0;
    memcpy(&lo0, p + (size_t)base * sz, sizeof lo0);
    int res = base + (lo0 <= a ? 1 : 0);
    return f3 ? (res > 0 ? res - 1 : nseg) : res;
}

/* 4. O(1) minimum-sealed-fence prune gates for ub_prune_inner & gw_prune. */
static inline void totk_fence_min_note(uint64_t *minf, uint64_t fence) {
    if (fence && fence < *minf) *minf = fence;
}

static inline int totk_fence_can_prune(int count, uint64_t minf, uint64_t done) {
    if (totk_w58_fault_mode() == 4) return 0;
    return count > 0 && minf != UINT64_MAX && minf <= done;
}

/* 5. 4-lane SIMD low-32 handle diffing for exec_bind_tex & OP_BIND_SAMP. */
static inline int totk_u64_low32_changed(const uint64_t *old_h, const uint64_t *new_h, int n) {
    if (totk_w58_fault_mode() == 5) return 0;
    int i = 0;
#if defined(__ARM_NEON) && defined(__aarch64__)
    for (; i + 4 <= n; i += 4) {
        uint64x2_t o0 = vld1q_u64(old_h + i), n0 = vld1q_u64(new_h + i);
        uint64x2_t o1 = vld1q_u64(old_h + i + 2), n1 = vld1q_u64(new_h + i + 2);
        uint64x2_t d0 = vshlq_n_u64(veorq_u64(o0, n0), 32);
        uint64x2_t d1 = vshlq_n_u64(veorq_u64(o1, n1), 32);
        if (vmaxvq_u32(vreinterpretq_u32_u64(vorrq_u64(d0, d1))) != 0u) return 1;
    }
#elif defined(__SSE2__)
    __m128i zr = _mm_setzero_si128();
    for (; i + 4 <= n; i += 4) {
        __m128i o0, n0, o1, n1, d0, d1, m;
        memcpy(&o0, old_h + i, 16u); memcpy(&n0, new_h + i, 16u);
        memcpy(&o1, old_h + i + 2, 16u); memcpy(&n1, new_h + i + 2, 16u);
        d0 = _mm_slli_epi64(_mm_xor_si128(o0, n0), 32);
        d1 = _mm_slli_epi64(_mm_xor_si128(o1, n1), 32);
        m = _mm_or_si128(d0, d1);
        if (_mm_movemask_epi8(_mm_cmpeq_epi32(m, zr)) != 0xffff) return 1;
    }
#endif
    for (; i < n; i++) if ((uint32_t)old_h[i] != (uint32_t)new_h[i]) return 1;
    return 0;
}
