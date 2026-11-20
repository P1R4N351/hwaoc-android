/* Victim and direct-mapped front-caches for AoC texture/sampler lookups (TOTK_VK_RES_FAST=1).
 * Preserves legacy 4096-slot spatial bijection and read-clean hits (0 writes on hit) while eliminating
 * conflict misses via compact 256x4 victim caches and 512x4 per-thread sampler sets. 0 GAME_* branches. */
#ifndef VK_RES_FAST_H
#define VK_RES_FAST_H

#include <assert.h>
#include <stdint.h>
#include <string.h>

#ifdef W7_TEST_HOOKS
extern int g_w7_aoc_fault;
#define TOTK_RES_FAST_FAULT(n) (g_w7_aoc_fault == (n))
#else
#define TOTK_RES_FAST_FAULT(n) 0
#endif

enum {
    TOTK_FM_SLOTS = 4096,
    TOTK_FM_VSETS = 256,
    TOTK_FM_WAYS = 4,
    TOTK_TVIC_SETS = 256,
    TOTK_TVIC_WAYS = 4,
    TOTK_SC4_SETS = 512,
    TOTK_SC4_WAYS = 4
};

typedef struct {
    uint32_t gen, key;
    uint64_t sub;
    uint64_t v;
    void *x;
} totk_fm_ent;

typedef struct {
    totk_fm_ent e[TOTK_FM_WAYS];
    uint32_t rr, pad;
} totk_fm_vset;

typedef struct {
    uint64_t registry, identity;
    uint32_t gen, pad;
    uint64_t counts[3], published[3];
    totk_fm_ent p[TOTK_FM_SLOTS];
    totk_fm_vset v[TOTK_FM_VSETS];
} totk_fmemo;

typedef struct {
    int tid, want, in_pool;
    uint32_t gen;
    uint64_t sub, epoch, view_u;
    void *x, *tex_cell;
    uint64_t tex_val;
    void *img_cell;
    uint64_t img_val;
} totk_tvic_ent;

typedef struct {
    totk_tvic_ent e[TOTK_TVIC_WAYS];
    uint32_t rr, pad;
} totk_tvic_set;

typedef struct {
    totk_tvic_set s[TOTK_TVIC_SETS];
} totk_tidc_vic;

typedef struct {
    int sid;
    uint32_t ep;
    uint64_t samp_u;
} totk_sc4_ent;

typedef struct {
    totk_sc4_ent e[TOTK_SC4_WAYS];
    uint32_t rr, pad[3];
} totk_sc4_set;

typedef struct {
    totk_sc4_set s[TOTK_SC4_SETS];
} totk_scache4;

static inline unsigned totk_fm_idx(uint32_t key) {
    return (key ^ (key >> 12) ^ (key >> 20)) & (TOTK_FM_SLOTS - 1u);
}

static inline const totk_fm_ent *totk_fmemo_get(const totk_fmemo *m, uint32_t key) {
    assert(m != NULL);
    unsigned idx = totk_fm_idx(key);
    const totk_fm_ent *p = &m->p[idx];
    if (p->gen == m->gen && p->key == key) return p;
    if (p->gen != m->gen) return NULL;
    const totk_fm_vset *vs = &m->v[idx & (TOTK_FM_VSETS - 1u)];
    for (unsigned w = 0; w < TOTK_FM_WAYS; w++) {
        const totk_fm_ent *e = &vs->e[w];
        if (e->gen == m->gen && e->key == key) return e;
    }
    return NULL;
}

static inline int totk_fmemo_sub_ok(const totk_fm_ent *e, uint64_t sub) {
    assert(e != NULL);
    return TOTK_RES_FAST_FAULT(16) || e->sub == sub;
}

static inline void totk_fmemo_put(totk_fmemo *m, uint32_t key, uint64_t sub, uint64_t v, void *x) {
    assert(m != NULL);
    unsigned idx = totk_fm_idx(key);
    totk_fm_ent *p = &m->p[idx];
    if (p->gen == m->gen && p->key != key) {
        totk_fm_vset *vs = &m->v[idx & (TOTK_FM_VSETS - 1u)];
        unsigned slot = TOTK_FM_WAYS;
        for (unsigned w = 0; w < TOTK_FM_WAYS; w++) {
            if (vs->e[w].gen != m->gen || vs->e[w].key == p->key) { slot = w; break; }
        }
        if (slot == TOTK_FM_WAYS) slot = vs->rr++ & (TOTK_FM_WAYS - 1u);
        vs->e[slot] = *p;
    }
    *p = (totk_fm_ent){ m->gen, key, sub, v, x };
}

static inline totk_tvic_ent *totk_tvic_find(totk_tidc_vic *vc, unsigned hash, int tid, int want, uint32_t gen) {
    assert(vc != NULL);
    totk_tvic_set *set = &vc->s[hash & (TOTK_TVIC_SETS - 1u)];
    for (unsigned w = 0; w < TOTK_TVIC_WAYS; w++) {
        totk_tvic_ent *e = &set->e[w];
        if (e->tid == tid && e->want == want && (TOTK_RES_FAST_FAULT(17) || e->gen == gen)) return e;
    }
    return NULL;
}

static inline void totk_tvic_put(totk_tidc_vic *vc, unsigned hash, const totk_tvic_ent *src) {
    assert(vc != NULL && src != NULL);
    if (src->tid <= 0) return;
    totk_tvic_set *set = &vc->s[hash & (TOTK_TVIC_SETS - 1u)];
    unsigned slot = TOTK_TVIC_WAYS;
    for (unsigned w = 0; w < TOTK_TVIC_WAYS; w++) {
        if (!set->e[w].tid || (set->e[w].tid == src->tid && set->e[w].want == src->want)) { slot = w; break; }
    }
    if (slot == TOTK_TVIC_WAYS) slot = set->rr++ & (TOTK_TVIC_WAYS - 1u);
    set->e[slot] = *src;
}

static inline unsigned totk_sc4_idx(int sid) {
    return (unsigned)sid & (TOTK_SC4_SETS - 1u);
}

static inline uint64_t totk_scache4_get(const totk_scache4 *c, int sid, uint32_t ep) {
    assert(c != NULL);
    if (sid <= 0) return 0;
    const totk_sc4_set *set = &c->s[totk_sc4_idx(sid)];
    for (unsigned w = 0; w < TOTK_SC4_WAYS; w++) {
        const totk_sc4_ent *e = &set->e[w];
        if (e->sid == sid && (TOTK_RES_FAST_FAULT(18) || e->ep == ep) && e->samp_u) return e->samp_u;
    }
    return 0;
}

static inline void totk_scache4_put(totk_scache4 *c, int sid, uint32_t ep, uint64_t samp_u) {
    assert(c != NULL);
    if (sid <= 0 || !samp_u) return;
    totk_sc4_set *set = &c->s[totk_sc4_idx(sid)];
    unsigned slot = TOTK_SC4_WAYS;
    for (unsigned w = 0; w < TOTK_SC4_WAYS; w++) {
        if (set->e[w].sid == sid || !set->e[w].sid) { slot = w; break; }
    }
    if (slot == TOTK_SC4_WAYS) slot = set->rr++ & (TOTK_SC4_WAYS - 1u);
    set->e[slot] = (totk_sc4_ent){sid, ep, samp_u};
}

#endif
