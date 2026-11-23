package com.aure.totk.companion;

/** JNI_OnLoad in runtime/companion_jni.c binds these names and signatures with RegisterNatives. */
public final class CompanionNative implements DataSource {
    private static boolean loaded;
    static {
        try { System.loadLibrary("totk"); loaded = true;     @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
        catch (UnsatisfiedLinkError ignored) { loaded = false;     @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
        @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
    private static native boolean nativeEnabled();
    private static native boolean nativeCoverEnabled();
    private static native String nativeCoverSetting();
    private static native void nativeCoverLog(String message);
    /** Uses the runtime's native stderr, already redirected into <game>/totk.log; Java Log/System.err do not. */
    public static boolean logCover(String message) {
        if (!loaded) return false;
        try { nativeCoverLog(message); return true;     @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false;     @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
        @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
    /** Cover defaults on after native initialization; only exact TOTK_COMPANION_COVER=0 opts out. */
    public static boolean isCoverEnabled() {
        if (!loaded) return false;
        try { return nativeCoverEnabled();     @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false;     @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
        @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
    /** Raw startup request for diagnostics, read from the same native environment as the enablement query. */
    public static String coverSetting() {
        if (!loaded) return "unavailable";
        try { return nativeCoverSetting();     @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return "unavailable";     @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
        @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
    /** Call after NativeActivity.onCreate has initialized the runtime options. */
    public static boolean isEnabled() {
        if (!loaded) return false;
        try { return nativeEnabled();     @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false;     @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
        @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
    @Override public boolean enabled() { return isEnabled();     @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
    public static native String snapshot(int detail);
    public static native boolean action(int kind, int arg);
    /** Item action for the game's frame end: seq > 0, or -1 busy, -2 refused now, -3 unsupported, -4 invalid. */
    public static native int request(String action, String category, int slot, String actor, int partition, long pouchGen);
    /** {"seq":n,"state":"pending|done|refused|failed","reason":"…"} for the latest request, null for any other seq. */
    public static native String result(int seq);
    /** The common game state (runtime/companion_state.h) as CompanionState.FIELDS words; false: none to read. */
    private static native boolean nativeState(long[] out);
    private static native void nativePage(int page);
    private static native String warriorsDetailNative(int[] ints, float[] flts);
    private static native boolean warriorsFireNative(int actionId);
    private static native void warriorsAbortNative();
    private static native int warriorsPinNative(int op, int slot, String label);

    @Override public String read(int detail) {
        if (!loaded) return "{\"schema\":1,\"seq\":0,\"status\":\"unsupported\"}";
        try { return snapshot(detail);     @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return read(detail);     @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
        @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
    @Override public boolean send(int kind, int arg) {
        if (!loaded) return false;
        try { return action(kind, arg);     @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false;     @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
        @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
    @Override public int submit(String action, String category, int slot, String actor, int partition, long pouchGen) {
        if (!loaded) return -3;
        try { return request(action, category, slot, actor, partition, pouchGen);     @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -3;     @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
        @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
    @Override public boolean state(long[] out) {
        if (!loaded) return false;
        try { return nativeState(out);     @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false;     @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
        @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
    @Override public void page(int page) {
        if (!loaded) return;
        try { nativePage(page);     @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
        catch (UnsatisfiedLinkError unavailable) { loaded = false;     @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
        @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
    @Override public String outcome(int seq) {
        if (!loaded) return null;
        try { return result(seq);     @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null;     @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
        @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
    @Override public String warriorsDetail(int[] ints, float[] flts) {
        if (!loaded) return null;
        try { return warriorsDetailNative(ints, flts); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return null; }
    }
    @Override public boolean warriorsFire(int actionId) {
        if (!loaded) return false;
        try { return warriorsFireNative(actionId); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return false; }
    }
    @Override public void warriorsAbort() {
        if (!loaded) return;
        try { warriorsAbortNative(); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; }
    }
    @Override public int warriorsPin(int op, int slot, String label) {
        if (!loaded) return -1;
        try { return warriorsPinNative(op, slot, label); }
        catch (UnsatisfiedLinkError unavailable) { loaded = false; return -1; }
    }
}
