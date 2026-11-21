#pragma once
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { TOTK_SPV_STACK_BOUND = 1024u, TOTK_SPV_CERT_STACK = 256u };

#ifdef T_AOC_W39_TEST_HOOKS
extern _Atomic int g_totk_vk_spv_fast_state;
extern _Atomic int g_totk_w39_fault;
static inline void totk_vk_spv_fast_reset_for_test(int forced) {
    atomic_store_explicit(&g_totk_vk_spv_fast_state, forced >= 0 ? (forced ? 1 : 0) : -1, memory_order_release);
}
static inline void totk_w39_set_fault(int m) {
    atomic_store_explicit(&g_totk_w39_fault, m, memory_order_release);
}
static inline int totk_w39_fault(void) {
    int m = atomic_load_explicit(&g_totk_w39_fault, memory_order_acquire);
    if (m) return m;
    const char *e = getenv("TOTK_W39_FAULT");
    if (e && *e) return atoi(e);
    const char *k = getenv("TOTK_KTGL_OPT_FAULT");
    if (k && *k) { int v = atoi(k); if (v >= 141 && v <= 145) return v - 140; }
    return 0;
}
#else
static _Atomic int g_totk_vk_spv_fast_state;
static inline int totk_w39_fault(void) { return 0; }
#endif

static inline int totk_vk_spv_fast_on(void) {
    int st = atomic_load_explicit(&g_totk_vk_spv_fast_state, memory_order_acquire);
    if (st >= 0) return st;
    const char *v = getenv("TOTK_VK_SPV_FAST");
    int on = !(v && (v[0] == '0' || v[0] == 'f' || v[0] == 'F' || v[0] == 'n' || v[0] == 'N'));
    atomic_store_explicit(&g_totk_vk_spv_fast_state, on, memory_order_release);
    return on;
}

static inline void totk_spirv_patch_set_fast(uint32_t *w, size_t nw, uint32_t set) {
    if (nw < 5u || w[0] != 0x07230203u) return;
    uint32_t eff = (totk_w39_fault() == 1) ? (set ^ 1u) : set;
    for (size_t i = 5u; i < nw;) {
        uint32_t op = w[i] & 0xffffu, n = w[i] >> 16;
        if (!n || n > nw - i || op == 54u) break;
        if (op == 71u && n == 4u && w[i + 2] == 34u) w[i + 3] = eff;
        i += n;
    }
}

typedef struct {
    int heap;
    int32_t *loc;
    uint32_t *ptype;
    uint8_t *builtin, *tkind;
    int32_t sloc[TOTK_SPV_STACK_BOUND];
    uint32_t sptype[TOTK_SPV_STACK_BOUND];
    uint8_t sbuiltin[TOTK_SPV_STACK_BOUND], stkind[TOTK_SPV_STACK_BOUND];
} totk_io_tbl;

static inline int totk_io_tbl_init(totk_io_tbl *t, uint32_t bound) {
    if (bound <= TOTK_SPV_STACK_BOUND) {
        t->heap = 0; t->loc = t->sloc; t->ptype = t->sptype;
        t->builtin = t->sbuiltin; t->tkind = t->stkind;
        memset(t->builtin, 0, bound); memset(t->ptype, 0, bound * sizeof(uint32_t));
    } else {
        t->heap = 1;
        t->loc = (int32_t *)malloc(bound * sizeof *t->loc);
        t->builtin = (uint8_t *)calloc(bound, 1u);
        t->ptype = (uint32_t *)calloc(bound, sizeof *t->ptype);
        t->tkind = (uint8_t *)malloc(bound);
        if (!t->loc || !t->builtin || !t->ptype || !t->tkind) return 0;
    }
    memset(t->tkind, 255, bound);
    for (uint32_t i = 0u; i < bound; i++) t->loc[i] = -1;
    return 1;
}

static inline void totk_io_tbl_free(totk_io_tbl *t) {
    if (t->heap) { free(t->loc); free(t->builtin); free(t->ptype); free(t->tkind); }
}

