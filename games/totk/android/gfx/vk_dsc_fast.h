#ifndef TOTK_VK_DSC_FAST_H
#define TOTK_VK_DSC_FAST_H
/* Wave 8 Part B: 64x4 driver-block ring & 256x4 descriptor-set victim cache for vk_draw.c (TOTK_VK_DSC_FAST=1). */
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TOTK_KTGL_OPT_TEST
extern int g_fault_mode;
#define TOTK_DSC_FAST_FAULT(n) (g_fault_mode == (n))
#else
#define TOTK_DSC_FAST_FAULT(n) 0
#endif

enum { TOTK_GEN_SETS = 64, TOTK_BLK_SETS = 64, TOTK_DSC_VIC_SETS = 256, TOTK_DSC_WAYS = 4 };

typedef struct {
    uint64_t sub, rev, buf, off, range;
    uint32_t key, w, h, g0, g1, g2;
} totk_gen_ent;

typedef struct {
    totk_gen_ent e[TOTK_DSC_WAYS];
    uint32_t rr, pad;
} totk_gen_set;

typedef struct {
    totk_gen_set s[TOTK_GEN_SETS];
} totk_gen_memo4;

typedef struct {
    uint64_t sub, h0, h1, buf, off, range;
    size_t n;
    uint8_t *bytes;
    int32_t key, pad;
} totk_blk_ent;

typedef struct {
    totk_blk_ent e[TOTK_DSC_WAYS];
    uint32_t rr, pad;
} totk_blk_set;

typedef struct {
    totk_blk_set s[TOTK_BLK_SETS];
} totk_blk_memo4;

typedef struct {
    uint64_t sub, h0, h1, dsl, set;
    size_t n;
} totk_dsc_vic_ent;

typedef struct {
    totk_dsc_vic_ent e[TOTK_DSC_WAYS];
    uint32_t rr, pad;
} totk_dsc_vic_set;

typedef struct {
    totk_dsc_vic_set s[TOTK_DSC_VIC_SETS];
} totk_dsc_vic;

static inline int totk_vk_dsc_fast_on(void) {
    static _Atomic int v = -1;
    int r = atomic_load_explicit(&v, memory_order_relaxed);
    if (r < 0) {
        const char *e = getenv("TOTK_VK_DSC_FAST");
        r = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&v, r, memory_order_relaxed);
    }
    return r;
}

static inline void totk_hash_dual64(const void *p, size_t n, uint64_t h[2]) {
    assert(h != NULL && (n == 0 || p != NULL));
    const uint8_t *b = (const uint8_t *)p;
    size_t i = 0;
    uint64_t a0 = 0xcbf29ce484222325ull, a1 = 0x9e3779b97f4a7c15ull;
    uint64_t c0 = 0x6a09e667f3bcc909ull, c1 = 0xbb67ae8584caa73bull;
    for (; i + 16 <= n; i += 16) {
        uint64_t w0, w1;
        memcpy(&w0, b + i, 8);
        memcpy(&w1, b + i + 8, 8);
        a0 = (a0 ^ w0) * 0x9e3779b97f4a7c15ull;
        a1 = (a1 ^ w1) * 0xbf58476d1ce4e5b9ull;
        c0 = (c0 + w0) * 0xc2b2ae3d27d4eb4full;
        c1 = (c1 + w1) * 0x94d049bb133111ebull;
    }
    uint64_t a = a0 ^ (a1 << 17 | a1 >> 47), c = c0 ^ (c1 << 31 | c1 >> 33);
    for (; i + 8 <= n; i += 8) {
        uint64_t w; memcpy(&w, b + i, 8);
        a = (a ^ w) * 0x9e3779b97f4a7c15ull;
        c = (c + w) * 0xc2b2ae3d27d4eb4full;
    }
    for (; i < n; i++) {
        a = (a ^ b[i]) * 0x100000001b3ull;
        c = (c + b[i]) * 0xc2b2ae3d27d4eb4full;
    }
    h[0] = a ^ (a >> 32);
    h[1] = c ^ (c >> 31);
}

static inline uint32_t totk_gen_idx(uint32_t key, uint64_t rev, uint32_t w, uint32_t g0) {
    uint64_t x = (rev * 0x9e3779b97f4a7c15ull) ^ ((uint64_t)key * 0xbf58476d1ce4e5b9ull) ^
                 ((uint64_t)w * 0x94d049bb133111ebull) ^ ((uint64_t)g0 * 0xc2b2ae3d27d4eb4full);
    return (uint32_t)(((x ^ (x >> 30)) >> 4) & (TOTK_GEN_SETS - 1u));
}

static inline int totk_gen_memo4_find(const totk_gen_memo4 *m, uint64_t sub, uint64_t rev, uint32_t key,
                                      uint32_t rw, uint32_t rh, const uint32_t g[3],
                                      uint64_t *buf_out, uint64_t *off_out, uint64_t *rng_out) {
    assert(m != NULL && g != NULL && buf_out != NULL && off_out != NULL && rng_out != NULL);
    const totk_gen_set *set = &m->s[totk_gen_idx(key, rev, rw, g[0])];
    for (uint32_t w = 0; w < TOTK_DSC_WAYS; w++) {
        const totk_gen_ent *e = &set->e[w];
        if ((TOTK_DSC_FAST_FAULT(21) || e->sub == sub) && e->rev == rev && e->key == key &&
            e->w == rw && e->h == rh && e->g0 == g[0] && e->g1 == g[1] && e->g2 == g[2] && e->buf) {
            *buf_out = e->buf; *off_out = e->off; *rng_out = e->range;
            return 1;
        }
    }
    return 0;
}

