/* Fast paths for TBV buffer release bloom filter, 44B seqlock sampler shadow, sRGB twin LUT & xlate hashing/fields (Opt 51). */
#ifndef VK_RES_SAMP_FAST_H
#define VK_RES_SAMP_FAST_H
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TOTK_W49_TEST_HOOKS
extern _Atomic int g_totk_vk_res_samp_fast_override;
extern _Atomic int g_totk_w49_fault;
static inline void totk_vk_res_samp_fast_set_for_test(int v) {
    atomic_store_explicit(&g_totk_vk_res_samp_fast_override, v, memory_order_release);
}
static inline void totk_w49_set_fault(int f) {
    atomic_store_explicit(&g_totk_w49_fault, f, memory_order_release);
}
static inline int totk_w49_fault_mode(void) {
    int f = atomic_load_explicit(&g_totk_w49_fault, memory_order_relaxed);
    if (f >= 0) return f;
    const char *e = getenv("TOTK_W49_FAULT");
    int v = (e && *e) ? atoi(e) : 0;
    atomic_store_explicit(&g_totk_w49_fault, v, memory_order_relaxed);
    return v;
}
#else
static inline int totk_w49_fault_mode(void) { return 0; }
#endif

static inline int totk_vk_res_samp_fast_on(void) {
#ifdef TOTK_W49_TEST_HOOKS
    int ov = atomic_load_explicit(&g_totk_vk_res_samp_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
#endif
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("TOTK_VK_RES_SAMP_FAST");
        v = !(e && strcmp(e, "0") == 0);
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
}

typedef struct {
    _Atomic uint32_t active_n;
    _Atomic uint64_t bloom;
} totk_tb_tracker_t;

static inline uint64_t totk_tb_bloom_mask(uint64_t x) {
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
    return (UINT64_C(1) << (x & 63u)) | (UINT64_C(1) << ((x >> 6) & 63u));
}

static inline void totk_tb_note_add(totk_tb_tracker_t *t, uint64_t key) {
    if (!key || totk_w49_fault_mode() == 1) return;
    uint64_t bm = totk_tb_bloom_mask(key);
    atomic_fetch_or_explicit(&t->bloom, bm, memory_order_acq_rel);
    atomic_fetch_add_explicit(&t->active_n, 1u, memory_order_acq_rel);
    atomic_fetch_or_explicit(&t->bloom, bm, memory_order_release);
}

static inline void totk_tb_note_sub(totk_tb_tracker_t *t, uint32_t count) {
    if (!count) return;
    uint32_t cur = atomic_load_explicit(&t->active_n, memory_order_acquire);
    for (unsigned tries = 0u; tries < 64u && cur > 0u; tries++) {
        uint32_t nxt = cur > count ? (cur - count) : 0u;
        if (atomic_compare_exchange_weak_explicit(&t->active_n, &cur, nxt,
                memory_order_acq_rel, memory_order_acquire)) {
            if (nxt == 0u) atomic_store_explicit(&t->bloom, 0u, memory_order_release);
            return;
        }
    }
}

static inline int totk_tb_may_have(const totk_tb_tracker_t *t, uint64_t key) {
    if (!key || atomic_load_explicit(&t->active_n, memory_order_acquire) == 0u) return 0;
    uint64_t bm = totk_tb_bloom_mask(key);
    return (atomic_load_explicit(&t->bloom, memory_order_acquire) & bm) == bm;
}

static inline int totk_samp_par_eq_fast(const void *a, const void *b) {
    const uint8_t *pa = (const uint8_t *)a, *pb = (const uint8_t *)b;
    uint64_t a0 = 0, a1 = 0, a2 = 0, a3 = 0, a4 = 0;
    uint64_t b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0;
    uint32_t a5 = 0, b5 = 0;
    memcpy(&a0, pa, 8);
    memcpy(&b0, pb, 8);
    if (totk_w49_fault_mode() == 2) return a0 == b0;
    memcpy(&a1, pa + 8u, 8);
    memcpy(&b1, pb + 8u, 8);
    memcpy(&a2, pa + 16u, 8);
    memcpy(&b2, pb + 16u, 8);
    memcpy(&a3, pa + 24u, 8);
    memcpy(&b3, pb + 24u, 8);
    memcpy(&a4, pa + 32u, 8);
    memcpy(&b4, pb + 32u, 8);
    memcpy(&a5, pa + 40u, 4);
    memcpy(&b5, pb + 40u, 4);
    return ((a0 ^ b0) | (a1 ^ b1) | (a2 ^ b2) | (a3 ^ b3) | (a4 ^ b4) | (uint64_t)(a5 ^ b5)) == 0u;
}

enum { TOTK_SREG_FAST_MAX = 4096u };

typedef struct {
    _Atomic uint32_t seq[TOTK_SREG_FAST_MAX];
    _Atomic uint64_t words[TOTK_SREG_FAST_MAX][6];
    _Atomic uint8_t has_obj[TOTK_SREG_FAST_MAX];
} totk_sreg_shadow_t;

static inline void totk_sreg_write(totk_sreg_shadow_t *s, int sid, const void *sp, size_t sz, uint8_t obj_ready) {
    if ((unsigned)sid >= TOTK_SREG_FAST_MAX || sz > 48u) return;
    uint64_t w[6] = {0, 0, 0, 0, 0, 0};
    memcpy(w, sp, sz);
    uint32_t cur = atomic_load_explicit(&s->seq[sid], memory_order_relaxed);
    uint32_t odd = (cur + 1u) | 1u;
    uint32_t even = odd + 1u;
    if (!even) even = 2u;
    atomic_store_explicit(&s->seq[sid], odd, memory_order_release);
    atomic_store_explicit(&s->has_obj[sid], obj_ready, memory_order_relaxed);
    for (unsigned i = 0u; i < 6u; i++) atomic_store_explicit(&s->words[sid][i], w[i], memory_order_relaxed);
    atomic_store_explicit(&s->seq[sid], even, memory_order_release);
}

static inline void totk_sreg_mark_obj(totk_sreg_shadow_t *s, int sid, uint8_t obj_ready) {
    if ((unsigned)sid < TOTK_SREG_FAST_MAX) atomic_store_explicit(&s->has_obj[sid], obj_ready, memory_order_release);
}

static inline int totk_sreg_same_with_obj(const totk_sreg_shadow_t *s, int sid, const void *sp) {
    if ((unsigned)sid >= TOTK_SREG_FAST_MAX) return 0;
    uint32_t s0 = atomic_load_explicit(&s->seq[sid], memory_order_acquire);
    if (!s0 || (s0 & 1u)) return 0;
    uint8_t obj = atomic_load_explicit(&s->has_obj[sid], memory_order_relaxed);
    if (!obj) return 0;
    uint64_t w[6];
    for (unsigned i = 0u; i < 6u; i++) w[i] = atomic_load_explicit(&s->words[sid][i], memory_order_relaxed);
    uint32_t s1 = atomic_load_explicit(&s->seq[sid], memory_order_acquire);
    if (s0 != s1) return 0;
    return totk_samp_par_eq_fast(w, sp);
}

static inline uint32_t totk_srgb_twin_fast(uint32_t f) {
    if (totk_w49_fault_mode() == 3 && f == 37u) return 0u;
    static const uint8_t k_tw[24] = { [0] = 43, [6] = 37, [7] = 50, [13] = 44, [14] = 57, [20] = 51 };
    uint32_t idx = f - 37u;
    return idx < 24u ? (uint32_t)k_tw[idx] : 0u;
}

static inline void totk_xlate_hash_bytes_fast(uint64_t h[2], const void *bytes, size_t n) {
    if (totk_w49_fault_mode() == 4) return;
    const uint8_t *p = (const uint8_t *)bytes;
    uint64_t h0 = h[0], h1 = h[1];
    size_t i = 0u;
    for (; i + 8u <= n; i += 8u) {
        uint64_t w = 0u;
        memcpy(&w, p + i, 8);
        h0 = (h0 ^ w) * UINT64_C(11400714819323198485);
        h1 = (h1 ^ (w + UINT64_C(0x9e3779b97f4a7c15))) * UINT64_C(14029467366897019727);
        h1 = (h1 << 29) | (h1 >> 35);
        h0 ^= h1 >> 17;
    }
    for (; i < n; i++) {
        h0 = (h0 ^ p[i]) * UINT64_C(1099511628211);
        h1 = (h1 ^ p[i]) * UINT64_C(0x9e3779b185ebca87);
        h1 = (h1 << 13) | (h1 >> 51);
    }
    h[0] = h0;
    h[1] = h1;
}

static inline void totk_xlate_deferred_bind_fast(uint8_t *row, unsigned cap, int32_t first, int32_t n, int val) {
    if (n <= 0 || !row) return;
    uint8_t bv = val ? 1u : 0u;
    if (first >= 0) {
        if ((unsigned)first >= cap) return;
        unsigned rem = cap - (unsigned)first;
        unsigned cnt = (unsigned)n < rem ? (unsigned)n : rem;
        memset(row + (unsigned)first, bv, cnt);
        return;
    }
    for (int32_t i = 0; i < n; i++) {
        unsigned slot = (unsigned)first + (unsigned)i;
        if (slot < cap) row[slot] = bv;
    }
}

enum {
    TOTK_XL_NONE = 0,
    TOTK_XL_FIXED1 = 1,
    TOTK_XL_FIXED2 = 2,
    TOTK_XL_UPDATE_UB = 3,
    TOTK_XL_SET_RT = 4,
    TOTK_XL_BIND_VBUF = 5,
    TOTK_XL_ARR_CHECK = 6
};

typedef struct { size_t offset, size; } totk_xlate_field_t;

typedef struct {
    uint16_t min_size;
    uint16_t elem_size;
    uint8_t mode;
    uint8_t max_n;
    uint8_t valid;
    uint8_t n_off;
    uint16_t off0;
    uint8_t sz0;
    uint16_t off1;
    uint8_t sz1;
} totk_xlate_op_spec_t;

static inline int totk_xlate_address_fields_fast(const totk_xlate_op_spec_t *specs, unsigned nspecs,
                                                 unsigned op, const uint8_t *a, size_t size,
                                                 totk_xlate_field_t fields[17]) {
    if (totk_w49_fault_mode() == 5 && op == 26u) return -1;
    if (op >= nspecs || !specs[op].valid) return -1;
    const totk_xlate_op_spec_t *sp = &specs[op];
    if (size < (size_t)sp->min_size) return -1;
    if (sp->mode == TOTK_XL_NONE) return 0;
    if (sp->mode == TOTK_XL_FIXED1) { fields[0] = (totk_xlate_field_t){sp->off0, sp->sz0}; return 1; }
    if (sp->mode == TOTK_XL_FIXED2) {
        fields[0] = (totk_xlate_field_t){sp->off0, sp->sz0};
        fields[1] = (totk_xlate_field_t){sp->off1, sp->sz1};
        return 2;
    }
    if (sp->mode == TOTK_XL_UPDATE_UB) {
        uint64_t usz = 0u;
        memcpy(&usz, a + sp->n_off, 8);
        if (usz > size - (size_t)sp->min_size) return -1;
        fields[0] = (totk_xlate_field_t){sp->off0, sp->sz0};
        return 1;
    }
    if (sp->mode == TOTK_XL_SET_RT) {
        for (unsigned i = 0u; i < 8u; i++) fields[i] = (totk_xlate_field_t){(size_t)sp->off0 + i * (size_t)sp->elem_size, sp->sz0};
        fields[8] = (totk_xlate_field_t){sp->off1, sp->sz1};
        return 9;
    }
    int32_t an = 0;
    memcpy(&an, a + sp->n_off, 4);
    if (an < 0 || an > (int32_t)sp->max_n || (size_t)an > (size - (size_t)sp->min_size) / (size_t)sp->elem_size) return -1;
    if (sp->mode == TOTK_XL_BIND_VBUF) {
        for (int32_t i = 0; i < an; i++) fields[i] = (totk_xlate_field_t){(size_t)sp->off1 + (size_t)i * (size_t)sp->elem_size, sp->sz1};
        return (int)an;
    }
    if (sp->sz0) { fields[0] = (totk_xlate_field_t){sp->off0, sp->sz0}; return 1; }
    return 0;
}
#endif
