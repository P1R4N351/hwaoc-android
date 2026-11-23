package com.aure.totk.companion;

/** All methods run on the poller thread; enabled is also queried before starting it. Preview fixtures stay enabled. */
public interface DataSource {
    default boolean enabled() { return true; }
    String read(int detail) throws Exception;
    boolean send(int kind, int arg);
    /** Item action request: seq > 0, or -1 busy, -2 refused now, -3 unsupported, -4 invalid. */
    default int submit(String action, String category, int slot, String actor, int partition, long pouchGen) { return -3; }
    /** {"seq":n,"state":"pending|done|refused|failed","reason":"…"}, or null when unknown. */
    default String outcome(int seq) { return null; }
    /** The common game state as CompanionState.FIELDS words (CompanionState.set reads them); false when there is none
     *  to read. Any thread: the shell and the stick-LED bridge call it from their own. Fixtures have none. */
    default boolean state(long[] out) { return false; }
    /** The page the screen shows now, for that state's page field. Any thread. */
    default void page(int page) { }
    /** Detailed Warriors companion snapshot (ints[32], flts[416]); null when unavailable. */
    default String warriorsDetail(int[] ints, float[] flts) { return null; }
    default boolean warriorsFire(int actionId) { return false; }
    default void warriorsAbort() { }
    default int warriorsPin(int op, int slot, String label) { return -1; }
}
