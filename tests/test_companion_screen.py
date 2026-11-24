"""The second screen's shell contract (CompanionScreen): both games' screens behind one interface, the page round
trip, and the shell's pump that publishes the page to the common state and tells the screen what the game does.

The real CompanionView (TotK Native's MIT page, compiled into every app and chosen by no game at the moment: run here
with a GameBuild that names it), DuoSurfaceView (the Eden Duo page of both games), the one CompanionContent (which picks
by GameBuild.COMPANION, generated from games/games.toml for each game) and PresentationController's pump run here on a
plain JVM over the smallest android.* that compiles them."""
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ANDROID = ROOT / 'games/totk/android'
JAVA = ANDROID / 'java/com/aure/totk/companion'
sys.path.insert(0, str(ROOT / 'tools'))
import games  # noqa: E402

# android.* and org.json as far as the two screens touch them. Nothing draws: the probe never attaches a view.
ANDROID_STUBS = {
    'android/content/Context.java': '''package android.content;
public class Context {
    public static final int MODE_PRIVATE = 0;
    public static final String BATTERY_SERVICE = "batterymanager";
    public Context getApplicationContext() { return this; }
    public java.io.File getFilesDir() { return new java.io.File("."); }
    public java.io.File getExternalFilesDir(String type) { return null; }
    public SharedPreferences getSharedPreferences(String name, int mode) { return new SharedPreferences(); }
    public Object getSystemService(String name) { return null; }
    public android.content.res.AssetManager getAssets() { return new android.content.res.AssetManager(); }
    public android.content.pm.PackageManager getPackageManager() { return new android.content.pm.PackageManager(); }
    public String getPackageName() { return "probe"; }
}''',
    'android/content/SharedPreferences.java': '''package android.content;
public class SharedPreferences {
    public static class Editor {
        public Editor putBoolean(String k, boolean v) { return this; }
        public void apply() { }
    }
    public boolean getBoolean(String k, boolean d) { return d; }
    public Editor edit() { return new Editor(); }
}''',
    'android/os/BatteryManager.java': '''package android.os;
public class BatteryManager {
    public static final int BATTERY_PROPERTY_CAPACITY = 4;
    public int getIntProperty(int id) { return -1; }
}''',
    'android/content/pm/PackageInfo.java': 'package android.content.pm; public class PackageInfo { public long lastUpdateTime; public long getLongVersionCode() { return 1; } }',
    'android/content/pm/PackageManager.java': '''package android.content.pm;
public class PackageManager {
    public static class NameNotFoundException extends Exception { }
    public PackageInfo getPackageInfo(String name, int flags) throws NameNotFoundException { return new PackageInfo(); }
}''',
    'android/content/res/AssetManager.java': '''package android.content.res;
public class AssetManager {
    public String[] list(String path) { return new String[0]; }
    public java.io.InputStream open(String path) throws java.io.IOException { throw new java.io.IOException(path); }
}''',
    'android/content/res/Configuration.java': 'package android.content.res; public class Configuration { }',
    'android/content/res/Resources.java': 'package android.content.res; public class Resources { public android.util.DisplayMetrics getDisplayMetrics() { return new android.util.DisplayMetrics(); } }',
    'android/util/DisplayMetrics.java': 'package android.util; public class DisplayMetrics { public float density = 2, scaledDensity = 2; }',
    'android/util/Log.java': 'package android.util; public final class Log { public static int w(String t, String m) { return 0; } public static int w(String t, String m, Throwable e) { return 0; } }',
    'android/os/SystemClock.java': 'package android.os; public final class SystemClock { public static long now = 1_000_000; public static long uptimeMillis() { return now; } }',
    'android/graphics/PixelFormat.java': 'package android.graphics; public class PixelFormat { public static final int RGBA_8888 = 1; }',
    'android/graphics/Bitmap.java': '''package android.graphics;
public class Bitmap {
    public enum Config { ARGB_8888 }
    public static Bitmap createBitmap(int w, int h, Config c) { return new Bitmap(); }
    public int getWidth() { return 1; } public int getHeight() { return 1; } public boolean isRecycled() { return false; }
    public void copyPixelsFromBuffer(java.nio.Buffer src) { }
}''',
    'android/graphics/Color.java': 'package android.graphics; public class Color { public static final int WHITE = -1; }',
    'android/graphics/ColorFilter.java': 'package android.graphics; public class ColorFilter { }',
    'android/graphics/PorterDuff.java': 'package android.graphics; public class PorterDuff { public enum Mode { SRC_IN } }',
    'android/graphics/PorterDuffColorFilter.java': 'package android.graphics; public class PorterDuffColorFilter extends ColorFilter { public PorterDuffColorFilter(int color, PorterDuff.Mode mode) { } }',
    'android/graphics/Typeface.java': 'package android.graphics; public class Typeface { public static final Typeface DEFAULT = new Typeface(), DEFAULT_BOLD = new Typeface(); }',
    'android/graphics/Path.java': '''package android.graphics;
public class Path {
    public enum Direction { CW, CCW }
    public void reset() { } public void moveTo(float x, float y) { } public void cubicTo(float a, float b, float c, float d, float e, float f) { }
    public void lineTo(float x, float y) { } public void close() { }
    public void addRoundRect(RectF r, float rx, float ry, Direction d) { }
    public void addCircle(float x, float y, float r, Direction d) { }
}''',
    'android/graphics/RectF.java': '''package android.graphics;
public class RectF {
    public float left, top, right, bottom;
    public RectF() { }
    public RectF(float l, float t, float r, float b) { set(l, t, r, b); }
    public RectF(RectF o) { set(o.left, o.top, o.right, o.bottom); }
    public void set(float l, float t, float r, float b) { left = l; top = t; right = r; bottom = b; }
    public void set(RectF o) { set(o.left, o.top, o.right, o.bottom); }
    public float width() { return right - left; } public float height() { return bottom - top; }
    public float centerX() { return (left + right) / 2; } public float centerY() { return (top + bottom) / 2; }
    public boolean contains(float x, float y) { return x >= left && x < right && y >= top && y < bottom; }
    public void offset(float dx, float dy) { left += dx; right += dx; top += dy; bottom += dy; }
    public boolean intersect(RectF o) { return true; }
}''',
    'android/graphics/Paint.java': '''package android.graphics;
public class Paint {
    public static final int ANTI_ALIAS_FLAG = 1, FILTER_BITMAP_FLAG = 2;
    public enum Style { FILL, STROKE } public enum Align { LEFT, CENTER, RIGHT } public enum Cap { ROUND, BUTT }
    public Paint(int flags) { }
    public void setColor(int c) { } public void setAlpha(int a) { } public void setStrokeWidth(float w) { } public void setStyle(Style s) { } public void setTextAlign(Align a) { }
    public void setStrokeCap(Cap c) { } public void setTextSize(float s) { } public Typeface setTypeface(Typeface t) { return t; }
    public ColorFilter setColorFilter(ColorFilter f) { return f; }
    public float measureText(String text) { return text.length(); }
    public int breakText(String text, boolean forward, float max, float[] measured) { return text.length(); }
}''',
    'android/graphics/Picture.java': '''package android.graphics;
public class Picture { public Canvas beginRecording(int w, int h) { return new Canvas(); } public void endRecording() { } }''',
    'android/graphics/Canvas.java': '''package android.graphics;
public class Canvas {
    public int save() { return 0; } public void restore() { } public void restoreToCount(int n) { }
    public int saveLayerAlpha(RectF b, int a) { return 0; } public int saveLayerAlpha(float l, float t, float r, float b, int a) { return 0; }
    public boolean clipRect(RectF r) { return true; } public boolean clipRect(float l, float t, float r, float b) { return true; }
    public boolean clipPath(Path path) { return true; }
    public void translate(float x, float y) { } public void scale(float x, float y, float px, float py) { } public void rotate(float d, float px, float py) { }
    public void drawColor(int c) { } public void drawRoundRect(RectF r, float rx, float ry, Paint p) { }
    public void drawRect(RectF r, Paint p) { } public void drawRect(float l, float t, float r, float b, Paint p) { }
    public void drawLine(float a, float b, float c, float d, Paint p) { } public void drawText(String s, float x, float y, Paint p) { }
    public void drawBitmap(Bitmap b, Rect s, RectF d, Paint p) { } public void drawBitmap(Bitmap b, Rect s, Rect d, Paint p) { } public void drawCircle(float x, float y, float r, Paint p) { }
    public void drawOval(RectF r, Paint p) { } public void drawArc(RectF r, float a, float b, boolean c, Paint p) { }
    public void drawPath(Path path, Paint p) { } public void drawPicture(Picture p) { }
}''',
    'android/graphics/Rect.java': 'package android.graphics; public class Rect { public Rect() { } public Rect(int l, int t, int r, int b) { } public void set(int l, int t, int r, int b) { } }',
    'android/view/MotionEvent.java': '''package android.view;
public class MotionEvent {
    public static final int ACTION_DOWN = 0, ACTION_UP = 1, ACTION_MOVE = 2, ACTION_CANCEL = 3, ACTION_POINTER_DOWN = 5, ACTION_POINTER_UP = 6;
    public int getActionMasked() { return 0; } public int getActionIndex() { return 0; } public int getPointerCount() { return 0; }
    public int getPointerId(int i) { return 0; } public float getX() { return 0; } public float getY() { return 0; }
    public float getX(int i) { return 0; } public float getY(int i) { return 0; }
}''',
    'android/view/ScaleGestureDetector.java': '''package android.view;
public class ScaleGestureDetector {
    public static class SimpleOnScaleGestureListener { public boolean onScale(ScaleGestureDetector detector) { return false; } }
    public ScaleGestureDetector(android.content.Context context, SimpleOnScaleGestureListener listener) { }
    public boolean onTouchEvent(MotionEvent event) { return true; } public boolean isInProgress() { return false; }
    public float getScaleFactor() { return 1; } public float getFocusX() { return 0; } public float getFocusY() { return 0; }
}''',
    'android/view/View.java': '''package android.view;
public class View {
    public static int invalidated;
    private final android.content.Context context;
    public View(android.content.Context context) { this.context = context; }
    public android.content.Context getContext() { return context; }
    public android.content.res.Resources getResources() { return new android.content.res.Resources(); }
    public void setBackgroundColor(int c) { } public void setFocusable(boolean f) { } public void setContentDescription(CharSequence d) { }
    public void invalidate() { invalidated++; } public void postInvalidateOnAnimation() { }
    public boolean postDelayed(Runnable r, long ms) { return true; } public boolean removeCallbacks(Runnable r) { return true; }
    public int getWidth() { return 0; } public int getHeight() { return 0; }
    public boolean performClick() { return true; } public boolean onTouchEvent(MotionEvent e) { return false; }
    protected void onAttachedToWindow() { } protected void onDetachedFromWindow() { } protected void onDraw(android.graphics.Canvas c) { }
    protected void onSizeChanged(int w, int h, int ow, int oh) { } protected void onConfigurationChanged(android.content.res.Configuration c) { }
}''',
    'android/view/Surface.java': 'package android.view; public class Surface { }',
    'android/view/SurfaceHolder.java': '''package android.view;
public interface SurfaceHolder {
    interface Callback { void surfaceCreated(SurfaceHolder h); void surfaceChanged(SurfaceHolder h, int f, int w, int hh); void surfaceDestroyed(SurfaceHolder h); }
    void setFormat(int format); void addCallback(Callback c); Surface getSurface();
}''',
    'android/view/SurfaceView.java': '''package android.view;
public class SurfaceView extends View {
    public SurfaceView(android.content.Context context) { super(context); }
    public void setZOrderOnTop(boolean top) { }
    public SurfaceHolder getHolder() {
        return new SurfaceHolder() {
            public void setFormat(int format) { } public void addCallback(Callback c) { } public Surface getSurface() { return new Surface(); }
        };
    }
}''',
    'org/json/JSONArray.java': '''package org.json;
public class JSONArray { public int length() { return 0; } public JSONObject optJSONObject(int i) { return null; } public Object opt(int i) { return null; } }''',
    'org/json/JSONObject.java': '''package org.json;
public class JSONObject {
    public boolean has(String k) { return false; } public boolean isNull(String k) { return true; }
    public int optInt(String k) { return 0; } public int optInt(String k, int d) { return d; } public long optLong(String k, long d) { return d; }
    public double optDouble(String k) { return Double.NaN; } public double optDouble(String k, double d) { return d; }
    public boolean optBoolean(String k) { return false; } public String optString(String k) { return ""; } public String optString(String k, String d) { return d; }
    public JSONObject optJSONObject(String k) { return null; } public JSONArray optJSONArray(String k) { return null; }
}''',
}

