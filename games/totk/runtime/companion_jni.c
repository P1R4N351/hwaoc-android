/* JNI side of the dual-screen companion (Android app only): the Java companion
   (android/java/com/aure/totk/companion/CompanionNative.java) loads libtotk.so with System.loadLibrary, which runs
   JNI_OnLoad below; NativeActivity has already loaded the same library, so both share one runtime. Its drawings come
   from <game>/companion/, which the app's setup generates from the player's RomFS (Java AssetStore).
     boolean nativeEnabled()           -> TOTK_COMPANION != "0", queried after NativeActivity has read totk.env
     boolean nativeCoverEnabled()      -> TOTK_COMPANION_COVER != "0" with companion enabled (default on)
     String nativeCoverSetting()       -> raw requested cover option, or "unset", for the policy log
     void nativeCoverLog(String message)-> native stderr / totk.log (also available when the companion is off)
     String snapshot(int detail)        -> companion_snapshot() JSON for the running game
     boolean action(int kind, int arg)  -> kind 1: press and release Switch button `arg` (libnx Npad bit index)
     int request(String action, String category, int slot, String actor, int partition, long pouchGen)
                                        -> companion_request(): an item action for the game's frame end (seq > 0, or
                                           -1 busy, -2 refused now, -3 unsupported / disabled, -4 invalid)
     String result(int seq)             -> companion_result() JSON, null for an unknown seq
     boolean nativeState(long[] out)    -> the common second-screen state (companion_state.h): COMPANION_STATE_FIELDS
                                           words, false when there is none. The Eden Duo companion host's last sample
                                           (duo_port.c) when it has a fresh one; before that, without the host
                                           (TOTK_DUO=0, no libduo_host.so) or when its sample is older than
                                           DUO_STATE_MAX_AGE_MS (the host stopped), Tears of the Kingdom reads it now
                                           with its own reader (companion_state()), Breath of the Wild has none. The one getter the stick
                                           LEDs and the shell both use.
     void nativePage(int page)          -> the page the Java screen shows, for that state's page field
   The same item actions also serve the Eden Duo companion page: runtime_companion_item_* below, which duo_port.c hands
   to libduo_host.so (the DuoPort table, ABI 3) for Tears of the Kingdom's title module. They install the frame-end hook
   themselves, so the requests run whether or not the Java companion class was ever loaded.
   With TOTK_COMPANION=0 every game-facing entry point returns its "off" value without inspecting the game. */
#include "companion.h"
#include "autoload.h"
#include "host_os.h"
#include "runtime.h"
#include "world_survey.h"
#include "android_pause.h"
#include "duo_link.h"
#include <jni.h>
#if defined(__ANDROID__)
#include <android/log.h>
#endif
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
void runtime_android_present_transform_hint(uint64_t epoch, int hint);
static void JNICALL native_present_transform_hint(JNIEnv *env, jclass cls, jlong epoch, jint hint) {
    (void)env; (void)cls;
    runtime_android_present_transform_hint((uint64_t)epoch,hint);
}
void runtime_android_text_done(JNIEnv *env, jclass cls, jlong serial, jbyteArray utf8);   /* frontend_android.c: swkbd */
void runtime_android_game_focus(JNIEnv *env, jclass cls, jboolean focused, jboolean single_display);
void runtime_android_game_input_focus(JNIEnv *env, jclass cls, jboolean top);
/* feedback_android.c: the game's rumble and six-axis sensor */
void runtime_android_motion(JNIEnv *env, jclass cls, jfloat ax, jfloat ay, jfloat az, jfloat gx, jfloat gy, jfloat gz);
void runtime_android_motion_live(JNIEnv *env, jclass cls, jboolean on, jstring source);
jint runtime_android_feedback_enabled(JNIEnv *env, jclass cls);
#if defined(__GNUC__)
extern int vk_overlay_set_mode(int mode) __attribute__((weak));
extern int vk_minimap_read(uint8_t *rgba_out, int max_bytes, int32_t meta[5]) __attribute__((weak));
extern int vk_minimap_cycle(void) __attribute__((weak));
extern void vk_minimap_enable(int on) __attribute__((weak));
extern void vk_minimap_stream_mode(int stream, int mode) __attribute__((weak));
extern int vk_minimap_objective_read(uint8_t *rgba_out, int max_bytes, int32_t meta[4]) __attribute__((weak));
extern int vk_minimap_lhud_read(uint8_t *rgba_out, int max_bytes, int32_t meta[4]) __attribute__((weak));
extern int vk_minimap_ui_tex_read(uint8_t *rgba_out, int max_bytes, int32_t meta[4], int request) __attribute__((weak));
#else
extern int vk_overlay_set_mode(int mode);
extern int vk_minimap_read(uint8_t *rgba_out, int max_bytes, int32_t meta[5]);
extern int vk_minimap_cycle(void);
extern void vk_minimap_enable(int on);
extern void vk_minimap_stream_mode(int stream, int mode);
extern int vk_minimap_objective_read(uint8_t *rgba_out, int max_bytes, int32_t meta[4]);
extern int vk_minimap_lhud_read(uint8_t *rgba_out, int max_bytes, int32_t meta[4]);
extern int vk_minimap_ui_tex_read(uint8_t *rgba_out, int max_bytes, int32_t meta[4], int request);
#endif

