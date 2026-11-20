/* Wave 21 (Opt 23: TOTK_VK_SERIAL_FAST=1) unit, benchmark & fault-injection suite (faults 59..61). */
#ifndef TEST_VK_KTGL_OPT_W21_H
#define TEST_VK_KTGL_OPT_W21_H

#include "vk_serial_fast.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t w21_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

typedef struct {
    uint64_t serial, x_u;
    uint8_t non_mirrored, valid;
} w21_slot_t;

typedef struct {
    uint32_t shared, refs, wbrefs, retired;
    uint64_t idle_sub;
} w21_img_t;

enum { W21_CAP = 2048u };
static pthread_mutex_t g_w21_mu = PTHREAD_MUTEX_INITIALIZER;
static w21_slot_t g_w21_slots[64];

static uint64_t w21_legacy_get(uint64_t ser) {
    pthread_mutex_lock(&g_w21_mu);
    uint64_t x = 0;
    for (uint32_t i = 0; i < 64u; i++) {
        if (g_w21_slots[i].valid && g_w21_slots[i].serial == ser) {
            x = g_w21_slots[i].x_u; break;
        }
    }
    pthread_mutex_unlock(&g_w21_mu);
    return x;
}

static int w21_test_correctness(void) {
    totk_tex_serial_memo4 *m = calloc(1, sizeof *m);
    assert(m != NULL);
    uint64_t ser_nm = 0x101u, ser_m = 0x202u, x = 0;
    totk_tex_serial_memo4_put(m, ser_nm, 1u, 10u, 1u, 0xaaa0u);
    totk_tex_serial_memo4_put(m, ser_m, 1u, 10u, 0u, 0xbbb0u);
    if (!totk_tex_serial_memo4_get(m, ser_nm, 1u, 11u, &x) || x != 0xaaa0u) { free(m); return 1; }
    if (!totk_tex_serial_memo4_get(m, ser_m, 1u, 10u, &x) || x != 0xbbb0u) { free(m); return 1; }
    if (totk_tex_serial_memo4_get(m, ser_m, 1u, 11u, &x)) { free(m); return 1; }
    if (totk_tex_serial_memo4_get(m, ser_nm, 2u, 11u, &x)) { free(m); return 1; }
    free(m);

    totk_idle_shared_summary sum = {0, 0};
    if (!totk_idle_shared_can_skip(&sum, 1000u, 600u)) return 1;
    totk_idle_shared_note(&sum, 500u);
    if (!totk_idle_shared_can_skip(&sum, 1099u, 600u)) return 1;
    if (totk_idle_shared_can_skip(&sum, 1100u, 600u)) return 1;
    totk_idle_shared_reset(&sum, 0u, 0u);
    if (!totk_idle_shared_can_skip(&sum, 5000u, 600u)) return 1;
    return 0;
}

static int w21_bench_serial_memo(void) {
    for (uint32_t i = 0; i < 64u; i++) {
        g_w21_slots[i] = (w21_slot_t){0x5000u + i, 0x900000u + i * 16u, (uint8_t)((i & 7u) != 0u), 1u};
    }
    totk_tex_serial_memo4 *m = calloc(1, sizeof *m);
    assert(m != NULL);
    _Atomic uint32_t sgen = 1u;
    enum { ITERS = 400000 };
    uint64_t leg = 0, fast = 0, t0 = w21_now_ns();
    for (int i = 0; i < ITERS; i++) {
        uint64_t ser = 0x5000u + (uint64_t)(i & 31);
        leg ^= w21_legacy_get(ser);
    }
    uint64_t leg_ns = w21_now_ns() - t0;
    t0 = w21_now_ns();
    for (int i = 0; i < ITERS; i++) {
        uint64_t ser = 0x5000u + (uint64_t)(i & 31);
        uint64_t sub = 100u + (uint64_t)(i >> 5);
        uint32_t sg0 = atomic_load_explicit(&sgen, memory_order_acquire);
        uint64_t x = 0;
        if (!(totk_tex_serial_memo4_get(m, ser, sg0, sub, &x) &&
              atomic_load_explicit(&sgen, memory_order_acquire) == sg0)) {
            x = w21_legacy_get(ser);
            uint8_t non_mir = (uint8_t)(((i & 31) & 7) != 0);
            totk_tex_serial_memo4_put(m, ser, atomic_load_explicit(&sgen, memory_order_relaxed), sub, non_mir, x);
        }
        fast ^= x;
    }
    uint64_t fast_ns = w21_now_ns() - t0;
    free(m);
    printf("aoc_w21_serial_memo: iters=%d leg_ms=%.3f fast_ms=%.3f speedup=%.2fx diff=%llu\n",
           ITERS, leg_ns / 1e6, fast_ns / 1e6,
           fast_ns ? (double)leg_ns / (double)fast_ns : 0.0,
           (unsigned long long)(leg ^ fast));
    return leg != fast;
}

