/* Fast O(1) shader touch masks, alias buffer index, UB unsealed range & retired filter (Opt 56). */
#ifndef VK_TOUCH_ALIAS_FAST_H
#define VK_TOUCH_ALIAS_FAST_H
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TOTK_W54_TEST_HOOKS
extern _Atomic int g_totk_vk_touch_alias_fast_override;
extern _Atomic int g_totk_w54_fault;
static inline void totk_vk_touch_alias_fast_set_for_test(int v) {
    atomic_store_explicit(&g_totk_vk_touch_alias_fast_override, v, memory_order_release);
}
static inline void totk_w54_set_fault(int f) {
    atomic_store_explicit(&g_totk_w54_fault, f, memory_order_release);
}
static inline int totk_w54_parse_fault(void) {
    const char *e = getenv("TOTK_W54_FAULT");
    if (e && *e) return atoi(e);
    const char *k = getenv("TOTK_KTGL_OPT_FAULT");
    if (k && *k) {
        int kv = atoi(k);
        if (kv >= 216 && kv <= 220) return kv - 215;
    }
    return 0;
}
static inline int totk_w54_fault_mode(void) {
    int f = atomic_load_explicit(&g_totk_w54_fault, memory_order_relaxed);
    if (f >= 0) return f;
    int v = totk_w54_parse_fault();
    atomic_store_explicit(&g_totk_w54_fault, v, memory_order_relaxed);
    return v;
}
#else
static inline int totk_w54_fault_mode(void) { return 0; }
#endif

