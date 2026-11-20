#ifndef TOTK_VK_PROG_FAST_H
#define TOTK_VK_PROG_FAST_H
/* Wave 13 (Opt 15): Program-prepare gate/64x4 memo & separable KTGL (VS,FS) 128x4 pair front-memo (TOTK_VK_PROG_FAST=1). */
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TOTK_KTGL_OPT_TEST
extern int g_fault_mode;
#define TOTK_PROG_FAST_FAULT(n) (g_fault_mode == (n))
#else
#define TOTK_PROG_FAST_FAULT(n) 0
#endif

static inline int totk_vk_prog_fast_on(void) {
    static _Atomic int cached = -1;
    int v = atomic_load_explicit(&cached, memory_order_relaxed);
    if (v < 0) {
        const char *e = getenv("TOTK_VK_PROG_FAST");
        v = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&cached, v, memory_order_relaxed);
    }
    return v;
}

enum { TOTK_PROG_PREP_SETS = 64u, TOTK_PROG_PREP_WAYS = 4u };

typedef struct {
    uint64_t prog_u, serial, epoch;
    uint32_t valid, pad;
} totk_prog_prep_ent;

typedef struct {
    totk_prog_prep_ent ways[TOTK_PROG_PREP_WAYS];
    uint32_t next_way, pad;
} totk_prog_prep_set;

typedef struct {
    totk_prog_prep_set sets[TOTK_PROG_PREP_SETS];
} totk_prog_prep_memo4;

static inline uint32_t totk_prog_prep_idx(uint64_t prog_u) {
    return (uint32_t)(((prog_u >> 5) ^ (prog_u >> 11) ^ (prog_u >> 17)) & (TOTK_PROG_PREP_SETS - 1u));
}

static inline int totk_prog_prep_memo4_has(const totk_prog_prep_memo4 *m, uint64_t prog_u,
                                           uint64_t serial, uint64_t epoch) {
    assert(m != NULL);
    const totk_prog_prep_set *s = &m->sets[totk_prog_prep_idx(prog_u)];
    for (uint32_t i = 0; i < TOTK_PROG_PREP_WAYS; i++) {
        const totk_prog_prep_ent *w = &s->ways[i];
        if (!w->valid || w->prog_u != prog_u) continue;
        if (!TOTK_PROG_FAST_FAULT(35) && w->serial != serial) continue;
        if (w->epoch != epoch) continue;
        return 1;
    }
    return 0;
}

static inline void totk_prog_prep_memo4_put(totk_prog_prep_memo4 *m, uint64_t prog_u,
                                            uint64_t serial, uint64_t epoch) {
    assert(m != NULL);
    totk_prog_prep_set *s = &m->sets[totk_prog_prep_idx(prog_u)];
    for (uint32_t i = 0; i < TOTK_PROG_PREP_WAYS; i++) {
        totk_prog_prep_ent *w = &s->ways[i];
        if (!w->valid || w->prog_u == prog_u) {
            w->prog_u = prog_u; w->serial = serial; w->epoch = epoch; w->valid = 1u; w->pad = 0u;
            return;
        }
    }
    totk_prog_prep_ent *vict = &s->ways[s->next_way & (TOTK_PROG_PREP_WAYS - 1u)];
    s->next_way = (s->next_way + 1u) & (TOTK_PROG_PREP_WAYS - 1u);
    vict->prog_u = prog_u; vict->serial = serial; vict->epoch = epoch; vict->valid = 1u; vict->pad = 0u;
}

enum { TOTK_GFX_PAIR_SETS = 128u, TOTK_GFX_PAIR_WAYS = 4u };

typedef struct {
    uint64_t vs_u, vs_ser, fs_u, fs_ser;
    uint64_t epoch, sig0, sig1;
    uint64_t sh0_u, sh1_u, reg;
    uint32_t valid, pad;
} totk_gfx_pair_ent;

typedef struct {
    totk_gfx_pair_ent ways[TOTK_GFX_PAIR_WAYS];
    uint32_t next_way, pad;
} totk_gfx_pair_set;

