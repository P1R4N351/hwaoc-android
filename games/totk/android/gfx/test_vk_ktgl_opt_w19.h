#ifndef TEST_VK_KTGL_OPT_W19_H
#define TEST_VK_KTGL_OPT_W19_H
/* Wave 19 (Opt 21): Verification & fault-injection suite for totk_nvn_hle_fast.h. */
#include "totk_nvn_hle_fast.h"
#include <pthread.h>

extern uint64_t host_now_ns(void);
static volatile uint64_t g_w19_aoc_sink = 0;

static inline double w19_now_ms(void) {
    return (double)host_now_ns() * 1e-6;
}

static int test_w19_pin_span_equiv(void) {
    totk_pin_span sp, outer;
    totk_pin_span_init(&sp);
    uint32_t lo = 0, hi = 0;
    if (totk_pin_span_slice(sp.lo, sp.hi, &lo, &hi) != 0) return 1;
    totk_pin_span_note_op(&sp, 128u, 48u);
    if (totk_pin_span_slice(sp.lo, sp.hi, &lo, &hi) != 1 || lo != 128u || hi != 176u) {
        fprintf(stderr, "W19 FAIL: fault 53 or bounded pin span missed\n");
        return 1;
    }
    totk_pin_span_init(&outer);
    totk_pin_span_note_marker(&outer, 64u, sp.lo, sp.hi);
    if (totk_pin_span_slice(outer.lo, outer.hi, &lo, &hi) != 1 || lo != 184u || hi != 232u) return 1;
    totk_pin_span_note_op(&outer, (size_t)TOTK_PIN_UNBOUNDED, 16u);
    if (totk_pin_span_slice(outer.lo, outer.hi, &lo, &hi) != 2) return 1;
    return 0;
}

static int test_w19_pin_memo_equiv(void) {
    totk_pin_sync sync = {.epoch = 1u};
    totk_pin_memo4 *tls = NULL;
    uint32_t tag_a = 1u, tag_b = 2u;
    uint32_t w0 = 0; uint64_t e0 = 0;
    totk_pin_snap(&sync, &w0, &e0);
    totk_pin_sync_note_insert(&sync, 0x1001ull, 1u);
    totk_pin_fast_put(&tls, &sync, w0, e0, 0x1001ull, &tag_a, 100u);

    if (totk_pin_fast_get(&tls, &sync, 0x1001ull, 120u) != &tag_a) return 1;
    if (totk_pin_serial_may_exist(&sync, 0x1001ull, tls) != 1) return 1;
    if (totk_pin_fast_get(&tls, &sync, 0x1001ull, 164u) != NULL) {
        fprintf(stderr, "W19 FAIL: pin memo failed to force 64-submit refresh (fault 54)\n");
        free(tls);
        return 1;
    }
    totk_pin_wbegin(&sync);
    if (totk_pin_fast_get(&tls, &sync, 0x1001ull, 120u) != NULL) {
        fprintf(stderr, "W19 FAIL: pin memo hit during active sweep writer\n");
        free(tls);
        return 1;
    }
    uint64_t w[4] = {0, 0, 0, 0};
    totk_pin_sync_set_bloom(&sync, w, 0u);
    totk_pin_wend(&sync);
    if (totk_pin_fast_get(&tls, &sync, 0x1001ull, 120u) != NULL) {
        fprintf(stderr, "W19 FAIL: stale pin memo hit after sweep/rekey\n");
        free(tls);
        return 1;
    }
    if (totk_pin_serial_may_exist(&sync, 0x1001ull, tls) != 0) {
        fprintf(stderr, "W19 FAIL: serial_bloom failed after empty sweep (fault 54)\n");
        free(tls);
        return 1;
    }
    totk_pin_snap(&sync, &w0, &e0);
    totk_pin_sync_note_insert(&sync, 0x2002ull, 1u);
    totk_pin_fast_put(&tls, &sync, w0, e0, 0x2002ull, &tag_b, 200u);
    if (totk_pin_serial_may_exist(&sync, 0x2002ull, tls) != 1) { free(tls); return 1; }
    free(tls);
    return 0;
}

