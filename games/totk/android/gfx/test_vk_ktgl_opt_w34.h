#ifndef TEST_VK_KTGL_OPT_W34_H
#define TEST_VK_KTGL_OPT_W34_H
#include "vk_pcache_fast.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint64_t host_now_ns(void);

enum { W34_AOC_N = 4096u, W34_PSO_KEY_BYTES = 784u, W34_AOC_PSO_LEN = 784u };

typedef struct {
    uint8_t key[W34_PSO_KEY_BYTES];
    uint32_t frequency;
    uint32_t capture;
} w34_pso_rec;

_Static_assert(sizeof(w34_pso_rec) == 792u, "w34_pso_rec size mismatch");

static inline uint32_t w34_pso_u32(const uint8_t *p) {
    uint32_t v = 0; memcpy(&v, p, sizeof v); return v;
}

static inline void w34_pso_put(uint8_t *p, uint32_t v) {
    memcpy(p, &v, sizeof v);
}

static inline int w34_legacy_pso_name(const uint8_t *p, int optional) {
    size_t n = 0;
    while (n < 80u && p[n]) n++;
    if (!n) {
        if (!optional) return 0;
        for (size_t i = 0; i < 80u; i++) if (p[i]) return 0;
        return 1;
    }
    if (n != 64u && n != 77u) return 0;
    for (size_t i = 0; i < 64u; i++) if (!strchr("0123456789abcdef", p[i])) return 0;
    if (n == 77u) {
        if (p[64] != '-') return 0;
        for (size_t i = 65u; i < 77u; i++) if (!strchr("0123456789abcdef", p[i])) return 0;
    }
    for (size_t i = n; i < 80u; i++) if (p[i]) return 0;
    return 1;
}

static inline int w34_legacy_pso_valid(const w34_pso_rec *r) {
    if (!w34_legacy_pso_name(r->key, 0) || !w34_legacy_pso_name(r->key + 80u, 1) ||
        w34_pso_u32(r->key + 160u) > 15u || !r->frequency || r->capture > 1u) return 0;
    const uint8_t *s = r->key + 164u;
    uint32_t n = w34_pso_u32(s + 40u), samples = w34_pso_u32(s + 44u);
    if (n > 8u || samples > 64u || (samples && (samples & (samples - 1u)))) return 0;
    if (w34_pso_u32(s + 48u) > 255u || w34_pso_u32(s + 56u) > 1u || w34_pso_u32(s + 60u) > 2u ||
        w34_pso_u32(s + 64u) > 2u || w34_pso_u32(s + 68u) > 1u || w34_pso_u32(s + 72u) > 16u) return 0;
    for (unsigned i = 0; i < 8u; i++) {
        const uint8_t *b = s + (19u + 7u * i) * 4u;
        if (w34_pso_u32(b) > 7u || w34_pso_u32(b + 20u) > 7u || w34_pso_u32(b + 24u) > 7u) return 0;
        for (unsigned j = 1u; j < 5u; j++) if (w34_pso_u32(b + 4u * j) > 127u) return 0;
    }
    for (unsigned i = 0; i < 16u; i++) {
        if (w34_pso_u32(s + (75u + 3u * i + 2u) * 4u) > 15u || w34_pso_u32(s + (123u + 2u * i) * 4u) > 1u) return 0;
    }
    return 1;
}

static inline int w34_pso_name(const uint8_t *p, int optional) {
    if (totk_vk_pcache_fast_on()) return totk_pso_name_fast(p, optional, totk_w34_fault());
    return w34_legacy_pso_name(p, optional);
}

static inline int w34_pso_valid(const w34_pso_rec *r) {
    if (totk_vk_pcache_fast_on()) return totk_pso_valid_fast(r->key, r->frequency, r->capture, totk_w34_fault());
    return w34_legacy_pso_valid(r);
}

static inline uint64_t w34_aoc_rng(uint64_t *s) {
    uint64_t x = *s;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    *s = x;
    return x * UINT64_C(0x2545f4914f6cdd1d);
}

