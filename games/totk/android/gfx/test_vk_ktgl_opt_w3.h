#ifndef TEST_VK_KTGL_OPT_W3_H
#define TEST_VK_KTGL_OPT_W3_H
#include "vk_ktgl_memo.h"
#include "romfs_handles.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint64_t host_now_ns(void);

typedef struct { uint64_t a, b, c, d, lay; } w3_pipe_key;
typedef struct { w3_pipe_key k; uint64_t lay; uintptr_t pipe; } w3_pipe_ent;

static uint64_t w3_hm_fnv_miss(const w3_pipe_ent *ents, const w3_pipe_key *k, int n_pipe) {
    const uint8_t *p = (const uint8_t *)k;
    uint64_t h = UINT64_C(1469598103934665603);
    for (size_t i = 0; i < sizeof *k; i++) h = (h ^ p[i]) * UINT64_C(1099511628211);
    return ents[h % (uint64_t)n_pipe].pipe ^ (uint64_t)(uintptr_t)k;
}

static uint64_t w3_bench_pipe_memo(int four_way, const w3_pipe_ent *ents, int n_pipe,
                                   int n_draw, int n_frames, uint64_t *steady_miss, uint64_t *sink_out) {
    static const w3_pipe_ent *m64[64];
    static vk_pipe_memo pm;
    memset(m64, 0, sizeof m64);
    memset(&pm, 0, sizeof pm);
    const w3_pipe_ent *last = NULL;
    static volatile uint64_t miss_guard;
    uint64_t miss = 0, sink = 0, m_sink = 0, t0 = 0;
    for (int f = 0; f < n_frames; f++) {
        if (f == 1) t0 = host_now_ns();
        for (int d = 0; d < n_draw; d++) {
            const w3_pipe_ent *want = &ents[(d * 7 + f) % n_pipe];
            const w3_pipe_ent *e = last;
            if (!e || e->lay != want->lay || memcmp(&e->k, &want->k, sizeof e->k) != 0) {
                if (four_way) {
                    const vk_pipe_set_t *ps = vk_pipe_memo_set(&pm, (const void *)(uintptr_t)want->k.a,
                        (const void *)(uintptr_t)want->k.b, (uint32_t)want->k.c, want->lay);
                    e = NULL;
                    for (int w = 0; w < VK_KTGL_PIPE_WAYS; w++) {
                        const w3_pipe_ent *c = (const w3_pipe_ent *)ps->ways[w];
                        if (!c) break;
                        if (c->lay == want->lay && !memcmp(&c->k, &want->k, sizeof want->k)) { e = c; break; }
                    }
                    if (!e) { if (f > 0) miss++; m_sink ^= w3_hm_fnv_miss(ents, &want->k, n_pipe); e = want;
                        vk_pipe_memo_put(&pm, (const void *)(uintptr_t)want->k.a, (const void *)(uintptr_t)want->k.b, (uint32_t)want->k.c, want->lay, (void *)e); }
                } else {
                    unsigned h = (unsigned)(((want->k.a >> 4) ^ (want->k.b >> 8) ^ want->k.c) & 63u);
                    const w3_pipe_ent *c = m64[h];
                    if (c && c->lay == want->lay && !memcmp(&c->k, &want->k, sizeof want->k)) e = c;
                    else { if (f > 0) miss++; m_sink ^= w3_hm_fnv_miss(ents, &want->k, n_pipe); e = want; m64[h] = e; }
                }
                last = e;
            }
            sink ^= e->pipe;
        }
    }
    uint64_t dt = host_now_ns() - t0;
    miss_guard ^= m_sink;
    *steady_miss = miss;
    *sink_out = sink;
    return dt;
}

static uint64_t w3_bench_cs_memo(int use_memo, const uintptr_t *shaders, const uint64_t *pipes,
                                 const uint64_t *lays, int n_cs, int n_disp, int n_frames,
                                 uint64_t *locks_out, uint64_t *sink_out) {
    static vk_cs_memo cm;
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    memset(&cm, 0, sizeof cm);
    uint64_t locks = 0, sink = 0, t0 = 0;
    for (int f = 0; f < n_frames; f++) {
        if (f == 1) t0 = host_now_ns();
        for (int d = 0; d < n_disp; d++) {
            int idx = (d * 5 + f) % n_cs;
            const void *cs = (const void *)shaders[idx];
            uint64_t p = 0, l = 0;
            if (!use_memo || !vk_cs_memo_get(&cm, cs, &p, &l)) {
                pthread_mutex_lock(&mu); if (f > 0) locks++;
                for (int probe = 0; probe < 8; probe++) { if (shaders[(idx + probe) % n_cs] == (uintptr_t)cs) { p = pipes[idx]; l = lays[idx]; break; } }
                pthread_mutex_unlock(&mu);
                if (use_memo) vk_cs_memo_put(&cm, cs, p, l);
            }
            sink ^= p ^ l;
        }
    }
    uint64_t dt = host_now_ns() - t0;
    *locks_out = locks;
    *sink_out = sink;
    return dt;
}

