/* Wave 56 (Optimization 58 / TOTK_VK_LOAD_WB_FAST=1) test & benchmark. */
#ifndef TEST_VK_KTGL_OPT_W56_H
#define TEST_VK_KTGL_OPT_W56_H
#ifndef TOTK_W56_TEST_HOOKS
#define TOTK_W56_TEST_HOOKS
#endif
#include "vk_load_wb_fast.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

_Atomic int g_totk_vk_load_wb_fast_override = -1;
_Atomic int g_totk_w56_fault = -1;

static void w56_req(int ok) {
    if (!ok) abort();
}

static uint64_t w56_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static uint64_t w56_oracle_cam_hash(const float c[84]) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (unsigned i = 0u; i < 84u; i++) {
        if (i < 58u || i == 62u || i == 70u || i == 72u || i == 73u) {
            uint32_t bits = 0u;
            memcpy(&bits, c + i, sizeof bits);
            hash = (hash ^ bits) * UINT64_C(1099511628211);
        }
    }
    return hash;
}

typedef struct { uint64_t lo, hi; } w56_rng_t;
static int w56_rng_cmp(const void *x, const void *y) {
    const w56_rng_t *a = (const w56_rng_t *)x, *b = (const w56_rng_t *)y;
    return a->lo < b->lo ? -1 : (a->lo > b->lo ? 1 : 0);
}

static size_t w56_oracle_coalesce(w56_rng_t *r, size_t n) {
    if (n > 1u) qsort(r, n, sizeof *r, w56_rng_cmp);
    size_t count = 0u;
    for (size_t i = 0u; i < n; i++) {
        w56_rng_t x = r[i];
        if (count && x.lo <= r[count - 1u].hi) {
            if (x.hi > r[count - 1u].hi) r[count - 1u].hi = x.hi;
        } else {
            r[count++] = x;
        }
    }
    return count;
}

static void w56_check_masks_and_cam(void) {
    uint64_t tex_nz[2] = {0u, 0u};
    uint32_t img_nz = 0u;
    uint8_t ready = 1u;
    uint64_t h[4] = {0x11u, 0u, 0x22u, 0x33u};
    totk_img_nz_update(&img_nz, &ready, 4, h, 4);
    w56_req(ready == 1u && img_nz == 0xd0u);
    tex_nz[0] = (UINT64_C(1) << 3) | (UINT64_C(1) << 61);
    tex_nz[1] = UINT64_C(1) << 9;
    totk_slot160_nz_t sm = {0};
    totk_slot160_from_state(&sm, tex_nz, img_nz);
    uint64_t upd[2] = {0u, 0x99u};
    totk_slot160_update_range(&sm, 128 + 6, upd, 2);
    w56_req(sm.w[0] == tex_nz[0] && sm.w[1] == tex_nz[1]);
    w56_req((sm.w[2] & (UINT64_C(1) << 7)) != 0u && (sm.w[2] & (UINT64_C(1) << 6)) == 0u);
    w56_req((sm.w[2] & (UINT64_C(1) << 4)) != 0u);

    float cam[84];
    for (unsigned i = 0u; i < 84u; i++) cam[i] = (float)i * 0.25f - 3.5f;
    w56_req(totk_fg_camera_hash84(cam) == w56_oracle_cam_hash(cam));
}

static void w56_check_wb_and_rng(void) {
    totk_wb_im_tags_t tags = {0};
    uintptr_t ptrs[5] = {0x10100u, 0x20200u, 0x30300u, 0x40400u, 0x20200u};
    for (uint32_t i = 0u; i < 5u; i++) totk_wb_im_set(&tags, i, ptrs[i]);
    uint64_t cmask[4] = {0u};
    totk_wb_im_or_mask(&tags, 0x20200u, 5, cmask);
    int out[256] = {0};
    int nc = totk_wb_mask_to_indices(cmask, 5, out);
    w56_req(nc == 2 && out[0] == 1 && out[1] == 4);
    totk_wb_im_remove_swap(&tags, 1u, 4u);
    memset(cmask, 0, sizeof cmask);
    totk_wb_im_or_mask(&tags, 0x20200u, 4, cmask);
    nc = totk_wb_mask_to_indices(cmask, 4, out);
    w56_req(nc == 1 && out[0] == 1);
    w56_req(totk_ub_has_after(0u, 50u, 50u) == 0);
    w56_req(totk_ub_has_after(1u, 10u, 50u) == 1);
    w56_req(totk_ub_has_after(0u, 51u, 50u) == 1);

    w56_rng_t r0[64], r1[64];
    uint64_t seed = 0x123456789abcdef0ull;
    for (int i = 0; i < 64; i++) {
        seed = seed * 6364136223846793005ull + 1ull;
        uint64_t lo = ((seed >> 16) & 0x3ffu) * 4096u;
        uint64_t hi = lo + (((seed >> 32) & 7u) + 1u) * 4096u;
        r0[i] = (w56_rng_t){lo, hi};
        r1[i] = r0[i];
    }
    size_t c0 = w56_oracle_coalesce(r0, 64u);
    size_t c1 = totk_rng_sort_coalesce((uint64_t *)r1, 64u);
    w56_req(c0 == c1 && memcmp(r0, r1, c0 * sizeof *r0) == 0);
}

