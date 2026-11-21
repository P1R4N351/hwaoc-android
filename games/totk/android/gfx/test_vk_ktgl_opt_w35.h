#ifndef TEST_VK_KTGL_OPT_W35_H
#define TEST_VK_KTGL_OPT_W35_H
#include "vk_rec_fast.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { W35_REC_CLASSES = 24, W35_REC_MIN = 256, W35_DYN_SZ = 160 };

static unsigned w35_legacy_storage_class(size_t bytes) {
    size_t cap = W35_REC_MIN; unsigned idx = 0;
    while (cap < bytes && idx + 1u < W35_REC_CLASSES) { cap *= 2u; idx++; }
    return (cap < bytes) ? W35_REC_CLASSES : idx;
}

static int w35_legacy_pool_slot(int dtype) {
    static const int types[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    for (int t = 0; t < 8; t++) if (types[t] == dtype) return t;
    return -1;
}

static void w35_check_storage_and_pool(int fault_mode) {
    static const size_t sizes[10] = {1u, 64u, 256u, 257u, 512u, 513u, 1024u, 65536u, 1048577u, (size_t)256u << 23};
    for (size_t i = 0; i < 10u; i++) {
        assert(totk_rec_storage_class_fast(sizes[i], W35_REC_CLASSES, fault_mode) == w35_legacy_storage_class(sizes[i]));
    }
    assert(totk_rec_storage_class_fast(((size_t)256u << 23) + 1u, W35_REC_CLASSES, fault_mode) == W35_REC_CLASSES);
    for (int t = 0; t < 10; t++) {
        assert(totk_desc_pool_type_slot(t, fault_mode) == w35_legacy_pool_slot(t));
    }
}

static void w35_check_memo_dyn_vmask(int fault_mode) {
    totk_desc_stock_memo8 m = {0};
    uintptr_t k1 = (uintptr_t)0x1000u, k2 = k1 + 64u;
    unsigned s1 = totk_desc_memo_slot(k1);
    for (unsigned step = 1u; step < 64u; step++) {
        uintptr_t cand = k1 + (uintptr_t)step * 64u;
        if (totk_desc_memo_slot(cand) == s1) { k2 = cand; break; }
    }
    assert(totk_desc_memo_slot(k1) == totk_desc_memo_slot(k2) && k1 != k2);
    void *v1 = (void *)(uintptr_t)0xdead01u;
    totk_desc_stock_memo_put(&m, (const void *)k1, v1);
    assert(totk_desc_stock_memo_get(&m, (const void *)k1, fault_mode) == v1);
    assert(totk_desc_stock_memo_get(&m, (const void *)k2, fault_mode) == NULL);

    uint8_t d0[W35_DYN_SZ], d1[W35_DYN_SZ];
    memset(d0, 0x11, sizeof d0); memset(d1, 0x11, sizeof d1);
    assert(totk_rec_dyn_unchanged(d0, d1, sizeof d0, 0, fault_mode) == 1);
    assert(totk_rec_dyn_unchanged(d0, d1, sizeof d0, 1, fault_mode) == 0);
    d1[W35_DYN_SZ - 4u] = 0x22u;
    assert(totk_rec_dyn_unchanged(d0, d1, sizeof d0, 0, fault_mode) == 0);

    assert(totk_rec_vmask(UINT32_C(0x10005), fault_mode) == UINT32_C(0x10005));
    assert(totk_rec_need_guard_zero(0u, 1) == 0 && totk_rec_need_guard_zero(3u, 1) == 1);
    assert(totk_rec_need_guard_zero(0u, 0) == 1);
}

static uint64_t w35_aoc_bench_once(const size_t *szs, const uint8_t *dyn_a, const uint8_t *dyn_b, int fast) {
    uint64_t sink = 0;
    totk_desc_stock_memo8 memo = {0};
    void *lays[8] = {0};
    for (unsigned target = 0; target < 8u; target++) {
        for (unsigned c = 0; c < 256u; c++) {
            uintptr_t cand = (uintptr_t)(0x800000u + c * 64u);
            if (totk_desc_memo_slot(cand) == target) { lays[target] = (void *)cand; break; }
        }
        assert(lays[target] != NULL);
        totk_desc_stock_memo_put(&memo, lays[target], (void *)(uintptr_t)(target + 101u));
    }
    nvn_hmap hm = {.klen = sizeof(void *)};
    for (int i = 0; i < 8; i++) {
        struct { void *p, *z; } kp = {lays[i], NULL};
        nvn_hm_put(&hm, &kp, (void *)(uintptr_t)(i + 101));
    }

    for (uint32_t r = 0; r < 6000u; r++) {
        size_t b = szs[r & 15u];
        unsigned sc = fast ? totk_rec_storage_class_fast(b, W35_REC_CLASSES, 0) : w35_legacy_storage_class(b);
        int sl = fast ? totk_desc_pool_type_slot((int)(r & 7u), 0) : w35_legacy_pool_slot((int)(r & 7u));
        struct { void *p, *z; } lk = {lays[r & 7u], NULL};
        void *sp = fast ? totk_desc_stock_memo_get(&memo, lk.p, 0) : NULL;
        if (!sp) sp = nvn_hm_get(&hm, &lk);
        uintptr_t sv = (uintptr_t)sp;
        const uint8_t *cmp = ((r & 31u) == 0u) ? dyn_b : dyn_a;
        int eq = fast ? totk_rec_dyn_unchanged(dyn_a, cmp, W35_DYN_SZ, 0, 0) : (memcmp(dyn_a, cmp, W35_DYN_SZ) == 0);
        uint32_t vm = fast ? totk_rec_vmask(UINT32_C(0x10105), 0) : 0u;
        if (!fast) { for (uint32_t v = 0; v < 17u; v++) if ((UINT32_C(0x10105) >> v) & 1u) vm |= (1u << v); }
        sink += (uint64_t)sc + (uint64_t)(unsigned)sl + (uint64_t)sv + (uint64_t)eq + (uint64_t)vm;
    }
    free(hm.keys); free(hm.vals);
    return sink;
}

static void *w35_aoc_tsan_worker(void *arg) {
    uint64_t id = (uint64_t)(uintptr_t)arg, sum = 0;
    uint8_t da[W35_DYN_SZ], db[W35_DYN_SZ];
    memset(da, (int)(id + 1u), sizeof da); memcpy(db, da, sizeof db);
    totk_desc_stock_memo8 m = {0};
    totk_desc_stock_memo_put(&m, (const void *)(uintptr_t)0x5000u, (void *)(uintptr_t)(id + 7u));
    for (int i = 0; i < 4000; i++) {
        sum += (uint64_t)totk_vk_rec_fast_on();
        sum += (uint64_t)totk_rec_storage_class_fast(512u + (size_t)(i & 127), W35_REC_CLASSES, 0);
        sum += (uint64_t)(uintptr_t)totk_desc_stock_memo_get(&m, (const void *)(uintptr_t)0x5000u, 0);
        sum += (uint64_t)totk_rec_dyn_unchanged(da, db, W35_DYN_SZ, 0, 0);
    }
    return (void *)(uintptr_t)sum;
}

static int run_ktgl_w35_tests(int fault_mode) {
    const char *env = getenv("TOTK_VK_REC_FAST");
    int want_on = !(env && env[0] == '0' && !env[1]);
    assert(totk_vk_rec_fast_on() == want_on);
    totk_w35_set_fault(fault_mode);
    w35_check_storage_and_pool(fault_mode);
    w35_check_memo_dyn_vmask(fault_mode);
    pthread_t th[4];
    for (int i = 0; i < 4; i++) assert(pthread_create(&th[i], NULL, w35_aoc_tsan_worker, (void *)(uintptr_t)i) == 0);
    for (int i = 0; i < 4; i++) { void *r = NULL; assert(pthread_join(th[i], &r) == 0); }

    static const size_t szs[16] = {128, 256, 512, 768, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 98304, 131072, 262144, 524288, 1048576};
    uint8_t da[W35_DYN_SZ], db[W35_DYN_SZ];
    memset(da, 0x33, sizeof da); memcpy(db, da, sizeof db); db[W35_DYN_SZ - 1u] = 0x77u;

    uint64_t t0 = host_now_ns(), s0 = w35_aoc_bench_once(szs, da, db, 0); uint64_t off_ns = host_now_ns() - t0;
    uint64_t t1 = host_now_ns(), s1 = w35_aoc_bench_once(szs, da, db, 1); uint64_t on_ns = host_now_ns() - t1;
    assert(s0 == s1);
    totk_vk_rec_fast_reset(want_on);
    printf("aoc_rec_w35: off=%.3f ms on=%.3f ms sink=%llu\n", off_ns / 1e6, on_ns / 1e6, (unsigned long long)s0);
    return 0;
}
#endif
