#ifndef TEST_VK_KTGL_OPT_W49_H
#define TEST_VK_KTGL_OPT_W49_H
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "vk_res_samp_fast.h"
#ifdef TOTK_W49_STANDALONE
int g_fault_mode = 0;
#endif

_Atomic int g_totk_vk_res_samp_fast_override = -1;
_Atomic int g_totk_w49_fault = -1;
static int g_w49_fail = 0;

#define W49_EXPECT_EQ(m, w, g) do { \
    uint64_t wa_ = (uint64_t)(w), ga_ = (uint64_t)(g); \
    if (wa_ != ga_) { \
        g_w49_fail++; \
        fprintf(stderr, "FAIL %s:%d %s want=%llu got=%llu\n", __FILE__, __LINE__, m, \
                (unsigned long long)wa_, (unsigned long long)ga_); \
    } \
} while (0)

static uint64_t w49_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static uint32_t w49_ref_srgb_twin(uint32_t f) {
    switch (f) {
    case 37u: return 43u; case 43u: return 37u;
    case 44u: return 50u; case 50u: return 44u;
    case 51u: return 57u; case 57u: return 51u;
    default: return 0u;
    }
}

static void w49_ref_hash_bytes(uint64_t h[2], const void *bytes, size_t n) {
    const uint8_t *p = (const uint8_t *)bytes;
    for (size_t i = 0u; i < n; i++) {
        h[0] = (h[0] ^ p[i]) * UINT64_C(1099511628211);
        h[1] = (h[1] ^ p[i]) * UINT64_C(0x9e3779b185ebca87);
        h[1] = (h[1] << 13) | (h[1] >> 51);
    }
}

static int w49_ref_fields(unsigned op, const uint8_t *a, size_t size, totk_xlate_field_t f[17]) {
    if (op == 0u || op == 2u) return 0;
    if (op == 1u || op == 3u) { if (size < 8u) return -1; f[0] = (totk_xlate_field_t){0u, 8u}; return 1; }
    if (op == 7u) { if (size < 16u) return -1; uint64_t usz = 0u; memcpy(&usz, a + 8u, 8); if (usz > size - 16u) return -1; f[0] = (totk_xlate_field_t){0u, 8u}; return 1; }
    if (op == 8u) { if (size < 216u) return -1; for (unsigned i = 0u; i < 8u; i++) f[i] = (totk_xlate_field_t){8u + i * 8u, 8u}; f[8] = (totk_xlate_field_t){208u, 8u}; return 9; }
    if (op == 18u) {
        if (size < 8u) return -1;
        int32_t bn = 0; memcpy(&bn, a + 4u, 4);
        if (bn < 0 || bn > 16 || (size_t)bn > (size - 8u) / 16u) return -1;
        for (int32_t i = 0; i < bn; i++) f[i] = (totk_xlate_field_t){8u + (size_t)i * 16u, 8u};
        return (int)bn;
    }
    if (op == 21u || op == 22u || op == 36u) {
        if (size < 24u) return -1;
        int32_t hn = 0; memcpy(&hn, a + 8u, 4);
        if (hn < 0 || hn > 32 || (size_t)hn > (size - 24u) / 8u) return -1;
        f[0] = (totk_xlate_field_t){16u, 8u}; return 1;
    }
    if (op == 26u) { if (size < 32u) return -1; f[0] = (totk_xlate_field_t){24u, 8u}; return 1; }
    return -1;
}

