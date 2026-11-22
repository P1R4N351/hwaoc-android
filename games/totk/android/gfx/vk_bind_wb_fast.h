/* Fast O(1) shader bind summary, write-back tile dual index & GPU pool map (Opt 54). */
#ifndef VK_BIND_WB_FAST_H
#define VK_BIND_WB_FAST_H
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TOTK_W52_TEST_HOOKS
extern _Atomic int g_totk_vk_bind_wb_fast_override;
extern _Atomic int g_totk_w52_fault;
static inline void totk_vk_bind_wb_fast_set_for_test(int v) {
    atomic_store_explicit(&g_totk_vk_bind_wb_fast_override, v, memory_order_release);
}
static inline void totk_w52_set_fault(int f) {
    atomic_store_explicit(&g_totk_w52_fault, f, memory_order_release);
}
static inline int totk_w52_parse_fault(void) {
    const char *e = getenv("TOTK_W52_FAULT");
    if (e && *e) return atoi(e);
    const char *k = getenv("TOTK_KTGL_OPT_FAULT");
    if (k && *k) {
        int kv = atoi(k);
        if (kv >= 206 && kv <= 210) return kv - 205;
    }
    return 0;
}
static inline int totk_w52_fault_mode(void) {
    int f = atomic_load_explicit(&g_totk_w52_fault, memory_order_relaxed);
    if (f >= 0) return f;
    int v = totk_w52_parse_fault();
    atomic_store_explicit(&g_totk_w52_fault, v, memory_order_relaxed);
    return v;
}
#else
static inline int totk_w52_fault_mode(void) { return 0; }
#endif

