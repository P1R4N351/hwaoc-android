#ifndef TEST_VK_KTGL_OPT_W33_H
#define TEST_VK_KTGL_OPT_W33_H
#include "vk_seed_pack.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint64_t host_now_ns(void);

enum { W33_AOC_N = 8192u, W33_AOC_WARM = 2000u };

static inline uint64_t w33_aoc_rng(uint64_t *s) {
    uint64_t x = *s;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    *s = x;
    return x * UINT64_C(0x2545f4914f6cdd1d);
}

static inline void w33_put32(unsigned char *p, uint32_t v) {
    for (unsigned i = 0; i < 4u; i++) p[i] = (unsigned char)(v >> (8u * i));
}
static inline void w33_put64(unsigned char *p, uint64_t v) {
    for (unsigned i = 0; i < 8u; i++) p[i] = (unsigned char)(v >> (8u * i));
}

typedef struct w33_warm_node {
    struct w33_warm_node *queue;
    totk_seed_warm_link warm;
    uint32_t id;
} w33_warm_node;

static int w33_legacy_valid_name(const unsigned char *name, size_t *length) {
    size_t n = 0;
    while (n < 84u && name[n]) {
        unsigned char c = name[n];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return 0;
        n++;
    }
    if (!n || n > 83u) return 0;
    for (size_t i = n + 1u; i < 84u; i++) if (name[i]) return 0;
    if ((n > 4u && !memcmp(name + n - 4u, ".spv", 4u)) ||
        (n > 3u && (!memcmp(name + n - 3u, ".vk", 3u) || !memcmp(name + n - 3u, ".vv", 3u)))) {
        *length = n; return 1;
    }
    return 0;
}

static unsigned char *w33_make_pack(uint32_t count, int overlap, size_t *out_sz) {
    size_t hdr = 32u + (size_t)count * 104u;
    size_t fsz = hdr + (size_t)count * 8u;
    unsigned char *b = (unsigned char *)calloc(1, fsz);
    assert(b != NULL);
    memcpy(b, "TOTKVKP1", 8u);
    w33_put32(b + 8u, 1u); w33_put32(b + 12u, 104u);
    w33_put64(b + 16u, count); w33_put64(b + 24u, fsz);
    for (uint32_t i = 0; i < count; i++) {
        unsigned char *rec = b + 32u + (size_t)i * 104u;
        snprintf((char *)rec, 84u, "k%06u.spv", i);
        uint64_t off = overlap && i + 1u == count ? (uint64_t)hdr : (uint64_t)hdr + (uint64_t)i * 8u;
        w33_put64(rec + 88u, off);
        w33_put64(rec + 96u, 8u);
    }
    *out_sz = fsz;
    return b;
}

static void w33_aoc_check_faults_and_names(int fault_mode) {
    unsigned char rec[84];
    for (unsigned c = 0; c < 256u; c++) {
        memset(rec, 0, sizeof rec);
        snprintf((char *)rec, sizeof rec, "a%ca.spv", (char)c);
        if (c == 0) continue;
        size_t l0 = 0, l1 = 0;
        int ok0 = w33_legacy_valid_name(rec, &l0);
        int ok1 = totk_seed_valid_name_fast(rec, &l1, 0);
        assert(ok0 == ok1 && (!ok0 || l0 == l1));
    }
    memset(rec, 0, sizeof rec);
    memcpy(rec, "s.spv", 6u); rec[20] = 0x55u;
    size_t dummy = 0;
    assert(w33_legacy_valid_name(rec, &dummy) == 0);
    assert(totk_seed_valid_name_fast(rec, &dummy, fault_mode) == 0);

    size_t psz = 0;
    unsigned char *bad = w33_make_pack(4u, 1, &psz);
    assert(vk_seed_pack_validate(bad, 4u, psz, 32u + 4u * 104u, 0, 0) == -2);
    assert(vk_seed_pack_validate(bad, 4u, psz, 32u + 4u * 104u, 1, fault_mode) == -2);
    free(bad);

    totk_seed_idx idx = {0};
    assert(totk_seed_idx_reserve(&idx, 16u));
    const char *kA = "alpha_001.spv", *kB = "amiss_002.spv";
    totk_seed_idx_put_unique(&idx, kA, 13u, totk_seed_hash(kA, 13u, fault_mode), (void *)(uintptr_t)42u);
    assert(totk_seed_idx_get(&idx, kB, 13u, totk_seed_hash(kB, 13u, fault_mode), fault_mode) == NULL);
    totk_seed_idx_fini(&idx);

    w33_warm_node n0 = {0}, n1 = {0}, n2 = {0};
    totk_seed_warm_link *lhd = NULL, *ltl = NULL;
    totk_seed_warm_link_push(&lhd, &ltl, &n0.warm, &n0);
    totk_seed_warm_link_push(&lhd, &ltl, &n1.warm, &n1);
    totk_seed_warm_link_push(&lhd, &ltl, &n2.warm, &n2);
    totk_seed_warm_link_unlink(&lhd, &ltl, &n1.warm, fault_mode);
    assert(n0.warm.next == &n2.warm && n2.warm.prev == &n0.warm);
    assert(totk_seed_ready_decode(2, fault_mode) == 1);
}