static const totk_xlate_op_spec_t k_w49_specs[47] = {
    [0] = {0u, 0u, TOTK_XL_NONE, 0u, 1u, 0u, 0u, 0u, 0u, 0u},
    [1] = {8u, 0u, TOTK_XL_FIXED1, 0u, 1u, 0u, 0u, 8u, 0u, 0u},
    [2] = {0u, 0u, TOTK_XL_NONE, 0u, 1u, 0u, 0u, 0u, 0u, 0u},
    [3] = {8u, 0u, TOTK_XL_FIXED1, 0u, 1u, 0u, 0u, 8u, 0u, 0u},
    [7] = {16u, 0u, TOTK_XL_UPDATE_UB, 0u, 1u, 8u, 0u, 8u, 0u, 0u},
    [8] = {216u, 8u, TOTK_XL_SET_RT, 0u, 1u, 0u, 8u, 8u, 208u, 8u},
    [18] = {8u, 16u, TOTK_XL_BIND_VBUF, 16u, 1u, 4u, 0u, 0u, 8u, 8u},
    [21] = {24u, 8u, TOTK_XL_ARR_CHECK, 32u, 1u, 8u, 16u, 8u, 0u, 0u},
    [22] = {24u, 8u, TOTK_XL_ARR_CHECK, 32u, 1u, 8u, 16u, 8u, 0u, 0u},
    [26] = {32u, 0u, TOTK_XL_FIXED1, 0u, 1u, 0u, 24u, 8u, 0u, 0u},
    [36] = {24u, 8u, TOTK_XL_ARR_CHECK, 32u, 1u, 8u, 16u, 8u, 0u, 0u}
};

static void w49_test_tracker_and_samp(void) {
    totk_tb_tracker_t tr = {0};
    W49_EXPECT_EQ("tb_empty", 0, totk_tb_may_have(&tr, 0x1000u));
    totk_tb_note_add(&tr, 0x12340000u);
    totk_tb_note_add(&tr, 0x56780000u);
    W49_EXPECT_EQ("tb_has1", 1, totk_tb_may_have(&tr, 0x12340000u));
    W49_EXPECT_EQ("tb_has2", 1, totk_tb_may_have(&tr, 0x56780000u));
    totk_tb_note_sub(&tr, 1u);
    W49_EXPECT_EQ("tb_still", 1, totk_tb_may_have(&tr, 0x56780000u));
    totk_tb_note_sub(&tr, 1u);
    W49_EXPECT_EQ("tb_cleared", 0, totk_tb_may_have(&tr, 0x12340000u));

    uint8_t s0[44], s1[44];
    for (size_t i = 0u; i < 44u; i++) s0[i] = (uint8_t)(i * 7u + 3u);
    memcpy(s1, s0, 44u);
    W49_EXPECT_EQ("samp_eq_same", 1, totk_samp_par_eq_fast(s0, s1));
    for (size_t off = 0u; off < 44u; off++) {
        s1[off] ^= 0x10u;
        W49_EXPECT_EQ("samp_eq_diff", 0, totk_samp_par_eq_fast(s0, s1));
        s1[off] ^= 0x10u;
    }
    totk_sreg_shadow_t *sh = calloc(1u, sizeof *sh);
    if (!sh) { g_w49_fail++; return; }
    W49_EXPECT_EQ("sreg_cold", 0, totk_sreg_same_with_obj(sh, 5, s0));
    totk_sreg_write(sh, 5, s0, 44u, 0u);
    W49_EXPECT_EQ("sreg_no_obj", 0, totk_sreg_same_with_obj(sh, 5, s0));
    totk_sreg_mark_obj(sh, 5, 1u);
    W49_EXPECT_EQ("sreg_hit", 1, totk_sreg_same_with_obj(sh, 5, s0));
    s1[43] ^= 1u;
    W49_EXPECT_EQ("sreg_miss_tail", 0, totk_sreg_same_with_obj(sh, 5, s1));
    free(sh);
    for (uint32_t f = 0u; f < 100u; f++) W49_EXPECT_EQ("srgb_twin", w49_ref_srgb_twin(f), totk_srgb_twin_fast(f));
}

