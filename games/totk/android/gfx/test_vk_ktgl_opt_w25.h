/* Wave 25 (Opt 27) tests, TSan stress, microbench, and faults 77..81 for TOTK_TIME_FAST. */
#pragma once
#define TOTK_TIME_FAST_TEST_HOOKS 1
#include "../../runtime/ipc.h"
#include "../../runtime/totk_time_fast.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

uint64_t host_now_ns(void);

typedef struct { int16_t year; int8_t month, day, hour, minute, second, pad; } w25_ct_t;
typedef struct { int32_t day_of_week, day_of_year; char name[8]; uint8_t dst, pad[3]; int32_t utc_offset; } w25_info_t;

static void w25_slow_cal(w25_ct_t *c, w25_info_t *info, int64_t posix, int local) {
    time_t t = (time_t)posix; struct tm tm;
    if (local) localtime_r(&t, &tm); else gmtime_r(&t, &tm);
    *c = (w25_ct_t){(int16_t)(tm.tm_year + 1900), (int8_t)(tm.tm_mon + 1), (int8_t)tm.tm_mday,
                    (int8_t)tm.tm_hour, (int8_t)tm.tm_min, (int8_t)tm.tm_sec, 0};
    memset(info, 0, sizeof *info);
    info->day_of_week = tm.tm_wday; info->day_of_year = tm.tm_yday;
    snprintf(info->name, sizeof info->name, "%s", local && tm.tm_zone ? tm.tm_zone : "UTC");
    info->dst = local && tm.tm_isdst > 0; info->utc_offset = local ? (int32_t)tm.tm_gmtoff : 0;
}

static int w25_test_cal_and_loc(int f) {
    totk_time_fast_reset_for_test(1);
    static totk_time_cal_cache cc;
    static totk_time_loc_cache lc;
    memset(&cc, 0, sizeof cc); memset(&lc, 0, sizeof lc);
    setenv("TZ", "UTC", 1); tzset();
    int64_t t1 = 1700000000, t2 = 1700003600;
    uint64_t tz1 = totk_time_tz_tag("UTC"), tz2 = totk_time_tz_tag("Asia/Seoul");
    w25_ct_t c1, c2; w25_info_t i1, i2;
    w25_slow_cal(&c1, &i1, t1, 1);
    totk_time_cal_write(&cc, 1, t1, tz1, &c1, &i1);
    assert(totk_time_cal_read(&cc, 1, t1, tz1, &c2, &i2));
    if (f == 77) return memcmp(&c1, &c2, sizeof c1) == 0 ? 0 : 1;
    assert(memcmp(&c1, &c2, sizeof c1) == 0 && memcmp(&i1, &i2, sizeof i1) == 0);
    if (f == 78) return totk_time_cal_read(&cc, 1, t2, tz1, &c2, &i2) == 0 ? 0 : 1;
    assert(!totk_time_cal_read(&cc, 1, t2, tz1, &c2, &i2));
    if (f == 79) return totk_time_cal_read(&cc, 1, t1, tz2, &c2, &i2) == 0 ? 0 : 1;
    assert(!totk_time_cal_read(&cc, 1, t1, tz2, &c2, &i2));
    w25_slow_cal(&c1, &i1, t1, 0);
    totk_time_cal_write(&cc, 0, t1, 0ull, &c1, &i1);
    assert(totk_time_cal_read(&cc, 0, t1, 0ull, &c2, NULL) && memcmp(&c1, &c2, sizeof c1) == 0);
    int misses = 0; char loc[0x24] = "UTC", lout[0x24] = {0};
    for (int k = 0; k < 16; k++) {
        if (!totk_time_loc_read(&lc, tz1, lout)) { misses++; totk_time_loc_write(&lc, tz1, loc); memcpy(lout, loc, 0x24); }
        if (k > 0 && f == 80) return strcmp(lout, "UTC") == 0 ? 0 : 1;
        assert(strcmp(lout, "UTC") == 0);
    }
    assert(misses == 1);
    return 0;
}

