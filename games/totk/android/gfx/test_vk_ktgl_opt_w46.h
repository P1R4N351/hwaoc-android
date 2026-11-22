#ifndef TEST_VK_KTGL_OPT_W46_H
#define TEST_VK_KTGL_OPT_W46_H
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "vk_bind_fast.h"

_Atomic int g_totk_vk_bind_fast_override = -1;
_Atomic int g_totk_w46_fault = -1;
static int g_w46_fail = 0;

#define W46_EXPECT_EQ(m, w, g) do { \
    uint64_t wa_ = (uint64_t)(w), ga_ = (uint64_t)(g); \
    if (wa_ != ga_) { \
        g_w46_fail++; \
        fprintf(stderr, "FAIL %s:%d %s want=%llu got=%llu\n", __FILE__, __LINE__, m, (unsigned long long)wa_, (unsigned long long)ga_); \
    } \
} while (0)

static uint64_t w46_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static void w46_ref_bank_fill(
    uint8_t *d, int bank, int stage, const uint32_t grid[3],
    const totk_bind_buf_slot_t *ssbo, const totk_bind_buf_slot_t *ubo5,
    const uint64_t *tex, const uint64_t *samp) {
    memset(d, 0, 0xA00u);
    if (bank == 0) {
        int base = stage == 0 ? 0x110 : stage == 5 ? 0x310 : stage == 1 ? 0x510 : -1;
        if (base >= 0) for (int r = 0; r < 16; r++) {
            uint64_t a = ssbo[r].addr; uint32_t n = (uint32_t)ssbo[r].size;
            memcpy(d + base + 16 * r, &a, 8); memcpy(d + base + 16 * r + 8, &n, 4);
        }
        if (stage == 5) {
            if (grid) memcpy(d + 0x200, grid, 12);
            for (int r = 0; r < 16; r++) {
                uint64_t a = ubo5[r].addr; uint32_t n = (uint32_t)ubo5[r].size;
                memcpy(d + 0x210 + 16 * r, &a, 8); memcpy(d + 0x210 + 16 * r + 8, &n, 4);
            }
        } else if (stage == 0) { const float ps_max = 2047.9375f; memcpy(d + 0x610, &ps_max, sizeof ps_max); }
    } else {
        for (int r = 0; r < 128; r++) {
            uint32_t th = (uint32_t)tex[r], sh_ = (uint32_t)samp[r];
            memcpy(d + 0x168 + 8 * r, &th, 4); memcpy(d + 0x568 + 8 * r, &sh_, 4);
        }
        for (int r = 0; r < 32; r++) { uint32_t th = (uint32_t)tex[r]; memcpy(d + 0x20 + 8 * r, &th, 4); }
    }
}

static void w46_ref_support_fill(uint8_t *d, uint32_t w, uint32_t h) {
    memset(d, 0, 1392);
    float vi[4] = {2.0f / (w ? w : 1u), 2.0f / (h ? h : 1u), 1.0f, 1.0f};
    float vs[4] = {(float)w, (float)h, 1.0f, 1.0f};
    memcpy(d + 144, vi, 16); memcpy(d + 160, vs, 16);
    for (int i = 0; i < 73; i++) { float one = 1.0f; memcpy(d + 192 + 16 * i, &one, 4); }
}

static void w46_ref_hash_words(const void *p, size_t n, uint64_t h[2]) {
    const uint8_t *b = (const uint8_t *)p; size_t i = 0; uint64_t a = h[0], c = h[1];
    for (; i + 8 <= n; i += 8) {
        uint64_t w; memcpy(&w, b + i, 8);
        a = (a ^ w) * 0x9e3779b97f4a7c15ull; a ^= a >> 32;
        c = (c + w) * 0xc2b2ae3d27d4eb4full; c = c << 31 | c >> 33;
    }
    for (; i < n; i++) {
        a = (a ^ b[i]) * 0x100000001b3ull;
        c = (c + b[i]) * 0xc2b2ae3d27d4eb4full; c = c << 31 | c >> 33;
    }
    h[0] = a; h[1] = c;
}

