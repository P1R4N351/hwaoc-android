#ifndef TEST_VK_KTGL_OPT_W45_H
#define TEST_VK_KTGL_OPT_W45_H
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "vk_compute_fast.h"

_Atomic int g_totk_vk_compute_fast_override = -1;
_Atomic int g_totk_w45_fault = -1;
static int g_w45_fail = 0;

#define W45_EXPECT_EQ(m, w, g) do { \
    uint64_t wa_ = (uint64_t)(w), ga_ = (uint64_t)(g); \
    if (wa_ != ga_) { \
        g_w45_fail++; \
        fprintf(stderr, "FAIL %s:%d %s want=%llu got=%llu\n", __FILE__, __LINE__, m, (unsigned long long)wa_, (unsigned long long)ga_); \
    } \
} while (0)

static uint64_t w45_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static void w45_make_key(uint32_t id, char out[80]) {
    memset(out, 0, 80u);
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 64; i++) out[i] = hex[((id >> ((i & 7) * 4)) + (uint32_t)i) & 15u];
    out[64] = '\0';
}

static void w45_ref_learn(char ref[TOTK_CP_HIST_CAP][80], uint32_t *cnt, const char k[80]) {
    uint32_t i;
    for (i = 0; i < *cnt; i++) if (memcmp(ref[i], k, 80u) == 0) return;
    if (*cnt == TOTK_CP_HIST_CAP) {
        for (uint32_t j = 0u; j + 1u < TOTK_CP_HIST_CAP; j++) memcpy(ref[j], ref[j + 1u], 80u);
        i--;
    } else {
        (*cnt)++;
    }
    memcpy(ref[i], k, 80u);
}

static void w45_test_hist_and_queue(void) {
    static char keys_ring[TOTK_CP_HIST_CAP][80], ref[TOTK_CP_HIST_CAP][80], snap[TOTK_CP_HIST_CAP][80];
    totk_cp_hist_t h;
    totk_cp_hist_reset(&h, keys_ring);
    uint32_t ref_cnt = 0;
    for (uint32_t id = 0; id < 5000u; id++) {
        char k[80];
        w45_make_key(id % 4500u, k);
        w45_ref_learn(ref, &ref_cnt, k);
        (void)totk_cp_hist_learn(&h, k);
    }
    W45_EXPECT_EQ("hist count cap", ref_cnt, h.count);
    totk_cp_hist_snapshot(&h, snap);
    W45_EXPECT_EQ("hist fifo parity", 0, memcmp(ref, snap, (size_t)ref_cnt * 80u));
    totk_cp_queue_t q = {0};
    totk_cp_q_node_t nd[4];
    memset(nd, 0, sizeof nd);
    for (int i = 0; i < 4; i++) totk_cp_q_push(&q, &nd[i]);
    totk_cp_q_promote(&q, &nd[2]);
    W45_EXPECT_EQ("promote head", (uintptr_t)&nd[2], (uintptr_t)totk_cp_q_pop(&q));
    W45_EXPECT_EQ("promote second", (uintptr_t)&nd[0], (uintptr_t)totk_cp_q_pop(&q));
    W45_EXPECT_EQ("promote third", (uintptr_t)&nd[1], (uintptr_t)totk_cp_q_pop(&q));
    W45_EXPECT_EQ("promote tail", (uintptr_t)&nd[3], (uintptr_t)totk_cp_q_pop(&q));
}

typedef struct { pthread_mutex_t mu; pthread_cond_t cv; _Atomic uint32_t waiters; int ready; } w45_wait_t;
static void *w45_waiter_fn(void *arg) {
    w45_wait_t *w = (w45_wait_t *)arg;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t ns = (uint64_t)ts.tv_nsec + UINT64_C(250000000);
    ts.tv_sec += (time_t)(ns / UINT64_C(1000000000));
    ts.tv_nsec = (long)(ns % UINT64_C(1000000000));
    pthread_mutex_lock(&w->mu);
    atomic_fetch_add_explicit(&w->waiters, 1u, memory_order_relaxed);
    int to = 0;
    while (!w->ready) { if (pthread_cond_timedwait(&w->cv, &w->mu, &ts) != 0) { to = 1; break; } }
    atomic_fetch_sub_explicit(&w->waiters, 1u, memory_order_relaxed);
    int ok = (!to && w->ready);
    pthread_mutex_unlock(&w->mu);
    return (void *)(uintptr_t)ok;
}

static totk_xlate_bad_fast_t g_w45_bad;
static pthread_mutex_t g_w45_bad_mu = PTHREAD_MUTEX_INITIALIZER;
static _Atomic int g_w45_stop;

static void *w45_xlate_reader(void *arg) {
    (void)arg;
    for (int i = 0; i < 3000 && !atomic_load_explicit(&g_w45_stop, memory_order_relaxed); i++) {
        const char *v = (i & 1) ? "deadbeef-bad-001" : "good-variant-999";
        int r = totk_xlate_bad_check(&g_w45_bad, v);
        if (r < -1 || r > 1) return (void *)(uintptr_t)1;
    }
    return NULL;
}

