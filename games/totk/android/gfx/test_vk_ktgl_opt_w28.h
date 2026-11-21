/* Wave 28 V2 (Opt 30) tests, TSan stress, microbench, and faults 92..96 for TOTK_OE_FAST. */
#pragma once
#include <assert.h>
#include <ctype.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOTK_OE_FAST_TEST_HOOKS 1
#include "totk_oe_fast.h"

uint64_t host_now_ns(void);

static void w28_mq_init(totk_oe_mq *q) {
    memset(q, 0, sizeof *q);
}

static unsigned char w28_mq_try(pthread_mutex_t *mu, totk_oe_mq *q, uint32_t *out) {
    unsigned char fast_ret = 0;
    if (totk_oe_mq_try_empty(q, out, &fast_ret)) return fast_ret;
    pthread_mutex_lock(mu);
    int ok = totk_oe_mq_pop_locked(q, out);
    pthread_mutex_unlock(mu);
    return (unsigned char)ok;
}

static uint64_t w28_slow_lang(const char *tlang, const char *tloc, const char *lc, const char *lg) {
    char code[8] = {0}, language[16] = {0}, script[8] = {0}, region[4] = {0};
    if (tlang && *tlang) {
        size_t n = strnlen(tlang, 7u);
        memcpy(code, tlang, n);
    } else {
        if (tloc && *tloc) {
            size_t n = strcspn(tloc, "-_");
            if (n < sizeof language) memcpy(language, tloc, n);
            for (const char *p = tloc + n; *p;) {
                p += strspn(p, "-_");
                size_t k = strcspn(p, "-_");
                if (k == 2u && isalpha((unsigned char)p[0]) && isalpha((unsigned char)p[1])) {
                    region[0] = (char)toupper((unsigned char)p[0]);
                    region[1] = (char)toupper((unsigned char)p[1]);
                } else if (k == 4u && !script[0] && isalpha((unsigned char)p[0])) {
                    for (size_t i = 0; i < 4u; i++)
                        script[i] = (char)(i ? tolower((unsigned char)p[i]) : toupper((unsigned char)p[i]));
                }
                p += k;
            }
        } else {
            const char *l = (lc && *lc) ? lc : ((lg && *lg) ? lg : "");
            size_t n = strcspn(l, "_-.@");
            if (n < sizeof language) memcpy(language, l, n);
            if (!strcmp(language, "C") || !strcmp(language, "POSIX")) language[0] = 0;
        }
        const char *ch = !strcmp(script, "Hant") || (!script[0] && (!strcmp(region, "TW") || !strcmp(region, "HK") || !strcmp(region, "MO"))) ? "zh-Hant" : "zh-Hans";
        const char *pt = !strcmp(region, "BR") ? "pt-BR" : "pt";
        const char *sel = !strcmp(language, "ja") ? "ja" : !strcmp(language, "de") ? "de" : !strcmp(language, "fr") ? "fr"
            : !strcmp(language, "es") ? "es" : !strcmp(language, "it") ? "it" : !strcmp(language, "nl") ? "nl"
            : !strcmp(language, "ru") ? "ru" : !strcmp(language, "ko") ? "ko" : !strcmp(language, "zh") ? ch
            : !strcmp(language, "pt") ? pt : "en-US";
        memcpy(code, sel, strlen(sel));
    }
    uint64_t u = 0;
    memcpy(&u, code, 8u);
    return u;
}

static uint64_t w28_fast_lang(totk_oe_lang_slot *s, pthread_mutex_t *mu,
                              const char *tlang, const char *tloc,
                              const char *lc, const char *lg) {
    uint64_t u = 0;
    if (totk_oe_lang_get(s, tlang, tloc, lc, lg, &u)) return u;
    u = w28_slow_lang(tlang, tloc, lc, lg);
    pthread_mutex_lock(mu);
    totk_oe_lang_put(s, tlang, tloc, lc, lg, u);
    pthread_mutex_unlock(mu);
    return u;
}

static int w28_check_fault(int f) {
    totk_oe_reset_for_test(1);
    totk_oe_mq q;
    w28_mq_init(&q);
    pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    if (f == 92) {
        uint32_t m = 555u;
        unsigned char ok = w28_mq_try(&mu, &q, &m);
        return (ok == 0u && m == 555u) ? 0 : 1;
    }
    if (f == 93) {
        pthread_mutex_lock(&mu);
        for (int i = 0; i < 40; i++) totk_oe_mq_push_locked(&q, 15u);
        int p30 = totk_oe_mq_push_locked(&q, 30u);
        pthread_mutex_unlock(&mu);
        uint32_t a = 0u, b = 0u, c = 0u;
        int ok1 = w28_mq_try(&mu, &q, &a);
        int ok2 = w28_mq_try(&mu, &q, &b);
        int ok3 = w28_mq_try(&mu, &q, &c);
        return (p30 == 1 && ok1 == 1 && a == 15u && ok2 == 1 && b == 30u && ok3 == 0) ? 0 : 1;
    }
    if (f == 94) {
        totk_oe_balance bal;
        memset(&bal, 0, sizeof bal);
        float dl = 0.0f, dr = 0.0f;
        totk_oe_bal_get(&bal, &dl, &dr);
        assert(dl == 1.0f && dr == 1.0f);
        totk_oe_bal_set(&bal, 0.25f, 0.75f);
        float l = 0.0f, r = 0.0f;
        totk_oe_bal_get(&bal, &l, &r);
        return (l == 0.25f && r == 0.75f) ? 0 : 1;
    }
    if (f == 95) {
        static totk_oe_lang_slot s;
        memset(&s, 0, sizeof s);
        uint64_t u1 = w28_fast_lang(&s, &mu, NULL, "ja-JP", NULL, "C");
        uint64_t u2 = w28_fast_lang(&s, &mu, "pt-BR", "ja-JP", NULL, "C");
        return (u1 != u2) ? 0 : 1;
    }
    if (f == 96) {
        return (totk_oe_opmode_to_console(0) == 1 && totk_oe_opmode_to_console(1) == 0) ? 0 : 1;
    }
    return 1;
}