static inline int totk_spirv_io_fast(
    const uint32_t *w, size_t nw, uint32_t storage, uint32_t *mask, uint8_t kind[32],
    uint8_t v_flt, uint8_t v_sint, uint8_t v_uint) {
    *mask = 0u;
    if (nw < 5u || w[0] != 0x07230203u || !w[3] || w[3] > (1u << 22)) return -1;
    uint32_t bound = w[3];
    totk_io_tbl t;
    if (!totk_io_tbl_init(&t, bound)) { totk_io_tbl_free(&t); return -1; }
    int rc = 0, skip = 0;
    for (size_t i = 5u; i < nw;) {
        uint32_t n = w[i] >> 16, op = w[i] & 0xffffu;
        if (!n || n > nw - i) { rc = -1; break; }
        if (op == 54u && storage != 7u) skip = 1;
        if (!skip) {
            if (op == 71u && n >= 4u && w[i + 1] < bound && w[i + 2] == 30u) t.loc[w[i + 1]] = (int32_t)w[i + 3];
            if (op == 71u && n >= 3u && w[i + 1] < bound && w[i + 2] == 11u) t.builtin[w[i + 1]] = 1u;
            if (op == 22u && n == 3u && w[i + 1] < bound) t.tkind[w[i + 1]] = v_flt;
            if (op == 21u && n == 4u && w[i + 1] < bound) t.tkind[w[i + 1]] = w[i + 3] ? v_sint : v_uint;
            if (op == 23u && n == 4u && w[i + 1] < bound && w[i + 2] < bound) t.tkind[w[i + 1]] = t.tkind[w[i + 2]];
            if (op == 32u && n == 4u && w[i + 1] < bound) t.ptype[w[i + 1]] = w[i + 3];
            if (op == 59u && n >= 4u && w[i + 1] < bound && w[i + 2] < bound && w[i + 3] == storage) {
                uint32_t id = w[i + 2], ty = t.ptype[w[i + 1]];
                if (!t.builtin[id] && t.loc[id] >= 0 && t.loc[id] < 32 && ty && ty < bound && t.tkind[ty] != 255u) {
                    *mask |= 1u << t.loc[id]; if (kind) kind[t.loc[id]] = t.tkind[ty];
                }
            }
        }
        i += n;
    }
    totk_io_tbl_free(&t);
    if (rc) *mask = 0u; else if (totk_w39_fault() == 2) *mask ^= 1u;
    return rc;
}

typedef struct {
    int heap;
    int32_t *loc;
    uint32_t *val, *cnt, *ptee;
    int32_t sloc[TOTK_SPV_STACK_BOUND];
    uint32_t sval[TOTK_SPV_STACK_BOUND], scnt[TOTK_SPV_STACK_BOUND], sptee[TOTK_SPV_STACK_BOUND];
} totk_fout_tbl;

static inline int totk_fout_tbl_init(totk_fout_tbl *t, uint32_t bound) {
    if (bound <= TOTK_SPV_STACK_BOUND) {
        t->heap = 0; t->loc = t->sloc; t->val = t->sval; t->cnt = t->scnt; t->ptee = t->sptee;
        memset(t->val, 0, bound * 4u); memset(t->cnt, 0, bound * 4u); memset(t->ptee, 0, bound * 4u);
    } else {
        t->heap = 1;
        t->loc = (int32_t *)malloc(bound * sizeof *t->loc); t->val = (uint32_t *)calloc(bound, 4u);
        t->cnt = (uint32_t *)calloc(bound, 4u); t->ptee = (uint32_t *)calloc(bound, 4u);
        if (!t->loc || !t->val || !t->cnt || !t->ptee) return 0;
    }
    for (uint32_t i = 0u; i < bound; i++) t->loc[i] = -1;
    return 1;
}

static inline void totk_fout_tbl_free(totk_fout_tbl *t) {
    if (t->heap) { free(t->loc); free(t->val); free(t->cnt); free(t->ptee); }
}

