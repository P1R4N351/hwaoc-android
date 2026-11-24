// SPDX-License-Identifier: MIT
#pragma once
// Native Vulkan HUD minimap & objective redirection (2-slot staging rings) + one-shot UI texture harvest.
// Included only by vk_draw.c; exports vk_minimap_* for companion_jni.c.

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    VK_MM_MIN_SIDE = 96u,
    VK_MM_MAX_SIDE = 768u,
    VK_MM_SAVE_W = 896u,
    VK_MM_SAVE_H = 896u,
    VK_OBJ_MAX_W = 768u,
    VK_OBJ_MAX_H = 224u,
    VK_UI_TEX_SIDE = 512u,
    VK_MM_SLOTS = 2,
    VK_MM_MAX_CAND = 8,
    VK_MM_TTL = 3u
};
#define VK_MM_BUF_BYTES ((VkDeviceSize)(VK_MM_MAX_SIDE * VK_MM_MAX_SIDE * 4u))
#define VK_OBJ_BUF_BYTES ((VkDeviceSize)(VK_OBJ_MAX_W * VK_OBJ_MAX_H * 4u))
#define VK_UI_BUF_BYTES ((VkDeviceSize)(VK_UI_TEX_SIDE * VK_UI_TEX_SIDE * 4u))

typedef struct { VkBuffer buf; VkDeviceMemory mem; uint8_t *ptr; } vk_mm_slot;
typedef struct { uint32_t w, h; VkFormat fmt; uint64_t serial; } vk_mm_cand;
typedef struct { int32_t x, y; uint32_t w, h; } vk_mm_rect;
typedef struct {
    VkImage img; uint32_t w, h; VkFormat fmt; uint64_t serial; int score;
} vk_ui_cand;

typedef struct {
    vk_mm_slot slot[VK_MM_SLOTS];
    VkDeviceSize slot_bytes;
    int ring_ready, ring_failed;
    _Atomic int pending_valid;
    int write_slot, pending_slot, ready_slot, wrote_in_frame;
    uint32_t pending_frame, write_w, write_h, pending_w, pending_h, ready_w, ready_h, seq;
    VkFormat write_fmt, pending_fmt, ready_fmt;
    uint64_t pending_fence;
} vk_mm_stream;

static struct {
    pthread_mutex_t mu;
    vk_mm_stream mm, obj;
    vk_mm_slot ui_slot;
    VkImage ui_img;
    VkDeviceMemory ui_mem;
    int ui_ready, ui_failed, ui_wrote_frame, ui_pending, ui_has_ready;
    uint32_t ui_pending_frame, ui_seq;
    uint64_t ui_pending_fence, ui_seen[8];
    int ui_seen_n, ui_seen_pos;
    _Atomic int ui_want;
    vk_ui_cand ui_cand;
    VkImage save_img;
    VkDeviceMemory save_mem;
    VkFormat save_fmt;
    int save_ready, save_failed;
    vk_mm_cand cand[VK_MM_MAX_CAND];
    _Atomic int cand_count, want_active;
    _Atomic uint32_t cand_idx;
    uint32_t frame_counter, mm_ttl, obj_ttl;
    int saw_mm_frame, saw_obj_frame;
    int in_hud_pass, pass_opaque_fs, pass_mm_hit, pass_obj_hit, pc_mm_done, pc_obj_done;
    VkImage pass_im, saved_hit_img;
    uint32_t frame_got_mm, frame_got_obj, pass_w, pass_h;
    vk_mm_rect save_box, pc_mm, pc_obj, hit_mm, hit_obj;
    uint32_t learn_mm_w, learn_mm_h;
    int32_t learn_mm_cx, learn_mm_cy, learn_mm_half;
    uint32_t learn_obj_w, learn_obj_h;
    vk_mm_rect learn_obj;
    float obj_umin_x, obj_umin_y, obj_umax_x, obj_umax_y;
} g_vk_mm = {
    .mu = PTHREAD_MUTEX_INITIALIZER,
    .mm = { .slot_bytes = VK_MM_BUF_BYTES, .pending_slot = -1, .ready_slot = -1 },
    .obj = { .slot_bytes = VK_OBJ_BUF_BYTES, .pending_slot = -1, .ready_slot = -1 },
};

static int vk_mm_supported_fmt(VkFormat fmt) {
    return fmt == VK_FORMAT_R8G8B8A8_UNORM || fmt == VK_FORMAT_R8G8B8A8_SRGB
        || fmt == VK_FORMAT_B8G8R8A8_UNORM || fmt == VK_FORMAT_B8G8R8A8_SRGB;
}

static int vk_mm_is_color4_fmt(VkFormat f) {
    if (f == VK_FORMAT_R8_UNORM || f == VK_FORMAT_R8_SNORM || f == VK_FORMAT_R16_UNORM
            || f == VK_FORMAT_R16_SFLOAT || f == VK_FORMAT_R32_SFLOAT
            || f == VK_FORMAT_R8G8_UNORM || f == VK_FORMAT_R8G8_SNORM || f == VK_FORMAT_R16G16_UNORM
            || f == VK_FORMAT_R16G16_SFLOAT || f == VK_FORMAT_R32G32_SFLOAT
            || f == VK_FORMAT_BC4_UNORM_BLOCK || f == VK_FORMAT_BC4_SNORM_BLOCK
            || f == VK_FORMAT_BC5_UNORM_BLOCK || f == VK_FORMAT_BC5_SNORM_BLOCK) return 0;
    return f != VK_FORMAT_UNDEFINED;
}

static int vk_mm_find_cand(uint32_t w, uint32_t h, VkFormat fmt, uint64_t serial) {
    int cnt = atomic_load_explicit(&g_vk_mm.cand_count, memory_order_relaxed);
    for (int i = 0; i < cnt && i < VK_MM_MAX_CAND; i++) {
        vk_mm_cand *c = &g_vk_mm.cand[i];
        if (c->w == w && c->h == h && c->fmt == fmt) { c->serial = serial; return i; }
    }
    if (cnt >= VK_MM_MAX_CAND) return -1;
    g_vk_mm.cand[cnt] = (vk_mm_cand){w, h, fmt, serial};
    atomic_store_explicit(&g_vk_mm.cand_count, cnt + 1, memory_order_release);
    LOGV("[companion-rt] HUD minimap stream #%d: %ux%u fmt=%d", cnt, w, h, (int)fmt);
    return cnt;
}

