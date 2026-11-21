/* Wave 27 V2 (Opt 29) tests, TSan stress, microbench, and faults 87..91 for TOTK_FS_LOOKUP_FAST. */
#pragma once
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOTK_FS_FAST_TEST_HOOKS 1
#include "totk_fs_fast.h"

#define W27_AOC_ROM_N 512

uint64_t host_now_ns(void);

static const char *const g_w27_mnts[16] = {
    "save", "save_alt", "temp", "cache", "m04", "m05", "m06", "m07",
    "m08", "m09", "m10", "m11", "m12", "m13", "m14", "target",
};

static char g_w27_rom_paths[W27_AOC_ROM_N][96];
static const int g_w27_hot[8] = {15, 79, 149, 221, 303, 381, -1, 487};

static void w27_init_sorted_rom(void) {
    for (int i = 0; i < W27_AOC_ROM_N; i++) {
        if (i == 487) {
            snprintf(g_w27_rom_paths[i], sizeof g_w27_rom_paths[i],
                     "%03d/Model/Character_Zelda_LongAssetWithinCache_VariantB.bfres.zs", i);
        } else {
            snprintf(g_w27_rom_paths[i], sizeof g_w27_rom_paths[i],
                     "%03d/Pack/Actor/Asset_Bundle_%03d.pack.zs", i, i);
        }
    }
}

static const char *w27_fixture_path(int p) {
    if (p == 6) return "999/Missing/Optional/DLC_Probe.byml";
    return g_w27_rom_paths[g_w27_hot[p]];
}

static uint64_t w27_bsearch_rom(const char *p) {
    int lo = 0, hi = W27_AOC_ROM_N;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        int c = strcmp(g_w27_rom_paths[mid], p);
        if (c == 0) return 1000ull + (uint64_t)mid;
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    return UINT64_MAX;
}

static uint64_t w27_slow_lookup(const char *p, uint32_t *ov_out, uint8_t *fst, uint8_t *dst) {
    uint64_t idx = w27_bsearch_rom(p);
    if (idx != UINT64_MAX) {
        *fst = TOTK_FS_FSTATE_FILE;
        *dst = TOTK_FS_DSTATE_UNK;
        *ov_out = (idx == 1000ull + (uint64_t)g_w27_hot[1]) ? 2u : 0u;
        return idx;
    }
    *fst = TOTK_FS_FSTATE_NOFILE;
    *dst = !strcmp(p, "015/Pack") ? TOTK_FS_DSTATE_DIR : TOTK_FS_DSTATE_NODIR;
    *ov_out = 0u;
    return UINT64_MAX;
}

static uint64_t w27_fast_lookup(totk_fs_rom_cache *rc, const char *p, uint32_t *ov_out, uint8_t *fst, uint8_t *dst) {
    size_t plen = strlen(p);
    uint64_t idx = UINT64_MAX;
    uint32_t ov = 0;
    uint8_t cf = 0, cd = 0;
    if (totk_fs_fast_active() && totk_fs_rom_get(rc, p, plen, &idx, &ov, &cf, &cd)) {
        if (cf == TOTK_FS_FSTATE_FILE || cd != TOTK_FS_DSTATE_UNK) {
            *ov_out = ov; *fst = cf; *dst = cd; return idx;
        }
    }
    idx = w27_slow_lookup(p, ov_out, fst, dst);
    if (totk_fs_fast_active())
        totk_fs_rom_put(rc, p, plen, idx, *ov_out, *fst, *dst);
    return idx;
}

static void w27_publish_mnts(totk_fs_mnt_table *mt, int cnt) {
    uint64_t ep = totk_fs_mnt_begin(mt);
    for (int i = 0; i < cnt; i++)
        totk_fs_mnt_insert(mt, ep, g_w27_mnts[i], strlen(g_w27_mnts[i]), i);
}

static int w27_check_fault(int f) {
    totk_fs_reset_for_test(1);
    static totk_fs_rom_cache rc;
    static totk_fs_mnt_table mt;
    memset(&rc, 0, sizeof rc);
    memset(&mt, 0, sizeof mt);
    rc.epoch = 1;
    w27_publish_mnts(&mt, 16);
    uint32_t ov = 0; uint8_t fst = 0, dst = 0;
    if (f == 87) {
        const char *p0 = w27_fixture_path(0);
        uint64_t a = w27_fast_lookup(&rc, p0, &ov, &fst, &dst);
        uint64_t b = w27_fast_lookup(&rc, p0, &ov, &fst, &dst);
        return (a == 1015ull && b == 1015ull) ? 0 : 1;
    }
    if (f == 88) {
        (void)w27_fast_lookup(&rc, w27_fixture_path(2), &ov, &fst, &dst);
        totk_fs_rom_bump(&rc);
        w27_publish_mnts(&mt, 2);
        int slot = -1;
        return totk_fs_mnt_find(&mt, "target", 6u, &slot) == 0 ? 0 : 1;
    }
    if (f == 89) {
        const char *p6 = w27_fixture_path(6);
        (void)w27_fast_lookup(&rc, p6, &ov, &fst, &dst);
        uint64_t b = w27_fast_lookup(&rc, p6, &ov, &fst, &dst);
        return (b == UINT64_MAX && fst == TOTK_FS_FSTATE_NOFILE && dst == TOTK_FS_DSTATE_NODIR) ? 0 : 1;
    }
    if (f == 90) {
        const char *p0 = w27_fixture_path(0);
        assert(w27_fast_lookup(&rc, p0, &ov, &fst, &dst) == 1015ull);
        uint32_t slot0 = (uint32_t)(totk_fs_hash64(p0, strlen(p0)) & (TOTK_FS_ROM_WAYS - 1u));
        char coll[64] = {0};
        for (int n = 0; n < 16384; n++) {
            int len = snprintf(coll, sizeof coll, "999/Coll%d", n);
            if ((uint32_t)(totk_fs_hash64(coll, (size_t)len) & (TOTK_FS_ROM_WAYS - 1u)) == slot0) break;
            coll[0] = 0;
        }
        assert(coll[0] != 0);
        return w27_fast_lookup(&rc, coll, &ov, &fst, &dst) == UINT64_MAX ? 0 : 1;
    }
    if (f == 91) {
        int slot = -1;
        int hit = totk_fs_mnt_find(&mt, "target", 6u, &slot);
        return (hit == 1 && slot == 15 && totk_fs_fd_unpack_fd(0ull) == 0) ? 0 : 1;
    }
    return 1;
}