static inline void w34_make_record(w34_pso_rec *r, uint32_t id) {
    memset(r, 0, sizeof *r);
    static const char hx[] = "0123456789abcdef";
    for (unsigned i = 0; i < 64u; i++) {
        r->key[i] = (uint8_t)hx[(id >> (i & 15u)) & 15u];
        r->key[80u + i] = (uint8_t)hx[((id + 5u) >> (i & 15u)) & 15u];
    }
    r->key[64] = '-';
    for (unsigned i = 65u; i < 77u; i++) r->key[i] = (uint8_t)hx[(id + i) & 15u];
    w34_pso_put(r->key + 160u, 3u);
    w34_pso_put(r->key + 164u + 10u * 4u, 2u);
    w34_pso_put(r->key + 164u + 11u * 4u, 4u);
    r->frequency = 1u + (id & 7u); r->capture = id & 1u;
}

static void w34_check_names_and_valid(int fault_mode) {
    w34_pso_rec r; w34_make_record(&r, 7u);
    totk_vk_pcache_fast_reset(0);
    assert(w34_pso_name(r.key, 0) == 1 && w34_pso_valid(&r) == 1);
    totk_vk_pcache_fast_reset(1);
    totk_w34_set_fault(fault_mode);
    assert(w34_pso_name(r.key, 0) == 1 && w34_pso_valid(&r) == 1);

    uint8_t bad[80]; memcpy(bad, r.key, sizeof bad);
    bad[79] = 0x41u;
    assert(totk_pso_name_fast(bad, 0, fault_mode) == 0);
    w34_pso_put(r.key + 164u + 11u * 4u, 6u);
    assert(totk_pso_valid_fast(r.key, r.frequency, r.capture, fault_mode) == 0);
}

static void w34_check_faults_and_bitmap(int fault_mode) {
    totk_pw_link a = {0}, b = {0}, *hd = NULL, *tl = NULL;
    totk_pw_link_push(&hd, &tl, &a, &a);
    totk_pw_link_push(&hd, &tl, &b, &b);
    totk_pw_link_unlink(&hd, &tl, &b, fault_mode);
    assert(hd == &a && tl == &a && a.next == NULL);

    w34_pso_rec recs[2]; w34_make_record(&recs[0], 11u); w34_make_record(&recs[1], 12u);
    totk_pso_idx idx = {0};
    assert(totk_pso_idx_reserve(&idx, 32u));
    totk_pso_idx_put_raw(&idx, (const uint8_t *)recs, sizeof *recs, 1u,
                         totk_pso_hash64(recs[0].key, W34_PSO_KEY_BYTES, fault_mode));
    assert(totk_pso_idx_get_raw(&idx, (const uint8_t *)recs, sizeof *recs, recs[1].key,
                                W34_PSO_KEY_BYTES, totk_pso_hash64(recs[1].key, W34_PSO_KEY_BYTES, fault_mode),
                                fault_mode) == 0u);
    totk_pso_idx_fini(&idx);

    uint8_t cur[2048] = {0}, saved[2048] = {0};
    cur[0] = 5u; cur[1] = 1u;
    saved[0] = 5u; saved[1] = 0u;
    assert(totk_pso_bitmap_dirty_fast(cur, saved, 9u, fault_mode) == 1);
    assert(totk_pso_popcount_bits(cur, 9u, fault_mode) == 3u);
}

