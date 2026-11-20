#ifndef TOTK_NVN_HLE_FAST_H
#define TOTK_NVN_HLE_FAST_H
/* Wave 19 (Opt 21): Pin-span slice, lock-free 128x4 pin front-memo & ctl counting bloom (TOTK_NVN_HLE_FAST=1). */
#include <assert.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TOTK_KTGL_OPT_TEST
extern int g_fault_mode;
#define TOTK_HLE_FAST_FAULT(n) (g_fault_mode == (n))
#else
#define TOTK_HLE_FAST_FAULT(n) 0
#endif

static inline int totk_nvn_hle_fast_on(void) {
    static _Atomic int cached = -1;
    int v = atomic_load_explicit(&cached, memory_order_relaxed);
    if (v < 0) {
        const char *e = getenv("TOTK_NVN_HLE_FAST");
        v = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&cached, v, memory_order_relaxed);
    }
    return v;
}

enum {
    TOTK_PIN_UNBOUNDED = UINT32_MAX,
    TOTK_PIN_M4_SETS = 128u,
    TOTK_PIN_M4_WAYS = 4u,
    TOTK_PIN_REFRESH_SUBS = 64u,
    TOTK_CTL_PAGE_SHIFT = 12u
};

#define TOTK_PIN_EPOCH_MASK 0x0000FFFFFFFFFFFFull
#define TOTK_PIN_WRITER_ONE (1ull << 48)
#define TOTK_PIN_WEND_DELTA 0xFFFF000000000001ull

typedef struct {
    uint32_t lo;
    uint32_t hi;
} totk_pin_span;

static inline void totk_pin_span_init(totk_pin_span *s) {
    assert(s != NULL);
    s->lo = TOTK_PIN_UNBOUNDED;
    s->hi = 0u;
}

static inline void totk_pin_span_note_op(totk_pin_span *s, size_t off, uint32_t size) {
    assert(s != NULL);
    if (s->hi == TOTK_PIN_UNBOUNDED) return;
    if (off >= (size_t)TOTK_PIN_UNBOUNDED ||
        (uint64_t)off + (uint64_t)size >= (uint64_t)TOTK_PIN_UNBOUNDED) {
        s->lo = 0u;
        s->hi = TOTK_PIN_UNBOUNDED;
        return;
    }
    uint32_t off32 = (uint32_t)off;
    uint32_t end = off32 + size;
    if (off32 < s->lo) s->lo = off32;
    if (end > s->hi) s->hi = end;
}

static inline void totk_pin_span_note_marker(totk_pin_span *s, size_t rec_off,
                                             uint32_t t_lo, uint32_t t_hi) {
    assert(s != NULL);
    if (s->hi == TOTK_PIN_UNBOUNDED || t_lo >= t_hi) return;
    if (t_hi == TOTK_PIN_UNBOUNDED || t_lo < 8u || rec_off >= (size_t)TOTK_PIN_UNBOUNDED) {
        s->lo = 0u;
        s->hi = TOTK_PIN_UNBOUNDED;
        return;
    }
    uint64_t alo = (uint64_t)rec_off + (uint64_t)(t_lo - 8u);
    uint64_t ahi = (uint64_t)rec_off + (uint64_t)(t_hi - 8u);
    if (ahi >= (uint64_t)TOTK_PIN_UNBOUNDED) {
        s->lo = 0u;
        s->hi = TOTK_PIN_UNBOUNDED;
        return;
    }
    if ((uint32_t)alo < s->lo) s->lo = (uint32_t)alo;
    if ((uint32_t)ahi > s->hi) s->hi = (uint32_t)ahi;
}

static inline int totk_pin_span_slice(uint32_t pin_lo, uint32_t pin_hi,
                                      uint32_t *out_lo, uint32_t *out_hi) {
    assert(out_lo != NULL && out_hi != NULL);
    if (TOTK_HLE_FAST_FAULT(53)) return 0;
    if (pin_lo >= pin_hi) return 0;
    if (pin_hi == TOTK_PIN_UNBOUNDED) return 2;
    *out_lo = pin_lo;
    *out_hi = pin_hi;
    return 1;
}

typedef struct {
    _Atomic uint64_t epoch;
    _Atomic size_t live_n;
    _Atomic uint64_t serial_bloom[4];
} totk_pin_sync;

