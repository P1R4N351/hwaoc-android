#ifndef TOTK_ROMFS_HANDLES_H
#define TOTK_ROMFS_HANDLES_H
#include <stdatomic.h>
#include <stdint.h>

enum { ROMFS_NH = 65536u };

typedef struct {
    _Atomic uint64_t seq;        /* even = closed, odd = open; high bits carry monotonic cycle */
    _Atomic uint64_t file_plus1; /* entry index + 1; 0 when closed */
    _Atomic uint32_t ovl;        /* overlay slot + 1; 0 = base RomFS image */
    _Atomic int fd;              /* overlay file descriptor when unmapped, else -1 */
} romfs_hslot;

typedef struct {
    romfs_hslot slots[ROMFS_NH];
    uint16_t free_ring[ROMFS_NH];
    uint32_t free_rd, free_wr;
    int free_inited;
} romfs_htable;

static inline uint64_t romfs_h_encode(uint32_t idx, uint64_t cycle) {
    return UINT64_C(0x100000000) | ((cycle & 0xffffull) << 16) | (uint64_t)(idx & 0xffffu);
}

static inline int romfs_h_valid_bits(uint64_t h) {
    return (h >> 32) == 1u && (h & 0xffffu) != 0u;
}

static inline void romfs_htable_init_locked(romfs_htable *t) {
    if (t->free_inited) return;
    for (uint32_t j = 1u; j < ROMFS_NH; j++) t->free_ring[j - 1u] = (uint16_t)j;
    t->free_rd = 0u;
    t->free_wr = ROMFS_NH - 1u;
    t->free_inited = 1;
}

static inline uint64_t romfs_htable_alloc_locked(romfs_htable *t, uint64_t file_plus1, uint32_t ovl, int fd) {
    romfs_htable_init_locked(t);
    if (t->free_rd == t->free_wr) return 0u;
    uint32_t idx = t->free_ring[t->free_rd++ & (ROMFS_NH - 1u)];
    romfs_hslot *s = &t->slots[idx];
    uint64_t closed_seq = atomic_load_explicit(&s->seq, memory_order_relaxed);
    uint64_t cycle = closed_seq >> 1;
    atomic_store_explicit(&s->file_plus1, file_plus1, memory_order_relaxed);
    atomic_store_explicit(&s->ovl, ovl, memory_order_relaxed);
    atomic_store_explicit(&s->fd, fd, memory_order_relaxed);
    atomic_store_explicit(&s->seq, (cycle << 1) | 1ull, memory_order_release);
    return romfs_h_encode(idx, cycle);
}

static inline int romfs_htable_resolve_fast(const romfs_htable *t, uint64_t h, uint64_t *file_plus1, uint32_t *ovl, int *fd) {
    if (!romfs_h_valid_bits(h)) return 0;
    uint32_t idx = (uint32_t)(h & 0xffffu);
    uint16_t hgen = (uint16_t)((h >> 16) & 0xffffu);
    const romfs_hslot *s = &t->slots[idx];
    for (int attempt = 0; attempt < 4; attempt++) {
        uint64_t s1 = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (!(s1 & 1ull)) return 0;
        if ((uint16_t)((s1 >> 1) & 0xffffu) != hgen) return 0;
        uint64_t fp1 = atomic_load_explicit(&s->file_plus1, memory_order_relaxed);
        uint32_t ov = atomic_load_explicit(&s->ovl, memory_order_relaxed);
        int ofd = atomic_load_explicit(&s->fd, memory_order_relaxed);
        atomic_thread_fence(memory_order_acquire);
        uint64_t s2 = atomic_load_explicit(&s->seq, memory_order_relaxed);
        if (s1 == s2) {
            if (!fp1) return 0;
            *file_plus1 = fp1;
            if (ovl) *ovl = ov;
            if (fd) *fd = ofd;
            return 1;
        }
    }
    return 0;
}

static inline int romfs_htable_resolve_locked(const romfs_htable *t, uint64_t h, uint64_t *file_plus1, uint32_t *ovl, int *fd) {
    if (!romfs_h_valid_bits(h)) return 0;
    uint32_t idx = (uint32_t)(h & 0xffffu);
    uint16_t hgen = (uint16_t)((h >> 16) & 0xffffu);
    const romfs_hslot *s = &t->slots[idx];
    uint64_t seq = atomic_load_explicit(&s->seq, memory_order_relaxed);
    if (!(seq & 1ull) || (uint16_t)((seq >> 1) & 0xffffu) != hgen) return 0;
    uint64_t fp1 = atomic_load_explicit(&s->file_plus1, memory_order_relaxed);
    if (!fp1) return 0;
    *file_plus1 = fp1;
    if (ovl) *ovl = atomic_load_explicit(&s->ovl, memory_order_relaxed);
    if (fd) *fd = atomic_load_explicit(&s->fd, memory_order_relaxed);
    return 1;
}

static inline int romfs_htable_close_locked(romfs_htable *t, uint64_t h, int *fd_out) {
    if (!t->free_inited || !romfs_h_valid_bits(h)) return 0;
    uint32_t idx = (uint32_t)(h & 0xffffu);
    uint16_t hgen = (uint16_t)((h >> 16) & 0xffffu);
    romfs_hslot *s = &t->slots[idx];
    uint64_t seq = atomic_load_explicit(&s->seq, memory_order_relaxed);
    if (!(seq & 1ull) || (uint16_t)((seq >> 1) & 0xffffu) != hgen) return 0;
    uint32_t ov = atomic_load_explicit(&s->ovl, memory_order_relaxed);
    int ofd = atomic_load_explicit(&s->fd, memory_order_relaxed);
    if (fd_out) *fd_out = ov ? ofd : -1;
    uint64_t next_cycle = (seq >> 1) + 1ull;
    atomic_store_explicit(&s->seq, next_cycle << 1, memory_order_release);
    atomic_store_explicit(&s->file_plus1, 0u, memory_order_relaxed);
    atomic_store_explicit(&s->ovl, 0u, memory_order_relaxed);
    atomic_store_explicit(&s->fd, -1, memory_order_relaxed);
    if (t->free_wr - t->free_rd < ROMFS_NH - 1u) t->free_ring[t->free_wr++ & (ROMFS_NH - 1u)] = (uint16_t)idx;
    return 1;
}
#endif