typedef struct {
    totk_fs_rom_cache *rc;
    totk_fs_mnt_table *mt;
    _Atomic uint64_t *fd_slot;
} w27_stress_t;

static void *w27_worker(void *arg) {
    w27_stress_t *c = (w27_stress_t *)arg;
    for (int i = 0; i < 4000; i++) {
        uint32_t ov = 0; uint8_t fst = 0, dst = 0;
        int p = i & 7;
        uint64_t idx = w27_fast_lookup(c->rc, w27_fixture_path(p), &ov, &fst, &dst);
        if (p == 6) assert(idx == UINT64_MAX && fst == TOTK_FS_FSTATE_NOFILE && dst == TOTK_FS_DSTATE_NODIR);
        else assert(idx == 1000ull + (uint64_t)g_w27_hot[p] && fst == TOTK_FS_FSTATE_FILE && ov == ((p == 1) ? 2u : 0u));
        int slot = -1;
        int mi = i & 15;
        assert(totk_fs_mnt_find(c->mt, g_w27_mnts[mi], strlen(g_w27_mnts[mi]), &slot) && slot == mi);
        uint64_t fw = atomic_load_explicit(c->fd_slot, memory_order_acquire);
        assert(totk_fs_fd_unpack_fd(fw) == 17 && totk_fs_fd_unpack_mode(fw) == 3 && totk_fs_fd_unpack_save(fw) == 1);
    }
    return NULL;
}

static int run_ktgl_w27_tests(int fault_mode) {
    w27_init_sorted_rom();
    if (fault_mode >= 87 && fault_mode <= 91)
        return w27_check_fault(fault_mode);
    static totk_fs_rom_cache rc;
    static totk_fs_mnt_table mt;
    memset(&rc, 0, sizeof rc);
    memset(&mt, 0, sizeof mt);
    rc.epoch = 1;
    w27_publish_mnts(&mt, 16);
    _Atomic uint64_t fd_slot = 0;
    atomic_store_explicit(&fd_slot, totk_fs_fd_pack(17, 3, 1), memory_order_release);

    for (int mode = 0; mode < 2; mode++) {
        totk_fs_reset_for_test(mode);
        for (int pass = 0; pass < 2; pass++) {
            for (int p = 0; p < 8; p++) {
                uint32_t ov = 0; uint8_t fst = 0, dst = 0;
                uint64_t got = w27_fast_lookup(&rc, w27_fixture_path(p), &ov, &fst, &dst);
                if (p == 6) assert(got == UINT64_MAX && fst == TOTK_FS_FSTATE_NOFILE && dst == TOTK_FS_DSTATE_NODIR);
                else assert(got == 1000ull + (uint64_t)g_w27_hot[p] && ov == ((p == 1) ? 2u : 0u));
            }
            uint32_t ov = 0; uint8_t fst = 0, dst = 0;
            assert(w27_fast_lookup(&rc, "015/Pack", &ov, &fst, &dst) == UINT64_MAX && dst == TOTK_FS_DSTATE_DIR);
            for (int mi = 0; mi < 16; mi++) {
                int slot = -1;
                if (mode) assert(totk_fs_mnt_find(&mt, g_w27_mnts[mi], strlen(g_w27_mnts[mi]), &slot) && slot == mi);
                else assert(!totk_fs_mnt_find(&mt, g_w27_mnts[mi], strlen(g_w27_mnts[mi]), &slot));
            }
        }
    }

    totk_fs_reset_for_test(1);
    w27_stress_t ctx = {.rc = &rc, .mt = &mt, .fd_slot = &fd_slot};
    pthread_t th[4];
    for (int i = 0; i < 4; i++) assert(pthread_create(&th[i], NULL, w27_worker, &ctx) == 0);
    for (int i = 0; i < 4; i++) assert(pthread_join(th[i], NULL) == 0);

    for (int mode = 0; mode < 2; mode++) {
        totk_fs_reset_for_test(mode);
        volatile uint64_t sink = 0;
        uint64_t t0 = host_now_ns();
        for (int i = 0; i < 160000; i++) {
            uint32_t ov = 0; uint8_t fst = 0, dst = 0;
            const char *path = w27_fixture_path(i & 7);
            uint64_t idx = mode ? w27_fast_lookup(&rc, path, &ov, &fst, &dst) : w27_slow_lookup(path, &ov, &fst, &dst);
            int slot = 0;
            if (mode) (void)totk_fs_mnt_find(&mt, g_w27_mnts[15], 6u, &slot);
            else for (int m = 0; m < 16; m++) { if (!strcmp(g_w27_mnts[m], "target")) { slot = m; break; } }
            sink += (idx == UINT64_MAX ? 7ull : idx) + (uint64_t)ov + (uint64_t)slot;
        }
        printf("BENCH aoc_w27_%s fs_lookup=%.3f ms sink=%llu\n", mode ? "on" : "off",
               (double)(host_now_ns() - t0) / 1e6, (unsigned long long)sink);
    }
    return 0;
}
