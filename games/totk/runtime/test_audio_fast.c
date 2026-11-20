/* Standalone host equivalence, benchmark, and Delta #27 fault test for audio_fast.h. */
#if !defined(__APPLE__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#define TOTK_AUDIO_FAST_TEST 1
#include "audio_mixer.c"
#include <time.h>

_Noreturn void runtime_fail(const char *s) { fprintf(stderr, "FAIL runtime_fail: %s\n", s); exit(2); }
void *ipc_guest(uint64_t at, uint64_t n) { (void)n; return (void *)(uintptr_t)at; }
void dm_guest_write_prepare(void *p, size_t n) { (void)p; (void)n; }
int audio_buffer_empty(const AudrenWaveBuffer *b) { return !b->address || !b->size || b->end <= b->start; }
const void *ipc_arg(const IpcRequest *r, uint32_t a, uint32_t n) { (void)r; (void)a; (void)n; exit(2); }
void ipc_out(IpcResponse *r, const void *p, uint32_t n) { (void)r; (void)p; (void)n; exit(2); }
_Noreturn void ipc_unimplemented(const IpcSession *s, const IpcRequest *r) { (void)s; (void)r; exit(2); }
uint32_t runtime_event_new(void) { return 1u; }
uint32_t runtime_event_readable(uint32_t h) { return h; }

static uint64_t af_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static AudioRenderer af_make_renderer(int frames, int voices, int submixes, int nbuf) {
    AudioRenderer r = {0};
    r.param = (AudrenParameter){
        .sample_rate = 48000, .sample_count = frames, .mix_buffer_count = nbuf,
        .submix_count = submixes, .voice_count = voices, .sink_count = 1, .effect_count = 0
    };
    r.revision = 0x3b564552u;
    r.voices = calloc((size_t)voices, sizeof *r.voices);
    r.channels = calloc((size_t)voices * 6u, sizeof *r.channels);
    r.mixes = calloc((size_t)submixes + 1u, sizeof *r.mixes);
    r.sinks = calloc(1u, sizeof *r.sinks);
    assert(r.voices && r.channels && r.mixes && r.sinks);
    r.mixes[0] = (AudrenMixIn){.volume = 1.0f, .buffer_count = 6, .in_use = 1, .dest_mix_id = -1, .dest_splitter_id = SPLITTER_UNUSED};
    audio_mixer_open(&r);
    return r;
}

static void af_free_renderer(AudioRenderer *r) {
    audio_mixer_close(r);
    free(r->voices); free(r->channels); free(r->mixes); free(r->sinks);
}

static void af_advance_voice(ServerVoice *v, double secs) {
    double want = v->fraction + secs * v->in.sample_rate * (v->in.pitch > 0 ? v->in.pitch : 1.0);
    uint64_t samples = (uint64_t)want;
    v->fraction = want - (double)samples;
    AudrenWaveBuffer *b = &v->buffers[v->index];
    uint64_t len = (uint64_t)(b->end - b->start);
    if (v->offset + samples < len) { v->offset += samples; return; }
    if (b->loop && (b->loop_count < 0 || v->loops_done < b->loop_count)) {
        v->loops_done++; v->offset = 0; return;
    }
    v->valid[v->index] = 0; v->offset = 0;
}

static int test_ring_push_pull(void) {
    Mixer *m = calloc(1, sizeof *m);
    assert(m);
    m->frame = 64; m->unit_ok = 1; m->ready = 1;
    atomic_store(&m->playing, 1);
    for (int i = 0; i < RING_FRAMES * 2; i++) m->ring[i] = 0.777f;
    float in[128];
    for (int i = 0; i < 64; i++) {
        if (i < 16) { in[2 * i] = -0.0f; in[2 * i + 1] = +0.0f; }
        else if (i < 32) { in[2 * i] = -0.0f; in[2 * i + 1] = 0.25f; }
        else { in[2 * i] = (float)(i - 32) * 0.01f; in[2 * i + 1] = -(float)(i - 32) * 0.01f; }
    }
    float out_ref[128] = {0}, out_fast[128] = {0};
    uint64_t nz_ref = 0, nz_fast = 0;
    uint64_t wr = (uint64_t)RING_FRAMES - 10u;
    setenv("TOTK_AUDIO_FAST", "0", 1); totk_audio_fast_sync();
    atomic_store(&m->wr, wr); atomic_store(&m->rd, wr);
    output_push(m, in, 64);
    m->unit_ok = 1; m->ready = 1; atomic_store(&m->callback_nonzero, 0);
    pull_frames(m, out_ref, 64);
    nz_ref = atomic_load(&m->callback_nonzero);
    assert(nz_ref == 47u);   /* 0..15 (-0,+0) and 32 (+0,-0) are 17 zero stereo frames; 47 nonzero */

    for (int i = 0; i < RING_FRAMES * 2; i++) m->ring[i] = 0.777f;
    setenv("TOTK_AUDIO_FAST", "1", 1); totk_audio_fast_sync();
    m->unit_ok = 1; m->ready = 1;
    atomic_store(&m->wr, wr); atomic_store(&m->rd, wr);
    output_push(m, in, 64);
    m->unit_ok = 1; m->ready = 1; atomic_store(&m->callback_nonzero, 0);
    pull_frames(m, out_fast, 64);
    nz_fast = atomic_load(&m->callback_nonzero);

    if (memcmp(out_ref, out_fast, sizeof out_ref) != 0 || nz_ref != nz_fast || nz_fast != 47u) {
        fprintf(stderr, "FAIL: ring wrap parity mismatch (nz_ref=%llu nz_fast=%llu memdiff=%d fault=%d)\n",
                (unsigned long long)nz_ref, (unsigned long long)nz_fast, memcmp(out_ref, out_fast, sizeof out_ref), g_totk_audio_fault);
        free(m); return 1;
    }
    free(m);
    return 0;
}

