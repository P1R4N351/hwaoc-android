#ifndef TEST_VK_KTGL_OPT_W18_H
#define TEST_VK_KTGL_OPT_W18_H

#include "vk_view_fast.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint64_t host_now_ns(void);
static volatile uint64_t w18_sink;

static int test_w18_view_canon_and_fault50(void) {
    static totk_view_memo4 vm;
    memset(&vm, 0, sizeof vm);
    uint8_t sw_a[4] = {2u, 3u, 4u, 5u}, sw_b[4] = {9u, 8u, 7u, 6u};
    uint64_t a0 = 0, a1 = 0, b0 = 0, b1 = 0, c0 = 0, c1 = 0, out = 0;
    totk_view_pack_fields(1, 0, 0, 0, 0, 0, 1, 0u, 2u, 4u, 77u, 99u, 0x25u, sw_a, &a0, &a1);
    totk_view_pack_fields(1, 0, 0, 0, 0, 0, 0, 0u, 2u, 1u, 11u, 22u, 0x3du, sw_b, &b0, &b1);
    if (a0 != b0 || a1 != b1) return 1;
    totk_view_pack_fields(1, 0, 1, 0, 0, 0, 0, 0u, 2u, 1u, 0u, 0u, 0x25u, sw_a, &a0, &a1);
    totk_view_pack_fields(1, 0, 1, 0, 0, 0, 0, 0u, 2u, 1u, 0u, 0u, 0x3du, sw_a, &c0, &c1);
    if (a0 == c0) return 1;
    totk_view_memo4_put(&vm, 0x1000u, 1u, 10u, 0x55u, a0, a1, 0x9001u);
    if (!totk_view_memo4_get(&vm, 0x1000u, 1u, 10u, 0x55u, a0, a1, &out) || out != 0x9001u) return 1;
    if (totk_view_memo4_get(&vm, 0x1000u, 1u, 10u, 0x55u, c0, c1, &out)) return 1;
    return 0;
}

static int test_w18_ident_and_cut_faults(void) {
    static totk_view_id_memo4 idm;
    static totk_view_cut_memo4 cm;
    memset(&idm, 0, sizeof idm);
    memset(&cm, 0, sizeof cm);
    totk_view_id_ent in = {0x2000u, 0x7001u, 4u, 0x3000u, 9u, 0x800000u, 4096u, 101u, 1280u, 720u, 37u, 1u, 0u, 4u, 0u, 1u, 1, 1u};
    totk_view_id_put(&idm, &in);
    const totk_view_id_ent *got = totk_view_id_get(&idm, 0x2000u, 0x7001u, 4u);
    if (!got || got->view_type != 1 || got->incarn != 9u) return 1;
    if (totk_view_id_get(&idm, 0x2000u, 0x7001u, 5u) != NULL) return 1;

    totk_view_cut_put(&cm, 0x4000u, 2u, 8u, 0x7001u, 2u, 0x7002u);
    totk_view_cut_put(&cm, 0x4000u, 2u, 8u, 0x7001u, 3u, 0x7003u);
    uint64_t cut = 0;
    if (!totk_view_cut_get(&cm, 0x4000u, 2u, 8u, 0x7001u, 2u, &cut) || cut != 0x7002u) return 1;
    if (!totk_view_cut_get(&cm, 0x4000u, 2u, 8u, 0x7001u, 3u, &cut) || cut != 0x7003u) return 1;
    return 0;
}

static void bench_w18_view_memo(void) {
    enum { NQ = 200000, NV = 12 };
    typedef struct { uint64_t k0, k1, view_u; } sim_ve;
    static totk_view_memo4 vm;
    static sim_ve ves[NV];
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    memset(&vm, 0, sizeof vm);
    const uint8_t sw[4] = {2u, 3u, 4u, 5u};
    uint64_t v0[NV], v1[NV];
    for (int i = 0; i < NV; i++) {
        totk_view_pack_fields(1, 0, 1, 0, 1, 0, 0, (uint32_t)(i & 3), 1u, 1u, 0u, 1u, 0x25u + (uint32_t)(i >> 2), sw, &v0[i], &v1[i]);
        ves[i] = (sim_ve){v0[i], v1[i], UINT64_C(0x60000) + (uint64_t)i};
        totk_view_memo4_put(&vm, 0x1000u, 1u, 1u, 0x25u, v0[i], v1[i], ves[i].view_u);
    }
    uint64_t t0 = host_now_ns(), sb = 0;
    for (int q = 0; q < NQ; q++) {
        uint32_t idx = (uint32_t)((q ^ (int)(w18_sink & 0u)) & 7);
        pthread_mutex_lock(&mu);
        for (int i = 0; i < NV; i++) {
            if (ves[i].k0 == v0[idx] && ves[i].k1 == v1[idx]) {
                if (i >= 2) { sim_ve tmp = ves[1]; ves[1] = ves[i]; ves[i] = tmp; sb += ves[1].view_u; }
                else sb += ves[i].view_u;
                break;
            }
        }
        pthread_mutex_unlock(&mu);
    }
    uint64_t ns0 = host_now_ns() - t0;
    uint64_t t1 = host_now_ns(), sf = 0;
    for (int q = 0; q < NQ; q++) {
        uint32_t idx = (uint32_t)((q ^ (int)(w18_sink & 0u)) & 7);
        uint64_t vu = 0;
        if (totk_view_memo4_get(&vm, 0x1000u, 1u, 1u, 0x25u, v0[idx], v1[idx], &vu)) sf += vu;
    }
    uint64_t ns1 = host_now_ns() - t1;
    assert(sb == sf);
    w18_sink += sb;
    double ms0 = (double)ns0 * 1e-6, ms1 = (double)ns1 * 1e-6;
    printf("aoc_w18_view_memo: base=%.3f ms fast=%.3f ms speedup=%.2fx\n", ms0, ms1, ms0 / (ms1 > 0.001 ? ms1 : 0.001));
}

