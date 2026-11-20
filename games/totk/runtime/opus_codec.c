/* nn::codec hardware Opus decoder, implemented with libopus on the host (the hwopus service is a
   hardware-accelerated libopus). Input frames carry Nintendo's 8-byte big-endian header
   {payload size, final range}; the decoder's final range is checked against it. A header range of 0 is not a
   recorded value: none of the 280,723 packets in 1.4.3's English voice streams has it, while a fan dub's encoder
   writes 0 into every packet (all 186,758 of the PKG PT-BR dub, which decode without error). Those are not checked. */
#include "runtime.h"
#include "opus_fast.h"
#include <opus.h>
#include <pthread.h>
#include <stdatomic.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define R_INVALID 0x1fcd1u   /* codec: invalid argument (module 111) */
/* A fixed-capacity host table could terminate a running game when another
 * stream opened. Allocate one stable node per live decoder and free it at
 * Finalize; growing the registry must not move a decoder another thread uses.
 * Lock order: bucket (or legacy registry lock) -> decoder->use_lock -> stats_lock.
 * Calling decode/finalize across distinct hash buckets never contends globally. */
typedef struct Decoder {
    const void *guest;
    OpusDecoder *dec;
    int rate, channels;
    struct Decoder *next, *prev, *bnext;
    pthread_mutex_t use_lock;
} Decoder;
typedef struct {
    pthread_mutex_t mu;
    Decoder *head;
} OpusBucket;
static Decoder *decoders;
static size_t decoder_count, decoder_high_water, decoder_created, decoder_finalized, decoder_reinitialized;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t stats_lock = PTHREAD_MUTEX_INITIALIZER;
static OpusBucket g_buckets[OPUS_BUCKETS];
static opus_fmt_cache g_fmt_cache;
static pthread_once_t g_buckets_once = PTHREAD_ONCE_INIT;
static _Atomic int g_opus_fast = -1;
static void buckets_init(void) {
    for (unsigned i = 0; i < OPUS_BUCKETS; i++) pthread_mutex_init(&g_buckets[i].mu, NULL);
}
static int opus_fast_on(void) {
    int f = atomic_load_explicit(&g_opus_fast, memory_order_acquire);
    if (f < 0) {
        pthread_once(&g_buckets_once, buckets_init);
        const char *v = getenv("TOTK_OPUS_FAST");
        int want = !(v && v[0] == '0' && !v[1]);
        atomic_store_explicit(&g_opus_fast, want, memory_order_release);
        return want;
    }
    return f;
}

