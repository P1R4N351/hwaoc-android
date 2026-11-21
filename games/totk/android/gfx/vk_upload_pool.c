/* Parallel texture deswizzle (vk_upload_pool.h). A block-linear level/layer is stored as block rows: each covers
   whole GOB columns of 8 << tileH texel-block rows and 1 << tileD slices, and no two block rows decode to the same
   staging bytes. A run is a span of whole block rows (at least UPLOAD_POOL_RUN_BYTES), decoded by
   nvn_deswizzle_span, the engine nvn_deswizzle uses. Runs of one upload are claimed from one counter by the caller and
   the helpers; the caller returns once every run is done, so its staging bytes equal the serial decode. */
#include "vk_upload_pool.h"
#include "vk_upload_fast.h"
#include "../../third_party/d2r_gfx/gfx/tex/nvn_deswizzle.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

static pthread_mutex_t g_job_mu = PTHREAD_MUTEX_INITIALIZER;   /* one upload owns the pool and the run array */
static int g_started;                                         /* g_job_mu: helper threads running */
static upload_run *g_runs; static unsigned g_runs_cap;         /* g_job_mu */
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;       /* publication and completion of the open job */
static pthread_cond_t g_work = PTHREAD_COND_INITIALIZER, g_done = PTHREAD_COND_INITIALIZER;
static int g_limit;                                            /* g_mu: configured helpers; higher indices sit out */
static _Atomic int g_limit_fast;                               /* release under g_mu; lock-free acquire read */
static unsigned g_gen, g_open, g_finished, g_active, g_done_waiting; static int g_failed;   /* g_mu; g_open: runs of the open job, 0 when closed */
/* Written under g_mu while no helper is active, read by helpers between joining and leaving a job. */
static const nvn_texdesc *g_job_d; static const void *g_job_src; static const upload_run *g_job_runs;
static _Atomic unsigned g_next;
static _Atomic uint64_t g_stat_jobs, g_stat_runs, g_stat_helper_runs;

