#ifndef TEST_VK_KTGL_OPT_W14_H
#define TEST_VK_KTGL_OPT_W14_H

#include "totk_nvn_sub_fast.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint64_t host_now_ns(void);
static volatile uint64_t w14_sink;

typedef struct { uint32_t op, size; } w14_hdr;
typedef struct { int32_t stage, index; uint64_t addr, size; } w14_bind_buf;

static int test_w14_sub_fast(void) {
    /* Fault 38: bounded deferred span [def_lo, def_hi) must not be skipped. */
    totk_def_span ds;
    totk_def_span_init(&ds);
    uint32_t lo = 0, hi = 0;
    assert(totk_def_span_slice(ds.lo, ds.hi, &lo, &hi) == 0);
    totk_def_span_note_op(&ds, 64u, 272u);
    totk_def_span_note_marker(&ds, 512u, 16u, 288u);
    if (totk_def_span_slice(ds.lo, ds.hi, &lo, &hi) != 1 || lo != 64u || hi != 792u) {
        fprintf(stderr, "w14 FAIL def_span slice broken\n");
        return 1;
    }
    totk_def_span empty;
    totk_def_span_init(&empty);
    totk_def_span_note_marker(&empty, 128u, 100u, 0u);
    assert(totk_def_span_slice(empty.lo, empty.hi, &lo, &hi) == 0);
    totk_def_span ovf;
    totk_def_span_init(&ovf);
    totk_def_span_note_op(&ovf, (size_t)UINT32_MAX + 64u, 32u);
    assert(totk_def_span_slice(ovf.lo, ovf.hi, &lo, &hi) == 2);

    /* Fault 39: per-submit deduplication must match exact handle address, not 64 KiB page. */
    totk_sub_seen4 seen;
    memset(&seen, 0, sizeof seen);
    totk_sub_seen4_reset(&seen);
    uint64_t a0 = 0x10000000ull, a1 = 0x10001020ull;
    assert(totk_sub_seen_idx(a0) == totk_sub_seen_idx(a1));
    assert((a0 >> 16) == (a1 >> 16));
    assert(totk_sub_seen4_first(&seen, a0) == 1);
    assert(totk_sub_seen4_first(&seen, a0) == 0);
    if (totk_sub_seen4_first(&seen, a1) != 1) {
        fprintf(stderr, "w14 FAIL sub_seen aliases same 64KiB page\n");
        return 1;
    }
    totk_sub_seen4_reset(&seen);
    assert(totk_sub_seen4_first(&seen, a0) == 1);

    /* Fault 40: packed UBO/SSBO record must preserve slot index and match byte layout. */
    uint8_t fast[32], ref[32];
    memset(fast, 0, sizeof fast);
    memset(ref, 0, sizeof ref);
    totk_pack_bind_buf_rec(fast, 19u, 2, 5, 0x12345000ull, 512ull);
    w14_hdr rh = {19u, 32u};
    w14_bind_buf rb = {2, 5, 0x12345000ull, 512ull};
    memcpy(ref, &rh, 8);
    memcpy(ref + 8, &rb, sizeof rb);
    if (memcmp(fast, ref, 32u) != 0) {
        fprintf(stderr, "w14 FAIL pack_bind_buf_rec byte mismatch\n");
        return 1;
    }
    return 0;
}

__attribute__((noinline)) static void *w14_sim_rec_base(
    uint8_t *ring, size_t *off, uint32_t op, size_t body) {
    size_t tot = 8u + body;
    if (*off + tot > 4096u) *off = 0u;
    uint8_t *p = ring + *off;
    *off += tot;
    w14_hdr h = {op, (uint32_t)tot};
    memcpy(p, &h, 8);
    memset(p + 8, 0, body);
    return p + 8;
}

__attribute__((noinline)) static void w14_sim_pack_fast(
    uint8_t *ring, size_t *off, int32_t idx, uint64_t addr) {
    if (*off + 32u > 4096u) *off = 0u;
    totk_pack_bind_buf_rec(ring + *off, 19u, 1, idx, addr, 256u);
    *off += 32u;
}

