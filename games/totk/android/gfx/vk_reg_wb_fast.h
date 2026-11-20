#ifndef TOTK_VK_REG_WB_FAST_H
#define TOTK_VK_REG_WB_FAST_H
/* Wave 20 (Opt 22): TLS 32x4 vk_treg_get front-memo & seqlock counting WB summary (TOTK_VK_REG_WB_FAST=1). */
#include <assert.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TOTK_KTGL_OPT_TEST
extern int g_fault_mode;
#define TOTK_REG_WB_FAULT(n) (g_fault_mode == (n))
#else
#define TOTK_REG_WB_FAULT(n) 0
#endif

static inline int totk_vk_reg_wb_fast_on(void) {
    static _Atomic int cached = -1;
    int v = atomic_load_explicit(&cached, memory_order_relaxed);
    if (v < 0) {
        const char *e = getenv("TOTK_VK_REG_WB_FAST");
        v = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&cached, v, memory_order_relaxed);
    }
    return v;
}

enum {
    TOTK_TREG_SETS = 32u,
    TOTK_TREG_WAYS = 4u,
    TOTK_TREG_MAX_BYTES = 256u,
    TOTK_WB_MAX_CHUNKS = 64u
};

typedef struct {
    uint64_t gtag[TOTK_TREG_SETS][TOTK_TREG_WAYS];
    uint32_t tid[TOTK_TREG_SETS][TOTK_TREG_WAYS];
    uint8_t ok[TOTK_TREG_SETS][TOTK_TREG_WAYS];
    uint8_t ref[TOTK_TREG_SETS];
    uint8_t hand[TOTK_TREG_SETS];
    uint8_t data[TOTK_TREG_SETS][TOTK_TREG_WAYS][TOTK_TREG_MAX_BYTES];
} totk_treg_memo4;

static inline uint32_t totk_treg_set_idx(uint32_t tid) {
    return ((tid * UINT32_C(0x9e3779b1)) >> 27) & (TOTK_TREG_SETS - 1u);
}

static inline int totk_treg_memo4_get(
    totk_treg_memo4 **tls_slot, uint32_t tid, uint64_t gtag,
    void *out, size_t nbytes, int *ok_out) {
    assert(nbytes <= TOTK_TREG_MAX_BYTES && ok_out != NULL);
    totk_treg_memo4 *m = tls_slot ? *tls_slot : NULL;
    if (!m || !tid || !gtag) return 0;
    uint32_t s = totk_treg_set_idx(tid);
    for (uint32_t w = 0; w < TOTK_TREG_WAYS; w++) {
        uint64_t sg = m->gtag[s][w];
        if (!sg || m->tid[s][w] != tid) continue;
        if (!TOTK_REG_WB_FAULT(56) && sg != gtag) continue;
        int ok = (int)m->ok[s][w];
        *ok_out = ok;
        if (ok && out && nbytes) memcpy(out, m->data[s][w], nbytes);
        m->ref[s] |= (uint8_t)(1u << w);
        return 1;
    }
    return 0;
}

static inline void totk_treg_memo4_put(
    totk_treg_memo4 **tls_slot, uint32_t tid, uint64_t gtag,
    int ok, const void *val, size_t nbytes) {
    assert(nbytes <= TOTK_TREG_MAX_BYTES);
    if (!tls_slot || !tid || !gtag) return;
    totk_treg_memo4 *m = *tls_slot;
    if (!m) {
        m = (totk_treg_memo4 *)calloc(1u, sizeof *m);
        if (!m) return;
        *tls_slot = m;
    }
    uint32_t s = totk_treg_set_idx(tid), h = m->hand[s] & 3u, slot = h;
    int found = 0;
    for (uint32_t w = 0; w < TOTK_TREG_WAYS; w++) {
        if (m->tid[s][w] == tid && m->gtag[s][w]) { slot = w; found = 1; break; }
    }
    if (!found) {
        for (uint32_t step = 0; step < TOTK_TREG_WAYS * 2u; step++) {
            uint8_t bit = (uint8_t)(1u << h);
            if (!m->gtag[s][h] || !(m->ref[s] & bit)) { slot = h; break; }
            m->ref[s] &= (uint8_t)~bit;
            h = (h + 1u) & 3u;
        }
        m->hand[s] = (uint8_t)((slot + 1u) & 3u);
    }
    m->tid[s][slot] = tid;
    m->gtag[s][slot] = gtag;
    m->ok[s][slot] = (uint8_t)(ok != 0);
    m->ref[s] |= (uint8_t)(1u << slot);
    if (ok && val && nbytes) memcpy(m->data[s][slot], val, nbytes);
}

