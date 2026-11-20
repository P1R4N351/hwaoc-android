#ifndef TEST_VK_KTGL_OPT_W15_H
#define TEST_VK_KTGL_OPT_W15_H

#include "vk_tbuf_fast.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint64_t host_now_ns(void);
static volatile uint64_t w15_sink;

typedef struct { uint32_t tid; uint64_t serial, mem, width, bpb, bv, buf; } w15_tbuf_row;

static int test_w15_tbuf_faults(void) {
    totk_tbuf_memo4 m;
    memset(&m, 0, sizeof m);
    uint64_t ep0 = totk_tbuf_epoch(10ull, 20ull, 30ull, 1ull);
    uint64_t ep1 = totk_tbuf_epoch(10ull, 20ull, 30ull, 2ull);
    assert(ep0 != ep1);
    totk_tbuf_memo4_put(&m, 16u, ep0, 0, 0x55550000ull, 0x11110000ull, 256u, 4096u, 0x80000000ull, 4096u);

    totk_tbuf_ent out;
    assert(totk_tbuf_memo4_get(&m, 16u, ep0, &out) == 1 && out.bv_u == 0x55550000ull);

    /* Fault 41: bumped epoch after retire/release must miss. */
    if (totk_tbuf_memo4_get(&m, 16u, ep1, &out) != 0) {
        fprintf(stderr, "w15 FAIL stale epoch returned retired buffer view\n");
        return 1;
    }
    /* Fault 42: upper bits [16..19] of 20-bit tid must not be truncated on same set. */
    uint32_t tid_alias = 16u ^ (1u << 18);
    assert(totk_tbuf_set_idx(16u) == totk_tbuf_set_idx(tid_alias));
    if (totk_tbuf_memo4_get(&m, tid_alias, ep0, &out) != 0) {
        fprintf(stderr, "w15 FAIL tid tag truncated at 16 bits\n");
        return 1;
    }
    /* Fault 43: frozen lag-copy view must never be admitted into base-pool memo. */
    totk_tbuf_memo4_put(&m, 18u, ep0, 1, 0xdeadf00dull, 0x99990000ull, 0u, 1024u, 0x81000000ull, 1024u);
    if (totk_tbuf_memo4_get(&m, 18u, ep0, &out) != 0) {
        fprintf(stderr, "w15 FAIL frozen lag buffer view admitted into base memo\n");
        return 1;
    }
    return 0;
}

__attribute__((noinline)) static uint64_t w15_sim_tbuf_base(
    pthread_mutex_t *m_reg, pthread_mutex_t *m_tex, const w15_tbuf_row *rows, uint32_t tid) {
    pthread_mutex_lock(m_reg);
    w15_tbuf_row r = rows[tid & 31u];
    pthread_mutex_unlock(m_reg);
    pthread_mutex_lock(m_tex);
    uint64_t bv = r.bv ^ (r.serial & 1u);
    pthread_mutex_unlock(m_tex);
    return bv;
}

__attribute__((noinline)) static uint64_t w15_sim_tbuf_fast(
    const totk_tbuf_memo4 *m, uint32_t tid, uint64_t ep) {
    totk_tbuf_ent out;
    return totk_tbuf_memo4_get(m, tid, ep, &out) ? out.bv_u : 0ull;
}

static void *w15_mt_worker(void *arg) {
    const w15_tbuf_row *rows = (const w15_tbuf_row *)arg;
    totk_tbuf_memo4 m;
    memset(&m, 0, sizeof m);
    uint64_t ep = totk_tbuf_epoch(1ull, 1ull, 1ull, 1ull), acc = 0;
    for (uint32_t i = 0; i < 32u; i++) {
        totk_tbuf_memo4_put(&m, i + 1u, ep, 0, rows[i].bv, rows[i].buf, 0u, 4096u, rows[i].mem, 4096u);
    }
    for (uint32_t i = 0; i < 100000u; i++) {
        acc += w15_sim_tbuf_fast(&m, (i & 31u) + 1u, ep);
    }
    return (void *)(uintptr_t)acc;
}

static void bench_w15_tbuf_memo(void) {
    static pthread_mutex_t m_reg = PTHREAD_MUTEX_INITIALIZER;
    static pthread_mutex_t m_tex = PTHREAD_MUTEX_INITIALIZER;
    w15_tbuf_row rows[32];
    totk_tbuf_memo4 m;
    memset(&m, 0, sizeof m);
    uint64_t ep = totk_tbuf_epoch(1ull, 1ull, 1ull, 1ull);
    for (uint32_t i = 0; i < 32u; i++) {
        rows[i] = (w15_tbuf_row){i + 1u, (i + 1u) * 2u, 0x30000000ull + i * 4096u, 1024u, 4u, 0x90000000ull + i, 0x70000000ull};
        totk_tbuf_memo4_put(&m, i + 1u, ep, 0, rows[i].bv, rows[i].buf, 0u, 4096u, rows[i].mem, 4096u);
    }
    uint64_t sb = 0, so = 0;
    uint64_t t0 = host_now_ns();
    for (uint32_t i = 0; i < 400000u; i++) sb += w15_sim_tbuf_base(&m_reg, &m_tex, rows, i & 31u);
    double ms_b = (double)(host_now_ns() - t0) / 1e6;
    uint64_t t1 = host_now_ns();
    for (uint32_t i = 0; i < 400000u; i++) so += w15_sim_tbuf_fast(&m, (i & 31u) + 1u, ep);
    double ms_o = (double)(host_now_ns() - t1) / 1e6;
    assert(sb == so);

    pthread_t th[4];
    uint64_t t2 = host_now_ns();
    for (int k = 0; k < 4; k++) assert(pthread_create(&th[k], NULL, w15_mt_worker, rows) == 0);
    uint64_t smt = 0;
    for (int k = 0; k < 4; k++) { void *rv = NULL; assert(pthread_join(th[k], &rv) == 0); smt += (uint64_t)(uintptr_t)rv; }
    double ms_mt = (double)(host_now_ns() - t2) / 1e6;
    assert(smt == so);
    w15_sink = sb ^ smt;
    printf("aoc_w15_tbuf_memo: base=%.2fms opt=%.2fms speedup=%.2fx mt4=%.2fms\n",
           ms_b, ms_o, ms_b / (ms_o > 0.01 ? ms_o : 0.01), ms_mt);
}

static int run_ktgl_w15_tests(int fault_mode) {
    (void)fault_mode;
    (void)totk_tbuf_fast_on();
    if (test_w15_tbuf_faults() != 0) return 1;
    bench_w15_tbuf_memo();
    return 0;
}

#endif