static void decoder_stats(const char *event) { /* registry lock held */
    fprintf(stderr, "[totk-audio] Opus %s: active %zu, peak %zu, created %zu, finalized %zu, reinitialized %zu (dynamic registry)\n",
            event, decoder_count, decoder_high_water, decoder_created, decoder_finalized, decoder_reinitialized);
}
static int codec_trace(void) {
    const char *v = getenv("TOTK_CODEC_TRACE");
    return v && *v && strcmp(v, "0");
}
static Decoder *acquire(const void *guest) {
    Decoder *d;
    if (opus_fast_on()) {
        OpusBucket *b = &g_buckets[opus_ptr_bucket(guest)];
        pthread_mutex_lock(&b->mu);
        d = b->head;
        while (d && d->guest != guest) d = d->bnext;
        if (d) pthread_mutex_lock(&d->use_lock);
        pthread_mutex_unlock(&b->mu);
    } else {
        pthread_mutex_lock(&lock);
        d = decoders;
        while (d && d->guest != guest) d = d->next;
        if (d) pthread_mutex_lock(&d->use_lock);
        pthread_mutex_unlock(&lock);
    }
    if (!d) runtime_fail("unknown hardware Opus decoder");
    return d;
}
static void release(Decoder *d) { pthread_mutex_unlock(&d->use_lock); }
static int valid_format(int rate, int channels) {
    return (rate == 48000 || rate == 24000 || rate == 16000 || rate == 12000 || rate == 8000) && (channels == 1 || channels == 2);
}
static size_t work_size(int rate, int channels, int option) {
    (void)option; if (!valid_format(rate, channels)) return 0;
    return (size_t)opus_decoder_get_size(channels) + 0x1000;   /* host keeps the state; the game's buffer is reserved but unused */
}
static void list_link(Decoder *d) {
    d->next = decoders; d->prev = NULL;
    if (decoders) decoders->prev = d;
    decoders = d; decoder_count++; decoder_created++;
    int peak = decoder_count > decoder_high_water;
    if (peak) decoder_high_water = decoder_count;
    if (codec_trace() || (peak && decoder_count > 64 && (decoder_count == 65 || !(decoder_count & (decoder_count - 1)))))
        decoder_stats("initialize");
}
static void list_unlink(Decoder *d) {
    if (d->prev) d->prev->next = d->next; else decoders = d->next;
    if (d->next) d->next->prev = d->prev;
    d->prev = d->next = NULL; decoder_count--; decoder_finalized++;
    if (codec_trace()) decoder_stats("finalize");
}
static void reinit_stats(const void *guest) {
    decoder_reinitialized++;
    if (decoder_reinitialized == 1)
        fprintf(stderr, "[totk] hardware Opus decoder %p initialized again without Finalize: its slot is reused (logged once)\n", guest);
    if (codec_trace()) decoder_stats("reinitialize");
}
static unsigned initialize(void *guest, int rate, int channels, void *work, size_t size, int option) {
    (void)work; (void)option;
    if (!guest || !valid_format(rate, channels) || size < work_size(rate, channels, 0)) return R_INVALID;
    Decoder *d = calloc(1, sizeof *d);
    if (!d) runtime_fail("hardware Opus decoder allocation");
    int err = 0; d->dec = opus_decoder_create(rate, channels, &err);
    if (!d->dec || err != OPUS_OK) { if (d->dec) opus_decoder_destroy(d->dec); free(d); runtime_fail("libopus decoder creation"); }
    if (pthread_mutex_init(&d->use_lock, NULL)) { opus_decoder_destroy(d->dec); free(d); runtime_fail("hardware Opus decoder mutex"); }
    d->guest = guest; d->rate = rate; d->channels = channels;
    int fast = opus_fast_on();
    OpusBucket *b = fast ? &g_buckets[opus_ptr_bucket(guest)] : NULL;
    pthread_mutex_t *rmu = fast ? &b->mu : &lock;
    pthread_mutex_lock(rmu);
    Decoder *old = fast ? b->head : decoders;
    while (old && old->guest != guest) old = fast ? old->bnext : old->next;
    if (old) {
        pthread_mutex_lock(&old->use_lock);
        OpusDecoder *previous = old->dec; old->dec = d->dec; old->rate = rate; old->channels = channels;
        if (fast) opus_fmt_put(&g_fmt_cache, guest, rate, channels);
        pthread_mutex_lock(&stats_lock); reinit_stats(guest); pthread_mutex_unlock(&stats_lock);
        pthread_mutex_unlock(rmu);
        opus_decoder_destroy(previous); release(old);
        pthread_mutex_destroy(&d->use_lock); free(d); return 0;
    }
    if (fast) { d->bnext = b->head; b->head = d; opus_fmt_put(&g_fmt_cache, guest, rate, channels); }
    pthread_mutex_lock(&stats_lock); list_link(d); pthread_mutex_unlock(&stats_lock);
    pthread_mutex_unlock(rmu);
    return 0;
}
static void finalize(void *guest) {
    int fast = opus_fast_on();
    OpusBucket *b = fast ? &g_buckets[opus_ptr_bucket(guest)] : NULL;
    pthread_mutex_t *rmu = fast ? &b->mu : &lock;
    pthread_mutex_lock(rmu);
    Decoder **slot = fast ? &b->head : &decoders;
    while (*slot && (*slot)->guest != guest) slot = fast ? &(*slot)->bnext : &(*slot)->next;
    Decoder *d = *slot;
    if (!d) { pthread_mutex_unlock(rmu); runtime_fail("unknown hardware Opus decoder"); }
    pthread_mutex_lock(&d->use_lock);
    if (fast) { *slot = d->bnext; opus_fmt_del(&g_fmt_cache, guest); }
    pthread_mutex_lock(&stats_lock); list_unlink(d); pthread_mutex_unlock(&stats_lock);
    pthread_mutex_unlock(rmu);
    opus_decoder_destroy(d->dec);
    release(d); pthread_mutex_destroy(&d->use_lock); free(d);
}
static int sample_rate(const void *guest) {
    int r = 0;
    if (opus_fast_on() && opus_fmt_get(&g_fmt_cache, guest, &r, NULL)) return r;
    Decoder *d = acquire(guest); r = d->rate; release(d); return r;
}
static int channel_count(const void *guest) {
    int c = 0;
    if (opus_fast_on() && opus_fmt_get(&g_fmt_cache, guest, NULL, &c)) return c;
    Decoder *d = acquire(guest); c = d->channels; release(d); return c;
}
static unsigned reset(void *guest) {
    Decoder *d = acquire(guest); int ok = opus_decoder_ctl(d->dec, OPUS_RESET_STATE); release(d);
    return ok == OPUS_OK ? 0 : R_INVALID;
}
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static unsigned decode(void *guest, size_t *consumed, int *samples, int16_t *out, size_t out_size, const uint8_t *in, size_t in_size) {
    if (!consumed || !samples || !out || !in || in_size < 8) return R_INVALID;
    uint32_t n = be32(in), range = be32(in + 4);
    if (n > in_size - 8 || n > INT_MAX) return R_INVALID;
    Decoder *d = acquire(guest);
    size_t capacity = out_size / (sizeof(int16_t) * (size_t)d->channels);
    if (capacity > INT_MAX) { release(d); return R_INVALID; }
    int max = (int)capacity;
    dm_guest_write_prepare(out, out_size);
    int got = opus_decode(d->dec, in + 8, (opus_int32)n, out, max, 0);
    if (got < 0) { fprintf(stderr, "[totk] opus_decode: %s\n", opus_strerror(got)); release(d); return R_INVALID; }
    opus_uint32 final = 0; opus_decoder_ctl(d->dec, OPUS_GET_FINAL_RANGE(&final));
    if (n && range && final != range) { fprintf(stderr, "[totk] Opus final range %#x != stream %#x\n", final, range); runtime_fail("Opus stream integrity"); }
    *consumed = 8 + n; *samples = got; release(d); return 0;
}