typedef struct {
    _Atomic uint32_t seq;
    uint32_t active_n;
    uint32_t huge_n;
    uint32_t cnt[256];
    _Atomic uint32_t pub_active;
    _Atomic uint32_t pub_huge;
    _Atomic uint64_t pub_bloom[4];
    _Atomic uint32_t pub_landing_n;
    _Atomic uint32_t pub_landing_huge;
    _Atomic uint64_t pub_landing_lo;
    _Atomic uint64_t pub_landing_hi;
    _Atomic uint64_t pub_landing_bloom[4];
} totk_wb_summary;

static inline uint32_t totk_wb_chunk_bit(uint64_t c) {
    return (uint32_t)((c * UINT64_C(0x9e3779b97f4a7c15)) >> 56);
}

static inline int totk_wb_bloom_hit(const _Atomic uint64_t bl[4], uint64_t addr, uint64_t sz) {
    if (!sz || addr > UINT64_MAX - sz) return 1;
    uint64_t c0 = addr >> 16, c1 = (addr + sz - 1u) >> 16;
    if (c1 - c0 >= TOTK_WB_MAX_CHUNKS) return 1;
    for (uint64_t c = c0; c <= c1; c++) {
        uint32_t b = totk_wb_chunk_bit(c);
        uint64_t w = atomic_load_explicit(&bl[b >> 6], memory_order_relaxed);
        if (w & (UINT64_C(1) << (b & 63u))) return 1;
    }
    return 0;
}

static inline void totk_wb_apply_delta(totk_wb_summary *s, uint64_t addr, uint64_t sz, int dir) {
    if (!sz) return;
    if (dir > 0) s->active_n++;
    else if (s->active_n) s->active_n--;
    uint64_t c0 = addr >> 16;
    uint64_t end = addr > UINT64_MAX - sz ? UINT64_MAX : addr + sz - 1u;
    uint64_t c1 = end >> 16;
    if (c1 - c0 >= TOTK_WB_MAX_CHUNKS) {
        if (dir > 0) s->huge_n++;
        else if (s->huge_n) s->huge_n--;
        return;
    }
    for (uint64_t c = c0; c <= c1; c++) {
        uint32_t b = totk_wb_chunk_bit(c);
        if (dir > 0) s->cnt[b]++;
        else if (s->cnt[b]) s->cnt[b]--;
    }
}

static inline void totk_wb_publish_main(totk_wb_summary *s) {
    uint64_t w[4] = {0, 0, 0, 0};
    for (uint32_t b = 0; b < 256u; b++) {
        if (s->cnt[b]) w[b >> 6] |= UINT64_C(1) << (b & 63u);
    }
    atomic_store_explicit(&s->pub_active, s->active_n, memory_order_relaxed);
    atomic_store_explicit(&s->pub_huge, s->huge_n, memory_order_relaxed);
    for (uint32_t i = 0; i < 4u; i++) {
        atomic_store_explicit(&s->pub_bloom[i], w[i], memory_order_relaxed);
    }
}

static inline void totk_wb_sum_delta(totk_wb_summary *s, uint64_t addr, uint64_t sz, int dir) {
    assert(dir == 1 || dir == -1);
    if (!sz) return;
    atomic_fetch_add_explicit(&s->seq, 1u, memory_order_release);
    totk_wb_apply_delta(s, addr, sz, dir);
    totk_wb_publish_main(s);
    atomic_fetch_add_explicit(&s->seq, 1u, memory_order_release);
}

static inline void totk_wb_sum_replace(
    totk_wb_summary *s, uint64_t old_a, uint64_t old_sz, uint64_t new_a, uint64_t new_sz) {
    if (!old_sz && !new_sz) return;
    atomic_fetch_add_explicit(&s->seq, 1u, memory_order_release);
    if (old_sz) totk_wb_apply_delta(s, old_a, old_sz, -1);
    if (new_sz) totk_wb_apply_delta(s, new_a, new_sz, 1);
    totk_wb_publish_main(s);
    atomic_fetch_add_explicit(&s->seq, 1u, memory_order_release);
}