static void fill_pcm16_fixture(int16_t *pcm, int count) {
    for (int i = 0; i < count; i++) pcm[i] = (int16_t)(((i * 197 + 13) % 60000) - 30000);
}

static void fill_adpcm_fixture(uint8_t *buf, int nframes, int16_t coef[16]) {
    for (int c = 0; c < 8; c++) { coef[2 * c] = (int16_t)(1600 - c * 180); coef[2 * c + 1] = (int16_t)(-650 + c * 90); }
    for (int f = 0; f < nframes; f++) {
        buf[f * 8] = (uint8_t)(((f % 8) << 4) | ((f % 4) + 1));
        for (int b = 1; b < 8; b++) buf[f * 8 + b] = (uint8_t)(((f * 29 + b * 17) & 0xff));
    }
}

static int compare_voice_case(int fmt, int cc, uint32_t rate, float pitch, int with_ctx, int loop) {
    enum { NFRAMES = 12, F = 240 };
    static int16_t pcm[16384];
    static uint8_t adpcm[8 * 512];
    static int16_t coef[16], ctx[3] = {0x0102, 400, -250};
    fill_pcm16_fixture(pcm, 16384);
    fill_adpcm_fixture(adpcm, 512, coef);

    AudioRenderer r = af_make_renderer(F, 1, 0, 6);
    float out_ref[6][MAX_FRAME], out_fast[6][MAX_FRAME];
    ServerVoice base = {0};
    base.in.in_use = 1; base.in.sample_format = (uint8_t)fmt; base.in.channel_count = (uint32_t)cc;
    base.in.sample_rate = rate; base.in.pitch = pitch; base.in.volume = 1.0f;
    base.state = SV_STARTED; base.valid[0] = 1; base.adpcm.buffer = -1;
    base.buffers[0] = (AudrenWaveBuffer){
        .address = fmt == FMT_ADPCM ? (uintptr_t)adpcm : (uintptr_t)pcm,
        .size = fmt == FMT_ADPCM ? sizeof adpcm : sizeof pcm,
        .start = with_ctx ? 5 : 0,
        .end = loop ? 360 : 6000,
        .loop = (uint8_t)loop, .loop_count = loop ? -1 : 0,
        .context_address = with_ctx ? (uintptr_t)ctx : 0,
        .context_size = with_ctx ? sizeof ctx : 0
    };
    if (fmt == FMT_ADPCM) { base.in.extra_address = (uintptr_t)coef; base.in.extra_size = sizeof coef; }

    ServerVoice va = base, vb = base;
    for (int f = 0; f < NFRAMES; f++) {
        memset(out_ref, 0, sizeof out_ref);
        memset(out_fast, 0, sizeof out_fast);
        setenv("TOTK_AUDIO_FAST", "0", 1); totk_audio_fast_sync();
        voice_render(&r, &va, out_ref, F);
        setenv("TOTK_AUDIO_FAST", "1", 1); totk_audio_fast_sync();
        voice_render(&r, &vb, out_fast, F);
        if (memcmp(out_ref, out_fast, sizeof out_ref) != 0 ||
            memcmp(&va.adpcm, &vb.adpcm, sizeof va.adpcm) != 0) {
            fprintf(stderr, "FAIL: voice parity fmt=%d cc=%d rate=%u pitch=%.2f ctx=%d loop=%d frame=%d (at %llu vs %llu fault=%d)\n",
                    fmt, cc, rate, pitch, with_ctx, loop, f,
                    (unsigned long long)va.adpcm.at, (unsigned long long)vb.adpcm.at, g_totk_audio_fault);
            af_free_renderer(&r); return 1;
        }
        af_advance_voice(&va, (double)F / 48000.0);
        af_advance_voice(&vb, (double)F / 48000.0);
    }
    af_free_renderer(&r);
    return 0;
}