static inline uint32_t totk_spirv_fout_locations_fast(const uint32_t *w, size_t nw) {
    if (nw < 5u || w[0] != 0x07230203u || !w[3] || w[3] > (1u << 22)) return 0u;
    uint32_t bound = w[3], mask = 0u;
    totk_fout_tbl t;
    if (!totk_fout_tbl_init(&t, bound)) { totk_fout_tbl_free(&t); return 0u; }
    for (size_t i = 5u; i < nw;) {
        uint32_t op = w[i] & 0xffffu, n = w[i] >> 16;
        if (!n || i + n > nw || op == 54u) break;
        const uint32_t *a = w + i + 1;
        if (op == 71u && n >= 4u && a[1] == 30u && a[0] && a[0] < bound) t.loc[a[0]] = (int32_t)a[2];
        else if (op == 43u && n >= 4u && a[1] && a[1] < bound) t.val[a[1]] = a[2];
        else if (op == 28u && n >= 4u && a[0] && a[0] < bound && a[2] && a[2] < bound)
            t.cnt[a[0]] = t.val[a[2]] * (a[1] && a[1] < bound && t.cnt[a[1]] ? t.cnt[a[1]] : 1u);
        else if (op == 32u && n >= 4u && a[0] && a[0] < bound) t.ptee[a[0]] = a[2];
        else if (op == 59u && n >= 4u && a[2] == 3u && a[0] && a[0] < bound && a[1] && a[1] < bound && t.loc[a[1]] >= 0) {
            uint32_t ty = t.ptee[a[0]], span = (ty && ty < bound && t.cnt[ty]) ? t.cnt[ty] : 1u;
            for (uint32_t k = 0u; k < span && (uint32_t)t.loc[a[1]] + k < 8u; k++) mask |= 1u << ((uint32_t)t.loc[a[1]] + k);
        }
        i += n;
    }
    totk_fout_tbl_free(&t);
    return totk_w39_fault() == 3 ? (mask ^ 1u) : mask;
}

static inline uint32_t *totk_spirv_depth_nudge_emit(
    const uint32_t *w, size_t nw, uint32_t spec_id, uint32_t fragdepth, uint32_t tfloat,
    size_t first_dec, size_t after_float, int stores, size_t *out_nw) {
    uint32_t *o = (uint32_t *)malloc((nw + 8u + (size_t)stores * 5u) * sizeof *o);
    if (!o) return NULL;
    uint32_t bound = w[3], spec = bound++; size_t k = 5u, run = 5u;
    memcpy(o, w, 20u);
    uint32_t esid = (totk_w39_fault() == 4) ? (spec_id ^ 1u) : spec_id;
    for (size_t i = 5u; i < nw;) {
        uint32_t op = w[i] & 0xffffu, n = w[i] >> 16;
        if (i == first_dec) {
            if (i > run) { memcpy(o + k, w + run, (i - run) * 4u); k += i - run; }
            uint32_t d[4] = {(4u << 16) | 71u, spec, 1u, esid};
            memcpy(o + k, d, 16u); k += 4u; run = i;
        }
        if (op == 62u && n >= 3u && w[i + 1] == fragdepth) {
            if (i > run) { memcpy(o + k, w + run, (i - run) * 4u); k += i - run; }
            uint32_t t = bound++, add[5] = {(5u << 16) | 129u, tfloat, t, w[i + 2], spec};
            memcpy(o + k, add, 20u); k += 5u;
            o[k++] = w[i]; o[k++] = w[i + 1]; o[k++] = t;
            if (n > 3u) { memcpy(o + k, w + i + 3, (n - 3u) * 4u); k += n - 3u; }
            run = i + n;
        }
        i += n;
        if (i == after_float) {
            if (i > run) { memcpy(o + k, w + run, (i - run) * 4u); k += i - run; }
            uint32_t c[4] = {(4u << 16) | 50u, tfloat, spec, 0u};
            memcpy(o + k, c, 16u); k += 4u; run = i;
        }
    }
    if (nw > run) { memcpy(o + k, w + run, (nw - run) * 4u); k += nw - run; }
    o[3] = bound; *out_nw = k;
    return o;
}

static inline uint32_t *totk_spirv_depth_nudge_fast(
    const uint32_t *w, size_t nw, uint32_t spec_id, size_t *out_nw) {
    if (nw < 5u || w[0] != 0x07230203u) return NULL;
    uint32_t fragdepth = 0u, tfloat = 0u; size_t after_float = 0u, first_dec = 0u; int stores = 0;
    for (size_t i = 5u; i < nw;) {
        uint32_t op = w[i] & 0xffffu, n = w[i] >> 16;
        if (!n || i + n > nw) return NULL;
        if (op == 54u && (!fragdepth || !tfloat || !first_dec)) return NULL;
        if (op == 71u && !first_dec) first_dec = i;
        if (op == 71u && n >= 4u && w[i + 2] == 11u && w[i + 3] == 22u) fragdepth = w[i + 1];
        if (op == 22u && n >= 3u && w[i + 2] == 32u && !tfloat) { tfloat = w[i + 1]; after_float = i + n; }
        if (fragdepth && op == 62u && n >= 3u && w[i + 1] == fragdepth) stores++;
        i += n;
    }
    if (!fragdepth || !tfloat || !first_dec || !stores) return NULL;
    return totk_spirv_depth_nudge_emit(w, nw, spec_id, fragdepth, tfloat, first_dec, after_float, stores, out_nw);
}