typedef struct {
    uint64_t serial[TOTK_PIN_M4_WAYS];
    uint64_t epoch[TOTK_PIN_M4_WAYS];
    uint64_t stamped_sub[TOTK_PIN_M4_WAYS];
    const void *tex_ptr[TOTK_PIN_M4_WAYS];
    uint8_t valid[TOTK_PIN_M4_WAYS];
    uint8_t next_way;
    uint8_t pad_[3];
} totk_pin_memo_set;

typedef struct {
    totk_pin_memo_set sets[TOTK_PIN_M4_SETS];
} totk_pin_memo4;

static inline uint32_t totk_pin_bloom_bit(uint64_t serial) {
    return (uint32_t)(((serial * 0x9E3779B97F4A7C15ull) >> 56) & 255u);
}

static inline uint32_t totk_pin_set_idx(uint64_t serial) {
    return (uint32_t)(((serial * 0x9E3779B97F4A7C15ull) >> 57) & (TOTK_PIN_M4_SETS - 1u));
}

static inline void totk_pin_wbegin(totk_pin_sync *s) {
    assert(s != NULL);
    uint64_t prev = atomic_fetch_add_explicit(&s->epoch, TOTK_PIN_WRITER_ONE, memory_order_acq_rel);
    assert((prev >> 48) < 0xFFFFu);
    (void)prev;
}

static inline void totk_pin_wend(totk_pin_sync *s) {
    assert(s != NULL);
    uint64_t prev = atomic_fetch_add_explicit(&s->epoch, TOTK_PIN_WEND_DELTA, memory_order_acq_rel);
    assert((prev >> 48) > 0u);
    (void)prev;
}

static inline void totk_pin_sync_note_insert(totk_pin_sync *s, uint64_t serial, size_t live_n) {
    assert(s != NULL);
    uint32_t b = totk_pin_bloom_bit(serial);
    atomic_fetch_or_explicit(&s->serial_bloom[b >> 6], 1ull << (b & 63u), memory_order_release);
    atomic_store_explicit(&s->live_n, live_n, memory_order_release);
}

static inline void totk_pin_sync_set_bloom(totk_pin_sync *s, const uint64_t w[4], size_t live_n) {
    assert(s != NULL && w != NULL);
    for (int k = 0; k < 4; k++) atomic_store_explicit(&s->serial_bloom[k], w[k], memory_order_release);
    atomic_store_explicit(&s->live_n, live_n, memory_order_release);
}

static inline void totk_pin_snap(const totk_pin_sync *s, uint32_t *w_out, uint64_t *ep_out) {
    assert(s != NULL && w_out != NULL && ep_out != NULL);
    uint64_t raw = atomic_load_explicit(&s->epoch, memory_order_acquire);
    *w_out = (uint32_t)(raw >> 48);
    *ep_out = raw & TOTK_PIN_EPOCH_MASK;
}

static inline const void *totk_pin_fast_get(totk_pin_memo4 **tls_slot, const totk_pin_sync *s,
                                            uint64_t serial, uint64_t cur_sub) {
    assert(tls_slot != NULL && s != NULL);
    totk_pin_memo4 *m = *tls_slot;
    if (!m) return NULL;
    uint64_t raw0 = atomic_load_explicit(&s->epoch, memory_order_acquire);
    uint32_t w0 = (uint32_t)(raw0 >> 48);
    uint64_t e0 = raw0 & TOTK_PIN_EPOCH_MASK;
    if (!TOTK_HLE_FAST_FAULT(54) && w0 != 0u) return NULL;
    const totk_pin_memo_set *st = &m->sets[totk_pin_set_idx(serial)];
    const void *hit = NULL;
    for (uint32_t w = 0; w < TOTK_PIN_M4_WAYS; w++) {
        if (!st->valid[w] || st->serial[w] != serial) continue;
        if (!TOTK_HLE_FAST_FAULT(54)) {
            if (st->epoch[w] != e0) continue;
            if (cur_sub >= st->stamped_sub[w] &&
                cur_sub - st->stamped_sub[w] >= (uint64_t)TOTK_PIN_REFRESH_SUBS) continue;
        }
        hit = st->tex_ptr[w];
        break;
    }
    if (!hit) return NULL;
    if (!TOTK_HLE_FAST_FAULT(54)) {
        if (atomic_load_explicit(&s->epoch, memory_order_acquire) != raw0) return NULL;
    }
    return hit;
}

