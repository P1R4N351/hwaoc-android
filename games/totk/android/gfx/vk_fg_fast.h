#ifndef VK_FG_FAST_H
#define VK_FG_FAST_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef T_AOC_W38_TEST_HOOKS
extern _Atomic int g_totk_fg_fast_state;
extern _Atomic int g_totk_w38_fault;
static inline void totk_fg_fast_reset_for_test(int forced) {
    atomic_store_explicit(&g_totk_fg_fast_state, forced ? 2 : 1, memory_order_release);
}
static inline void totk_w38_set_fault(int m) {
    atomic_store_explicit(&g_totk_w38_fault, m, memory_order_release);
}
static inline int totk_w38_fault(void) {
    return atomic_load_explicit(&g_totk_w38_fault, memory_order_acquire);
}
#else
static _Atomic int g_totk_fg_fast_state;
static inline int totk_w38_fault(void) { return 0; }
#endif

static inline int totk_fg_fast_on(void) {
    int s = atomic_load_explicit(&g_totk_fg_fast_state, memory_order_acquire);
    if (!s) {
        const char *e = getenv("TOTK_VK_FG_FAST");
        s = (e && e[0] == '0' && !e[1]) ? 1 : 2;
        atomic_store_explicit(&g_totk_fg_fast_state, s, memory_order_release);
    }
    return s == 2;
}

enum { TOTK_FG_FMT_CACHE_N = 64u, TOTK_FG_TERM_HT_N = 512u, TOTK_FG_TERM_HT_MASK = 511u };

typedef struct {
    _Atomic uint32_t state;
    _Atomic uintptr_t pd;
    _Atomic uint32_t fmt;
    _Atomic uint32_t feat;
} totk_fg_fmt_slot;

typedef struct {
    totk_fg_fmt_slot slots[TOTK_FG_FMT_CACHE_N];
} totk_fg_fmt_cache;

static inline uint32_t totk_fg_fmt_hash(uintptr_t pd, uint32_t fmt) {
    uint64_t x = ((uint64_t)(pd >> 3) * 11400714819323198485ull) ^
                 ((uint64_t)fmt * 11400714819323198485ull);
    return (uint32_t)((x ^ (x >> 32)) & (TOTK_FG_FMT_CACHE_N - 1u));
}

static inline int totk_fg_fmt_get(
    const totk_fg_fmt_cache *c, uintptr_t pd, uint32_t fmt, uint32_t *out) {
    if (!c || !pd || !out) return 0;
    uint32_t h = totk_fg_fmt_hash(pd, fmt);
    for (unsigned step = 0; step < 8u; step++) {
        const totk_fg_fmt_slot *s = &c->slots[(h + step) & (TOTK_FG_FMT_CACHE_N - 1u)];
        if (atomic_load_explicit(&s->state, memory_order_acquire) != 2u) continue;
        if (atomic_load_explicit(&s->pd, memory_order_relaxed) == pd &&
            atomic_load_explicit(&s->fmt, memory_order_relaxed) == fmt) {
            uint32_t f = atomic_load_explicit(&s->feat, memory_order_relaxed);
            if (totk_w38_fault() == 1) f ^= 1u;
            *out = f;
            return 1;
        }
    }
    return 0;
}

static inline void totk_fg_fmt_put(
    totk_fg_fmt_cache *c, uintptr_t pd, uint32_t fmt, uint32_t feat) {
    if (!c || !pd) return;
    uint32_t h = totk_fg_fmt_hash(pd, fmt);
    for (unsigned step = 0; step < 8u; step++) {
        totk_fg_fmt_slot *s = &c->slots[(h + step) & (TOTK_FG_FMT_CACHE_N - 1u)];
        uint32_t exp = 0u;
        if (atomic_compare_exchange_strong_explicit(
                &s->state, &exp, 1u, memory_order_acq_rel, memory_order_acquire)) {
            atomic_store_explicit(&s->pd, pd, memory_order_relaxed);
            atomic_store_explicit(&s->fmt, fmt, memory_order_relaxed);
            atomic_store_explicit(&s->feat, feat, memory_order_relaxed);
            atomic_store_explicit(&s->state, 2u, memory_order_release);
            return;
        }
        if (exp == 2u && atomic_load_explicit(&s->pd, memory_order_relaxed) == pd &&
            atomic_load_explicit(&s->fmt, memory_order_relaxed) == fmt) {
            return;
        }
    }
}