#ifdef GAME_HWAOC
/* SDK 9.6.0 HardwareOpusMultiStreamDecoder opens "hwopus" IPC, whereas SDK's software OpusMultiStreamDecoder
   shares the identical 11-slot IOpusMultiStreamDecoder vtable (+0x10..+0x68) and fits within its 0x48-byte object. */
static void hw_ms_ctor(void *self, int option) {
    (void)option;
    if (!self) runtime_fail("null HardwareOpusMultiStreamDecoder");
    void (*ctor)(void *) = (void (*)(void *))runtime_symbol("_ZN2nn5codec22OpusMultiStreamDecoderC1Ev");
    if (!ctor) runtime_fail("missing guest OpusMultiStreamDecoder::OpusMultiStreamDecoder()");
    ctor(self);
}
static void hw_ms_dtor(void *self) { (void)self; }
static size_t hw_ms_work_size(int rate, int ch, int total_streams, int stereo_streams) {
    size_t (*fn)(int, int, int, int) = (size_t (*)(int, int, int, int))
        runtime_symbol("_ZN2nn5codec39GetOpusMultiStreamDecoderWorkBufferSizeEiiii");
    if (!fn) runtime_fail("missing guest GetOpusMultiStreamDecoderWorkBufferSize");
    size_t sz = fn(rate, ch, total_streams, stereo_streams);
    return (sz + 0xfffu) & ~(size_t)0xfffu;
}
#endif

void *runtime_codec_override(const char *name) {
    static const struct { const char *name; void *fn; } map[] = {
        {"_ZN2nn5codec36GetHardwareOpusDecoderWorkBufferSizeEiii", work_size},
        {"_ZN2nn5codec29InitializeHardwareOpusDecoderEPNS0_23HardwareOpusDecoderTypeEiiPvmi", initialize},
        {"_ZN2nn5codec27FinalizeHardwareOpusDecoderEPNS0_23HardwareOpusDecoderTypeE", finalize},
        {"_ZN2nn5codec32GetHardwareOpusDecoderSampleRateEPKNS0_23HardwareOpusDecoderTypeE", sample_rate},
        {"_ZN2nn5codec34GetHardwareOpusDecoderChannelCountEPKNS0_23HardwareOpusDecoderTypeE", channel_count},
        {"_ZN2nn5codec24ResetHardwareOpusDecoderEPNS0_23HardwareOpusDecoderTypeE", reset},
        {"_ZN2nn5codec33DecodeOpusInterleavedWithHardwareEPNS0_23HardwareOpusDecoderTypeEPmPiPsmPKvm", decode},
#ifdef GAME_HWAOC
        {"_ZN2nn5codec30HardwareOpusMultiStreamDecoderC1Ei", hw_ms_ctor},
        {"_ZN2nn5codec30HardwareOpusMultiStreamDecoderD1Ev", hw_ms_dtor},
        {"_ZN2nn5codec47GetHardwareOpusMultiStreamDecoderWorkBufferSizeEiiii", hw_ms_work_size},
#endif
    };
    for (size_t i = 0; i < sizeof map / sizeof *map; i++) if (!strcmp(name, map[i].name)) return map[i].fn;
    return NULL;
}
