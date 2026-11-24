// SPDX-License-Identifier: MIT
package com.aure.totk.companion;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.os.BatteryManager;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.View;
import com.aure.totk.BifrostBridge;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.nio.ByteBuffer;

/**
 * Interactive Canvas CompanionScreen for Hyrule Warriors: Age of Calamity.
 * Mirrors the game's offscreen Vulkan minimap RT onto Display 4, harvests target-game
 * background/emblem art at runtime, streams dev telemetry on STATUS, and syncs Bifrost stick LEDs.
 */
public final class WarriorsCompanionView extends View implements CompanionScreen {
    private static final long POLL_MS = 50L;
    private static final long DEV_STAT_MS = 1000L;
    private static final int MINIMAP_MAX_BYTES = 768 * 768 * 4;
    private static final int OBJ_MAX_BYTES = 768 * 224 * 4, UI_TEX_BYTES = 512 * 512 * 4;
    private static final String PREFS = "warriors_companion";

    private final DataSource source;
    private final WarriorsCompanionModel model = new WarriorsCompanionModel();
    private final WarriorsCompanionDraw draw = new WarriorsCompanionDraw();
    private final WarriorsAssetHarvester art = new WarriorsAssetHarvester();
    private final int[] ints = new int[WarriorsCompanionModel.DETAIL_INTS];
    private final float[] flts = new float[WarriorsCompanionModel.DETAIL_FLOATS];
    private final byte[] minimapBytes = new byte[MINIMAP_MAX_BYTES];
    private final int[] minimapMeta = new int[5];
    private final byte[] objBytes = new byte[OBJ_MAX_BYTES];
    private final int[] objMeta = new int[4];
    private final byte[] uiBytes = new byte[UI_TEX_BYTES];
    private final int[] uiMeta = new int[4];
    private Bitmap minimapBmp, objBmp;
    private int uiTexTick, uiTexTries;
    private long lastStateKey = -1L, lastDevMs;
    private int priorOverlayMode = -1;
    private boolean attached;

    private final Runnable pollTask = new Runnable() {
        @Override public void run() {
            if (!attached) return;
            long now = SystemClock.uptimeMillis();
            String text = source != null ? source.warriorsDetail(ints, flts) : null;
            if (text != null) model.update(ints, flts, text, now);
            else model.updateOrbit(now);
            model.stickLights = BifrostBridge.enabled;
            model.stickSync = BifrostBridge.syncScreenBrightness;
            model.screenBrightnessPct = BifrostBridge.lastScreenPct;
            pollNativeMinimap();
            if ((now - lastDevMs) >= DEV_STAT_MS) {
                lastDevMs = now;
                updateLocalDevStats();
            }
            syncArtFlags();
            invalidate();
            postDelayed(this, POLL_MS);
        }
    };

    public WarriorsCompanionView(Context context, DataSource source) {
        super(context);
        this.source = source;
        this.model.gameKind = WarriorsCompanionModel.GAME_AOC;
        SharedPreferences sp = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        this.model.devOnStatus = sp.getBoolean("dev_on_status", true);
        boolean lights = sp.getBoolean("stick_lights", true);
        this.model.stickLights = lights;
        BifrostBridge.setEnabled(lights);
        boolean sync = sp.getBoolean("stick_screen_sync", true);
        this.model.stickSync = sync;
        BifrostBridge.syncScreenBrightness = sync;
        this.art.loadCached(context);
        syncArtFlags();
        triggerArtRescan();
        setBackgroundColor(0xFF000000);
        setFocusable(false);
    }

    private void pollNativeMinimap() {
        if (model.inGame && !model.moviePlaying && !model.hostPaused) {
            CompanionNative.warriorsMinimapEnable(true);
            int got = CompanionNative.warriorsMinimap(minimapBytes, minimapMeta);
            int rw = minimapMeta[0], rh = minimapMeta[1];
            model.minimapW = rw;
            model.minimapH = rh;
            model.minimapCandCount = minimapMeta[3];
            model.minimapCandIdx = minimapMeta[4];
            if (got == 1 && rw > 0 && rw <= 768 && rh > 0 && rh <= 768 && rw * rh * 4 <= minimapBytes.length) {
                if (minimapBmp == null || minimapBmp.getWidth() != rw || minimapBmp.getHeight() != rh) {
                    minimapBmp = Bitmap.createBitmap(rw, rh, Bitmap.Config.ARGB_8888);
                }
                minimapBmp.copyPixelsFromBuffer(ByteBuffer.wrap(minimapBytes, 0, rw * rh * 4));
                art.setMinimap(minimapBmp);
            }
            pollObjectiveAndUiTex();
        } else {
            CompanionNative.warriorsMinimapEnable(false);
            art.setObjective(null);
        }
    }

