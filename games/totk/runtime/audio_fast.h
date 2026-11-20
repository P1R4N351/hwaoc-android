/* Zero-overhead hot-path helpers for audio_mixer.c (TOTK_AUDIO_FAST=1, default on). */
#pragma once
#include <assert.h>
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#endif

static _Atomic int g_totk_audio_fast = -1;
#ifdef TOTK_AUDIO_FAST_TEST
static int g_totk_audio_fault;
#define TOTK_AUD_FAULT() (g_totk_audio_fault)
#else
#define TOTK_AUD_FAULT() (0)
#endif

static inline int totk_audio_fast_sync(void) {
    const char *e = getenv("TOTK_AUDIO_FAST");
    int on = !(e && e[0] == '0' && !e[1]);
    atomic_store_explicit(&g_totk_audio_fast, on, memory_order_release);
    return on;
}

static inline int totk_audio_fast_enabled(void) {
    int v = atomic_load_explicit(&g_totk_audio_fast, memory_order_acquire);
    return v >= 0 ? v : totk_audio_fast_sync();
}

static inline void totk_ring_push_stereo(float *ring, uint32_t cap_frames, uint64_t wr,
                                         const float *lr, uint32_t frames) {
    if (!frames) return;
    assert(ring && lr && cap_frames > 0 && frames <= cap_frames);
    uint32_t idx = (uint32_t)(wr % cap_frames);
    uint32_t first = frames <= cap_frames - idx ? frames : cap_frames - idx;
    memcpy(ring + (size_t)idx * 2u, lr, (size_t)first * 2u * sizeof(float));
    if (frames > first && TOTK_AUD_FAULT() != 4)
        memcpy(ring, lr + (size_t)first * 2u, (size_t)(frames - first) * 2u * sizeof(float));
}

static inline uint64_t totk_count_nonzero_stereo(const float *out, uint32_t frames) {
    uint64_t nz = 0;
    uint32_t i = 0;
    uint32_t mask = (TOTK_AUD_FAULT() == 1) ? 0xffffffffu : 0x7fffffffu;
#if defined(__aarch64__) && defined(__ARM_NEON)
    const uint32x4_t vmask = vdupq_n_u32(mask);
    for (; i + 4u <= frames; i += 4u) {
        uint32x4x2_t p = vld2q_u32((const uint32_t *)(out + (size_t)i * 2u));
        uint32x4_t any = vandq_u32(vorrq_u32(p.val[0], p.val[1]), vmask);
        uint32x4_t tz = vtstq_u32(any, any);
        nz += (uint64_t)vaddvq_u32(vshrq_n_u32(tz, 31));
    }
#endif
    for (; i < frames; i++) {
        uint32_t l, r;
        memcpy(&l, out + (size_t)i * 2u, 4);
        memcpy(&r, out + (size_t)i * 2u + 1u, 4);
        if (((l | r) & mask) != 0u) nz++;
    }
    return nz;
}

static inline void totk_ring_pull_stereo(const float *ring, uint32_t cap_frames, uint64_t rd,
                                         float *out, uint32_t frames, uint64_t *nonzero_out) {
    if (!frames) { *nonzero_out = 0; return; }
    assert(ring && out && cap_frames > 0 && frames <= cap_frames);
    uint32_t idx = (uint32_t)(rd % cap_frames);
    uint32_t first = frames <= cap_frames - idx ? frames : cap_frames - idx;
    memcpy(out, ring + (size_t)idx * 2u, (size_t)first * 2u * sizeof(float));
    if (frames > first && TOTK_AUD_FAULT() != 4)
        memcpy(out + (size_t)first * 2u, ring, (size_t)(frames - first) * 2u * sizeof(float));
    *nonzero_out = totk_count_nonzero_stereo(out, frames);
}