static uint64_t w21_sweep_scan(w21_img_t *imgs, uint64_t sub, totk_idle_shared_summary *sum) {
    uint32_t rc = 0;
    uint64_t rmin = 0, retired = 0;
    for (uint32_t i = 0; i < W21_CAP; i++) {
        w21_img_t *im = &imgs[i];
        if (!im->shared || im->refs || im->wbrefs) continue;
        if (sub - im->idle_sub < 600u) {
            if (!rc || im->idle_sub < rmin) rmin = im->idle_sub;
            rc++;
            continue;
        }
        im->shared = 0;
        im->retired++;
        retired++;
    }
    if (sum) totk_idle_shared_reset(sum, rc, rmin);
    return retired;
}

static int w21_bench_idle_sweep(void) {
    static w21_img_t a[W21_CAP], b[W21_CAP];
    memset(a, 0, sizeof a);
    memset(b, 0, sizeof b);
    totk_idle_shared_summary sum = {0, 0};
    for (uint32_t i = 0; i < W21_CAP; i++) {
        uint32_t busy = (i % 16u) != 0u;
        a[i] = (w21_img_t){1u, busy, 0u, 0u, 2000u + (i & 31u)};
        b[i] = a[i];
        if (!busy) totk_idle_shared_note(&sum, b[i].idle_sub);
    }
    enum { ITERS = 20000 };
    uint64_t leg_ret = 0, fast_ret = 0, t0 = w21_now_ns();
    for (int it = 0; it < ITERS; it++) {
        uint64_t sub = 2100u + (uint64_t)it;
        if ((it & 255) == 0) {
            uint32_t idx = (uint32_t)(it & 255u) * 8u;
            a[idx].refs = 0u; a[idx].idle_sub = sub;
        }
        leg_ret += w21_sweep_scan(a, sub, NULL);
    }
    uint64_t leg_ns = w21_now_ns() - t0;
    t0 = w21_now_ns();
    for (int it = 0; it < ITERS; it++) {
        uint64_t sub = 2100u + (uint64_t)it;
        if ((it & 255) == 0) {
            uint32_t idx = (uint32_t)(it & 255u) * 8u;
            b[idx].refs = 0u; b[idx].idle_sub = sub;
            totk_idle_shared_note(&sum, sub);
        }
        if (!totk_idle_shared_can_skip(&sum, sub, 600u)) fast_ret += w21_sweep_scan(b, sub, &sum);
    }
    uint64_t fast_ns = w21_now_ns() - t0;
    printf("aoc_w21_idle_sweep: iters=%d leg_ms=%.3f fast_ms=%.3f speedup=%.2fx diff=%llu\n",
           ITERS, leg_ns / 1e6, fast_ns / 1e6,
           fast_ns ? (double)leg_ns / (double)fast_ns : 0.0,
           (unsigned long long)(leg_ret ^ fast_ret));
    return leg_ret != fast_ret || memcmp(a, b, sizeof a) != 0;
}

static int run_ktgl_w21_tests(int fault_mode) {
    (void)fault_mode;
    if (w21_test_correctness() != 0) return 1;
    if (w21_bench_serial_memo() != 0) return 1;
    if (w21_bench_idle_sweep() != 0) return 1;
    return 0;
}

#endif
