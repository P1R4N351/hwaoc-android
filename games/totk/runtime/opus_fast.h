#ifndef TOTK_OPUS_FAST_H
#define TOTK_OPUS_FAST_H
/* 64-way sharded hash helper and lock-free C11 atomic seqlock 64x4 format cache
   for nn::codec hardware Opus decoders (TOTK_OPUS_FAST=1). */
#include <stdatomic.h>
#include <stdint.h>

enum { OPUS_BUCKETS = 64, OPUS_FMT_WAYS = 4 };

static inline unsigned opus_ptr_bucket(const void *guest) {
    uint64_t x = (uint64_t)(uintptr_t)guest >> 3;
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return (unsigned)((x ^ (x >> 31)) & (OPUS_BUCKETS - 1));
}

typedef struct {
    uintptr_t guest;
    int rate;
    int channels;
} opus_fmt_slot;

typedef struct {
    _Atomic uint32_t seq;
    uint8_t hand;
    opus_fmt_slot ways[OPUS_FMT_WAYS];
} opus_fmt_set;

typedef struct {
    opus_fmt_set sets[OPUS_BUCKETS];
} opus_fmt_cache;

static inline int opus_fmt_get(const opus_fmt_cache *c, const void *guest, int *rate_out, int *ch_out) {
    if (!c || !guest) return 0;
    const opus_fmt_set *s = &c->sets[opus_ptr_bucket(guest)];
    uintptr_t key = (uintptr_t)guest;
    for (int tries = 0; tries < 4; tries++) {
        uint32_t seq0 = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (seq0 & 1u) continue;
        int hit = 0, rate = 0, ch = 0;
        for (int w = 0; w < OPUS_FMT_WAYS; w++) {
            if (s->ways[w].guest == key) { hit = 1; rate = s->ways[w].rate; ch = s->ways[w].channels; break; }
        }
        atomic_thread_fence(memory_order_acquire);
        uint32_t seq1 = atomic_load_explicit(&s->seq, memory_order_relaxed);
        if (seq0 == seq1) {
            if (!hit) return 0;
            if (rate_out) *rate_out = rate;
            if (ch_out) *ch_out = ch;
            return 1;
        }
    }
    return 0;
}

/* Caller holds g_buckets[opus_ptr_bucket(guest)].mu so updates to this set are serialized. */
static inline void opus_fmt_put(opus_fmt_cache *c, const void *guest, int rate, int channels) {
    if (!c || !guest) return;
    opus_fmt_set *s = &c->sets[opus_ptr_bucket(guest)];
    uintptr_t key = (uintptr_t)guest;
    uint32_t seq = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_store_explicit(&s->seq, seq + 1u, memory_order_release);
    int slot = OPUS_FMT_WAYS;
    for (int w = 0; w < OPUS_FMT_WAYS; w++) {
        if (s->ways[w].guest == key || !s->ways[w].guest) { slot = w; break; }
    }
    if (slot >= OPUS_FMT_WAYS) slot = (int)(s->hand++ & (OPUS_FMT_WAYS - 1));
    s->ways[slot] = (opus_fmt_slot){key, rate, channels};
    atomic_store_explicit(&s->seq, seq + 2u, memory_order_release);
}

/* Caller holds g_buckets[opus_ptr_bucket(guest)].mu. */
static inline void opus_fmt_del(opus_fmt_cache *c, const void *guest) {
    if (!c || !guest) return;
    opus_fmt_set *s = &c->sets[opus_ptr_bucket(guest)];
    uintptr_t key = (uintptr_t)guest;
    for (int w = 0; w < OPUS_FMT_WAYS; w++) {
        if (s->ways[w].guest == key) {
            uint32_t seq = atomic_load_explicit(&s->seq, memory_order_relaxed);
            atomic_store_explicit(&s->seq, seq + 1u, memory_order_release);
            s->ways[w] = (opus_fmt_slot){0, 0, 0};
            atomic_store_explicit(&s->seq, seq + 2u, memory_order_release);
            return;
        }
    }
}

#endif