static void w49_test_xlate_parity(void) {
    uint8_t row_r[128], row_f[128];
    const int32_t firsts[4] = {0, 12, -3, 120};
    for (int fi = 0; fi < 4; fi++) {
        memset(row_r, 0, sizeof row_r);
        memset(row_f, 0, sizeof row_f);
        int32_t first = firsts[fi], n = 10;
        for (int i = 0; i < n; i++) if ((unsigned)first + (unsigned)i < 128u) row_r[first + i] = 1u;
        totk_xlate_deferred_bind_fast(row_f, 128u, first, n, 1);
        W49_EXPECT_EQ("def_bind_cmp", 0, memcmp(row_r, row_f, 128u));
    }
    uint8_t payload[264];
    for (size_t i = 0u; i < sizeof payload; i++) payload[i] = (uint8_t)(i & 0xffu);
    uint64_t a[2] = {1u, 2u}, b[2] = {1u, 2u};
    totk_xlate_hash_bytes_fast(a, payload, sizeof payload);
    totk_xlate_hash_bytes_fast(b, payload, sizeof payload);
    W49_EXPECT_EQ("hash_eq0", a[0], b[0]);
    W49_EXPECT_EQ("hash_eq1", a[1], b[1]);
    payload[17] ^= 1u;
    totk_xlate_hash_bytes_fast(b, payload, sizeof payload);
    W49_EXPECT_EQ("hash_diff", 1, a[0] != b[0] || a[1] != b[1]);

    memset(payload, 0, sizeof payload);
    int32_t four = 4;
    memcpy(payload + 4u, &four, 4);
    memcpy(payload + 8u, &four, 4);
    static const unsigned ops[8] = {0u, 1u, 7u, 8u, 18u, 21u, 26u, 55u};
    for (unsigned oi = 0u; oi < 8u; oi++) {
        for (size_t sz = 0u; sz <= 264u; sz += 24u) {
            totk_xlate_field_t fr[17], ff[17];
            int nr = w49_ref_fields(ops[oi], payload, sz, fr);
            int nf = totk_xlate_address_fields_fast(k_w49_specs, 47u, ops[oi], payload, sz, ff);
            W49_EXPECT_EQ("xlate_n", nr, nf);
            for (int k = 0; k < nr; k++) {
                W49_EXPECT_EQ("xlate_off", fr[k].offset, ff[k].offset);
                W49_EXPECT_EQ("xlate_sz", fr[k].size, ff[k].size);
            }
        }
    }
}

typedef struct { totk_sreg_shadow_t *sh; totk_tb_tracker_t *tr; uint32_t tid; uint32_t errs; } w49_thr_arg_t;

static void *w49_tsan_worker(void *arg) {
    w49_thr_arg_t *t = (w49_thr_arg_t *)arg;
    uint8_t sp[44];
    memset(sp, 0x22, 44u);
    sp[0] = (uint8_t)(t->tid + 1u);
    for (uint32_t i = 0u; i < 200u; i++) {
        if (t->tid == 0u) {
            totk_sreg_write(t->sh, (int)(i & 15u), sp, 44u, 1u);
        } else {
            (void)totk_sreg_same_with_obj(t->sh, (int)(i & 15u), sp);
        }
        (void)totk_tb_may_have(t->tr, UINT64_C(0xabc000) + i);
        if (totk_srgb_twin_fast(37u) != 43u) t->errs++;
    }
    return NULL;
}

static void w49_test_tsan(void) {
    totk_sreg_shadow_t *sh = calloc(1u, sizeof *sh);
    totk_tb_tracker_t tr = {0};
    if (!sh) { g_w49_fail++; return; }
    totk_tb_note_add(&tr, UINT64_C(0xabc000));
    pthread_t th[4];
    w49_thr_arg_t args[4];
    for (uint32_t t = 0u; t < 4u; t++) {
        args[t] = (w49_thr_arg_t){sh, &tr, t, 0u};
        pthread_create(&th[t], NULL, w49_tsan_worker, &args[t]);
    }
    for (uint32_t t = 0u; t < 4u; t++) {
        pthread_join(th[t], NULL);
        W49_EXPECT_EQ("tsan_worker", 0u, args[t].errs);
    }
    free(sh);
}