typedef struct { romfs_htable *t; pthread_mutex_t *mu; uint64_t h; int use_lock; int reads; } w3_rh_arg;
static void *w3_rh_worker(void *p) {
    w3_rh_arg *a = (w3_rh_arg *)p;
    uint64_t acc = 0;
    for (int i = 0; i < a->reads; i++) {
        uint64_t fp1 = 0; uint32_t ov = 0; int fd = -1, ok;
        if (a->use_lock) { pthread_mutex_lock(a->mu); ok = romfs_htable_resolve_locked(a->t, a->h, &fp1, &ov, &fd); pthread_mutex_unlock(a->mu); }
        else ok = romfs_htable_resolve_fast(a->t, a->h, &fp1, &ov, &fd);
        assert(ok == 1 && fp1 == 42u);
        acc += fp1;
    }
    assert(acc == (uint64_t)a->reads * 42u);
    return NULL;
}

static int w3_verify_handles(int fault_mode) {
    static romfs_htable ht;
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    memset(&ht, 0, sizeof ht);
    uint64_t h1 = romfs_htable_alloc_locked(&ht, 42u, 0u, -1);
    assert(h1 == 0x100000001ULL && romfs_h_valid_bits(h1));
    int fd = -1;
    assert(romfs_htable_close_locked(&ht, h1, &fd) == 1 && fd == -1);
    uint64_t dummy = 0;
    if (fault_mode == 6) atomic_store(&ht.slots[1].seq, 1u);
    if (romfs_htable_resolve_fast(&ht, h1, &dummy, NULL, NULL) != 0) { fprintf(stderr, "FAIL: closed handle resolved\n"); return 1; }
    if (romfs_htable_close_locked(&ht, h1, &fd) != 0) { fprintf(stderr, "FAIL: double close succeeded\n"); return 1; }
    uint64_t h2 = romfs_htable_alloc_locked(&ht, 99u, 0u, -1);
    assert(h2 == 0x100000002ULL);
    for (int k = 0; k < 65533; k++) {
        uint64_t hk = romfs_htable_alloc_locked(&ht, (uint64_t)(k + 100), 0u, -1);
        assert(hk != 0 && romfs_htable_close_locked(&ht, hk, &fd) == 1);
    }
    uint64_t h_reuse = romfs_htable_alloc_locked(&ht, 777u, 0u, -1);
    if (fault_mode == 7) h_reuse = h1;
    if ((h_reuse & 0xffffu) != 1u || h_reuse == h1 || (h_reuse >> 16 & 0xffffu) != 1u) {
        fprintf(stderr, "FAIL: FIFO slot generation wrap unexpected (%#llx)\n", (unsigned long long)h_reuse); return 1;
    }
    if (romfs_htable_resolve_fast(&ht, h1, &dummy, NULL, NULL) != 0) {
        fprintf(stderr, "FAIL: stale gen0 handle matched gen1 slot (%#llx)\n", (unsigned long long)h1); return 1;
    }
    enum { NTH = 4, NREADS = 100000 };
    uint64_t hb = romfs_htable_alloc_locked(&ht, 42u, 0u, -1);
    pthread_t th[NTH]; w3_rh_arg args[NTH];
    uint64_t t0 = host_now_ns();
    for (int i = 0; i < NTH; i++) { args[i] = (w3_rh_arg){&ht, &mu, hb, 1, NREADS}; assert(pthread_create(&th[i], NULL, w3_rh_worker, &args[i]) == 0); }
    for (int i = 0; i < NTH; i++) pthread_join(th[i], NULL);
    uint64_t locked_ns = host_now_ns() - t0;
    uint64_t t1 = host_now_ns();
    for (int i = 0; i < NTH; i++) { args[i] = (w3_rh_arg){&ht, &mu, hb, 0, NREADS}; assert(pthread_create(&th[i], NULL, w3_rh_worker, &args[i]) == 0); }
    for (int i = 0; i < NTH; i++) pthread_join(th[i], NULL);
    uint64_t fast_ns = host_now_ns() - t1;
    printf("aoc_romfs_handles: threads=%d ops=%d locked=%.2f ms seqlock=%.2f ms speedup=%.2fx\n",
           NTH, NTH * NREADS, locked_ns / 1e6, fast_ns / 1e6, (double)locked_ns / (fast_ns ? fast_ns : 1));
    return 0;
}

