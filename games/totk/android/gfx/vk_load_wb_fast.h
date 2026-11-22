/* Pure C11 helpers for 160-slot lookahead masks, camera hash, wb image tags,
   UB/GW max-fence short-circuits & inline range sort/coalesce (TOTK_VK_LOAD_WB_FAST=1). */
#pragma once
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TOTK_W56_TEST_HOOKS
extern _Atomic int g_totk_vk_load_wb_fast_override;
extern _Atomic int g_totk_w56_fault;
static inline void totk_vk_load_wb_fast_set_for_test(int v) {
    atomic_store_explicit(&g_totk_vk_load_wb_fast_override, v, memory_order_release);
}
static inline void totk_w56_set_fault(int f) {
    atomic_store_explicit(&g_totk_w56_fault, f, memory_order_release);
}
static inline int totk_w56_parse_fault(void) {
    const char *e = getenv("TOTK_W56_FAULT");
    if (e && *e) return atoi(e);
    const char *k = getenv("TOTK_KTGL_OPT_FAULT");
    if (k && *k) {
        int kv = atoi(k);
        if (kv >= 226 && kv <= 230) return kv - 225;
    }
    return 0;
}
static inline int totk_w56_fault_mode(void) {
    int f = atomic_load_explicit(&g_totk_w56_fault, memory_order_relaxed);
    if (f >= 0) return f;
    int v = totk_w56_parse_fault();
    atomic_store_explicit(&g_totk_w56_fault, v, memory_order_relaxed);
    return v;
}
#else
static inline int totk_w56_fault_mode(void) { return 0; }
#endif

