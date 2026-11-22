/* Fast paths for GPU profiler key hashing/eq, texel LUT, window ranking, draw hashing, and pipeline helpers (Opt 46). */
#pragma once
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TOTK_W44_TEST_HOOKS
extern _Atomic int g_totk_vk_gpuprof_fast_override;
extern _Atomic int g_totk_w44_fault;
static inline void totk_vk_gpuprof_fast_set_for_test(int v) { atomic_store_explicit(&g_totk_vk_gpuprof_fast_override, v, memory_order_release); }
static inline void totk_w44_set_fault(int f) { atomic_store_explicit(&g_totk_w44_fault, f, memory_order_release); }
static inline int totk_w44_fault_mode(void) {
    int f = atomic_load_explicit(&g_totk_w44_fault, memory_order_relaxed);
    if (f >= 0) return f;
    const char *e = getenv("TOTK_W44_FAULT");
    int v = (e && *e) ? atoi(e) : 0;
    atomic_store_explicit(&g_totk_w44_fault, v, memory_order_relaxed);
    return v;
}
#else
static inline int totk_w44_fault_mode(void) { return 0; }
#endif
static inline int totk_vk_gpuprof_fast_on(void) {
#ifdef TOTK_W44_TEST_HOOKS
    int ov = atomic_load_explicit(&g_totk_vk_gpuprof_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
#endif
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("TOTK_VK_GPUPROF_FAST");
        v = !(e && strcmp(e, "0") == 0);
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
}

typedef struct { uint32_t format, w, h, load, store, bytes; } vk_gp_att_t;
static inline uint64_t vk_gp_fnv_u32(uint64_t h, uint32_t x) {
    const uint64_t P = UINT64_C(1099511628211);
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    if (x == 0u) return h * UINT64_C(0x9ffaac085635bc91);
    if (x <= 0xffu) return ((h ^ x) * P) * UINT64_C(0x08a97b0004e7feab);
    h = (h ^ (x & 0xffu)) * P; h = (h ^ ((x >> 8) & 0xffu)) * P;
    h = (h ^ ((x >> 16) & 0xffu)) * P; return (h ^ (x >> 24)) * P;
#else
    const uint8_t *b = (const uint8_t *)&x;
    for (size_t i = 0; i < 4; i++) { h ^= b[i]; h *= P; }
    return h;
#endif
}
static inline uint64_t vk_gp_fnv_u64(uint64_t h, uint64_t x) {
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    return vk_gp_fnv_u32(vk_gp_fnv_u32(h, (uint32_t)x), (uint32_t)(x >> 32));
#else
    const uint8_t *b = (const uint8_t *)&x;
    for (size_t i = 0; i < 8; i++) { h ^= b[i]; h *= UINT64_C(1099511628211); }
    return h;
#endif
}

static inline uint64_t vk_gp_key_hash_fast(uint64_t shader, uint32_t w, uint32_t h, uint32_t samples,
        uint32_t colors, uint32_t compute, const vk_gp_att_t a[10]) {
    static const uint64_t k_p20[11] = {
        UINT64_C(1), UINT64_C(0xe22cbbe2283778d1), UINT64_C(0x9e9c08ca72d29aa1), UINT64_C(0x117dc3a4d702b571),
        UINT64_C(0xc1be550d718a1941), UINT64_C(0x996b7c32028c1611), UINT64_C(0x82d22b93245cfbe1),
        UINT64_C(0x9f0d9b238b521ab1), UINT64_C(0xeda2ef649e91c281), UINT64_C(0x4f8f6410b9e34351),
        UINT64_C(0x785593a4687eed21)
    };
    uint64_t acc = vk_gp_fnv_u64(UINT64_C(14695981039346656037), shader);
    acc = vk_gp_fnv_u32(vk_gp_fnv_u32(vk_gp_fnv_u32(vk_gp_fnv_u32(vk_gp_fnv_u32(acc, w), h), samples), colors), compute);
    unsigned nz = 10u;
    while (nz > 0u && !(a[nz - 1u].format | a[nz - 1u].w | a[nz - 1u].h | a[nz - 1u].load | a[nz - 1u].store)) nz--;
    for (unsigned j = 0u; j < nz; j++) {
        const vk_gp_att_t *x = &a[j];
        if (!(x->format | x->w | x->h | x->load | x->store)) acc *= k_p20[1];
        else acc = vk_gp_fnv_u32(vk_gp_fnv_u32(vk_gp_fnv_u32(vk_gp_fnv_u32(vk_gp_fnv_u32(acc, x->format), x->w), x->h), x->load), x->store);
    }
    if (totk_w44_fault_mode() == 1 && nz < 10u) return acc;
    return acc * k_p20[10u - nz];
}
static inline int vk_gp_key_eq_fast(uint64_t as, uint32_t aw, uint32_t ah, uint32_t asmp, uint32_t ac, uint32_t acmp, const vk_gp_att_t aa[10],
        uint64_t bs, uint32_t bw, uint32_t bh, uint32_t bsmp, uint32_t bc, uint32_t bcmp, const vk_gp_att_t ba[10]) {
    if ((as ^ bs) | (uint64_t)(aw ^ bw) | (uint64_t)(ah ^ bh) | (uint64_t)(asmp ^ bsmp) | (uint64_t)(ac ^ bc) | (uint64_t)(acmp ^ bcmp)) return 0;
    unsigned lim = (totk_w44_fault_mode() == 2) ? 8u : 10u;
    for (unsigned j = 0u; j < lim; j++) {
        uint32_t d = (aa[j].format ^ ba[j].format) | (aa[j].w ^ ba[j].w) | (aa[j].h ^ ba[j].h) | (aa[j].load ^ ba[j].load) | (aa[j].store ^ ba[j].store);
        if (d) return 0;
    }
    return 1;
}

static inline uint32_t vk_gp_texel_bytes_fast(uint32_t f) {
    static const uint8_t lut[128] = {
        0,0,2,2,2,2,2,2,2, 1,1,1,1,1,1,1, 2,2,2,2,2,2,2, 3,3,3,3,3,3,3,3,3,3,3,3,3,3,
        4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
        2,2,2,2,2,2,2, 4,4,4,4,4,4,4, 6,6,6,6,6,6,6, 8,8,8,8,8,8,8,
        4,4,4, 8,8,8, 12,12,12, 16,16,16,
        0,0,0,0,0,0,0,0,0,0,0,0, 4,4, 2,4,4,1
    };
    if (totk_w44_fault_mode() == 3 && f == 126u) return 0u;
    return f < 128u ? (uint32_t)lut[f] : 0u;
}
typedef struct { double ms; uint64_t hash; uint32_t idx, pad; } vk_gp_rank_item_t;
static inline int vk_gp_rank_higher(const vk_gp_rank_item_t *a, const vk_gp_rank_item_t *b) {
    if (a->ms != b->ms) return a->ms > b->ms;
    return (totk_w44_fault_mode() == 4) ? (a->hash > b->hash) : (a->hash < b->hash);
}
static inline void vk_gp_sift_down(vk_gp_rank_item_t *a, size_t root, size_t end) {
    for (size_t step = 0u; step < 32u; step++) {
        size_t child = root * 2u + 1u;
        if (child >= end) break;
        if (child + 1u < end && vk_gp_rank_higher(&a[child], &a[child + 1u])) child++;
        if (!vk_gp_rank_higher(&a[root], &a[child])) break;
        vk_gp_rank_item_t tmp = a[root]; a[root] = a[child]; a[child] = tmp; root = child;
    }
}

static inline void vk_gp_rank_sort_fast(vk_gp_rank_item_t *a, size_t n) {
    if (n <= 1u) return;
    if (n <= 24u) {
        for (size_t i = 1u; i < n; i++) {
            vk_gp_rank_item_t key = a[i]; size_t j = i;
            while (j > 0u && vk_gp_rank_higher(&key, &a[j - 1u])) { a[j] = a[j - 1u]; j--; }
            a[j] = key;
        }
        return;
    }
    for (size_t i = n / 2u; i > 0u; i--) vk_gp_sift_down(a, i - 1u, n);
    for (size_t end = n - 1u; end > 0u; end--) {
        vk_gp_rank_item_t tmp = a[0]; a[0] = a[end]; a[end] = tmp;
        vk_gp_sift_down(a, 0u, end);
    }
}
static inline size_t vk_gp_rank_top64_fast(const vk_gp_rank_item_t *src, size_t n, vk_gp_rank_item_t out[64]) {
    size_t cap = n < 64u ? n : 64u;
    for (size_t i = 0u; i < cap; i++) out[i] = src[i];
    if (cap > 1u) for (size_t i = cap / 2u; i > 0u; i--) vk_gp_sift_down(out, i - 1u, cap);
    for (size_t i = cap; i < n; i++) {
        if (vk_gp_rank_higher(&src[i], &out[0])) { out[0] = src[i]; vk_gp_sift_down(out, 0u, cap); }
    }
    for (size_t end = cap; end > 1u; end--) {
        vk_gp_rank_item_t tmp = out[0]; out[0] = out[end - 1u]; out[end - 1u] = tmp;
        vk_gp_sift_down(out, 0u, end - 1u);
    }
    return cap;
}

static inline uint64_t vk_gp_draw_shaders_hash_fast(const char *const keys[6]) {
    uint64_t h = UINT64_C(14695981039346656037);
    const uint64_t P = UINT64_C(1099511628211);
    for (uint32_t i = 0u; i < 6u; i++) {
        if (!keys[i]) continue;
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
        h = ((h ^ i) * P) * UINT64_C(0x08a97b0004e7feab);
#else
        h = vk_gp_fnv_u32(h, i);
#endif
        const uint8_t *p = (const uint8_t *)keys[i];
        for (size_t k = 0u; k < 1024u; k++) { uint8_t c = p[k]; h = (h ^ c) * P; if (c == 0u) break; }
    }
    return h;
}
static inline uint32_t vk_gp_pipe_population_fast(uint32_t previous, uint32_t observed) {
    if (previous == 0u || previous >= observed || previous >= (UINT32_C(1) << 31)) return previous;
    uint32_t res = previous << (uint32_t)(__builtin_clz(previous) - __builtin_clz(observed));
    if (totk_w44_fault_mode() == 5) return res;
    if (res < observed && res < (UINT32_C(1) << 31)) res <<= 1;
    return res;
}
static inline int vk_gp_pipe_primitives_fast(int prim, uint32_t count, uint32_t instances, int indexed, uint64_t *out) {
    typedef struct { uint32_t mul; uint8_t shift, sub, post_sh, reject; } rule_t;
    static const rule_t k_rules[16] = {
        {1u,0,0,0,0}, {1u,1,0,0,0}, {0xaaaaaaabu,33,0,0,0}, {1u,0,1,0,0},
        {0xaaaaaaabu,33,0,0,0}, {1u,0,2,0,0}, {1u,0,2,0,0}, {1u,2,0,1,0},
        {1u,0,0,0,0}, {1u,1,0,0,0}, {0xaaaaaaabu,33,0,0,0}, {0u,0,0,0,1},
        {0xaaaaaaabu,33,0,0,0}, {0u,0,0,0,1}, {1u,0,2,0,0}, {0xaaaaaaabu,33,0,0,0}
    };
    rule_t r = k_rules[((unsigned)(indexed != 0) * 8u) + ((unsigned)prim < 8u ? (unsigned)prim : 4u)];
    if (r.reject && totk_w44_fault_mode() != 5) return 0;
    uint32_t adj = count > r.sub ? count - r.sub : 0u;
    *out = ((((uint64_t)adj * (uint64_t)r.mul) >> r.shift) << r.post_sh) * (uint64_t)instances;
    return 1;
}