# The Tears of the Kingdom screen's own collaborators, as far as an unattached view touches them.
TOTK_STUBS = {
    'com/aure/totk/companion/AssetStore.java': '''package com.aure.totk.companion;
import android.graphics.*;
public final class AssetStore {
    public org.json.JSONObject index = new org.json.JSONObject();
    public org.json.JSONArray markers = new org.json.JSONArray();
    public AssetStore(android.content.Context context, Runnable changed) { }
    public static boolean available(android.content.Context context) { return true; }
    public static String humanize(String key) { return key; }
    public boolean decor(Canvas c, String name, RectF b, float u) { return false; } public boolean decor(Canvas c, String name, RectF b, float u, int o) { return false; }
    public boolean softDecor(Canvas c, String name, RectF b, float u) { return false; } public float decorAspect(String name, float d) { return d; }
    public int color(String name, int d) { return d; } public Bitmap ui(String name) { return null; } public Bitmap icon(String name) { return null; }
    public Bitmap pageIcon(String name) { return null; } public Typeface typeface(String text) { return null; }
    public String label(String g, String k, String d) { return d; } public String itemName(String a) { return a; } public String itemDescription(String a) { return ""; }
    public void close() { }
}''',
    'com/aure/totk/companion/MapRenderer.java': '''package com.aure.totk.companion;
final class MapRenderer {
    boolean following = true, automaticLayer = true, round; String layer = "Ground";
    MapRenderer(AssetStore assets, float unit) { }
    void setUnit(float u) { } void recenter() { } void pan(float dx, float dy) { } void scale(float f, float x, float y) { }
    void marker(double x, double z, double heading) { } void noMarker() { }
    void draw(android.graphics.Canvas c, android.graphics.RectF b, Snapshot data, boolean mini) { }
}''',
    'com/aure/totk/companion/Snapshot.java': '''package com.aure.totk.companion;
import org.json.*;
public final class Snapshot {
    public static final String[] CATEGORIES = {"weapons", "bows", "shields", "armor", "materials", "food", "zonai", "keyItems"};
    public final JSONObject root = new JSONObject(), player = new JSONObject();
    public String status = "starting";
    public static Snapshot waiting() { return new Snapshot(); }
    public static JSONObject object(JSONObject v, String k) { return new JSONObject(); } public static JSONArray array(JSONObject v, String k) { return new JSONArray(); }
    public JSONArray items(int category) { return new JSONArray(); } public String itemCategory(int category, int index) { return CATEGORIES[category]; }
    public double coordinate(int axis) { return Double.NaN; }
}''',
    'com/aure/totk/BifrostBridge.java':
        'package com.aure.totk; public final class BifrostBridge { public static volatile boolean syncScreenBrightness = true; public static volatile int lastScreenPct = -1; }',
    'com/aure/totk/companion/CompanionNative.java': '''package com.aure.totk.companion;
public final class CompanionNative {
    public static boolean enabledRt;
    public static int cycleCount;
    public static boolean isEnabled() { return true; }
    public static int setOverlayMode(int mode) { return mode < 0 ? 0 : mode; }
    public static int warriorsMinimap(byte[] rgbaOut, int[] meta) { return 0; }
    public static int warriorsMinimapCycle() { return ++cycleCount; }
    public static void warriorsMinimapEnable(boolean on) { enabledRt = on; }
}''',
    'com/aure/totk/companion/WarriorsAssetHarvester.java': '''package com.aure.totk.companion;
import android.content.Context; import android.graphics.Bitmap; import java.io.File;
public final class WarriorsAssetHarvester {
    public void loadCached(Context c) { }
    public void harvestFromGameDirsAsync(Context c, File[] r, int k, Runnable u) { }
    public void clearCache(Context c) { }
    public void setMinimap(Bitmap bmp) { }
    public Bitmap backdrop() { return null; }
    public Bitmap emblem() { return null; }
    public Bitmap minimap() { return null; }
    public String source() { return "NONE"; }
}''',
    'com/aure/totk/companion/SnapshotPoller.java': '''package com.aure.totk.companion;
public final class SnapshotPoller {
    public interface Listener { void changed(Snapshot snapshot, boolean stale); }
    public static int fullRequests;
    public final ItemActions actions = new ItemActions();
    public SnapshotPoller(DataSource source, Listener listener) { }
    public void start() { } public void stop() { } public void requestFull() { fullRequests++; } public void action(int kind, int arg) { }
}''',
}