static void vk_mm_free_slot(vk_mm_slot *s) {
    if (s->ptr && s->mem) vkUnmapMemory(V.dev, s->mem);
    if (s->buf) vkDestroyBuffer(V.dev, s->buf, NULL);
    if (s->mem) vk_memory_release(s->mem, VK_MEMORY_READBACK);
    memset(s, 0, sizeof *s);
}

static int vk_mm_alloc_slot(vk_mm_slot *s, VkDeviceSize sz) {
    VkBufferCreateInfo bi = {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sz, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT
    };
    if (vkCreateBuffer(V.dev, &bi, NULL, &s->buf)) return 0;
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(V.dev, s->buf, &mr);
    int t = vk_pick_type(mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    if (t < 0) t = vk_pick_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (t < 0) { vk_mm_free_slot(s); return 0; }
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size, .memoryTypeIndex = (uint32_t)t};
    void *p = NULL;
    if (vk_memory_allocate(&ai, &s->mem, VK_MEMORY_READBACK)
            || vkBindBufferMemory(V.dev, s->buf, s->mem, 0)
            || vkMapMemory(V.dev, s->mem, 0, VK_WHOLE_SIZE, 0, &p)) {
        vk_mm_free_slot(s);
        return 0;
    }
    s->ptr = (uint8_t *)p;
    return 1;
}

static int vk_mm_ensure_stream_ring(vk_mm_stream *st) {
    if (st->ring_ready) return 1;
    if (st->ring_failed || !V.dev) return 0;
    for (int i = 0; i < VK_MM_SLOTS; i++) {
        if (!vk_mm_alloc_slot(&st->slot[i], st->slot_bytes)) {
            for (int j = 0; j < i; j++) vk_mm_free_slot(&st->slot[j]);
            st->ring_failed = 1;
            return 0;
        }
    }
    st->ring_ready = 1;
    return 1;
}

static int vk_mm_create_img(
    VkCommandBuffer cb, uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags usage,
    VkImage *im_out, VkDeviceMemory *mem_out) {
    VkImageCreateInfo ci = {
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = fmt,
        .extent = {w, h, 1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    if (vkCreateImage(V.dev, &ci, NULL, im_out)) return 0;
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(V.dev, *im_out, &mr);
    int t = vk_pick_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (t < 0) t = vk_pick_type(mr.memoryTypeBits, 0);
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size, .memoryTypeIndex = (uint32_t)(t < 0 ? 0 : t)};
    if (t < 0 || vk_memory_allocate(&ai, mem_out, VK_MEMORY_DIAGNOSTIC) || vkBindImageMemory(V.dev, *im_out, *mem_out, 0)) {
        if (*im_out) vkDestroyImage(V.dev, *im_out, NULL);
        if (*mem_out) vk_memory_release(*mem_out, VK_MEMORY_DIAGNOSTIC);
        *im_out = VK_NULL_HANDLE; *mem_out = VK_NULL_HANDLE;
        return 0;
    }
    VkImageMemoryBarrier2 ib = {
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = *im_out, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
    };
    VkDependencyInfo di = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &ib};
    vkCmdPipelineBarrier2(cb, &di);
    return 1;
}

static int vk_mm_ensure_save(VkCommandBuffer cb, VkFormat fmt) {
    if (g_vk_mm.save_ready) {
        if (g_vk_mm.save_fmt == fmt) return 1;
        vkDestroyImage(V.dev, g_vk_mm.save_img, NULL);
        vk_memory_release(g_vk_mm.save_mem, VK_MEMORY_DIAGNOSTIC);
        g_vk_mm.save_img = VK_NULL_HANDLE; g_vk_mm.save_mem = VK_NULL_HANDLE; g_vk_mm.save_ready = 0;
    }
    if (g_vk_mm.save_failed || !V.dev || !cb) return 0;
    if (!vk_mm_create_img(cb, VK_MM_SAVE_W, VK_MM_SAVE_H, fmt,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            &g_vk_mm.save_img, &g_vk_mm.save_mem)) {
        g_vk_mm.save_failed = 1;
        return 0;
    }
    g_vk_mm.save_fmt = fmt;
    g_vk_mm.save_ready = 1;
    return 1;
}

static vk_mm_rect vk_mm_clamp_rect(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const vk_mm_rect *lim) {
    int32_t lx0 = lim->x, ly0 = lim->y, lx1 = lim->x + (int32_t)lim->w, ly1 = lim->y + (int32_t)lim->h;
    if (x0 < lx0) x0 = lx0; if (y0 < ly0) y0 = ly0;
    if (x1 > lx1) x1 = lx1; if (y1 > ly1) y1 = ly1;
    return (vk_mm_rect){x0, y0, (uint32_t)(x1 > x0 ? x1 - x0 : 0), (uint32_t)(y1 > y0 ? y1 - y0 : 0)};
}

static vk_mm_rect vk_mm_union_rect(const vk_mm_rect *a, const vk_mm_rect *b) {
    if (!a->w || !a->h) return *b;
    if (!b->w || !b->h) return *a;
    int32_t x0 = a->x < b->x ? a->x : b->x, y0 = a->y < b->y ? a->y : b->y;
    int32_t x1 = (a->x + (int32_t)a->w) > (b->x + (int32_t)b->w) ? (a->x + (int32_t)a->w) : (b->x + (int32_t)b->w);
    int32_t y1 = (a->y + (int32_t)a->h) > (b->y + (int32_t)b->h) ? (a->y + (int32_t)a->h) : (b->y + (int32_t)b->h);
    return (vk_mm_rect){x0, y0, (uint32_t)(x1 - x0), (uint32_t)(y1 - y0)};
}

static vk_mm_rect vk_mm_pad_rect(const vk_mm_rect *r, int32_t px, int32_t py, uint32_t pw, uint32_t ph) {
    vk_mm_rect fb = {0, 0, pw, ph};
    return vk_mm_clamp_rect(r->x - px, r->y - py, r->x + (int32_t)r->w + px, r->y + (int32_t)r->h + py, &fb);
}

