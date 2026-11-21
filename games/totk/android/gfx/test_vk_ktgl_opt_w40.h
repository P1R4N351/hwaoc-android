#ifndef TEST_VK_KTGL_OPT_W40_H
#define TEST_VK_KTGL_OPT_W40_H
#define T_AOC_W40_TEST_HOOKS 1
#include "vk_ovl_fast.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

_Atomic int g_totk_vk_ovl_fast_state;
_Atomic int g_totk_w40_fault;

static inline uint64_t w40_aoc_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}

static const uint8_t w40_glyphs[4][7] = {
    {14, 17, 17, 17, 17, 17, 14},
    {31, 16, 16, 30, 16, 16, 31},
    {0, 0, 0, 0, 0, 0, 0},
    {31, 4, 4, 4, 4, 4, 4},
};

typedef struct { uint32_t *origin; ptrdiff_t dx, dy; } w40_rt;

static w40_rt w40_rt_make(uint32_t *px, unsigned w, unsigned h, int rot) {
    switch (rot & 3) {
    case 1: return (w40_rt){px + h - 1u, (ptrdiff_t)h, -1};
    case 2: return (w40_rt){px + (size_t)w * h - 1u, -1, -(ptrdiff_t)w};
    case 3: return (w40_rt){px + (size_t)(w - 1u) * h, -(ptrdiff_t)h, 1};
    default: return (w40_rt){px, 1, (ptrdiff_t)w};
    }
}

static void w40_slow_cell(
    w40_rt out, unsigned row, unsigned col, const uint8_t glyph[7],
    unsigned scale, uint32_t fg, uint32_t bg) {
    unsigned x = (2u + col * 6u) * scale, y = (2u + row * 9u) * scale;
    for (unsigned r = 0u; r < 7u; r++) for (unsigned sy = 0u; sy < scale; sy++) {
        uint32_t *dst = out.origin + (ptrdiff_t)(y + r * scale + sy) * out.dy + (ptrdiff_t)x * out.dx;
        for (unsigned c = 0u; c < 6u; c++) {
            uint32_t color = c < 5u && (glyph[r] & (16u >> c)) ? fg : bg;
            for (unsigned sx = 0u; sx < scale; sx++) { *dst = color; dst += out.dx; }
        }
    }
}

static int w40_test_fill_and_raster(void) {
    enum { W = 128, H = 64, N = W * H };
    const uint32_t bg = 0xff100c0cu, fg = 0xffe0f0e0u;
    uint32_t *a = (uint32_t *)malloc((size_t)N * 4u);
    uint32_t *b = (uint32_t *)malloc((size_t)N * 4u);
    assert(a && b);
    for (int rot = 0; rot < 4; rot++) {
        for (unsigned scale = 1u; scale <= 3u; scale++) {
            for (int skip_bg = 0; skip_bg <= 1; skip_bg++) {
                for (size_t i = 0u; i < N; i++) a[i] = bg;
                totk_ovl_fill_u32(b, N, bg);
                if (memcmp(a, b, (size_t)N * 4u) != 0) { free(a); free(b); return 1; }
                w40_rt ra = w40_rt_make(a, W, H, rot), rb = w40_rt_make(b, W, H, rot);
                for (unsigned g = 0u; g < 4u; g++) {
                    w40_slow_cell(ra, 0u, g, w40_glyphs[g], scale, fg, bg);
                    unsigned x = (2u + g * 6u) * scale, y = 2u * scale;
                    totk_ovl_raster_cell_fast(rb.origin, rb.dx, rb.dy, x, y, w40_glyphs[g], scale, 6u, fg, bg, skip_bg);
                }
                if (memcmp(a, b, (size_t)N * 4u) != 0) { free(a); free(b); return 1; }
            }
        }
    }
    free(a); free(b);
    return 0;
}

static void w40_cleanup_tmp(const char *dir) {
    const char *names[3] = {"good_int", "junk", "nanval"};
    char fp[512];
    for (int n = 0; n < 3; n++) { snprintf(fp, sizeof fp, "%s/%s", dir, names[n]); remove(fp); }
    rmdir(dir);
}