static void w25_build_cmif(uint32_t *m, uint32_t cmd, uint32_t arg, uint32_t x_sz, int nmove) {
    memset(m, 0, 0x100);
    uint32_t off = 2 + (nmove ? 1u + (uint32_t)nmove : 0u) + (x_sz ? 2u : 0u);
    uint32_t pay = (off * 4u + 15u) & ~15u;
    uint32_t nwords = (uint32_t)((pay - off * 4u + 16u + 4u + 16u + 3u) / 4u);
    m[0] = 4u | ((x_sz ? 1u : 0u) << 16);
    m[1] = (nwords & 0x3ffu) | (nmove ? (1u << 31) : 0u);
    uint32_t *p = m + 2;
    if (nmove) { *p++ = (uint32_t)nmove << 5; for (int i = 0; i < nmove; i++) *p++ = (uint32_t)(0x100u + i); }
    if (x_sz) { *p++ = (x_sz << 16) | 1u; *p++ = 0x12340000u; }
    uint32_t *h = (uint32_t *)((uint8_t *)m + pay);
    h[0] = 0x49434653u; h[1] = 0; h[2] = cmd; h[4] = arg;
}

static void w25_emit_resp(uint32_t *dst, int slow, int tipc, uint32_t val, int nmove, int nxout) {
    uint32_t out[64];
    unsigned sp = nmove > 0;
    size_t widx = 2u + (sp ? 1u + (size_t)nmove : 0u) + (size_t)nxout * 2u;
    size_t woff = widx * 4u, pay = tipc ? woff : ((woff + 15u) & ~15u);
    unsigned hdr = tipc ? 4u : 16u, used = (unsigned)(pay - woff) + hdr + 8u;
    unsigned nw = tipc ? (used + 3u) / 4u : (used + 16u + 3u) / 4u;
    size_t total = slow ? 0x100u : totk_ipc_respond_bytes(woff, nw, 0x100u);
    memset(out, 0, total);
    uint32_t *p = out + 2;
    if (sp) { *p++ = (uint32_t)nmove << 5; for (int i = 0; i < nmove; i++) *p++ = (uint32_t)(0x200u + i); }
    for (int i = 0; i < nxout; i++) { *p++ = (uint32_t)i | (16u << 16); *p++ = 0x56780000u; }
    uint8_t *pl = (uint8_t *)out + pay;
    if (tipc) { uint32_t r0 = 0; memcpy(pl, &r0, 4); }
    else { uint32_t h[4] = {0x4f434653u, 0, 0, 0}; memcpy(pl, h, 16); }
    uint64_t v64 = (uint64_t)val | (0xaa550000ull << 32);
    memcpy(pl + hdr, &v64, 8);
    out[0] = ((uint32_t)nxout & 0xfu) << 16; out[1] = (nw & 0x3ffu) | (sp ? 1u << 31 : 0u);
    memcpy(dst, out, total);
}

static int w25_test_ipc_framing(int f) {
    totk_time_fast_reset_for_test(1);
    uint32_t msg[64], ref[64], fast[64];
    w25_build_cmif(msg, 42u, 0x77u, 16u, 1);
    IpcRequest rq0, rq1;
    memset(&rq0, 0, sizeof rq0); memset(&rq1, 0xa5, sizeof rq1);
    rq0.type = msg[0] & 0xffffu; rq0.nx = (msg[0] >> 16) & 0xfu;
    totk_ipc_req_reset_fast(&rq1, msg[0]);
    assert(rq0.type == rq1.type && rq0.nx == rq1.nx && rq1.ncopy == 0 && rq1.nmove == 0 && !rq1.has_pid);
    IpcResponse rs0, rs1;
    memset(&rs0, 0, sizeof rs0); memset(&rs1, 0xa5, sizeof rs1);
    totk_ipc_resp_reset_fast(&rs1);
    assert(rs1.result == 0 && rs1.data_size == 0 && rs1.nxout == 0 && rs1.ncopy == 0 && rs1.nmove == 0);
    memset(ref, 0, sizeof ref); memset(fast, 0, sizeof fast);
    w25_emit_resp(ref, 1, 1, 0x5678u, 0, 0);
    w25_emit_resp(fast, 0, 1, 0x5678u, 0, 0);
    size_t tipc_bytes = 8u + (size_t)(ref[1] & 0x3ffu) * 4u;
    if (f == 81) return memcmp(ref, fast, tipc_bytes) == 0 ? 0 : 1;
    assert(memcmp(ref, fast, tipc_bytes) == 0);
    memset(ref, 0, sizeof ref); memset(fast, 0, sizeof fast);
    w25_emit_resp(ref, 1, 0, 0x1234u, 1, 1);
    w25_emit_resp(fast, 0, 0, 0x1234u, 1, 1);
    uint32_t ref_words = ref[1] & 0x3ffu;
    assert(memcmp(ref, fast, 20u + (size_t)ref_words * 4u) == 0);
    return 0;
}

