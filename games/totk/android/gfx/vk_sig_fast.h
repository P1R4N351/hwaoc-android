// Fast 128-bit texture-occupancy mask, revision-stamped treg depth memo, O(1) alt-variant map & vin stream mask (Opt 52).
#ifndef VK_SIG_FAST_H
#define VK_SIG_FAST_H
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TOTK_W50_TEST_HOOKS
extern _Atomic int g_totk_vk_sig_fast_override;
extern _Atomic int g_totk_w50_fault;
static inline void totk_vk_sig_fast_set_for_test(int v) {
    atomic_store_explicit(&g_totk_vk_sig_fast_override, v, memory_order_release);
}
static inline void totk_w50_set_fault(int f) {
    atomic_store_explicit(&g_totk_w50_fault, f, memory_order_release);
}
static inline int totk_w50_parse_fault(void) {
    const char *e = getenv("TOTK_W50_FAULT");
    if (e && *e) return atoi(e);
    const char *k = getenv("TOTK_KTGL_OPT_FAULT");
    if (k && *k) {
        int kv = atoi(k);
        if (kv >= 196 && kv <= 200) return kv - 195;
    }
    return 0;
}
static inline int totk_w50_fault_mode(void) {
    int f = atomic_load_explicit(&g_totk_w50_fault, memory_order_relaxed);
    if (f >= 0) return f;
    int v = totk_w50_parse_fault();
    atomic_store_explicit(&g_totk_w50_fault, v, memory_order_relaxed);
    return v;
}
#else
static inline int totk_w50_fault_mode(void) { return 0; }
#endif

