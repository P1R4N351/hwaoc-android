/* Wave 23 (Opt 25): Lock-free input, gamepad seqlock, motion ring & idle-zero rumble filter. */
#pragma once
#include "runtime.h"
#include <assert.h>
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    TOTK_HID_FAST_RETRIES = 8,
    TOTK_PAD_FAST_WORDS = 8,
    TOTK_MOTION_FAST_RING = 32,
    TOTK_MOTION_FAST_WORDS = (sizeof(RuntimeMotionState) + 3u) / 4u,
    TOTK_VIB_FAST_WORDS = 4
};

#if defined(W6_TEST_HOOKS) || defined(TOTK_W23_TEST_HOOKS)
static _Atomic int g_totk_hid_fast_ovr = -1;
static inline void totk_hid_fast_reset_for_test(int v) {
    atomic_store_explicit(&g_totk_hid_fast_ovr, v, memory_order_release);
}
static inline int totk_hid_w23_fault(void) {
    const char *e = getenv("TOTK_KTGL_OPT_FAULT");
    return e ? atoi(e) : 0;
}
#else
static inline int totk_hid_w23_fault(void) { return 0; }
#endif

static inline int totk_hid_fast_on(void) {
#if defined(W6_TEST_HOOKS) || defined(TOTK_W23_TEST_HOOKS)
    int o = atomic_load_explicit(&g_totk_hid_fast_ovr, memory_order_acquire);
    if (o >= 0) return o != 0;
#endif
    static _Atomic int cached = -1;
    int v = atomic_load_explicit(&cached, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("TOTK_HID_FAST");
        v = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&cached, v, memory_order_release);
    }
    return v;
}

/* ---------- 1. Keyboard tracker with refcounted dual keycodes (O(1) poll when idle) ---------- */
typedef struct {
    uint8_t keys[128];
    uint8_t btn_cnt[16];
    uint64_t held_mask;
    uint64_t pending;
    uint32_t active_keys;
    _Atomic uint32_t nonzero;
} totk_kb_tracker;

static inline void totk_kb_key_update(
    totk_kb_tracker *k, unsigned code, int down, uint64_t bit_mask) {
    assert(k != NULL && code < 128u);
    uint8_t prev = k->keys[code];
    uint8_t next = (uint8_t)(down != 0);
    if (prev == next) return;
    k->keys[code] = next;
    int bit = bit_mask ? __builtin_ctzll(bit_mask) : -1;
    if (next) {
        k->active_keys++;
        if (bit >= 0 && bit < 16) {
            if (!k->btn_cnt[bit]) k->held_mask |= bit_mask;
            if (k->btn_cnt[bit] < 255u) k->btn_cnt[bit]++;
            k->pending |= bit_mask;
        }
    } else {
        if (k->active_keys > 0) k->active_keys--;
        if (bit >= 0 && bit < 16) {
            if (k->btn_cnt[bit] > 0) k->btn_cnt[bit]--;
            if (totk_hid_w23_fault() == 67 || !k->btn_cnt[bit]) k->held_mask &= ~bit_mask;
        }
    }
    atomic_store_explicit(&k->nonzero, (k->active_keys || k->pending) ? 1u : 0u, memory_order_release);
}

static inline void totk_kb_clear(totk_kb_tracker *k) {
    assert(k != NULL);
    memset(k->keys, 0, sizeof k->keys);
    memset(k->btn_cnt, 0, sizeof k->btn_cnt);
    k->held_mask = 0;
    k->pending = 0;
    k->active_keys = 0;
    atomic_store_explicit(&k->nonzero, 0u, memory_order_release);
}

static inline void totk_kb_consume(
    totk_kb_tracker *k, uint64_t *buttons, int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry) {
    assert(k != NULL && buttons != NULL && lx && ly && rx && ry);
    *buttons |= k->held_mask | k->pending;
    k->pending = 0;
    *lx = 32767 * ((int)k->keys[2] - (int)k->keys[0]);
    *ly = 32767 * ((int)k->keys[13] - (int)k->keys[1]);
    *rx = 32767 * ((int)k->keys[124] - (int)k->keys[123]);
    *ry = 32767 * ((int)k->keys[126] - (int)k->keys[125]);
    atomic_store_explicit(&k->nonzero, k->active_keys ? 1u : 0u, memory_order_release);
}

/* ---------- 2. Gamepad seqlock snapshot ---------- */
typedef struct {
    uint64_t buttons;
    int32_t axes[4];
    int32_t connected;
    uint32_t fast_ok;
} totk_pad_snapshot;

_Static_assert(sizeof(totk_pad_snapshot) == TOTK_PAD_FAST_WORDS * 4, "pad snap words");

typedef struct {
    _Atomic uint32_t seq;
    _Atomic uint32_t words[TOTK_PAD_FAST_WORDS];
} totk_pad_fast_slot;