static inline void totk_pin_fast_put(totk_pin_memo4 **tls_slot, const totk_pin_sync *s,
                                     uint32_t w0, uint64_t e0, uint64_t serial,
                                     const void *ptr, uint64_t sub) {
    assert(tls_slot != NULL && s != NULL);
    if (!ptr) return;
    totk_pin_memo4 *m = *tls_slot;
    if (!m) {
        m = (totk_pin_memo4 *)calloc(1, sizeof *m);
        if (!m) return;
        *tls_slot = m;
    }
    if (!TOTK_HLE_FAST_FAULT(54)) {
        if (w0 != 0u) return;
        if (atomic_load_explicit(&s->epoch, memory_order_acquire) != (e0 & TOTK_PIN_EPOCH_MASK)) return;
    }
    totk_pin_memo_set *st = &m->sets[totk_pin_set_idx(serial)];
    uint32_t slot = st->next_way & (TOTK_PIN_M4_WAYS - 1u);
    for (uint32_t w = 0; w < TOTK_PIN_M4_WAYS; w++) {
        if (!st->valid[w] || st->serial[w] == serial) { slot = w; break; }
    }
    st->serial[slot] = serial;
    st->epoch[slot] = e0;
    st->stamped_sub[slot] = sub;
    st->tex_ptr[slot] = ptr;
    st->valid[slot] = 1u;
    st->next_way = (uint8_t)((slot + 1u) & (TOTK_PIN_M4_WAYS - 1u));
}

static inline int totk_pin_serial_may_exist(const totk_pin_sync *s, uint64_t serial,
                                            const totk_pin_memo4 *tls) {
    assert(s != NULL);
    if (TOTK_HLE_FAST_FAULT(54)) return 1;
    uint64_t raw0 = atomic_load_explicit(&s->epoch, memory_order_acquire);
    uint32_t w0 = (uint32_t)(raw0 >> 48);
    uint64_t e0 = raw0 & TOTK_PIN_EPOCH_MASK;
    if (w0 != 0u) return -1;
    if (tls) {
        const totk_pin_memo_set *st = &tls->sets[totk_pin_set_idx(serial)];
        for (uint32_t w = 0; w < TOTK_PIN_M4_WAYS; w++) {
            if (st->valid[w] && st->serial[w] == serial && st->epoch[w] == e0) {
                return (atomic_load_explicit(&s->epoch, memory_order_acquire) == raw0) ? 1 : -1;
            }
        }
    }
    size_t n = atomic_load_explicit(&s->live_n, memory_order_acquire);
    int ans = -1;
    if (n == 0u) {
        ans = 0;
    } else {
        uint32_t b = totk_pin_bloom_bit(serial);
        uint64_t word = atomic_load_explicit(&s->serial_bloom[b >> 6], memory_order_acquire);
        if (!((word >> (b & 63u)) & 1ull)) ans = 0;
    }
    if (ans < 0) return -1;
    return (atomic_load_explicit(&s->epoch, memory_order_acquire) == raw0) ? ans : -1;
}

typedef struct {
    _Atomic uint32_t seq;
    _Atomic size_t live_ctl_n;
    _Atomic uintptr_t min_lo;
    _Atomic uintptr_t max_hi;
    _Atomic uint64_t bits[4];
    uint32_t ref[256];
} totk_ctl_copy_summary;

static inline uint32_t totk_ctl_page_bit(uintptr_t pg) {
    uint64_t h = (uint64_t)pg * 0x9E3779B97F4A7C15ull;
    return (uint32_t)((h >> 20) & 255u);
}

