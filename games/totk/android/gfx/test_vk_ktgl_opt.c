/* Host unit, benchmark, and Delta #27 fault-injection suite for KTGL optimizations:
 *   1) vk_edge_guard.h + real nvn_dirty.c + host_test_support.c SIGSEGV fault handler
 *   2) vk_quad_batch.h look-ahead op-stream scanner, mid-pass hazard invalidation, and cache hits */
#include "vk_edge_guard.h"
#include "vk_quad_batch.h"
#include "test_vk_ktgl_opt_w2.h"
#include "test_vk_ktgl_opt_w3.h"
#include "test_vk_ktgl_opt_w5.h"
#include "test_vk_ktgl_opt_w6.h"
#include "test_vk_ktgl_opt_w7.h"
#define TOTK_KTGL_OPT_TEST 1
#include "test_vk_ktgl_opt_w8.h"
#include "test_vk_ktgl_opt_w9.h"
#include "test_vk_ktgl_opt_w10.h"
#include "test_vk_ktgl_opt_w11.h"
#include "test_vk_ktgl_opt_w12.h"
#include "test_vk_ktgl_opt_w13.h"
#include "test_vk_ktgl_opt_w14.h"
#include "test_vk_ktgl_opt_w15.h"
#include "test_vk_ktgl_opt_w16.h"
#include "test_vk_ktgl_opt_w17.h"
#include "test_vk_ktgl_opt_w18.h"
#include "test_vk_ktgl_opt_w19.h"
#include "nvn_dirty.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

uint64_t host_now_ns(void);
void mprotect_stats_record_failure(const void *address, size_t len, int err) { (void)address; (void)len; (void)err; }
void mprotect_stats_record_success(void) {}
void dm_wait_stats_record(const char *reason, uint64_t ns, const void *addr) { (void)reason; (void)ns; (void)addr; }

static int g_fail;
int g_fault_mode;

#define EXPECT_EQ(msg, want, got) do {     long long w_ = (long long)(want), g_ = (long long)(got);     if (w_ != g_) { fprintf(stderr, "FAIL %-56s expected=%lld got=%lld\n", (msg), w_, g_); g_fail++; } } while (0)

typedef struct {
    const uint8_t *mem;
    size_t size;
    uint64_t stamp;
    vk_edge_state edge;
    uint64_t reuploads, edge_skips, edge_unpins;
} mock_mirrored;

static int mock_geom(const mock_mirrored *m, vk_edge_geom *g) {
    uintptr_t u_lo = 0; size_t u_sz = 0;
    if (!nvn_dirty_unit_info(m->mem, m->size, &u_lo, &u_sz)) return 0;
    vk_edge_split(u_lo, (uintptr_t)m->mem, m->size, u_sz, g);
    return 1;
}

static void mock_protect(mock_mirrored *m, size_t pg, int use_guard) {
    (void)pg;
    assert(m != NULL && m->mem != NULL && m->size > 0);
    vk_edge_geom g;
    if (!use_guard || !mock_geom(m, &g)) { m->edge.armed = m->edge.unpinned = m->edge.streaks = m->edge.in_hashed = 0; nvn_dirty_protect(m->mem, m->size); return; }
    vk_edge_snapshot(&g, &m->edge);
    if (m->edge.unpinned) { if (g.ie > g.ia) nvn_dirty_protect((const void *)g.ia, g.ie - g.ia); }
    else nvn_dirty_protect(m->mem, m->size);
}

static void mock_upload(mock_mirrored *m, size_t pg, int use_guard) {
    m->edge.streaks = 0;
    m->stamp = nvn_dirty_now();
    mock_protect(m, pg, use_guard);
    m->reuploads++;
}

