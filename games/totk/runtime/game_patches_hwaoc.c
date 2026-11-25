/* Age of Calamity 1.3.0 (v327680) game-code patches: KTSL2 engine/data sound-heap split and frame telemetry. */
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "duo_link.h"
#include "game_patches.h"
#include "game_version.h"
#include "mod_ips.h"
#include "android_pause.h"
#include "fs_host.h"
#include "movie_host.h"
#include "hwaoc_companion.h"
#include "ktgl_ui_patch.h"
#include <pthread.h>

enum {
    KTSL_ENGINE_OFF = 0x8d9610u,
    KTSL_ENGINE_OLD = 0xd2a01e08u, /* mov x8, #0xf00000 (15 MiB) */
    KTSL_ENGINE_NEW = 0xd2a02608u, /* mov x8, #0x1300000 (19 MiB) */
    KTSL_DATA_OFF = 0x8d9618u,
    KTSL_DATA_OLD = 0x52a11c09u,   /* mov w9, #0x8e00000 (142 MiB) */
    KTSL_DATA_NEW = 0x52a11409u,   /* mov w9, #0x8a00000 (138 MiB) */
};

static const uint8_t K_130_BUILD_ID[16] = {
    0xc3, 0xcf, 0x52, 0xbf, 0x2b, 0x05, 0xd7, 0x31,
    0x08, 0x84, 0x97, 0x1b, 0x50, 0xbc, 0xcc, 0xb8,
};

static _Atomic uint64_t g_last_report_ns;
static _Atomic uint64_t g_report_frames;

static uint32_t read_u32(const uint8_t *base, uint64_t off) {
    uint32_t v = 0;
    memcpy(&v, base + off, sizeof v);
    return v;
}

static void write_u32(uint8_t *base, uint64_t off, uint32_t word) {
    memcpy(base + off, &word, sizeof word);
}

static void patch_ktsl_heap(Module *m) {
    const char *env = getenv("TOTK_KTSL_HEAP");
    if (env && !strcmp(env, "0")) {
        fprintf(stderr, "[hwaoc] KTSL2 heap patch off (TOTK_KTSL_HEAP=0)\n");
        return;
    }
    uint8_t bid[32] = {0};
    if (!mod_ips_build_id(m, bid) || memcmp(bid, K_130_BUILD_ID, sizeof K_130_BUILD_ID)) {
        runtime_fail("Age of Calamity KTSL2 heap patch requires 1.3.0 main build ID");
    }
    if (m->text_end < KTSL_DATA_OFF + 4) {
        runtime_fail("main text extent smaller than KTSL2 site");
    }
    uint32_t w0 = read_u32(m->base, KTSL_ENGINE_OFF);
    uint32_t w1 = read_u32(m->base, KTSL_DATA_OFF);
    if (w0 != KTSL_ENGINE_OLD || w1 != KTSL_DATA_OLD) {
        fprintf(stderr, "[hwaoc] KTSL2 words mismatch: +0x%x=%08x +0x%x=%08x\n",
                KTSL_ENGINE_OFF, w0, KTSL_DATA_OFF, w1);
        runtime_fail("KTSL2 heap instructions did not match AoC 1.3.0");
    }
    write_u32(m->base, KTSL_ENGINE_OFF, KTSL_ENGINE_NEW);
    write_u32(m->base, KTSL_DATA_OFF, KTSL_DATA_NEW);
    fprintf(stderr, "[hwaoc] KTSL2 sound heap: 15+142 MiB -> 19+138 MiB (+4/-4 MiB at main+0x%x/0x%x)\n",
            KTSL_ENGINE_OFF, KTSL_DATA_OFF);
}

