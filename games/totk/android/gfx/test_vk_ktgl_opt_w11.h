#ifndef TEST_VK_KTGL_OPT_W11_H
#define TEST_VK_KTGL_OPT_W11_H

#include "vk_state_fast.h"
#include "vk_record.h"
#include "nvn_hmap.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint64_t host_now_ns(void);
static volatile uint64_t w11_sink;

typedef struct {
    blend_st blend[8]; color_st color; chmask_st chmask; ms_st ms; poly_st poly; ds_st ds;
    int pipe_dirty; uint64_t dyn_rev;
} w11_pso_st;

static inline int w11_blend_eq(const blend_st *a, const blend_st *b) {
    return a->target == b->target && a->src_rgb == b->src_rgb && a->dst_rgb == b->dst_rgb &&
           a->src_a == b->src_a && a->dst_a == b->dst_a && a->eq_rgb == b->eq_rgb && a->eq_a == b->eq_a;
}

static inline int w11_poly_eq(const poly_st *a, const poly_st *b) {
    return a->cull == b->cull && a->front == b->front && a->mode == b->mode && a->offset_en == b->offset_en;
}

static void w11_apply_st(w11_pso_st *s, int32_t kind, const void *bytes) {
    int ch = 1;
    if (kind == 0) { blend_st bs; memcpy(&bs, bytes, sizeof bs);
        ch = !w11_blend_eq(&s->blend[bs.target & 7], &bs); s->blend[bs.target & 7] = bs; }
    else if (kind == 4) { poly_st p; memcpy(&p, bytes, sizeof p);
        ch = !w11_poly_eq(&s->poly, &p); s->poly = p; if (ch) s->dyn_rev++; }
    if (kind < 5 && totk_state_dirty_gate(ch)) s->pipe_dirty = 1;
}

static int test_w11_pso_and_dyn(void) {
    w11_pso_st s = {0}; s.dyn_rev = 1u;
    blend_st bs = {0u, 2u, 3u, 2u, 3u, 0u, 0u}; poly_st po = {1u, 0u, 2u, 0u};
    w11_apply_st(&s, 0, &bs); w11_apply_st(&s, 4, &po);
    if (!s.pipe_dirty) return 1;
    s.pipe_dirty = 0;
    w11_apply_st(&s, 0, &bs); w11_apply_st(&s, 4, &po);
    if (s.pipe_dirty) return 2;
    bs.src_rgb = 5u; w11_apply_st(&s, 0, &bs);
    if (!s.pipe_dirty) return 3;

    totk_dyn_memo dm = {0};
    totk_dyn_rpass_key rk = {1280u, 720u, 1280u, 720u, 1280u, 720u, 126, 0, 0, 0};
    vk_record_dynamic d0 = {0}, d1 = {0}, out = {0};
    d0.vp = (VkViewport){0.f, 720.f, 1280.f, -720.f, 0.f, 1.f}; d0.sc = (VkRect2D){{0, 0}, {1280u, 720u}}; d0.topo = 3u;
    d1 = d0; d1.sc.extent.width = 640u;
    totk_dyn_memo_put(&dm, s.dyn_rev, &rk, 0, 6, &d0, sizeof d0);
    if (!totk_dyn_memo_get(&dm, s.dyn_rev, &rk, 0, 6, &out, sizeof out) || memcmp(&out, &d0, sizeof out)) return 4;
    po.front = 1u; w11_apply_st(&s, 4, &po);
    if (totk_dyn_memo_get(&dm, s.dyn_rev, &rk, 0, 6, &out, sizeof out)) return 5;
    totk_dyn_memo_put(&dm, s.dyn_rev, &rk, 0, 6, &d1, sizeof d1);
    if (!totk_dyn_memo_get(&dm, s.dyn_rev, &rk, 0, 6, &out, sizeof out) || out.sc.extent.width != 640u) return 6;
    return 0;
}