static vk_mm_rect vk_mm_compute_save_box(uint32_t w, uint32_t h) {
    int32_t sw = (int32_t)(w < VK_MM_SAVE_W ? w : VK_MM_SAVE_W);
    int32_t sh = (int32_t)(h < VK_MM_SAVE_H ? h : VK_MM_SAVE_H);
    int32_t x1 = (int32_t)w, x0 = x1 - sw, y1 = (int32_t)h, y0 = y1 - sh;
    if (g_vk_mm.learn_mm_w == w && g_vk_mm.learn_mm_h == h && g_vk_mm.learn_mm_half > 0) {
        int32_t half = g_vk_mm.learn_mm_half + 48;
        if (half > (int32_t)(VK_MM_SAVE_W / 2u)) half = (int32_t)(VK_MM_SAVE_W / 2u);
        vk_mm_rect mm = vk_mm_pad_rect(&(vk_mm_rect){g_vk_mm.learn_mm_cx - half,
            g_vk_mm.learn_mm_cy - half, (uint32_t)(half * 2), (uint32_t)(half * 2)}, 0, 0, w, h);
        vk_mm_rect u = mm;
        if (g_vk_mm.learn_obj_w == w && g_vk_mm.learn_obj_h == h && g_vk_mm.learn_obj.w > 0u) {
            vk_mm_rect ob = vk_mm_pad_rect(&g_vk_mm.learn_obj, 24, 24, w, h);
            vk_mm_rect cand = vk_mm_union_rect(&mm, &ob);
            if (cand.w <= VK_MM_SAVE_W && cand.h <= VK_MM_SAVE_H) u = cand;
        }
        int32_t ux0 = u.x, uy0 = u.y, ux1 = u.x + (int32_t)u.w, uy1 = u.y + (int32_t)u.h;
        if (ux0 < x0 && ux1 <= x1) x0 = ux1 - sw > 0 ? ux1 - sw : 0;
        if (ux0 >= x0 && ux1 <= x1 && uy0 >= y0 && uy1 <= y1) return (vk_mm_rect){x0, y0, (uint32_t)sw, (uint32_t)sh};
        x0 = ux0; x1 = ux1; y0 = uy0; y1 = uy1;
        if (x1 - x0 < sw) { int32_t nx0 = x1 - sw; x0 = nx0 > 0 ? nx0 : 0; if (x0 + sw <= (int32_t)w) x1 = x0 + sw; }
        if (y1 - y0 < sh) { int32_t ny0 = y1 - sh; y0 = ny0 > 0 ? ny0 : 0; if (y0 + sh <= (int32_t)h) y1 = y0 + sh; }
    }
    return (vk_mm_rect){x0, y0, (uint32_t)(x1 - x0), (uint32_t)(y1 - y0)};
}

static void vk_minimap_pass_begin(
    VkCommandBuffer cb, vk_tex *t, unsigned lv, unsigned l0,
    uint32_t w, uint32_t h, int load_c0, int has_stencil, int native_ui) {
    g_vk_mm.in_hud_pass = 0; g_vk_mm.pass_opaque_fs = 0;
    g_vk_mm.pass_mm_hit = 0; g_vk_mm.pass_obj_hit = 0;
    g_vk_mm.pc_mm_done = 0; g_vk_mm.pc_obj_done = 0;
    g_vk_mm.ui_cand.img = VK_NULL_HANDLE; g_vk_mm.ui_cand.score = -1;
    if (!cb || !t || !V.dev || !vk_tex_image(t)) return;
    if (!atomic_load_explicit(&g_vk_mm.want_active, memory_order_acquire)) return;
    if (native_ui || !load_c0 || !has_stencil || vk_tex_samples(t) > 1u || vk_tex_is3d(t)) return;
    if (!(vk_tex_aspect(t) & VK_IMAGE_ASPECT_COLOR_BIT) || w < 640u || h < 360u) return;
    if (lv >= vk_tex_levels(t) || l0 >= vk_tex_layers(t)) return;
    VkFormat fmt = vk_tex_format(t);
    if (!vk_mm_supported_fmt(fmt)) return;
    VkImage im = vk_tex_image(t);
    uint32_t fr = g_vk_mm.frame_counter;
    if (g_vk_mm.frame_got_mm == fr && g_vk_mm.frame_got_obj == fr && g_vk_mm.saved_hit_img == im) return;
    if (!vk_mm_ensure_save(cb, fmt)) return;
    vk_mm_rect sb = vk_mm_compute_save_box(w, h);
    if (!sb.w || !sb.h) return;
    vk_barrier_all(cb);
    VkImageCopy cp = {
        {VK_IMAGE_ASPECT_COLOR_BIT, lv, l0, 1}, {sb.x, sb.y, 0},
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {sb.w, sb.h, 1}
    };
    vkCmdCopyImage(cb, im, VK_IMAGE_LAYOUT_GENERAL, g_vk_mm.save_img, VK_IMAGE_LAYOUT_GENERAL, 1, &cp);
    vk_barrier_all(cb);
    g_vk_mm.in_hud_pass = 1; g_vk_mm.pass_im = im;
    g_vk_mm.pass_w = w; g_vk_mm.pass_h = h;
    g_vk_mm.save_box = sb;
}