static void w45_test_wake_memo_xlate(void) {
    w45_wait_t w = {.mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER};
    atomic_init(&w.waiters, 0u);
    pthread_t th;
    pthread_create(&th, NULL, w45_waiter_fn, &w);
    for (int i = 0; i < 2000 && atomic_load_explicit(&w.waiters, memory_order_relaxed) == 0u; i++) {
        struct timespec req = {0, 100000};
        nanosleep(&req, NULL);
    }
    pthread_mutex_lock(&w.mu); w.ready = 1; totk_cp_wake_built(&w.cv, &w.waiters, 1); pthread_mutex_unlock(&w.mu);
    void *ret = NULL; pthread_join(th, &ret);
    W45_EXPECT_EQ("waiter woke", 1, (uint64_t)(uintptr_t)ret);
    static totk_cp_prep_memo_t pm; totk_cp_prep_memo_reset(&pm);
    char k0[80], k1[80]; w45_make_key(11u, k0); w45_make_key(22u, k1);
    W45_EXPECT_EQ("prep miss", 0, (uint64_t)totk_cp_prep_memo_seen(&pm, k0));
    totk_cp_prep_memo_put_locked(&pm, k0);
    W45_EXPECT_EQ("prep hit", 1, (uint64_t)totk_cp_prep_memo_seen(&pm, k0));
    W45_EXPECT_EQ("prep miss 2", 0, (uint64_t)totk_cp_prep_memo_seen(&pm, k1));
    memset(&g_w45_bad, 0, sizeof g_w45_bad);
    W45_EXPECT_EQ("xlate empty", 0, (uint64_t)totk_xlate_bad_check(&g_w45_bad, "deadbeef-bad-001"));
    totk_xlate_bad_add_locked(&g_w45_bad, "deadbeef-bad-001");
    W45_EXPECT_EQ("xlate hit", 1, (uint64_t)totk_xlate_bad_check(&g_w45_bad, "deadbeef-bad-001"));
    W45_EXPECT_EQ("xlate miss", 0, (uint64_t)totk_xlate_bad_check(&g_w45_bad, "good-variant-999"));
    atomic_store_explicit(&g_w45_stop, 0, memory_order_release);
    pthread_t xt[3];
    for (int i = 0; i < 3; i++) pthread_create(&xt[i], NULL, w45_xlate_reader, NULL);
    for (int i = 0; i < 120; i++) {
        char buf[80]; snprintf(buf, sizeof buf, "deadbeef-bad-%03d", i);
        pthread_mutex_lock(&g_w45_bad_mu); totk_xlate_bad_add_locked(&g_w45_bad, buf); pthread_mutex_unlock(&g_w45_bad_mu);
    }
    atomic_store_explicit(&g_w45_stop, 1, memory_order_release);
    for (int i = 0; i < 3; i++) { void *xr = NULL; pthread_join(xt[i], &xr); W45_EXPECT_EQ("xlate tsan", 0, (uint64_t)(uintptr_t)xr); }
}

static uint64_t w45_bench_arm(int fast, double *ms_out) {
    static char ring[TOTK_CP_HIST_CAP][80], sample[512][80];
    static char slow_buf[TOTK_CP_HIST_CAP][80];
    totk_cp_hist_t h; totk_cp_hist_reset(&h, ring); uint32_t sc = 0;
    static totk_xlate_bad_fast_t bt; memset(&bt, 0, sizeof bt);
    char bad_arr[64][80]; uint32_t nb = 64u;
    for (uint32_t i = 0; i < 512u; i++) { w45_make_key(i, sample[i]); w45_ref_learn(slow_buf, &sc, sample[i]); (void)totk_cp_hist_learn(&h, sample[i]); }
    for (uint32_t i = 0; i < 64u; i++) { snprintf(bad_arr[i], 80u, "bad-variant-%03u", i); totk_xlate_bad_add_locked(&bt, bad_arr[i]); }
    uint64_t t0 = w45_now_ns(), acc = 0;
    for (uint32_t it = 0; it < 20000u; it++) {
        const char *k = sample[it & 511u];
        const char *v = (it & 1u) ? "bad-variant-037" : "ok-variant-xyz";
        if (fast) {
            acc += (uint64_t)(totk_cp_hist_find(&h, k, totk_cp_key_hash80(k)) >= 0);
            acc += (uint64_t)totk_xlate_bad_check(&bt, v);
        } else {
            uint32_t f = 0;
            for (uint32_t j = 0; j < sc; j++) if (memcmp(slow_buf[j], k, 80u) == 0) { f = 1; break; }
            for (uint32_t j = 0; j < nb; j++) if (strcmp(bad_arr[j], v) == 0) { f++; break; }
            acc += f;
        }
    }
    *ms_out = (double)(w45_now_ns() - t0) / 1e6;
    return acc;
}

static int run_ktgl_w45_tests(int fault_mode) {
    g_w45_fail = 0;
    int f = fault_mode;
    const char *envf = getenv("TOTK_W45_FAULT");
    if (envf && *envf) f = atoi(envf); else if (f >= 171 && f <= 175) f = f - 170; else f = 0;
    totk_w45_set_fault(f);
    totk_vk_compute_fast_set_for_test(1);
    w45_test_hist_and_queue();
    w45_test_wake_memo_xlate();
    double bms = 0.0, fms = 0.0;
    uint64_t s0 = w45_bench_arm(0, &bms), s1 = w45_bench_arm(1, &fms);
    W45_EXPECT_EQ("bench parity", s0, s1);
    totk_vk_compute_fast_set_for_test(-1);
    totk_w45_set_fault(-1);
    printf("aoc_w45_compute_fast: base_ms=%.3f fast_ms=%.3f speedup=%.2fx sink=%llu %s\n",
           bms, fms, bms / (fms > 0.0 ? fms : 1e-6), (unsigned long long)s1, g_w45_fail ? "FAIL" : "PASS");
    return g_w45_fail != 0;
}
#if defined(TOTK_W45_STANDALONE)
int main(void) { return run_ktgl_w45_tests(0); }
#endif
#endif
