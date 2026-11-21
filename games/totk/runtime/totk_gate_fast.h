#pragma once
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define TOTK_GATE_MOD_WAYS 64u
#define TOTK_GATE_RETRIES 8

static inline int totk_gate_fast_enabled(void) {
    static _Atomic int on = -1;
    int v = atomic_load_explicit(&on, memory_order_relaxed);
    if (v < 0) {
        const char *e = getenv("TOTK_GATE_FAST");
        v = (e && e[0] == '0' && e[1] == '\0') ? 0 : 1;
        atomic_store_explicit(&on, v, memory_order_relaxed);
    }
    return v;
}

#ifdef TOTK_GATE_FAST_TEST_HOOKS
static _Atomic int g_totk_gate_fast_override = -1;

static inline void totk_gate_fast_reset_for_test(int en) {
    atomic_store_explicit(&g_totk_gate_fast_override, en, memory_order_relaxed);
}

static inline int totk_gate_fast_active(void) {
    int ov = atomic_load_explicit(&g_totk_gate_fast_override, memory_order_relaxed);
    return ov >= 0 ? ov : totk_gate_fast_enabled();
}

static inline int totk_gate_w26_fault(void) {
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
static inline int totk_gate_fast_active(void) { return totk_gate_fast_enabled(); }
static inline int totk_gate_w26_fault(void) { return 0; }
#endif

static inline unsigned totk_gate_hash64(uint64_t x, uint32_t mask) {
    uint64_t h = (x >> 2) * 0x9e3779b97f4a7c15ull;
    h ^= h >> 30;
    return (unsigned)(h & (uint64_t)mask);
}

typedef struct {
    uint64_t ret_abs;
    const void *gate;
    const char *mod_name;
} totk_gate_slot;

typedef struct {
    totk_gate_slot *slots;
    uint32_t mask;
    uint32_t count;
    _Atomic int ready;
} totk_gate_table;

static inline int totk_gate_table_init(totk_gate_table *t, uint32_t total_gates) {
    uint32_t cap = 4096u;
    while (cap < total_gates * 4u && cap < (1u << 22)) cap <<= 1u;
    t->slots = (totk_gate_slot *)calloc(cap, sizeof(totk_gate_slot));
    if (!t->slots) return 0;
    t->mask = cap - 1u;
    t->count = total_gates;
    atomic_store_explicit(&t->ready, 0, memory_order_relaxed);
    return 1;
}

static inline int totk_gate_table_insert(totk_gate_table *t, uint64_t ret_abs, const void *gate, const char *mod_name) {
    if (!t->slots || !ret_abs || !gate) return 0;
    uint32_t i = totk_gate_hash64(ret_abs, t->mask);
    for (uint32_t p = 0; p <= t->mask; p++) {
        totk_gate_slot *s = &t->slots[i];
        if (!s->ret_abs) {
            s->ret_abs = ret_abs;
            s->gate = gate;
            s->mod_name = mod_name;
            return 1;
        }
        if (s->ret_abs == ret_abs) return 0;
        i = (i + 1u) & t->mask;
    }
    return 0;
}

static inline void totk_gate_table_publish(totk_gate_table *t) {
    if (t->slots) atomic_store_explicit(&t->ready, 1, memory_order_release);
}

static inline const totk_gate_slot *totk_gate_table_find(const totk_gate_table *t, uint64_t ret_abs) {
    if (!ret_abs || !atomic_load_explicit(&t->ready, memory_order_acquire) || !t->slots) return NULL;
    int f = totk_gate_w26_fault();
    if (f == 82) return NULL;
    uint32_t i = totk_gate_hash64(ret_abs, t->mask);
    for (uint32_t p = 0; p <= t->mask; p++) {
        const totk_gate_slot *s = &t->slots[i];
        if (!s->ret_abs) return NULL;
        if (s->ret_abs == ret_abs || f == 83) return s;
        i = (i + 1u) & t->mask;
    }
    return NULL;
}

typedef struct {
    _Atomic uint64_t seq;
    _Atomic uint64_t chunk;
    _Atomic(const char *) name;
    _Atomic uint64_t base;
    _Atomic uint64_t end;
} totk_mod_slot;

typedef struct {
    _Atomic int ready;
    _Atomic uint64_t lo;
    _Atomic uint64_t hi;
    totk_mod_slot s[TOTK_GATE_MOD_WAYS];
} totk_mod_cache;

static inline void totk_mod_cache_publish(totk_mod_cache *c, uint64_t lo, uint64_t hi) {
    atomic_store_explicit(&c->lo, lo, memory_order_relaxed);
    atomic_store_explicit(&c->hi, hi, memory_order_relaxed);
    atomic_store_explicit(&c->ready, hi > lo ? 1 : 0, memory_order_release);
}

static inline int totk_mod_cache_try(const totk_mod_cache *c, uint64_t pc, const char **name, uint64_t *off, int *miss) {
    *miss = 1;
    if (!atomic_load_explicit(&c->ready, memory_order_acquire)) return 0;
    uint64_t lo = atomic_load_explicit(&c->lo, memory_order_relaxed);
    uint64_t hi = atomic_load_explicit(&c->hi, memory_order_relaxed);
    int f = totk_gate_w26_fault();
    if (pc < lo || pc >= hi || f == 84) {
        *miss = 0;
        return 0;
    }
    uint64_t ch = pc >> 16;
    const totk_mod_slot *s = &c->s[(unsigned)(ch * 0x9e3779b97f4a7c15ull >> 58) & (TOTK_GATE_MOD_WAYS - 1u)];
    for (int r = 0; r < TOTK_GATE_RETRIES; r++) {
        uint64_t s1 = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (!s1 || (s1 & 1ull)) continue;
        uint64_t sc = atomic_load_explicit(&s->chunk, memory_order_relaxed);
        const char *sn = atomic_load_explicit(&s->name, memory_order_relaxed);
        uint64_t sb = atomic_load_explicit(&s->base, memory_order_relaxed);
        uint64_t se = atomic_load_explicit(&s->end, memory_order_relaxed);
        uint64_t s2 = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (s1 == s2 && sc == ch && sn && (f == 85 || (pc >= sb && pc < se))) {
            *name = sn;
            *off = pc - sb;
            return 1;
        }
        return 0;
    }
    return 0;
}

static inline void totk_mod_cache_write(totk_mod_cache *c, uint64_t pc, const char *name, uint64_t base, uint64_t end) {
    if (!name || !atomic_load_explicit(&c->ready, memory_order_acquire)) return;
    uint64_t ch = pc >> 16;
    totk_mod_slot *s = &c->s[(unsigned)(ch * 0x9e3779b97f4a7c15ull >> 58) & (TOTK_GATE_MOD_WAYS - 1u)];
    uint64_t q = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_store_explicit(&s->seq, (q + 1u) | 1ull, memory_order_release);
    atomic_store_explicit(&s->chunk, ch, memory_order_relaxed);
    atomic_store_explicit(&s->name, name, memory_order_relaxed);
    atomic_store_explicit(&s->base, base, memory_order_relaxed);
    atomic_store_explicit(&s->end, end, memory_order_relaxed);
    atomic_store_explicit(&s->seq, (q + 2u) & ~1ull, memory_order_release);
}
