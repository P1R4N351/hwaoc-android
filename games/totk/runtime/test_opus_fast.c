#if !defined(__APPLE__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "opus_codec.c"
#include <assert.h>
#include <time.h>

enum { N_THREADS = 8, N_PER_THREAD = 32, N_FMT_OPS = 25000 };

_Noreturn void runtime_fail(const char *msg) {
    fprintf(stderr, "runtime_fail: %s\n", msg);
    exit(1);
}
void dm_guest_write_prepare(void *p, size_t n) { (void)p; (void)n; }

static uint64_t tof_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int g_fault;
static uint8_t g_keys[N_THREADS][N_PER_THREAD];

typedef struct {
    int tid;
    uint64_t sum;
} worker_arg;

static void *fmt_worker(void *arg) {
    worker_arg *w = (worker_arg *)arg;
    uint64_t sum = 0;
    for (int i = 0; i < N_FMT_OPS; i++) {
        void *k = &g_keys[w->tid][i & (N_PER_THREAD - 1)];
        int r = sample_rate(k);
        int c = channel_count(k);
        if (r != 48000 || c != 2) { fprintf(stderr, "FAIL format mismatch r=%d c=%d\n", r, c); exit(1); }
        sum += (uint64_t)(r + c);
    }
    w->sum = sum;
    return NULL;
}

static uint64_t run_bench_mode(int fast_mode) {
    atomic_store(&g_opus_fast, fast_mode);
    if (fast_mode) pthread_once(&g_buckets_once, buckets_init);
    size_t ws = work_size(48000, 2, 0); uint8_t *work = calloc(1, ws); assert(work);
    for (int t = 0; t < N_THREADS; t++) {
        for (int j = 0; j < N_PER_THREAD; j++) {
            unsigned r = initialize(&g_keys[t][j], 48000, 2, work, ws, 0);
            assert(r == 0);
        }
    }
    pthread_t th[N_THREADS];
    worker_arg wa[N_THREADS];
    uint64_t t0 = tof_now_ns();
    for (int t = 0; t < N_THREADS; t++) {
        wa[t] = (worker_arg){t, 0};
        assert(!pthread_create(&th[t], NULL, fmt_worker, &wa[t]));
    }
    for (int t = 0; t < N_THREADS; t++) assert(!pthread_join(th[t], NULL));
    uint64_t dt = tof_now_ns() - t0;
    for (int t = 0; t < N_THREADS; t++) {
        for (int j = 0; j < N_PER_THREAD; j++) {
            if (!(g_fault == 2 && t == 0 && j == 0)) finalize(&g_keys[t][j]);
        }
    }
    if (g_fault == 2) assert(decoder_count == 0);
    free(work);
    assert(decoder_count == 0 && decoders == NULL);
    return dt;
}

static void verify_lifecycle_and_reinit(void) {
    atomic_store(&g_opus_fast, 1);
    size_t ws = work_size(48000, 2, 0); uint8_t *work = calloc(1, ws); assert(work);
    uint8_t arena[512];
    uint8_t *k0 = &arena[0];
    uint8_t *k1 = &arena[8];
    unsigned b0 = opus_ptr_bucket(k0);
    for (int off = 8; off < 512 && opus_ptr_bucket(&arena[off]) == b0; off += 8) k1 = &arena[off + 8];
    assert(opus_ptr_bucket(k0) != opus_ptr_bucket(k1));
    assert(initialize(k0, 24000, 1, work, ws, 0) == 0);
    assert(sample_rate(k0) == 24000 && channel_count(k0) == 1);
    assert(initialize(k0, 48000, 2, work, ws, 0) == 0);
    int got_rate = g_fault == 1 ? 24000 : sample_rate(k0);
    assert(got_rate == 48000 && channel_count(k0) == 2);
    assert(initialize(k1, 16000, 1, work, ws, 0) == 0);
    unsigned probe_b = g_fault == 3 ? b0 : opus_ptr_bucket(k1);
    pthread_mutex_lock(&g_buckets[b0].mu);
    assert(pthread_mutex_trylock(&g_buckets[probe_b].mu) == 0);
    pthread_mutex_unlock(&g_buckets[probe_b].mu);
    pthread_mutex_unlock(&g_buckets[b0].mu);
    finalize(k0);
    finalize(k1);
    free(work);
}

int main(void) {
    const char *f = getenv("T_OPUS_FAST_FAULT");
    g_fault = f ? atoi(f) : 0;
    uint64_t dt_leg = run_bench_mode(0);
    uint64_t dt_fast = run_bench_mode(1);
    verify_lifecycle_and_reinit();
    long long total_ops = (long long)N_THREADS * N_FMT_OPS * 2;
    printf("aoc_opus_fast: threads=%d decoders=%d ops=%lld legacy_ms=%.2f (%.1f ns/op) fast_ms=%.2f (%.1f ns/op) speedup=%.2fx\n",
           N_THREADS, N_THREADS * N_PER_THREAD, total_ops,
           dt_leg / 1e6, (double)dt_leg / (double)total_ops,
           dt_fast / 1e6, (double)dt_fast / (double)total_ops,
           dt_fast ? (double)dt_leg / (double)dt_fast : 0.0);
    return 0;
}