static int w40_test_sysfs_and_proc_parsers(void) {
    char dir[] = "/tmp/w40_aoc_XXXXXX";
    assert(mkdtemp(dir) != NULL);
    char fp[512];
    snprintf(fp, sizeof fp, "%s/good_int", dir);
    FILE *f = fopen(fp, "w"); assert(f); fputs("1800000\n", f); fclose(f);
    double v = 0.0;
    if (totk_ovl_read_number_fast(dir, "good_int", &v) != 1 || v != 1800000.0) { w40_cleanup_tmp(dir); return 1; }
    snprintf(fp, sizeof fp, "%s/junk", dir);
    f = fopen(fp, "w"); assert(f); fputs("1500000junk\n", f); fclose(f);
    if (totk_ovl_read_number_fast(dir, "junk", &v) != 0) { w40_cleanup_tmp(dir); return 1; }
    snprintf(fp, sizeof fp, "%s/nanval", dir);
    f = fopen(fp, "w"); assert(f); fputs("nan\n", f); fclose(f);
    if (totk_ovl_read_number_fast(dir, "nanval", &v) != 0) { w40_cleanup_tmp(dir); return 1; }
    w40_cleanup_tmp(dir);
    const char *good_mem = "MemTotal:       16384000 kB\nMemFree:         2048000 kB\nMemAvailable:    8192000.0 kB\n";
    unsigned mt = 0u, ma = 0u;
    if (!totk_ovl_parse_meminfo_buf(good_mem, strlen(good_mem), &mt, &ma) || mt != 16000u || ma != 8000u) return 1;
    const char *dup_mem = "MemTotal:       16384000 kB\nMemTotal:       16384000 kB\nMemAvailable:    8192000 kB\n";
    if (totk_ovl_parse_meminfo_buf(dup_mem, strlen(dup_mem), &mt, &ma) != 0) return 1;
    const char *good_st = "Name:\tgame\nVmPeak:\t1024 kB\nVmRSS:     512000 kB\n";
    unsigned rss = 0u;
    if (!totk_ovl_parse_status_rss_buf(good_st, strlen(good_st), &rss) || rss != 500u) return 1;
    const char *bad_st = "VmRSS: 512000 MB\n";
    if (totk_ovl_parse_status_rss_buf(bad_st, strlen(bad_st), &rss) != 0) return 1;
    return 0;
}

static void *w40_aoc_worker(void *arg) {
    (void)arg;
    uint32_t px[128 * 32];
    const char *mem = "MemTotal: 8388608 kB\nMemAvailable: 4194304 kB\n";
    for (int i = 0; i < 400; i++) {
        totk_ovl_fill_u32(px, 128u * 32u, 0xff100c0cu);
        totk_ovl_raster_cell_fast(px, 1, 128, 2u, 2u, w40_glyphs[i & 3], 1u, 6u, 0xffe0f0e0u, 0xff100c0cu, 1);
        unsigned mt = 0u, ma = 0u;
        assert(totk_ovl_parse_meminfo_buf(mem, strlen(mem), &mt, &ma));
    }
    return NULL;
}

static int w40_run_aoc_suite(int fault) {
    totk_vk_ovl_fast_reset_for_test(1);
    totk_w40_set_fault(fault);
    if (w40_test_fill_and_raster() != 0) return 1;
    if (w40_test_sysfs_and_proc_parsers() != 0) return 1;
    if (fault == 0) {
        pthread_t th[4];
        for (int t = 0; t < 4; t++) assert(pthread_create(&th[t], NULL, w40_aoc_worker, NULL) == 0);
        for (int t = 0; t < 4; t++) assert(pthread_join(th[t], NULL) == 0);
    }
    totk_w40_set_fault(0);
    return 0;
}