/* Do not cache this in JNI_OnLoad: NativeActivity's start_game reads totk.env
   synchronously during super.onCreate, before Java constructs its controller. */
static jboolean JNICALL native_enabled(JNIEnv *env, jclass cls) {
    (void)env; (void)cls;
    const char *option = getenv("TOTK_COMPANION");
    return !option || strcmp(option, "0") ? JNI_TRUE : JNI_FALSE;
}
static jboolean JNICALL native_cover_enabled(JNIEnv *env, jclass cls) {
    const char *option = getenv("TOTK_COMPANION_COVER");
    return native_enabled(env, cls) && (!option || strcmp(option, "0")) ? JNI_TRUE : JNI_FALSE;
}
static jstring JNICALL native_cover_setting(JNIEnv *env, jclass cls) {
    (void)cls;
    const char *option = getenv("TOTK_COMPANION_COVER");
    return (*env)->NewStringUTF(env, option ? option : "unset");
}
/* Same sink as all native runtime diagnostics: frontend_android.c log_start redirects fd 2 to totk.log.
   Do not gate diagnostics on native_enabled: the one cover-off policy line must remain observable too. */
static void JNICALL native_cover_log(JNIEnv *env, jclass cls, jstring message) {
    (void)cls;
    if (!message) return;
    const char *text = (*env)->GetStringUTFChars(env, message, NULL);
    if (!text) return;
#if defined(__ANDROID__)
    if (!strncmp(text, "cover requested=", 16)) {
        fprintf(stderr, "[TotkCompanion] %s logcat_min_priority=%d logcat_info=%d logcat_warn=%d\n", text,
                __android_log_get_minimum_priority(),
                __android_log_is_loggable(ANDROID_LOG_INFO, "TotkCompanion", ANDROID_LOG_INFO),
                __android_log_is_loggable(ANDROID_LOG_WARN, "TotkCompanion", ANDROID_LOG_INFO));
    } else
#endif
    fprintf(stderr, "[TotkCompanion] %s\n", text);
    (*env)->ReleaseStringUTFChars(env, message, text);
}

static int guest_read(uint64_t a, void *out, size_t n) { return host_safe_read(a, out, n); }

/* ---------- item actions: the host side of companion_actions.c ---------- */
/* Game code is native here: a guest function is called like a host one, on the frame-end thread only (the same way
   world_survey.c calls forceSetMatrix). */
static uint64_t game_call(uint64_t fn, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    return ((uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t))(uintptr_t)fn)(a0, a1, a2, a3, a4, a5);
}
static float game_call_f(uint64_t fn, uint64_t a0, float s0) { return ((float (*)(uint64_t, float))(uintptr_t)fn)(a0, s0); }
/* Only the battery fields the pause menu's own energy branch writes, on the thread that called the game for them. */
static int game_write(uint64_t a, const void *src, size_t n) {
    if (!a) return 0;
    memcpy((void *)(uintptr_t)a, src, n);
    return 1;
}
static uint64_t now_ms(void) { return runtime_now_ns() / 1000000u; }
static void set_pending(int on) { atomic_store_explicit(&runtime_companion_pending, on, memory_order_release); }
static void frame_end_tick(uint64_t frame) { companion_tick((uintptr_t)runtime_module_base("main"), frame); }
static void actions_init(void) {   /* once: JNI_OnLoad or the first Eden Duo call, before any snapshot or request */
    static const CompanionEnv env = {guest_read, game_call, game_call_f, game_write, runtime_fs_save_busy, now_ms, set_pending};
    companion_actions_init(&env);
    atomic_store_explicit(&runtime_companion_tick, frame_end_tick, memory_order_release);
}
static pthread_once_t actions_once = PTHREAD_ONCE_INIT;
static void actions_ready(void) { pthread_once(&actions_once, actions_init); }

