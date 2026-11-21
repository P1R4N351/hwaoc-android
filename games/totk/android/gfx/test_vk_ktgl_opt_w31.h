#ifndef TEST_VK_KTGL_OPT_W31_H
#define TEST_VK_KTGL_OPT_W31_H
#include "totk_movie_fast.h"
#include "../../runtime/movie_nv12.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint64_t host_now_ns(void);

static void w31_verify_slots_and_table(int fault_mode) {
    totk_movie_table tbl = {0};
    assert(!totk_movie_is_ready(&tbl));
    totk_movie_mark_ready(&tbl);
    assert(totk_movie_is_ready(&tbl));

    int fake_guest = 1, fake_movie = 2;
    if (fault_mode == 107) {
        totk_movie_slot_set(&tbl, 0, &fake_guest, &fake_movie);
        assert(totk_movie_has_live(&tbl) == 1);
        totk_movie_slot_clear(&tbl, 0);
    }
    assert(!totk_movie_has_live(&tbl));

    void *found = (void *)(uintptr_t)1u;
    assert(totk_movie_find_fast(&tbl, &fake_guest, &found) == 1 && found == NULL);
    totk_movie_slot_set(&tbl, 3, &fake_guest, &fake_movie);
    assert(totk_movie_has_live(&tbl) == 1);
    assert(totk_movie_find_fast(&tbl, &fake_guest, &found) == 1 && found == &fake_movie);
    totk_movie_slot_clear(&tbl, 3);
    assert(!totk_movie_has_live(&tbl));
    assert(totk_movie_find_fast(&tbl, &fake_guest, &found) == 1 && found == NULL);

    for (uint32_t mask = 0u; mask <= 0xffu; mask++) {
        int exp_free = 0, exp_slot = -1;
        for (int i = 0; i < TOTK_MOVIE_OUT_BUFS; i++) {
            if (!((mask >> i) & 1u)) {
                if (exp_slot < 0) exp_slot = i;
                exp_free++;
            }
        }
        assert(totk_movie_free_count(mask) == exp_free);
        assert(totk_movie_alloc_slot(mask) == exp_slot);
    }
}

static void w31_verify_uv_case(int w2, int fault_mode) {
    assert(w2 > 0 && w2 <= 128);
    uint8_t u[128], v[128], ref[264], got[264];
    for (int i = 0; i < w2; i++) {
        u[i] = (uint8_t)((i * 9 + 5) & 0xff);
        v[i] = (uint8_t)((i * 15 + 3) & 0xff);
    }
    memset(ref, 0xaa, sizeof ref);
    memset(got, 0x55, sizeof got);
    for (int i = 0; i < w2; i++) { ref[2 * i] = u[i]; ref[2 * i + 1] = v[i]; }
    totk_nv12_interleave_row_ex(u, v, got, w2, 1, fault_mode == 111);
    assert(memcmp(ref, got, (size_t)w2 * 2u) == 0);
}

static void w31_verify_uv_swar(int fault_mode) {
    w31_verify_uv_case(97, fault_mode);
    w31_verify_uv_case(32, fault_mode);
}

static _Atomic int g_w31_stop;
static totk_movie_table g_w31_par_tbl;
static int g_w31_guests[TOTK_MOVIE_MAX];
static int g_w31_movies[TOTK_MOVIE_MAX];

static void *w31_reader_thread(void *arg) {
    (void)arg;
    for (uint32_t spin = 0u; spin < 10000000u && !atomic_load_explicit(&g_w31_stop, memory_order_acquire); spin++) {
        for (int i = 0; i < TOTK_MOVIE_MAX; i++) {
            void *mv = NULL;
            if (totk_movie_find_fast(&g_w31_par_tbl, &g_w31_guests[i], &mv) && mv != NULL) {
                assert(mv == &g_w31_movies[i]);
            }
        }
    }
    return NULL;
}

static void w31_verify_seqlock_threads(void) {
    memset(&g_w31_par_tbl, 0, sizeof g_w31_par_tbl);
    totk_movie_mark_ready(&g_w31_par_tbl);
    atomic_store_explicit(&g_w31_stop, 0, memory_order_release);
    pthread_t th[2];
    for (int t = 0; t < 2; t++) assert(pthread_create(&th[t], NULL, w31_reader_thread, NULL) == 0);
    for (uint32_t it = 0u; it < 400u; it++) {
        int slot = (int)(it % TOTK_MOVIE_MAX);
        totk_movie_slot_set(&g_w31_par_tbl, slot, &g_w31_guests[slot], &g_w31_movies[slot]);
        assert(totk_movie_has_live(&g_w31_par_tbl));
        if (it >= TOTK_MOVIE_MAX) totk_movie_slot_clear(&g_w31_par_tbl, slot);
    }
    atomic_store_explicit(&g_w31_stop, 1, memory_order_release);
    for (int t = 0; t < 2; t++) assert(pthread_join(th[t], NULL) == 0);
}