typedef struct {
    uint32_t gen;
    uint32_t stamp[TOTK_FG_TERM_HT_N];
    uint16_t slot[TOTK_FG_TERM_HT_N];
} totk_fg_term_ht;

static inline void totk_fg_term_ht_reset(totk_fg_term_ht *ht) {
    if (!ht) return;
    ht->gen++;
    if (!ht->gen) {
        memset(ht->stamp, 0, sizeof ht->stamp);
        ht->gen = 1u;
    }
}

static inline uint32_t totk_fg_term_hash(
    uint64_t buf, uint64_t off, uint64_t rng, uint64_t ser) {
    uint64_t x = buf * 11400714819323198485ull;
    x ^= (off + 0x9e3779b97f4a7c15ull) * 0xbf58476d1ce4e5b9ull;
    x ^= (rng ^ (ser << 7)) * 0x94d049bb133111ebull;
    return (uint32_t)((x ^ (x >> 29)) & TOTK_FG_TERM_HT_MASK);
}

static inline void totk_fg_term_ht_insert(
    totk_fg_term_ht *ht, uint32_t h, uint16_t copy_idx) {
    if (!ht) return;
    if (!ht->gen) ht->gen = 1u;
    for (unsigned step = 0; step < TOTK_FG_TERM_HT_N; step++) {
        uint32_t pos = (h + step) & TOTK_FG_TERM_HT_MASK;
        if (ht->stamp[pos] != ht->gen) {
            ht->stamp[pos] = ht->gen;
            ht->slot[pos] = copy_idx;
            return;
        }
    }
}

typedef struct {
    uint64_t epoch;
    int inited;
    uintptr_t img_view[11];
    uint32_t img_layout[11];
    uintptr_t buf_h[2];
    uint64_t buf_off[2];
    uint64_t buf_rng[2];
} totk_fg_render_slot;

static inline uint32_t totk_fg_desc_diff_mask(
    totk_fg_render_slot *st, uint64_t epoch,
    const uintptr_t views[11], const uint32_t layouts[11],
    const uintptr_t bufs[2], const uint64_t offs[2], const uint64_t rngs[2]) {
    if (!st) return 0x7ffu;
    int cold = (!st->inited || st->epoch != epoch);
    uint32_t mask = cold ? 0x7ffu : 0u;
    for (unsigned i = 0; i < 11u; i++) {
        if (i == 7u || i == 8u) {
            unsigned b = i - 7u;
            if (cold || st->buf_h[b] != bufs[b] || st->buf_off[b] != offs[b] ||
                st->buf_rng[b] != rngs[b]) {
                st->buf_h[b] = bufs[b];
                st->buf_off[b] = offs[b];
                st->buf_rng[b] = rngs[b];
                mask |= (1u << i);
            }
        } else if (cold || st->img_view[i] != views[i] || st->img_layout[i] != layouts[i]) {
            st->img_view[i] = views[i];
            st->img_layout[i] = layouts[i];
            mask |= (1u << i);
        }
    }
    st->epoch = epoch;
    st->inited = 1;
    if (totk_w38_fault() == 3) mask &= ~1u;
    return mask;
}

static inline unsigned totk_fg_term_flush_count(unsigned n_writes) {
    if (totk_w38_fault() == 4 && n_writes > 0u) return n_writes - 1u;
    return n_writes;
}

#endif
