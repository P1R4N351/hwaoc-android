/* Wave 26 (Opt 28) tests, TSan stress, microbench, and faults 82..86 for TOTK_GATE_FAST. */
#pragma once
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOTK_GATE_FAST_TEST_HOOKS 1
#include "totk_gate_fast.h"

uint64_t host_now_ns(void);

typedef struct {
    uint64_t ret;
    uint64_t original;
    unsigned timer;
    unsigned arg;
} w26_gate_t;

typedef struct {
    const char *name;
    uint64_t base;
    uint64_t size;
    const w26_gate_t *gates;
    unsigned ngate;
} w26_mod_t;

static w26_mod_t g_w26_mods[3];
static w26_gate_t g_w26_gates[3][600];

static const w26_gate_t *w26_slow_gate(uint64_t ret, const char **mname) {
    for (unsigned i = 0; i < 3; i++) {
        uint64_t b = g_w26_mods[i].base;
        if (!b || ret < b) continue;
        unsigned lo = 0, hi = g_w26_mods[i].ngate;
        while (lo < hi) {
            unsigned m = lo + (hi - lo) / 2;
            if (b + g_w26_mods[i].gates[m].ret < ret) lo = m + 1;
            else hi = m;
        }
        if (lo < g_w26_mods[i].ngate && b + g_w26_mods[i].gates[lo].ret == ret) {
            *mname = g_w26_mods[i].name;
            return &g_w26_mods[i].gates[lo];
        }
    }
    return NULL;
}

static int w26_slow_mod_off(uint64_t pc, const char **name, uint64_t *off) {
    for (unsigned i = 0; i < 3; i++) {
        if (pc >= g_w26_mods[i].base && pc < g_w26_mods[i].base + g_w26_mods[i].size) {
            *name = g_w26_mods[i].name;
            *off = pc - g_w26_mods[i].base;
            return 1;
        }
    }
    return 0;
}

static int w26_fast_mod_off(totk_mod_cache *mc, uint64_t pc, const char **name, uint64_t *off) {
    if (totk_gate_fast_active()) {
        int miss = 1;
        if (totk_mod_cache_try(mc, pc, name, off, &miss)) return 1;
        if (!miss) return 0;
    }
    for (unsigned i = 0; i < 3; i++) {
        if (pc >= g_w26_mods[i].base && pc < g_w26_mods[i].base + g_w26_mods[i].size) {
            *name = g_w26_mods[i].name;
            *off = pc - g_w26_mods[i].base;
            if (totk_gate_fast_active())
                totk_mod_cache_write(mc, pc, g_w26_mods[i].name, g_w26_mods[i].base,
                                     g_w26_mods[i].base + g_w26_mods[i].size);
            return 1;
        }
    }
    return 0;
}

static void w26_build_fixture(totk_gate_table *gt, totk_mod_cache *mc) {
    static const char *const names[3] = {"main", "subsdk0", "sdk"};
    memset(gt, 0, sizeof *gt);
    memset(mc, 0, sizeof *mc);
    uint32_t total = 0;
    uint64_t lo = UINT64_MAX, hi = 0;
    for (unsigned i = 0; i < 3; i++) {
        g_w26_mods[i].name = names[i];
        g_w26_mods[i].base = 0x4000000000ull + (uint64_t)i * 0x2000000ull;
        g_w26_mods[i].size = (i == 0) ? 0x1008000ull : 0x400000ull;
        g_w26_mods[i].ngate = 600u;
        g_w26_mods[i].gates = g_w26_gates[i];
        for (unsigned k = 0; k < 600u; k++) {
            g_w26_gates[i][k] = (w26_gate_t){
                .ret = 0x1000ull + (uint64_t)k * 0x200ull,
                .original = 0x1000ull + (uint64_t)k * 0x200ull - 4ull,
                .timer = (k % 5u == 0u) ? 1u : 0u,
                .arg = (k % 5u == 0u) ? (k % 8u) : (0x06u + (k % 4u)),
            };
        }
        total += g_w26_mods[i].ngate;
        if (g_w26_mods[i].base < lo) lo = g_w26_mods[i].base;
        if (g_w26_mods[i].base + g_w26_mods[i].size > hi) hi = g_w26_mods[i].base + g_w26_mods[i].size;
    }
    assert(totk_gate_table_init(gt, total));
    for (unsigned i = 0; i < 3; i++) {
        for (unsigned k = 0; k < g_w26_mods[i].ngate; k++) {
            const w26_gate_t *g = &g_w26_mods[i].gates[k];
            assert(totk_gate_table_insert(gt, g_w26_mods[i].base + g->ret, g, g_w26_mods[i].name));
        }
    }
    totk_gate_table_publish(gt);
    totk_mod_cache_publish(mc, lo, hi);
}

