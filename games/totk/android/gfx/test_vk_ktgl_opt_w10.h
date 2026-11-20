#ifndef TEST_VK_KTGL_OPT_W10_H
#define TEST_VK_KTGL_OPT_W10_H

#include "vk_stage_fast.h"
#include "vk_dsc_fast.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint64_t host_now_ns(void);
static volatile uint64_t w10_sink;

typedef struct { uint64_t value[2]; } w10_rb;

static int w10_uni_ok(void *ctx, uint64_t cached_uni) {
    (void)cached_uni;
    return *(const int *)ctx;
}

static int test_w10_stage_freeze4(void) {
    totk_stage_freeze4 *m = calloc(1, sizeof *m);
    totk_stage_freeze_slot *slots = calloc(TOTK_STAGE_FREEZE_SLOTS, sizeof *slots);
    if (!m || !slots) { free(m); free(slots); return 1; }
    w10_rb b0[16], b1[16];
    for (uint32_t i = 0; i < 16u; i++) {
        b0[i] = (w10_rb){{0x1000u + i, 256u}};
        b1[i] = (w10_rb){{0x2000u + i, 512u}};
    }
    totk_stage_env env = {1u, 1u, 10u, 20u, 30u, 0u, 0u, 0xaaaau, 0x1111u, 0u, 0u, 0u, 1280u, 720u, 1u, 16u};
    totk_stage_set_ref r0 = {0x9001u, 0x8001u, 0u, 16u}, r1 = {0x9001u, 0x8001u, 16u, 16u}, out = {0};
    int allow_uni = 1;
    uint64_t s0 = totk_stage_bind_hash(env.sh_u, env.stage, b0, 16u);
    uint64_t s1 = totk_stage_bind_hash(env.sh_u, env.stage, b1, 16u);
    totk_stage_freeze4_put(m, slots, &env, s0, b0, &r0);
    totk_stage_freeze4_put(m, slots, &env, s1, b1, &r1);
    if (!totk_stage_freeze4_find(m, slots, &env, s0, b0, w10_uni_ok, &allow_uni, &out) || out.first != 0u) {
        free(m); free(slots); return 2;
    }
    if (!totk_stage_freeze4_find(m, slots, &env, s1, b1, w10_uni_ok, &allow_uni, &out) || out.first != 16u) {
        free(m); free(slots); return 3;
    }
    env.uniforms = 11u;
    if (!totk_stage_freeze4_find(m, slots, &env, s0, b0, w10_uni_ok, &allow_uni, &out) || out.first != 0u) {
        free(m); free(slots); return 4;
    }
    totk_stage_env env_new_chunk = env; env_new_chunk.chunk = 2u;
    if (totk_stage_freeze4_find(m, slots, &env_new_chunk, s0, b0, w10_uni_ok, &allow_uni, &out)) {
        free(m); free(slots); return 5;
    }
    (void)totk_vk_stage_fast_on();
    free(m); free(slots);
    return 0;
}

static int test_w10_rec_slice4(void) {
    totk_rec_slice_memo4 *rm = calloc(1, sizeof *rm);
    if (!rm) return 1;
    uint64_t su = 0;
    totk_rec_slice4_put(rm, 5u, 32u, 16u, 0x5555u, 0x100u, 7u, 0xabc1u);
    if (!totk_rec_slice4_find(rm, 5u, 32u, 16u, 0x5555u, 0x100u, 7u, &su) || su != 0xabc1u) { free(rm); return 2; }
    if (totk_rec_slice4_find(rm, 6u, 32u, 16u, 0x5555u, 0x100u, 7u, &su)) { free(rm); return 3; }
    if (totk_rec_slice4_find(rm, 5u, 32u, 16u, 0x5555u, 0x200u, 7u, &su)) { free(rm); return 4; }
    if (totk_rec_slice4_find(rm, 5u, 32u, 16u, 0x5555u, 0x100u, 8u, &su)) { free(rm); return 5; }
    free(rm);
    return 0;
}

static uint64_t w10_kv_resolve(const uint64_t keys[512], const uint64_t vals[512], uint64_t k) {
    uint32_t idx = (uint32_t)(((k * 0x9e3779b97f4a7c15ull) >> 23) & 511u);
    for (uint32_t p = 0; p < 512u; p++) {
        uint32_t s = (idx + p) & 511u;
        if (keys[s] == k) return vals[s];
        if (!keys[s]) break;
    }
    return 0;
}

