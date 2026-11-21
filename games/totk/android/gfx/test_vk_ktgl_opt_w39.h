#ifndef TEST_VK_KTGL_OPT_W39_H
#define TEST_VK_KTGL_OPT_W39_H

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define T_AOC_W39_TEST_HOOKS 1
#include "vk_spv_fast.h"
#include "vk_varyings.h"
#include "vk_load_proof.h"

_Atomic int g_totk_vk_spv_fast_state;
_Atomic int g_totk_w39_fault;

static uint64_t w39_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void w39_slow_patch_set(uint32_t *w, size_t nw, uint32_t set) {
    for (size_t i = 5u; i < nw;) {
        uint32_t op = w[i] & 0xffffu, n = w[i] >> 16; if (!n) break;
        if (op == 71u && n == 4u && w[i + 2] == 34u) w[i + 3] = set;
        i += n;
    }
}

static uint32_t w39_slow_fout_locs(const uint32_t *w, size_t nw) {
    uint32_t bound = w[3];
    int32_t *loc = (int32_t *)malloc(bound * sizeof *loc);
    uint32_t *val = (uint32_t *)calloc(bound, 4u), *cnt = (uint32_t *)calloc(bound, 4u), *ptee = (uint32_t *)calloc(bound, 4u);
    for (uint32_t i = 0u; i < bound; i++) loc[i] = -1;
    uint32_t mask = 0u;
    for (size_t i = 5u; i < nw;) {
        uint32_t op = w[i] & 0xffffu, n = w[i] >> 16; if (!n || i + n > nw) break;
        const uint32_t *a = w + i + 1;
        if (op == 71u && n >= 4u && a[1] == 30u && a[0] && a[0] < bound) loc[a[0]] = (int32_t)a[2];
        else if (op == 43u && n >= 4u && a[1] && a[1] < bound) val[a[1]] = a[2];
        else if (op == 28u && n >= 4u && a[0] && a[0] < bound && a[2] && a[2] < bound)
            cnt[a[0]] = val[a[2]] * (a[1] && a[1] < bound && cnt[a[1]] ? cnt[a[1]] : 1u);
        else if (op == 32u && n >= 4u && a[0] && a[0] < bound) ptee[a[0]] = a[2];
        else if (op == 59u && n >= 4u && a[2] == 3u && a[0] && a[0] < bound && a[1] && a[1] < bound && loc[a[1]] >= 0) {
            uint32_t t = ptee[a[0]], sp = t && t < bound && cnt[t] ? cnt[t] : 1u;
            for (uint32_t k = 0u; k < sp && (uint32_t)loc[a[1]] + k < 8u; k++) mask |= 1u << ((uint32_t)loc[a[1]] + k);
        }
        i += n;
    }
    free(loc); free(val); free(cnt); free(ptee);
    return mask;
}

static uint32_t *w39_slow_depth_nudge(const uint32_t *w, size_t nw, uint32_t spec_id, size_t *out_nw) {
    uint32_t bound = w[3], fragdepth = 0u, tfloat = 0u; size_t after_float = 0u, first_dec = 0u; int stores = 0;
    for (size_t i = 5u; i < nw;) {
        uint32_t op = w[i] & 0xffffu, n = w[i] >> 16; if (!n || i + n > nw) return NULL;
        if (op == 71u && !first_dec) first_dec = i;
        if (op == 71u && n >= 4u && w[i + 2] == 11u && w[i + 3] == 22u) fragdepth = w[i + 1];
        if (op == 22u && n >= 3u && w[i + 2] == 32u && !tfloat) { tfloat = w[i + 1]; after_float = i + n; }
        i += n;
    }
    if (!fragdepth || !tfloat || !first_dec) return NULL;
    for (size_t i = 5u; i < nw;) { uint32_t op = w[i] & 0xffffu, n = w[i] >> 16; if (op == 62u && n >= 3u && w[i + 1] == fragdepth) stores++; i += n; }
    if (!stores) return NULL;
    uint32_t *o = (uint32_t *)malloc((nw + 8u + (size_t)stores * 5u) * sizeof *o); size_t k = 0u; uint32_t spec = bound++;
    for (size_t i = 0u; i < 5u; i++) o[k++] = w[i];
    for (size_t i = 5u; i < nw;) {
        uint32_t op = w[i] & 0xffffu, n = w[i] >> 16;
        if (i == first_dec) { o[k++] = 4u << 16 | 71u; o[k++] = spec; o[k++] = 1u; o[k++] = spec_id; }
        if (op == 62u && n >= 3u && w[i + 1] == fragdepth) {
            uint32_t t = bound++;
            o[k++] = 5u << 16 | 129u; o[k++] = tfloat; o[k++] = t; o[k++] = w[i + 2]; o[k++] = spec;
            o[k++] = w[i]; o[k++] = w[i + 1]; o[k++] = t; for (uint32_t j = 3u; j < n; j++) o[k++] = w[i + j];
        } else for (uint32_t j = 0u; j < n; j++) o[k++] = w[i + j];
        i += n;
        if (i == after_float) { o[k++] = 4u << 16 | 50u; o[k++] = tfloat; o[k++] = spec; o[k++] = 0u; }
    }
    o[3] = bound; *out_nw = k; return o;
}

