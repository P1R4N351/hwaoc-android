/* Wave 29 (Opt 31) tests, TSan stress, microbench, and faults 97..101 for TOTK_AUDREN_FAST. */
#pragma once
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOTK_AUDREN_FAST_TEST 1
#include "totk_audren_fast.h"

uint64_t host_now_ns(void);

typedef struct {
    AudrenVoiceIn in;
    AudrenBiquadFloat fbq[2];
    uint8_t use_fbq;
    uint8_t pad[3];
} w29_voice_slot;

static int w29_floats_close(const float *a, const float *b, int n) {
    for (int i = 0; i < n; i++) {
        if (!isfinite(b[i]) || fabsf(a[i] - b[i]) > 1e-6f) return 0;
    }
    return 1;
}

static float w29_slow_reverb_delay(const float pre[128], const unsigned et[20], const float gains[20],
                                   const float in0[240], const float in1[240],
                                   float out0[240], float out1[240]) {
    static const unsigned st[20] = {0,0,0,1,1,1,1,0,0,0,1,1,1,0,0,0,0,1,1,1};
    float early[6] = {0}, d0[8] = {0}, d1[8] = {0}, h[2] = {0};
    unsigned at0 = 0u, at1 = 0u;
    for (int k = 0; k < 240; k++) {
        int ch = 2;
        for (int i = 0; i < 20; i++) {
            unsigned rt = ch == 1 ? 0u : st[i];
            int idx = 100 - (int)et[i];
            if (idx < 0) idx += 128;
            early[rt] += pre[idx] * gains[i];
        }
        float x0 = in0[k] * 64.0f, x1 = in1[k] * 64.0f;
        float r0 = d0[at0], r1 = d1[at1], fb0 = r0 * 0.35f + r1 * 0.15f, fb1 = r1 * 0.35f + r0 * 0.15f;
        h[0] = 0.2f * h[0] + (x0 * 0.8f + fb0) * (1.0f - 0.2f);
        d0[at0++] = h[0]; if (at0 >= 7u) at0 = 0u;
        out0[k] = (x0 * 0.4f + r0 * 0.6f) / 64.0f;
        h[1] = 0.2f * h[1] + (x1 * 0.8f + fb1) * (1.0f - 0.2f);
        d1[at1++] = h[1]; if (at1 >= 7u) at1 = 0u;
        out1[k] = (x1 * 0.4f + r1 * 0.6f) / 64.0f;
    }
    float s = early[0] + early[1];
    for (int k = 0; k < 240; k++) s += out0[k] + out1[k];
    return s;
}

static float w29_fast_reverb_delay(const float pre[128], const unsigned et[20], const float gains[20],
                                   const float in0[240], const float in1[240], float out0[240], float out1[240]) {
    float early[6] = {0}, d0[8] = {0}, d1[8] = {0}, h[2] = {0};
    const unsigned *rt = totk_reverb3d_route(2);
    const int f100 = (totk_audren_w29_fault() == 100);
    for (int k = 0; k < 240; k++) totk_reverb3d_early_accum(pre, 100, 128, et, gains, rt, 0, f100, early);
    const float g[6] = {0.35f, 0.15f, 0.2f, 0.8f, 0.4f, 0.6f};
    unsigned at0 = 0u, at1 = 0u;
    totk_delay_stereo_run(in0, in1, out0, out1, d0, d1, 8u, 8u, &at0, &at1, h, g, 240);
    float s = early[0] + early[1];
    for (int k = 0; k < 240; k++) s += out0[k] + out1[k];
    return s;
}

