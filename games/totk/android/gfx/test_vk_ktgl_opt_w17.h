#ifndef TEST_VK_KTGL_OPT_W17_H
#define TEST_VK_KTGL_OPT_W17_H

#include "vk_ub_fast.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern uint64_t host_now_ns(void);
static volatile uint64_t w17_sink;

static int test_w17_bloom_and_align(void) {
    const uint64_t al[4] = {16u, 64u, 256u, 48u};
    for (int i = 0; i < 4; i++) {
        for (uint64_t u = 0; u < 512u; u += 11u) {
            if (totk_align_up_sz(u, al[i]) != (u + al[i] - 1u) / al[i] * al[i]) return 1;
        }
    }
    uint64_t upd_lo = 0x2000f000ull, upd_hi = 0x20011000ull;
    uint64_t blm = totk_ub_page_bloom(upd_lo, upd_hi);
    if ((blm & totk_ub_page_bloom(0x20010000ull, 0x20010100ull)) == 0u) return 1;
    assert((blm & totk_ub_page_bloom(0x20020000ull, 0x20020100ull)) == 0u);
    totk_snap_memo4 sm = {0};
    totk_snap_memo4_put(&sm, 0x20000000ull, 256u, 3u, 5);
    int32_t sidx = -1;
    if (!totk_snap_memo4_get(&sm, 0x20000000ull, 256u, 3u, &sidx) || sidx != 5) return 1;
    if (totk_snap_memo4_get(&sm, 0x20000000ull, 256u, 4u, &sidx)) return 1;
    return 0;
}

static int test_w17_rename_and_ptr_faults(void) {
    static totk_ub_rename_memo4 rm;
    static totk_ub_ptr_memo4 pm;
    memset(&rm, 0, sizeof rm);
    memset(&pm, 0, sizeof pm);
    uint64_t base = 0x30000100ull, ren_a[4] = {0}, ptr_a[4] = {0};
    int gr = 0, gp = 0;
    for (uint64_t k = 0; k < 4096u && (gr < 4 || gp < 4); k++) {
        uint64_t cand = base + (k << 16);
        if (gr < 4 && totk_ub_ren_set_idx(cand, 256u) == totk_ub_ren_set_idx(base, 256u)) ren_a[gr++] = cand;
        if (gp < 4 && totk_ub_ptr_set_idx(cand, 16u) == totk_ub_ptr_set_idx(base, 16u)) ptr_a[gp++] = cand;
    }
    assert(gr == 4 && gp == 4);
    assert(totk_ub_ren_set_idx(ren_a[0], 256u) == totk_ub_ren_set_idx(ren_a[1], 256u));
    assert(totk_ub_ptr_set_idx(ptr_a[0], 16u) == totk_ub_ptr_set_idx(ptr_a[1], 16u));
    for (int i = 0; i < 4; i++)
        totk_ub_ren_put(&rm, ren_a[i], 256u, 10u, 1u, 99u, 1, 0x5000u + (uint64_t)i, 64u, 256u);
    int r = 0; uint64_t b = 0, o = 0, rng = 0;
    if (totk_ub_ren_get(&rm, ren_a[0], 256u, 11u, 1u, 99u, &r, &b, &o, &rng)) return 1;
    for (int i = 0; i < 4; i++)
        if (!totk_ub_ren_get(&rm, ren_a[i], 256u, 10u, 1u, 99u, &r, &b, &o, &rng)) return 1;
    uint8_t val[16] = {1, 2, 3, 4};
    totk_ub_ptr_put(&pm, ptr_a[0], 16u, 7u, 1u, 99u, val);
    if (totk_ub_ptr_find(&pm, ptr_a[1], 16u, 1u, 99u) != NULL) return 1;
    if (totk_ub_ptr_find(&pm, ptr_a[0], 16u, 1u, 99u) == NULL) return 1;
    return 0;
}

static void bench_w17_snap_bloom(void) {
    enum { NQ = 200000, NUB = 64 };
    typedef struct { uint64_t lo, hi, seq; } sim_ub;
    static sim_ub ubs[NUB];
    static uint64_t maxhi[NUB];
    static totk_snap_memo4 sm;
    memset(&sm, 0, sizeof sm);
    uint64_t upd_lo = 0x20000000ull, upd_hi = 0x20000400ull, snap_seq = 5u;
    uint64_t upd_bloom = totk_ub_page_bloom(upd_lo, upd_hi);
    for (int i = 0; i < NUB; i++) {
        ubs[i] = (sim_ub){0x20000000ull + (uint64_t)i * 0x8000ull, 0x20000400ull + (uint64_t)i * 0x8000ull, i ? 2u : 9u};
        maxhi[i] = ubs[i].hi;
    }
    totk_snap_memo4_put(&sm, 0x20001000ull, 256u, 1u, 0);
    totk_snap_memo4_put(&sm, 0x20002000ull, 256u, 1u, 0);
    totk_snap_memo4_put(&sm, 0x20003000ull, 256u, 1u, 0);
    totk_snap_memo4_put(&sm, 0x20004000ull, 256u, 1u, 0);
    uint64_t t0 = host_now_ns(), hits_b = 0;
    for (int q = 0; q < NQ; q++) {
        uint64_t addr = 0x20001000ull + (((uint64_t)q ^ (w17_sink & 0u)) & 3u) * 0x1000ull, end = addr + 256u;
        int a = 0, b = NUB;
        while (a < b) { int m = (a + b) >> 1; if (ubs[m].lo < end) a = m + 1; else b = m; }
        for (int i = a - 1; i >= 0 && maxhi[i] > addr; i--) if (ubs[i].hi > addr && ubs[i].seq > snap_seq) { hits_b++; break; }
    }
    uint64_t ns0 = host_now_ns() - t0;
    uint64_t t1 = host_now_ns(), hits_f = 0;
    for (int q = 0; q < NQ; q++) {
        uint64_t addr = 0x20001000ull + (((uint64_t)q ^ (w17_sink & 0u)) & 3u) * 0x1000ull, end = addr + 256u;
        int32_t idx = -1;
        if (totk_snap_memo4_get(&sm, addr, 256u, 1u, &idx)) {
            if (!(end <= upd_lo || addr >= upd_hi || !(upd_bloom & totk_ub_page_bloom(addr, end)))) hits_f++;
        }
    }
    uint64_t ns1 = host_now_ns() - t1;
    w17_sink += hits_b + hits_f;
    double ms0 = (double)ns0 * 1e-6, ms1 = (double)ns1 * 1e-6;
    printf("aoc_w17_snap_bloom: base=%.3f ms fast=%.3f ms speedup=%.2fx\n", ms0, ms1, ms0 / (ms1 > 0.001 ? ms1 : 0.001));
}

