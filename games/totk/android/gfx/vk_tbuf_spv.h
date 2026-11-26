/* Retype sampled-2D bindings that the game feeds with BUFFER textures into texel buffers.

   The translator emits a buffer-texture read as texelFetch(sampler2D, ivec2(x, 0), 0). A buffer texture
   has no image here (vk_res.c serves it only as a VkBufferView), so such a slot got the all-zero dummy:
   skinning palettes, per-batch tables and light lists all read zeros. This rewrite turns each chosen
   binding into a samplerBuffer and each of its fetches into texelFetch(samplerBuffer, x).

   Admitted shape only, as observed in the translator's output:
     OpVariable (UniformConstant, set 0, chosen binding) of pointer -> OpTypeSampledImage -> OpTypeImage 2D,
       not depth, not arrayed, not multisampled, sampled;
     every use of the variable is an OpLoad, every use of that value an OpImage, every use of that image an
       OpImageFetch whose coordinate is an OpCompositeConstruct of exactly two components and whose image
       operands are absent or a Lod.
   Anything else refuses the whole module: a half-retyped shader would be worse than the dummy. */
#ifndef TOTK_VK_TBUF_SPV_H
#define TOTK_VK_TBUF_SPV_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { VK_TBUF_SPV_MAX_BINDINGS = 16, VK_TBUF_SPV_MAX_TYPES = 4, VK_TBUF_SPV_MAX_BOUND = 1 << 20 };

/* Whether an instruction that takes ids at w[i+first..] mentions an id for which mark[] is set. */
static inline int vk_tbuf_spv_uses(const uint32_t *w, size_t i, uint32_t n, uint32_t first, const uint8_t *mark, uint32_t bound, uint8_t which) {
    for (uint32_t k = first; k < n; k++) if (w[i + k] < bound && (mark[w[i + k]] & which)) return 1;
    return 0;
}

/* 0: none of the bindings is a sampled 2D image of set 0 (nothing to do); n > 0: n variables retyped;
   -1: malformed module, unsupported use, or allocation failure. No mutation on 0 or -1. On success *words is a
   new malloc block (the old one is freed) and *count its length. */
