#ifndef TOTK_VK_UB_FAST_H
#define TOTK_VK_UB_FAST_H
/* Wave 17 (Opt 19): Power-of-two arena alignment, snapshot 4 KiB page bloom + 64x4 snap_find & 4-way uniform memos (TOTK_VK_UB_FAST=1). */
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TOTK_KTGL_OPT_TEST
extern int g_fault_mode;
#define TOTK_UB_FAULT(n) (g_fault_mode == (n))
#else
#define TOTK_UB_FAULT(n) 0
#endif

static inline int totk_ub_fast_on(void) {
    static _Atomic int cached = -1;
    int v = atomic_load_explicit(&cached, memory_order_relaxed);
    if (v < 0) {
        const char *e = getenv("TOTK_VK_UB_FAST");
        v = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&cached, v, memory_order_relaxed);
    }
    return v;
}

static inline uint64_t totk_align_up_sz(uint64_t used, uint64_t align) {
    assert(align > 0u);
    if ((align & (align - 1u)) == 0u) return (used + align - 1u) & ~(align - 1u);
    return (used + align - 1u) / align * align;
}

static inline uint32_t totk_ub_bit_idx(uint64_t p) {
    uint64_t x = (p ^ (p >> 7) ^ (p >> 17)) * UINT64_C(0x9e3779b97f4a7c15);
    return (uint32_t)(x >> 58) & 63u;
}

static inline uint64_t totk_ub_page_bloom(uint64_t lo, uint64_t hi) {
    if (hi < lo) return ~UINT64_C(0);
    if (hi == lo) return 0u;
    uint64_t p0 = lo >> 12, p1 = (hi - 1u) >> 12;
    if (TOTK_UB_FAULT(47)) p1 = p0;
    if (p0 == p1) return UINT64_C(1) << totk_ub_bit_idx(p0);
    if (p1 - p0 >= 64u) return ~UINT64_C(0);
    uint64_t m = 0u;
    for (uint64_t k = 0, n = p1 - p0 + 1u; k < n && k < 64u; k++)
        m |= UINT64_C(1) << totk_ub_bit_idx(p0 + k);
    return m;
}

enum {
    TOTK_SNAP_M_SETS = 64u,
    TOTK_UB_NEG_SETS = 64u,
    TOTK_UB_REN_SETS = 64u,
    TOTK_UB_CPY_SETS = 64u,
    TOTK_UB_PTR_SETS = 32u,
    TOTK_UB_WAYS = 4u
};

static inline uint32_t totk_ub_set_hash(uint64_t addr, uint64_t size, uint32_t mask) {
    uint64_t x = ((addr >> 4) ^ (addr >> 16) ^ (addr >> 30) ^ (size * UINT64_C(0x9e3779b97f4a7c15))) * UINT64_C(0xbf58476d1ce4e5b9);
    return (uint32_t)(x >> 40) & mask;
}

typedef struct {
    uint64_t addr, size, snap_gen;
    int32_t snap_idx;
    uint32_t valid;
} totk_snap_ent;

typedef struct {
    totk_snap_ent w[TOTK_UB_WAYS];
    uint8_t hand, pad[7];
} totk_snap_set;

typedef struct {
    totk_snap_set s[TOTK_SNAP_M_SETS];
} totk_snap_memo4;

static inline int totk_snap_memo4_get(const totk_snap_memo4 *m, uint64_t addr, uint64_t size,
                                      uint64_t snap_gen, int32_t *idx_out) {
    assert(m != NULL && idx_out != NULL && size > 0u);
    const totk_snap_set *set = &m->s[totk_ub_set_hash(addr, size, TOTK_SNAP_M_SETS - 1u)];
    for (uint32_t i = 0; i < TOTK_UB_WAYS; i++) {
        const totk_snap_ent *e = &set->w[i];
        if (e->valid && e->snap_gen == snap_gen && e->addr == addr && e->size == size) {
            *idx_out = e->snap_idx;
            return 1;
        }
    }
    return 0;
}

static inline void totk_snap_memo4_put(totk_snap_memo4 *m, uint64_t addr, uint64_t size,
                                       uint64_t snap_gen, int32_t snap_idx) {
    assert(m != NULL && size > 0u);
    totk_snap_set *set = &m->s[totk_ub_set_hash(addr, size, TOTK_SNAP_M_SETS - 1u)];
    totk_snap_ent ent = {addr, size, snap_gen, snap_idx, 1u};
    for (uint32_t i = 0; i < TOTK_UB_WAYS; i++) {
        totk_snap_ent *e = &set->w[i];
        if (!e->valid || e->snap_gen != snap_gen || (e->addr == addr && e->size == size)) {
            *e = ent;
            return;
        }
    }
    uint32_t v = set->hand & (TOTK_UB_WAYS - 1u);
    set->hand = (uint8_t)((set->hand + 1u) & (TOTK_UB_WAYS - 1u));
    set->w[v] = ent;
}

