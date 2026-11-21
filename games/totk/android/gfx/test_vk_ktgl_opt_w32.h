#ifndef TEST_VK_KTGL_OPT_W32_H
#define TEST_VK_KTGL_OPT_W32_H
#include "vk_imgmem_fast.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint64_t host_now_ns(void);

enum { W32_AOC_BLK = 12, W32_AOC_BLOCK_SZ = 64u << 20, W32_AOC_LIVE = 1800 };

static inline uint64_t w32_aoc_rng(uint64_t *s) {
    uint64_t x = *s;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *s = x;
    return x * UINT64_C(0x2545f4914f6cdd1d);
}

static void w32_ref_insert(totk_mem_block *b, uint64_t off, uint64_t size) {
    int i = 0;
    while (i < b->nfree && b->free[i].off < off) i++;
    assert(totk_imgmem_ensure_room(b, b->nfree + 1) == 1);
    if (b->nfree - i > 0) memmove(b->free + i + 1, b->free + i, (size_t)(b->nfree - i) * sizeof *b->free);
    b->free[i] = (totk_mem_span){off, size};
    b->nfree++;
    if (i + 1 < b->nfree && b->free[i].off + b->free[i].size == b->free[i + 1].off) {
        b->free[i].size += b->free[i + 1].size;
        if (b->nfree - i - 2 > 0) memmove(b->free + i + 1, b->free + i + 2, (size_t)(b->nfree - i - 2) * sizeof *b->free);
        b->nfree--;
    }
    if (i > 0 && b->free[i - 1].off + b->free[i - 1].size == b->free[i].off) {
        b->free[i - 1].size += b->free[i].size;
        if (b->nfree - i - 1 > 0) memmove(b->free + i, b->free + i + 1, (size_t)(b->nfree - i - 1) * sizeof *b->free);
        b->nfree--;
    }
    b->max_valid = 0u;
}

static int w32_ref_alloc(totk_mem_block *blocks, int nblocks, uint32_t type,
                         uint64_t size, uint64_t align, int *out_bi, uint64_t *out_off) {
    for (int bi = 0; bi < nblocks; bi++) {
        totk_mem_block *b = &blocks[bi];
        if (b->type != type) continue;
        for (int i = 0; i < b->nfree; i++) {
            uint64_t at = (b->free[i].off + align - 1u) / align * align;
            uint64_t end = b->free[i].off + b->free[i].size;
            if (at + size > end) continue;
            totk_mem_span f = b->free[i];
            if (b->nfree - i - 1 > 0) memmove(b->free + i, b->free + i + 1, (size_t)(b->nfree - i - 1) * sizeof *b->free);
            b->nfree--;
            if (at > f.off) w32_ref_insert(b, f.off, at - f.off);
            if (at + size < end) w32_ref_insert(b, at + size, end - at - size);
            b->used += size;
            *out_bi = bi; *out_off = at;
            return 1;
        }
    }
    return 0;
}

typedef struct { int bi; uint64_t off, size; } w32_aoc_rec;

static void w32_aoc_init_pool(totk_mem_block blk[W32_AOC_BLK], uint64_t mask[32],
                              uint64_t (*kept_off)[400], uint64_t (*kept_sz)[400], int kept_n[W32_AOC_BLK], uint64_t *rng) {
    memset(mask, 0, 32u * sizeof *mask);
    for (int b = 0; b < W32_AOC_BLK; b++) {
        memset(&blk[b], 0, sizeof blk[b]);
        blk[b].type = (uint32_t)(b < 8 ? (b & 1) : 2);
        mask[blk[b].type] |= UINT64_C(1) << b;
        assert(totk_imgmem_block_free(&blk[b], 0u, W32_AOC_BLOCK_SZ, 0) == 1);
        kept_n[b] = 0;
        if (b < W32_AOC_BLK - 2) {
            uint64_t ao[320], asz[320];
            int k = 0;
            for (int s = 0; s < 320; s++) {
                uint64_t hsz = b >= 9 ? (UINT64_C(4096) * (4u + (w32_aoc_rng(rng) % 5u))) : (UINT64_C(256) * (1u + (w32_aoc_rng(rng) % 15u)));
                uint64_t sz = (s & 1) ? UINT64_C(4096) : hsz;
                uint64_t off = 0u;
                int bi = -1;
                assert(w32_ref_alloc(&blk[b], 1, blk[b].type, sz, 256u, &bi, &off) == 1);
                ao[k] = off; asz[k] = sz; k++;
            }
            for (int s = 0; s < k; s += 2) w32_ref_insert(&blk[b], ao[s], asz[s]);
            for (int s = 1; s < k; s += 2) {
                int idx = kept_n[b]++;
                kept_off[b][idx] = ao[s]; kept_sz[b][idx] = asz[s];
                blk[b].used += asz[s];
            }
            blk[b].used = 0u;
            for (int s = 0; s < kept_n[b]; s++) blk[b].used += kept_sz[b][s];
            uint64_t tail = blk[b].nfree ? blk[b].free[blk[b].nfree - 1].size : 0u;
            if (tail > UINT64_C(8192)) {
                uint64_t csz = tail - UINT64_C(4096), off = 0u;
                int bi = -1;
                assert(w32_ref_alloc(&blk[b], 1, blk[b].type, csz, 256u, &bi, &off) == 1);
                int idx = kept_n[b]++;
                kept_off[b][idx] = off; kept_sz[b][idx] = csz;
            }
        }
    }
}

