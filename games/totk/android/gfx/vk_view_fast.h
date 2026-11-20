#ifndef TOTK_VK_VIEW_FAST_H
#define TOTK_VK_VIEW_FAST_H
/* Wave 18 (Opt 20): Lock-free TLS VkImageView, identity & feedback-cut front-memos (TOTK_VK_VIEW_FAST=1). */
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TOTK_KTGL_OPT_TEST
extern int g_fault_mode;
#define TOTK_VIEW_FAULT(n) (g_fault_mode == (n))
#else
#define TOTK_VIEW_FAULT(n) 0
#endif

static inline int totk_view_fast_on(void) {
    static _Atomic int cached = -1;
    int v = atomic_load_explicit(&cached, memory_order_relaxed);
    if (v < 0) {
        const char *e = getenv("TOTK_VK_VIEW_FAST");
        v = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&cached, v, memory_order_relaxed);
    }
    return v;
}

enum {
    TOTK_VIEW_M_SETS = 64u,
    TOTK_VIEW_ID_SETS = 16u,
    TOTK_VIEW_CUT_SETS = 16u,
    TOTK_VIEW_WAYS = 4u
};

static inline uint32_t totk_view_mix(uint64_t a, uint64_t b, uint64_t c, uint32_t mask) {
    uint64_t x = a ^ (b * UINT64_C(0x9e3779b97f4a7c15)) ^ (c * UINT64_C(0xbf58476d1ce4e5b9));
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    return (uint32_t)(x ^ (x >> 27)) & mask;
}

static inline void totk_view_pack_fields(
    int set_levels, int set_layers, int set_format, int set_target,
    int set_swizzle, int set_dsmode, int dsmode,
    uint32_t base_level, uint32_t nlevels, uint32_t target,
    uint32_t min_layer, uint32_t nlayers, uint32_t format,
    const uint8_t swz[4], uint64_t *v0_out, uint64_t *v1_out) {
    assert(v0_out != NULL && v1_out != NULL);
    uint32_t fl = (set_levels ? 1u : 0u) | (set_layers ? 2u : 0u) | (set_format ? 4u : 0u) |
                  (set_target ? 8u : 0u) | (set_swizzle ? 16u : 0u) |
                  (set_dsmode ? 32u : 0u) | ((set_dsmode && dsmode) ? 64u : 0u);
    uint32_t l0 = set_levels ? (base_level & 0xffu) : 0u;
    uint32_t nl = set_levels ? (nlevels & 0xffu) : 0u;
    uint32_t tg = set_target ? (target & 0xffu) : 0u;
    uint32_t fm = (set_format && !TOTK_VIEW_FAULT(50)) ? (format & 0xffffu) : 0u;
    uint32_t s0 = set_layers ? (min_layer & 0xffffu) : 0u;
    uint32_t ns = set_layers ? (nlayers & 0xffffu) : 0u;
    uint32_t sw0 = (set_swizzle && swz) ? (swz[0] & 0xffu) : 0u;
    uint32_t sw1 = (set_swizzle && swz) ? (swz[1] & 0xffu) : 0u;
    uint32_t sw2 = (set_swizzle && swz) ? (swz[2] & 0xffu) : 0u;
    uint32_t sw3 = (set_swizzle && swz) ? (swz[3] & 0xffu) : 0u;
    *v0_out = (uint64_t)fl | ((uint64_t)l0 << 8) | ((uint64_t)nl << 16) |
              ((uint64_t)tg << 24) | ((uint64_t)fm << 32);
    *v1_out = (uint64_t)s0 | ((uint64_t)ns << 16) | ((uint64_t)sw0 << 32) |
              ((uint64_t)sw1 << 40) | ((uint64_t)sw2 << 48) | ((uint64_t)sw3 << 56);
}

typedef struct {
    uint64_t im_ptr, incarn, ident_rev, t_meta, v0, v1, view_u;
    uint32_t valid, pad_;
} totk_view_ent;