static void bench_w14_pack_buf(void) {
    uint8_t ring_b[4096], ring_o[4096];
    memset(ring_b, 0, sizeof ring_b);
    memset(ring_o, 0, sizeof ring_o);
    size_t off_b = 0, off_o = 0;
    uint64_t pb = 0, po = 0;

    uint64_t t2 = host_now_ns();
    for (uint32_t i = 0; i < 400000u; i++) {
        w14_bind_buf *bb = (w14_bind_buf *)w14_sim_rec_base(ring_b, &off_b, 19u, 24u);
        *bb = (w14_bind_buf){1, (int32_t)(i & 7u), 0x100000ull + i, 256u};
        pb += ring_b[(off_b - 32u) + 20u];
    }
    double ms_pb = (double)(host_now_ns() - t2) / 1e6;

    uint64_t t3 = host_now_ns();
    for (uint32_t i = 0; i < 400000u; i++) {
        w14_sim_pack_fast(ring_o, &off_o, (int32_t)(i & 7u), 0x100000ull + i);
        po += ring_o[(off_o - 32u) + 20u];
    }
    double ms_po = (double)(host_now_ns() - t3) / 1e6;
    assert(pb == po);
    w14_sink ^= po;
    printf("aoc_w14_pack_buf: base=%.2fms opt=%.2fms speedup=%.2fx\n",
           ms_pb, ms_po, ms_pb / (ms_po > 0.01 ? ms_po : 0.01));
}

static void bench_w14_sub_fast(void) {
    /* Benchmark skipping zero-deferred copies and deduplicating repeated handles across 50k submits. */
    enum { COPIES = 16u, OPS_PER_COPY = 64u, SUB_ITERS = 25000u };
    uint32_t ops[COPIES][OPS_PER_COPY];
    for (uint32_t c = 0; c < COPIES; c++) {
        for (uint32_t j = 0; j < OPS_PER_COPY; j++) {
            ops[c][j] = (j == 12u) ? 1u : 0u;
        }
    }
    totk_def_span spans[COPIES];
    for (uint32_t c = 0; c < COPIES; c++) {
        totk_def_span_init(&spans[c]);
        if (c == 0u) totk_def_span_note_op(&spans[c], 12u * 32u, 272u);
    }

    uint64_t sb = 0, so = 0;
    uint64_t t0 = host_now_ns();
    for (uint32_t it = 0; it < SUB_ITERS; it++) {
        for (uint32_t k = 0; k < 32u; k++) {
            uint32_t c = k & 15u;
            for (uint32_t j = 0; j < OPS_PER_COPY; j++) {
                sb += ops[c][j];
            }
        }
    }
    double ms_b = (double)(host_now_ns() - t0) / 1e6;

    totk_sub_seen4 seen;
    memset(&seen, 0, sizeof seen);
    uint64_t t1 = host_now_ns();
    for (uint32_t it = 0; it < SUB_ITERS; it++) {
        totk_sub_seen4_reset(&seen);
        for (uint32_t k = 0; k < 32u; k++) {
            uint32_t c = k & 15u;
            if (!totk_sub_seen4_first(&seen, 0x20000000ull + (uint64_t)c * 64u)) continue;
            uint32_t lo = 0, hi = 0;
            if (totk_def_span_slice(spans[c].lo, spans[c].hi, &lo, &hi) == 1) {
                so += 2u;
            }
        }
    }
    double ms_o = (double)(host_now_ns() - t1) / 1e6;
    assert(sb == (uint64_t)SUB_ITERS * 32u && so == (uint64_t)SUB_ITERS * 2u);
    w14_sink = sb ^ so;
    printf("aoc_w14_sub_def: base=%.2fms opt=%.2fms speedup=%.2fx\n",
           ms_b, ms_o, ms_b / (ms_o > 0.01 ? ms_o : 0.01));
    bench_w14_pack_buf();
}

static int run_ktgl_w14_tests(int fault_mode) {
    (void)fault_mode;
    (void)totk_nvn_sub_fast_on();
    if (test_w14_sub_fast() != 0) return 1;
    bench_w14_sub_fast();
    return 0;
}

#endif