static void bench_w18_view_ident(void) {
    enum { NQ = 200000, NV = 12 };
    typedef struct { uint64_t k0, k1, v; } sim_ve;
    static totk_view_id_memo4 idm;
    static totk_view_cut_memo4 cm;
    static sim_ve ves[NV];
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    memset(&idm, 0, sizeof idm);
    memset(&cm, 0, sizeof cm);
    for (int i = 0; i < NV; i++) {
        uint64_t v = UINT64_C(0x80000) + (uint64_t)i, k0 = (uint64_t)(37u + i) | (UINT64_C(1) << 32), k1 = (uint64_t)(i & 3) | (UINT64_C(4) << 8);
        ves[i] = (sim_ve){k0, k1, v};
        totk_view_id_ent ent = {0x2000u, v, 1u, 0x3000u, 1u, 0x1000u, 4096u, 7u, 1280u, 720u, (uint32_t)(37 + i), 1u, (uint32_t)(i & 3), 4u, 0u, 1u, 1, 1u};
        totk_view_id_put(&idm, &ent);
        totk_view_cut_put(&cm, 0x3000u, 1u, 1u, v, 2u, v + 0x100u);
    }
    uint64_t t0 = host_now_ns(), sb = 0;
    for (int q = 0; q < NQ; q++) {
        uint32_t idx = (uint32_t)((q ^ (int)(w18_sink & 0u)) & 7);
        uint64_t v = ves[idx].v, k0 = 0, k1 = 0, cut = v;
        pthread_mutex_lock(&mu);
        for (int i = 0; i < NV; i++) {
            if (ves[i].v == v) { k0 = ves[i].k0; k1 = ves[i].k1; break; }
        }
        pthread_mutex_unlock(&mu);
        pthread_mutex_lock(&mu);
        for (int i = 0; i < NV; i++) {
            if (ves[i].v == v) {
                for (int j = 0; j < NV; j++) {
                    if (ves[j].k0 == ves[i].k0 && ves[j].k1 == (ves[i].k1 & 0xffu)) { cut = ves[j].v; break; }
                }
                if (cut == v) cut = ves[i].v + 0x100u;
                break;
            }
        }
        pthread_mutex_unlock(&mu);
        sb += (uint32_t)k0 + (k1 & 0xffu) + cut;
    }
    uint64_t ns0 = host_now_ns() - t0;
    uint64_t t1 = host_now_ns(), sf = 0;
    for (int q = 0; q < NQ; q++) {
        uint32_t idx = (uint32_t)((q ^ (int)(w18_sink & 0u)) & 7);
        uint64_t v = ves[idx].v, cut = 0;
        const totk_view_id_ent *e = totk_view_id_get(&idm, 0x2000u, v, 1u);
        (void)totk_view_cut_get(&cm, 0x3000u, 1u, 1u, v, 2u, &cut);
        if (e) sf += e->fmt_u + e->base_mip + cut;
    }
    uint64_t ns1 = host_now_ns() - t1;
    assert(sb == sf);
    w18_sink += sb;
    double ms0 = (double)ns0 * 1e-6, ms1 = (double)ns1 * 1e-6;
    printf("aoc_w18_view_ident: base=%.3f ms fast=%.3f ms speedup=%.2fx\n", ms0, ms1, ms0 / (ms1 > 0.001 ? ms1 : 0.001));
}

static int run_ktgl_w18_tests(int fault_mode) {
    (void)fault_mode;
    if (test_w18_view_canon_and_fault50() != 0) { fprintf(stderr, "FAIL w18 view_canon fault=%d\n", fault_mode); return 1; }
    if (test_w18_ident_and_cut_faults() != 0) { fprintf(stderr, "FAIL w18 ident_cut fault=%d\n", fault_mode); return 1; }
    if (!fault_mode) { bench_w18_view_memo(); bench_w18_view_ident(); }
    return 0;
}

#endif
