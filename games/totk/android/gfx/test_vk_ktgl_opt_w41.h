#ifndef TEST_VK_KTGL_OPT_W41_H
#define TEST_VK_KTGL_OPT_W41_H
#ifndef TOTK_W41_TEST_HOOKS
#define TOTK_W41_TEST_HOOKS 1
#endif
#include "vk_pace_fast.h"
#include "predict_pace.h"
#include "display_clock.h"
#include "fps_auto.h"
#include "framegen_adaptive_slots.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

_Atomic int g_totk_pace_fast_state;
_Atomic int g_totk_w41_fault;

static inline uint64_t w41_aoc_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}

static int w41_test_predict_pace_parity(void) {
    predict_pacer pf = {0}, ps = {0};
    uint64_t period = UINT64_C(16666667), slot = UINT64_C(2000000000);
    for (unsigned f = 0u; f < 900u; f++, slot += period) {
        totk_pace_fast_set_for_test(1);
        predict_decision df = predict_pacer_choose(&pf, slot, period);
        totk_pace_fast_set_for_test(0);
        predict_decision ds = predict_pacer_choose(&ps, slot, period);
        if (memcmp(&df, &ds, sizeof df) != 0) return 1;
        uint64_t base_w = UINT64_C(5000000) + (uint64_t)((f * 131u) % 23u) * UINT64_C(200000);
        if (f > 320u && (f % 17u) < 4u) base_w = UINT64_C(7000000);
        uint64_t work = (f == 750u) ? (3u * period) : base_w;
        uint64_t allow = UINT64_C(900000) + (uint64_t)((f * 37u) % 5u) * UINT64_C(50000);
        totk_pace_fast_set_for_test(1);
        predict_observation of = predict_pacer_observe(&pf, &df, work, allow, df.release_ns + work);
        int64_t ph = (int64_t)(((f * 79u) % 29u) - 10u) * INT64_C(400000);
        uint64_t disp = ph >= 0 ? slot + (uint64_t)ph : slot - (uint64_t)(-ph);
        int mf = predict_pacer_presented(&pf, slot, period, period, disp);
        totk_pace_fast_set_for_test(0);
        predict_observation os = predict_pacer_observe(&ps, &ds, work, allow, ds.release_ns + work);
        int ms = predict_pacer_presented(&ps, slot, period, period, disp);
        if (mf != ms || memcmp(&of, &os, sizeof of) != 0 || memcmp(&pf, &ps, sizeof pf) != 0) return 1;
    }
    return 0;
}

static int w41_test_display_clock_parity(void) {
    display_period_clock cf = {0}, cs = {0};
    uint64_t nom = UINT64_C(16666667), frame = UINT64_C(1000000000);
    for (unsigned i = 0u; i < 900u; i++) {
        uint64_t actual = i < 450u ? UINT64_C(16680000) : UINT64_C(16740000);
        unsigned skip = (i % 41u == 0u) ? 2u : 1u;
        int64_t jitter = (i % 79u == 5u) ? INT64_C(400000) : (int64_t)((i % 5u) * 250u);
        frame += actual * skip + (uint64_t)jitter;
        totk_pace_fast_set_for_test(1);
        display_clock_result rf = display_clock_observe(&cf, frame, nom);
        totk_pace_fast_set_for_test(0);
        display_clock_result rs = display_clock_observe(&cs, frame, nom);
        if (memcmp(&rf, &rs, sizeof rf) != 0 || memcmp(&cf, &cs, sizeof cf) != 0) return 1;
    }
    return 0;
}

