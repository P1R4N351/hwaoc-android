#ifndef TOTK_VK_SHADER_FAST_H
#define TOTK_VK_SHADER_FAST_H
/* Wave 8 Part A: lock-free read-clean shader specialization, layout, template & deps fast paths (TOTK_VK_SHADER_FAST=1). */
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef TOTK_KTGL_OPT_TEST
extern int g_fault_mode;
#define TOTK_SHADER_FAST_FAULT(n) (g_fault_mode == (n))
#else
#define TOTK_SHADER_FAST_FAULT(n) 0
#endif

enum { TOTK_VFIX_SETS = 256, TOTK_LAY_SETS = 512, TOTK_TMPL_SETS = 512, TOTK_SH_WAYS = 4 };

typedef struct {
    uint64_t vs, fs, mod;
    uint8_t valid, pad[7];
} totk_vfix_ent;

typedef struct {
    totk_vfix_ent e[TOTK_SH_WAYS];
    uint32_t rr, pad;
} totk_vfix_set;

typedef struct {
    totk_vfix_set s[TOTK_VFIX_SETS];
} totk_vfix_memo4;

typedef struct {
    uint64_t vs, fs, layout;
} totk_lay_ent;

typedef struct {
    totk_lay_ent e[TOTK_SH_WAYS];
    uint32_t rr, pad;
} totk_lay_set;

typedef struct {
    totk_lay_set s[TOTK_LAY_SETS];
} totk_lay_memo4;

typedef struct {
    uint64_t dsl, tmpl;
} totk_tmpl_ent;

typedef struct {
    totk_tmpl_ent e[TOTK_SH_WAYS];
    uint32_t rr, pad;
} totk_tmpl_set;

typedef struct {
    totk_tmpl_set s[TOTK_TMPL_SETS];
} totk_tmpl_memo4;

static inline int totk_vk_shader_fast_on(void) {
    static _Atomic int v = -1;
    int r = atomic_load_explicit(&v, memory_order_relaxed);
    if (r < 0) {
        const char *e = getenv("TOTK_VK_SHADER_FAST");
        r = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&v, r, memory_order_relaxed);
    }
    return r;
}

static inline uint32_t totk_sh_mix(uint64_t x, uint32_t sets) {
    uint64_t h = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    h = (h ^ (h >> 27)) * 0x94d049bb133111ebull;
    return (uint32_t)((h ^ (h >> 31)) & (uint64_t)(sets - 1u));
}

static inline uint64_t totk_pack_vfix_kind(uint32_t missing, const uint8_t kind[32]) {
    assert(kind != NULL);
    uint64_t pack = 0;
    for (int l = 0; l < 32; l++) {
        if ((missing >> l) & 1u) pack |= ((uint64_t)(kind[l] & 3u)) << (2 * l);
    }
    return pack;
}

static inline int totk_vfix_memo4_find(const totk_vfix_memo4 *m, uint64_t vs, uint64_t fs, uint64_t *mod_out) {
    assert(m != NULL && mod_out != NULL);
    if (!vs || !fs) return 0;
    uint32_t idx = totk_sh_mix(vs * 0x9e3779b97f4a7c15ull ^ fs, TOTK_VFIX_SETS);
    const totk_vfix_set *set = &m->s[idx];
    for (uint32_t w = 0; w < TOTK_SH_WAYS; w++) {
        const totk_vfix_ent *e = &set->e[w];
        if (e->valid && e->vs == vs && (TOTK_SHADER_FAST_FAULT(19) || e->fs == fs)) {
            *mod_out = e->mod;
            return 1;
        }
    }
    return 0;
}

static inline void totk_vfix_memo4_put(totk_vfix_memo4 *m, uint64_t vs, uint64_t fs, uint64_t mod) {
    assert(m != NULL);
    if (!vs || !fs) return;
    uint32_t idx = totk_sh_mix(vs * 0x9e3779b97f4a7c15ull ^ fs, TOTK_VFIX_SETS);
    totk_vfix_set *set = &m->s[idx];
    uint32_t slot = TOTK_SH_WAYS;
    for (uint32_t w = 0; w < TOTK_SH_WAYS; w++) {
        if (!set->e[w].valid || (set->e[w].vs == vs && set->e[w].fs == fs)) { slot = w; break; }
    }
    if (slot == TOTK_SH_WAYS) slot = set->rr++ & (TOTK_SH_WAYS - 1u);
    set->e[slot] = (totk_vfix_ent){vs, fs, mod, 1, {0}};
}