static inline int totk_vk_bind_wb_fast_on(void) {
#ifdef TOTK_W52_TEST_HOOKS
    int ov = atomic_load_explicit(&g_totk_vk_bind_wb_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
#endif
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("TOTK_VK_BIND_WB_FAST");
        v = !(e && strcmp(e, "0") == 0);
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
}

static inline uint64_t totk_bw_mix64(uint64_t x) {
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

enum {
    TOTK_WBTI_CAP = 1024u,
    TOTK_WBTI_MASK = TOTK_WBTI_CAP - 1u,
    TOTK_PMAP_CAP = 4096u,
    TOTK_PMAP_MASK = TOTK_PMAP_CAP - 1u
};

_Static_assert((TOTK_WBTI_CAP & TOTK_WBTI_MASK) == 0u, "wbti pow2");
_Static_assert((TOTK_PMAP_CAP & TOTK_PMAP_MASK) == 0u, "pmap pow2");

typedef struct {
    uint32_t desc_total_count;
    uint16_t ubo_check_mask;
    uint8_t has_any_rw, ubo_unchanged_blocked, has_pso_life_img, ready;
    uint8_t pad_[2];
} totk_sh_bind_sum_t;

static inline void totk_sh_bind_sum_add(totk_sh_bind_sum_t *s, uint32_t count, int rw,
                                        int blocks_ubo_unch, int is_ubo_cbuf,
                                        int32_t reg, int is_pso_img) {
    s->desc_total_count += count;
    if (rw) s->has_any_rw = 1u;
    if (blocks_ubo_unch) s->ubo_unchanged_blocked = 1u;
    if (is_ubo_cbuf && reg >= 0 && reg < 16) s->ubo_check_mask |= (uint16_t)(1u << (unsigned)reg);
    if (is_pso_img && reg >= 0) s->has_pso_life_img = 1u;
}

static inline void totk_sh_bind_sum_finish(totk_sh_bind_sum_t *s) {
    int fm = totk_w52_fault_mode();
    if (fm == 1) {
        s->ubo_unchanged_blocked = 0u;
        s->ubo_check_mask = 0u;
    } else if (fm == 2) {
        s->desc_total_count ^= 1u;
        s->has_any_rw ^= 1u;
    }
    s->ready = 1u;
}

typedef struct {
    uint64_t addr, pool;
    uint16_t slot;
    uint8_t used, pad_[5];
} totk_wbt_ap_slot_t;

typedef struct {
    uint64_t version;
    uint16_t slot;
    uint8_t used, pad_[5];
} totk_wbt_ver_slot_t;

typedef struct {
    totk_wbt_ap_slot_t ap[TOTK_WBTI_CAP];
    totk_wbt_ver_slot_t ver[TOTK_WBTI_CAP];
    uint32_t ap_count, ver_count;
} totk_wb_tile_idx_t;

static inline uint32_t totk_wbt_ap_hash(uint64_t addr, uint64_t pool) {
    return (uint32_t)totk_bw_mix64(addr ^ totk_bw_mix64(pool)) & TOTK_WBTI_MASK;
}

static inline uint32_t totk_wbt_ver_hash(uint64_t version) {
    return (uint32_t)totk_bw_mix64(version) & TOTK_WBTI_MASK;
}

static inline int totk_wb_tile_find_ap(const totk_wb_tile_idx_t *t, uint64_t addr,
                                       uint64_t pool, uint16_t *slot_out) {
    if (!t->ap_count) return 0;
    uint32_t idx = totk_wbt_ap_hash(addr, pool);
    for (uint32_t probe = 0u; probe < TOTK_WBTI_CAP; probe++) {
        const totk_wbt_ap_slot_t *e = &t->ap[idx];
        if (!e->used) return 0;
        if (e->addr == addr && e->pool == pool) {
            uint16_t s = e->slot;
            if (totk_w52_fault_mode() == 3) s ^= 1u;
            *slot_out = s;
            return 1;
        }
        idx = (idx + 1u) & TOTK_WBTI_MASK;
    }
    return 0;
}

static inline void totk_wb_tile_put_ap(totk_wb_tile_idx_t *t, uint64_t addr,
                                       uint64_t pool, uint16_t slot) {
    if (t->ap_count >= TOTK_WBTI_CAP - 32u) return;
    uint32_t idx = totk_wbt_ap_hash(addr, pool);
    for (uint32_t probe = 0u; probe < TOTK_WBTI_CAP; probe++) {
        totk_wbt_ap_slot_t *e = &t->ap[idx];
        if (!e->used) {
            e->addr = addr; e->pool = pool; e->slot = slot; e->used = 1u;
            t->ap_count++;
            return;
        }
        if (e->addr == addr && e->pool == pool) { e->slot = slot; return; }
        idx = (idx + 1u) & TOTK_WBTI_MASK;
    }
}

static inline void totk_wb_tile_del_ap(totk_wb_tile_idx_t *t, uint64_t addr, uint64_t pool) {
    if (!t->ap_count) return;
    uint32_t cur = totk_wbt_ap_hash(addr, pool);
    int found = 0;
    for (uint32_t probe = 0u; probe < TOTK_WBTI_CAP; probe++) {
        if (!t->ap[cur].used) return;
        if (t->ap[cur].addr == addr && t->ap[cur].pool == pool) { found = 1; break; }
        cur = (cur + 1u) & TOTK_WBTI_MASK;
    }
    if (!found) return;
    t->ap_count--;
    uint32_t gap = cur;
    for (uint32_t step = 0u; step < TOTK_WBTI_CAP; step++) {
        cur = (cur + 1u) & TOTK_WBTI_MASK;
        if (!t->ap[cur].used) break;
        uint32_t home = totk_wbt_ap_hash(t->ap[cur].addr, t->ap[cur].pool);
        if (((cur - home) & TOTK_WBTI_MASK) >= ((cur - gap) & TOTK_WBTI_MASK)) {
            t->ap[gap] = t->ap[cur];
            gap = cur;
        }
    }
    t->ap[gap] = (totk_wbt_ap_slot_t){0};
}

static inline int totk_wb_tile_find_ver(const totk_wb_tile_idx_t *t, uint64_t version,
                                        uint16_t *slot_out) {
    if (!t->ver_count || !version) return 0;
    uint32_t idx = totk_wbt_ver_hash(version);
    for (uint32_t probe = 0u; probe < TOTK_WBTI_CAP; probe++) {
        const totk_wbt_ver_slot_t *e = &t->ver[idx];
        if (!e->used) return 0;
        if (e->version == version) {
            uint16_t s = e->slot;
            if (totk_w52_fault_mode() == 4) s ^= 1u;
            *slot_out = s;
            return 1;
        }
        idx = (idx + 1u) & TOTK_WBTI_MASK;
    }
    return 0;
}

static inline void totk_wb_tile_put_ver(totk_wb_tile_idx_t *t, uint64_t version, uint16_t slot) {
    if (!version || t->ver_count >= TOTK_WBTI_CAP - 32u) return;
    uint32_t idx = totk_wbt_ver_hash(version);
    for (uint32_t probe = 0u; probe < TOTK_WBTI_CAP; probe++) {
        totk_wbt_ver_slot_t *e = &t->ver[idx];
        if (!e->used) {
            e->version = version; e->slot = slot; e->used = 1u;
            t->ver_count++;
            return;
        }
        if (e->version == version) { e->slot = slot; return; }
        idx = (idx + 1u) & TOTK_WBTI_MASK;
    }
}

static inline void totk_wb_tile_del_ver(totk_wb_tile_idx_t *t, uint64_t version) {
    if (!version || !t->ver_count) return;
    uint32_t cur = totk_wbt_ver_hash(version);
    int found = 0;
    for (uint32_t probe = 0u; probe < TOTK_WBTI_CAP; probe++) {
        if (!t->ver[cur].used) return;
        if (t->ver[cur].version == version) { found = 1; break; }
        cur = (cur + 1u) & TOTK_WBTI_MASK;
    }
    if (!found) return;
    t->ver_count--;
    uint32_t gap = cur;
    for (uint32_t step = 0u; step < TOTK_WBTI_CAP; step++) {
        cur = (cur + 1u) & TOTK_WBTI_MASK;
        if (!t->ver[cur].used) break;
        uint32_t home = totk_wbt_ver_hash(t->ver[cur].version);
        if (((cur - home) & TOTK_WBTI_MASK) >= ((cur - gap) & TOTK_WBTI_MASK)) {
            t->ver[gap] = t->ver[cur];
            gap = cur;
        }
    }
    t->ver[gap] = (totk_wbt_ver_slot_t){0};
}

typedef struct { uint64_t buf_u, base; uint32_t gen, pad_; } totk_pm_buf_t;
typedef struct { uint64_t identity; int32_t idx; uint32_t gen; } totk_pm_id_t;
typedef struct { uint64_t lo, size; int32_t idx; uint32_t gen; } totk_pm_ls_t;

typedef struct {
    totk_pm_buf_t buf[TOTK_PMAP_CAP];
    totk_pm_id_t id[TOTK_PMAP_CAP];
    totk_pm_ls_t ls[TOTK_PMAP_CAP];
    uint32_t cur_gen, buf_n, id_n, ls_n;
} totk_pool_map_t;

static inline void totk_pool_map_clear(totk_pool_map_t *m) {
    m->cur_gen++;
    if (!m->cur_gen) {
        memset(m->buf, 0, sizeof m->buf);
        memset(m->id, 0, sizeof m->id);
        memset(m->ls, 0, sizeof m->ls);
        m->cur_gen = 1u;
    }
    m->buf_n = m->id_n = m->ls_n = 0u;
}

static inline void totk_pool_map_put_buf(totk_pool_map_t *m, uint64_t buf_u, uint64_t base) {
    if (!buf_u || m->buf_n >= TOTK_PMAP_CAP - 32u) return;
    if (!m->cur_gen) m->cur_gen = 1u;
    uint32_t s = (uint32_t)totk_bw_mix64(buf_u) & TOTK_PMAP_MASK;
    for (uint32_t probe = 0u; probe < TOTK_PMAP_CAP; probe++) {
        totk_pm_buf_t *e = &m->buf[s];
        if (e->gen != m->cur_gen) {
            e->buf_u = buf_u; e->base = base; e->gen = m->cur_gen;
            m->buf_n++;
            return;
        }
        if (e->buf_u == buf_u) return;
        s = (s + 1u) & TOTK_PMAP_MASK;
    }
}

static inline uint64_t totk_pool_map_find_buf(const totk_pool_map_t *m, uint64_t buf_u) {
    if (!buf_u || !m->cur_gen || !m->buf_n) return 0u;
    uint32_t s = (uint32_t)totk_bw_mix64(buf_u) & TOTK_PMAP_MASK;
    for (uint32_t probe = 0u; probe < TOTK_PMAP_CAP; probe++) {
        const totk_pm_buf_t *e = &m->buf[s];
        if (e->gen != m->cur_gen) return 0u;
        if (e->buf_u == buf_u) {
            uint64_t b = e->base;
            if (totk_w52_fault_mode() == 5) b ^= UINT64_C(0x10);
            return b;
        }
        s = (s + 1u) & TOTK_PMAP_MASK;
    }
    return 0u;
}

static inline void totk_pool_map_put_id(totk_pool_map_t *m, uint64_t identity, int32_t arr_idx) {
    if (!identity || m->id_n >= TOTK_PMAP_CAP - 32u) return;
    if (!m->cur_gen) m->cur_gen = 1u;
    uint32_t s = (uint32_t)totk_bw_mix64(identity) & TOTK_PMAP_MASK;
    for (uint32_t probe = 0u; probe < TOTK_PMAP_CAP; probe++) {
        totk_pm_id_t *e = &m->id[s];
        if (e->gen != m->cur_gen) {
            e->identity = identity; e->idx = arr_idx; e->gen = m->cur_gen;
            m->id_n++;
            return;
        }
        if (e->identity == identity) { e->idx = arr_idx; return; }
        s = (s + 1u) & TOTK_PMAP_MASK;
    }
}

static inline int32_t totk_pool_map_find_id(const totk_pool_map_t *m, uint64_t identity) {
    if (!identity || !m->cur_gen || !m->id_n) return -1;
    uint32_t s = (uint32_t)totk_bw_mix64(identity) & TOTK_PMAP_MASK;
    for (uint32_t probe = 0u; probe < TOTK_PMAP_CAP; probe++) {
        const totk_pm_id_t *e = &m->id[s];
        if (e->gen != m->cur_gen) return -1;
        if (e->identity == identity) {
            int32_t idx = e->idx;
            if (totk_w52_fault_mode() == 5) idx ^= 1;
            return idx;
        }
        s = (s + 1u) & TOTK_PMAP_MASK;
    }
    return -1;
}

static inline void totk_pool_map_put_ls(totk_pool_map_t *m, uint64_t lo, uint64_t size, int32_t arr_idx) {
    if (!size || m->ls_n >= TOTK_PMAP_CAP - 32u) return;
    if (!m->cur_gen) m->cur_gen = 1u;
    uint32_t s = (uint32_t)totk_bw_mix64(lo ^ totk_bw_mix64(size)) & TOTK_PMAP_MASK;
    for (uint32_t probe = 0u; probe < TOTK_PMAP_CAP; probe++) {
        totk_pm_ls_t *e = &m->ls[s];
        if (e->gen != m->cur_gen) {
            e->lo = lo; e->size = size; e->idx = arr_idx; e->gen = m->cur_gen;
            m->ls_n++;
            return;
        }
        if (e->lo == lo && e->size == size) { e->idx = arr_idx; return; }
        s = (s + 1u) & TOTK_PMAP_MASK;
    }
}

static inline int32_t totk_pool_map_find_ls(const totk_pool_map_t *m, uint64_t lo, uint64_t size) {
    if (!size || !m->cur_gen || !m->ls_n) return -1;
    uint32_t s = (uint32_t)totk_bw_mix64(lo ^ totk_bw_mix64(size)) & TOTK_PMAP_MASK;
    for (uint32_t probe = 0u; probe < TOTK_PMAP_CAP; probe++) {
        const totk_pm_ls_t *e = &m->ls[s];
        if (e->gen != m->cur_gen) return -1;
        if (e->lo == lo && e->size == size) {
            int32_t idx = e->idx;
            if (totk_w52_fault_mode() == 5) idx ^= 1;
            return idx;
        }
        s = (s + 1u) & TOTK_PMAP_MASK;
    }
    return -1;
}
#endif
