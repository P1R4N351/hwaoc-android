/* Wave 43 (Optimization 45): cpend seqlock/buckets, epoch UBO dedup, radix sort, span window, and slab pool tests. */
#ifndef TEST_VK_KTGL_OPT_W43_H
#define TEST_VK_KTGL_OPT_W43_H
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vk_subcap_fast.h"

_Atomic int g_totk_subcap_fast_override = -1;
_Atomic int g_totk_w43_fault = -1;

static inline uint64_t w43_aoc_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

typedef struct { uint64_t addr, val; int known; } w43_ref_cp;

static inline int w43_test_cpend_parity(void) {
    totk_cpend_fast_t *cf = calloc(1u, sizeof *cf);
    w43_ref_cp ref[128];
    uint64_t h = 0u, t = 0u;
    int err = 0;
    uint64_t a0 = UINT64_C(0x10000010), a1 = UINT64_C(0x10000020);
    for (uint64_t i = 0u; i < 90u; i++) {
        uint64_t addr = (i & 3u) == 0u ? a0 : ((i & 3u) == 1u ? a1 : (a0 + (i & 15u) * 32u));
        uint64_t val = 100u + i;
        int kn = (i & 7u) != 0u;
        ref[h % 128u] = (w43_ref_cp){addr, val, kn}; h++;
        totk_cpend_fast_push(cf, addr, val, kn);
        if (i > 12u && (i & 1u)) { totk_cpend_fast_pop(cf, 0); t++; }
        int f0 = 0, k0 = 1; uint64_t v0 = 0u;
        if (totk_cpend_fast_lookup(cf, a0, &f0, &v0, &k0) != 0) err = 1;
        int rf = 0, rk = 1; uint64_t rv = 0u;
        for (uint64_t q = h; q > t; q--) {
            if (ref[(q - 1u) % 128u].addr == a0) { rf = 1; rv = ref[(q - 1u) % 128u].val; rk = ref[(q - 1u) % 128u].known; break; }
        }
        if (f0 != rf || (rf && (v0 != rv || k0 != rk))) err = 1;
        int pf[2], pk[2]; uint64_t pv[2];
        if (totk_cpend_fast_pair(cf, a0, a1, pf, pv, pk) != 0 || pf[0] != rf || (rf && pk[0] != rk)) err = 1;
    }
    free(cf);
    return err;
}

typedef struct { totk_cpend_fast_t *cf; _Atomic int stop; _Atomic int err; } w43_cp_ctx;

static inline void *w43_cp_reader(void *arg) {
    w43_cp_ctx *c = (w43_cp_ctx *)arg;
    while (!atomic_load_explicit(&c->stop, memory_order_acquire)) {
        int f = 0, k = 1; uint64_t v = 0u;
        if (totk_cpend_fast_lookup(c->cf, UINT64_C(0x40000100), &f, &v, &k) == 0 && f && v == 0u) {
            atomic_store_explicit(&c->err, 1, memory_order_release);
        }
    }
    return NULL;
}

static inline int w43_test_tsan_and_ubo_seen(void) {
    w43_cp_ctx ctx = {.cf = calloc(1u, sizeof(totk_cpend_fast_t))};
    pthread_t th[3];
    for (int i = 0; i < 3; i++) pthread_create(&th[i], NULL, w43_cp_reader, &ctx);
    for (uint64_t i = 1u; i <= 1200u; i++) {
        totk_cpend_fast_push(ctx.cf, UINT64_C(0x40000000) + (i & 31u) * 16u, i, 1);
        if (i > 32u) totk_cpend_fast_pop(ctx.cf, 0);
    }
    atomic_store_explicit(&ctx.stop, 1, memory_order_release);
    for (int i = 0; i < 3; i++) pthread_join(th[i], NULL);
    int err = atomic_load_explicit(&ctx.err, memory_order_acquire) != 0;
    free(ctx.cf);

    totk_ubo_seen_tls *st = calloc(1u, sizeof *st);
    totk_ubo_seen_reset(st);
    if (totk_ubo_seen_hit(st, 7u, UINT64_C(0x1000), UINT64_C(0x1100)) != 0) err = 1;
    if (totk_ubo_seen_hit(st, 7u, UINT64_C(0x1000), UINT64_C(0x1100)) != 1) err = 1;
    totk_ubo_seen_reset(st);
    if (totk_ubo_seen_hit(st, 7u, UINT64_C(0x1000), UINT64_C(0x1100)) != 0) err = 1;
    st->gen = UINT32_MAX;
    totk_ubo_seen_reset(st);
    if (st->gen != 1u || totk_ubo_seen_hit(st, 7u, UINT64_C(0x1000), UINT64_C(0x1100)) != 0) err = 1;
    free(st);
    return err;
}