static jint JNICALL native_request(JNIEnv *env, jclass cls, jstring action, jstring category, jint slot, jstring actor,
                                   jint partition, jlong pouch_gen) {
    if (!native_enabled(env, cls)) return COMPANION_UNSUPPORTED;
    if (runtime_android_game_paused()) return COMPANION_BUSY;
    if (!action || !category || !actor) return COMPANION_INVALID;
    const char *a = (*env)->GetStringUTFChars(env, action, NULL), *c = (*env)->GetStringUTFChars(env, category, NULL),
               *n = (*env)->GetStringUTFChars(env, actor, NULL);
    jint r = a && c && n ? companion_request((uintptr_t)runtime_module_base("main"), a, c, slot, n, partition, pouch_gen)
                         : COMPANION_INVALID;
    if (a) (*env)->ReleaseStringUTFChars(env, action, a);
    if (c) (*env)->ReleaseStringUTFChars(env, category, c);
    if (n) (*env)->ReleaseStringUTFChars(env, actor, n);
    return r;
}
static jstring JNICALL native_result(JNIEnv *env, jclass cls, jint seq) {
    if (!native_enabled(env, cls)) return NULL;
    CompanionBuf b = {0};
    jstring r = companion_result(seq, &b) ? (*env)->NewStringUTF(env, b.data) : NULL;
    free(b.data);
    return r;
}

/* ---------- the Eden Duo companion's item actions (duo_port.c: DuoPort ABI 3) ----------
   The route CompanionNative.request / result take, for the title module of the Eden Duo page: the same rules
   (companion_item_actions on a fresh view of the item), the same gate (TOTK_COMPANION, the paused game), the same
   single request at a time, carried out by the same frame-end tick. Called from the companion host's threads. */
unsigned runtime_companion_item_actions(int key, int slot, CompanionAction out[3]);
void runtime_companion_action_state(int *ok, const char **reason);
long long runtime_companion_pouch_gen(void);
int runtime_companion_item_request(const char *action, const char *category, int slot, const char *actor, int partition,
                                   long long pouch_gen);
size_t runtime_companion_item_result(int seq, char *out, size_t cap);

unsigned runtime_companion_item_actions(int key, int slot, CompanionAction out[3]) {
    if (!native_enabled(NULL, NULL)) return 0;
    actions_ready();
    if (key == COMPANION_SAGES) return companion_sage_actions(slot, out);   /* the Sage powers switches; slot = the sage */
    CompanionView v;
    if (!companion_view((uintptr_t)runtime_module_base("main"), guest_read, key, slot, &v)) return 0;
    return companion_item_actions(key, &v, out);   /* also keeps the frame-end refresh running (watch) */
}
void runtime_companion_action_state(int *ok, const char **reason) {
    *ok = 0; *reason = "unsupported";
    if (!native_enabled(NULL, NULL)) return;
    actions_ready();
    int pause_menu, aiming;
    companion_action_state(ok, reason, &pause_menu, &aiming);
}
long long runtime_companion_pouch_gen(void) {
    if (!native_enabled(NULL, NULL)) return 0;
    CompanionView v;
    companion_view((uintptr_t)runtime_module_base("main"), guest_read, -1, 0, &v);
    return v.ident ? companion_pouch_gen(v.ident) : 0;
}
int runtime_companion_item_request(const char *action, const char *category, int slot, const char *actor, int partition,
                                   long long pouch_gen) {
    if (!native_enabled(NULL, NULL)) return COMPANION_UNSUPPORTED;
    if (runtime_android_game_paused()) return COMPANION_BUSY;
    if (!action || !category || !actor) return COMPANION_INVALID;
    actions_ready();
    return companion_request((uintptr_t)runtime_module_base("main"), action, category, slot, actor, partition, pouch_gen);
}
size_t runtime_companion_item_result(int seq, char *out, size_t cap) {
    if (!native_enabled(NULL, NULL) || !out || !cap) return 0;
    CompanionBuf b = {0};
    size_t n = companion_result(seq, &b);
    if (n && n < cap) memcpy(out, b.data, n + 1);
    else n = 0;
    free(b.data);
    return n;
}