static int w41_test_fps_auto_parity(void) {
    fps_auto_policy af = {0}, as = {0};
    uint64_t ref = UINT64_C(16666667), t = UINT64_C(1000000000);
    totk_pace_fast_set_for_test(1); fps_auto_reset(&af, t, ref);
    totk_pace_fast_set_for_test(0); fps_auto_reset(&as, t, ref);
    for (unsigned f = 0u; f < 1600u; f++) {
        uint64_t dt = (f < 700u) ? UINT64_C(20000000) : UINT64_C(15000000);
        if (f == 1200u) dt = UINT64_C(500000000);
        t += dt;
        uint64_t rdy = (f >= 700u && (f % 31u != 0u)) ? UINT64_C(14000000) : UINT64_C(22000000);
        totk_pace_fast_set_for_test(1);
        fps_auto_game_present(&af, t);
        fps_auto_observation of = fps_auto_observe(&af, t, rdy, dt);
        unsigned pf = fps_auto_apply(&af, t);
        totk_pace_fast_set_for_test(0);
        fps_auto_game_present(&as, t);
        fps_auto_observation os = fps_auto_observe(&as, t, rdy, dt);
        unsigned ps = fps_auto_apply(&as, t);
        if (pf != ps || memcmp(&of, &os, sizeof of) != 0 || memcmp(&af, &as, sizeof af) != 0) return 1;
    }
    return 0;
}

static int w41_test_fgs_parity_step(fgs_state *sf, fgs_state *ss, uint64_t now, uint64_t f) {
    fgs_real_plan pf, ps; fgs_batch bf, bs;
    totk_pace_fast_set_for_test(1); fgs_status rf = fgs_peek_real(sf, now, &pf);
    totk_pace_fast_set_for_test(0); fgs_status rs = fgs_peek_real(ss, now, &ps);
    if (rf != rs || memcmp(&pf, &ps, sizeof pf) != 0) return 1;
    totk_pace_fast_set_for_test(1); rf = fgs_commit_real(sf, pf.slot, now, f + 1u, 1u, 1, &bf);
    totk_pace_fast_set_for_test(0); rs = fgs_commit_real(ss, ps.slot, now, f + 1u, 1u, 1, &bs);
    if (rf != rs || memcmp(&bf, &bs, sizeof bf) != 0 || memcmp(sf, ss, sizeof *sf) != 0) return 1;
    for (unsigned k = 0u; k < bf.request_count; k++) {
        uint64_t tok = bf.requests[k].token; fgs_cancel cf, cs;
        totk_pace_fast_set_for_test(1); rf = fgs_mark_ready(sf, tok, bf.requests[k].publication_ns + UINT64_C(2000000), &cf);
        totk_pace_fast_set_for_test(0); rs = fgs_mark_ready(ss, tok, bs.requests[k].publication_ns + UINT64_C(2000000), &cs);
        if (rf != rs || memcmp(&cf, &cs, sizeof cf) != 0) return 1;
        fgs_request af, as;
        totk_pace_fast_set_for_test(1); rf = fgs_admit_gen(sf, tok, bf.requests[k].deadline_ns, NULL, &af);
        totk_pace_fast_set_for_test(0); rs = fgs_admit_gen(ss, tok, bs.requests[k].deadline_ns, NULL, &as);
        if (rf != rs || memcmp(&af, &as, sizeof af) != 0) return 1;
        if (rf == FGS_OK && ((f + k) % 2u == 0u)) {
            fgs_gen_commit gf, gs;
            totk_pace_fast_set_for_test(1); rf = fgs_commit_gen(sf, tok, bf.requests[k].deadline_ns, &gf);
            totk_pace_fast_set_for_test(0); rs = fgs_commit_gen(ss, tok, bs.requests[k].deadline_ns, &gs);
            if (rf != rs || memcmp(&gf, &gs, sizeof gf) != 0) return 1;
        }
    }
    return memcmp(sf, ss, sizeof *sf) != 0;
}

