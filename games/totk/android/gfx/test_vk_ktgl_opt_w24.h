/* Wave 24 (Opt 26) unit tests, TSan stress, microbench, and faults 72..76 for TOTK_SVC_FAST. */
#pragma once
#define TOTK_SVC_FAST_TEST_HOOKS 1
#include "../../runtime/totk_svc_fast.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

uint64_t host_now_ns(void);

static int w24_test_thr_and_ipc_tables(void) {
    totk_svc_fast_reset_for_test(1);
    static totk_svc_thr_table tt;
    static totk_svc_h_table ht;
    memset(&tt, 0, sizeof tt);
    memset(&ht, 0, sizeof ht);
    totk_svc_thr_meta self_m = {0};
    totk_svc_thr_pub(&tt, &self_m, 0x1001u, 44, 0, 1ull, 0x51ull);
    totk_svc_thr_pub(&tt, NULL, 0x18002u, 36, 1, 7ull, 0x52ull);
    int32_t p = 0, c = 0; uint64_t m = 0, id = 0;
    assert(totk_svc_thr_read(&self_m, 0x1001u, &p, &c, &m, &id) && p == 44 && m == 1ull && id == 0x51ull);
    assert(totk_svc_thr_lookup(&tt, 0x18002u, &p, &c, &m, &id) && p == 36 && c == 1 && m == 7ull && id == 0x52ull);
    assert(!totk_svc_thr_lookup(&tt, 0x08002u, &p, &c, &m, &id));
    totk_svc_thr_drop(&tt, 0x18002u);
    assert(!totk_svc_thr_lookup(&tt, 0x18002u, &p, &c, &m, &id));
    int dummy_sess = 42;
    totk_svc_h_bind(&ht, 0x1005u, 4, &dummy_sess);
    assert(totk_svc_h_lookup(&ht, 0x1005u, 4) == &dummy_sess);
    assert(totk_svc_h_lookup(&ht, 0x1005u, 5) == NULL);
    assert(totk_svc_h_lookup(&ht, 0x2005u, 4) == NULL);
    totk_svc_h_unbind(&ht, 0x1005u);
    assert(totk_svc_h_lookup(&ht, 0x1005u, 4) == NULL);
    return 0;
}

typedef struct {
    totk_svc_ev_sys *sys;
    uint32_t hw, hr;
    pthread_mutex_t *mu;
    pthread_cond_t *cv;
    _Atomic int waiting;
    _Atomic int woke;
} w24_wait_ctx;

static void *w24_ev_waiter_fn(void *arg) {
    w24_wait_ctx *ctx = arg;
    pthread_mutex_lock(ctx->mu);
    atomic_fetch_add_explicit(&ctx->sys->wsync_count, 1u, memory_order_seq_cst);
    totk_svc_ev *e = &ctx->sys->pool[ctx->sys->hslots[ctx->hr & 0xFFFu].pool_idx];
    atomic_store(&ctx->waiting, 1);
    for (int s = 0; s < 50 && !totk_svc_ev_is_sig(e); s++) {
        struct timespec dl; clock_gettime(CLOCK_REALTIME, &dl);
        dl.tv_nsec += 5000000L;
        if (dl.tv_nsec >= 1000000000L) { dl.tv_sec++; dl.tv_nsec -= 1000000000L; }
        pthread_cond_timedwait(ctx->cv, ctx->mu, &dl);
    }
    atomic_store(&ctx->woke, totk_svc_ev_is_sig(e));
    atomic_fetch_sub_explicit(&ctx->sys->wsync_count, 1u, memory_order_seq_cst);
    pthread_mutex_unlock(ctx->mu);
    return NULL;
}

