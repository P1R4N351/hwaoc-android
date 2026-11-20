#ifndef TOTK_TEST_VK_KTGL_OPT_W5_H
#define TOTK_TEST_VK_KTGL_OPT_W5_H
#include "vk_ktgl_memo.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint64_t host_now_ns(void);

typedef struct { uint64_t lo, end, phys; } w5_span;
typedef struct { uint64_t lo, hi, end_va; int bounded; } w5_iv_oracle;

static uint64_t w5_ref_lag_bound(const w5_span *al, int na, const w5_span *po, int np, uint64_t addr, uint64_t limit) {
    uint64_t physical = addr, bound = UINT64_MAX;
    for (int i = 0; i < na; i++) {
        if (addr >= al[i].lo && addr < al[i].end) {
            physical = al[i].phys + (addr - al[i].lo);
            bound = al[i].end - addr;
            break;
        }
    }
    for (int i = 0; i < np; i++) {
        if (physical >= po[i].lo && physical < po[i].end && po[i].end - physical < bound) bound = po[i].end - physical;
    }
    return limit < bound ? limit : bound;
}

static uint64_t w5_thresh(uint64_t addr, uint64_t phys, uint64_t edge) {
    if (edge <= phys) { uint64_t d = phys - edge; return d <= addr ? addr - d : 0; }
    uint64_t d = edge - phys;
    return d <= UINT64_MAX - addr ? addr + d : UINT64_MAX;
}

static void w5_iv_scan(const w5_span *al, int na, const w5_span *po, int np, uint64_t addr,
                       uint64_t *ilo, uint64_t *ihi, uint64_t *eva, uint8_t *bnd) {
    uint64_t iv_lo = 0, iv_hi = UINT64_MAX, phys = addr, end_va = UINT64_MAX;
    uint8_t bounded = 0;
    for (int i = 0; i < na; i++) {
        if (addr >= al[i].lo && addr < al[i].end) {
            if (al[i].lo > iv_lo) iv_lo = al[i].lo;
            if (al[i].end < iv_hi) iv_hi = al[i].end;
            phys = al[i].phys + (addr - al[i].lo);
            end_va = al[i].end; bounded = 1; break;
        }
        if (al[i].end <= addr) { if (al[i].end > iv_lo) iv_lo = al[i].end; }
        else if (al[i].lo > addr && al[i].lo < iv_hi) iv_hi = al[i].lo;
    }
    for (int i = 0; i < np; i++) {
        if (po[i].end <= phys) { uint64_t t = w5_thresh(addr, phys, po[i].end); if (t > iv_lo) iv_lo = t; }
        else if (po[i].lo > phys) { uint64_t t = w5_thresh(addr, phys, po[i].lo); if (t < iv_hi) iv_hi = t; }
        else {
            uint64_t tlo = w5_thresh(addr, phys, po[i].lo), thi = w5_thresh(addr, phys, po[i].end);
            if (tlo > iv_lo) iv_lo = tlo;
            if (thi < iv_hi) iv_hi = thi;
            if (!bounded || thi < end_va) { end_va = thi; bounded = 1; }
        }
    }
    *ilo = iv_lo; *ihi = iv_hi; *eva = end_va; *bnd = bounded;
}

static int run_w5_lag_iv_test(int fault_mode) {
    int fails = 0;
    w5_span al[2] = {
        {0x40000000ULL, 0x42000000ULL, 0x10000000ULL},
        {0x41000000ULL, 0x43000000ULL, 0x14000000ULL},
    };
    w5_span po[4] = {
        {0x10000000ULL, 0x18000000ULL, 0},
        {0x10800000ULL, 0x11000000ULL, 0},   /* nested sub-pool */
        {0x14000000ULL, 0x15000000ULL, 0},   /* overlaps second alias */
        {0x80000000ULL, 0x84000000ULL, 0},   /* direct unaliased pool */
    };
    vk_lag_iv_cache c;
    memset(&c, 0, sizeof c);
    uint64_t gen = 1, leg_miss = 0, iv_miss = 0;
    struct { uint64_t addr, gen, bound; } leg[1024];
    memset(leg, 0, sizeof leg);

    uint64_t probes[12] = {
        0x3ffffff0ULL, 0x40000000ULL, 0x407fffffULL, 0x40800000ULL,
        0x40ffffffULL, 0x41000000ULL, 0x42000000ULL, 0x42000010ULL,
        0x43000000ULL, 0x80000000ULL, 0x83ffffffULL, 0x84000000ULL
    };
    for (int i = 0; i < 50000; i++) {
        if (i == 25000) { po[1].end = 0x10c00000ULL; gen++; }
        uint64_t addr = (i < 12) ? probes[i] :
            (i & 3) == 0 ? (0x40000000ULL + ((uint64_t)(i * 64u) & 0x02ffffffULL)) :
            (i & 3) == 1 ? (0x80000000ULL + ((uint64_t)(i * 256u) & 0x03ffffffULL)) :
            (i & 3) == 2 ? (0x10800000ULL + ((uint64_t)(i * 128u) & 0x00ffffffULL)) :
            (0x3f000000ULL + ((uint64_t)(i * 4096u) & 0x05ffffffULL));
        uint64_t want = w5_ref_lag_bound(al, 2, po, 4, addr, 0x10000000ULL);
        uint64_t b = UINT64_MAX;
        if (!vk_lag_iv_get(&c, fault_mode == 10 ? 1u : gen, addr, &b)) {
            iv_miss++;
            uint64_t ilo = 0, ihi = 0, eva = UINT64_MAX; uint8_t bnd = 0;
            w5_iv_scan(al, 2, po, 4, addr, &ilo, &ihi, &eva, &bnd);
            if (fault_mode == 9) ihi += 0x1000000ULL;
            if (ilo <= addr && addr < ihi) vk_lag_iv_put(&c, gen, ilo, ihi, eva, bnd);
            b = bnd ? (eva - addr) : UINT64_MAX;
        }
        uint64_t got = 0x10000000ULL < b ? 0x10000000ULL : b;
        if (got != want) {
            fprintf(stderr, "FAIL lag_iv step=%d gen=%llu addr=%#llx want=%#llx got=%#llx\n",
                    i, (unsigned long long)gen, (unsigned long long)addr,
                    (unsigned long long)want, (unsigned long long)got);
            fails++; break;
        }
        unsigned h = (unsigned)((addr >> 4) ^ (addr >> 20)) % 1024u;
        if (!(leg[h].addr == addr && leg[h].gen == gen)) {
            leg_miss++;
            leg[h].addr = addr; leg[h].gen = gen; leg[h].bound = want;
        }
    }
    printf("aoc_lag_iv: ops=50000 legacy_miss=%llu iv_miss=%llu (%.1fx fewer locks) fails=%d\n",
           (unsigned long long)leg_miss, (unsigned long long)iv_miss,
           iv_miss ? (double)leg_miss / (double)iv_miss : 0.0, fails);
    return fails;
}

