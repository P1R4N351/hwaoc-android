/* Shared-page boundary hashing and selective unpinning for mirrored CPU-pool textures (vk_res.c).
 * Mirrors vka_edge_unpin.h from hwaoi-android: prevents KTGL sub-allocated static textures sharing a
 * dirty protection unit (64 KiB on Linux/Android, system page on Apple) with hot CPU writes from
 * faulting, re-arming mprotect, and re-uploading every submission. */
#pragma once
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum { VK_EDGE_UNPIN_AFTER = 2, VK_EDGE_INTERIOR_HASH_MAX = 1u << 20 };

typedef struct {
    uintptr_t r[2][2];
    uintptr_t ia;
    uintptr_t ie;
    size_t unit;
    int has_edges;
} vk_edge_geom;

typedef struct {
    uint64_t h[2];
    uint64_t in_h;
    uint8_t armed;
    uint8_t streaks;
    uint8_t unpinned;
    uint8_t in_hashed;
} vk_edge_state;

static inline void vk_edge_split(uintptr_t unit_lo, uintptr_t a, size_t size, size_t unit, vk_edge_geom *g) {
    assert(g != NULL);
    assert(unit > 0);
    assert(size > 0);
    assert(a >= unit_lo && (a - unit_lo) < unit);
    uintptr_t e = a + size;
    assert(e >= a);
    uintptr_t ia = (a == unit_lo) ? a : (unit_lo + unit);
    uintptr_t ie = (e > ia) ? (ia + ((size_t)(e - ia) / unit) * unit) : 0;
    g->unit = unit;
    if (ie <= ia) {
        g->r[0][0] = a; g->r[0][1] = e;
        g->r[1][0] = 0; g->r[1][1] = 0;
        g->ia = 0; g->ie = 0;
    } else {
        g->r[0][0] = a;  g->r[0][1] = ia;
        g->r[1][0] = ie; g->r[1][1] = e;
        g->ia = ia; g->ie = ie;
    }
    g->has_edges = (g->r[0][1] > g->r[0][0]) || (g->r[1][1] > g->r[1][0]);
}

static inline uint64_t vk_edge_hash(uintptr_t lo, uintptr_t hi) {
    assert(hi >= lo);
    const uint8_t *p = (const uint8_t *)lo;
    size_t n = hi - lo, i = 0;
    uint64_t h = 0x9e3779b97f4a7c15ull ^ n, w = 0;
    for (; i + 8 <= n; i += 8) {
        memcpy(&w, p + i, 8);
        h ^= w * 0x87c37b91114253d5ull;
        h = ((h << 31) | (h >> 33)) * 0x4cf5ad432745937full;
    }
    for (; i < n; i++) h = (h ^ p[i]) * 0x100000001b3ull;
    return h ^ (h >> 29);
}

static inline void vk_edge_snapshot(const vk_edge_geom *g, vk_edge_state *st) {
    assert(g != NULL && st != NULL);
    st->h[0] = g->r[0][1] > g->r[0][0] ? vk_edge_hash(g->r[0][0], g->r[0][1]) : 0;
    st->h[1] = g->r[1][1] > g->r[1][0] ? vk_edge_hash(g->r[1][0], g->r[1][1]) : 0;
    st->armed = (uint8_t)(g->has_edges != 0);
    if (!st->unpinned && g->has_edges && g->ie > g->ia && (g->ie - g->ia) <= VK_EDGE_INTERIOR_HASH_MAX) {
        st->in_h = vk_edge_hash(g->ia, g->ie);
        st->in_hashed = 1;
    } else {
        st->in_h = 0;
        st->in_hashed = 0;
    }
}

static inline int vk_edge_changed(const vk_edge_geom *g, const vk_edge_state *st, int *h0_bad, int *h1_bad) {
    assert(g != NULL && st != NULL && h0_bad != NULL && h1_bad != NULL);
    *h0_bad = (g->r[0][1] > g->r[0][0]) && (vk_edge_hash(g->r[0][0], g->r[0][1]) != st->h[0]);
    *h1_bad = (g->r[1][1] > g->r[1][0]) && (vk_edge_hash(g->r[1][0], g->r[1][1]) != st->h[1]);
    return *h0_bad || *h1_bad;
}

static inline int vk_edge_interior_collateral(const vk_edge_geom *g, const vk_edge_state *st, int edge_pg_dirty) {
    assert(g != NULL && st != NULL);
    if (!st->in_hashed || !edge_pg_dirty || g->ie <= g->ia) return 0;
    return vk_edge_hash(g->ia, g->ie) == st->in_h;
}