static inline void totk_mix_accumulate(float * restrict dst, const float * restrict src,
                                       float vol, int frames) {
    if (vol == 0.0f || frames <= 0) return;
    int k = 0;
    if (vol == 1.0f) {
#if defined(__aarch64__) && defined(__ARM_NEON)
        for (; k + 4 <= frames; k += 4)
            vst1q_f32(dst + k, vaddq_f32(vld1q_f32(dst + k), vld1q_f32(src + k)));
#endif
        for (; k < frames; k++) dst[k] += src[k];
        return;
    }
#if defined(__aarch64__) && defined(__ARM_NEON)
    for (; k + 4 <= frames; k += 4)
        vst1q_f32(dst + k, vfmaq_n_f32(vld1q_f32(dst + k), vld1q_f32(src + k), vol));
#endif
    for (; k < frames; k++) dst[k] += src[k] * vol;
}

static inline int totk_voice_can_skip_silent(int fmt, float vol, uint32_t splitter_id,
                                             uint8_t bq0, uint8_t bq1) {
    if (vol != 0.0f || splitter_id != SPLITTER_UNUSED) return 0;
    if (TOTK_AUD_FAULT() == 3) return 1;
    return fmt != FMT_ADPCM && !bq0 && !bq1;
}

static inline void totk_pcm16_unit_render(const int16_t *pcm, int cc,
                                          float out[6][MAX_FRAME], int frames) {
    int k = 0;
#if defined(__aarch64__) && defined(__ARM_NEON)
    const float inv = 1.0f / 32768.0f;
#endif
    if (cc == 1) {
#if defined(__aarch64__) && defined(__ARM_NEON)
        for (; k + 8 <= frames; k += 8) {
            int16x8_t v = vld1q_s16(pcm + k);
            vst1q_f32(&out[0][k],     vmulq_n_f32(vcvtq_f32_s32(vmovl_s16(vget_low_s16(v))), inv));
            vst1q_f32(&out[0][k + 4], vmulq_n_f32(vcvtq_f32_s32(vmovl_high_s16(v)), inv));
        }
#endif
        for (; k < frames; k++) out[0][k] = pcm[k] / 32768.0f;
        return;
    }
#if defined(__aarch64__) && defined(__ARM_NEON)
    for (; k + 8 <= frames; k += 8) {
        int16x8x2_t v = vld2q_s16(pcm + (size_t)k * 2u);
        vst1q_f32(&out[0][k],     vmulq_n_f32(vcvtq_f32_s32(vmovl_s16(vget_low_s16(v.val[0]))), inv));
        vst1q_f32(&out[0][k + 4], vmulq_n_f32(vcvtq_f32_s32(vmovl_high_s16(v.val[0])), inv));
        vst1q_f32(&out[1][k],     vmulq_n_f32(vcvtq_f32_s32(vmovl_s16(vget_low_s16(v.val[1]))), inv));
        vst1q_f32(&out[1][k + 4], vmulq_n_f32(vcvtq_f32_s32(vmovl_high_s16(v.val[1])), inv));
    }
#endif
    for (; k < frames; k++) {
        out[0][k] = pcm[(size_t)k * 2u] / 32768.0f;
        out[1][k] = pcm[(size_t)k * 2u + 1u] / 32768.0f;
    }
}

static inline void totk_pcm16_interp_render(const int16_t *pcm, int cc, double step,
                                            double frac, float out[6][MAX_FRAME], int frames) {
    uint64_t pos = 0;
    const float inv = 1.0f / 32768.0f;
    for (int k = 0; k < frames; k++) {
        size_t i0 = (size_t)pos * (size_t)cc, i1 = (size_t)(pos + 1u) * (size_t)cc;
        for (int c = 0; c < cc; c++) {
            float s0 = pcm[i0 + (size_t)c] * inv, s1 = pcm[i1 + (size_t)c] * inv;
            out[c][k] = (float)(s0 + (s1 - s0) * frac);
        }
        frac += step;
        while (frac >= 1.0) { frac -= 1.0; pos++; }
    }
}

static inline void totk_sink_clamp_store(float * restrict lr, int k, float l, float r) {
    lr[2 * k]      = l > 1.0f ? 1.0f : l < -1.0f ? -1.0f : l;
    lr[2 * k + 1]  = r > 1.0f ? 1.0f : r < -1.0f ? -1.0f : r;
}