static const uint32_t k_w39_fs_depth[] = {
    0x07230203u, 0x00010300u, 0u, 32u, 0u,
    (4u << 16) | 71u, 10u, 30u, 2u,
    (4u << 16) | 71u, 11u, 11u, 22u,
    (4u << 16) | 71u, 12u, 34u, 0u,
    (4u << 16) | 71u, 12u, 30u, 2u,
    (3u << 16) | 22u, 1u, 32u,
    (4u << 16) | 23u, 2u, 1u, 4u,
    (4u << 16) | 21u, 3u, 32u, 0u,
    (4u << 16) | 43u, 3u, 4u, 2u,
    (4u << 16) | 28u, 5u, 2u, 4u,
    (4u << 16) | 32u, 6u, 3u, 5u,
    (4u << 16) | 32u, 7u, 3u, 1u,
    (4u << 16) | 32u, 8u, 1u, 2u,
    (4u << 16) | 59u, 6u, 10u, 3u,
    (4u << 16) | 59u, 7u, 11u, 3u,
    (4u << 16) | 59u, 8u, 12u, 1u,
    (4u << 16) | 43u, 1u, 13u, 0x3f000000u,
    (5u << 16) | 54u, 1u, 20u, 0u, 1u,
    (2u << 16) | 248u, 21u,
    (3u << 16) | 62u, 11u, 13u,
    (1u << 16) | 253u,
    (1u << 16) | 56u
};

static uint32_t *w39_make_big_no_depth(size_t *out_n) {
    size_t body = 128u, base = sizeof k_w39_fs_depth / 4u, n = base + body;
    uint32_t *w = (uint32_t *)malloc(n * 4u);
    assert(w != NULL);
    memcpy(w, k_w39_fs_depth, sizeof k_w39_fs_depth);
    w[3] = 48u;
    w[12] = 15u; /* id=11 BuiltIn FragDepth(22) -> BuiltIn FragCoord(15) */
    /* Replace OpStore 11 13 with OpNop x3 so FragCoord is unused in big module. */
    w[base - 5u] = (1u << 16) | 0u; w[base - 4u] = (1u << 16) | 0u; w[base - 3u] = (1u << 16) | 0u;
    size_t p = base - 1u;
    for (size_t k = 0u; k < body; k++) w[p++] = (1u << 16) | 0u;
    w[p] = (1u << 16) | 56u;
    *out_n = n;
    return w;
}