static inline void totk_pad_fast_write(
    totk_pad_fast_slot *s, uint64_t buttons, const int32_t axes[4], int connected, int fast_ok) {
    assert(s != NULL && axes != NULL);
    if (totk_hid_w23_fault() == 68) fast_ok = 1;
    totk_pad_snapshot snap = {
        .buttons = buttons,
        .axes = {axes[0], axes[1], axes[2], axes[3]},
        .connected = connected,
        .fast_ok = (uint32_t)(fast_ok != 0)
    };
    uint32_t tmp[TOTK_PAD_FAST_WORDS];
    memcpy(tmp, &snap, sizeof snap);
    uint32_t q = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_store_explicit(&s->seq, q + 1u, memory_order_release);
    atomic_thread_fence(memory_order_release);
    for (int i = 0; i < TOTK_PAD_FAST_WORDS; i++)
        atomic_store_explicit(&s->words[i], tmp[i], memory_order_relaxed);
    atomic_store_explicit(&s->seq, q + 2u, memory_order_release);
}

static inline int totk_pad_fast_read(const totk_pad_fast_slot *s, RuntimeGamepadState *out) {
    assert(s != NULL && out != NULL);
    for (int t = 0; t < TOTK_HID_FAST_RETRIES; t++) {
        uint32_t s1 = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (!s1 || (s1 & 1u)) continue;
        uint32_t tmp[TOTK_PAD_FAST_WORDS];
        for (int i = 0; i < TOTK_PAD_FAST_WORDS; i++)
            tmp[i] = atomic_load_explicit(&s->words[i], memory_order_relaxed);
        atomic_thread_fence(memory_order_acquire);
        uint32_t s2 = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (s1 != s2) continue;
        totk_pad_snapshot snap;
        memcpy(&snap, tmp, sizeof snap);
        if (!snap.fast_ok) return 0;
        out->buttons = snap.buttons;
        out->lx = snap.axes[0]; out->ly = snap.axes[1];
        out->rx = snap.axes[2]; out->ry = snap.axes[3];
        out->connected = snap.connected;
        return 1;
    }
    return 0;
}

/* ---------- 3. Motion fusion seqlock ring ---------- */
typedef struct {
    _Atomic int live;
    _Atomic uint32_t seq;
    _Atomic uint64_t last_ns;
    _Atomic uint64_t produced;
    _Atomic uint32_t words[TOTK_MOTION_FAST_RING][TOTK_MOTION_FAST_WORDS];
} totk_motion_fast_ring;

_Static_assert(sizeof(RuntimeMotionState) <= TOTK_MOTION_FAST_WORDS * 4, "motion words");

static inline void totk_motion_fast_set_live(totk_motion_fast_ring *r, int on) {
    assert(r != NULL);
    atomic_store_explicit(&r->live, on != 0, memory_order_release);
}

static inline void totk_motion_fast_publish(
    totk_motion_fast_ring *r, const RuntimeMotionState *s, uint64_t produced_after, uint64_t now_t) {
    assert(r != NULL && s != NULL && produced_after > 0);
    uint64_t slot = (produced_after - 1u) % TOTK_MOTION_FAST_RING;
    uint32_t tmp[TOTK_MOTION_FAST_WORDS];
    memset(tmp, 0, sizeof tmp);
    memcpy(tmp, s, sizeof *s);
    uint32_t q = atomic_load_explicit(&r->seq, memory_order_relaxed);
    atomic_store_explicit(&r->seq, q + 1u, memory_order_release);
    atomic_thread_fence(memory_order_release);
    atomic_store_explicit(&r->live, 1, memory_order_relaxed);
    atomic_store_explicit(&r->last_ns, now_t, memory_order_relaxed);
    atomic_store_explicit(&r->produced, produced_after, memory_order_relaxed);
    for (size_t i = 0; i < TOTK_MOTION_FAST_WORDS; i++)
        atomic_store_explicit(&r->words[slot][i], tmp[i], memory_order_relaxed);
    atomic_store_explicit(&r->seq, q + 2u, memory_order_release);
}