static uint64_t w29_slow_frame(const AudrenPoolIn *pins, AudrenPoolIn *rpools, uint32_t npools,
                               const uint8_t *voices, w29_voice_slot *sv, uint32_t vc,
                               uint8_t *out, size_t out_sz, const float pre[128],
                               const unsigned et[20], const float gains[20],
                               const float in0[240], const float in1[240], float o0[240], float o1[240]) {
    memset(out, 0, out_sz);
    for (uint32_t i = 0u; i < vc; i++) {
        AudrenVoiceIn v; AudrenVoiceInV2 v2;
        memcpy(&v2, voices + (size_t)i * sizeof v2, sizeof v2);
        memset(&v, 0, sizeof v);
        memcpy(&v, v2.prefix, sizeof v2.prefix);
        memcpy((uint8_t *)&v + 0x3cu, v2.tail, sizeof v2.tail);
        for (unsigned f = 0u; f < 2u; f++) v.biquads[f].enable = v2.biquads[f].enable;
        w29_voice_slot *s = &sv[v.id];
        s->in = v; s->use_fbq = 1u; memcpy(s->fbq, v2.biquads, sizeof v2.biquads);
    }
    AudrenHeader o = {.revision = 0x3f564552u, .memory_pools = npools * 16u, .voices = vc * 16u};
    uint8_t *w = out; memcpy(w, &o, sizeof o); w += sizeof o;
    for (uint32_t i = 0u; i < npools; i++) {
        uint32_t st = pins[i].state;
        if (st == POOL_REQUEST_ATTACH) st = POOL_ATTACHED;
        else if (st == POOL_REQUEST_DETACH) st = POOL_DETACHED;
        rpools[i] = pins[i]; rpools[i].state = st;
        AudrenPoolOut po = {st, {0}}; memcpy(w, &po, sizeof po); w += sizeof po;
    }
    for (uint32_t i = 0u; i < vc; i++) {
        AudrenVoiceOut vo = {sv[i].in.sample_rate, i, 0u}; memcpy(w, &vo, sizeof vo); w += sizeof vo;
    }
    float dsp = w29_slow_reverb_delay(pre, et, gains, in0, in1, o0, o1);
    return (uint64_t)out[sizeof o + npools * 16u - 16u] + (uint64_t)(dsp > 0.0f ? dsp : -dsp) + sv[0].in.id;
}

static uint64_t w29_fast_frame(const AudrenPoolIn *pins, AudrenPoolIn *rpools, uint32_t npools,
                               const uint8_t *voices, const uint8_t *srv_idle, w29_voice_slot *sv,
                               uint32_t vc, uint8_t *out, size_t out_sz, const float pre[128],
                               const unsigned et[20], const float gains[20],
                               const float in0[240], const float in1[240], float o0[240], float o1[240]) {
    const int f99 = (totk_audren_w29_fault() == 99);
    for (uint32_t i = 0u; i < vc; i++) {
        const uint8_t *vp = voices + (size_t)i * sizeof(AudrenVoiceInV2);
        uint32_t vid = 0u;
        if (totk_audren_voice_is_noop_idle(vp, 1, vc, srv_idle[i], f99, &vid)) {
            w29_voice_slot *s = &sv[vid]; s->in.id = vid; s->in.is_new = 0u; s->in.in_use = 0u; s->use_fbq = 0u;
            continue;
        }
        AudrenVoiceIn v; AudrenBiquadFloat fbq[2];
        totk_audren_voice_v2_unpack(&v, fbq, vp);
        w29_voice_slot *s = &sv[v.id]; s->in = v; s->use_fbq = 1u; memcpy(s->fbq, fbq, sizeof fbq);
    }
    AudrenHeader o = {.revision = 0x3f564552u, .memory_pools = npools * 16u, .voices = vc * 16u};
    totk_audren_zero_tail(out, sizeof o + (size_t)o.memory_pools + (size_t)o.voices, out_sz);
    uint8_t *w = out; memcpy(w, &o, sizeof o); w += sizeof o;
    totk_audren_pools_update(rpools, pins, npools, w);
    w += (size_t)npools * sizeof(AudrenPoolOut);
    for (uint32_t i = 0u; i < vc; i++) {
        AudrenVoiceOut vo = {sv[i].in.sample_rate, i, 0u}; memcpy(w, &vo, sizeof vo); w += sizeof vo;
    }
    float dsp = w29_fast_reverb_delay(pre, et, gains, in0, in1, o0, o1);
    return (uint64_t)out[sizeof o + npools * 16u - 16u] + (uint64_t)(dsp > 0.0f ? dsp : -dsp) + sv[0].in.id;
}

