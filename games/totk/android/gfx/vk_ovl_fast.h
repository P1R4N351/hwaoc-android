#pragma once
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef T_AOC_W40_TEST_HOOKS
extern _Atomic int g_totk_vk_ovl_fast_state;
extern _Atomic int g_totk_w40_fault;
static _Atomic int g_totk_w40_env_fault;
static inline void totk_vk_ovl_fast_reset_for_test(int forced) {
    int v = forced < 0 ? 0 : (forced ? 2 : 1);
    atomic_store_explicit(&g_totk_vk_ovl_fast_state, v, memory_order_release);
}
static inline void totk_w40_set_fault(int m) {
    atomic_store_explicit(&g_totk_w40_fault, m, memory_order_release);
}
static inline int totk_w40_fault(void) {
    int m = atomic_load_explicit(&g_totk_w40_fault, memory_order_acquire);
    if (m) return m;
    int c = atomic_load_explicit(&g_totk_w40_env_fault, memory_order_acquire);
    if (!c) {
        int v = 0;
        const char *e = getenv("TOTK_W40_FAULT");
        if (e && *e) v = atoi(e);
        if (!v) {
            const char *k = getenv("TOTK_KTGL_OPT_FAULT");
            if (k && *k) { int kv = atoi(k); if (kv >= 146 && kv <= 150) v = kv - 145; }
        }
        c = v > 0 ? v + 1 : 1;
        atomic_store_explicit(&g_totk_w40_env_fault, c, memory_order_release);
    }
    return c - 1;
}
#else
static _Atomic int g_totk_vk_ovl_fast_state;
static inline int totk_w40_fault(void) { return 0; }
#endif

static inline int totk_vk_ovl_fast_on(void) {
    int s = atomic_load_explicit(&g_totk_vk_ovl_fast_state, memory_order_acquire);
    if (!s) {
        const char *v = getenv("TOTK_VK_OVL_FAST");
        s = (v && (v[0] == '0' || v[0] == 'f' || v[0] == 'F' || v[0] == 'n' || v[0] == 'N')) ? 1 : 2;
        atomic_store_explicit(&g_totk_vk_ovl_fast_state, s, memory_order_release);
    }
    return s == 2;
}

static inline void totk_ovl_fill_u32(uint32_t * restrict dst, size_t n, uint32_t c) {
    if (!dst || !n) return;
    for (size_t i = 0u; i < n; i++) dst[i] = c;
    if (totk_vk_ovl_fast_on() && totk_w40_fault() == 1) dst[n - 1u] ^= 1u;
}

static inline void totk_ovl_cell_dx1(
    uint32_t *origin, ptrdiff_t dy, unsigned x, unsigned y,
    const uint8_t glyph[7], unsigned scale, unsigned cell_w,
    uint32_t fg, uint32_t bg, int skip_bg) {
    uint32_t span[64];
    unsigned span_px = cell_w * scale;
    int bad_bits = totk_w40_fault() == 2;
    for (unsigned r = 0u; r < 7u; r++) {
        uint8_t bits = glyph[r];
        uint32_t *row0 = origin + (ptrdiff_t)(y + r * scale) * dy + x;
        if (skip_bg && !bits) continue;
        if (scale == 1u && cell_w == 6u && !bad_bits) {
            uint32_t row[6] = {
                (bits & 16u) ? fg : bg, (bits & 8u) ? fg : bg,
                (bits & 4u) ? fg : bg, (bits & 2u) ? fg : bg,
                (bits & 1u) ? fg : bg, bg
            };
            memcpy(row0, row, 24u);
            continue;
        }
        if (scale == 2u && cell_w == 6u && !bad_bits) {
            uint32_t c0 = (bits & 16u) ? fg : bg, c1 = (bits & 8u) ? fg : bg;
            uint32_t c2 = (bits & 4u) ? fg : bg, c3 = (bits & 2u) ? fg : bg, c4 = (bits & 1u) ? fg : bg;
            uint32_t row[12] = {c0, c0, c1, c1, c2, c2, c3, c3, c4, c4, bg, bg};
            memcpy(row0, row, 48u);
            memcpy(row0 + dy, row, 48u);
            continue;
        }
        for (unsigned c = 0u; c < cell_w; c++) {
            uint8_t m = bad_bits ? (uint8_t)(1u << c) : (uint8_t)(16u >> c);
            uint32_t col = (c < 5u && (bits & m)) ? fg : bg;
            for (unsigned sx = 0u; sx < scale; sx++) span[c * scale + sx] = col;
        }
        for (unsigned sy = 0u; sy < scale; sy++)
            memcpy(row0 + (ptrdiff_t)sy * dy, span, (size_t)span_px * 4u);
    }
}