static void *w56_thread_fn(void *arg) {
    int tid = *(int *)arg;
    for (int it = 0; it < 200; it++) {
        totk_vk_load_wb_fast_set_for_test((tid + it) & 1);
        (void)totk_vk_load_wb_fast_on();
        w56_check_masks_and_cam();
    }
    return NULL;
}

static uint64_t w56_bench_run(int fast) {
    static uint64_t slots[2][160];
    static w56_rng_t base[192], work[192];
    uint64_t tex_nz[2][2] = {{0u, 0u}, {0u, 0u}}, sink = 0u;
    uint32_t img_nz[2] = {0u, 0u};
    memset(slots, 0, sizeof slots);
    slots[0][3] = 0x1001u; tex_nz[0][0] |= UINT64_C(1) << 3;
    slots[1][129] = 0x2002u; img_nz[1] |= UINT32_C(1) << 1;
    uint64_t s = 0x9988776655443322ull;
    for (int i = 0; i < 192; i++) {
        s = s * 6364136223846793005ull + 1ull;
        uint64_t lo = ((s >> 20) & 0x7ffu) * 4096u;
        base[i] = (w56_rng_t){lo, lo + 8192u};
    }
    float cam[84] = {0};
    cam[0] = 1.0f; cam[62] = 2.0f; cam[73] = 3.0f;
    for (int it = 0; it < 1500; it++) {
        memcpy(work, base, sizeof base);
        if (fast) {
            for (int pass = 0; pass < 8; pass++) {
                for (int st = 0; st < 2; st++) {
                    totk_slot160_nz_t m;
                    totk_slot160_from_state(&m, tex_nz[st], img_nz[st]);
                    for (int w = 0; w < 3; w++) {
                        for (uint64_t b = m.w[w]; b; b &= b - 1u) {
                            int idx = (w << 6) + __builtin_ctzll(b);
                            sink += slots[st][idx];
                        }
                    }
                }
            }
            cam[0] = (float)it;
            sink ^= totk_fg_camera_hash84(cam);
            sink += (uint64_t)totk_rng_sort_coalesce((uint64_t *)work, 192u);
        } else {
            for (int pass = 0; pass < 8; pass++) {
                for (int st = 0; st < 2; st++) {
                    for (int i = 0; i < 160; i++) if (slots[st][i]) sink += slots[st][i];
                }
            }
            cam[0] = (float)it;
            sink ^= w56_oracle_cam_hash(cam);
            sink += (uint64_t)w56_oracle_coalesce(work, 192u);
        }
    }
    return sink;
}

static int run_ktgl_w56_tests(int fault_mode) {
    (void)fault_mode;
    totk_w56_set_fault(-1);
    pthread_t th[4];
    int tids[4] = {0, 1, 2, 3};
    w56_check_masks_and_cam();
    w56_check_wb_and_rng();
    for (int i = 0; i < 4; i++) w56_req(pthread_create(&th[i], NULL, w56_thread_fn, &tids[i]) == 0);
    for (int i = 0; i < 4; i++) pthread_join(th[i], NULL);
    totk_vk_load_wb_fast_set_for_test(-1);
    uint64_t t0 = w56_now_ns(), s0 = w56_bench_run(0), t1 = w56_now_ns(), s1 = w56_bench_run(1), t2 = w56_now_ns();
    w56_req(s0 == s1);
    double ms0 = (double)(t1 - t0) / 1e6, ms1 = (double)(t2 - t1) / 1e6;
    printf("ktglopttest w56: baseline=%.3f ms fast=%.3f ms speedup=%.2fx sink=%llu failures=0\n",
           ms0, ms1, ms0 / (ms1 > 0.001 ? ms1 : 0.001), (unsigned long long)s1);
    return 0;
}

#ifdef TOTK_W56_STANDALONE
int main(void) { return run_ktgl_w56_tests(-1); }
#endif
#endif
