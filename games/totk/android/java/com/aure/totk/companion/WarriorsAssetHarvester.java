// SPDX-License-Identifier: MIT
package com.aure.totk.companion;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

/**
 * BYOG runtime asset harvester for the Warriors second-screen companion.
 * Zero game assets ship in the repository or APK: Pad chrome sprites (backdrop,
 * filter, glyph, border frieze/edge/side) are extracted directly from the user's
 * installed RomFS ScreenLayout.rdb* via {@link WarriorsKtglArt} and cached under
 * filesDir/companion_art/.
 */
public final class WarriorsAssetHarvester {
    private static final String TAG = "WarriorsArt";
    private static final String DIR_NAME = "companion_art";
    private static final String[] ART_FILES = {
        "backdrop.png", "filter.png", "glyph.png",
        "border_frieze.png", "border_edge.png", "border_side.png"
    };
    private static final String[] LEGACY_FILES = {"emblem.png", "purah_tex.png"};
    private static final String SRC_FILE = "source.txt";

    private volatile Bitmap backdrop;
    private volatile Bitmap filter;
    private volatile Bitmap glyph;
    private volatile Bitmap borderFrieze;
    private volatile Bitmap borderEdge;
    private volatile Bitmap borderSide;
    private volatile Bitmap minimap;
    private volatile Bitmap objective;
    private volatile Bitmap leftHud;
    private volatile String source = "NONE";
    private volatile boolean scanning;
    private volatile boolean suppressed;

    public Bitmap backdrop() { return backdrop; }
    public Bitmap filter() { return filter; }
    public Bitmap glyph() { return glyph; }
    public Bitmap borderFrieze() { return borderFrieze; }
    public Bitmap borderEdge() { return borderEdge; }
    public Bitmap borderSide() { return borderSide; }
    public Bitmap minimap() { return minimap; }
    public void setMinimap(Bitmap bmp) { minimap = bmp; }
    public Bitmap objective() { return objective; }
    public void setObjective(Bitmap bmp) { objective = bmp; }
    public Bitmap leftHud() { return leftHud; }
    public void setLeftHud(Bitmap bmp) { leftHud = bmp; }
    public String source() { return source; }
    public boolean isSuppressed() { return suppressed; }
    public void setSuppressed(boolean s) { suppressed = s; }

    public void loadCached(Context ctx) {
        File dir = artDir(ctx);
        if (dir == null) return;
        String tag = readSmallText(new File(dir, SRC_FILE));
        if (!WarriorsKtglArt.SRC_TAG.equals(tag)) {
            purgeFiles(dir);
            clearBitmaps();
            return;
        }
        backdrop = decodeFileSimple(new File(dir, "backdrop.png"));
        filter = decodeFileSimple(new File(dir, "filter.png"));
        glyph = decodeFileSimple(new File(dir, "glyph.png"));
        borderFrieze = decodeFileSimple(new File(dir, "border_frieze.png"));
        borderEdge = decodeFileSimple(new File(dir, "border_edge.png"));
        borderSide = decodeFileSimple(new File(dir, "border_side.png"));
        source = hasAllArt() ? WarriorsKtglArt.SRC_TAG : "NONE";
        if (!hasAllArt()) {
            purgeFiles(dir);
            clearBitmaps();
        }
    }

    public void harvestFromGameDirsAsync(Context ctx, File[] roots, int gameKind, Runnable onUpdated) {
        suppressed = false;
        if (scanning || ctx == null) return;
        if (hasAllArt() && WarriorsKtglArt.SRC_TAG.equals(source)) return;
        scanning = true;
        final Context app = ctx.getApplicationContext();
        final Handler main = new Handler(Looper.getMainLooper());
        Thread t = new Thread(() -> runHarvest(app, roots, main, onUpdated), "warriors-art-harvest");
        t.setDaemon(true);
        t.start();
    }

    private void runHarvest(Context app, File[] roots, Handler main, Runnable onUpdated) {
        try {
            File dir = artDir(app);
            if (dir != null && WarriorsKtglArt.harvest(app, roots, dir) && !suppressed) {
                loadCached(app);
                if (hasAllArt() && onUpdated != null) main.post(onUpdated);
            }
        } catch (RuntimeException e) {
            Log.w(TAG, "harvest scan failed", e);
        } finally {
            scanning = false;
        }
    }

    public void clearCache(Context ctx) {
        suppressed = true;
        File dir = artDir(ctx);
        if (dir != null) purgeFiles(dir);
        clearBitmaps();
    }

    private boolean hasAllArt() {
        return backdrop != null && filter != null && glyph != null
                && borderFrieze != null && borderEdge != null && borderSide != null;
    }

    private void clearBitmaps() {
        backdrop = null;
        filter = null;
        glyph = null;
        borderFrieze = null;
        borderEdge = null;
        borderSide = null;
        source = "NONE";
    }

    private static void purgeFiles(File dir) {
        for (String n : ART_FILES) deleteQuiet(new File(dir, n));
        for (String n : LEGACY_FILES) deleteQuiet(new File(dir, n));
        deleteQuiet(new File(dir, SRC_FILE));
    }

    private static File artDir(Context ctx) {
        if (ctx == null) return null;
        File d = new File(ctx.getFilesDir(), DIR_NAME);
        if (!d.exists() && !d.mkdirs()) return null;
        return d;
    }

    private static Bitmap decodeFileSimple(File f) {
        return (f.isFile() && f.length() > 0) ? BitmapFactory.decodeFile(f.getAbsolutePath()) : null;
    }

    private static String readSmallText(File f) {
        if (!f.isFile() || f.length() <= 0 || f.length() > 64) return "";
        byte[] buf = new byte[64];
        try (FileInputStream in = new FileInputStream(f)) {
            int n = in.read(buf);
            return n > 0 ? new String(buf, 0, n, StandardCharsets.UTF_8).trim() : "";
        } catch (IOException e) {
            return "";
        }
    }

    private static void deleteQuiet(File f) {
        if (f.exists() && !f.delete()) Log.w(TAG, "delete failed: " + f.getName());
    }
}
