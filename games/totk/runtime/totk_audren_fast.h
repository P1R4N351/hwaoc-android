/* TOTK_AUDREN_FAST sparse output/pool update, idle voice skip & hoisted DSP fast paths (Opt 31). */
#pragma once
#include "audio_renderer.h"
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(AudrenPoolOut) == 16u && sizeof(AudrenVoiceOut) == 16u, "audren out wire");
_Static_assert(offsetof(AudrenVoiceIn, biquads) == 0x24u, "voice biquads offset");
_Static_assert(sizeof(((AudrenVoiceIn *)0)->biquads) == 0x18u, "voice biquads size");
_Static_assert(sizeof(AudrenVoiceIn) - 0x3cu == 0x134u, "voice tail size");
_Static_assert(offsetof(AudrenVoiceInV2, biquads) == 0x24u, "voice v2 biquads offset");
_Static_assert(offsetof(AudrenVoiceInV2, tail) == 0x54u, "voice v2 tail offset");

static inline int totk_audren_fast_enabled(void) {
    static _Atomic int on = -1;
    int v = atomic_load_explicit(&on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("TOTK_AUDREN_FAST");
        v = (e && e[0] == '0' && e[1] == '\0') ? 0 : 1;
        atomic_store_explicit(&on, v, memory_order_release);
    }
    return v;
}

#if defined(TOTK_AUDREN_FAST_TEST) || defined(TOTK_KTGL_OPT_TEST)
static _Atomic int g_totk_audren_override = -1;

static inline void totk_audren_reset_for_test(int en) {
    atomic_store_explicit(&g_totk_audren_override, en, memory_order_release);
}

static inline int totk_audren_fast_active(void) {
    int ov = atomic_load_explicit(&g_totk_audren_override, memory_order_acquire);
    return ov >= 0 ? ov : totk_audren_fast_enabled();
}

static inline int totk_audren_w29_fault(void) {
    static _Atomic int gf = -1;
    int v = atomic_load_explicit(&gf, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("TOTK_KTGL_OPT_FAULT");
        v = e ? atoi(e) : 0;
        atomic_store_explicit(&gf, v, memory_order_release);
    }
    return v;
}
#else
static inline int totk_audren_fast_active(void) { return totk_audren_fast_enabled(); }
static inline int totk_audren_w29_fault(void) { return 0; }
#endif

static inline void totk_audren_zero_tail(uint8_t *output, size_t effects_off, size_t out_size) {
    if (totk_audren_w29_fault() == 97 || !output || effects_off >= out_size) return;
    memset(output + effects_off, 0, out_size - effects_off);
}

static inline void totk_audren_pools_update(AudrenPoolIn *slots, const AudrenPoolIn *ins,
                                            uint32_t npools, uint8_t *w) {
    const int f98 = (totk_audren_w29_fault() == 98);
    for (uint32_t i = 0u; i < npools; i++) {
        uint32_t s = ins[i].state;
        if (!f98) {
            if (s == POOL_REQUEST_ATTACH) s = POOL_ATTACHED;
            else if (s == POOL_REQUEST_DETACH) s = POOL_DETACHED;
        }
        if (slots[i].state != s || slots[i].address != ins[i].address || slots[i].size != ins[i].size) {
            slots[i] = ins[i];
            slots[i].state = s;
        }
        AudrenPoolOut po = {s, {0}};
        memcpy(w + (size_t)i * sizeof po, &po, sizeof po);
    }
}

static inline int totk_audren_voice_is_noop_idle(const uint8_t *vp, int float_voices,
                                                 uint32_t vc, int srv_stopped_idle,
                                                 int f99, uint32_t *id_out) {
    uint32_t vid = 0u;
    memcpy(&vid, vp, sizeof vid);
    if (vid >= vc) return 0;
    if (vp[8] != 0u || vp[9] != 0u) return 0;
    if (float_voices && (vp[0x24] != 0u || vp[0x3c] != 0u)) return 0;
    if (!f99 && !srv_stopped_idle) return 0;
    *id_out = vid;
    return 1;
}

static inline void totk_audren_voice_v2_unpack(AudrenVoiceIn *v, AudrenBiquadFloat fbq[2],
                                               const uint8_t *vp) {
    memcpy(v, vp, 0x24u);
    memset(v->biquads, 0, sizeof v->biquads);
    memcpy((uint8_t *)v + 0x3cu, vp + 0x54u, 0x134u);
    memcpy(fbq, vp + 0x24u, 0x30u);
}

static inline const unsigned *totk_reverb3d_route(int channels) {
    static const unsigned mono[20] = {0};
    static const unsigned stereo[20] = {0,0,0,1,1,1,1,0,0,0,1,1,1,0,0,0,0,1,1,1};
    static const unsigned quad[20] = {0,0,0,1,1,1,1,2,2,2,1,1,1,0,0,0,0,3,3,3};
    static const unsigned surround[20] = {4,5,0,5,0,5,1,5,1,5,1,5,1,5,2,5,2,5,2,5};
    return channels == 1 ? mono : channels == 2 ? stereo : channels == 4 ? quad : surround;
}

static inline void totk_reverb3d_early_accum(const float *pre_data, int pre_w, int pre_wrap,
                                             const unsigned early_time[20],
                                             const float early_gains[20],
                                             const unsigned route[20], int is_surround,
                                             int f100, float early[6]) {
    for (int i = 0; i < 20; i++) {
        int idx = pre_w - (int)early_time[i];
        if (idx < 0) idx += pre_wrap;
        float x = pre_data[idx] * early_gains[i];
        unsigned o = f100 ? (route[i] ^ 1u) : route[i];
        early[o] += x;
        if (is_surround) early[o] += x;
    }
}

static inline void totk_delay_stereo_run(const float *in0, const float *in1, float *out0, float *out1,
                                         float *data0, float *data1, unsigned c0, unsigned c1,
                                         unsigned *at0_io, unsigned *at1_io, float hist[2],
                                         const float g[6], int frames) {
    unsigned at0 = *at0_io, at1 = *at1_io;
    unsigned lim0 = c0 ? c0 - 1u : 0u, lim1 = c1 ? c1 - 1u : 0u;
    float base = g[0], cross = g[1], lp = g[2], omp = 1.0f - lp;
    float in_g = g[3], dry_g = g[4], out_g = g[5];
    int f101 = (totk_audren_w29_fault() == 101);
    for (int k = 0; k < frames; k++) {
        float x0 = in0[k] * 64.0f, x1 = in1[k] * 64.0f;
        float d0 = data0[at0], d1 = data1[at1];
        float fb0 = d0 * base + (f101 ? 0.0f : d1 * cross);
        float fb1 = d1 * base + (f101 ? 0.0f : d0 * cross);
        float z0 = lp * hist[0] + (x0 * in_g + fb0) * omp;
        hist[0] = z0; data0[at0++] = z0; if (at0 >= lim0) at0 = 0u;
        out0[k] = (x0 * dry_g + d0 * out_g) / 64.0f;
        float z1 = lp * hist[1] + (x1 * in_g + fb1) * omp;
        hist[1] = z1; data1[at1++] = z1; if (at1 >= lim1) at1 = 0u;
        out1[k] = (x1 * dry_g + d1 * out_g) / 64.0f;
    }
    *at0_io = at0; *at1_io = at1;
}
