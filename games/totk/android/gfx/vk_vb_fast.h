#ifndef TOTK_VK_VB_FAST_H
#define TOTK_VK_VB_FAST_H
/* Wave 9 (Opt 11): per-cb vertex/index bind filter, coalesced runs, per-sub zero VBO & 64x4 vbuf span memo (TOTK_VK_VB_FILTER=1). */
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef TOTK_KTGL_OPT_TEST
extern int g_fault_mode;
#define TOTK_VB_FAST_FAULT(n) (g_fault_mode == (n))
#else
#define TOTK_VB_FAST_FAULT(n) 0
#endif

enum { TOTK_VB_SLOTS = 17, TOTK_VBUF_SETS = 64, TOTK_VBUF_WAYS = 4 };

typedef struct {
    uint64_t buf_u, off, sz, stride;
} totk_vb_slot;

typedef struct {
    uint64_t cb_u, sub, ib_u, ib_off, zero_sub, zero_buf_u, zero_off;
    uint32_t valid_mask, ib_type;
    int ib_valid, pad;
    totk_vb_slot slot[TOTK_VB_SLOTS];
} totk_vb_state;

typedef struct {
    uint64_t gen, addr, buf_u, off, avail;
} totk_vbuf_ent;

typedef struct {
    totk_vbuf_ent e[TOTK_VBUF_WAYS];
    uint32_t rr, pad;
} totk_vbuf_set;

typedef struct {
    totk_vbuf_set s[TOTK_VBUF_SETS];
} totk_vbuf_memo4;

static inline int totk_vk_vb_filter_on(void) {
    static _Atomic int v = -1;
    int r = atomic_load_explicit(&v, memory_order_relaxed);
    if (r < 0) {
        const char *e = getenv("TOTK_VK_VB_FILTER");
        r = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&v, r, memory_order_relaxed);
    }
    return r;
}

static inline void totk_vb_invalidate(totk_vb_state *s) {
    assert(s != NULL);
    s->cb_u = 0; s->sub = 0; s->valid_mask = 0; s->ib_valid = 0;
}

static inline void totk_vb_sync_cb(totk_vb_state *s, uint64_t cb_u, uint64_t sub) {
    assert(s != NULL);
    if (s->cb_u != cb_u || s->sub != sub) {
        s->cb_u = cb_u; s->sub = sub; s->valid_mask = 0; s->ib_valid = 0;
    }
}

static inline int totk_vb_slot_changed(totk_vb_state *s, uint64_t cb_u, uint64_t sub, uint32_t idx,
                                       uint64_t buf_u, uint64_t off, uint64_t sz, uint64_t stride) {
    assert(s != NULL && idx < TOTK_VB_SLOTS);
    totk_vb_sync_cb(s, cb_u, sub);
    totk_vb_slot *e = &s->slot[idx];
    if ((s->valid_mask >> idx & 1u) && e->buf_u == buf_u &&
        (TOTK_VB_FAST_FAULT(23) || e->off == off) && e->sz == sz && e->stride == stride) {
        return 0;
    }
    *e = (totk_vb_slot){buf_u, off, sz, stride};
    s->valid_mask |= 1u << idx;
    return 1;
}

static inline void totk_vb_next_run(uint32_t *mask, uint32_t *first_out, uint32_t *count_out) {
    assert(mask != NULL && *mask != 0 && first_out != NULL && count_out != NULL);
    uint32_t first = (uint32_t)__builtin_ctz(*mask), end = first;
    while (end < TOTK_VB_SLOTS && ((*mask >> end) & 1u)) end++;
    *first_out = first;
    *count_out = end - first;
    uint32_t clr = (*count_out >= 32u) ? ~0u : (((1u << *count_out) - 1u) << first);
    *mask &= ~clr;
}

static inline int totk_ib_need_bind(totk_vb_state *s, uint64_t cb_u, uint64_t sub,
                                    uint64_t ib_u, uint64_t ib_off, uint32_t ib_type) {
    assert(s != NULL);
    totk_vb_sync_cb(s, cb_u, sub);
    if (s->ib_valid && s->ib_u == ib_u && s->ib_off == ib_off &&
        (TOTK_VB_FAST_FAULT(24) || s->ib_type == ib_type)) {
        return 0;
    }
    s->ib_valid = 1; s->ib_u = ib_u; s->ib_off = ib_off; s->ib_type = ib_type;
    return 1;
}

static inline uint32_t totk_vbuf_idx(uint64_t addr) {
    uint64_t x = ((addr >> 4) ^ (addr >> 17)) * 0xbf58476d1ce4e5b9ull;
    return (uint32_t)((x ^ (x >> 31)) & (TOTK_VBUF_SETS - 1u));
}

static inline int totk_vbuf_memo4_find(const totk_vbuf_memo4 *m, uint64_t gen, uint64_t addr,
                                       uint64_t *buf_out, uint64_t *off_out, uint64_t *avail_out) {
    assert(m != NULL && buf_out != NULL && off_out != NULL && avail_out != NULL);
    if (!gen || !addr) return 0;
    const totk_vbuf_set *set = &m->s[totk_vbuf_idx(addr)];
    for (uint32_t w = 0; w < TOTK_VBUF_WAYS; w++) {
        const totk_vbuf_ent *e = &set->e[w];
        if (e->avail && e->addr == addr && (TOTK_VB_FAST_FAULT(25) || e->gen == gen)) {
            *buf_out = e->buf_u; *off_out = e->off; *avail_out = e->avail;
            return 1;
        }
    }
    return 0;
}

static inline void totk_vbuf_memo4_put(totk_vbuf_memo4 *m, uint64_t gen, uint64_t addr,
                                       uint64_t buf_u, uint64_t off, uint64_t avail) {
    assert(m != NULL);
    if (!gen || !addr || !avail) return;
    totk_vbuf_set *set = &m->s[totk_vbuf_idx(addr)];
    uint32_t slot = TOTK_VBUF_WAYS;
    for (uint32_t w = 0; w < TOTK_VBUF_WAYS; w++) {
        const totk_vbuf_ent *e = &set->e[w];
        if (!e->avail || e->gen != gen || e->addr == addr) { slot = w; break; }
    }
    if (slot == TOTK_VBUF_WAYS) slot = set->rr++ & (TOTK_VBUF_WAYS - 1u);
    set->e[slot] = (totk_vbuf_ent){gen, addr, buf_u, off, avail};
}

#endif