static inline int vk_spirv_tbuf(uint32_t **words, size_t *count, const uint32_t *bindings, int nb) {
    const uint32_t *w = *words; size_t nw = *count;
    if (nb <= 0) return 0;
    if (nb > VK_TBUF_SPV_MAX_BINDINGS || nw < 5 || w[0] != 0x07230203 || !w[3] || w[3] > VK_TBUF_SPV_MAX_BOUND) return -1;
    uint32_t bound = w[3];
    size_t ids = (size_t)bound + 3 * VK_TBUF_SPV_MAX_TYPES;   /* the new type ids index the same tables */
    /* per id: the instruction offset of its type declaration (types), its Binding + 1 and DescriptorSet + 1
       (decorations), and its role in the rewrite (mark) */
    uint32_t *decl = (uint32_t *)calloc(ids, sizeof *decl), *bind = (uint32_t *)calloc(ids, sizeof *bind);
    uint32_t *set = (uint32_t *)calloc(ids, sizeof *set), *aux = (uint32_t *)calloc(ids, sizeof *aux);
    uint8_t *mark = (uint8_t *)calloc(ids, 1);
    enum { M_VAR = 1, M_LOAD = 2, M_IMAGE = 4 };
    uint32_t *out = NULL; int result = -1, nvar = 0, has_cap = 0;
    uint32_t old_im[VK_TBUF_SPV_MAX_TYPES], new_im[VK_TBUF_SPV_MAX_TYPES], new_si[VK_TBUF_SPV_MAX_TYPES], new_pt[VK_TBUF_SPV_MAX_TYPES];
    uint8_t mk_im[VK_TBUF_SPV_MAX_TYPES], mk_si[VK_TBUF_SPV_MAX_TYPES], mk_pt[VK_TBUF_SPV_MAX_TYPES];
    int ntypes = 0; size_t first_var = 0, last_cap_end = 0;
    if (!decl || !bind || !set || !aux || !mark) goto done;

    /* pass 1: declarations and decorations */
    for (size_t i = 5; i < nw; ) {
        uint32_t n = w[i] >> 16, op = w[i] & 65535;
        if (!n || n > nw - i) goto done;
        if (op == 17 && n == 2) { if (w[i + 1] == 46) has_cap = 1; last_cap_end = i + n; }
        if (op == 71 && n == 4 && w[i + 1] < bound) {
            if (w[i + 2] == 33) bind[w[i + 1]] = w[i + 3] + 1;
            if (w[i + 2] == 34) set[w[i + 1]] = w[i + 3] + 1;
        }
        if ((op == 25 && n >= 9) || (op == 27 && n == 3) || (op == 32 && n == 4)) { if (w[i + 1] >= bound) goto done; decl[w[i + 1]] = (uint32_t)i; }
        if (op == 59 && n >= 4 && !first_var) first_var = i;
        if (op == 54) break;   /* functions: no more declarations */
        i += n;
    }
    if (!first_var || !last_cap_end) goto done;

    /* pass 2: the chosen variables */
    for (size_t i = first_var; i < nw; ) {
        uint32_t n = w[i] >> 16, op = w[i] & 65535;
        if (!n || n > nw - i) goto done;
        if (op == 54) break;
        if (op == 59 && n >= 4 && w[i + 3] == 0 && w[i + 2] < bound && bind[w[i + 2]]) {
            int chosen = 0; for (int b = 0; b < nb; b++) if (bindings[b] + 1 == bind[w[i + 2]]) chosen = 1;
            if (chosen) {
                if (set[w[i + 2]] > 1) goto done;                        /* set 0 only */
                uint32_t pt = w[i + 1]; size_t dp = pt < bound ? decl[pt] : 0;
                if (!dp || (w[dp] & 65535) != 32 || w[dp + 2] != 0) goto done;
                uint32_t si = w[dp + 3]; size_t ds = si < bound ? decl[si] : 0;
                if (!ds || (w[ds] & 65535) != 27) goto done;             /* not a sampled image: refuse */
                uint32_t im = w[ds + 2]; size_t di = im < bound ? decl[im] : 0;
                if (!di || (w[di] & 65535) != 25) goto done;
                /* OpTypeImage: result, sampled type, dim, depth, arrayed, ms, sampled, format */
                if (w[di + 3] != 1 || w[di + 4] != 0 || w[di + 5] != 0 || w[di + 6] != 0 || w[di + 7] != 1) goto done;
                int t = 0; while (t < ntypes && old_im[t] != im) t++;
                if (t == ntypes) { if (ntypes == VK_TBUF_SPV_MAX_TYPES) goto done; old_im[ntypes++] = im; }
                mark[w[i + 2]] |= M_VAR; aux[w[i + 2]] = (uint32_t)t; nvar++;
            }
        }
        i += n;
    }
    if (!nvar) { result = 0; goto done; }
    if (nvar != nb) goto done;   /* every chosen binding must be such a variable (bindings are distinct): the caller
                                    relabels all of them, so a partial retype would leave its reflection wrong */

    /* an existing identical Buffer image / sampled image / pointer type is reused: SPIR-V forbids duplicates */
    for (int t = 0; t < ntypes; t++) {
        size_t di = decl[old_im[t]]; new_im[t] = new_si[t] = new_pt[t] = 0;
        for (size_t i = 5; i < nw; ) {
            uint32_t n = w[i] >> 16, op = w[i] & 65535;
            if (op == 54) break;
            if (op == 25 && n == (w[di] >> 16) && w[i + 2] == w[di + 2] && w[i + 3] == 5 && !w[i + 4] && !w[i + 5] && !w[i + 6] &&
                w[i + 7] == 1 && w[i + 8] == w[di + 8] && n == 9) new_im[t] = w[i + 1];
            if (op == 27 && new_im[t] && w[i + 2] == new_im[t]) new_si[t] = w[i + 1];
            if (op == 32 && new_si[t] && w[i + 2] == 0 && w[i + 3] == new_si[t]) new_pt[t] = w[i + 1];
            i += n;
        }
        if ((w[di] >> 16) != 9) goto done;                               /* an access qualifier: not the observed shape */
        mk_im[t] = !new_im[t]; mk_si[t] = !new_si[t]; mk_pt[t] = !new_pt[t];
        if (mk_im[t]) new_im[t] = bound++;
        if (mk_si[t]) new_si[t] = bound++;
        if (mk_pt[t]) new_pt[t] = bound++;
    }
    /* a reused type must be declared before the first variable, where the new ones go; it is (types precede variables
       of their type), but a reused pointer declared after first_var would be used before its declaration */
    for (int t = 0; t < ntypes; t++) if (!mk_pt[t] && decl[new_pt[t]] > first_var) goto done;

    size_t cap = nw + (size_t)ntypes * 16 + 2;
    out = (uint32_t *)malloc(cap * sizeof *out);
    if (!out) goto done;
    memcpy(out, w, 20); out[3] = bound;
    size_t k = 5; int in_fn = 0;
    for (size_t i = 5; i < nw; ) {
        uint32_t n = w[i] >> 16, op = w[i] & 65535;
        if (!n || n > nw - i) goto fail;
        if (i == last_cap_end && !has_cap) { out[k++] = (2u << 16) | 17; out[k++] = 46; }   /* SampledBuffer */
        if (i == first_var) for (int t = 0; t < ntypes; t++) {
            size_t di = decl[old_im[t]];
            if (mk_im[t]) { out[k++] = (9u << 16) | 25; out[k++] = new_im[t]; out[k++] = w[di + 2]; out[k++] = 5; out[k++] = 0; out[k++] = 0; out[k++] = 0; out[k++] = 1; out[k++] = w[di + 8]; }
            if (mk_si[t]) { out[k++] = (3u << 16) | 27; out[k++] = new_si[t]; out[k++] = new_im[t]; }
            if (mk_pt[t]) { out[k++] = (4u << 16) | 32; out[k++] = new_pt[t]; out[k++] = 0; out[k++] = new_si[t]; }
        }
        if (op == 54) in_fn = 1;
        if (op == 59 && n >= 4 && w[i + 2] < bound && (mark[w[i + 2]] & M_VAR) && !in_fn) {
            memcpy(out + k, w + i, n * 4); out[k + 1] = new_pt[aux[w[i + 2]]]; k += n; i += n; continue;
        }
        if (op == 61 && n >= 4 && w[i + 3] < bound && (mark[w[i + 3]] & M_VAR)) {       /* OpLoad of a retyped variable */
            if (w[i + 2] >= bound) goto fail;
            uint32_t t = aux[w[i + 3]]; mark[w[i + 2]] |= M_LOAD; aux[w[i + 2]] = t;
            memcpy(out + k, w + i, n * 4); out[k + 1] = new_si[t]; k += n; i += n; continue;
        }
        if (op == 100 && n == 4 && w[i + 3] < bound && (mark[w[i + 3]] & M_LOAD)) {     /* OpImage of that value */
            if (w[i + 2] >= bound) goto fail;
            uint32_t t = aux[w[i + 3]]; mark[w[i + 2]] |= M_IMAGE; aux[w[i + 2]] = t;
            memcpy(out + k, w + i, n * 4); out[k + 1] = new_im[t]; k += n; i += n; continue;
        }
        if (op == 80 && n == 5 && w[i + 2] < bound) decl[w[i + 2]] = (uint32_t)i;      /* two-component construct: remember it */
        if (op == 95 && n >= 5 && w[i + 3] < bound && (mark[w[i + 3]] & M_IMAGE)) {     /* OpImageFetch on a retyped image */
            uint32_t c = w[i + 4]; size_t dc = c < bound ? decl[c] : 0;
            if (!dc || (w[dc] & 65535) != 80 || (w[dc] >> 16) != 5) goto fail;
            if (!(n == 5 || (n == 7 && w[i + 5] == 2))) goto fail;                      /* no operands, or a Lod */
            out[k++] = (5u << 16) | 95; out[k++] = w[i + 1]; out[k++] = w[i + 2]; out[k++] = w[i + 3]; out[k++] = w[dc + 3];
            i += n; continue;
        }
        /* any other use of a retyped variable, value or image is outside the admitted shape */
        if (in_fn) {
            if (op >= 86 && op <= 109 && n >= 4 && w[i + 3] < bound && (mark[w[i + 3]] & (M_LOAD | M_IMAGE))) goto fail;
            uint32_t first = op == 57 ? 4 : op == 62 || op == 63 ? 1 : op == 65 || op == 66 || op == 83 || op == 169 || op == 245 ? 3 : 0;
            if (first && vk_tbuf_spv_uses(w, i, n, first, mark, w[3], M_VAR | M_LOAD | M_IMAGE)) goto fail;
        }
        if (k + n > cap) goto fail;
        memcpy(out + k, w + i, n * 4); k += n; i += n;
    }
    free(*words); *words = out; *count = k; out = NULL; result = nvar;
    goto done;
fail:
    free(out); out = NULL; result = -1;
done:
    free(out); free(decl); free(bind); free(set); free(aux); free(mark);
    return result;
}
#endif