static inline void totk_ovl_cell_strided(
    uint32_t *origin, ptrdiff_t dx, ptrdiff_t dy, unsigned x, unsigned y,
    const uint8_t glyph[7], unsigned scale, unsigned cell_w,
    uint32_t fg, uint32_t bg, int skip_bg) {
    int bad_bits = totk_w40_fault() == 2;
    for (unsigned r = 0u; r < 7u; r++) {
        uint8_t bits = glyph[r];
        if (skip_bg && !bits) continue;
        for (unsigned sy = 0u; sy < scale; sy++) {
            uint32_t *dst = origin + (ptrdiff_t)(y + r * scale + sy) * dy + (ptrdiff_t)x * dx;
            for (unsigned c = 0u; c < cell_w; c++) {
                uint8_t m = bad_bits ? (uint8_t)(1u << c) : (uint8_t)(16u >> c);
                int is_fg = c < 5u && (bits & m);
                if (skip_bg && !is_fg) { dst += (ptrdiff_t)scale * dx; continue; }
                uint32_t col = is_fg ? fg : bg;
                for (unsigned sx = 0u; sx < scale; sx++) { *dst = col; dst += dx; }
            }
        }
    }
}

static inline void totk_ovl_raster_cell_fast(
    uint32_t *origin, ptrdiff_t dx, ptrdiff_t dy, unsigned x, unsigned y,
    const uint8_t glyph[7], unsigned scale, unsigned cell_w,
    uint32_t fg, uint32_t bg, int skip_bg) {
    if (dx == 1 && cell_w * scale <= 64u) {
        totk_ovl_cell_dx1(origin, dy, x, y, glyph, scale, cell_w, fg, bg, skip_bg);
        return;
    }
    totk_ovl_cell_strided(origin, dx, dy, x, y, glyph, scale, cell_w, fg, bg, skip_bg);
}

static inline void totk_ovl_raster_col_fast(
    uint32_t *origin, ptrdiff_t dx, ptrdiff_t dy,
    unsigned gx, unsigned gy, unsigned gh, unsigned scale,
    unsigned bar, unsigned line, uint32_t bar_col, uint32_t line_col,
    uint32_t bg, int skip_bg) {
    unsigned line_y = gh - 1u - line;
    unsigned bar_y = gh - bar;
    for (unsigned y = 0u; y < gh; y++) {
        uint32_t px = (y == line_y) ? line_col : (y >= bar_y ? bar_col : bg);
        if (skip_bg && px == bg) continue;
        for (unsigned sy = 0u; sy < scale; sy++) {
            uint32_t *dst = origin + (ptrdiff_t)((gy + y) * scale + sy) * dy + (ptrdiff_t)(gx * scale) * dx;
            if (dx == 1) {
                for (unsigned sx = 0u; sx < scale; sx++) dst[sx] = px;
            } else {
                for (unsigned sx = 0u; sx < scale; sx++) dst[(ptrdiff_t)sx * dx] = px;
            }
        }
    }
}

static inline int totk_ovl_read_fd_small(
    const char *root, const char *name, char *buf, size_t cap, size_t *out_len) {
    char path[512];
    if (!root || !name || cap < 4u) return -1;
    if (snprintf(path, sizeof path, "%s/%s", root, name) >= (int)sizeof path) return 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    size_t n = 0u;
    for (unsigned it = 0u; it < 64u && n < cap - 1u; it++) {
        ssize_t r = read(fd, buf + n, (cap - 1u) - n);
        if (r == 0) break;
        if (r < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return 0;
        }
        n += (size_t)r;
    }
    int trunc = 0;
    if (n == cap - 1u) {
        char probe = 0;
        ssize_t pr = read(fd, &probe, 1);
        if (pr > 0 || (pr < 0 && errno != EINTR)) trunc = 1;
    }
    close(fd);
    if (trunc) return -1;
    buf[n] = '\0';
    for (size_t i = 0u; i < n; i++) if (buf[i] == '\0') return -1;
    *out_len = n;
    return 1;
}

static inline int totk_ovl_parse_number_buf(
    const char *buf, size_t len, double *value) {
    size_t i = 0u;
    while (i < len && isspace((unsigned char)buf[i])) i++;
    if (i >= len) return 0;
    size_t p = i; int neg = 0;
    if (buf[p] == '-' || buf[p] == '+') { neg = (buf[p] == '-'); p++; }
    uint64_t iv = 0u; size_t nd = 0u;
    while (p < len && buf[p] >= '0' && buf[p] <= '9' && nd < 15u) {
        iv = iv * 10u + (uint64_t)(buf[p] - '0'); p++; nd++;
    }
    if (nd > 0u && (p == len || isspace((unsigned char)buf[p]))) {
        size_t q = p;
        while (q < len && isspace((unsigned char)buf[q])) q++;
        if (q == len) { *value = neg ? -(double)iv : (double)iv; return 1; }
    }
    errno = 0; char *end = NULL;
    double v = strtod(buf + i, &end);
    if (end == buf + i) return 0;
    if (totk_w40_fault() != 3) {
        for (size_t q = (size_t)(end - buf); q < len; q++)
            if (!isspace((unsigned char)buf[q])) return 0;
    }
    if (totk_w40_fault() != 5 && !isfinite(v)) return 0;
    *value = v;
    return 1;
}

