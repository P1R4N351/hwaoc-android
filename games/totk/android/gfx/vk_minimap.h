// SPDX-License-Identifier: MIT
#pragma once
// Native Vulkan HUD minimap interception and redirection (2-slot staging ring).
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
    VK_MM_SLOTS = 2,
    VK_MM_MAX_CAND = 8
};
#define VK_MM_BUF_BYTES ((VkDeviceSize)(VK_MM_MAX_SIDE * VK_MM_MAX_SIDE * 4u))

typedef struct { VkBuffer buf; VkDeviceMemory mem; uint8_t *ptr; } vk_mm_slot;
typedef struct { uint32_t w, h; VkFormat fmt; uint64_t serial; } vk_mm_cand;

static struct {
    pthread_mutex_t mu;
    vk_mm_slot slot[VK_MM_SLOTS];
    int ring_ready, ring_failed;
    VkImage save_img;
    VkDeviceMemory save_mem;
    VkFormat save_fmt;
    int save_ready, save_failed;
    vk_mm_cand cand[VK_MM_MAX_CAND];
    _Atomic int cand_count, want_active, pending_valid;
    _Atomic uint32_t cand_idx;
    uint32_t frame_counter, pending_frame, saved_hit_frame;
    int write_slot, pending_slot, ready_slot, wrote_in_frame;
    uint32_t write_w, write_h, pending_w, pending_h, ready_w, ready_h, seq;
    VkFormat write_fmt, pending_fmt, ready_fmt;
    uint64_t pending_fence;
    int in_hud_pass, pass_hit;
    VkImage pass_im, saved_hit_img;
    uint32_t pass_w, pass_h;
    int32_t save_x, save_y, hit_x, hit_y;
    uint32_t save_w, save_h, hit_side;
    uint32_t learn_w, learn_h;
    int32_t learn_cx, learn_cy, learn_half;
} g_vk_mm = {
    .mu = PTHREAD_MUTEX_INITIALIZER,
    .ready_slot = -1,
    .pending_slot = -1,
};