static void w32_aoc_free_pool(totk_mem_block blk[W32_AOC_BLK], uint64_t (*kept_off)[400], uint64_t (*kept_sz)[400], int kept_n[W32_AOC_BLK]) {
    for (int b = 0; b < W32_AOC_BLK; b++) {
        for (int k = 0; k < kept_n[b]; k++) {
            assert(totk_imgmem_block_free(&blk[b], kept_off[b][k], kept_sz[b][k], 0) == 1);
            blk[b].used -= kept_sz[b][k];
        }
        assert(blk[b].used == 0u && blk[b].nfree == 1 && blk[b].free[0].off == 0u && blk[b].free[0].size == W32_AOC_BLOCK_SZ);
        free(blk[b].free);
    }
}

static uint64_t w32_aoc_run_churn(int fast_on, int ops, int fault_mode) {
    totk_mem_block blk[W32_AOC_BLK];
    uint64_t mask[32];
    uint64_t (*kept_off)[400] = calloc(W32_AOC_BLK, sizeof *kept_off);
    uint64_t (*kept_sz)[400] = calloc(W32_AOC_BLK, sizeof *kept_sz);
    int kept_n[W32_AOC_BLK] = {0};
    w32_aoc_rec *live = calloc(W32_AOC_LIVE, sizeof *live);
    assert(kept_off != NULL && kept_sz != NULL && live != NULL);
    uint64_t rng = UINT64_C(0x9e3779b97f4a7c15), sink = 0u;
    w32_aoc_init_pool(blk, mask, kept_off, kept_sz, kept_n, &rng);
    for (int b = 0; b < W32_AOC_BLK; b++) {
        uint64_t mx = 0u;
        for (int s = 0; s < blk[b].nfree; s++) if (blk[b].free[s].size > mx) mx = blk[b].free[s].size;
        blk[b].max_free = mx; blk[b].max_valid = 1u;
    }
    int nlive = 0;
    for (int step = 0; step < ops; step++) {
        if (nlive >= W32_AOC_LIVE - 4 || (nlive > 64 && (w32_aoc_rng(&rng) & 3u) == 0u)) {
            int pos = (int)(w32_aoc_rng(&rng) % (uint64_t)nlive);
            w32_aoc_rec r = live[pos];
            live[pos] = live[--nlive];
            if (fast_on) assert(totk_imgmem_block_free(&blk[r.bi], r.off, r.size, fault_mode) == 1);
            else w32_ref_insert(&blk[r.bi], r.off, r.size);
            blk[r.bi].used -= r.size;
            sink ^= (uint64_t)r.bi * 31u + r.off;
            continue;
        }
        uint32_t ty = 2u;
        uint64_t sz = UINT64_C(4096) * (4u + (w32_aoc_rng(&rng) % 24u));
        uint64_t al = UINT64_C(256) << (w32_aoc_rng(&rng) % 5u);
        int bi = -1; uint64_t off = 0u;
        int ok = fast_on ? totk_imgmem_pool_alloc(blk, W32_AOC_BLK, mask, ty, sz, al, W32_AOC_BLOCK_SZ, &bi, &off, fault_mode)
                         : w32_ref_alloc(blk, W32_AOC_BLK, ty, sz, al, &bi, &off);
        if (ok) {
            live[nlive++] = (w32_aoc_rec){bi, off, sz};
            sink += (uint64_t)bi * 97u + off;
        } else {
            sink += 3u;
        }
    }
    for (int i = 0; i < nlive; i++) {
        assert(totk_imgmem_block_free(&blk[live[i].bi], live[i].off, live[i].size, 0) == 1);
        blk[live[i].bi].used -= live[i].size;
    }
    w32_aoc_free_pool(blk, kept_off, kept_sz, kept_n);
    free(live); free(kept_off); free(kept_sz);
    return sink;
}