typedef struct {
    totk_gfx_pair_set sets[TOTK_GFX_PAIR_SETS];
} totk_gfx_pair_memo4;

static inline uint32_t totk_gfx_pair_idx(uint64_t vs_u, uint64_t fs_u) {
    uint64_t x = ((vs_u >> 4) * 0x9e3779b97f4a7c15ull) ^ ((fs_u >> 4) * 0xc2b2ae3d27d4eb4full);
    return (uint32_t)(x >> 57) & (TOTK_GFX_PAIR_SETS - 1u);
}

static inline int totk_gfx_pair_memo4_find(totk_gfx_pair_memo4 *m, uint64_t vs_u, uint64_t vs_ser,
                                           uint64_t fs_u, uint64_t fs_ser, uint64_t epoch,
                                           uint64_t sig0, uint64_t sig1, uint64_t *sh0_out,
                                           uint64_t *sh1_out, uint64_t *reg_inout) {
    assert(m != NULL && sh0_out != NULL && sh1_out != NULL);
    totk_gfx_pair_set *s = &m->sets[totk_gfx_pair_idx(vs_u, fs_u)];
    for (uint32_t i = 0; i < TOTK_GFX_PAIR_WAYS; i++) {
        totk_gfx_pair_ent *w = &s->ways[i];
        if (!w->valid) continue;
        if (w->vs_u != vs_u || w->vs_ser != vs_ser || w->fs_u != fs_u) continue;
        if (!TOTK_PROG_FAST_FAULT(36) && w->fs_ser != fs_ser) continue;
        if (w->epoch != epoch || w->sig0 != sig0) continue;
        if (!TOTK_PROG_FAST_FAULT(37) && w->sig1 != sig1) continue;
        *sh0_out = w->sh0_u;
        *sh1_out = w->sh1_u;
        if (reg_inout) {
            uint64_t prev = w->reg;
            w->reg = *reg_inout;
            *reg_inout = prev;
        }
        return 1;
    }
    return 0;
}

static inline void totk_gfx_pair_memo4_put(totk_gfx_pair_memo4 *m, uint64_t vs_u, uint64_t vs_ser,
                                           uint64_t fs_u, uint64_t fs_ser, uint64_t epoch,
                                           uint64_t sig0, uint64_t sig1, uint64_t sh0_u,
                                           uint64_t sh1_u, uint64_t reg) {
    assert(m != NULL);
    totk_gfx_pair_set *s = &m->sets[totk_gfx_pair_idx(vs_u, fs_u)];
    for (uint32_t i = 0; i < TOTK_GFX_PAIR_WAYS; i++) {
        totk_gfx_pair_ent *w = &s->ways[i];
        int same_pair = w->vs_u == vs_u && w->fs_u == fs_u;
        int dead_ser = same_pair && (w->vs_ser != vs_ser || w->fs_ser != fs_ser);
        int same_full = same_pair && w->vs_ser == vs_ser && w->fs_ser == fs_ser &&
                        w->epoch == epoch && w->sig0 == sig0 && w->sig1 == sig1;
        if (!w->valid || dead_ser || same_full) {
            w->vs_u = vs_u; w->vs_ser = vs_ser; w->fs_u = fs_u; w->fs_ser = fs_ser;
            w->epoch = epoch; w->sig0 = sig0; w->sig1 = sig1;
            w->sh0_u = sh0_u; w->sh1_u = sh1_u; w->reg = reg; w->valid = 1u;
            return;
        }
    }
    totk_gfx_pair_ent *vict = &s->ways[s->next_way & (TOTK_GFX_PAIR_WAYS - 1u)];
    s->next_way = (s->next_way + 1u) & (TOTK_GFX_PAIR_WAYS - 1u);
    vict->vs_u = vs_u; vict->vs_ser = vs_ser; vict->fs_u = fs_u; vict->fs_ser = fs_ser;
    vict->epoch = epoch; vict->sig0 = sig0; vict->sig1 = sig1;
    vict->sh0_u = sh0_u; vict->sh1_u = sh1_u; vict->reg = reg; vict->valid = 1u;
}

#endif
