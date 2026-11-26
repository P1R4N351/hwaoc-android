// SPDX-License-Identifier: MIT
package com.aure.totk.companion;

import android.graphics.Bitmap;
import android.graphics.BitmapShader;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffColorFilter;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import java.util.Locale;

/**
 * Pure borderless Purah/Sheikah Pad renderer for {@link WarriorsCompanionView}.
 * Page 0 (TAB_TACTICAL) renders only harvested Pad chrome, the live top objective banner,
 * the live left HUD and the circular minimap without cards, borders, or filler labels.
 * Page 1 (TAB_LEDGER) shows unboxed telemetry on the left and 7 hairline settings rows on the right.
 * Geometry and motion come from {@link WarriorsCompanionLayout}; live elements and page switches ease
 * instead of snapping.
 */
public final class WarriorsCompanionDraw {
    static final float LOGIC_W = WarriorsCompanionLayout.LOGIC_W, LOGIC_H = WarriorsCompanionLayout.LOGIC_H;
    private static final float VIS_EPS = 0.004f;

    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint imgPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Paint tintPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Paint texPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Rect dstRect = new Rect();
    private final RectF dstF = new RectF();
    private final Path clipPath = new Path();
    private final Path diamondPath = new Path();
    private final WarriorsCompanionLayout.Scene scene = new WarriorsCompanionLayout.Scene();

    private final PorterDuffColorFilter filterTint = new PorterDuffColorFilter(0xD8020508, PorterDuff.Mode.SRC_IN);
    private final PorterDuffColorFilter sideTint = new PorterDuffColorFilter(0xB85CE6C4, PorterDuff.Mode.SRC_IN);
    private final PorterDuffColorFilter friezeTint = new PorterDuffColorFilter(0x665CE6C4, PorterDuff.Mode.SRC_IN);
    private final PorterDuffColorFilter navOnTint = new PorterDuffColorFilter(0xFF5CE6C4, PorterDuff.Mode.SRC_IN);
    private final PorterDuffColorFilter navOffTint = new PorterDuffColorFilter(0xAA7FA39E, PorterDuff.Mode.SRC_IN);
    private final PorterDuffColorFilter dimGlyphTint = new PorterDuffColorFilter(0x66DCC08A, PorterDuff.Mode.SRC_IN);

    private Bitmap lastBackdrop, lastMinimap, lastObjective, lastLeftHud;
    private BitmapShader backdropShader;
    private final Rect srcRect = new Rect();
    private final int[] hudRow = new int[WarriorsCompanionLayout.MAX_SCAN_WIDTH];
    private float alpha = 1f;
    private long hudScanMs;
    private int hudScanW, hudScanH;
    private boolean hudScanned;

    /** Draws one frame; returns true while an element is still easing, so the view should draw again. */
    public boolean drawAll(
        Canvas c, WarriorsCompanionModel m, Bitmap backdrop, Bitmap filter, Bitmap glyph,
        Bitmap borderFrieze, Bitmap borderEdge, Bitmap borderSide,
        Bitmap minimap, Bitmap objective, Bitmap leftHud, int w, int h, long nowMs) {
        c.drawColor(0xFF000000);
        if (w <= 0 || h <= 0) return false;
        syncBackdropShader(backdrop);
        boolean moving = stepScene(m, minimap, objective, leftHud, nowMs);
        float sc = Math.min(w / LOGIC_W, h / LOGIC_H);
        float ox = (w - LOGIC_W * sc) * 0.5f + m.orbitDx();
        float oy = (h - LOGIC_H * sc) * 0.5f + m.orbitDy();
        c.save();
        c.translate(ox, oy);
        c.scale(sc, sc, 0f, 0f);
        drawChrome(c, m, filter, borderFrieze, borderEdge, borderSide);
        drawPages(c, m, nowMs);
        alpha = 1f;
        float dim = scene.dimT(nowMs);
        if (dim > VIS_EPS) drawDimVeil(c, m, glyph, dim);
        drawHeader(c, m, glyph, nowMs, 1f - 0.35f * dim);
        alpha = 1f;
        c.restore();
        return moving;
    }

    private static boolean live(Bitmap b, boolean flag) {
        return flag && b != null && !b.isRecycled();
    }

    private static int fade(int argb, float a) {
        int al = Math.round(((argb >>> 24) & 0xFF) * Math.max(0f, Math.min(1f, a)));
        return (al << 24) | (argb & 0x00FFFFFF);
    }