static int mock_mirrored_dirty(mock_mirrored *m, size_t pg, int use_guard) {
    vk_edge_geom g;
    if (!use_guard || !m->edge.armed || !mock_geom(m, &g)) return !nvn_dirty_tracked(m->mem, m->size) || nvn_dirty_since(m->mem, m->size, m->stamp);
    int h0 = 0, h1 = 0;
    int e_dirty = vk_edge_changed(&g, &m->edge, &h0, &h1);
    if (g_fault_mode == 2) h0 = 0;   /* Delta #27 fault mode 2: ignore head edge hash */
    if (m->edge.unpinned) {
        int in_dirty = (g.ie > g.ia) && (!nvn_dirty_tracked((const void *)g.ia, g.ie - g.ia) ||
                                         nvn_dirty_since((const void *)g.ia, g.ie - g.ia, m->stamp));
        (void)e_dirty;
        if (in_dirty || h0 || h1) { m->edge.streaks = 0; return 1; }
        m->edge_skips++;
        return 0;
    }
    if (!nvn_dirty_tracked(m->mem, m->size)) { m->edge.streaks = 0; return 1; }
    if (!nvn_dirty_since(m->mem, m->size, m->stamp)) return 0;
    uint64_t st = nvn_dirty_now();
    int in_dirty = (g.ie > g.ia && nvn_dirty_since((const void *)g.ia, g.ie - g.ia, m->stamp));
    if (in_dirty && !h0 && !h1 && m->edge.in_hashed) {
        int ep = (g.r[0][1] > g.r[0][0] && nvn_dirty_since((const void *)g.r[0][0], g.r[0][1] - g.r[0][0], m->stamp)) ||
                 (g.r[1][1] > g.r[1][0] && nvn_dirty_since((const void *)g.r[1][0], g.r[1][1] - g.r[1][0], m->stamp));
        if (vk_edge_interior_collateral(&g, &m->edge, ep)) in_dirty = 0;
    }
    if (in_dirty || h0 || h1) { m->edge.streaks = 0; return 1; }
    m->edge_skips++;
    if (m->edge.streaks < 255) m->edge.streaks++;
    if (m->edge.streaks >= VK_EDGE_UNPIN_AFTER) { m->edge.unpinned = 1; m->edge_unpins++; }
    m->stamp = st;
    mock_protect(m, pg, use_guard);
    return 0;
}

