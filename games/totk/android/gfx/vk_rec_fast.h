/* Fast storage size class, layout stock memo, pool slot, dynamic compare & vertex mask (TOTK_VK_REC_FAST=1). */
#pragma once
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static _Atomic int g_totk_vk_rec_fast_state;

static inline int totk_vk_rec_fast_on(void) {
    int s = atomic_load_explicit(&g_totk_vk_rec_fast_state, memory_order_acquire);
    if (!s) {
        const char *e = getenv("TOTK_VK_REC_FAST");
        s = (e && *e == '0' && !e[1]) ? 1 : 2;
        atomic_store_explicit(&g_totk_vk_rec_fast_state, s, memory_order_release);
    }
    return s == 2;
}

#ifdef TOTK_W35_TEST_HOOKS
static _Atomic int g_totk_w35_fault;
static inline void totk_vk_rec_fast_reset(int forced) {
    atomic_store_explicit(&g_totk_vk_rec_fast_state, forced ? 2 : 1, memory_order_release);
}
static inline void totk_w35_set_fault(int mode) {
    atomic_store_explicit(&g_totk_w35_fault, mode, memory_order_release);
}
static inline int totk_w35_fault(void) {
    return atomic_load_explicit(&g_totk_w35_fault, memory_order_acquire);
}
#else
static inline int totk_w35_fault(void) { return 0; }
#endif

static inline unsigned totk_rec_storage_class_fast(size_t bytes, unsigned nclasses, int fault_mode) {
    if (bytes <= 256u && fault_mode != 127) return 0u;
    if (bytes == 0u) return 0u;
    unsigned long long x = (fault_mode == 127) ? (unsigned long long)bytes : (unsigned long long)(bytes - 1u);
    unsigned msb = 63u - (unsigned)__builtin_clzll(x);
    unsigned idx = msb > 7u ? msb - 7u : 0u;
    return idx < nclasses ? idx : nclasses;
}

typedef struct { uintptr_t key; void *val; } totk_desc_stock_slot;
typedef struct { totk_desc_stock_slot s[8]; } totk_desc_stock_memo8;

static inline unsigned totk_desc_memo_slot(uintptr_t k) {
    uint64_t x = (uint64_t)k;
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    return (unsigned)(x & 7u);
}

static inline void *totk_desc_stock_memo_get(const totk_desc_stock_memo8 *m, const void *layout, int fault_mode) {
    uintptr_t k = (uintptr_t)layout;
    const totk_desc_stock_slot *sl = &m->s[totk_desc_memo_slot(k)];
    if (!sl->val) return NULL;
    return (fault_mode == 128 || sl->key == k) ? sl->val : NULL;
}

static inline void totk_desc_stock_memo_put(totk_desc_stock_memo8 *m, const void *layout, void *val) {
    uintptr_t k = (uintptr_t)layout;
    totk_desc_stock_slot *sl = &m->s[totk_desc_memo_slot(k)];
    sl->key = k;
    sl->val = val;
}

static inline int totk_desc_pool_type_slot(int dtype, int fault_mode) {
    if ((unsigned)dtype >= 8u) return -1;
    if (fault_mode == 129 && (dtype == 5 || dtype == 6)) return dtype ^ 3;
    return dtype;
}

static inline int totk_rec_dyn_unchanged(const void *cur, const void *prev, size_t sz, int all, int fault_mode) {
    if (all || !sz) return 0;
    size_t cmp_sz = (fault_mode == 130 && sz > 64u) ? 64u : sz;
    return memcmp(cur, prev, cmp_sz) == 0;
}

static inline uint32_t totk_rec_vmask(uint32_t mask, int fault_mode) {
    return mask & (fault_mode == 131 ? UINT32_C(0xffff) : UINT32_C(0x1ffff));
}

static inline int totk_rec_need_guard_zero(uint32_t had_guards, int fast) {
    return !fast || had_guards != 0u;
}