typedef struct {
    totk_view_ent w[TOTK_VIEW_WAYS];
    uint8_t hand, pad[7];
} totk_view_set;

typedef struct {
    totk_view_set s[TOTK_VIEW_M_SETS];
} totk_view_memo4;

static inline int totk_view_memo4_get(const totk_view_memo4 *m, uint64_t im_ptr,
                                      uint64_t incarn, uint64_t ident_rev,
                                      uint64_t t_meta, uint64_t v0, uint64_t v1,
                                      uint64_t *view_out) {
    assert(m != NULL && view_out != NULL);
    const totk_view_set *set = &m->s[totk_view_mix(im_ptr, t_meta ^ v0, v1, TOTK_VIEW_M_SETS - 1u)];
    for (uint32_t i = 0; i < TOTK_VIEW_WAYS; i++) {
        const totk_view_ent *e = &set->w[i];
        if (e->valid && e->ident_rev == ident_rev && e->im_ptr == im_ptr &&
            e->incarn == incarn && e->t_meta == t_meta && e->v0 == v0 && e->v1 == v1) {
            *view_out = e->view_u;
            return 1;
        }
    }
    return 0;
}

static inline void totk_view_memo4_put(totk_view_memo4 *m, uint64_t im_ptr,
                                       uint64_t incarn, uint64_t ident_rev,
                                       uint64_t t_meta, uint64_t v0, uint64_t v1,
                                       uint64_t view_u) {
    assert(m != NULL && view_u != 0u);
    totk_view_set *set = &m->s[totk_view_mix(im_ptr, t_meta ^ v0, v1, TOTK_VIEW_M_SETS - 1u)];
    totk_view_ent ent = {im_ptr, incarn, ident_rev, t_meta, v0, v1, view_u, 1u, 0u};
    for (uint32_t i = 0; i < TOTK_VIEW_WAYS; i++) {
        totk_view_ent *e = &set->w[i];
        if (!e->valid || e->ident_rev != ident_rev ||
            (e->im_ptr == im_ptr && e->incarn == incarn && e->t_meta == t_meta && e->v0 == v0 && e->v1 == v1)) {
            *e = ent;
            return;
        }
    }
    uint32_t v = set->hand & (TOTK_VIEW_WAYS - 1u);
    set->hand = (uint8_t)((set->hand + 1u) & (TOTK_VIEW_WAYS - 1u));
    set->w[v] = ent;
}

typedef struct {
    uint64_t x_ptr, view_u, ident_rev, img_u, incarn, storage, size, serial;
    uint32_t width, height, fmt_u, aspect, base_mip, level_cnt, base_layer, layer_cnt;
    int32_t view_type;
    uint32_t valid;
} totk_view_id_ent;

typedef struct {
    uint64_t x_ptr[TOTK_VIEW_WAYS];
    uint64_t view_u[TOTK_VIEW_WAYS];
    uint64_t ident_rev[TOTK_VIEW_WAYS];
    uint8_t valid[TOTK_VIEW_WAYS];
    uint8_t hand, pad[3];
    totk_view_id_ent w[TOTK_VIEW_WAYS];
} totk_view_id_set;

typedef struct {
    totk_view_id_set s[TOTK_VIEW_ID_SETS];
} totk_view_id_memo4;

static inline const totk_view_id_ent *totk_view_id_get(const totk_view_id_memo4 *m,
                                                       uint64_t x_ptr, uint64_t view_u,
                                                       uint64_t ident_rev) {
    assert(m != NULL);
    const totk_view_id_set *set = &m->s[totk_view_mix(x_ptr, view_u, 0u, TOTK_VIEW_ID_SETS - 1u)];
    for (uint32_t i = 0; i < TOTK_VIEW_WAYS; i++) {
        if (!set->valid[i] || set->x_ptr[i] != x_ptr || set->view_u[i] != view_u) continue;
        if (!TOTK_VIEW_FAULT(51) && set->ident_rev[i] != ident_rev) continue;
        return &set->w[i];
    }
    return NULL;
}