static uint64_t w34_aoc_bench_once(const w34_pso_rec *recs, totk_pw_link *nodes,
                                     const uint8_t *cur, const uint8_t *saved, int fast) {
    uint64_t sink = 0, rng = UINT64_C(0x5734a0c00000001);
    totk_vk_pcache_fast_reset(fast);
    totk_w34_set_fault(0);
    for (uint32_t i = 0; i < W34_AOC_N; i++) sink += (uint64_t)w34_pso_valid(&recs[i]);
    if (fast) {
        totk_pso_idx idx = {0};
        assert(totk_pso_idx_reserve(&idx, W34_AOC_N));
        for (uint32_t i = 0; i < W34_AOC_N; i++) {
            uint64_t h = totk_pso_hash64(recs[i].key, W34_PSO_KEY_BYTES, 0);
            totk_pso_idx_put_raw(&idx, (const uint8_t *)recs, sizeof *recs, i + 1u, h);
        }
        for (uint32_t q = 0; q < 40000u; q++) {
            uint32_t id = (uint32_t)(w34_aoc_rng(&rng) % W34_AOC_N);
            uint64_t h = totk_pso_hash64(recs[id].key, W34_PSO_KEY_BYTES, 0);
            sink += totk_pso_idx_get_raw(&idx, (const uint8_t *)recs, sizeof *recs, recs[id].key, W34_PSO_KEY_BYTES, h, 0);
        }
        totk_pso_idx_fini(&idx);
    } else {
        nvn_hmap hm = {.klen = W34_PSO_KEY_BYTES};
        for (uint32_t i = 0; i < W34_AOC_N; i++) nvn_hm_put(&hm, recs[i].key, (void *)(uintptr_t)(i + 1u));
        for (uint32_t q = 0; q < 40000u; q++) {
            uint32_t id = (uint32_t)(w34_aoc_rng(&rng) % W34_AOC_N);
            sink += (uint64_t)(uintptr_t)nvn_hm_get(&hm, recs[id].key);
        }
        free(hm.keys); free(hm.vals);
    }
    totk_pw_link *hd = NULL, *tl = NULL;
    for (uint32_t i = 0; i < W34_AOC_N; i++) totk_pw_link_push(&hd, &tl, &nodes[i], (void *)(uintptr_t)(i + 1u));
    for (uint32_t i = 0; i < W34_AOC_N; i++) {
        totk_pw_link *n = &nodes[(i * 733u) % W34_AOC_N];
        sink += (uint64_t)(uintptr_t)n->ent;
        if (fast) totk_pw_link_unlink(&hd, &tl, n, 0);
        else {
            totk_pw_link *prev = NULL, *q = hd;
            while (q && q != n) { prev = q; q = q->next; }
            if (q) { if (prev) prev->next = q->next; else hd = q->next; if (tl == q) tl = prev; }
        }
    }
    assert(!hd && !tl);
    uint32_t d = fast ? (uint32_t)totk_pso_bitmap_dirty_fast(cur, saved, 250u, 0) : (uint32_t)(memcmp(cur, saved, 2048u) != 0);
    return sink + d;
}

static void *w34_aoc_tsan_worker(void *arg) {
    w34_pso_rec r; w34_make_record(&r, (uint32_t)(uintptr_t)arg + 1u);
    uint64_t sum = 0;
    for (int i = 0; i < 4000; i++) {
        sum += (uint64_t)totk_pso_valid_fast(r.key, r.frequency, r.capture, 0);
        sum += totk_pso_hash64(r.key, W34_PSO_KEY_BYTES, 0);
    }
    return (void *)(uintptr_t)sum;
}

static int run_ktgl_w34_tests(int fault_mode) {
    const char *env = getenv("TOTK_VK_PCACHE_FAST");
    int want_on = !(env && env[0] == '0' && !env[1]);
    int init_on = totk_vk_pcache_fast_on();
    assert(init_on == want_on);
    w34_check_names_and_valid(fault_mode);
    w34_check_faults_and_bitmap(fault_mode);
    pthread_t th[4];
    for (int i = 0; i < 4; i++) assert(pthread_create(&th[i], NULL, w34_aoc_tsan_worker, (void *)(uintptr_t)i) == 0);
    for (int i = 0; i < 4; i++) { void *r = NULL; assert(pthread_join(th[i], &r) == 0); }

    w34_pso_rec *recs = (w34_pso_rec *)calloc(W34_AOC_N, sizeof *recs);
    totk_pw_link *nodes = (totk_pw_link *)calloc(W34_AOC_N, sizeof *nodes);
    assert(recs && nodes);
    for (uint32_t i = 0; i < W34_AOC_N; i++) w34_make_record(&recs[i], i);
    uint8_t cur[2048] = {0}, saved[2048] = {0};
    cur[0] = 0x55u; saved[0] = 0x55u;

    uint64_t t0 = host_now_ns(), s0 = w34_aoc_bench_once(recs, nodes, cur, saved, 0); uint64_t off_ns = host_now_ns() - t0;
    uint64_t t1 = host_now_ns(), s1 = w34_aoc_bench_once(recs, nodes, cur, saved, 1); uint64_t on_ns = host_now_ns() - t1;
    assert(s0 == s1);
    totk_vk_pcache_fast_reset(want_on);
    printf("aoc_pcache_w34: off=%.3f ms on=%.3f ms sink=%llu\n", off_ns / 1e6, on_ns / 1e6, (unsigned long long)s0);
    free(recs); free(nodes);
    return 0;
}
#endif