/* TOTK_COMPANION_LOG=1: log every status change and, once a save is loaded, one full snapshot (for checking a new
   device or game build without the second screen). */
static void log_snapshot(const char *json, int detail) {
    static int on = -1; static char last[16]; static _Atomic int full_logged;
    if (on < 0) { const char *e = getenv("TOTK_COMPANION_LOG"); on = e && *e && strcmp(e, "0"); }
    if (!on) return;
    const char *s = strstr(json, "\"status\":\"");
    char status[16] = {0};
    if (s) { s += 10; size_t n = strcspn(s, "\""); if (n < sizeof status) memcpy(status, s, n); }
    if (strcmp(status, last)) { fprintf(stderr, "[companion] status %s\n", status); snprintf(last, sizeof last, "%s", status); }
    if (detail == COMPANION_FULL && !strcmp(status, "ready") && !atomic_exchange(&full_logged, 1))
        fprintf(stderr, "[companion] snapshot %s\n", json);
}

static jstring JNICALL native_snapshot(JNIEnv *env, jclass cls, jint detail) {
    if (!native_enabled(env, cls)) return NULL;
    CompanionBuf b = {0};
    jstring r = NULL;
    if (companion_snapshot((uintptr_t)runtime_module_base("main"), detail == COMPANION_FULL ? COMPANION_FULL : COMPANION_LIVE,
                           guest_read, &b)) {
        log_snapshot(b.data, detail);
        r = (*env)->NewStringUTF(env, b.data);   /* the JSON is ASCII: names are identifiers and other text is escaped */
    }
    free(b.data);
    return r;
}

/* The common state. The page is the Java screen's unless the game side filled one in. */

__attribute__((weak)) int hwaoc_companion_state(CompanionState *out, int64_t *age_ms);
__attribute__((weak)) void hwaoc_companion_page(int page);
__attribute__((weak)) int hwaoc_companion_detail(int32_t *ints, int max_ints, float *flts, int max_flts, char *txt, int max_txt);
__attribute__((weak)) int hwaoc_companion_fire(int action_id);
__attribute__((weak)) void hwaoc_companion_abort(void);
__attribute__((weak)) int hwaoc_companion_pin(int op, int slot, const char *label);
static _Atomic int shown_page = -1;
static void JNICALL native_page(JNIEnv *env, jclass cls, jint page) {
    (void)env; (void)cls;
    atomic_store(&shown_page, page);
    if (hwaoc_companion_page) hwaoc_companion_page(page);
}
static jboolean JNICALL native_state(JNIEnv *env, jclass cls, jlongArray out) {
    if (!out || (*env)->GetArrayLength(env, out) < COMPANION_STATE_FIELDS || !native_enabled(env, cls)) return JNI_FALSE;
    CompanionState s; int64_t age = 0, words[COMPANION_STATE_FIELDS];
    if (hwaoc_companion_state && hwaoc_companion_state(&s, &age)) {
        if (s.page < 0) s.page = atomic_load(&shown_page);
        companion_state_words(&s, age, words);
        jlong copy[COMPANION_STATE_FIELDS];
        for (int i = 0; i < COMPANION_STATE_FIELDS; i++) copy[i] = words[i];
        (*env)->SetLongArrayRegion(env, out, 0, COMPANION_STATE_FIELDS, copy);
        return JNI_TRUE;
    }
    int hosted = runtime_duo_state(&s, &age);
#if defined(GAME_BOTW) || defined(GAME_HWAOC)
    if (!hosted) return JNI_FALSE;   /* runtime has no companion reader for this game */
#else
    if (!hosted || age > DUO_STATE_MAX_AGE_MS) {   /* none yet, or the host stopped: the MIT reader, read now */
        companion_state((uintptr_t)runtime_module_base("main"), guest_read, &s);
        age = 0;
    }
#endif
    if (s.page < 0) s.page = atomic_load(&shown_page);
    companion_state_words(&s, age, words);
    jlong copy[COMPANION_STATE_FIELDS];
    for (int i = 0; i < COMPANION_STATE_FIELDS; i++) copy[i] = words[i];
    (*env)->SetLongArrayRegion(env, out, 0, COMPANION_STATE_FIELDS, copy);
    return JNI_TRUE;
}