static int w24_test_ev_pool_and_dekker(void) {
    static totk_svc_ev_sys sys;
    memset(&sys, 0, sizeof sys);
    totk_svc_ev *e = totk_svc_ev_alloc(&sys, 2);
    assert(e != NULL);
    totk_svc_ev_h_bind(&sys, 0x1010u, TOTK_SVC_EV_W, e);
    totk_svc_ev_h_bind(&sys, 0x1011u, TOTK_SVC_EV_R, e);
    int64_t r = -1; int set = 0;
    assert(totk_svc_ev_signal_try(&sys, 0x1010u, &set, &r) == 1 && r == 0 && totk_svc_ev_is_sig(e));
    assert(totk_svc_ev_clear(&sys, 0x1011u, &r) == 1 && r == 0 && !totk_svc_ev_is_sig(e));
    assert(totk_svc_ev_reset(&sys, 0x1011u, 0xFA01u, &r) == 1 && (uint32_t)r == 0xFA01u);
    assert(totk_svc_ev_signal_try(&sys, 0x1010u, &set, &r) == 1 && r == 0);
    assert(totk_svc_ev_reset(&sys, 0x1011u, 0xFA01u, &r) == 1 && r == 0 && !totk_svc_ev_is_sig(e));
    pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
    w24_wait_ctx ctx = {&sys, 0x1010u, 0x1011u, &mu, &cv, 0, 0};
    pthread_t th; assert(!pthread_create(&th, NULL, w24_ev_waiter_fn, &ctx));
    for (int s = 0; s < 5000 && !atomic_load(&ctx.waiting); s++) {
        struct timespec ts = {0, 100000L};
        nanosleep(&ts, NULL);
    }
    assert(atomic_load(&ctx.waiting) == 1);
    assert(totk_svc_ev_signal_try(&sys, 0x1010u, &set, &r) == 0 && set == 1);
    pthread_mutex_lock(&mu); pthread_cond_broadcast(&cv); pthread_mutex_unlock(&mu);
    pthread_join(th, NULL);
    assert(atomic_load(&ctx.woke) == 1);
    totk_svc_ev_h_unbind(&sys, 0x1010u); totk_svc_ev_h_unbind(&sys, 0x1011u);
    totk_svc_ev_free(&sys, e);
    assert(totk_svc_ev_clear(&sys, 0x1011u, &r) == 0);
    return 0;
}

static int w24_test_alias_and_qmem_parity(void) {
    static _Alignas(8) unsigned char pages[257];
    uint32_t rng = 0x9e3779b9u;
    for (int step = 0; step < 250; step++) {
        rng = rng * 1664525u + 1013904223u;
        unsigned lo = rng % 257u;
        rng = rng * 1664525u + 1013904223u;
        unsigned len = 1u + (rng % (257u - lo));
        unsigned char val = (rng >> 24) & 1u;
        memset(pages + lo, val, len);
        unsigned q = (step * 37u) % 257u;
        unsigned r_lo = q, r_hi = q + 1u;
        unsigned char al = pages[q];
        while (r_lo > 0 && pages[r_lo - 1u] == al) r_lo--;
        while (r_hi < sizeof pages && pages[r_hi] == al) r_hi++;
        unsigned f_lo = 0, f_hi = 0;
        totk_alias_scan_run(pages, sizeof pages, q, &f_lo, &f_hi);
        assert(f_lo == r_lo && f_hi == r_hi);
    }
    totk_qmem_tls tls = {0};
    totk_svc_qmem_info in = {0x7000000000ULL, 0x100000ULL, 5, 0, 3, 0, 0, 0}, out = {0};
    totk_qmem_store(&tls, 2u, &in);
    assert(totk_qmem_lookup(&tls, 0x7000050000ULL, 2u, &out) == 1 && out.size == 0x100000ULL);
    _Atomic uint32_t mseq = 2;
    totk_svc_seq_begin(&mseq);
    assert(totk_qmem_lookup(&tls, 0x7000050000ULL, atomic_load(&mseq), &out) == 0);
    totk_svc_seq_end(&mseq);
    assert(totk_qmem_lookup(&tls, 0x7000050000ULL, atomic_load(&mseq), &out) == 0);
    return 0;
}

typedef struct { totk_svc_thr_table *tt; totk_svc_ev_sys *es; _Atomic int stop; } w24_stress_ctx;
static void *w24_stress_writer(void *arg) {
    w24_stress_ctx *c = arg;
    for (int i = 0; i < 600 && !atomic_load(&c->stop); i++) {
        totk_svc_thr_pub(c->tt, NULL, 0x1001u, 30 + (i & 15), i & 3, 7ull, 0x51ull);
        int set = 0; int64_t r = 0;
        totk_svc_ev_signal_try(c->es, 0x1002u, &set, &r);
        totk_svc_ev_clear(c->es, 0x1003u, &r);
    }
    return NULL;
}