static inline int totk_ovl_read_number_fast(
    const char *root, const char *name, double *value) {
    char buf[128]; size_t n = 0u;
    int st = totk_ovl_read_fd_small(root, name, buf, sizeof buf, &n);
    if (st == 0) return 0;
    if (st == 1) return totk_ovl_parse_number_buf(buf, n, value);
    return -1;
}

static inline int totk_ovl_read_text_line(
    const char *root, const char *name, char *out, size_t cap) {
    size_t n = 0u;
    int st = totk_ovl_read_fd_small(root, name, out, cap, &n);
    if (st <= 0) return st;
    for (size_t i = 0u; i < n; i++) {
        if (out[i] == '\r' || out[i] == '\n') { out[i] = '\0'; break; }
    }
    return 1;
}

static inline int totk_ovl_parse_kb_line(
    const char *line, size_t len, const char *key, size_t klen, unsigned *mib_out) {
    if (len < klen || memcmp(line, key, klen) != 0) return 0;
    size_t p = klen;
    while (p < len && isspace((unsigned char)line[p])) p++;
    if (p >= len) return -1;
    size_t q = p; uint64_t kb_i = 0u; size_t nd = 0u;
    while (q < len && line[q] >= '0' && line[q] <= '9' && nd < 13u) {
        kb_i = kb_i * 10u + (uint64_t)(line[q] - '0'); q++; nd++;
    }
    double kb = 0.0;
    if (nd > 0u && q < len && isspace((unsigned char)line[q])) {
        kb = (double)kb_i; p = q;
    } else {
        char tmp[128]; size_t tl = len - p < 127u ? len - p : 127u;
        memcpy(tmp, line + p, tl); tmp[tl] = '\0';
        char *end = NULL; kb = strtod(tmp, &end);
        if (end == tmp) return -1;
        p += (size_t)(end - tmp);
    }
    if (!isfinite(kb) || kb < 0.0 || kb >= (double)UINT_MAX * 1024.0 || floor(kb) != kb) return -1;
    while (p < len && isspace((unsigned char)line[p])) p++;
    if (p + 2u > len || line[p] != 'k' || line[p + 1u] != 'B') return -1;
    p += 2u;
    while (p < len && isspace((unsigned char)line[p])) p++;
    if (p != len) return -1;
    *mib_out = (unsigned)(kb / 1024.0);
    return 1;
}

static inline int totk_ovl_parse_meminfo_buf(
    const char *buf, size_t len, unsigned *total_mib, unsigned *avail_mib) {
    int seen_tot = 0, seen_av = 0; unsigned tv = UINT_MAX, av = UINT_MAX;
    for (size_t pos = 0u; pos < len;) {
        const char *nl = (const char *)memchr(buf + pos, '\n', len - pos);
        size_t eol = nl ? (size_t)(nl - buf) : len;
        size_t ll = eol - pos;
        unsigned v = UINT_MAX;
        int mt = totk_ovl_parse_kb_line(buf + pos, ll, "MemTotal:", 9u, &v);
        if (mt < 0 || (mt > 0 && seen_tot && totk_w40_fault() != 4)) return 0;
        if (mt > 0) { seen_tot = 1; tv = v; }
        int ma = totk_ovl_parse_kb_line(buf + pos, ll, "MemAvailable:", 13u, &v);
        if (ma < 0 || (ma > 0 && seen_av && totk_w40_fault() != 4)) return 0;
        if (ma > 0) { seen_av = 1; av = v; }
        pos = nl ? eol + 1u : len;
    }
    if (!seen_tot || !seen_av) return 0;
    *total_mib = tv; *avail_mib = av;
    return 1;
}

static inline int totk_ovl_parse_status_rss_buf(
    const char *buf, size_t len, unsigned *rss_mib) {
    int seen = 0; unsigned rv = UINT_MAX;
    for (size_t pos = 0u; pos < len;) {
        const char *nl = (const char *)memchr(buf + pos, '\n', len - pos);
        size_t eol = nl ? (size_t)(nl - buf) : len;
        unsigned v = UINT_MAX;
        int mr = totk_ovl_parse_kb_line(buf + pos, eol - pos, "VmRSS:", 6u, &v);
        if (mr < 0 || (mr > 0 && seen && totk_w40_fault() != 4)) return 0;
        if (mr > 0) { seen = 1; rv = v; }
        pos = nl ? eol + 1u : len;
    }
    if (!seen) return 0;
    *rss_mib = rv;
    return 1;
}