/* The Mac key codes input.c maps to Switch buttons, by libnx Npad bit index (A, B, X, Y, LS, RS, L, R, ZL, ZR, Plus,
   Minus, Left, Up, Right, Down). The synthetic key is separate from the controller's own state (input.c ORs both), so a
   pulse never releases a button the player is holding. */
static const unsigned char BUTTON_KEY[16] = {38, 40, 34, 32, 6, 8, 12, 14, 58, 56, 36, 48, 3, 17, 4, 5};
static _Atomic int pulsing[16];   /* a tap while the same button's pulse runs joins it: one press, one release */
static void *release_later(void *arg) {
    unsigned bit = (unsigned)(uintptr_t)arg;
    struct timespec t = {0, 120 * 1000000L};   /* several game frames at 30 fps: the press is seen as held, then released */
    nanosleep(&t, NULL);
    runtime_input_key_synthetic(BUTTON_KEY[bit], 0);
    atomic_store(&pulsing[bit], 0);
    return NULL;
}
/* Only while a save is loaded (status "ready"): the shortcuts are buttons the player could press, so the game's own rules
   decide what they do in a menu, a cutscene or a loading screen. */
static jboolean JNICALL native_action(JNIEnv *env, jclass cls, jint kind, jint arg) {
    if (!native_enabled(env, cls) || kind != 1 || arg < 0 || arg >= 16) return JNI_FALSE;
    if (runtime_android_game_paused()) return JNI_FALSE;
    CompanionBuf b = {0};
    int ready = companion_snapshot((uintptr_t)runtime_module_base("main"), COMPANION_LIVE, guest_read, &b) &&
                strstr(b.data, "\"status\":\"ready\"") != NULL;
    free(b.data);
    if (!ready) return JNI_FALSE;
    int idle = 0;
    if (!atomic_compare_exchange_strong(&pulsing[arg], &idle, 1)) return JNI_TRUE;
    pthread_t t;
    runtime_input_key_synthetic(BUTTON_KEY[arg], 1);
    if (pthread_create(&t, NULL, release_later, (void *)(uintptr_t)arg)) {
        runtime_input_key_synthetic(BUTTON_KEY[arg], 0); atomic_store(&pulsing[arg], 0); return JNI_FALSE;
    }
    pthread_detach(t);
    fprintf(stderr, "[companion] button %d pressed from the second screen\n", (int)arg);
    return JNI_TRUE;
}


