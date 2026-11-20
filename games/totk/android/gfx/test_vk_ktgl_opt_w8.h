#ifndef TEST_VK_KTGL_OPT_W8_H
#define TEST_VK_KTGL_OPT_W8_H

#include "vk_shader_fast.h"
#include "vk_dsc_fast.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint64_t host_now_ns(void);
static volatile uint64_t w8_sink;
static pthread_mutex_t w8_vfix_mu = PTHREAD_MUTEX_INITIALIZER;

typedef struct { uint64_t vs, fs, mod; uint32_t missing; uint8_t kind[32]; } w8_vfix_rec;
typedef struct { uint64_t k0, k1, val; } w8_kv64;

static uint64_t w8_find_colliding_fs(uint64_t vs, uint64_t fs0) {
    uint32_t b0 = totk_sh_mix(vs * 0x9e3779b97f4a7c15ull ^ fs0, TOTK_VFIX_SETS);
    for (uint64_t c = 1; c <= 65536ull; c++) {
        uint64_t fs = fs0 + c * 17ull;
        if (fs != fs0 && totk_sh_mix(vs * 0x9e3779b97f4a7c15ull ^ fs, TOTK_VFIX_SETS) == b0) return fs;
    }
    return 0;
}

static int test_w8_vfix_lay_tmpl(void) {
    totk_vfix_memo4 *vm = calloc(1, sizeof *vm);
    totk_lay_memo4 *lm = calloc(1, sizeof *lm);
    totk_tmpl_memo4 *tm = calloc(1, sizeof *tm);
    if (!vm || !lm || !tm) { free(vm); free(lm); free(tm); return 1; }
    uint8_t kind[32] = {0};
    kind[0] = 1; kind[5] = 2; kind[31] = 3;
    uint64_t packed = totk_pack_vfix_kind((1u << 0) | (1u << 5) | (1u << 31), kind);
    if (packed != ((1ull << 0) | (2ull << 10) | (3ull << 62))) { free(vm); free(lm); free(tm); return 2; }
    uint64_t vs = 0x111100001111ull, fs0 = 0x222200002222ull, fs1 = w8_find_colliding_fs(vs, fs0);
    if (!fs1) { free(vm); free(lm); free(tm); return 3; }
    totk_vfix_memo4_put(vm, vs, fs0, 0xaaaa01ull);
    uint64_t got = 0;
    if (!totk_vfix_memo4_find(vm, vs, fs0, &got) || got != 0xaaaa01ull) { free(vm); free(lm); free(tm); return 4; }
    if (totk_vfix_memo4_find(vm, vs, fs1, &got)) { free(vm); free(lm); free(tm); return 5; }
    for (int i = 1; i <= 3; i++) totk_vfix_memo4_put(vm, vs + (uint64_t)i, fs0 + (uint64_t)i, 0xaaaa10ull + (uint64_t)i);
    if (!totk_vfix_memo4_find(vm, vs + 3u, fs0 + 3u, &got) || got != 0xaaaa13ull) { free(vm); free(lm); free(tm); return 6; }
    totk_lay_memo4_put(lm, vs, fs0, 0xbbbb01ull);
    if (totk_lay_memo4_find(lm, vs, fs0) != 0xbbbb01ull || totk_lay_memo4_find(lm, vs, fs1) != 0) { free(vm); free(lm); free(tm); return 7; }
    totk_tmpl_memo4_put(tm, 0xdead01ull, 0xcccc01ull);
    if (totk_tmpl_memo4_find(tm, 0xdead01ull) != 0xcccc01ull || totk_tmpl_memo4_find(tm, 0xdead02ull) != 0) { free(vm); free(lm); free(tm); return 8; }
    (void)totk_vk_shader_fast_on();
    free(vm); free(lm); free(tm);
    return 0;
}

