/* Wave 54 (Opt 56 / TOTK_VK_TOUCH_ALIAS_FAST=1) unit tests, fault injection and benchmark. */
#ifndef TEST_VK_KTGL_OPT_W54_H
#define TEST_VK_KTGL_OPT_W54_H
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "vk_touch_alias_fast.h"
#ifdef TOTK_W54_STANDALONE
int g_fault_mode = 0;
#endif

_Atomic int g_totk_vk_touch_alias_fast_override = -1;
_Atomic int g_totk_w54_fault = -1;
static int g_w54_fail = 0;

typedef struct { uint32_t kind, dtype, count; int32_t reg; uint8_t rw; } w54_bind_t;

#define W54_EXPECT_EQ(m, w, g) do { \
    uint64_t wa_ = (uint64_t)(w), ga_ = (uint64_t)(g); \
    if (wa_ != ga_) { \
        g_w54_fail++; \
        fprintf(stderr, "FAIL %s:%d %s want=%llu got=%llu\n", __FILE__, __LINE__, m, \
                (unsigned long long)wa_, (unsigned long long)ga_); \
    } \
} while (0)

static uint64_t w54_ns(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static void w54_test_touch_sum(void) {
    w54_bind_t bs[28];
    for (int i = 0; i < 28; i++) {
        bs[i] = (w54_bind_t){1u, 6u, 1u, i & 15, 0u};
    }
    bs[3] = (w54_bind_t){2u, 7u, 1u, 2, 1u};
    bs[7] = (w54_bind_t){4u, 5u, 1u, 1, 1u};
    bs[12] = (w54_bind_t){3u, 0u, 1u, 4, 0u};
    bs[19] = (w54_bind_t){2u, 7u, 1u, 5, 0u};
    totk_sh_touch_sum_t ts = {0};
    for (int i = 0; i < 28; i++) {
        int is_sbuf = bs[i].dtype == 7u, is_stbuf = bs[i].dtype == 5u;
        int is_img3 = bs[i].dtype == 0u || bs[i].dtype == 1u || bs[i].dtype == 3u;
        totk_sh_touch_sum_add_stor(&ts, i, bs[i].rw, is_sbuf, is_stbuf, bs[i].reg);
        totk_sh_touch_sum_add_img(&ts, i, bs[i].rw, is_img3);
        int pm_bad = (bs[i].kind == 0u || bs[i].kind == 4u);
        int pm_skip = !pm_bad && (bs[i].dtype == 6u || bs[i].dtype == 9u);
        int pm_tex = !pm_bad && !pm_skip && (bs[i].dtype == 0u || bs[i].dtype == 1u || bs[i].dtype == 4u) && bs[i].reg >= 0 && bs[i].reg < 128;
        int fb_bad = (bs[i].dtype == 7u);
        int fb_tex = !fb_bad && bs[i].dtype == 0u;
        totk_sh_touch_sum_add_pm_fb(&ts, i, pm_bad, pm_skip, pm_tex, fb_bad, fb_tex);
    }
    totk_sh_touch_sum_finish(&ts, 28);
    W54_EXPECT_EQ("ro_sbuf_mask", UINT64_C(1) << 19, ts.ro_sbuf_mask);
    W54_EXPECT_EQ("rw_sbuf_mask", UINT64_C(1) << 3, ts.rw_sbuf_mask);
    W54_EXPECT_EQ("rw_stbuf_mask", UINT64_C(1) << 7, ts.rw_stbuf_mask);
    W54_EXPECT_EQ("pm_imm_haz", 1u, ts.pm_imm_hazard);
    W54_EXPECT_EQ("fb_imm_unk", 1u, ts.fb_imm_unknown);
    W54_EXPECT_EQ("fg_ro_img", UINT64_C(1) << 12, ts.fg_ro_img_mask);
    W54_EXPECT_EQ("pm_tex", UINT64_C(1) << 12, ts.pm_tex_mask);

    /* Separate RB_IMG-only check so Fault 2 (pm_bad_kind) is isolated from dtype != tex. */
    totk_sh_touch_sum_t ts2 = {0};
    totk_sh_touch_sum_add_pm_fb(&ts2, 0, 1, 0, 0, 0, 1);
    totk_sh_touch_sum_finish(&ts2, 1);
    W54_EXPECT_EQ("pm_bad_kind_only", 1u, ts2.pm_imm_hazard);
}

static void w54_test_alias_ub_and_ret(void) {
    totk_alias_buf_idx_t *ax = calloc(1, sizeof *ax);
    assert(ax != NULL);
    uint64_t abuf[32];
    for (int i = 0; i < 32; i++) abuf[i] = UINT64_C(0x10000) + (uint64_t)(i & 7) * 64u;
    assert(totk_alias_buf_sync_begin(ax, 32, 7u) == 1);
    for (int i = 0; i < 32; i++) totk_alias_buf_push(ax, i, abuf[i]);
    uint64_t sum_idx = 0u, sum_lin = 0u, qbuf = abuf[3];
    for (int i = totk_alias_buf_first(ax, qbuf); i >= 0; i = totk_alias_buf_next(ax, i, qbuf)) sum_idx += (uint64_t)(i + 1);
    for (int i = 0; i < 32; i++) if (abuf[i] == qbuf) sum_lin += (uint64_t)(i + 1);
    W54_EXPECT_EQ("alias_sum", sum_lin, sum_idx);
    W54_EXPECT_EQ("alias_miss", (uint64_t)(unsigned)-1, (uint64_t)(unsigned)totk_alias_buf_first(ax, UINT64_C(0xdeadbeef)));
    totk_alias_buf_free(ax);
    free(ax);

    totk_ub_unsealed_t u = {0};
    totk_ub_unsealed_note(&u, 14);
    totk_ub_unsealed_note(&u, 5);
    totk_ub_unsealed_shift_on_log(&u, 3, 2, 20);
    W54_EXPECT_EQ("ub_lo", 2u, (uint64_t)(unsigned)u.lo);
    W54_EXPECT_EQ("ub_hi_cov", 1u, (uint64_t)(u.hi >= 20 ? 1 : 0));
    totk_ub_unsealed_t u2 = {0};
    totk_ub_unsealed_note(&u2, 9);
    W54_EXPECT_EQ("ub_single_covers_9", 1u, (uint64_t)(u2.lo <= 9 && 9 < u2.hi ? 1 : 0));

    totk_retired_filter_t rf = {0};
    totk_retired_add(&rf, UINT64_C(0x2000), UINT64_C(0x3000));
    W54_EXPECT_EQ("ret_bv_hit", 1u, (uint64_t)totk_retired_may_have_bv(&rf, UINT64_C(0x2000)));
    W54_EXPECT_EQ("ret_img_hit", 1u, (uint64_t)totk_retired_may_have_img(&rf, UINT64_C(0x3000)));
    totk_retired_sub_bv(&rf, 1u);
    W54_EXPECT_EQ("ret_bv_cleared", 0u, (uint64_t)rf.n_bv);

    totk_ptr_set256_t ps = {0};
    int uniq = 0;
    for (int i = 0; i < 64; i++) uniq += totk_ptr_set256_insert(&ps, (uintptr_t)(0x1000u + (i & 15) * 32u));
    W54_EXPECT_EQ("pset_uniq", 16u, (uint64_t)(unsigned)uniq);
}

static void *w54_worker(void *arg) {
    const totk_alias_buf_idx_t *ax = (const totk_alias_buf_idx_t *)arg;
    uint64_t acc = 0u;
    for (int r = 0; r < 6000; r++) {
        uint64_t q = UINT64_C(0x10000) + (uint64_t)(r & 7) * 64u;
        for (int i = totk_alias_buf_first(ax, q); i >= 0; i = totk_alias_buf_next(ax, i, q)) acc += (uint64_t)(i + 1);
    }
    return (void *)(uintptr_t)acc;
}

static void w54_test_tsan_readers(void) {
    totk_alias_buf_idx_t *ax = calloc(1, sizeof *ax);
    assert(ax != NULL);
    assert(totk_alias_buf_sync_begin(ax, 32, 1u) == 1);
    for (int i = 0; i < 32; i++) totk_alias_buf_push(ax, i, UINT64_C(0x10000) + (uint64_t)(i & 7) * 64u);
    pthread_t th[4];
    for (int i = 0; i < 4; i++) assert(pthread_create(&th[i], NULL, w54_worker, ax) == 0);
    for (int i = 0; i < 4; i++) { void *r = NULL; assert(pthread_join(th[i], &r) == 0); assert((uintptr_t)r > 0u); }
    totk_alias_buf_free(ax);
    free(ax);
}

static void w54_bench_run(void) {
    enum { NB = 48, NALIAS = 256, NQ = 12000, REP = 3 };
    w54_bind_t bs[NB];
    totk_sh_touch_sum_t ts = {0};
    for (int i = 0; i < NB; i++) {
        int ro_sbuf = (i == 17 || i == 23);
        bs[i] = (w54_bind_t){(i == 5 || ro_sbuf) ? 3u : 1u, (i == 5 || ro_sbuf) ? 7u : (i & 3) ? 6u : 9u, 1u, i & 15, (i == 5)};
        totk_sh_touch_sum_add_stor(&ts, i, bs[i].rw, bs[i].dtype == 7u, 0, bs[i].reg);
        totk_sh_touch_sum_add_pm_fb(&ts, i, 0, bs[i].dtype != 7u, 0, bs[i].dtype == 7u, 0);
    }
    totk_sh_touch_sum_finish(&ts, NB);
    uint64_t *abuf = calloc(NALIAS, sizeof *abuf);
    totk_alias_buf_idx_t *ax = calloc(1, sizeof *ax);
    assert(abuf && ax);
    assert(totk_alias_buf_sync_begin(ax, NALIAS, 1u) == 1);
    for (int i = 0; i < NALIAS; i++) {
        abuf[i] = UINT64_C(0x70000) + (uint64_t)(i & 31) * 16u;
        totk_alias_buf_push(ax, i, abuf[i]);
    }
    totk_retired_filter_t rf = {0};
    uint64_t s0 = 0u, s1 = 0u, bs_ns = UINT64_MAX, bf_ns = UINT64_MAX;
    for (int r = 0; r < REP; r++) {
        uint64_t t0 = w54_ns(), a0 = 0u;
        for (int q = 0; q < NQ; q++) {
            for (int i = 0; i < NB; i++) if (!bs[i].rw && bs[i].dtype == 7u) a0 += (uint64_t)i;
            uint64_t qb = (q & 7) == 0 ? abuf[q & 31] : (UINT64_C(0x99000) + (uint64_t)(q & 63) * 16u);
            for (int i = 0; i < NALIAS; i++) if (abuf[i] == qb) a0 += (uint64_t)(i + 1);
            for (int i = 0; i < 64; i++) if (rf.n_bv && abuf[i] == qb) a0++;
        }
        uint64_t t1 = w54_ns(), a1 = 0u;
        for (int q = 0; q < NQ; q++) {
            for (uint64_t m = ts.ro_sbuf_mask; m; m &= m - 1u) a1 += (uint64_t)__builtin_ctzll(m);
            uint64_t qb = (q & 7) == 0 ? abuf[q & 31] : (UINT64_C(0x99000) + (uint64_t)(q & 63) * 16u);
            for (int i = totk_alias_buf_first(ax, qb); i >= 0; i = totk_alias_buf_next(ax, i, qb)) a1 += (uint64_t)(i + 1);
            if (totk_retired_may_have_bv(&rf, qb)) a1++;
        }
        uint64_t t2 = w54_ns();
        s0 = a0; s1 = a1;
        if (t1 - t0 < bs_ns) bs_ns = t1 - t0;
        if (t2 - t1 < bf_ns) bf_ns = t2 - t1;
    }
    W54_EXPECT_EQ("bench_sink", s0, s1);
    double sms = (double)bs_ns / 1e6, fms = (double)bf_ns / 1e6;
    printf("ktglopttest w54: slow=%.3f ms fast=%.3f ms speedup=%.2fx sink=%llu failures=%d\n",
           sms, fms, sms / (fms > 0.001 ? fms : 0.001), (unsigned long long)s1, g_w54_fail);
    totk_alias_buf_free(ax);
    free(abuf); free(ax);
}

static int run_ktgl_w54_tests(int fault_mode) {
    if (fault_mode > 0) totk_w54_set_fault(fault_mode);
    g_w54_fail = 0;
    totk_vk_touch_alias_fast_set_for_test(1);
    w54_test_touch_sum();
    w54_test_alias_ub_and_ret();
    w54_test_tsan_readers();
    w54_bench_run();
    return g_w54_fail != 0;
}
#ifdef TOTK_W54_STANDALONE
int main(void) {
    return run_ktgl_w54_tests(totk_w54_fault_mode());
}
#endif
#endif