static int run_ktgl_w3_tests(int fault_mode) {
    enum { NP = 320, ND = 800, NF = 400, NC = 64, NDISP = 160 };
    w3_pipe_ent *ents = calloc(NP, sizeof *ents);
    uintptr_t sh[NC]; uint64_t pipes[NC], lays[NC];
    assert(ents != NULL);
    for (int i = 0; i < NP; i++) {
        uint64_t va = 0x400000ull + (uint64_t)(i % 100) * 64u, fb = 0x800000ull + (uint64_t)(i % 80) * 64u;
        uint64_t var = (uint64_t)(i / 100);
        ents[i] = (w3_pipe_ent){{va, fb, (uint64_t)i % 5u, var, (uint64_t)(i % 16) + 1u}, (uint64_t)(i % 16) + 1u, (uintptr_t)(0x10000u + i * 16u)};
    }
    vk_pipe_memo fchk = {0};
    w3_pipe_ent ka = ents[0], kb = ka; kb.k.d = 99u; kb.pipe ^= 1u;
    vk_pipe_memo_put(&fchk, (const void *)(uintptr_t)ka.k.a, (const void *)(uintptr_t)ka.k.b, (uint32_t)ka.k.c, ka.lay, &ka);
    const vk_pipe_set_t *ps = vk_pipe_memo_set(&fchk, (const void *)(uintptr_t)kb.k.a, (const void *)(uintptr_t)kb.k.b, (uint32_t)kb.k.c, kb.lay);
    const w3_pipe_ent *hit_b = (const w3_pipe_ent *)ps->ways[0];
    size_t chk_sz = fault_mode == 5 ? 24u : sizeof kb.k;
    if (hit_b && hit_b->lay == kb.lay && !memcmp(&hit_b->k, &kb.k, chk_sz)) {
        fprintf(stderr, "FAIL: vk_pipe_memo aliased distinct variant keys (fault=%d)\n", fault_mode);
        free(ents); return 1;
    }
    uint64_t m1 = 0, m4 = 0, s1 = 0, s4 = 0;
    uint64_t t1 = w3_bench_pipe_memo(0, ents, NP, ND, NF, &m1, &s1);
    uint64_t t4 = w3_bench_pipe_memo(1, ents, NP, ND, NF, &m4, &s4);
    free(ents);
    if (s1 != s4 || m4 != 0) { fprintf(stderr, "FAIL: pipe_memo mismatch or steady misses=%llu (fault=%d)\n", (unsigned long long)m4, fault_mode); return 1; }
    for (int i = 0; i < NC; i++) { sh[i] = 0x900000ull + (uint64_t)i * 64u; pipes[i] = 0x50000ull + (uint64_t)i * 8u; lays[i] = 0x60000ull + (uint64_t)(i % 8) * 8u; }
    uint64_t cl1 = 0, cl4 = 0, cs1 = 0, cs4 = 0;
    uint64_t ct1 = w3_bench_cs_memo(0, sh, pipes, lays, NC, NDISP, NF, &cl1, &cs1);
    uint64_t ct4 = w3_bench_cs_memo(fault_mode == 8 ? 0 : 1, sh, pipes, lays, NC, NDISP, NF, &cl4, &cs4);
    if (cs1 != cs4 || cl4 != 0) { fprintf(stderr, "FAIL: cs_memo mismatch or nonzero locks=%llu (fault=%d)\n", (unsigned long long)cl4, fault_mode); return 1; }
    printf("aoc_pipe_cs_memo: pipe_miss1=%llu (%.2f ms) -> pipe_miss4=%llu (%.2f ms, %.2fx) cs_locks=%llu->%llu (%.2f->%.2f ms, %.2fx)\n",
           (unsigned long long)m1, t1 / 1e6, (unsigned long long)m4, t4 / 1e6, (double)t1 / (t4 ? t4 : 1),
           (unsigned long long)cl1, (unsigned long long)cl4, ct1 / 1e6, ct4 / 1e6, (double)ct1 / (ct4 ? ct4 : 1));
    return w3_verify_handles(fault_mode);
}
#endif
