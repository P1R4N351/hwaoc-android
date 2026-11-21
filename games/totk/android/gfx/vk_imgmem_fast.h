/* Fast 64 MiB Vulkan block suballocator & bit-transposed memory-type LUT (TOTK_VK_IMGMEM_FAST=1, default on; =0 disables). */
#ifndef TOTK_VK_IMGMEM_FAST_H
#define TOTK_VK_IMGMEM_FAST_H
#include <assert.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static _Atomic int g_totk_vk_imgmem_fast_state;

static inline int totk_vk_imgmem_fast_on(void) {
    int s = atomic_load_explicit(&g_totk_vk_imgmem_fast_state, memory_order_acquire);
    if (!s) {
        const char *e = getenv("TOTK_VK_IMGMEM_FAST");
        s = (e && *e == '0' && !e[1]) ? 1 : 2;
        atomic_store_explicit(&g_totk_vk_imgmem_fast_state, s, memory_order_release);
    }
    return s == 2;
}

#ifdef TOTK_W32_TEST_HOOKS
static _Atomic int g_totk_w32_fault;
static inline void totk_vk_imgmem_fast_reset_for_test(int forced) {
    atomic_store_explicit(&g_totk_vk_imgmem_fast_state, forced ? 2 : 1, memory_order_release);
}
static inline void totk_w32_set_fault(int mode) {
    atomic_store_explicit(&g_totk_w32_fault, mode, memory_order_release);
}
static inline int totk_w32_fault_mode(void) {
    return atomic_load_explicit(&g_totk_w32_fault, memory_order_acquire);
}
#else
static inline int totk_w32_fault_mode(void) { return 0; }
#endif

typedef struct {
    uint64_t off;
    uint64_t size;
} totk_mem_span;

typedef struct {
    uint32_t type;
    int nfree;
    int capfree;
    totk_mem_span *free;
    uint64_t used;
    uint64_t max_free;
    uint8_t max_valid;
} totk_mem_block;

static inline uint64_t totk_mem_align_up(uint64_t off, uint64_t align, int fault_mode) {
    if (fault_mode == 112) return off;
    if (align <= 1u) return off;
    if ((align & (align - 1u)) == 0u) return (off + align - 1u) & ~(align - 1u);
    return ((off + align - 1u) / align) * align;
}

static inline int totk_imgmem_ensure_room(totk_mem_block *b, int need) {
    if (need <= b->capfree) return 1;
    int cap = b->capfree ? b->capfree : 16;
    for (int step = 0; step < 30 && cap < need; step++) cap *= 2;
    if (cap < need) return 0;
    totk_mem_span *p = (totk_mem_span *)realloc(b->free, (size_t)cap * sizeof *p);
    if (!p) return 0;
    b->free = p;
    b->capfree = cap;
    return 1;
}

static inline int totk_imgmem_carve_span(totk_mem_block *b, int i, uint64_t off, uint64_t size,
                                         uint64_t at, uint64_t req_size, int fault_mode) {
    uint64_t front = at - off;
    uint64_t back = off + size - at - req_size;
    if (front && back) {
        if (!totk_imgmem_ensure_room(b, b->nfree + 1)) return 0;
        if (b->nfree - i - 1 > 0) memmove(&b->free[i + 2], &b->free[i + 1], (size_t)(b->nfree - i - 1) * sizeof *b->free);
        b->nfree++;
        b->free[i] = (totk_mem_span){off, front};
        b->free[i + 1] = (totk_mem_span){at + req_size, back};
    } else if (front) {
        b->free[i].size = front;
    } else if (back) {
        b->free[i].off = at + req_size;
        b->free[i].size = back;
    } else {
        if (b->nfree - i - 1 > 0) memmove(&b->free[i], &b->free[i + 1], (size_t)(b->nfree - i - 1) * sizeof *b->free);
        b->nfree--;
    }
    b->used += req_size;
    uint64_t rem = front > back ? front : back;
    if (b->nfree == 0) {
        b->max_free = 0u;
        b->max_valid = 1u;
    } else if (b->nfree == 1) {
        b->max_free = b->free[0].size;
        b->max_valid = 1u;
    } else if (b->max_valid && size == b->max_free && rem < size) {
        b->max_valid = 0u;
    }
    if (fault_mode == 113 && b->nfree > 0) {
        b->max_free = 0u;
        b->max_valid = 1u;
    }
    return 1;
}

static inline int totk_imgmem_scan_span(const totk_mem_block *b, uint64_t req_size, uint64_t align,
                                        int *out_idx, uint64_t *out_at, uint64_t *out_mx, int fault_mode) {
    int i = 0, n = b->nfree;
    uint64_t mx = 0u;
    if (b->max_valid) {
        for (; i + 4 <= n; i += 4) {
            const totk_mem_span *f = &b->free[i];
            if ((f[0].size < req_size) & (f[1].size < req_size) & (f[2].size < req_size) & (f[3].size < req_size)) continue;
            for (int j = 0; j < 4; j++) {
                if (f[j].size < req_size) continue;
                uint64_t at = totk_mem_align_up(f[j].off, align, fault_mode);
                if (at + req_size <= f[j].off + f[j].size) { *out_idx = i + j; *out_at = at; return 1; }
            }
        }
    }
    for (; i < n; i++) {
        uint64_t foff = b->free[i].off, fsz = b->free[i].size;
        if (fsz > mx) mx = fsz;
        if (fsz < req_size) continue;
        uint64_t at = totk_mem_align_up(foff, align, fault_mode);
        if (at + req_size <= foff + fsz) { *out_idx = i; *out_at = at; return 1; }
    }
    *out_mx = mx;
    return 0;
}