    private boolean stepScene(WarriorsCompanionModel m, Bitmap minimap, Bitmap objective, Bitmap leftHud, long now) {
        boolean mapIn = live(minimap, m.hasLiveMinimap);
        boolean hudIn = live(leftHud, m.hasLiveLeftHud);
        boolean objIn = live(objective, m.hasLiveObjective);
        if (mapIn) lastMinimap = minimap;
        if (hudIn) lastLeftHud = leftHud;
        if (objIn) lastObjective = objective;
        if (hudIn) scanLeftHud(leftHud, now);
        boolean moving = scene.step(now, m.activeTab(), m.dimmed(), mapIn, hudIn,
                objIn, objIn ? objective.getWidth() : 0, objIn ? objective.getHeight() : 0);
        if (scene.map.hidden(now)) lastMinimap = null;
        if (!hudIn && scene.hudAlpha(now) <= 0f) { lastLeftHud = null; hudScanned = false; }
        if (scene.obj.hidden(now)) lastObjective = null;
        return moving;
    }

    /**
     * Content clusters of the left-HUD bitmap: one sampled row at a time into a preallocated buffer, at most
     * every RESCAN_MS (or at once when the bitmap size changes); the layout holds them steady in between.
     */
    private void scanLeftHud(Bitmap b, long now) {
        int w = b.getWidth(), h = b.getHeight();
        long since = now - hudScanMs;
        boolean due = !hudScanned || w != hudScanW || h != hudScanH
                || since >= WarriorsCompanionLayout.RESCAN_MS || since < 0L;
        if (!due || w <= 0 || h <= 0 || w > hudRow.length) return;
        hudScanned = true; hudScanMs = now; hudScanW = w; hudScanH = h;
        WarriorsCompanionLayout.ClusterScan s = scene.hudScan();
        s.begin(w, h);
        for (int i = 0; i < s.rowCount(); i++) {
            b.getPixels(hudRow, 0, w, 0, s.rowY(i), w, 1);
            s.row(i, hudRow, 0);
        }
        s.finish();
        scene.ingestHud(now);
    }

    private static final float BLEED = 400f;

    private void syncBackdropShader(Bitmap tex) {
        if (tex == lastBackdrop) return;
        lastBackdrop = tex;
        backdropShader = (tex != null && !tex.isRecycled())
                ? new BitmapShader(tex, Shader.TileMode.REPEAT, Shader.TileMode.REPEAT) : null;
    }

    private void drawChrome(
        Canvas c, WarriorsCompanionModel m, Bitmap filter, Bitmap borderFrieze,
        Bitmap borderEdge, Bitmap borderSide) {
        p.setStyle(Paint.Style.FILL);
        p.setColor(m.bgColor());
        c.drawRect(0f, 0f, LOGIC_W, LOGIC_H, p);
        if (backdropShader != null) {
            texPaint.setShader(backdropShader);
            texPaint.setAlpha(255);
            c.drawRect(0f, 0f, LOGIC_W, LOGIC_H, texPaint);
        }
        drawFilterEdges(c, filter);
        drawPadBorders(c, borderFrieze, borderEdge, borderSide);
    }

    private void drawFilterEdges(Canvas c, Bitmap filter) {
        if (filter == null || filter.isRecycled()) return;
        tintPaint.setColorFilter(filterTint);
        dstRect.set(0, 0, 220, (int) LOGIC_H);
        c.drawBitmap(filter, null, dstRect, tintPaint);
        c.save();
        c.translate(LOGIC_W, 0f);
        c.scale(-1f, 1f, 0f, 0f);
        dstRect.set(0, 0, 220, (int) LOGIC_H);
        c.drawBitmap(filter, null, dstRect, tintPaint);
        c.restore();
    }

    private void drawPadBorders(Canvas c, Bitmap borderFrieze, Bitmap borderEdge, Bitmap borderSide) {
        if (borderSide != null && !borderSide.isRecycled()) {
            tintPaint.setColorFilter(sideTint);
            dstRect.set(30, 92, 42, 992);
            c.drawBitmap(borderSide, null, dstRect, tintPaint);
            dstRect.set(1198, 92, 1210, 992);
            c.drawBitmap(borderSide, null, dstRect, tintPaint);
        }
        if (borderEdge != null && !borderEdge.isRecycled()) {
            dstRect.set(0, 62, (int) LOGIC_W, 92);
            c.drawBitmap(borderEdge, null, dstRect, imgPaint);
            c.save();
            c.translate(0f, 1044f);
            c.scale(1f, -1f, 0f, 0f);
            dstRect.set(0, 0, (int) LOGIC_W, 30);
            c.drawBitmap(borderEdge, null, dstRect, imgPaint);
            c.restore();
        }
        drawWallFrieze(c, borderFrieze);
    }