static inline int totk_motion_fast_read(
    const totk_motion_fast_ring *r, uint64_t (*now_fn)(void), RuntimeMotionState *out, int max) {
    assert(r != NULL && now_fn != NULL && out != NULL);
    if (max <= 0) return 0;
    if (!atomic_load_explicit(&r->live, memory_order_acquire)) return 0;
    uint64_t now_t = now_fn();
    int fault = totk_hid_w23_fault();
    for (int t = 0; t < TOTK_HID_FAST_RETRIES; t++) {
        uint32_t s1 = atomic_load_explicit(&r->seq, memory_order_acquire);
        if (!s1 || (s1 & 1u)) continue;
        int lv = atomic_load_explicit(&r->live, memory_order_relaxed);
        uint64_t last_t = atomic_load_explicit(&r->last_ns, memory_order_relaxed);
        uint64_t prod = atomic_load_explicit(&r->produced, memory_order_relaxed);
        if (!lv || !prod) return 0;
        if (fault != 69 && now_t >= last_t && now_t - last_t >= UINT64_C(200000000)) return 0;
        uint64_t have = prod < TOTK_MOTION_FAST_RING ? prod : TOTK_MOTION_FAST_RING;
        int n = (int)(have < (uint64_t)max ? have : (uint64_t)max);
        for (int i = 0; i < n; i++) {
            uint64_t idx = (prod - 1u - (uint64_t)i) % TOTK_MOTION_FAST_RING;
            uint32_t tmp[TOTK_MOTION_FAST_WORDS];
            for (size_t w = 0; w < TOTK_MOTION_FAST_WORDS; w++)
                tmp[w] = atomic_load_explicit(&r->words[idx][w], memory_order_relaxed);
            memcpy(&out[i], tmp, sizeof out[i]);
        }
        atomic_thread_fence(memory_order_acquire);
        uint32_t s2 = atomic_load_explicit(&r->seq, memory_order_acquire);
        if (s1 == s2) return n;
    }
    return -1;
}

/* ---------- 4. HID vibration shadow seqlock & idle-zero rumble elision ---------- */
typedef struct {
    _Atomic uint32_t seq[2];
    _Atomic uint32_t words[2][TOTK_VIB_FAST_WORDS];
    _Atomic int silent[2];
} totk_vib_fast_state;

static inline void totk_vib_fast_write(
    totk_vib_fast_state *v, int side, float al, float fl, float ah, float fh) {
    assert(v != NULL && (side == 0 || side == 1));
    float f[4] = {al, fl, ah, fh};
    uint32_t tmp[TOTK_VIB_FAST_WORDS];
    memcpy(tmp, f, sizeof tmp);
    uint32_t q = atomic_load_explicit(&v->seq[side], memory_order_relaxed);
    atomic_store_explicit(&v->seq[side], q + 1u, memory_order_release);
    atomic_thread_fence(memory_order_release);
    for (int i = 0; i < TOTK_VIB_FAST_WORDS; i++)
        atomic_store_explicit(&v->words[side][i], tmp[i], memory_order_relaxed);
    atomic_store_explicit(&v->seq[side], q + 2u, memory_order_release);
}

static inline int totk_vib_fast_read(
    const totk_vib_fast_state *v, int side, float *al, float *fl, float *ah, float *fh) {
    assert(v != NULL && (side == 0 || side == 1) && al && fl && ah && fh);
    for (int t = 0; t < TOTK_HID_FAST_RETRIES; t++) {
        uint32_t s1 = atomic_load_explicit(&v->seq[side], memory_order_acquire);
        if (s1 & 1u) continue;
        if (!s1) {
            *al = 0.0f; *fl = 160.0f; *ah = 0.0f; *fh = 320.0f;
            atomic_thread_fence(memory_order_acquire);
            if (!atomic_load_explicit(&v->seq[side], memory_order_acquire)) return 0;
            continue;
        }
        uint32_t tmp[TOTK_VIB_FAST_WORDS];
        for (int i = 0; i < TOTK_VIB_FAST_WORDS; i++)
            tmp[i] = atomic_load_explicit(&v->words[side][i], memory_order_relaxed);
        atomic_thread_fence(memory_order_acquire);
        uint32_t s2 = atomic_load_explicit(&v->seq[side], memory_order_acquire);
        if (s1 == s2) {
            float f[4];
            memcpy(f, tmp, sizeof f);
            *al = f[0]; *fl = f[1]; *ah = f[2]; *fh = f[3];
            return 0;
        }
    }
    return -1;
}

static inline int totk_vib_fast_should_send(
    totk_vib_fast_state *v, int side, float eff_al, float eff_ah) {
    assert(v != NULL && (side == 0 || side == 1));
    if (eff_al > 0.0f || eff_ah > 0.0f) {
        if (totk_hid_w23_fault() == 70 && atomic_load_explicit(&v->silent[side], memory_order_relaxed) == 2)
            return 0;
        atomic_store_explicit(&v->silent[side], totk_hid_w23_fault() == 70 ? 2 : 0, memory_order_release);
        return 1;
    }
    if (atomic_load_explicit(&v->silent[side], memory_order_acquire) == 1) return 0;
    atomic_store_explicit(&v->silent[side], 1, memory_order_release);
    return 1;
}

static inline void totk_vib_fast_reset_silent(totk_vib_fast_state *v, int state) {
    assert(v != NULL);
    atomic_store_explicit(&v->silent[0], state, memory_order_release);
    atomic_store_explicit(&v->silent[1], state, memory_order_release);
}