static int drain(unsigned n, unsigned *count) {
    unsigned i, c = 0; int bad = 0;
    while ((i = atomic_fetch_add_explicit(&g_next, 1, memory_order_relaxed)) < n) {
        const upload_run *r = g_job_runs + i;
        bad |= nvn_deswizzle_span(g_job_d, r->level, r->layer, g_job_src, r->dst, r->pitch, r->first, r->count) != 0;
        c++;
    }
    *count = c; return bad;
}
static void *helper(void *arg) {
    unsigned index = (unsigned)(uintptr_t)arg;
    char name[16]; snprintf(name, sizeof name, "totk-upload%u", index);
#if defined(__APPLE__)
    pthread_setname_np(name);
#else
    pthread_setname_np(pthread_self(), name);
#endif
    pthread_mutex_lock(&g_mu);
    for (unsigned seen = g_gen;;) {
        while (g_gen == seen) pthread_cond_wait(&g_work, &g_mu);
        seen = g_gen;
        unsigned n = g_open;
        if (!n || index >= (unsigned)g_limit) continue;   /* that job already completed, or this helper is not configured */
        g_active++;
        pthread_mutex_unlock(&g_mu);
        unsigned count; int bad = drain(n, &count);
        pthread_mutex_lock(&g_mu);
        g_failed |= bad; g_finished += count; g_active--;
        if (count) atomic_fetch_add_explicit(&g_stat_helper_runs, count, memory_order_relaxed);
        if (g_finished == g_open && !g_active && (!totk_vk_upload_fast_on() || g_done_waiting)) pthread_cond_signal(&g_done);
    }
    return NULL;
}
#ifdef TOTK_W30_TEST_HOOKS
void totk_w30_pool_test_reset(int fast, int fault) { totk_vk_upload_fast_reset_for_test(fast, fault); }
#endif
void upload_pool_configure(int helpers) {
    int clamped = helpers < 0 ? 0 : helpers > UPLOAD_POOL_MAX_HELPERS ? UPLOAD_POOL_MAX_HELPERS : helpers;
    pthread_mutex_lock(&g_mu);
    g_limit = clamped;
    atomic_store_explicit(&g_limit_fast, clamped, memory_order_release);
    pthread_mutex_unlock(&g_mu);
}
int upload_pool_helpers(void) {
    if (totk_vk_upload_fast_on()) {
        if (totk_w30_fault_mode() == 102) return 0;
        return atomic_load_explicit(&g_limit_fast, memory_order_acquire);
    }
    pthread_mutex_lock(&g_mu); int n = g_limit; pthread_mutex_unlock(&g_mu); return n;
}
void upload_pool_stats_take(upload_pool_stats *out) {
    out->jobs = atomic_exchange_explicit(&g_stat_jobs, 0, memory_order_relaxed);
    out->runs = atomic_exchange_explicit(&g_stat_runs, 0, memory_order_relaxed);
    out->helper_runs = atomic_exchange_explicit(&g_stat_helper_runs, 0, memory_order_relaxed);
}
static int serial(const nvn_texdesc *d, const void *src, const upload_sub *subs, unsigned nsubs) {
    for (unsigned i = 0; i < nsubs; i++) if (nvn_deswizzle(d, subs[i].level, subs[i].layer, src, subs[i].dst, subs[i].pitch)) return -1;
    return 0;
}
/* Block-row geometry of every subresource: texel bytes, and the run count when split. -1 if any is not decodable. */
static int plan(const nvn_texdesc *d, const upload_sub *subs, unsigned nsubs, uint64_t *texels, uint64_t *nruns, upload_run *out) {
    uint64_t bytes = 0, n = 0;
    for (unsigned i = 0; i < nsubs; i++) {
        uint32_t row_bytes, rows, slices, bpb; uint64_t base; unsigned th, td;
        if (nvn_deswizzle_geom(d, subs[i].level, subs[i].layer, &row_bytes, &rows, &slices, &base, &th, &td, &bpb) ||
            subs[i].pitch < row_bytes) return -1;
        bytes += (uint64_t)row_bytes * rows * slices;
        uint64_t block_row = (row_bytes + 63) / 64 * (UINT64_C(512) << (th + td));
        uint64_t block_rows = ((uint64_t)rows + (8u << th) - 1) / (8u << th) * (((uint64_t)slices + (1u << td) - 1) >> td);
        uint64_t per_run = (UPLOAD_POOL_RUN_BYTES + block_row - 1) / block_row, total = block_row * block_rows;
        for (uint64_t first = 0; first < total; first += per_run * block_row, n++)
            if (out) out[n] = (upload_run){subs[i].level, subs[i].layer, subs[i].dst, subs[i].pitch, first,
                                           total - first < per_run * block_row ? total - first : per_run * block_row};
    }
    *texels = bytes; *nruns = n; return 0;
}
static int dispatch_runs(const nvn_texdesc *d, const void *src, const upload_run *runs, unsigned n, int fast) {
    pthread_mutex_lock(&g_mu);
    g_job_d = d; g_job_src = src; g_job_runs = runs;
    atomic_store_explicit(&g_next, 0, memory_order_relaxed);
    g_finished = 0u; g_failed = 0; g_open = n; g_gen++;
    if (fast && g_started == 1) pthread_cond_signal(&g_work); else pthread_cond_broadcast(&g_work);
    pthread_mutex_unlock(&g_mu);
    unsigned count = 0u; int bad = drain(n, &count);
    pthread_mutex_lock(&g_mu);
    g_failed |= bad; g_finished += count;
    if (g_finished < g_open || g_active) {
        g_done_waiting++;
        while (g_finished < g_open || g_active) pthread_cond_wait(&g_done, &g_mu);
        g_done_waiting--;
    }
    bad = g_failed; g_open = 0u;
    pthread_mutex_unlock(&g_mu);
    atomic_fetch_add_explicit(&g_stat_jobs, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&g_stat_runs, n, memory_order_relaxed);
    return bad ? -1 : 0;
}
static int ensure_started(int helpers) {
    for (; g_started < helpers; g_started++) {
        pthread_t t;
        if (pthread_create(&t, NULL, helper, (void *)(uintptr_t)g_started)) break;
        pthread_detach(t);
    }
    return g_started > 0;
}
static int deswizzle_fast(const nvn_texdesc *d, const void *src, const upload_sub *subs, unsigned nsubs, size_t min_bytes, int helpers) {
    totk_upload_sub_geom geoms[TOTK_UPLOAD_STACK_SUBS]; upload_run stack_runs[TOTK_UPLOAD_STACK_RUNS];
    uint64_t texels = 0u, n = 0u;
    if (totk_upload_geom_pass(d, subs, nsubs, &texels, &n, geoms, TOTK_UPLOAD_STACK_SUBS) || texels < min_bytes || n < 2u || n > UINT32_MAX)
        return serial(d, src, subs, nsubs);
    if (n <= TOTK_UPLOAD_STACK_RUNS && nsubs <= TOTK_UPLOAD_STACK_SUBS) {
        totk_upload_emit_runs(subs, nsubs, geoms, stack_runs, n);
        pthread_mutex_lock(&g_job_mu);
        int started = ensure_started(helpers);
        int rc = started ? dispatch_runs(d, src, stack_runs, (unsigned)n, 1) : -2;
        pthread_mutex_unlock(&g_job_mu);
        return rc == -2 ? serial(d, src, subs, nsubs) : rc;
    }
    pthread_mutex_lock(&g_job_mu);
    if (!ensure_started(helpers)) { pthread_mutex_unlock(&g_job_mu); return serial(d, src, subs, nsubs); }
    if (n > g_runs_cap) {
        upload_run *grown = (upload_run *)realloc(g_runs, (size_t)n * sizeof *g_runs);
        if (!grown) { pthread_mutex_unlock(&g_job_mu); return serial(d, src, subs, nsubs); }
        g_runs = grown; g_runs_cap = (unsigned)n;
    }
    if (nsubs <= TOTK_UPLOAD_STACK_SUBS) totk_upload_emit_runs(subs, nsubs, geoms, g_runs, n);
    else plan(d, subs, nsubs, &texels, &n, g_runs);
    if (totk_w30_fault_mode() == 106) n--;
    int rc = dispatch_runs(d, src, g_runs, (unsigned)n, 1);
    pthread_mutex_unlock(&g_job_mu);
    return rc;
}
int upload_pool_deswizzle(const nvn_texdesc *d, const void *src, const upload_sub *subs, unsigned nsubs, size_t min_bytes) {
    int helpers = upload_pool_helpers();
    if (!helpers || nvn_tex_is_linear(d)) return serial(d, src, subs, nsubs);
    if (totk_vk_upload_fast_on()) return deswizzle_fast(d, src, subs, nsubs, min_bytes, helpers);
    uint64_t texels, n;
    if (plan(d, subs, nsubs, &texels, &n, NULL) || texels < min_bytes || n < 2 || n > UINT32_MAX) return serial(d, src, subs, nsubs);
    pthread_mutex_lock(&g_job_mu);
    if (!ensure_started(helpers)) { pthread_mutex_unlock(&g_job_mu); return serial(d, src, subs, nsubs); }
    if (n > g_runs_cap) { upload_run *grown = realloc(g_runs, (size_t)n * sizeof *g_runs); if (grown) { g_runs = grown; g_runs_cap = (unsigned)n; } }
    if (n > g_runs_cap) { pthread_mutex_unlock(&g_job_mu); return serial(d, src, subs, nsubs); }
    plan(d, subs, nsubs, &texels, &n, g_runs);
    int rc = dispatch_runs(d, src, g_runs, (unsigned)n, 0);
    pthread_mutex_unlock(&g_job_mu);
    return rc;
}
