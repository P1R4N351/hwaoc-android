#ifndef TOTK_VK_TBUF_FAST_H
#define TOTK_VK_TBUF_FAST_H
/* Wave 15 (Opt 17): Lock-free TLS texel-buffer VkBufferView 64x4 front-memo (TOTK_VK_TBUF_FAST=1). */
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TOTK_KTGL_OPT_TEST
extern int g_fault_mode;
#define TOTK_TBUF_FAULT(n) (g_fault_mode == (n))
#else
#define TOTK_TBUF_FAULT(n) 0
#endif

static inline int totk_tbuf_fast_on(void) {
    static _Atomic int cached = -1;
    int v = atomic_load_explicit(&cached, memory_order_relaxed);
    if (v < 0) {
        const char *e = getenv("TOTK_VK_TBUF_FAST");
        v = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&cached, v, memory_order_relaxed);
    }
    return v;
}

static inline uint64_t totk_tbuf_epoch(uint64_t treg_rev, uint64_t ident_rev,
                                       uint64_t span_gen, uint64_t evict_gen) {
    uint64_t x = treg_rev ^ (ident_rev * UINT64_C(0x9e3779b97f4a7c15));
    x ^= (span_gen + UINT64_C(0x517cc1b727220a95)) * UINT64_C(0xbf58476d1ce4e5b9);
    x ^= evict_gen * UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
    return x ? x : 1ull;
}

enum { TOTK_TBV_SETS = 64u, TOTK_TBV_WAYS = 4u };

typedef struct {
    uint32_t tid, valid;
    uint64_t epoch, bv_u, buf_u, off, size, addr, touch_bytes;
} totk_tbuf_ent;

typedef struct {
    totk_tbuf_ent w[TOTK_TBV_WAYS];
    uint32_t hand, pad_;
} totk_tbuf_set;

typedef struct {
    totk_tbuf_set s[TOTK_TBV_SETS];
} totk_tbuf_memo4;

static inline uint32_t totk_tbuf_set_idx(uint32_t tid) {
    uint32_t x = tid ^ (tid >> 6) ^ (tid >> 12);
    return x & (TOTK_TBV_SETS - 1u);
}

static inline int totk_tbuf_memo4_get(const totk_tbuf_memo4 *m, uint32_t tid,
                                      uint64_t epoch, totk_tbuf_ent *out) {
    assert(m != NULL && out != NULL);
    const totk_tbuf_set *set = &m->s[totk_tbuf_set_idx(tid)];
    for (uint32_t i = 0; i < TOTK_TBV_WAYS; i++) {
        const totk_tbuf_ent *e = &set->w[i];
        if (!e->valid) continue;
        int id_ok = TOTK_TBUF_FAULT(42) ? ((e->tid & 0xffffu) == (tid & 0xffffu)) : (e->tid == tid);
        if (!id_ok) continue;
        if (!TOTK_TBUF_FAULT(41) && e->epoch != epoch) continue;
        *out = *e;
        return 1;
    }
    return 0;
}

static inline void totk_tbuf_memo4_put(totk_tbuf_memo4 *m, uint32_t tid, uint64_t epoch,
                                       int frozen_hit, uint64_t bv_u, uint64_t buf_u,
                                       uint64_t off, uint64_t size, uint64_t addr,
                                       uint64_t touch_bytes) {
    assert(m != NULL);
    if (!bv_u || !tid) return;
    if (frozen_hit && !TOTK_TBUF_FAULT(43)) return;
    totk_tbuf_set *set = &m->s[totk_tbuf_set_idx(tid)];
    totk_tbuf_ent ent = {tid, 1u, epoch, bv_u, buf_u, off, size, addr, touch_bytes};
    for (uint32_t i = 0; i < TOTK_TBV_WAYS; i++) {
        if (!set->w[i].valid || set->w[i].tid == tid) {
            set->w[i] = ent;
            return;
        }
    }
    uint32_t v = set->hand & (TOTK_TBV_WAYS - 1u);
    set->hand = (set->hand + 1u) & (TOTK_TBV_WAYS - 1u);
    set->w[v] = ent;
}

#endif
