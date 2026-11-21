/* Straight-line SPIR-V coverage certificates. Unknown values never certify a
   position. No shader names, guessed vertex layouts or bounds are trusted. */
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "vk_spv_fast.h"

typedef struct { uint8_t position_attr; uint32_t full_outputs; } vk_load_certificate;
typedef struct {
    uint32_t type, storage, base, member, location, builtin, width, literal;
    uint32_t component[4]; /* 1/2: input xy, 3: zero, 4: one; 0 unknown */
    int32_t integer;
} vk_load_value;
static inline int vk_load_step(
    const uint32_t *w, size_t p, size_t n, uint32_t bound, int fragment,
    vk_load_value *v, vk_load_certificate *res, uint32_t position[4], uint32_t *attr,
    uint32_t *functions, uint32_t *labels, unsigned *out_z) {
    unsigned op = w[p] & 65535u, z = w[p] >> 16; const uint32_t *a = w + p;
    if (!z || z > n - p) return 1;
    *out_z = z;
#define ID(x) ((x) < bound)
    if (op == 71u && z >= 4u && ID(a[1])) { if (a[2] == 30u) v[a[1]].location = a[3]; if (a[2] == 11u) v[a[1]].builtin = a[3]; if (a[2] == 31u && a[3] != 0u) return 1; }
    else if (op == 72u && z >= 5u && ID(a[1]) && a[3] == 11u && a[4] == 0u && a[2] == 0u) v[a[1]].builtin = 0u;
    else if (op == 22u && z == 3u && ID(a[1]) && a[2] == 32u) v[a[1]].width = 1u;
    else if (op == 23u && z == 4u && ID(a[1]) && ID(a[2]) && v[a[2]].width == 1u && a[3] <= 4u) v[a[1]].width = a[3];
    else if (op == 32u && z == 4u && ID(a[1]) && ID(a[3])) { v[a[1]].type = a[3]; v[a[1]].storage = a[2]; }
    else if (op == 43u && z == 4u && ID(a[1]) && ID(a[2])) { v[a[2]].integer = (int32_t)a[3]; v[a[2]].literal = 1u; v[a[2]].width = v[a[1]].width; v[a[2]].component[0] = a[3] == 0u ? 3u : a[3] == 0x3f800000u ? 4u : 0u; }
    else if (op == 59u && z >= 4u && ID(a[1]) && ID(a[2])) { v[a[2]].type = v[a[1]].type; v[a[2]].storage = a[3]; v[a[2]].base = a[2]; }
    else if (op == 65u && z == 5u && ID(a[1]) && ID(a[2]) && ID(a[3]) && ID(a[4]) && v[a[4]].literal) { v[a[2]].base = v[a[3]].base; v[a[2]].storage = v[a[3]].storage; v[a[2]].type = v[a[1]].type; v[a[2]].member = (uint32_t)v[a[4]].integer; }
    else if (op == 61u && z >= 4u && ID(a[1]) && ID(a[2]) && ID(a[3])) {
        uint32_t b = v[a[3]].base; v[a[2]].width = v[a[1]].width;
        if (!fragment && v[a[3]].storage == 1u && (!ID(b) || v[b].builtin != UINT32_MAX)) return 1;
        if (ID(b) && v[a[3]].storage == 1u && v[b].location < 16u && v[a[3]].member == UINT32_MAX) {
            if (*attr != UINT32_MAX && *attr != v[b].location && !fragment) return 1;
            *attr = v[b].location; for (unsigned k = 0u; k < v[a[2]].width && k < 2u; k++) v[a[2]].component[k] = k + 1u;
        }
    } else if (op == 81u && z == 5u && ID(a[1]) && ID(a[2]) && ID(a[3]) && a[4] < 4u) { v[a[2]].width = 1u; v[a[2]].component[0] = v[a[3]].component[a[4]]; }
    else if (op == 80u && z >= 4u && ID(a[1]) && ID(a[2])) { unsigned k = 0u; v[a[2]].width = v[a[1]].width; for (unsigned j = 3u; j < z && k < 4u; j++) { if (!ID(a[j]) || !v[a[j]].width) break; for (unsigned q = 0u; q < v[a[j]].width && k < 4u; q++) v[a[2]].component[k++] = v[a[j]].component[q]; } }
    else if (op == 62u && z >= 3u && ID(a[1]) && ID(a[2])) {
        uint32_t b = v[a[1]].base;
        if (ID(b) && v[a[1]].storage == 3u) {
            if (fragment) { if (v[b].location < 8u && v[a[1]].member == UINT32_MAX && v[v[a[1]].type].width == 4u) res->full_outputs |= 1u << v[b].location; else return 1; }
            else if ((v[b].builtin == 0u && v[a[1]].member == UINT32_MAX) || (v[v[b].type].builtin == 0u && v[a[1]].member == 0u)) memcpy(position, v[a[2]].component, 16u);
            else if (v[b].builtin != UINT32_MAX || v[a[1]].member != UINT32_MAX) return 1;
        } else if (!fragment) return 1;
    } else if (op == 54u) (*functions)++;
    else if (op == 248u) (*labels)++;
    else if ((!fragment && (op == 12u || op == 63u || op == 64u || (op >= 227u && op <= 242u))) || op == 57u || op == 249u || op == 250u || op == 251u || op == 252u || op == 255u || op == 4416u || op == 5380u) return 1;
#undef ID
    return 0;
}
static inline vk_load_certificate vk_load_certify(const uint32_t *w, size_t n, int fragment) {
    vk_load_certificate result = {0}; vk_load_value stk[TOTK_SPV_CERT_STACK];
    if (n < 5u || w[0] != 0x07230203u || !w[3] || w[3] > 65536u) return result;
    uint32_t bound = w[3], functions = 0u, labels = 0u, position[4] = {0}, attr = UINT32_MAX;
    int heap = !(totk_vk_spv_fast_on() && bound <= TOTK_SPV_CERT_STACK);
    vk_load_value *v = heap ? (vk_load_value *)calloc(bound, sizeof *v) : stk;
    if (!v) return result;
    if (!heap) memset(v, 0, bound * sizeof *v);
    for (uint32_t i = 0u; i < bound; i++) v[i].location = v[i].builtin = v[i].member = UINT32_MAX;
    int bad = 0;
    for (size_t p = 5u; p < n && !bad;) { unsigned z = 0u; bad = vk_load_step(w, p, n, bound, fragment, v, &result, position, &attr, &functions, &labels, &z); p += z; }
    if (bad || functions != 1u || labels != 1u) result = (vk_load_certificate){0};
    else if (!fragment && attr < 16u && position[0] == 1u && position[1] == 2u && position[2] == 3u && position[3] == 4u) result.position_attr = (uint8_t)(attr + 1u);
    if (heap) free(v);
    return result;
}
