#pragma once
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TOTK_TIME_FAST_RETRIES 8

static inline int totk_time_fast_enabled(void) {
    static _Atomic int on = -1;
    int v = atomic_load_explicit(&on, memory_order_relaxed);
    if (v < 0) {
        const char *e = getenv("TOTK_TIME_FAST");
        v = (e && e[0] == '0' && e[1] == '\0') ? 0 : 1;
        atomic_store_explicit(&on, v, memory_order_relaxed);
    }
    return v;
}

#ifdef TOTK_TIME_FAST_TEST_HOOKS
static _Atomic int g_totk_time_fast_override = -1;

static inline void totk_time_fast_reset_for_test(int en) {
    atomic_store_explicit(&g_totk_time_fast_override, en, memory_order_relaxed);
}

static inline int totk_time_fast_active(void) {
    int ov = atomic_load_explicit(&g_totk_time_fast_override, memory_order_relaxed);
    return ov >= 0 ? ov : totk_time_fast_enabled();
}

static inline int totk_time_w25_fault(void) {
    static _Atomic int gf = -1;
    int v = atomic_load_explicit(&gf, memory_order_relaxed);
    if (v < 0) {
        const char *e = getenv("TOTK_KTGL_OPT_FAULT");
        v = e ? atoi(e) : 0;
        atomic_store_explicit(&gf, v, memory_order_relaxed);
    }
    return v;
}
#else
static inline int totk_time_fast_active(void) { return totk_time_fast_enabled(); }
static inline int totk_time_w25_fault(void) { return 0; }
#endif

static inline uint64_t totk_time_tz_tag(const char *tz) {
    if (!tz) return 0ull;
    uint64_t h = 14695981039346656037ull;
    for (int i = 0; i < 128 && tz[i]; i++) {
        h ^= (uint8_t)tz[i];
        h *= 1099511628211ull;
    }
    return h | 1ull;
}

typedef struct {
    _Atomic uint64_t seq;
    _Atomic int64_t posix_sec;
    _Atomic uint64_t tz_tag;
    _Atomic uint64_t ct_w;
    _Atomic uint64_t info_w[3];
} totk_time_cal_slot;

typedef struct {
    totk_time_cal_slot slots[2];
} totk_time_cal_cache;

static inline void totk_time_cal_write(totk_time_cal_cache *c, int local,
                                       int64_t sec, uint64_t tz, const void *ct,
                                       const void *info) {
    totk_time_cal_slot *s = &c->slots[local ? 1 : 0];
    uint64_t cw = 0;
    uint64_t iw[3] = {0, 0, 0};
    if (ct) memcpy(&cw, ct, 8);
    if (info) memcpy(iw, info, 24);
    uint64_t q = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_store_explicit(&s->seq, (q + 1u) | 1ull, memory_order_release);
    atomic_store_explicit(&s->posix_sec, sec, memory_order_relaxed);
    atomic_store_explicit(&s->tz_tag, tz, memory_order_relaxed);
    atomic_store_explicit(&s->ct_w, cw, memory_order_relaxed);
    for (int i = 0; i < 3; i++)
        atomic_store_explicit(&s->info_w[i], iw[i], memory_order_relaxed);
    atomic_store_explicit(&s->seq, (q + 2u) & ~1ull, memory_order_release);
}

static inline int totk_time_cal_read(const totk_time_cal_cache *c, int local,
                                     int64_t sec, uint64_t tz, void *ct_out,
                                     void *info_out) {
    const totk_time_cal_slot *s = &c->slots[local ? 1 : 0];
    int f = totk_time_w25_fault();
    for (int r = 0; r < TOTK_TIME_FAST_RETRIES; r++) {
        uint64_t s1 = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (!s1 || (s1 & 1ull)) continue;
        int64_t ssec = atomic_load_explicit(&s->posix_sec, memory_order_relaxed);
        uint64_t stz = atomic_load_explicit(&s->tz_tag, memory_order_relaxed);
        if ((f != 78 && ssec != sec) || (f != 79 && stz != tz)) return 0;
        uint64_t cw = atomic_load_explicit(&s->ct_w, memory_order_relaxed);
        uint64_t iw[3];
        for (int i = 0; i < 3; i++)
            iw[i] = atomic_load_explicit(&s->info_w[i], memory_order_relaxed);
        uint64_t s2 = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (s1 == s2) {
            if (ct_out) {
                memcpy(ct_out, &cw, 8);
                if (f == 77) ((uint8_t *)ct_out)[0] ^= 0x7fu;
            }
            if (info_out) memcpy(info_out, iw, 24);
            return 1;
        }
    }
    return 0;
}

typedef struct {
    _Atomic uint64_t seq;
    _Atomic uint64_t tz_tag;
    _Atomic uint64_t loc_w[5];
} totk_time_loc_cache;

static inline void totk_time_loc_write(totk_time_loc_cache *s, uint64_t tz,
                                       const char in[0x24]) {
    uint64_t lw[5] = {0, 0, 0, 0, 0};
    memcpy(lw, in, 0x24);
    uint64_t q = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_store_explicit(&s->seq, (q + 1u) | 1ull, memory_order_release);
    atomic_store_explicit(&s->tz_tag, tz, memory_order_relaxed);
    for (int i = 0; i < 5; i++)
        atomic_store_explicit(&s->loc_w[i], lw[i], memory_order_relaxed);
    atomic_store_explicit(&s->seq, (q + 2u) & ~1ull, memory_order_release);
}

static inline int totk_time_loc_read(const totk_time_loc_cache *s, uint64_t tz,
                                     char out[0x24]) {
    for (int r = 0; r < TOTK_TIME_FAST_RETRIES; r++) {
        uint64_t s1 = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (!s1 || (s1 & 1ull)) continue;
        uint64_t stz = atomic_load_explicit(&s->tz_tag, memory_order_relaxed);
        if (stz != tz) return 0;
        uint64_t lw[5];
        for (int i = 0; i < 5; i++)
            lw[i] = atomic_load_explicit(&s->loc_w[i], memory_order_relaxed);
        uint64_t s2 = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (s1 == s2) {
            memcpy(out, lw, 0x24);
            if (totk_time_w25_fault() == 80) out[0] ^= 0x7fu;
            return 1;
        }
    }
    return 0;
}

#ifdef IPC_MAX_BUF
static inline void totk_ipc_req_reset_fast(IpcRequest *rq, unsigned m0) {
    rq->type = m0 & 0xffffu;
    rq->cmd = 0u;
    rq->has_pid = 0;
    rq->pid = 0ull;
    rq->ncopy = 0u;
    rq->nmove = 0u;
    rq->nx = (m0 >> 16) & 0xfu;
    rq->na = (m0 >> 20) & 0xfu;
    rq->nb = (m0 >> 24) & 0xfu;
    rq->nw = (m0 >> 28) & 0xfu;
    rq->nc = 0u;
    rq->data = NULL;
    rq->data_size = 0u;
}

static inline void totk_ipc_resp_reset_fast(IpcResponse *rs) {
    rs->result = 0u;
    rs->data_size = 0u;
    rs->ncopy = 0u;
    rs->nmove = 0u;
    rs->nxout = 0u;
}
#endif

static inline size_t totk_ipc_respond_bytes(size_t words_off, unsigned nwords,
                                            size_t max_bytes) {
    size_t total = words_off + (size_t)nwords * 4u;
    if (total > max_bytes) total = max_bytes;
    if (totk_time_w25_fault() == 81 && total >= 8u) total -= 4u;
    return total;
}
