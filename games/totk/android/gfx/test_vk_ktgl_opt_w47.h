#ifndef TEST_VK_KTGL_OPT_W47_H
#define TEST_VK_KTGL_OPT_W47_H
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "vk_canvas_fast.h"
#ifdef TOTK_W47_STANDALONE
#ifndef TOTK_KTGL_OPT_TEST
#define TOTK_KTGL_OPT_TEST 1
#endif
int g_fault_mode = 0;
#endif
#include "nvn_hmap.h"

_Atomic int g_totk_vk_canvas_fast_override = -1;
_Atomic int g_totk_w47_fault = -1;
static int g_w47_fail = 0;

#define W47_EXPECT_EQ(m, w, g) do { \
    uint64_t wa_ = (uint64_t)(w), ga_ = (uint64_t)(g); \
    if (wa_ != ga_) { \
        g_w47_fail++; \
        fprintf(stderr, "FAIL %s:%d %s want=%llu got=%llu\n", __FILE__, __LINE__, m, \
                (unsigned long long)wa_, (unsigned long long)ga_); \
    } \
} while (0)

static uint64_t w47_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static int w47_ref_ui_shader(const char *key, int after) {
    static const char *const keys[] = {
        "7f2113e2","e9fd7b43","a662e313","16684efc","3e11cee5",
        "48c18ae6","35cbc686","4872bec9","9f47ed90","1a40e5f5","2117f58b","8d4e36d0","1469983f"
    };
    if (!key) return 0;
    unsigned count = after ? (unsigned)(sizeof keys / sizeof *keys) : 9u;
    for (unsigned i = 0; i < count; i++) if (!strncmp(key, keys[i], 8)) return 1;
    return 0;
}

static int w47_ref_transfer_shader(const char *key) {
    return key && !strncmp(key, "dd3d1e0ebf73242845970b5655fac199a30f5acf747975c5e771caa49af8b12b", 64);
}

static int w47_ref_same(const vk_canvas_image *a, const vk_canvas_image *b) {
    return a->image && a->image == b->image && a->incarnation == b->incarnation && a->storage == b->storage &&
           a->size == b->size && a->width == b->width && a->height == b->height && a->format == b->format &&
           a->level == b->level && a->layer == b->layer;
}

static unsigned w47_ref_slot(const vk_canvas_image *image, const totk_canvas_known_t *known) {
    unsigned oldest = 0;
    for (unsigned i = 0; i < 16; i++) {
        if (w47_ref_same(image, &known[i].canvas)) return i;
        if (known[i].seen < known[oldest].seen) oldest = i;
    }
    return oldest;
}

static uint64_t w47_ref_hm_hash(const void *k, uint32_t len) {
    const uint8_t *p = (const uint8_t *)k;
    uint64_t h = 0x9e3779b97f4a7c15ull ^ len;
    for (; len >= 8; p += 8, len -= 8) {
        uint64_t w; memcpy(&w, p, 8);
        h = (h ^ w) * 0xff51afd7ed558ccdull; h ^= h >> 32;
    }
    if (len) {
        uint64_t w = 0; memcpy(&w, p, len);
        h = (h ^ w) * 0xff51afd7ed558ccdull;
    }
    h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ull; h ^= h >> 33;
    return h;
}

