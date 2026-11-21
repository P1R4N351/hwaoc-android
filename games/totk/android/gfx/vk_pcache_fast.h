/* Fast PSO key validation, compact open-addressed index, O(1) demand queue & word bitmap (TOTK_VK_PCACHE_FAST=1). */
#ifndef TOTK_VK_PCACHE_FAST_H
#define TOTK_VK_PCACHE_FAST_H
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static _Atomic int g_totk_vk_pcache_fast_state;

static inline int totk_vk_pcache_fast_on(void) {
    int s = atomic_load_explicit(&g_totk_vk_pcache_fast_state, memory_order_acquire);
    if (!s) {
        const char *e = getenv("TOTK_VK_PCACHE_FAST");
        s = (e && *e == '0' && !e[1]) ? 1 : 2;
        atomic_store_explicit(&g_totk_vk_pcache_fast_state, s, memory_order_release);
    }
    return s == 2;
}

#ifdef TOTK_W34_TEST_HOOKS
static _Atomic int g_totk_w34_fault;
static inline void totk_vk_pcache_fast_reset(int forced) {
    atomic_store_explicit(&g_totk_vk_pcache_fast_state, forced ? 2 : 1, memory_order_release);
}
static inline void totk_w34_set_fault(int mode) {
    atomic_store_explicit(&g_totk_w34_fault, mode, memory_order_release);
}
static inline int totk_w34_fault(void) {
    return atomic_load_explicit(&g_totk_w34_fault, memory_order_acquire);
}
#else
static inline int totk_w34_fault(void) { return 0; }
#endif

static inline int totk_pso_hex_ok(unsigned char c) {
    static const uint64_t lut[4] = {
        UINT64_C(0x03ff000000000000), UINT64_C(0x0000007e00000000), 0u, 0u
    };
    return (int)((lut[c >> 6] >> (c & 63u)) & 1u);
}

static inline uint32_t totk_pso_rd32(const uint8_t *p) {
    uint32_t v = 0;
    memcpy(&v, p, sizeof v);
    return v;
}

static inline uint64_t totk_pso_rd64(const uint8_t *p) {
    uint64_t v = 0;
    memcpy(&v, p, sizeof v);
    return v;
}

static inline int totk_pso_zero80(const uint8_t *p) {
    uint64_t acc = 0;
    for (size_t i = 0; i < 80u; i += 8u) acc |= totk_pso_rd64(p + i);
    return acc == 0;
}

static inline int totk_pso_name_fast(const uint8_t *p, int optional, int fault_mode) {
    if (!p[0]) return optional ? totk_pso_zero80(p) : 0;
    for (size_t i = 0; i < 64u; i++) if (!totk_pso_hex_ok(p[i])) return 0;
    size_t tail_off = 64u;
    if (p[64] == '-') {
        for (size_t i = 65u; i < 77u; i++) if (!totk_pso_hex_ok(p[i])) return 0;
        tail_off = 77u;
    } else if (p[64]) {
        return 0;
    }
    if (fault_mode == 122) return 1;
    for (size_t i = tail_off; i < 80u; i++) if (p[i]) return 0;
    return 1;
}

static inline int totk_pso_valid_fast(const uint8_t *key, uint32_t freq, uint32_t cap, int fault_mode) {
    if (!totk_pso_name_fast(key, 0, fault_mode) || !totk_pso_name_fast(key + 80, 1, fault_mode) ||
        totk_pso_rd32(key + 160u) > 15u || !freq || cap > 1u) return 0;
    const uint8_t *s = key + 164u;
    uint32_t n = totk_pso_rd32(s + 40u), samples = totk_pso_rd32(s + 44u);
    if (n > 8u || samples > 64u) return 0;
    if (fault_mode != 123 && samples && (samples & (samples - 1u))) return 0;
    if (totk_pso_rd32(s + 48u) > 255u || totk_pso_rd32(s + 56u) > 1u || totk_pso_rd32(s + 60u) > 2u ||
        totk_pso_rd32(s + 64u) > 2u || totk_pso_rd32(s + 68u) > 1u || totk_pso_rd32(s + 72u) > 16u) return 0;
    for (unsigned i = 0; i < 8u; i++) {
        const uint8_t *b = s + (19u + 7u * i) * 4u;
        if (totk_pso_rd32(b) > 7u || totk_pso_rd32(b + 20u) > 7u || totk_pso_rd32(b + 24u) > 7u) return 0;
        for (unsigned j = 1u; j < 5u; j++) if (totk_pso_rd32(b + 4u * j) > 127u) return 0;
    }
    for (unsigned i = 0; i < 16u; i++) {
        if (totk_pso_rd32(s + (75u + 3u * i + 2u) * 4u) > 15u || totk_pso_rd32(s + (123u + 2u * i) * 4u) > 1u) return 0;
    }
    return 1;
}

typedef struct { uint64_t h; uint32_t idx_p1; } totk_pso_slot;
typedef struct { totk_pso_slot *slots; uint32_t n, cap; } totk_pso_idx;