    private void drawWallFrieze(Canvas c, Bitmap frieze) {
        if (frieze == null || frieze.isRecycled()) return;
        int span = 872, thick = 20;
        int segW = Math.max(140, Math.min(span, Math.round(frieze.getWidth() * ((float) thick / Math.max(1, frieze.getHeight())))));
        tintPaint.setColorFilter(friezeTint);
        drawWallStrip(c, frieze, 8f, 976f, -90f, span, thick, segW);
        drawWallStrip(c, frieze, 1232f, 104f, 90f, span, thick, segW);
    }

    private void drawWallStrip(
        Canvas c, Bitmap frieze, float tx, float ty, float rot, int span, int thick, int segW) {
        c.save();
        c.translate(tx, ty);
        c.rotate(rot, 0f, 0f);
        c.clipRect(0, 0, span, thick);
        for (int x = 0, n = 0; x < span && n < 8; x += segW, n++) {
            dstRect.set(x, 0, x + segW, thick);
            c.drawBitmap(frieze, null, dstRect, tintPaint);
        }
        c.restore();
    }

    private void drawHeader(Canvas c, WarriorsCompanionModel m, Bitmap glyph, long now, float navAlpha) {
        boolean padSel = m.activeTab() == WarriorsCompanionModel.TAB_TACTICAL;
        float cx = WarriorsCompanionLayout.TAB0_CX, cy = WarriorsCompanionLayout.NAV_CY;
        int navA = Math.round(255f * Math.max(0f, Math.min(1f, navAlpha)));
        if (glyph != null && !glyph.isRecycled()) {
            tintPaint.setColorFilter(padSel ? navOnTint : navOffTint);
            tintPaint.setAlpha(navA);
            dstF.set(cx - 18f, cy - 18f, cx + 18f, cy + 18f);
            c.drawBitmap(glyph, null, dstF, tintPaint);
            tintPaint.setAlpha(255);
        } else {
            drawDiamond(c, cx, cy, 11f, fade(padSel ? m.primaryColor() : 0xAA7FA39E, navAlpha));
        }
        drawStatusIcon(c, WarriorsCompanionLayout.TAB1_CX, cy, fade(!padSel ? m.primaryColor() : 0xAA7FA39E, navAlpha));
        drawNavRules(c, fade(m.secondaryColor(), navAlpha), now);
    }

    /** The selected-tab rule cross-fades between the two header glyphs instead of jumping. */
    private void drawNavRules(Canvas c, int col, long now) {
        float t = scene.tabT(now), y = WarriorsCompanionLayout.NAV_RULE_Y, hw = WarriorsCompanionLayout.NAV_RULE_HALF;
        p.setStyle(Paint.Style.FILL);
        if (t < 1f - VIS_EPS) {
            p.setColor(fade(col, 1f - t));
            c.drawRect(WarriorsCompanionLayout.TAB0_CX - hw, y, WarriorsCompanionLayout.TAB0_CX + hw, y + 3f, p);
        }
        if (t > VIS_EPS) {
            p.setColor(fade(col, t));
            c.drawRect(WarriorsCompanionLayout.TAB1_CX - hw, y, WarriorsCompanionLayout.TAB1_CX + hw, y + 3f, p);
        }
    }

    private void drawStatusIcon(Canvas c, float cx, float cy, int col) {
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(2f);
        p.setColor(col);
        c.drawLine(cx - 16f, cy - 9f, cx + 16f, cy - 9f, p);
        c.drawLine(cx - 16f, cy, cx + 16f, cy, p);
        c.drawLine(cx - 16f, cy + 9f, cx + 16f, cy + 9f, p);
        drawDiamond(c, cx - 6f, cy - 9f, 3.6f, col);
        drawDiamond(c, cx + 7f, cy, 3.6f, col);
        drawDiamond(c, cx - 2f, cy + 9f, 3.6f, col);
    }