static int test_w19_ctl_bloom_equiv(void) {
    totk_ctl_copy_summary sum = {.min_lo = UINTPTR_MAX};
    if (totk_ctl_copy_may_overlap(&sum, 0x70001000ull, 0x70002000ull) != 0) return 1;
    totk_ctl_copy_note_add(&sum, 0x70001040ull, 0x70001041ull);
    totk_ctl_copy_note_add(&sum, 0x70001080ull, 0x70001081ull);
    if (totk_ctl_copy_may_overlap(&sum, 0x70001000ull, 0x70002000ull) != 1) {
        fprintf(stderr, "W19 FAIL: overlapping control region skipped (fault 55)\n");
        return 1;
    }
    totk_ctl_copy_note_drop(&sum, 0x70001040ull, 0x70001041ull);
    if (totk_ctl_copy_may_overlap(&sum, 0x70001000ull, 0x70002000ull) != 1) return 1;
    totk_ctl_copy_note_drop(&sum, 0x70001080ull, 0x70001081ull);
    if (totk_ctl_copy_may_overlap(&sum, 0x70001000ull, 0x70002000ull) != 0) return 1;
    return 0;
}

static void init_w19_span_bufs(uint8_t *rec_empty, uint8_t *rec_one, int ops, size_t sz) {
    memset(rec_empty, 0, sz);
    memset(rec_one, 0, sz);
    uint32_t hdr[2] = {1u, 8u};
    memcpy(rec_empty, hdr, 8);
    memcpy(rec_one, hdr, 8);
    for (int i = 0; i < ops; i++) {
        uint32_t oh_e[2] = {20u, 16u};
        uint32_t oh_o[2] = {(i == 0) ? 99u : 20u, 16u};
        memcpy(rec_empty + 8 + i * 16, oh_e, 8);
        memcpy(rec_one + 8 + i * 16, oh_o, 8);
    }
    uint32_t endh[2] = {2u, 8u};
    memcpy(rec_empty + 8 + ops * 16, endh, 8);
    memcpy(rec_one + 8 + ops * 16, endh, 8);
}

static uint64_t count_w19_span_ops(const uint8_t *buf, size_t off, size_t hi) {
    uint64_t hits = 0;
    for (; off < hi;) {
        uint32_t h[2];
        memcpy(h, buf + off, 8);
        if (h[0] == 99u) hits++;
        if (h[0] == 2u) break;
        off += h[1];
    }
    return hits;
}

static void bench_w19_pin_span(void) {
    enum { OPS = 64, NCOPIES = 10000 };
    static uint8_t rec_empty[OPS * 16u + 16u], rec_one[OPS * 16u + 16u];
    init_w19_span_bufs(rec_empty, rec_one, OPS, sizeof rec_empty);

    double t0 = w19_now_ms();
    uint64_t leg_scan = 0, fast_scan = 0;
    for (int r = 0; r < NCOPIES; r++) {
        const uint8_t *buf = (r % 100 == 0) ? rec_one : rec_empty;
        leg_scan += count_w19_span_ops(buf, 8u, sizeof rec_empty);
    }
    double ms_leg_scan = w19_now_ms() - t0;

    double t1 = w19_now_ms();
    for (int r = 0; r < NCOPIES; r++) {
        const uint8_t *buf = (r % 100 == 0) ? rec_one : rec_empty;
        uint32_t lo = 0, hi = 0;
        uint32_t pin_lo = (r % 100 == 0) ? 8u : TOTK_PIN_UNBOUNDED;
        uint32_t pin_hi = (r % 100 == 0) ? 24u : 0u;
        int sl = totk_pin_span_slice(pin_lo, pin_hi, &lo, &hi);
        if (sl == 1) fast_scan += count_w19_span_ops(buf, lo, hi);
        else if (sl == 2) fast_scan += count_w19_span_ops(buf, 8u, sizeof rec_empty);
    }
    double ms_fast_scan = w19_now_ms() - t1;
    g_w19_aoc_sink ^= leg_scan ^ fast_scan;
    printf("aoc_w19_pin_span: scans=%d leg_ms=%.3f fast_ms=%.3f speedup=%.2fx diff=%llu\n",
           NCOPIES, ms_leg_scan, ms_fast_scan,
           ms_leg_scan / (ms_fast_scan > 0.001 ? ms_fast_scan : 0.001),
           (unsigned long long)(leg_scan ^ fast_scan));
}