typedef struct { totk_time_cal_cache *cc; totk_time_loc_cache *lc; _Atomic int stop; } w25_stress_ctx;

static void *w25_stress_writer(void *arg) {
    w25_stress_ctx *ctx = (w25_stress_ctx *)arg;
    w25_ct_t ct; w25_info_t info; char loc[0x24] = "UTC";
    w25_slow_cal(&ct, &info, 1700000000, 1);
    for (int i = 0; i < 4000 && !atomic_load_explicit(&ctx->stop, memory_order_relaxed); i++) {
        totk_time_cal_write(ctx->cc, 1, 1700000000, 1ull, &ct, &info);
        totk_time_loc_write(ctx->lc, 1ull, loc);
    }
    return NULL;
}

static int w25_run_bench(void) {
    static totk_time_cal_cache cc; static totk_time_loc_cache lc;
    memset(&cc, 0, sizeof cc); memset(&lc, 0, sizeof lc);
    setenv("TZ", "UTC", 1); tzset();
    w25_ct_t ct; w25_info_t info; char loc[0x24] = "UTC", lout[0x24];
    uint64_t tz = totk_time_tz_tag("UTC");
    w25_slow_cal(&ct, &info, 1700000000, 1);
    totk_time_cal_write(&cc, 1, 1700000000, tz, &ct, &info);
    totk_time_loc_write(&lc, tz, loc);
    uint32_t m[64], out[64];
    for (int mode = 0; mode < 2; mode++) {
        volatile uint64_t sink = 0;
        totk_time_fast_reset_for_test(mode);
        uint64_t t0 = host_now_ns();
        for (int i = 0; i < 50000; i++) {
            IpcRequest rq; IpcResponse rs;
            if (!mode) {
                memset(&rq, 0, sizeof rq); memset(&rs, 0, sizeof rs);
                w25_slow_cal(&ct, &info, 1700000000, 1);
                w25_slow_cal(&ct, &info, 1700000000, 1);
                snprintf(lout, sizeof lout, "%s", "UTC");
                w25_build_cmif(m, 1u, (uint32_t)i, 0u, 0);
                w25_emit_resp(out, 1, 0, (uint32_t)i, 0, 0);
            } else {
                totk_ipc_req_reset_fast(&rq, 4u); totk_ipc_resp_reset_fast(&rs);
                totk_time_cal_read(&cc, 1, 1700000000, tz, &ct, &info);
                totk_time_loc_read(&lc, tz, lout);
                w25_build_cmif(m, 1u, (uint32_t)i, 0u, 0);
                w25_emit_resp(out, 0, 0, (uint32_t)i, 0, 0);
            }
            sink += (uint64_t)ct.year + (uint64_t)lout[0] + out[1];
        }
        printf("aoc_w25_%s time_snap_ipc_dispatch=%.3f ms sink=%llu\n",
               mode ? "on" : "off", (double)(host_now_ns() - t0) / 1e6, (unsigned long long)sink);
    }
    return 0;
}

static int run_ktgl_w25_tests(int fault_mode) {
    if (fault_mode >= 77 && fault_mode <= 80) return w25_test_cal_and_loc(fault_mode);
    if (fault_mode == 81) return w25_test_ipc_framing(fault_mode);
    assert(w25_test_cal_and_loc(0) == 0);
    assert(w25_test_ipc_framing(0) == 0);
    static totk_time_cal_cache cc; static totk_time_loc_cache lc;
    memset(&cc, 0, sizeof cc); memset(&lc, 0, sizeof lc);
    w25_stress_ctx ctx = {&cc, &lc, 0};
    w25_ct_t ct; w25_info_t info; char lin[0x24] = "UTC", lout[0x24];
    w25_slow_cal(&ct, &info, 1700000000, 1);
    totk_time_cal_write(&cc, 1, 1700000000, 1ull, &ct, &info);
    totk_time_loc_write(&lc, 1ull, lin);
    pthread_t th[3];
    for (int i = 0; i < 3; i++) assert(pthread_create(&th[i], NULL, w25_stress_writer, &ctx) == 0);
    for (int i = 0; i < 4000; i++) {
        (void)totk_time_cal_read(&cc, 1, 1700000000, 1ull, &ct, &info);
        (void)totk_time_loc_read(&lc, 1ull, lout);
    }
    atomic_store_explicit(&ctx.stop, 1, memory_order_relaxed);
    for (int i = 0; i < 3; i++) assert(pthread_join(th[i], NULL) == 0);
    return w25_run_bench();
}