static inline uint64_t totk_pso_hash64(const uint8_t *p, size_t n, int fault_mode) {
    if (fault_mode == 125) return UINT64_C(0x1111111111111111);
    uint64_t h = UINT64_C(0x9e3779b97f4a7c15) ^ ((uint64_t)n * UINT64_C(0xbf58476d1ce4e5b9));
    size_t i = 0;
    for (; i + 8u <= n; i += 8u) {
        h = (h ^ totk_pso_rd64(p + i)) * UINT64_C(0x9e3779b97f4a7c15);
        h ^= h >> 29;
    }
    for (; i < n; i++) h = (h ^ p[i]) * UINT64_C(0x100000001b3);
    h ^= h >> 30; h *= UINT64_C(0xbf58476d1ce4e5b9); h ^= h >> 27;
    return h ? h : UINT64_C(1);
}

static inline int totk_pso_idx_reserve(totk_pso_idx *m, uint32_t want) {
    uint32_t nc = 128u;
    while (nc < want * 2u && nc < (1u << 20)) nc <<= 1;
    if (nc <= m->cap) return 1;
    totk_pso_slot *ns = (totk_pso_slot *)calloc(nc, sizeof *ns);
    if (!ns) return 0;
    for (uint32_t i = 0; i < m->cap; i++) {
        if (!m->slots[i].idx_p1) continue;
        uint32_t j = (uint32_t)m->slots[i].h & (nc - 1u);
        while (ns[j].idx_p1) j = (j + 1u) & (nc - 1u);
        ns[j] = m->slots[i];
    }
    free(m->slots); m->slots = ns; m->cap = nc;
    return 1;
}

static inline uint32_t totk_pso_idx_get_raw(const totk_pso_idx *m, const uint8_t *recs,
                                            size_t rec_stride, const uint8_t *key,
                                            size_t klen, uint64_t h, int fault_mode) {
    if (!m->n || !m->cap || !m->slots) return 0u;
    uint32_t mask = m->cap - 1u, i = (uint32_t)h & mask;
    for (uint32_t step = 0; step < m->cap; step++) {
        const totk_pso_slot *s = &m->slots[i];
        if (!s->idx_p1) return 0u;
        const uint8_t *kptr = recs + (size_t)(s->idx_p1 - 1u) * rec_stride;
        if (s->h == h && (fault_mode == 125 || memcmp(kptr, key, klen) == 0)) return s->idx_p1;
        i = (i + 1u) & mask;
    }
    return 0u;
}

static inline int totk_pso_idx_put_raw(totk_pso_idx *m, const uint8_t *recs, size_t rec_stride,
                                       uint32_t idx_p1, uint64_t h) {
    if (!m->cap || 2u * (m->n + 1u) > m->cap) {
        if (!totk_pso_idx_reserve(m, m->n + 64u)) return 0;
    }
    (void)recs; (void)rec_stride;
    uint32_t mask = m->cap - 1u, i = (uint32_t)h & mask;
    while (m->slots[i].idx_p1) i = (i + 1u) & mask;
    m->slots[i] = (totk_pso_slot){h, idx_p1}; m->n++;
    return 1;
}

static inline void totk_pso_idx_fini(totk_pso_idx *m) { free(m->slots); memset(m, 0, sizeof *m); }

typedef struct totk_pw_link { struct totk_pw_link *prev, *next; void *ent; } totk_pw_link;

static inline void totk_pw_link_push(totk_pw_link **head, totk_pw_link **tail,
                                     totk_pw_link *node, void *ent) {
    node->next = NULL; node->prev = *tail; node->ent = ent;
    if (*tail) (*tail)->next = node; else *head = node;
    *tail = node;
}

static inline void totk_pw_link_unlink(totk_pw_link **head, totk_pw_link **tail,
                                       totk_pw_link *node, int fm) {
    if (node->prev) node->prev->next = node->next; else *head = node->next;
    if (node->next) node->next->prev = node->prev;
    else if (fm != 124) *tail = node->prev;
    node->prev = NULL; node->next = NULL; node->ent = NULL;
}

static inline int totk_pso_bitmap_dirty_fast(const uint8_t *cur, const uint8_t *saved,
                                             uint32_t total_bits, int fault_mode) {
    uint32_t bits = total_bits <= 16384u ? total_bits : 16384u;
    uint32_t full = bits >> 6, rem = bits & 63u;
    for (uint32_t i = 0; i < full; i++) {
        if (totk_pso_rd64(cur + i * 8u) != totk_pso_rd64(saved + i * 8u)) return 1;
    }
    if (rem && fault_mode != 126) {
        uint64_t mask = (UINT64_C(1) << rem) - 1u;
        uint64_t a = totk_pso_rd64(cur + full * 8u) & mask;
        uint64_t b = totk_pso_rd64(saved + full * 8u) & mask;
        if (a != b) return 1;
    }
    return 0;
}

static inline uint32_t totk_pso_popcount_bits(const uint8_t *buf, uint32_t total_bits, int fault_mode) {
    uint32_t bits = total_bits <= 16384u ? total_bits : 16384u;
    uint32_t full = bits >> 6, rem = bits & 63u, sum = 0;
    for (uint32_t i = 0; i < full; i++) sum += (uint32_t)__builtin_popcountll(totk_pso_rd64(buf + i * 8u));
    if (rem && fault_mode != 126) {
        uint64_t mask = (UINT64_C(1) << rem) - 1u;
        sum += (uint32_t)__builtin_popcountll(totk_pso_rd64(buf + full * 8u) & mask);
    }
    return sum;
}
#endif