static int w29_check_dsp_parity(void) {
    float pre[128], gains[20], e0[6] = {0}, e1[6] = {0};
    unsigned et[20];
    for (int i = 0; i < 128; i++) pre[i] = (float)(i - 64) * 0.01f;
    for (int i = 0; i < 20; i++) { et[i] = (unsigned)(i * 3 + 1); gains[i] = 0.05f * (float)(i + 1); }
    static const unsigned stereo[20] = {0,0,0,1,1,1,1,0,0,0,1,1,1,0,0,0,0,1,1,1};
    for (int i = 0; i < 20; i++) {
        int idx = 100 - (int)et[i]; if (idx < 0) idx += 128;
        e0[stereo[i]] += pre[idx] * gains[i];
    }
    totk_reverb3d_early_accum(pre, 100, 128, et, gains, totk_reverb3d_route(2), 0, totk_audren_w29_fault() == 100, e1);
    if (!w29_floats_close(e0, e1, 6)) return 1;

    float d0a[8] = {0}, d1a[8] = {0}, d0b[8] = {0}, d1b[8] = {0}, ha[2] = {0}, hb[2] = {0};
    float in0[64], in1[64], o0a[64], o1a[64], o0b[64], o1b[64];
    for (int k = 0; k < 64; k++) { in0[k] = (float)(k - 32) * 0.02f; in1[k] = (float)(31 - k) * 0.02f; }
    float base = 0.35f, cross = 0.15f, lp = 0.2f, ing = 0.8f, dryg = 0.4f, outg = 0.6f;
    unsigned at0 = 0u, at1 = 0u, f0 = 0u, f1 = 0u;
    for (int k = 0; k < 64; k++) {
        float x0 = in0[k] * 64.0f, x1 = in1[k] * 64.0f, r0 = d0a[at0], r1 = d1a[at1];
        float v0 = x0 * ing + (r0 * base + r1 * cross), v1 = x1 * ing + (r1 * base + r0 * cross);
        ha[0] = lp * ha[0] + v0 * (1.0f - lp); d0a[at0++] = ha[0]; if (at0 >= 7u) at0 = 0u;
        o0a[k] = (x0 * dryg + r0 * outg) / 64.0f;
        ha[1] = lp * ha[1] + v1 * (1.0f - lp); d1a[at1++] = ha[1]; if (at1 >= 7u) at1 = 0u;
        o1a[k] = (x1 * dryg + r1 * outg) / 64.0f;
    }
    const float g[6] = {base, cross, lp, ing, dryg, outg};
    totk_delay_stereo_run(in0, in1, o0b, o1b, d0b, d1b, 8u, 8u, &f0, &f1, hb, g, 64);
    return (w29_floats_close(o0a, o0b, 64) && w29_floats_close(o1a, o1b, 64)) ? 0 : 1;
}

static int w29_check_fault(int f) {
    totk_audren_reset_for_test(1);
    if (f == 97) {
        uint8_t buf[64]; memset(buf, 0xa5, sizeof buf);
        totk_audren_zero_tail(buf, 16u, sizeof buf);
        return buf[32] == 0u ? 0 : 1;
    }
    if (f == 98) {
        AudrenPoolIn s = {.address = 0x1000u, .size = 0x1000u, .state = POOL_DETACHED};
        AudrenPoolIn in = {.address = 0x1000u, .size = 0x1000u, .state = POOL_REQUEST_ATTACH};
        uint8_t w[16] = {0}; AudrenPoolOut po = {0};
        totk_audren_pools_update(&s, &in, 1u, w);
        memcpy(&po, w, sizeof po);
        return (s.state == POOL_ATTACHED && po.state == POOL_ATTACHED) ? 0 : 1;
    }
    if (f == 99) {
        AudrenVoiceInV2 v2; memset(&v2, 0, sizeof v2); v2.prefix[0] = 3u;
        uint32_t id = 0u;
        return totk_audren_voice_is_noop_idle((const uint8_t *)&v2, 1, 256u, 0, totk_audren_w29_fault() == 99, &id) == 0 ? 0 : 1;
    }
    if (f == 100 || f == 101) return w29_check_dsp_parity();
    return 1;
}

static void *w29_tsan_worker(void *arg) {
    (void)arg;
    for (int i = 0; i < 400; i++) assert(w29_check_dsp_parity() == 0);
    return NULL;
}

