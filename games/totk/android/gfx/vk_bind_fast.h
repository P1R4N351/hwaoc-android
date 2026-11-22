/* Fast paths for driver bank zero-skipping, support template copy, stage binding keys, and 2x-unrolled hash_words (Opt 48). */
#ifndef VK_BIND_FAST_H
#define VK_BIND_FAST_H
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TOTK_W46_TEST_HOOKS
extern _Atomic int g_totk_vk_bind_fast_override;
extern _Atomic int g_totk_w46_fault;
static inline void totk_vk_bind_fast_set_for_test(int v) {
    atomic_store_explicit(&g_totk_vk_bind_fast_override, v, memory_order_release);
}
static inline void totk_w46_set_fault(int f) {
    atomic_store_explicit(&g_totk_w46_fault, f, memory_order_release);
}
static inline int totk_w46_fault_mode(void) {
    int f = atomic_load_explicit(&g_totk_w46_fault, memory_order_relaxed);
    if (f >= 0) return f;
    const char *e = getenv("TOTK_W46_FAULT");
    int v = (e && *e) ? atoi(e) : 0;
    atomic_store_explicit(&g_totk_w46_fault, v, memory_order_relaxed);
    return v;
}
#else
static inline int totk_w46_fault_mode(void) { return 0; }
#endif

