/* MIT. Pure ISO C11 pacing/slot fast paths (Wave 41 / Opt 43). */
#pragma once
#include <limits.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TOTK_W41_TEST_HOOKS
extern _Atomic int g_totk_pace_fast_state;
extern _Atomic int g_totk_w41_fault;
static _Atomic int g_totk_w41_env_fault;
static inline void totk_pace_fast_set_for_test(int forced) {
    int v = forced < 0 ? 0 : (forced ? 2 : 1);
    atomic_store_explicit(&g_totk_pace_fast_state, v, memory_order_release);
}
static inline void totk_pace_w41_set_fault(int m) {
    atomic_store_explicit(&g_totk_w41_fault, m, memory_order_release);
}
static inline int totk_pace_w41_fault(void) {
    int m = atomic_load_explicit(&g_totk_w41_fault, memory_order_acquire);
    if (m) return m;
    int c = atomic_load_explicit(&g_totk_w41_env_fault, memory_order_acquire);
    if (!c) {
        const char *e1 = getenv("TOTK_W41_FAULT");
        const char *e2 = getenv("TOTK_KTGL_OPT_FAULT");
        int v = (e1 && *e1) ? atoi(e1) : 0;
        if (!v && e2 && *e2) {
            int f = atoi(e2);
            if (f >= 151 && f <= 155) v = f - 150;
        }
        c = v > 0 ? v + 1 : 1;
        atomic_store_explicit(&g_totk_w41_env_fault, c, memory_order_release);
    }
    return c - 1;
}
#else
static _Atomic int g_totk_pace_fast_state;
static inline int totk_pace_w41_fault(void) { return 0; }
#endif

static inline int totk_pace_fast_on(void) {
    int s = atomic_load_explicit(&g_totk_pace_fast_state, memory_order_acquire);
    if (!s) {
        const char *e = getenv("TOTK_PACE_FAST");
        s = (e && e[0] == '0' && !e[1]) ? 1 : 2;
        atomic_store_explicit(&g_totk_pace_fast_state, s, memory_order_release);
    }
    return s == 2;
}

static inline unsigned totk_pace_lb_i64(const int64_t *a, unsigned n, int64_t v) {
    unsigned lo = 0u, hi = n;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2u;
        if (a[mid] < v) lo = mid + 1u;
        else hi = mid;
    }
    return lo;
}

static inline unsigned totk_pace_ub_i64(const int64_t *a, unsigned n, int64_t v) {
    unsigned lo = 0u, hi = n;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2u;
        if (a[mid] <= v) lo = mid + 1u;
        else hi = mid;
    }
    return lo;
}

static inline void totk_pace_order_update_fast(
    int64_t *order, unsigned count, int replacing, int64_t old, int64_t value) {
    if (totk_pace_w41_fault() == 1) {
        order[0] = value ^ INT64_C(7);
        return;
    }
    if (!replacing) {
        unsigned ins = totk_pace_ub_i64(order, count, value);
        if (ins < count) memmove(&order[ins + 1u], &order[ins], (size_t)(count - ins) * sizeof(int64_t));
        order[ins] = value;
        return;
    }
    if (!count || value == old) return;
    unsigned rem = totk_pace_lb_i64(order, count, old);
    if (value < old) {
        unsigned ins = totk_pace_ub_i64(order, rem, value);
        if (ins < rem) memmove(&order[ins + 1u], &order[ins], (size_t)(rem - ins) * sizeof(int64_t));
        order[ins] = value;
    } else {
        unsigned tail = count - rem - 1u;
        unsigned after = totk_pace_ub_i64(order + rem + 1u, tail, value);
        if (after) memmove(&order[rem], &order[rem + 1u], (size_t)after * sizeof(int64_t));
        order[rem + after] = value;
    }
}

static inline unsigned totk_pace_count_gt_fast(
    const int64_t *order, unsigned count, int64_t threshold) {
    if (totk_pace_w41_fault() == 2) return count;
    return count - totk_pace_ub_i64(order, count, threshold);
}