static inline int totk_vk_load_wb_fast_on(void) {
#ifdef TOTK_W56_TEST_HOOKS
    int ov = atomic_load_explicit(&g_totk_vk_load_wb_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
#endif
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("TOTK_VK_LOAD_WB_FAST");
        v = (e && *e && atoi(e) == 0) ? 0 : 1;
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
}

/* 1. 32-slot image non-zero mask & 160-slot lookahead mask (0..127 tex, 128..159 img). */
typedef struct {
    uint64_t w[3];
} totk_slot160_nz_t;

static inline void totk_img_nz_update(uint32_t *nz, uint8_t *ready, int first,
                                      const uint64_t *h, int n) {
    if (!*ready || first < 0 || first + n > 32) { *ready = 0u; return; }
    uint32_t clr = 0u, set = 0u;
    for (int i = 0; i < n; i++) {
        uint32_t bit = UINT32_C(1) << (unsigned)(first + i);
        clr |= bit;
        if (h[i]) set |= bit;
    }
    *nz = (*nz & ~clr) | set;
}

static inline void totk_slot160_from_state(totk_slot160_nz_t *m,
                                           const uint64_t tex_nz[2], uint32_t img_nz) {
    m->w[0] = (totk_w56_fault_mode() == 1) ? UINT64_C(0) : tex_nz[0];
    m->w[1] = tex_nz[1];
    m->w[2] = (uint64_t)img_nz;
}

static inline void totk_slot160_update_range(totk_slot160_nz_t *m, int off,
                                             const uint64_t *h, int n) {
    for (int i = 0; i < n; i++) {
        int s = off + i;
        uint32_t wi = (uint32_t)s >> 6;
        uint64_t bit = UINT64_C(1) << ((unsigned)s & 63u);
        if (h[i]) m->w[wi] |= bit;
        else m->w[wi] &= ~bit;
    }
}

/* 2. Branchless FNV-1a camera[84] hash for fg_extract_camera_hash. */
static inline uint64_t totk_fg_fnv_f32(uint64_t h, float f) {
    uint32_t bits = 0u;
    memcpy(&bits, &f, sizeof bits);
    return (h ^ bits) * UINT64_C(1099511628211);
}

static inline uint64_t totk_fg_camera_hash84(const float camera[84]) {
    uint64_t h = UINT64_C(14695981039346656037);
    for (unsigned i = 0u; i < 58u; i++) h = totk_fg_fnv_f32(h, camera[i]);
    h = totk_fg_fnv_f32(h, camera[62]);
    h = totk_fg_fnv_f32(h, camera[70]);
    h = totk_fg_fnv_f32(h, camera[72]);
    if (totk_w56_fault_mode() != 2) h = totk_fg_fnv_f32(h, camera[73]);
    return h;
}

/* 3. Write-back 256-slot image-tag filter + descending candidate index extractor. */
enum { TOTK_WB_TAG_CAP = 256 };

typedef struct {
    uint16_t tag[TOTK_WB_TAG_CAP];
} totk_wb_im_tags_t;

static inline uint16_t totk_wb_im_hash(uintptr_t im) {
    uint64_t x = (uint64_t)im * UINT64_C(0x9e3779b97f4a7c15);
    return (uint16_t)((x >> 48) | UINT64_C(1));
}

static inline void totk_wb_im_set(totk_wb_im_tags_t *t, uint32_t idx, uintptr_t im) {
    if (idx < TOTK_WB_TAG_CAP) t->tag[idx] = im ? totk_wb_im_hash(im) : 0u;
}

static inline void totk_wb_im_remove_swap(totk_wb_im_tags_t *t, uint32_t idx, uint32_t new_n) {
    if (idx < TOTK_WB_TAG_CAP) t->tag[idx] = (new_n < TOTK_WB_TAG_CAP) ? t->tag[new_n] : 0u;
}

static inline void totk_wb_im_or_mask(const totk_wb_im_tags_t *t, uintptr_t im,
                                      int nwb, uint64_t cmask[4]) {
    if (!im || nwb <= 0) return;
    int lim = nwb > (int)TOTK_WB_TAG_CAP ? (int)TOTK_WB_TAG_CAP : nwb;
    uint16_t want = totk_wb_im_hash(im);
    for (int i = 0; i < lim; i++) {
        if (t->tag[i] == want) cmask[(unsigned)i >> 6] |= UINT64_C(1) << ((unsigned)i & 63u);
    }
    if (totk_w56_fault_mode() == 3) cmask[0] = 0u;
}

static inline int totk_wb_mask_to_indices(const uint64_t cmask[4], int nwb, int out_idx[256]) {
    int nc = 0;
    for (int w = 0; w < 4; w++) {
        uint64_t m = cmask[w];
        while (m) {
            int idx = (w << 6) + __builtin_ctzll(m);
            m &= m - 1u;
            if (idx < nwb) out_idx[nc++] = idx;
        }
    }
    return nc;
}

/* 4. O(1) UB/GW max-fence check & inline Ciura shell-sort + interval coalesce. */
static inline int totk_ub_has_after(uint32_t unsealed_cnt, uint64_t max_fence, uint64_t done) {
    if (totk_w56_fault_mode() == 4) return 0;
    return unsealed_cnt > 0u || max_fence > done;
}

static inline void totk_rng_sort_pairs(uint64_t *pairs, size_t n) {
    static const size_t gaps[6] = {132u, 57u, 23u, 10u, 4u, 1u};
    for (int g = 0; g < 6; g++) {
        size_t h = gaps[g];
        for (size_t i = h; i < n; i++) {
            uint64_t vlo = pairs[i * 2u], vhi = pairs[i * 2u + 1u];
            size_t j = i;
            while (j >= h && pairs[(j - h) * 2u] > vlo) {
                pairs[j * 2u] = pairs[(j - h) * 2u];
                pairs[j * 2u + 1u] = pairs[(j - h) * 2u + 1u];
                j -= h;
            }
            pairs[j * 2u] = vlo;
            pairs[j * 2u + 1u] = vhi;
        }
    }
}

static inline size_t totk_rng_sort_coalesce(uint64_t *pairs, size_t n) {
    if (n == 0u) return 0u;
    if (n > 1u) totk_rng_sort_pairs(pairs, n);
    size_t count = 1u;
    for (size_t i = 1u; i < n; i++) {
        uint64_t rlo = pairs[i * 2u], rhi = pairs[i * 2u + 1u];
        if (rlo <= pairs[(count - 1u) * 2u + 1u]) {
            if (rhi > pairs[(count - 1u) * 2u + 1u]) pairs[(count - 1u) * 2u + 1u] = rhi;
        } else {
            pairs[count * 2u] = rlo;
            pairs[count * 2u + 1u] = rhi;
            count++;
        }
    }
    if (totk_w56_fault_mode() == 5 && count > 1u) return 1u;
    return count;
}