static inline void totk_sink_downmix_stereo(float * restrict lr, const float * const ch[6],
                                            uint32_t input_count, int downmix_enabled,
                                            const float downmix[4], float gain, int frames) {
    if (frames <= 0) return;
    if (input_count >= 6u && ch[0] && ch[1] && ch[2] && ch[4] && ch[5]) {
        const float ff = downmix_enabled ? downmix[0] : 1.0f;
        const float cf = downmix_enabled ? downmix[1] : 0.7071f;
        const float sf = downmix_enabled ? downmix[2] : 0.7071f;
        int k = 0;
#if defined(__aarch64__) && defined(__ARM_NEON)
        const float32x4_t vlo = vdupq_n_f32(-1.0f), vhi = vdupq_n_f32(1.0f);
        for (; k + 4 <= frames; k += 4) {
            float32x4_t vc = vld1q_f32(ch[2] + k);
            float32x4_t vl = vfmaq_n_f32(vfmaq_n_f32(vmulq_n_f32(vld1q_f32(ch[0] + k), ff), vc, cf), vld1q_f32(ch[4] + k), sf);
            float32x4_t vr = vfmaq_n_f32(vfmaq_n_f32(vmulq_n_f32(vld1q_f32(ch[1] + k), ff), vc, cf), vld1q_f32(ch[5] + k), sf);
            float32x4x2_t st;
            st.val[0] = vminq_f32(vmaxq_f32(vmulq_n_f32(vl, gain), vlo), vhi);
            st.val[1] = vminq_f32(vmaxq_f32(vmulq_n_f32(vr, gain), vlo), vhi);
            vst2q_f32(lr + (size_t)k * 2u, st);
        }
#endif
        for (; k < frames; k++) {
            float c = ch[2][k];
            float l = (ff * ch[0][k] + cf * c + sf * ch[4][k]) * gain;
            float r = (ff * ch[1][k] + cf * c + sf * ch[5][k]) * gain;
            totk_sink_clamp_store(lr, k, l, r);
        }
        return;
    }
    if (input_count >= 2u && ch[0] && ch[1]) {
        int k = 0;
#if defined(__aarch64__) && defined(__ARM_NEON)
        const float32x4_t vlo = vdupq_n_f32(-1.0f), vhi = vdupq_n_f32(1.0f);
        for (; k + 4 <= frames; k += 4) {
            float32x4x2_t st;
            st.val[0] = vminq_f32(vmaxq_f32(vmulq_n_f32(vld1q_f32(ch[0] + k), gain), vlo), vhi);
            st.val[1] = vminq_f32(vmaxq_f32(vmulq_n_f32(vld1q_f32(ch[1] + k), gain), vlo), vhi);
            vst2q_f32(lr + (size_t)k * 2u, st);
        }
#endif
        for (; k < frames; k++) totk_sink_clamp_store(lr, k, ch[0][k] * gain, ch[1][k] * gain);
        return;
    }
    for (int k = 0; k < frames; k++) {
        float l, r;
        if (input_count >= 6u) {
            float ff = downmix_enabled ? downmix[0] : 1.0f, cf = downmix_enabled ? downmix[1] : 0.7071f, sf = downmix_enabled ? downmix[2] : 0.7071f;
            float C = ch[2] ? ch[2][k] : 0.0f;
            l = ff * (ch[0] ? ch[0][k] : 0.0f) + cf * C + sf * (ch[4] ? ch[4][k] : 0.0f);
            r = ff * (ch[1] ? ch[1][k] : 0.0f) + cf * C + sf * (ch[5] ? ch[5][k] : 0.0f);
        } else if (input_count >= 2u) { l = ch[0] ? ch[0][k] : 0.0f; r = ch[1] ? ch[1][k] : 0.0f; }
        else { l = r = ch[0] ? ch[0][k] : 0.0f; }
        totk_sink_clamp_store(lr, k, l * gain, r * gain);
    }
}