static void w46_ref_binding_of(const totk_bind_stage_ctx_t *ctx, const totk_vk_bind_rec_t *b, totk_rec_bind_val_t *r) {
    *r = (totk_rec_bind_val_t){{0}};
    if (b->kind == TOTK_RB_SUPPORT || b->kind == TOTK_RB_C1) return;
    if (b->kind == TOTK_RB_ALPHA) { r->value[0] = ctx->alpha_bits; return; }
    if (b->dtype == TOTK_DT_UBO || b->dtype == TOTK_DT_SSBO) {
        if (b->kind == TOTK_RB_DRV0 || b->kind == TOTK_RB_DRV2)
            r->value[0] = (b->kind == TOTK_RB_DRV2) ? ctx->drv2_rev : ctx->drv0_rev;
        else if (b->reg >= 0 && b->reg < 16) {
            const totk_bind_buf_slot_t *u = b->kind == TOTK_RB_SBUF ? &ctx->ssbo[b->reg] : &ctx->ubo[b->reg];
            r->value[0] = u->addr; r->value[1] = u->size;
        }
    } else {
        uint64_t tex = b->reg >= 0 ? (b->kind == TOTK_RB_IMG ? (b->reg < 32 ? ctx->img[b->reg] : 0) : (b->reg < 128 ? ctx->tex[b->reg] : 0)) : 0;
        r->value[0] = tex;
        if (b->dtype == TOTK_DT_SAMPLER)
            r->value[0] = b->reg >= 0 && b->reg < 128 ? (b->sreg == -2 ? ctx->tex[b->reg] : ctx->samp[b->reg]) >> 20 : 0;
        else if (b->dtype == TOTK_DT_COMBINED)
            r->value[1] = b->sreg >= 0 && b->sreg < 128 ? ctx->samp[b->sreg] >> 20 : tex >> 20;
    }
}

static void w46_test_bank_and_support(void) {
    static totk_bind_buf_slot_t ssbo[16], ubo5[16];
    static uint64_t tex[128], samp[128];
    uint8_t ref[0xA00], fast[0xA00], sref[1392], sfast[1392];
    memset(ssbo, 0, sizeof ssbo); memset(ubo5, 0, sizeof ubo5);
    memset(tex, 0, sizeof tex); memset(samp, 0, sizeof samp);
    ssbo[0].addr = 0x10000ULL; ssbo[0].size = 256u; ssbo[15].addr = 0x20000ULL; ssbo[15].size = 512u;
    ubo5[3].addr = 0x30000ULL; ubo5[3].size = 1024u;
    tex[2] = 0x11ULL | (0x22ULL << 20); tex[97] = 0x33ULL | (0x44ULL << 20);
    samp[2] = 0x55ULL << 20; samp[97] = 0x66ULL << 20;
    uint32_t grid[3] = {8u, 4u, 2u};
    int stages[4] = {0, 1, 2, 5};
    for (int si = 0; si < 4; si++) {
        for (int bank = 0; bank <= 2; bank += 2) {
            w46_ref_bank_fill(ref, bank, stages[si], grid, ssbo, ubo5, tex, samp);
            totk_drv_bank_fill_fast(fast, bank, stages[si], grid, ssbo, ubo5, tex, samp);
            W46_EXPECT_EQ("bank parity", 0, memcmp(ref, fast, sizeof ref));
        }
    }
    w46_ref_support_fill(sref, 1280u, 720u);
    totk_support_fill_fast(sfast, 1280u, 720u);
    W46_EXPECT_EQ("support 1280x720", 0, memcmp(sref, sfast, sizeof sref));
    w46_ref_support_fill(sref, 0u, 0u);
    totk_support_fill_fast(sfast, 0u, 0u);
    W46_EXPECT_EQ("support 0x0", 0, memcmp(sref, sfast, sizeof sref));
}

