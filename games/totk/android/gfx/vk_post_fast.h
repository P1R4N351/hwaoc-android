/* Fast post-processing descriptor reuse, lockless present-ready ring & CV wake elision (TOTK_VK_POST_FAST=1). */
#pragma once
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    TOTK_POST_EDGE = 0,
    TOTK_POST_WEIGHTS = 1,
    TOTK_POST_BLEND = 2,
    TOTK_POST_EASU = 3,
    TOTK_POST_RCAS = 4,
    TOTK_POST_SGSR = 5
};

static _Atomic int g_totk_vk_post_fast_state;

static inline int totk_vk_post_fast_on(void) {
    int s = atomic_load_explicit(&g_totk_vk_post_fast_state, memory_order_acquire);
    if (!s) {
        const char *e = getenv("TOTK_VK_POST_FAST");
        s = (e && *e == '0' && !e[1]) ? 1 : 2;
        atomic_store_explicit(&g_totk_vk_post_fast_state, s, memory_order_release);
    }
    return s == 2;
}

#ifdef TOTK_W36_TEST_HOOKS
static _Atomic int g_totk_w36_fault;
static inline void totk_vk_post_fast_reset(int forced) {
    int v = (forced < 0) ? 0 : (forced ? 2 : 1);
    atomic_store_explicit(&g_totk_vk_post_fast_state, v, memory_order_release);
}
static inline void totk_w36_set_fault(int mode) {
    atomic_store_explicit(&g_totk_w36_fault, mode, memory_order_release);
}
static inline int totk_w36_fault(void) {
    return atomic_load_explicit(&g_totk_w36_fault, memory_order_acquire);
}
#else
static inline int totk_w36_fault(void) { return 0; }
#endif

typedef struct {
    uint64_t aux_rev;
    uint64_t lut_rev;
    uint64_t src_rev;
    uint8_t inited;
} totk_post_dset_state;

static inline unsigned totk_post_dset_mask(
    totk_post_dset_state *st, unsigned stage, uint64_t src_rev, uint64_t aux_rev, uint64_t lut_rev, int fault_mode) {
    if (!st) return 0xFu;
    if (!st->inited) {
        st->inited = 1u;
        st->src_rev = src_rev;
        st->aux_rev = aux_rev;
        st->lut_rev = lut_rev;
        return fault_mode == 132 ? 0u : 0xFu;
    }
    if (stage == TOTK_POST_EDGE) {
        unsigned m = 0x3u;
        if (!lut_rev || st->lut_rev != lut_rev) { m |= 0xCu; st->lut_rev = lut_rev; }
        return fault_mode == 132 ? 0u : m;
    }
    if (stage == TOTK_POST_WEIGHTS || stage == TOTK_POST_BLEND) {
        unsigned m = 0x1u;
        if (!aux_rev || st->aux_rev != aux_rev) { m |= 0x2u; st->aux_rev = aux_rev; }
        if (!lut_rev || st->lut_rev != lut_rev) { m |= 0xCu; st->lut_rev = lut_rev; }
        return fault_mode == 132 ? 0u : m;
    }
    if (stage == TOTK_POST_RCAS || stage == TOTK_POST_EASU || stage == TOTK_POST_SGSR) {
        if (src_rev && st->src_rev == src_rev) return 0u;
        st->src_rev = src_rev;
        if (fault_mode == 132 || (!src_rev && fault_mode == 133)) return 0u;
        return 0xFu;
    }
    return fault_mode == 132 ? 0u : 0xFu;
}

enum { TOTK_PRESENT_READY_RING = 1024 };

typedef struct {
    _Atomic uint32_t seq;
    uint32_t pad;
    _Atomic uint64_t ticket;
    _Atomic uint64_t ready_ns;
} totk_present_ready_slot;

typedef struct {
    totk_present_ready_slot s[TOTK_PRESENT_READY_RING];
} totk_present_ready_ring;

static inline void totk_present_ready_note(totk_present_ready_ring *r, uint64_t ticket, uint64_t ready_ns) {
    if (!r || !ticket) return;
    totk_present_ready_slot *sl = &r->s[ticket % TOTK_PRESENT_READY_RING];
    uint32_t seq = atomic_load_explicit(&sl->seq, memory_order_relaxed);
    (void)atomic_exchange_explicit(&sl->seq, seq + 1u, memory_order_acq_rel);
    atomic_store_explicit(&sl->ticket, ticket, memory_order_release);
    atomic_store_explicit(&sl->ready_ns, ready_ns, memory_order_release);
    atomic_store_explicit(&sl->seq, seq + 2u, memory_order_release);
}

static inline uint64_t totk_present_ready_read(const totk_present_ready_ring *r, uint64_t ticket, int fault_mode) {
    if (!r || !ticket) return 0u;
    const totk_present_ready_slot *sl = &r->s[ticket % TOTK_PRESENT_READY_RING];
    for (unsigned it = 0u; it < 64u; it++) {
        uint32_t s0 = atomic_load_explicit(&sl->seq, memory_order_acquire);
        if (s0 & 1u) continue;
        uint64_t rt = atomic_load_explicit(&sl->ticket, memory_order_acquire);
        uint64_t rn = atomic_load_explicit(&sl->ready_ns, memory_order_acquire);
        uint32_t s1 = atomic_load_explicit(&sl->seq, memory_order_relaxed);
        if (s0 == s1) {
            if (rt != ticket && fault_mode != 135) return 0u;
            return fault_mode == 134 ? (rn ^ 1u) : rn;
        }
    }
    return 0u;
}

static inline int totk_cv_should_wake(uint32_t waiters, int fast, int fault_mode) {
    if (fault_mode == 136) return 0;
    return !fast || waiters > 0u;
}
