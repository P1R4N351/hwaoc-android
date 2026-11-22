/* Fast O(1) shader FG bind summary, FGX watch/scene indices, WB bucket dir & arena buffer map (Opt 55). */
#ifndef VK_FG_WB_FAST_H
#define VK_FG_WB_FAST_H
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TOTK_W53_TEST_HOOKS
extern _Atomic int g_totk_vk_fg_wb_fast_override;
extern _Atomic int g_totk_w53_fault;
static inline void totk_vk_fg_wb_fast_set_for_test(int v) {
    atomic_store_explicit(&g_totk_vk_fg_wb_fast_override, v, memory_order_release);
}
static inline void totk_w53_set_fault(int f) {
    atomic_store_explicit(&g_totk_w53_fault, f, memory_order_release);
}
static inline int totk_w53_parse_fault(void) {
    const char *e = getenv("TOTK_W53_FAULT");
    if (e && *e) return atoi(e);
    const char *k = getenv("TOTK_KTGL_OPT_FAULT");
    if (k && *k) {
        int kv = atoi(k);
        if (kv >= 211 && kv <= 215) return kv - 210;
    }
    return 0;
}
static inline int totk_w53_fault_mode(void) {
    int f = atomic_load_explicit(&g_totk_w53_fault, memory_order_relaxed);
    if (f >= 0) return f;
    int v = totk_w53_parse_fault();
    atomic_store_explicit(&g_totk_w53_fault, v, memory_order_relaxed);
    return v;
}
#else
static inline int totk_w53_fault_mode(void) { return 0; }
#endif