static uint64_t w33_aoc_bench_once(const unsigned char *pack, size_t psz,
                                     char (*names)[84], w33_warm_node *nodes, int fast_on) {
    uint64_t sink = 0, rng = UINT64_C(0x5733a0c00000001);
    int r = vk_seed_pack_validate(pack, W33_AOC_N, psz, 32u + (uint64_t)W33_AOC_N * 104u, fast_on, 0);
    assert(r == 0);
    if (fast_on) {
        totk_seed_idx idx = {0};
        assert(totk_seed_idx_reserve(&idx, W33_AOC_N));
        for (uint32_t i = 0; i < W33_AOC_N; i++) {
            size_t len = 11u;
            totk_seed_idx_put_unique(&idx, names[i], len, totk_seed_hash(names[i], len, 0), (void *)(uintptr_t)(i + 1u));
        }
        for (uint32_t q = 0; q < 60000u; q++) {
            uint32_t id = (uint32_t)(w33_aoc_rng(&rng) % W33_AOC_N);
            void *v = totk_seed_idx_get(&idx, names[id], 11u, totk_seed_hash(names[id], 11u, 0), 0);
            sink += (uint64_t)(uintptr_t)v;
        }
        totk_seed_idx_fini(&idx);
    } else {
        nvn_hmap hm = {.klen = 84u};
        for (uint32_t i = 0; i < W33_AOC_N; i++) nvn_hm_put(&hm, names[i], (void *)(uintptr_t)(i + 1u));
        for (uint32_t q = 0; q < 60000u; q++) {
            uint32_t id = (uint32_t)(w33_aoc_rng(&rng) % W33_AOC_N);
            void *v = nvn_hm_get(&hm, names[id]);
            sink += (uint64_t)(uintptr_t)v;
        }
        free(hm.keys); free(hm.vals);
    }
    w33_warm_node *hd = NULL, *tl = NULL;
    totk_seed_warm_link *lhd = NULL, *ltl = NULL;
    for (uint32_t i = 0; i < W33_AOC_WARM; i++) {
        nodes[i].id = i + 1u; nodes[i].queue = NULL;
        if (fast_on) totk_seed_warm_link_push(&lhd, &ltl, &nodes[i].warm, &nodes[i]);
        else { if (tl) tl->queue = &nodes[i]; else hd = &nodes[i]; tl = &nodes[i]; }
    }
    for (uint32_t i = W33_AOC_WARM; i > 0u; i--) {
        w33_warm_node *f = &nodes[(i * 733u) % W33_AOC_WARM];
        sink += f->id;
        if (fast_on) {
            totk_seed_warm_link_unlink(&lhd, &ltl, &f->warm, 0);
        } else {
            w33_warm_node **lk = &hd, *prev = NULL;
            while (*lk && *lk != f) { prev = *lk; lk = &(*lk)->queue; }
            if (*lk) { *lk = f->queue; if (tl == f) tl = prev; }
        }
    }
    assert(fast_on ? (!lhd && !ltl) : (!hd && !tl));
    return sink;
}

static void *w33_aoc_tsan_worker(void *arg) {
    uint64_t sum = 0;
    for (int i = 0; i < 4000; i++) {
        unsigned char rec[84] = {0};
        snprintf((char *)rec, sizeof rec, "t%05d.spv", i);
        size_t n = 0;
        assert(totk_seed_valid_name_fast(rec, &n, 0) == 1);
        sum += totk_seed_hash((const char *)rec, n, 0) + (uint64_t)totk_seed_ready_decode(2, 0);
    }
    return (void *)(uintptr_t)(sum + (uint64_t)(uintptr_t)arg);
}

static int run_ktgl_w33_tests(int fault_mode) {
    w33_aoc_check_faults_and_names(fault_mode);
    size_t psz = 0;
    unsigned char *pack = w33_make_pack(W33_AOC_N, 0, &psz);
    char (*names)[84] = (char (*)[84])calloc(W33_AOC_N, sizeof *names);
    w33_warm_node *nodes = (w33_warm_node *)calloc(W33_AOC_WARM, sizeof *nodes);
    assert(names && nodes);
    for (uint32_t i = 0; i < W33_AOC_N; i++) snprintf(names[i], 84u, "k%06u.spv", i);

    pthread_t th[4];
    for (int i = 0; i < 4; i++) assert(pthread_create(&th[i], NULL, w33_aoc_tsan_worker, (void *)(uintptr_t)i) == 0);
    for (int i = 0; i < 4; i++) { void *r = NULL; assert(pthread_join(th[i], &r) == 0); }

    uint64_t t0 = host_now_ns(), s0 = w33_aoc_bench_once(pack, psz, names, nodes, 0);
    uint64_t off_ns = host_now_ns() - t0;
    uint64_t t1 = host_now_ns(), s1 = w33_aoc_bench_once(pack, psz, names, nodes, 1);
    uint64_t on_ns = host_now_ns() - t1;
    assert(s0 == s1);
    printf("aoc_seed_w33: off=%.3f ms on=%.3f ms sink=%llu\n", off_ns / 1e6, on_ns / 1e6, (unsigned long long)s0);
    free(pack); free(names); free(nodes);
    return 0;
}
#endif
