/* Fast movie-decoder lookup, idle present-tick short-circuit & slot-mask helpers (TOTK_MOVIE_FAST=1, default on; =0 disables).
   Pure ISO C11 (zero GAME_* conditionals) so both libtotk.so and host ktglopttest execute exact helpers. */
#pragma once
#include <assert.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

enum { TOTK_MOVIE_MAX = 16, TOTK_MOVIE_OUT_BUFS = 8 };

static _Atomic int g_totk_movie_fast_state;

static inline int totk_movie_fast_on(void) {
    int s = atomic_load_explicit(&g_totk_movie_fast_state, memory_order_acquire);
    if (!s) {
        const char *e = getenv("TOTK_MOVIE_FAST");
        s = (e && *e == '0' && !e[1]) ? 1 : 2;
        atomic_store_explicit(&g_totk_movie_fast_state, s, memory_order_release);
    }
    return s == 2;
}

#ifdef TOTK_W31_TEST_HOOKS
static _Atomic int g_totk_w31_fault;
static inline void totk_movie_fast_reset_for_test(int forced, int fault) {
    atomic_store_explicit(&g_totk_movie_fast_state, forced ? 2 : 1, memory_order_release);
    atomic_store_explicit(&g_totk_w31_fault, fault, memory_order_release);
}
static inline int totk_w31_fault_mode(void) {
    return atomic_load_explicit(&g_totk_w31_fault, memory_order_acquire);
}
#else
static inline int totk_w31_fault_mode(void) { return 0; }
#endif

typedef struct {
    _Atomic uint32_t seq;
    _Atomic uint32_t active_mask;
    _Atomic int ready;
    _Atomic uintptr_t guests[TOTK_MOVIE_MAX];
    _Atomic uintptr_t movies[TOTK_MOVIE_MAX];
} totk_movie_table;

static inline void totk_movie_mark_ready(totk_movie_table *t) {
    assert(t != NULL);
    atomic_store_explicit(&t->ready, 1, memory_order_release);
}

static inline int totk_movie_is_ready(const totk_movie_table *t) {
    assert(t != NULL);
    return atomic_load_explicit(&t->ready, memory_order_acquire) != 0;
}

static inline int totk_movie_has_live(const totk_movie_table *t) {
    assert(t != NULL);
    if (totk_w31_fault_mode() == 107) return 0;
    return atomic_load_explicit(&t->active_mask, memory_order_acquire) != 0u;
}

static inline void totk_movie_slot_set(totk_movie_table *t, int idx, const void *guest, const void *movie) {
    assert(t != NULL && idx >= 0 && idx < TOTK_MOVIE_MAX);
    uint32_t s = atomic_load_explicit(&t->seq, memory_order_relaxed);
    atomic_store_explicit(&t->seq, s + 1u, memory_order_release);
    atomic_store_explicit(&t->guests[idx], (uintptr_t)guest, memory_order_release);
    atomic_store_explicit(&t->movies[idx], (uintptr_t)movie, memory_order_release);
    uint32_t bit = UINT32_C(1) << idx;
    uint32_t am = atomic_load_explicit(&t->active_mask, memory_order_relaxed);
    am = movie ? (am | bit) : (am & ~bit);
    atomic_store_explicit(&t->active_mask, am, memory_order_release);
    atomic_store_explicit(&t->seq, s + 2u, memory_order_release);
}

static inline void totk_movie_slot_clear(totk_movie_table *t, int idx) {
    totk_movie_slot_set(t, idx, NULL, NULL);
}

static inline int totk_movie_find_fast(const totk_movie_table *t, const void *guest, void **out_movie) {
    assert(t != NULL && out_movie != NULL);
    if (!guest) { *out_movie = NULL; return 1; }
    int fmode = totk_w31_fault_mode();
    uintptr_t key = (uintptr_t)guest;
    for (int attempt = 0; attempt < 64; attempt++) {
        uint32_t s0 = atomic_load_explicit(&t->seq, memory_order_acquire);
        if (s0 & 1u) continue;
        uint32_t m = atomic_load_explicit(&t->active_mask, memory_order_acquire);
        uintptr_t found = 0u;
        for (int step = 0; step < TOTK_MOVIE_MAX && m; step++) {
            int i = __builtin_ctz(m);
            m &= m - 1u;
            if (atomic_load_explicit(&t->guests[i], memory_order_acquire) == key) {
                found = atomic_load_explicit(&t->movies[i], memory_order_acquire);
                break;
            }
        }
        if (s0 == atomic_load_explicit(&t->seq, memory_order_acquire)) {
            if (fmode == 108 && found) found = 0u;
            *out_movie = (void *)found;
            return 1;
        }
    }
    return 0;
}

static inline int totk_movie_free_count(uint32_t held_mask) {
    uint32_t m = totk_w31_fault_mode() == 109 ? 0xffu : (held_mask & 0xffu);
    return TOTK_MOVIE_OUT_BUFS - __builtin_popcount(m);
}

static inline int totk_movie_alloc_slot(uint32_t held_mask) {
    uint32_t m = totk_w31_fault_mode() == 110 ? (held_mask | 1u) : held_mask;
    uint32_t free_bits = (~m) & ((1u << TOTK_MOVIE_OUT_BUFS) - 1u);
    return free_bits ? __builtin_ctz(free_bits) : -1;
}