static uint64_t w11_ref_hash(const void *k, uint32_t len) {
    const uint8_t *p = k; uint64_t h = 0x9e3779b97f4a7c15ull ^ len;
    for (; len >= 8; p += 8, len -= 8) { uint64_t w; memcpy(&w, p, 8); h = (h ^ w) * 0xff51afd7ed558ccdull; h ^= h >> 32; }
    if (len) { uint64_t w = 0; memcpy(&w, p, len); h = (h ^ w) * 0xff51afd7ed558ccdull; }
    h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ull; h ^= h >> 33;
    return h;
}

static int test_w11_hmap(void) {
    nvn_hmap m8, m16; nvn_hm_init(&m8, 8u); nvn_hm_init(&m16, 16u);
    uint64_t k16[256][2]; void *v16[256];
    for (uint32_t i = 0; i < 256u; i++) {
        uint64_t k = 0x5000000000000000ull + i * 31u;
        k16[i][0] = k; k16[i][1] = 0x9000000000000000ull + (i & 7u);
        if (w11_ref_hash(&k, 8u) != nvn_hm_hash(&k, 8u) || w11_ref_hash(k16[i], 16u) != nvn_hm_hash(k16[i], 16u)) return 1;
        v16[i] = (void *)(uintptr_t)(i + 1u);
        if (nvn_hm_put(&m8, &k, v16[i]) || nvn_hm_put(&m16, k16[i], v16[i])) return 2;
    }
    for (uint32_t i = 0; i < 256u; i++) {
        uint64_t k = 0x5000000000000000ull + i * 31u;
        uint64_t miss[2] = {k16[i][0], k16[i][1] ^ 1ull};
        if (nvn_hm_get(&m8, &k) != v16[i] || nvn_hm_get(&m16, k16[i]) != v16[i] || nvn_hm_get(&m16, miss) != NULL) {
            free(m8.keys); free(m8.vals); free(m16.keys); free(m16.vals); return 3;
        }
    }
    free(m8.keys); free(m8.vals); free(m16.keys); free(m16.vals);
    return 0;
}

static volatile float w11_vp[4] = {0.f, 0.f, 1280.f, 720.f};
static volatile float w11_dr[2] = {0.f, 1.f};
static volatile int32_t w11_sc[4] = {0, 0, 1280, 720};
static volatile float w11_po[3] = {1.f, 0.f, 2.f};
static volatile uint32_t w11_df_v = 4u, w11_prim_v = 4u;

static void w11_pack_ref(vk_record_dynamic *out, uint32_t rw, uint32_t rh) {
    memset(out, 0, sizeof *out);
    float vx = w11_vp[0], vy = w11_vp[1], vw = w11_vp[2], vh = w11_vp[3];
    float n0 = w11_dr[0], n1 = w11_dr[1];
    out->vp = (VkViewport){vx, vy + vh, vw, -vh, n0 < 0.f ? 0.f : n0, n1 > 1.f ? 1.f : n1};
    int32_t x0 = w11_sc[0] > 0 ? w11_sc[0] : 0, y0 = w11_sc[1] > 0 ? w11_sc[1] : 0;
    int32_t x1 = (int32_t)rw < w11_sc[0] + w11_sc[2] ? (int32_t)rw : w11_sc[0] + w11_sc[2];
    int32_t y1 = (int32_t)rh < w11_sc[1] + w11_sc[3] ? (int32_t)rh : w11_sc[1] + w11_sc[3];
    out->sc = (VkRect2D){{x0, y0}, {(uint32_t)(x1 > x0 ? x1 - x0 : 0), (uint32_t)(y1 > y0 ? y1 - y0 : 0)}};
    out->bias_en = 1u; out->bias[0] = w11_po[1]; out->bias[1] = w11_po[2]; out->bias[2] = w11_po[0];
    out->topo = w11_prim_v == 4u ? 3u : 0u; out->dc = w11_df_v;
    out->cmask[0] = out->cmask[1] = out->wmask[0] = out->wmask[1] = 0xffu;
}