static int w39_test_spv_parity(void) {
    uint32_t a[sizeof k_w39_fs_depth / 4u], b[sizeof k_w39_fs_depth / 4u];
    memcpy(a, k_w39_fs_depth, sizeof a); memcpy(b, k_w39_fs_depth, sizeof b);
    w39_slow_patch_set(a, sizeof a / 4u, 1u);
    totk_spirv_patch_set_fast(b, sizeof b / 4u, 1u);
    if (memcmp(a, b, sizeof a) != 0) return 1;

    uint32_t f0 = w39_slow_fout_locs(k_w39_fs_depth, sizeof k_w39_fs_depth / 4u);
    uint32_t f1 = totk_spirv_fout_locations_fast(k_w39_fs_depth, sizeof k_w39_fs_depth / 4u);
    if (f0 != 0x0cu || f0 != f1) return 1;

    size_t n0 = 0u, n1 = 0u;
    uint32_t *d0 = w39_slow_depth_nudge(k_w39_fs_depth, sizeof k_w39_fs_depth / 4u, 99u, &n0);
    uint32_t *d1 = totk_spirv_depth_nudge_fast(k_w39_fs_depth, sizeof k_w39_fs_depth / 4u, 99u, &n1);
    int eq = (d0 && d1 && n0 == n1 && memcmp(d0, d1, n0 * 4u) == 0);
    free(d0); free(d1);
    if (!eq) return 1;

    totk_vk_spv_fast_reset_for_test(0);
    uint32_t m0 = 0u, m1 = 0u; uint8_t k0[32] = {0}, k1[32] = {0};
    int r0 = vk_spirv_io(k_w39_fs_depth, sizeof k_w39_fs_depth / 4u, 1u, &m0, k0);
    totk_vk_spv_fast_reset_for_test(1);
    int r1 = vk_spirv_io(k_w39_fs_depth, sizeof k_w39_fs_depth / 4u, 1u, &m1, k1);
    if (r0 != 0 || r0 != r1 || m0 != (1u << 2) || m0 != m1 || memcmp(k0, k1, 32) != 0) return 1;
    uint32_t bad[] = {0x07230203u, 0x00010300u, 0u, 8u, 0u, (2u << 16) | 54u, 1u, 0u};
    uint32_t bm = 99u;
    if (vk_spirv_io(bad, sizeof bad / 4u, 1u, &bm, NULL) != -1 || bm != 0u) return 1;
    return 0;
}

static int w39_test_cert_and_refl(void) {
    static const uint32_t vs[] = {
        0x07230203u, 0x00010300u, 0u, 32u, 0u,
        (4u << 16) | 71u, 10u, 30u, 3u,
        (4u << 16) | 71u, 11u, 11u, 0u,
        (3u << 16) | 22u, 1u, 32u,
        (4u << 16) | 23u, 2u, 1u, 4u,
        (4u << 16) | 32u, 3u, 1u, 2u,
        (4u << 16) | 32u, 4u, 3u, 2u,
        (4u << 16) | 59u, 3u, 10u, 1u,
        (4u << 16) | 59u, 4u, 11u, 3u,
        (4u << 16) | 43u, 1u, 12u, 0u,
        (4u << 16) | 43u, 1u, 13u, 0x3f800000u,
        (5u << 16) | 54u, 1u, 20u, 0u, 1u,
        (2u << 16) | 248u, 21u,
        (4u << 16) | 61u, 2u, 14u, 10u,
        (5u << 16) | 81u, 1u, 15u, 14u, 0u,
        (5u << 16) | 81u, 1u, 16u, 14u, 1u,
        (7u << 16) | 80u, 2u, 17u, 15u, 16u, 12u, 13u,
        (3u << 16) | 62u, 11u, 17u,
        (1u << 16) | 253u,
        (1u << 16) | 56u
    };
    totk_vk_spv_fast_reset_for_test(0);
    vk_load_certificate c0 = vk_load_certify(vs, sizeof vs / 4u, 0);
    totk_vk_spv_fast_reset_for_test(1);
    vk_load_certificate c1 = vk_load_certify(vs, sizeof vs / 4u, 0);
    if (c0.position_attr != 4u || c0.position_attr != c1.position_attr || c0.full_outputs != c1.full_outputs) return 1;

    uint32_t vm = 0u, fm = 0u; uint8_t vc[32] = {0};
    const char *vln = " 2:1 5:2", *fln = " 0 3", *bln = " 4 1 6 1 2 -1 16 1 0 0 256";
    totk_vk_refl_parse_vin(vln, vln + strlen(vln), &vm, vc);
    totk_vk_refl_parse_fout(fln, fln + strlen(fln), &fm);
    int32_t bv[10] = {0}; uint64_t ext = 0u;
    int okb = totk_vk_refl_parse_bind(bln, bln + strlen(bln), bv, &ext);
    if (vm != ((1u << 2) | (1u << 5)) || vc[2] != 1u || vc[5] != 2u || fm != 9u || !okb || bv[0] != 4 || ext != 256u) return 1;
    uint8_t *c1buf = NULL; uint32_t c1n = 0u;
    const char *hx = "deadbeef";
    totk_vk_refl_decode_c1(hx, hx + 8, &c1buf, &c1n);
    int hex_ok = (c1buf && c1n == 4u && c1buf[0] == 0xdeu && c1buf[3] == 0xefu);
    free(c1buf);
    return hex_ok ? 0 : 1;
}

