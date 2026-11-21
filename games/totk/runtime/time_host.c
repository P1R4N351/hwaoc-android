/* nn::time for the functions main imports, backed by the host clock and time zone.
   User and network system clocks both follow the (NTP-synchronised) host wall clock; the steady clock
   counts seconds of host continuous time with one source id per process. ClockSnapshot uses the public
   0xD0-byte layout because game code may read its fields through inline accessors. */
#include "runtime.h"
#if defined(__APPLE__)
#include <mach/mach_time.h>
#endif
#include <pthread.h>
#include <stdatomic.h>
#include "totk_time_fast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct { int64_t value; uint8_t source[16]; } SteadyClockTimePoint;
typedef struct { int64_t offset; SteadyClockTimePoint point; } SystemClockContext;
typedef struct { int16_t year; int8_t month, day, hour, minute, second, pad; } CalendarTime;
typedef struct { int32_t day_of_week, day_of_year; char name[8]; uint8_t dst, pad[3]; int32_t utc_offset; } CalendarAdditionalInfo;
typedef struct {
    SystemClockContext user, network; int64_t user_time, network_time;
    CalendarTime user_calendar, network_calendar; CalendarAdditionalInfo user_info, network_info;
    SteadyClockTimePoint steady; char location[0x24]; uint8_t automatic_correction, type, pad[2];
} ClockSnapshot;
_Static_assert(sizeof(SteadyClockTimePoint) == 0x18 && sizeof(CalendarTime) == 8 && sizeof(CalendarAdditionalInfo) == 0x18, "nn::time types");
_Static_assert(sizeof(ClockSnapshot) == 0xd0, "nn::time::ClockSnapshot");

#define R_INVALID 0x74u          /* time module 116: invalid argument family */
#define R_SOURCE_MISMATCH 0x18074u
static uint8_t source_id[16];
static int initialized;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static void make_source(void) { arc4random_buf(source_id, sizeof source_id); }

static totk_time_cal_cache g_totk_cal_fast;
static totk_time_loc_cache g_totk_loc_fast;

static int64_t steady_seconds(void) {   /* counts through sleep, like the Switch's steady clock */
#if defined(__APPLE__)
    static _Atomic uint64_t g_tb_pack;
    uint64_t tp = totk_time_fast_active() ? atomic_load_explicit(&g_tb_pack, memory_order_relaxed) : 0ull;
    uint32_t numer, denom;
    if (!tp) { mach_timebase_info_data_t tb; mach_timebase_info(&tb); numer = tb.numer; denom = tb.denom;
        if (totk_time_fast_active()) atomic_store_explicit(&g_tb_pack, ((uint64_t)numer << 32) | denom, memory_order_relaxed); }
    else { numer = (uint32_t)(tp >> 32); denom = (uint32_t)tp; }
    return (int64_t)((__uint128_t)mach_continuous_time() * numer / denom / 1000000000u);
#else
    struct timespec t; clock_gettime(CLOCK_BOOTTIME, &t); return (int64_t)t.tv_sec;
#endif
}
static SteadyClockTimePoint steady_now(void) { pthread_once(&once, make_source); SteadyClockTimePoint p = {steady_seconds(), {0}}; memcpy(p.source, source_id, 16); return p; }
static int64_t posix_now(void) {
    static _Atomic long a_off = -1;
    long off = atomic_load_explicit(&a_off, memory_order_relaxed);
    if (off < 0) { const char *e = getenv("TOTK_TIME_OFFSET"); off = e ? atol(e) : 0; if (off < 0) off = 0; atomic_store_explicit(&a_off, off, memory_order_relaxed); }
    return (int64_t)time(NULL) + off;
}

static unsigned initialize(void) { initialized = 1; pthread_once(&once, make_source); return 0; }
static unsigned finalize(void) { initialized = 0; return 0; }
static unsigned char is_initialized(void) { return (unsigned char)initialized; }
static unsigned user_time(int64_t *out) { if (!out) return R_INVALID; *out = posix_now(); return 0; }
/* StandardSteadyClock::now() returns the chrono-style time_point: the int64 seconds value only, 0 without a source
   (sdk+0x3d1d88 returns it in x0; returning the 0x18-byte struct made the host write through the caller's stale x8). */