    /** Both pages while switching: the outgoing one fades and slides out as the incoming one slides in. */
    private void drawPages(Canvas c, WarriorsCompanionModel m, long now) {
        float t = scene.tabT(now);
        if (t < 1f - VIS_EPS) {
            c.save();
            c.translate(-WarriorsCompanionLayout.TAB_SHIFT * t, 0f);
            alpha = 1f - t;
            drawMapTab(c, now);
            c.restore();
        }
        if (t > VIS_EPS) {
            c.save();
            c.translate(WarriorsCompanionLayout.TAB_SHIFT * (1f - t), 0f);
            alpha = t;
            drawStatusTab(c, m);
            c.restore();
        }
        alpha = 1f;
    }

    private void drawMapTab(Canvas c, long now) {
        drawLiveLeftHud(c, now);
        drawLiveMinimap(c, now);
        drawLiveObjective(c, now);
    }

    private void drawImage(Canvas c, Bitmap b, float a) {
        imgPaint.setAlpha(Math.round(255f * a));
        c.drawBitmap(b, null, dstF, imgPaint);
        imgPaint.setAlpha(255);
    }

    /** Live bitmap at its own aspect, anchored top-left in its animated slot (held size, so no per-frame pulse). */
    private void drawAnchored(Canvas c, WarriorsCompanionLayout.Slot s, Bitmap b, float k, long now) {
        float a = alpha * s.alpha(now);
        if (a <= VIS_EPS || k <= 0f || b == null || b.isRecycled()) return;
        // The stream's rect grows and shrinks at its left and around its row (the marker's glow); its right edge is the game
        // screen's. Anchored right and on the row's centre, a narrower frame does not jump toward the middle.
        float r = s.right(now), cy = (s.top(now) + s.bottom(now)) * 0.5f, bw = b.getWidth() * k, bh = b.getHeight() * k;
        dstF.set(r - bw, cy - bh * 0.5f, r, cy + bh * 0.5f);
        drawImage(c, b, a);
    }

    /** Each visible content cluster of the left HUD, cropped to its held bounds, in its animated slot. */
    private void drawLiveLeftHud(Canvas c, long now) {
        Bitmap b = lastLeftHud;
        if (b == null || b.isRecycled()) return;
        WarriorsCompanionLayout.ClusterHold h = scene.clusters;
        for (int i = 0; i < WarriorsCompanionLayout.MAX_CLUSTERS; i++) {
            WarriorsCompanionLayout.Slot s = scene.hudParts[i];
            if (!h.live(i) && h.overlapsLive(i)) continue;
            float a = alpha * s.alpha(now);
            if (a <= VIS_EPS) continue;
            srcRect.set(Math.round(h.left(i)), Math.round(h.top(i)),
                    Math.min(b.getWidth(), Math.round(h.right(i))), Math.min(b.getHeight(), Math.round(h.bottom(i))));
            dstF.set(s.left(now), s.top(now), s.right(now), s.bottom(now));
            imgPaint.setAlpha(Math.round(255f * a));
            c.drawBitmap(b, srcRect, dstF, imgPaint);
            imgPaint.setAlpha(255);
        }
    }

    private void drawLiveObjective(Canvas c, long now) {
        drawAnchored(c, scene.obj, lastObjective, scene.objectiveDrawScale(now), now);
    }

    private void drawLiveMinimap(Canvas c, long now) {
        WarriorsCompanionLayout.Slot s = scene.map;
        float a = alpha * s.alpha(now);
        Bitmap b = lastMinimap;
        if (a <= VIS_EPS || b == null || b.isRecycled()) return;
        float l = s.left(now), t = s.top(now), r = s.right(now), bt = s.bottom(now);
        float cx = (l + r) * 0.5f, cy = (t + bt) * 0.5f, rad = Math.min(r - l, bt - t) * 0.5f;
        p.setStyle(Paint.Style.FILL);
        p.setColor(fade(0xFF000000, a));
        c.drawCircle(cx, cy, rad, p);
        c.save();
        clipPath.reset();
        clipPath.addCircle(cx, cy, rad - WarriorsCompanionLayout.MAP_RING, Path.Direction.CW);
        c.clipPath(clipPath);
        dstF.set(cx - rad, cy - rad, cx + rad, cy + rad);
        drawImage(c, b, a);
        c.restore();
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(WarriorsCompanionLayout.MAP_RING);
        p.setColor(fade(WarriorsCompanionLayout.MAP_RING_COLOR, a));
        c.drawCircle(cx, cy, rad, p);
    }

    private void drawStatusTab(Canvas c, WarriorsCompanionModel m) {
        drawPerfLeft(c, m);
        drawCornerStats(c, m);
        drawSaveFooter(c, m);
        drawSettingsList(c, m);
    }