static uint64_t w11_bench_dyn(int fast, uint64_t *hits_o, uint64_t *sum_o) {
    totk_dyn_memo dm = {0};
    totk_dyn_rpass_key rk = {1280u, 720u, 1280u, 720u, 1280u, 720u, 126, 0, 0, 0};
    vk_record_dynamic d; w11_pack_ref(&d, rk.w, rk.h);
    totk_dyn_memo_put(&dm, 1u, &rk, 0, 6, &d, sizeof d);
    uint64_t hits = 0, sum = 0, t0 = host_now_ns();
    for (uint32_t i = 0; i < 200000u; i++) {
        vk_record_dynamic cur;
        if (fast && totk_dyn_memo_get(&dm, 1u, &rk, 0, 6, &cur, sizeof cur)) hits++;
        else w11_pack_ref(&cur, rk.w, rk.h);
        sum ^= (uint64_t)(uint32_t)cur.vp.width ^ cur.sc.extent.width ^ cur.topo ^ cur.dc ^ cur.cmask[0];
    }
    uint64_t dt = host_now_ns() - t0; *hits_o = hits; *sum_o = sum; w11_sink ^= sum; return dt;
}

static void *w11_hm_get_generic(const nvn_hmap *m, const void *k) {
    if (!m->n) return NULL;
    uint32_t mask = m->cap - 1u;
    for (uint32_t i = (uint32_t)w11_ref_hash(k, m->klen) & mask, s = 0; s < m->cap; s++, i = (i + 1u) & mask) {
        if (!m->vals[i]) return NULL;
        if (!memcmp(m->keys + (size_t)i * m->klen, k, m->klen)) return m->vals[i];
    }
    return NULL;
}

static uint64_t w11_bench_hmap16(int fast) {
    nvn_hmap m; nvn_hm_init(&m, 16u); uint64_t ks[64][2];
    for (uint32_t i = 0; i < 64u; i++) {
        ks[i][0] = 0xabcdef0100000000ull + i * 17u; ks[i][1] = 0x1234567800000000ull + (i ^ 5u);
        nvn_hm_put(&m, ks[i], (void *)(uintptr_t)(i + 1u));
    }
    uint64_t sum = 0, t0 = host_now_ns();
    for (uint32_t i = 0; i < 200000u; i++) {
        void *v = fast ? nvn_hm_get(&m, ks[i & 63u]) : w11_hm_get_generic(&m, ks[i & 63u]);
        sum += (uintptr_t)v;
    }
    uint64_t dt = host_now_ns() - t0; w11_sink ^= sum; free(m.keys); free(m.vals); return dt;
}

static int run_ktgl_w11_tests(int fault_mode) {
    (void)fault_mode; (void)totk_vk_state_fast_on();
    int r1 = test_w11_pso_and_dyn(), r2 = test_w11_hmap();
    if (r1 || r2) { fprintf(stderr, "FAIL w11 r1=%d r2=%d fault=%d\n", r1, r2, fault_mode); return 1; }
    uint64_t h0 = 0, h1 = 0, s0 = 0, s1 = 0;
    uint64_t dt0 = w11_bench_dyn(0, &h0, &s0), dt1 = w11_bench_dyn(1, &h1, &s1);
    uint64_t ht0 = w11_bench_hmap16(0), ht1 = w11_bench_hmap16(1);
    printf("aoc_w11_dyn_memo: fault=%d ops=200000 base=%.2fms (hits=%llu) fast=%.2fms (hits=%llu) speedup=%.2fx sink=%llu\n",
           fault_mode, (double)dt0 / 1e6, (unsigned long long)h0, (double)dt1 / 1e6, (unsigned long long)h1,
           dt1 ? (double)dt0 / (double)dt1 : 0.0, (unsigned long long)(s0 ^ s1));
    printf("aoc_w11_hmap16: fault=%d ops=200000 generic=%.2fms fast=%.2fms speedup=%.2fx\n",
           fault_mode, (double)ht0 / 1e6, (double)ht1 / 1e6, ht1 ? (double)ht0 / (double)ht1 : 0.0);
    if (fault_mode == 0 && (h0 != 0u || h1 != 200000u || s0 != s1)) return 1;
    return 0;
}

#endif
