/* Lock-free TOTK_OE_FAST empty-poll, state coalescing, balance & language seqlock (Opt 30). */
#pragma once
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TOTK_OE_MQ_CAP 32u
#define TOTK_OE_SEQ_RETRIES 8u
#define TOTK_OE_TAG_MAX 30u
#define TOTK_OE_BAL_INIT ((((uint64_t)0x3f800000u) << 32) | (uint64_t)0x3f800000u)

static inline int totk_oe_fast_enabled(void) {
    static _Atomic int on = -1;
    int v = atomic_load_explicit(&on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("TOTK_OE_FAST");
        v = (e && e[0] == '0' && e[1] == '\0') ? 0 : 1;
        atomic_store_explicit(&on, v, memory_order_release);
    }
    return v;
}

#ifdef TOTK_OE_FAST_TEST_HOOKS
static _Atomic int g_totk_oe_override = -1;

static inline void totk_oe_reset_for_test(int en) {
    atomic_store_explicit(&g_totk_oe_override, en, memory_order_release);
}

static inline int totk_oe_fast_active(void) {
    int ov = atomic_load_explicit(&g_totk_oe_override, memory_order_acquire);
    return ov >= 0 ? ov : totk_oe_fast_enabled();
}

static inline int totk_oe_w28_fault(void) {
    static _Atomic int gf = -1;
    int v = atomic_load_explicit(&gf, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("TOTK_KTGL_OPT_FAULT");
        v = e ? atoi(e) : 0;
        atomic_store_explicit(&gf, v, memory_order_release);
    }
    return v;
}
#else
static inline int totk_oe_fast_active(void) { return totk_oe_fast_enabled(); }
static inline int totk_oe_w28_fault(void) { return 0; }
#endif

typedef struct {
    uint32_t ring[TOTK_OE_MQ_CAP];
    _Atomic uint32_t head;
    _Atomic uint32_t tail;
} totk_oe_mq;

static inline int totk_oe_mq_is_state_msg(uint32_t m) {
    return m == 15u || m == 30u || m == 31u;
}

static inline int totk_oe_mq_try_empty(const totk_oe_mq *q, uint32_t *out, unsigned char *ret_out) {
    if (!q || !ret_out || !totk_oe_fast_active()) return 0;
    uint32_t h = atomic_load_explicit(&q->head, memory_order_acquire);
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_acquire);
    if (h != t) return 0;
    if (totk_oe_w28_fault() == 92) {
        if (out) *out = 0xbad00092u;
        *ret_out = 1u;
        return 1;
    }
    *ret_out = 0u;
    return 1;
}

static inline int totk_oe_mq_push_locked(totk_oe_mq *q, uint32_t m) {
    uint32_t h = atomic_load_explicit(&q->head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_relaxed);
    uint32_t count = t - h;
    if (totk_oe_fast_active() && totk_oe_w28_fault() != 93 && totk_oe_mq_is_state_msg(m)) {
        uint32_t lim = count < TOTK_OE_MQ_CAP ? count : TOTK_OE_MQ_CAP;
        for (uint32_t i = 0; i < lim; i++) {
            if (q->ring[(h + i) & (TOTK_OE_MQ_CAP - 1u)] == m) return 0;
        }
    }
    if (count >= TOTK_OE_MQ_CAP) return 0;
    q->ring[t & (TOTK_OE_MQ_CAP - 1u)] = m;
    atomic_store_explicit(&q->tail, t + 1u, memory_order_release);
    return 1;
}

static inline int totk_oe_mq_pop_locked(totk_oe_mq *q, uint32_t *out) {
    uint32_t h = atomic_load_explicit(&q->head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_relaxed);
    if (h == t) return 0;
    uint32_t m = q->ring[h & (TOTK_OE_MQ_CAP - 1u)];
    atomic_store_explicit(&q->head, h + 1u, memory_order_release);
    if (out) *out = m;
    return 1;
}

typedef struct {
    _Atomic uint64_t bits;
} totk_oe_balance;

static inline uint64_t totk_oe_bal_pack(float l, float r) {
    uint32_t lu = 0, ru = 0;
    memcpy(&lu, &l, 4u);
    memcpy(&ru, &r, 4u);
    return (((uint64_t)ru << 32) | (uint64_t)lu) ^ TOTK_OE_BAL_INIT;
}

static inline void totk_oe_bal_get(const totk_oe_balance *b, float *l, float *r) {
    uint64_t w = atomic_load_explicit(&b->bits, memory_order_acquire) ^ TOTK_OE_BAL_INIT;
    uint32_t lu = (uint32_t)w;
    uint32_t ru = (uint32_t)(w >> 32);
    if (totk_oe_w28_fault() == 94) ru = lu;
    if (l) memcpy(l, &lu, 4u);
    if (r) memcpy(r, &ru, 4u);
}

