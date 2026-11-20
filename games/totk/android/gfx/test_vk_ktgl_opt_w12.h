#ifndef TEST_VK_KTGL_OPT_W12_H
#define TEST_VK_KTGL_OPT_W12_H

#include "vk_cs_fast.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint64_t host_now_ns(void);
static volatile uint64_t w12_sink;

typedef struct {
    uint64_t prog_u, serial, epoch, sig5, sh5;
    int ready;
} w12_cs_item;

static volatile uint64_t g_w12_nonce = 0x9e3779b97f4a7c15ull;

static uint64_t w12_resolve_base(const w12_cs_item *pool, uint32_t n, uint64_t prog_u,
                                 uint64_t serial, uint64_t epoch, uint64_t sig5) {
    uint64_t acc = (serial ^ g_w12_nonce) & 7u;
    uint64_t found = 0;
    for (uint32_t i = 0; i < n; i++) {
        const w12_cs_item *it = &pool[i];
        acc += (it->epoch ^ (it->sig5 << 1)) & 3u;
        if (it->prog_u == prog_u && it->serial == serial &&
            it->epoch == epoch && it->sig5 == sig5 && it->ready) {
            found = it->sh5;
        }
    }
    return found ? (found ^ acc) : 0;
}

static uint64_t w12_resolve_opt(totk_cs_prog_memo4 *m, const w12_cs_item *pool, uint32_t n,
                                uint64_t prog_u, uint64_t serial, uint64_t epoch, uint64_t sig5) {
    uint64_t hit = 0;
    if (totk_cs_prog_memo4_find(m, prog_u, serial, epoch, sig5, &hit)) return hit;
    uint64_t res = w12_resolve_base(pool, n, prog_u, serial, epoch, sig5);
    if (res) totk_cs_prog_memo4_put(m, prog_u, serial, epoch, sig5, res);
    return res;
}

static int test_w12_cs_prog_memo(void) {
    w12_cs_item pool[16];
    memset(pool, 0, sizeof pool);
    for (uint32_t i = 0; i < 8u; i++) {
        pool[i] = (w12_cs_item){0x200000ull + i * 64u, 100u + i, 1u, 0x10u + i, 0x900000ull + i * 32u, 1};
    }
    /* Negative/unready entry must not be cached so later retry succeeds once ready. */
    pool[8] = (w12_cs_item){0x200400ull, 200u, 1u, 0x20u, 0x990000ull, 0};
    /* Fault 32 target: same prog/serial/epoch, different sig5. */
    pool[9] = (w12_cs_item){0x200000ull, 100u, 1u, 0x777u, 0x991111ull, 1};
    /* Fault 33 target: same prog/epoch/sig5, new serial (nvnProgramSetShaders reuse). */
    pool[10] = (w12_cs_item){0x200000ull, 300u, 1u, 0x10u, 0x992222ull, 1};

    totk_cs_prog_memo4 m;
    memset(&m, 0, sizeof m);

    uint64_t r0b = w12_resolve_base(pool, 11u, 0x200000ull, 100u, 1u, 0x10u);
    uint64_t r0o = w12_resolve_opt(&m, pool, 11u, 0x200000ull, 100u, 1u, 0x10u);
    if (!r0b || r0b != r0o) { fprintf(stderr, "w12 FAIL init resolve\n"); return 1; }

    /* Negative retry -> publish -> must resolve cleanly. */
    if (w12_resolve_opt(&m, pool, 11u, 0x200400ull, 200u, 1u, 0x20u) != 0) return 1;
    pool[8].ready = 1;
    uint64_t rnb = w12_resolve_base(pool, 11u, 0x200400ull, 200u, 1u, 0x20u);
    uint64_t rno = w12_resolve_opt(&m, pool, 11u, 0x200400ull, 200u, 1u, 0x20u);
    if (!rnb || rnb != rno) { fprintf(stderr, "w12 FAIL unready retry cached miss\n"); return 1; }

    /* Fault 32 check: sig5 changed on same serial/epoch. */
    uint64_t rsb = w12_resolve_base(pool, 11u, 0x200000ull, 100u, 1u, 0x777u);
    uint64_t rso = w12_resolve_opt(&m, pool, 11u, 0x200000ull, 100u, 1u, 0x777u);
    if (rsb != rso) { fprintf(stderr, "w12 FAIL sig5 mismatch\n"); return 1; }

    /* Fault 33 check: serial changed on same prog pointer & sig5. */
    uint64_t rrb = w12_resolve_base(pool, 11u, 0x200000ull, 300u, 1u, 0x10u);
    uint64_t rro = w12_resolve_opt(&m, pool, 11u, 0x200000ull, 300u, 1u, 0x10u);
    if (rrb != rro) { fprintf(stderr, "w12 FAIL serial mismatch\n"); return 1; }

    /* Fault 34 check: compute pipeline bind elision + foreign invalidation. */
    totk_cs_pipe_state st = {0};
    if (!totk_cs_pipe_bind_needed(&st, 1, 0x10u, 5u, 0xaaa1ull)) return 1;
    if (totk_cs_pipe_bind_needed(&st, 1, 0x10u, 5u, 0xaaa1ull)) return 1;
    if (!totk_cs_pipe_bind_needed(&st, 1, 0x10u, 5u, 0xaaa2ull)) {
        fprintf(stderr, "w12 FAIL cs pipe change ignored\n"); return 1;
    }
    if (!totk_cs_pipe_bind_needed(&st, 1, 0x20u, 5u, 0xaaa2ull)) return 1;
    if (!totk_cs_pipe_bind_needed(&st, 1, 0x20u, 6u, 0xaaa2ull)) return 1;
    totk_cs_pipe_invalidate(&st);
    if (!totk_cs_pipe_bind_needed(&st, 1, 0x20u, 6u, 0xaaa2ull)) return 1;
    return 0;
}