static int test_w8_deps_next(void) {
    const char *p = ";type:3=u32;attr:12=vec4;flag:0=1";
    char kind[16], want[64];
    int slot = -1, bad = 0, n = 0;
    const char *wk[3] = {"type", "attr", "flag"};
    const int ws[3] = {3, 12, 0};
    const char *ww[3] = {"u32", "vec4", "1"};
    while (totk_deps_next(&p, kind, &slot, want, &bad)) {
        if (bad || n >= 3) return 10 + n;
        if (strcmp(kind, wk[n]) != 0 || slot != ws[n] || strcmp(want, ww[n]) != 0) return 20 + n;
        n++;
    }
    if (bad || n != 3) return 30;
    const char *m1 = "broken_token";
    bad = 0;
    if (totk_deps_next(&m1, kind, &slot, want, &bad) || bad != 1) return 31;
    const char *m2 = "0123456789abcdef0:1=ok";
    bad = 0;
    if (totk_deps_next(&m2, kind, &slot, want, &bad) || bad != 2) return 32;
    return 0;
}

static int test_w8_gen_and_blk(void) {
    totk_gen_memo4 *gm = calloc(1, sizeof *gm);
    totk_blk_memo4 *bm = calloc(1, sizeof *bm);
    if (!gm || !bm) { free(gm); free(bm); return 1; }
    uint32_t g[3] = {1u, 2u, 3u};
    uint64_t b = 0, o = 0, r = 0;
    totk_gen_memo4_put(gm, 10u, 7u, 3u, 64u, 32u, g, 0x9001u, 128u, 256u);
    if (!totk_gen_memo4_find(gm, 10u, 7u, 3u, 64u, 32u, g, &b, &o, &r) || b != 0x9001u) { free(gm); free(bm); return 2; }
    if (totk_gen_memo4_find(gm, 11u, 7u, 3u, 64u, 32u, g, &b, &o, &r)) { free(gm); free(bm); return 3; }
    uint8_t blk[256];
    for (int i = 0; i < 256; i++) blk[i] = (uint8_t)(i * 7 + 3);
    uint64_t h[2] = {0, 0};
    totk_hash_dual64(blk, sizeof blk, h);
    totk_blk_memo4_put(bm, 10u, 5, sizeof blk, h, 0x8001u, 64u, 256u, blk);
    uint8_t *ptr = NULL;
    if (!totk_blk_memo4_find(bm, 10u, 5, blk, sizeof blk, &b, &o, &r, &ptr, h) || b != 0x8001u) { free(gm); free(bm); return 4; }
    if (totk_blk_memo4_find(bm, 11u, 5, blk, sizeof blk, &b, &o, &r, &ptr, h)) { free(gm); free(bm); return 5; }
    (void)totk_vk_dsc_fast_on();
    free(gm); free(bm);
    return 0;
}

static int test_w8_dsc_vic(void) {
    totk_dsc_vic *vc = calloc(1, sizeof *vc);
    if (!vc) return 1;
    uint64_t ha[2] = {0x123456789abcdef0ull, 0x0fedcba987654321ull};
    uint64_t hb[2] = {ha[0], ha[1] ^ 0x100ull};
    totk_dsc_vic_put(vc, 4u, 0xabcdu, 96u, ha, 0xdead01ull);
    if (totk_dsc_vic_find(vc, 4u, 0xabcdu, 96u, ha) != 0xdead01ull) { free(vc); return 2; }
    if (totk_dsc_vic_find(vc, 4u, 0xabcdu, 96u, hb) != 0) { free(vc); return 3; }
    if (totk_dsc_vic_find(vc, 5u, 0xabcdu, 96u, ha) != 0) { free(vc); return 4; }
    for (int i = 0; i < 4; i++) {
        uint64_t hc[2] = {ha[0] + (uint64_t)(i << 11), ha[1] ^ (uint64_t)(i << 8)};
        totk_dsc_vic_put(vc, 6u, 0xabcdu, 96u, hc, 0xee00u + (uint64_t)i);
    }
    for (int i = 0; i < 4; i++) {
        uint64_t hc[2] = {ha[0] + (uint64_t)(i << 11), ha[1] ^ (uint64_t)(i << 8)};
        if (totk_dsc_vic_find(vc, 6u, 0xabcdu, 96u, hc) != 0xee00u + (uint64_t)i) { free(vc); return 5; }
    }
    free(vc);
    return 0;
}

static uint64_t w8_kv64_get(const w8_kv64 *t, uint64_t k0, uint64_t k1) {
    uint32_t idx = (uint32_t)((k0 * 0x9e3779b97f4a7c15ull ^ k1) >> 24) & 63u;
    for (uint32_t p = 0; p < 64u; p++) {
        const w8_kv64 *e = &t[(idx + p) & 63u];
        if (e->k0 == k0 && e->k1 == k1) return e->val;
    }
    return 0;
}