static inline void totk_ctl_copy_note_add(totk_ctl_copy_summary *s, uintptr_t lo, uintptr_t hi) {
    assert(s != NULL);
    if (hi <= lo) return;
    uint32_t seq = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_store_explicit(&s->seq, seq + 1u, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    size_t prev = atomic_load_explicit(&s->live_ctl_n, memory_order_relaxed);
    if (prev == 0u) {
        atomic_store_explicit(&s->min_lo, lo, memory_order_relaxed);
        atomic_store_explicit(&s->max_hi, hi, memory_order_relaxed);
    } else {
        uintptr_t ml = atomic_load_explicit(&s->min_lo, memory_order_relaxed);
        uintptr_t mh = atomic_load_explicit(&s->max_hi, memory_order_relaxed);
        if (lo < ml) atomic_store_explicit(&s->min_lo, lo, memory_order_relaxed);
        if (hi > mh) atomic_store_explicit(&s->max_hi, hi, memory_order_relaxed);
    }
    uintptr_t p0 = lo >> TOTK_CTL_PAGE_SHIFT;
    uintptr_t p1 = (hi - 1u) >> TOTK_CTL_PAGE_SHIFT;
    for (uintptr_t pg = p0, k = 0; pg <= p1 && k < 4u; pg++, k++) {
        uint32_t b = totk_ctl_page_bit(pg);
        if (s->ref[b]++ == 0u) {
            uint64_t cur = atomic_load_explicit(&s->bits[b >> 6], memory_order_relaxed);
            atomic_store_explicit(&s->bits[b >> 6], cur | (1ull << (b & 63u)), memory_order_relaxed);
        }
    }
    atomic_store_explicit(&s->live_ctl_n, prev + 1u, memory_order_relaxed);
    atomic_store_explicit(&s->seq, seq + 2u, memory_order_release);
}

static inline void totk_ctl_copy_note_drop(totk_ctl_copy_summary *s, uintptr_t lo, uintptr_t hi) {
    assert(s != NULL);
    if (hi <= lo) return;
    uint32_t seq = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_store_explicit(&s->seq, seq + 1u, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    uintptr_t p0 = lo >> TOTK_CTL_PAGE_SHIFT;
    uintptr_t p1 = (hi - 1u) >> TOTK_CTL_PAGE_SHIFT;
    for (uintptr_t pg = p0, k = 0; pg <= p1 && k < 4u; pg++, k++) {
        uint32_t b = totk_ctl_page_bit(pg);
        if (s->ref[b] > 0u && --s->ref[b] == 0u) {
            uint64_t cur = atomic_load_explicit(&s->bits[b >> 6], memory_order_relaxed);
            atomic_store_explicit(&s->bits[b >> 6], cur & ~(1ull << (b & 63u)), memory_order_relaxed);
        }
    }
    size_t prev = atomic_load_explicit(&s->live_ctl_n, memory_order_relaxed);
    size_t next = prev > 0u ? prev - 1u : 0u;
    atomic_store_explicit(&s->live_ctl_n, next, memory_order_relaxed);
    if (next == 0u) {
        atomic_store_explicit(&s->min_lo, UINTPTR_MAX, memory_order_relaxed);
        atomic_store_explicit(&s->max_hi, 0u, memory_order_relaxed);
    }
    atomic_store_explicit(&s->seq, seq + 2u, memory_order_release);
}

static inline int totk_ctl_copy_may_overlap(const totk_ctl_copy_summary *s, uintptr_t lo, uintptr_t hi) {
    assert(s != NULL);
    if (hi <= lo || TOTK_HLE_FAST_FAULT(55)) return 0;
    uint32_t seq = atomic_load_explicit(&s->seq, memory_order_acquire);
    if (seq & 1u) return 1;
    size_t n = atomic_load_explicit(&s->live_ctl_n, memory_order_relaxed);
    uintptr_t ml = atomic_load_explicit(&s->min_lo, memory_order_relaxed);
    uintptr_t mh = atomic_load_explicit(&s->max_hi, memory_order_relaxed);
    int disjoint = (n == 0u || hi <= ml || lo >= mh);
    if (!disjoint) {
        uintptr_t p0 = lo >> TOTK_CTL_PAGE_SHIFT;
        uintptr_t p1 = (hi - 1u) >> TOTK_CTL_PAGE_SHIFT;
        if (p1 - p0 < 8u) {
            int hit = 0;
            for (uintptr_t pg = p0; pg <= p1; pg++) {
                uint32_t b = totk_ctl_page_bit(pg);
                uint64_t w = atomic_load_explicit(&s->bits[b >> 6], memory_order_relaxed);
                if ((w >> (b & 63u)) & 1ull) { hit = 1; break; }
            }
            if (!hit) disjoint = 1;
        }
    }
    if (!disjoint) return 1;
    atomic_thread_fence(memory_order_acquire);
    return atomic_load_explicit(&s->seq, memory_order_relaxed) != seq;
}

#endif