    private void drawPerfLeft(Canvas c, WarriorsCompanionModel m) {
        float x = WarriorsCompanionLayout.ST_X;
        String fpsStr = m.formatClock(m.fps);
        String lowStr = m.formatClock(m.fpsOnePercentLow());
        String msStr = m.frameMs > 0.1f ? String.format(Locale.ROOT, "%.1f ms", m.frameMs) : "—";
        drawText(c, fpsStr + " FPS", x, WarriorsCompanionLayout.ST_FPS_Y, 44f, 0xFFE9E5D5, true, Paint.Align.LEFT);
        drawText(c, lowStr + "  ·  " + msStr, x, WarriorsCompanionLayout.ST_SUB_Y, 18f, m.secondaryColor(), true, Paint.Align.LEFT);
        drawFpsSparkline(c, m, x, WarriorsCompanionLayout.ST_GRAPH_Y, WarriorsCompanionLayout.ST_GRAPH_W, WarriorsCompanionLayout.ST_GRAPH_H);
    }

    private void drawFpsSparkline(Canvas c, WarriorsCompanionModel m, float gx, float gy, float gw, float gh) {
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(1f);
        p.setColor(fade(0x285CE6C4, alpha));
        c.drawLine(gx, gy, gx + gw, gy, p);
        c.drawLine(gx, gy + gh * 0.5f, gx + gw, gy + gh * 0.5f, p);
        c.drawLine(gx, gy + gh, gx + gw, gy + gh, p);
        drawText(c, "60", gx + 4f, gy + 18f, 14f, 0xFF6B7F7D, false, Paint.Align.LEFT);
        drawText(c, "30", gx + 4f, gy + gh * 0.5f - 6f, 14f, 0xFF6B7F7D, false, Paint.Align.LEFT);
        if (m.fpsHistCount < 2) return;
        p.setStyle(Paint.Style.STROKE);
        p.setColor(fade(m.primaryColor(), alpha));
        p.setStrokeWidth(3f);
        int start = (m.fpsHistHead + WarriorsCompanionModel.FPS_CAP - m.fpsHistCount) % WarriorsCompanionModel.FPS_CAP;
        float px = 0f, py = 0f;
        for (int i = 0; i < m.fpsHistCount; i++) {
            float v = Math.max(0f, Math.min(60f, m.fpsHist[(start + i) % WarriorsCompanionModel.FPS_CAP]));
            float sx = gx + (i * gw) / Math.max(1, WarriorsCompanionModel.FPS_CAP - 1);
            float sy = gy + gh - (v / 60f) * gh;
            if (i > 0) c.drawLine(px, py, sx, sy, p);
            px = sx; py = sy;
        }
    }

    private void drawCornerStats(Canvas c, WarriorsCompanionModel m) {
        float x = WarriorsCompanionLayout.ST_X, y = WarriorsCompanionLayout.ST_DEV_Y;
        if (m.devBuildTag != null && !m.devBuildTag.isEmpty()) {
            drawText(c, m.devBuildTag, x, y, 18f, m.secondaryColor(), true, Paint.Align.LEFT);
            y += 30f;
        }
        if (m.devOverlayText != null && !m.devOverlayText.isEmpty()) {
            String[] lines = m.devOverlayText.split("\n");
            for (int i = 0; i < Math.min(lines.length, WarriorsCompanionLayout.ST_DEV_LINES); i++) {
                drawText(c, lines[i].trim(), x, y, 18f, 0xFFE9E5D5, false, Paint.Align.LEFT);
                y += WarriorsCompanionLayout.ST_LINE;
            }
        }
    }

    private void drawSaveFooter(Canvas c, WarriorsCompanionModel m) {
        String age = m.saveBusy ? "saving" : (m.lastSaveAgeS < 0 ? "—" : (m.lastSaveAgeS + "s"));
        String line = m.saveFileName() + "  ·  " + m.saveCommits + "  ·  " + age;
        drawText(c, line, WarriorsCompanionLayout.ST_X, WarriorsCompanionLayout.ST_FOOTER_Y, 17f, m.secondaryColor(), false, Paint.Align.LEFT);
    }

