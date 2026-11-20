#ifndef TOTK_VK_KTGL_MEMO_H
#define TOTK_VK_KTGL_MEMO_H
/* KTGL separable shader program (512x4 = 2,048) & pipeline layout (256x4 = 1,024) front-memos
   + narrow VkMemoryBarrier2 specs for indirect QUADS. Recording-thread owned. */
#include <stdint.h>

enum {
    VK_KTGL_PROGC_SETS = 512,
    VK_KTGL_PROGC_WAYS = 4,
    VK_KTGL_LAY_SETS = 512,
    VK_KTGL_LAY_WAYS = 4,
    VK_KTGL_PIPE_SETS = 1024,
    VK_KTGL_PIPE_WAYS = 4,
    VK_KTGL_CS_SETS = 64,
    VK_KTGL_CS_WAYS = 4
};

typedef struct { uint64_t serial; void *pr; } vk_progc_slot;
typedef struct { vk_progc_slot ways[VK_KTGL_PROGC_WAYS]; uint8_t next; } vk_progc_set_t;
typedef struct { vk_progc_set_t sets[VK_KTGL_PROGC_SETS]; } vk_progc_memo;

typedef struct { const void *vs; const void *fs; uint64_t layout; } vk_lay_slot;
typedef struct { vk_lay_slot ways[VK_KTGL_LAY_WAYS]; uint8_t next; } vk_lay_set_t;
typedef struct { vk_lay_set_t sets[VK_KTGL_LAY_SETS]; } vk_lay_memo;

typedef struct { uint64_t src_stage, src_access, dst_stage, dst_access; } vk_qi_bar2_spec;

static inline uint32_t vk_progc_set_idx(uint64_t serial) {
    return (uint32_t)(((serial * 11400714819323198485ull) >> 55) & (VK_KTGL_PROGC_SETS - 1));
}

static inline uint32_t vk_lay_set_idx(const void *vs, const void *fs) {
    uint64_t h = (uint64_t)(uintptr_t)vs * 11400714819323198485ull + (uint64_t)(uintptr_t)fs;
    h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ull;
    h = (h ^ (h >> 27)) * 0x94d049bb133111ebull;
    return (uint32_t)((h ^ (h >> 31)) >> 55) & (VK_KTGL_LAY_SETS - 1);
}

static inline void *vk_progc_peek(const vk_progc_memo *m, uint64_t serial) {
    if (!m || !serial) return 0;
    const vk_progc_slot *ways = m->sets[vk_progc_set_idx(serial)].ways;
    for (int w = 0; w < VK_KTGL_PROGC_WAYS; w++) if (ways[w].serial == serial) return ways[w].pr;
    return 0;
}

static inline void *vk_progc_get(const vk_progc_memo *m, uint64_t serial) {
    return vk_progc_peek(m, serial);
}

static inline void vk_progc_put(vk_progc_memo *m, uint64_t serial, void *pr) {
    if (!m || !serial || !pr) return;
    vk_progc_set_t *s = &m->sets[vk_progc_set_idx(serial)];
    int slot = -1;
    for (int w = 0; w < VK_KTGL_PROGC_WAYS; w++) {
        if (!s->ways[w].serial || s->ways[w].serial == serial) { slot = w; break; }
    }
    if (slot < 0) { slot = s->next & (VK_KTGL_PROGC_WAYS - 1); s->next = (uint8_t)(s->next + 1); }
    s->ways[slot].serial = serial;
    s->ways[slot].pr = pr;
}

static inline uint64_t vk_lay_memo_get(const vk_lay_memo *m, const void *vs, const void *fs) {
    if (!m || !vs) return 0;
    const vk_lay_slot *ways = m->sets[vk_lay_set_idx(vs, fs)].ways;
    for (int w = 0; w < VK_KTGL_LAY_WAYS; w++) {
        if (ways[w].layout && ways[w].vs == vs && ways[w].fs == fs) return ways[w].layout;
    }
    return 0;
}

static inline void vk_lay_memo_put(vk_lay_memo *m, const void *vs, const void *fs, uint64_t layout) {
    if (!m || !vs || !layout) return;
    vk_lay_set_t *s = &m->sets[vk_lay_set_idx(vs, fs)];
    int slot = -1;
    for (int w = 0; w < VK_KTGL_LAY_WAYS; w++) {
        if (!s->ways[w].layout || (s->ways[w].vs == vs && s->ways[w].fs == fs)) { slot = w; break; }
    }
    if (slot < 0) { slot = s->next & (VK_KTGL_LAY_WAYS - 1); s->next = (uint8_t)(s->next + 1); }
    s->ways[slot].vs = vs;
    s->ways[slot].fs = fs;
    s->ways[slot].layout = layout;
}