void runtime_game_patches(Module *m) {
    if (!m || !m->base) runtime_fail("invalid main module in runtime_game_patches");
    fprintf(stderr, "[hwaoc] %s game-code patches\n", TOTK_GAME_VERSION);
    mod_ips_load(m);
#if DUO_LINK_WEAK
    if (runtime_duo_main) runtime_duo_main(m);
#endif
    patch_ktsl_heap(m);
    const char *ktgl_env = getenv("TOTK_KTGL_TAG");
    if (ktgl_env && !strcmp(ktgl_env, "0")) {
        fprintf(stderr, "[hwaoc] KTGL UI tag off (TOTK_KTGL_TAG=0)\n");
        return;
    }
    uint8_t bid[32] = {0};
    if (mod_ips_build_id(m, bid) && !memcmp(bid, K_130_BUILD_ID, sizeof K_130_BUILD_ID)) {
        uint64_t mend = m->key_off ? m->key_off : m->size;
        if (ktgl_aoc_patch_raw(m->base, m->text_end, m->ro_off, m->data_off, mend) == 1) {
            fprintf(stderr, "[hwaoc] KTGL UI widget tagger enabled (24 hooks)\n");
        } else {
            fprintf(stderr, "[hwaoc] KTGL UI widget tagger skipped (site/slack mismatch)\n");
        }
    }
}


static pthread_mutex_t g_comp_mu = PTHREAD_MUTEX_INITIALIZER;
static KtglCompanionCtx g_comp_ctx;
static int g_comp_inited;
static uint64_t g_comp_win_t0, g_comp_win_frames, g_comp_prev_btn;
static float g_comp_fps;

static void comp_ensure_locked(void) {
    if (g_comp_inited) return;
    const char *rot = getenv("TOTK_DUO_MAP_ROTATE");
    const char *left = getenv("TOTK_DUO_LEFT_PANEL");
    int heading = (rot && !strcmp(rot, "1")) ? 1 : 0;
    int init_pg = (left && !strcmp(left, "powers")) ? 1 : 0;
    ktgl_companion_init(&g_comp_ctx, KTGL_GAME_AOC, heading, init_pg);
    g_comp_inited = 1;
}

static int comp_enabled(void) {
    const char *e = getenv("TOTK_COMPANION");
    return !(e && !strcmp(e, "0"));
}

static float comp_sample_fps(uint64_t now_ns) {
    if (!g_comp_win_t0 || now_ns <= g_comp_win_t0) {
        g_comp_win_t0 = now_ns;
        g_comp_win_frames = 1;
        return g_comp_fps;
    }
    g_comp_win_frames++;
    uint64_t dt = now_ns - g_comp_win_t0;
    if (dt >= 500000000ULL) {
        g_comp_fps = (float)((double)g_comp_win_frames * 1e9 / (double)dt);
        g_comp_win_t0 = now_ns;
        g_comp_win_frames = 0;
    }
    return g_comp_fps;
}

static void comp_present(uint64_t now_ns) {
    int32_t lx = 0, ly = 0, rx = 0, ry = 0;
    uint64_t phys = 0ULL;
    runtime_input_peek_pad(&lx, &ly, &rx, &ry, &phys);
    int loaded = runtime_module_base("main") != NULL;
    KtglHostSample s = {
        .enabled = comp_enabled(), .game_kind = KTGL_GAME_AOC, .main_loaded = loaded, .build_ok = loaded,
        .host_paused = runtime_android_game_paused(), .movie_playing = movie_host_active(),
        .save_busy = runtime_fs_save_busy(), .save_commits = runtime_fs_save_commits(),
        .now_ns = now_ns, .fps = comp_sample_fps(now_ns),
        .lx = lx, .ly = ly, .rx = rx, .ry = ry, .phys_buttons = phys,
    };
    uint64_t seq_btn = 0ULL;
    int push = 0;
    pthread_mutex_lock(&g_comp_mu);
    comp_ensure_locked();
    ktgl_companion_present(&g_comp_ctx, &s);
    seq_btn = g_comp_ctx.seq_buttons;
    if (seq_btn != g_comp_prev_btn) { g_comp_prev_btn = seq_btn; push = 1; }
    pthread_mutex_unlock(&g_comp_mu);
    if (push) runtime_input_companion_buttons(seq_btn);
}

int hwaoc_companion_state(CompanionState *out, int64_t *age_ms) {
    if (!out || !comp_enabled()) return 0;
    pthread_mutex_lock(&g_comp_mu);
    comp_ensure_locked();
    *out = g_comp_ctx.state;
    uint64_t st_ns = g_comp_ctx.state_ns;
    pthread_mutex_unlock(&g_comp_mu);
    if (out->status == COMPANION_STATE_NONE || !st_ns) return 0;
    if (age_ms) {
        uint64_t now = runtime_now_ns();
        *age_ms = (now >= st_ns) ? (int64_t)((now - st_ns) / 1000000ULL) : 0;
    }
    return 1;
}