static inline int totk_vk_fg_wb_fast_on(void) {
#ifdef TOTK_W53_TEST_HOOKS
    int ov = atomic_load_explicit(&g_totk_vk_fg_wb_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
#endif
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("TOTK_VK_FG_WB_FAST");
        v = !(e && strcmp(e, "0") == 0);
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
}

static inline uint64_t totk_fgw_mix64(uint64_t x) {
    x += UINT64_C(0x9e3779b97f4a7c15);
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

static inline uint64_t totk_fgw_bloom(uint64_t lo, uint64_t hi, unsigned shift) {
    if (hi <= lo) return 0u;
    uint64_t a = lo >> shift, b = (hi - 1u) >> shift;
    if (b >= a + 63u) return ~UINT64_C(0);
    uint64_t m = 0u;
    for (uint64_t p = a; p <= b; p++) m |= UINT64_C(1) << (p & 63u);
    return m;
}

/* 1. Precomputed framegen rw-storage and camera-buffer binding summary on vk_shader. */
typedef struct {
    uint64_t rw_simg_mask;
    uint64_t rw_sbuf_mask;
    uint8_t reads_cam1;
    uint8_t cam_valid;
    uint8_t mask_valid;
} totk_sh_fg_sum_t;

static inline void totk_sh_fg_sum_add(totk_sh_fg_sum_t *s, int idx, int rw,
                                      int is_simg, int is_sbuf, int is_cam_buf, int reg) {
    if (reg == 1 && is_cam_buf) s->reads_cam1 = 1u;
    if (rw && idx >= 0 && idx < 64) {
        uint64_t bit = UINT64_C(1) << (unsigned)idx;
        if (is_simg) s->rw_simg_mask |= bit;
        if (is_sbuf) s->rw_sbuf_mask |= bit;
    }
}

static inline void totk_sh_fg_sum_finish(totk_sh_fg_sum_t *s, int nb) {
    s->cam_valid = 1u;
    s->mask_valid = (nb >= 0 && nb <= 64) ? 1u : 0u;
}

static inline int totk_sh_fg_reads_cam(const totk_sh_fg_sum_t *s) {
    if (totk_w53_fault_mode() == 1) return !s->reads_cam1;
    return (int)s->reads_cam1;
}

static inline uint64_t totk_sh_fg_rw_mask(const totk_sh_fg_sum_t *s, unsigned stage) {
    uint64_t m = stage == 5u ? s->rw_sbuf_mask : (s->rw_simg_mask | s->rw_sbuf_mask);
    if (totk_w53_fault_mode() == 1 && m) m &= m - 1u;
    return m;
}

/* 2. FGX watch hash + range filter + backing bucket chain (FGX_WATCHES = 256). */
enum {
    TOTK_FGW_SLOTS = 512u,
    TOTK_FGW_SMASK = 511u,
    TOTK_FGW_BUCKETS = 128u,
    TOTK_FGW_BMASK = 127u,
    TOTK_FGW_MAX = 256u
};
typedef struct {
    uint64_t k_back[TOTK_FGW_SLOTS];
    uint32_t k_mip[TOTK_FGW_SLOTS], k_lay[TOTK_FGW_SLOTS], k_asp[TOTK_FGW_SLOTS];
    uint32_t sgen[TOTK_FGW_SLOTS];
    uint16_t sval[TOTK_FGW_SLOTS];
    uint32_t gen;
    int16_t head[TOTK_FGW_BUCKETS];
    int16_t next[TOTK_FGW_MAX];
    uint64_t min_lo, max_hi, b64k, b1m, back_bloom, b0;
    uint32_t synced_n;
    uint8_t dirty;
} totk_fgx_watch_idx_t;

static inline uint32_t totk_fgw_key_hash(uint64_t back, uint32_t mip, uint32_t lay, uint32_t asp) {
    uint64_t h = totk_fgw_mix64(back) ^ totk_fgw_mix64(((uint64_t)mip << 32) | lay) ^ totk_fgw_mix64(asp);
    return (uint32_t)h & TOTK_FGW_SMASK;
}

static inline void totk_fgx_watch_dirty(totk_fgx_watch_idx_t *idx) { idx->dirty = 1u; }

static inline void totk_fgx_watch_begin(totk_fgx_watch_idx_t *idx) {
    if (++idx->gen == 0u) { memset(idx->sgen, 0, sizeof idx->sgen); idx->gen = 1u; }
    for (uint32_t b = 0u; b < TOTK_FGW_BUCKETS; b++) idx->head[b] = -1;
    idx->min_lo = UINT64_MAX; idx->max_hi = 0u;
    idx->b64k = 0u; idx->b1m = 0u; idx->back_bloom = 0u;
    idx->synced_n = 0u; idx->b0 = 0u; idx->dirty = 0u;
}

static inline void totk_fgx_watch_range_add(totk_fgx_watch_idx_t *idx, uint64_t addr, uint64_t size) {
    if (!addr || !size) return;
    uint64_t hi = addr + size;
    if (hi < addr) hi = UINT64_MAX;
    if (addr < idx->min_lo) idx->min_lo = addr;
    if (hi > idx->max_hi) idx->max_hi = hi;
    idx->b64k |= totk_fgw_bloom(addr, hi, 16u);
    idx->b1m |= totk_fgw_bloom(addr, hi, 20u);
}

static inline void totk_fgx_watch_add_entry(totk_fgx_watch_idx_t *idx, uint32_t pos,
                                            uint64_t back, uint32_t mip, uint32_t lay,
                                            uint32_t asp, uint64_t addr, uint64_t size) {
    if (pos >= TOTK_FGW_MAX) return;
    if (pos == 0u) idx->b0 = back;
    uint32_t g = idx->gen, s = totk_fgw_key_hash(back, mip, lay, asp);
    for (uint32_t p = 0u; p < TOTK_FGW_SLOTS; p++, s = (s + 1u) & TOTK_FGW_SMASK) {
        if (idx->sgen[s] != g || (idx->k_back[s] == back && idx->k_mip[s] == mip &&
                                  idx->k_lay[s] == lay && idx->k_asp[s] == asp)) {
            idx->k_back[s] = back; idx->k_mip[s] = mip; idx->k_lay[s] = lay; idx->k_asp[s] = asp;
            idx->sval[s] = (uint16_t)pos; idx->sgen[s] = g; break;
        }
    }
    uint32_t bkt = (uint32_t)totk_fgw_mix64(back) & TOTK_FGW_BMASK;
    idx->next[pos] = idx->head[bkt]; idx->head[bkt] = (int16_t)pos;
    idx->back_bloom |= UINT64_C(1) << (totk_fgw_mix64(back) & 63u);
    totk_fgx_watch_range_add(idx, addr, size);
}

static inline int totk_fgx_watch_find_key(const totk_fgx_watch_idx_t *idx, uint64_t back,
                                          uint32_t mip, uint32_t lay, uint32_t asp) {
    if (totk_w53_fault_mode() == 2) return -1;
    if (!idx->gen) return -1;
    uint32_t g = idx->gen, s = totk_fgw_key_hash(back, mip, lay, asp);
    for (uint32_t p = 0u; p < TOTK_FGW_SLOTS; p++, s = (s + 1u) & TOTK_FGW_SMASK) {
        if (idx->sgen[s] != g) return -1;
        if (idx->k_back[s] == back && idx->k_mip[s] == mip &&
            idx->k_lay[s] == lay && idx->k_asp[s] == asp) return (int)idx->sval[s];
    }
    return -1;
}

static inline int totk_fgx_watch_may_overlap(const totk_fgx_watch_idx_t *idx, uint64_t addr, uint64_t size) {
    if (totk_w53_fault_mode() == 2) return 0;
    if (!idx->synced_n || !addr || !size) return 0;
    uint64_t hi = addr + size;
    if (hi < addr) hi = UINT64_MAX;
    if (hi <= idx->min_lo || addr >= idx->max_hi) return 0;
    if (!(totk_fgw_bloom(addr, hi, 16u) & idx->b64k)) return 0;
    if (!(totk_fgw_bloom(addr, hi, 20u) & idx->b1m)) return 0;
    return 1;
}

static inline int totk_fgx_watch_head(const totk_fgx_watch_idx_t *idx, uint64_t back) {
    if (!idx->synced_n) return -1;
    if (!(idx->back_bloom & (UINT64_C(1) << (totk_fgw_mix64(back) & 63u)))) return -1;
    return (int)idx->head[totk_fgw_mix64(back) & TOTK_FGW_BMASK];
}

/* 3. FGX scene ascending bucket index (FG_DEP_MAX_CANDIDATES <= 64). */
enum { TOTK_FGS_MAX = 64u, TOTK_FGS_BUCKETS = 128u, TOTK_FGS_BMASK = 127u };
typedef struct {
    int8_t head[TOTK_FGS_BUCKETS], next[TOTK_FGS_MAX];
    uint64_t back_bloom, b0;
    uint32_t synced_n;
    uint8_t dirty;
} totk_fgx_scene_idx_t;

static inline void totk_fgx_scene_dirty(totk_fgx_scene_idx_t *s) { s->dirty = 1u; }

static inline void totk_fgx_scene_begin(totk_fgx_scene_idx_t *s) {
    for (uint32_t b = 0u; b < TOTK_FGS_BUCKETS; b++) s->head[b] = -1;
    s->back_bloom = 0u; s->b0 = 0u; s->synced_n = 0u; s->dirty = 0u;
}

static inline void totk_fgx_scene_add(totk_fgx_scene_idx_t *s, uint32_t pos, uint64_t back) {
    if (pos >= TOTK_FGS_MAX) return;
    if (pos == 0u) s->b0 = back;
    uint32_t bkt = (uint32_t)totk_fgw_mix64(back) & TOTK_FGS_BMASK;
    s->next[pos] = s->head[bkt]; s->head[bkt] = (int8_t)pos;
    s->back_bloom |= UINT64_C(1) << (totk_fgw_mix64(back) & 63u);
}

static inline int totk_fgx_scene_head(const totk_fgx_scene_idx_t *s, uint64_t back) {
    if (totk_w53_fault_mode() == 3) return -1;
    if (!s->synced_n) return -1;
    if (!(s->back_bloom & (UINT64_C(1) << (totk_fgw_mix64(back) & 63u)))) return -1;
    return (int)s->head[totk_fgw_mix64(back) & TOTK_FGS_BMASK];
}

/* 4. Write-back spatial bucket directory (64 x 256-KiB bins -> 256-bit mask) + landing bounds. */
enum { TOTK_WB_DIR_MAX = 256u, TOTK_WB_DIR_BINS = 64u, TOTK_WB_DIR_BMASK = 63u, TOTK_WB_DIR_SHIFT = 18u };
typedef struct {
    uint64_t bins[TOTK_WB_DIR_BINS][4];
    uint64_t all_mask[4];
    uint64_t slot_lo[TOTK_WB_DIR_MAX], slot_hi[TOTK_WB_DIR_MAX];
    uint32_t nslots;
    uint8_t overflow;
    uint64_t landing_lo, landing_hi;
} totk_wb_bucket_dir_t;

static inline void totk_wb_dir_set_bit(totk_wb_bucket_dir_t *d, uint32_t s, uint64_t addr, uint64_t sz, int set) {
    if (s >= TOTK_WB_DIR_MAX) return;
    unsigned w = s >> 6u; uint64_t bit = UINT64_C(1) << (s & 63u);
    if (!sz) {
        if (set) { d->all_mask[w] |= bit; d->slot_lo[s] = 0u; d->slot_hi[s] = 0u; }
        else d->all_mask[w] &= ~bit;
        return;
    }
    uint64_t hi = addr + sz;
    if (hi < addr) hi = UINT64_MAX;
    uint64_t b0 = addr >> TOTK_WB_DIR_SHIFT, b1 = (hi - 1u) >> TOTK_WB_DIR_SHIFT;
    if (set) { d->slot_lo[s] = addr; d->slot_hi[s] = hi; }
    else { d->all_mask[w] &= ~bit; }
    if (b1 - b0 + 1u >= TOTK_WB_DIR_BINS) {
        if (set) d->all_mask[w] |= bit;
        return;
    }
    for (uint64_t b = b0; b <= b1; b++) {
        uint64_t *dst = &d->bins[b & TOTK_WB_DIR_BMASK][w];
        if (set) *dst |= bit; else *dst &= ~bit;
    }
}

static inline void totk_wb_dir_clear_slot(totk_wb_bucket_dir_t *d, uint32_t s) {
    if (s >= TOTK_WB_DIR_MAX) return;
    unsigned w = s >> 6u; uint64_t nbit = ~(UINT64_C(1) << (s & 63u));
    d->all_mask[w] &= nbit;
    if (d->slot_hi[s] > d->slot_lo[s]) {
        uint64_t b0 = d->slot_lo[s] >> TOTK_WB_DIR_SHIFT;
        uint64_t b1 = (d->slot_hi[s] - 1u) >> TOTK_WB_DIR_SHIFT;
        if (b1 - b0 + 1u < TOTK_WB_DIR_BINS) {
            for (uint64_t b = b0; b <= b1; b++) d->bins[b & TOTK_WB_DIR_BMASK][w] &= nbit;
        }
    }
    d->slot_lo[s] = 0u; d->slot_hi[s] = 0u;
}

static inline void totk_wb_dir_set(totk_wb_bucket_dir_t *d, uint32_t idx, uint64_t addr, uint64_t sz, uint32_t new_n) {
    if (new_n > TOTK_WB_DIR_MAX) { d->overflow = 1u; d->nslots = new_n; return; }
    d->nslots = new_n;
    if (d->overflow) return;
    totk_wb_dir_clear_slot(d, idx);
    totk_wb_dir_set_bit(d, idx, addr, sz, 1);
}

static inline void totk_wb_dir_remove_swap(totk_wb_bucket_dir_t *d, uint32_t idx, uint32_t new_n) {
    if (new_n == 0u) {
        memset(d->bins, 0, sizeof d->bins); memset(d->all_mask, 0, sizeof d->all_mask);
        memset(d->slot_lo, 0, sizeof d->slot_lo); memset(d->slot_hi, 0, sizeof d->slot_hi);
        d->nslots = 0u; d->overflow = 0u; return;
    }
    if (new_n > TOTK_WB_DIR_MAX) { d->overflow = 1u; d->nslots = new_n; return; }
    d->nslots = new_n;
    if (d->overflow) return;
    totk_wb_dir_clear_slot(d, idx);
    if (idx < new_n) {
        uint64_t la = d->slot_lo[new_n], lh = d->slot_hi[new_n];
        totk_wb_dir_clear_slot(d, new_n);
        totk_wb_dir_set_bit(d, idx, la, lh > la ? lh - la : 0u, 1);
    }
}

static inline void totk_wb_dir_landing_reset(totk_wb_bucket_dir_t *d) {
    d->landing_lo = UINT64_MAX; d->landing_hi = 0u;
}

static inline void totk_wb_dir_landing_add(totk_wb_bucket_dir_t *d, uint64_t addr, uint64_t sz) {
    if (!sz) return;
    uint64_t hi = addr + sz;
    if (hi < addr) hi = UINT64_MAX;
    if (addr < d->landing_lo) d->landing_lo = addr;
    if (hi > d->landing_hi) d->landing_hi = hi;
}

static inline int totk_wb_dir_query(const totk_wb_bucket_dir_t *d, uint64_t addr, uint64_t n, uint64_t out[4]) {
    if (d->overflow || d->nslots > TOTK_WB_DIR_MAX || !n) return 0;
    uint64_t hi = addr + n;
    if (hi < addr) hi = UINT64_MAX;
    uint64_t b0 = addr >> TOTK_WB_DIR_SHIFT, b1 = (hi - 1u) >> TOTK_WB_DIR_SHIFT;
    if (b1 - b0 + 1u >= TOTK_WB_DIR_BINS) return 0;
    for (int w = 0; w < 4; w++) out[w] = d->all_mask[w];
    for (uint64_t b = b0; b <= b1; b++) {
        uint32_t bi = (uint32_t)b & TOTK_WB_DIR_BMASK;
        for (int w = 0; w < 4; w++) out[w] |= d->bins[bi][w];
    }
    if (totk_w53_fault_mode() == 4) {
        for (int w = 0; w < 4; w++) if (out[w]) { out[w] &= out[w] - 1u; break; }
    }
    return 1;
}

/* 5. Embedded per-arena 64-slot VkBuffer -> chunk index map (nvn_vk.c). */
enum { TOTK_ABM_SLOTS = 64u, TOTK_ABM_MASK = 63u, TOTK_ABM_MAX = 32u };
typedef struct {
    uint64_t key[TOTK_ABM_SLOTS];
    uint8_t val[TOTK_ABM_SLOTS]; /* chunk index + 1; 0 = empty */
    uint64_t bloom;
    uint8_t count;
    uint8_t overflow;
} totk_arena_buf_map_t;

static inline void totk_arena_bmap_clear(totk_arena_buf_map_t *m) {
    memset(m, 0, sizeof *m);
}

static inline void totk_arena_bmap_put(totk_arena_buf_map_t *m, uint64_t buf, int idx) {
    if (!buf || idx < 0) return;
    if (m->overflow || (unsigned)idx >= TOTK_ABM_MAX) { m->overflow = 1u; return; }
    m->bloom |= UINT64_C(1) << (totk_fgw_mix64(buf) & 63u);
    uint32_t s = (uint32_t)totk_fgw_mix64(buf) & TOTK_ABM_MASK;
    for (uint32_t p = 0u; p < TOTK_ABM_SLOTS; p++, s = (s + 1u) & TOTK_ABM_MASK) {
        if (!m->val[s] || m->key[s] == buf) {
            if (!m->val[s]) m->count++;
            m->key[s] = buf; m->val[s] = (uint8_t)(idx + 1); return;
        }
    }
    m->overflow = 1u;
}

static inline void totk_arena_bmap_swap(totk_arena_buf_map_t *m, uint64_t buf_a, int idx_a,
                                        uint64_t buf_b, int idx_b) {
    if (m->overflow) return;
    totk_arena_bmap_put(m, buf_a, idx_a);
    totk_arena_bmap_put(m, buf_b, idx_b);
}

static inline int totk_arena_bmap_find(const totk_arena_buf_map_t *m, uint64_t buf, int *overflow_out) {
    *overflow_out = m->overflow ? 1 : 0;
    if (m->overflow || !buf) return -1;
    if (totk_w53_fault_mode() == 5) { *overflow_out = 0; return 9999; }
    if (!(m->bloom & (UINT64_C(1) << (totk_fgw_mix64(buf) & 63u)))) return -1;
    uint32_t s = (uint32_t)totk_fgw_mix64(buf) & TOTK_ABM_MASK;
    for (uint32_t p = 0u; p < TOTK_ABM_SLOTS; p++, s = (s + 1u) & TOTK_ABM_MASK) {
        if (!m->val[s]) return -1;
        if (m->key[s] == buf) return (int)m->val[s] - 1;
    }
    return -1;
}
#endif