static int64_t steady_clock_now(void) { return totk_time_fast_active() ? steady_seconds() : steady_now().value; }
#ifdef GAME_BOTW
/* SDK 7 (Breath of the Wild 1.6.0, botw.md): the steady clock is read into a time point, and a span is only defined between
   points of the same clock source. */
static uint32_t steady_time_point(SteadyClockTimePoint *out) { if (!out) runtime_fail("steady clock time point"); *out = steady_now(); return 0; }
static uint32_t span_between_points(int64_t *out, const SteadyClockTimePoint *a, const SteadyClockTimePoint *b) {
    if (!out || !a || !b) runtime_fail("time span between points");
    if (memcmp(a->source, b->source, 16)) return 0x2a6u;          /* time: not comparable (different clock sources) */
    *out = b->value - a->value; return 0;
}
#endif

static void fill_calendar_tz(CalendarTime *c, CalendarAdditionalInfo *info, int64_t posix, int local, int fast, uint64_t tz) {
    if (fast && totk_time_cal_read(&g_totk_cal_fast, local, posix, tz, c, info)) return;
    time_t t = (time_t)posix; struct tm tm;
    if (!(local ? localtime_r(&t, &tm) : gmtime_r(&t, &tm))) runtime_fail("calendar conversion");
    CalendarTime c_val = {(int16_t)(tm.tm_year + 1900), (int8_t)(tm.tm_mon + 1), (int8_t)tm.tm_mday, (int8_t)tm.tm_hour, (int8_t)tm.tm_min, (int8_t)tm.tm_sec, 0};
    CalendarAdditionalInfo a_val; memset(&a_val, 0, sizeof a_val);
    a_val.day_of_week = tm.tm_wday; a_val.day_of_year = tm.tm_yday;
    snprintf(a_val.name, sizeof a_val.name, "%s", local && tm.tm_zone ? tm.tm_zone : "UTC");
    a_val.dst = local && tm.tm_isdst > 0; a_val.utc_offset = local ? (int32_t)tm.tm_gmtoff : 0;
    if (fast) totk_time_cal_write(&g_totk_cal_fast, local, posix, tz, &c_val, &a_val);
    *c = c_val; if (info) *info = a_val;
}
static void fill_calendar(CalendarTime *c, CalendarAdditionalInfo *info, int64_t posix, int local) {
    int fast = totk_time_fast_active(); uint64_t tz = (fast && local) ? totk_time_tz_tag(getenv("TZ")) : 0ull;
    fill_calendar_tz(c, info, posix, local, fast, tz);
}
static unsigned to_calendar(CalendarTime *c, CalendarAdditionalInfo *info, const int64_t *posix) {
    if (!c || !posix) return R_INVALID;
    fill_calendar(c, info, *posix, 1);
    return 0;
}
static CalendarTime to_calendar_utc(const int64_t *posix) { CalendarTime c; fill_calendar(&c, NULL, posix ? *posix : 0, 0); return c; }
static int64_t to_posix_utc(const CalendarTime *c) {
    struct tm tm = {0}; tm.tm_year = c->year - 1900; tm.tm_mon = c->month - 1; tm.tm_mday = c->day; tm.tm_hour = c->hour; tm.tm_min = c->minute; tm.tm_sec = c->second;
    return (int64_t)timegm(&tm);
}

