/* Clean-room host unit tests for games/totk/runtime/ktgl_companion.h (AoC companion). */
#include "ktgl_companion.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static KtglHostSample make_sample(uint64_t now_ns) {
    KtglHostSample s = {0};
    s.enabled = 1;
    s.game_kind = KTGL_GAME_AOC;
    s.main_loaded = 1;
    s.build_ok = 1;
    s.now_ns = now_ns;
    s.fps = 30.0f;
    return s;
}

int main(void) {
    KtglCompanionCtx c;
    ktgl_companion_init(&c, KTGL_GAME_AOC, 1, 0);
    uint64_t t = 1000000000ULL;
    for (int i = 0; i < 6; i++) {
        KtglHostSample s = make_sample(t + (uint64_t)i * 60000000ULL);
        s.ly = 32767;
        s.save_commits = (i >= 4) ? 1ULL : 0ULL;
        ktgl_companion_present(&c, &s);
    }
    assert(c.state.status == COMPANION_STATE_READY && ktgl_companion_write_safe(&c));
    assert(c.pos_z < -1.5f && c.trail.count >= 2U);
    assert(ktgl_companion_pin(&c, 0, -1, "Keep") == 0);

    t += 400000000ULL;
    assert(ktgl_companion_fire(&c, KTGL_ACT_R_BASE, t));
    assert(c.seq_buttons == KTGL_BTN_R);
    KtglHostSample step = make_sample(t + 90000000ULL);
    ktgl_companion_present(&c, &step);
    assert(c.seq_buttons == (KTGL_BTN_R | KTGL_BTN_Y));
    step = make_sample(t + 180000000ULL);
    ktgl_companion_present(&c, &step);
    assert(c.seq_buttons == 0ULL);

    t += 300000000ULL;
    assert(ktgl_companion_fire(&c, KTGL_ACT_COMBO_1 + 2, t));
    KtglHostSample mv = make_sample(t + 20000000ULL);
    mv.movie_playing = 1;
    ktgl_companion_present(&c, &mv);
    assert(!ktgl_companion_write_safe(&c) && !c.seq.active && c.seq_buttons == 0ULL);
    assert(ktgl_companion_fire(&c, KTGL_ACT_PARTY_0, t + 40000000ULL) == 0);

    int32_t ints[KTGL_DETAIL_INTS] = {0};
    float flts[KTGL_DETAIL_FLOATS] = {0};
    char txt[512] = "";
    assert(ktgl_companion_pack_detail(&c, ints, KTGL_DETAIL_INTS, flts, KTGL_DETAIL_FLOATS, txt, sizeof(txt)));
    assert(ints[3] == 1 && ints[6] == KTGL_GAME_AOC && ints[13] == 1);
    printf("test_ktgl_companion: PASS\n");
    return 0;
}