static inline vk_qi_bar2_spec vk_qi_barrier_spec(int phase, int fault_all) {
    enum {
        QI_STAGE_DRAW_INDIRECT = 0x00000002u, QI_STAGE_COMPUTE = 0x00000800u,
        QI_STAGE_TRANSFER = 0x00001000u, QI_STAGE_ALL = 0x00010000u,
        QI_ACC_INDIRECT = 0x00000001u, QI_ACC_SHADER_R = 0x00000020u,
        QI_ACC_SHADER_W = 0x00000040u, QI_ACC_XFER_W = 0x00001000u
    };
    if (fault_all) return (vk_qi_bar2_spec){QI_STAGE_ALL, QI_ACC_XFER_W, QI_STAGE_ALL, QI_ACC_SHADER_R};
    if (phase == 1) return (vk_qi_bar2_spec){QI_STAGE_TRANSFER, QI_ACC_XFER_W, QI_STAGE_COMPUTE, QI_ACC_SHADER_R | QI_ACC_SHADER_W};
    return (vk_qi_bar2_spec){QI_STAGE_COMPUTE, QI_ACC_SHADER_W, QI_STAGE_DRAW_INDIRECT, QI_ACC_INDIRECT};
}

typedef struct { void *ways[VK_KTGL_PIPE_WAYS]; uint8_t next; } vk_pipe_set_t;
typedef struct { vk_pipe_set_t sets[VK_KTGL_PIPE_SETS]; } vk_pipe_memo;

typedef struct { const void *cs; uint64_t pipeline; uint64_t layout; } vk_cs_slot;
typedef struct { vk_cs_slot ways[VK_KTGL_CS_WAYS]; uint8_t next; } vk_cs_set_t;
typedef struct { vk_cs_set_t sets[VK_KTGL_CS_SETS]; } vk_cs_memo;