static inline int totk_vk_bind_fast_on(void) {
#ifdef TOTK_W46_TEST_HOOKS
    int ov = atomic_load_explicit(&g_totk_vk_bind_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
#endif
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("TOTK_VK_BIND_FAST");
        v = !(e && strcmp(e, "0") == 0);
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
}

typedef struct {
    int32_t stage, index;
    uint64_t addr, size;
} totk_bind_buf_slot_t;

typedef struct {
    uint16_t binding, count;
    uint8_t kind, dtype, target, rw, is_int;
    int16_t reg, sreg;
    uint16_t off;
    uint64_t extent;
} totk_vk_bind_rec_t;

typedef struct {
    uint64_t value[2];
} totk_rec_bind_val_t;

enum {
    TOTK_RB_SUPPORT = 1, TOTK_RB_C1 = 3, TOTK_RB_DRV0 = 4,
    TOTK_RB_DRV2 = 5, TOTK_RB_SBUF = 6, TOTK_RB_IMG = 11, TOTK_RB_ALPHA = 14
};
enum { TOTK_DT_SAMPLER = 0, TOTK_DT_COMBINED = 1, TOTK_DT_UBO = 6, TOTK_DT_SSBO = 7 };

static inline void totk_drv_bank0_fast(
    uint8_t *d, int stage, const uint32_t grid[3],
    const totk_bind_buf_slot_t *ssbo_st, const totk_bind_buf_slot_t *ubo5) {
    int base = stage == 0 ? 0x110 : stage == 5 ? 0x310 : stage == 1 ? 0x510 : -1;
    if (base >= 0 && ssbo_st) {
        for (int r = 0; r < 16; r++) {
            uint64_t a = ssbo_st[r].addr;
            uint32_t n = (uint32_t)ssbo_st[r].size;
            if ((a | n) && !(totk_w46_fault_mode() == 1 && r == 15)) {
                memcpy(d + base + 16 * r, &a, 8);
                memcpy(d + base + 16 * r + 8, &n, 4);
            }
        }
    }
    if (stage == 5) {
        if (grid) memcpy(d + 0x200, grid, 12);
        if (ubo5) {
            for (int r = 0; r < 16; r++) {
                uint64_t a = ubo5[r].addr;
                uint32_t n = (uint32_t)ubo5[r].size;
                if (a | n) {
                    memcpy(d + 0x210 + 16 * r, &a, 8);
                    memcpy(d + 0x210 + 16 * r + 8, &n, 4);
                }
            }
        }
    } else if (stage == 0) {
        const float ps_max = 2047.9375f;
        memcpy(d + 0x610, &ps_max, sizeof ps_max);
    }
}

static inline void totk_drv_bank2_fast(uint8_t *d, const uint64_t *tex_st, const uint64_t *samp_st) {
    int skip32 = (totk_w46_fault_mode() == 2);
    if (tex_st) {
        for (int g = 0; g < 128; g += 8) {
            const uint64_t *p = tex_st + g;
            if (!(p[0] | p[1] | p[2] | p[3] | p[4] | p[5] | p[6] | p[7])) continue;
            for (int k = 0; k < 8; k++) {
                uint32_t th = (uint32_t)p[k];
                if (!th) continue;
                memcpy(d + 0x168 + 8 * (g + k), &th, 4);
                if (g + k < 32 && !skip32) memcpy(d + 0x20 + 8 * (g + k), &th, 4);
            }
        }
    }
    if (samp_st) {
        for (int g = 0; g < 128; g += 8) {
            const uint64_t *p = samp_st + g;
            if (!(p[0] | p[1] | p[2] | p[3] | p[4] | p[5] | p[6] | p[7])) continue;
            for (int k = 0; k < 8; k++) {
                uint32_t sh_ = (uint32_t)p[k];
                if (sh_) memcpy(d + 0x568 + 8 * (g + k), &sh_, 4);
            }
        }
    }
}

static inline void totk_drv_bank_fill_fast(
    uint8_t *d, int bank, int stage, const uint32_t grid[3],
    const totk_bind_buf_slot_t *ssbo_st, const totk_bind_buf_slot_t *ubo5,
    const uint64_t *tex_st, const uint64_t *samp_st) {
    memset(d, 0, 0xA00u);
    if (bank == 0) totk_drv_bank0_fast(d, stage, grid, ssbo_st, ubo5);
    else totk_drv_bank2_fast(d, tex_st, samp_st);
}

static uint8_t g_totk_sup_tpl[1392];
static pthread_once_t g_totk_sup_once = PTHREAD_ONCE_INIT;

static void totk_sup_init_tpl(void) {
    memset(g_totk_sup_tpl, 0, sizeof g_totk_sup_tpl);
    const float one = 1.0f;
    for (int i = 0; i < 73; i++) memcpy(g_totk_sup_tpl + 192 + 16 * i, &one, 4);
}

static inline void totk_support_fill_fast(uint8_t *d, uint32_t w, uint32_t h) {
    (void)pthread_once(&g_totk_sup_once, totk_sup_init_tpl);
    memcpy(d, g_totk_sup_tpl, sizeof g_totk_sup_tpl);
    float v[8] = {
        2.0f / (w ? w : 1u), 2.0f / (h ? h : 1u), 1.0f, 1.0f,
        (float)w, (float)h, 1.0f, 1.0f
    };
    memcpy(d + 144, v, sizeof v);
    if (totk_w46_fault_mode() == 3) memset(d + 192, 0, 4);
}

typedef struct {
    const totk_bind_buf_slot_t *ubo;
    const totk_bind_buf_slot_t *ssbo;
    const uint64_t *tex;
    const uint64_t *samp;
    const uint64_t *img;
    uint64_t drv0_rev;
    uint64_t drv2_rev;
    uint64_t alpha_bits;
} totk_bind_stage_ctx_t;

static inline void totk_bind_eval_one_fast(
    const totk_bind_stage_ctx_t *ctx, const totk_vk_bind_rec_t *b,
    uint64_t *v0_out, uint64_t *v1_out) {
    uint64_t v0 = 0ULL, v1 = 0ULL;
    uint8_t kind = b->kind, dtype = b->dtype;
    if (kind == TOTK_RB_SUPPORT || kind == TOTK_RB_C1) { *v0_out = 0ULL; *v1_out = 0ULL; return; }
    if (kind == TOTK_RB_ALPHA) { *v0_out = ctx->alpha_bits; *v1_out = 0ULL; return; }
    if ((unsigned)(dtype - TOTK_DT_UBO) <= 1u) {
        if (kind == TOTK_RB_DRV0) v0 = ctx->drv0_rev;
        else if (kind == TOTK_RB_DRV2) v0 = ctx->drv2_rev;
        else if ((unsigned)(int)b->reg < 16u) {
            const totk_bind_buf_slot_t *u = (kind == TOTK_RB_SBUF) ? &ctx->ssbo[b->reg] : &ctx->ubo[b->reg];
            v0 = u->addr;
            v1 = u->size;
        }
    } else {
        int reg = b->reg;
        if (dtype == TOTK_DT_SAMPLER) {
            if ((unsigned)reg < 128u) v0 = (b->sreg == -2 ? ctx->tex[reg] : ctx->samp[reg]) >> 20;
        } else {
            v0 = (kind == TOTK_RB_IMG)
                ? ((unsigned)reg < 32u ? ctx->img[reg] : 0ULL)
                : ((unsigned)reg < 128u ? ctx->tex[reg] : 0ULL);
            if (dtype == TOTK_DT_COMBINED) {
                if (totk_w46_fault_mode() == 4 && b->sreg >= 0) v1 = v0 >> 20;
                else v1 = (unsigned)(int)b->sreg < 128u ? (ctx->samp[b->sreg] >> 20) : (v0 >> 20);
            }
        }
    }
    *v0_out = v0;
    *v1_out = v1;
}

static inline void totk_bind_fill_fast(
    const totk_bind_stage_ctx_t *ctx, const totk_vk_bind_rec_t *bs, int nb, totk_rec_bind_val_t *out) {
    for (int i = 0; i < nb; i++) totk_bind_eval_one_fast(ctx, &bs[i], &out[i].value[0], &out[i].value[1]);
}

static inline int totk_bind_eq_fast(
    const totk_bind_stage_ctx_t *ctx, const totk_vk_bind_rec_t *bs, int nb, const totk_rec_bind_val_t *prev) {
    for (int i = 0; i < nb; i++) {
        uint64_t v0 = 0ULL, v1 = 0ULL;
        totk_bind_eval_one_fast(ctx, &bs[i], &v0, &v1);
        if (((v0 ^ prev[i].value[0]) | (v1 ^ prev[i].value[1])) != 0ULL) return 0;
    }
    return 1;
}

static inline void totk_hash_words_fast(const void *p, size_t n, uint64_t h[2]) {
    const uint8_t *b = (const uint8_t *)p;
    size_t i = 0;
    uint64_t a = h[0], c = h[1];
    for (; i + 16u <= n; i += 16u) {
        uint64_t w0, w1;
        memcpy(&w0, b + i, 8);
        memcpy(&w1, b + i + 8u, 8);
        a = (a ^ w0) * UINT64_C(0x9e3779b97f4a7c15); a ^= a >> 32;
        c = (c + w0) * UINT64_C(0xc2b2ae3d27d4eb4f); c = (c << 31) | (c >> 33);
        a = (a ^ w1) * UINT64_C(0x9e3779b97f4a7c15); a ^= a >> 32;
        c = (c + w1) * UINT64_C(0xc2b2ae3d27d4eb4f); c = (c << 31) | (c >> 33);
    }
    for (; i + 8u <= n; i += 8u) {
        uint64_t w;
        memcpy(&w, b + i, 8);
        a = (a ^ w) * UINT64_C(0x9e3779b97f4a7c15); a ^= a >> 32;
        c = (c + w) * UINT64_C(0xc2b2ae3d27d4eb4f); c = (c << 31) | (c >> 33);
    }
    for (; i < n; i++) {
        a = (a ^ b[i]) * UINT64_C(0x100000001b3);
        c = (c + b[i]) * UINT64_C(0xc2b2ae3d27d4eb4f); c = (c << 31) | (c >> 33);
    }
    if (totk_w46_fault_mode() == 5 && n >= 16u) a ^= 1ULL;
    h[0] = a; h[1] = c;
}
#endif