    private void drawSettingsList(Canvas c, WarriorsCompanionModel m) {
        drawSettingRow(c, 0, "Corner HUD", m.devOnStatus ? "ON" : "TOP", m.devOnStatus, m);
        drawSettingRow(c, 1, "Stick Lights", m.stickLights ? "ON" : "OFF", m.stickLights, m);
        drawSettingRow(c, 2, "Stick Brightness", m.stickLights ? m.stickSyncLabel() : "OFF",
                m.stickLights && m.stickSync, m);
        drawSettingRow(c, 3, "Cutscene Dim", m.autoDim ? "ON" : "OFF", m.autoDim, m);
        drawSettingRow(c, 4, "Minimap Layer", m.rtCandLabel(), m.minimapCandCount > 0, m);
        drawSettingRow(c, 5, "Rescan Assets", "", false, m);
        drawSettingRow(c, 6, "Clear Assets", "", false, m);
        drawSettingRow(c, 7, "Minimap", m.streamModeLabel(WarriorsCompanionModel.STREAM_MINIMAP), m.onSecond(WarriorsCompanionModel.STREAM_MINIMAP), m);
        drawSettingRow(c, 8, "Objective", m.streamModeLabel(WarriorsCompanionModel.STREAM_OBJECTIVE), m.onSecond(WarriorsCompanionModel.STREAM_OBJECTIVE), m);
        drawSettingRow(c, 9, "HUD Column", m.streamModeLabel(WarriorsCompanionModel.STREAM_HUD), m.onSecond(WarriorsCompanionModel.STREAM_HUD), m);
    }

    private void drawSettingRow(
        Canvas c, int row, String label, String val, boolean active, WarriorsCompanionModel m) {
        float ry = WarriorsCompanionLayout.rowTop(row);
        drawDiamond(c, WarriorsCompanionLayout.ROW_DIAMOND_X, ry + WarriorsCompanionLayout.ROW_MID, 7f, active ? m.primaryColor() : 0xFF475C58);
        drawText(c, label, WarriorsCompanionLayout.ROW_LABEL_X, ry + WarriorsCompanionLayout.ROW_BASELINE, 20f, 0xFFE9E5D5, true, Paint.Align.LEFT);
        if (val != null && !val.isEmpty()) {
            drawText(c, val, WarriorsCompanionLayout.ROW_X1, ry + WarriorsCompanionLayout.ROW_BASELINE, 18f,
                    active ? m.primaryColor() : m.secondaryColor(), true, Paint.Align.RIGHT);
        }
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(1f);
        p.setColor(fade(0x285CE6C4, alpha));
        c.drawLine(WarriorsCompanionLayout.ROW_X0, ry + WarriorsCompanionLayout.ROW_RULE,
                WarriorsCompanionLayout.ROW_X1, ry + WarriorsCompanionLayout.ROW_RULE, p);
    }

    private void drawDimVeil(Canvas c, WarriorsCompanionModel m, Bitmap glyph, float d) {
        p.setStyle(Paint.Style.FILL);
        p.setColor(fade(0xD4000000, d));
        c.drawRect(-BLEED, -BLEED, LOGIC_W + BLEED, LOGIC_H + BLEED, p);
        if (glyph != null && !glyph.isRecycled()) {
            tintPaint.setColorFilter(dimGlyphTint);
            tintPaint.setAlpha(Math.round(255f * d));
            dstRect.set(550, 461, 690, 619);
            c.drawBitmap(glyph, null, dstRect, tintPaint);
            tintPaint.setAlpha(255);
        } else {
            alpha = d;
            drawDiamond(c, LOGIC_W * 0.5f, LOGIC_H * 0.5f, 36f, m.secondaryColor());
            alpha = 1f;
        }
    }

    private void drawDiamond(Canvas c, float cx, float cy, float r, int col) {
        diamondPath.reset();
        diamondPath.moveTo(cx, cy - r);
        diamondPath.lineTo(cx + r, cy);
        diamondPath.lineTo(cx, cy + r);
        diamondPath.lineTo(cx - r, cy);
        diamondPath.close();
        p.setStyle(Paint.Style.FILL);
        p.setColor(fade(col, alpha));
        c.drawPath(diamondPath, p);
    }

    private void drawText(Canvas c, String s, float x, float y, float sp, int col, boolean bold, Paint.Align align) {
        if (s == null || s.isEmpty()) return;
        p.setStyle(Paint.Style.FILL);
        p.setColor(fade(col, alpha));
        p.setTextSize(sp);
        p.setTextAlign(align);
        p.setTypeface(bold ? Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD) : Typeface.SANS_SERIF);
        c.drawText(s, x, y, p);
    }
}