static inline int totk_spirv_fragcoord_scan(
    const uint32_t *w, size_t nw, uint32_t bound, uint32_t root, uint8_t *ptr, uint8_t *constant) {
    ptr[root] = 15u; int res = 0, fn = 0;
    for (size_t i = 5u; i < nw && !res;) {
        uint32_t n = w[i] >> 16, op = w[i] & 0xffffu;
        if (!n || i + n > nw) { res = 1; break; }
        if (op == 54u) fn = 1;
        else if (op == 43u && n == 4u && w[i + 2] < bound && w[i + 3] < 4u) constant[w[i + 2]] = (uint8_t)(w[i + 3] + 1u);
        else if ((op == 65u || op == 66u) && n >= 5u && w[i + 2] < bound && w[i + 3] < bound && ptr[w[i + 3]]) {
            uint8_t mask = ptr[w[i + 3]], idx = w[i + 4] < bound ? constant[w[i + 4]] : 0u;
            ptr[w[i + 2]] = mask == 15u ? (idx ? (uint8_t)(1u << (idx - 1u)) : 15u) : mask;
        } else if (op == 83u && n == 4u && w[i + 2] < bound && w[i + 3] < bound) ptr[w[i + 2]] = ptr[w[i + 3]];
        else if (op == 61u && n >= 4u && w[i + 3] < bound && (ptr[w[i + 3]] & 3u)) res = 1;
        else if (fn) for (uint32_t j = 1u; j < n; j++) if (w[i + j] < bound && (ptr[w[i + j]] & 3u)) { res = 1; break; }
        i += n;
    }
    return res;
}

static inline int totk_spirv_fragcoord_xy_fast(const uint32_t *w, size_t nw) {
    if (nw < 5u || w[0] != 0x07230203u || !w[3] || w[3] > (1u << 22)) return 1;
    uint32_t bound = w[3], root = 0u; int in_fn = 0;
    for (size_t i = 5u; i < nw;) {
        uint32_t n = w[i] >> 16, op = w[i] & 0xffffu;
        if (!n || i + n > nw) return 1;
        if (op == 54u) in_fn = 1;
        if (!in_fn) {
            if (op == 71u && n >= 4u && w[i + 2] == 11u && w[i + 3] == 15u) root = w[i + 1];
            if (op == 72u && n >= 5u && w[i + 3] == 11u && w[i + 4] == 15u) return 1;
        }
        i += n;
    }
    if (!root || root >= bound) return 0;
    uint8_t sptr[TOTK_SPV_STACK_BOUND], scnst[TOTK_SPV_STACK_BOUND];
    int heap = bound > TOTK_SPV_STACK_BOUND;
    uint8_t *ptr = heap ? (uint8_t *)calloc(bound, 1u) : sptr;
    uint8_t *cnst = heap ? (uint8_t *)calloc(bound, 1u) : scnst;
    if (!ptr || !cnst) { if (heap) { free(ptr); free(cnst); } return -1; }
    if (!heap) { memset(ptr, 0, bound); memset(cnst, 0, bound); }
    int res = totk_spirv_fragcoord_scan(w, nw, bound, root, ptr, cnst);
    if (heap) { free(ptr); free(cnst); }
    return res;
}

static inline void totk_spirv_mark_stores_scan(
    const uint32_t *w, size_t nw, uint32_t bound,
    int32_t *bind, uint32_t *root, uint8_t *isbuf, uint8_t *stored) {
    for (size_t i = 5u; i < nw;) {
        uint32_t op = w[i] & 0xffffu, n = w[i] >> 16;
        if (!n || i + n > nw) break;
        const uint32_t *a = w + i + 1;
        switch (op) {
            case 71u:
                if (n >= 4u && a[1] == 33u && a[0] && a[0] < bound) bind[a[0]] = (int32_t)a[2];
                break;
            case 59u:
                if (n >= 4u && a[1] && a[1] < bound && (a[2] == 12u || a[2] == 2u)) { isbuf[a[1]] = 1u; root[a[1]] = a[1]; }
                break;
            case 65u: case 66u: case 67u: case 70u: case 83u: case 124u:
                if (n >= 4u && a[1] && a[1] < bound && a[2] && a[2] < bound && root[a[2]]) root[a[1]] = root[a[2]];
                break;
            case 62u: case 63u: case 228u:
                if (n >= (op == 228u ? 2u : 3u) && a[0] && a[0] < bound && root[a[0]]) stored[root[a[0]]] = 1u;
                break;
            case 57u:
                for (uint32_t k = 3u; k + 1u < n; k++) if (a[k] && a[k] < bound && root[a[k]]) stored[root[a[k]]] = 1u;
                break;
            default:
                if (((op >= 229u && op <= 242u) || op == 6035u || op == 5614u || op == 5615u) &&
                    n >= 4u && a[2] && a[2] < bound && root[a[2]]) stored[root[a[2]]] = 1u;
                break;
        }
        i += n;
    }
}