void hwaoc_companion_page(int page) {
    pthread_mutex_lock(&g_comp_mu);
    comp_ensure_locked();
    g_comp_ctx.user_page = (int32_t)page;
    pthread_mutex_unlock(&g_comp_mu);
}

int hwaoc_companion_detail(int32_t *ints, int max_ints, float *flts, int max_flts, char *txt, int max_txt) {
    if (!comp_enabled()) return 0;
    int ok = 0;
    pthread_mutex_lock(&g_comp_mu);
    comp_ensure_locked();
    if (g_comp_ctx.state.status != COMPANION_STATE_NONE && g_comp_ctx.state_ns > 0ULL) {
        ok = ktgl_companion_pack_detail(&g_comp_ctx, ints, max_ints, flts, max_flts, txt, max_txt);
    }
    pthread_mutex_unlock(&g_comp_mu);
    return ok;
}

int hwaoc_companion_fire(int action_id) {
    if (!comp_enabled()) return 0;
    uint64_t now = runtime_now_ns(), seq_btn = 0ULL;
    int ok = 0;
    pthread_mutex_lock(&g_comp_mu);
    comp_ensure_locked();
    ok = ktgl_companion_fire(&g_comp_ctx, action_id, now);
    seq_btn = g_comp_ctx.seq_buttons;
    if (ok) g_comp_prev_btn = seq_btn;
    pthread_mutex_unlock(&g_comp_mu);
    if (ok) runtime_input_companion_buttons(seq_btn);
    return ok;
}

void hwaoc_companion_abort(void) {
    pthread_mutex_lock(&g_comp_mu);
    comp_ensure_locked();
    ktgl_companion_abort(&g_comp_ctx);
    g_comp_prev_btn = 0ULL;
    pthread_mutex_unlock(&g_comp_mu);
    runtime_input_companion_buttons(0ULL);
}

int hwaoc_companion_pin(int op, int slot, const char *label) {
    if (!comp_enabled()) return -1;
    pthread_mutex_lock(&g_comp_mu);
    comp_ensure_locked();
    int rc = ktgl_companion_pin(&g_comp_ctx, op, slot, label);
    pthread_mutex_unlock(&g_comp_mu);
    return rc;
}

void runtime_frame_step_update(uint64_t now_ns) {
    comp_present(now_ns);
#if DUO_LINK_WEAK
    if (runtime_duo_frame) runtime_duo_frame();
#endif
    uint64_t prev = atomic_load_explicit(&g_last_report_ns, memory_order_relaxed);
    if (!prev) {
        atomic_compare_exchange_strong(&g_last_report_ns, &prev, now_ns);
        return;
    }
    atomic_fetch_add_explicit(&g_report_frames, 1, memory_order_relaxed);
    uint64_t dt = now_ns - prev;
    if (dt >= 30000000000ull && atomic_compare_exchange_strong(&g_last_report_ns, &prev, now_ns)) {
        uint64_t f = atomic_exchange_explicit(&g_report_frames, 0, memory_order_relaxed);
        double sec = (double)dt / 1e9;
        fprintf(stderr, "[hwaoc] telemetry: %llu frames in %.2f s (%.2f fps)\n",
                (unsigned long long)f, sec, sec > 0 ? (double)f / sec : 0.0);
    }
}

void runtime_frame_step_pin(double ms) {
    fprintf(stderr, "[hwaoc] frame-step pin %.3f ms not applicable (shipped 30 fps simulation)\n", ms);
}

int runtime_output_res(unsigned *w, unsigned *h) { (void)w; (void)h; return 0; }
int runtime_render_res(unsigned *w, unsigned *h) { (void)w; (void)h; return 0; }
uint64_t runtime_output_res_extra_bytes(void) { return 0; }
int runtime_output_res_toggle(void) { return 0; }
int runtime_output_mode_cycle(void) { return 0; }
float runtime_aspect_now(void) { return 0; }
int runtime_aspect_toggle(void) { return 0; }