static uint64_t g_w12_cb_ring[64][3];
static volatile uint64_t g_w12_cmd_binds;

static inline void w12_sim_bind(uint64_t pipe) {
    uint32_t idx = (uint32_t)g_w12_cmd_binds & 63u;
    g_w12_cb_ring[idx][0] = 0x42494e44u;
    g_w12_cb_ring[idx][1] = 1u;
    g_w12_cb_ring[idx][2] = pipe;
    g_w12_cmd_binds = g_w12_cmd_binds + 1u;
}

static void bench_w12_cs_fast(void) {
    w12_cs_item pool[16];
    memset(pool, 0, sizeof pool);
    for (uint32_t i = 0; i < 16u; i++) {
        pool[i] = (w12_cs_item){0x300000ull + i * 64u, 500u + i, 2u, 0x40u + (i & 3u), 0xa00000ull + i * 32u, 1};
    }
    totk_cs_prog_memo4 m;
    memset(&m, 0, sizeof m);

    const uint32_t N = 400000u;
    uint64_t sb = 0, so = 0;
    uint64_t t0 = host_now_ns();
    for (uint32_t i = 0; i < N; i++) {
        uint32_t idx = i & 7u;
        sb += w12_resolve_base(pool, 16u, pool[idx].prog_u, pool[idx].serial, 2u, pool[idx].sig5);
    }
    double ms_base = (double)(host_now_ns() - t0) / 1e6;

    uint64_t t1 = host_now_ns();
    for (uint32_t i = 0; i < N; i++) {
        uint32_t idx = i & 7u;
        so += w12_resolve_opt(&m, pool, 16u, pool[idx].prog_u, pool[idx].serial, 2u, pool[idx].sig5);
    }
    double ms_opt = (double)(host_now_ns() - t1) / 1e6;
    assert(sb == so);

    totk_cs_pipe_state st = {0};
    uint64_t binds_base = 0, binds_opt = 0, work_base = 0, work_opt = 0;
    uint64_t t2 = host_now_ns();
    for (uint32_t i = 0; i < N; i++) {
        uint64_t pipe = 0xbb00u + ((i >> 14) & 3u);
        w12_sim_bind(pipe);
        binds_base++;
        work_base += (pipe & 1u);
    }
    double ms_pb = (double)(host_now_ns() - t2) / 1e6;
    uint64_t t3 = host_now_ns();
    for (uint32_t i = 0; i < N; i++) {
        uint64_t pipe = 0xbb00u + ((i >> 14) & 3u);
        if (totk_cs_pipe_bind_needed(&st, 1, 0x100u, 9u, pipe)) {
            w12_sim_bind(pipe);
            binds_opt++;
        }
        work_opt += (pipe & 1u);
    }
    double ms_po = (double)(host_now_ns() - t3) / 1e6;
    assert(work_base == work_opt && binds_opt < binds_base);

    w12_sink = sb ^ so ^ work_base ^ binds_opt ^ g_w12_cmd_binds ^ g_w12_cb_ring[0][2];
    printf("aoc_w12_cs_prog: baseline=%.2fms opt=%.2fms speedup=%.2fx\n",
           ms_base, ms_opt, ms_base / (ms_opt > 0.01 ? ms_opt : 0.01));
    printf("aoc_w12_cs_pipe: base_binds=%llu opt_binds=%llu (%.2fms->%.2fms, %.2fx)\n",
           (unsigned long long)binds_base, (unsigned long long)binds_opt,
           ms_pb, ms_po, ms_pb / (ms_po > 0.01 ? ms_po : 0.01));
}

static int run_ktgl_w12_tests(int fault_mode) {
    (void)fault_mode; (void)totk_vk_cs_fast_on();
    if (test_w12_cs_prog_memo() != 0) return 1;
    bench_w12_cs_fast();
    return 0;
}

#endif