static int vk_minimap_pass_preclear(VkClearAttachment ca[2], VkClearRect cr[2], int max_n) {
    if (!g_vk_mm.in_hud_pass) return 0;
    int n = 0; uint32_t fr = g_vk_mm.frame_counter;
    VkClearValue blk = {.color = {.float32 = {0.f, 0.f, 0.f, 1.f}}};
    if (n < max_n && g_vk_mm.mm_ttl > 0u && g_vk_mm.frame_got_mm != fr
            && g_vk_mm.learn_mm_w == g_vk_mm.pass_w && g_vk_mm.learn_mm_h == g_vk_mm.pass_h && g_vk_mm.learn_mm_half > 0) {
        int32_t half = g_vk_mm.learn_mm_half, pad = half / 12;
        if (pad < 12) pad = 12; else if (pad > 32) pad = 32;
        vk_mm_rect r = vk_mm_clamp_rect(g_vk_mm.learn_mm_cx - half - pad, g_vk_mm.learn_mm_cy - half - pad,
            g_vk_mm.learn_mm_cx + half + pad, g_vk_mm.learn_mm_cy + half + pad, &g_vk_mm.save_box);
        if (r.w > 0u && r.h > 0u) {
            g_vk_mm.pc_mm = r; g_vk_mm.pc_mm_done = 1;
            ca[n] = (VkClearAttachment){VK_IMAGE_ASPECT_COLOR_BIT, 0u, blk};
            cr[n++] = (VkClearRect){{{r.x, r.y}, {r.w, r.h}}, 0u, 1u};
        }
    }
    if (n < max_n && g_vk_mm.obj_ttl > 0u && g_vk_mm.frame_got_obj != fr
            && g_vk_mm.learn_obj_w == g_vk_mm.pass_w && g_vk_mm.learn_obj_h == g_vk_mm.pass_h
            && g_vk_mm.learn_obj.w > 0u && g_vk_mm.learn_obj.h > 0u) {
        vk_mm_rect ob = g_vk_mm.learn_obj;
        vk_mm_rect r = vk_mm_clamp_rect(ob.x - 16, ob.y - 16,
            ob.x + (int32_t)ob.w + 16, ob.y + (int32_t)ob.h + 16, &g_vk_mm.save_box);
        if (r.w > 0u && r.h > 0u) {
            g_vk_mm.pc_obj = r; g_vk_mm.pc_obj_done = 1;
            ca[n] = (VkClearAttachment){VK_IMAGE_ASPECT_COLOR_BIT, 0u, blk};
            cr[n++] = (VkClearRect){{{r.x, r.y}, {r.w, r.h}}, 0u, 1u};
        }
    }
    return n;
}

static float vk_mm_f16(uint16_t h) {
    uint32_t sg = (uint32_t)(h >> 15) << 31, e = (h >> 10) & 31u, m = h & 1023u;
    if (e == 0u) return m ? ((sg ? -1.f : 1.f) * (float)m / 16777216.f) : (sg ? -0.f : 0.f);
    uint32_t u = (e == 31u) ? (sg | 0x7f800000u | (m << 13)) : (sg | ((e + 112u) << 23) | (m << 13));
    float f = 0.f; memcpy(&f, &u, 4); return f;
}

static int vk_mm_read_mem(uint64_t addr, void *out, size_t n) {
    VkBuffer bb = VK_NULL_HANDLE; uint64_t oo = 0, av = 0;
    if (!addr || !vk_resolve_span(addr, &bb, &oo, &av) || av < n) return 0;
    memcpy(out, (const void *)(uintptr_t)addr, n);
    return 1;
}

static int vk_mm_attr_xy_bounds(
    VkFormat vfmt, uint32_t off, uint32_t stride, uint64_t vbuf, uint64_t vbuf_sz,
    const p_draw *dw, const p_draw_idx *di, int32_t cnt, float *mn, float *mx) {
    int half = (vfmt == VK_FORMAT_R16G16_SFLOAT || vfmt == VK_FORMAT_R16G16B16A16_SFLOAT);
    int flt = (vfmt == VK_FORMAT_R32G32_SFLOAT || vfmt == VK_FORMAT_R32G32B32_SFLOAT || vfmt == VK_FORMAT_R32G32B32A32_SFLOAT);
    if ((!half && !flt) || !vbuf || cnt <= 0) return 0;
    int32_t lim = cnt < 256 ? cnt : 256;
    size_t elem = half ? 4u : 8u;
    for (int32_t i = 0; i < lim; i++) {
        int32_t v = cnt <= 256 ? i : (int32_t)(((int64_t)i * cnt) / lim);
        int64_t vi = dw ? (int64_t)dw->first + v : -1;
        if (di) {
            uint32_t ix = 0; size_t isz = di->itype == 0 ? 1u : di->itype == 1 ? 2u : 4u;
            if (!di->ibuf || !vk_mm_read_mem(di->ibuf + (uint64_t)v * isz, &ix, isz)) return 0;
            vi = (int64_t)di->base_vertex + (int64_t)ix;
        }
        if (vi < 0) return 0;
        uint64_t vo = (uint64_t)vi * stride + off;
        if (vbuf_sz && (vo > vbuf_sz || vbuf_sz - vo < elem)) return 0;
        uint8_t b[8] = {0}; float xy[2] = {0.f, 0.f};
        if (!vk_mm_read_mem(vbuf + vo, b, elem)) return 0;
        if (half) { uint16_t h0, h1; memcpy(&h0, b, 2); memcpy(&h1, b + 2, 2); xy[0] = vk_mm_f16(h0); xy[1] = vk_mm_f16(h1); }
        else memcpy(xy, b, 8);
        if (i == 0) { mn[0] = mx[0] = xy[0]; mn[1] = mx[1] = xy[1]; }
        else { if (xy[0] < mn[0]) mn[0] = xy[0]; if (xy[0] > mx[0]) mx[0] = xy[0]; if (xy[1] < mn[1]) mn[1] = xy[1]; if (xy[1] > mx[1]) mx[1] = xy[1]; }
    }
    return 1;
}

static void vk_mm_try_minimap(const float *mn, const float *mx) {
    float w = (float)g_vk_mm.pass_w, h = (float)g_vk_mm.pass_h;
    float bw = mx[0] - mn[0], bh = mx[1] - mn[1];
    if (bw < h * 0.14f || bw > h * 0.46f || fabsf(bw - bh) > fmaxf(12.f, bw * 0.10f)) return;
    float cx = (mn[0] + mx[0]) * 0.5f, cy = (mn[1] + mx[1]) * 0.5f;
    if (cx < w * 0.50f || cx > w * 0.99f || cy < h * 0.35f) return;
    uint32_t side = (uint32_t)lrintf(fmaxf(bw, bh));
    if (side < VK_MM_MIN_SIDE) side = VK_MM_MIN_SIDE;
    if (side > VK_MM_MAX_SIDE) side = VK_MM_MAX_SIDE;
    if (side > g_vk_mm.pass_w) side = g_vk_mm.pass_w;
    if (side > g_vk_mm.pass_h) side = g_vk_mm.pass_h;
    int32_t x0 = (int32_t)lrintf(cx - side * 0.5f), y0 = (int32_t)lrintf(cy - side * 0.5f);
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x0 + (int32_t)side > (int32_t)g_vk_mm.pass_w) x0 = (int32_t)g_vk_mm.pass_w - (int32_t)side;
    if (y0 + (int32_t)side > (int32_t)g_vk_mm.pass_h) y0 = (int32_t)g_vk_mm.pass_h - (int32_t)side;
    g_vk_mm.hit_mm = (vk_mm_rect){x0, y0, side, side};
    g_vk_mm.pass_mm_hit = 1;
    g_vk_mm.learn_mm_w = g_vk_mm.pass_w; g_vk_mm.learn_mm_h = g_vk_mm.pass_h;
    g_vk_mm.learn_mm_cx = (int32_t)lrintf(cx); g_vk_mm.learn_mm_cy = (int32_t)lrintf(cy);
    g_vk_mm.learn_mm_half = (int32_t)(side / 2u);
}

