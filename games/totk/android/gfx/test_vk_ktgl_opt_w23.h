/* Wave 23 (Opt 25) unit tests, TSan stress, microbench, and faults 67..71 for TOTK_HID_FAST. */
#pragma once
#include "../../runtime/overlay_chord.h"
#include "../../runtime/totk_hid_fast.h"
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

uint64_t host_now_ns(void);

static uint64_t w23_kb_btn(unsigned code) {
    switch (code) {
    case 38: return 1; case 40: case 49: return 2; case 34: return 4; case 32: return 8;
    case 6: return 16; case 8: return 32; case 12: return 64; case 14: return 128;
    case 58: return 256; case 56: return 512; case 36: return 1024; case 48: case 53: return 2048;
    case 3: return 4096; case 17: return 8192; case 4: return 16384; case 5: return 32768;
    default: return 0;
    }
}

static int w23_test_kb_tracker(void) {
    totk_kb_tracker kb = {0};
    assert(atomic_load(&kb.nonzero) == 0);
    /* Dual-mapped keys: 40 + 49 both map to B (bit 1); releasing 40 must preserve held bit 1. */
    totk_kb_key_update(&kb, 40, 1, w23_kb_btn(40));
    totk_kb_key_update(&kb, 49, 1, w23_kb_btn(49));
    totk_kb_key_update(&kb, 13, 1, 0);
    uint64_t b = 0; int32_t lx = 0, ly = 0, rx = 0, ry = 0;
    totk_kb_key_update(&kb, 40, 0, w23_kb_btn(40));
    totk_kb_consume(&kb, &b, &lx, &ly, &rx, &ry);
    assert(b == 2 && ly == 32767 && atomic_load(&kb.nonzero) == 1);
    totk_kb_key_update(&kb, 49, 0, w23_kb_btn(49));
    b = 0; totk_kb_consume(&kb, &b, &lx, &ly, &rx, &ry);
    assert(b == 0 && ly == 32767);
    totk_kb_clear(&kb);
    assert(atomic_load(&kb.nonzero) == 0);
    return 0;
}

static int w23_test_pad_seqlock(void) {
    totk_pad_fast_slot slot = {0};
    const int32_t z[4] = {0}, axes[4] = {12000, -16000, 0, 8000};
    totk_pad_fast_write(&slot, 0x5, axes, 1, 1);
    RuntimeGamepadState out = {0};
    assert(totk_pad_fast_read(&slot, &out) == 1);
    assert(out.buttons == 0x5 && out.lx == 12000 && out.ly == -16000 && out.connected == 1);
    totk_pad_fast_write(&slot, 0x800, z, 1, 0);
    assert(totk_pad_fast_read(&slot, &out) == 0);
    overlay_chord ch = {0};
    assert(overlay_chord_key(&ch, OVERLAY_CHORD_MINUS, OVERLAY_CHORD_PRESS, 1000) == 0);
    assert(ch.minus == CHORD_HELD_BACK);
    int fast_ok = (!0 && ch.minus != CHORD_HELD_BACK && ch.minus != CHORD_TAP);
    totk_pad_fast_write(&slot, 0, z, 1, fast_ok);
    assert(totk_pad_fast_read(&slot, &out) == 0);
    return 0;
}

static _Atomic uint64_t g_w23_fake_now = 1000000000ull;
static uint64_t w23_fake_now_fn(void) { return atomic_load_explicit(&g_w23_fake_now, memory_order_relaxed); }

static _Atomic int g_w23_stop;
static void *w23_motion_writer(void *arg) {
    totk_motion_fast_ring *r = arg;
    for (int i = 1; i <= 500 && !atomic_load(&g_w23_stop); i++) {
        RuntimeMotionState s = {.sampling = (uint64_t)i, .delta_ns = 4000000, .acc = {0, 0, -1}, .gyro = {0, 0, 0.25f}};
        totk_motion_fast_publish(r, &s, (uint64_t)i, w23_fake_now_fn());
    }
    return NULL;
}

