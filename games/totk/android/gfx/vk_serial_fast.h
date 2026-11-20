#ifndef TOTK_VK_SERIAL_FAST_H
#define TOTK_VK_SERIAL_FAST_H
/* Wave 21 (Opt 23): Lock-free TLS tex_get_inner front-memo & idle shared-image sweep summary (TOTK_VK_SERIAL_FAST=1). */
#include <assert.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TOTK_KTGL_OPT_TEST
extern int g_fault_mode;
#define TOTK_SERIAL_FAULT(n) (g_fault_mode == (n))
#else
#define TOTK_SERIAL_FAULT(n) 0
#endif

static inline int totk_vk_serial_fast_on(void) {
    static _Atomic int cached = -1;
    int v = atomic_load_explicit(&cached, memory_order_relaxed);
    if (v < 0) {
        const char *e = getenv("TOTK_VK_SERIAL_FAST");
        v = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&cached, v, memory_order_relaxed);
    }
    return v;
}

enum {
    TOTK_TEX_SERIAL_SETS = 64u,
    TOTK_TEX_SERIAL_WAYS = 4u
};

static inline uint32_t totk_serial_hash64(uint64_t x) {
    x ^= x >> 33;
    x *= UINT64_C(0xff51afd7ed558ccd);
    x ^= x >> 33;
    return (uint32_t)x ^ (uint32_t)(x >> 16);
}

typedef struct {
    uint64_t serial, clean_sub, x_u;
    uint32_t sgen;
    uint8_t non_mirrored, valid;
    uint8_t pad[2];
} totk_tex_serial_ent;

typedef struct {
    totk_tex_serial_ent e[TOTK_TEX_SERIAL_SETS][TOTK_TEX_SERIAL_WAYS];
    uint8_t hand[TOTK_TEX_SERIAL_SETS];
} totk_tex_serial_memo4;

static inline int totk_tex_serial_memo4_get(const totk_tex_serial_memo4 *m,
                                            uint64_t serial, uint32_t sgen,
                                            uint64_t sub, uint64_t *x_out) {
    assert(m != NULL && x_out != NULL);
    if (!serial || !sgen) return 0;
    uint32_t s = totk_serial_hash64(serial) & (TOTK_TEX_SERIAL_SETS - 1u);
    for (uint32_t w = 0; w < TOTK_TEX_SERIAL_WAYS; w++) {
        const totk_tex_serial_ent *e = &m->e[s][w];
        if (!e->valid || e->serial != serial) continue;
        if (!TOTK_SERIAL_FAULT(59) && e->sgen != sgen) continue;
        if (!TOTK_SERIAL_FAULT(60) && !e->non_mirrored && e->clean_sub != sub) continue;
        *x_out = e->x_u;
        return 1;
    }
    return 0;
}

static inline void totk_tex_serial_memo4_put(totk_tex_serial_memo4 *m,
                                             uint64_t serial, uint32_t sgen,
                                             uint64_t sub, uint8_t non_mirrored,
                                             uint64_t x_u) {
    assert(m != NULL);
    if (!serial || !sgen || !x_u) return;
    uint32_t s = totk_serial_hash64(serial) & (TOTK_TEX_SERIAL_SETS - 1u);
    for (uint32_t w = 0; w < TOTK_TEX_SERIAL_WAYS; w++) {
        totk_tex_serial_ent *e = &m->e[s][w];
        if (!e->valid || e->serial == serial) {
            e->serial = serial; e->clean_sub = sub; e->x_u = x_u;
            e->sgen = sgen; e->non_mirrored = non_mirrored ? 1u : 0u; e->valid = 1u;
            return;
        }
    }
    uint32_t w = m->hand[s] & (TOTK_TEX_SERIAL_WAYS - 1u);
    m->hand[s] = (uint8_t)((w + 1u) & (TOTK_TEX_SERIAL_WAYS - 1u));
    totk_tex_serial_ent *e = &m->e[s][w];
    e->serial = serial; e->clean_sub = sub; e->x_u = x_u;
    e->sgen = sgen; e->non_mirrored = non_mirrored ? 1u : 0u; e->valid = 1u;
}

typedef struct {
    uint32_t count;
    uint64_t min_idle_sub;
} totk_idle_shared_summary;

static inline void totk_idle_shared_note(totk_idle_shared_summary *s, uint64_t idle_sub) {
    assert(s != NULL);
    if (!s->count || idle_sub < s->min_idle_sub) s->min_idle_sub = idle_sub;
    s->count++;
}

static inline int totk_idle_shared_can_skip(const totk_idle_shared_summary *s,
                                            uint64_t sub, uint64_t max_age) {
    assert(s != NULL);
    if (TOTK_SERIAL_FAULT(61)) return 1;
    if (!s->count || sub < s->min_idle_sub) return 1;
    return (sub - s->min_idle_sub) < max_age;
}

static inline void totk_idle_shared_reset(totk_idle_shared_summary *s,
                                          uint32_t count, uint64_t min_idle_sub) {
    assert(s != NULL);
    s->count = count;
    s->min_idle_sub = min_idle_sub;
}

#endif