SCREEN_PROBE = r'''package com.aure.totk.companion;

public final class ScreenProbe {
    static void require(boolean ok, String what) { if (!ok) throw new AssertionError(what); }
    /** SnapshotPoller's request count (a stub of the Tears of the Kingdom build only, so by name). */
    static int fullRequests() throws ReflectiveOperationException {
        return Class.forName("com.aure.totk.companion.SnapshotPoller").getField("fullRequests").getInt(null);
    }
    public static void main(String[] args) throws ReflectiveOperationException {
        android.content.Context context = new android.content.Context();
        DataSource source = new DataSource() {
            @Override public String read(int detail) { return null; }
            @Override public boolean send(int kind, int arg) { return false; }
        };
        boolean botw = args[0].equals("botw"), duo = args[1].equals("duo"), warriors = args[1].equals("warriors");
        require(CompanionContent.TABLET_CAN_BE_MISSING == botw, "the build says whether its tablet can be missing");
        for (int initial = 0; initial < 3; initial++) {
            CompanionScreen screen = CompanionContent.create(context, source, initial);
            require(screen.view() == screen, "the screen is its own view");
            String wantClass = duo ? "DuoSurfaceView" : warriors ? "WarriorsCompanionView" : "CompanionView";
            require(screen.getClass().getSimpleName().equals(wantClass), "this build's screen");
            CompanionState state = new CompanionState();
            screen.onGameState(state);                                    // no state at all is a state a screen takes
            require(state.set(new long[]{CompanionState.READY, 1, 1, 1, 12, 12, initial, 0}), "state");
            screen.onGameState(state);
            if (duo) {
                // The native page keeps its own pages: none to report, and a request changes nothing here.
                require(screen.page() == -1, "no page of its own");
                screen.setPage(2); require(screen.page() == -1, "still none");
                continue;
            }
            require(screen.page() == initial, "created on the page asked for");
            for (int page = 0; page < 3; page++) { screen.setPage(page); require(screen.page() == page, "page round trip " + page); }
            int lastPage = warriors ? 3 : 2;
            screen.setPage(7); require(screen.page() == lastPage, "beyond the last page: the last");
            screen.setPage(-3); require(screen.page() == 0, "before the first: the first");
            int redraws = android.view.View.invalidated, fulls = fullRequests();
            screen.setPage(1);
            int expectFull = warriors ? fulls : fulls + 1;
            require(android.view.View.invalidated == redraws + 1 && fullRequests() == expectFull, "a page change redraws once");
            // A loading screen named by the common state is a change of view: one redraw; the same state again: none.
            state.set(new long[]{CompanionState.LOADING, 0, 0, 1, -1, -1, 1, 0});
            redraws = android.view.View.invalidated;
            screen.onGameState(state); screen.onGameState(state);
            require(android.view.View.invalidated == redraws + 1, "told twice, redrawn once");
            require(screen.page() == 1, "the game's state does not move the page");
        }
        System.out.println("pass: companion screen " + args[0] + " " + args[1]);
    }
}
'''

