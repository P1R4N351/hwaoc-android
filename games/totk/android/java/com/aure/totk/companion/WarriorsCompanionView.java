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
 * Mirrors the game's offscreen Vulkan minimap and objective RTs onto Display 4,
 * extracts Pad chrome art from the user's ScreenLayout.rdb*, streams dev telemetry
 * on STATUS, and syncs Bifrost stick LEDs.
 */
public final class WarriorsCompanionView extends View implements CompanionScreen {
    private static final long POLL_MS = 50L;
    private static final long DEV_STAT_MS = 1000L;
    private static final int MINIMAP_MAX_BYTES = 768 * 768 * 4;
    private static final int OBJ_MAX_BYTES = 1152 * 288 * 4;
    private static final int LHUD_MAX_BYTES = 640 * 1440 * 4;
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
    private final byte[] objCropBytes = new byte[OBJ_MAX_BYTES];
    private final int[] objMeta = new int[4];
    private final byte[] lhudBytes = new byte[LHUD_MAX_BYTES];
    private final int[] lhudMeta = new int[4];
    private Bitmap minimapBmp, objBmp, lhudBmp;
    private long lastStateKey = -1L, lastDevMs;
    private int priorOverlayMode = -1;
    private boolean attached;

    private final Runnable pollTask = new Runnable() {
        @Override public void run() {
            if (!attached) return;
            tickFrame(SystemClock.uptimeMillis());
            postDelayed(this, POLL_MS);
        }
    };

    public WarriorsCompanionView(Context context, DataSource source) {
        super(context);
        this.source = source;
        this.model.gameKind = WarriorsCompanionModel.GAME_AOC;
        loadPrefs(context);
        this.art.loadCached(context);
        syncArtFlags();
        triggerArtRescan();
        setBackgroundColor(0xFF000000);
        setFocusable(false);
    }

    private void loadPrefs(Context context) {
        SharedPreferences sp = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        model.devOnStatus = sp.getBoolean("dev_on_status", true);
        model.autoDim = sp.getBoolean("auto_dim", true);
        boolean lights = sp.getBoolean("stick_lights", true);
        model.stickLights = lights;
        BifrostBridge.setEnabled(lights);
        boolean sync = sp.getBoolean("stick_screen_sync", true);
        model.stickSync = sync;
        BifrostBridge.syncScreenBrightness = sync;
        for (int i = 0; i < WarriorsCompanionModel.STREAM_COUNT; i++) {
            int mode = sp.getInt("stream_mode_" + i, WarriorsCompanionModel.ON_SECOND);
            model.streamMode[i] = mode >= 0 && mode <= WarriorsCompanionModel.ON_NEITHER ? mode : WarriorsCompanionModel.ON_SECOND;
        }
        streamModesPushed = false;
    }

    private boolean streamModesPushed;

    /** Tells the native capture where each module goes; repeated only after a change. */
    private void pushStreamModes() {
        if (streamModesPushed) return;
        for (int i = 0; i < WarriorsCompanionModel.STREAM_COUNT; i++) CompanionNative.warriorsStreamMode(i, model.streamMode[i]);
        streamModesPushed = true;
    }

    private void cycleStream(SharedPreferences sp, int stream) {
        if (stream < 0 || stream >= WarriorsCompanionModel.STREAM_COUNT) return;
        model.streamMode[stream] = WarriorsCompanionModel.nextStreamMode(model.streamMode[stream]);
        sp.edit().putInt("stream_mode_" + stream, model.streamMode[stream]).apply();
        streamModesPushed = false;
    }

    private void tickFrame(long now) {
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
    }

