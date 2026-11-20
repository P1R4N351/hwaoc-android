#ifndef TOTK_VK_STAGE_FAST_H
#define TOTK_VK_STAGE_FAST_H
/* Wave 10 (Opt 12): 128x4 multi-material stage freeze memo & 64x4 per-job worker descriptor-slice memo (TOTK_VK_STAGE_FAST=1). */
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TOTK_KTGL_OPT_TEST
extern int g_fault_mode;
#define TOTK_STAGE_FAST_FAULT(n) (g_fault_mode == (n))
#else
#define TOTK_STAGE_FAST_FAULT(n) 0
#endif

enum {
    TOTK_STAGE_FREEZE_SETS = 128,
    TOTK_STAGE_FREEZE_WAYS = 4,
    TOTK_STAGE_FREEZE_SLOTS = 512,
    TOTK_STAGE_MAX_NB = 48,
    TOTK_REC_SLICE_SETS = 64,
    TOTK_REC_SLICE_WAYS = 4
};

typedef struct {
    uint64_t chunk, submission, uniforms, resources, samplers, cpu_dirty, dirty_epoch;
    uint64_t spans_u, sh_u, native_sample_u, feedback_pass, native_image_u;
    uint32_t width, height, stage, nb;
} totk_stage_env;

typedef struct {
    uint64_t layout_u, update_u;
    uint32_t first, count;
} totk_stage_set_ref;

typedef struct {
    totk_stage_env env;
    totk_stage_set_ref set;
    uint64_t bind_sig;
    uint32_t valid, pad;
} totk_stage_freeze_meta;

typedef struct {
    totk_stage_freeze_meta e[TOTK_STAGE_FREEZE_WAYS];
    uint32_t rr, pad;
} totk_stage_freeze_set;

typedef struct {
    totk_stage_freeze_set s[TOTK_STAGE_FREEZE_SETS];
} totk_stage_freeze4;

typedef struct {
    uint64_t bind_words[TOTK_STAGE_MAX_NB * 2];
} totk_stage_freeze_slot;

typedef struct {
    uint64_t job_seq, layout_u, spans_u, lag_ticket, set_u;
    uint32_t first, count;
} totk_rec_slice_ent;

typedef struct {
    totk_rec_slice_ent e[TOTK_REC_SLICE_WAYS];
    uint32_t rr, pad;
} totk_rec_slice_set;

typedef struct {
    totk_rec_slice_set s[TOTK_REC_SLICE_SETS];
} totk_rec_slice_memo4;

static inline int totk_vk_stage_fast_on(void) {
    static _Atomic int v = -1;
    int r = atomic_load_explicit(&v, memory_order_relaxed);
    if (r < 0) {
        const char *e = getenv("TOTK_VK_STAGE_FAST");
        r = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&v, r, memory_order_relaxed);
    }
    return r;
}

static inline uint64_t totk_stage_bind_hash(uint64_t sh_u, uint32_t stage, const void *bindings, uint32_t nb) {
    assert(nb <= TOTK_STAGE_MAX_NB && (nb == 0 || bindings != NULL));
    const uint64_t *w = (const uint64_t *)bindings;
    uint64_t a = 0xcbf29ce484222325ull ^ (sh_u * 0x9e3779b97f4a7c15ull) ^ ((uint64_t)stage << 48);
    uint64_t c = 0x6a09e667f3bcc909ull ^ ((uint64_t)nb * 0xbf58476d1ce4e5b9ull);
    for (uint32_t i = 0; i < nb; i++) {
        a = (a ^ w[2u * i]) * 0x9e3779b97f4a7c15ull;
        c = (c + w[2u * i + 1u]) * 0xc2b2ae3d27d4eb4full;
        a ^= a >> 31; c = c << 29 | c >> 35;
    }
    return a ^ c;
}

static inline uint32_t totk_stage_idx(uint64_t sh_u, uint64_t bind_sig) {
    if (TOTK_STAGE_FAST_FAULT(26)) return (uint32_t)((sh_u >> 4) & (TOTK_STAGE_FREEZE_SETS - 1u));
    return (uint32_t)((bind_sig ^ (bind_sig >> 32)) & (TOTK_STAGE_FREEZE_SETS - 1u));
}

static inline int totk_stage_env_base_eq(const totk_stage_env *a, const totk_stage_env *b) {
    if (!TOTK_STAGE_FAST_FAULT(27) && (a->chunk != b->chunk || a->submission != b->submission)) return 0;
    return a->sh_u == b->sh_u && a->stage == b->stage && a->nb == b->nb &&
           a->resources == b->resources && a->samplers == b->samplers &&
           a->cpu_dirty == b->cpu_dirty && a->dirty_epoch == b->dirty_epoch &&
           a->spans_u == b->spans_u && a->native_sample_u == b->native_sample_u &&
           a->feedback_pass == b->feedback_pass && a->native_image_u == b->native_image_u &&
           a->width == b->width && a->height == b->height;
}