static inline int totk_imgmem_block_alloc(totk_mem_block *b, uint64_t req_size, uint64_t align,
                                          uint64_t block_bytes, uint64_t *out_at, int fault_mode) {
    if (!b || !req_size || req_size > block_bytes - b->used) return 0;
    if (b->max_valid && req_size > b->max_free) return 0;
    int idx = -1;
    uint64_t at = 0u, mx = 0u;
    if (!totk_imgmem_scan_span(b, req_size, align, &idx, &at, &mx, fault_mode)) {
        if (!b->max_valid) { b->max_free = mx; b->max_valid = 1u; }
        return 0;
    }
    if (!totk_imgmem_carve_span(b, idx, b->free[idx].off, b->free[idx].size, at, req_size, fault_mode)) return 0;
    *out_at = at;
    return 1;
}

static inline int totk_imgmem_pool_alloc(totk_mem_block *blocks, int nblocks, const uint64_t type_mask[32],
                                         uint32_t type, uint64_t req_size, uint64_t align,
                                         uint64_t block_bytes, int *out_bi, uint64_t *out_at, int fault_mode) {
    if (type < 32u && type_mask) {
        uint64_t m = type_mask[type];
        if (fault_mode == 115 && (m & (m - 1u)) != 0u) m &= m - 1u;
        for (int step = 0; step < 64 && m; step++) {
            int bi = __builtin_ctzll(m);
            m &= m - 1u;
            if (bi < nblocks && totk_imgmem_block_alloc(&blocks[bi], req_size, align, block_bytes, out_at, fault_mode)) {
                *out_bi = bi;
                return 1;
            }
        }
    }
    int start = type < 32u && type_mask ? 64 : 0;
    for (int bi = start; bi < nblocks; bi++) {
        if (blocks[bi].type != type) continue;
        if (totk_imgmem_block_alloc(&blocks[bi], req_size, align, block_bytes, out_at, fault_mode)) {
            *out_bi = bi;
            return 1;
        }
    }
    return 0;
}

static inline int totk_imgmem_block_free(totk_mem_block *b, uint64_t off, uint64_t size, int fault_mode) {
    assert(b != NULL);
    int lo = 0, hi = b->nfree;
    for (int step = 0; step < 32 && lo < hi; step++) {
        int m = lo + (hi - lo) / 2;
        if (b->free[m].off < off) lo = m + 1;
        else hi = m;
    }
    int left = lo > 0 && b->free[lo - 1].off + b->free[lo - 1].size == off;
    int right = lo < b->nfree && off + size == b->free[lo].off;
    if (fault_mode == 114) right = 0;
    uint64_t merged = size;
    if (left && right) {
        merged = b->free[lo - 1].size + size + b->free[lo].size;
        b->free[lo - 1].size = merged;
        if (b->nfree - lo - 1 > 0) memmove(&b->free[lo], &b->free[lo + 1], (size_t)(b->nfree - lo - 1) * sizeof *b->free);
        b->nfree--;
    } else if (left) {
        merged = b->free[lo - 1].size + size;
        b->free[lo - 1].size = merged;
    } else if (right) {
        merged = b->free[lo].size + size;
        b->free[lo].off = off;
        b->free[lo].size = merged;
    } else {
        if (!totk_imgmem_ensure_room(b, b->nfree + 1)) return 0;
        if (b->nfree - lo > 0) memmove(&b->free[lo + 1], &b->free[lo], (size_t)(b->nfree - lo) * sizeof *b->free);
        b->free[lo] = (totk_mem_span){off, size};
        b->nfree++;
    }
    if (b->nfree == 1) {
        b->max_free = merged;
        b->max_valid = 1u;
    } else if (b->max_valid && merged > b->max_free) {
        b->max_free = merged;
    }
    return 1;
}

typedef struct {
    uint32_t count;
    uint32_t valid_mask;
    uint32_t prop_mask[6];
    uint32_t flags[32];
    int ready;
} totk_memtype_lut;

static inline void totk_memtype_lut_build(totk_memtype_lut *lut, uint32_t count, const uint32_t *flags_arr) {
    assert(lut != NULL);
    memset(lut, 0, sizeof *lut);
    if (!flags_arr || count == 0u || count > 32u) return;
    lut->count = count;
    lut->valid_mask = count == 32u ? UINT32_MAX : ((UINT32_C(1) << count) - 1u);
    for (uint32_t i = 0u; i < count; i++) {
        uint32_t fl = flags_arr[i];
        lut->flags[i] = fl;
        for (int b = 0; b < 6; b++) {
            if (fl & (1u << b)) lut->prop_mask[b] |= (1u << i);
        }
    }
    lut->ready = 1;
}

static inline int totk_memtype_pick_fast(const totk_memtype_lut *lut, uint32_t bits, uint32_t want, int fault_mode) {
    if (!lut || !lut->ready) return -1;
    uint32_t m = bits & lut->valid_mask;
    for (int b = 0; b < 6; b++) {
        if (want & (1u << b)) m &= lut->prop_mask[b];
    }
    uint32_t hi = want & ~0x3fu;
    if (hi && m) {
        uint32_t cand = m;
        m = 0u;
        for (int step = 0; step < 32 && cand; step++) {
            int i = __builtin_ctz(cand);
            cand &= cand - 1u;
            if ((lut->flags[i] & hi) == hi) m |= (1u << i);
        }
    }
    if (!m) return -1;
    if (fault_mode == 116 && (m & (m - 1u)) != 0u) return (int)(31u - (uint32_t)__builtin_clz(m));
    return __builtin_ctz(m);
}
#endif