typedef struct { uintptr_t lo, end; int best; } w5_seg;

static int w5_seg_match(const w5_seg *seg, int n, uintptr_t a, int h) {
    return h > 0 && h < n && seg[h - 1].lo <= a && a < seg[h].lo;
}

static int w5_seg_bsearch(const w5_seg *seg, int n, uintptr_t a) {
    int lo = 0, hi = n;
    while (lo < hi) { int m = (lo + hi) >> 1; if (seg[m].lo <= a) lo = m + 1; else hi = m; }
    return lo;
}

static int run_w5_hints64_test(int fault_mode) {
    enum { NSEGS = 512, NLOOKUPS = 200000 };
    w5_seg seg[NSEGS + 1];
    for (int i = 0; i <= NSEGS; i++) {
        seg[i].lo = (uintptr_t)0x10000000ULL + (uintptr_t)i * (uintptr_t)0x04000000ULL;
        seg[i].end = seg[i].lo + (uintptr_t)0x04000000ULL;
        seg[i].best = i < NSEGS ? i : -1;
    }
    int leg_hint[64] = {0}, fast_hint[64] = {0};
    uint64_t leg_bs = 0, fast_bs = 0, sink1 = 0, sink2 = 0;

    uint64_t t0 = host_now_ns();
    for (int i = 0; i < NLOOKUPS; i++) {
        /* 8 active 64 MiB-aliased pools + streaming 4 KiB steps across 512 segments */
        int p = i & 7;
        uintptr_t a = seg[p].lo + (((uintptr_t)(i >> 3) * 4096u) & 0x000ff000ULL);
        unsigned h = (unsigned)((a >> 12) ^ (a >> 24)) & 63u;
        int lo = leg_hint[h];
        if (!w5_seg_match(seg, NSEGS + 1, a, lo)) { leg_bs++; lo = w5_seg_bsearch(seg, NSEGS + 1, a); leg_hint[h] = lo; }
        sink1 += (uint64_t)seg[lo - 1].best;
    }
    uint64_t dt_leg = host_now_ns() - t0;

    t0 = host_now_ns();
    for (int i = 0; i < NLOOKUPS; i++) {
        int p = i & 7;
        uintptr_t a = seg[p].lo + (((uintptr_t)(i >> 3) * 4096u) & 0x000ff000ULL);
        int *w = &fast_hint[vk_ktgl_hint_set4(a)];
        int lo = 0;
        if (w5_seg_match(seg, NSEGS + 1, a, w[0])) lo = w[0];
        else if (w5_seg_match(seg, NSEGS + 1, a, w[1])) lo = w[1];
        else if (w5_seg_match(seg, NSEGS + 1, a, w[2])) lo = w[2];
        else if (w5_seg_match(seg, NSEGS + 1, a, w[3])) lo = w[3];
        if (!lo) {
            fast_bs++;
            lo = w5_seg_bsearch(seg, NSEGS + 1, a);
            w[3] = w[2]; w[2] = w[1]; w[1] = w[0]; w[0] = lo;
        }
        if (fault_mode == 11 && i == 1000) lo = 2;
        sink2 += (uint64_t)seg[lo - 1].best;
    }
    uint64_t dt_fast = host_now_ns() - t0;

    int fails = (sink1 != sink2);
    printf("aoc_hints64: ops=%d leg_bs=%llu fast_bs=%llu leg_ms=%.2f (%.2f ns/op) fast_ms=%.2f (%.2f ns/op) speedup=%.2fx\n",
           NLOOKUPS, (unsigned long long)leg_bs, (unsigned long long)fast_bs,
           dt_leg / 1e6, (double)dt_leg / NLOOKUPS,
           dt_fast / 1e6, (double)dt_fast / NLOOKUPS,
           dt_fast ? (double)dt_leg / (double)dt_fast : 0.0);
    return fails;
}

static int run_ktgl_w5_tests(int fault_mode) {
    int fails = 0;
    fails += run_w5_lag_iv_test(fault_mode);
    fails += run_w5_hints64_test(fault_mode);
    return fails;
}
#endif