static inline void totk_wb_landing_begin(totk_wb_summary *s) {
    atomic_fetch_add_explicit(&s->seq, 1u, memory_order_release);
    atomic_store_explicit(&s->pub_landing_n, 0u, memory_order_relaxed);
    atomic_store_explicit(&s->pub_landing_huge, 0u, memory_order_relaxed);
    atomic_store_explicit(&s->pub_landing_lo, UINT64_MAX, memory_order_relaxed);
    atomic_store_explicit(&s->pub_landing_hi, 0u, memory_order_relaxed);
    for (uint32_t i = 0; i < 4u; i++) {
        atomic_store_explicit(&s->pub_landing_bloom[i], 0u, memory_order_relaxed);
    }
}

static inline void totk_wb_landing_add(totk_wb_summary *s, uint64_t addr, uint64_t sz) {
    if (!sz) return;
    uint64_t hi = addr > UINT64_MAX - sz ? UINT64_MAX : addr + sz;
    uint64_t lo0 = atomic_load_explicit(&s->pub_landing_lo, memory_order_relaxed);
    uint64_t hi0 = atomic_load_explicit(&s->pub_landing_hi, memory_order_relaxed);
    if (addr < lo0) atomic_store_explicit(&s->pub_landing_lo, addr, memory_order_relaxed);
    if (hi > hi0) atomic_store_explicit(&s->pub_landing_hi, hi, memory_order_relaxed);
    atomic_fetch_add_explicit(&s->pub_landing_n, 1u, memory_order_relaxed);
    uint64_t c0 = addr >> 16, c1 = (hi - 1u) >> 16;
    if (c1 - c0 >= TOTK_WB_MAX_CHUNKS) {
        atomic_fetch_add_explicit(&s->pub_landing_huge, 1u, memory_order_relaxed);
        return;
    }
    for (uint64_t c = c0; c <= c1; c++) {
        uint32_t b = totk_wb_chunk_bit(c);
        atomic_fetch_or_explicit(&s->pub_landing_bloom[b >> 6], UINT64_C(1) << (b & 63u), memory_order_relaxed);
    }
}

static inline void totk_wb_landing_end(totk_wb_summary *s) {
    atomic_fetch_add_explicit(&s->seq, 1u, memory_order_release);
}

static inline int totk_wb_sum_may_overlap(const totk_wb_summary *s, uint64_t addr, uint64_t sz) {
    if (!sz) return 0;
    uint32_t s0 = atomic_load_explicit(&s->seq, memory_order_acquire);
    if (!TOTK_REG_WB_FAULT(58) && (s0 & 1u)) return 1;
    uint32_t act = atomic_load_explicit(&s->pub_active, memory_order_relaxed);
    uint32_t huge = atomic_load_explicit(&s->pub_huge, memory_order_relaxed);
    int may_lk = 0;
    if (act && !TOTK_REG_WB_FAULT(57)) {
        may_lk = huge || totk_wb_bloom_hit(s->pub_bloom, addr, sz);
    }
    int may_ld = 0;
    if (!TOTK_REG_WB_FAULT(58) && atomic_load_explicit(&s->pub_landing_n, memory_order_relaxed)) {
        uint64_t lo = atomic_load_explicit(&s->pub_landing_lo, memory_order_relaxed);
        uint64_t hi = atomic_load_explicit(&s->pub_landing_hi, memory_order_relaxed);
        uint64_t qhi = addr > UINT64_MAX - sz ? UINT64_MAX : addr + sz;
        if (addr < hi && qhi > lo) {
            uint32_t lh = atomic_load_explicit(&s->pub_landing_huge, memory_order_relaxed);
            may_ld = lh || totk_wb_bloom_hit(s->pub_landing_bloom, addr, sz);
        }
    }
    uint32_t s1 = atomic_load_explicit(&s->seq, memory_order_acquire);
    if (!TOTK_REG_WB_FAULT(58) && s0 != s1) return 1;
    return may_lk || may_ld;
}
#endif
