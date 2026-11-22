#ifndef TEST_VK_KTGL_OPT_W51_H
#define TEST_VK_KTGL_OPT_W51_H
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "vk_view_vfix_fast.h"
#ifdef TOTK_W51_STANDALONE
int g_fault_mode = 0;
#endif

_Atomic int g_totk_vk_view_vfix_fast_override = -1;
_Atomic int g_totk_w51_fault = -1;
static int g_w51_fail = 0;

typedef struct {
    uint64_t k0, k1, v;
} w51_view_ent;

typedef struct {
    uint64_t img, incarn;
    uint32_t mip, layer, aspect, stage, binding;
} w51_pl_key;

#define W51_EXPECT_EQ(m, w, g) do { \
    uint64_t wa_ = (uint64_t)(w), ga_ = (uint64_t)(g); \
    if (wa_ != ga_) { \
        g_w51_fail++; \
        fprintf(stderr, "FAIL %s:%d %s want=%llu got=%llu\n", __FILE__, __LINE__, m, \
                (unsigned long long)wa_, (unsigned long long)ga_); \
    } \
} while (0)

static uint64_t w51_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static void w51_test_views_and_vfix(void) {
    totk_view_kv_map_t *kvm = calloc(1, sizeof *kvm);
    totk_view_rev_map_t *rvm = calloc(1, sizeof *rvm);
    totk_vfix_map_t *vfm = calloc(1, sizeof *vfm);
    assert(kvm && rvm && vfm);
    uint64_t k0a = UINT64_C(37) | (UINT64_C(2) << 32) | (UINT64_C(1) << 40);
    uint64_t k1a = UINT64_C(0x110022), k1b = UINT64_C(0x110022) | (UINT64_C(2) << 40);
    totk_view_kv_map_put(kvm, 1u, k0a, k1a, UINT64_C(0x7001));
    totk_view_kv_map_put(kvm, 1u, k0a, k1b, UINT64_C(0x7002));
    uint64_t got = 0u, gk0 = 0u, gk1 = 0u;
    W51_EXPECT_EQ("kv_a", 1, totk_view_kv_map_find(kvm, 1u, k0a, k1a, &got));
    W51_EXPECT_EQ("kv_val_a", UINT64_C(0x7001), got);
    W51_EXPECT_EQ("kv_b", 1, totk_view_kv_map_find(kvm, 1u, k0a, k1b, &got));
    W51_EXPECT_EQ("kv_val_b", UINT64_C(0x7002), got);
    for (uint64_t hi = 1u; hi < 65536u; hi++) {
        uint64_t k1c = k1a ^ (hi << 32);
        if (totk_vkm_hash(1u, k0a, k1c) == totk_vkm_hash(1u, k0a, k1a)) {
            W51_EXPECT_EQ("kv_col_miss", 0, totk_view_kv_map_find(kvm, 1u, k0a, k1c, &got));
            break;
        }
    }
    totk_view_rev_map_put(rvm, 1u, UINT64_C(0x7002), k0a, k1b);
    W51_EXPECT_EQ("rev_hit", 1, totk_view_rev_map_find(rvm, 1u, UINT64_C(0x7002), &gk0, &gk1));
    W51_EXPECT_EQ("rev_k0", k0a, gk0);
    W51_EXPECT_EQ("rev_k1", k1b, gk1);
    for (uint64_t i = 0u; i < 8000u; i++) totk_view_kv_map_put(kvm, 2u + i, k0a, k1a, UINT64_C(0x9000) + i);
    W51_EXPECT_EQ("kv_tgen", 2u, kvm->tgen);
    W51_EXPECT_EQ("kv_stale", 0, totk_view_kv_map_find(kvm, 1u, k0a, k1a, &got));
    uint8_t fk[32], outk[32];
    for (int l = 0; l < 32; l++) fk[l] = (uint8_t)((l * 3) & 7);
    totk_pack_vfix_sparse(UINT32_C(0x80000021), fk, outk);
    W51_EXPECT_EQ("sp_0", fk[0], outk[0]); W51_EXPECT_EQ("sp_1", 0u, outk[1]);
    W51_EXPECT_EQ("sp_5", fk[5], outk[5]); W51_EXPECT_EQ("sp_31", fk[31], outk[31]);
    totk_vfix_map_put(vfm, UINT64_C(0xaaa0), UINT32_C(0x5), UINT64_C(0x1111), UINT64_C(0x801));
    totk_vfix_map_put(vfm, UINT64_C(0xaaa0), UINT32_C(0x5), UINT64_C(0x2222), UINT64_C(0x802));
    W51_EXPECT_EQ("vfix_h1", 1, totk_vfix_map_find(vfm, UINT64_C(0xaaa0), UINT32_C(0x5), UINT64_C(0x1111), &got));
    W51_EXPECT_EQ("vfix_v1", UINT64_C(0x801), got);
    W51_EXPECT_EQ("vfix_h2", 1, totk_vfix_map_find(vfm, UINT64_C(0xaaa0), UINT32_C(0x5), UINT64_C(0x2222), &got));
    W51_EXPECT_EQ("vfix_v2", UINT64_C(0x802), got);
    for (uint64_t kp = 1u; kp < 65536u; kp++) {
        if (kp != UINT64_C(0x1111) && kp != UINT64_C(0x2222) &&
            totk_vfix_map_hash(UINT64_C(0xaaa0), UINT32_C(0x5), kp) ==
            totk_vfix_map_hash(UINT64_C(0xaaa0), UINT32_C(0x5), UINT64_C(0x1111))) {
            W51_EXPECT_EQ("vfix_col_miss", 0, totk_vfix_map_find(vfm, UINT64_C(0xaaa0), UINT32_C(0x5), kp, &got));
            break;
        }
    }
    free(kvm); free(rvm); free(vfm);
}