static int w32_aoc_check_faults_and_pick(int fault_mode, uint64_t *out_sink) {
    static const uint32_t flags[10] = {0x01u, 0x01u, 0x07u, 0x0fu, 0x06u, 0x0bu, 0x11u, 0x21u, 0x41u, 0x02u};
    totk_memtype_lut lut;
    totk_memtype_lut_build(&lut, 10u, flags);
    uint64_t rng = UINT64_C(0x123456789abcdef1), sink = 0u;
    for (int i = 0; i < 4000; i++) {
        uint32_t bits = (uint32_t)(w32_aoc_rng(&rng) & 0x3ffu);
        uint32_t want = (uint32_t)(w32_aoc_rng(&rng) & 0x4fu);
        int ref = -1;
        for (uint32_t k = 0u; k < 10u; k++) if ((bits >> k & 1u) && (flags[k] & want) == want) { ref = (int)k; break; }
        int got = totk_memtype_pick_fast(&lut, bits, want, fault_mode);
        if (got != ref) return 1;
        sink += (uint64_t)(got & 31);
    }
    uint64_t at = totk_mem_align_up(512u, 4096u, fault_mode);
    if ((at & 4095u) != 0u) return 1;
    totk_mem_block tb = {0};
    if (!totk_imgmem_block_free(&tb, 512u, 512u, 0)) return 1;
    if (!totk_imgmem_block_free(&tb, 0u, 512u, fault_mode)) { free(tb.free); return 1; }
    int coalesced = tb.nfree;
    free(tb.free);
    if (coalesced != 1) return 1;
    totk_mem_block pb[2] = {{0}, {0}};
    uint64_t pmask[32] = {0};
    pmask[0] = UINT64_C(3);
    int pbi = -1;
    uint64_t poff = 0u;
    int pok = totk_imgmem_block_free(&pb[0], 0u, W32_AOC_BLOCK_SZ, 0) &&
              totk_imgmem_block_free(&pb[1], 0u, W32_AOC_BLOCK_SZ, 0) &&
              totk_imgmem_pool_alloc(pb, 2, pmask, 0u, 4096u, 256u, W32_AOC_BLOCK_SZ, &pbi, &poff, fault_mode);
    free(pb[0].free);
    free(pb[1].free);
    if (!pok || pbi != 0 || poff != 0u) return 1;
    *out_sink = sink;
    return 0;
}

static void *w32_aoc_tsan_worker(void *p) {
    totk_mem_block b = {0};
    assert(totk_imgmem_block_free(&b, 0u, 1u << 20, 0) == 1);
    uint64_t s = 0u;
    for (int i = 0; i < 200; i++) {
        uint64_t off = 0u;
        assert(totk_imgmem_block_alloc(&b, 4096u, 512u, 1u << 20, &off, 0) == 1);
        s += off;
        assert(totk_imgmem_block_free(&b, off, 4096u, 0) == 1);
        b.used -= 4096u;
    }
    free(b.free);
    return (void *)(uintptr_t)(s + (uint64_t)(uintptr_t)p);
}

static int run_ktgl_w32_tests(int fault_mode) {
    uint64_t ps = 0u;
    if (w32_aoc_check_faults_and_pick(fault_mode, &ps) != 0) return 1;
    uint64_t r0 = w32_aoc_run_churn(0, 4000, 0);
    uint64_t r1 = w32_aoc_run_churn(1, 4000, fault_mode);
    if (r0 != r1) return 1;
    pthread_t th[4];
    for (int i = 0; i < 4; i++) if (pthread_create(&th[i], NULL, w32_aoc_tsan_worker, (void *)(uintptr_t)i) != 0) return 1;
    for (int i = 0; i < 4; i++) { void *r = NULL; if (pthread_join(th[i], &r) != 0) return 1; }

    uint64_t t0 = host_now_ns();
    uint64_t b0 = w32_aoc_run_churn(0, 18000, 0);
    uint64_t off_ns = host_now_ns() - t0;
    uint64_t t1 = host_now_ns();
    uint64_t b1 = w32_aoc_run_churn(1, 18000, 0);
    uint64_t on_ns = host_now_ns() - t1;
    if (b0 != b1) return 1;
    printf("aoc_imgmem_w32: off=%.3f ms on=%.3f ms sink=%llu pick_sink=%llu\n",
           off_ns / 1e6, on_ns / 1e6, (unsigned long long)b0, (unsigned long long)ps);
    return 0;
}
#endif
