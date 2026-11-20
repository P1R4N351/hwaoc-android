#ifndef TOTK_VK_STATE_FAST_H
#define TOTK_VK_STATE_FAST_H
/* Wave 11 (Opt 13): PSO-state dedup, dynamic-state front-memo & 8B/16B hmap fast paths (TOTK_VK_STATE_FAST=1). */
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TOTK_KTGL_OPT_TEST
extern int g_fault_mode;
#define TOTK_STATE_FAST_FAULT(n) (g_fault_mode == (n))
#else
#define TOTK_STATE_FAST_FAULT(n) 0
#endif

typedef struct {
    uint32_t w, h, host_w, host_h, native_w, native_h;
    int32_t dfmt, sfmt, native, pad;
} totk_dyn_rpass_key;

typedef struct {
    uint64_t dyn_rev, seq, last_seq;
    totk_dyn_rpass_key rk;
    int32_t prim_key, d16_tie;
    uint32_t dyn_size, valid;
    uint8_t dyn_bytes[256];
} totk_dyn_memo;

static inline int totk_vk_state_fast_on(void) {
    static _Atomic int v = -1;
    int r = atomic_load_explicit(&v, memory_order_relaxed);
    if (r < 0) {
        const char *e = getenv("TOTK_VK_STATE_FAST");
        r = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&v, r, memory_order_relaxed);
    }
    return r;
}

static inline int totk_dyn_memo_get(const totk_dyn_memo *m, uint64_t dyn_rev, const totk_dyn_rpass_key *rk,
                                      int d16_tie, int32_t prim_key, void *dyn_out, size_t dyn_sz) {
    assert(m != NULL && rk != NULL && dyn_out != NULL && dyn_sz <= sizeof(m->dyn_bytes));
    if (!m->valid || m->dyn_size != (uint32_t)dyn_sz || m->prim_key != prim_key || m->d16_tie != d16_tie) return 0;
    if (!TOTK_STATE_FAST_FAULT(30) && m->dyn_rev != dyn_rev) return 0;
    if (memcmp(&m->rk, rk, sizeof *rk) != 0) return 0;
    memcpy(dyn_out, m->dyn_bytes, dyn_sz);
    return 1;
}

static inline void totk_dyn_memo_put(totk_dyn_memo *m, uint64_t dyn_rev, const totk_dyn_rpass_key *rk,
                                       int d16_tie, int32_t prim_key, const void *dyn_in, size_t dyn_sz) {
    assert(m != NULL && rk != NULL && dyn_in != NULL && dyn_sz <= sizeof(m->dyn_bytes));
    m->dyn_rev = dyn_rev;
    m->rk = *rk;
    m->prim_key = prim_key;
    m->d16_tie = d16_tie;
    m->dyn_size = (uint32_t)dyn_sz;
    memcpy(m->dyn_bytes, dyn_in, dyn_sz);
    m->valid = 1u;
    if (++m->seq == 0u) m->seq = 1u;
}

static inline int totk_state_dirty_gate(int changed) {
    if (TOTK_STATE_FAST_FAULT(29)) return 0;
    return changed;
}

#endif