typedef struct { uintptr_t lo, end; } w43_span;
typedef struct { uintptr_t lo; int best; } w43_seg;

static inline int w43_test_span_win_and_pool(void) {
    static const w43_span sp[2] = {{0x10000u, 0x20000u}, {0x30000u, 0x50000u}};
    static const w43_seg sg[4] = {{0x10000u, 0}, {0x20000u, -1}, {0x30000u, 1}, {0x50000u, -1}};
    totk_span_win_t kw = {.seg_idx = -1};
    totk_span_win_seek(&kw, sp, sizeof sp[0], __builtin_offsetof(w43_span, end),
                       sg, sizeof sg[0], __builtin_offsetof(w43_seg, lo), __builtin_offsetof(w43_seg, best), 4, 0x11000u);
    if (!totk_span_win_contains(&kw, 0x18000u)) return 1;
    if (totk_span_win_contains(&kw, 0x22000u)) return 1;
    if (totk_span_win_contains(&kw, 0x35000u)) return 1;

    totk_ucap_pool_t pool = {0};
    for (int it = 0; it < 8; it++) {
        size_t need = totk_ucap_slab_need(48u, 16, 1, 2048u), got = 0u;
        void *p = totk_ucap_pool_acquire(&pool, need, &got);
        uint64_t *lo = NULL, *hi = NULL; size_t *off = NULL; uint8_t *by = NULL;
        totk_ucap_slice(p, 48u, 16, 1, &lo, &hi, &off, &by);
        lo[0] = UINT64_C(0xabc000); hi[0] = UINT64_C(0xabc100);
        if (lo == hi || lo[0] != UINT64_C(0xabc000) || hi[0] != UINT64_C(0xabc100)) { totk_ucap_pool_drain(&pool); free(p); return 1; }
        totk_ucap_pool_release(&pool, p, got);
    }
    totk_ucap_pool_drain(&pool);
    return 0;
}

static inline uint64_t w43_bench_fast_frame(const totk_cpend_fast_t *cf, totk_ubo_seen_tls *st,
                                            totk_rng_pair *r, totk_rng_pair *tmp, totk_ucap_pool_t *pool,
                                            const w43_span *sp, const w43_seg *sg, int nseg) {
    uint64_t sink = 0u;
    totk_ubo_seen_reset(st);
    for (uint32_t i = 0u; i < 256u; i++) {
        sink += (uint64_t)totk_ubo_seen_hit(st, i & 63u, 0x100000u + (i & 63u) * 256u, 0x100100u + (i & 63u) * 256u);
        int f = 0, kn = 1; uint64_t v = 0u;
        totk_cpend_fast_lookup(cf, UINT64_C(0x20000000) + (i & 255u) * 32u, &f, &v, &kn);
        sink += v;
        r[i] = (totk_rng_pair){0x100000u + (255u - i) * 256u, 0x100100u + (255u - i) * 256u};
    }
    totk_radix_sort_rng(r, 256, tmp, 512);
    totk_span_win_t kw = {.seg_idx = -1};
    totk_span_win_seek(&kw, sp, sizeof sp[0], __builtin_offsetof(w43_span, end),
                       sg, sizeof sg[0], __builtin_offsetof(w43_seg, lo), __builtin_offsetof(w43_seg, best), nseg, r[0].lo);
    for (int i = 0; i < 256; i++) sink += (uint64_t)totk_span_win_contains(&kw, r[i].hi);
    size_t got = 0u;
    void *p = totk_ucap_pool_acquire(pool, totk_ucap_slab_need(48u, 32, 0, 0u), &got);
    sink += (p != NULL) ? 1u : 0u;
    totk_ucap_pool_release(pool, p, got);
    return sink;
}