static void bench_w19_pin_memo(void) {
    enum { TAB_CAP = 4096, ITERS = 300000 };
    static uint64_t stab[TAB_CAP];
    static uintptr_t sptr[TAB_CAP];
    memset(stab, 0, sizeof stab);
    uint64_t serials[64];
    for (int i = 0; i < 64; i++) {
        serials[i] = 0x1000000ull + (uint64_t)i * 17ull;
        size_t j = (size_t)((serials[i] * 0x9E3779B97F4A7C15ull) & (TAB_CAP - 1u));
        while (stab[j]) j = (j + 1u) & (TAB_CAP - 1u);
        stab[j] = serials[i];
        sptr[j] = (uintptr_t)&stab[j];
    }
    pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    totk_pin_sync sync = {.epoch = 1u};
    totk_pin_memo4 *tls = NULL;

    double t2 = w19_now_ms();
    uint64_t leg_pin = 0, fast_pin = 0;
    for (int i = 0; i < ITERS; i++) {
        uint64_t sr = serials[i & 63];
        pthread_mutex_lock(&mu);
        size_t j = (size_t)((sr * 0x9E3779B97F4A7C15ull) & (TAB_CAP - 1u));
        while (stab[j] && stab[j] != sr) j = (j + 1u) & (TAB_CAP - 1u);
        leg_pin += (uint64_t)sptr[j];
        pthread_mutex_unlock(&mu);
    }
    double ms_leg_pin = w19_now_ms() - t2;

    double t3 = w19_now_ms();
    for (int i = 0; i < ITERS; i++) {
        uint64_t sr = serials[i & 63];
        const void *p = totk_pin_fast_get(&tls, &sync, sr, 100u);
        if (!p) {
            uint32_t w0 = 0; uint64_t e0 = 0;
            totk_pin_snap(&sync, &w0, &e0);
            pthread_mutex_lock(&mu);
            size_t j = (size_t)((sr * 0x9E3779B97F4A7C15ull) & (TAB_CAP - 1u));
            while (stab[j] && stab[j] != sr) j = (j + 1u) & (TAB_CAP - 1u);
            p = (const void *)sptr[j];
            pthread_mutex_unlock(&mu);
            totk_pin_fast_put(&tls, &sync, w0, e0, sr, p, 100u);
        }
        fast_pin += (uint64_t)(uintptr_t)p;
    }
    double ms_fast_pin = w19_now_ms() - t3;
    free(tls);
    g_w19_aoc_sink ^= leg_pin ^ fast_pin;
    printf("aoc_w19_pin_memo: iters=%d leg_ms=%.3f fast_ms=%.3f speedup=%.2fx diff=%llu\n",
           ITERS, ms_leg_pin, ms_fast_pin,
           ms_leg_pin / (ms_fast_pin > 0.001 ? ms_fast_pin : 0.001),
           (unsigned long long)(leg_pin ^ fast_pin));
}

static int run_ktgl_w19_tests(int fault_mode) {
    (void)totk_nvn_hle_fast_on();
    if (test_w19_pin_span_equiv() != 0) return 1;
    if (test_w19_pin_memo_equiv() != 0) return 1;
    if (test_w19_ctl_bloom_equiv() != 0) return 1;
    if (!fault_mode) { bench_w19_pin_span(); bench_w19_pin_memo(); }
    return 0;
}

#endif
