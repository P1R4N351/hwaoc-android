#ifndef TEST_VK_KTGL_OPT_W13_H
#define TEST_VK_KTGL_OPT_W13_H

#include "vk_prog_fast.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint64_t host_now_ns(void);
static volatile uint64_t w13_sink;

typedef struct {
    uint64_t prog_u, serial, epoch, sig;
    uint64_t sh_u;
    int ready;
} w13_stage_item;

static volatile uint64_t g_w13_nonce = 0x517cc1b727220a95ull;

static uint64_t w13_lookup_stage(const w13_stage_item *pool, uint32_t n, uint64_t prog_u,
                                   uint64_t serial, uint64_t epoch, uint64_t sig) {
    uint64_t acc = (serial ^ g_w13_nonce) & 7u;
    uint64_t found = 0;
    for (uint32_t i = 0; i < n; i++) {
        const w13_stage_item *it = &pool[i];
        acc += (it->epoch ^ it->sig) & 3u;
        if (it->prog_u == prog_u && it->serial == serial &&
            it->epoch == epoch && it->sig == sig && it->ready) {
            found = it->sh_u;
        }
    }
    return found ? (found ^ acc) : 0;
}

static int w13_resolve_pair_base(const w13_stage_item *pool, uint32_t n, uint64_t vs_u,
                                 uint64_t vs_ser, uint64_t fs_u, uint64_t fs_ser,
                                 uint64_t epoch, uint64_t sig0, uint64_t sig1,
                                 uint64_t *sh0, uint64_t *sh1) {
    *sh0 = w13_lookup_stage(pool, n, vs_u, vs_ser, epoch, sig0);
    *sh1 = fs_u ? w13_lookup_stage(pool, n, fs_u, fs_ser, epoch, sig1) : 0;
    return (*sh0 != 0 && (!fs_u || *sh1 != 0)) ? 0 : -1;
}

static int w13_resolve_pair_opt(totk_gfx_pair_memo4 *m, const w13_stage_item *pool, uint32_t n,
                                uint64_t vs_u, uint64_t vs_ser, uint64_t fs_u, uint64_t fs_ser,
                                uint64_t epoch, uint64_t sig0, uint64_t sig1, uint64_t reg,
                                uint64_t *sh0, uint64_t *sh1) {
    uint64_t rtmp = reg;
    if (totk_gfx_pair_memo4_find(m, vs_u, vs_ser, fs_u, fs_ser, epoch, sig0, sig1,
                                 sh0, sh1, &rtmp)) {
        return 0;
    }
    if (w13_resolve_pair_base(pool, n, vs_u, vs_ser, fs_u, fs_ser, epoch, sig0, sig1, sh0, sh1) == 0) {
        totk_gfx_pair_memo4_put(m, vs_u, vs_ser, fs_u, fs_ser, epoch, sig0, sig1,
                                *sh0, *sh1, reg);
        return 0;
    }
    return -1;
}

static int sim_prep_base(totk_prog_prep_ent dm[64], const w13_stage_item *pool, uint32_t n,
                         uint64_t prog_u, uint64_t serial, uint64_t epoch) {
    uint32_t slot = (uint32_t)((prog_u >> 5) & 63u);
    if (dm[slot].valid && dm[slot].prog_u == prog_u &&
        dm[slot].serial == serial && dm[slot].epoch == epoch) {
        return 1;
    }
    uint64_t acc = (serial ^ g_w13_nonce) & 7u;
    int hit = 0;
    for (uint32_t i = 0; i < n; i++) {
        const w13_stage_item *it = &pool[i];
        acc += (it->epoch ^ it->prog_u) & 3u;
        if (it->prog_u == prog_u && it->serial == serial && it->epoch == epoch && it->ready) {
            hit = 1;
        }
    }
    if (hit) {
        w13_sink ^= acc;
        dm[slot] = (totk_prog_prep_ent){prog_u, serial, epoch, 1u, 0u};
    }
    return hit;
}