static void w51_test_pso_and_cpm(void) {
    totk_pso_life_set_t *pls = calloc(1, sizeof *pls);
    assert(pls != NULL);
    uint64_t m0 = 0u, m1 = 0u;
    totk_pso_life_pack_meta(1u, 3u, 1u, 1u, 7u, &m0);
    totk_pso_life_pack_meta(1u, 4u, 1u, 1u, 7u, &m1);
    W51_EXPECT_EQ("pl_add0", 0, totk_pso_life_set_check_add(pls, UINT64_C(0x55), 2u, m0, 0u, 2048u));
    W51_EXPECT_EQ("pl_dup0", 1, totk_pso_life_set_check_add(pls, UINT64_C(0x55), 2u, m0, 1u, 2048u));
    W51_EXPECT_EQ("pl_add1", 0, totk_pso_life_set_check_add(pls, UINT64_C(0x55), 2u, m1, 1u, 2048u));
    W51_EXPECT_EQ("pl_cap_miss", -1, totk_pso_life_set_check_add(pls, UINT64_C(0x56), 2u, m1, 2048u, 2048u));
    W51_EXPECT_EQ("pl_cap_dup", 1, totk_pso_life_set_check_add(pls, UINT64_C(0x55), 2u, m0, 2048u, 2048u));
    totk_pso_life_set_clear(pls);
    W51_EXPECT_EQ("pl_clr", 0, totk_pso_life_set_check_add(pls, UINT64_C(0x55), 2u, m0, 0u, 2048u));
    free(pls);
    totk_clear_pipe_map_t cpm = {0};
    totk_clear_pipe_map_put(&cpm, 37u, 0x3u, 1u, 1u);
    totk_clear_pipe_map_put(&cpm, 37u, 0xcu, 1u, 2u);
    W51_EXPECT_EQ("cpm_0", 0, totk_clear_pipe_map_find(&cpm, 37u, 0x3u, 1u));
    W51_EXPECT_EQ("cpm_1", 1, totk_clear_pipe_map_find(&cpm, 37u, 0xcu, 1u));
    for (uint32_t m = 16u; m < 65536u; m++) {
        if (totk_cpm_hash(37u, m, 1u) == totk_cpm_hash(37u, 0x3u, 1u)) {
            W51_EXPECT_EQ("cpm_m", -1, totk_clear_pipe_map_find(&cpm, 37u, m, 1u));
            break;
        }
    }
}

typedef struct {
    int tid;
    pthread_mutex_t *mu;
    totk_view_kv_map_t *kvm;
    totk_vfix_map_t *vfm;
} w51_targs;

static void *w51_tsan_worker(void *arg) {
    w51_targs *a = (w51_targs *)arg;
    for (int i = 0; i < 200; i++) {
        uint64_t ep = 1u + (uint64_t)(i & 7), k0 = 37u + (uint64_t)(i & 3), k1 = (uint64_t)(i & 15), out = 0u;
        pthread_mutex_lock(a->mu);
        if (!totk_view_kv_map_find(a->kvm, ep, k0, k1, &out))
            totk_view_kv_map_put(a->kvm, ep, k0, k1, UINT64_C(0x3000) + ep * 32u + k1);
        if (!totk_vfix_map_find(a->vfm, UINT64_C(0x1000) + ep, 1u, k1, &out))
            totk_vfix_map_put(a->vfm, UINT64_C(0x1000) + ep, 1u, k1, UINT64_C(0x9000) + ep * 32u + k1);
        pthread_mutex_unlock(a->mu);
    }
    return NULL;
}

static void w51_test_tsan(void) {
    pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    totk_view_kv_map_t *kvm = calloc(1, sizeof *kvm);
    totk_vfix_map_t *vfm = calloc(1, sizeof *vfm);
    assert(kvm && vfm);
    pthread_t th[4];
    w51_targs args[4];
    for (int i = 0; i < 4; i++) {
        args[i] = (w51_targs){i, &mu, kvm, vfm};
        pthread_create(&th[i], NULL, w51_tsan_worker, &args[i]);
    }
    for (int i = 0; i < 4; i++) pthread_join(th[i], NULL);
    free(kvm); free(vfm);
}