static void w47_test_canvas_shaders_and_slots(void) {
    static const char *const cases[] = {
        "7f2113e2-abc","e9fd7b43","a662e313","16684efc","3e11cee5","48c18ae6","35cbc686","4872bec9",
        "9f47ed90","1a40e5f5","2117f58b","8d4e36d0","1469983f-xyz","7f2113e","7f","dd3d1e0e",
        "7f2113e3","78eb7b52-000","","0000000000000000"
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        W47_EXPECT_EQ("ui0", w47_ref_ui_shader(cases[i], 0), totk_canvas_ui_shader_fast(cases[i], 0));
        W47_EXPECT_EQ("ui1", w47_ref_ui_shader(cases[i], 1), totk_canvas_ui_shader_fast(cases[i], 1));
    }
    W47_EXPECT_EQ("ui null", 0, totk_canvas_ui_shader_fast(NULL, 1));
    static const char *const tx_cases[] = {
        "dd3d1e0ebf73242845970b5655fac199a30f5acf747975c5e771caa49af8b12b",
        "dd3d1e0ebf73242845970b5655fac199a30f5acf747975c5e771caa49af8b12b-tail",
        "dd3d1e0ebf73242845970b5655fac199a30f5acf747975c5e771caa49af8b12c",
        "dd3d1e0e", "dd", ""
    };
    for (size_t i = 0; i < sizeof tx_cases / sizeof *tx_cases; i++) {
        W47_EXPECT_EQ("tx", w47_ref_transfer_shader(tx_cases[i]), totk_canvas_transfer_shader_fast(tx_cases[i]));
    }
    W47_EXPECT_EQ("tx null", 0, totk_canvas_transfer_shader_fast(NULL));

    totk_canvas_known_t known[16];
    memset(known, 0, sizeof known);
    uint32_t hint = 0u;
    for (unsigned i = 0; i < 16; i++) {
        known[i].canvas = (vk_canvas_image){.image = i + 10u, .incarnation = 1, .storage = 0x1000u + i * 0x100u,
            .size = 4096, .width = 1280, .height = 720, .format = 37, .level = 0, .layer = 0};
        known[i].seen = 100u - i;
    }
    vk_canvas_image probe = known[5].canvas;
    W47_EXPECT_EQ("slot hit", w47_ref_slot(&probe, known), totk_canvas_slot_fast(&probe, known, &hint));
    W47_EXPECT_EQ("slot hint", w47_ref_slot(&probe, known), totk_canvas_slot_fast(&probe, known, &hint));
    vk_canvas_image diff_stor = probe; diff_stor.storage++;
    W47_EXPECT_EQ("diff stor", w47_ref_slot(&diff_stor, known), totk_canvas_slot_fast(&diff_stor, known, &hint));
}

typedef struct { _Atomic uint32_t probes; } w47_aoc_probe_ctx_t;
static uint32_t w47_aoc_probe(uint32_t fmt, void *ctx) {
    w47_aoc_probe_ctx_t *p = (w47_aoc_probe_ctx_t *)ctx;
    atomic_fetch_add_explicit(&p->probes, 1u, memory_order_relaxed);
    return (fmt * UINT32_C(0x45D9F3B)) ^ UINT32_C(0xA5A55A5A);
}

typedef struct { totk_fmt_feats_cache_t *c; w47_aoc_probe_ctx_t *p; } w47_thr_arg_t;
static void *w47_feats_thr(void *arg) {
    w47_thr_arg_t *a = (w47_thr_arg_t *)arg;
    static const uint32_t fmts[4] = {37u, 122u, 1000340001u, 1000470000u};
    for (int it = 0; it < 200; it++) {
        uint32_t f = fmts[it & 3];
        uint32_t want = (f * UINT32_C(0x45D9F3B)) ^ UINT32_C(0xA5A55A5A);
        if (totk_fmt_feats_fast_get(a->c, f, w47_aoc_probe, a->p) != want) atomic_fetch_add(&a->p->probes, 1000u);
    }
    return NULL;
}

static void w47_test_feats_and_hmap(void) {
    static totk_fmt_feats_cache_t fc;
    memset(&fc, 0, sizeof fc);
    w47_aoc_probe_ctx_t pc = {0};
    for (int r = 0; r < 3; r++) {
        W47_EXPECT_EQ("core37", (37u * UINT32_C(0x45D9F3B)) ^ UINT32_C(0xA5A55A5A), totk_fmt_feats_fast_get(&fc, 37u, w47_aoc_probe, &pc));
        W47_EXPECT_EQ("ext340001", (1000340001u * UINT32_C(0x45D9F3B)) ^ UINT32_C(0xA5A55A5A), totk_fmt_feats_fast_get(&fc, 1000340001u, w47_aoc_probe, &pc));
    }
    W47_EXPECT_EQ("single probe", 2u, atomic_load(&pc.probes));
    pthread_t th[4];
    w47_thr_arg_t arg = {&fc, &pc};
    for (int t = 0; t < 4; t++) pthread_create(&th[t], NULL, w47_feats_thr, &arg);
    for (int t = 0; t < 4; t++) pthread_join(th[t], NULL);
    W47_EXPECT_EQ("thr probes", 4u, atomic_load(&pc.probes));

    uint8_t k1[80], k2[80];
    for (uint32_t i = 0; i < 80u; i++) { k1[i] = (uint8_t)(i * 3u + 1u); k2[i] = k1[i]; }
    uint32_t b1 = (uint32_t)nvn_hm_hash(k1, 80u) & 63u;
    for (uint32_t t = 1u; t < 4096u; t++) {
        k2[78] = (uint8_t)(t & 0xFFu);
        k2[79] = (uint8_t)(t >> 8);
        if ((k2[78] != k1[78] || k2[79] != k1[79]) && ((uint32_t)nvn_hm_hash(k2, 80u) & 63u) == b1) break;
    }
    W47_EXPECT_EQ("hm eq diff", 0, nvn_hm_eq_gen(k1, k2, 80u));
    const uint32_t lens[5] = {8u, 16u, 24u, 64u, 80u};
    for (size_t i = 0; i < 5; i++) {
        W47_EXPECT_EQ("hm hash", w47_ref_hm_hash(k1, lens[i]), nvn_hm_hash(k1, lens[i]));
    }
    nvn_hmap hm = {.klen = 80u};
    nvn_hm_put(&hm, k1, (void *)(uintptr_t)0x1111u);
    nvn_hm_put(&hm, k2, (void *)(uintptr_t)0x2222u);
    W47_EXPECT_EQ("hm get k1", (uintptr_t)0x1111u, (uintptr_t)nvn_hm_get(&hm, k1));
    W47_EXPECT_EQ("hm get k2", (uintptr_t)0x2222u, (uintptr_t)nvn_hm_get(&hm, k2));
    nvn_hm_del(&hm, k1);
    W47_EXPECT_EQ("hm del k1", 0u, (uintptr_t)nvn_hm_get(&hm, k1));
    W47_EXPECT_EQ("hm keep k2", (uintptr_t)0x2222u, (uintptr_t)nvn_hm_get(&hm, k2));
    free(hm.keys); free(hm.vals);
}