static inline int totk_vk_sig_fast_on(void) {
#ifdef TOTK_W50_TEST_HOOKS
    int ov = atomic_load_explicit(&g_totk_vk_sig_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
#endif
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("TOTK_VK_SIG_FAST");
        v = !(e && strcmp(e, "0") == 0);
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
}

static inline void totk_tex_nz_update(uint64_t nz[2], uint8_t *ready, int first,
                                        const uint64_t *h, int n) {
    if (!*ready || n <= 0) return;
    uint64_t lo = nz[0], hi = nz[1];
    for (int i = 0; i < n; i++) {
        int slot = first + i;
        uint64_t bit = UINT64_C(1) << (unsigned)(slot & 63);
        if (h[i] != UINT64_C(0)) {
            if (slot < 64) lo |= bit; else hi |= bit;
        } else {
            if (slot < 64) lo &= ~bit; else hi &= ~bit;
        }
    }
    nz[0] = lo;
    nz[1] = hi;
}

static inline void totk_stage_tex_hi_words(const uint64_t nz[2], int hi, uint64_t out_w[2]) {
    if (hi <= 0) {
        out_w[0] = UINT64_C(0);
        out_w[1] = UINT64_C(0);
        return;
    }
    uint64_t m0 = hi >= 64 ? ~UINT64_C(0) : ((UINT64_C(1) << (unsigned)hi) - UINT64_C(1));
    uint64_t m1 = hi <= 64 ? UINT64_C(0)
                           : (hi >= 128 ? ~UINT64_C(0) : ((UINT64_C(1) << (unsigned)(hi - 64)) - UINT64_C(1)));
    out_w[0] = nz[0] & m0;
    out_w[1] = (totk_w50_fault_mode() == 1) ? UINT64_C(0) : (nz[1] & m1);
}

static inline int totk_is_depth_fmt_fast(int f) {
    if (totk_w50_fault_mode() == 2) return f >= 0x32 && f <= 0x35;
    return f >= 0x32 && f <= 0x36;
}

enum { TOTK_TREG_DEPTH_CAP = 1024u, TOTK_TREG_DEPTH_MASK = TOTK_TREG_DEPTH_CAP - 1u };

typedef struct {
    uint32_t tid, gen;
    uint8_t depth;
} totk_treg_depth_slot_t;

typedef struct {
    uint64_t rev;
    uint32_t gen;
    totk_treg_depth_slot_t slots[TOTK_TREG_DEPTH_CAP];
} totk_treg_depth_cache_t;

static inline uint32_t totk_treg_depth_slot(int tid) {
    uint32_t x = (uint32_t)tid * UINT32_C(0x9e3779b1);
    return (x ^ (x >> 16)) & TOTK_TREG_DEPTH_MASK;
}

static inline int totk_treg_depth_peek(totk_treg_depth_cache_t *c, int tid, uint64_t rev, int *depth_out) {
    if (!c->gen) { c->gen = 1u; c->rev = rev; }
    if (c->rev != rev && totk_w50_fault_mode() != 3) {
        c->gen++;
        if (!c->gen) { memset(c->slots, 0, sizeof c->slots); c->gen = 1u; }
        c->rev = rev;
        return 0;
    }
    const totk_treg_depth_slot_t *e = &c->slots[totk_treg_depth_slot(tid)];
    if (e->gen == c->gen && e->tid == (uint32_t)tid) {
        *depth_out = (int)e->depth;
        return 1;
    }
    return 0;
}

static inline void totk_treg_depth_put(totk_treg_depth_cache_t *c, int tid, int depth) {
    if (!c->gen) c->gen = 1u;
    totk_treg_depth_slot_t *e = &c->slots[totk_treg_depth_slot(tid)];
    e->tid = (uint32_t)tid;
    e->gen = c->gen;
    e->depth = (uint8_t)(depth != 0);
}

enum { TOTK_ALT_MAP_CAP = 64u, TOTK_ALT_MAP_MASK = TOTK_ALT_MAP_CAP - 1u };

typedef struct {
    uint64_t sigs[TOTK_ALT_MAP_CAP];
    uint16_t idx1[TOTK_ALT_MAP_CAP];
    uint16_t count;
    uint8_t overflow;
} totk_alt_map_t;

static inline uint32_t totk_alt_hash(uint64_t sig) {
    sig = (sig ^ (sig >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    sig = (sig ^ (sig >> 27)) * UINT64_C(0x94d049bb133111eb);
    return (uint32_t)(sig ^ (sig >> 31)) & TOTK_ALT_MAP_MASK;
}

static inline void totk_alt_map_clear(totk_alt_map_t *m) {
    memset(m, 0, sizeof *m);
}

static inline int totk_alt_map_find(const totk_alt_map_t *m, uint64_t sig) {
    uint32_t slot = totk_alt_hash(sig);
    int fault4 = totk_w50_fault_mode() == 4;
    for (uint32_t probe = 0u; probe < TOTK_ALT_MAP_CAP; probe++) {
        uint16_t idx1 = m->idx1[slot];
        if (!idx1) return -1;
        uint64_t s = m->sigs[slot];
        if (fault4 ? ((uint32_t)s == (uint32_t)sig) : (s == sig)) return (int)(idx1 - 1u);
        slot = (slot + 1u) & TOTK_ALT_MAP_MASK;
    }
    return -1;
}

static inline void totk_alt_map_put(totk_alt_map_t *m, uint64_t sig, uint16_t idx1) {
    if (!idx1 || m->count >= 48u) { m->overflow = 1u; return; }
    uint32_t slot = totk_alt_hash(sig);
    for (uint32_t probe = 0u; probe < TOTK_ALT_MAP_CAP; probe++) {
        uint16_t cur = m->idx1[slot];
        if (!cur) {
            m->idx1[slot] = idx1;
            m->sigs[slot] = sig;
            m->count++;
            return;
        }
        if (m->sigs[slot] == sig) { m->idx1[slot] = idx1; return; }
        slot = (slot + 1u) & TOTK_ALT_MAP_MASK;
    }
    m->overflow = 1u;
}

static inline uint32_t totk_vin_cand_mask_fast(uint32_t vin_mask, int nattr) {
    if (nattr <= 0) return 0u;
    if (totk_w50_fault_mode() == 5) return vin_mask & 0xffu;
    uint32_t cap = nattr >= 16 ? 0xffffu : ((1u << (unsigned)nattr) - 1u);
    return vin_mask & cap;
}
#endif
