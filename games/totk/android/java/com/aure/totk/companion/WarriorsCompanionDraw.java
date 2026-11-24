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
import android.graphics.Shader;
import android.graphics.Typeface;
import java.util.Locale;

/**
 * Pure borderless Purah/Sheikah Pad renderer for {@link WarriorsCompanionView}.
 * Page 0 (TAB_TACTICAL) renders only harvested Pad chrome, the live top objective banner,
 * and the centered circular minimap without cards, borders, or filler labels.
 * Page 1 (TAB_LEDGER) shows unboxed telemetry on the left and 7 hairline settings rows on the right.
 */
public final class WarriorsCompanionDraw {
    static final float LOGIC_W = 1240f, LOGIC_H = 1080f;
    static final float MAP_CX = 620f, MAP_CY = 628f, MAP_RAD = 340f;
    static final float ROW_Y0 = 140f, ROW_STEP = 112f;

    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint imgPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Paint tintPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Paint texPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Rect dstRect = new Rect();
    private final Path clipPath = new Path();
    private final Path diamondPath = new Path();

    private final PorterDuffColorFilter filterTint = new PorterDuffColorFilter(0xD8020508, PorterDuff.Mode.SRC_IN);
    private final PorterDuffColorFilter sideTint = new PorterDuffColorFilter(0xB85CE6C4, PorterDuff.Mode.SRC_IN);
    private final PorterDuffColorFilter friezeTint = new PorterDuffColorFilter(0x665CE6C4, PorterDuff.Mode.SRC_IN);
    private final PorterDuffColorFilter navOnTint = new PorterDuffColorFilter(0xFF5CE6C4, PorterDuff.Mode.SRC_IN);
    private final PorterDuffColorFilter navOffTint = new PorterDuffColorFilter(0xAA7FA39E, PorterDuff.Mode.SRC_IN);
    private final PorterDuffColorFilter dimGlyphTint = new PorterDuffColorFilter(0x66DCC08A, PorterDuff.Mode.SRC_IN);

    private Bitmap lastBackdrop;
    private BitmapShader backdropShader;

    public void drawAll(
        Canvas c, WarriorsCompanionModel m, Bitmap backdrop, Bitmap filter, Bitmap glyph,
        Bitmap borderFrieze, Bitmap borderEdge, Bitmap borderSide,
        Bitmap minimap, Bitmap objective, Bitmap leftHud, int w, int h, long nowMs) {
        c.drawColor(0xFF000000);
        if (w <= 0 || h <= 0) return;
        syncBackdropShader(backdrop);
        float sc = Math.min(w / LOGIC_W, h / LOGIC_H);
        float ox = (w - LOGIC_W * sc) * 0.5f + m.orbitDx();
        float oy = (h - LOGIC_H * sc) * 0.5f + m.orbitDy();
        c.save();
        c.translate(ox, oy);
        c.scale(sc, sc, 0f, 0f);
        drawChrome(c, m, filter, borderFrieze, borderEdge, borderSide);
        drawHeader(c, m, glyph);
        if (m.activeTab() == WarriorsCompanionModel.TAB_TACTICAL) drawMapTab(c, m, minimap, objective, leftHud);
        else drawStatusTab(c, m);
        if (m.dimmed()) drawDimVeil(c, m, glyph);
        c.restore();
    }

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

