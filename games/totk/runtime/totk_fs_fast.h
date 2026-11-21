/* Lock-free TOTK_FS_LOOKUP_FAST monotonic RomFS cache, mount table, and fd side-slot (Opt 29). */
#pragma once
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TOTK_FS_ROM_WAYS 2048u
#define TOTK_FS_ROM_PROBE 8u
#define TOTK_FS_PATH_CAP 128u
#define TOTK_FS_MNT_WAYS 256u
#define TOTK_FS_MNT_PROBE 16u
#define TOTK_FS_MNT_NAME_CAP 32u

enum {
    TOTK_FS_FSTATE_UNK = 0u,
    TOTK_FS_FSTATE_FILE = 1u,
    TOTK_FS_FSTATE_NOFILE = 2u
};

enum {
    TOTK_FS_DSTATE_UNK = 0u,
    TOTK_FS_DSTATE_DIR = 1u,
    TOTK_FS_DSTATE_NODIR = 2u
};

static inline int totk_fs_fast_enabled(void) {
    static _Atomic int on = -1;
    int v = atomic_load_explicit(&on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("TOTK_FS_LOOKUP_FAST");
        v = (e && e[0] == '0' && e[1] == '\0') ? 0 : 1;
        atomic_store_explicit(&on, v, memory_order_release);
    }
    return v;
}

#ifdef TOTK_FS_FAST_TEST_HOOKS
static _Atomic int g_totk_fs_override = -1;

static inline void totk_fs_reset_for_test(int en) {
    atomic_store_explicit(&g_totk_fs_override, en, memory_order_release);
}

static inline int totk_fs_fast_active(void) {
    int ov = atomic_load_explicit(&g_totk_fs_override, memory_order_acquire);
    return ov >= 0 ? ov : totk_fs_fast_enabled();
}

static inline int totk_fs_w27_fault(void) {
    static _Atomic int gf = -1;
    int v = atomic_load_explicit(&gf, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("TOTK_KTGL_OPT_FAULT");
        v = e ? atoi(e) : 0;
        atomic_store_explicit(&gf, v, memory_order_release);
    }
    return v;
}
#else
static inline int totk_fs_fast_active(void) { return totk_fs_fast_enabled(); }
static inline int totk_fs_w27_fault(void) { return 0; }
#endif

static inline uint64_t totk_fs_hash64(const char *s, size_t n) {
    uint64_t h = 14695981039346656037ull ^ (uint64_t)n;
    size_t i = 0;
    for (; i + 8u <= n && i < 4096u; i += 8u) {
        uint64_t w = 0;
        memcpy(&w, s + i, 8u);
        h = (h ^ w) * 0xbf58476d1ce4e5b9ull;
        h ^= h >> 31;
    }
    if (i < n && i < 4096u) {
        uint64_t tail = 0;
        memcpy(&tail, s + i, n - i < 8u ? n - i : 8u);
        h = (h ^ tail) * 0x94d049bb133111ebull;
    }
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    return h;
}

typedef struct {
    _Atomic uint64_t state;
    uint64_t hash;
    uint64_t idx;
    uint32_t ov;
    uint16_t len;
    uint8_t fstate;
    _Atomic uint8_t dstate;
    char path[TOTK_FS_PATH_CAP];
} totk_fs_rom_slot;

typedef struct {
    _Atomic uint64_t epoch;
    totk_fs_rom_slot s[TOTK_FS_ROM_WAYS];
} totk_fs_rom_cache;

static inline void totk_fs_rom_bump(totk_fs_rom_cache *c) {
    atomic_fetch_add_explicit(&c->epoch, 1ull, memory_order_acq_rel);
}