__attribute__((noinline))
static uint64_t w31_bench_off_iter(pthread_mutex_t *mu, const void **slots, int *movies,
                                   const void *key, uint32_t hmask, uint32_t qmask, int idle,
                                   uint8_t *u, const uint8_t *v, uint8_t *o, uint32_t it, int w2) {
    int live = 0, queued = 0;
    void *mv = NULL;
    pthread_mutex_lock(mu);
    for (int s = 0; s < TOTK_MOVIE_MAX; s++) if (slots[s]) live = 1;
    pthread_mutex_unlock(mu);
    if (!idle) {
        for (int call = 0; call < 2; call++) {
            pthread_mutex_lock(mu);
            for (int s = 0; s < TOTK_MOVIE_MAX; s++) if (slots[s] == key) mv = &movies[s];
            pthread_mutex_unlock(mu);
        }
        int fc = 0, fs = -1;
        for (int i = 0; i < TOTK_MOVIE_OUT_BUFS; i++) if (!((hmask >> i) & 1u)) { if (fs < 0) fs = i; fc++; }
        for (int i = 0; i < TOTK_MOVIE_OUT_BUFS; i++) if (i == 3 && ((qmask >> i) & 1u)) queued |= 1;
        live += fc + fs + queued;
        if (it < 3200u) totk_nv12_interleave_row_ex(u, v, o, w2, 0, 0);
    }
    return (uint64_t)live + (uint64_t)(mv != NULL) + (uint64_t)o[(it * 2u) % (uint32_t)(w2 * 2)];
}

__attribute__((noinline))
static uint64_t w31_bench_on_iter(const totk_movie_table *tbl, const void *key, uint32_t hmask,
                                  uint32_t qmask, int idle, uint8_t *u, const uint8_t *v,
                                  uint8_t *o, uint32_t it, int w2) {
    int live = totk_movie_has_live(tbl);
    void *mv = NULL;
    if (!idle) {
        assert(totk_movie_find_fast(tbl, key, &mv) == 1 && mv != NULL);
        int queued = (int)((qmask >> 3) & 1u);
        live += totk_movie_free_count(hmask) + totk_movie_alloc_slot(hmask) + queued;
        if (it < 3200u) totk_nv12_interleave_row_ex(u, v, o, w2, 1, 0);
    }
    return (uint64_t)live + (uint64_t)(mv != NULL) + (uint64_t)o[(it * 2u) % (uint32_t)(w2 * 2)];
}

static void w31_run_bench(void) {
    enum { W2 = 960 };
    static totk_movie_table tbl_idle, tbl_act;
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    static int guests[TOTK_MOVIE_MAX], movies[TOTK_MOVIE_MAX];
    static const void *slots_idle[TOTK_MOVIE_MAX], *slots_act[TOTK_MOVIE_MAX];
    static uint8_t u[W2], v[W2], o[W2 * 2];
    memset(&tbl_idle, 0, sizeof tbl_idle); memset(&tbl_act, 0, sizeof tbl_act);
    totk_movie_mark_ready(&tbl_idle); totk_movie_mark_ready(&tbl_act);
    for (int i = 0; i < TOTK_MOVIE_MAX; i++) {
        slots_idle[i] = NULL;
        slots_act[i] = i < 2 ? &guests[i] : NULL;
        if (i < 2) totk_movie_slot_set(&tbl_act, i, &guests[i], &movies[i]);
    }
    for (int i = 0; i < W2; i++) v[i] = (uint8_t)(i ^ 0x55);
    uint64_t ref_sink = 0u;
    for (int fast = 0; fast < 2; fast++) {
        for (int i = 0; i < W2; i++) u[i] = (uint8_t)i;
        memset(o, 0, sizeof o);
        totk_movie_fast_reset_for_test(fast, 0);
        volatile uint64_t sink = 0u;
        uint64_t t0 = host_now_ns();
        for (uint32_t it = 0u; it < 40000u; it++) {
            int idle = (it & 7u) != 0u;
            uint32_t hmask = (it & 16u) ? 0x0du : 0x55u, qmask = (it & 8u) ? 0x08u : 0x04u;
            u[it % W2] = (uint8_t)it;
            sink += fast ? w31_bench_on_iter(idle ? &tbl_idle : &tbl_act, &guests[1], hmask, qmask, idle, u, v, o, it, W2)
                         : w31_bench_off_iter(&mu, idle ? slots_idle : slots_act, movies, &guests[1], hmask, qmask, idle, u, v, o, it, W2);
        }
        double ms = (double)(host_now_ns() - t0) / 1e6;
        uint64_t got = sink;
        if (fast == 0) ref_sink = got;
        else assert(got == ref_sink);
        printf("aoc_movie_w31(%s): elapsed=%.3f ms sink=%llu\n", fast ? "on" : "off", ms, (unsigned long long)got);
    }
}

static int run_ktgl_w31_tests(int fault_mode) {
    totk_movie_fast_reset_for_test(1, fault_mode);
    w31_verify_slots_and_table(fault_mode);
    w31_verify_uv_swar(fault_mode);
    if (!fault_mode) w31_verify_seqlock_threads();
    if (fault_mode < 107 || fault_mode > 111) w31_run_bench();
    return 0;
}

#endif