static void location_name_tz(char out[0x24], int fast, uint64_t tz) {
    if (fast && totk_time_loc_read(&g_totk_loc_fast, tz, out)) return;
    char loc[0x24]; memset(loc, 0, sizeof loc);
    char link[512]; ssize_t n = readlink("/etc/localtime", link, sizeof link - 1);
    if (n > 0) { link[n] = 0; const char *z = strstr(link, "zoneinfo/"); snprintf(loc, sizeof loc, "%s", z ? z + 9 : "UTC"); }
    else snprintf(loc, sizeof loc, "UTC");
    if (fast) totk_time_loc_write(&g_totk_loc_fast, tz, loc);
    memcpy(out, loc, 0x24);
}
static void location_name(char out[0x24]) {
    int fast = totk_time_fast_active(); uint64_t tz = fast ? totk_time_tz_tag(getenv("TZ")) : 0ull;
    location_name_tz(out, fast, tz);
}
static void snapshot_init(ClockSnapshot *s) { memset(s, 0, sizeof *s); }
static unsigned snapshot_create(ClockSnapshot *s) {
    if (!s) return R_INVALID;
    SteadyClockTimePoint steady = steady_now(); int64_t now = posix_now();
    memset(s, 0, sizeof *s);
    s->user = (SystemClockContext){now - steady.value, steady}; s->network = s->user;
    s->user_time = now; s->network_time = now;
    int fast = totk_time_fast_active(); uint64_t tz = fast ? totk_time_tz_tag(getenv("TZ")) : 0ull;
    fill_calendar_tz(&s->user_calendar, &s->user_info, now, 1, fast, tz);
    if (fast) { s->network_calendar = s->user_calendar; s->network_info = s->user_info; }
    else fill_calendar_tz(&s->network_calendar, &s->network_info, now, 1, 0, 0ull);
    s->steady = steady; location_name_tz(s->location, fast, tz); s->automatic_correction = 1; s->type = 0;
    return 0;
}
static unsigned span_between(int64_t *out_ns, const ClockSnapshot *a, const ClockSnapshot *b) {
    if (!out_ns || !a || !b) return R_INVALID;
    if (memcmp(a->steady.source, b->steady.source, 16)) return R_SOURCE_MISMATCH;
    *out_ns = (b->steady.value - a->steady.value) * 1000000000LL; return 0;
}
/* Change of the user clock offset between snapshots, i.e. manual adjustments, as a TimeSpan. */
static int64_t user_difference(const ClockSnapshot *a, const ClockSnapshot *b) {
    if (memcmp(a->user.point.source, b->user.point.source, 16)) return 0;
    return (b->user.offset - a->user.offset) * 1000000000LL;
}
static int claim_periodic_benefit(void *ctx, const ClockSnapshot *last, int64_t min_interval, int64_t grace, const void *cb) {
    (void)ctx; (void)last; (void)min_interval; (void)grace; (void)cb; return 1;
}

void *runtime_time_override(const char *name) {
    static const struct { const char *name; void *fn; } map[] = {
        {"_ZN2nn4time10InitializeEv", initialize}, {"_ZN2nn4time8FinalizeEv", finalize}, {"_ZN2nn4time13IsInitializedEv", is_initialized},
        {"_ZN2nn4time23StandardUserSystemClock14GetCurrentTimeEPNS0_9PosixTimeE", user_time},
        {"_ZN2nn4time26StandardNetworkSystemClock14GetCurrentTimeEPNS0_9PosixTimeE", user_time},
        {"_ZN2nn4time19StandardSteadyClock3nowEv", steady_clock_now},
#ifdef GAME_BOTW
        {"_ZN2nn4time19StandardSteadyClock19GetCurrentTimePointEPNS0_20SteadyClockTimePointE", steady_time_point},
        {"_ZN2nn4time14GetSpanBetweenEPlRKNS0_20SteadyClockTimePointES4_", span_between_points},
#endif
        {"_ZN2nn4time14ToCalendarTimeEPNS0_12CalendarTimeEPNS0_22CalendarAdditionalInfoERKNS0_9PosixTimeE", to_calendar},
        {"_ZN2nn4time19ToCalendarTimeInUtcERKNS0_9PosixTimeE", to_calendar_utc},
        {"_ZN2nn4time18ToPosixTimeFromUtcERKNS0_12CalendarTimeE", to_posix_utc},
        {"_ZN2nn4time13ClockSnapshotC1Ev", snapshot_init},
        {"_ZN2nn4time13ClockSnapshot29CreateWithStandardSystemClockEPS1_", snapshot_create},
        {"_ZN2nn4time20CalculateSpanBetweenEPNS_8TimeSpanERKNS0_13ClockSnapshotES5_", span_between},
        {"_ZN2nn4time48CalculateStandardUserSystemClockDifferenceByUserERKNS0_13ClockSnapshotES3_", user_difference},
        {"_ZN2nn4time39ClaimPeriodicBenefitWithUserSystemClockEPNS0_27PeriodicBenefitClaimContextERKNS0_13ClockSnapshotERKNS_8TimeSpanES8_PKNS0_25CheckCalendarTimeCallbackE", claim_periodic_benefit},
    };
    for (size_t i = 0; i < sizeof map / sizeof *map; i++) if (!strcmp(name, map[i].name)) return map[i].fn;
    return NULL;
}
