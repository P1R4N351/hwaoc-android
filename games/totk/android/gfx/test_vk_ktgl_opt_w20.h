#ifndef TEST_VK_KTGL_OPT_W20_H
#define TEST_VK_KTGL_OPT_W20_H
/* Wave 20 (Opt 22): Tests, benchmarks & Delta #27 faults 56..58 for vk_reg_wb_fast.h. */
#include "vk_reg_wb_fast.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint64_t host_now_ns(void);
extern int g_fault_mode;

#define W20_CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL[w20]: %s (line %d)\n", msg, __LINE__); return 1; } \
} while (0)

typedef struct {
    uint64_t serial, mem, sz;
    uint32_t fmt, w, h;
    uint8_t tail[96];
} mock_vk_treg_t;

static int test_w20_treg_memo(void) {
    totk_treg_memo4 *tls = NULL;
    mock_vk_treg_t v1 = {101u, 0x100000u, 0x4000u, 0x25u, 256u, 256u, {7}};
    mock_vk_treg_t v2 = {202u, 0x200000u, 0x8000u, 0x2du, 512u, 512u, {9}};
    mock_vk_treg_t out = {0};
    int ok = 0;
    W20_CHECK(!totk_treg_memo4_get(&tls, 5u, 2u, &out, sizeof out, &ok), "empty memo must miss");
    totk_treg_memo4_put(&tls, 5u, 2u, 1, &v1, sizeof v1);
    W20_CHECK(totk_treg_memo4_get(&tls, 5u, 2u, &out, sizeof out, &ok) && ok && out.serial == 101u, "memo hit");
    W20_CHECK(!totk_treg_memo4_get(&tls, 5u, 3u, &out, sizeof out, &ok), "stale generation must miss");
    totk_treg_memo4_put(&tls, 5u, 3u, 1, &v2, sizeof v2);
    W20_CHECK(totk_treg_memo4_get(&tls, 5u, 3u, &out, sizeof out, &ok) && ok && out.serial == 202u, "updated gen hit");
    totk_treg_memo4_put(&tls, 7u, 2u, 0, NULL, sizeof v1);
    ok = 1;
    W20_CHECK(totk_treg_memo4_get(&tls, 7u, 2u, &out, sizeof out, &ok) && !ok, "negative unregistered hit");
    for (uint32_t k = 0; k < 5u; k++) {
        uint32_t t = 100u + k * 32u;
        v1.serial = 500u + k;
        totk_treg_memo4_put(&tls, t, 2u, 1, &v1, sizeof v1);
    }
    free(tls);
    return 0;
}

static int test_w20_wb_sum(void) {
    totk_wb_summary sum;
    memset(&sum, 0, sizeof sum);
    W20_CHECK(!totk_wb_sum_may_overlap(&sum, 0x20000000u, 0x1000u), "empty summary disjoint");
    totk_wb_sum_delta(&sum, 0x20000000u, 0x20000u, 1);
    W20_CHECK(totk_wb_sum_may_overlap(&sum, 0x20010000u, 0x1000u), "live range must overlap");
    W20_CHECK(!totk_wb_sum_may_overlap(&sum, 0x30000000u, 0x1000u), "far range must reject");
    totk_wb_sum_delta(&sum, 0x20000000u, 0x20000u, -1);
    W20_CHECK(!totk_wb_sum_may_overlap(&sum, 0x20010000u, 0x1000u), "removed range must reject");
    totk_wb_sum_replace(&sum, 0u, 0u, 0x40000000u, 0x800000u);
    W20_CHECK(totk_wb_sum_may_overlap(&sum, 0x40400000u, 0x1000u), "huge >4MiB range overlaps");
    totk_wb_sum_replace(&sum, 0x40000000u, 0x800000u, 0u, 0u);
    W20_CHECK(!totk_wb_sum_may_overlap(&sum, 0x40400000u, 0x1000u), "cleared huge range rejects");
    totk_wb_landing_begin(&sum);
    totk_wb_landing_add(&sum, 0x60000000u, 0x4000u);
    totk_wb_landing_end(&sum);
    W20_CHECK(totk_wb_sum_may_overlap(&sum, 0x60001000u, 0x1000u), "landing range overlaps");
    totk_wb_landing_begin(&sum);
    totk_wb_landing_end(&sum);
    W20_CHECK(!totk_wb_sum_may_overlap(&sum, 0x60001000u, 0x1000u), "cleared landing rejects");
    atomic_fetch_add(&sum.seq, 1u);
    W20_CHECK(totk_wb_sum_may_overlap(&sum, 0x70000000u, 0x1000u), "odd seqlock conservatively overlaps");
    atomic_fetch_add(&sum.seq, 1u);
    return 0;
}

