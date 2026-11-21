/* Fast seed pack validator, open-addressed index, O(1) warm queue & ready check (TOTK_VK_SEED_FAST=1). */
#ifndef TOTK_VK_SEED_FAST_H
#define TOTK_VK_SEED_FAST_H
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static _Atomic int g_totk_vk_seed_fast_state;

static inline int totk_vk_seed_fast_on(void) {
    int s = atomic_load_explicit(&g_totk_vk_seed_fast_state, memory_order_acquire);
    if (!s) {
        const char *e = getenv("TOTK_VK_SEED_FAST");
        s = (e && *e == '0' && !e[1]) ? 1 : 2;
        atomic_store_explicit(&g_totk_vk_seed_fast_state, s, memory_order_release);
    }
    return s == 2;
}

#ifdef TOTK_W33_TEST_HOOKS
static _Atomic int g_totk_w33_fault;
static inline void totk_vk_seed_fast_reset(int forced) {
    atomic_store_explicit(&g_totk_vk_seed_fast_state, forced ? 2 : 1, memory_order_release);
}
static inline void totk_w33_set_fault(int mode) {
    atomic_store_explicit(&g_totk_w33_fault, mode, memory_order_release);
}
static inline int totk_w33_fault(void) {
    return atomic_load_explicit(&g_totk_w33_fault, memory_order_acquire);
}
#else
static inline int totk_w33_fault(void) { return 0; }
#endif

typedef struct { uint64_t begin, end; } totk_seed_range;
typedef struct { uint64_t h; void *val; const char *key; uint8_t len; } totk_seed_slot;
typedef struct { totk_seed_slot *slots; uint32_t n, cap; } totk_seed_idx;

static inline int totk_seed_char_ok(unsigned char c) {
    static const uint64_t k_lut[4] = {
        UINT64_C(0x03ff600000000000), UINT64_C(0x07fffffe87fffffe),
        UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000)
    };
    return (int)((k_lut[c >> 6] >> (c & 63u)) & 1u);
}

static inline int totk_seed_suffix_ok(const char *s, size_t n) {
    return (n > 4u && !memcmp(s + n - 4u, ".spv", 4u)) ||
           (n > 3u && (!memcmp(s + n - 3u, ".vk", 3u) || !memcmp(s + n - 3u, ".vv", 3u)));
}

static inline int totk_seed_valid_cstr_fast(const char *s, size_t *out_len) {
    if (!s) return 0;
    size_t n = 0;
    while (n < 84u && s[n]) {
        if (!totk_seed_char_ok((unsigned char)s[n])) return 0;
        n++;
    }
    if (!n || n > 83u || !totk_seed_suffix_ok(s, n)) return 0;
    if (out_len) *out_len = n;
    return 1;
}

static inline int totk_seed_valid_name_fast(const unsigned char *name, size_t *length, int fault_mode) {
    if (!name || !length) return 0;
    size_t n = 0;
    while (n < 84u && name[n]) {
        if (!totk_seed_char_ok(name[n])) return 0;
        n++;
    }
    if (!n || n > 83u || !totk_seed_suffix_ok((const char *)name, n)) return 0;
    if (fault_mode != 117) {
        size_t i = n + 1u;
        for (; i + 8u <= 84u; i += 8u) {
            uint64_t w = 0;
            memcpy(&w, name + i, sizeof w);
            if (w) return 0;
        }
        for (; i < 84u; i++) if (name[i]) return 0;
    }
    *length = n;
    return 1;
}

static inline int totk_seed_range_cmp(const void *a, const void *b) {
    const totk_seed_range *x = (const totk_seed_range *)a;
    const totk_seed_range *y = (const totk_seed_range *)b;
    if (x->begin != y->begin) return x->begin < y->begin ? -1 : 1;
    if (x->end != y->end) return x->end < y->end ? -1 : 1;
    return 0;
}

static inline uint32_t totk_seed_rd32(const unsigned char *p) {
    uint32_t v = 0;
    memcpy(&v, p, sizeof v);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
    v = __builtin_bswap32(v);
#endif
    return v;
}