static jstring JNICALL native_warriors_detail(JNIEnv *env, jclass cls, jintArray ints, jfloatArray flts) {
    if (!native_enabled(env, cls) || !ints || !flts || !hwaoc_companion_detail) return NULL;
    if ((*env)->GetArrayLength(env, ints) < 32 || (*env)->GetArrayLength(env, flts) < 416) return NULL;
    jint ibuf[32] = {0}; jfloat fbuf[416] = {0}; char txt[512] = "";
    if (!hwaoc_companion_detail((int32_t *)ibuf, 32, (float *)fbuf, 416, txt, sizeof txt)) return NULL;
    (*env)->SetIntArrayRegion(env, ints, 0, 32, ibuf);
    (*env)->SetFloatArrayRegion(env, flts, 0, 416, fbuf);
    return (*env)->NewStringUTF(env, txt);
}
static jboolean JNICALL native_warriors_fire(JNIEnv *env, jclass cls, jint id) {
    return (native_enabled(env, cls) && hwaoc_companion_fire && hwaoc_companion_fire((int)id)) ? JNI_TRUE : JNI_FALSE;
}
static void JNICALL native_warriors_abort(JNIEnv *env, jclass cls) {
    (void)env; (void)cls; if (hwaoc_companion_abort) hwaoc_companion_abort();
}
static jint JNICALL native_warriors_pin(JNIEnv *env, jclass cls, jint op, jint slot, jstring label) {
    if (!native_enabled(env, cls) || !hwaoc_companion_pin) return -1;
    const char *s = label ? (*env)->GetStringUTFChars(env, label, NULL) : NULL;
    int r = hwaoc_companion_pin((int)op, (int)slot, s);
    if (s) (*env)->ReleaseStringUTFChars(env, label, s);
    return (jint)r;
}
static jint JNICALL native_overlay_set_mode(JNIEnv *env, jclass cls, jint mode) {
    (void)env; (void)cls;
    return vk_overlay_set_mode ? (jint)vk_overlay_set_mode((int)mode) : 0;
}
static jint JNICALL native_warriors_minimap(JNIEnv *env, jclass cls, jbyteArray rgbaOut, jintArray metaOut) {
    if (!native_enabled(env, cls) || !rgbaOut || !metaOut || !vk_minimap_read) return 0;
    if ((*env)->GetArrayLength(env, metaOut) < 5) return 0;
    jint jm[5] = {0};
    (*env)->GetIntArrayRegion(env, metaOut, 0, 5, jm);
    int32_t m[5] = {(int32_t)jm[0], (int32_t)jm[1], (int32_t)jm[2], (int32_t)jm[3], (int32_t)jm[4]};
    jsize cap = (*env)->GetArrayLength(env, rgbaOut);
    jbyte *buf = cap > 0 ? (*env)->GetByteArrayElements(env, rgbaOut, NULL) : NULL;
    int updated = buf ? vk_minimap_read((uint8_t *)buf, (int)cap, m) : 0;
    if (buf) (*env)->ReleaseByteArrayElements(env, rgbaOut, buf, updated ? 0 : JNI_ABORT);
    for (int i = 0; i < 5; i++) jm[i] = (jint)m[i];
    (*env)->SetIntArrayRegion(env, metaOut, 0, 5, jm);
    return updated ? 1 : 0;
}
static jint JNICALL native_warriors_minimap_cycle(JNIEnv *env, jclass cls) {
    (void)env; (void)cls;
    return vk_minimap_cycle ? (jint)vk_minimap_cycle() : 0;
}
static void JNICALL native_warriors_stream_mode(JNIEnv *env, jclass cls, jint stream, jint mode) {
    (void)env; (void)cls;
    if (vk_minimap_stream_mode) vk_minimap_stream_mode((int)stream, (int)mode);
}
static void JNICALL native_warriors_minimap_enable(JNIEnv *env, jclass cls, jboolean on) {
    (void)env; (void)cls;
    if (vk_minimap_enable) vk_minimap_enable(on ? 1 : 0);
}

static jint jni_read_rgba_buf(
        JNIEnv *env, jclass cls, jbyteArray rgbaOut, jintArray metaOut, int nmeta,
        int (*fn4)(uint8_t *, int, int32_t *),
        int (*fn_req)(uint8_t *, int, int32_t *, int), int req) {
    if (!native_enabled(env, cls) || !rgbaOut || !metaOut || (*env)->GetArrayLength(env, metaOut) < nmeta || nmeta > 8) return 0;
    jint jm[8] = {0}; int32_t m[8] = {0};
    (*env)->GetIntArrayRegion(env, metaOut, 0, nmeta, jm);
    for (int i = 0; i < nmeta; i++) m[i] = (int32_t)jm[i];
    jsize cap = (*env)->GetArrayLength(env, rgbaOut);
    jbyte *buf = cap > 0 ? (*env)->GetByteArrayElements(env, rgbaOut, NULL) : NULL;
    int updated = 0;
    if (buf) updated = fn_req ? fn_req((uint8_t *)buf, (int)cap, m, req) : (fn4 ? fn4((uint8_t *)buf, (int)cap, m) : 0);
    if (buf) (*env)->ReleaseByteArrayElements(env, rgbaOut, buf, updated ? 0 : JNI_ABORT);
    for (int i = 0; i < nmeta; i++) jm[i] = (jint)m[i];
    (*env)->SetIntArrayRegion(env, metaOut, 0, nmeta, jm);
    return updated ? 1 : 0;
}

static jint JNICALL native_warriors_objective(JNIEnv *env, jclass cls, jbyteArray rgbaOut, jintArray metaOut) {
    return vk_minimap_objective_read ? jni_read_rgba_buf(env, cls, rgbaOut, metaOut, 4, vk_minimap_objective_read, NULL, 0) : 0;
}

static jint JNICALL native_warriors_lhud(JNIEnv *env, jclass cls, jbyteArray rgbaOut, jintArray metaOut) {
    return vk_minimap_lhud_read ? jni_read_rgba_buf(env, cls, rgbaOut, metaOut, 4, vk_minimap_lhud_read, NULL, 0) : 0;
}