typedef struct {
    uint64_t address, size, revision;
    uint8_t valid, pad[7];
} totk_ub_neg_ent;

typedef struct {
    totk_ub_neg_ent w[TOTK_UB_WAYS];
    uint8_t hand, pad[7];
} totk_ub_neg_set;

typedef struct {
    totk_ub_neg_set s[TOTK_UB_NEG_SETS];
} totk_ub_neg_memo4;

static inline totk_ub_neg_ent *totk_ub_neg_find(totk_ub_neg_memo4 *m, uint64_t addr, uint64_t size) {
    assert(m != NULL && size > 0u);
    totk_ub_neg_set *set = &m->s[totk_ub_set_hash(addr, size, TOTK_UB_NEG_SETS - 1u)];
    for (uint32_t i = 0; i < TOTK_UB_WAYS; i++) {
        totk_ub_neg_ent *e = &set->w[i];
        if (e->valid && e->address == addr && e->size == size) return e;
    }
    return NULL;
}

static inline void totk_ub_neg_put(totk_ub_neg_memo4 *m, uint64_t addr, uint64_t size, uint64_t revision) {
    assert(m != NULL && size > 0u);
    totk_ub_neg_set *set = &m->s[totk_ub_set_hash(addr, size, TOTK_UB_NEG_SETS - 1u)];
    totk_ub_neg_ent ent = {addr, size, revision, 1u, {0}};
    for (uint32_t i = 0; i < TOTK_UB_WAYS; i++) {
        totk_ub_neg_ent *e = &set->w[i];
        if (!e->valid || (e->address == addr && e->size == size)) { *e = ent; return; }
    }
    uint32_t v = set->hand & (TOTK_UB_WAYS - 1u);
    set->hand = (uint8_t)((set->hand + 1u) & (TOTK_UB_WAYS - 1u));
    set->w[v] = ent;
}

typedef struct {
    uint64_t addr, size, gen, sub, sn_tag, buf_u, off, range;
    int ret, valid;
} totk_ub_ren_ent;

typedef struct {
    totk_ub_ren_ent w[TOTK_UB_WAYS];
    uint8_t hand, pad[7];
} totk_ub_ren_set;

typedef struct {
    totk_ub_ren_set s[TOTK_UB_REN_SETS];
} totk_ub_rename_memo4;

static inline uint32_t totk_ub_ren_set_idx(uint64_t addr, uint64_t size) {
    return totk_ub_set_hash(addr, size, TOTK_UB_REN_SETS - 1u);
}

static inline int totk_ub_ren_get(const totk_ub_rename_memo4 *m, uint64_t addr, uint64_t size,
                                  uint64_t gen, uint64_t sub, uint64_t sn_tag,
                                  int *ret_out, uint64_t *buf_out, uint64_t *off_out, uint64_t *rng_out) {
    assert(m != NULL && ret_out != NULL && buf_out != NULL && off_out != NULL && rng_out != NULL);
    const totk_ub_ren_set *set = &m->s[totk_ub_ren_set_idx(addr, size)];
    for (uint32_t i = 0; i < TOTK_UB_WAYS; i++) {
        const totk_ub_ren_ent *e = &set->w[i];
        if (!e->valid || e->addr != addr || e->size != size) continue;
        if (!TOTK_UB_FAULT(48) && e->gen != gen) continue;
        if (e->sub != sub || e->sn_tag != sn_tag) continue;
        *ret_out = e->ret;
        *buf_out = e->buf_u;
        *off_out = e->off;
        *rng_out = e->range;
        return 1;
    }
    return 0;
}

static inline void totk_ub_ren_put(totk_ub_rename_memo4 *m, uint64_t addr, uint64_t size,
                                   uint64_t gen, uint64_t sub, uint64_t sn_tag,
                                   int ret, uint64_t buf_u, uint64_t off, uint64_t range) {
    assert(m != NULL);
    totk_ub_ren_set *set = &m->s[totk_ub_ren_set_idx(addr, size)];
    totk_ub_ren_ent ent = {addr, size, gen, sub, sn_tag, buf_u, off, range, ret, 1};
    for (uint32_t i = 0; i < TOTK_UB_WAYS; i++) {
        totk_ub_ren_ent *e = &set->w[i];
        if (!e->valid || e->sub != sub || (e->addr == addr && e->size == size)) { *e = ent; return; }
    }
    uint32_t v = set->hand & (TOTK_UB_WAYS - 1u);
    set->hand = (uint8_t)((set->hand + 1u) & (TOTK_UB_WAYS - 1u));
    set->w[v] = ent;
}

typedef struct {
    uint64_t addr, size, seq, sub, base_tag, buf_u, off, range;
    int valid, pad_;
} totk_ub_cpy_ent;

typedef struct {
    totk_ub_cpy_ent w[TOTK_UB_WAYS];
    uint8_t hand, pad[7];
} totk_ub_cpy_set;