static int w26_check_fault(int f, totk_gate_table *gt, totk_mod_cache *mc) {
    totk_gate_fast_reset_for_test(1);
    if (f == 82) {
        uint64_t r = g_w26_mods[2].base + g_w26_gates[2][599].ret;
        const totk_gate_slot *s = totk_gate_table_find(gt, r);
        return (s && s->gate == &g_w26_gates[2][599]) ? 0 : 1;
    }
    if (f == 83) {
        uint64_t target = g_w26_mods[1].base + g_w26_gates[1][12].ret;
        const totk_gate_slot *s = totk_gate_table_find(gt, target + 8ull);
        return s == NULL ? 0 : 1;
    }
    if (f == 84) {
        const char *nm = NULL;
        uint64_t off = 0;
        uint64_t tail = g_w26_mods[2].base + g_w26_mods[2].size - 4ull;
        return (w26_fast_mod_off(mc, tail, &nm, &off) && strcmp(nm, "sdk") == 0 && off == g_w26_mods[2].size - 4ull) ? 0 : 1;
    }
    if (f == 85) {
        const char *nm = NULL;
        uint64_t off = 0;
        uint64_t in_mod = g_w26_mods[0].base + 0x1004000ull;
        uint64_t just_out = g_w26_mods[0].base + g_w26_mods[0].size + 0x100ull;
        assert(w26_fast_mod_off(mc, in_mod, &nm, &off) && strcmp(nm, "main") == 0);
        return w26_fast_mod_off(mc, just_out, &nm, &off) == 0 ? 0 : 1;
    }
    if (f == 86) {
        uint64_t r = g_w26_mods[2].base + g_w26_gates[2][10].ret;
        const totk_gate_slot *s = totk_gate_table_find(gt, r);
        const w26_gate_t *g = s ? (const w26_gate_t *)s->gate : NULL;
        if (totk_gate_w26_fault() == 86) return 1;
        return (s && g && g->timer == 1u && strcmp(s->mod_name, "sdk") == 0) ? 0 : 1;
    }
    return 1;
}

typedef struct {
    totk_gate_table *gt;
    totk_mod_cache *mc;
} w26_stress_t;

static void *w26_worker(void *p) {
    w26_stress_t *c = (w26_stress_t *)p;
    for (int i = 0; i < 6000; i++) {
        unsigned mi = (unsigned)(i % 3);
        unsigned gi = (unsigned)((i * 7) % 600);
        uint64_t ret = g_w26_mods[mi].base + g_w26_gates[mi][gi].ret;
        const totk_gate_slot *s = totk_gate_table_find(c->gt, ret);
        assert(s && s->gate == &g_w26_gates[mi][gi]);
        assert(strcmp(s->mod_name, g_w26_mods[mi].name) == 0);
        const char *nm = NULL;
        uint64_t off = 0;
        assert(w26_fast_mod_off(c->mc, g_w26_mods[mi].base + 0x12000ull, &nm, &off));
        assert(strcmp(nm, g_w26_mods[mi].name) == 0 && off == 0x12000ull);
        assert(!w26_fast_mod_off(c->mc, 0x7fff12340000ull, &nm, &off));
    }
    return NULL;
}