static int run_ktgl_w29_tests(int fault_mode) {
    if (fault_mode >= 97 && fault_mode <= 101) return w29_check_fault(fault_mode);
    totk_audren_reset_for_test(1);
    assert(w29_check_dsp_parity() == 0);

    enum { NP = 512, NV = 128, OUT_SZ = 0x40 + (NP + NV) * 16 + 2048 };
    static AudrenPoolIn pins[NP], rp0[NP], rp1[NP];
    static AudrenVoiceInV2 v2s[NV];
    static w29_voice_slot sv0[NV], sv1[NV];
    static uint8_t srv_idle[NV], out0[OUT_SZ], out1[OUT_SZ];
    float pre[128], gains[20], in0[240], in1[240], o0[240], o1[240];
    unsigned et[20];
    for (int i = 0; i < 128; i++) pre[i] = (float)(i - 64) * 0.005f;
    for (int i = 0; i < 20; i++) { et[i] = (unsigned)(i * 3 + 1); gains[i] = 0.04f; }
    for (int i = 0; i < 240; i++) { in0[i] = (float)(i - 120) * (0.25f / 64.0f); in1[i] = (float)(119 - i) * (0.25f / 64.0f); }
    memset(pins, 0, sizeof pins); memset(rp0, 0, sizeof rp0); memset(rp1, 0, sizeof rp1);
    memset(v2s, 0, sizeof v2s); memset(sv0, 0, sizeof sv0); memset(sv1, 0, sizeof sv1);
    for (uint32_t i = 0u; i < NP; i++) {
        pins[i].address = 0x10000ull + i * 0x1000ull; pins[i].size = 0x1000ull;
        pins[i].state = (i == 7u) ? POOL_REQUEST_ATTACH : POOL_ATTACHED;
        rp0[i] = pins[i]; rp0[i].state = POOL_ATTACHED; rp1[i] = rp0[i];
    }
    for (uint32_t i = 0u; i < NV; i++) {
        memcpy(v2s[i].prefix, &i, 4u); v2s[i].prefix[9] = (i < 24u) ? 1u : 0u;
        srv_idle[i] = (i >= 24u) ? 1u : 0u;
    }
    memset(out0, 0x5a, sizeof out0); memset(out1, 0x5a, sizeof out1);
    w29_slow_frame(pins, rp0, NP, (const uint8_t *)v2s, sv0, NV, out0, OUT_SZ, pre, et, gains, in0, in1, o0, o1);
    float o0f[240], o1f[240];
    w29_fast_frame(pins, rp1, NP, (const uint8_t *)v2s, srv_idle, sv1, NV, out1, OUT_SZ, pre, et, gains, in0, in1, o0f, o1f);
    assert(memcmp(out0, out1, OUT_SZ) == 0 && memcmp(rp0, rp1, sizeof rp0) == 0);
    assert(w29_floats_close(o0, o0f, 240) && w29_floats_close(o1, o1f, 240));
    for (uint32_t i = 0u; i < NV; i++) assert(memcmp(&sv0[i].in, &sv1[i].in, sizeof sv0[i].in) == 0);

    pthread_t th[4];
    for (int i = 0; i < 4; i++) assert(pthread_create(&th[i], NULL, w29_tsan_worker, NULL) == 0);
    for (int i = 0; i < 4; i++) assert(pthread_join(th[i], NULL) == 0);

    for (int mode = 0; mode < 2; mode++) {
        totk_audren_reset_for_test(mode);
        volatile uint64_t sink = 0u;
        uint64_t t0 = host_now_ns();
        for (int it = 0; it < 2000; it++) {
            sink += mode
                ? w29_fast_frame(pins, rp1, NP, (const uint8_t *)v2s, srv_idle, sv1, NV, out1, OUT_SZ, pre, et, gains, in0, in1, o0f, o1f)
                : w29_slow_frame(pins, rp0, NP, (const uint8_t *)v2s, sv0, NV, out0, OUT_SZ, pre, et, gains, in0, in1, o0, o1);
        }
        printf("aoc_audren_w29(%s): elapsed=%.3f ms sink=%llu\n", mode ? "on" : "off",
               (double)(host_now_ns() - t0) / 1e6, (unsigned long long)sink);
    }
    return 0;
}