static uint64_t w8_shader_slow(const w8_vfix_rec *vtab, const w8_kv64 *ltab, const w8_kv64 *ttab,
                               const uint8_t kind[32], uint64_t vs, uint64_t fs, uint64_t *lay_o, uint64_t *tmpl_o) {
    uint64_t mod = 0;
    pthread_mutex_lock(&w8_vfix_mu);
    for (int i = 0; i < 16; i++) {
        if (vtab[i].vs == vs && vtab[i].fs == fs && memcmp(vtab[i].kind, kind, 32) == 0) { mod = vtab[i].mod; break; }
    }
    pthread_mutex_unlock(&w8_vfix_mu);
    *lay_o = w8_kv64_get(ltab, vs, fs);
    *tmpl_o = w8_kv64_get(ttab, *lay_o, 0);
    return mod;
}

static uint64_t w8_bench_shader(int use_fast, uint64_t *misses_out) {
    totk_vfix_memo4 *vm = calloc(1, sizeof *vm);
    totk_lay_memo4 *lm = calloc(1, sizeof *lm);
    totk_tmpl_memo4 *tm = calloc(1, sizeof *tm);
    w8_vfix_rec *vt = calloc(16, sizeof *vt);
    w8_kv64 *lt = calloc(64, sizeof *lt), *tt = calloc(64, sizeof *tt);
    assert(vm && lm && tm && vt && lt && tt);
    uint8_t kbuf[32] = {1, 2, 3, 1};
    for (int m = 0; m < 16; m++) {
        uint64_t vs = 0x100000ull + (uint64_t)m * 0x101ull, fs = 0x200000ull + (uint64_t)m * 0x203ull;
        uint64_t lay = vs + fs + 1u;
        vt[m] = (w8_vfix_rec){vs, fs, (vs ^ fs) * 0x9e3779b97f4a7c15ull, 0xfu, {1, 2, 3, 1}};
        uint32_t li = (uint32_t)((vs * 0x9e3779b97f4a7c15ull ^ fs) >> 24) & 63u;
        while (lt[li].val) li = (li + 1u) & 63u;
        lt[li] = (w8_kv64){vs, fs, lay};
        uint32_t ti = (uint32_t)((lay * 0x9e3779b97f4a7c15ull) >> 24) & 63u;
        while (tt[ti].val) ti = (ti + 1u) & 63u;
        tt[ti] = (w8_kv64){lay, 0, lay ^ 0x5555ull};
    }
    uint64_t t0 = host_now_ns(), misses = 0, sum = 0;
    for (int it = 0; it < 200000; it++) {
        uint64_t m = (uint64_t)(it & 15), vs = 0x100000ull + m * 0x101ull, fs = 0x200000ull + m * 0x203ull, mod = 0;
        if (use_fast && totk_vfix_memo4_find(vm, vs, fs, &mod)) {
            uint64_t lay = totk_lay_memo4_find(lm, vs, fs), t = totk_tmpl_memo4_find(tm, lay);
            if (lay && t) { sum += mod ^ lay ^ t; continue; }
        }
        misses++;
        uint64_t lay = 0, t = 0;
        mod = w8_shader_slow(vt, lt, tt, kbuf, vs, fs, &lay, &t);
        if (use_fast) { totk_vfix_memo4_put(vm, vs, fs, mod); totk_lay_memo4_put(lm, vs, fs, lay); totk_tmpl_memo4_put(tm, lay, t); }
        sum += mod ^ lay ^ t;
    }
    uint64_t dt = host_now_ns() - t0;
    w8_sink ^= sum; *misses_out = misses;
    free(vm); free(lm); free(tm); free(vt); free(lt); free(tt);
    return dt;
}

static void w8_fill_blk2560(uint8_t blk[2560], uint32_t key, uint64_t rev) {
    uint64_t v = rev * 0x9e3779b97f4a7c15ull ^ ((uint64_t)key << 32);
    for (int i = 0; i < 320; i++) {
        uint64_t w = v ^ (uint64_t)i * 0x100000001b3ull;
        memcpy(blk + (size_t)i * 8u, &w, 8);
    }
}