static int w26_run_bench(totk_gate_table *gt, totk_mod_cache *mc) {
    totk_gate_fast_reset_for_test(1);
    for (int mode = 0; mode < 2; mode++) {
        volatile uint64_t sink = 0;
        totk_gate_fast_reset_for_test(mode);
        uint64_t t0 = host_now_ns();
        for (int i = 0; i < 200000; i++) {
            unsigned mi = (unsigned)(i % 3);
            unsigned gi = (unsigned)((i * 13) % 600);
            uint64_t ret = g_w26_mods[mi].base + g_w26_gates[mi][gi].ret;
            uint64_t pc = (i & 1) ? 0x7fff00012340ull : (g_w26_mods[mi].base + 0x12345ull);
            const char *gnm = NULL, *mnm = NULL;
            uint64_t off = 0;
            const w26_gate_t *g = NULL;
            int found = 0;
            if (!mode) {
                g = w26_slow_gate(ret, &gnm);
                found = w26_slow_mod_off(pc, &mnm, &off);
            } else {
                const totk_gate_slot *s = totk_gate_table_find(gt, ret);
                if (s) {
                    g = (const w26_gate_t *)s->gate;
                    gnm = s->mod_name;
                }
                found = w26_fast_mod_off(mc, pc, &mnm, &off);
            }
            sink += (g ? g->arg + (uint64_t)gnm[0] : 0ull) + (found ? off + (uint64_t)mnm[0] : 1ull);
        }
        printf("aoc_w26_%s gate_mod_dispatch=%.3f ms sink=%llu\n", mode ? "on" : "off",
               (double)(host_now_ns() - t0) / 1e6, (unsigned long long)sink);
    }
    return 0;
}

static inline int run_ktgl_w26_tests(int fault_mode) {
    totk_gate_table gt;
    totk_mod_cache mc;
    w26_build_fixture(&gt, &mc);
    if (fault_mode >= 82 && fault_mode <= 86) {
        int rc = w26_check_fault(fault_mode, &gt, &mc);
        free(gt.slots);
        return rc;
    }
    for (int mode = 0; mode < 2; mode++) {
        totk_gate_fast_reset_for_test(mode);
        for (unsigned mi = 0; mi < 3; mi++) {
            for (unsigned gi = 0; gi < 600u; gi += 97u) {
                uint64_t ret = g_w26_mods[mi].base + g_w26_gates[mi][gi].ret;
                const char *sn = NULL;
                const w26_gate_t *sg = w26_slow_gate(ret, &sn);
                const totk_gate_slot *fs = totk_gate_table_find(&gt, ret);
                assert(sg && fs && fs->gate == sg && strcmp(fs->mod_name, sn) == 0);
            }
            const char *mn0 = NULL, *mn1 = NULL;
            uint64_t o0 = 0, o1 = 0;
            uint64_t p = g_w26_mods[mi].base + 0x18000ull;
            assert(w26_slow_mod_off(p, &mn0, &o0) == w26_fast_mod_off(&mc, p, &mn1, &o1));
            assert(o0 == o1 && strcmp(mn0, mn1) == 0);
        }
        const char *hn = NULL;
        uint64_t ho = 0;
        assert(w26_fast_mod_off(&mc, 0x7fff00010000ull, &hn, &ho) == 0);
    }
    w26_stress_t st = {.gt = &gt, .mc = &mc};
    pthread_t th[4];
    totk_gate_fast_reset_for_test(1);
    for (int i = 0; i < 4; i++) assert(pthread_create(&th[i], NULL, w26_worker, &st) == 0);
    for (int i = 0; i < 4; i++) assert(pthread_join(th[i], NULL) == 0);
    int rc = w26_run_bench(&gt, &mc);
    free(gt.slots);
    return rc;
}