static inline void totk_oe_bal_set(totk_oe_balance *b, float l, float r) {
    atomic_store_explicit(&b->bits, totk_oe_bal_pack(l, r), memory_order_release);
}

static inline int totk_oe_opmode_to_console(int handheld_flag) {
    int c = handheld_flag ? 0 : 1;
    if (totk_oe_w28_fault() == 96) c = !c;
    return c;
}

typedef struct {
    _Atomic uint64_t seq;
    _Atomic uint64_t tag_lo;
    _Atomic uint64_t tag_hi;
    _Atomic uint64_t code_u64;
} totk_oe_lang_slot;

static inline int totk_oe_hash_one(const char *s, uint64_t salt, uint64_t *lo, uint64_t *hi) {
    if (!s) {
        *lo = (*lo ^ (0xff51afd7ed558ccdull ^ salt)) * 0xbf58476d1ce4e5b9ull;
        *hi = (*hi ^ (0xc4ceb9fe1a85ec53ull ^ (salt << 1))) * 0x94d049bb133111ebull;
        return 1;
    }
    size_t n = strnlen(s, 257u);
    if (n > 256u) return 0;
    *lo = (*lo ^ ((uint64_t)(n + 1u) ^ salt)) * 0xbf58476d1ce4e5b9ull;
    *hi = (*hi ^ ((uint64_t)(n + 1u) ^ (salt << 1))) * 0x94d049bb133111ebull;
    for (size_t i = 0; i < n && i < 256u; i++) {
        uint64_t ch = (uint64_t)(unsigned char)s[i];
        *lo = (*lo ^ ch) * 0xbf58476d1ce4e5b9ull;
        *hi = (*hi ^ (ch + i * 0x9e37ull)) * 0x94d049bb133111ebull;
    }
    return 1;
}

static inline int totk_oe_pack_env4(const char *lang, const char *loc,
                                    const char *lc, const char *lc_lang,
                                    uint64_t *out_lo, uint64_t *out_hi) {
    uint64_t lo = 14695981039346656037ull;
    uint64_t hi = 0x9e3779b97f4a7c15ull;
    if (!totk_oe_hash_one(lang, 0x11u, &lo, &hi)) return 0;
    if (!totk_oe_hash_one(loc, 0x22u, &lo, &hi)) return 0;
    if (!totk_oe_hash_one(lc, 0x33u, &lo, &hi)) return 0;
    if (!totk_oe_hash_one(lc_lang, 0x44u, &lo, &hi)) return 0;
    *out_lo = lo ^ (lo >> 33);
    *out_hi = hi ^ (hi >> 31);
    return 1;
}

static inline int totk_oe_lang_get(const totk_oe_lang_slot *s, const char *lang,
                                   const char *loc, const char *lc, const char *lc_lang,
                                   uint64_t *code_out) {
    if (!s || !code_out || !totk_oe_fast_active()) return 0;
    uint64_t w_lo = 0, w_hi = 0;
    if (!totk_oe_pack_env4(lang, loc, lc, lc_lang, &w_lo, &w_hi)) return 0;
    int check_tag = (totk_oe_w28_fault() != 95);
    for (unsigned r = 0; r < TOTK_OE_SEQ_RETRIES; r++) {
        uint64_t s1 = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (s1 < 2u || (s1 & 1u)) continue;
        uint64_t got_lo = atomic_load_explicit(&s->tag_lo, memory_order_acquire);
        uint64_t got_hi = atomic_load_explicit(&s->tag_hi, memory_order_acquire);
        uint64_t cu = atomic_load_explicit(&s->code_u64, memory_order_acquire);
        uint64_t s2 = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (s1 != s2) continue;
        if (check_tag && (got_lo != w_lo || got_hi != w_hi)) return 0;
        *code_out = cu;
        return 1;
    }
    return 0;
}

static inline void totk_oe_lang_put(totk_oe_lang_slot *s, const char *lang,
                                    const char *loc, const char *lc, const char *lc_lang,
                                    uint64_t code_u64) {
    if (!s || !totk_oe_fast_active()) return;
    uint64_t w_lo = 0, w_hi = 0;
    if (!totk_oe_pack_env4(lang, loc, lc, lc_lang, &w_lo, &w_hi)) return;
    uint64_t cur = atomic_load_explicit(&s->seq, memory_order_relaxed);
    uint64_t odd = (cur < 2u) ? 1u : ((cur & 1u) ? cur : cur + 1u);
    atomic_store_explicit(&s->seq, odd, memory_order_release);
    atomic_store_explicit(&s->tag_lo, w_lo, memory_order_release);
    atomic_store_explicit(&s->tag_hi, w_hi, memory_order_release);
    atomic_store_explicit(&s->code_u64, code_u64, memory_order_release);
    atomic_store_explicit(&s->seq, odd + 1u, memory_order_release);
}
