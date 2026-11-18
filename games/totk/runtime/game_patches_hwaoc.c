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
}

void runtime_frame_step_update(uint64_t now_ns) {
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
