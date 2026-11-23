package com.aure.totk.companion;

import android.content.Context;

import com.aure.totk.setup.GameBuild;

/** What the second screen shows, and whether there is anything to show, by the game's companion variable
 *  (games/games.toml companion, GameBuild.COMPANION):
 *  - "map": TotK Native's own map and inventory (CompanionView, MIT, drawn from the art the setup generated);
 *  - "warriors": the clean-room MIT KTGL procedural Canvas companion (WarriorsCompanionView, Age of Calamity);
 *  - "duo": the game's Eden Duo page drawn by the native companion host (libduo_host.so, GPL-3.0).
 *  All three sit behind CompanionScreen; PresentationController knows nothing else of them. */
public final class CompanionContent {
    private CompanionContent() { }
    static final boolean DUO = GameBuild.COMPANION.equals("duo");
    static final boolean WARRIORS = GameBuild.COMPANION.equals("warriors");
    /** Whether this game starts without its tablet (the stick LEDs then have a look for that). */
    public static final boolean TABLET_CAN_BE_MISSING = GameBuild.TABLET_CAN_BE_MISSING;
    /** Call after NativeActivity.onCreate (the runtime options are read there). */
    public static boolean available(Context context) {
        if (!CompanionNative.isEnabled()) return false;
        if (WARRIORS) return true;
        if (!DUO) return AssetStore.available(context);
        if (!DuoSurfaceView.available()) return false;
        DuoSurfaceView.prepare(context);
        return true;
    }
    static CompanionScreen create(Context context, DataSource source, int initialPage) {
        CompanionScreen screen = DUO ? new DuoSurfaceView(context)
                : WARRIORS ? new WarriorsCompanionView(context, source)
                : new CompanionView(context, source);
        screen.setPage(initialPage);
        return screen;
    }
}
