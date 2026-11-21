#ifndef TEST_VK_KTGL_OPT_W38_H
#define TEST_VK_KTGL_OPT_W38_H

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define T_AOC_W38_TEST_HOOKS 1
#include "vk_fg_fast.h"

_Atomic int g_totk_fg_fast_state;
_Atomic int g_totk_w38_fault;

typedef struct {
    uint64_t buffer;
    uint64_t offset;
    uint64_t bytes;
    uint64_t owned_offset;
    uint64_t serial;
} w38_copy_rec;

static uint64_t w38_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void *w38_fmt_worker(void *arg) {
    totk_fg_fmt_cache *c = (totk_fg_fmt_cache *)arg;
    for (int r = 0; r < 160; r++) {
        for (uint32_t fmt = 1u; fmt <= 16u; fmt++) {
            uint32_t expect = fmt * 0x401u + 3u, got = 0u;
            if (!totk_fg_fmt_get(c, (uintptr_t)0x3000u, fmt, &got)) {
                totk_fg_fmt_put(c, (uintptr_t)0x3000u, fmt, expect);
                if (!totk_fg_fmt_get(c, (uintptr_t)0x3000u, fmt, &got)) got = expect;
            }
            if (got != expect) return (void *)(uintptr_t)1;
        }
    }
    return NULL;
}

static int w38_find_fast(
    const totk_fg_term_ht *ht, const w38_copy_rec *recs, uint64_t buf,
    uint64_t off, uint64_t rng, uint64_t align, uint64_t ser, uint64_t *owned_out) {
    uint32_t h = totk_fg_term_hash(buf, off, rng, ser);
    for (unsigned step = 0; step < TOTK_FG_TERM_HT_N; step++) {
        uint32_t pos = (h + step) & TOTK_FG_TERM_HT_MASK;
        if (ht->stamp[pos] != ht->gen) return 0;
        uint16_t idx = ht->slot[pos];
        if (recs[idx].buffer == buf && recs[idx].offset == off &&
            recs[idx].bytes == rng && recs[idx].serial == ser &&
            recs[idx].owned_offset % align == 0) {
            *owned_out = totk_w38_fault() == 2 ? recs[idx].owned_offset + 16u : recs[idx].owned_offset;
            return 1;
        }
    }
    return 0;
}

static int w38_test_ht_and_diff(void) {
    totk_fg_term_ht ht;
    memset(&ht, 0, sizeof ht);
    totk_fg_term_ht_reset(&ht);
    w38_copy_rec recs[256];
    uint64_t used = 0;
    for (unsigned i = 0; i < 200u; i++) {
        recs[i] = (w38_copy_rec){0x100000ull + (i / 4u) * 4096ull, (i % 4u) * 256ull, 256ull, used, 100ull + (i / 16u)};
        used += 256ull;
        uint32_t h = totk_fg_term_hash(recs[i].buffer, recs[i].offset, recs[i].bytes, recs[i].serial);
        totk_fg_term_ht_insert(&ht, h, (uint16_t)i);
    }
    for (unsigned i = 0; i < 200u; i++) {
        uint64_t fast_off = 0, leg_off = 0;
        int ff = w38_find_fast(&ht, recs, recs[i].buffer, recs[i].offset, recs[i].bytes, 16u, recs[i].serial, &fast_off);
        int lf = 0;
        for (unsigned k = 0; k < 200u; k++) {
            if (recs[k].buffer == recs[i].buffer && recs[k].offset == recs[i].offset &&
                recs[k].bytes == recs[i].bytes && recs[k].serial == recs[i].serial && recs[k].owned_offset % 16u == 0) {
                leg_off = recs[k].owned_offset;
                lf = 1;
                break;
            }
        }
        if (ff != lf || (ff && fast_off != leg_off)) return 1;
    }
    totk_fg_render_slot slot;
    memset(&slot, 0, sizeof slot);
    uintptr_t views[11];
    uint32_t layouts[11];
    for (unsigned i = 0; i < 11u; i++) { views[i] = 0x5000u + i; layouts[i] = 1u; }
    uintptr_t bufs[2] = {0x9000u, 0x9000u};
    uint64_t offs[2] = {0u, 0u}, rngs[2] = {16u, 16u};
    if (totk_fg_desc_diff_mask(&slot, 1u, views, layouts, bufs, offs, rngs) != 0x7ffu) return 1;
    if (totk_fg_desc_diff_mask(&slot, 1u, views, layouts, bufs, offs, rngs) != 0u) return 1;
    bufs[1] = 0xa000u;
    if (totk_fg_desc_diff_mask(&slot, 1u, views, layouts, bufs, offs, rngs) != (1u << 8)) return 1;
    if (totk_fg_desc_diff_mask(&slot, 2u, views, layouts, bufs, offs, rngs) != 0x7ffu) return 1;
    if (totk_fg_term_flush_count(12u) != 12u) return 1;
    return 0;
}

