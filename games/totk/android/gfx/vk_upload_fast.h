/* Fast texture-upload planner & stack storage helpers (TOTK_VK_UPLOAD_FAST=1, default on; =0 disables).
   Portable ISO C11 (zero GAME_* conditionals) so both libtotk.so and host uploadpooltest/ktglopttest execute it. */
#pragma once
#include <assert.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "vk_upload_pool.h"
#include "../../third_party/d2r_gfx/gfx/tex/nvn_deswizzle.h"

enum {
    TOTK_UPLOAD_STACK_SUBS = 96,
    TOTK_UPLOAD_STACK_RUNS = 128,
    TOTK_UPLOAD_INLINE_REG = 16
};

typedef struct {
    unsigned level, layer;
    void *dst;
    size_t pitch;
    uint64_t first, count;
} upload_run;

typedef struct {
    uint64_t block_row;
    uint64_t total;
    uint64_t step;
} totk_upload_sub_geom;

static _Atomic int g_totk_vk_upload_fast_state;

static inline int totk_vk_upload_fast_on(void) {
    int s = atomic_load_explicit(&g_totk_vk_upload_fast_state, memory_order_acquire);
    if (!s) {
        const char *e = getenv("TOTK_VK_UPLOAD_FAST");
        s = (e && *e == '0' && !e[1]) ? 1 : 2;
        atomic_store_explicit(&g_totk_vk_upload_fast_state, s, memory_order_release);
    }
    return s == 2;
}

#ifdef TOTK_W30_TEST_HOOKS
static _Atomic int g_totk_w30_fault;
static inline void totk_vk_upload_fast_reset_for_test(int forced, int fault) {
    atomic_store_explicit(&g_totk_vk_upload_fast_state, forced ? 2 : 1, memory_order_release);
    atomic_store_explicit(&g_totk_w30_fault, fault, memory_order_release);
}
static inline int totk_w30_fault_mode(void) {
    return atomic_load_explicit(&g_totk_w30_fault, memory_order_acquire);
}
#else
static inline int totk_w30_fault_mode(void) { return 0; }
#endif

static inline int totk_upload_geom_pass(const nvn_texdesc *d, const upload_sub *subs, unsigned nsubs,
                                        uint64_t *texels, uint64_t *nruns, totk_upload_sub_geom *geoms,
                                        unsigned geom_cap) {
    assert(d != NULL && texels != NULL && nruns != NULL);
    assert(nsubs == 0u || subs != NULL);
    int fmode = totk_w30_fault_mode();
    uint64_t bytes = 0u, n = 0u;
    for (unsigned i = 0u; i < nsubs; i++) {
        uint32_t row_bytes = 0u, rows = 0u, slices = 0u, bpb = 0u;
        uint64_t base = 0u;
        unsigned th = 0u, td = 0u;
        if (nvn_deswizzle_geom(d, subs[i].level, subs[i].layer, &row_bytes, &rows, &slices, &base, &th, &td, &bpb) ||
            subs[i].pitch < row_bytes)
            return -1;
        assert(th <= 5u && td <= 5u);
        bytes += (uint64_t)row_bytes * rows * slices;
        uint64_t rb_pad = (fmode == 103) ? 0u : 63u;
        uint64_t rh_pad = (fmode == 104) ? 0u : ((UINT64_C(8) << th) - 1u);
        uint64_t block_row = (((uint64_t)row_bytes + rb_pad) >> 6u) * (UINT64_C(512) << (th + td));
        uint64_t block_rows_y = ((uint64_t)rows + rh_pad) >> (th + 3u);
        uint64_t block_rows_z = ((uint64_t)slices + (UINT64_C(1) << td) - 1u) >> td;
        uint64_t block_rows = block_rows_y * block_rows_z;
        assert(block_row > 0u);
        uint64_t per_run = (block_row >= UPLOAD_POOL_RUN_BYTES)
            ? 1u
            : ((UPLOAD_POOL_RUN_BYTES + block_row - 1u) / block_row);
        uint64_t sub_runs = (block_rows == 0u) ? 0u
            : (per_run == 1u ? block_rows : ((block_rows + per_run - 1u) / per_run));
        uint64_t total = block_row * block_rows;
        uint64_t step = per_run * block_row;
        if (geoms && i < geom_cap) geoms[i] = (totk_upload_sub_geom){block_row, total, step};
        n += sub_runs;
    }
    *texels = bytes;
    *nruns = n;
    return 0;
}

static inline void totk_upload_emit_runs(const upload_sub *subs, unsigned nsubs,
                                         const totk_upload_sub_geom *geoms,
                                         upload_run *out, uint64_t expected_runs) {
    int fmode = totk_w30_fault_mode();
    uint64_t n = 0u;
    for (unsigned i = 0u; i < nsubs; i++) {
        uint64_t total = geoms[i].total, step = geoms[i].step;
        for (uint64_t first = 0u; first < total; first += step, n++) {
            uint64_t rem = total - first;
            uint64_t cnt = (fmode == 105 || rem < step) ? rem : step;
            out[n] = (upload_run){subs[i].level, subs[i].layer, subs[i].dst, subs[i].pitch, first, cnt};
        }
    }
    assert(n == expected_runs);
}