static void w46_test_bind_and_hash(void) {
    totk_bind_buf_slot_t ubo[16] = {0}, ssbo[16] = {0};
    uint64_t tex[128] = {0}, samp[128] = {0}, img[32] = {0};
    for (int i = 0; i < 16; i++) {
        ubo[i] = (totk_bind_buf_slot_t){1, i, 0x10000ULL + (uint64_t)i * 64u, 256u + (uint64_t)i};
        ssbo[i] = (totk_bind_buf_slot_t){1, i, 0x80000ULL + (uint64_t)i * 128u, 512u + (uint64_t)i};
    }
    for (int i = 0; i < 32; i++) img[i] = (uint64_t)(i + 100);
    for (int i = 0; i < 128; i++) { tex[i] = (uint64_t)(i + 1) | ((uint64_t)(i + 3) << 20); samp[i] = (uint64_t)(i + 7) << 20; }
    totk_bind_stage_ctx_t ctx = {ubo, ssbo, tex, samp, img, 17ULL, 29ULL, 0x3f800000ULL};
    totk_vk_bind_rec_t bs[10] = {
        {.kind = TOTK_RB_SUPPORT, .dtype = TOTK_DT_UBO, .reg = -1},
        {.kind = TOTK_RB_ALPHA, .dtype = TOTK_DT_UBO, .reg = -1},
        {.kind = TOTK_RB_DRV0, .dtype = TOTK_DT_UBO, .reg = 0},
        {.kind = TOTK_RB_DRV2, .dtype = TOTK_DT_UBO, .reg = 2},
        {.kind = 2, .dtype = TOTK_DT_UBO, .reg = 5},
        {.kind = TOTK_RB_SBUF, .dtype = TOTK_DT_SSBO, .reg = 7},
        {.kind = 12, .dtype = TOTK_DT_SAMPLER, .reg = 9, .sreg = -2},
        {.kind = 12, .dtype = TOTK_DT_SAMPLER, .reg = 10, .sreg = 0},
        {.kind = 9, .dtype = TOTK_DT_COMBINED, .reg = 11, .sreg = 12},
        {.kind = TOTK_RB_IMG, .dtype = 3, .reg = 3},
    };
    totk_rec_bind_val_t ref[10], fast[10];
    for (int i = 0; i < 10; i++) w46_ref_binding_of(&ctx, &bs[i], &ref[i]);
    totk_bind_fill_fast(&ctx, bs, 10, fast);
    W46_EXPECT_EQ("bind fill", 0, memcmp(ref, fast, sizeof ref));
    W46_EXPECT_EQ("bind eq same", 1, totk_bind_eq_fast(&ctx, bs, 10, ref));
    samp[12] += 1ULL << 20;
    W46_EXPECT_EQ("bind eq changed", 0, totk_bind_eq_fast(&ctx, bs, 10, ref));
    uint8_t msg[333];
    for (size_t i = 0; i < sizeof msg; i++) msg[i] = (uint8_t)(i * 37u + 11u);
    for (size_t ln = 0; ln <= sizeof msg; ln++) {
        uint64_t hr[2] = {0x123456789abcdef0ULL, 0x0fedcba987654321ULL}, hf[2] = {hr[0], hr[1]};
        w46_ref_hash_words(msg, ln, hr);
        totk_hash_words_fast(msg, ln, hf);
        W46_EXPECT_EQ("hash h0", hr[0], hf[0]); W46_EXPECT_EQ("hash h1", hr[1], hf[1]);
    }
}

static void *w46_tsan_worker(void *arg) {
    int id = (int)(intptr_t)arg;
    uint8_t ref[1392], buf[1392];
    w46_ref_support_fill(ref, 1920u + (uint32_t)id, 1080u);
    for (int i = 0; i < 200; i++) {
        totk_support_fill_fast(buf, 1920u + (uint32_t)id, 1080u);
        if (memcmp(ref, buf, sizeof buf) != 0) return (void *)(uintptr_t)1;
    }
    return NULL;
}

static void w46_test_tsan(void) {
    pthread_t th[4];
    for (int i = 0; i < 4; i++) pthread_create(&th[i], NULL, w46_tsan_worker, (void *)(intptr_t)i);
    for (int i = 0; i < 4; i++) { void *r = NULL; pthread_join(th[i], &r); W46_EXPECT_EQ("tsan sup", 0, (uint64_t)(uintptr_t)r); }
}

