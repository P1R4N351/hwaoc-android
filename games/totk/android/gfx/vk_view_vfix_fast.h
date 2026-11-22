// Fast O(1) forward/reverse image-view maps, vfix sparse map, generation-stamped pso-life set & clear-pipe map (Opt 53).
#ifndef VK_VIEW_VFIX_FAST_H
#define VK_VIEW_VFIX_FAST_H
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TOTK_W51_TEST_HOOKS
extern _Atomic int g_totk_vk_view_vfix_fast_override;
extern _Atomic int g_totk_w51_fault;
static inline void totk_vk_view_vfix_fast_set_for_test(int v) {
    atomic_store_explicit(&g_totk_vk_view_vfix_fast_override, v, memory_order_release);
}
static inline void totk_w51_set_fault(int f) {
    atomic_store_explicit(&g_totk_w51_fault, f, memory_order_release);
}
static inline int totk_w51_parse_fault(void) {
    const char *e = getenv("TOTK_W51_FAULT");
    if (e && *e) return atoi(e);
    const char *k = getenv("TOTK_KTGL_OPT_FAULT");
    if (k && *k) {
        int kv = atoi(k);
        if (kv >= 201 && kv <= 205) return kv - 200;
    }
    return 0;
}
static inline int totk_w51_fault_mode(void) {
    int f = atomic_load_explicit(&g_totk_w51_fault, memory_order_relaxed);
    if (f >= 0) return f;
    int v = totk_w51_parse_fault();
    atomic_store_explicit(&g_totk_w51_fault, v, memory_order_relaxed);
    return v;
}
#else
static inline int totk_w51_fault_mode(void) { return 0; }
#endif