static int test_w13_prog_fast(void) {
    /* Fault 35: vk_prog_prepare memo must re-prepare when p->serial changes on the same prog_t *. */
    totk_prog_prep_memo4 pm;
    memset(&pm, 0, sizeof pm);
    assert(!totk_prog_prep_memo4_has(&pm, 0x1000u, 1u, 1u));
    totk_prog_prep_memo4_put(&pm, 0x1000u, 1u, 1u);
    assert(totk_prog_prep_memo4_has(&pm, 0x1000u, 1u, 1u));
    if (totk_prog_prep_memo4_has(&pm, 0x1000u, 2u, 1u)) {
        fprintf(stderr, "w13 FAIL prep serial mismatch ignored\n");
        return 1;
    }
    assert(!totk_prog_prep_memo4_has(&pm, 0x1000u, 1u, 2u));

    w13_stage_item pool[16];
    memset(pool, 0, sizeof pool);
    pool[0] = (w13_stage_item){0x100000u, 10u, 1u, 0x11u, 0xaaa100u, 1};
    pool[1] = (w13_stage_item){0x200000u, 20u, 1u, 0x22u, 0xbbb200u, 1};
    /* Fault 36: reused FS pointer with new serial 99. */
    pool[2] = (w13_stage_item){0x200000u, 99u, 1u, 0x22u, 0xbbb999u, 1};
    /* Fault 37: same FS serial=20, changed sig1=0x33. */
    pool[3] = (w13_stage_item){0x200000u, 20u, 1u, 0x33u, 0xbbb333u, 1};
    /* Pending negative VS -> must not be cached. */
    pool[4] = (w13_stage_item){0x300000u, 30u, 1u, 0x11u, 0xccc300u, 0};

    totk_gfx_pair_memo4 gm;
    memset(&gm, 0, sizeof gm);
    uint64_t b0 = 0, b1 = 0, o0 = 0, o1 = 0;

    assert(w13_resolve_pair_base(pool, 5u, 0x100000u, 10u, 0x200000u, 20u, 1u, 0x11u, 0x22u, &b0, &b1) == 0);
    assert(w13_resolve_pair_opt(&gm, pool, 5u, 0x100000u, 10u, 0x200000u, 20u, 1u, 0x11u, 0x22u, 5u, &o0, &o1) == 0);
    assert(b0 == o0 && b1 == o1);

    /* Unready VS must not cache miss. */
    assert(w13_resolve_pair_opt(&gm, pool, 5u, 0x300000u, 30u, 0u, 0u, 1u, 0x11u, 0u, 5u, &o0, &o1) == -1);
    pool[4].ready = 1;
    assert(w13_resolve_pair_opt(&gm, pool, 5u, 0x300000u, 30u, 0u, 0u, 1u, 0x11u, 0u, 5u, &o0, &o1) == 0);
    assert(o0 != 0 && o1 == 0);

    /* Fault 37: sig1 changes on (0x100000, 0x200000, fs_ser=20) before fs_ser=99 evicts it! */
    assert(w13_resolve_pair_base(pool, 5u, 0x100000u, 10u, 0x200000u, 20u, 1u, 0x11u, 0x33u, &b0, &b1) == 0);
    assert(w13_resolve_pair_opt(&gm, pool, 5u, 0x100000u, 10u, 0x200000u, 20u, 1u, 0x11u, 0x33u, 5u, &o0, &o1) == 0);
    if (b1 != o1) { fprintf(stderr, "w13 FAIL sig1 mismatch\n"); return 1; }

    /* Fault 36: fs_ser changes to 99 on reused FS pointer 0x200000. */
    assert(w13_resolve_pair_base(pool, 5u, 0x100000u, 10u, 0x200000u, 99u, 1u, 0x11u, 0x22u, &b0, &b1) == 0);
    assert(w13_resolve_pair_opt(&gm, pool, 5u, 0x100000u, 10u, 0x200000u, 99u, 1u, 0x11u, 0x22u, 5u, &o0, &o1) == 0);
    if (b1 != o1) { fprintf(stderr, "w13 FAIL fs_ser mismatch\n"); return 1; }

    return 0;
}