static int w23_test_motion_and_vib(void) {
    totk_motion_fast_ring ring = {0};
    RuntimeMotionState out[16];
    assert(totk_motion_fast_read(&ring, w23_fake_now_fn, out, 16) == 0);
    pthread_t th; atomic_store(&g_w23_stop, 0);
    assert(!pthread_create(&th, NULL, w23_motion_writer, &ring));
    for (int i = 0; i < 400; i++) {
        int n = totk_motion_fast_read(&ring, w23_fake_now_fn, out, 16);
        assert(n >= 0 && n <= 16);
        for (int j = 1; j < n; j++) assert(out[j - 1].sampling == out[j].sampling + 1);
    }
    pthread_join(th, NULL);
    assert(totk_motion_fast_read(&ring, w23_fake_now_fn, out, 16) == 16 && out[0].sampling == 500);
    atomic_fetch_add(&g_w23_fake_now, UINT64_C(200000001));
    assert(totk_motion_fast_read(&ring, w23_fake_now_fn, out, 16) == 0);

    totk_vib_fast_state vf = {0};
    float al = 0, fl = 0, ah = 0, fh = 0;
    assert(totk_vib_fast_read(&vf, 0, &al, &fl, &ah, &fh) == 0 && fl == 160.0f && fh == 320.0f);
    assert(totk_vib_fast_should_send(&vf, 0, 0.4f, 0.0f) == 1);
    assert(totk_vib_fast_should_send(&vf, 0, 0.4f, 0.0f) == 1);
    assert(totk_vib_fast_should_send(&vf, 0, 0.0f, 0.0f) == 1);
    assert(totk_vib_fast_should_send(&vf, 0, 0.0f, 0.0f) == 0);
    totk_vib_fast_reset_silent(&vf, 0);
    assert(totk_vib_fast_should_send(&vf, 0, 0.0f, 0.0f) == 1);
    return 0;
}

static void w23_run_benchmarks(void) {
    totk_pad_fast_slot pad = {0};
    totk_motion_fast_ring mr = {0};
    totk_vib_fast_state vf = {0};
    const int32_t axes[4] = {0, 0, 0, 0};
    totk_pad_fast_write(&pad, 0, axes, 1, 1);
    pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    RuntimeMotionState ms[16]; RuntimeGamepadState gp = {0};
    for (int mode = 0; mode <= 1; mode++) {
        totk_hid_fast_reset_for_test(mode);
        uint64_t sink = 0, t0 = host_now_ns();
        for (int i = 0; i < 40000; i++) {
            if (mode) {
                totk_pad_fast_read(&pad, &gp);
                sink += (uint64_t)gp.connected + (uint64_t)totk_motion_fast_read(&mr, w23_fake_now_fn, ms, 16);
                sink += (uint64_t)totk_vib_fast_should_send(&vf, 0, 0.0f, 0.0f);
            } else {
                pthread_mutex_lock(&mu);
                gp.connected = 1;
                sink += (uint64_t)gp.connected + (w23_fake_now_fn() > 0 ? 1u : 0u);
                pthread_mutex_unlock(&mu);
            }
        }
        double ms_t = (double)(host_now_ns() - t0) / 1e6;
        printf("BENCH aoc_w23_%s pad_motion_vib_poll=%.3f ms sink=%llu\n",
            mode ? "on" : "off", ms_t, (unsigned long long)sink);
    }
    totk_hid_fast_reset_for_test(-1);
}

static int run_ktgl_w23_tests(int fault_mode) {
    if (fault_mode == 67) {
        totk_kb_tracker kb = {0};
        totk_kb_key_update(&kb, 40, 1, w23_kb_btn(40));
        totk_kb_key_update(&kb, 49, 1, w23_kb_btn(49));
        totk_kb_key_update(&kb, 40, 0, w23_kb_btn(40));
        return (kb.held_mask & 2ull) ? 0 : 1;
    }
    if (fault_mode == 68) {
        totk_pad_fast_slot slot = {0};
        const int32_t z[4] = {0}; RuntimeGamepadState out = {0};
        totk_pad_fast_write(&slot, 0, z, 1, 0);
        return totk_pad_fast_read(&slot, &out) == 0 ? 0 : 1;
    }
    if (fault_mode == 69) {
        totk_motion_fast_ring r = {0}; RuntimeMotionState out[16];
        RuntimeMotionState s = {.sampling = 1};
        atomic_store(&g_w23_fake_now, 1000000000ull);
        totk_motion_fast_publish(&r, &s, 1, w23_fake_now_fn());
        atomic_fetch_add(&g_w23_fake_now, UINT64_C(200000001));
        return totk_motion_fast_read(&r, w23_fake_now_fn, out, 16) == 0 ? 0 : 1;
    }
    if (fault_mode == 70) {
        totk_vib_fast_state vf = {0};
        int c = totk_vib_fast_should_send(&vf, 0, 0.5f, 0.0f) + totk_vib_fast_should_send(&vf, 0, 0.5f, 0.0f);
        return c == 2 ? 0 : 1;
    }
    if (fault_mode == 71) {
        _Atomic uint64_t values_in = 0;
        if (totk_hid_w23_fault() != 71) atomic_fetch_add(&values_in, 1);
        return atomic_load(&values_in) == 1 ? 0 : 1;
    }
    if (fault_mode > 0) return 0;
    if (w23_test_kb_tracker() || w23_test_pad_seqlock() || w23_test_motion_and_vib()) return 1;
    w23_run_benchmarks();
    return 0;
}
