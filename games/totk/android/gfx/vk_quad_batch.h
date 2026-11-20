/* Render-pass look-ahead batching and conversion cache for indirect QUADS draws (vk_draw.c).
 * Mirrors hwaoi-android's vka_exec.c qdx_scan/qdx_convert pattern so indirect quad draws inside a pass
 * execute without splitting the active Vulkan render pass or emitting mid-pass pipeline barriers. */
#pragma once
#include "nvn_obj.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum { VK_QI_BATCH_MAX = 64, VK_QI_SCAN_OPS_MAX = 4096, VK_QI_CALL_DEPTH = 16 };

typedef struct {
    uint64_t ind;
    uint64_t off;
    uint64_t buf_u64;
} vk_qi_ent;

typedef struct {
    uint64_t sub;
    uint64_t wgen;
    int n;
    vk_qi_ent ent[VK_QI_BATCH_MAX];
} vk_qi_cache;

static inline int vk_qi_cache_valid(const vk_qi_cache *c, uint64_t sub, uint64_t wgen) {
    assert(c != NULL);
    return c->n > 0 && c->sub == sub && c->wgen == wgen;
}

static inline void vk_qi_cache_reset(vk_qi_cache *c, uint64_t sub, uint64_t wgen) {
    assert(c != NULL);
    c->sub = sub;
    c->wgen = wgen;
    c->n = 0;
}

static inline int vk_qi_cache_find(const vk_qi_cache *c, uint64_t sub, uint64_t wgen, uint64_t ind,
                                     uint64_t *buf_u64, uint64_t *off) {
    assert(c != NULL);
    if (!vk_qi_cache_valid(c, sub, wgen)) return 0;
    for (int i = 0; i < c->n && i < VK_QI_BATCH_MAX; i++) {
        if (c->ent[i].ind == ind) {
            if (buf_u64) *buf_u64 = c->ent[i].buf_u64;
            if (off) *off = c->ent[i].off;
            return 1;
        }
    }
    return 0;
}

static inline int vk_qi_cache_put(vk_qi_cache *c, uint64_t ind, uint64_t buf_u64, uint64_t off) {
    assert(c != NULL);
    for (int i = 0; i < c->n && i < VK_QI_BATCH_MAX; i++) {
        if (c->ent[i].ind == ind) { c->ent[i].buf_u64 = buf_u64; c->ent[i].off = off; return 1; }
    }
    if (c->n >= VK_QI_BATCH_MAX) return 0;
    c->ent[c->n++] = (vk_qi_ent){.ind = ind, .off = off, .buf_u64 = buf_u64};
    return 1;
}

static inline int vk_qi_op_stops_scan(uint32_t op, const void *a, const p_set_rt *cur_rt,
                                      int (*rt_same_fn)(const p_set_rt *, const p_set_rt *)) {
    switch (op) {
    case OP_SIGNAL_EVENT: case OP_REPORT_COUNTER: case OP_COPY_B2B: case OP_UPDATE_UB:
    case OP_DISPATCH: case OP_COPY_B2T: case OP_COPY_T2B: case OP_COPY_T2T:
    case OP_RESET_COUNTER: case OP_CLEAR_TEX: case OP_RENDER_COND:
        return 1;
    case OP_SET_RT:
        return !cur_rt || !rt_same_fn || !rt_same_fn(cur_rt, (const p_set_rt *)a);
    case OP_BARRIER: {
        int bits = 0;
        memcpy(&bits, a, sizeof bits);
        return (bits & ~NVN_BARRIER_ORDER_PRIMITIVES_BIT) != 0;
    }
    default:
        return op >= OP_COUNT_;
    }
}

static inline void vk_qi_push_unique(uint64_t ind, const vk_qi_cache *existing, uint64_t sub, uint64_t wgen,
                                     int (*gpu_pending_fn)(uint64_t, uint64_t),
                                     uint64_t *out, int *nq, int max_out) {
    assert(nq != NULL && out != NULL && gpu_pending_fn != NULL);
    if (!ind || *nq >= max_out) return;
    if (existing && vk_qi_cache_find(existing, sub, wgen, ind, NULL, NULL)) return;
    for (int k = 0; k < *nq; k++) if (out[k] == ind) return;
    if (!gpu_pending_fn(ind, 16)) return;
    out[(*nq)++] = ind;
}

static inline int vk_qi_scan_ops(const uint8_t *p, uint64_t cur_ind, const p_set_rt *cur_rt,
                                 int (*rt_same_fn)(const p_set_rt *, const p_set_rt *),
                                 int (*gpu_pending_fn)(uint64_t, uint64_t),
                                 const vk_qi_cache *existing, uint64_t sub, uint64_t wgen,
                                 uint64_t *out, int max_out) {
    assert(gpu_pending_fn != NULL && out != NULL);
    assert(max_out > 0 && max_out <= VK_QI_BATCH_MAX);
    int nq = 0, nret = 0;
    const uint8_t *ret[VK_QI_CALL_DEPTH];
    vk_qi_push_unique(cur_ind, existing, sub, wgen, gpu_pending_fn, out, &nq, max_out);
    for (int guard = 0; p && guard < VK_QI_SCAN_OPS_MAX && nq < max_out; guard++) {
        op_hdr h; memcpy(&h, p, sizeof h);
        const void *a = p + sizeof h;
        if (h.op == OP_END) { if (!nret) break; p = ret[--nret]; continue; }
        if (h.size < 8 || (h.size & 7u)) break;
        if (h.op == OP_JUMP) { memcpy(&p, a, 8); continue; }
        if (h.op == OP_CALL) { if (nret >= VK_QI_CALL_DEPTH) break; ret[nret++] = p + h.size; memcpy(&p, a, 8); continue; }
        if (vk_qi_op_stops_scan(h.op, a, cur_rt, rt_same_fn)) break;
        if (h.op == OP_DRAW_INDIRECT && h.size >= (uint32_t)(sizeof h + sizeof(p_draw_indirect))) {
            p_draw_indirect iw; memcpy(&iw, a, sizeof iw);
            if (iw.prim == 7) vk_qi_push_unique(iw.buf, existing, sub, wgen, gpu_pending_fn, out, &nq, max_out);
        }
        p += h.size;
    }
    return nq;
}