    private void pollObjectiveAndUiTex() {
        int got = CompanionNative.warriorsObjective(objBytes, objMeta);
        int ow = objMeta[0], oh = objMeta[1], active = objMeta[3];
        model.objectiveW = ow;
        model.objectiveH = oh;
        if (active == 0) {
            art.setObjective(null);
        } else if (got == 1 && ow > 0 && ow <= 768 && oh > 0 && oh <= 224 && ow * oh * 4 <= objBytes.length) {
            if (objBmp == null || objBmp.getWidth() != ow || objBmp.getHeight() != oh) {
                objBmp = Bitmap.createBitmap(ow, oh, Bitmap.Config.ARGB_8888);
            }
            objBmp.copyPixelsFromBuffer(ByteBuffer.wrap(objBytes, 0, ow * oh * 4));
            art.setObjective(objBmp);
        }
        if (art.purahTex() == null && !art.isSuppressed() && uiTexTries < 8) {
            boolean req = (uiTexTick % 30) == 0 && uiMeta[3] == 0;
            uiTexTick++;
            if (CompanionNative.warriorsUiTex(uiBytes, uiMeta, req) == 1) {
                uiTexTries++;
                art.ingestGpuUiTex(getContext(), uiBytes, uiMeta[0], uiMeta[1], model.gameKind);
            }
        }
    }

    private void syncArtFlags() {
        model.hasBackdrop = art.backdrop() != null;
        model.hasEmblem = art.emblem() != null;
        model.hasLiveMinimap = art.minimap() != null;
        model.hasLiveObjective = art.objective() != null;
        model.hasPurahTex = art.purahTex() != null;
        model.assetSource = art.source();
    }

    private void updateLocalDevStats() {
        model.devBuildTag = "AoC Native  (01002B00111A2000)";
        long rssMb = readRssMb();
        int batPct = readBatteryPct();
        StringBuilder sb = new StringBuilder();
        if (rssMb > 0) sb.append("MEM  ").append(rssMb).append(" MiB RSS\n");
        if (batPct >= 0) sb.append("BAT  ").append(batPct).append("%\n");
        if (BifrostBridge.lastScreenPct >= 0) {
            sb.append("SCR  ").append(BifrostBridge.lastScreenPct).append("% -> STICKS\n");
        }
        sb.append("MAP  ").append(model.minimapLabel()).append(" (").append(model.rtCandLabel()).append(")");
        model.devOverlayText = sb.toString();
    }

    private static long readRssMb() {
        try (BufferedReader br = new BufferedReader(new FileReader("/proc/self/statm"))) {
            String line = br.readLine();
            if (line == null) return -1L;
            String[] parts = line.trim().split("\\s+");
            if (parts.length < 2) return -1L;
            long pages = Long.parseLong(parts[1]);
            return (pages * 4096L) / (1024L * 1024L);
        } catch (IOException | NumberFormatException e) {
            return -1L;
        }
    }

    private int readBatteryPct() {
        try {
            BatteryManager bm = (BatteryManager) getContext().getSystemService(Context.BATTERY_SERVICE);
            return bm != null ? bm.getIntProperty(BatteryManager.BATTERY_PROPERTY_CAPACITY) : -1;
        } catch (RuntimeException e) {
            return -1;
        }
    }

    @Override public View view() { return this; }

    @Override public void setPage(int page) {
        int clamped = Math.max(0, Math.min(WarriorsCompanionModel.TAB_COUNT - 1, page));
        if (clamped != model.activeTab()) {
            model.selectTab(clamped);
            CompanionNative.warriorsMinimapEnable(model.inGame && !model.moviePlaying && !model.hostPaused);
            invalidate();
        }
    }

    @Override public int page() { return model.activeTab(); }

    @Override public void onGameState(CompanionState state) {
        CompanionState s = state == null ? new CompanionState() : state;
        long key = s.key();
        if (key == lastStateKey) return;
        lastStateKey = key;
        model.status = s.status;
        model.inGame = s.inGame;
        model.hostPaused = s.menuOpen;
        model.updateOrbit(SystemClock.uptimeMillis());
        invalidate();
    }

