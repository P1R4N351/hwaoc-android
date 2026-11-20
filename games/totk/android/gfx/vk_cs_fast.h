#ifndef TOTK_VK_CS_FAST_H
#define TOTK_VK_CS_FAST_H
/* Wave 12 (Opt 14): Compute program 64x4 front-memo & CB compute pipeline bind elision (TOTK_VK_CS_FAST=1). */
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TOTK_KTGL_OPT_TEST
extern int g_fault_mode;
#define TOTK_CS_FAST_FAULT(n) (g_fault_mode == (n))
#else
#define TOTK_CS_FAST_FAULT(n) 0
#endif

static inline int totk_vk_cs_fast_on(void) {
    static _Atomic int cached = -1;
    int v = atomic_load_explicit(&cached, memory_order_relaxed);
    if (v < 0) {
        const char *e = getenv("TOTK_VK_CS_FAST");
        v = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&cached, v, memory_order_relaxed);
    }
    return v;
}

enum { TOTK_CS_PROG_SETS = 64u, TOTK_CS_PROG_WAYS = 4u };

typedef struct {
    uint64_t prog_u, serial, epoch, sig5, sh5_u;
    uint32_t valid, pad;
} totk_cs_prog_ent;

typedef struct {
    totk_cs_prog_ent ways[TOTK_CS_PROG_WAYS];
    uint32_t next_way, pad;
} totk_cs_prog_set;

typedef struct {
    totk_cs_prog_set sets[TOTK_CS_PROG_SETS];
} totk_cs_prog_memo4;

static inline uint32_t totk_cs_prog_idx(uint64_t prog_u) {
    return (uint32_t)(((prog_u >> 5) ^ (prog_u >> 11) ^ (prog_u >> 17)) & (TOTK_CS_PROG_SETS - 1u));
}

static inline int totk_cs_prog_memo4_find(const totk_cs_prog_memo4 *m, uint64_t prog_u,
                                         uint64_t serial, uint64_t epoch, uint64_t sig5,
                                         uint64_t *sh5_out) {
    assert(m != NULL && sh5_out != NULL);
    const totk_cs_prog_set *s = &m->sets[totk_cs_prog_idx(prog_u)];
    for (uint32_t i = 0; i < TOTK_CS_PROG_WAYS; i++) {
        const totk_cs_prog_ent *w = &s->ways[i];
        if (!w->valid) continue;
        if (w->prog_u != prog_u || w->epoch != epoch) continue;
        if (!TOTK_CS_FAST_FAULT(33) && w->serial != serial) continue;
        if (!TOTK_CS_FAST_FAULT(32) && w->sig5 != sig5) continue;
        *sh5_out = w->sh5_u;
        return 1;
    }
    return 0;
}

static inline void totk_cs_prog_memo4_put(totk_cs_prog_memo4 *m, uint64_t prog_u,
                                          uint64_t serial, uint64_t epoch, uint64_t sig5,
                                          uint64_t sh5_u) {
    assert(m != NULL);
    totk_cs_prog_set *s = &m->sets[totk_cs_prog_idx(prog_u)];
    for (uint32_t i = 0; i < TOTK_CS_PROG_WAYS; i++) {
        totk_cs_prog_ent *w = &s->ways[i];
        if (!w->valid || (w->prog_u == prog_u && (w->serial != serial ||
                          (w->epoch == epoch && w->sig5 == sig5)))) {
            w->prog_u = prog_u; w->serial = serial; w->epoch = epoch;
            w->sig5 = sig5; w->sh5_u = sh5_u; w->valid = 1u; w->pad = 0u;
            return;
        }
    }
    totk_cs_prog_ent *vict = &s->ways[s->next_way & (TOTK_CS_PROG_WAYS - 1u)];
    s->next_way = (s->next_way + 1u) & (TOTK_CS_PROG_WAYS - 1u);
    vict->prog_u = prog_u; vict->serial = serial; vict->epoch = epoch;
    vict->sig5 = sig5; vict->sh5_u = sh5_u; vict->valid = 1u; vict->pad = 0u;
}

typedef struct {
    uint64_t cb_u, sub, pipe_u;
    int valid, pad;
} totk_cs_pipe_state;

static inline void totk_cs_pipe_invalidate(totk_cs_pipe_state *s) {
    assert(s != NULL);
    s->valid = 0;
    s->pipe_u = 0;
}

static inline int totk_cs_pipe_bind_needed(totk_cs_pipe_state *s, int enabled,
                                           uint64_t cb_u, uint64_t sub, uint64_t pipe_u) {
    assert(s != NULL);
    if (!enabled) {
        totk_cs_pipe_invalidate(s);
        return 1;
    }
    if (s->valid && s->cb_u == cb_u && s->sub == sub &&
        (TOTK_CS_FAST_FAULT(34) || s->pipe_u == pipe_u)) {
        return 0;
    }
    s->valid = 1;
    s->cb_u = cb_u;
    s->sub = sub;
    s->pipe_u = pipe_u;
    return 1;
}

#endif