static inline uint64_t w43_bench_slow_frame(pthread_mutex_t *mu, const w43_ref_cp *lin, int nlin,
                                            totk_rng_pair *r, const w43_span *sp, const w43_seg *sg, int nseg) {
    uint64_t sink = 0u;
    totk_rng_pair seen_leg[1024]; memset(seen_leg, 0, sizeof seen_leg);
    for (uint32_t i = 0u; i < 256u; i++) {
        uint64_t lo = 0x100000u + (i & 63u) * 256u, hi = lo + 256u;
        int hit = (seen_leg[i & 63u].lo == lo && seen_leg[i & 63u].hi == hi);
        seen_leg[i & 63u] = (totk_rng_pair){lo, hi};
        sink += (uint64_t)hit;
        uint64_t v = 0u, target = UINT64_C(0x20000000) + (i & 255u) * 32u;
        pthread_mutex_lock(mu);
        for (int q = nlin - 1; q >= 0; q--) {
            if (lin[q].addr == target) { v = lin[q].val; break; }
        }
        pthread_mutex_unlock(mu);
        sink += v;
        r[i] = (totk_rng_pair){0x100000u + (255u - i) * 256u, 0x100100u + (255u - i) * 256u};
    }
    totk_radix_sort_rng(r, 256, NULL, 0);
    for (int i = 0; i < 256; i++) {
        totk_span_win_t kw = {.seg_idx = -1};
        totk_span_win_seek(&kw, sp, sizeof sp[0], __builtin_offsetof(w43_span, end),
                           sg, sizeof sg[0], __builtin_offsetof(w43_seg, lo), __builtin_offsetof(w43_seg, best), nseg, r[i].lo);
        sink += (uint64_t)totk_span_win_contains(&kw, r[i].hi);
    }
    void *p0 = malloc(48u), *p1 = malloc(32u * 8u), *p2 = malloc(32u * 8u);
    sink += (p0 && p1 && p2) ? 1u : 0u;
    free(p2); free(p1); free(p0);
    return sink;
}

static inline uint64_t w43_bench_mode(int mode) {
    totk_vk_subcap_fast_set_for_test(mode);
    totk_cpend_fast_t *cf = calloc(1u, sizeof *cf);
    static const w43_span sp[2] = {{0x100000u, 0x400000u}, {0x500000u, 0x900000u}};
    static const w43_seg sg[4] = {{0x100000u, 0}, {0x400000u, -1}, {0x500000u, 1}, {0x900000u, -1}};
    totk_ubo_seen_tls *st = calloc(1u, sizeof *st);
    totk_ucap_pool_t pool = {0};
    totk_rng_pair *tmp = malloc(512u * sizeof *tmp), *r = malloc(512u * sizeof *r);
    w43_ref_cp *lin = malloc(512u * sizeof *lin);
    pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    uint64_t sink = 0u;
    for (uint32_t k = 0u; k < 256u; k++) {
        uint64_t ad = UINT64_C(0x20000000) + k * 32u;
        totk_cpend_fast_push(cf, ad, k + 1u, 1);
        lin[k] = (w43_ref_cp){ad, k + 1u, 1};
    }
    for (uint32_t frame = 0u; frame < 400u; frame++) {
        sink += mode ? w43_bench_fast_frame(cf, st, r, tmp, &pool, sp, sg, 4)
                     : w43_bench_slow_frame(&mu, lin, 256, r, sp, sg, 4);
    }
    pthread_mutex_destroy(&mu);
    totk_ucap_pool_drain(&pool); free(lin); free(r); free(tmp); free(st); free(cf);
    totk_vk_subcap_fast_set_for_test(-1);
    return sink;
}

static inline int run_ktgl_w43_tests(int fault_mode) {
    int prev = atomic_load_explicit(&g_totk_w43_fault, memory_order_acquire);
    if (fault_mode >= 161 && fault_mode <= 165) totk_w43_set_fault(fault_mode - 160);
    int err = 0;
    if (w43_test_cpend_parity() != 0) err = 1;
    if (w43_test_tsan_and_ubo_seen() != 0) err = 1;
    if (w43_test_span_win_and_pool() != 0) err = 1;
    uint64_t t0 = w43_aoc_ns(), s0 = w43_bench_mode(0), t1 = w43_aoc_ns();
    uint64_t t2 = w43_aoc_ns(), s1 = w43_bench_mode(1), t3 = w43_aoc_ns();
    if (s0 != s1) err = 1;
    double slow_ms = (double)(t1 - t0) / 1e6, fast_ms = (double)(t3 - t2) / 1e6;
    printf("aoc_w43_subcap_fast: slow_ms=%.3f fast_ms=%.3f speedup=%.2fx sink=%llu %s\n",
           slow_ms, fast_ms, fast_ms > 0.0 ? slow_ms / fast_ms : 0.0,
           (unsigned long long)s1, err ? "FAIL" : "PASS");
    totk_w43_set_fault(prev);
    return err;
}

#ifdef TOTK_W43_STANDALONE
int main(void) {
    const char *f = getenv("TOTK_W43_FAULT");
    return run_ktgl_w43_tests(f && *f ? (160 + atoi(f)) : 0);
}
#endif
#endif