static void vk_mm_try_objective(const float *mn, const float *mx) {
    float w = (float)g_vk_mm.pass_w, h = (float)g_vk_mm.pass_h;
    float bw = mx[0] - mn[0], bh = mx[1] - mn[1];
    float cy = (mn[1] + mx[1]) * 0.5f;
    if (mn[0] < w * 0.62f || mx[0] > w * 0.995f || cy < h * 0.28f || cy > h * 0.62f) return;
    if (bw < h * 0.08f || bw > w * 0.36f || bh < h * 0.015f || bh > h * 0.14f || bw < bh * 1.4f) return;
    if (!g_vk_mm.pass_obj_hit) {
        g_vk_mm.obj_umin_x = mn[0]; g_vk_mm.obj_umin_y = mn[1];
        g_vk_mm.obj_umax_x = mx[0]; g_vk_mm.obj_umax_y = mx[1];
    } else {
        if (mn[0] < g_vk_mm.obj_umin_x) g_vk_mm.obj_umin_x = mn[0];
        if (mn[1] < g_vk_mm.obj_umin_y) g_vk_mm.obj_umin_y = mn[1];
        if (mx[0] > g_vk_mm.obj_umax_x) g_vk_mm.obj_umax_x = mx[0];
        if (mx[1] > g_vk_mm.obj_umax_y) g_vk_mm.obj_umax_y = mx[1];
    }
    int32_t x0 = (int32_t)floorf(g_vk_mm.obj_umin_x - h * 0.085f), x1 = (int32_t)g_vk_mm.pass_w;
    int32_t y0 = (int32_t)floorf(g_vk_mm.obj_umin_y - h * 0.026f);
    int32_t y1 = (int32_t)ceilf(g_vk_mm.obj_umax_y + h * 0.026f);
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (y1 > (int32_t)g_vk_mm.pass_h) y1 = (int32_t)g_vk_mm.pass_h;
    if (x1 - x0 > (int32_t)VK_OBJ_MAX_W) x0 = x1 - (int32_t)VK_OBJ_MAX_W;
    if (y1 - y0 > (int32_t)VK_OBJ_MAX_H) y1 = y0 + (int32_t)VK_OBJ_MAX_H;
    int32_t ow = (x1 - x0) & ~3;
    if (ow < 64 || y1 <= y0) return;
    x0 = x1 - ow;
    vk_mm_rect ob = {x0, y0, (uint32_t)ow, (uint32_t)(y1 - y0)};
    if (ob.h > VK_OBJ_MAX_H) ob.h = VK_OBJ_MAX_H;
    g_vk_mm.hit_obj = ob; g_vk_mm.pass_obj_hit = 1;
    g_vk_mm.learn_obj = ob; g_vk_mm.learn_obj_w = g_vk_mm.pass_w; g_vk_mm.learn_obj_h = g_vk_mm.pass_h;
}

static void vk_minimap_note_draw(
    int indirect, uint32_t st, uint32_t dt, uint32_t c0_mask, uint32_t blend_en,
    VkFormat vfmt, uint32_t off, uint32_t stride, uint64_t vbuf, uint64_t vbuf_sz,
    const p_draw *dw, const p_draw_idx *di, float ax, float ay, float acx, float acy) {
    if (!g_vk_mm.in_hud_pass || indirect || dt != 0u) return;
    int32_t cnt = dw ? dw->count : (di ? di->count : 0);
    float mn[2] = {0}, mx[2] = {0};
    if (!vk_mm_attr_xy_bounds(vfmt, off, stride, vbuf, vbuf_sz, dw, di, cnt, mn, mx)) return;
    mn[0] = acx + (mn[0] - acx) * ax; mx[0] = acx + (mx[0] - acx) * ax;
    mn[1] = acy + (mn[1] - acy) * ay; mx[1] = acy + (mx[1] - acy) * ay;
    if (st != 0u && (c0_mask & 0xfu) == 0u && cnt >= 4 && cnt <= 12) { vk_mm_try_minimap(mn, mx); return; }
    if (st == 0u && (c0_mask & 0xfu) != 0u && !blend_en && cnt >= 3 && cnt <= 6) {
        float bw = mx[0] - mn[0], bh = mx[1] - mn[1];
        if ((bw >= g_vk_mm.pass_w * 0.90f && bh >= g_vk_mm.pass_h * 0.90f)
                || (mn[0] <= -0.95f && mx[0] >= 0.95f && mn[1] <= -0.95f && mx[1] >= 0.95f)) {
            g_vk_mm.pass_opaque_fs = 1;
        }
        return;
    }
    if (st == 0u && (c0_mask & 0xfu) != 0u && blend_en && cnt >= 16 && cnt <= 600) vk_mm_try_objective(mn, mx);
}

static int vk_minimap_want_ui_tex(void) {
    return g_vk_mm.in_hud_pass && !g_vk_mm.pass_opaque_fs
        && atomic_load_explicit(&g_vk_mm.ui_want, memory_order_relaxed) == 1;
}

static int vk_mm_ui_seen(uint64_t serial) {
    for (int i = 0; i < g_vk_mm.ui_seen_n; i++) if (g_vk_mm.ui_seen[i] == serial) return 1;
    return 0;
}