static inline int totk_fs_rom_get(const totk_fs_rom_cache *c, const char *p, size_t len,
                                  uint64_t *idx_out, uint32_t *ov_out,
                                  uint8_t *fstate_out, uint8_t *dstate_out) {
    if (!totk_fs_fast_active() || !p || len >= TOTK_FS_PATH_CAP) return 0;
    uint64_t ep = atomic_load_explicit(&c->epoch, memory_order_acquire);
    if (!ep) return 0;
    uint64_t h = totk_fs_hash64(p, len);
    uint32_t pos = (uint32_t)(h & (TOTK_FS_ROM_WAYS - 1u));
    int f = totk_fs_w27_fault();
    for (uint32_t probe = 0; probe < TOTK_FS_ROM_PROBE; probe++) {
        const totk_fs_rom_slot *s = &c->s[(pos + probe) & (TOTK_FS_ROM_WAYS - 1u)];
        uint64_t st = atomic_load_explicit(&s->state, memory_order_acquire);
        if (st == 0ull) return 0;
        if ((st & 3ull) != 2ull) continue;
        if (f != 88 && (st >> 2) != ep) continue;
        int key_ok = (f == 90) ? 1 : (s->hash == h && s->len == (uint16_t)len && !memcmp(s->path, p, len));
        if (key_ok) {
            uint64_t si = s->idx;
            uint8_t fst = s->fstate;
            uint8_t dst = atomic_load_explicit(&s->dstate, memory_order_acquire);
            if (f == 87 && fst == TOTK_FS_FSTATE_FILE) si ^= 1ull;
            if (f == 89 && fst == TOTK_FS_FSTATE_NOFILE && dst == TOTK_FS_DSTATE_NODIR)
                dst = TOTK_FS_DSTATE_DIR;
            *idx_out = si;
            if (ov_out) *ov_out = s->ov;
            *fstate_out = fst;
            if (dstate_out) *dstate_out = dst;
            return 1;
        }
    }
    return 0;
}

static inline void totk_fs_rom_put(totk_fs_rom_cache *c, const char *p, size_t len,
                                   uint64_t idx, uint32_t ov, uint8_t fstate, uint8_t dstate) {
    if (!totk_fs_fast_active() || !p || len >= TOTK_FS_PATH_CAP) return;
    uint64_t ep = atomic_load_explicit(&c->epoch, memory_order_acquire);
    if (!ep) ep = 1ull;
    uint64_t h = totk_fs_hash64(p, len);
    uint32_t pos = (uint32_t)(h & (TOTK_FS_ROM_WAYS - 1u));
    uint64_t pub = (ep << 2) | 2ull;
    for (uint32_t probe = 0; probe < TOTK_FS_ROM_PROBE; probe++) {
        totk_fs_rom_slot *s = &c->s[(pos + probe) & (TOTK_FS_ROM_WAYS - 1u)];
        uint64_t st = atomic_load_explicit(&s->state, memory_order_acquire);
        if (st == pub && s->hash == h && s->len == (uint16_t)len && !memcmp(s->path, p, len)) {
            if (dstate != TOTK_FS_DSTATE_UNK)
                atomic_store_explicit(&s->dstate, dstate, memory_order_release);
            return;
        }
        if (st != 0ull) continue;
        uint64_t expected = 0ull;
        if (!atomic_compare_exchange_strong_explicit(&s->state, &expected, 1ull, memory_order_acq_rel, memory_order_relaxed))
            continue;
        s->hash = h;
        s->idx = idx;
        s->ov = ov;
        s->len = (uint16_t)len;
        s->fstate = fstate;
        atomic_store_explicit(&s->dstate, dstate, memory_order_relaxed);
        memcpy(s->path, p, len);
        s->path[len] = 0;
        atomic_store_explicit(&s->state, pub, memory_order_release);
        return;
    }
}

typedef struct {
    _Atomic uint64_t state;
    uint64_t hash;
    uint16_t len;
    int16_t slot;
    char name[TOTK_FS_MNT_NAME_CAP];
} totk_fs_mnt_slot;

