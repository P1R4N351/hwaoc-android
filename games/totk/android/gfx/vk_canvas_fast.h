/* Fast paths for canvas UI/transfer shader classification, identity slots, and atomic format feature cache (Opt 49). */
#ifndef VK_CANVAS_FAST_H
#define VK_CANVAS_FAST_H
#include "vk_canvas.h"
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TOTK_W47_TEST_HOOKS
extern _Atomic int g_totk_vk_canvas_fast_override;
extern _Atomic int g_totk_w47_fault;
static inline void totk_vk_canvas_fast_set_for_test(int v) {
    atomic_store_explicit(&g_totk_vk_canvas_fast_override, v, memory_order_release);
}
static inline void totk_w47_set_fault(int f) {
    atomic_store_explicit(&g_totk_w47_fault, f, memory_order_release);
}
static inline int totk_w47_fault_mode(void) {
    int f = atomic_load_explicit(&g_totk_w47_fault, memory_order_relaxed);
    if (f >= 0) return f;
    const char *e = getenv("TOTK_W47_FAULT");
    int v = (e && *e) ? atoi(e) : 0;
    atomic_store_explicit(&g_totk_w47_fault, v, memory_order_relaxed);
    return v;
}
#else
static inline int totk_w47_fault_mode(void) { return 0; }
#endif

static inline int totk_vk_canvas_fast_on(void) {
#ifdef TOTK_W47_TEST_HOOKS
    int ov = atomic_load_explicit(&g_totk_vk_canvas_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
#endif
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("TOTK_VK_CANVAS_FAST");
        v = !(e && strcmp(e, "0") == 0);
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
}

typedef struct {
    vk_canvas_image canvas, window;
    uint64_t seen, ui_frame;
} totk_canvas_known_t;

static inline int totk_canvas_same_fast(const vk_canvas_image *a, const vk_canvas_image *b) {
    int match = (a->image != 0u) & (a->image == b->image) & (a->incarnation == b->incarnation) &
                (a->size == b->size) & (a->width == b->width) & (a->height == b->height) &
                (a->format == b->format) & (a->level == b->level) & (a->layer == b->layer);
    if (totk_w47_fault_mode() == 3) return match;
    return match & (a->storage == b->storage);
}

static inline unsigned totk_canvas_slot_fast(
    const vk_canvas_image *image, const totk_canvas_known_t *known, uint32_t *hint) {
    uint32_t h = *hint;
    if (h < 16u && totk_canvas_same_fast(image, &known[h].canvas)) return h;
    unsigned oldest = 0u;
    for (unsigned i = 0u; i < 16u; i++) {
        if (totk_canvas_same_fast(image, &known[i].canvas)) {
            *hint = i;
            return i;
        }
        if (known[i].seen < known[oldest].seen) oldest = i;
    }
    *hint = oldest;
    return oldest;
}

static inline int totk_canvas_ui_shader_fast(const char *key, int after) {
    if (!key) return 0;
    for (int i = 0; i < 8; i++) {
        if (!key[i]) return 0;
    }
    typedef struct { char tag[9]; uint8_t rank; } ui_slot_t;
    static const ui_slot_t k_tbl[32] = {
        [0]  = {"1a40e5f5", 10}, [1]  = {"16684efc",  4}, [2]  = {"e9fd7b43",  2}, [4]  = {"3e11cee5",  5},
        [6]  = {"9f47ed90",  9}, [7]  = {"2117f58b", 11}, [9]  = {"a662e313",  3}, [10] = {"48c18ae6",  6},
        [12] = {"8d4e36d0", 12}, [18] = {"1469983f", 13}, [21] = {"4872bec9",  8}, [24] = {"7f2113e2",  1},
        [27] = {"35cbc686",  7}
    };
    uint32_t slot = ((uint32_t)(uint8_t)key[1] ^ ((uint32_t)(uint8_t)key[5] << 2) ^ (uint32_t)(uint8_t)key[7]) & 31u;
    const ui_slot_t *e = &k_tbl[slot];
    if (!e->rank) return 0;
    if (totk_w47_fault_mode() == 1 && e->rank == 13u) return 0;
    if (!after && e->rank > 9u) return 0;
    uint64_t kw = 0u, tw = 0u;
    memcpy(&kw, key, 8);
    memcpy(&tw, e->tag, 8);
    return kw == tw;
}

static inline int totk_canvas_transfer_shader_fast(const char *key) {
    if (!key) return 0;
    for (int i = 0; i < 8; i++) {
        if (!key[i]) return 0;
    }
    static const char k_ref[65] = "dd3d1e0ebf73242845970b5655fac199a30f5acf747975c5e771caa49af8b12b";
    uint64_t kw = 0u, rw = 0u;
    memcpy(&kw, key, 8);
    memcpy(&rw, k_ref, 8);
    if (kw != rw) return 0;
    if (totk_w47_fault_mode() == 2) return 1;
    for (size_t i = 8u; i < 64u; i++) {
        if (key[i] != k_ref[i]) return 0;
    }
    return 1;
}

typedef uint32_t (*totk_fmt_feats_probe_fn)(uint32_t fmt, void *ctx);

typedef struct {
    _Atomic uint64_t core[256];
    _Atomic uint32_t ext_state[64];
    _Atomic uint32_t ext_fmt[64];
    _Atomic uint32_t ext_val[64];
} totk_fmt_feats_cache_t;

static inline uint32_t totk_fmt_feats_ext_get(
    totk_fmt_feats_cache_t *c, uint32_t fmt, totk_fmt_feats_probe_fn probe, void *ctx) {
    uint32_t idx = (fmt * UINT32_C(2654435761)) & 63u;
    for (uint32_t step = 0u; step < 8u; step++) {
        uint32_t s = (idx + step) & 63u;
        uint32_t st = atomic_load_explicit(&c->ext_state[s], memory_order_acquire);
        if (st == 2u && atomic_load_explicit(&c->ext_fmt[s], memory_order_relaxed) == fmt) {
            return atomic_load_explicit(&c->ext_val[s], memory_order_relaxed);
        }
        if (st == 0u) {
            uint32_t expected = 0u;
            if (atomic_compare_exchange_strong_explicit(
                    &c->ext_state[s], &expected, 1u, memory_order_acq_rel, memory_order_relaxed)) {
                uint32_t ff = probe(fmt, ctx);
                atomic_store_explicit(&c->ext_fmt[s], fmt, memory_order_relaxed);
                atomic_store_explicit(&c->ext_val[s], ff, memory_order_relaxed);
                atomic_store_explicit(&c->ext_state[s], 2u, memory_order_release);
                return ff;
            }
        }
    }
    return probe(fmt, ctx);
}

static inline uint32_t totk_fmt_feats_fast_get(
    totk_fmt_feats_cache_t *c, uint32_t fmt, totk_fmt_feats_probe_fn probe, void *ctx) {
    if (fmt < 256u) {
        uint64_t cached = atomic_load_explicit(&c->core[fmt], memory_order_acquire);
        if ((cached & (UINT64_C(1) << 63)) != 0u) return (uint32_t)cached;
        uint32_t ff = probe(fmt, ctx);
        uint32_t stored = totk_w47_fault_mode() == 4 ? (ff ^ 1u) : ff;
        atomic_store_explicit(&c->core[fmt], (UINT64_C(1) << 63) | (uint64_t)stored, memory_order_release);
        return ff;
    }
    return totk_fmt_feats_ext_get(c, fmt, probe, ctx);
}

#endif