static inline uint64_t totk_seed_rd64(const unsigned char *p) {
    uint64_t v = 0;
    memcpy(&v, p, sizeof v);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
    v = __builtin_bswap64(v);
#endif
    return v;
}

static inline int totk_seed_check_overlaps(const unsigned char *b, uint64_t count, uint64_t non_empty) {
    if (non_empty <= 1u) return 0;
    if (non_empty > SIZE_MAX / sizeof(totk_seed_range)) return -2;
    totk_seed_range *r = (totk_seed_range *)malloc((size_t)non_empty * sizeof *r);
    if (!r) return -1;
    uint64_t k = 0;
    for (uint64_t i = 0; i < count; i++) {
        const unsigned char *rec = b + 32u + (size_t)i * 104u;
        uint64_t off = totk_seed_rd64(rec + 88u), len = totk_seed_rd64(rec + 96u);
        if (len && k < non_empty) { r[k].begin = off; r[k].end = off + len; k++; }
    }
    qsort(r, (size_t)k, sizeof *r, totk_seed_range_cmp);
    int bad = 0;
    for (uint64_t i = 1u; i < k; i++) if (r[i].begin < r[i - 1u].end) { bad = -2; break; }
    free(r);
    return bad;
}

static inline int totk_seed_pack_scan_fast(const unsigned char *b, uint64_t count,
                                           size_t file_size, uint64_t index_end64, int fault_mode) {
    const unsigned char *prev = NULL;
    size_t prev_len = 0;
    uint64_t prev_end = 0, non_empty = 0;
    int monotonic = 1;
    for (uint64_t i = 0; i < count; i++) {
        const unsigned char *rec = b + 32u + (size_t)i * 104u;
        size_t nlen = 0;
        if (!totk_seed_valid_name_fast(rec, &nlen, fault_mode)) return -2;
        if (prev) {
            size_t common = prev_len < nlen ? prev_len : nlen;
            int order = memcmp(prev, rec, common);
            if (order > 0 || (order == 0 && prev_len >= nlen)) return -2;
        }
        prev = rec; prev_len = nlen;
        uint64_t off = totk_seed_rd64(rec + 88u), len = totk_seed_rd64(rec + 96u);
        if (totk_seed_rd32(rec + 84u) || (off & 7u) || off < index_end64 ||
            off > (uint64_t)file_size || len > UINT64_MAX - off || off + len > (uint64_t)file_size) return -2;
        if (len) {
            if (off < prev_end) monotonic = 0; else prev_end = off + len;
            non_empty++;
        }
    }
    if (fault_mode == 118) monotonic = 1;
    return monotonic ? 0 : totk_seed_check_overlaps(b, count, non_empty);
}

static inline uint64_t totk_seed_hash(const char *s, size_t len, int fault_mode) {
    if (fault_mode == 119) return (uint64_t)(unsigned char)s[0] | UINT64_C(1);
    uint64_t h = UINT64_C(0x9e3779b97f4a7c15) ^ ((uint64_t)len * UINT64_C(0xbf58476d1ce4e5b9));
    size_t i = 0;
    for (; i + 8u <= len; i += 8u) {
        uint64_t w = 0;
        memcpy(&w, s + i, sizeof w);
        h = (h ^ w) * UINT64_C(0x9e3779b97f4a7c15);
        h ^= h >> 29;
    }
    if (i + 4u <= len) {
        uint32_t w4 = 0;
        memcpy(&w4, s + i, sizeof w4);
        h = (h ^ w4) * UINT64_C(0xbf58476d1ce4e5b9);
        i += 4u;
    }
    for (; i < len; i++) h = (h ^ (uint64_t)(unsigned char)s[i]) * UINT64_C(1099511628211);
    h ^= h >> 30;
    h *= UINT64_C(0x94d049bb133111eb);
    h ^= h >> 31;
    return h | UINT64_C(1);
}

