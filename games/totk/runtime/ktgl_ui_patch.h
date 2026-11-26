#pragma once
/* Authoritative KTGL ScreenLayout/Pane tagging hooks for AoC 1.3.0. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "ktgl_wid_classify.h"

void totk_nvn_enable_wid(void);
void totk_nvn_set_wid(uint8_t w);

#if defined(__aarch64__)
extern void hwaoc_ktgl_ui_bridge(void);
#else
static void hwaoc_ktgl_ui_bridge(void) {}
#endif

enum {
    AOC_MGR_CTX_OFF = 0x40u,
    AOC_CTX_BATCH_OFF = 0x2870u,
    AOC_FLUSH_OFF = 0x17dc840u,
    AOC_SITE_COUNT = 25u
};

enum {
    AOC_HK_NODE = 0,
    AOC_HK_EXIT = 1,
    AOC_HK_ENTER = 2,
    AOC_HK_PANE = 3,
    AOC_HK_SUBMIT = 4
};

typedef struct {
    uint64_t site;
    uint32_t old_insn;
    uint8_t post_old;
    uint8_t r0, r1, r2;
    uint8_t hook_kind;
} KtglAocSite;

typedef struct {
    uint64_t pool_lo, pool_hi, cur;
    uint64_t flush_off, tramp_off;
    uintptr_t bridge_va, post_flush_va;
} KtglAocEmit;

static const KtglAocSite k_aoc_sites[AOC_SITE_COUNT] = {
    {0x18b4b78u, 0xb9010408u, 1u, 0u,  31u, 31u, AOC_HK_PANE},
    {0x18b6eccu, 0xb9010408u, 1u, 0u,  31u, 31u, AOC_HK_PANE},
    {0x18bfd44u, 0xb9010409u, 1u, 0u,  31u, 31u, AOC_HK_PANE},
    {0x18c3138u, 0xb901040au, 1u, 0u,  31u, 31u, AOC_HK_PANE},
    {0x18c4168u, 0xb9010408u, 1u, 0u,  31u, 31u, AOC_HK_PANE},
    {0x18c4850u, 0xb9010409u, 1u, 0u,  31u, 31u, AOC_HK_PANE},
    {0x18c5184u, 0xb9010409u, 1u, 0u,  31u, 31u, AOC_HK_PANE},
    {0x18ccd48u, 0xb9010408u, 1u, 0u,  31u, 31u, AOC_HK_PANE},
    {0x18d1ef8u, 0xb9010408u, 1u, 0u,  31u, 31u, AOC_HK_PANE},
    {0x18d2a08u, 0xb9010408u, 1u, 0u,  31u, 31u, AOC_HK_PANE},
    {0x18d3848u, 0xb9010409u, 1u, 0u,  31u, 31u, AOC_HK_PANE},
    {0x18d3908u, 0xb9010408u, 1u, 0u,  31u, 31u, AOC_HK_PANE},
    {0x18d6cbcu, 0xb9010408u, 1u, 0u,  31u, 31u, AOC_HK_PANE},
    {0x18daaccu, 0xb9010409u, 1u, 0u,  31u, 31u, AOC_HK_PANE},
    {0x12cc144u, 0xf9401288u, 0u, 21u, 21u, 20u, AOC_HK_SUBMIT},
    {0x18effe8u, 0xf94002e8u, 0u, 23u, 21u, 20u, AOC_HK_SUBMIT},
    {0x18f6668u, 0xaa0003f8u, 1u, 0u,  31u, 31u, AOC_HK_ENTER},
    {0x18f6764u, 0xf9400ac0u, 1u, 0u,  24u, 31u, AOC_HK_NODE},
    {0x18f733cu, 0xf9400ac0u, 1u, 0u,  24u, 31u, AOC_HK_NODE},
    {0x18f7654u, 0xf9400adbu, 1u, 27u, 24u, 31u, AOC_HK_NODE},
    {0x18f7cf8u, 0xf9400ad3u, 1u, 19u, 24u, 31u, AOC_HK_NODE},
    {0x18f85d4u, 0xf9400ac0u, 1u, 0u,  24u, 31u, AOC_HK_NODE},
    {0x18f8c58u, 0xf9400ac0u, 1u, 0u,  24u, 31u, AOC_HK_NODE},
    {0x18f9c48u, 0xf9400ac0u, 1u, 0u,  24u, 31u, AOC_HK_NODE},
    {0x18fb670u, 0x9108c3ffu, 0u, 24u, 31u, 31u, AOC_HK_EXIT},
};

static KtglAddrBounds g_aoc_bounds;
static KtglSnapMap g_aoc_snap_map;
static __thread struct {
    uintptr_t mgr;
    const void *cur_pane;
    uint8_t wid, pend_wid;
} t_aoc_tag;
static __thread KtglWidCache t_aoc_cache;

static inline int ktgl_aoc_live_on(void) {
    const char *e = getenv("TOTK_KTGL_TAG");
    return !(e && !strcmp(e, "0"));
}

static inline uint32_t aoc_rd32(const uint8_t *b, uint64_t off) {
    uint32_t v = 0; memcpy(&v, b + off, sizeof v); return v;
}

static inline void aoc_wr32(uint8_t *b, uint64_t off, uint32_t w) {
    memcpy(b + off, &w, sizeof w);
}

static inline int aoc_emit_w(uint8_t *b, KtglAocEmit *e, uint32_t w) {
    if (!e || e->cur + 4u > e->pool_hi) return 0;
    aoc_wr32(b, e->cur, w);
    e->cur += 4u;
    return 1;
}

static inline int aoc_emit_seq(
    uint8_t *b, KtglAocEmit *e, const uint32_t *w, size_t n) {
    for (size_t i = 0; i < n; i++) if (!aoc_emit_w(b, e, w[i])) return 0;
    return 1;
}

static inline uint32_t aoc_b_word(uint64_t from, uint64_t to, int is_bl) {
    int64_t diff = (int64_t)to - (int64_t)from;
    if ((diff & 3) || diff < -(1LL << 27) || diff >= (1LL << 27)) return 0;
    return (is_bl ? 0x94000000u : 0x14000000u) | (((uint32_t)(diff >> 2)) & 0x03ffffffu);
}

static inline int aoc_emit_mov_x(uint8_t *b, KtglAocEmit *e, uint8_t rd, uint8_t rm) {
    uint32_t src = rm <= 30u ? (uint32_t)rm : 31u;
    return aoc_emit_w(b, e, 0xaa0003e0u | (src << 16) | (rd & 31u));
}

static inline int aoc_emit_arg_x(uint8_t *b, KtglAocEmit *e, uint8_t rd, uint8_t src) {
    if (src >= 9u && src <= 11u) {
        uint32_t off8 = (uint32_t)(src - 9u);
        return aoc_emit_w(b, e, 0xf94003e0u | (off8 << 10) | (rd & 31u));
    }
    return aoc_emit_mov_x(b, e, rd, src);
}

static inline int aoc_emit_mov64(uint8_t *b, KtglAocEmit *e, uint8_t rd, uint64_t val) {
    uint32_t r = rd & 31u;
    const uint32_t w[4] = {
        0xd2800000u | ((uint32_t)(val & 0xffffu) << 5) | r,
        0xf2a00000u | ((uint32_t)((val >> 16) & 0xffffu) << 5) | r,
        0xf2c00000u | ((uint32_t)((val >> 32) & 0xffffu) << 5) | r,
        0xf2e00000u | ((uint32_t)((val >> 48) & 0xffffu) << 5) | r,
    };
    return aoc_emit_seq(b, e, w, 4u);
}

static inline int aoc_emit_flush_trampoline(uint8_t *b, KtglAocEmit *e) {
    static const uint32_t pre[] = {
        0xd10a43ffu, 0xa90007e0u, 0xa9010fe2u, 0xa90217e4u, 0xa9031fe6u,
        0xa90433e8u, 0xa9053bedu, 0xa90647efu, 0xa9077bfdu, 0xd53b4209u, 0xa9087fe9u,
    };
    static const uint32_t post[] = {
        0xf94043e9u, 0xd51b4209u, 0xa94007e0u, 0xa9410fe2u, 0xa94217e4u, 0xa9431fe6u,
        0xa94433e8u, 0xa9453bedu, 0xa94647efu, 0xa9477bfdu, 0x910a43ffu, 0xd65f03c0u,
    };
    e->tramp_off = e->cur;
    if (!aoc_emit_seq(b, e, pre, sizeof pre / sizeof pre[0])) return 0;
    for (uint32_t q = 0; q < 32u; q += 2u) {
        uint32_t w = 0xad0003e0u | (((144u + q * 16u) / 16u) << 15) | ((q + 1u) << 10) | q;
        if (!aoc_emit_w(b, e, w)) return 0;
    }
    if (!aoc_emit_mov_x(b, e, 0u, 17u)) return 0;
    uint32_t bl = aoc_b_word(e->cur, e->flush_off, 1);
    if (!bl || !aoc_emit_w(b, e, bl)) return 0;
    if (!aoc_emit_w(b, e, 0xf94037e9u)) return 0;
    if (!aoc_emit_mov_x(b, e, 10u, 31u) || !aoc_emit_mov_x(b, e, 11u, 31u)) return 0;
    if (!aoc_emit_mov64(b, e, 16u, (uint64_t)e->post_flush_va)) return 0;
    if (!aoc_emit_mov64(b, e, 17u, (uint64_t)e->bridge_va)) return 0;
    if (!aoc_emit_w(b, e, 0xd63f0220u)) return 0;
    for (uint32_t q = 0; q < 32u; q += 2u) {
        uint32_t w = 0xad4003e0u | (((144u + q * 16u) / 16u) << 15) | ((q + 1u) << 10) | q;
        if (!aoc_emit_w(b, e, w)) return 0;
    }
    return aoc_emit_seq(b, e, post, sizeof post / sizeof post[0]);
}

static inline int aoc_emit_site_stub(
    uint8_t *b, KtglAocEmit *e, const KtglAocSite *s, uintptr_t hook_va, int with_flush) {
    static const uint32_t push48[] = {0xd100c3ffu, 0xa9002be9u, 0xa90143ebu, 0xa9027bf1u};
    static const uint32_t pop48[] = {0xa9427bf1u, 0xa94143ebu, 0xa9402be9u, 0x9100c3ffu};
    uint64_t stub_off = e->cur;
    if (s->post_old && !aoc_emit_w(b, e, s->old_insn)) return 0;
    if (!aoc_emit_seq(b, e, push48, 4u)) return 0;
    if (!aoc_emit_arg_x(b, e, 9u, s->r0)) return 0;
    if (!aoc_emit_arg_x(b, e, 10u, s->r1)) return 0;
    if (!aoc_emit_arg_x(b, e, 11u, s->r2)) return 0;
    if (!aoc_emit_mov64(b, e, 16u, (uint64_t)hook_va)) return 0;
    if (!aoc_emit_mov64(b, e, 17u, (uint64_t)e->bridge_va)) return 0;
    if (!aoc_emit_w(b, e, 0xd63f0220u)) return 0;
    if (with_flush) {
        if (!aoc_emit_w(b, e, 0xb4000051u)) return 0;
        uint32_t bl = aoc_b_word(e->cur, e->tramp_off, 1);
        if (!bl || !aoc_emit_w(b, e, bl)) return 0;
    }
    if (!aoc_emit_seq(b, e, pop48, 4u)) return 0;
    if (!s->post_old && !aoc_emit_w(b, e, s->old_insn)) return 0;
    uint32_t br = aoc_b_word(e->cur, s->site + 4u, 0);
    uint32_t pb = aoc_b_word(s->site, stub_off, 0);
    if (!br || !pb || !aoc_emit_w(b, e, br)) return 0;
    aoc_wr32(b, s->site, pb);
    return 1;
}

static inline uintptr_t aoc_rd_ptr(uintptr_t base, size_t off) {
    uintptr_t v = 0;
    if (base < 0x10000ULL || base + off < base) return 0;
    return ktgl_read_mem(base + off, &v, sizeof v, &g_aoc_bounds) ? v : 0;
}

static inline uint32_t aoc_rd_u32(uintptr_t base, size_t off) {
    uint32_t v = 0;
    if (base < 0x10000ULL || base + off < base) return 0;
    return ktgl_read_mem(base + off, &v, sizeof v, &g_aoc_bounds) ? v : 0;
}

static uintptr_t ktgl_aoc_on_pane(uintptr_t pane, uintptr_t u1, uintptr_t u2) {
    (void)u1; (void)u2;
    if (!ktgl_aoc_live_on()) return 0u;
    t_aoc_tag.cur_pane = pane ? (const void *)pane : NULL;
    return 0u;
}

static uintptr_t ktgl_aoc_on_submit(uintptr_t snap_node, uintptr_t live_node, uintptr_t mgr) {
    (void)mgr;
    if (!ktgl_aoc_live_on() || !ktgl_ptr_aligned((const void *)snap_node)) return 0u;
    if (!t_aoc_tag.cur_pane && live_node == snap_node) {
        ktgl_snap_del(&g_aoc_snap_map, (const void *)snap_node);
        return 0u;
    }
    const void *pane = (t_aoc_tag.cur_pane && ktgl_valid_pane(t_aoc_tag.cur_pane, &g_aoc_bounds))
        ? t_aoc_tag.cur_pane
        : ((live_node != snap_node) ? ktgl_resolve_live_pane((const void *)live_node, &g_aoc_bounds) : NULL);
    int cls = pane ? ktgl_classify_pane_cached(pane, &g_aoc_bounds, &t_aoc_cache) : 0;
    uint8_t wid = cls >= 0 ? (uint8_t)cls : 0u;
    ktgl_snap_put(&g_aoc_snap_map, (const void *)snap_node, wid);
    return 0u;
}

static uintptr_t ktgl_aoc_mgr_enter(uintptr_t mgr, uintptr_t u1, uintptr_t u2) {
    (void)u1; (void)u2;
    static _Atomic uint32_t s_aoc_enter = 0u;
    if (((atomic_fetch_add_explicit(&s_aoc_enter, 1u, memory_order_relaxed) + 1u) & 63u) == 0u) {
        (void)ktgl_pg_bump_epoch();
    }
    t_aoc_tag.mgr = mgr;
    if (!ktgl_aoc_live_on()) {
        if (t_aoc_tag.wid != 0u) { t_aoc_tag.wid = 0u; totk_nvn_set_wid(0u); }
        return 0u;
    }
    uintptr_t ctx = aoc_rd_ptr(mgr, AOC_MGR_CTX_OFF);
    t_aoc_tag.wid = 0u;
    if (ctx >= 0x10000ULL && aoc_rd_u32(ctx, AOC_CTX_BATCH_OFF) == 0u) totk_nvn_set_wid(0u);
    return 0u;
}

static uintptr_t ktgl_aoc_pre_node(uintptr_t node_obj, uintptr_t mgr, uintptr_t unused) {
    (void)unused;
    if (!ktgl_aoc_live_on()) {
        if (t_aoc_tag.wid != 0u) { t_aoc_tag.wid = 0u; totk_nvn_set_wid(0u); }
        return 0u;
    }
    int cls = ktgl_snap_get(&g_aoc_snap_map, (const void *)node_obj);
    uint8_t wid = cls >= 0 ? (uint8_t)cls : 0u;
    if (wid == t_aoc_tag.wid) return 0u;
    uintptr_t eff_mgr = t_aoc_tag.mgr ? t_aoc_tag.mgr : mgr;
    uintptr_t ctx = aoc_rd_ptr(eff_mgr, AOC_MGR_CTX_OFF);
    if (ctx >= 0x10000ULL && aoc_rd_u32(ctx, AOC_CTX_BATCH_OFF) != 0u) {
        t_aoc_tag.pend_wid = wid;
        return ctx;
    }
    t_aoc_tag.wid = wid;
    totk_nvn_set_wid(wid);
    return 0u;
}

static uintptr_t ktgl_aoc_mgr_exit(uintptr_t mgr, uintptr_t u1, uintptr_t u2) {
    (void)u1; (void)u2;
    uintptr_t eff_mgr = t_aoc_tag.mgr ? t_aoc_tag.mgr : mgr;
    t_aoc_tag.mgr = 0u;
    t_aoc_tag.cur_pane = NULL;
    uintptr_t ctx = aoc_rd_ptr(eff_mgr, AOC_MGR_CTX_OFF);
    if (ctx >= 0x10000ULL && aoc_rd_u32(ctx, AOC_CTX_BATCH_OFF) != 0u && t_aoc_tag.wid != 0u) {
        t_aoc_tag.pend_wid = 0u;
        return ctx;
    }
    t_aoc_tag.wid = 0u;
    totk_nvn_set_wid(0u);
    return 0u;
}

static uintptr_t ktgl_aoc_post_flush(uintptr_t ctx, uintptr_t u1, uintptr_t u2) {
    (void)ctx; (void)u1; (void)u2;
    t_aoc_tag.wid = t_aoc_tag.pend_wid;
    totk_nvn_set_wid(t_aoc_tag.wid);
    return 0u;
}

static inline uintptr_t aoc_hook_va(uint8_t hk) {
    switch (hk) {
    case AOC_HK_PANE:   return (uintptr_t)ktgl_aoc_on_pane;
    case AOC_HK_SUBMIT: return (uintptr_t)ktgl_aoc_on_submit;
    case AOC_HK_ENTER:  return (uintptr_t)ktgl_aoc_mgr_enter;
    case AOC_HK_NODE:   return (uintptr_t)ktgl_aoc_pre_node;
    case AOC_HK_EXIT:   return (uintptr_t)ktgl_aoc_mgr_exit;
    default:            return 0u;
    }
}

static inline void ktgl_aoc_reset_for_test(void) {
    ktgl_snap_reset(&g_aoc_snap_map);
    memset(&t_aoc_tag, 0, sizeof t_aoc_tag);
    memset(&t_aoc_cache, 0, sizeof t_aoc_cache);
}

static inline int ktgl_aoc_patch_raw(
    uint8_t *base, uint64_t text_end, uint64_t ro_off, uint64_t data_off, uint64_t mod_end) {
    if (!base) return -1;
    uint64_t pool_lo = ((text_end + 15ULL) & ~15ULL) + 64ULL;
    uint64_t pool_hi = (text_end + 0x3fffULL) & ~0x3fffULL;
    if (pool_hi <= pool_lo) return -1;
    for (uint64_t p = pool_lo; p < pool_hi; p++) if (base[p] != 0u) return -1;
    for (size_t i = 0; i < AOC_SITE_COUNT; i++) {
        if (k_aoc_sites[i].site + 4u > text_end) return -1;
        if (aoc_rd32(base, k_aoc_sites[i].site) != k_aoc_sites[i].old_insn) return -1;
    }
    uint64_t mhi = mod_end > data_off ? mod_end : data_off;
    g_aoc_bounds.ro_lo = (uintptr_t)base + ro_off;
    g_aoc_bounds.ro_hi = (uintptr_t)base + ktgl_nso_vtable_end(base, data_off, mhi);
    g_aoc_bounds.mod_lo = (uintptr_t)base;
    g_aoc_bounds.mod_hi = (uintptr_t)base + mhi;
    g_aoc_bounds.mem_lo = 0x10000ULL;
    g_aoc_bounds.mem_hi = (1ULL << 48);
    KtglAocEmit emit = {
        .pool_lo = pool_lo, .pool_hi = pool_hi, .cur = pool_lo,
        .flush_off = AOC_FLUSH_OFF,
        .bridge_va = (uintptr_t)hwaoc_ktgl_ui_bridge,
        .post_flush_va = (uintptr_t)ktgl_aoc_post_flush,
    };
    if (!aoc_emit_flush_trampoline(base, &emit)) return -1;
    for (size_t i = 0; i < AOC_SITE_COUNT; i++) {
        uint8_t hk = k_aoc_sites[i].hook_kind;
        int with_flush = (hk == AOC_HK_NODE || hk == AOC_HK_EXIT);
        if (!aoc_emit_site_stub(base, &emit, &k_aoc_sites[i], aoc_hook_va(hk), with_flush)) return -1;
    }
    totk_nvn_enable_wid();
    return 1;
}
