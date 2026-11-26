/* vk_depth_spv.h: remove the NEAR_IS_MINUS_W clip-depth conversion from a translated vertex shader.
 *
 * The translator ends a vertex shader with  gl_Position.z = fma(gl_Position.z, 0.5, gl_Position.w * 0.5)  when asked for
 * depth_minus_w. A seed built with that flag for a game that selects NEAR_IS_ZERO puts every seed shader's depth in the front
 * half of the range, in front of everything translated at run time. This finds that one epilogue and makes it the identity,
 * fma(z, 1.0, 0.0), by swapping two operand ids: no instruction moves, the word count is unchanged.
 *
 * Returns 1 when the epilogue was neutralised, 0 when the module has none (nothing changed), -1 when the module is not
 * understood or the match is not unique (nothing changed).
 * Admitted pattern: the last nine instructions before an OpReturn, exactly as the translator writes them:
 *     m = OpFMul x, 0.5;  OpStore T, m;  a = OpLoad Z;  c = OpLoad T;  r = Fma a, 0.5, c;  OpStore R, r;
 *     p = OpAccessChain <Output variable>, ..., <int constant 2>;  v = OpLoad R;  OpStore p, v;  OpReturn
 * with float constants 0.0 and 1.0 present in the module and exactly one such site.
 * Authored-by: CPCS */
#ifndef VK_DEPTH_SPV_H
#define VK_DEPTH_SPV_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static inline int vk_spirv_strip_minus_w(uint32_t *w, size_t n) {
    if (!w || n < 5 || w[0] != 0x07230203u) return -1;
    uint32_t bound = w[3]; if (bound == 0 || bound > (1u << 24)) return -1;
    uint8_t *kind = calloc(bound, 1);          /* 1 float32 type, 2 int32 type, 3 Output variable, 4 int constant 2 */
    size_t *at = malloc((n + 1) * sizeof *at); /* offset of each instruction, in order */
    if (!kind || !at) { free(kind); free(at); return -1; }
    uint32_t glsl = 0, c_half = 0, c_zero = 0, c_one = 0; int rc = -1, sites = 0; size_t site = 0, ni = 0;
    for (size_t i = 5; i < n;) {
        uint32_t wc = w[i] >> 16, op = w[i] & 0xffffu;
        if (wc == 0 || i + wc > n) goto done;
        at[ni++] = i;
        if (op == 11 && wc >= 3 && (wc - 2) * 4 >= 13 && !memcmp(&w[i + 2], "GLSL.std.450", 13)) glsl = w[i + 1];
        else if (op == 22 && wc == 3 && w[i + 2] == 32 && w[i + 1] < bound) kind[w[i + 1]] = 1;
        else if (op == 21 && wc == 4 && w[i + 2] == 32 && w[i + 1] < bound) kind[w[i + 1]] = 2;
        else if (op == 43 && wc == 4 && w[i + 1] < bound && w[i + 2] < bound) {
            if (kind[w[i + 1]] == 1) {
                if (w[i + 3] == 0x3f000000u && !c_half) c_half = w[i + 2];
                else if (w[i + 3] == 0x00000000u && !c_zero) c_zero = w[i + 2];
                else if (w[i + 3] == 0x3f800000u && !c_one) c_one = w[i + 2];
            } else if (kind[w[i + 1]] == 2 && w[i + 3] == 2) kind[w[i + 2]] = 4;
        }
        else if (op == 59 && wc >= 4 && w[i + 3] == 3 && w[i + 2] < bound) kind[w[i + 2]] = 3;
        i += wc;
    }
    if (!glsl || !c_half) { rc = 0; goto done; }
#define OPC(k) (w[at[k]] & 0xffffu)
#define WCN(k) (w[at[k]] >> 16)
#define ARG(k, j) (w[at[k] + (j)])
    for (size_t k = 9; k < ni; k++) {
        if (OPC(k) != 253) continue;                                                     /* OpReturn */
        size_t m = k - 9, st = k - 8, la = k - 7, lc = k - 6, f = k - 5, sr = k - 4, ac = k - 3, lv = k - 2, so = k - 1;
        if (OPC(m) != 133 || WCN(m) != 5 || (ARG(m, 3) != c_half && ARG(m, 4) != c_half)) continue;
        if (OPC(st) != 62 || WCN(st) < 3 || ARG(st, 2) != ARG(m, 2)) continue;
        if (OPC(la) != 61 || WCN(la) < 4 || OPC(lc) != 61 || WCN(lc) < 4 || ARG(lc, 3) != ARG(st, 1)) continue;
        if (OPC(f) != 12 || WCN(f) != 8 || ARG(f, 3) != glsl || ARG(f, 4) != 50) continue;      /* GLSL.std.450 Fma (50) */
        if (ARG(f, 5) != ARG(la, 2) || ARG(f, 6) != c_half || ARG(f, 7) != ARG(lc, 2)) continue;
        if (OPC(sr) != 62 || WCN(sr) < 3 || ARG(sr, 2) != ARG(f, 2)) continue;
        if (OPC(ac) != 65 || WCN(ac) < 5 || ARG(ac, 3) >= bound || kind[ARG(ac, 3)] != 3) continue;
        uint32_t last = ARG(ac, WCN(ac) - 1); if (last >= bound || kind[last] != 4) continue;  /* component z */
        if (OPC(lv) != 61 || WCN(lv) < 4 || ARG(lv, 3) != ARG(sr, 1)) continue;
        if (OPC(so) != 62 || WCN(so) < 3 || ARG(so, 1) != ARG(ac, 2) || ARG(so, 2) != ARG(lv, 2)) continue;
        sites++; site = at[f];
    }
#undef OPC
#undef WCN
#undef ARG
    if (sites == 0) { rc = 0; goto done; }
    if (sites != 1 || !c_zero || !c_one) goto done;
    w[site + 6] = c_one; w[site + 7] = c_zero; rc = 1;   /* fma(z, 1.0, 0.0) */
done:
    free(kind); free(at); return rc;
}
#endif