static int w41_test_fgs_parity(void) {
    fgs_state sf = {0}, ss = {0};
    uint64_t period = UINT64_C(16666667), base = UINT64_C(1000000000), margin = UINT64_C(2000000);
    totk_pace_fast_set_for_test(1); fgs_init(&sf, 1u, base, period, margin);
    totk_pace_fast_set_for_test(0); fgs_init(&ss, 1u, base, period, margin);
    uint64_t now = base + UINT64_C(1000000);
    for (uint64_t f = 0u; f < 300u; f++, now += UINT64_C(33333334)) {
        if (w41_test_fgs_parity_step(&sf, &ss, now, f) != 0) return 1;
        if (f == 60u || f == 240u) {
            fgs_batch bf, bs;
            totk_pace_fast_set_for_test(1); fgs_reset(&sf, 2u, now, period, margin, &bf);
            totk_pace_fast_set_for_test(0); fgs_reset(&ss, 2u, now, period, margin, &bs);
            if (memcmp(&bf, &bs, sizeof bf) != 0 || memcmp(&sf, &ss, sizeof sf) != 0) return 1;
            fgs_reanchor_result rf, rs;
            totk_pace_fast_set_for_test(1); fgs_status stf = fgs_reanchor(&sf, base + f * UINT64_C(33333334), INT64_C(500000), period, &rf);
            totk_pace_fast_set_for_test(0); fgs_status sts = fgs_reanchor(&ss, base + f * UINT64_C(33333334), INT64_C(500000), period, &rs);
            if (stf != sts || memcmp(&rf, &rs, sizeof rf) != 0 || memcmp(&sf, &ss, sizeof sf) != 0) return 1;
        } else if (f == 90u && sf.have_real) {
            fgs_reanchor_result rf, rs;
            for (unsigned k = 0u; k < 16u; k++) if (sf.pending[k].token) {
                fgs_cancel cf, cs; uint64_t tok = sf.pending[k].token;
                totk_pace_fast_set_for_test(1); fgs_cancel_gen(&sf, tok, FGS_CANCEL_OWNER, &cf);
                totk_pace_fast_set_for_test(0); fgs_cancel_gen(&ss, tok, FGS_CANCEL_OWNER, &cs);
            }
            totk_pace_fast_set_for_test(1); fgs_status stf = fgs_reanchor(&sf, sf.last_real.target_ns, INT64_C(500000), period, &rf);
            totk_pace_fast_set_for_test(0); fgs_status sts = fgs_reanchor(&ss, ss.last_real.target_ns, INT64_C(500000), period, &rs);
            if (stf != FGS_OK || stf != sts || memcmp(&rf, &rs, sizeof rf) != 0 || memcmp(&sf, &ss, sizeof sf) != 0) return 1;
        }
    }
    fgs_batch ef, es;
    totk_pace_fast_set_for_test(1); fgs_end(&sf, &ef);
    totk_pace_fast_set_for_test(0); fgs_end(&ss, &es);
    return memcmp(&ef, &es, sizeof ef) != 0 || memcmp(&sf, &ss, sizeof sf) != 0;
}

static void *w41_aoc_tsan_worker(void *arg) {
    _Atomic int *fail = (_Atomic int *)arg;
    predict_pacer p = {0};
    display_period_clock clk = {0};
    uint64_t period = UINT64_C(16666667), slot = UINT64_C(2000000000), frame = UINT64_C(1000000000);
    for (unsigned i = 0u; i < 400u; i++, slot += period) {
        predict_decision d = predict_pacer_choose(&p, slot, period);
        predict_observation o = predict_pacer_observe(&p, &d, UINT64_C(6000000), UINT64_C(1000000), d.release_ns + UINT64_C(6000000));
        if (!o.valid) { atomic_store_explicit(fail, 1, memory_order_relaxed); break; }
        frame += UINT64_C(16680000);
        (void)display_clock_observe(&clk, frame, period);
    }
    return NULL;
}

static int w41_test_aoc_tsan(void) {
    _Atomic int fail = 0;
    pthread_t th[4];
    int started = 0;
    totk_pace_fast_set_for_test(1);
    for (int i = 0; i < 4; i++) {
        if (pthread_create(&th[i], NULL, w41_aoc_tsan_worker, &fail) != 0) {
            atomic_store_explicit(&fail, 1, memory_order_release);
            break;
        }
        started++;
    }
    for (int i = 0; i < started; i++) pthread_join(th[i], NULL);
    totk_pace_fast_set_for_test(-1);
    return atomic_load_explicit(&fail, memory_order_acquire);
}