static int bench_w20_treg_memo(void) {
    mock_vk_treg_t tab[32], out = {0};
    _Atomic uint32_t gen[32];
    pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    totk_treg_memo4 *tls = NULL;
    for (uint32_t i = 0; i < 32u; i++) {
        memset(&tab[i], (int)(i + 1u), sizeof tab[i]);
        tab[i].serial = 1000u + i;
        atomic_init(&gen[i], 2u);
        totk_treg_memo4_put(&tls, i + 1u, 3u, 1, &tab[i], sizeof tab[i]);
    }
    uint64_t lsum = 0u, fsum = 0u;
    uint64_t t0 = host_now_ns();
    for (int it = 0; it < 200000; it++) {
        uint32_t id = (uint32_t)(it & 31) + 1u;
        pthread_mutex_lock(&mu);
        out = tab[id - 1u];
        pthread_mutex_unlock(&mu);
        lsum += out.serial;
    }
    double leg_ms = (double)(host_now_ns() - t0) * 1e-6;
    t0 = host_now_ns();
    for (int it = 0; it < 200000; it++) {
        uint32_t id = (uint32_t)(it & 31) + 1u, g = atomic_load_explicit(&gen[id - 1u], memory_order_acquire);
        int ok = 0;
        if (!totk_treg_memo4_get(&tls, id, (uint64_t)g + 1ull, &out, sizeof out, &ok)) {
            pthread_mutex_lock(&mu);
            out = tab[id - 1u];
            pthread_mutex_unlock(&mu);
            totk_treg_memo4_put(&tls, id, (uint64_t)g + 1ull, 1, &out, sizeof out);
        }
        fsum += out.serial;
    }
    double fast_ms = (double)(host_now_ns() - t0) * 1e-6;
    printf("aoc_w20_treg_memo: iters=200000 leg_ms=%.3f fast_ms=%.3f speedup=%.2fx diff=%llu\n",
           leg_ms, fast_ms, fast_ms > 0.0 ? leg_ms / fast_ms : 0.0, (unsigned long long)(lsum ^ fsum));
    free(tls);
    return lsum != fsum;
}

static int bench_w20_wb_bloom(void) {
    typedef struct { uint64_t a, s; } wb_r;
    wb_r rows[32];
    totk_wb_summary sum;
    pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    memset(&sum, 0, sizeof sum);
    for (uint32_t i = 0; i < 32u; i++) {
        rows[i].a = 0x20000000ull + ((uint64_t)i << 17);
        rows[i].s = 0x4000u;
        totk_wb_sum_delta(&sum, rows[i].a, rows[i].s, 1);
    }
    uint64_t lsum = 0u, fsum = 0u;
    uint64_t t0 = host_now_ns();
    for (int it = 0; it < 200000; it++) {
        uint64_t qa = 0x50000000ull + (((uint64_t)it & 63u) << 17), qn = 0x2000u;
        int hit = 0;
        pthread_mutex_lock(&mu);
        for (int j = 0; j < 32; j++) {
            if (rows[j].a < qa + qn && qa < rows[j].a + rows[j].s) { hit = 1; break; }
        }
        pthread_mutex_unlock(&mu);
        lsum += (uint64_t)hit;
    }
    double leg_ms = (double)(host_now_ns() - t0) * 1e-6;
    t0 = host_now_ns();
    for (int it = 0; it < 200000; it++) {
        uint64_t qa = 0x50000000ull + (((uint64_t)it & 63u) << 17), qn = 0x2000u;
        int hit = 0;
        if (totk_wb_sum_may_overlap(&sum, qa, qn)) {
            pthread_mutex_lock(&mu);
            for (int j = 0; j < 32; j++) {
                if (rows[j].a < qa + qn && qa < rows[j].a + rows[j].s) { hit = 1; break; }
            }
            pthread_mutex_unlock(&mu);
        }
        fsum += (uint64_t)hit;
    }
    double fast_ms = (double)(host_now_ns() - t0) * 1e-6;
    printf("aoc_w20_wb_bloom: iters=200000 leg_ms=%.3f fast_ms=%.3f speedup=%.2fx diff=%llu\n",
           leg_ms, fast_ms, fast_ms > 0.0 ? leg_ms / fast_ms : 0.0, (unsigned long long)(lsum ^ fsum));
    return lsum != fsum;
}

static int run_ktgl_w20_tests(int fault_mode) {
    (void)fault_mode;
    if (test_w20_treg_memo() != 0) return 1;
    if (test_w20_wb_sum() != 0) return 1;
    if (!fault_mode) {
        if (bench_w20_treg_memo() != 0 || bench_w20_wb_bloom() != 0) return 1;
    }
    return 0;
}
#endif