static uint64_t w51_bench_pass(int fast, double *ms_out, const w51_view_ent *views, int nviews,
                               const totk_view_kv_map_t *kvm, const totk_view_rev_map_t *rvm,
                               const w51_pl_key *keys, int nkeys, w51_pl_key *memo,
                               totk_pso_life_set_t *pls) {
    uint64_t sink = 0u, t0 = w51_now_ns();
    for (int rep = 0; rep < 40; rep++) {
        for (int i = 0; i < nviews; i++) {
            uint64_t v = 0u, k0 = 0u, k1 = 0u;
            if (fast) {
                totk_view_kv_map_find(kvm, 1u, views[i].k0, views[i].k1, &v);
                totk_view_rev_map_find(rvm, 1u, views[i].v, &k0, &k1);
            } else {
                for (int j = 0; j < nviews; j++) if (views[j].k0 == views[i].k0 && views[j].k1 == views[i].k1) { v = views[j].v; break; }
                for (int j = 0; j < nviews; j++) if (views[j].v == views[i].v) { k0 = views[j].k0; k1 = views[j].k1; break; }
            }
            sink += v + k0 + k1;
        }
        size_t nm = 0u;
        if (fast) totk_pso_life_set_clear(pls);
        for (int pass = 0; pass < 2; pass++) for (int i = 0; i < nkeys; i++) {
            const w51_pl_key *k = &keys[i];
            int dup = 0;
            if (fast) {
                uint64_t meta = 0u;
                totk_pso_life_pack_meta(k->mip, k->layer, k->aspect, k->stage, k->binding, &meta);
                dup = totk_pso_life_set_check_add(pls, k->img, k->incarn, meta, nm, (size_t)nkeys) == 1;
                if (!dup && nm < (size_t)nkeys) nm++;
            } else {
                for (size_t j = 0u; j < nm; j++) {
                    if (memo[j].img == k->img && memo[j].incarn == k->incarn && memo[j].mip == k->mip &&
                        memo[j].layer == k->layer && memo[j].aspect == k->aspect && memo[j].stage == k->stage &&
                        memo[j].binding == k->binding) { dup = 1; break; }
                }
                if (!dup && nm < (size_t)nkeys) memo[nm++] = *k;
            }
            if (!dup) sink += (uint64_t)i + 1u;
        }
    }
    *ms_out = (double)(w51_now_ns() - t0) / 1000000.0;
    return sink;
}

static int run_ktgl_w51_tests(int fault_mode) {
    g_w51_fail = 0;
    totk_w51_set_fault(fault_mode ? fault_mode : totk_w51_parse_fault());
    totk_vk_view_vfix_fast_set_for_test(1);
    W51_EXPECT_EQ("sw1", 1, totk_vk_view_vfix_fast_on());
    totk_vk_view_vfix_fast_set_for_test(0);
    W51_EXPECT_EQ("sw0", 0, totk_vk_view_vfix_fast_on());
    totk_vk_view_vfix_fast_set_for_test(1);
    w51_test_views_and_vfix();
    w51_test_pso_and_cpm();
    w51_test_tsan();
    enum { NV = 64, NK = 512 };
    w51_view_ent *views = calloc(NV, sizeof *views);
    w51_pl_key *keys = calloc(NK, sizeof *keys), *memo = calloc(NK, sizeof *memo);
    totk_view_kv_map_t *kvm = calloc(1, sizeof *kvm);
    totk_view_rev_map_t *rvm = calloc(1, sizeof *rvm);
    totk_pso_life_set_t *pls = calloc(1, sizeof *pls);
    assert(views && keys && memo && kvm && rvm && pls);
    for (int i = 0; i < NV; i++) {
        views[i] = (w51_view_ent){UINT64_C(37) + (uint64_t)(i & 3), (uint64_t)(i + 1) | ((uint64_t)i << 16), UINT64_C(0x40000) + (uint64_t)i};
        totk_view_kv_map_put(kvm, 1u, views[i].k0, views[i].k1, views[i].v);
        totk_view_rev_map_put(rvm, 1u, views[i].v, views[i].k0, views[i].k1);
    }
    for (int i = 0; i < NK; i++) {
        keys[i] = (w51_pl_key){UINT64_C(0x900) + (uint64_t)i, 1u, (uint32_t)(i & 3), (uint32_t)(i & 7), 1u, 1u, (uint32_t)(i & 15)};
    }
    double s_ms = 0.0, f_ms = 0.0;
    uint64_t s_sink = w51_bench_pass(0, &s_ms, views, NV, kvm, rvm, keys, NK, memo, pls);
    uint64_t f_sink = w51_bench_pass(1, &f_ms, views, NV, kvm, rvm, keys, NK, memo, pls);
    W51_EXPECT_EQ("bench_sink", s_sink, f_sink);
    printf("ktglopttest w51: slow=%.3f ms fast=%.3f ms speedup=%.2fx sink=%llu failures=%d\n",
           s_ms, f_ms, f_ms > 0.0 ? s_ms / f_ms : 0.0, (unsigned long long)f_sink, g_w51_fail);
    free(views); free(keys); free(memo); free(kvm); free(rvm); free(pls);
    return g_w51_fail ? 1 : 0;
}

#ifdef TOTK_W51_STANDALONE
int main(void) {
    return run_ktgl_w51_tests(totk_w51_fault_mode());
}
#endif
#endif