static uint64_t w8_bench_blk(int use_fast, uint64_t *misses_out) {
    totk_gen_memo4 *gm = calloc(1, sizeof *gm);
    totk_blk_memo4 *bm = calloc(1, sizeof *bm);
    uint8_t (*mats)[2560] = calloc(16, 2560);
    assert(gm && bm && mats);
    struct { uint64_t rev, buf; } last[5] = {0};
    uint32_t g[3] = {4u, 5u, 6u};
    uint64_t t0 = host_now_ns(), misses = 0, sum = 0;
    uint8_t blk[2560];
    for (int it = 0; it < 200000; it++) {
        uint32_t m = (uint32_t)(it & 15), key = (m & 3u) + 1u;
        uint64_t rev = (uint64_t)(m >> 2) + 1u, buf = 0, off = 0, rng = 0, h[2] = {0, 0};
        if (last[key].rev == rev) { sum += last[key].buf; continue; }
        if (use_fast && totk_gen_memo4_find(gm, 1u, rev, key, 64u, 32u, g, &buf, &off, &rng)) {
            last[key].rev = rev; last[key].buf = buf; sum += buf; continue;
        }
        misses++;
        w8_fill_blk2560(blk, key, rev);
        totk_hash_dual64(blk, sizeof blk, h);
        buf = 0x700000ull + (uint64_t)m;
        uint8_t *rp = NULL;
        if (!use_fast || !totk_blk_memo4_find(bm, 1u, (int)key, blk, sizeof blk, &buf, &off, &rng, &rp, h)) {
            memcpy(mats[m], blk, sizeof blk);
            if (use_fast) totk_blk_memo4_put(bm, 1u, (int)key, sizeof blk, h, buf, 0u, sizeof blk, mats[m]);
        }
        if (use_fast) totk_gen_memo4_put(gm, 1u, rev, key, 64u, 32u, g, buf, 0u, sizeof blk);
        last[key].rev = rev; last[key].buf = buf; sum += buf;
    }
    uint64_t dt = host_now_ns() - t0;
    w8_sink ^= sum; *misses_out = misses;
    free(gm); free(bm); free(mats);
    return dt;
}

static int run_ktgl_w8_tests(int fault_mode) {
    (void)fault_mode;
    int r1 = test_w8_vfix_lay_tmpl(), r2 = test_w8_deps_next(), r3 = test_w8_gen_and_blk(), r4 = test_w8_dsc_vic();
    if (r1 || r2 || r3 || r4) {
        fprintf(stderr, "FAIL w8 r1=%d r2=%d r3=%d r4=%d fault=%d\n", r1, r2, r3, r4, fault_mode);
        return 1;
    }
    uint64_t sm0 = 0, sm1 = 0, bm0 = 0, bm1 = 0;
    uint64_t sns0 = w8_bench_shader(0, &sm0), sns1 = w8_bench_shader(1, &sm1);
    uint64_t bns0 = w8_bench_blk(0, &bm0), bns1 = w8_bench_blk(1, &bm1);
    printf("aoc_w8_shader_fast: fault=%d ops=200000 base=%.2fms (misses=%llu) fast=%.2fms (misses=%llu) speedup=%.2fx sink=%llu\n",
           fault_mode, (double)sns0 / 1e6, (unsigned long long)sm0, (double)sns1 / 1e6, (unsigned long long)sm1,
           sns1 ? (double)sns0 / (double)sns1 : 0.0, (unsigned long long)w8_sink);
    printf("aoc_w8_dsc_fast: fault=%d ops=200000 base=%.2fms (misses=%llu) fast=%.2fms (misses=%llu) speedup=%.2fx\n",
           fault_mode, (double)bns0 / 1e6, (unsigned long long)bm0, (double)bns1 / 1e6, (unsigned long long)bm1,
           bns1 ? (double)bns0 / (double)bns1 : 0.0);
    if (fault_mode == 0 && (sm0 != 200000 || sm1 != 16 || bm0 != 200000 || bm1 != 16)) {
        fprintf(stderr, "FAIL w8 miss counts sm0=%llu sm1=%llu bm0=%llu bm1=%llu\n",
                (unsigned long long)sm0, (unsigned long long)sm1, (unsigned long long)bm0, (unsigned long long)bm1);
        return 1;
    }
    return 0;
}

#endif
