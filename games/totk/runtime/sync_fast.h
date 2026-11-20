/* O(1) Horizon kernel synchronization fast paths for AoC/TotK (TOTK_SYNC_FAST=1):
 *   1) 64-word bitmap allocator for MAX_H=4096 handle table (preserves lowest-free-slot semantics)
 *   2) 64-bucket hash index + priority/FIFO comparison helper for wait queues
 *   3) 64-bit handle-object Bloom filter for full 64-handle WaitSynchronization selective wakes
 *   4) 64x4 set-associative front-index for SystemEvent (ev -> wh)
 * 0 GAME_* branches. */
#ifndef TOTK_SYNC_FAST_H
#define TOTK_SYNC_FAST_H

#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef W7_TEST_HOOKS
extern int g_sync_fault;
#define TOTK_SYNC_FAULT(n) (g_sync_fault == (n))
#else
#define TOTK_SYNC_FAULT(n) 0
#endif

enum { SYNC_HMAP_WORDS = 64, SYNC_WB_BUCKETS = 64, SYNC_SE_SETS = 64, SYNC_SE_WAYS = 4 };

typedef struct {
    uint64_t used[SYNC_HMAP_WORDS];
} sync_hmap;

typedef struct {
    void *ev;
    uint32_t wh;
    uint32_t pad;
} sync_se_slot;

typedef struct {
    sync_se_slot way[SYNC_SE_WAYS];
} sync_se_set;

typedef struct {
    sync_se_set set[SYNC_SE_SETS];
    uint32_t overflow;
} sync_se_idx;

static inline unsigned sync_hmap_alloc(sync_hmap *m) {
    assert(m != NULL);
    m->used[0] |= 1ull;
    for (unsigned w = 0; w < SYNC_HMAP_WORDS; w++) {
        uint64_t free_bits = ~m->used[w];
        if (!free_bits) continue;
        unsigned bit = (unsigned)__builtin_ctzll(free_bits);
        if (!TOTK_SYNC_FAULT(1)) m->used[w] |= 1ull << bit;
        return (w << 6) | bit;
    }
    return 0;
}

static inline void sync_hmap_mark(sync_hmap *m, unsigned idx) {
    assert(m != NULL && idx > 0 && idx < 4096u);
    m->used[0] |= 1ull;
    m->used[idx >> 6] |= 1ull << (idx & 63u);
}

static inline void sync_hmap_free(sync_hmap *m, unsigned idx) {
    assert(m != NULL && idx < 4096u);
    if (!idx) return;
    m->used[idx >> 6] &= ~(1ull << (idx & 63u));
}

static inline int sync_hmap_count(const sync_hmap *m) {
    assert(m != NULL);
    int n = 0;
    for (unsigned w = 0; w < SYNC_HMAP_WORDS; w++) {
        uint64_t bits = w ? m->used[w] : (m->used[0] & ~1ull);
        n += __builtin_popcountll(bits);
    }
    return n;
}

static inline unsigned sync_wb_hash(int kind, uint64_t addr) {
    uint64_t x = (addr >> 2) ^ ((uint64_t)(unsigned)kind * 0x9E3779B97F4A7C15ull);
    return (unsigned)(((x * 0xBF58476D1CE4E5B9ull) >> 58) & (SYNC_WB_BUCKETS - 1u));
}

static inline int sync_wb_prio_better(int prio_a, uint64_t seq_a, int prio_b, uint64_t seq_b) {
    if (TOTK_SYNC_FAULT(2)) return prio_a > prio_b || (prio_a == prio_b && seq_a > seq_b);
    return prio_a < prio_b || (prio_a == prio_b && seq_a < seq_b);
}

static inline uint64_t sync_obj_bloom_ins(const void *obj) {
    uint64_t b = (((uintptr_t)obj >> 4) * 0x9E3779B97F4A7C15ull) >> 58;
    return 1ull << (b & 63u);
}

static inline uint64_t sync_obj_bloom_qry(const void *obj) {
    uint64_t s = TOTK_SYNC_FAULT(3) ? 57u : 58u;
    uint64_t b = (((uintptr_t)obj >> 4) * 0x9E3779B97F4A7C15ull) >> s;
    return 1ull << (b & 63u);
}

static inline unsigned sync_se_bucket(const void *ev) {
    uint64_t x = ((uintptr_t)ev >> 3) * 0x9E3779B97F4A7C15ull;
    return (unsigned)((x >> 58) & (SYNC_SE_SETS - 1u));
}

static inline void sync_se_put(sync_se_idx *ix, void *ev, uint32_t wh) {
    assert(ix != NULL && ev != NULL && wh != 0);
    sync_se_set *s = &ix->set[sync_se_bucket(ev)];
    for (unsigned w = 0; w < SYNC_SE_WAYS; w++) {
        if (s->way[w].ev == ev || !s->way[w].ev) {
            s->way[w] = (sync_se_slot){ev, wh, 0};
            return;
        }
    }
    ix->overflow++;
}

static inline void sync_se_del_wh(sync_se_idx *ix, uint32_t wh) {
    assert(ix != NULL);
    if (!wh || TOTK_SYNC_FAULT(4)) return;
    for (unsigned b = 0; b < SYNC_SE_SETS; b++) {
        for (unsigned w = 0; w < SYNC_SE_WAYS; w++) {
            if (ix->set[b].way[w].wh == wh) memset(&ix->set[b].way[w], 0, sizeof ix->set[b].way[w]);
        }
    }
}

static inline uint32_t sync_se_find_ev(const sync_se_idx *ix, const void *ev) {
    assert(ix != NULL && ev != NULL);
    const sync_se_set *s = &ix->set[sync_se_bucket(ev)];
    for (unsigned w = 0; w < SYNC_SE_WAYS; w++) {
        if (s->way[w].ev == ev) return s->way[w].wh;
    }
    return 0;
}

#endif