static inline void totk_view_id_put(totk_view_id_memo4 *m, const totk_view_id_ent *in) {
    assert(m != NULL && in != NULL && in->valid);
    totk_view_id_set *set = &m->s[totk_view_mix(in->x_ptr, in->view_u, 0u, TOTK_VIEW_ID_SETS - 1u)];
    uint32_t slot = TOTK_VIEW_WAYS;
    for (uint32_t i = 0; i < TOTK_VIEW_WAYS; i++) {
        if (!set->valid[i] || set->ident_rev[i] != in->ident_rev ||
            (set->x_ptr[i] == in->x_ptr && set->view_u[i] == in->view_u)) {
            slot = i;
            break;
        }
    }
    if (slot == TOTK_VIEW_WAYS) {
        slot = set->hand & (TOTK_VIEW_WAYS - 1u);
        set->hand = (uint8_t)((set->hand + 1u) & (TOTK_VIEW_WAYS - 1u));
    }
    set->x_ptr[slot] = in->x_ptr;
    set->view_u[slot] = in->view_u;
    set->ident_rev[slot] = in->ident_rev;
    set->valid[slot] = 1u;
    set->w[slot] = *in;
}

typedef struct {
    uint64_t im_ptr, incarn, ident_rev, view_u, cut_view_u;
    uint32_t levels, valid;
} totk_view_cut_ent;

typedef struct {
    totk_view_cut_ent w[TOTK_VIEW_WAYS];
    uint8_t hand, pad[7];
} totk_view_cut_set;

typedef struct {
    totk_view_cut_set s[TOTK_VIEW_CUT_SETS];
} totk_view_cut_memo4;

static inline int totk_view_cut_get(const totk_view_cut_memo4 *m, uint64_t im_ptr,
                                    uint64_t incarn, uint64_t ident_rev,
                                    uint64_t view_u, uint32_t levels,
                                    uint64_t *cut_out) {
    assert(m != NULL && cut_out != NULL);
    const totk_view_cut_set *set = &m->s[totk_view_mix(im_ptr, view_u, 0u, TOTK_VIEW_CUT_SETS - 1u)];
    for (uint32_t i = 0; i < TOTK_VIEW_WAYS; i++) {
        const totk_view_cut_ent *e = &set->w[i];
        if (!e->valid || e->ident_rev != ident_rev || e->im_ptr != im_ptr ||
            e->incarn != incarn || e->view_u != view_u) continue;
        if (!TOTK_VIEW_FAULT(52) && e->levels != levels) continue;
        *cut_out = e->cut_view_u;
        return 1;
    }
    return 0;
}

static inline void totk_view_cut_put(totk_view_cut_memo4 *m, uint64_t im_ptr,
                                     uint64_t incarn, uint64_t ident_rev,
                                     uint64_t view_u, uint32_t levels,
                                     uint64_t cut_view_u) {
    assert(m != NULL && cut_view_u != 0u);
    totk_view_cut_set *set = &m->s[totk_view_mix(im_ptr, view_u, 0u, TOTK_VIEW_CUT_SETS - 1u)];
    totk_view_cut_ent ent = {im_ptr, incarn, ident_rev, view_u, cut_view_u, levels, 1u};
    for (uint32_t i = 0; i < TOTK_VIEW_WAYS; i++) {
        totk_view_cut_ent *e = &set->w[i];
        if (!e->valid || e->ident_rev != ident_rev ||
            (e->im_ptr == im_ptr && e->incarn == incarn && e->view_u == view_u && e->levels == levels)) {
            *e = ent;
            return;
        }
    }
    uint32_t v = set->hand & (TOTK_VIEW_WAYS - 1u);
    set->hand = (uint8_t)((set->hand + 1u) & (TOTK_VIEW_WAYS - 1u));
    set->w[v] = ent;
}

#endif