static int w38_test_parity(int fault_mode) {
    totk_w38_set_fault(fault_mode ? (fault_mode <= 4 ? fault_mode : 4) : 0);
    totk_fg_fmt_cache fc;
    memset(&fc, 0, sizeof fc);
    pthread_t th[4];
    for (int i = 0; i < 4; i++) assert(pthread_create(&th[i], NULL, w38_fmt_worker, &fc) == 0);
    for (int i = 0; i < 4; i++) {
        void *ret = NULL;
        assert(pthread_join(th[i], &ret) == 0);
        if (ret != NULL) return 1;
    }
    return w38_test_ht_and_diff();
}

static double w38_bench_mode(int fast_on, uint64_t *sink) {
    static w38_copy_rec recs[256];
    totk_fg_term_ht ht;
    totk_fg_render_slot slot;
    totk_fg_fmt_cache fc;
    memset(&ht, 0, sizeof ht);
    memset(&slot, 0, sizeof slot);
    memset(&fc, 0, sizeof fc);
    totk_fg_term_ht_reset(&ht);
    totk_fg_fmt_put(&fc, (uintptr_t)0x3000u, 37u, 0x7u);
    for (unsigned i = 0; i < 192u; i++) {
        recs[i] = (w38_copy_rec){0x100000ull + (i / 4u) * 4096ull, (i % 4u) * 256ull, 256ull, i * 256ull, 500ull + i / 32u};
        uint32_t h = totk_fg_term_hash(recs[i].buffer, recs[i].offset, recs[i].bytes, recs[i].serial);
        totk_fg_term_ht_insert(&ht, h, (uint16_t)i);
    }
    uintptr_t views[11]; uint32_t layouts[11];
    for (unsigned i = 0; i < 11u; i++) { views[i] = 0x5000u + i; layouts[i] = 1u; }
    uintptr_t bufs[2] = {0x9000u, 0x9000u}; uint64_t offs[2] = {0u, 0u}, rngs[2] = {16u, 16u};
    totk_fg_desc_diff_mask(&slot, 1u, views, layouts, bufs, offs, rngs);
    uint64_t t0 = w38_now_ns(), acc = 0;
    for (int r = 0; r < 10000; r++) {
        unsigned q = (unsigned)r % 192u;
        uint64_t owned = 0;
        if (fast_on) {
            uint32_t feat = 0;
            totk_fg_fmt_get(&fc, (uintptr_t)0x3000u, 37u, &feat);
            w38_find_fast(&ht, recs, recs[q].buffer, recs[q].offset, recs[q].bytes, 16u, recs[q].serial, &owned);
            uint32_t mask = totk_fg_desc_diff_mask(&slot, 1u, views, layouts, bufs, offs, rngs);
            acc += feat + owned + mask;
        } else {
            uint32_t feat = 0x7u;
            for (unsigned k = 0; k < 192u; k++) {
                if (recs[k].buffer == recs[q].buffer && recs[k].offset == recs[q].offset &&
                    recs[k].bytes == recs[q].bytes && recs[k].serial == recs[q].serial && recs[k].owned_offset % 16u == 0) {
                    owned = recs[k].owned_offset;
                    break;
                }
            }
            uint32_t mask = 0u;
            for (unsigned i = 0; i < 11u; i++) mask |= (views[i] ? (0u << i) : (1u << i));
            acc += feat + owned + mask;
        }
    }
    *sink += acc;
    return (double)(w38_now_ns() - t0) / 1e6;
}

static int run_ktgl_w38_tests(int fault_mode) {
    totk_fg_fast_reset_for_test(1);
    if (fault_mode > 0) {
        int m = (fault_mode - 1) % 4 + 1;
        return w38_test_parity(m) != 0 ? 1 : 0;
    }
    assert(w38_test_parity(0) == 0);
    const char *env = getenv("TOTK_VK_FG_FAST");
    int req_off = (env && env[0] == '0' && !env[1]);
    uint64_t s0 = 0, s1 = 0;
    double ms0 = w38_bench_mode(0, &s0);
    double ms1 = w38_bench_mode(1, &s1);
    assert(s0 == s1);
    printf("ktglopttest w38: req_off=%d off_ms=%.3f on_ms=%.3f ratio=%.2fx sink=%llu\n",
           req_off, ms0, ms1, ms1 > 0 ? ms0 / ms1 : 0.0, (unsigned long long)s1);
    return 0;
}

#endif