static inline int totk_seed_idx_reserve(totk_seed_idx *m, uint32_t min_items) {
    uint32_t need = (uint32_t)(((uint64_t)min_items * 4u) / 3u + 4u);
    uint32_t cap = m->cap ? m->cap : 64u;
    for (unsigned s = 0; s < 26u && cap < need; s++) cap <<= 1u;
    if (cap < need) cap = need;
    if (cap <= m->cap && m->slots) return 1;
    totk_seed_slot *ns = (totk_seed_slot *)calloc(cap, sizeof *ns);
    if (!ns) return 0;
    uint32_t mask = cap - 1u;
    for (uint32_t i = 0; i < m->cap; i++) {
        if (!m->slots[i].val) continue;
        uint32_t j = (uint32_t)m->slots[i].h & mask;
        for (uint32_t st = 0; st < cap; st++, j = (j + 1u) & mask) {
            if (!ns[j].val) { ns[j] = m->slots[i]; break; }
        }
    }
    free(m->slots); m->slots = ns; m->cap = cap;
    return 1;
}

static inline void *totk_seed_idx_get(const totk_seed_idx *m, const char *key,
                                       size_t len, uint64_t h, int fault_mode) {
    if (!m->n || !m->cap || !m->slots) return NULL;
    uint32_t mask = m->cap - 1u, i = (uint32_t)h & mask;
    for (uint32_t st = 0; st < m->cap; st++, i = (i + 1u) & mask) {
        const totk_seed_slot *s = &m->slots[i];
        if (!s->val) return NULL;
        if (s->h == h && (fault_mode == 119 || (s->len == (uint8_t)len && !memcmp(s->key, key, len)))) {
            return s->val;
        }
    }
    return NULL;
}

static inline void totk_seed_idx_put_unique(totk_seed_idx *m, const char *key,
                                            size_t len, uint64_t h, void *val) {
    if ((m->n + 1u) * 4u > m->cap * 3u && !totk_seed_idx_reserve(m, m->n + 16u)) abort();
    uint32_t mask = m->cap - 1u, i = (uint32_t)h & mask;
    for (uint32_t st = 0; st < m->cap; st++, i = (i + 1u) & mask) {
        if (!m->slots[i].val) {
            m->slots[i] = (totk_seed_slot){h, val, key, (uint8_t)len};
            m->n++;
            return;
        }
    }
    abort();
}

static inline void totk_seed_idx_fini(totk_seed_idx *m) {
    free(m->slots);
    m->slots = NULL; m->n = 0; m->cap = 0;
}

typedef struct totk_seed_warm_link {
    struct totk_seed_warm_link *next, *prev;
    void *item;
} totk_seed_warm_link;

static inline void totk_seed_warm_link_push(totk_seed_warm_link **head, totk_seed_warm_link **tail,
                                            totk_seed_warm_link *node, void *item) {
    node->next = NULL; node->prev = *tail; node->item = item;
    if (*tail) (*tail)->next = node; else *head = node;
    *tail = node;
}

static inline void *totk_seed_warm_link_pop(totk_seed_warm_link **head, totk_seed_warm_link **tail) {
    totk_seed_warm_link *h = *head;
    if (!h) return NULL;
    *head = h->next;
    if (*head) (*head)->prev = NULL; else *tail = NULL;
    void *item = h->item;
    h->next = NULL; h->prev = NULL; h->item = NULL;
    return item;
}

static inline void totk_seed_warm_link_unlink(totk_seed_warm_link **head, totk_seed_warm_link **tail,
                                              totk_seed_warm_link *node, int fm) {
    if (fm == 120) return;
    if (node->prev) node->prev->next = node->next; else *head = node->next;
    if (node->next) node->next->prev = node->prev; else *tail = node->prev;
    node->next = NULL; node->prev = NULL; node->item = NULL;
}

static inline int totk_seed_ready_decode(int fast_state, int fault_mode) {
    if (fault_mode == 121) return 0;
    return fast_state == 2;
}
#endif