static inline int totk_vk_view_vfix_fast_on(void) {
#ifdef TOTK_W51_TEST_HOOKS
    int ov = atomic_load_explicit(&g_totk_vk_view_vfix_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
#endif
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("TOTK_VK_VIEW_VFIX_FAST");
        v = !(e && strcmp(e, "0") == 0);
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
}

static inline uint64_t totk_vv_mix64(uint64_t x) {
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

enum { TOTK_VKM_CAP = 8192u, TOTK_VKM_MASK = TOTK_VKM_CAP - 1u };

typedef struct {
    uint64_t view_ep, k0, k1, view_u;
    uint32_t tgen, pad_;
} totk_view_kv_slot_t;

typedef struct {
    totk_view_kv_slot_t slots[TOTK_VKM_CAP];
    uint32_t tgen, inserted;
} totk_view_kv_map_t;

static inline uint32_t totk_vkm_hash(uint64_t view_ep, uint64_t k0, uint64_t k1) {
    uint64_t h = totk_vv_mix64(view_ep ^ totk_vv_mix64(k0));
    return (uint32_t)totk_vv_mix64(h ^ k1) & TOTK_VKM_MASK;
}

static inline int totk_view_kv_map_find(const totk_view_kv_map_t *m, uint64_t view_ep,
                                        uint64_t k0, uint64_t k1, uint64_t *view_out) {
    if (!m->tgen || !view_ep) return 0;
    uint32_t slot = totk_vkm_hash(view_ep, k0, k1);
    int f1 = totk_w51_fault_mode() == 1;
    for (uint32_t probe = 0u; probe < TOTK_VKM_CAP; probe++) {
        const totk_view_kv_slot_t *e = &m->slots[slot];
        if (e->tgen != m->tgen) return 0;
        if (e->view_ep == view_ep && e->k0 == k0 &&
            (f1 ? ((uint32_t)e->k1 == (uint32_t)k1) : (e->k1 == k1))) {
            *view_out = e->view_u;
            return 1;
        }
        slot = (slot + 1u) & TOTK_VKM_MASK;
    }
    return 0;
}

static inline uint32_t totk_view_kv_map_put(totk_view_kv_map_t *m, uint64_t view_ep,
                                            uint64_t k0, uint64_t k1, uint64_t view_u) {
    if (!view_ep || !view_u) return 0u;
    if (!m->tgen) m->tgen = 1u;
    if (m->inserted >= TOTK_VKM_CAP - 1024u) {
        m->tgen++;
        m->inserted = 0u;
        if (!m->tgen) { memset(m->slots, 0, sizeof m->slots); m->tgen = 1u; }
    }
    uint32_t slot = totk_vkm_hash(view_ep, k0, k1);
    for (uint32_t probe = 0u; probe < TOTK_VKM_CAP; probe++) {
        totk_view_kv_slot_t *e = &m->slots[slot];
        if (e->tgen != m->tgen) {
            e->view_ep = view_ep; e->k0 = k0; e->k1 = k1;
            e->view_u = view_u; e->tgen = m->tgen;
            m->inserted++;
            return m->tgen;
        }
        if (e->view_ep == view_ep && e->k0 == k0 && e->k1 == k1) {
            e->view_u = view_u;
            return m->tgen;
        }
        slot = (slot + 1u) & TOTK_VKM_MASK;
    }
    return 0u;
}

typedef struct {
    uint64_t view_ep, view_u, k0, k1;
    uint32_t tgen, pad_;
} totk_view_rev_slot_t;

typedef struct {
    totk_view_rev_slot_t slots[TOTK_VKM_CAP];
    uint32_t tgen, inserted;
} totk_view_rev_map_t;

static inline uint32_t totk_vrm_hash(uint64_t view_ep, uint64_t view_u) {
    return (uint32_t)totk_vv_mix64(view_ep ^ totk_vv_mix64(view_u)) & TOTK_VKM_MASK;
}

static inline int totk_view_rev_map_find(const totk_view_rev_map_t *m, uint64_t view_ep,
                                         uint64_t view_u, uint64_t *k0_out, uint64_t *k1_out) {
    if (!m->tgen || !view_ep || !view_u) return 0;
    uint32_t slot = totk_vrm_hash(view_ep, view_u);
    int f2 = totk_w51_fault_mode() == 2;
    for (uint32_t probe = 0u; probe < TOTK_VKM_CAP; probe++) {
        const totk_view_rev_slot_t *e = &m->slots[slot];
        if (e->tgen != m->tgen) return 0;
        if (e->view_ep == view_ep && e->view_u == view_u) {
            *k0_out = f2 ? (uint64_t)(uint32_t)e->k0 : e->k0;
            *k1_out = e->k1;
            return 1;
        }
        slot = (slot + 1u) & TOTK_VKM_MASK;
    }
    return 0;
}

static inline uint32_t totk_view_rev_map_put(totk_view_rev_map_t *m, uint64_t view_ep,
                                             uint64_t view_u, uint64_t k0, uint64_t k1) {
    if (!view_ep || !view_u) return 0u;
    if (!m->tgen) m->tgen = 1u;
    if (m->inserted >= TOTK_VKM_CAP - 1024u) {
        m->tgen++;
        m->inserted = 0u;
        if (!m->tgen) { memset(m->slots, 0, sizeof m->slots); m->tgen = 1u; }
    }
    uint32_t slot = totk_vrm_hash(view_ep, view_u);
    for (uint32_t probe = 0u; probe < TOTK_VKM_CAP; probe++) {
        totk_view_rev_slot_t *e = &m->slots[slot];
        if (e->tgen != m->tgen) {
            e->view_ep = view_ep; e->view_u = view_u; e->k0 = k0; e->k1 = k1; e->tgen = m->tgen;
            m->inserted++;
            return m->tgen;
        }
        if (e->view_ep == view_ep && e->view_u == view_u) {
            e->k0 = k0; e->k1 = k1;
            return m->tgen;
        }
        slot = (slot + 1u) & TOTK_VKM_MASK;
    }
    return 0u;
}

static inline void totk_pack_vfix_sparse(uint32_t missing, const uint8_t fin_kind[32], uint8_t kind_out[32]) {
    memset(kind_out, 0, 32u);
    for (uint32_t m = missing; m; m &= m - 1u) {
        unsigned l = (unsigned)__builtin_ctz(m);
        kind_out[l] = fin_kind[l];
    }
}

enum { TOTK_VFIX_MAP_CAP = 2048u, TOTK_VFIX_MAP_MASK = TOTK_VFIX_MAP_CAP - 1u };

typedef struct {
    uint64_t vs_u, kpack, mod_u;
    uint32_t missing;
    uint8_t used, pad[3];
} totk_vfix_map_slot_t;

typedef struct {
    totk_vfix_map_slot_t slots[TOTK_VFIX_MAP_CAP];
    uint32_t inserted;
    uint8_t partial;
} totk_vfix_map_t;

static inline uint32_t totk_vfix_map_hash(uint64_t vs_u, uint32_t missing, uint64_t kpack) {
    uint64_t h = totk_vv_mix64(vs_u ^ ((uint64_t)missing << 32));
    return (uint32_t)totk_vv_mix64(h ^ kpack) & TOTK_VFIX_MAP_MASK;
}

static inline int totk_vfix_map_find(const totk_vfix_map_t *m, uint64_t vs_u,
                                     uint32_t missing, uint64_t kpack, uint64_t *mod_out) {
    uint32_t slot = totk_vfix_map_hash(vs_u, missing, kpack);
    int f3 = totk_w51_fault_mode() == 3;
    for (uint32_t probe = 0u; probe < TOTK_VFIX_MAP_CAP; probe++) {
        const totk_vfix_map_slot_t *e = &m->slots[slot];
        if (!e->used) return 0;
        if (e->vs_u == vs_u && e->missing == missing && (f3 || e->kpack == kpack)) {
            *mod_out = e->mod_u;
            return 1;
        }
        slot = (slot + 1u) & TOTK_VFIX_MAP_MASK;
    }
    return 0;
}

static inline void totk_vfix_map_put(totk_vfix_map_t *m, uint64_t vs_u,
                                     uint32_t missing, uint64_t kpack, uint64_t mod_u) {
    if (m->inserted >= TOTK_VFIX_MAP_CAP - 128u) { m->partial = 1u; return; }
    uint32_t slot = totk_vfix_map_hash(vs_u, missing, kpack);
    for (uint32_t probe = 0u; probe < TOTK_VFIX_MAP_CAP; probe++) {
        totk_vfix_map_slot_t *e = &m->slots[slot];
        if (!e->used) {
            e->vs_u = vs_u; e->missing = missing; e->kpack = kpack; e->mod_u = mod_u; e->used = 1u;
            m->inserted++;
            return;
        }
        if (e->vs_u == vs_u && e->missing == missing && e->kpack == kpack) {
            e->mod_u = mod_u;
            return;
        }
        slot = (slot + 1u) & TOTK_VFIX_MAP_MASK;
    }
    m->partial = 1u;
}

enum { TOTK_PL_SET_CAP = 4096u, TOTK_PL_SET_MASK = TOTK_PL_SET_CAP - 1u };

typedef struct {
    uint64_t image, incarn, meta;
    uint32_t gen, pad_;
} totk_pso_life_slot_t;

typedef struct {
    totk_pso_life_slot_t slots[TOTK_PL_SET_CAP];
    uint32_t gen;
} totk_pso_life_set_t;

static inline void totk_pso_life_pack_meta(uint32_t mip, uint32_t layer, uint32_t aspect,
                                           uint32_t stage, uint32_t binding, uint64_t *meta_out) {
    if (totk_w51_fault_mode() == 4) {
        *meta_out = (uint64_t)(mip & 0xffu) | ((uint64_t)(aspect & 0xffu) << 20) |
                    ((uint64_t)(stage & 0xfu) << 28);
        return;
    }
    *meta_out = (uint64_t)(mip & 0xffu) | ((uint64_t)(layer & 0xfffu) << 8) |
                ((uint64_t)(aspect & 0xffu) << 20) | ((uint64_t)(stage & 0xfu) << 28) |
                ((uint64_t)binding << 32);
}

static inline void totk_pso_life_set_clear(totk_pso_life_set_t *s) {
    if (!s) return;
    s->gen++;
    if (!s->gen) { memset(s->slots, 0, sizeof s->slots); s->gen = 1u; }
}

static inline int totk_pso_life_set_check_add(totk_pso_life_set_t *s, uint64_t image, uint64_t incarn,
                                              uint64_t meta, size_t nmemo, size_t cap) {
    if (!s->gen) s->gen = 1u;
    uint32_t slot = (uint32_t)totk_vv_mix64(image ^ totk_vv_mix64(incarn) ^ meta) & TOTK_PL_SET_MASK;
    for (uint32_t probe = 0u; probe < TOTK_PL_SET_CAP; probe++) {
        totk_pso_life_slot_t *e = &s->slots[slot];
        if (e->gen != s->gen) {
            if (nmemo >= cap) return -1;
            e->image = image; e->incarn = incarn; e->meta = meta; e->gen = s->gen;
            return 0;
        }
        if (e->image == image && e->incarn == incarn && e->meta == meta) return 1;
        slot = (slot + 1u) & TOTK_PL_SET_MASK;
    }
    return -1;
}

enum { TOTK_CPM_CAP = 64u, TOTK_CPM_MASK = TOTK_CPM_CAP - 1u };

typedef struct {
    uint32_t fmt, mask, samples;
    uint16_t idx1, pad_;
} totk_clear_pipe_slot_t;

typedef struct {
    totk_clear_pipe_slot_t slots[TOTK_CPM_CAP];
    uint16_t inserted, pad_;
} totk_clear_pipe_map_t;

static inline uint32_t totk_cpm_hash(uint32_t fmt, uint32_t mask, uint32_t samples) {
    uint64_t h = totk_vv_mix64(((uint64_t)fmt << 32) ^ ((uint64_t)samples << 8) ^ (uint64_t)mask);
    return (uint32_t)h & TOTK_CPM_MASK;
}

static inline int totk_clear_pipe_map_find(const totk_clear_pipe_map_t *m, uint32_t fmt,
                                           uint32_t mask, uint32_t samples) {
    uint32_t slot = totk_cpm_hash(fmt, mask, samples);
    int f5 = totk_w51_fault_mode() == 5;
    for (uint32_t probe = 0u; probe < TOTK_CPM_CAP; probe++) {
        const totk_clear_pipe_slot_t *e = &m->slots[slot];
        if (!e->idx1) return -1;
        if (e->fmt == fmt && e->samples == samples && (f5 || e->mask == mask)) return (int)(e->idx1 - 1u);
        slot = (slot + 1u) & TOTK_CPM_MASK;
    }
    return -1;
}

static inline void totk_clear_pipe_map_put(totk_clear_pipe_map_t *m, uint32_t fmt,
                                           uint32_t mask, uint32_t samples, uint16_t idx1) {
    if (!idx1 || m->inserted >= TOTK_CPM_CAP - 8u) return;
    uint32_t slot = totk_cpm_hash(fmt, mask, samples);
    for (uint32_t probe = 0u; probe < TOTK_CPM_CAP; probe++) {
        totk_clear_pipe_slot_t *e = &m->slots[slot];
        if (!e->idx1) {
            e->fmt = fmt; e->mask = mask; e->samples = samples; e->idx1 = idx1; m->inserted++;
            return;
        }
        if (e->fmt == fmt && e->mask == mask && e->samples == samples) { e->idx1 = idx1; return; }
        slot = (slot + 1u) & TOTK_CPM_MASK;
    }
}
#endif