static int test_voices_all(void) {
    int rc = 0;
    rc |= compare_voice_case(FMT_PCM16, 1, 48000, 1.0f, 0, 0);
    rc |= compare_voice_case(FMT_PCM16, 2, 48000, 1.0f, 0, 0);
    rc |= compare_voice_case(FMT_PCM16, 1, 24000, 1.0f, 0, 0);
    rc |= compare_voice_case(FMT_PCM16, 2, 48000, 1.375f, 0, 1);
    rc |= compare_voice_case(FMT_ADPCM, 1, 24000, 1.0f, 0, 0);
    rc |= compare_voice_case(FMT_ADPCM, 1, 48000, 1.0f, 1, 0);
    rc |= compare_voice_case(FMT_ADPCM, 1, 48000, 1.73f, 0, 0);
    return rc;
}

static void setup_unmute_scene(AudioRenderer *r, int16_t *pcm, uint8_t *adpcm, int16_t coef[16]) {
    for (int i = 0; i < 3; i++) {
        ServerVoice *v = &r->voices[i];
        v->in.in_use = 1; v->state = SV_STARTED; v->in.sample_rate = 48000; v->in.pitch = 1.0f;
        v->in.channel_count = 1; v->in.volume = 0.0f; v->in.splitter_id = SPLITTER_UNUSED; v->in.mix_id = 0;
        v->valid[0] = 1; v->adpcm.buffer = -1;
        if (i == 0) {
            v->in.sample_format = FMT_ADPCM; v->in.extra_address = (uintptr_t)coef; v->in.extra_size = 32;
            v->buffers[0] = (AudrenWaveBuffer){.address = (uintptr_t)adpcm, .size = 8 * 512, .end = 3500};
        } else {
            v->in.sample_format = FMT_PCM16;
            v->buffers[0] = (AudrenWaveBuffer){.address = (uintptr_t)pcm, .size = 32768, .end = 8000};
            if (i == 1) { v->in.biquads[0] = (AudrenBiquad){.enable = 1, .numerator = {8192, 4096, 0}, .denominator = {4096, 0}}; }
        }
        r->channels[i * 6].mix[0] = 0.5f; r->channels[i * 6].mix[1] = 0.5f;
    }
    r->sinks[0] = (AudrenSinkIn){.type = SINK_DEVICE, .in_use = 1, .input_count = 2, .inputs = {0, 1}};
}

static int test_silent_unmute(void) {
    enum { F = 240, NFRAMES = 8 };
    static int16_t pcm[16384], coef[16];
    static uint8_t adpcm[8 * 512];
    fill_pcm16_fixture(pcm, 16384);
    fill_adpcm_fixture(adpcm, 512, coef);

    float out[2][NFRAMES * F * 2];
    for (int mode = 0; mode < 2; mode++) {
        setenv("TOTK_AUDIO_FAST", mode ? "1" : "0", 1); totk_audio_fast_sync();
        AudioRenderer r = af_make_renderer(F, 3, 0, 6);
        setup_unmute_scene(&r, pcm, adpcm, coef);
        for (int f = 0; f < NFRAMES; f++) {
            if (f == 5) { r.voices[0].in.volume = 0.8f; r.voices[1].in.volume = 0.6f; r.voices[2].in.volume = 0.4f; }
            audio_mixer_render(&r, (double)F / 48000.0);
            memcpy(out[mode] + (size_t)f * F * 2, ((Mixer *)r.mixer)->output_scratch, (size_t)F * 2 * sizeof(float));
            for (int i = 0; i < 3; i++) af_advance_voice(&r.voices[i], (double)F / 48000.0);
        }
        af_free_renderer(&r);
    }
    for (int i = 0; i < NFRAMES * F * 2; i++) {
        if (fabsf(out[0][i] - out[1][i]) > 1e-6f) {
            fprintf(stderr, "FAIL: silent unmute mismatch at %d ref=%.8g fast=%.8g (fault=%d)\n",
                    i, out[0][i], out[1][i], g_totk_audio_fault);
            return 1;
        }
    }
    return 0;
}