typedef struct { uint64_t lo, hi, seq; } sim_ub_w17;

static inline int sim_ub_overlaps_w17(const sim_ub_w17 *ubs, const uint64_t *maxhi,
                                      int nub, uint64_t lo, uint64_t hi) {
    assert(ubs != NULL && maxhi != NULL && nub > 0);
    int a = 0, b = nub;
    while (a < b) {
        int m = (a + b) >> 1;
        if (ubs[m].lo < hi) a = m + 1;
        else b = m;
    }
    int n = 0;
    for (int i = a - 1; i >= 0 && maxhi[i] > lo && n < 8; i--) {
        if (ubs[i].hi > lo) n++;
    }
    return n;
}

static void bench_w17_ub_memo4(void) {
    enum { NQ = 200000, NUB = 64 };
    static totk_ub_rename_memo4 rm;
    static struct { uint64_t addr, size, gen, sub; const void *sn; int ret; uint64_t buf; } m1[1024];
    static sim_ub_w17 ubs[NUB];
    static uint64_t maxhi[NUB];
    memset(&rm, 0, sizeof rm);
    memset(m1, 0, sizeof m1);
    uint64_t a[4] = {0}, base = 0x40000000ull;
    int got = 0;
    unsigned h0 = (unsigned)((base >> 4) ^ (base >> 14) ^ (256u * 2654435761u)) % 1024u;
    for (uint64_t k = 0; k < 262144u && got < 4; k++) {
        uint64_t c = base + k * 0x1000ull;
        unsigned hc = (unsigned)((c >> 4) ^ (c >> 14) ^ (256u * 2654435761u)) % 1024u;
        if (hc == h0) a[got++] = c;
    }
    assert(got == 4);
    for (int i = 0; i < NUB; i++) {
        ubs[i] = (sim_ub_w17){base + (uint64_t)i * 0x100000ull, base + (uint64_t)i * 0x100000ull + 0x100ull, (uint64_t)(i + 1)};
        maxhi[i] = ubs[i].hi;
    }
    uint64_t t0 = host_now_ns(), miss1 = 0, ov1 = 0;
    for (int i = 0; i < NQ; i++) {
        uint64_t ad = a[i & 3] + (w17_sink & 0u);
        unsigned h = (unsigned)((ad >> 4) ^ (ad >> 14) ^ (256u * 2654435761u)) % 1024u;
        if (!(m1[h].addr == ad && m1[h].size == 256u && m1[h].gen == 1u && m1[h].sub == 1u)) {
            int ov = sim_ub_overlaps_w17(ubs, maxhi, NUB, ad, ad + 256u);
            m1[h].addr = ad; m1[h].size = 256u; m1[h].gen = 1u; m1[h].sub = 1u; m1[h].ret = ov ? 2 : 1; m1[h].buf = 0x800u;
            ov1 += (uint64_t)m1[h].ret; miss1++;
        }
    }
    uint64_t ns0 = host_now_ns() - t0;
    uint64_t t1 = host_now_ns(), miss4 = 0, ov4 = 0;
    for (int i = 0; i < NQ; i++) {
        uint64_t ad = a[i & 3] + (w17_sink & 0u), bu = 0, of = 0, rn = 0; int ret = 0;
        if (!totk_ub_ren_get(&rm, ad, 256u, 1u, 1u, 1u, &ret, &bu, &of, &rn)) {
            int ov = sim_ub_overlaps_w17(ubs, maxhi, NUB, ad, ad + 256u);
            totk_ub_ren_put(&rm, ad, 256u, 1u, 1u, 1u, ov ? 2 : 1, 0x800u, 0u, 256u);
            miss4++; ret = ov ? 2 : 1;
        }
        ov4 += (uint64_t)ret;
    }
    uint64_t ns1 = host_now_ns() - t1;
    w17_sink += miss1 + miss4 + ov1 + ov4;
    double ms0 = (double)ns0 * 1e-6, ms1 = (double)ns1 * 1e-6;
    printf("aoc_w17_ub_memo4: base=%.3f ms fast=%.3f ms speedup=%.2fx (misses=%llu->%llu)\n",
           ms0, ms1, ms0 / (ms1 > 0.001 ? ms1 : 0.001), (unsigned long long)miss1, (unsigned long long)miss4);
}

static int run_ktgl_w17_tests(int fault_mode) {
    (void)fault_mode;
    if (test_w17_bloom_and_align() != 0) { fprintf(stderr, "FAIL w17 bloom_and_align\n"); return 1; }
    if (test_w17_rename_and_ptr_faults() != 0) { fprintf(stderr, "FAIL w17 rename_and_ptr\n"); return 1; }
    if (!fault_mode) { bench_w17_snap_bloom(); bench_w17_ub_memo4(); }
    return 0;
}

#endif