static void vk_minimap_note_fs_tex(vk_tex *x, uint32_t dt) {
    if (!vk_minimap_want_ui_tex() || dt != 0u || !x || !vk_tex_image(x) || vk_tex_is3d(x)) return;
    VkImage im = vk_tex_image(x);
    uint32_t tw = vk_tex_width(x, 0u), th = vk_tex_height(x, 0u);
    VkFormat fmt = vk_tex_format(x);
    uint64_t ser = vk_tex_serial(x);
    if (im == g_vk_mm.pass_im || vk_tex_layers(x) != 1u || vk_tex_samples(x) != 1u) return;
    if (vk_tex_aspect(x) != VK_IMAGE_ASPECT_COLOR_BIT || tw < 128u || th < 128u || tw > 2048u || th > 2048u) return;
    if ((tw == g_vk_mm.pass_w && th == g_vk_mm.pass_h) || (tw == 1280u && th == 720u) || (tw == 1920u && th == 1080u)) return;
    if (!vk_mm_is_color4_fmt(fmt) || vk_mm_ui_seen(ser)) return;
    VkFormatProperties fp;
    V.get_format_props(V.pd, fmt, &fp);
    if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT)) return;
    int score = (tw == th ? 20 : 5) + (tw >= 512u && th >= 512u ? 20 : (tw >= 256u ? 10 : 0));
    if (score > g_vk_mm.ui_cand.score) g_vk_mm.ui_cand = (vk_ui_cand){im, tw, th, fmt, ser, score};
}

static void vk_mm_stream_stage(
    vk_mm_stream *st, VkCommandBuffer cb, VkImage im, unsigned lv, unsigned l0, VkFormat fmt, const vk_mm_rect *r) {
    if (st->wrote_in_frame || atomic_load_explicit(&st->pending_valid, memory_order_acquire)) return;
    VkDeviceSize need = (VkDeviceSize)r->w * r->h * 4u;
    if ((g_vk_mm.frame_counter & 1u) != 0u || !r->w || !r->h || need > st->slot_bytes) return;
    if (!vk_mm_ensure_stream_ring(st)) return;
    vk_mm_slot *dst = &st->slot[st->write_slot];
    VkBufferImageCopy rg = {
        .bufferOffset = 0, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, lv, l0, 1},
        .imageOffset = {r->x, r->y, 0}, .imageExtent = {r->w, r->h, 1}
    };
    vkCmdCopyImageToBuffer(cb, im, VK_IMAGE_LAYOUT_GENERAL, dst->buf, 1, &rg);
    VkBufferMemoryBarrier2 bb = {
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2, .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT, .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
        .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .buffer = dst->buf, .offset = 0, .size = need
    };
    VkDependencyInfo di = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &bb};
    vkCmdPipelineBarrier2(cb, &di);
    st->wrote_in_frame = 1; st->write_w = r->w; st->write_h = r->h; st->write_fmt = fmt;
}

static void vk_mm_restore_box(
    VkCommandBuffer cb, VkImage im, unsigned lv, unsigned l0, const vk_mm_rect *raw) {
    if (!g_vk_mm.save_ready) return;
    vk_mm_rect r = vk_mm_clamp_rect(raw->x, raw->y, raw->x + (int32_t)raw->w, raw->y + (int32_t)raw->h, &g_vk_mm.save_box);
    if (!r.w || !r.h) return;
    vk_barrier_all(cb);
    VkImageCopy cp = {
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {r.x - g_vk_mm.save_box.x, r.y - g_vk_mm.save_box.y, 0},
        {VK_IMAGE_ASPECT_COLOR_BIT, lv, l0, 1}, {r.x, r.y, 0}, {r.w, r.h, 1}
    };
    vkCmdCopyImage(cb, g_vk_mm.save_img, VK_IMAGE_LAYOUT_GENERAL, im, VK_IMAGE_LAYOUT_GENERAL, 1, &cp);
    vk_barrier_all(cb);
}

static int vk_mm_ensure_ui(VkCommandBuffer cb) {
    if (g_vk_mm.ui_ready) return 1;
    if (g_vk_mm.ui_failed) return 0;
    VkFormatProperties fp;
    V.get_format_props(V.pd, VK_FORMAT_R8G8B8A8_UNORM, &fp);
    if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT)
            || !vk_mm_alloc_slot(&g_vk_mm.ui_slot, VK_UI_BUF_BYTES)
            || !vk_mm_create_img(cb, VK_UI_TEX_SIDE, VK_UI_TEX_SIDE, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    &g_vk_mm.ui_img, &g_vk_mm.ui_mem)) {
        vk_mm_free_slot(&g_vk_mm.ui_slot); g_vk_mm.ui_failed = 1; return 0;
    }
    g_vk_mm.ui_ready = 1; return 1;
}

static void vk_mm_stage_ui_tex(VkCommandBuffer cb) {
    vk_ui_cand *c = &g_vk_mm.ui_cand;
    if (!c->img || !vk_mm_ensure_ui(cb)) return;
    vk_barrier_all(cb);
    VkImageBlit bl = {
        .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .srcOffsets = {{0, 0, 0}, {(int32_t)c->w, (int32_t)c->h, 1}},
        .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .dstOffsets = {{0, 0, 0}, {(int32_t)VK_UI_TEX_SIDE, (int32_t)VK_UI_TEX_SIDE, 1}},
    };
    vkCmdBlitImage(cb, c->img, VK_IMAGE_LAYOUT_GENERAL, g_vk_mm.ui_img, VK_IMAGE_LAYOUT_GENERAL, 1, &bl, VK_FILTER_LINEAR);
    vk_barrier_all(cb);
    VkBufferImageCopy rg = {
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageExtent = {VK_UI_TEX_SIDE, VK_UI_TEX_SIDE, 1}
    };
    vkCmdCopyImageToBuffer(cb, g_vk_mm.ui_img, VK_IMAGE_LAYOUT_GENERAL, g_vk_mm.ui_slot.buf, 1, &rg);
    VkBufferMemoryBarrier2 bb = {
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2, .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT, .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
        .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .buffer = g_vk_mm.ui_slot.buf, .offset = 0, .size = VK_UI_BUF_BYTES
    };
    VkDependencyInfo di = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &bb};
    vkCmdPipelineBarrier2(cb, &di);
    g_vk_mm.ui_wrote_frame = 1;
    g_vk_mm.ui_seen[g_vk_mm.ui_seen_pos] = c->serial;
    g_vk_mm.ui_seen_pos = (g_vk_mm.ui_seen_pos + 1) & 7;
    if (g_vk_mm.ui_seen_n < 8) g_vk_mm.ui_seen_n++;
    atomic_store_explicit(&g_vk_mm.ui_want, 2, memory_order_release);
}