# The main page's rules that need no drawing: which screens get the Ready grid, and the low-hearts threshold it shares
# with the stick LEDs (BifrostLooks.lowOn).
READY_PROBE = r'''package com.aure.totk.companion;

public final class ReadyProbe {
    static void require(boolean ok, String what) { if (!ok) throw new AssertionError(what); }
    public static void main(String[] args) {
        require(CompanionView.readyLayout(1240, 1080), "the Thor's second screen");
        require(CompanionView.readyLayout(1920, 1080), "a wide landscape screen");
        require(!CompanionView.readyLayout(1080, 2400), "a phone in portrait keeps the responsive page");
        require(!CompanionView.readyLayout(420, 380), "a small panel keeps the responsive page");
        require(!CompanionView.readyLayout(0, 0), "no size yet");
        for (int max = 12; max <= 120; max += 4) {
            int on = Math.max(4, (max + 3) / 4);   // BifrostLooks.lowOn
            require(CompanionView.lowHearts(on, max) && !CompanionView.lowHearts(on + 1, max), "low hearts at " + on + " of " + max);
        }
        require(CompanionView.lowHearts(0, 80) && !CompanionView.lowHearts(-1, 80) && !CompanionView.lowHearts(4, -1), "unknown is not low");
        System.out.println("pass: ready rules");
    }
}
'''