static uint64_t w46_bench_arm(int fast, double *ms_out) {
    static totk_bind_buf_slot_t ubo[16], ssbo[16];
    static uint64_t tex[128], samp[128], img[32];
    for (int i = 0; i < 16; i++) {
        ubo[i].addr = 0x10000ULL + (uint64_t)i * 64u; ubo[i].size = 256u;
        ssbo[i].addr = 0x20000ULL + (uint64_t)i * 128u; ssbo[i].size = 512u;
        img[i] = (uint64_t)(i + 100);
    }
    for (int i = 0; i < 16; i++) { tex[i] = (uint64_t)(i + 1) | ((uint64_t)(i + 5) << 20); samp[i] = (uint64_t)(i + 9) << 20; }
    totk_bind_stage_ctx_t ctx = {ubo, ssbo, tex, samp, img, 11ULL, 22ULL, 0x3f800000ULL};
    totk_vk_bind_rec_t bs[16] = {
        {.kind = TOTK_RB_DRV0, .dtype = TOTK_DT_UBO, .reg = 0},
        {.kind = TOTK_RB_DRV2, .dtype = TOTK_DT_UBO, .reg = 2},
        {.kind = 2, .dtype = TOTK_DT_UBO, .reg = 0}, {.kind = 2, .dtype = TOTK_DT_UBO, .reg = 1},
        {.kind = TOTK_RB_SBUF, .dtype = TOTK_DT_SSBO, .reg = 0}, {.kind = TOTK_RB_SBUF, .dtype = TOTK_DT_SSBO, .reg = 1},
        {.kind = 9, .dtype = TOTK_DT_COMBINED, .reg = 0, .sreg = 1}, {.kind = 9, .dtype = TOTK_DT_COMBINED, .reg = 2, .sreg = 3},
        {.kind = 9, .dtype = TOTK_DT_COMBINED, .reg = 4, .sreg = 5}, {.kind = 9, .dtype = TOTK_DT_COMBINED, .reg = 6, .sreg = 7},
        {.kind = 12, .dtype = TOTK_DT_SAMPLER, .reg = 8, .sreg = 0}, {.kind = 12, .dtype = TOTK_DT_SAMPLER, .reg = 9, .sreg = -2},
        {.kind = TOTK_RB_IMG, .dtype = 3, .reg = 0}, {.kind = TOTK_RB_IMG, .dtype = 3, .reg = 1},
        {.kind = TOTK_RB_SUPPORT, .dtype = TOTK_DT_UBO, .reg = -1}, {.kind = TOTK_RB_ALPHA, .dtype = TOTK_DT_UBO, .reg = -1},
    };
    totk_rec_bind_val_t ref[16], cur[16], tmp[16];
    totk_bind_fill_fast(&ctx, bs, 16, ref);
    uint8_t blk[0xA00] = {0}, sup[1392] = {0};
    uint64_t t0 = w46_now_ns(), acc = 0;
    for (uint32_t it = 0; it < 20000u; it++) {
        uint64_t h[2] = {1ULL, 2ULL};
        if (fast) {
            if ((it & 7u) == 0u) { totk_drv_bank_fill_fast(blk, (it & 8u) ? 2 : 0, 1, NULL, ssbo, ubo, tex, samp); totk_support_fill_fast(sup, 1280u, 720u); }
            if ((it & 3u) == 0u) { totk_bind_fill_fast(&ctx, bs, 16, cur); acc += cur[2].value[0] & 0xffu; }
            acc += (uint64_t)totk_bind_eq_fast(&ctx, bs, 16, ref);
            if ((it & 1u) == 0u) { totk_hash_words_fast(blk, 1024u, h); acc += h[0] & 0xffu; }
        } else {
            if ((it & 7u) == 0u) { w46_ref_bank_fill(blk, (it & 8u) ? 2 : 0, 1, NULL, ssbo, ubo, tex, samp); w46_ref_support_fill(sup, 1280u, 720u); }
            if ((it & 3u) == 0u) { for (int j = 0; j < 16; j++) w46_ref_binding_of(&ctx, &bs[j], &cur[j]); acc += cur[2].value[0] & 0xffu; }
            int eq = 1;
            for (int j = 0; j < 16; j++) { w46_ref_binding_of(&ctx, &bs[j], &tmp[j]); if (memcmp(&ref[j], &tmp[j], sizeof ref[j])) eq = 0; }
            acc += (uint64_t)eq;
            if ((it & 1u) == 0u) { w46_ref_hash_words(blk, 1024u, h); acc += h[0] & 0xffu; }
        }
        acc += (uint64_t)blk[0x568] + (uint64_t)sup[192];
    }
    *ms_out = (double)(w46_now_ns() - t0) / 1e6;
    return acc;
}

static int run_ktgl_w46_tests(int fault_mode) {
    g_w46_fail = 0;
    int f = fault_mode;
    const char *envf = getenv("TOTK_W46_FAULT");
    if (envf && *envf) f = atoi(envf); else if (f >= 176 && f <= 180) f = f - 175; else f = 0;
    totk_w46_set_fault(f);
    totk_vk_bind_fast_set_for_test(1);
    w46_test_bank_and_support();
    w46_test_bind_and_hash();
    w46_test_tsan();
    double bms = 0.0, fms = 0.0;
    uint64_t s0 = w46_bench_arm(0, &bms), s1 = w46_bench_arm(1, &fms);
    W46_EXPECT_EQ("bench parity", s0, s1);
    totk_vk_bind_fast_set_for_test(-1);
    totk_w46_set_fault(-1);
    printf("aoc_w46_bind_fast: base_ms=%.3f fast_ms=%.3f speedup=%.2fx sink=%llu %s\n",
           bms, fms, bms / (fms > 0.0 ? fms : 1e-6), (unsigned long long)s1, g_w46_fail ? "FAIL" : "PASS");
    return g_w46_fail != 0;
}
#if defined(TOTK_W46_STANDALONE)
int main(void) { return run_ktgl_w46_tests(0); }
#endif
#endif