static uint64_t w10_bench_stage(int fast, totk_stage_freeze4 *m, totk_stage_freeze_slot *slots,
                                  uint64_t *misses_o, uint64_t *sink_o) {
    w10_rb mats[16][16]; uint64_t ktab[512] = {0}, vtab[512] = {0};
    for (uint32_t a = 0; a < 16u; a++) for (uint32_t b = 0; b < 16u; b++) {
        uint64_t addr = 0x100000ull + a * 0x1000ull + b * 16u;
        mats[a][b] = (w10_rb){{addr, 256u + b * 8u}};
        uint32_t s = (uint32_t)(((addr * 0x9e3779b97f4a7c15ull) >> 23) & 511u);
        while (ktab[s] && ktab[s] != addr) s = (s + 1u) & 511u;
        ktab[s] = addr; vtab[s] = addr ^ 0xa5a5u;
    }
    w10_rb last_b[16]; uint64_t last_sh = 0; uint32_t last_first = 0; int last_valid = 0;
    totk_stage_env env = {1u, 1u, 10u, 20u, 30u, 0u, 0u, 0xaaaau, 0x2222u, 0u, 0u, 0u, 1280u, 720u, 1u, 16u};
    uint64_t t0 = host_now_ns(), misses = 0, sum = 0; int allow = 1;
    for (uint32_t it = 0; it < 200000u; it++) {
        uint32_t mat = it & 15u;
        int hit = last_valid && last_sh == env.sh_u && memcmp(last_b, mats[mat], sizeof last_b) == 0;
        totk_stage_set_ref ref = {0x9001u, 0x8001u, last_first, 16u};
        if (!hit && fast) {
            uint64_t sig = totk_stage_bind_hash(env.sh_u, 1u, mats[mat], 16u);
            hit = totk_stage_freeze4_find(m, slots, &env, sig, mats[mat], w10_uni_ok, &allow, &ref);
        }
        if (!hit) {
            misses++;
            uint64_t acc = 0;
            for (uint32_t b = 0; b < 16u; b++) acc ^= w10_kv_resolve(ktab, vtab, mats[mat][b].value[0]);
            ref = (totk_stage_set_ref){0x9001u, 0x8001u, mat * 16u, 16u};
            if (fast) {
                uint64_t sig = totk_stage_bind_hash(env.sh_u, 1u, mats[mat], 16u);
                totk_stage_freeze4_put(m, slots, &env, sig, mats[mat], &ref);
            }
            w10_sink ^= acc;
        }
        memcpy(last_b, mats[mat], sizeof last_b);
        last_sh = env.sh_u; last_first = ref.first; last_valid = 1;
        sum += ref.first;
    }
    uint64_t dt = host_now_ns() - t0;
    *misses_o = misses; *sink_o = sum; w10_sink ^= sum;
    return dt;
}

static uint64_t w10_bench_rec_slice(int fast, totk_rec_slice_memo4 *rm, uint64_t *misses_o, uint64_t *sink_o) {
    uint64_t info[16][128]; uint64_t local[128];
    for (uint32_t m = 0; m < 16u; m++) for (uint32_t k = 0; k < 128u; k++) info[m][k] = 0x55aa00ull + m * 256u + k;
    uint64_t t0 = host_now_ns(), misses = 0, sum = 0;
    for (uint32_t it = 0; it < 200000u; it++) {
        uint32_t mat = it & 15u; uint64_t su = 0;
        if (!fast || !totk_rec_slice4_find(rm, 1u, mat * 16u, 16u, 0x9001u, 0xaaaau, 1u, &su)) {
            misses++;
            memcpy(local, info[mat], sizeof local);
            uint64_t h[2] = {0xcbf29ce484222325ull, 0x6a09e667f3bcc909ull};
            totk_hash_dual64(local, sizeof local, h);
            su = 0x70000ull + (h[0] & 0xfffu);
            if (fast) totk_rec_slice4_put(rm, 1u, mat * 16u, 16u, 0x9001u, 0xaaaau, 1u, su);
        }
        sum ^= su;
    }
    uint64_t dt = host_now_ns() - t0;
    *misses_o = misses; *sink_o = sum; w10_sink ^= sum;
    return dt;
}

static int run_ktgl_w10_tests(int fault_mode) {
    (void)fault_mode;
    int r1 = test_w10_stage_freeze4(), r2 = test_w10_rec_slice4();
    if (r1 || r2) { fprintf(stderr, "FAIL w10 r1=%d r2=%d fault=%d\n", r1, r2, fault_mode); return 1; }
    totk_stage_freeze4 *m = calloc(1, sizeof *m);
    totk_stage_freeze_slot *slots = calloc(TOTK_STAGE_FREEZE_SLOTS, sizeof *slots);
    totk_rec_slice_memo4 *rm = calloc(1, sizeof *rm);
    assert(m && slots && rm);
    uint64_t sm0 = 0, sm1 = 0, ss0 = 0, ss1 = 0, rm0 = 0, rm1 = 0, rs0 = 0, rs1 = 0;
    uint64_t st0 = w10_bench_stage(0, m, slots, &sm0, &ss0), st1 = w10_bench_stage(1, m, slots, &sm1, &ss1);
    uint64_t rt0 = w10_bench_rec_slice(0, rm, &rm0, &rs0), rt1 = w10_bench_rec_slice(1, rm, &rm1, &rs1);
    free(m); free(slots); free(rm);
    printf("aoc_w10_stage_fast: fault=%d ops=200000 base=%.2fms (misses=%llu) fast=%.2fms (misses=%llu) speedup=%.2fx sink=%llu\n",
           fault_mode, (double)st0 / 1e6, (unsigned long long)sm0, (double)st1 / 1e6, (unsigned long long)sm1,
           st1 ? (double)st0 / (double)st1 : 0.0, (unsigned long long)(ss0 ^ ss1));
    printf("aoc_w10_rec_slice: fault=%d ops=200000 base=%.2fms (misses=%llu) fast=%.2fms (misses=%llu) speedup=%.2fx sink=%llu\n",
           fault_mode, (double)rt0 / 1e6, (unsigned long long)rm0, (double)rt1 / 1e6, (unsigned long long)rm1,
           rt1 ? (double)rt0 / (double)rt1 : 0.0, (unsigned long long)(rs0 ^ rs1));
    if (fault_mode == 0 && (sm1 != 16u || rm1 != 16u)) return 1;
    return 0;
}

#endif