typedef struct {
    totk_oe_mq *q;
    totk_oe_balance *bal;
    totk_oe_lang_slot *s;
    pthread_mutex_t *mu;
    pthread_mutex_t *lang_mu;
} w28_stress_t;

static void *w28_worker(void *arg) {
    w28_stress_t *c = (w28_stress_t *)arg;
    static const char *const locs[4] = {"ja-JP", "pt-BR", "zh-TW", "en-US"};
    for (int i = 0; i < 4000; i++) {
        uint32_t m = 999u;
        if ((i & 31) == 0) {
            pthread_mutex_lock(c->mu);
            totk_oe_mq_push_locked(c->q, (i & 64) ? 15u : 95u);
            pthread_mutex_unlock(c->mu);
        }
        unsigned char ok = w28_mq_try(c->mu, c->q, &m);
        if (!ok) assert(m == 999u);
        float l = 0.0f, r = 0.0f;
        totk_oe_bal_get(c->bal, &l, &r);
        assert((l == 0.25f && r == 0.75f) || (l == 1.0f && r == 1.0f));
        uint64_t u = w28_fast_lang(c->s, c->lang_mu, NULL, locs[i & 3], NULL, "C");
        assert(u != 0u);
    }
    return NULL;
}

static void w28_run_parity(void) {
    totk_oe_mq q;
    pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    for (int mode = 0; mode < 2; mode++) {
        totk_oe_reset_for_test(mode);
        w28_mq_init(&q);
        uint32_t m = 777u;
        assert(w28_mq_try(&mu, &q, &m) == 0u && m == 777u);
        pthread_mutex_lock(&mu);
        assert(totk_oe_mq_push_locked(&q, 15u) == 1);
        assert(totk_oe_mq_push_locked(&q, 15u) == (mode ? 0 : 1));
        assert(totk_oe_mq_push_locked(&q, 95u) == 1);
        assert(totk_oe_mq_push_locked(&q, 95u) == 1);
        pthread_mutex_unlock(&mu);
        assert(w28_mq_try(&mu, &q, &m) == 1u && m == 15u);
        if (!mode) assert(w28_mq_try(&mu, &q, &m) == 1u && m == 15u);
        assert(w28_mq_try(&mu, &q, &m) == 1u && m == 95u);
        assert(w28_mq_try(&mu, &q, &m) == 1u && m == 95u);
        assert(w28_mq_try(&mu, &q, &m) == 0u);
        assert(totk_oe_opmode_to_console(0) == 1 && totk_oe_opmode_to_console(1) == 0);
    }
}

static int run_ktgl_w28_tests(int fault_mode) {
    if (fault_mode >= 92 && fault_mode <= 96) return w28_check_fault(fault_mode);
    w28_run_parity();

    totk_oe_reset_for_test(1);
    totk_oe_mq q;
    w28_mq_init(&q);
    totk_oe_balance bal;
    memset(&bal, 0, sizeof bal);
    float init_l = 0.0f, init_r = 0.0f;
    totk_oe_bal_get(&bal, &init_l, &init_r);
    assert(init_l == 1.0f && init_r == 1.0f);
    totk_oe_bal_set(&bal, 0.25f, 0.75f);
    static totk_oe_lang_slot s;
    memset(&s, 0, sizeof s);
    pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t lang_mu = PTHREAD_MUTEX_INITIALIZER;
    w28_stress_t ctx = {.q = &q, .bal = &bal, .s = &s, .mu = &mu, .lang_mu = &lang_mu};
    pthread_t th[4];
    for (int i = 0; i < 4; i++) assert(pthread_create(&th[i], NULL, w28_worker, &ctx) == 0);
    for (int i = 0; i < 4; i++) assert(pthread_join(th[i], NULL) == 0);
    uint32_t d = 0u;
    while (w28_mq_try(&mu, &q, &d)) {}

    static const char *volatile bench_locs[4] = {"pt-BR", "pt-BR", "pt-BR", "pt-BR"};
    float bal_slow[2] = {0.25f, 0.75f};
    memset(&s, 0, sizeof s);
    for (int mode = 0; mode < 2; mode++) {
        totk_oe_reset_for_test(mode);
        volatile uint64_t sink = 0;
        uint64_t t0 = host_now_ns();
        for (int i = 0; i < 200000; i++) {
            uint32_t m = 0u;
            unsigned char ok = w28_mq_try(&mu, &q, &m);
            float l = 0.0f, r = 0.0f;
            if (mode) {
                totk_oe_bal_get(&bal, &l, &r);
            } else {
                pthread_mutex_lock(&mu);
                l = bal_slow[0];
                r = bal_slow[1];
                pthread_mutex_unlock(&mu);
            }
            uint64_t u = w28_fast_lang(&s, &lang_mu, NULL, bench_locs[i & 3], NULL, "C");
            int om = totk_oe_opmode_to_console(i & 1);
            sink += (uint64_t)ok + (uint64_t)(l * 100.0f) + (uint64_t)(r * 100.0f) + (uint64_t)om + (u & 0xffu);
        }
        printf("aoc_oe_w28(%s): elapsed=%.3f ms sink=%llu\n", mode ? "on" : "off",
               (double)(host_now_ns() - t0) / 1e6, (unsigned long long)sink);
    }
    return 0;
}