static int w40_slow_sscanf_meminfo(const char *buf, unsigned *mt, unsigned *ma) {
    const char *p = buf;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t ll = nl ? (size_t)(nl - p) : strlen(p);
        char ln[256]; size_t cl = ll < 255u ? ll : 255u;
        memcpy(ln, p, cl); ln[cl] = '\0';
        double kb = 0.0; char unit[8], extra;
        if (!strncmp(ln, "MemTotal:", 9) && sscanf(ln + 9, "%lf %7s %c", &kb, unit, &extra) == 2 && !strcmp(unit, "kB"))
            *mt = (unsigned)(kb / 1024.0);
        else if (!strncmp(ln, "MemAvailable:", 13) && sscanf(ln + 13, "%lf %7s %c", &kb, unit, &extra) == 2 && !strcmp(unit, "kB"))
            *ma = (unsigned)(kb / 1024.0);
        p = nl ? nl + 1 : p + ll;
    }
    return 1;
}

static double w40_aoc_bench(int fast_on, uint64_t *sink) {
    enum { W = 320, H = 96 };
    uint32_t *px = (uint32_t *)malloc((size_t)W * H * 4u);
    assert(px);
    const char *mem = "MemTotal:       16384000 kB\nMemFree:         2048000 kB\nMemAvailable:    8192000 kB\n";
    size_t mem_len = strlen(mem);
    totk_vk_ovl_fast_reset_for_test(fast_on);
    uint64_t acc = 0u, t0 = w40_aoc_ns();
    for (int it = 0; it < 2000; it++) {
        if (fast_on) totk_ovl_fill_u32(px, (size_t)W * H, 0xff100c0cu);
        else for (size_t i = 0u; i < (size_t)W * H; i++) px[i] = 0xff100c0cu;
        for (unsigned c = 0u; c < 12u; c++) {
            if (fast_on)
                totk_ovl_raster_cell_fast(px, 1, (ptrdiff_t)W, (2u + c * 6u) * 2u, 4u, w40_glyphs[c & 3u], 2u, 6u, 0xffe0f0e0u, 0xff100c0cu, 1);
            else
                w40_slow_cell((w40_rt){px, 1, (ptrdiff_t)W}, 0u, c, w40_glyphs[c & 3u], 2u, 0xffe0f0e0u, 0xff100c0cu);
        }
        unsigned mt = 0u, ma = 0u;
        if (fast_on) assert(totk_ovl_parse_meminfo_buf(mem, mem_len, &mt, &ma));
        else assert(w40_slow_sscanf_meminfo(mem, &mt, &ma));
        acc += (uint64_t)px[2u * 2u * W + 16u] + mt + ma;
    }
    double ms = (double)(w40_aoc_ns() - t0) / 1e6;
    free(px); *sink += acc;
    return ms;
}

static int run_ktgl_w40_tests(int fault_mode) {
    totk_vk_ovl_fast_reset_for_test(-1);
    const char *env = getenv("TOTK_VK_OVL_FAST");
    int req_off = (env && (env[0] == '0' || env[0] == 'f' || env[0] == 'F' || env[0] == 'n' || env[0] == 'N'));
    int ext_f = totk_w40_fault();
    int eff = (fault_mode >= 146 && fault_mode <= 150) ? (fault_mode - 145) : ext_f;
    if (eff > 0) return w40_run_aoc_suite(eff) != 0 ? 1 : 0;
    assert(w40_run_aoc_suite(0) == 0);
    for (int f = 1; f <= 5; f++) assert(w40_run_aoc_suite(f) != 0);
    totk_w40_set_fault(0);
    uint64_t s0 = 0u, s1 = 0u;
    double ms0 = w40_aoc_bench(0, &s0), ms1 = w40_aoc_bench(1, &s1);
    assert(s0 == s1);
    totk_vk_ovl_fast_reset_for_test(-1);
    printf("ktglopttest w40: req_off=%d off_ms=%.3f on_ms=%.3f ratio=%.2fx sink=%llu\n",
           req_off, ms0, ms1, ms1 > 0.0 ? ms0 / ms1 : 0.0, (unsigned long long)s1);
    return 0;
}
#endif