# PresentationController's pump (the section between its two markers) around a queue the probe runs by hand.
PUMP_PRELUDE = r'''package com.aure.totk.companion;

import java.util.*;

public final class PumpProbe {
    static void require(boolean ok, String what) { if (!ok) throw new AssertionError(what); }
    static final class Handler {
        final List<Object[]> queue = new ArrayList<>();
        long now;
        void post(Runnable r) { postDelayed(r, 0); }
        void postDelayed(Runnable r, long ms) { queue.add(new Object[]{now + ms, r}); }
        void removeCallbacks(Runnable r) { queue.removeIf(job -> job[1] == r); }
        void runUntil(long until) {
            for (;;) {
                int best = -1;
                for (int i = 0; i < queue.size(); i++) if ((long)queue.get(i)[0] <= until && (best < 0 || (long)queue.get(i)[0] < (long)queue.get(best)[0])) best = i;
                if (best < 0) break;
                Object[] job = queue.remove(best);
                now = Math.max(now, (long)job[0]);
                ((Runnable)job[1]).run();
            }
            now = until;
        }
    }
    static final class Source implements DataSource {
        long[] words; int reads; final List<Integer> pages = new ArrayList<>();
        @Override public String read(int detail) { return null; }
        @Override public boolean send(int kind, int arg) { return false; }
        @Override public boolean state(long[] out) { reads++; if (words == null) return false; System.arraycopy(words, 0, out, 0, words.length); return true; }
        @Override public void page(int page) { pages.add(page); }
    }
    static final class Screen implements CompanionScreen {
        int page; final List<String> told = new ArrayList<>();
        @Override public android.view.View view() { return null; }
        @Override public void setPage(int value) { page = value; }
        @Override public int page() { return page; }
        @Override public void onGameState(CompanionState s) { told.add(s.status + ":" + s.inGame + ":" + s.menuOpen + ":" + s.life + ":" + s.page); }
    }
    final Handler main = new Handler();
    final Source source = new Source();
    boolean closed;
'''
PUMP_TESTS = r'''
    public static void main(String[] args) {
        PumpProbe shell = new PumpProbe();
        Screen screen = new Screen();
        screen.page = 1;
        shell.screen(screen);
        require(shell.source.reads == 0, "nothing read before the queue runs");
        shell.main.runUntil(0);
        require(shell.source.pages.equals(List.of(1)), "the screen's page is published at once");
        require(screen.told.equals(List.of("0:false:false:-1:-1")), "a new screen is told even that there is no state");
        // A save, as the getter gives it (the page comes back in it).
        shell.source.words = new long[]{CompanionState.READY, 1, 0, 1, 12, 12, 1, 0};
        shell.main.runUntil(1000);
        require(screen.told.size() == 2 && screen.told.get(1).equals("5:true:false:12:1"), "told once when the state appears: " + screen.told);
        require(shell.source.reads == 5, "four looks a second: " + shell.source.reads);
        // The sample's age moves, nothing else: not told again.
        shell.source.words[CompanionState.AGE_MS] = 100;
        shell.main.runUntil(2000);
        require(screen.told.size() == 2, "an unchanged state is not repeated");
        // The menu opens, a heart goes.
        shell.source.words[CompanionState.MENU_OPEN] = 1; shell.main.runUntil(2250);
        shell.source.words[CompanionState.LIFE] = 8; shell.main.runUntil(2500);
        require(screen.told.subList(2, 4).equals(List.of("5:true:true:12:1", "5:true:true:8:1")), "each change once: " + screen.told);
        // The screen moves to another page: published once, within one look.
        screen.setPage(2);
        shell.main.runUntil(2750); shell.main.runUntil(3500);
        require(shell.source.pages.equals(List.of(1, 2)), "page published on change only: " + shell.source.pages);
        // The source goes quiet (an old sample): the screen is told there is no state.
        shell.source.words[CompanionState.AGE_MS] = CompanionState.MAX_AGE_MS + 1;
        shell.main.runUntil(3750);
        require(screen.told.get(screen.told.size() - 1).equals("0:false:false:-1:-1"), "a stale sample is no state");
        // The screen goes away: the pump stops, and the page is unpublished.
        int reads = shell.source.reads, told = screen.told.size();
        shell.screen(null);
        shell.main.runUntil(10_000);
        require(shell.source.reads == reads && screen.told.size() == told && shell.main.queue.isEmpty(), "no screen: no looks");
        require(shell.source.pages.equals(List.of(1, 2, -1)), "no screen: no page");
        // A screen without pages of its own (Breath of the Wild) publishes none.
        Screen paged = new Screen(); paged.page = -1;
        shell.screen(paged); shell.main.runUntil(11_000);
        require(shell.source.pages.equals(List.of(1, 2, -1)), "nothing to publish");
        require(paged.told.size() == 1, "told the state once");
        shell.closed = true; reads = shell.source.reads;
        shell.main.runUntil(20_000);
        require(shell.source.reads == reads, "a closed shell stops looking");
        System.out.println("pass: companion shell pump");
    }
}
'''


def section(text, start, end):
    a = text.index(start)
    return text[a:text.index(end, a)]


class CompanionScreenTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.javac, cls.java = shutil.which('javac'), shutil.which('java')
        if not cls.javac or not cls.java:
            raise unittest.SkipTest('a JDK is required')
        cls.temp = tempfile.TemporaryDirectory(prefix='totk-screen-')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.work = Path(cls.temp.name)

    def write(self, folder, files):
        paths = []
        for name, text in files.items():
            path = self.work / folder / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
            paths.append(path)
        return paths

    def build(self, name, sources):
        classes = self.work / (name + '-classes')
        done = subprocess.run([self.javac, '--release', '17', '-Xlint:-options', '-d', str(classes), *map(str, sources)],
                              capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, done.stderr)
        return classes

    def run_probe(self, classes, main, *args):
        done = subprocess.run([self.java, '-cp', str(classes), main, *args], capture_output=True, text=True, timeout=60)
        self.assertEqual(done.returncode, 0, done.stderr)
        return done.stdout

    def test_interface_is_the_ports_own(self):
        text = (JAVA / 'CompanionScreen.java').read_text()
        for method in ('View view();', 'void setPage(int page);', 'int page();', 'void onGameState(CompanionState state);'):
            self.assertIn(method, text)
        controller = (JAVA / 'PresentationController.java').read_text()
        self.assertIn('CompanionScreen created = CompanionContent.create(getContext(), source, initialPage);', controller)
        self.assertIn('setContentView(created.view());', controller)
        self.assertNotIn('CompanionView', controller)      # the shell knows the interface, not a game's screen
        self.assertNotIn('DuoSurfaceView', controller)

    def screen_classes(self, game, companion=None):
        """Both screens and the one CompanionContent, compiled with this game's generated GameBuild (with another
        companion value when given: the MIT page is chosen by no game today, but stays compiled and working)."""
        shared = [JAVA / n for n in ('CompanionScreen.java', 'CompanionState.java', 'DataSource.java', 'ItemActions.java', 'Motion.java',
                                     'WarriorsCompanionModel.java', 'WarriorsCompanionDraw.java', 'WarriorsCompanionView.java',
                                     'CompanionView.java', 'DuoSurfaceView.java', 'CompanionContent.java')]
        build = games.java(game)
        if companion:
            current = f'COMPANION = "{games.values(game)["companion"]}"'
            self.assertIn(current, build)
            build = build.replace(current, f'COMPANION = "{companion}"')
        name = game + (f'-{companion}' if companion else '')
        sources = self.write(name, {**ANDROID_STUBS, **TOTK_STUBS, 'com/aure/totk/companion/ScreenProbe.java': SCREEN_PROBE,
                                    'com/aure/totk/setup/GameBuild.java': build})
        return self.build(name, sources + shared)

    def test_totk_screen_is_the_eden_duo_page(self):
        classes = self.screen_classes('totk')
        self.assertIn('pass: companion screen totk duo', self.run_probe(classes, 'com.aure.totk.companion.ScreenProbe', 'totk', 'duo'))

    def test_mit_screen_page_round_trip(self):
        # CompanionView, kept compiled but unused (games.toml companion = "duo" for both games), still works behind the
        # interface when a build names it.
        classes = self.screen_classes('totk', companion='map')
        self.assertIn('pass: companion screen totk map', self.run_probe(classes, 'com.aure.totk.companion.ScreenProbe', 'totk', 'map'))

    def test_totk_ready_rules(self):
        classes = self.screen_classes('totk')
        probe = self.write('totk-ready', {'com/aure/totk/companion/ReadyProbe.java': READY_PROBE})
        done = subprocess.run([self.javac, '--release', '17', '-Xlint:-options', '-cp', str(classes), '-d', str(classes), *map(str, probe)],
                              capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertIn('pass: ready rules', self.run_probe(classes, 'com.aure.totk.companion.ReadyProbe'))

    def test_hwaoc_screen_is_warriors_view(self):
        classes = self.screen_classes('hwaoc')
        self.assertIn('pass: companion screen hwaoc warriors', self.run_probe(classes, 'com.aure.totk.companion.ScreenProbe', 'hwaoc', 'warriors'))
        self.assertIn('CYCLE RT', (JAVA / 'WarriorsCompanionDraw.java').read_text())
        self.assertNotIn('PixelCopy', (JAVA / 'WarriorsAssetHarvester.java').read_text())

    def test_botw_screen_keeps_no_page_and_needs_no_library(self):
        # No libduo_host.so on the host: the class still loads and the screen still stands behind the interface.
        classes = self.screen_classes('botw')
        self.assertIn('pass: companion screen botw duo', self.run_probe(classes, 'com.aure.totk.companion.ScreenProbe', 'botw', 'duo'))

    def test_screen_is_chosen_by_the_game_variable(self):
        content = (JAVA / 'CompanionContent.java').read_text()
        self.assertIn('GameBuild.COMPANION.equals("duo")', content)
        self.assertIn('new CompanionView(context, source)', content)   # the MIT page stays compiled
        self.assertEqual({games.values(g)['companion'] for g in ('totk', 'botw')}, {'duo'})
        self.assertFalse((ANDROID / 'java-botw').exists())   # no per-game file overrides

    def test_shell_publishes_the_page_and_tells_the_screen(self):
        controller = (JAVA / 'PresentationController.java').read_text()
        pump = section(controller, '    // ---- the screen shown (CompanionScreen', '    /** These presentation displays')
        view = {'android/view/View.java': 'package android.view; public class View { }'}
        sources = self.write('pump', {**view, 'com/aure/totk/companion/PumpProbe.java': PUMP_PRELUDE + pump + PUMP_TESTS})
        classes = self.build('pump', sources + [JAVA / n for n in ('CompanionScreen.java', 'CompanionState.java', 'DataSource.java')])
        self.assertIn('pass: companion shell pump', self.run_probe(classes, 'com.aure.totk.companion.PumpProbe'))


class DuoTouchDefault(unittest.TestCase):
    """Touch on the Eden Duo second screen is on by default for both games: a release build's game data is private, so
    totk.env cannot be edited there and the default is what players get. TOTK_DUO_TOUCH=0 is the one way to turn it off."""

    def test_touch_is_on_unless_turned_off(self):
        host = (ROOT / 'games/totk/duo/duo_host.cpp').read_text()
        self.assertIn('static const bool enabled = !EnvIs("TOTK_DUO_TOUCH", "0");', host)
        self.assertNotIn('EnvIs("TOTK_DUO_TOUCH", "1")', host)
        # one entry point for both games: no per-game branch decides it
        entry = host[host.index('DuoSurfaceView_nativeTouch'):]
        entry = entry[:entry.index('\n}\n')]
        self.assertNotIn('GAME_BOTW', entry)
        self.assertNotIn('title_id', entry)

    def test_docs_say_on_by_default(self):
        for doc in ('docs/developer-settings.md', 'docs/companion.md', 'docs/companion-waist.md', 'games/totk/android/APP.md'):
            text = (ROOT / doc).read_text()
            self.assertIn('TOTK_DUO_TOUCH=0', text, doc)
            self.assertNotIn('TOTK_DUO_TOUCH=1', text, doc)


class DuoMapTurn(unittest.TestCase):
    """The second screen's minimap (owner, 2026-11-17): both games north up by default, one preference common to both
    (the menu's Second screen map, settings.env TOTK_DUO_MAP_ROTATE=1, and each package's own settings row, flag
    map_rotate). The module decides the turn and publishes it under the names the minimap, the arrow and the N badge
    already bind, so no widget changes its binds and no widget index moves (the TotK generator addresses BotW's by
    index)."""
    VENDOR = ROOT / 'games/totk/duo/vendor/companions'
    PACKAGES = {'botw': 'BreathOfTheWild', 'totk': 'TearsOfTheKingdom'}

    def manifest(self, game):
        import json
        return json.loads((self.VENDOR / 'packages' / self.PACKAGES[game] / 'dualscreen/manifest.json').read_text())

    def test_both_packages_default_to_north_up(self):
        for game, turn in (('botw', 'Rotate with camera'), ('totk', 'Rotate with Link')):
            with self.subTest(game=game):
                m = self.manifest(game)
                self.assertEqual(m['settings'][0], {'flag': 'map_rotate', 'label': 'Minimap', 'type': 'toggle',
                                                    'choices': ['North up', turn], 'default': 0})
                self.assertEqual([r['flag'] for r in m['settings']], ['map_rotate', 'left_mode'])   # + the left panel
                self.assertNotIn('map_rotate', m.get('flags', {}))      # a "flags" value would win over the default
                self.assertGreaterEqual(m['min_runtime'], 17)           # settings rows: runtime 17
                self.assertEqual(m['actions']['open_settings']['page'], '@settings')
                ready = next(p for p in m['pages'] if p['id'] == 'ready')['widgets']
                holds = [w for w in ready if w.get('on_hold') == 'open_settings']
                # holding the minimap opens it (and holding the left pager: its two swipe areas, the Left panel row)
                self.assertEqual(sorted(w['rect'] for w in holds), [[16, 232, 184, 468]] * 2 + [[220, 64, 800, 800]])

    def test_the_turn_is_bound_through_the_module_values(self):
        for game in ('botw', 'totk'):
            with self.subTest(game=game):
                ready = next(p for p in self.manifest(game)['pages'] if p['id'] == 'ready')['widgets']
                by_id = {w['id']: w for w in ready if 'id' in w}
                minimap = by_id['ready_map' if game == 'botw' else 'totk_minimap']
                self.assertEqual(minimap['view_rotate_bind'], f'{game}.map.view.rotation')
                self.assertEqual(minimap['marker_rotate_bind'], f'{game}.map.view.heading')
                for arrow in ('pd_ready_player_arrow_shadow', 'pg_fix_ready_player_arrow'):
                    self.assertEqual(by_id[arrow]['rotate_bind'], f'{game}.map.view.heading')
                # the N badge orbits the disc's centre with the map: north up = at the top
                for badge in ('pg_ready_north_ring', 'pg_ready_north_disc'):
                    self.assertEqual(by_id[badge]['rotate_bind'], f'{game}.map.view.rotation')
                    self.assertAlmostEqual(by_id[badge]['rect'][1] + by_id[badge]['pivot'][1], 64 + 400, delta=2)
                # the full Map page is north up in both games
                for page in self.manifest(game)['pages']:
                    if page['id'] != 'ready':
                        self.assertFalse([w for w in page['widgets'] if 'view_rotate_bind' in w], page['id'])
        # 990 upstream widgets, then the Champion powers switches appended (test_companion_quests.LeftPanel)
        self.assertEqual(len(next(p for p in self.manifest('botw')['pages'] if p['id'] == 'ready')['widgets']), 1034)

    def test_the_modules_turn_only_on_the_flag(self):
        modules = self.VENDOR / 'native/modules'
        view = (modules / 'botw_map_view.h').read_text()
        self.assertIn('return h.get_i64 && h.get_i64(h.userdata,"@flag:map_rotate",0)==1;', view)
        self.assertIn('if(rotate){out.map_rz=yaw;if(v.cone_valid)out.cone_rz=0.f;}', view)
        self.assertIn('else{out.map_rz=0.f;if(v.cone_valid)out.cone_rz=-yaw;}', view)
        botw = (modules / '01007EF00011E000.cpp').read_text()
        # both reads of the view (the 100 ms pass and the 30 Hz camera pass) go through the choice; nothing else reads it
        self.assertEqual(botw.count('botw::prefer_map_view(botw::map_view(*h,s.view.cone_pane),botw::map_rotate_chosen(*h))'), 2)
        self.assertEqual(botw.count('botw::map_view(*h,'), 2)
        totk = (modules / '0100F2C0115B6000.cpp').read_text()
        self.assertIn('const double turn = Get("@flag:map_rotate", 0) == 1 ? Turn(-heading) : 0.0;', totk)
        self.assertIn('F64("totk.map.view.rotation", turn);', totk)

    def test_the_host_applies_the_menu_at_start(self):
        host = (ROOT / 'games/totk/duo/duo_host.cpp').read_text()
        # one table of the menu's options (generalised from the minimap's): flag, settings.env line, its value
        self.assertIn('{"map_rotate", "TOTK_DUO_MAP_ROTATE", "1", "Second screen map",', host)
        self.assertIn('{"left_mode", "TOTK_DUO_LEFT_PANEL", "powers", "Left panel",', host)
        self.assertIn('const bool set = EnvIs(pref.env, pref.set);', host)
        self.assertIn('manifest.flag_defaults.insert_or_assign(flag, set ? 1 : 0);', host)
        self.assertIn('std::erase(manifest.persist_flags, flag);', host)   # the menu wins over a persisted value
        start = host[host.index('bool StartRuntime() {'):]
        self.assertLess(start.index('ApplyMenuPreferences(*manifest);'), start.index('g_runtime.emplace('))
        self.assertNotIn('ApplyMapPreference', host)
        entry = host[host.index('void ApplyMenuPreferences('):host.index('bool StartRuntime() {')]
        self.assertNotIn('title_id', entry)   # one rule for both games

    def test_the_settings_page_says_a_change_there_is_for_this_game(self):
        # In the apps the menu decides map_rotate at every start, so the companion's own settings row (hold the minimap)
        # only lasts until the game closes. The host adds a note under that row saying so and naming the menu option.
        host = (ROOT / 'games/totk/duo/duo_host.cpp').read_text()
        entry = host[host.index('void ApplyMenuPreferences('):host.index('bool StartRuntime() {')]
        text = host[host.index('std::string MenuNoteText('):host.index('void ApplyMenuPreferences(')]
        self.assertIn('until the game closes', text)
        self.assertIn("in the app's Graphics menu.", text)
        menu = (ROOT / 'games/totk/android/java/com/aure/totk/setup/Strings.java').read_text()
        self.assertIn('case "duo_map": return "Second screen map";', menu)
        self.assertIn('case "duo_left": return "Left panel";', menu)
        self.assertTrue(text.isascii())
        # one note under the lowest row, naming every menu row the host applied (two rows: no note covers a row)
        self.assertIn('it->rect[1] > row->rect[1]', entry)
        self.assertIn('note.text = MenuNoteText(menus);', entry)
        # The ids the runtime gives the generated row and value (mod_settings.cpp) are the ones the host looks for.
        settings = (ROOT / 'games/totk/duo/vendor/emu/src/core/mods/mod_settings.cpp').read_text()
        self.assertIn('row.id = std::format("@settings.row.{}", def.flag);', settings)
        self.assertIn('value.id = std::format("@settings.value.{}", def.flag);', settings)
        self.assertIn('"@settings.row."', entry)
        self.assertIn('"@settings.value." + flag', entry)
        self.assertIn('Core::Mods::SettingsPageId', entry)
        self.assertIn('note.wrap_width', entry)
        self.assertLess(entry.index('std::erase(manifest.persist_flags, flag);'), entry.index('widgets.push_back('))
        readme = (ROOT / 'games/totk/duo/vendor/companions/packages/TearsOfTheKingdom/README.md').read_text()
        self.assertIn('unless the host decides it', readme)
        self.assertIn('until the game closes', readme)

    def test_docs_name_the_setting(self):
        for doc in ('docs/developer-settings.md', 'docs/companion.md', 'games/totk/android/APP.md',
                    'games/totk/android/BOTW.md'):
            self.assertIn('Second screen map', (ROOT / doc).read_text(), doc)
            self.assertIn('Left panel', (ROOT / doc).read_text(), doc)
        self.assertIn('TOTK_DUO_MAP_ROTATE=1', (ROOT / 'docs/developer-settings.md').read_text())
        self.assertIn('TOTK_DUO_LEFT_PANEL=powers', (ROOT / 'docs/developer-settings.md').read_text())
        local = (ROOT / 'games/totk/duo/vendor/README.md').read_text()
        local = local[local.index('## Local changes'):]
        for name in ('botw_map_view.h', '01007EF00011E000.cpp', 'BreathOfTheWild/dualscreen/manifest.json',
                     'botw_champion_toggle.h', 'botw_ready_panes.h', '01007EF00011E000_actions.h'):
            self.assertIn(name, local)
        notice = (ROOT / 'games/totk/duo/NOTICE.txt').read_text()
        self.assertIn('map_rotate', notice)
        self.assertIn('champion_toggle', notice)
        self.assertIn('champion_toggle', local)


if __name__ == '__main__':
    unittest.main()