static inline int totk_stage_freeze4_find(totk_stage_freeze4 *m, const totk_stage_freeze_slot *slots,
                                          const totk_stage_env *env, uint64_t bind_sig, const void *bindings,
                                          int (*uni_ok)(void *ctx, uint64_t cached_uni), void *ctx,
                                          totk_stage_set_ref *out_set) {
    assert(m != NULL && slots != NULL && env != NULL && bindings != NULL && out_set != NULL);
    if (!env->nb || env->nb > TOTK_STAGE_MAX_NB) return 0;
    uint32_t set_idx = totk_stage_idx(env->sh_u, bind_sig);
    totk_stage_freeze_set *set = &m->s[set_idx];
    size_t bbytes = (size_t)env->nb * 16u;
    for (uint32_t w = 0; w < TOTK_STAGE_FREEZE_WAYS; w++) {
        totk_stage_freeze_meta *e = &set->e[w];
        uint32_t slot = set_idx * TOTK_STAGE_FREEZE_WAYS + w;
        if (!e->valid) continue;
        if (!TOTK_STAGE_FAST_FAULT(26) && e->bind_sig != bind_sig) continue;
        if (!totk_stage_env_base_eq(&e->env, env)) continue;
        if (!TOTK_STAGE_FAST_FAULT(26) && memcmp(slots[slot].bind_words, bindings, bbytes) != 0) continue;
        if (e->env.uniforms != env->uniforms && !(uni_ok && uni_ok(ctx, e->env.uniforms))) continue;
        e->env.uniforms = env->uniforms;
        *out_set = e->set;
        return 1;
    }
    return 0;
}

static inline void totk_stage_freeze4_put(totk_stage_freeze4 *m, totk_stage_freeze_slot *slots,
                                          const totk_stage_env *env, uint64_t bind_sig, const void *bindings,
                                          const totk_stage_set_ref *set_ref) {
    assert(m != NULL && slots != NULL && env != NULL && bindings != NULL && set_ref != NULL);
    if (!env->nb || env->nb > TOTK_STAGE_MAX_NB) return;
    uint32_t set_idx = totk_stage_idx(env->sh_u, bind_sig);
    totk_stage_freeze_set *set = &m->s[set_idx];
    uint32_t way = TOTK_STAGE_FREEZE_WAYS;
    for (uint32_t w = 0; w < TOTK_STAGE_FREEZE_WAYS; w++) {
        const totk_stage_freeze_meta *e = &set->e[w];
        if (!e->valid || e->env.chunk != env->chunk || e->env.submission != env->submission) { way = w; break; }
    }
    if (way == TOTK_STAGE_FREEZE_WAYS) way = set->rr++ & (TOTK_STAGE_FREEZE_WAYS - 1u);
    uint32_t slot = set_idx * TOTK_STAGE_FREEZE_WAYS + way;
    memcpy(slots[slot].bind_words, bindings, (size_t)env->nb * 16u);
    set->e[way] = (totk_stage_freeze_meta){*env, *set_ref, bind_sig, 1u, 0u};
}

static inline uint32_t totk_rec_slice_idx(uint32_t first, uint32_t count, uint64_t layout_u) {
    uint64_t z = ((uint64_t)first << 32) ^ (uint64_t)count ^ layout_u;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return (uint32_t)((z ^ (z >> 31)) & (TOTK_REC_SLICE_SETS - 1u));
}

static inline int totk_rec_slice4_find(const totk_rec_slice_memo4 *m, uint64_t job_seq,
                                       uint32_t first, uint32_t count, uint64_t layout_u,
                                       uint64_t spans_u, uint64_t lag_ticket, uint64_t *set_out) {
    assert(m != NULL && set_out != NULL);
    if (!job_seq || !count) return 0;
    const totk_rec_slice_set *set = &m->s[totk_rec_slice_idx(first, count, layout_u)];
    for (uint32_t w = 0; w < TOTK_REC_SLICE_WAYS; w++) {
        const totk_rec_slice_ent *e = &set->e[w];
        if (e->job_seq == job_seq && e->set_u && e->first == first && e->count == count && e->layout_u == layout_u &&
            (TOTK_STAGE_FAST_FAULT(28) || (e->spans_u == spans_u && e->lag_ticket == lag_ticket))) {
            *set_out = e->set_u;
            return 1;
        }
    }
    return 0;
}

static inline void totk_rec_slice4_put(totk_rec_slice_memo4 *m, uint64_t job_seq,
                                       uint32_t first, uint32_t count, uint64_t layout_u,
                                       uint64_t spans_u, uint64_t lag_ticket, uint64_t set_u) {
    assert(m != NULL);
    if (!job_seq || !count || !set_u) return;
    totk_rec_slice_set *set = &m->s[totk_rec_slice_idx(first, count, layout_u)];
    uint32_t way = TOTK_REC_SLICE_WAYS;
    for (uint32_t w = 0; w < TOTK_REC_SLICE_WAYS; w++) {
        const totk_rec_slice_ent *e = &set->e[w];
        if (e->job_seq != job_seq || !e->set_u || (e->first == first && e->count == count && e->layout_u == layout_u)) {
            way = w; break;
        }
    }
    if (way == TOTK_REC_SLICE_WAYS) way = set->rr++ & (TOTK_REC_SLICE_WAYS - 1u);
    set->e[way] = (totk_rec_slice_ent){job_seq, layout_u, spans_u, lag_ticket, set_u, first, count};
}

#endif