static uint64_t w41_bench_pace(int fast_on) {
    totk_pace_fast_set_for_test(fast_on);
    predict_pacer p = {0};
    display_period_clock clk = {0};
    fps_auto_policy fa = {0};
    fgs_state fgs = {0};
    uint64_t period = UINT64_C(16666667), slot = UINT64_C(2000000000), frame = UINT64_C(1000000000), sink = 0u;
    fps_auto_reset(&fa, frame, period);
    fgs_init(&fgs, 1u, frame, period, UINT64_C(2000000));
    for (unsigned i = 0u; i < 15000u; i++, slot += period) {
        predict_decision d = predict_pacer_choose(&p, slot, period);
        uint64_t work = UINT64_C(5000000) + (uint64_t)((i * 97u) % 29u) * UINT64_C(100000);
        predict_pacer_observe(&p, &d, work, UINT64_C(1000000), d.release_ns + work);
        sink += (uint64_t)predict_pacer_presented(&p, slot, period, period, slot + UINT64_C(400000));
        frame += UINT64_C(16670000) + (uint64_t)(i & 1u) * UINT64_C(10000000);
        display_clock_result cr = display_clock_observe(&clk, frame, period);
        sink += cr.refresh_ns;
        fps_auto_game_present(&fa, frame);
        (void)fps_auto_observe(&fa, frame, UINT64_C(14000000), UINT64_C(16670000));
        sink += fps_auto_apply(&fa, frame);
        fgs_real_plan plan; fgs_batch batch;
        if (fgs_peek_real(&fgs, frame, &plan) == FGS_OK && fgs_commit_real(&fgs, plan.slot, frame, (uint64_t)i + 1u, 1u, 1, &batch) == FGS_OK) {
            sink += batch.request_count;
            for (unsigned k = 0u; k < batch.request_count; k++) {
                int idx = fgs_pending_index(&fgs, batch.requests[k].token);
                sink += (uint64_t)(idx >= 0 ? idx : 0);
            }
        }
    }
    totk_pace_fast_set_for_test(-1);
    return sink;
}

static inline int run_ktgl_w41_tests(int fault_mode) {
    int prev = atomic_load_explicit(&g_totk_w41_fault, memory_order_acquire);
    if (fault_mode >= 151 && fault_mode <= 155) totk_pace_w41_set_fault(fault_mode - 150);
    int err = 0;
    if (w41_test_predict_pace_parity() != 0) err = 1;
    if (w41_test_display_clock_parity() != 0) err = 1;
    if (w41_test_fps_auto_parity() != 0) err = 1;
    if (w41_test_fgs_parity() != 0) err = 1;
    if (!err && w41_test_aoc_tsan() != 0) err = 1;
    uint64_t t0 = w41_aoc_ns(), s0 = w41_bench_pace(0), t1 = w41_aoc_ns();
    uint64_t t2 = w41_aoc_ns(), s1 = w41_bench_pace(1), t3 = w41_aoc_ns();
    if (s0 != s1) err = 1;
    double slow_ms = (double)(t1 - t0) / 1e6, fast_ms = (double)(t3 - t2) / 1e6;
    printf("aoc_w41_pace_fast: slow_ms=%.3f fast_ms=%.3f speedup=%.2fx sink=%llu %s\n",
        slow_ms, fast_ms, fast_ms > 0.0 ? slow_ms / fast_ms : 0.0,
        (unsigned long long)s1, err ? "FAIL" : "PASS");
    totk_pace_w41_set_fault(prev);
    return err;
}

#ifdef TOTK_W41_STANDALONE
int main(void) {
    const char *f = getenv("TOTK_W41_FAULT");
    return run_ktgl_w41_tests(f && *f ? (150 + atoi(f)) : 0);
}
#endif
#endif