typedef struct {
    totk_ub_cpy_set s[TOTK_UB_CPY_SETS];
} totk_ub_copy_memo4;

static inline int totk_ub_cpy_get(const totk_ub_copy_memo4 *m, uint64_t addr, uint64_t size,
                                  uint64_t seq, uint64_t sub, uint64_t base_tag,
                                  uint64_t *buf_out, uint64_t *off_out, uint64_t *rng_out) {
    assert(m != NULL && buf_out != NULL && off_out != NULL && rng_out != NULL);
    const totk_ub_cpy_set *set = &m->s[totk_ub_set_hash(addr, size, TOTK_UB_CPY_SETS - 1u)];
    for (uint32_t i = 0; i < TOTK_UB_WAYS; i++) {
        const totk_ub_cpy_ent *e = &set->w[i];
        if (e->valid && e->addr == addr && e->size == size && e->seq == seq && e->sub == sub && e->base_tag == base_tag) {
            *buf_out = e->buf_u; *off_out = e->off; *rng_out = e->range; return 1;
        }
    }
    return 0;
}

static inline void totk_ub_cpy_put(totk_ub_copy_memo4 *m, uint64_t addr, uint64_t size,
                                   uint64_t seq, uint64_t sub, uint64_t base_tag,
                                   uint64_t buf_u, uint64_t off, uint64_t range) {
    assert(m != NULL);
    totk_ub_cpy_set *set = &m->s[totk_ub_set_hash(addr, size, TOTK_UB_CPY_SETS - 1u)];
    totk_ub_cpy_ent ent = {addr, size, seq, sub, base_tag, buf_u, off, range, 1, 0};
    for (uint32_t i = 0; i < TOTK_UB_WAYS; i++) {
        totk_ub_cpy_ent *e = &set->w[i];
        if (!e->valid || e->sub != sub || (e->addr == addr && e->size == size)) { *e = ent; return; }
    }
    uint32_t v = set->hand & (TOTK_UB_WAYS - 1u);
    set->hand = (uint8_t)((set->hand + 1u) & (TOTK_UB_WAYS - 1u));
    set->w[v] = ent;
}

typedef struct {
    uint64_t address, size, revision, submission, sn_tag;
    uint8_t bytes[32];
    uint8_t valid, pad[7];
} totk_ub_ptr_ent;

typedef struct {
    totk_ub_ptr_ent w[TOTK_UB_WAYS];
    uint8_t hand, pad[7];
} totk_ub_ptr_set;

typedef struct {
    totk_ub_ptr_set s[TOTK_UB_PTR_SETS];
} totk_ub_ptr_memo4;

static inline uint32_t totk_ub_ptr_set_idx(uint64_t addr, uint64_t size) {
    return totk_ub_set_hash(addr, size, TOTK_UB_PTR_SETS - 1u);
}

static inline totk_ub_ptr_ent *totk_ub_ptr_find(totk_ub_ptr_memo4 *m, uint64_t addr, uint64_t size,
                                                uint64_t submission, uint64_t sn_tag) {
    assert(m != NULL && size <= 32u);
    totk_ub_ptr_set *set = &m->s[totk_ub_ptr_set_idx(addr, size)];
    for (uint32_t i = 0; i < TOTK_UB_WAYS; i++) {
        totk_ub_ptr_ent *e = &set->w[i];
        if (!e->valid) continue;
        int addr_ok = TOTK_UB_FAULT(49) ? ((e->address & 0xffffu) == (addr & 0xffffu)) : (e->address == addr);
        if (addr_ok && e->size == size && e->sn_tag == sn_tag && e->submission == submission) return e;
    }
    return NULL;
}

static inline void totk_ub_ptr_put(totk_ub_ptr_memo4 *m, uint64_t addr, uint64_t size,
                                   uint64_t revision, uint64_t submission, uint64_t sn_tag,
                                   const void *src) {
    assert(m != NULL && src != NULL && size <= 32u);
    totk_ub_ptr_set *set = &m->s[totk_ub_ptr_set_idx(addr, size)];
    for (uint32_t i = 0; i < TOTK_UB_WAYS; i++) {
        totk_ub_ptr_ent *e = &set->w[i];
        if (!e->valid || e->submission != submission || (e->address == addr && e->size == size)) {
            e->address = addr; e->size = size; e->revision = revision; e->submission = submission; e->sn_tag = sn_tag;
            memcpy(e->bytes, src, (size_t)size); e->valid = 1u; return;
        }
    }
    uint32_t v = set->hand & (TOTK_UB_WAYS - 1u);
    set->hand = (uint8_t)((set->hand + 1u) & (TOTK_UB_WAYS - 1u));
    totk_ub_ptr_ent *dst = &set->w[v];
    dst->address = addr; dst->size = size; dst->revision = revision; dst->submission = submission; dst->sn_tag = sn_tag;
    memcpy(dst->bytes, src, (size_t)size); dst->valid = 1u;
}

#endif
