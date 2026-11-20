#ifndef TOTK_NVN_SUB_FAST_H
#define TOTK_NVN_SUB_FAST_H
/* Wave 14 (Opt 16): Deferred-bind span slice, per-submit handle deduplication & zero-memset buffer binds (TOTK_NVN_SUB_FAST=1). */
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TOTK_KTGL_OPT_TEST
extern int g_fault_mode;
#define TOTK_SUB_FAST_FAULT(n) (g_fault_mode == (n))
#else
#define TOTK_SUB_FAST_FAULT(n) 0
#endif

static inline int totk_nvn_sub_fast_on(void) {
    static _Atomic int cached = -1;
    int v = atomic_load_explicit(&cached, memory_order_relaxed);
    if (v < 0) {
        const char *e = getenv("TOTK_NVN_SUB_FAST");
        v = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&cached, v, memory_order_relaxed);
    }
    return v;
}

enum { TOTK_DEF_UNBOUNDED = UINT32_MAX };

typedef struct {
    uint32_t lo, hi;
} totk_def_span;

static inline void totk_def_span_init(totk_def_span *s) {
    assert(s != NULL);
    s->lo = TOTK_DEF_UNBOUNDED;
    s->hi = 0u;
}

static inline void totk_def_span_note_op(totk_def_span *s, size_t off, uint32_t size) {
    assert(s != NULL);
    if (s->hi == TOTK_DEF_UNBOUNDED) return;
    if (off >= (size_t)TOTK_DEF_UNBOUNDED ||
        (uint64_t)off + (uint64_t)size >= (uint64_t)TOTK_DEF_UNBOUNDED) {
        s->lo = 0u;
        s->hi = TOTK_DEF_UNBOUNDED;
        return;
    }
    uint32_t off32 = (uint32_t)off;
    uint32_t end = off32 + size;
    if (off32 < s->lo) s->lo = off32;
    if (end > s->hi) s->hi = end;
}

static inline void totk_def_span_note_marker(totk_def_span *s, size_t rec_off,
                                             uint32_t t_lo, uint32_t t_hi) {
    assert(s != NULL);
    if (s->hi == TOTK_DEF_UNBOUNDED || t_lo >= t_hi) return;
    if (t_hi == TOTK_DEF_UNBOUNDED || t_lo < 8u || rec_off >= (size_t)TOTK_DEF_UNBOUNDED) {
        s->lo = 0u;
        s->hi = TOTK_DEF_UNBOUNDED;
        return;
    }
    uint64_t alo = (uint64_t)rec_off + (uint64_t)(t_lo - 8u);
    uint64_t ahi = (uint64_t)rec_off + (uint64_t)(t_hi - 8u);
    if (ahi >= (uint64_t)TOTK_DEF_UNBOUNDED) {
        s->lo = 0u;
        s->hi = TOTK_DEF_UNBOUNDED;
        return;
    }
    if ((uint32_t)alo < s->lo) s->lo = (uint32_t)alo;
    if ((uint32_t)ahi > s->hi) s->hi = (uint32_t)ahi;
}

static inline int totk_def_span_slice(uint32_t def_lo, uint32_t def_hi,
                                      uint32_t *out_lo, uint32_t *out_hi) {
    assert(out_lo != NULL && out_hi != NULL);
    if (TOTK_SUB_FAST_FAULT(38)) return 0;
    if (def_lo >= def_hi) return 0;
    if (def_hi == TOTK_DEF_UNBOUNDED) return 2;
    *out_lo = def_lo;
    *out_hi = def_hi;
    return 1;
}

enum { TOTK_SUB_SEEN_SETS = 64u, TOTK_SUB_SEEN_WAYS = 4u };

typedef struct {
    uint64_t addr[TOTK_SUB_SEEN_WAYS];
    uint32_t gen;
    uint16_t valid_mask, next_way;
} totk_sub_seen_set;

typedef struct {
    uint32_t cur_gen, pad_;
    totk_sub_seen_set sets[TOTK_SUB_SEEN_SETS];
} totk_sub_seen4;

static inline uint32_t totk_sub_seen_idx(uint64_t addr_u) {
    return (uint32_t)(((addr_u >> 5) ^ (addr_u >> 12) ^ (addr_u >> 19)) & (TOTK_SUB_SEEN_SETS - 1u));
}

static inline void totk_sub_seen4_reset(totk_sub_seen4 *s) {
    assert(s != NULL);
    if (++s->cur_gen == 0u) {
        memset(s, 0, sizeof *s);
        s->cur_gen = 1u;
    }
}

static inline int totk_sub_seen4_first(totk_sub_seen4 *s, uint64_t addr_u) {
    assert(s != NULL);
    if (s->cur_gen == 0u) s->cur_gen = 1u;
    totk_sub_seen_set *b = &s->sets[totk_sub_seen_idx(addr_u)];
    if (b->gen != s->cur_gen) {
        b->gen = s->cur_gen;
        b->valid_mask = 1u;
        b->next_way = 0u;
        b->addr[0] = addr_u;
        return 1;
    }
    for (uint32_t i = 0; i < TOTK_SUB_SEEN_WAYS; i++) {
        if (!((b->valid_mask >> i) & 1u)) continue;
        int hit = TOTK_SUB_FAST_FAULT(39)
                      ? ((b->addr[i] >> 16) == (addr_u >> 16))
                      : (b->addr[i] == addr_u);
        if (hit) return 0;
    }
    for (uint32_t i = 0; i < TOTK_SUB_SEEN_WAYS; i++) {
        if (!((b->valid_mask >> i) & 1u)) {
            b->addr[i] = addr_u;
            b->valid_mask |= (uint16_t)(1u << i);
            return 1;
        }
    }
    uint32_t v = b->next_way & (TOTK_SUB_SEEN_WAYS - 1u);
    b->next_way = (uint16_t)((b->next_way + 1u) & (TOTK_SUB_SEEN_WAYS - 1u));
    b->addr[v] = addr_u;
    return 1;
}

static inline void totk_pack_bind_buf_rec(uint8_t *p, uint32_t op, int32_t st,
                                          int32_t idx, uint64_t addr, uint64_t sz) {
    assert(p != NULL);
    uint32_t hdr[2] = {op, 32u};
    int32_t si[2] = {st, TOTK_SUB_FAST_FAULT(40) ? 0 : idx};
    memcpy(p, hdr, 8);
    memcpy(p + 8, si, 8);
    memcpy(p + 16, &addr, 8);
    memcpy(p + 24, &sz, 8);
}

#endif