    private void drawHeader(Canvas c, WarriorsCompanionModel m, Bitmap glyph) {
        boolean padSel = m.activeTab() == WarriorsCompanionModel.TAB_TACTICAL;
        if (glyph != null && !glyph.isRecycled()) {
            tintPaint.setColorFilter(padSel ? navOnTint : navOffTint);
            dstRect.set(56, 32, 92, 68);
            c.drawBitmap(glyph, null, dstRect, tintPaint);
        } else {
            drawDiamond(c, 74f, 50f, 11f, padSel ? m.primaryColor() : 0xAA7FA39E);
        }
        if (padSel) {
            p.setStyle(Paint.Style.FILL);
            p.setColor(m.secondaryColor());
            c.drawRect(46f, 74f, 102f, 77f, p);
        }
        int stCol = !padSel ? m.primaryColor() : 0xAA7FA39E;
        drawStatusIcon(c, 1166f, 50f, stCol);
        if (!padSel) {
            p.setStyle(Paint.Style.FILL);
            p.setColor(m.secondaryColor());
            c.drawRect(1138f, 74f, 1194f, 77f, p);
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

    private void drawMapTab(
        Canvas c, WarriorsCompanionModel m, Bitmap minimap, Bitmap objective, Bitmap leftHud) {
        drawLiveLeftHud(c, m, leftHud);
        drawLiveMinimap(c, m, minimap);
        drawLiveObjective(c, m, objective);
    }

    private void drawLiveLeftHud(Canvas c, WarriorsCompanionModel m, Bitmap leftHud) {
        if (leftHud == null || leftHud.isRecycled() || !m.hasLiveLeftHud) return;
        int dt = 198, dh = 852;
        int dw = Math.min(420, Math.round(leftHud.getWidth() * (852f / Math.max(1, leftHud.getHeight()))));
        dstRect.set(42, dt, 42 + dw, dt + dh);
        c.drawBitmap(leftHud, null, dstRect, imgPaint);
    }

    private void drawLiveMinimap(Canvas c, WarriorsCompanionModel m, Bitmap minimap) {
        if (minimap == null || minimap.isRecycled() || !m.hasLiveMinimap) return;
        float cx = m.hasLiveLeftHud ? 816f : MAP_CX, cy = 624f, rad = m.hasLiveLeftHud ? 328f : MAP_RAD;
        p.setStyle(Paint.Style.FILL);
        p.setColor(0xFF000000);
        c.drawCircle(cx, cy, rad, p);
        c.save();
        clipPath.reset();
        clipPath.addCircle(cx, cy, rad - 2f, Path.Direction.CW);
        c.clipPath(clipPath);
        dstRect.set(Math.round(cx - rad), Math.round(cy - rad), Math.round(cx + rad), Math.round(cy + rad));
        c.drawBitmap(minimap, null, dstRect, imgPaint);
        c.restore();
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(2f);
        p.setColor(0x705CE6C4);
        c.drawCircle(cx, cy, rad, p);
    }

    private void drawLiveObjective(Canvas c, WarriorsCompanionModel m, Bitmap objective) {
        if (objective == null || objective.isRecycled() || !m.hasLiveObjective) return;
        float sc = Math.min(980f / Math.max(1, objective.getWidth()), 280f / Math.max(1, objective.getHeight()));
        int dw = Math.round(objective.getWidth() * sc), dh = Math.round(objective.getHeight() * sc);
        int dl = Math.round((LOGIC_W - dw) * 0.5f), dt = Math.max(4, Math.round(144f - dh * 0.5f));
        dstRect.set(dl, dt, dl + dw, dt + dh);
        c.drawBitmap(objective, null, dstRect, imgPaint);
    }

    private void drawStatusTab(Canvas c, WarriorsCompanionModel m) {
        drawPerfLeft(c, m);
        drawCornerStats(c, m);
        drawSaveFooter(c, m);
        drawSettingsList(c, m);
    }

    private void drawPerfLeft(Canvas c, WarriorsCompanionModel m) {
        String fpsStr = m.formatClock(m.fps);
        String lowStr = m.formatClock(m.fpsOnePercentLow());
        String msStr = m.frameMs > 0.1f ? String.format(Locale.ROOT, "%.1f ms", m.frameMs) : "\u2014";
        drawText(c, fpsStr + " FPS", 56f, 192f, 44f, 0xFFE9E5D5, true, Paint.Align.LEFT);
        drawText(c, lowStr + "  \u00b7  " + msStr, 56f, 228f, 18f, m.secondaryColor(), true, Paint.Align.LEFT);
        drawFpsSparkline(c, m, 56f, 258f, 620f, 204f);
    }

    private void drawFpsSparkline(Canvas c, WarriorsCompanionModel m, float gx, float gy, float gw, float gh) {
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(1f);
        p.setColor(0x285CE6C4);
        c.drawLine(gx, gy, gx + gw, gy, p);
        c.drawLine(gx, gy + gh * 0.5f, gx + gw, gy + gh * 0.5f, p);
        c.drawLine(gx, gy + gh, gx + gw, gy + gh, p);
        drawText(c, "60", gx + 4f, gy + 18f, 14f, 0xFF6B7F7D, false, Paint.Align.LEFT);
        drawText(c, "30", gx + 4f, gy + gh * 0.5f - 6f, 14f, 0xFF6B7F7D, false, Paint.Align.LEFT);
        if (m.fpsHistCount < 2) return;
        p.setStyle(Paint.Style.STROKE);
        p.setColor(m.primaryColor());
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
        float y = 516f;
        if (m.devBuildTag != null && !m.devBuildTag.isEmpty()) {
            drawText(c, m.devBuildTag, 56f, y, 18f, m.secondaryColor(), true, Paint.Align.LEFT);
            y += 30f;
        }
        if (m.devOverlayText != null && !m.devOverlayText.isEmpty()) {
            String[] lines = m.devOverlayText.split("\n");
            for (int i = 0; i < Math.min(lines.length, 7); i++) {
                drawText(c, lines[i].trim(), 56f, y, 18f, 0xFFE9E5D5, false, Paint.Align.LEFT);
                y += 28f;
            }
        }
    }

    private void drawSaveFooter(Canvas c, WarriorsCompanionModel m) {
        String age = m.saveBusy ? "saving" : (m.lastSaveAgeS < 0 ? "\u2014" : (m.lastSaveAgeS + "s"));
        String line = m.saveFileName() + "  \u00b7  " + m.saveCommits + "  \u00b7  " + age;
        drawText(c, line, 56f, 950f, 17f, m.secondaryColor(), false, Paint.Align.LEFT);
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
    }

    private void drawSettingRow(
        Canvas c, int row, String label, String val, boolean active, WarriorsCompanionModel m) {
        float ry = ROW_Y0 + row * ROW_STEP;
        drawDiamond(c, 756f, ry + 48f, 7f, active ? m.primaryColor() : 0xFF475C58);
        drawText(c, label, 780f, ry + 55f, 20f, 0xFFE9E5D5, true, Paint.Align.LEFT);
        if (val != null && !val.isEmpty()) {
            drawText(c, val, 1190f, ry + 55f, 18f, active ? m.primaryColor() : m.secondaryColor(), true, Paint.Align.RIGHT);
        }
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(1f);
        p.setColor(0x285CE6C4);
        c.drawLine(744f, ry + 96f, 1190f, ry + 96f, p);
    }

    private void drawDimVeil(Canvas c, WarriorsCompanionModel m, Bitmap glyph) {
        p.setStyle(Paint.Style.FILL);
        p.setColor(0xD4000000);
        c.drawRect(0f, 0f, LOGIC_W, LOGIC_H, p);
        if (glyph != null && !glyph.isRecycled()) {
            tintPaint.setColorFilter(dimGlyphTint);
            dstRect.set(550, 461, 690, 619);
            c.drawBitmap(glyph, null, dstRect, tintPaint);
        } else {
            drawDiamond(c, LOGIC_W * 0.5f, LOGIC_H * 0.5f, 36f, m.secondaryColor());
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
        p.setColor(col);
        c.drawPath(diamondPath, p);
    }

    private void drawText(Canvas c, String s, float x, float y, float sp, int col, boolean bold, Paint.Align align) {
        if (s == null || s.isEmpty()) return;
        p.setStyle(Paint.Style.FILL);
        p.setColor(col);
        p.setTextSize(sp);
        p.setTextAlign(align);
        p.setTypeface(bold ? Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD) : Typeface.SANS_SERIF);
        c.drawText(s, x, y, p);
    }
}