static uint64_t w49_bench_pass(int fast, uint32_t iters) {
    totk_vk_res_samp_fast_set_for_test(fast);
    totk_sreg_shadow_t *sh = calloc(1u, sizeof *sh);
    totk_tb_tracker_t tr = {0};
    if (!sh) { g_w49_fail++; return 0u; }
    uint8_t recs[32][44];
    for (int s = 0; s < 32; s++) {
        for (int b = 0; b < 44; b++) recs[s][b] = (uint8_t)(s * 5 + b);
        totk_sreg_write(sh, s, recs[s], 44u, 1u);
    }
    uint8_t pay[64];
    for (size_t i = 0u; i < 64u; i++) pay[i] = (uint8_t)(i * 3u + 1u);
    uint8_t row[128];
    totk_xlate_field_t flds[17];
    uint64_t h[2] = {UINT64_C(0x123456789abcdef0), UINT64_C(0x0fedcba987654321)};
    uint64_t sink = 0u;
    for (uint32_t it = 0u; it < iters; it++) {
        int sid = (int)(it & 31u);
        uint32_t fmt = 37u + (it & 15u);
        if (fast) {
            sink += (uint64_t)totk_tb_may_have(&tr, UINT64_C(0xdeadbeef) + sid);
            sink += (uint64_t)totk_sreg_same_with_obj(sh, sid, recs[sid]);
            sink += totk_srgb_twin_fast(fmt);
            totk_xlate_hash_bytes_fast(h, pay, 64u);
            totk_xlate_deferred_bind_fast(row, 128u, 16, 24, 1);
            sink += (uint64_t)totk_xlate_address_fields_fast(k_w49_specs, 47u, 26u, pay, 64u, flds);
        } else {
            sink += (uint64_t)(memcmp(recs[sid], recs[sid], 44u) == 0);
            sink += w49_ref_srgb_twin(fmt);
            w49_ref_hash_bytes(h, pay, 64u);
            for (int i = 0; i < 24; i++) row[16 + i] = 1u;
            sink += (uint64_t)w49_ref_fields(26u, pay, 64u, flds);
        }
        sink += (h[0] & 3u) + row[16];
    }
    free(sh);
    return sink;
}

static int run_ktgl_w49_tests(int fault_mode) {
    g_w49_fail = 0;
    const char *ef = getenv("TOTK_W49_FAULT");
    if (ef && *ef) totk_w49_set_fault(atoi(ef));
    if (fault_mode >= 191 && fault_mode <= 195) totk_w49_set_fault(fault_mode - 190);
    totk_vk_res_samp_fast_set_for_test(1);
    w49_test_tracker_and_samp();
    w49_test_xlate_parity();
    w49_test_tsan();
    uint32_t iters = 60000u;
    uint64_t t0 = w49_now_ns();
    (void)w49_bench_pass(0, iters);
    uint64_t dt_slow = w49_now_ns() - t0;
    uint64_t t1 = w49_now_ns();
    uint64_t s_fast = w49_bench_pass(1, iters);
    uint64_t dt_fast = w49_now_ns() - t1;
    printf("aoc_w49_res_samp_fast: slow=%.3f ms fast=%.3f ms speedup=%.2fx sink=%llu\n",
           (double)dt_slow / 1e6, (double)dt_fast / 1e6,
           dt_fast ? (double)dt_slow / (double)dt_fast : 0.0, (unsigned long long)s_fast);
    totk_w49_set_fault(0);
    return g_w49_fail != 0;
}

#ifdef TOTK_W49_STANDALONE
int main(void) {
    const char *f = getenv("TOTK_KTGL_OPT_FAULT");
    if (f && *f) g_fault_mode = atoi(f);
    int rc = run_ktgl_w49_tests(g_fault_mode);
    printf("w49_standalone: fault=%d failures=%d\n", g_fault_mode, rc);
    return rc;
}
#endif
#endif