static void bench_w13_prog_fast(void) {
    w13_stage_item pool[16];
    memset(pool, 0, sizeof pool);
    for (uint32_t i = 0; i < 8u; i++) {
        pool[i] = (w13_stage_item){0x400000u + i * 64u, 100u + i, 2u, 0x10u, 0xd00000u + i * 32u, 1};
        pool[8u + i] = (w13_stage_item){0x500000u + i * 64u, 200u + i, 2u, 0x20u, 0xe00000u + i * 32u, 1};
    }
    totk_gfx_pair_memo4 gm;
    memset(&gm, 0, sizeof gm);

    const uint32_t N = 200000u;
    uint64_t sb = 0, so = 0;
    uint64_t t0 = host_now_ns();
    for (uint32_t i = 0; i < N; i++) {
        uint32_t idx = i & 7u;
        uint64_t s0 = 0, s1 = 0;
        if (w13_resolve_pair_base(pool, 16u, pool[idx].prog_u, pool[idx].serial,
                                  pool[8u + idx].prog_u, pool[8u + idx].serial,
                                  2u, 0x10u, 0x20u, &s0, &s1) == 0) {
            sb += s0 ^ s1;
        }
    }
    double ms_b = (double)(host_now_ns() - t0) / 1e6;

    uint64_t t1 = host_now_ns();
    for (uint32_t i = 0; i < N; i++) {
        uint32_t idx = i & 7u;
        uint64_t s0 = 0, s1 = 0;
        if (w13_resolve_pair_opt(&gm, pool, 16u, pool[idx].prog_u, pool[idx].serial,
                                 pool[8u + idx].prog_u, pool[8u + idx].serial,
                                 2u, 0x10u, 0x20u, 1u, &s0, &s1) == 0) {
            so += s0 ^ s1;
        }
    }
    double ms_o = (double)(host_now_ns() - t1) / 1e6;
    assert(sb == so);

    w13_stage_item ppool[4];
    memset(ppool, 0, sizeof ppool);
    totk_prog_prep_memo4 pm;
    memset(&pm, 0, sizeof pm);
    totk_prog_prep_ent dm[64];
    memset(dm, 0, sizeof dm);
    for (uint32_t k = 0; k < 4u; k++) {
        uint64_t pu = 0x400000ull + (uint64_t)k * ((1ull << 11) | (1ull << 17));
        ppool[k] = (w13_stage_item){pu, 50u + k, 2u, 0u, 1u, 1};
        totk_prog_prep_memo4_put(&pm, pu, 50u + k, 2u);
    }
    uint64_t prep_base_hits = 0, prep_opt_hits = 0;
    uint64_t t2 = host_now_ns();
    for (uint32_t i = 0; i < N; i++) {
        uint32_t k = i & 3u;
        prep_base_hits += (uint64_t)sim_prep_base(dm, ppool, 4u, ppool[k].prog_u, ppool[k].serial, 2u);
    }
    double ms_pb = (double)(host_now_ns() - t2) / 1e6;
    uint64_t t3 = host_now_ns();
    for (uint32_t i = 0; i < N; i++) {
        uint32_t k = i & 3u;
        if (totk_prog_prep_memo4_has(&pm, ppool[k].prog_u, ppool[k].serial, 2u) ||
            sim_prep_base(dm, ppool, 4u, ppool[k].prog_u, ppool[k].serial, 2u)) {
            prep_opt_hits++;
        }
    }
    double ms_po = (double)(host_now_ns() - t3) / 1e6;
    assert(prep_base_hits == N && prep_opt_hits == N);

    w13_sink = sb ^ so ^ prep_opt_hits;
    printf("aoc_w13_pair_memo: base=%.2fms opt=%.2fms speedup=%.2fx\n",
           ms_b, ms_o, ms_b / (ms_o > 0.01 ? ms_o : 0.01));
    printf("aoc_w13_prep_memo: base=%.2fms opt=%.2fms speedup=%.2fx\n",
           ms_pb, ms_po, ms_pb / (ms_po > 0.01 ? ms_po : 0.01));
}

static int run_ktgl_w13_tests(int fault_mode) {
    (void)fault_mode; (void)totk_vk_prog_fast_on();
    if (test_w13_prog_fast() != 0) return 1;
    bench_w13_prog_fast();
    return 0;
}

#endif