static int w24_run_fault_or_all(int f) {
    if (f == 72) {
        totk_svc_thr_meta m = {0}; totk_svc_thr_write(&m, 0x1001u, 44, 0, 1, 0x51);
        int32_t p = 0; totk_svc_thr_read(&m, 0x1001u, &p, NULL, NULL, NULL);
        return p == 44 ? 0 : 1;
    } else if (f == 73) {
        static totk_svc_h_table ht; memset(&ht, 0, sizeof ht); int x = 7;
        totk_svc_h_bind(&ht, 0x1005u, 4, &x);
        return totk_svc_h_lookup(&ht, 0x2005u, 4) == NULL ? 0 : 1;
    } else if (f == 74) {
        static totk_svc_ev_sys s; memset(&s, 0, sizeof s);
        totk_svc_ev *e = totk_svc_ev_alloc(&s, 2);
        totk_svc_ev_h_bind(&s, 0x1002u, TOTK_SVC_EV_W, e);
        totk_svc_ev_h_bind(&s, 0x1003u, TOTK_SVC_EV_R, e);
        int set = 0; int64_t r = 0;
        totk_svc_ev_signal_try(&s, 0x1002u, &set, &r);
        totk_svc_ev_clear(&s, 0x1003u, &r);
        return !totk_svc_ev_is_sig(e) ? 0 : 1;
    } else if (f == 75) {
        static totk_svc_ev_sys s; memset(&s, 0, sizeof s);
        totk_svc_ev *e = totk_svc_ev_alloc(&s, 2);
        totk_svc_ev_h_bind(&s, 0x1002u, TOTK_SVC_EV_W, e);
        atomic_store(&s.wsync_count, 1u);
        int set = 0; int64_t r = 0;
        return (totk_svc_ev_signal_try(&s, 0x1002u, &set, &r) == 0 && set == 1) ? 0 : 1;
    } else if (f == 76) {
        unsigned char pg[64] = {0}; unsigned lo = 0, hi = 0;
        totk_alias_scan_run(pg, sizeof pg, 4, &lo, &hi);
        return hi == 64 ? 0 : 1;
    }
    return 0;
}

static int run_ktgl_w24_tests(int fault_mode) {
    totk_svc_fast_reset_for_test(1);
    if (fault_mode >= 72 && fault_mode <= 76) {
        int rc = w24_run_fault_or_all(fault_mode);
        printf("aoc_w24_fault_%d caught=%d\n", fault_mode, rc != 0 ? 1 : 0);
        return rc;
    }
    w24_test_thr_and_ipc_tables();
    w24_test_ev_pool_and_dekker();
    w24_test_alias_and_qmem_parity();
    static totk_svc_thr_table tt; static totk_svc_h_table ht; static totk_svc_ev_sys es;
    memset(&tt, 0, sizeof tt); memset(&ht, 0, sizeof ht); memset(&es, 0, sizeof es);
    totk_svc_ev *ev = totk_svc_ev_alloc(&es, 2);
    totk_svc_ev_h_bind(&es, 0x1002u, TOTK_SVC_EV_W, ev);
    totk_svc_ev_h_bind(&es, 0x1003u, TOTK_SVC_EV_R, ev);
    totk_svc_thr_pub(&tt, NULL, 0x1001u, 44, 0, 1ull, 0x51ull);
    int sess = 1; totk_svc_h_bind(&ht, 0x1005u, 4, &sess);
    w24_stress_ctx ctx = {&tt, &es, 0};
    pthread_t th; assert(!pthread_create(&th, NULL, w24_stress_writer, &ctx));
    for (int i = 0; i < 600; i++) {
        int32_t p = 0; (void)totk_svc_thr_lookup(&tt, 0x1001u, &p, NULL, NULL, NULL);
        int64_t r = 0; (void)totk_svc_ev_reset(&es, 0x1003u, 0xFA01u, &r);
        (void)totk_svc_h_lookup(&ht, 0x1005u, 4);
    }
    pthread_join(th, NULL);
    static _Alignas(8) unsigned char big[4096];
    for (int mode = 0; mode <= 1; mode++) {
        totk_svc_fast_reset_for_test(mode);
        pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
        uint64_t sink = 0, t0 = host_now_ns();
        for (int i = 0; i < 30000; i++) {
            if (!totk_svc_fast_enabled()) pthread_mutex_lock(&mu);
            int32_t p = 0; totk_svc_thr_lookup(&tt, 0x1001u, &p, NULL, NULL, NULL);
            void *sp = totk_svc_h_lookup(&ht, 0x1005u, 4);
            unsigned lo = 0, hi = 0;
            if (totk_svc_fast_enabled()) totk_alias_scan_run(big, sizeof big, 100u, &lo, &hi);
            else { lo = 100u; hi = 101u; while (lo > 0 && !big[lo - 1u]) lo--; while (hi < sizeof big && !big[hi]) hi++; }
            if (!totk_svc_fast_enabled()) pthread_mutex_unlock(&mu);
            sink += (uint64_t)p + (uintptr_t)sp + hi - lo;
        }
        printf("aoc_w24_%s svc_ipc_alias_poll=%.3f ms sink=%llu\n",
            mode ? "on" : "off", (double)(host_now_ns() - t0) / 1e6, (unsigned long long)sink);
    }
    totk_svc_ev_h_unbind(&es, 0x1002u); totk_svc_ev_h_unbind(&es, 0x1003u);
    totk_svc_ev_free(&es, ev);
    return 0;
}
