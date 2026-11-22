/* Fast paths for NVN/Vulkan format LUTs, block-linear layout math, mip-walk elision, and seqlock vpool backing (Opt 44). */
#pragma once
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t bpb, bw, bh;
} totk_fmt_fast_t;

typedef struct {
    _Atomic uint32_t seq;
    _Atomic uint32_t active_cnt;
    uint32_t count;
    uint16_t hash_slot[1024];
    _Atomic uint64_t live_mask[8];
    _Atomic uintptr_t va[512];
    _Atomic uintptr_t cpu[512];
    _Atomic uint64_t size[512];
} totk_vmap_fast_t;

#ifdef TOTK_W42_TEST_HOOKS
extern _Atomic int g_totk_tex_layout_fast_override;
extern _Atomic int g_totk_w42_fault;
#endif

static inline int totk_tex_layout_fast_on(void) {
#ifdef TOTK_W42_TEST_HOOKS
    int ov = atomic_load_explicit(&g_totk_tex_layout_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
    const char *e = getenv("TOTK_TEX_LAYOUT_FAST");
    return !(e && strcmp(e, "0") == 0);
#else
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("TOTK_TEX_LAYOUT_FAST");
        v = !(e && strcmp(e, "0") == 0);
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
#endif
}

static inline int totk_w42_fault_mode(void) {
#ifdef TOTK_W42_TEST_HOOKS
    int f = atomic_load_explicit(&g_totk_w42_fault, memory_order_relaxed);
    if (f >= 0) return f;
    const char *e = getenv("TOTK_W42_FAULT");
    int v = (e && *e) ? atoi(e) : 0;
    if (!v) {
        const char *k = getenv("TOTK_KTGL_OPT_FAULT");
        if (k && *k) { int kv = atoi(k); if (kv >= 156 && kv <= 160) v = kv - 155; }
    }
    atomic_store_explicit(&g_totk_w42_fault, v, memory_order_relaxed);
    return v;
#else
    return 0;
#endif
}

#ifdef TOTK_W42_TEST_HOOKS
static inline void totk_tex_layout_fast_set_for_test(int mode) {
    atomic_store_explicit(&g_totk_tex_layout_fast_override, mode, memory_order_release);
}
static inline void totk_w42_set_fault(int mode) {
    atomic_store_explicit(&g_totk_w42_fault, mode, memory_order_release);
}
#endif

#define TOTK_FMT_ROW4(b) {b, 1, 1}, {b, 1, 1}, {b, 1, 1}, {b, 1, 1}
#define TOTK_FMT_BC4(b) {b, 4, 4}, {b, 4, 4}, {b, 4, 4}, {b, 4, 4}
#define TOTK_FMT_ASTC14 \
    {16, 4, 4}, {16, 5, 4}, {16, 5, 5}, {16, 6, 5}, {16, 6, 6}, {16, 8, 5}, {16, 8, 6}, \
    {16, 8, 8}, {16, 10, 5}, {16, 10, 6}, {16, 10, 8}, {16, 10, 10}, {16, 12, 10}, {16, 12, 12}

static const totk_fmt_fast_t k_totk_nvn_fmt_lut[0x9D] = {
    [0x00] = {0, 1, 1},
    [0x01] = TOTK_FMT_ROW4(1),
    [0x05] = TOTK_FMT_ROW4(2), {2, 1, 1},
    [0x0A] = {4, 1, 1}, {4, 1, 1}, {4, 1, 1},
    [0x0D] = TOTK_FMT_ROW4(2),
    [0x11] = TOTK_FMT_ROW4(4), {4, 1, 1},
    [0x16] = {8, 1, 1}, {8, 1, 1}, {8, 1, 1},
    [0x19] = TOTK_FMT_ROW4(3),
    [0x1D] = TOTK_FMT_ROW4(6), {6, 1, 1},
    [0x22] = {12, 1, 1}, {12, 1, 1}, {12, 1, 1},
    [0x25] = TOTK_FMT_ROW4(4),
    [0x29] = TOTK_FMT_ROW4(8), {8, 1, 1},
    [0x2E] = {16, 1, 1}, {16, 1, 1}, {16, 1, 1},
    [0x31] = {1, 1, 1}, {2, 1, 1}, {4, 1, 1}, {4, 1, 1}, {4, 1, 1}, {8, 1, 1}, {4, 1, 1}, {4, 1, 1},
    [0x39] = TOTK_FMT_ROW4(2),
    [0x3D] = TOTK_FMT_ROW4(4),
    [0x41] = {8, 4, 4}, {8, 4, 4}, {16, 4, 4}, {16, 4, 4},
    [0x45] = {8, 4, 4}, {8, 4, 4}, {16, 4, 4}, {16, 4, 4},
    [0x49] = {8, 4, 4}, {8, 4, 4}, {16, 4, 4}, {16, 4, 4},
    [0x4D] = TOTK_FMT_BC4(16),
    [0x51] = {1, 1, 1}, {1, 1, 1}, {2, 1, 1}, {2, 1, 1}, {4, 1, 1}, {4, 1, 1},
    [0x57] = {2, 1, 1}, {2, 1, 1}, {4, 1, 1}, {4, 1, 1}, {8, 1, 1}, {8, 1, 1},
    [0x5D] = {3, 1, 1}, {3, 1, 1}, {6, 1, 1}, {6, 1, 1}, {12, 1, 1}, {12, 1, 1},
    [0x63] = {4, 1, 1}, {4, 1, 1}, {8, 1, 1}, {8, 1, 1}, {16, 1, 1}, {16, 1, 1},
    [0x69] = TOTK_FMT_ROW4(4),
    [0x6D] = TOTK_FMT_ROW4(4),
    [0x71] = TOTK_FMT_ROW4(8), {8, 1, 1},
    [0x76] = {16, 1, 1}, {16, 1, 1}, {16, 1, 1},
    [0x79] = TOTK_FMT_ASTC14,
    [0x87] = TOTK_FMT_ASTC14,
    [0x95] = TOTK_FMT_ROW4(2),
    [0x99] = TOTK_FMT_ROW4(4),
};

#undef TOTK_FMT_ASTC14
#undef TOTK_FMT_BC4
#undef TOTK_FMT_ROW4

static inline totk_fmt_fast_t totk_nvn_fmt_lookup(int f) {
    if ((unsigned)f >= 0x9Du) return (totk_fmt_fast_t){0, 1, 1};
    totk_fmt_fast_t t = k_totk_nvn_fmt_lut[f];
    if (totk_w42_fault_mode() == 1 && f >= 0x79 && f <= 0x94) t.bpb = 20u;
    return t;
}

static inline uint32_t totk_blk_fast(uint32_t s, uint32_t b) {
    uint32_t num = s + b - 1u;
    switch (b) {
        case 1u: return s;
        case 4u: return num >> 2;
        case 8u: return num >> 3;
        case 5u: return (uint32_t)(((uint64_t)num * UINT64_C(0xCCCCCCCD)) >> 34);
        case 6u:
            if (totk_w42_fault_mode() == 2) return s / 6u;
            return (uint32_t)(((uint64_t)num * UINT64_C(0xAAAAAAAB)) >> 34);
        case 10u: return (uint32_t)(((uint64_t)num * UINT64_C(0xCCCCCCCD)) >> 35);
        case 12u: return (uint32_t)(((uint64_t)num * UINT64_C(0xAAAAAAAB)) >> 35);
        default: return b ? num / b : 0u;
    }
}

static inline unsigned totk_pick_tile_fast(uint32_t gobs) {
    if (gobs < 2u) return 0u;
    if (gobs >= 16u) return 4u;
    return 31u - (unsigned)__builtin_clz(gobs);
}

static inline unsigned totk_adj_tile_fast(unsigned shift, uint32_t unit, uint32_t dim) {
    if (!shift || !unit || dim <= unit) return 0u;
    if (totk_w42_fault_mode() == 3) return shift;
    unsigned u_log2 = (unit == 8u) ? 3u : 0u;
    unsigned lim = 32u - u_log2 - (unsigned)__builtin_clz(dim - 1u);
    return shift < lim ? shift : lim;
}

static inline int totk_vmap_find_or_alloc(totk_vmap_fast_t *t, uintptr_t va) {
    uint32_t h = (uint32_t)(((uint64_t)(va >> 16) * UINT64_C(0x9E3779B1)) & 1023u);
    for (uint32_t step = 0u; step < 1024u; step++) {
        uint32_t sl = (h + step) & 1023u;
        uint16_t cur = t->hash_slot[sl];
        if (!cur) {
            if (t->count >= 512u) return -1;
            uint32_t idx = t->count++;
            t->hash_slot[sl] = (uint16_t)(idx + 1u);
            return (int)idx;
        }
        if (atomic_load_explicit(&t->va[cur - 1u], memory_order_relaxed) == va) return (int)(cur - 1u);
    }
    return -1;
}

static inline int totk_vmap_fast_note(totk_vmap_fast_t *t, uintptr_t va, uint64_t size, uintptr_t cpu) {
    int idx = totk_vmap_find_or_alloc(t, va);
    if (idx < 0) return -1;
    uint32_t wi = (uint32_t)idx >> 6;
    uint64_t bit = UINT64_C(1) << ((unsigned)idx & 63u);
    atomic_fetch_add_explicit(&t->seq, 1u, memory_order_acq_rel);
    uint64_t mask = atomic_load_explicit(&t->live_mask[wi], memory_order_relaxed);
    int was_live = (mask & bit) != 0u;
    atomic_store_explicit(&t->va[idx], va, memory_order_release);
    atomic_store_explicit(&t->size[idx], size, memory_order_release);
    atomic_store_explicit(&t->cpu[idx], cpu, memory_order_release);
    if (cpu && !was_live) {
        atomic_store_explicit(&t->live_mask[wi], mask | bit, memory_order_release);
        atomic_fetch_add_explicit(&t->active_cnt, 1u, memory_order_release);
    } else if (!cpu && was_live) {
        atomic_store_explicit(&t->live_mask[wi], mask & ~bit, memory_order_release);
        atomic_fetch_sub_explicit(&t->active_cnt, 1u, memory_order_release);
    }
    atomic_fetch_add_explicit(&t->seq, 1u, memory_order_release);
    return idx;
}

static inline int totk_vmap_scan_word(const totk_vmap_fast_t *t, uint32_t w, uintptr_t va, uint64_t len, uintptr_t *out_cpu) {
    uint64_t mask = atomic_load_explicit(&t->live_mask[w], memory_order_acquire);
    for (unsigned it = 0u; it < 64u && mask; it++) {
        unsigned b = (unsigned)__builtin_ctzll(mask);
        mask &= mask - 1u;
        uint32_t idx = w * 64u + b;
        uintptr_t e_cpu = atomic_load_explicit(&t->cpu[idx], memory_order_acquire);
        uintptr_t e_va = atomic_load_explicit(&t->va[idx], memory_order_acquire);
        uint64_t e_sz = atomic_load_explicit(&t->size[idx], memory_order_acquire);
        if (e_cpu && va >= e_va && len <= e_sz && (va - e_va) <= (e_sz - len)) {
            *out_cpu = e_cpu + (va - e_va);
            return 1;
        }
    }
    return 0;
}

static inline int totk_vmap_fast_lookup(const totk_vmap_fast_t *t, uintptr_t va, uint64_t len, uintptr_t *out_cpu) {
    for (int attempt = 0; attempt < 2; attempt++) {
        uint32_t s1 = atomic_load_explicit(&t->seq, memory_order_acquire);
        if (s1 & 1u) continue;
        if (atomic_load_explicit(&t->active_cnt, memory_order_acquire) == 0u) {
            if (s1 == atomic_load_explicit(&t->seq, memory_order_acquire)) return 0;
            continue;
        }
        uintptr_t cand = 0;
        int hit = 0;
        for (uint32_t w = 0u; w < 8u && !hit; w++) hit = totk_vmap_scan_word(t, w, va, len, &cand);
        uint32_t s2 = atomic_load_explicit(&t->seq, memory_order_acquire);
        if (s1 == s2) {
            if (!hit) return 0;
            if (totk_w42_fault_mode() == 5) return 0;
            *out_cpu = cand;
            return 1;
        }
    }
    return -1;
}
