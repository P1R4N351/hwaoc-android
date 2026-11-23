// SPDX-License-Identifier: MIT
package com.aure.totk.companion;

import android.content.Context;
import android.graphics.Canvas;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.View;

/**
 * Interactive procedural Canvas CompanionScreen for Hyrule Warriors: Age of Calamity.
 * Converged 1240x1080 layout shared with Age of Imprisonment; zero copyrighted game assets.
 */
public final class WarriorsCompanionView extends View implements CompanionScreen {
    private static final long POLL_MS = 50L;
    private final DataSource source;
    private final WarriorsCompanionModel model = new WarriorsCompanionModel();
    private final WarriorsCompanionDraw draw = new WarriorsCompanionDraw();
    private final int[] ints = new int[WarriorsCompanionModel.DETAIL_INTS];
    private final float[] flts = new float[WarriorsCompanionModel.DETAIL_FLOATS];
    private long lastStateKey = -1L;
    private boolean attached;

    private final Runnable pollTask = new Runnable() {
        @Override public void run() {
            if (!attached) return;
            String text = source != null ? source.warriorsDetail(ints, flts) : null;
            if (text != null) {
                model.update(ints, flts, text, SystemClock.uptimeMillis());
            } else {
                model.updateOrbit(SystemClock.uptimeMillis());
            }
            invalidate();
            postDelayed(this, POLL_MS);
        }
    };

    public WarriorsCompanionView(Context context, DataSource source) {
        super(context);
        this.source = source;
        this.model.gameKind = WarriorsCompanionModel.GAME_AOC;
        setBackgroundColor(0xFF060709);
        setFocusable(false);
    }

    @Override public View view() { return this; }

    @Override public void setPage(int page) {
        int clamped = Math.max(0, Math.min(WarriorsCompanionModel.TAB_COUNT - 1, page));
        if (clamped != model.activeTab()) {
            model.selectTab(clamped);
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
        if (source != null && source.enabled()) {
            removeCallbacks(pollTask);
            postDelayed(pollTask, POLL_MS);
        }
    }

    @Override protected void onDetachedFromWindow() {
        attached = false;
        removeCallbacks(pollTask);
        if (source != null) source.warriorsAbort();
        super.onDetachedFromWindow();
    }

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        draw.drawAll(canvas, model, getWidth(), getHeight(), SystemClock.uptimeMillis());
    }

    @Override public boolean performClick() { return super.performClick(); }

    @Override public boolean onTouchEvent(MotionEvent ev) {
        if (ev.getActionMasked() != MotionEvent.ACTION_DOWN) return true;
        int w = getWidth(), h = getHeight();
        if (w <= 0 || h <= 0) return true;
        float sc = Math.min(w / WarriorsCompanionDraw.LOGIC_W, h / WarriorsCompanionDraw.LOGIC_H);
        float ox = (w - WarriorsCompanionDraw.LOGIC_W * sc) * 0.5f + model.orbitDx();
        float oy = (h - WarriorsCompanionDraw.LOGIC_H * sc) * 0.5f + model.orbitDy();
        float lx = (ev.getX() - ox) / sc;
        float ly = (ev.getY() - oy) / sc;
        if (lx < 0f || ly < 0f || lx > WarriorsCompanionDraw.LOGIC_W || ly > WarriorsCompanionDraw.LOGIC_H) {
            return true;
        }
        if (handleHeaderTap(lx, ly)) { invalidate(); return true; }
        if (model.touchBlocked()) return true;
        if (dispatchTabTap(lx, ly)) invalidate();
        return true;
    }

    private boolean handleHeaderTap(float lx, float ly) {
        if (ly < 10f || ly > 86f) return false;
        for (int i = 0; i < WarriorsCompanionModel.TAB_COUNT; i++) {
            float left = 24f + i * 302f;
            if (lx >= left && lx <= left + 286f) {
                model.selectTab(i);
                if (source != null) source.page(i);
                return true;
            }
        }
        return false;
    }

    private boolean dispatchTabTap(float lx, float ly) {
        int tab = model.activeTab();
        if (tab == WarriorsCompanionModel.TAB_TACTICAL) return handleTacticalTap(lx, ly);
        if (tab == WarriorsCompanionModel.TAB_COMMAND) return handleCommandTap(lx, ly);
        if (tab == WarriorsCompanionModel.TAB_GRIMOIRE) return handleGrimoireTap(lx, ly);
        return handleLedgerTap(lx, ly);
    }