    @Override protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        attached = true;
        priorOverlayMode = CompanionNative.setOverlayMode(-1);
        if (model.devOnStatus && priorOverlayMode > 0) CompanionNative.setOverlayMode(0);
        CompanionNative.warriorsMinimapEnable(model.activeTab() == WarriorsCompanionModel.TAB_TACTICAL);
        if (source != null && source.enabled()) {
            removeCallbacks(pollTask);
            postDelayed(pollTask, POLL_MS);
        }
    }

    @Override protected void onDetachedFromWindow() {
        attached = false;
        removeCallbacks(pollTask);
        CompanionNative.warriorsMinimapEnable(false);
        art.setMinimap(null);
        art.setObjective(null);
        minimapBmp = null;
        objBmp = null;
        if (model.devOnStatus && priorOverlayMode > 0) CompanionNative.setOverlayMode(priorOverlayMode);
        if (source != null) source.warriorsAbort();
        super.onDetachedFromWindow();
    }

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        draw.drawAll(canvas, model, art.backdrop(), art.emblem(), art.minimap(),
                art.objective(), art.purahTex(), getWidth(), getHeight(), SystemClock.uptimeMillis());
    }

    @Override public boolean performClick() { return super.performClick(); }

    @Override public boolean onTouchEvent(MotionEvent ev) {
        if (ev.getActionMasked() != MotionEvent.ACTION_DOWN) return true;
        int w = getWidth(), h = getHeight();
        if (w <= 0 || h <= 0) return true;
        float sc = Math.min(w / WarriorsCompanionDraw.LOGIC_W, h / WarriorsCompanionDraw.LOGIC_H);
        float ox = (w - WarriorsCompanionDraw.LOGIC_W * sc) * 0.5f + model.orbitDx();
        float oy = (h - WarriorsCompanionDraw.LOGIC_H * sc) * 0.5f + model.orbitDy();
        float lx = (ev.getX() - ox) / sc, ly = (ev.getY() - oy) / sc;
        if (lx < 0f || ly < 0f || lx > WarriorsCompanionDraw.LOGIC_W || ly > WarriorsCompanionDraw.LOGIC_H) {
            return true;
        }
        if (handleHeaderTap(lx, ly)) { performClick(); invalidate(); return true; }
        if (model.touchBlocked()) return true;
        if (dispatchTabTap(lx, ly)) { performClick(); invalidate(); }
        return true;
    }

    private boolean handleHeaderTap(float lx, float ly) {
        if (ly > 92f) return false;
        for (int i = 0; i < WarriorsCompanionModel.TAB_COUNT; i++) {
            float left = 24f + i * 302f;
            if (lx >= left && lx <= left + 286f) {
                model.selectTab(i);
                CompanionNative.warriorsMinimapEnable(i == WarriorsCompanionModel.TAB_TACTICAL);
                if (source != null) source.page(i);
                return true;
            }
        }
        return false;
    }

    private boolean dispatchTabTap(float lx, float ly) {
        int tab = model.activeTab();
        if (tab == WarriorsCompanionModel.TAB_TACTICAL) return handleMapTap(lx, ly);
        if (tab == WarriorsCompanionModel.TAB_COMMAND) return handleCommandTap(lx, ly);
        if (tab == WarriorsCompanionModel.TAB_GRIMOIRE) return handleGrimoireTap(lx, ly);
        return handleStatusTap(lx, ly);
    }

    private void cycleRt() {
        model.minimapCandIdx = CompanionNative.warriorsMinimapCycle();
        minimapMeta[2] = 0;
        pollNativeMinimap();
    }

    private boolean handleMapTap(float lx, float ly) {
        if (lx >= 24f && lx <= 748f && ly >= 108f && ly <= 1048f) { cycleRt(); return true; }
        if (lx < 784f || lx > 1200f) return false;
        if (ly >= 404f && ly <= 496f && source != null) { source.warriorsFire(WarriorsCompanionModel.ACT_LOCKON); return true; }
        if (ly >= 512f && ly <= 604f) { cycleRt(); return true; }
        if (ly >= 620f && ly <= 716f && source != null) {
            source.warriorsFire(lx <= 992f ? (WarriorsCompanionModel.ACT_R_BASE + 1) : WarriorsCompanionModel.ACT_SPECIAL);
            return true;
        }
        if (ly >= 740f && ly <= 828f) { triggerArtRescan(); return true; }
        return false;
    }

    private boolean handleCommandTap(float lx, float ly) {
        if (source == null) return false;
        if (ly >= 172f && ly <= 620f) {
            boolean rSide = lx <= 610f;
            float baseL = rSide ? 24f : 630f;
            float bw = (586f - 56f) * 0.5f;
            for (int i = 0; i < 4; i++) {
                float bl = baseL + 20f + (i % 2) * (bw + 16f);
                float bt = 172f + (i / 2) * (208f + 18f);
                if (lx >= bl && lx <= bl + bw && ly >= bt && ly <= bt + 208f) {
                    source.warriorsFire((rSide ? WarriorsCompanionModel.ACT_R_BASE : WarriorsCompanionModel.ACT_L_BASE) + i);
                    return true;
                }
            }
        }
        if (ly >= 700f && ly <= 808f) {
            for (int i = 0; i < 4; i++) {
                float bl = 46f + i * 290f;
                if (lx >= bl && lx <= bl + 274f) {
                    source.warriorsFire(WarriorsCompanionModel.ACT_PARTY_BASE + i);
                    return true;
                }
            }
        }
        if (ly >= 880f && ly <= 1018f) {
            if (lx >= 46f && lx <= 324f) { source.warriorsFire(WarriorsCompanionModel.ACT_LOCKON); return true; }
            if (lx >= 340f && lx <= 618f) { source.warriorsFire(WarriorsCompanionModel.ACT_SPECIAL); return true; }
            if (lx >= 634f && lx <= 912f) { source.warriorsFire(WarriorsCompanionModel.ACT_DODGE); return true; }
            if (lx >= 928f && lx <= 1194f) { source.warriorsAbort(); return true; }
        }
        return false;
    }

    private boolean handleGrimoireTap(float lx, float ly) {
        if (ly >= 120f && ly <= 178f) {
            if (lx >= 680f && lx <= 990f) { model.toggleMacroMode(); return true; }
            if (lx >= 1006f && lx <= 1198f && source != null) { source.warriorsAbort(); return true; }
        }
        for (int i = 0; i < 6; i++) {
            float bl = 24f + (i % 3) * 402f, bt = 208f + (i / 3) * 422f;
            if (lx >= bl && lx <= bl + 388f && ly >= bt && ly <= bt + 400f) {
                if (model.macroMode && source != null) source.warriorsFire(WarriorsCompanionModel.ACT_COMBO_BASE + i);
                return true;
            }
        }
        return false;
    }

    private boolean handleStatusTap(float lx, float ly) {
        if (lx < 796f || lx > 1194f) return false;
        SharedPreferences sp = getContext().getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        if (ly >= 170f && ly <= 264f) {
            model.devOnStatus = !model.devOnStatus;
            sp.edit().putBoolean("dev_on_status", model.devOnStatus).apply();
            if (model.devOnStatus) {
                int cur = CompanionNative.setOverlayMode(-1);
                if (cur > 0) priorOverlayMode = cur;
                CompanionNative.setOverlayMode(0);
            } else {
                CompanionNative.setOverlayMode(priorOverlayMode > 0 ? priorOverlayMode : 2);
            }
            return true;
        }
        if (ly >= 280f && ly <= 374f) {
            model.stickLights = !model.stickLights;
            sp.edit().putBoolean("stick_lights", model.stickLights).apply();
            BifrostBridge.setEnabled(model.stickLights);
            return true;
        }
        if (ly >= 390f && ly <= 484f) {
            model.stickSync = !model.stickSync;
            BifrostBridge.syncScreenBrightness = model.stickSync;
            sp.edit().putBoolean("stick_screen_sync", model.stickSync).apply();
            return true;
        }
        if (ly >= 500f && ly <= 594f) { model.toggleAutoDim(); return true; }
        if (ly >= 746f && ly <= 840f) { triggerArtRescan(); return true; }
        if (ly >= 856f && ly <= 950f) {
            art.clearCache(getContext());
            syncArtFlags();
            return true;
        }
        return false;
    }

    private void triggerArtRescan() {
        uiTexTries = 0; uiTexTick = 0;
        String dataDir = System.getenv("TOTK_DATA_DIR");
        File dd = dataDir != null && !dataDir.isEmpty() ? new File(dataDir) : null;
        File[] roots = new File[] {
            dd != null ? new File(dd, "romfs/Movie") : null,
            dd != null ? new File(dd, "romfs") : null,
            dd,
            getContext().getFilesDir(),
            getContext().getExternalFilesDir(null),
        };
        art.harvestFromGameDirsAsync(getContext(), roots, model.gameKind, () -> {
            syncArtFlags();
            invalidate();
        });
    }
}