static void w47_run_benchmark(void) {
    volatile uint64_t sink = 0;
    const char *keys[4] = {"7f2113e2-aa", "78eb7b52-bb", "dd3d1e0ebf73242845970b5655fac199a30f5acf747975c5e771caa49af8b12b", "1469983f-cc"};
    uint8_t hbuf[80];
    for (int i = 0; i < 80; i++) hbuf[i] = (uint8_t)(i + 7);
    uint64_t t0 = w47_now_ns();
    for (int it = 0; it < 200000; it++) {
        const char *s = keys[it & 3];
        sink += (uint64_t)(w47_ref_ui_shader(s, 1) + w47_ref_transfer_shader(s)) + (w47_ref_hm_hash(hbuf, 64u) & 3u);
    }
    uint64_t dt_slow = w47_now_ns() - t0;
    uint64_t t1 = w47_now_ns();
    for (int it = 0; it < 200000; it++) {
        const char *s = keys[it & 3];
        sink += (uint64_t)(totk_canvas_ui_shader_fast(s, 1) + totk_canvas_transfer_shader_fast(s)) + (nvn_hm_hash(hbuf, 64u) & 3u);
    }
    uint64_t dt_fast = w47_now_ns() - t1;
    printf("aoc_w47_canvas_fast: slow=%.3f ms fast=%.3f ms speedup=%.2fx sink=%llu\n",
           (double)dt_slow / 1e6, (double)dt_fast / 1e6,
           dt_fast ? (double)dt_slow / (double)dt_fast : 0.0, (unsigned long long)sink);
}

static int run_ktgl_w47_tests(int fault_mode) {
    g_w47_fail = 0;
    const char *ef = getenv("TOTK_W47_FAULT");
    if (ef && *ef) totk_w47_set_fault(atoi(ef));
    int saved_fm = g_fault_mode;
    if (fault_mode >= 181 && fault_mode <= 184) totk_w47_set_fault(fault_mode - 180);
    else if (totk_w47_fault_mode() == 5) g_fault_mode = 185;
    totk_vk_canvas_fast_set_for_test(1);
    w47_test_canvas_shaders_and_slots();
    w47_test_feats_and_hmap();
    w47_run_benchmark();
    totk_vk_canvas_fast_set_for_test(0);
    w47_test_canvas_shaders_and_slots();
    totk_vk_canvas_fast_set_for_test(1);
    totk_w47_set_fault(0);
    g_fault_mode = saved_fm;
    return g_w47_fail != 0;
}

#ifdef TOTK_W47_STANDALONE
int main(void) {
    const char *f = getenv("TOTK_KTGL_OPT_FAULT");
    if (f && *f) g_fault_mode = atoi(f);
    int rc = run_ktgl_w47_tests(g_fault_mode);
    printf("w47_standalone: fault=%d failures=%d\n", g_fault_mode, rc);
    return rc;
}
#endif
#endif
