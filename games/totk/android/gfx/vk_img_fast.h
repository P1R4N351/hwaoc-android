#ifndef TOTK_VK_IMG_FAST_H
#define TOTK_VK_IMG_FAST_H
/* Wave 22 (Opt 24): Lock-free reserved-fence reads, 4-way tidc4 memo, clear-on-read bitmask & steady gpu_written elision. */
#include <assert.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TOTK_KTGL_OPT_TEST
extern int g_fault_mode;
#define TOTK_IMG_FAULT(n) (g_fault_mode == (n))
#else
#define TOTK_IMG_FAULT(n) 0
#endif

#ifdef W22_KTGL_TEST
extern int g_w22_fast_on;
static inline int totk_vk_img_fast_on(void) { return g_w22_fast_on != 0; }
#else
static inline int totk_vk_img_fast_on(void) {
    static _Atomic int cached = -1;
    int v = atomic_load_explicit(&cached, memory_order_relaxed);
    if (v < 0) {
        const char *e = getenv("TOTK_VK_IMG_FAST");
        v = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&cached, v, memory_order_relaxed);
    }
    return v;
}
#endif

enum {
    TOTK_TIDC4_SETS = 256u,
    TOTK_TIDC4_WAYS = 4u,
    TOTK_CRM_SETS = 32u,
    TOTK_CRM_WAYS = 4u
};

typedef struct {
    void *tex_cell, *img_cell;
    uint64_t tex_val, img_val, sub, epoch, view_u;
    void *x;
    int32_t tid, want;
    uint32_t gen;
    uint8_t in_pool, valid, pad0, pad1;
} totk_tidc4_ent;

typedef struct {
    totk_tidc4_ent e[TOTK_TIDC4_SETS][TOTK_TIDC4_WAYS];
} totk_tidc4;

static inline uint32_t totk_tidc4_set(int32_t tid, int32_t want) {
    uint32_t h = (uint32_t)tid ^ ((uint32_t)tid >> 12) ^ (((uint32_t)want + 2u) * 0x9e3779b9u);
    return h & (TOTK_TIDC4_SETS - 1u);
}

static inline totk_tidc4_ent *totk_tidc4_probe(totk_tidc4 *c, int32_t tid,
                                                int32_t want, uint32_t gen) {
    assert(c != NULL);
    if (tid <= 0) return NULL;
    totk_tidc4_ent *ways = c->e[totk_tidc4_set(tid, want)];
    for (uint32_t w = 0; w < TOTK_TIDC4_WAYS; w++) {
        totk_tidc4_ent *e = &ways[w];
        if (!e->valid || e->tid != tid) continue;
        if (!TOTK_IMG_FAULT(62) && e->want != want) continue;
        if (!TOTK_IMG_FAULT(63) && e->gen != gen) continue;
        if (w > 0u) {
            totk_tidc4_ent hit = ways[w];
            for (uint32_t k = w; k > 0u; k--) ways[k] = ways[k - 1u];
            ways[0] = hit;
        }
        return &ways[0];
    }
    return NULL;
}

static inline void totk_tidc4_put(totk_tidc4 *c, const totk_tidc4_ent *in) {
    assert(c != NULL && in != NULL);
    if (in->tid <= 0) return;
    totk_tidc4_ent *ways = c->e[totk_tidc4_set(in->tid, in->want)];
    uint32_t pos = TOTK_TIDC4_WAYS - 1u;
    for (uint32_t w = 0; w < TOTK_TIDC4_WAYS; w++) {
        if (!ways[w].valid || (ways[w].tid == in->tid && ways[w].want == in->want)) {
            pos = w; break;
        }
    }
    for (uint32_t k = pos; k > 0u; k--) ways[k] = ways[k - 1u];
    ways[0] = *in;
    ways[0].valid = 1u;
}

static inline int totk_tidc4_needs_sub_check(const totk_tidc4_ent *e, uint64_t sub,
                                             int mirrored, int gpu_written) {
    assert(e != NULL);
    if (TOTK_IMG_FAULT(64)) return 0;
    return e->sub != sub && mirrored && !gpu_written;
}

typedef struct {
    uint64_t treg_rev, rt_sig;
    uint32_t tid;
    uint16_t clear_mask;
    uint8_t valid, pad;
} totk_crm_ent;

typedef struct {
    totk_crm_ent e[TOTK_CRM_SETS][TOTK_CRM_WAYS];
} totk_clear_read_memo4;

static inline uint32_t totk_crm_set(uint32_t tid) {
    return (tid ^ (tid >> 5) ^ (tid >> 11)) & (TOTK_CRM_SETS - 1u);
}

static inline int totk_clear_read_memo4_hit(totk_clear_read_memo4 *m, uint32_t tid,
                                            uint64_t treg_rev, uint64_t rt_sig,
                                            uint16_t clear_mask) {
    assert(m != NULL);
    if (!tid) return 0;
    totk_crm_ent *ways = m->e[totk_crm_set(tid)];
    for (uint32_t w = 0; w < TOTK_CRM_WAYS; w++) {
        totk_crm_ent *e = &ways[w];
        if (!e->valid || e->tid != tid || e->treg_rev != treg_rev) continue;
        if (!TOTK_IMG_FAULT(65) && (e->rt_sig != rt_sig || e->clear_mask != clear_mask)) continue;
        if (w > 0u) {
            totk_crm_ent hit = ways[w];
            for (uint32_t k = w; k > 0u; k--) ways[k] = ways[k - 1u];
            ways[0] = hit;
        }
        return 1;
    }
    return 0;
}

static inline void totk_clear_read_memo4_put(totk_clear_read_memo4 *m, uint32_t tid,
                                             uint64_t treg_rev, uint64_t rt_sig,
                                             uint16_t clear_mask) {
    assert(m != NULL);
    if (!tid) return;
    totk_crm_ent *ways = m->e[totk_crm_set(tid)];
    uint32_t pos = TOTK_CRM_WAYS - 1u;
    for (uint32_t w = 0; w < TOTK_CRM_WAYS; w++) {
        if (!ways[w].valid || ways[w].tid == tid) { pos = w; break; }
    }
    for (uint32_t k = pos; k > 0u; k--) ways[k] = ways[k - 1u];
    ways[0] = (totk_crm_ent){treg_rev, rt_sig, tid, clear_mask, 1u, 0u};
}

static inline int totk_tex_steady_gpu_written(int gpu_written, int mirrored, int wb_track) {
    if (TOTK_IMG_FAULT(66)) return gpu_written != 0;
    return gpu_written && !mirrored && !wb_track;
}

#endif