static int vk_mm_supported_fmt(VkFormat fmt) {
    return fmt == VK_FORMAT_R8G8B8A8_UNORM || fmt == VK_FORMAT_R8G8B8A8_SRGB
        || fmt == VK_FORMAT_B8G8R8A8_UNORM || fmt == VK_FORMAT_B8G8R8A8_SRGB;
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

static int vk_mm_alloc_slot(vk_mm_slot *s) {
    VkBufferCreateInfo bi = {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = VK_MM_BUF_BYTES,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT
    };
    if (vkCreateBuffer(V.dev, &bi, NULL, &s->buf)) return 0;
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(V.dev, s->buf, &mr);
    int t = vk_pick_type(mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    if (t < 0) {
        t = vk_pick_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }
    if (t < 0) { vk_mm_free_slot(s); return 0; }
    VkMemoryAllocateInfo ai = {
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mr.size,
        .memoryTypeIndex = (uint32_t)t
    };
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

static int vk_mm_ensure_ring(void) {
    if (g_vk_mm.ring_ready) return 1;
    if (g_vk_mm.ring_failed || !V.dev) return 0;
    for (int i = 0; i < VK_MM_SLOTS; i++) {
        if (!vk_mm_alloc_slot(&g_vk_mm.slot[i])) {
            for (int j = 0; j < i; j++) vk_mm_free_slot(&g_vk_mm.slot[j]);
            g_vk_mm.ring_failed = 1;
            return 0;
        }
    }
    g_vk_mm.ring_ready = 1;
    return 1;
}

static int vk_mm_ensure_save(VkCommandBuffer cb, VkFormat fmt) {
    if (g_vk_mm.save_ready) return 1;
    if (g_vk_mm.save_failed || !V.dev || !cb) return 0;
    VkImageCreateInfo ci = {
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = fmt,
        .extent = {VK_MM_MAX_SIDE, VK_MM_MAX_SIDE, 1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    if (vkCreateImage(V.dev, &ci, NULL, &g_vk_mm.save_img)) { g_vk_mm.save_failed = 1; return 0; }
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(V.dev, g_vk_mm.save_img, &mr);
    int t = vk_pick_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (t < 0) t = vk_pick_type(mr.memoryTypeBits, 0);
    VkMemoryAllocateInfo ai = {
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size, .memoryTypeIndex = (uint32_t)(t < 0 ? 0 : t)
    };
    if (t < 0 || vk_memory_allocate(&ai, &g_vk_mm.save_mem, VK_MEMORY_DIAGNOSTIC)
            || vkBindImageMemory(V.dev, g_vk_mm.save_img, g_vk_mm.save_mem, 0)) {
        if (g_vk_mm.save_img) vkDestroyImage(V.dev, g_vk_mm.save_img, NULL);
        if (g_vk_mm.save_mem) vk_memory_release(g_vk_mm.save_mem, VK_MEMORY_DIAGNOSTIC);
        g_vk_mm.save_img = VK_NULL_HANDLE; g_vk_mm.save_mem = VK_NULL_HANDLE;
        g_vk_mm.save_failed = 1;
        return 0;
    }
    VkImageMemoryBarrier2 ib = {
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = g_vk_mm.save_img, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
    };
    VkDependencyInfo di = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &ib};
    vkCmdPipelineBarrier2(cb, &di);
    g_vk_mm.save_fmt = fmt;
    g_vk_mm.save_ready = 1;
    return 1;
}

static void vk_mm_save_box(uint32_t w, uint32_t h, int32_t *rx, int32_t *ry, uint32_t *rw, uint32_t *rh) {
    int32_t x0, y0, x1, y1;
    if (g_vk_mm.learn_w == w && g_vk_mm.learn_h == h && g_vk_mm.learn_half > 0) {
        int32_t half = g_vk_mm.learn_half + 48;
        if (half > (int32_t)(VK_MM_MAX_SIDE / 2u)) half = (int32_t)(VK_MM_MAX_SIDE / 2u);
        x0 = g_vk_mm.learn_cx - half; x1 = g_vk_mm.learn_cx + half;
        y0 = g_vk_mm.learn_cy - half; y1 = g_vk_mm.learn_cy + half;
    } else {
        int32_t sw = (int32_t)(w / 2u < VK_MM_MAX_SIDE ? w / 2u : VK_MM_MAX_SIDE);
        int32_t sh = (int32_t)((h * 3u) / 5u < VK_MM_MAX_SIDE ? (h * 3u) / 5u : VK_MM_MAX_SIDE);
        x0 = (int32_t)w - sw; x1 = (int32_t)w;
        y0 = (int32_t)h - sh; y1 = (int32_t)h;
    }
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 > (int32_t)w) x1 = (int32_t)w; if (y1 > (int32_t)h) y1 = (int32_t)h;
    if (x1 - x0 > (int32_t)VK_MM_MAX_SIDE) x0 = x1 - (int32_t)VK_MM_MAX_SIDE;
    if (y1 - y0 > (int32_t)VK_MM_MAX_SIDE) y0 = y1 - (int32_t)VK_MM_MAX_SIDE;
    *rx = x0; *ry = y0;
    *rw = (uint32_t)(x1 > x0 ? x1 - x0 : 0);
    *rh = (uint32_t)(y1 > y0 ? y1 - y0 : 0);
}

static void vk_minimap_pass_begin(
    VkCommandBuffer cb, vk_tex *t, unsigned lv, unsigned l0,
    uint32_t w, uint32_t h, int load_c0, int has_stencil, int native_ui) {
    g_vk_mm.in_hud_pass = 0; g_vk_mm.pass_hit = 0;
    if (!cb || !t || !V.dev || !vk_tex_image(t)) return;
    if (!atomic_load_explicit(&g_vk_mm.want_active, memory_order_acquire)) return;
    if (native_ui || !load_c0 || !has_stencil || vk_tex_samples(t) > 1u || vk_tex_is3d(t)) return;
    if (!(vk_tex_aspect(t) & VK_IMAGE_ASPECT_COLOR_BIT) || w < 640u || h < 360u) return;
    if (lv >= vk_tex_levels(t) || l0 >= vk_tex_layers(t)) return;
    VkFormat fmt = vk_tex_format(t);
    if (!vk_mm_supported_fmt(fmt)) return;
    VkImage im = vk_tex_image(t);
    if (g_vk_mm.saved_hit_frame == g_vk_mm.frame_counter && g_vk_mm.saved_hit_img == im) return;
    if (!vk_mm_ensure_save(cb, fmt)) return;
    int32_t sx = 0, sy = 0; uint32_t sw = 0, sh = 0;
    vk_mm_save_box(w, h, &sx, &sy, &sw, &sh);
    if (!sw || !sh) return;
    vk_barrier_all(cb);
    VkImageCopy cp = {
        {VK_IMAGE_ASPECT_COLOR_BIT, lv, l0, 1}, {sx, sy, 0},
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {sw, sh, 1}
    };
    vkCmdCopyImage(cb, im, VK_IMAGE_LAYOUT_GENERAL, g_vk_mm.save_img, VK_IMAGE_LAYOUT_GENERAL, 1, &cp);
    vk_barrier_all(cb);
    g_vk_mm.in_hud_pass = 1; g_vk_mm.pass_im = im;
    g_vk_mm.pass_w = w; g_vk_mm.pass_h = h;
    g_vk_mm.save_x = sx; g_vk_mm.save_y = sy; g_vk_mm.save_w = sw; g_vk_mm.save_h = sh;
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
    if ((!half && !flt) || !vbuf) return 0;
    int32_t lim = cnt < 16 ? cnt : 16;
    size_t elem = half ? 4u : 8u;
    for (int32_t v = 0; v < lim; v++) {
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
        if (v == 0) { mn[0] = mx[0] = xy[0]; mn[1] = mx[1] = xy[1]; }
        else { if (xy[0] < mn[0]) mn[0] = xy[0]; if (xy[0] > mx[0]) mx[0] = xy[0]; if (xy[1] < mn[1]) mn[1] = xy[1]; if (xy[1] > mx[1]) mx[1] = xy[1]; }
    }
    return 1;
}

static void vk_minimap_note_draw(
    int indirect, uint32_t st, uint32_t dt, uint32_t c0_mask,
    VkFormat vfmt, uint32_t off, uint32_t stride, uint64_t vbuf, uint64_t vbuf_sz,
    const p_draw *dw, const p_draw_idx *di, float ax, float ay, float acx, float acy) {
    if (!g_vk_mm.in_hud_pass || indirect || st == 0u || dt != 0u || (c0_mask & 0xfu) != 0u) return;
    int32_t cnt = dw ? dw->count : (di ? di->count : 0);
    if (cnt < 4 || cnt > 12) return;
    float mn[2] = {0}, mx[2] = {0};
    if (!vk_mm_attr_xy_bounds(vfmt, off, stride, vbuf, vbuf_sz, dw, di, cnt, mn, mx)) return;
    mn[0] = acx + (mn[0] - acx) * ax; mx[0] = acx + (mx[0] - acx) * ax;
    mn[1] = acy + (mn[1] - acy) * ay; mx[1] = acy + (mx[1] - acy) * ay;
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
    g_vk_mm.hit_x = x0; g_vk_mm.hit_y = y0; g_vk_mm.hit_side = side; g_vk_mm.pass_hit = 1;
    g_vk_mm.learn_w = g_vk_mm.pass_w; g_vk_mm.learn_h = g_vk_mm.pass_h;
    g_vk_mm.learn_cx = (int32_t)lrintf(cx); g_vk_mm.learn_cy = (int32_t)lrintf(cy);
    g_vk_mm.learn_half = (int32_t)(side / 2u);
}

static void vk_mm_stage_capture(VkCommandBuffer cb, VkImage im, unsigned lv, unsigned l0, VkFormat fmt) {
    uint32_t side = g_vk_mm.hit_side;
    if (g_vk_mm.wrote_in_frame || atomic_load_explicit(&g_vk_mm.pending_valid, memory_order_acquire)) return;
    if ((g_vk_mm.frame_counter & 1u) != 0u || (VkDeviceSize)side * side * 4u > VK_MM_BUF_BYTES) return;
    if (!vk_mm_ensure_ring()) return;
    vk_mm_slot *dst = &g_vk_mm.slot[g_vk_mm.write_slot];
    VkBufferImageCopy rg = {
        .bufferOffset = 0, .bufferRowLength = 0, .bufferImageHeight = 0,
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, lv, l0, 1},
        .imageOffset = {g_vk_mm.hit_x, g_vk_mm.hit_y, 0},
        .imageExtent = {side, side, 1}
    };
    vkCmdCopyImageToBuffer(cb, im, VK_IMAGE_LAYOUT_GENERAL, dst->buf, 1, &rg);
    VkBufferMemoryBarrier2 bb = {
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT, .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT, .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = dst->buf, .offset = 0, .size = (VkDeviceSize)side * side * 4u
    };
    VkDependencyInfo di = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &bb};
    vkCmdPipelineBarrier2(cb, &di);
    g_vk_mm.wrote_in_frame = 1;
    g_vk_mm.write_w = side; g_vk_mm.write_h = side; g_vk_mm.write_fmt = fmt;
}

static void vk_mm_restore_display0(VkCommandBuffer cb, VkImage im, unsigned lv, unsigned l0) {
    if (!g_vk_mm.save_ready) return;
    int32_t pad = (int32_t)(g_vk_mm.hit_side / 24u);
    if (pad < 12) pad = 12; else if (pad > 32) pad = 32;
    int32_t x0 = g_vk_mm.hit_x - pad, y0 = g_vk_mm.hit_y - pad;
    int32_t x1 = g_vk_mm.hit_x + (int32_t)g_vk_mm.hit_side + pad;
    int32_t y1 = g_vk_mm.hit_y + (int32_t)g_vk_mm.hit_side + pad;
    int32_t sx0 = g_vk_mm.save_x, sy0 = g_vk_mm.save_y;
    int32_t sx1 = sx0 + (int32_t)g_vk_mm.save_w, sy1 = sy0 + (int32_t)g_vk_mm.save_h;
    if (x0 < sx0) x0 = sx0; if (y0 < sy0) y0 = sy0;
    if (x1 > sx1) x1 = sx1; if (y1 > sy1) y1 = sy1;
    if (x1 <= x0 || y1 <= y0) return;
    vk_barrier_all(cb);
    VkImageCopy cp = {
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {x0 - sx0, y0 - sy0, 0},
        {VK_IMAGE_ASPECT_COLOR_BIT, lv, l0, 1}, {x0, y0, 0},
        {(uint32_t)(x1 - x0), (uint32_t)(y1 - y0), 1}
    };
    vkCmdCopyImage(cb, g_vk_mm.save_img, VK_IMAGE_LAYOUT_GENERAL, im, VK_IMAGE_LAYOUT_GENERAL, 1, &cp);
    vk_barrier_all(cb);
}

static void vk_minimap_pass_end(
    VkCommandBuffer cb, vk_tex *t, unsigned lv, unsigned l0, VkFormat fmt, uint64_t serial) {
    (void)fmt;
    int was_hud = g_vk_mm.in_hud_pass, hit = g_vk_mm.pass_hit;
    g_vk_mm.in_hud_pass = 0; g_vk_mm.pass_hit = 0;
    if (!was_hud || !hit || !t || !V.dev || vk_tex_image(t) != g_vk_mm.pass_im) return;
    if (!atomic_load_explicit(&g_vk_mm.want_active, memory_order_acquire)) return;
    VkCommandBuffer copy_cb = cb ? cb : vk_cb();
    if (!copy_cb) return;
    VkFormat tf = vk_tex_format(t);
    vk_mm_find_cand(g_vk_mm.hit_side, g_vk_mm.hit_side, tf, serial);
    vk_mm_stage_capture(copy_cb, vk_tex_image(t), lv, l0, tf);
    vk_mm_restore_display0(copy_cb, vk_tex_image(t), lv, l0);
    g_vk_mm.saved_hit_frame = g_vk_mm.frame_counter;
    g_vk_mm.saved_hit_img = vk_tex_image(t);
}

static void vk_minimap_pass_abort(void) {
    g_vk_mm.in_hud_pass = 0;
    g_vk_mm.pass_hit = 0;
}

static void vk_minimap_present(void) {
    g_vk_mm.frame_counter++;
    if (g_vk_mm.wrote_in_frame) {
        g_vk_mm.pending_slot = g_vk_mm.write_slot;
        g_vk_mm.pending_fence = mtl_last_fence() + 1u;
        g_vk_mm.pending_frame = g_vk_mm.frame_counter;
        g_vk_mm.pending_w = g_vk_mm.write_w;
        g_vk_mm.pending_h = g_vk_mm.write_h;
        g_vk_mm.pending_fmt = g_vk_mm.write_fmt;
        atomic_store_explicit(&g_vk_mm.pending_valid, 1, memory_order_release);
        g_vk_mm.wrote_in_frame = 0;
    }
    if (!atomic_load_explicit(&g_vk_mm.pending_valid, memory_order_relaxed)) return;
    int done = (g_vk_mm.frame_counter - g_vk_mm.pending_frame) >= 1u
        && (g_vk_mm.pending_fence == 0 || mtl_wait(g_vk_mm.pending_fence, 0));
    if (!done) return;
    pthread_mutex_lock(&g_vk_mm.mu);
    int prev = g_vk_mm.ready_slot;
    g_vk_mm.ready_slot = g_vk_mm.pending_slot;
    g_vk_mm.ready_w = g_vk_mm.pending_w;
    g_vk_mm.ready_h = g_vk_mm.pending_h;
    g_vk_mm.ready_fmt = g_vk_mm.pending_fmt;
    g_vk_mm.seq++;
    g_vk_mm.write_slot = prev >= 0 ? prev : (g_vk_mm.pending_slot ^ 1);
    atomic_store_explicit(&g_vk_mm.pending_valid, 0, memory_order_release);
    pthread_mutex_unlock(&g_vk_mm.mu);
}

static void vk_mm_copy_rgba(uint8_t *dst, const uint8_t *src, uint32_t w, uint32_t h, VkFormat fmt) {
    int bgra = (fmt == VK_FORMAT_B8G8R8A8_UNORM || fmt == VK_FORMAT_B8G8R8A8_SRGB);
    size_t n = (size_t)w * h;
    for (size_t i = 0; i < n; i++) {
        size_t o = i * 4u;
        dst[o + 0] = src[o + (bgra ? 2 : 0)];
        dst[o + 1] = src[o + 1];
        dst[o + 2] = src[o + (bgra ? 0 : 2)];
        dst[o + 3] = 255u;
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
    int rslot = g_vk_mm.ready_slot;
    uint32_t w = g_vk_mm.ready_w, h = g_vk_mm.ready_h, seq = g_vk_mm.seq;
    if (rslot >= 0 && rslot < VK_MM_SLOTS && g_vk_mm.slot[rslot].ptr && seq != client_seq
            && w > 0 && w <= VK_MM_MAX_SIDE && h > 0 && h <= VK_MM_MAX_SIDE) {
        size_t need = (size_t)w * h * 4u;
        if (rgba_out && need > 0 && max_bytes > 0 && (size_t)max_bytes >= need) {
            vk_mm_copy_rgba(rgba_out, g_vk_mm.slot[rslot].ptr, w, h, g_vk_mm.ready_fmt);
            updated = 1;
        }
    }
    meta[0] = (int32_t)w;
    meta[1] = (int32_t)h;
    meta[2] = (int32_t)seq;
    meta[3] = cnt;
    meta[4] = cidx;
    pthread_mutex_unlock(&g_vk_mm.mu);
    return updated;
}

int vk_minimap_cycle(void) {
    uint32_t next = atomic_fetch_add_explicit(&g_vk_mm.cand_idx, 1u, memory_order_acq_rel) + 1u;
    int cnt = atomic_load_explicit(&g_vk_mm.cand_count, memory_order_acquire);
    return cnt > 0 ? (int)(next % (uint32_t)cnt) : 0;
}

void vk_minimap_enable(int on) {
    atomic_store_explicit(&g_vk_mm.want_active, on ? 1 : 0, memory_order_release);
}