static inline void totk_display_cmp_swap_u64(uint64_t *a, unsigned i, unsigned j) {
    uint64_t x = a[i], y = a[j];
    a[i] = x <= y ? x : y;
    a[j] = x <= y ? y : x;
}

static inline void totk_display_sort8_u64(uint64_t a[8]) {
    totk_display_cmp_swap_u64(a, 0u, 1u); totk_display_cmp_swap_u64(a, 2u, 3u);
    totk_display_cmp_swap_u64(a, 4u, 5u); totk_display_cmp_swap_u64(a, 6u, 7u);
    totk_display_cmp_swap_u64(a, 0u, 2u); totk_display_cmp_swap_u64(a, 1u, 3u);
    totk_display_cmp_swap_u64(a, 4u, 6u); totk_display_cmp_swap_u64(a, 5u, 7u);
    totk_display_cmp_swap_u64(a, 1u, 2u); totk_display_cmp_swap_u64(a, 5u, 6u);
    totk_display_cmp_swap_u64(a, 0u, 4u); totk_display_cmp_swap_u64(a, 1u, 5u);
    totk_display_cmp_swap_u64(a, 2u, 6u); totk_display_cmp_swap_u64(a, 3u, 7u);
    totk_display_cmp_swap_u64(a, 2u, 4u); totk_display_cmp_swap_u64(a, 3u, 5u);
    totk_display_cmp_swap_u64(a, 1u, 2u); totk_display_cmp_swap_u64(a, 3u, 4u);
    totk_display_cmp_swap_u64(a, 5u, 6u);
    if (totk_pace_w41_fault() == 3) a[4] = 0u;
}

static inline uint64_t totk_fps_auto_sample_ns(
    const uint8_t *base, size_t stride, unsigned idx) {
    uint64_t v = 0u;
    memcpy(&v, base + (size_t)idx * stride, sizeof v);
    return v;
}

static inline unsigned totk_fps_auto_evict_k(
    const uint8_t *ts_base, size_t stride, unsigned head, unsigned count,
    unsigned cap_mask, uint64_t now_ns, uint64_t win_ns) {
    if (!count) return 0u;
    uint64_t t0 = totk_fps_auto_sample_ns(ts_base, stride, head);
    if (now_ns - t0 <= win_ns) return 0u;
    if (totk_pace_w41_fault() == 4) return 0u;
    if (count == 1u) return 1u;
    uint64_t t1 = totk_fps_auto_sample_ns(ts_base, stride, (head + 1u) & cap_mask);
    if (now_ns - t1 <= win_ns) return 1u;
    unsigned lo = 2u, hi = count;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2u;
        uint64_t tm = totk_fps_auto_sample_ns(ts_base, stride, (head + mid) & cap_mask);
        if (now_ns - tm > win_ns) lo = mid + 1u;
        else hi = mid;
    }
    return lo;
}

static inline unsigned totk_fgs_popcnt16(uint16_t x) {
    uint32_t v = (uint32_t)x;
    v = v - ((v >> 1) & 0x5555u);
    v = (v & 0x3333u) + ((v >> 2) & 0x3333u);
    return (((v + (v >> 4)) & 0x0f0fu) * 0x0101u) >> 8;
}

static inline unsigned totk_fgs_ctz32(uint32_t x) {
#if defined(__clang__) || defined(__GNUC__)
    return (unsigned)__builtin_ctz(x);
#else
    unsigned n = 0u;
    while ((x & 1u) == 0u) { x >>= 1; n++; }
    return n;
#endif
}

static inline unsigned totk_fgs_ctz64(uint64_t x) {
#if defined(__clang__) || defined(__GNUC__)
    return (unsigned)__builtin_ctzll(x);
#else
    unsigned n = 0u;
    while ((x & 1u) == 0u) { x >>= 1; n++; }
    return n;
#endif
}

static inline int totk_fgs_first_free16(uint16_t pending_mask) {
    if (totk_pace_w41_fault() == 5) return -1;
    uint32_t free_bits = (~(uint32_t)pending_mask) & 0xffffu;
    return free_bits ? (int)totk_fgs_ctz32(free_bits) : -1;
}