static void bench_shared_page(size_t pg, int use_guard) {
    enum { NFRAMES = 1000, TEX_OFF = 2048, TEX_SZ = 16 };
    uint8_t *pool = mmap(NULL, 2 * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(pool != MAP_FAILED);
    nvn_dirty_add_range(pool, 2 * pg);
    for (size_t i = 0; i < TEX_SZ; i++) pool[TEX_OFF + i] = (uint8_t)(i + 10);
    mock_mirrored m = {.mem = pool + TEX_OFF, .size = TEX_SZ};
    mock_upload(&m, pg, use_guard);
    m.reuploads = 0;

    uint64_t f0 = 0, p0 = 0, f1 = 0, p1 = 0;
    nvn_dirty_stats(&f0, &p0);
    uint64_t t0 = host_now_ns();
    for (int f = 0; f < NFRAMES; f++) {
        ((volatile uint32_t *)pool)[0] = (uint32_t)(f + 1);   /* neighbor write on same page */
        if (mock_mirrored_dirty(&m, pg, use_guard)) mock_upload(&m, pg, use_guard);
    }
    uint64_t dt = host_now_ns() - t0;
    nvn_dirty_stats(&f1, &p1);
    uint64_t faults = f1 - f0;
    printf("aoc_edge[%s]: frames=%d faults=%llu false_reuploads=%llu edge_skips=%llu time_ms=%.3f ns_per_frame=%.1f\n",
           use_guard ? "guarded " : "baseline", NFRAMES, (unsigned long long)faults,
           (unsigned long long)m.reuploads, (unsigned long long)m.edge_skips,
           dt / 1e6, (double)dt / NFRAMES);
    if (use_guard) {
        EXPECT_EQ("guarded faults capped at VK_EDGE_UNPIN_AFTER", VK_EDGE_UNPIN_AFTER, faults);
        EXPECT_EQ("guarded false reuploads == 0", 0, m.reuploads);
    } else {
        EXPECT_EQ("baseline reuploads on every neighbor write", NFRAMES, m.reuploads);
        EXPECT_EQ("baseline faults on every frame", NFRAMES, faults);
    }
    nvn_dirty_remove_range(pool);
    munmap(pool, 2 * pg);
}

static void test_straddling_mirrored(size_t pg) {
    uint8_t *probe = mmap(NULL, 16 * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(probe != MAP_FAILED);
    nvn_dirty_add_range(probe, 16 * pg);
    uintptr_t u_lo = 0; size_t u = 0;
    int ok_u = nvn_dirty_unit_info(probe, pg, &u_lo, &u);
    assert(ok_u && u > 0);
    nvn_dirty_remove_range(probe);
    munmap(probe, 16 * pg);

    uint8_t *pool = mmap(NULL, 4 * u, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(pool != MAP_FAILED);
    nvn_dirty_add_range(pool, 4 * u);
    size_t toff = u / 2, tsz = 3 * u;
    memset(pool + toff, 0x55, tsz);
    mock_mirrored m = {.mem = pool + toff, .size = tsz};
    mock_upload(&m, u, 1);

    for (int k = 0; k < VK_EDGE_UNPIN_AFTER; k++) {
        pool[0] = (uint8_t)(k + 1); pool[4 * u - 1] = (uint8_t)(k + 1);
        EXPECT_EQ("straddle warmup neighbor write clean", 0, mock_mirrored_dirty(&m, u, 1));
    }
    EXPECT_EQ("straddle unpinned", 1, m.edge.unpinned);
    uint64_t f0 = 0, p0 = 0, f1 = 0, p1 = 0; nvn_dirty_stats(&f0, &p0);
    for (int k = 0; k < 25; k++) {
        pool[8] = (uint8_t)(k + 3); pool[4 * u - 8] = (uint8_t)(k + 3);
        EXPECT_EQ("straddle post-unpin neighbor write clean", 0, mock_mirrored_dirty(&m, u, 1));
    }
    nvn_dirty_stats(&f1, &p1);
    EXPECT_EQ("post-unpin neighbor writes trigger 0 faults", 0, f1 - f0);

    pool[toff + 4] = 0x11;   /* head edge real write */
    EXPECT_EQ("straddle head edge real write detected", 1, mock_mirrored_dirty(&m, u, 1));
    mock_upload(&m, u, 1);

    pool[u + 32] = 0x22;     /* interior unit real write */
    EXPECT_EQ("straddle interior real write detected", 1, mock_mirrored_dirty(&m, u, 1));
    mock_upload(&m, u, 1);

    pool[toff + tsz - 4] = 0x33;   /* tail edge real write */
    EXPECT_EQ("straddle tail edge real write detected", 1, mock_mirrored_dirty(&m, u, 1));

    nvn_dirty_remove_range(pool);
    munmap(pool, 4 * u);
}

static int rt_same_mock(const p_set_rt *a, const p_set_rt *b) {
    return a && b && a->n == b->n && a->color[0] == b->color[0] && a->depth == b->depth;
}
static int pending_mock(uint64_t addr, uint64_t sz) { (void)sz; return (addr & 0xffu) != 0x99u; }

typedef struct { uint8_t buf[4096]; size_t n; } op_rec;
static void rec_push(op_rec *r, uint32_t op, const void *p, size_t psz) {
    assert(r->n + 8 + ((psz + 7) & ~7u) <= sizeof r->buf);
    op_hdr h = {.op = op, .size = (uint32_t)(8 + ((psz + 7) & ~7u))};
    memcpy(r->buf + r->n, &h, sizeof h);
    if (psz) memcpy(r->buf + r->n + sizeof h, p, psz);
    r->n += h.size;
}

static void build_quad_batch_stream(op_rec *sub, op_rec *stream, const p_set_rt *set_b) {
    /* Sub-list called mid-pass via OP_CALL: holds 2 unique pending indirect QUADS buffers (one repeated). */
    p_draw_indirect ind4 = {7, 0x100400}, ind5 = {7, 0x100500}, ind_cpu = {7, 0x100099};
    rec_push(sub, OP_DRAW_INDIRECT, &ind4, sizeof ind4);
    rec_push(sub, OP_DRAW_INDIRECT, &ind4, sizeof ind4);
    rec_push(sub, OP_DRAW_INDIRECT, &ind5, sizeof ind5);
    rec_push(sub, OP_DRAW_INDIRECT, &ind_cpu, sizeof ind_cpu);
    rec_push(sub, OP_END, NULL, 0);

    const uint8_t *sub_ptr = sub->buf;
    for (int i = 0; i < 4; i++) {
        p_draw_indirect iw = {7, 0x100000 + (uint64_t)i * 0x100};
        rec_push(stream, OP_DRAW_INDIRECT, &iw, sizeof iw);
    }
    rec_push(stream, OP_CALL, &sub_ptr, sizeof sub_ptr);
    p_draw_indirect ind6 = {7, 0x100600}, ind7 = {7, 0x100700};
    rec_push(stream, OP_DRAW_INDIRECT, &ind6, sizeof ind6);
    rec_push(stream, OP_DRAW_INDIRECT, &ind7, sizeof ind7);
    if (g_fault_mode == 1) {
        p_draw noop = {4, 0, 3, 0, 1};
        rec_push(stream, OP_DRAW, &noop, sizeof noop);   /* Delta #27 fault mode 1: omit hazard stop */
    } else {
        uint64_t cp[3] = {0x200000, 0x100800, 16};
        rec_push(stream, OP_COPY_B2B, cp, sizeof cp);
    }
    p_draw_indirect ind8 = {7, 0x100800}, ind9 = {7, 0x100900};
    rec_push(stream, OP_DRAW_INDIRECT, &ind8, sizeof ind8);
    rec_push(stream, OP_DRAW_INDIRECT, &ind9, sizeof ind9);
    rec_push(stream, OP_SET_RT, set_b, sizeof *set_b);
    rec_push(stream, OP_END, NULL, 0);
}

static void test_quad_batch_scanner(void) {
    tex_t rt_a = {.serial = 1}, rt_b = {.serial = 2};
    p_set_rt set_a = {.n = 1, .color = {&rt_a}}, set_b = {.n = 1, .color = {&rt_b}};
    op_rec sub = {0}, stream = {0};
    build_quad_batch_stream(&sub, &stream, &set_b);

    vk_qi_cache cache = {0};
    vk_qi_cache_reset(&cache, 10, 1);
    uint64_t inds[VK_QI_BATCH_MAX];
    int nq = vk_qi_scan_ops(stream.buf, 0, &set_a, rt_same_mock, pending_mock, &cache, 10, 1, inds, VK_QI_BATCH_MAX);
    EXPECT_EQ("pre-pass collects all 8 pre-hazard QUADS", 8, nq);
    for (int i = 0; i < nq; i++) vk_qi_cache_put(&cache, inds[i], 0xbeef, (uint64_t)(i * 64 + 32));

    uint64_t b = 0, o = 0;
    EXPECT_EQ("ind0..ind7 hit cache (0 pass splits across 8 draws)", 1, vk_qi_cache_find(&cache, 10, 1, 0x100400, &b, &o));
    EXPECT_EQ("post-hazard ind8 excluded from pre-hazard batch", 0, vk_qi_cache_find(&cache, 10, 1, 0x100800, &b, &o));

    /* Simulate OP_COPY_B2B bumping write generation -> invalidates cached pre-hazard indirects. */
    EXPECT_EQ("write_gen bump invalidates cache", 0, vk_qi_cache_find(&cache, 10, 2, 0x100400, &b, &o));

    /* Simulate post-hazard prescan starting at ind8 -> finds ind8 & ind9, stops at OP_SET_RT set_b. */
    const uint8_t *post_hazard = NULL;
    for (size_t off = 0; off < stream.n;) {
        op_hdr h; memcpy(&h, stream.buf + off, sizeof h);
        if (h.op == OP_COPY_B2B) { post_hazard = stream.buf + off + h.size; break; }
        off += h.size;
    }
    int nq2 = post_hazard ? vk_qi_scan_ops(post_hazard, 0, &set_a, rt_same_mock, pending_mock, &cache, 10, 2, inds, VK_QI_BATCH_MAX) : 0;
    EXPECT_EQ("post-hazard scan collects remaining 2 QUADS before set_b", 2, nq2);
    printf("aoc_quad_batch: baseline_splits=8 batched_splits=0 pre_batch=%d barriers=24->3 upd_sets=8->1 post_batch=%d\n", nq, nq2);
}

int main(void) {
    const char *f = getenv("TOTK_KTGL_OPT_FAULT");
    g_fault_mode = f ? atoi(f) : 0;
    size_t pg = (size_t)sysconf(_SC_PAGESIZE);
    bench_shared_page(pg, 0);
    bench_shared_page(pg, 1);
    test_straddling_mirrored(pg);
    test_quad_batch_scanner();
    if (run_ktgl_w2_tests(g_fault_mode) != 0) g_fail++;
    if (run_ktgl_w3_tests(g_fault_mode) != 0) g_fail++;
    if (run_ktgl_w5_tests(g_fault_mode) != 0) g_fail++;
    if (run_ktgl_w6_tests(g_fault_mode) != 0) g_fail++;
    if (run_ktgl_w7_tests(g_fault_mode) != 0) g_fail++;
    if (run_ktgl_w8_tests(g_fault_mode) != 0) g_fail++;
    if (run_ktgl_w9_tests(g_fault_mode) != 0) g_fail++;
    if (run_ktgl_w10_tests(g_fault_mode) != 0) g_fail++;
    if (run_ktgl_w11_tests(g_fault_mode) != 0) g_fail++;
    if (run_ktgl_w12_tests(g_fault_mode) != 0) g_fail++;
    if (run_ktgl_w13_tests(g_fault_mode) != 0) g_fail++;
    if (run_ktgl_w14_tests(g_fault_mode) != 0) g_fail++;
    if (run_ktgl_w15_tests(g_fault_mode) != 0) g_fail++;
    if (run_ktgl_w16_tests(g_fault_mode) != 0) g_fail++;
    if (run_ktgl_w17_tests(g_fault_mode) != 0) g_fail++;
    if (run_ktgl_w18_tests(g_fault_mode) != 0) g_fail++;
    if (run_ktgl_w19_tests(g_fault_mode) != 0) g_fail++;
    printf("ktglopttest: fault_mode=%d failures=%d\n", g_fault_mode, g_fail);
    return g_fail != 0;
}