static inline void totk_gen_memo4_put(totk_gen_memo4 *m, uint64_t sub, uint64_t rev, uint32_t key,
                                      uint32_t rw, uint32_t rh, const uint32_t g[3],
                                      uint64_t buf, uint64_t off, uint64_t rng) {
    assert(m != NULL && g != NULL);
    if (!buf) return;
    totk_gen_set *set = &m->s[totk_gen_idx(key, rev, rw, g[0])];
    uint32_t slot = TOTK_DSC_WAYS;
    for (uint32_t w = 0; w < TOTK_DSC_WAYS; w++) {
        const totk_gen_ent *e = &set->e[w];
        if (e->sub != sub || !e->buf || (e->rev == rev && e->key == key && e->w == rw && e->h == rh &&
            e->g0 == g[0] && e->g1 == g[1] && e->g2 == g[2])) { slot = w; break; }
    }
    if (slot == TOTK_DSC_WAYS) slot = set->rr++ & (TOTK_DSC_WAYS - 1u);
    set->e[slot] = (totk_gen_ent){sub, rev, buf, off, rng, key, rw, rh, g[0], g[1], g[2]};
}

static inline uint32_t totk_blk_idx(int key, uint64_t h0) {
    return (uint32_t)((h0 ^ ((uint64_t)(uint32_t)key * 0x9e3779b97f4a7c15ull >> 32)) & (TOTK_BLK_SETS - 1u));
}

static inline int totk_blk_memo4_find(const totk_blk_memo4 *m, uint64_t sub, int key, const void *data, size_t n,
                                      uint64_t *buf_out, uint64_t *off_out, uint64_t *rng_out,
                                      uint8_t **ptr_out, uint64_t h_out[2]) {
    assert(m != NULL && data != NULL && buf_out != NULL && off_out != NULL && rng_out != NULL && h_out != NULL);
    totk_hash_dual64(data, n, h_out);
    if (!n) return 0;
    const totk_blk_set *set = &m->s[totk_blk_idx(key, h_out[0])];
    for (uint32_t w = 0; w < TOTK_DSC_WAYS; w++) {
        const totk_blk_ent *e = &set->e[w];
        if ((TOTK_DSC_FAST_FAULT(21) || e->sub == sub) && e->key == key && e->n == n &&
            e->h0 == h_out[0] && e->h1 == h_out[1] && e->bytes && !memcmp(e->bytes, data, n)) {
            *buf_out = e->buf; *off_out = e->off; *rng_out = e->range;
            if (ptr_out) *ptr_out = e->bytes;
            return 1;
        }
    }
    return 0;
}

static inline void totk_blk_memo4_put(totk_blk_memo4 *m, uint64_t sub, int key, size_t n, const uint64_t h[2],
                                      uint64_t buf, uint64_t off, uint64_t rng, uint8_t *bytes) {
    assert(m != NULL && h != NULL);
    if (!n || !bytes || !buf) return;
    totk_blk_set *set = &m->s[totk_blk_idx(key, h[0])];
    uint32_t slot = TOTK_DSC_WAYS;
    for (uint32_t w = 0; w < TOTK_DSC_WAYS; w++) {
        const totk_blk_ent *e = &set->e[w];
        if (e->sub != sub || !e->bytes || (e->key == key && e->n == n && e->h0 == h[0] && e->h1 == h[1])) { slot = w; break; }
    }
    if (slot == TOTK_DSC_WAYS) slot = set->rr++ & (TOTK_DSC_WAYS - 1u);
    set->e[slot] = (totk_blk_ent){sub, h[0], h[1], buf, off, rng, n, bytes, (int32_t)key, 0};
}

static inline uint32_t totk_dsc_vic_idx(const uint64_t h[2]) {
    return (uint32_t)(((h[0] >> 11) ^ h[1]) & (TOTK_DSC_VIC_SETS - 1u));
}

static inline uint64_t totk_dsc_vic_find(const totk_dsc_vic *v, uint64_t sub, uint64_t dsl, size_t n, const uint64_t h[2]) {
    assert(v != NULL && h != NULL);
    if (!dsl) return 0;
    const totk_dsc_vic_set *set = &v->s[totk_dsc_vic_idx(h)];
    for (uint32_t w = 0; w < TOTK_DSC_WAYS; w++) {
        const totk_dsc_vic_ent *e = &set->e[w];
        if (e->sub == sub && e->dsl == dsl && e->n == n && e->h0 == h[0] &&
            (TOTK_DSC_FAST_FAULT(22) || e->h1 == h[1]) && e->set) {
            return e->set;
        }
    }
    return 0;
}

static inline void totk_dsc_vic_put(totk_dsc_vic *v, uint64_t sub, uint64_t dsl, size_t n, const uint64_t h[2], uint64_t set_val) {
    assert(v != NULL && h != NULL);
    if (!dsl || !set_val) return;
    totk_dsc_vic_set *set = &v->s[totk_dsc_vic_idx(h)];
    uint32_t slot = TOTK_DSC_WAYS;
    for (uint32_t w = 0; w < TOTK_DSC_WAYS; w++) {
        const totk_dsc_vic_ent *e = &set->e[w];
        if (e->sub != sub || !e->set || (e->dsl == dsl && e->n == n && e->h0 == h[0] && e->h1 == h[1])) { slot = w; break; }
    }
    if (slot == TOTK_DSC_WAYS) slot = set->rr++ & (TOTK_DSC_WAYS - 1u);
    set->e[slot] = (totk_dsc_vic_ent){sub, h[0], h[1], dsl, set_val, n};
}

#endif