static inline uint64_t totk_lay_memo4_find(const totk_lay_memo4 *m, uint64_t vs, uint64_t fs) {
    assert(m != NULL);
    uint32_t idx = totk_sh_mix(vs * 0x9e3779b97f4a7c15ull ^ fs, TOTK_LAY_SETS);
    const totk_lay_set *set = &m->s[idx];
    for (uint32_t w = 0; w < TOTK_SH_WAYS; w++) {
        const totk_lay_ent *e = &set->e[w];
        if (e->layout && e->vs == vs && e->fs == fs) return e->layout;
    }
    return 0;
}

static inline void totk_lay_memo4_put(totk_lay_memo4 *m, uint64_t vs, uint64_t fs, uint64_t layout) {
    assert(m != NULL);
    if (!layout) return;
    uint32_t idx = totk_sh_mix(vs * 0x9e3779b97f4a7c15ull ^ fs, TOTK_LAY_SETS);
    totk_lay_set *set = &m->s[idx];
    uint32_t slot = TOTK_SH_WAYS;
    for (uint32_t w = 0; w < TOTK_SH_WAYS; w++) {
        if (!set->e[w].layout || (set->e[w].vs == vs && set->e[w].fs == fs)) { slot = w; break; }
    }
    if (slot == TOTK_SH_WAYS) slot = set->rr++ & (TOTK_SH_WAYS - 1u);
    set->e[slot] = (totk_lay_ent){vs, fs, layout};
}

static inline uint64_t totk_tmpl_memo4_find(const totk_tmpl_memo4 *m, uint64_t dsl) {
    assert(m != NULL);
    if (!dsl) return 0;
    const totk_tmpl_set *set = &m->s[totk_sh_mix(dsl, TOTK_TMPL_SETS)];
    for (uint32_t w = 0; w < TOTK_SH_WAYS; w++) {
        if (set->e[w].dsl == dsl && set->e[w].tmpl) return set->e[w].tmpl;
    }
    return 0;
}

static inline void totk_tmpl_memo4_put(totk_tmpl_memo4 *m, uint64_t dsl, uint64_t tmpl) {
    assert(m != NULL);
    if (!dsl || !tmpl) return;
    totk_tmpl_set *set = &m->s[totk_sh_mix(dsl, TOTK_TMPL_SETS)];
    uint32_t slot = TOTK_SH_WAYS;
    for (uint32_t w = 0; w < TOTK_SH_WAYS; w++) {
        if (!set->e[w].tmpl || set->e[w].dsl == dsl) { slot = w; break; }
    }
    if (slot == TOTK_SH_WAYS) slot = set->rr++ & (TOTK_SH_WAYS - 1u);
    set->e[slot] = (totk_tmpl_ent){dsl, tmpl};
}

static inline int totk_deps_next(const char **pp, char kind[16], int *slot_out, char want[64], int *bad_out) {
    assert(pp != NULL && *pp != NULL && kind != NULL && slot_out != NULL && want != NULL && bad_out != NULL);
    const char *p = *pp;
    while (*p == ';') p++;
    if (!*p) { *pp = p; return 0; }
    const char *end = p, *colon = NULL, *eq = NULL;
    while (*end && *end != ';') {
        if (*end == ':' && !colon) colon = end;
        else if (colon && *end == '=' && !eq) eq = end;
        end++;
    }
    if (!colon || !eq) { *bad_out = 1; *pp = end; return 0; }
    size_t klen = (size_t)(colon - p), slen = (size_t)(eq - (colon + 1)), wlen = (size_t)(end - (eq + 1));
    if (klen >= 16 || slen >= 16 || wlen >= 64) { *bad_out = 2; *pp = end; return 0; }
    for (size_t i = 0; i < klen; i++) kind[i] = p[i];
    kind[klen] = '\0';
    char sbuf[16];
    for (size_t i = 0; i < slen; i++) sbuf[i] = colon[1 + i];
    sbuf[slen] = '\0';
    for (size_t i = 0; i < wlen; i++) want[i] = eq[1 + i];
    want[wlen] = '\0';
    *slot_out = (int)strtol(sbuf, NULL, 10) + (TOTK_SHADER_FAST_FAULT(20) ? 1 : 0);
    *pp = (*end == ';') ? end + 1 : end;
    return 1;
}

#endif