static void vk_minimap_pass_end(
    VkCommandBuffer cb, vk_tex *t, unsigned lv, unsigned l0, VkFormat fmt, uint64_t serial) {
    (void)fmt;
    int was = g_vk_mm.in_hud_pass, mh = g_vk_mm.pass_mm_hit, oh = g_vk_mm.pass_obj_hit, op = g_vk_mm.pass_opaque_fs;
    g_vk_mm.in_hud_pass = 0; g_vk_mm.pass_mm_hit = 0; g_vk_mm.pass_obj_hit = 0; g_vk_mm.pass_opaque_fs = 0;
    if (!was || !t || !V.dev || vk_tex_image(t) != g_vk_mm.pass_im) return;
    if (!atomic_load_explicit(&g_vk_mm.want_active, memory_order_acquire)) return;
    if (op && !mh && !oh) return;
    VkCommandBuffer copy_cb = cb ? cb : vk_cb();
    if (!copy_cb) return;
    VkImage im = vk_tex_image(t); VkFormat tf = vk_tex_format(t); uint32_t fr = g_vk_mm.frame_counter;
    if (mh) {
        vk_mm_find_cand(g_vk_mm.hit_mm.w, g_vk_mm.hit_mm.h, tf, serial);
        vk_mm_stream_stage(&g_vk_mm.mm, copy_cb, im, lv, l0, tf, &g_vk_mm.hit_mm);
        int32_t pad = (int32_t)(g_vk_mm.hit_mm.w / 24u);
        if (pad < 12) pad = 12; else if (pad > 32) pad = 32;
        vk_mm_rect pm = vk_mm_pad_rect(&g_vk_mm.hit_mm, pad, pad, g_vk_mm.pass_w, g_vk_mm.pass_h);
        vk_mm_rect u = g_vk_mm.pc_mm_done ? vk_mm_union_rect(&g_vk_mm.pc_mm, &pm) : pm;
        vk_mm_restore_box(copy_cb, im, lv, l0, &u);
        g_vk_mm.saw_mm_frame = 1; g_vk_mm.frame_got_mm = fr; g_vk_mm.saved_hit_img = im;
    } else if (g_vk_mm.pc_mm_done) {
        vk_mm_restore_box(copy_cb, im, lv, l0, &g_vk_mm.pc_mm);
    }
    if (oh) {
        vk_mm_stream_stage(&g_vk_mm.obj, copy_cb, im, lv, l0, tf, &g_vk_mm.hit_obj);
        vk_mm_rect po = vk_mm_pad_rect(&g_vk_mm.hit_obj, 16, 16, g_vk_mm.pass_w, g_vk_mm.pass_h);
        vk_mm_rect u = g_vk_mm.pc_obj_done ? vk_mm_union_rect(&g_vk_mm.pc_obj, &po) : po;
        vk_mm_restore_box(copy_cb, im, lv, l0, &u);
        g_vk_mm.saw_obj_frame = 1; g_vk_mm.frame_got_obj = fr; g_vk_mm.saved_hit_img = im;
    } else if (g_vk_mm.pc_obj_done) {
        vk_mm_restore_box(copy_cb, im, lv, l0, &g_vk_mm.pc_obj);
    }
    if ((mh || oh) && atomic_load_explicit(&g_vk_mm.ui_want, memory_order_relaxed) == 1) vk_mm_stage_ui_tex(copy_cb);
}

static void vk_minimap_pass_abort(void) {
    g_vk_mm.in_hud_pass = 0; g_vk_mm.pass_mm_hit = 0; g_vk_mm.pass_obj_hit = 0; g_vk_mm.pass_opaque_fs = 0;
}

static void vk_mm_stream_present(vk_mm_stream *st, uint32_t fr, uint64_t fence) {
    if (st->wrote_in_frame) {
        st->pending_slot = st->write_slot; st->pending_fence = fence; st->pending_frame = fr;
        st->pending_w = st->write_w; st->pending_h = st->write_h; st->pending_fmt = st->write_fmt;
        atomic_store_explicit(&st->pending_valid, 1, memory_order_release);
        st->wrote_in_frame = 0;
    }
    if (!atomic_load_explicit(&st->pending_valid, memory_order_relaxed)) return;
    if ((fr - st->pending_frame) < 1u || (st->pending_fence != 0 && !mtl_wait(st->pending_fence, 0))) return;
    pthread_mutex_lock(&g_vk_mm.mu);
    int prev = st->ready_slot;
    st->ready_slot = st->pending_slot; st->ready_w = st->pending_w; st->ready_h = st->pending_h; st->ready_fmt = st->pending_fmt;
    st->seq++;
    st->write_slot = prev >= 0 ? prev : (st->pending_slot ^ 1);
    atomic_store_explicit(&st->pending_valid, 0, memory_order_release);
    pthread_mutex_unlock(&g_vk_mm.mu);
}

static void vk_minimap_present(void) {
    g_vk_mm.frame_counter++;
    uint32_t fr = g_vk_mm.frame_counter;
    uint64_t fq = mtl_last_fence() + 1u;
    vk_mm_stream_present(&g_vk_mm.mm, fr, fq);
    vk_mm_stream_present(&g_vk_mm.obj, fr, fq);
    if (g_vk_mm.ui_wrote_frame) {
        g_vk_mm.ui_pending = 1; g_vk_mm.ui_pending_fence = fq; g_vk_mm.ui_pending_frame = fr;
        g_vk_mm.ui_wrote_frame = 0;
    }
    if (g_vk_mm.ui_pending && (fr - g_vk_mm.ui_pending_frame) >= 1u && (!g_vk_mm.ui_pending_fence || mtl_wait(g_vk_mm.ui_pending_fence, 0))) {
        pthread_mutex_lock(&g_vk_mm.mu);
        g_vk_mm.ui_pending = 0; g_vk_mm.ui_has_ready = 1; g_vk_mm.ui_seq++;
        atomic_store_explicit(&g_vk_mm.ui_want, 0, memory_order_release);
        pthread_mutex_unlock(&g_vk_mm.mu);
    }
    if (g_vk_mm.saw_mm_frame) g_vk_mm.mm_ttl = VK_MM_TTL; else if (g_vk_mm.mm_ttl > 0u) g_vk_mm.mm_ttl--;
    if (g_vk_mm.saw_obj_frame) g_vk_mm.obj_ttl = VK_MM_TTL; else if (g_vk_mm.obj_ttl > 0u) g_vk_mm.obj_ttl--;
    g_vk_mm.saw_mm_frame = 0; g_vk_mm.saw_obj_frame = 0;
}

