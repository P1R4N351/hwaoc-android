#ifndef TEST_VK_KTGL_OPT_W9_H
#define TEST_VK_KTGL_OPT_W9_H

#include "vk_vb_fast.h"
#include "vk_dsc_fast.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint64_t host_now_ns(void);
static volatile uint64_t w9_sink;

static int test_w9_vb_ib_state(void) {
    totk_vb_state st = {0};
    if (!totk_vb_slot_changed(&st, 0x100u, 1u, 2u, 0xb0u, 0x100u, 4096u, 32u)) return 1;
    if (totk_vb_slot_changed(&st, 0x100u, 1u, 2u, 0xb0u, 0x100u, 4096u, 32u)) return 2;
    if (!totk_vb_slot_changed(&st, 0x100u, 1u, 2u, 0xb0u, 0x200u, 4096u, 32u)) return 3;
    if (!totk_vb_slot_changed(&st, 0x200u, 1u, 2u, 0xb0u, 0x200u, 4096u, 32u)) return 4;
    if (!totk_ib_need_bind(&st, 0x200u, 1u, 0x9000u, 0u, 1u)) return 5;
    if (totk_ib_need_bind(&st, 0x200u, 1u, 0x9000u, 0u, 1u)) return 6;
    if (!totk_ib_need_bind(&st, 0x200u, 1u, 0x9000u, 0u, 2u)) return 7;
    uint32_t m = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 5) | (1u << 16);
    uint32_t f = 0, c = 0;
    totk_vb_next_run(&m, &f, &c); if (f != 0u || c != 3u) return 8;
    totk_vb_next_run(&m, &f, &c); if (f != 5u || c != 1u) return 9;
    totk_vb_next_run(&m, &f, &c); if (f != 16u || c != 1u || m != 0u) return 10;
    (void)totk_vk_vb_filter_on();
    return 0;
}

static int test_w9_vbuf_memo4(void) {
    totk_vbuf_memo4 *vm = calloc(1, sizeof *vm);
    if (!vm) return 1;
    uint64_t bu = 0, off = 0, av = 0;
    totk_vbuf_memo4_put(vm, 10u, 0x300000u, 0xaaa01u, 0x400u, 8192u);
    if (!totk_vbuf_memo4_find(vm, 10u, 0x300000u, &bu, &off, &av) || bu != 0xaaa01u) { free(vm); return 2; }
    if (totk_vbuf_memo4_find(vm, 11u, 0x300000u, &bu, &off, &av)) { free(vm); return 3; }
    free(vm);
    return 0;
}

static uint64_t w9_bench_vb(int fast, uint64_t *calls_o, uint64_t *zero_allocs_o, uint64_t *ib_calls_o) {
    totk_vb_state st = {0};
    totk_vbuf_memo4 *vm = calloc(1, sizeof *vm);
    assert(vm != NULL);
    uint64_t span_lo[16], span_buf[16]; uint8_t scratch[64];
    for (int s = 0; s < 16; s++) { span_lo[s] = 0x50000000ull + (uint64_t)s * 0x100000ull; span_buf[s] = 0x8000u + (uint64_t)s; }
    uint64_t t0 = host_now_ns(), calls = 0, zallocs = 0, ibc = 0, sum = 0;
    for (int it = 0; it < 200000; it++) {
        uint32_t dirty = 0;
        for (uint32_t s = 0; s < 4u; s++) {
            uint64_t a = span_lo[s] + (uint64_t)s * 128u, bu = 0, oo = 0, av = 0;
            if (!fast || !totk_vbuf_memo4_find(vm, 1u, a, &bu, &oo, &av)) {
                int k = 0; while (k < 16 && span_lo[k] + 0x100000ull <= a) k++;
                bu = span_buf[k]; oo = a - span_lo[k]; av = 0x100000ull - oo;
                if (fast) totk_vbuf_memo4_put(vm, 1u, a, bu, oo, av);
            }
            if (!fast) calls++;
            else if (totk_vb_slot_changed(&st, 0x42u, 1u, s, bu, oo, av, 32u)) dirty |= 1u << s;
            sum += bu ^ oo;
        }
        if (!fast || st.zero_sub != 1u) { memset(scratch, 0, sizeof scratch); zallocs++; st.zero_sub = 1u; }
        if (!fast) calls++;
        else if (totk_vb_slot_changed(&st, 0x42u, 1u, 16u, (uint64_t)(uintptr_t)scratch, 0u, 64u, 0u)) dirty |= 1u << 16;
        for (uint32_t m = dirty; m;) { uint32_t f = 0, c = 0; totk_vb_next_run(&m, &f, &c); calls++; sum += f + c; }
        if (!fast || totk_ib_need_bind(&st, 0x42u, 1u, 0x9999u, 0u, 1u)) ibc++;
    }
    uint64_t dt = host_now_ns() - t0;
    w9_sink ^= sum; *calls_o = calls; *zero_allocs_o = zallocs; *ib_calls_o = ibc;
    free(vm);
    return dt;
}