typedef struct {
    _Atomic uint64_t epoch;
    totk_fs_mnt_slot s[TOTK_FS_MNT_WAYS];
} totk_fs_mnt_table;

static inline uint64_t totk_fs_mnt_begin(totk_fs_mnt_table *t) {
    return atomic_fetch_add_explicit(&t->epoch, 1ull, memory_order_acq_rel) + 1ull;
}

static inline void totk_fs_mnt_insert(totk_fs_mnt_table *t, uint64_t ep, const char *name, size_t ln, int slot_idx) {
    if (!ep || !ln || ln >= TOTK_FS_MNT_NAME_CAP || slot_idx < 0) return;
    uint64_t h = totk_fs_hash64(name, ln);
    uint32_t pos = (uint32_t)(h & (TOTK_FS_MNT_WAYS - 1u));
    uint64_t pub = (ep << 2) | 2ull;
    for (uint32_t probe = 0; probe < TOTK_FS_MNT_PROBE; probe++) {
        totk_fs_mnt_slot *s = &t->s[(pos + probe) & (TOTK_FS_MNT_WAYS - 1u)];
        uint64_t st = atomic_load_explicit(&s->state, memory_order_acquire);
        if (st == pub && s->hash == h && s->len == (uint16_t)ln && !memcmp(s->name, name, ln)) return;
        if (st != 0ull) continue;
        uint64_t expected = 0ull;
        if (!atomic_compare_exchange_strong_explicit(&s->state, &expected, 1ull, memory_order_acq_rel, memory_order_relaxed))
            continue;
        s->hash = h;
        s->len = (uint16_t)ln;
        s->slot = (int16_t)slot_idx;
        memcpy(s->name, name, ln);
        s->name[ln] = 0;
        atomic_store_explicit(&s->state, pub, memory_order_release);
        return;
    }
}

static inline int totk_fs_mnt_find(const totk_fs_mnt_table *t, const char *name, size_t ln, int *slot_out) {
    if (!totk_fs_fast_active() || !ln || ln >= TOTK_FS_MNT_NAME_CAP) return 0;
    uint64_t ep = atomic_load_explicit(&t->epoch, memory_order_acquire);
    if (!ep) return 0;
    uint64_t h = totk_fs_hash64(name, ln);
    uint32_t pos = (uint32_t)(h & (TOTK_FS_MNT_WAYS - 1u));
    int f = totk_fs_w27_fault();
    for (uint32_t probe = 0; probe < TOTK_FS_MNT_PROBE; probe++) {
        const totk_fs_mnt_slot *s = &t->s[(pos + probe) & (TOTK_FS_MNT_WAYS - 1u)];
        uint64_t st = atomic_load_explicit(&s->state, memory_order_acquire);
        if (st == 0ull) return 0;
        if ((st & 3ull) != 2ull) continue;
        if (f != 88 && (st >> 2) != ep) continue;
        if (s->hash == h && s->len == (uint16_t)ln && !memcmp(s->name, name, ln)) {
            int slot = (int)s->slot;
            *slot_out = (f == 91) ? (slot + 1) : slot;
            return 1;
        }
    }
    return 0;
}

static inline uint64_t totk_fs_fd_pack(int fd, int mode, int save) {
    if (fd <= 0) return 0ull;
    return ((uint64_t)(uint32_t)fd) | (((uint64_t)(uint32_t)mode & 0xffull) << 32) | (((uint64_t)(save ? 1u : 0u)) << 40);
}

static inline int totk_fs_fd_unpack_fd(uint64_t w) {
    int fd = (int)(uint32_t)(w & 0xffffffffull);
    return totk_fs_w27_fault() == 91 ? (fd ? fd + 1 : 1) : fd;
}

static inline int totk_fs_fd_unpack_mode(uint64_t w) {
    return (int)((w >> 32) & 0xffull);
}

static inline int totk_fs_fd_unpack_save(uint64_t w) {
    return (int)((w >> 40) & 1ull);
}