static void *w39_worker(void *arg) {
    const uint32_t *big = (const uint32_t *)((uintptr_t *)arg)[0];
    size_t n = (size_t)((uintptr_t *)arg)[1];
    for (int r = 0; r < 200; r++) {
        uint32_t m = 0u; uint8_t k[32];
        if (vk_spirv_io(big, n, 1u, &m, k) != 0) return (void *)(uintptr_t)1;
        if (totk_spirv_fout_locations_fast(big, n) != 0x0cu) return (void *)(uintptr_t)1;
        size_t nn = 0u;
        if (totk_spirv_depth_nudge_fast(big, n, 99u, &nn) != NULL) return (void *)(uintptr_t)1;
    }
    return NULL;
}

static int w39_test_parity(int fmode) {
    totk_w39_set_fault(fmode);
    totk_vk_spv_fast_reset_for_test(1);
    if (w39_test_spv_parity() != 0 || w39_test_cert_and_refl() != 0) { totk_w39_set_fault(0); return 1; }
    size_t bn = 0u; uint32_t *big = w39_make_big_no_depth(&bn);
    if (totk_spirv_fragcoord_xy_fast(k_w39_fs_depth, sizeof k_w39_fs_depth / 4u) != 0) { free(big); return 1; }
    if (totk_spirv_fragcoord_xy_fast(big, bn) != 0) { free(big); return 1; }
    pthread_t th[4]; uintptr_t arg[2] = {(uintptr_t)big, (uintptr_t)bn};
    for (int t = 0; t < 4; t++) assert(pthread_create(&th[t], NULL, w39_worker, arg) == 0);
    for (int t = 0; t < 4; t++) { void *ret = NULL; assert(pthread_join(th[t], &ret) == 0 && !ret); }
    free(big); totk_w39_set_fault(0);
    return 0;
}

static double w39_bench_mode(int fast_on, uint64_t *sink) {
    size_t bn = 0u; uint32_t *big = w39_make_big_no_depth(&bn);
    uint32_t *tmp = (uint32_t *)malloc(bn * 4u);
    assert(tmp != NULL);
    totk_vk_spv_fast_reset_for_test(fast_on);
    uint64_t acc = 0u, t0 = w39_now_ns();
    for (int it = 0; it < 6000; it++) {
        memcpy(tmp, big, bn * 4u);
        if (fast_on) totk_spirv_patch_set_fast(tmp, bn, 1u); else w39_slow_patch_set(tmp, bn, 1u);
        uint32_t m = 0u; uint8_t k[32];
        assert(vk_spirv_io(tmp, bn, 1u, &m, k) == 0);
        acc += m + (fast_on ? totk_spirv_fout_locations_fast(tmp, bn) : w39_slow_fout_locs(tmp, bn));
        size_t nn = 0u;
        uint32_t *dn = fast_on ? totk_spirv_depth_nudge_fast(tmp, bn, 99u, &nn) : w39_slow_depth_nudge(tmp, bn, 99u, &nn);
        assert(dn == NULL);
    }
    double ms = (double)(w39_now_ns() - t0) / 1e6;
    free(tmp); free(big); *sink += acc;
    return ms;
}

static int run_ktgl_w39_tests(int fault_mode) {
    totk_vk_spv_fast_reset_for_test(-1);
    const char *env = getenv("TOTK_VK_SPV_FAST");
    int req_off = (env && (env[0] == '0' || env[0] == 'f' || env[0] == 'F' || env[0] == 'n' || env[0] == 'N'));
    int ext_f = totk_w39_fault();
    int eff = (fault_mode >= 141 && fault_mode <= 145) ? (fault_mode - 140) : ext_f;
    if (eff > 0) return w39_test_parity(eff) != 0 ? 1 : 0;
    assert(w39_test_parity(0) == 0);
    uint64_t s0 = 0u, s1 = 0u;
    double ms0 = w39_bench_mode(0, &s0), ms1 = w39_bench_mode(1, &s1);
    assert(s0 == s1);
    totk_vk_spv_fast_reset_for_test(-1);
    printf("ktglopttest w39: req_off=%d off_ms=%.3f on_ms=%.3f ratio=%.2fx sink=%llu\n",
           req_off, ms0, ms1, ms1 > 0.0 ? ms0 / ms1 : 0.0, (unsigned long long)s1);
    return 0;
}

#endif