static inline int totk_vk_hex_nibble(unsigned char c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return (int)(c - 'a' + 10);
    return (c >= 'A' && c <= 'F') ? (int)(c - 'A' + 10) : -1;
}

static inline const char *totk_vk_parse_i32(const char *p, const char *end, int32_t *out) {
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    if (p >= end) return NULL;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; } else if (*p == '+') p++;
    if (p >= end || *p < '0' || *p > '9') return NULL;
    uint32_t acc = 0u;
    while (p < end && *p >= '0' && *p <= '9') { acc = acc * 10u + (uint32_t)(*p - '0'); p++; }
    *out = neg ? -(int32_t)acc : (int32_t)acc;
    return p;
}

static inline const char *totk_vk_parse_u64(const char *p, const char *end, uint64_t *out) {
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    if (p >= end || *p < '0' || *p > '9') return NULL;
    uint64_t acc = 0u;
    while (p < end && *p >= '0' && *p <= '9') { acc = acc * 10u + (uint64_t)(*p - '0'); p++; }
    *out = acc;
    return p;
}

static inline int totk_vk_atoi_span(const char *p, const char *end) {
    int32_t v = 0; return totk_vk_parse_i32(p, end, &v) ? (int)v : 0;
}

static inline void totk_vk_refl_parse_vin(const char *p, const char *end, uint32_t *vin_mask, uint8_t vin_class[32]) {
    while (p < end) {
        while (p < end && *p == ' ') p++;
        if (p >= end) break;
        int32_t loc = 0, cls = 0;
        const char *q = totk_vk_parse_i32(p, end, &loc);
        if (q && q < end && *q == ':') {
            q = totk_vk_parse_i32(q + 1, end, &cls);
            if (q && loc >= 0 && loc < 32) { *vin_mask |= 1u << (unsigned)loc; vin_class[loc] = (uint8_t)cls; }
        }
        while (p < end && *p != ' ') p++;
    }
    if (totk_w39_fault() == 5) *vin_mask ^= 1u;
}

static inline void totk_vk_refl_parse_fout(const char *p, const char *end, uint32_t *fout_mask) {
    while (p < end) {
        while (p < end && *p == ' ') p++;
        if (p >= end) break;
        int32_t i = totk_vk_atoi_span(p, end);
        if (i >= 0 && i < 8) *fout_mask |= 1u << (unsigned)i;
        while (p < end && *p != ' ') p++;
    }
}

static inline int totk_vk_refl_parse_bind(const char *p, const char *end, int32_t v[10], uint64_t *extent) {
    for (int k = 0; k < 10; k++) {
        p = totk_vk_parse_i32(p, end, &v[k]);
        if (!p) return 0;
    }
    *extent = 0u;
    (void)totk_vk_parse_u64(p, end, extent);
    return 1;
}

static inline void totk_vk_refl_decode_c1(const char *h, const char *end, uint8_t **out_c1, uint32_t *out_len) {
    size_t len = (size_t)(end - h), n = len / 2u;
    uint8_t *buf = (uint8_t *)calloc(1u, n ? n : 1u);
    if (!buf) return;
    for (size_t i = 0u; i < n; i++) {
        int a = totk_vk_hex_nibble((unsigned char)h[2u * i]);
        int b = totk_vk_hex_nibble((unsigned char)h[2u * i + 1u]);
        buf[i] = (uint8_t)((a < 0 || b < 0) ? 0 : ((a << 4) | b));
    }
    *out_c1 = buf; *out_len = (uint32_t)n;
}