    private void pollNativeMinimap() {
        if (!model.inGame || model.moviePlaying || model.hostPaused) {
            CompanionNative.warriorsMinimapEnable(false);
            minimapMeta[2] = 0;
            objMeta[2] = 0;
            lhudMeta[2] = 0;
            art.setMinimap(null);
            art.setObjective(null);
            art.setLeftHud(null);
            return;
        }
        CompanionNative.warriorsMinimapEnable(true);
        pushStreamModes();
        if (!model.onSecond(WarriorsCompanionModel.STREAM_MINIMAP)) {
            minimapMeta[2] = 0;
            art.setMinimap(null);
            pollNativeObjective();
            pollNativeLeftHud();
            return;
        }
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
        } else if (rw <= 0 || rh <= 0) {
            minimapMeta[2] = 0;
            art.setMinimap(null);
        }
        pollNativeObjective();
        pollNativeLeftHud();
    }

    private void pollNativeObjective() {
        if (!model.onSecond(WarriorsCompanionModel.STREAM_OBJECTIVE)) { objMeta[2] = 0; art.setObjective(null); return; }
        int got = CompanionNative.warriorsObjective(objBytes, objMeta);
        int ow = objMeta[0], oh = objMeta[1], active = objMeta[3];
        model.objectiveW = ow;
        model.objectiveH = oh;
        if (active == 0) {
            art.setObjective(null);
            return;
        }
        if (got == 1 && ow > 0 && ow <= 1152 && oh > 0 && oh <= 288 && ow * oh * 4 <= objBytes.length) {
            art.setObjective(cropObjective(objBytes, ow, oh));
        }
    }

    private void pollNativeLeftHud() {
        if (!model.onSecond(WarriorsCompanionModel.STREAM_HUD)) { lhudMeta[2] = 0; art.setLeftHud(null); return; }
        int got = CompanionNative.warriorsLeftHud(lhudBytes, lhudMeta);
        int lw = lhudMeta[0], lh = lhudMeta[1], active = lhudMeta[3];
        model.leftHudW = lw;
        model.leftHudH = lh;
        if (active == 0) {
            art.setLeftHud(null);
            return;
        }
        if (got == 1 && lw > 0 && lw <= 640 && lh > 0 && lh <= 1440 && lw * lh * 4 <= lhudBytes.length) {
            if (lhudBmp == null || lhudBmp.getWidth() != lw || lhudBmp.getHeight() != lh) {
                lhudBmp = Bitmap.createBitmap(lw, lh, Bitmap.Config.ARGB_8888);
            }
            lhudBmp.copyPixelsFromBuffer(ByteBuffer.wrap(lhudBytes, 0, lw * lh * 4));
            art.setLeftHud(lhudBmp);
        }
    }

    private Bitmap cropObjective(byte[] rgba, int ow, int oh) {
        if (objBmp == null || objBmp.getWidth() != ow || objBmp.getHeight() != oh) {
            objBmp = Bitmap.createBitmap(ow, oh, Bitmap.Config.ARGB_8888);
        }
        int rowBytes = ow * 4;
        for (int y = 0; y < oh; y++) softenObjectiveRow(rgba, y * rowBytes, objCropBytes, y * rowBytes, ow, y, oh);
        objBmp.copyPixelsFromBuffer(ByteBuffer.wrap(objCropBytes, 0, ow * oh * 4));
        return objBmp;
    }

    private static void softenObjectiveRow(byte[] src, int sOff, byte[] dst, int dOff, int cw, int y, int oh) {
        int fy = y < 10 ? (y * 25) : (y >= oh - 10 ? ((oh - 1 - y) * 25) : 255);
        for (int x = 0; x < cw; x++) {
            int si = sOff + x * 4, di = dOff + x * 4;
            int a = src[si + 3] & 0xFF;
            if (a == 0) { dst[di] = 0; dst[di + 1] = 0; dst[di + 2] = 0; dst[di + 3] = 0; continue; }
            int r = src[si] & 0xFF, g = src[si + 1] & 0xFF, b = src[si + 2] & 0xFF;
            int mx = Math.max(r, Math.max(g, b));
            int na = mx < 24 ? ((a * 112) >> 8) : a;
            int fx = x < 16 ? (x * 16) : 255;
            int edge = Math.min(fx, fy);
            if (edge < 255) na = (na * edge) >> 8;
            if (na < a) { r = (r * na) / a; g = (g * na) / a; b = (b * na) / a; }
            dst[di] = (byte) r; dst[di + 1] = (byte) g; dst[di + 2] = (byte) b; dst[di + 3] = (byte) na;
        }
    }

    private void syncArtFlags() {
        model.hasBackdrop = art.backdrop() != null;
        model.hasFilter = art.filter() != null;
        model.hasGlyph = art.glyph() != null;
        model.hasBorders = art.borderSide() != null || art.borderEdge() != null || art.borderFrieze() != null;
        model.hasLiveMinimap = art.minimap() != null;
        model.hasLiveObjective = art.objective() != null;
        model.hasLiveLeftHud = art.leftHud() != null;
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
        art.setLeftHud(null);
        minimapBmp = null;
        objBmp = null;
        lhudBmp = null;
        if (model.devOnStatus && priorOverlayMode > 0) CompanionNative.setOverlayMode(priorOverlayMode);
        if (source != null) source.warriorsAbort();
        super.onDetachedFromWindow();
    }

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        boolean moving = draw.drawAll(canvas, model, art.backdrop(), art.filter(), art.glyph(),
                art.borderFrieze(), art.borderEdge(), art.borderSide(),
                art.minimap(), art.objective(), art.leftHud(),
                getWidth(), getHeight(), SystemClock.uptimeMillis());
        if (moving) postInvalidateOnAnimation();
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
        if (model.activeTab() == WarriorsCompanionModel.TAB_LEDGER && handleStatusTap(lx, ly)) {
            performClick();
            invalidate();
        }
        return true;
    }

    private boolean handleHeaderTap(float lx, float ly) {
        int hit = WarriorsCompanionLayout.headerTabAt(lx, ly);
        if (hit < 0) return false;
        model.selectTab(hit);
        CompanionNative.warriorsMinimapEnable(model.inGame && !model.moviePlaying && !model.hostPaused);
        if (source != null) source.page(hit);
        return true;
    }

    private void cycleRt() {
        model.minimapCandIdx = CompanionNative.warriorsMinimapCycle();
        minimapMeta[2] = 0;
        pollNativeMinimap();
    }

    private boolean handleStatusTap(float lx, float ly) {
        int row = WarriorsCompanionLayout.statusRowAt(lx, ly);
        if (row < 0) return false;
        SharedPreferences sp = getContext().getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        if (row == 0) { toggleCornerHud(sp); return true; }
        if (row == 1) { toggleStickLights(sp); return true; }
        if (row == 2) { toggleStickSync(sp); return true; }
        if (row == 3) {
            model.toggleAutoDim();
            sp.edit().putBoolean("auto_dim", model.autoDim).apply();
            return true;
        }
        if (row == 4) { cycleRt(); return true; }
        if (row == 5) { triggerArtRescan(); return true; }
        if (row == 6) { art.clearCache(getContext()); syncArtFlags(); return true; }
        if (row >= 7 && row <= 9) { cycleStream(sp, row - 7); return true; }
        return false;
    }

    private void toggleCornerHud(SharedPreferences sp) {
        model.devOnStatus = !model.devOnStatus;
        sp.edit().putBoolean("dev_on_status", model.devOnStatus).apply();
        if (model.devOnStatus) {
            int cur = CompanionNative.setOverlayMode(-1);
            if (cur > 0) priorOverlayMode = cur;
            CompanionNative.setOverlayMode(0);
        } else {
            CompanionNative.setOverlayMode(priorOverlayMode > 0 ? priorOverlayMode : 2);
        }
    }

    private void toggleStickLights(SharedPreferences sp) {
        model.stickLights = !model.stickLights;
        sp.edit().putBoolean("stick_lights", model.stickLights).apply();
        BifrostBridge.setEnabled(model.stickLights);
    }

    private void toggleStickSync(SharedPreferences sp) {
        model.stickSync = !model.stickSync;
        BifrostBridge.syncScreenBrightness = model.stickSync;
        sp.edit().putBoolean("stick_screen_sync", model.stickSync).apply();
    }

    private void triggerArtRescan() {
        String dataDir = System.getenv("TOTK_DATA_DIR");
        File dd = dataDir != null && !dataDir.isEmpty() ? new File(dataDir) : null;
        File ext = getContext().getExternalFilesDir(null);
        File[] roots = new File[] {
            dd != null ? new File(dd, "romfs") : null,
            dd,
            ext != null ? new File(ext, "extracted/hwaoc130/romfs") : null,
            ext,
            getContext().getFilesDir(),
        };
        art.harvestFromGameDirsAsync(getContext(), roots, model.gameKind, () -> {
            syncArtFlags();
            invalidate();
        });
    }
}