    private boolean handleTacticalTap(float lx, float ly) {
        if (ly >= 952f && ly <= 1048f && lx >= 24f && lx <= 736f && source != null) {
            if (lx <= 194f) model.cycleZoom();
            else if (lx >= 204f && lx <= 374f) model.toggleHeadingUp();
            else if (lx >= 384f && lx <= 554f) source.warriorsPin(0, -1, WarriorsCompanionModel.pinPreset(model.pinPresetIdx));
            else if (lx >= 564f) source.warriorsPin(2, -1, null);
            return true;
        }
        if (lx >= 774f && lx <= 1200f && ly >= 160f && ly <= 278f && source != null) {
            for (int i = 0; i < 5; i++) {
                float bl = 774f + (i % 3) * 144f, bt = 160f + (i / 3) * 60f;
                if (lx >= bl && lx <= bl + 136f && ly >= bt && ly <= bt + 50f) {
                    model.pinPresetIdx = i;
                    source.warriorsPin(0, -1, WarriorsCompanionModel.pinPreset(i));
                    return true;
                }
            }
        }
        return handlePinAndCounterTaps(lx, ly);
    }

    private boolean handlePinAndCounterTaps(float lx, float ly) {
        if (lx >= 1140f && lx <= 1200f && ly >= 358f && ly <= 610f && source != null) {
            int rIdx = (int) ((ly - 358f) / 42f), row = 0;
            for (int i = 0; i < WarriorsCompanionModel.PIN_CAP; i++) {
                if (!model.pinActive[i]) continue;
                if (row == rIdx) { source.warriorsPin(1, i, null); return true; }
                row++;
            }
        }
        if (ly >= 766f && ly <= 822f) {
            if (lx >= 774f && lx <= 980f) { model.toggleTimer(SystemClock.uptimeMillis()); return true; }
            if (lx >= 992f && lx <= 1198f) { model.resetTimer(SystemClock.uptimeMillis()); return true; }
        }
        if (ly >= 978f && ly <= 1034f) {
            if (lx >= 774f && lx <= 868f) { model.adjustKeep(-1); return true; }
            if (lx >= 878f && lx <= 972f) { model.adjustKeep(1); return true; }
            if (lx >= 982f && lx <= 1076f) { model.adjustKeep(10); return true; }
            if (lx >= 1086f && lx <= 1198f) { model.resetKeep(); return true; }
        }
        return false;
    }

    private boolean handleCommandTap(float lx, float ly) {
        if (source == null) return false;
        if (ly >= 152f && ly <= 290f) {
            for (int i = 0; i < 4; i++) {
                float bl = 24f + i * 300f;
                if (lx >= bl && lx <= bl + 292f) {
                    source.warriorsFire(WarriorsCompanionModel.ACT_PARTY_BASE + i);
                    return true;
                }
            }
        }
        if (ly >= 376f && ly <= 820f) {
            boolean rSide = lx <= 610f;
            float baseL = rSide ? 42f : 648f;
            for (int i = 0; i < 4; i++) {
                float bl = baseL + (i & 1) * 280f, bt = 376f + (i >> 1) * 224f;
                if (lx >= bl && lx <= bl + 266f && ly >= bt && ly <= bt + 204f) {
                    int baseAct = rSide ? WarriorsCompanionModel.ACT_R_BASE : WarriorsCompanionModel.ACT_L_BASE;
                    source.warriorsFire(baseAct + i);
                    return true;
                }
            }
        }
        if (ly >= 868f && ly <= 1040f) {
            if (lx >= 24f && lx <= 316f) { source.warriorsFire(WarriorsCompanionModel.ACT_LOCKON); return true; }
            if (lx >= 324f && lx <= 616f) { source.warriorsFire(WarriorsCompanionModel.ACT_SPECIAL); return true; }
            if (lx >= 624f && lx <= 916f) { source.warriorsFire(WarriorsCompanionModel.ACT_DODGE); return true; }
            if (lx >= 924f && lx <= 1216f) { source.warriorsAbort(); return true; }
        }
        return false;
    }

    private boolean handleGrimoireTap(float lx, float ly) {
        if (ly >= 118f && ly <= 178f) {
            if (lx >= 680f && lx <= 990f) { model.toggleMacroMode(); return true; }
            if (lx >= 1006f && lx <= 1200f && source != null) { source.warriorsAbort(); return true; }
        }
        for (int i = 0; i < 6; i++) {
            float bl = 24f + (i % 3) * 402f, bt = 212f + (i / 3) * 408f;
            if (lx >= bl && lx <= bl + 388f && ly >= bt && ly <= bt + 384f) {
                if (model.macroMode && source != null) source.warriorsFire(WarriorsCompanionModel.ACT_COMBO_BASE + i);
                return true;
            }
        }
        return false;
    }

    private boolean handleLedgerTap(float lx, float ly) {
        if (lx >= 652f && lx <= 1194f && ly >= 650f && ly <= 742f) {
            model.toggleAutoDim();
            return true;
        }
        return false;
    }
}