static uint64_t w9_bench_rec_hash(int fast) {
    uint64_t info[64];
    for (int i = 0; i < 64; i++) info[i] = 0x123456789abcdef0ull + (uint64_t)i * 17ull;
    uint64_t t0 = host_now_ns(), sum = 0;
    for (int it = 0; it < 200000; it++) {
        uint64_t h[2] = {0xcbf29ce484222325ull, 0x6a09e667f3bcc909ull};
        info[0] = (uint64_t)it;
        if (fast) totk_hash_dual64(info, sizeof info, h);
        else {
            const uint8_t *b = (const uint8_t *)info;
            for (size_t i = 0; i + 8 <= sizeof info; i += 8) {
                uint64_t w; memcpy(&w, b + i, 8);
                h[0] = (h[0] ^ w) * 0x9e3779b97f4a7c15ull; h[0] ^= h[0] >> 32;
                h[1] = (h[1] + w) * 0xc2b2ae3d27d4eb4full; h[1] = h[1] << 31 | h[1] >> 33;
            }
        }
        sum ^= h[0] ^ h[1];
    }
    uint64_t dt = host_now_ns() - t0; w9_sink ^= sum;
    return dt;
}

static int run_ktgl_w9_tests(int fault_mode) {
    (void)fault_mode;
    int r1 = test_w9_vb_ib_state(), r2 = test_w9_vbuf_memo4();
    if (r1 || r2) { fprintf(stderr, "FAIL w9 r1=%d r2=%d fault=%d\n", r1, r2, fault_mode); return 1; }
    uint64_t c0 = 0, c1 = 0, z0 = 0, z1 = 0, ib0 = 0, ib1 = 0;
    uint64_t ns0 = w9_bench_vb(0, &c0, &z0, &ib0), ns1 = w9_bench_vb(1, &c1, &z1, &ib1);
    uint64_t h0 = w9_bench_rec_hash(0), h1 = w9_bench_rec_hash(1);
    printf("aoc_w9_vb_fast: fault=%d ops=200000 base=%.2fms (vbb=%llu z=%llu ib=%llu) fast=%.2fms (vbb=%llu z=%llu ib=%llu) speedup=%.2fx sink=%llu\n",
           fault_mode, (double)ns0 / 1e6, (unsigned long long)c0, (unsigned long long)z0, (unsigned long long)ib0,
           (double)ns1 / 1e6, (unsigned long long)c1, (unsigned long long)z1, (unsigned long long)ib1,
           ns1 ? (double)ns0 / (double)ns1 : 0.0, (unsigned long long)w9_sink);
    printf("aoc_w9_rec_hash: fault=%d ops=200000 scalar=%.2fms dual64=%.2fms speedup=%.2fx\n",
           fault_mode, (double)h0 / 1e6, (double)h1 / 1e6, h1 ? (double)h0 / (double)h1 : 0.0);
    if (fault_mode == 0 && (c0 != 1000000 || c1 != 2 || z0 != 200000 || z1 != 1 || ib0 != 200000 || ib1 != 1)) return 1;
    return 0;
}

#endif