static inline int totk_vk_touch_alias_fast_on(void) {
#ifdef TOTK_W54_TEST_HOOKS
    int ov = atomic_load_explicit(&g_totk_vk_touch_alias_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
#endif
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("TOTK_VK_TOUCH_ALIAS_FAST");
        v = !(e && strcmp(e, "0") == 0);
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
}

static inline uint64_t totk_ta_mix64(uint64_t x) {
    x += UINT64_C(0x9e3779b97f4a7c15);
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

static inline uint64_t totk_ta_bbit(uint64_t k) {
    uint64_t h = totk_ta_mix64(k);
    return (UINT64_C(1) << (h & 63u)) | (UINT64_C(1) << ((h >> 32u) & 63u));
}

/* 1. Precomputed 64-bit binding-index masks for storage_touch, draw_touch, fg_extract, pass_merge, feedback. */
typedef struct {
    uint64_t rw_sbuf_mask;
    uint64_t rw_stbuf_mask;
    uint64_t ro_sbuf_mask;
    uint64_t vma_img_mask;
    uint64_t fg_ro_img_mask;
    uint64_t pm_tex_mask;
    uint64_t fb_tex_mask;
    uint8_t fast;
    uint8_t pm_imm_hazard;
    uint8_t fb_imm_unknown;
} totk_sh_touch_sum_t;

static inline void totk_sh_touch_sum_add_stor(totk_sh_touch_sum_t *s, int i, int rw,
                                              int is_sbuf, int is_stbuf, int reg) {
    if (i < 0 || i >= 64) return;
    uint64_t bit = UINT64_C(1) << (unsigned)i;
    if (is_sbuf && reg >= 0 && reg < 16) {
        if (rw) s->rw_sbuf_mask |= bit;
        else if (!(totk_w54_fault_mode() == 1 && i >= 16)) s->ro_sbuf_mask |= bit;
    }
    if (rw && is_stbuf) s->rw_stbuf_mask |= bit;
}

static inline void totk_sh_touch_sum_add_img(totk_sh_touch_sum_t *s, int i, int rw, int is_img3) {
    if (i < 0 || i >= 64 || !is_img3) return;
    uint64_t bit = UINT64_C(1) << (unsigned)i;
    s->vma_img_mask |= bit;
    if (!rw) s->fg_ro_img_mask |= bit;
}

static inline void totk_sh_touch_sum_add_pm_fb(totk_sh_touch_sum_t *s, int i,
                                               int pm_bad_kind, int pm_skip, int pm_tex_ok,
                                               int fb_bad, int fb_tex) {
    if (pm_bad_kind) {
        if (totk_w54_fault_mode() != 2) s->pm_imm_hazard = 1u;
    } else if (!pm_skip) {
        if (!pm_tex_ok) s->pm_imm_hazard = 1u;
        else if (i >= 0 && i < 64) s->pm_tex_mask |= UINT64_C(1) << (unsigned)i;
    }
    if (fb_bad) s->fb_imm_unknown = 1u;
    else if (fb_tex && i >= 0 && i < 64) s->fb_tex_mask |= UINT64_C(1) << (unsigned)i;
}

static inline void totk_sh_touch_sum_finish(totk_sh_touch_sum_t *s, int nb) {
    s->fast = (nb >= 0 && nb <= 64) ? 1u : 0u;
}

/* 2. Order-preserving 256-bucket (VkBuffer -> alias indices) index for lag_alias_ranges. */
enum { TOTK_ALIAS_BUCKETS = 256u, TOTK_ALIAS_BMASK = 255u };
typedef struct {
    int32_t head[TOTK_ALIAS_BUCKETS];
    int32_t tail[TOTK_ALIAS_BUCKETS];
    int32_t *next;
    uint64_t *buf_key;
    uint64_t bloom, gen;
    int cap, n;
} totk_alias_buf_idx_t;

static inline void totk_alias_buf_free(totk_alias_buf_idx_t *x) {
    if (!x) return;
    free(x->next); free(x->buf_key);
    memset(x, 0, sizeof *x);
}

static inline int totk_alias_buf_sync_begin(totk_alias_buf_idx_t *x, int nalias, uint64_t gen) {
    if (!x) return 0;
    if (nalias > x->cap) {
        int nc = x->cap ? x->cap : 256;
        while (nc < nalias) nc *= 2;
        int32_t *nn = (int32_t *)realloc(x->next, (size_t)nc * sizeof *nn);
        if (!nn) return 0;
        x->next = nn;
        uint64_t *nb = (uint64_t *)realloc(x->buf_key, (size_t)nc * sizeof *nb);
        if (!nb) return 0;
        x->buf_key = nb;
        x->cap = nc;
    }
    for (uint32_t b = 0u; b < TOTK_ALIAS_BUCKETS; b++) { x->head[b] = -1; x->tail[b] = -1; }
    x->bloom = 0u;
    x->n = nalias;
    x->gen = gen;
    return 1;
}

static inline void totk_alias_buf_push(totk_alias_buf_idx_t *x, int idx, uint64_t buf) {
    if (!x || idx < 0 || idx >= x->n || !buf) return;
    x->buf_key[idx] = buf;
    x->next[idx] = -1;
    uint32_t b = (uint32_t)totk_ta_mix64(buf) & TOTK_ALIAS_BMASK;
    if (x->tail[b] >= 0) x->next[x->tail[b]] = idx;
    else x->head[b] = idx;
    x->tail[b] = idx;
    x->bloom |= totk_ta_bbit(buf);
}

static inline int totk_alias_buf_first(const totk_alias_buf_idx_t *x, uint64_t buf) {
    if (!x || !x->n || !buf) return -1;
    if ((x->bloom & totk_ta_bbit(buf)) != totk_ta_bbit(buf)) return -1;
    uint32_t b = (uint32_t)totk_ta_mix64(buf) & TOTK_ALIAS_BMASK;
    if (totk_w54_fault_mode() == 3) b ^= 1u;
    for (int i = x->head[b], st = 0; i >= 0 && st < x->n; i = x->next[i], st++) {
        if (x->buf_key[i] == buf) return i;
    }
    return -1;
}

static inline int totk_alias_buf_next(const totk_alias_buf_idx_t *x, int idx, uint64_t buf) {
    if (!x || idx < 0 || idx >= x->n || !buf) return -1;
    for (int i = x->next[idx], st = 0; i >= 0 && st < x->n; i = x->next[i], st++) {
        if (x->buf_key[i] == buf) return i;
    }
    return -1;
}

/* 3. O(1) unsealed-flushed range tracker for nvn_vk.c ub_seal(). */
typedef struct {
    int32_t lo, hi, count;
} totk_ub_unsealed_t;

static inline void totk_ub_unsealed_reset(totk_ub_unsealed_t *u) {
    u->lo = 0; u->hi = 0; u->count = 0;
}

static inline void totk_ub_unsealed_note(totk_ub_unsealed_t *u, int idx) {
    if (!u || idx < 0) return;
    int end = (totk_w54_fault_mode() == 4) ? idx : (idx + 1);
    if (!u->count) { u->lo = idx; u->hi = end; u->count = 1; return; }
    if (idx < u->lo) u->lo = idx;
    if (end > u->hi) u->hi = end;
    u->count++;
}

static inline void totk_ub_unsealed_shift_on_log(totk_ub_unsealed_t *u, int first, int ins_at, int new_nub) {
    if (!u || !u->count) return;
    int m = first < ins_at ? first : ins_at;
    if (m < 0) m = 0;
    if (m < u->lo) u->lo = m;
    u->hi = new_nub;
}

/* 4. Retired resource presence filter + stack-local 256-slot VkImage pointer dedup set (vk_res.c). */
typedef struct {
    uint64_t bv_bloom, img_bloom;
    uint32_t n_bv, n_img;
} totk_retired_filter_t;

static inline void totk_retired_add(totk_retired_filter_t *f, uint64_t bv_buf, uint64_t img) {
    if (bv_buf) { f->n_bv++; f->bv_bloom |= totk_ta_bbit(bv_buf); }
    if (img) { f->n_img++; f->img_bloom |= totk_ta_bbit(img); }
}

static inline void totk_retired_sub_bv(totk_retired_filter_t *f, uint32_t cnt) {
    if (cnt >= f->n_bv) { f->n_bv = 0u; f->bv_bloom = 0u; }
    else f->n_bv -= cnt;
}

static inline void totk_retired_sub_img(totk_retired_filter_t *f, uint32_t cnt) {
    if (cnt >= f->n_img) { f->n_img = 0u; f->img_bloom = 0u; }
    else f->n_img -= cnt;
}

static inline int totk_retired_may_have_bv(const totk_retired_filter_t *f, uint64_t bv_buf) {
    if (totk_w54_fault_mode() == 5) return 0;
    if (!f->n_bv || !bv_buf) return 0;
    uint64_t b = totk_ta_bbit(bv_buf);
    return (f->bv_bloom & b) == b;
}

static inline int totk_retired_may_have_img(const totk_retired_filter_t *f, uint64_t img) {
    if (!f->n_img || !img) return 0;
    uint64_t b = totk_ta_bbit(img);
    return (f->img_bloom & b) == b;
}

enum { TOTK_PSET_SLOTS = 256u, TOTK_PSET_MASK = 255u };
typedef struct {
    uintptr_t keys[TOTK_PSET_SLOTS];
} totk_ptr_set256_t;

static inline int totk_ptr_set256_insert(totk_ptr_set256_t *s, uintptr_t ptr) {
    if (!ptr) return 1;
    uint32_t slot = (uint32_t)totk_ta_mix64((uint64_t)ptr) & TOTK_PSET_MASK;
    for (uint32_t p = 0u; p < TOTK_PSET_SLOTS; p++, slot = (slot + 1u) & TOTK_PSET_MASK) {
        if (!s->keys[slot]) { s->keys[slot] = ptr; return 1; }
        if (s->keys[slot] == ptr) return 0;
    }
    return 1;
}
#endif