static jint JNICALL native_warriors_ui_tex(JNIEnv *env, jclass cls, jbyteArray rgbaOut, jintArray metaOut, jboolean request) {
    return vk_minimap_ui_tex_read ? jni_read_rgba_buf(env, cls, rgbaOut, metaOut, 4, NULL, vk_minimap_ui_tex_read, request ? 1 : 0) : 0;
}

__attribute__((visibility("default"))) jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    (void)reserved;
    JNIEnv *env;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    /* System.loadLibrary from CompanionNative supplies the app class loader; without it (a plain NativeActivity start)
       the class is not found and the library load still succeeds. */
    jclass cls = (*env)->FindClass(env, "com/aure/totk/companion/CompanionNative");
    if (!cls) { (*env)->ExceptionClear(env); fprintf(stderr, "[companion] CompanionNative class not found\n"); return JNI_VERSION_1_6; }
    static const JNINativeMethod methods[] = {
        {"nativeEnabled", "()Z", (void *)native_enabled},
        {"nativeCoverEnabled", "()Z", (void *)native_cover_enabled},
        {"nativeCoverSetting", "()Ljava/lang/String;", (void *)native_cover_setting},
        {"nativeCoverLog", "(Ljava/lang/String;)V", (void *)native_cover_log},
        {"snapshot", "(I)Ljava/lang/String;", (void *)native_snapshot},
        {"action", "(II)Z", (void *)native_action},
        {"request", "(Ljava/lang/String;Ljava/lang/String;ILjava/lang/String;IJ)I", (void *)native_request},
        {"result", "(I)Ljava/lang/String;", (void *)native_result},
        {"nativeState", "([J)Z", (void *)native_state},
        {"nativePage", "(I)V", (void *)native_page},
        {"warriorsDetailNative", "([I[F)Ljava/lang/String;", (void *)native_warriors_detail},
        {"warriorsFireNative", "(I)Z", (void *)native_warriors_fire},
        {"warriorsAbortNative", "()V", (void *)native_warriors_abort},
        {"warriorsPinNative", "(IILjava/lang/String;)I", (void *)native_warriors_pin},
        {"overlaySetModeNative", "(I)I", (void *)native_overlay_set_mode},
        {"warriorsMinimapNative", "([B[I)I", (void *)native_warriors_minimap},
        {"warriorsMinimapCycleNative", "()I", (void *)native_warriors_minimap_cycle},
        {"warriorsMinimapEnableNative", "(Z)V", (void *)native_warriors_minimap_enable},
        {"warriorsStreamModeNative", "(II)V", (void *)native_warriors_stream_mode},
        {"warriorsObjectiveNative", "([B[I)I", (void *)native_warriors_objective},
        {"warriorsLeftHudNative", "([B[I)I", (void *)native_warriors_lhud},
        {"warriorsUiTexNative", "([B[IZ)I", (void *)native_warriors_ui_tex},
    };
    if ((*env)->RegisterNatives(env, cls, methods, sizeof methods / sizeof *methods)) {
        (*env)->ExceptionClear(env); fprintf(stderr, "[companion] RegisterNatives failed\n");
    } else actions_ready();
    cls = (*env)->FindClass(env, "com/aure/totk/TotkActivity");
    if (cls) {
        static const JNINativeMethod present_methods[] = {
            {"nativePresentTransformHint", "(JI)V", (void *)native_present_transform_hint},
            {"nativeTextInputDone", "(J[B)V", (void *)runtime_android_text_done},
            {"nativeGameFocus", "(ZZ)V", (void *)runtime_android_game_focus},
            {"nativeGameInputFocus", "(Z)V", (void *)runtime_android_game_input_focus},
            {"nativeMotion", "(FFFFFF)V", (void *)runtime_android_motion},
            {"nativeMotionLive", "(ZLjava/lang/String;)V", (void *)runtime_android_motion_live},
            {"nativeFeedbackEnabled", "()I", (void *)runtime_android_feedback_enabled},
        };
        if ((*env)->RegisterNatives(env,cls,present_methods,sizeof present_methods/sizeof *present_methods)) {
            (*env)->ExceptionClear(env); fprintf(stderr,"[present-prerotate] RegisterNatives failed\n");
        }
    } else (*env)->ExceptionClear(env); /* NativeActivity-only APK: optional Java hint unavailable. */
    return JNI_VERSION_1_6;
}