static void setup_bench_scene(AudioRenderer *r, int16_t *pcm, uint8_t *adpcm, int16_t coef[16]) {
    for (int s = 1; s <= 4; s++) {
        r->mixes[s] = (AudrenMixIn){.volume = s < 4 ? 0.5f : 0.0f, .buffer_count = 6, .in_use = 1, .dest_mix_id = 0, .dest_splitter_id = SPLITTER_UNUSED};
        for (int c = 0; c < 6; c++) r->mixes[s].mix[c][c] = 1.0f;
    }
    for (int i = 0; i < 48; i++) {
        ServerVoice *v = &r->voices[i];
        v->in.in_use = 1; v->state = SV_STARTED; v->in.splitter_id = SPLITTER_UNUSED;
        v->in.mix_id = (i % 3) + 1; v->valid[0] = 1; v->adpcm.buffer = -1;
        v->in.pitch = 1.0f;
        if (i < 16) {
            v->in.sample_format = FMT_PCM16; v->in.channel_count = (i & 1) ? 2u : 1u; v->in.sample_rate = 48000;
            v->in.volume = 0.25f; v->buffers[0] = (AudrenWaveBuffer){.address = (uintptr_t)pcm, .size = 32768, .end = 8000, .loop = 1, .loop_count = -1};
        } else if (i < 24) {
            v->in.sample_format = FMT_ADPCM; v->in.channel_count = 1; v->in.sample_rate = (i & 1) ? 24000u : 48000u;
            v->in.volume = 0.20f; v->in.extra_address = (uintptr_t)coef; v->in.extra_size = 32;
            v->buffers[0] = (AudrenWaveBuffer){.address = (uintptr_t)adpcm, .size = 8 * 512, .end = 3500, .loop = 1, .loop_count = -1};
        } else {
            v->in.sample_format = FMT_PCM16; v->in.channel_count = 1; v->in.sample_rate = 48000; v->in.volume = 0.0f;
            v->buffers[0] = (AudrenWaveBuffer){.address = (uintptr_t)pcm, .size = 32768, .end = 8000, .loop = 1, .loop_count = -1};
        }
        for (int c = 0; c < (int)v->in.channel_count; c++) {
            r->channels[i * 6 + c].mix[c] = 0.6f; r->channels[i * 6 + c].mix[2] = 0.2f; r->channels[i * 6 + c].mix[4 + c] = 0.3f;
        }
    }
    r->sinks[0] = (AudrenSinkIn){.type = SINK_DEVICE, .in_use = 1, .input_count = 6, .inputs = {0, 1, 2, 3, 4, 5}};
}

static int bench_full_mixer(void) {
    enum { F = 240, NFRAMES = 2000 };
    static int16_t pcm[16384], coef[16];
    static uint8_t adpcm[8 * 512];
    static float ring_out[480];
    fill_pcm16_fixture(pcm, 16384);
    fill_adpcm_fixture(adpcm, 512, coef);

    float last_lr[2][F * 2];
    double ms[2] = {0, 0};
    for (int mode = 0; mode < 2; mode++) {
        setenv("TOTK_AUDIO_FAST", mode ? "1" : "0", 1); totk_audio_fast_sync();
        AudioRenderer r = af_make_renderer(F, 48, 4, 32);
        setup_bench_scene(&r, pcm, adpcm, coef);
        uint64_t t0 = af_now_ns();
        for (int f = 0; f < NFRAMES; f++) {
            audio_mixer_render(&r, (double)F / 48000.0);
            if ((f & 1) == 1) pull_frames((Mixer *)r.mixer, ring_out, 480);
            for (int i = 0; i < 48; i++) af_advance_voice(&r.voices[i], (double)F / 48000.0);
        }
        ms[mode] = (af_now_ns() - t0) / 1e6;
        memcpy(last_lr[mode], ((Mixer *)r.mixer)->output_scratch, sizeof last_lr[0]);
        af_free_renderer(&r);
    }
    float max_diff = 0.0f;
    for (int i = 0; i < F * 2; i++) {
        float d = fabsf(last_lr[0][i] - last_lr[1][i]);
        if (d > max_diff) max_diff = d;
    }
    if (max_diff > 1e-5f) {
        fprintf(stderr, "FAIL: full mixer output divergence max_diff=%.8g\n", max_diff);
        return 1;
    }
    printf("aoc_audio_fast: voices=48(16pcm16,8adpcm,24muted) frames=%d base=%.2f ms (%.2f us/fr) fast=%.2f ms (%.2f us/fr) speedup=%.2fx max_diff=%.2e\n",
           NFRAMES, ms[0], ms[0] * 1000.0 / NFRAMES, ms[1], ms[1] * 1000.0 / NFRAMES, ms[0] / (ms[1] > 0 ? ms[1] : 0.001), max_diff);
    return 0;
}

int main(void) {
    setenv("TOTK_AUDIO_OFF", "1", 1);
    unsetenv("TOTK_AUDIO_CAPTURE");
    const char *fe = getenv("T_AUDIO_FAST_FAULT");
    g_totk_audio_fault = fe ? atoi(fe) : 0;
    if (test_ring_push_pull() != 0) return 1;
    if (test_voices_all() != 0) return 1;
    if (test_silent_unmute() != 0) return 1;
    if (bench_full_mixer() != 0) return 1;
    printf("test_audio_fast PASS (fault=%d)\n", g_totk_audio_fault);
    return 0;
}