static void vk_mm_copy_rgba(uint8_t *dst, const uint8_t *src, uint32_t w, uint32_t h, VkFormat fmt, int keep_a) {
    int bgra = (fmt == VK_FORMAT_B8G8R8A8_UNORM || fmt == VK_FORMAT_B8G8R8A8_SRGB);
    size_t n = (size_t)w * h;
    for (size_t i = 0; i < n; i++) {
        size_t o = i * 4u;
        dst[o + 0] = src[o + (bgra ? 2 : 0)];
        dst[o + 1] = src[o + 1];
        dst[o + 2] = src[o + (bgra ? 0 : 2)];
        dst[o + 3] = keep_a ? src[o + 3] : 255u;
    }
}

int vk_minimap_read(uint8_t *rgba_out, int max_bytes, int32_t meta[5]) {
    if (!meta) return 0;
    atomic_store_explicit(&g_vk_mm.want_active, 1, memory_order_release);
    uint32_t client_seq = (uint32_t)meta[2];
    int updated = 0;
    pthread_mutex_lock(&g_vk_mm.mu);
    int cnt = atomic_load_explicit(&g_vk_mm.cand_count, memory_order_acquire);
    int cidx = cnt > 0 ? (int)(atomic_load_explicit(&g_vk_mm.cand_idx, memory_order_acquire) % (uint32_t)cnt) : 0;
    int rslot = g_vk_mm.mm.ready_slot;
    uint32_t w = g_vk_mm.mm.ready_w, h = g_vk_mm.mm.ready_h, seq = g_vk_mm.mm.seq;
    if (rslot >= 0 && rslot < VK_MM_SLOTS && g_vk_mm.mm.slot[rslot].ptr && seq != client_seq
            && w > 0 && w <= VK_MM_MAX_SIDE && h > 0 && h <= VK_MM_MAX_SIDE) {
        size_t need = (size_t)w * h * 4u;
        if (rgba_out && need > 0 && max_bytes > 0 && (size_t)max_bytes >= need) {
            vk_mm_copy_rgba(rgba_out, g_vk_mm.mm.slot[rslot].ptr, w, h, g_vk_mm.mm.ready_fmt, 0);
            updated = 1;
        }
    }
    meta[0] = (int32_t)w; meta[1] = (int32_t)h; meta[2] = (int32_t)seq; meta[3] = cnt; meta[4] = cidx;
    pthread_mutex_unlock(&g_vk_mm.mu);
    return updated;
}

int vk_minimap_objective_read(uint8_t *rgba_out, int max_bytes, int32_t meta[4]) {
    if (!meta) return 0;
    atomic_store_explicit(&g_vk_mm.want_active, 1, memory_order_release);
    uint32_t client_seq = (uint32_t)meta[2];
    int updated = 0;
    pthread_mutex_lock(&g_vk_mm.mu);
    int active = (g_vk_mm.obj_ttl > 0u || g_vk_mm.saw_obj_frame
        || atomic_load_explicit(&g_vk_mm.obj.pending_valid, memory_order_relaxed)) ? 1 : 0;
    int rslot = g_vk_mm.obj.ready_slot;
    uint32_t w = g_vk_mm.obj.ready_w, h = g_vk_mm.obj.ready_h, seq = g_vk_mm.obj.seq;
    if (active && rslot >= 0 && rslot < VK_MM_SLOTS && g_vk_mm.obj.slot[rslot].ptr && seq != client_seq
            && w > 0 && w <= VK_OBJ_MAX_W && h > 0 && h <= VK_OBJ_MAX_H) {
        size_t need = (size_t)w * h * 4u;
        if (rgba_out && need > 0 && max_bytes > 0 && (size_t)max_bytes >= need) {
            vk_mm_copy_rgba(rgba_out, g_vk_mm.obj.slot[rslot].ptr, w, h, g_vk_mm.obj.ready_fmt, 0);
            updated = 1;
        }
    }
    meta[0] = (int32_t)w; meta[1] = (int32_t)h; meta[2] = (int32_t)seq; meta[3] = active;
    pthread_mutex_unlock(&g_vk_mm.mu);
    return updated;
}

int vk_minimap_ui_tex_read(uint8_t *rgba_out, int max_bytes, int32_t meta[4], int request) {
    if (!meta) return 0;
    uint32_t client_seq = (uint32_t)meta[2];
    int updated = 0;
    pthread_mutex_lock(&g_vk_mm.mu);
    int st = atomic_load_explicit(&g_vk_mm.ui_want, memory_order_relaxed);
    if (request && st == 0 && (!g_vk_mm.ui_has_ready || client_seq == g_vk_mm.ui_seq)) {
        atomic_store_explicit(&g_vk_mm.ui_want, 1, memory_order_release);
        st = 1;
    }
    if (g_vk_mm.ui_has_ready && g_vk_mm.ui_slot.ptr && g_vk_mm.ui_seq != client_seq) {
        if (rgba_out && max_bytes > 0 && (size_t)max_bytes >= (size_t)VK_UI_BUF_BYTES) {
            vk_mm_copy_rgba(rgba_out, g_vk_mm.ui_slot.ptr, VK_UI_TEX_SIDE, VK_UI_TEX_SIDE, VK_FORMAT_R8G8B8A8_UNORM, 1);
            updated = 1;
        }
    }
    meta[0] = (int32_t)VK_UI_TEX_SIDE; meta[1] = (int32_t)VK_UI_TEX_SIDE;
    meta[2] = (int32_t)g_vk_mm.ui_seq; meta[3] = st;
    pthread_mutex_unlock(&g_vk_mm.mu);
    return updated;
}

int vk_minimap_cycle(void) {
    uint32_t next = atomic_fetch_add_explicit(&g_vk_mm.cand_idx, 1u, memory_order_acq_rel) + 1u;
    int cnt = atomic_load_explicit(&g_vk_mm.cand_count, memory_order_acquire);
    return cnt > 0 ? (int)(next % (uint32_t)cnt) : 0;
}

void vk_minimap_enable(int on) {
    int v = on ? 1 : 0;
    atomic_store_explicit(&g_vk_mm.want_active, v, memory_order_release);
    if (!v) { g_vk_mm.mm_ttl = 0u; g_vk_mm.obj_ttl = 0u; }
}