static inline uint64_t vk_ktgl_splitmix64(uint64_t x) {
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

static inline uint32_t vk_pipe_set_idx(const void *vs, const void *fs, uint32_t topo, uint64_t layout) {
    uint64_t h = ((uint64_t)(uintptr_t)vs * UINT64_C(11400714819323198485)
                + (uint64_t)(uintptr_t)fs * UINT64_C(0x9e3779b97f4a7c15))
               ^ ((uint64_t)topo * UINT64_C(0xbf58476d1ce4e5b9)) ^ layout;
    return (uint32_t)((vk_ktgl_splitmix64(h) >> 54) & (VK_KTGL_PIPE_SETS - 1));
}

static inline const vk_pipe_set_t *vk_pipe_memo_set(const vk_pipe_memo *m, const void *vs,
                                                    const void *fs, uint32_t topo, uint64_t layout) {
    return &m->sets[vk_pipe_set_idx(vs, fs, topo, layout)];
}

static inline void vk_pipe_memo_put(vk_pipe_memo *m, const void *vs, const void *fs,
                                    uint32_t topo, uint64_t layout, void *ent) {
    if (!m || !ent) return;
    vk_pipe_set_t *s = &m->sets[vk_pipe_set_idx(vs, fs, topo, layout)];
    for (int w = 0; w < VK_KTGL_PIPE_WAYS; w++) {
        if (s->ways[w] == ent) return;
        if (!s->ways[w]) { s->ways[w] = ent; return; }
    }
    int slot = s->next & (VK_KTGL_PIPE_WAYS - 1);
    s->next = (uint8_t)(s->next + 1);
    s->ways[slot] = ent;
}

static inline uint32_t vk_cs_set_idx(const void *cs) {
    uint64_t h = vk_ktgl_splitmix64((uint64_t)(uintptr_t)cs * UINT64_C(11400714819323198485));
    return (uint32_t)((h >> 58) & (VK_KTGL_CS_SETS - 1));
}

static inline int vk_cs_memo_get(const vk_cs_memo *m, const void *cs, uint64_t *pipeline_out, uint64_t *layout_out) {
    if (!m || !cs || !pipeline_out || !layout_out) return 0;
    const vk_cs_slot *ways = m->sets[vk_cs_set_idx(cs)].ways;
    for (int w = 0; w < VK_KTGL_CS_WAYS; w++) {
        if (ways[w].cs == cs && ways[w].pipeline) {
            *pipeline_out = ways[w].pipeline;
            *layout_out = ways[w].layout;
            return 1;
        }
    }
    return 0;
}

static inline void vk_cs_memo_put(vk_cs_memo *m, const void *cs, uint64_t pipeline, uint64_t layout) {
    if (!m || !cs || !pipeline) return;
    vk_cs_set_t *s = &m->sets[vk_cs_set_idx(cs)];
    int slot = -1;
    for (int w = 0; w < VK_KTGL_CS_WAYS; w++) {
        if (!s->ways[w].cs || s->ways[w].cs == cs) { slot = w; break; }
    }
    if (slot < 0) { slot = s->next & (VK_KTGL_CS_WAYS - 1); s->next = (uint8_t)(s->next + 1); }
    s->ways[slot] = (vk_cs_slot){cs, pipeline, layout};
}

typedef struct {
    uint64_t lo, hi, end_va;
    uint8_t bounded;
} vk_lag_iv;

enum { VK_LAG_IV_MRU = 4, VK_LAG_IV_SETS = 128, VK_LAG_IV_WAYS = 2 };

typedef struct {
    uint64_t mru_gen;
    vk_lag_iv mru[VK_LAG_IV_MRU];
    uint64_t set_gen[VK_LAG_IV_SETS];
    uint8_t hand[VK_LAG_IV_SETS];
    vk_lag_iv ways[VK_LAG_IV_SETS][VK_LAG_IV_WAYS];
} vk_lag_iv_cache;

static inline unsigned vk_lag_iv_set(uint64_t addr) {
    uint64_t h = ((addr >> 20) ^ (addr >> 32)) * UINT64_C(0x9e3779b97f4a7c15);
    return (unsigned)(h >> 57) & (VK_LAG_IV_SETS - 1);
}

static inline int vk_lag_iv_get(const vk_lag_iv_cache *c, uint64_t gen, uint64_t addr, uint64_t *bound_out) {
    if (!c || !gen || !bound_out) return 0;
    if (c->mru_gen == gen) {
        for (int k = 0; k < VK_LAG_IV_MRU; k++) {
            const vk_lag_iv *v = &c->mru[k];
            if (v->hi > v->lo && addr >= v->lo && addr < v->hi) {
                *bound_out = v->bounded ? (v->end_va - addr) : UINT64_MAX;
                return 1;
            }
        }
    }
    unsigned s = vk_lag_iv_set(addr);
    if (c->set_gen[s] != gen) return 0;
    const vk_lag_iv *w = c->ways[s];
    for (int k = 0; k < VK_LAG_IV_WAYS; k++) {
        if (w[k].hi > w[k].lo && addr >= w[k].lo && addr < w[k].hi) {
            *bound_out = w[k].bounded ? (w[k].end_va - addr) : UINT64_MAX;
            return 1;
        }
    }
    return 0;
}

static inline void vk_lag_iv_put(vk_lag_iv_cache *c, uint64_t gen, uint64_t lo, uint64_t hi, uint64_t end_va, uint8_t bounded) {
    if (!c || !gen || hi <= lo) return;
    vk_lag_iv entry = {lo, hi, end_va, bounded};
    if (c->mru_gen != gen) {
        for (int k = 0; k < VK_LAG_IV_MRU; k++) c->mru[k] = (vk_lag_iv){0};
        c->mru_gen = gen;
    } else if (c->mru[0].lo != lo || c->mru[0].hi != hi) {
        for (int k = VK_LAG_IV_MRU - 1; k > 0; k--) c->mru[k] = c->mru[k - 1];
    }
    c->mru[0] = entry;
    unsigned s = vk_lag_iv_set(lo);
    if (c->set_gen[s] != gen) {
        for (int k = 0; k < VK_LAG_IV_WAYS; k++) c->ways[s][k] = (vk_lag_iv){0};
        c->set_gen[s] = gen;
        c->hand[s] = 0;
    }
    for (int k = 0; k < VK_LAG_IV_WAYS; k++) {
        if (c->ways[s][k].hi <= c->ways[s][k].lo || (c->ways[s][k].lo == lo && c->ways[s][k].hi == hi)) {
            c->ways[s][k] = entry;
            return;
        }
    }
    unsigned victim = (unsigned)(c->hand[s]++ & (VK_LAG_IV_WAYS - 1));
    c->ways[s][victim] = entry;
}

static inline unsigned vk_ktgl_hint_set4(uintptr_t a) {
    uint64_t x = (uint64_t)(a >> 20) ^ (uint64_t)(a >> 28) ^ (uint64_t)(a >> 36);
    return (unsigned)(((x * UINT64_C(0x9e3779b97f4a7c15)) >> 60) << 2);
}

static inline int vk_ktgl_hints_peek(int hints[64], const uint64_t *seg_lo, int nseg, uintptr_t a, size_t stride) {
    /* NOTE: callers with typed structs invoke vk_ktgl_hints_lookup / put macros or helpers directly. */
    (void)hints; (void)seg_lo; (void)nseg; (void)a; (void)stride; return 0;
}
#endif
