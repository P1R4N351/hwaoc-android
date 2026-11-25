// SPDX-License-Identifier: MIT
#pragma once
// Native Vulkan HUD minimap, left-HUD & objective per-draw attachment redirection (2-slot staging rings)
// + one-shot UI texture harvest. Redirects HUD draws at recording time onto an offscreen color attachment
// sharing the pass depth/stencil views; never masks, clears, or overwrites Display 0.

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    VK_MM_MIN_SIDE = 96u,
    VK_MM_MAX_SIDE = 768u,
    VK_MM_SAVE_MAX_W = 2560u,
    VK_MM_SAVE_MAX_H = 1440u,
    VK_OBJ_MAX_W = 1024u,
    VK_OBJ_MAX_H = 288u,
    VK_LHUD_MAX_W = 640u,
    VK_LHUD_MAX_H = 1440u,
    VK_UI_TEX_SIDE = 512u,
    VK_MM_SLOTS = 2,
    VK_MM_MAX_CAND = 8,
    VK_MM_TTL = 2u
};
#define VK_MM_BUF_BYTES ((VkDeviceSize)(VK_MM_MAX_SIDE * VK_MM_MAX_SIDE * 4u))
#define VK_OBJ_BUF_BYTES ((VkDeviceSize)(VK_OBJ_MAX_W * VK_OBJ_MAX_H * 4u))
#define VK_LHUD_BUF_BYTES ((VkDeviceSize)(VK_LHUD_MAX_W * VK_LHUD_MAX_H * 4u))
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
    vk_mm_stream mm, obj, lhud;
    vk_mm_slot ui_slot;
    VkImage ui_img;
    VkDeviceMemory ui_mem;
    int ui_ready, ui_failed, ui_wrote_frame, ui_pending, ui_has_ready;
    uint32_t ui_pending_frame, ui_seq;
    uint64_t ui_pending_fence, ui_seen[8];
    int ui_seen_n, ui_seen_pos;
    _Atomic int ui_want;
    vk_ui_cand ui_cand;
    VkImage hud_img, hud_cleared_im, hud_state_im, pass_im, saved_hit_img;
    VkImageView hud_view, main_cview, depth_view, stencil_view;
    VkDeviceMemory hud_mem;
    VkFormat hud_fmt;
    VkImageLayout main_clayout, depth_layout, stencil_layout;
    VkRect2D pass_area;
    uint32_t hud_w, hud_h, pass_w, pass_h;
    int hud_ready, hud_failed, has_depth, has_stencil_att, cur_hud, pass_hud_cleared;
    vk_mm_cand cand[VK_MM_MAX_CAND];
    _Atomic int cand_count, want_active;
    _Atomic uint32_t cand_idx;
    uint32_t frame_counter, mm_ttl, obj_ttl, lhud_ttl;
    uint32_t hud_cleared_frame, hud_state_frame, frame_got_mm, frame_got_obj, frame_got_lhud;
    int saw_mm_frame, saw_obj_frame, saw_lhud_frame;
    int in_hud_pass, pass_opaque_fs, pass_modal, pass_dialog;
    int pass_mm_hit, pass_obj_hit, obj_bounds_init, pass_obj_icon, pass_obj_pill, pass_obj_text, cand_obj_text;
    int pass_lhud_any, pass_lhud_top, prev_lhud_top, nfp, fr_modal, mm_open, nwm_cur, nwm_prev;
    float fp[8][4], wm_cur[4][4], wm_prev[4][4];
    uint64_t cur_tex, ring_tex;
    float last_q[4]; int last_small;
    vk_mm_rect hit_mm, hit_obj, prev_hit_obj, hit_lhud;
    uint32_t learn_mm_w, learn_mm_h;
    int32_t learn_mm_cx, learn_mm_cy, learn_mm_half;
    float obj_umin_x, obj_umin_y, obj_umax_x, obj_umax_y;
    float obj_icon_cx, obj_icon_core_w, obj_icon_cy, obj_pill_mn_x, obj_pill_cy;
    float cand_text_mn_x, cand_text_cy, learn_obj_icon_x, learn_obj_text_x, learn_obj_cy;
    uint8_t cur_wid, cur_wid_bg, wid_dest[15];
    int wid_init, skip_cur_draw, pass_lhud_tagged, pass_obj_tagged, pass_mm_tagged, pass_mm_square, mm_tbox_init;
    float mm_tbox[4];
} g_vk_mm = {
    .mu = PTHREAD_MUTEX_INITIALIZER,
    .mm = { .slot_bytes = VK_MM_BUF_BYTES, .pending_slot = -1, .ready_slot = -1 },
    .obj = { .slot_bytes = VK_OBJ_BUF_BYTES, .pending_slot = -1, .ready_slot = -1 },
    .lhud = { .slot_bytes = VK_LHUD_BUF_BYTES, .pending_slot = -1, .ready_slot = -1 },
};

static struct {
    int on, valid, hb; char cls; uint8_t wid; int32_t cnt; uint32_t stt, sref, blend, mask, seq; float mn[2], mx[2];
    uint8_t b_srgb, b_drgb, b_op, b_sa, b_da, b_opa;
} g_vk_mm_tr;

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

static void vk_mm_destroy_hud(void) {
    if (g_vk_mm.hud_view) vkDestroyImageView(V.dev, g_vk_mm.hud_view, NULL);
    if (g_vk_mm.hud_img) vkDestroyImage(V.dev, g_vk_mm.hud_img, NULL);
    if (g_vk_mm.hud_mem) vk_memory_release(g_vk_mm.hud_mem, VK_MEMORY_DIAGNOSTIC);
    g_vk_mm.hud_view = VK_NULL_HANDLE; g_vk_mm.hud_img = VK_NULL_HANDLE;
    g_vk_mm.hud_mem = VK_NULL_HANDLE; g_vk_mm.hud_ready = 0;
    g_vk_mm.hud_cleared_frame = 0u; g_vk_mm.hud_state_frame = 0u;
    g_vk_mm.hud_cleared_im = VK_NULL_HANDLE; g_vk_mm.hud_state_im = VK_NULL_HANDLE;
    g_vk_mm.prev_hit_obj = (vk_mm_rect){0, 0, 0u, 0u};
}

static int vk_mm_ensure_hud(VkCommandBuffer cb, uint32_t w, uint32_t h, VkFormat fmt) {
    if (w > VK_MM_SAVE_MAX_W || h > VK_MM_SAVE_MAX_H) return 0;
    if (g_vk_mm.hud_ready) {
        if (g_vk_mm.hud_fmt == fmt && g_vk_mm.hud_w == w && g_vk_mm.hud_h == h) return 1;
        vkDeviceWaitIdle(V.dev);
        vk_mm_destroy_hud();
    }
    if (g_vk_mm.hud_failed || !V.dev || !cb || !w || !h) return 0;
    if (!vk_mm_create_img(cb, w, h, fmt,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            &g_vk_mm.hud_img, &g_vk_mm.hud_mem)) {
        g_vk_mm.hud_failed = 1; return 0;
    }
    VkImageViewCreateInfo vi = {
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = g_vk_mm.hud_img, .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = fmt, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u}
    };
    if (vkCreateImageView(V.dev, &vi, NULL, &g_vk_mm.hud_view)) {
        vk_mm_destroy_hud(); g_vk_mm.hud_failed = 1; return 0;
    }
    g_vk_mm.hud_fmt = fmt; g_vk_mm.hud_w = w; g_vk_mm.hud_h = h;
    g_vk_mm.hud_ready = 1;
    return 1;
}

static vk_mm_rect vk_mm_lhud_box(uint32_t w, uint32_t h) {
    uint32_t lw = ((uint32_t)lrintf((float)w * 0.28f)) & ~3u;
    if (lw > VK_LHUD_MAX_W) lw = VK_LHUD_MAX_W;
    if (lw > (w & ~3u)) lw = w & ~3u;
    uint32_t lh = h < VK_LHUD_MAX_H ? h : VK_LHUD_MAX_H;
    return (vk_mm_rect){0, 0, lw, lh};
}

#include "../../runtime/ktgl_wid_ids.h"

static uint8_t vk_mm_default_wid_dest(uint8_t wid) {
    if (wid == KTGL_WID_MINIMAP) return KTGL_DEST_MM;
    if (wid == KTGL_WID_OBJECTIVE) return KTGL_DEST_OBJ;
    if (wid == KTGL_WID_SPEECH || wid == KTGL_WID_WORLD_MARKER || wid == KTGL_WID_MENU) return KTGL_DEST_TOP;
    if (wid >= KTGL_WID_HEARTS && wid < KTGL_WID_COUNT) return KTGL_DEST_LHUD;
    return KTGL_DEST_FALLBACK;
}

static uint8_t vk_mm_parse_dest_val(const char *s, size_t n, uint8_t wid) {
    if (n == 3u && !strncmp(s, "top", 3u)) return KTGL_DEST_TOP;
    if (n == 4u && !strncmp(s, "hide", 4u)) return KTGL_DEST_HIDE;
    if (n == 6u && !strncmp(s, "bottom", 6u)) {
        uint8_t d = vk_mm_default_wid_dest(wid);
        return (d == KTGL_DEST_TOP || d == KTGL_DEST_FALLBACK) ? KTGL_DEST_LHUD : d;
    }
    if (n == 2u && !strncmp(s, "mm", 2u)) return KTGL_DEST_MM;
    if (n == 3u && !strncmp(s, "obj", 3u)) return KTGL_DEST_OBJ;
    if (n == 4u && !strncmp(s, "lhud", 4u)) return KTGL_DEST_LHUD;
    return vk_mm_default_wid_dest(wid);
}

static void vk_mm_wid_dest_init(void) {
    if (g_vk_mm.wid_init) return;
    for (uint8_t i = 0u; i < KTGL_WID_COUNT; i++) g_vk_mm.wid_dest[i] = vk_mm_default_wid_dest(i);
    const char *e = getenv("TOTK_KTGL_WID_DEST");
    const char *p = e;
    for (int step = 0; p && *p && step < 64; step++) {
        while (*p == ',' || *p == ' ' || *p == ';') p++;
        if (!*p) break;
        char *eq = NULL;
        long id = strtol(p, &eq, 10);
        if (!eq || *eq != '=' || id <= 0 || id >= KTGL_WID_COUNT) {
            while (*p && *p != ',' && *p != ';') p++;
            continue;
        }
        const char *vs = eq + 1;
        size_t vl = 0u;
        while (vs[vl] && vs[vl] != ',' && vs[vl] != ';' && vs[vl] != ' ') vl++;
        g_vk_mm.wid_dest[(uint8_t)id] = vk_mm_parse_dest_val(vs, vl, (uint8_t)id);
        p = vs + vl;
    }
    g_vk_mm.wid_init = 1;
}

static uint8_t vk_mm_wid_dest(uint8_t wid) {
    vk_mm_wid_dest_init();
    return wid < KTGL_WID_COUNT ? g_vk_mm.wid_dest[wid] : KTGL_DEST_FALLBACK;
}

static int vk_mm_is_center_reset(const float *mn, const float *mx, float w) {
    float bw = mx[0] - mn[0], bh = mx[1] - mn[1];
    return bw <= 2.5f && bh <= 2.5f && mx[0] < w * 0.55f;
}

static void vk_minimap_note_draw_wid(uint8_t wid) {
    uint8_t base = wid & 0x7fu;
    g_vk_mm.cur_wid = base < KTGL_WID_COUNT ? base : 0u;
    g_vk_mm.cur_wid_bg = (wid & (uint8_t)KTGL_WID_FLAG_BG) != 0u && g_vk_mm.cur_wid != 0u;
}

static int vk_minimap_on_hud_target(void) {
    return g_vk_mm.in_hud_pass && g_vk_mm.cur_hud;
}

static int vk_minimap_in_hud_pass(void) { return g_vk_mm.in_hud_pass; }

static int vk_minimap_skip_draw(void) {
    return g_vk_mm.in_hud_pass && g_vk_mm.skip_cur_draw;
}

static int vk_hud_override_blend(
    uint32_t blend_en, uint32_t *chmask_io, uint32_t src_rgb, uint32_t dst_rgb,
    uint32_t eq_rgb, uint32_t *src_a_out, uint32_t *dst_a_out, uint32_t *eq_a_out) {
    if (!(blend_en & 1u) || (*chmask_io & 0xfu) == 0u || (eq_rgb != 0u && eq_rgb != 1u)) return 0;
    if ((src_rgb == 5u || src_rgb == 2u) && dst_rgb == 6u) {
        *chmask_io |= 0x8u; *src_a_out = 2u; *dst_a_out = 6u; *eq_a_out = 1u;
        return 1;
    }
    if ((src_rgb == 5u || src_rgb == 2u) && dst_rgb == 2u) {
        *chmask_io |= 0x8u; *src_a_out = 1u; *dst_a_out = 2u; *eq_a_out = 1u;
        return 1;
    }
    return 0;
}

static void vk_minimap_pass_begin(
    VkCommandBuffer cb, vk_tex *t, unsigned lv, unsigned l0, uint32_t w, uint32_t h,
    VkFormat att_fmt, VkRect2D area, VkImageView main_cview, VkImageLayout main_clayout,
    const VkRenderingAttachmentInfo *dp, const VkRenderingAttachmentInfo *sp,
    int load_c0, int ok) {
    vk_mm_wid_dest_init();
    g_vk_mm.in_hud_pass = 0; g_vk_mm.cur_hud = 0; g_vk_mm.pass_hud_cleared = 0;
    g_vk_mm.skip_cur_draw = 0;
    g_vk_mm.pass_opaque_fs = 0; g_vk_mm.pass_modal = 0; g_vk_mm.pass_dialog = 0;
    g_vk_mm.pass_lhud_tagged = 0; g_vk_mm.pass_obj_tagged = 0; g_vk_mm.pass_mm_tagged = 0;
    g_vk_mm.ui_cand.img = VK_NULL_HANDLE; g_vk_mm.ui_cand.score = -1;
    if (!ok || !cb || !t || !V.dev || !vk_tex_image(t) || !main_cview || !sp || !sp->imageView) return;
    if (!atomic_load_explicit(&g_vk_mm.want_active, memory_order_acquire)) return;
    if (!load_c0 || vk_tex_samples(t) > 1u || vk_tex_is3d(t) || !(vk_tex_aspect(t) & VK_IMAGE_ASPECT_COLOR_BIT)) return;
    if (w < 640u || h < 360u || w > VK_MM_SAVE_MAX_W || h > VK_MM_SAVE_MAX_H) return;
    if (lv >= vk_tex_levels(t) || l0 >= vk_tex_layers(t)) return;
    if (area.offset.x != 0 || area.offset.y != 0 || area.extent.width != w || area.extent.height != h) return;
    if (!vk_mm_supported_fmt(att_fmt) || !vk_mm_ensure_hud(cb, w, h, att_fmt)) return;
    VkImage im = vk_tex_image(t);
    uint32_t fr = g_vk_mm.frame_counter;
    if (g_vk_mm.hud_cleared_frame == fr && g_vk_mm.hud_cleared_im == im) g_vk_mm.pass_hud_cleared = 1;
    if (g_vk_mm.hud_state_frame != fr || g_vk_mm.hud_state_im != im) {
        g_vk_mm.pass_mm_hit = 0; g_vk_mm.pass_mm_square = 0; g_vk_mm.mm_tbox_init = 0; g_vk_mm.pass_obj_hit = 0; g_vk_mm.obj_bounds_init = 0;
        g_vk_mm.pass_obj_icon = 0; g_vk_mm.pass_obj_pill = 0; g_vk_mm.pass_obj_text = 0; g_vk_mm.cand_obj_text = 0;
        g_vk_mm.obj_icon_cx = 0.f; g_vk_mm.obj_icon_core_w = 0.f;
        if (g_vk_mm.nwm_cur || g_vk_mm.pass_lhud_top) {
            memcpy(g_vk_mm.wm_prev, g_vk_mm.wm_cur, sizeof g_vk_mm.wm_prev);
            g_vk_mm.nwm_prev = g_vk_mm.nwm_cur; g_vk_mm.nwm_cur = 0;
            g_vk_mm.prev_lhud_top = g_vk_mm.pass_lhud_top; g_vk_mm.pass_lhud_top = 0;
        }
        g_vk_mm.pass_lhud_any = 0; g_vk_mm.nfp = 0; g_vk_mm.fr_modal = 0; g_vk_mm.mm_open = 0;
        g_vk_mm.hud_state_frame = fr; g_vk_mm.hud_state_im = im;
    }
    g_vk_mm.pass_area = area; g_vk_mm.main_cview = main_cview; g_vk_mm.main_clayout = main_clayout;
    g_vk_mm.has_depth = (dp && dp->imageView) ? 1 : 0;
    if (g_vk_mm.has_depth) { g_vk_mm.depth_view = dp->imageView; g_vk_mm.depth_layout = dp->imageLayout; }
    g_vk_mm.has_stencil_att = 1; g_vk_mm.stencil_view = sp->imageView; g_vk_mm.stencil_layout = sp->imageLayout;
    g_vk_mm.in_hud_pass = 1; g_vk_mm.pass_im = im; g_vk_mm.pass_w = w; g_vk_mm.pass_h = h;
    g_vk_mm.last_small = 0; g_vk_mm.cur_tex = 0u;
    if (g_vk_mm.fr_modal) g_vk_mm.pass_modal = 1;
    { const char *e = getenv("TOTK_VK_MM_TRACE"); int tn = e && *e ? atoi(e) : 0;
      g_vk_mm_tr.on = tn > 0 && (fr % (uint32_t)tn) == 0u; g_vk_mm_tr.valid = 0; }
}

static void vk_mm_clear_hud_img(VkCommandBuffer cb) {
    VkImageMemoryBarrier2 pb = {
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT, .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL, .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = g_vk_mm.hud_img, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u}
    };
    VkDependencyInfo di = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1u, .pImageMemoryBarriers = &pb};
    VkClearColorValue clr = {{0.f, 0.f, 0.f, 0.f}};
    VkImageSubresourceRange rr = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
    vkCmdPipelineBarrier2(cb, &di);
    vkCmdClearColorImage(cb, g_vk_mm.hud_img, VK_IMAGE_LAYOUT_GENERAL, &clr, 1u, &rr);
    g_vk_mm.pass_hud_cleared = 1;
    g_vk_mm.hud_cleared_frame = g_vk_mm.frame_counter;
    g_vk_mm.hud_cleared_im = g_vk_mm.pass_im;
}

static int vk_mm_switch_target(VkCommandBuffer cb, int want_hud) {
    if (!g_vk_mm.in_hud_pass || !g_vk_mm.hud_ready || g_vk_mm.cur_hud == want_hud) return 0;
    vk_occlusion_end(cb);
    vkCmdEndRendering(cb);
    if (want_hud && !g_vk_mm.pass_hud_cleared) vk_mm_clear_hud_img(cb);
    VkMemoryBarrier2 mb = {
        VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
        .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
            | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
    };
    VkDependencyInfo di = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1u, .pMemoryBarriers = &mb};
    vkCmdPipelineBarrier2(cb, &di);
    VkRenderingAttachmentInfo ca = {
        VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = want_hud ? g_vk_mm.hud_view : g_vk_mm.main_cview,
        .imageLayout = want_hud ? VK_IMAGE_LAYOUT_GENERAL : g_vk_mm.main_clayout,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, .storeOp = VK_ATTACHMENT_STORE_OP_STORE
    };
    VkRenderingAttachmentInfo da = {
        VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = g_vk_mm.depth_view, .imageLayout = g_vk_mm.depth_layout,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, .storeOp = VK_ATTACHMENT_STORE_OP_STORE
    };
    VkRenderingAttachmentInfo sa = {
        VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = g_vk_mm.stencil_view, .imageLayout = g_vk_mm.stencil_layout,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, .storeOp = VK_ATTACHMENT_STORE_OP_STORE
    };
    VkRenderingInfo ri = {
        VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = g_vk_mm.pass_area, .layerCount = 1u,
        .colorAttachmentCount = 1u, .pColorAttachments = &ca,
        .pDepthAttachment = g_vk_mm.has_depth ? &da : NULL,
        .pStencilAttachment = g_vk_mm.has_stencil_att ? &sa : NULL
    };
    vkCmdBeginRendering(cb, &ri);
    g_vk_mm.cur_hud = want_hud;
    return 1;
}

static int vk_minimap_bind_main(VkCommandBuffer cb) {
    return vk_mm_switch_target(cb, 0);
}

static float vk_mm_f16(uint16_t h) {
    uint32_t sg = (uint32_t)(h >> 15) << 31, e = (h >> 10) & 31u, m = h & 1023u;
    if (e == 0u) return m ? ((sg ? -1.f : 1.f) * (float)m / 16777216.f) : (sg ? -0.f : 0.f);
    uint32_t u = (e == 31u) ? (sg | 0x7f800000u | (m << 13)) : (sg | ((e + 112u) << 23) | (m << 13));
    float f = 0.f; memcpy(&f, &u, 4); return f;
}

typedef struct {
    VkFormat vfmt;
    uint32_t offset, stride, divisor;
    uint64_t vbuf, vbuf_sz;
} vk_mm_attr;

typedef struct {
    int nattr, has_vp, base_instance;
    uint32_t vin_mask, st, dt, c0_mask, blend_en;
    vk_mm_attr attr[16];
    uint64_t ubo_addr[4], ubo_sz[4];
    float vp0[4], ax, ay, acx, acy;
} vk_mm_draw_ctx;

static int vk_mm_read_mem(uint64_t addr, void *out, size_t n) {
    VkBuffer bb = VK_NULL_HANDLE; uint64_t oo = 0, av = 0;
    if (!addr || !vk_resolve_span(addr, &bb, &oo, &av) || av < n) return 0;
    vk_ub_read(addr, out, n);
    return 1;
}

static int vk_mm_fmt_info(VkFormat vfmt, int *is_half) {
    *is_half = 0;
    if (vfmt == VK_FORMAT_R16_SFLOAT) { *is_half = 1; return 1; }
    if (vfmt == VK_FORMAT_R16G16_SFLOAT) { *is_half = 1; return 2; }
    if (vfmt == VK_FORMAT_R16G16B16_SFLOAT) { *is_half = 1; return 3; }
    if (vfmt == VK_FORMAT_R16G16B16A16_SFLOAT) { *is_half = 1; return 4; }
    if (vfmt == VK_FORMAT_R32_SFLOAT) return 1;
    if (vfmt == VK_FORMAT_R32G32_SFLOAT) return 2;
    if (vfmt == VK_FORMAT_R32G32B32_SFLOAT) return 3;
    if (vfmt == VK_FORMAT_R32G32B32A32_SFLOAT) return 4;
    return 0;
}

static int64_t vk_mm_vertex_idx(const p_draw *dw, const p_draw_idx *di, int32_t v) {
    if (di) {
        uint32_t ix = 0u;
        size_t isz = di->itype == 0 ? 1u : (di->itype == 1 ? 2u : 4u);
        if (!di->ibuf || !vk_mm_read_mem(di->ibuf + (uint64_t)v * isz, &ix, isz)) return -1;
        return (int64_t)di->base_vertex + (int64_t)ix;
    }
    return dw ? (int64_t)dw->first + v : -1;
}

static int vk_mm_read_fvec(
    const vk_mm_draw_ctx *dc, int loc, int64_t vi, int ncomp, int half, float out[4]) {
    out[0] = 0.f; out[1] = 0.f; out[2] = 0.f; out[3] = 1.f;
    if (loc < 0 || loc >= dc->nattr || loc >= 16 || ncomp < 1 || ncomp > 4) return 0;
    const vk_mm_attr *a = &dc->attr[loc];
    if (!a->vbuf) return 0;
    int64_t eff = a->divisor ? (int64_t)dc->base_instance : vi;
    if (eff < 0) return 0;
    size_t elem = (size_t)ncomp * (half ? 2u : 4u);
    uint64_t vo = (uint64_t)eff * a->stride + a->offset;
    if (a->vbuf_sz && (vo > a->vbuf_sz || a->vbuf_sz - vo < elem)) return 0;
    uint8_t raw[16] = {0};
    if (!vk_mm_read_mem(a->vbuf + vo, raw, elem)) return 0;
    if (half) {
        uint16_t hv[4] = {0};
        memcpy(hv, raw, elem);
        for (int k = 0; k < ncomp; k++) out[k] = vk_mm_f16(hv[k]);
    } else {
        memcpy(out, raw, elem);
    }
    for (int k = 0; k < ncomp; k++) if (!isfinite(out[k])) return 0;
    return 1;
}

static int vk_mm_find_sprite_locs(
    const vk_mm_draw_ctx *dc, int *pos_loc, int *pos_n, int *pos_half, int *idx_loc, int *idx_half) {
    *pos_loc = -1; *pos_n = 0; *pos_half = 0; *idx_loc = -1; *idx_half = 0;
    int n = dc->nattr < 16 ? dc->nattr : 16;
    if (n <= 0) return 0;
    uint32_t mask = dc->vin_mask ? dc->vin_mask : (n == 16 ? 0xffffu : ((1u << n) - 1u));
    for (int loc = 0; loc < n; loc++) {
        if (!(mask & (1u << loc))) continue;
        int h = 0, nc = vk_mm_fmt_info(dc->attr[loc].vfmt, &h);
        if (nc >= 2) { *pos_loc = loc; *pos_n = nc; *pos_half = h; }
    }
    if (*pos_loc < 0) return 0;
    for (int loc = 0; loc < *pos_loc; loc++) {
        if (!(mask & (1u << loc))) continue;
        int h = 0, nc = vk_mm_fmt_info(dc->attr[loc].vfmt, &h);
        if (nc == 1) { *idx_loc = loc; *idx_half = h; break; }
    }
    return 1;
}

static void vk_mm_viewport(const vk_mm_draw_ctx *dc, float vp[4], float *gvw, float *gvh) {
    float vx = dc->has_vp ? dc->vp0[0] : 0.f;
    float vy = dc->has_vp ? dc->vp0[1] : 0.f;
    float vw = dc->has_vp ? dc->vp0[2] : (float)g_vk_mm.pass_w;
    float vh = dc->has_vp ? dc->vp0[3] : (float)g_vk_mm.pass_h;
    *gvw = fabsf(vw); *gvh = fabsf(vh);
    if (!vw) vw = 1.f;
    if (!vh) vh = 1.f;
    vp[0] = vx; vp[1] = vy + vh; vp[2] = vw; vp[3] = -vh;
}

static int vk_mm_is_ortho_mat(const float p[16]) {
    for (int i = 0; i < 16; i++) if (!isfinite(p[i])) return 0;
    if (fabsf(p[15] - 1.f) >= 1e-2f) return 0;
    if (fabsf(p[1]) >= 1e-3f || fabsf(p[2]) >= 1e-3f || fabsf(p[3]) >= 1e-3f) return 0;
    if (fabsf(p[4]) >= 1e-3f || fabsf(p[6]) >= 1e-3f || fabsf(p[7]) >= 1e-3f) return 0;
    if (fabsf(p[8]) >= 1e-3f || fabsf(p[9]) >= 1e-3f || fabsf(p[11]) >= 1e-3f) return 0;
    float ax = fabsf(p[0]), ay = fabsf(p[5]);
    if (ax < (2.f / 8192.f) || ax > (2.f / 160.f)) return 0;
    if (ay < (2.f / 8192.f) || ay > (2.f / 160.f)) return 0;
    return fabsf(p[12]) <= 4.f && fabsf(p[13]) <= 4.f;
}

static int vk_mm_is_proj_mat(const float p[16]) {
    for (int i = 0; i < 16; i++) if (!isfinite(p[i])) return 0;
    float nx = p[0] * p[0] + p[1] * p[1] + p[2] * p[2];
    float ny = p[4] * p[4] + p[5] * p[5] + p[6] * p[6];
    if (nx < 1e-10f || nx > 16.f || ny < 1e-10f || ny > 16.f) return 0;
    float dot = p[0] * p[4] + p[1] * p[5] + p[2] * p[6];
    if (dot * dot >= 0.5f * nx * ny) return 0;
    float wnorm = fabsf(p[3]) + fabsf(p[7]) + fabsf(p[11]) + fabsf(p[15]);
    return wnorm >= 1e-4f;
}

static int vk_mm_proj_point(
    const float pos[4], const float m[12], const float p[16], const float vp[4], float xy[2]) {
    float wx = pos[0] * m[0] + pos[1] * m[1] + pos[2] * m[2] + pos[3] * m[3];
    float wy = pos[0] * m[4] + pos[1] * m[5] + pos[2] * m[6] + pos[3] * m[7];
    float wz = pos[0] * m[8] + pos[1] * m[9] + pos[2] * m[10] + pos[3] * m[11];
    float cx = p[0] * wx + p[4] * wy + p[8] * wz + p[12] * pos[3];
    float cy = p[1] * wx + p[5] * wy + p[9] * wz + p[13] * pos[3];
    float cw = p[3] * wx + p[7] * wy + p[11] * wz + p[15] * pos[3];
    if (!isfinite(cw) || fabsf(cw) < 1e-5f) return 0;
    xy[0] = vp[0] + ((cx / cw) * 0.5f + 0.5f) * vp[2];
    xy[1] = vp[1] + ((cy / cw) * 0.5f + 0.5f) * vp[3];
    return isfinite(xy[0]) && isfinite(xy[1]);
}

static int vk_mm_scan_ubo(
    const vk_mm_draw_ctx *dc, int ortho_only, float p_out[16],
    uint64_t *ua_out, uint64_t *usz_out, uint32_t *m_base_out) {
    static const uint32_t k_poff[2] = { 32u, 0u };
    static const uint32_t k_mbase[2] = { 96u, 64u };
    for (int u = 0; u < 4; u++) {
        uint64_t ua = dc->ubo_addr[u], usz = dc->ubo_sz[u];
        if (!ua || (usz && usz < 112u)) continue;
        for (int k = 0; k < 2; k++) {
            if (usz && k_mbase[k] + 48u > usz) continue;
            float p[16] = {0};
            if (!vk_mm_read_mem(ua + k_poff[k], p, sizeof p)) continue;
            int ok = ortho_only ? vk_mm_is_ortho_mat(p) : vk_mm_is_proj_mat(p);
            if (!ok) continue;
            memcpy(p_out, p, sizeof p);
            *ua_out = ua; *usz_out = usz; *m_base_out = k_mbase[k];
            return 1;
        }
    }
    return 0;
}

static int vk_mm_find_proj(
    const vk_mm_draw_ctx *dc, int allow_proj, float p_out[16], float vp_out[4],
    uint64_t *ua_out, uint64_t *usz_out, uint32_t *m_base_out) {
    float gvw = 0.f, gvh = 0.f;
    vk_mm_viewport(dc, vp_out, &gvw, &gvh);
    if (gvw < 64.f || gvh < 64.f) return 0;
    if (vk_mm_scan_ubo(dc, 1, p_out, ua_out, usz_out, m_base_out)) return 1;
    if (allow_proj && vk_mm_scan_ubo(dc, 0, p_out, ua_out, usz_out, m_base_out)) return 1;
    return 0;
}

static int vk_mm_ubo_xy_bounds(
    const vk_mm_draw_ctx *dc, const p_draw *dw, const p_draw_idx *di, int32_t cnt, float *mn, float *mx) {
    int pos_loc = -1, pos_n = 0, pos_half = 0, idx_loc = -1, idx_half = 0, h0 = 0;
    if (!vk_mm_find_sprite_locs(dc, &pos_loc, &pos_n, &pos_half, &idx_loc, &idx_half)) return 0;
    int nc0 = vk_mm_fmt_info(dc->attr[0].vfmt, &h0);
    (void)h0;
    float p[16] = {0}, vp[4] = {0}, m[12] = {0};
    uint64_t ua = 0u, usz = 0u;
    uint32_t m_base = 0u;
    if (!vk_mm_find_proj(dc, (idx_loc >= 0 || nc0 < 2), p, vp, &ua, &usz, &m_base)) return 0;
    int32_t lim = cnt < 256 ? cnt : 256, last_idx = -1;
    for (int32_t i = 0; i < lim; i++) {
        int32_t v = cnt <= 256 ? i : (int32_t)(((int64_t)i * cnt) / lim);
        int64_t vi = vk_mm_vertex_idx(dw, di, v);
        float pos[4] = {0}, iv[4] = {0}, xy[2] = {0};
        if (vi < 0 || !vk_mm_read_fvec(dc, pos_loc, vi, pos_n, pos_half, pos)) return 0;
        int32_t idx = 0;
        if (idx_loc >= 0) {
            if (!vk_mm_read_fvec(dc, idx_loc, vi, 1, idx_half, iv)) return 0;
            float tf = truncf(iv[0]);
            if (tf < 0.f || tf >= 512.f) return 0;
            idx = (int32_t)tf;
        }
        if (idx != last_idx) {
            uint64_t mo = (uint64_t)m_base + (uint64_t)idx * 48u;
            if ((usz && mo + 48u > usz) || !vk_mm_read_mem(ua + mo, m, sizeof m)) return 0;
            for (int k = 0; k < 12; k++) if (!isfinite(m[k])) return 0;
            last_idx = idx;
        }
        if (!vk_mm_proj_point(pos, m, p, vp, xy)) return 0;
        if (i == 0) { mn[0] = mx[0] = xy[0]; mn[1] = mx[1] = xy[1]; }
        else {
            if (xy[0] < mn[0]) { mn[0] = xy[0]; } if (xy[0] > mx[0]) { mx[0] = xy[0]; }
            if (xy[1] < mn[1]) { mn[1] = xy[1]; } if (xy[1] > mx[1]) { mx[1] = xy[1]; }
        }
    }
    float w = (float)g_vk_mm.pass_w, h = (float)g_vk_mm.pass_h;
    if (!isfinite(mn[0]) || !isfinite(mx[0]) || !isfinite(mn[1]) || !isfinite(mx[1])) return 0;
    if (mn[0] < -w || mx[0] > 2.f * w || mn[1] < -h || mx[1] > 2.f * h) return 0;
    return mx[0] >= mn[0] && mx[1] >= mn[1];
}

static int vk_mm_attr_xy_bounds(
    const vk_mm_draw_ctx *dc, const p_draw *dw, const p_draw_idx *di, int32_t cnt, float *mn, float *mx) {
    if (cnt <= 0) return 0;
    if (vk_mm_ubo_xy_bounds(dc, dw, di, cnt, mn, mx)) return 1;
    if (dc->nattr < 1) return 0;
    int half = 0, nc = vk_mm_fmt_info(dc->attr[0].vfmt, &half);
    if (nc < 2) return 0;
    int32_t lim = cnt < 256 ? cnt : 256;
    for (int32_t i = 0; i < lim; i++) {
        int32_t v = cnt <= 256 ? i : (int32_t)(((int64_t)i * cnt) / lim);
        int64_t vi = vk_mm_vertex_idx(dw, di, v);
        float xy[4] = {0};
        if (vi < 0 || !vk_mm_read_fvec(dc, 0, vi, 2, half, xy)) return 0;
        if (i == 0) { mn[0] = mx[0] = xy[0]; mn[1] = mx[1] = xy[1]; }
        else {
            if (xy[0] < mn[0]) { mn[0] = xy[0]; } if (xy[0] > mx[0]) { mx[0] = xy[0]; }
            if (xy[1] < mn[1]) { mn[1] = xy[1]; } if (xy[1] > mx[1]) { mx[1] = xy[1]; }
        }
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
    if (x0 < 0) { x0 = 0; } if (y0 < 0) { y0 = 0; }
    if (x0 + (int32_t)side > (int32_t)g_vk_mm.pass_w) x0 = (int32_t)g_vk_mm.pass_w - (int32_t)side;
    if (y0 + (int32_t)side > (int32_t)g_vk_mm.pass_h) y0 = (int32_t)g_vk_mm.pass_h - (int32_t)side;
    g_vk_mm.hit_mm = (vk_mm_rect){x0, y0, side, side};
    g_vk_mm.pass_mm_hit = 1; g_vk_mm.pass_mm_square = 1; g_vk_mm.mm_open = 1;
    g_vk_mm.learn_mm_w = g_vk_mm.pass_w; g_vk_mm.learn_mm_h = g_vk_mm.pass_h;
    g_vk_mm.learn_mm_cx = (int32_t)lrintf(cx); g_vk_mm.learn_mm_cy = (int32_t)lrintf(cy);
    g_vk_mm.learn_mm_half = (int32_t)(side / 2u);
}

static void vk_mm_expand_obj_y(float h, int32_t *y0, int32_t *y1) {
    if (!g_vk_mm.obj_bounds_init) return;
    int32_t uy0 = (int32_t)floorf(g_vk_mm.obj_umin_y - h * 0.014f);
    int32_t uy1 = (int32_t)ceilf(g_vk_mm.obj_umax_y + h * 0.014f);
    if (uy0 < 0) uy0 = 0;
    if (uy1 > (int32_t)g_vk_mm.pass_h) uy1 = (int32_t)g_vk_mm.pass_h;
    if (uy0 < *y0) *y0 = uy0;
    if (uy1 > *y1) *y1 = uy1;
    int32_t max_h = (int32_t)(VK_OBJ_MAX_H & ~1u);
    if (*y1 - *y0 <= max_h) return;
    *y0 = (int32_t)floorf(g_vk_mm.obj_umin_y);
    if (*y0 < 0) *y0 = 0;
    *y1 = *y0 + max_h;
    if (*y1 > (int32_t)g_vk_mm.pass_h) { *y1 = (int32_t)g_vk_mm.pass_h; *y0 = *y1 > max_h ? *y1 - max_h : 0; }
}

static void vk_mm_rebuild_obj_rect(void) {
    float h = (float)g_vk_mm.pass_h;
    float cy = g_vk_mm.pass_obj_text ? g_vk_mm.cand_text_cy
        : (g_vk_mm.pass_obj_pill ? g_vk_mm.obj_pill_cy : g_vk_mm.obj_icon_cy);
    int32_t oh = ((int32_t)lrintf(h * 0.19f)) & ~1;
    if (oh > (int32_t)VK_OBJ_MAX_H) { oh = (int32_t)(VK_OBJ_MAX_H & ~1u); }
    if (oh > (int32_t)g_vk_mm.pass_h) { oh = (int32_t)(g_vk_mm.pass_h & ~1u); }
    int32_t y0 = (int32_t)lrintf(cy) - oh / 2, y1 = y0 + oh;
    if (y0 < 0) { y0 = 0; y1 = oh; }
    if (y1 > (int32_t)g_vk_mm.pass_h) {
        y1 = (int32_t)g_vk_mm.pass_h;
        y0 = y1 > oh ? y1 - oh : 0;
    }
    vk_mm_expand_obj_y(h, &y0, &y1);
    float xmn = 1e9f, xmx = -1e9f;
    if (g_vk_mm.obj_bounds_init) {
        xmn = g_vk_mm.obj_umin_x - h * 0.018f;
        xmx = g_vk_mm.obj_umax_x + h * 0.018f;
    }
    int has_ic = g_vk_mm.pass_obj_icon;
    float icx = g_vk_mm.obj_icon_cx;
    if (!has_ic && g_vk_mm.obj_ttl > 0u && fabsf(cy - g_vk_mm.learn_obj_cy) <= h * 0.055f) {
        has_ic = 1; icx = g_vk_mm.learn_obj_icon_x;
    }
    if (has_ic) {
        float l = icx - h * 0.095f, r = icx + h * 0.095f;
        if (l < xmn) { xmn = l; } if (r > xmx) { xmx = r; }
    } else if (g_vk_mm.obj_bounds_init && !g_vk_mm.pass_obj_pill) {
        xmn = g_vk_mm.obj_umin_x - h * 0.065f;
    }
    if (xmn > xmx) return;
    int32_t x0 = (int32_t)floorf(xmn), x1 = (int32_t)ceilf(xmx);
    if (x0 < 0) { x0 = 0; } if (x1 > (int32_t)g_vk_mm.pass_w) { x1 = (int32_t)g_vk_mm.pass_w; }
    if (x1 - x0 > (int32_t)VK_OBJ_MAX_W) { x1 = x0 + (int32_t)VK_OBJ_MAX_W; }
    int32_t ow = (x1 - x0) & ~3;
    if (ow < 64 || y1 <= y0) return;
    vk_mm_rect nr = {x0, y0, (uint32_t)ow, (uint32_t)(y1 - y0)};
    vk_mm_rect pr = g_vk_mm.prev_hit_obj;
    if (g_vk_mm.obj_ttl > 0u && pr.w >= 64u && abs(x0 - pr.x) <= 6 && abs(y0 - pr.y) <= 6
            && abs(ow - (int32_t)pr.w) <= 8 && abs((y1 - y0) - (int32_t)pr.h) <= 6) {
        nr = pr;
    }
    g_vk_mm.hit_obj = nr;
}

static void vk_mm_add_bar_bounds(const float *mn, const float *mx) {
    if (!g_vk_mm.obj_bounds_init) {
        g_vk_mm.obj_umin_x = mn[0]; g_vk_mm.obj_umin_y = mn[1];
        g_vk_mm.obj_umax_x = mx[0]; g_vk_mm.obj_umax_y = mx[1];
        g_vk_mm.obj_bounds_init = 1;
    } else {
        if (mn[0] < g_vk_mm.obj_umin_x) { g_vk_mm.obj_umin_x = mn[0]; }
        if (mn[1] < g_vk_mm.obj_umin_y) { g_vk_mm.obj_umin_y = mn[1]; }
        if (mx[0] > g_vk_mm.obj_umax_x) { g_vk_mm.obj_umax_x = mx[0]; }
        if (mx[1] > g_vk_mm.obj_umax_y) { g_vk_mm.obj_umax_y = mx[1]; }
    }
    vk_mm_rebuild_obj_rect();
}

static int vk_mm_is_mm_draw(const float *mn, const float *mx, uint32_t st, int32_t cnt) {
    float w = (float)g_vk_mm.pass_w, h = (float)g_vk_mm.pass_h;
    if (g_vk_mm.pass_mm_hit) {
        if (st != 0u && mn[0] >= w * 0.45f) return 1;
        vk_mm_rect r = g_vk_mm.hit_mm;
        float rx1 = (float)(r.x + (int32_t)r.w), ry1 = (float)(r.y + (int32_t)r.h);
        if (st == 0u && cnt <= 6 && mn[0] <= (float)r.x && mx[0] >= rx1 && mn[1] <= (float)r.y && mx[1] >= ry1
                && fabsf((mn[0] + mx[0]) * 0.5f - ((float)r.x + rx1) * 0.5f) <= 4.f
                && fabsf((mn[1] + mx[1]) * 0.5f - ((float)r.y + ry1) * 0.5f) <= 4.f) return 1;
        int inside = mn[0] >= (float)(r.x - 28) && mx[0] <= (float)(r.x + (int32_t)r.w + 28)
            && mn[1] >= (float)(r.y - 28) && mx[1] <= (float)(r.y + (int32_t)r.h + 28);
        if (!inside) g_vk_mm.mm_open = 0;
        return inside && g_vk_mm.mm_open;
    }
    if (g_vk_mm.mm_ttl > 0u && !g_vk_mm.pass_dialog && st == 0u && cnt >= 4 && cnt <= 24) {
        float bw = mx[0] - mn[0], bh = mx[1] - mn[1];
        float cx = (mn[0] + mx[0]) * 0.5f, cy = (mn[1] + mx[1]) * 0.5f;
        if (bw >= h * 0.14f && bw <= h * 0.46f && fabsf(bw - bh) <= fmaxf(12.f, bw * 0.10f)
                && fabsf(cx - (float)g_vk_mm.learn_mm_cx) <= 28.f
                && fabsf(cy - (float)g_vk_mm.learn_mm_cy) <= 28.f) {
            vk_mm_try_minimap(mn, mx);
            return g_vk_mm.pass_mm_hit;
        }
    }
    return 0;
}

static void vk_mm_note_foreign(const float *mn, const float *mx, int32_t cnt) {
    float w = (float)g_vk_mm.pass_w, h = (float)g_vk_mm.pass_h;
    float bw = mx[0] - mn[0], bh = mx[1] - mn[1];
    if (cnt < 4 || g_vk_mm.nfp >= 8) return;
    int left_wide = mn[0] <= w * 0.26f && mx[0] >= w * 0.30f && bw >= w * 0.20f && bw <= w * 0.90f
        && bh >= h * 0.04f && bh <= h * 0.50f;
    int right_card = mn[0] >= w * 0.40f && mx[0] <= w + 8.f && mn[1] >= -8.f && mx[1] <= h + 8.f
        && bw >= w * 0.25f && bh >= h * 0.095f && bh <= h * 0.75f;
    if (!left_wide && !right_card) return;
    float *r = g_vk_mm.fp[g_vk_mm.nfp++];
    r[0] = mn[0]; r[1] = mn[1]; r[2] = mx[0]; r[3] = mx[1];
}

static int vk_mm_on_foreign(const float *mn, const float *mx) {
    float pad = (float)g_vk_mm.pass_h * 0.05f;
    float cx = (mn[0] + mx[0]) * 0.5f, cy = (mn[1] + mx[1]) * 0.5f;
    for (int i = 0; i < g_vk_mm.nfp; i++) {
        const float *r = g_vk_mm.fp[i];
        if (cx >= r[0] && cx <= r[2] && cy >= r[1] - pad && cy <= r[3] + pad) return 1;
    }
    return 0;
}

static int vk_mm_in_mm_box(float cx, float cy) {
    if (g_vk_mm.pass_mm_hit) {
        vk_mm_rect r = g_vk_mm.hit_mm;
        return cx >= (float)(r.x - 28) && cx <= (float)(r.x + (int32_t)r.w + 28)
            && cy >= (float)(r.y - 28) && cy <= (float)(r.y + (int32_t)r.h + 28);
    }
    if (g_vk_mm.mm_ttl > 0u && g_vk_mm.learn_mm_half > 0) {
        float pd = (float)g_vk_mm.learn_mm_half + 28.f;
        return fabsf(cx - (float)g_vk_mm.learn_mm_cx) <= pd && fabsf(cy - (float)g_vk_mm.learn_mm_cy) <= pd;
    }
    return 0;
}

static int vk_mm_near_wm_centre(const float list[4][4], int n, float cx, float cy, float tol) {
    for (int i = 0; i < n; i++) {
        if (fabsf(cx - (list[i][0] + list[i][2]) * 0.5f) <= tol && fabsf(cy - (list[i][1] + list[i][3]) * 0.5f) <= tol) return 1;
    }
    return 0;
}

static int vk_mm_is_world_marker(const float *mn, const float *mx, int32_t cnt) {
    float w = (float)g_vk_mm.pass_w, h = (float)g_vk_mm.pass_h, s = h / 1080.f;
    float bw = mx[0] - mn[0], bh = mx[1] - mn[1];
    float cx = (mn[0] + mx[0]) * 0.5f, cy = (mn[1] + mx[1]) * 0.5f;
    if (cnt > 12 || vk_mm_in_mm_box(cx, cy)) return 0;
    if ((g_vk_mm.pass_obj_pill && g_vk_mm.pass_obj_icon
                && fabsf(cx - g_vk_mm.obj_icon_cx) <= h * 0.06f && fabsf(cy - g_vk_mm.obj_icon_cy) <= h * 0.06f)
            || (g_vk_mm.obj_ttl > 0u && fabsf(cx - g_vk_mm.learn_obj_icon_x) <= h * 0.08f
                && fabsf(cy - g_vk_mm.learn_obj_cy) <= h * 0.06f)) return 0;
    if (g_vk_mm.obj_ttl == 0u && !g_vk_mm.pass_obj_pill && mn[0] >= w * 0.48f) return 0;
    int square = fabsf(bw - bh) <= fmaxf(8.f, bw * 0.20f);
    int ring = g_vk_mm.ring_tex && g_vk_mm.cur_tex == g_vk_mm.ring_tex && square && bw <= 170.f * s;
    if (cnt == 12 && ring) {
        if (g_vk_mm.nwm_cur < 4) {
            float *r = g_vk_mm.wm_cur[g_vk_mm.nwm_cur++];
            r[0] = mn[0]; r[1] = mn[1]; r[2] = mx[0]; r[3] = mx[1];
        }
        return 1;
    }
    if (cnt <= 6 && fabsf(bw - bh) <= fmaxf(6.f, bw * 0.12f) && bw >= 50.f * s && bw <= 200.f * s
            && (vk_mm_near_wm_centre(g_vk_mm.wm_cur, g_vk_mm.nwm_cur, cx, cy, 28.f * s)
                || vk_mm_near_wm_centre(g_vk_mm.wm_prev, g_vk_mm.nwm_prev, cx, cy, 28.f * s))) return 1;
    return 0;
}

static int vk_mm_is_lhud_draw(const float *mn, const float *mx, int32_t cnt) {
    if (cnt < 4 || g_vk_mm.pass_dialog) return 0;
    float w = (float)g_vk_mm.pass_w, h = (float)g_vk_mm.pass_h;
    float bw = mx[0] - mn[0], bh = mx[1] - mn[1];
    if (mn[1] >= -8.f && mx[1] <= h * 0.12f && cnt <= 12 && mn[0] >= -w * 0.03f && mn[0] <= w * 0.16f
            && mx[0] <= w * 0.22f && bw >= h * 0.03f && bw <= h * 0.075f
            && bh >= h * 0.03f && bh <= h * 0.075f && fabsf(bw - bh) <= fmaxf(6.f, bw * 0.25f)) {
        g_vk_mm.pass_lhud_top = 1;
    }
    if (!(g_vk_mm.pass_lhud_top || g_vk_mm.prev_lhud_top)) return 0;
    if (mn[0] < -w * 0.05f || mn[0] > w * 0.22f || mx[0] > w * 0.28f || mn[1] < -16.f || mx[1] > h + 16.f) return 0;
    if (bw < 6.f || bw > w * 0.32f || bh < 6.f || bh > h * 0.42f) return 0;
    if (mn[0] > w * 0.18f && bh > bw * 6.f && bh > h * 0.20f) return 0;
    if (mn[1] >= h * 0.20f && mx[1] <= h * 0.845f && cnt >= 30 && mn[0] > w * 0.08f) return 0;
    if (vk_mm_on_foreign(mn, mx)) return 0;
    vk_mm_rect lb = vk_mm_lhud_box(g_vk_mm.pass_w, g_vk_mm.pass_h);
    if (!g_vk_mm.pass_lhud_any || g_vk_mm.hit_lhud.w < lb.w) g_vk_mm.hit_lhud = lb;
    g_vk_mm.pass_lhud_any = 1;
    return 1;
}

static int vk_mm_obj_pill(const float *mn, const float *mx, int32_t cnt) {
    float w = (float)g_vk_mm.pass_w, h = (float)g_vk_mm.pass_h;
    float bw = mx[0] - mn[0], bh = mx[1] - mn[1], cy = (mn[1] + mx[1]) * 0.5f;
    if (cnt > 24 || mn[0] < w * 0.48f || mn[0] > w * 0.88f || mx[0] < w * 0.90f) return 0;
    if (bh < h * 0.018f || bh > h * 0.085f || bw < bh * 2.0f || bw < w * 0.16f) return 0;
    int known = (g_vk_mm.pass_obj_icon && fabsf(cy - g_vk_mm.obj_icon_cy) <= h * 0.06f)
        || (g_vk_mm.pass_obj_text && fabsf(cy - g_vk_mm.cand_text_cy) <= h * 0.06f && mn[0] <= g_vk_mm.cand_text_mn_x)
        || (g_vk_mm.obj_ttl > 0u && fabsf(cy - g_vk_mm.learn_obj_cy) <= h * 0.06f && mn[0] >= g_vk_mm.learn_obj_icon_x - 64.f);
    if (!known && !(mx[0] >= w * 0.88f && bw >= w * 0.16f)) return 0;
    g_vk_mm.pass_obj_pill = 1; g_vk_mm.obj_pill_mn_x = mn[0]; g_vk_mm.obj_pill_cy = cy;
    g_vk_mm.pass_obj_hit = 1;
    vk_mm_add_bar_bounds(mn, mx);
    return 1;
}

static int vk_mm_obj_text(const float *mn, const float *mx, int32_t cnt) {
    if (!g_vk_mm.pass_obj_pill) return 0;
    float w = (float)g_vk_mm.pass_w, h = (float)g_vk_mm.pass_h;
    float bw = mx[0] - mn[0], bh = mx[1] - mn[1], cy = (mn[1] + mx[1]) * 0.5f;
    if (cnt < 12 || cnt > 1200 || mn[0] < w * 0.52f || mx[0] > w + 24.f) return 0;
    if (bw < h * 0.045f || bw > w * 0.44f || bh < h * 0.014f || bh > h * 0.14f || bw < bh * 1.3f) return 0;
    float icx = g_vk_mm.obj_icon_cx;
    int near_pill = g_vk_mm.pass_obj_pill && fabsf(cy - g_vk_mm.obj_pill_cy) <= h * 0.055f
        && mn[0] >= g_vk_mm.obj_pill_mn_x - 24.f;
    int near_icon = g_vk_mm.pass_obj_icon && fabsf(cy - g_vk_mm.obj_icon_cy) <= h * 0.055f
        && mn[0] >= icx - 12.f && mn[0] <= icx + h * 0.10f;
    if (!g_vk_mm.cand_obj_text || near_pill || near_icon) {
        g_vk_mm.cand_obj_text = 1; g_vk_mm.cand_text_mn_x = mn[0]; g_vk_mm.cand_text_cy = cy;
    }
    if (!near_pill && !near_icon) return 0;
    g_vk_mm.pass_obj_text = 1; g_vk_mm.pass_obj_hit = 1;
    vk_mm_add_bar_bounds(mn, mx);
    return 1;
}

static int vk_mm_obj_icon(const float *mn, const float *mx, int32_t cnt) {
    if (!g_vk_mm.pass_obj_pill) return 0;
    float w = (float)g_vk_mm.pass_w, h = (float)g_vk_mm.pass_h;
    float bw = mx[0] - mn[0], bh = mx[1] - mn[1], cy = (mn[1] + mx[1]) * 0.5f;
    float icx = (mn[0] + mx[0]) * 0.5f;
    if (cnt > 12 || mx[0] > w * 0.92f || bw < h * 0.004f || bw > h * 0.18f || bh < h * 0.004f || bh > h * 0.18f) return 0;
    if (fabsf(bw - bh) > fmaxf(8.f, bw * 0.35f)) return 0;
    int near_text = (g_vk_mm.pass_obj_text || g_vk_mm.cand_obj_text)
        && fabsf(cy - g_vk_mm.cand_text_cy) <= h * 0.06f
        && icx <= g_vk_mm.cand_text_mn_x + 36.f && icx >= g_vk_mm.cand_text_mn_x - h * 0.12f;
    int near_pill = g_vk_mm.pass_obj_pill && fabsf(cy - g_vk_mm.obj_pill_cy) <= h * 0.06f
        && icx >= g_vk_mm.obj_pill_mn_x - h * 0.06f && icx <= g_vk_mm.obj_pill_mn_x + h * 0.14f;
    int near_learn = g_vk_mm.obj_ttl > 0u && fabsf(cy - g_vk_mm.learn_obj_cy) <= h * 0.06f
        && fabsf(icx - g_vk_mm.learn_obj_icon_x) <= h * 0.08f;
    int near_icon = g_vk_mm.pass_obj_icon && fabsf(cy - g_vk_mm.obj_icon_cy) <= h * 0.06f
        && fabsf(icx - g_vk_mm.obj_icon_cx) <= h * 0.05f;
    if (g_vk_mm.obj_ttl > 0u && !near_learn && !near_text && !near_pill) return 0;
    if (bw >= h * 0.016f || near_text || near_pill) {
        if (!g_vk_mm.pass_obj_icon || bw < g_vk_mm.obj_icon_core_w) {
            g_vk_mm.obj_icon_core_w = bw; g_vk_mm.obj_icon_cx = icx; g_vk_mm.obj_icon_cy = cy;
        }
        g_vk_mm.pass_obj_icon = 1;
    }
    if (!near_text && !near_pill && !(near_icon && (g_vk_mm.pass_obj_pill || g_vk_mm.cand_obj_text))) return 0;
    if (cnt == 12 && g_vk_mm.pass_obj_pill && g_vk_mm.cur_tex) g_vk_mm.ring_tex = g_vk_mm.cur_tex;
    g_vk_mm.pass_obj_hit = 1;
    vk_mm_rebuild_obj_rect();
    return 1;
}

static int vk_mm_is_obj_draw(const float *mn, const float *mx, int32_t cnt) {
    if (vk_mm_on_foreign(mn, mx)) return 0;
    float w = (float)g_vk_mm.pass_w, h = (float)g_vk_mm.pass_h;
    float cy = (mn[1] + mx[1]) * 0.5f;
    if (mn[0] < w * 0.45f || mx[0] > w + 48.f || cy < h * 0.14f || cy > h * 0.68f) return 0;
    if (vk_mm_obj_pill(mn, mx, cnt)) return 1;
    if (vk_mm_obj_text(mn, mx, cnt)) return 1;
    return vk_mm_obj_icon(mn, mx, cnt);
}

static void vk_minimap_note_draw_tex(uint64_t serial) {
    g_vk_mm.cur_tex = serial;
}

static void vk_minimap_trace_blend(uint8_t srgb, uint8_t drgb, uint8_t op, uint8_t sa, uint8_t da, uint8_t opa) {
    g_vk_mm_tr.b_srgb = srgb; g_vk_mm_tr.b_drgb = drgb; g_vk_mm_tr.b_op = op;
    g_vk_mm_tr.b_sa = sa; g_vk_mm_tr.b_da = da; g_vk_mm_tr.b_opa = opa;
}

static void vk_minimap_trace_draw(const char *vsk, const char *fsk, vk_tex *t0, int ntex) {
    (void)vsk; (void)fsk; (void)t0; (void)ntex;
    if (!g_vk_mm_tr.on || !g_vk_mm_tr.valid) return;
    g_vk_mm_tr.valid = 0;
    LOGV("mm trace f%llu d%u: cls %c vs %.16s fs %.16s cnt %d st %u ref %u bl %u m %x b %d %.0f,%.0f %.0f,%.0f tex %ux%u l%u s%llu n%d bf %u,%u,%u/%u,%u,%u fmt %u wid %u",
         (unsigned long long)g_vk_mm.frame_counter, g_vk_mm_tr.seq++, g_vk_mm_tr.cls, vsk ? vsk : "-", fsk ? fsk : "-",
         g_vk_mm_tr.cnt, g_vk_mm_tr.stt, g_vk_mm_tr.sref, g_vk_mm_tr.blend, g_vk_mm_tr.mask, g_vk_mm_tr.hb,
         g_vk_mm_tr.mn[0], g_vk_mm_tr.mn[1], g_vk_mm_tr.mx[0], g_vk_mm_tr.mx[1],
         t0 ? vk_tex_width(t0, 0u) : 0u, t0 ? vk_tex_height(t0, 0u) : 0u, t0 ? vk_tex_levels(t0) : 0u,
         (unsigned long long)(t0 ? vk_tex_serial(t0) : 0u), ntex,
         g_vk_mm_tr.b_srgb, g_vk_mm_tr.b_drgb, g_vk_mm_tr.b_op, g_vk_mm_tr.b_sa, g_vk_mm_tr.b_da, g_vk_mm_tr.b_opa, (unsigned)g_vk_mm.hud_fmt, (unsigned)g_vk_mm_tr.wid);
}

static void vk_mm_note_pass_overlays(float w, float h, const float *mn, const float *mx, uint32_t st, uint32_t bl, int32_t cnt) {
    float bw = mx[0] - mn[0], bh = mx[1] - mn[1];
    float cx = (mn[0] + mx[0]) * 0.5f, cy = (mn[1] + mx[1]) * 0.5f;
    int fs_dim = st == 0u && bl && cnt <= 6 && fabsf(mn[0]) <= 2.f && fabsf(mn[1]) <= 2.f
        && fabsf(mx[0] - w) <= 2.f && fabsf(mx[1] - h) <= 2.f;
    int menu_band = st == 0u && bl && cnt <= 12 && mn[0] <= 2.f && mx[0] >= w - 2.f
        && bh >= h * 0.04f && bh <= h * 0.12f && (mn[1] <= 2.f || mx[1] >= h - 2.f);
    if (fs_dim || menu_band) {
        g_vk_mm.fr_modal = 1; g_vk_mm.pass_modal = 1;
        g_vk_mm.mm_ttl = 0u; g_vk_mm.obj_ttl = 0u; g_vk_mm.lhud_ttl = 0u;
        g_vk_mm.nwm_cur = 0; g_vk_mm.nwm_prev = 0; g_vk_mm.pass_lhud_top = 0; g_vk_mm.prev_lhud_top = 0;
    }
    if (cnt >= 4 && bw >= w * 0.28f && bw <= w * 0.88f && bh >= h * 0.12f && bh <= h * 0.88f
            && fabsf(cx - w * 0.5f) < w * 0.16f && mn[1] < h * 0.66f && mx[1] > h * 0.20f)
        g_vk_mm.pass_modal = 1;
    if (cnt >= 4 && bw >= w * 0.34f && bw <= w * 0.88f && bh <= h * 0.45f && cy > h * 0.62f && fabsf(cx - w * 0.5f) < w * 0.18f)
        g_vk_mm.pass_dialog = 1;
}

static void vk_mm_grow_tagged_mm(const float *mn, const float *mx) {
    float x0 = mn[0] > 0.f ? mn[0] : 0.f, y0 = mn[1] > 0.f ? mn[1] : 0.f;
    float x1 = mx[0] < (float)g_vk_mm.pass_w ? mx[0] : (float)g_vk_mm.pass_w;
    float y1 = mx[1] < (float)g_vk_mm.pass_h ? mx[1] : (float)g_vk_mm.pass_h;
    if (x1 <= x0 || y1 <= y0) return;
    float nx0 = g_vk_mm.mm_tbox_init && g_vk_mm.mm_tbox[0] < x0 ? g_vk_mm.mm_tbox[0] : x0;
    float ny0 = g_vk_mm.mm_tbox_init && g_vk_mm.mm_tbox[1] < y0 ? g_vk_mm.mm_tbox[1] : y0;
    float nx1 = g_vk_mm.mm_tbox_init && g_vk_mm.mm_tbox[2] > x1 ? g_vk_mm.mm_tbox[2] : x1;
    float ny1 = g_vk_mm.mm_tbox_init && g_vk_mm.mm_tbox[3] > y1 ? g_vk_mm.mm_tbox[3] : y1;
    if (nx1 - nx0 > (float)VK_MM_MAX_SIDE || ny1 - ny0 > (float)VK_MM_MAX_SIDE) return;
    g_vk_mm.mm_tbox[0] = nx0; g_vk_mm.mm_tbox[1] = ny0;
    g_vk_mm.mm_tbox[2] = nx1; g_vk_mm.mm_tbox[3] = ny1;
    g_vk_mm.mm_tbox_init = 1;
    int32_t rx0 = (int32_t)floorf(nx0), ry0 = (int32_t)floorf(ny0);
    int32_t rx1 = (int32_t)ceilf(nx1), ry1 = (int32_t)ceilf(ny1);
    int32_t rw = rx1 - rx0 < (int32_t)VK_MM_MIN_SIDE ? (int32_t)VK_MM_MIN_SIDE : rx1 - rx0;
    int32_t rh = ry1 - ry0 < (int32_t)VK_MM_MIN_SIDE ? (int32_t)VK_MM_MIN_SIDE : ry1 - ry0;
    if (rw > (int32_t)g_vk_mm.pass_w) rw = (int32_t)g_vk_mm.pass_w;
    if (rh > (int32_t)g_vk_mm.pass_h) rh = (int32_t)g_vk_mm.pass_h;
    if (rx0 + rw > (int32_t)g_vk_mm.pass_w) rx0 = (int32_t)g_vk_mm.pass_w - rw;
    if (ry0 + rh > (int32_t)g_vk_mm.pass_h) ry0 = (int32_t)g_vk_mm.pass_h - rh;
    g_vk_mm.hit_mm = (vk_mm_rect){rx0, ry0, (uint32_t)rw, (uint32_t)rh};
    g_vk_mm.pass_mm_hit = 1;
}

static int vk_mm_tagged_mm(
    VkCommandBuffer cb, int32_t cnt, uint32_t stt, uint32_t c0_mask,
    int hb, const float *mn, const float *mx) {
    float w = (float)g_vk_mm.pass_w, h = (float)g_vk_mm.pass_h;
    int can_box = hb && !vk_mm_is_center_reset(mn, mx, w)
        && (mx[0] - mn[0]) <= w * 0.30f && (mx[1] - mn[1]) <= h * 0.50f && mx[0] >= w * 0.45f;
    if (stt != 0u && (c0_mask & 0xfu) == 0u) {
        g_vk_mm_tr.cls = 'S';
        if (can_box) vk_mm_try_minimap(mn, mx);
        return 0;
    }
    if (!hb) {
        g_vk_mm_tr.cls = '-';
        return vk_mm_switch_target(cb, 0);
    }
    if (vk_mm_is_center_reset(mn, mx, w) || mx[0] < w * 0.45f) return -1;
    if (can_box) {
        vk_mm_try_minimap(mn, mx);
        float bw = mx[0] - mn[0], bh = mx[1] - mn[1];
        if (stt == 0u && cnt == 12 && g_vk_mm.cur_tex && !g_vk_mm.ring_tex
                && fabsf(bw - bh) <= fmaxf(8.f, bw * 0.20f) && bw <= 170.f * (h / 1080.f)) {
            g_vk_mm.ring_tex = g_vk_mm.cur_tex;
        }
    }
    if (!g_vk_mm.pass_mm_hit && g_vk_mm.hit_mm.w >= VK_MM_MIN_SIDE
            && g_vk_mm.learn_mm_w == g_vk_mm.pass_w && g_vk_mm.learn_mm_h == g_vk_mm.pass_h) {
        g_vk_mm.pass_mm_hit = 1; g_vk_mm.pass_mm_square = 1;
    }
    int in_corner = mx[0] >= w * 0.65f && mx[1] >= h * 0.55f;
    if (!g_vk_mm.pass_mm_square && in_corner) vk_mm_grow_tagged_mm(mn, mx);
    const vk_mm_rect *mr = &g_vk_mm.hit_mm;
    int in_mm = g_vk_mm.pass_mm_hit && mr->w >= VK_MM_MIN_SIDE
        && mx[0] >= (float)mr->x - 16.f && mn[0] <= (float)(mr->x + (int32_t)mr->w) + 16.f
        && mx[1] >= (float)mr->y - 16.f && mn[1] <= (float)(mr->y + (int32_t)mr->h) + 16.f;
    if (!in_mm) return -1;
    g_vk_mm_tr.cls = 'm';
    g_vk_mm.mm_open = 1;
    g_vk_mm.pass_mm_tagged = 1;
    return vk_mm_switch_target(cb, 1);
}

static int vk_mm_tagged_obj(
    VkCommandBuffer cb, int32_t cnt, int hb, const float *mn, const float *mx) {
    float w = (float)g_vk_mm.pass_w, h = (float)g_vk_mm.pass_h;
    if (!hb) {
        g_vk_mm_tr.cls = '-';
        return vk_mm_switch_target(cb, 0);
    }
    if (vk_mm_is_center_reset(mn, mx, w) || mx[0] < w * 0.45f || mn[0] < w * 0.30f) return -1;
    float bw = mx[0] - mn[0], bh = mx[1] - mn[1], cy = (mn[1] + mx[1]) * 0.5f;
    if (cnt == 12 && fabsf(bw - bh) <= fmaxf(8.f, bw * 0.20f) && bw <= h * 0.26f) {
        if (bw <= 170.f * (h / 1080.f) && g_vk_mm.cur_tex) g_vk_mm.ring_tex = g_vk_mm.cur_tex;
        if (!g_vk_mm.pass_obj_hit && g_vk_mm.hit_obj.w < 64u) return -1;
        g_vk_mm_tr.cls = 'o'; g_vk_mm.pass_obj_hit = 1; g_vk_mm.pass_obj_tagged = 1;
        return vk_mm_switch_target(cb, 1);
    }
    float ux0 = g_vk_mm.obj_bounds_init && g_vk_mm.obj_umin_x < mn[0] ? g_vk_mm.obj_umin_x : mn[0];
    float ux1 = g_vk_mm.obj_bounds_init && g_vk_mm.obj_umax_x > mx[0] ? g_vk_mm.obj_umax_x : mx[0];
    float uy0 = g_vk_mm.obj_bounds_init && g_vk_mm.obj_umin_y < mn[1] ? g_vk_mm.obj_umin_y : mn[1];
    float uy1 = g_vk_mm.obj_bounds_init && g_vk_mm.obj_umax_y > mx[1] ? g_vk_mm.obj_umax_y : mx[1];
    if ((ux1 - ux0) > (float)(VK_OBJ_MAX_W - 32u) || (uy1 - uy0) > (float)(VK_OBJ_MAX_H - 24u)) return -1;
    g_vk_mm_tr.cls = 'o';
    g_vk_mm.pass_obj_hit = 1;
    g_vk_mm.pass_obj_tagged = 1;
    if (!vk_mm_obj_pill(mn, mx, cnt) && !vk_mm_obj_text(mn, mx, cnt)
            && !(cnt <= 6 && vk_mm_obj_icon(mn, mx, cnt))) {
        if (!g_vk_mm.pass_obj_pill && bw >= w * 0.12f && cnt <= 36) {
            g_vk_mm.pass_obj_pill = 1; g_vk_mm.obj_pill_mn_x = mn[0]; g_vk_mm.obj_pill_cy = cy;
        } else if (!g_vk_mm.obj_bounds_init && !g_vk_mm.pass_obj_pill) {
            g_vk_mm.obj_pill_mn_x = mn[0]; g_vk_mm.obj_pill_cy = cy;
        }
        vk_mm_add_bar_bounds(mn, mx);
    }
    return vk_mm_switch_target(cb, 1);
}

static int vk_mm_tagged_lhud(VkCommandBuffer cb, int hb, const float *mn, const float *mx) {
    float w = (float)g_vk_mm.pass_w;
    vk_mm_rect base = vk_mm_lhud_box(g_vk_mm.pass_w, g_vk_mm.pass_h);
    uint32_t w_cap = g_vk_mm.pass_w & ~3u;
    uint32_t max_w = VK_LHUD_MAX_W < w_cap ? VK_LHUD_MAX_W : w_cap;
    if (!hb) {
        g_vk_mm_tr.cls = '-';
        return vk_mm_switch_target(cb, 0);
    }
    if (mn[0] < -w * 0.30f || mn[0] >= (float)max_w || mx[0] > (float)max_w
            || mx[1] < 0.f || mn[1] >= (float)base.h) return -1;
    if (g_vk_mm.cur_wid_bg) {
        g_vk_mm_tr.cls = 'l';
        g_vk_mm.skip_cur_draw = 1;
        return 0;
    }
    g_vk_mm_tr.cls = 'l';
    if (!g_vk_mm.pass_lhud_any || g_vk_mm.hit_lhud.w < base.w) g_vk_mm.hit_lhud = base;
    g_vk_mm.pass_lhud_any = 1;
    g_vk_mm.pass_lhud_top = 1;
    g_vk_mm.pass_lhud_tagged = 1;
    int32_t ce = mx[0] > 0.f ? (int32_t)ceilf(mx[0]) : 0;
    uint32_t need = ((uint32_t)ce + 7u) & ~3u;
    if (need > max_w) need = max_w;
    if (need > g_vk_mm.hit_lhud.w) g_vk_mm.hit_lhud.w = need;
    g_vk_mm.hit_lhud.h = base.h;
    return vk_mm_switch_target(cb, 1);
}

static int vk_mm_select_tagged(
    VkCommandBuffer cb, uint8_t wid, uint8_t dest, int32_t cnt, uint32_t stt,
    uint32_t c0_mask, uint32_t blend_en, int hb, const float *mn, const float *mx) {
    if (dest == KTGL_DEST_TOP) {
        if (hb && (c0_mask & 0xfu) != 0u && wid == KTGL_WID_MENU) {
            vk_mm_note_pass_overlays((float)g_vk_mm.pass_w, (float)g_vk_mm.pass_h, mn, mx, stt, blend_en, cnt);
        }
        g_vk_mm_tr.cls = g_vk_mm.pass_modal ? 'M' : '-';
        return vk_mm_switch_target(cb, 0);
    }
    if (dest == KTGL_DEST_HIDE) {
        g_vk_mm_tr.cls = 'h';
        g_vk_mm.skip_cur_draw = 1;
        return 0;
    }
    if (dest == KTGL_DEST_MM) return vk_mm_tagged_mm(cb, cnt, stt, c0_mask, hb, mn, mx);
    if (dest == KTGL_DEST_OBJ) return vk_mm_tagged_obj(cb, cnt, hb, mn, mx);
    return vk_mm_tagged_lhud(cb, hb, mn, mx);
}

static int vk_mm_select_fallback(
    VkCommandBuffer cb, int32_t cnt, uint32_t stt, uint32_t c0_mask, uint32_t blend_en,
    const float *mn, const float *mx) {
    float w = (float)g_vk_mm.pass_w, h = (float)g_vk_mm.pass_h;
    float bw = mx[0] - mn[0], bh = mx[1] - mn[1];
    if ((c0_mask & 0xfu) != 0u) vk_mm_note_pass_overlays(w, h, mn, mx, stt, blend_en, cnt);
    if (stt != 0u && (c0_mask & 0xfu) == 0u && cnt >= 4 && cnt <= 12) { g_vk_mm_tr.cls = 'S'; vk_mm_try_minimap(mn, mx); return 0; }
    if (g_vk_mm.pass_opaque_fs || g_vk_mm.pass_modal || (c0_mask & 0xfu) == 0u) { g_vk_mm_tr.cls = 'M'; return vk_mm_switch_target(cb, 0); }
    int prev_small = g_vk_mm.last_small;
    float pq[4] = { g_vk_mm.last_q[0], g_vk_mm.last_q[1], g_vk_mm.last_q[2], g_vk_mm.last_q[3] };
    g_vk_mm.last_small = stt == 0u && cnt <= 6 && bw <= 8.f && bh <= 8.f;
    g_vk_mm.last_q[0] = mn[0]; g_vk_mm.last_q[1] = mn[1]; g_vk_mm.last_q[2] = mx[0]; g_vk_mm.last_q[3] = mx[1];
    if (!g_vk_mm.pass_mm_hit && stt == 0u && blend_en && cnt <= 6 && prev_small
            && pq[0] >= mn[0] && pq[2] <= mx[0] && pq[1] >= mn[1] && pq[3] <= mx[1]) vk_mm_try_minimap(mn, mx);
    int want = vk_mm_is_mm_draw(mn, mx, stt, cnt);
    if (want && stt == 0u && cnt == 12 && g_vk_mm.cur_tex && !g_vk_mm.ring_tex
            && fabsf(bw - bh) <= fmaxf(8.f, bw * 0.20f) && bw <= 170.f * (h / 1080.f)) g_vk_mm.ring_tex = g_vk_mm.cur_tex;
    if (!want && g_vk_mm.last_small && !g_vk_mm.pass_mm_hit && g_vk_mm.mm_ttl > 0u
            && vk_mm_in_mm_box((mn[0] + mx[0]) * 0.5f, (mn[1] + mx[1]) * 0.5f)) want = 1;
    if (want) g_vk_mm_tr.cls = 'm';
    int wm = !want && blend_en && vk_mm_is_world_marker(mn, mx, cnt);
    if (!want && !wm) { want = vk_mm_is_lhud_draw(mn, mx, cnt); if (want) g_vk_mm_tr.cls = 'l'; }
    if (!want && !wm && blend_en) { want = vk_mm_is_obj_draw(mn, mx, cnt); if (want) g_vk_mm_tr.cls = 'o'; }
    if (!want) vk_mm_note_foreign(mn, mx, cnt);
    return vk_mm_switch_target(cb, want);
}

static int vk_minimap_select_target(
    VkCommandBuffer cb, const vk_mm_draw_ctx *dc, int indirect, const p_draw *dw, const p_draw_idx *di) {
    g_vk_mm.skip_cur_draw = 0;
    if (!g_vk_mm.in_hud_pass) return 0;
    uint8_t wid = g_vk_mm.cur_wid;
    int32_t cnt = dw ? dw->count : (di ? di->count : 0);
    g_vk_mm_tr.valid = 1; g_vk_mm_tr.hb = 0; g_vk_mm_tr.cls = 'x'; g_vk_mm_tr.wid = wid;
    g_vk_mm_tr.cnt = cnt; g_vk_mm_tr.stt = dc->st;
    g_vk_mm_tr.sref = 0u; g_vk_mm_tr.blend = dc->blend_en; g_vk_mm_tr.mask = dc->c0_mask;
    if (indirect || dc->dt != 0u || dc->nattr < 1) return vk_mm_switch_target(cb, 0);
    float mn[2] = {0}, mx[2] = {0};
    int hb = cnt >= 3 && cnt <= 4096 && vk_mm_attr_xy_bounds(dc, dw, di, cnt, mn, mx);
    if (hb) {
        mn[0] = dc->acx + (mn[0] - dc->acx) * dc->ax; mx[0] = dc->acx + (mx[0] - dc->acx) * dc->ax;
        mn[1] = dc->acy + (mn[1] - dc->acy) * dc->ay; mx[1] = dc->acy + (mx[1] - dc->acy) * dc->ay;
        float w = (float)g_vk_mm.pass_w, h = (float)g_vk_mm.pass_h, bw = mx[0] - mn[0], bh = mx[1] - mn[1];
        g_vk_mm_tr.hb = 1; g_vk_mm_tr.cls = '-';
        g_vk_mm_tr.mn[0] = mn[0]; g_vk_mm_tr.mn[1] = mn[1]; g_vk_mm_tr.mx[0] = mx[0]; g_vk_mm_tr.mx[1] = mx[1];
        if (dc->st == 0u && !dc->blend_en && cnt <= 6
                && ((bw >= w * 0.90f && bh >= h * 0.90f)
                    || (mn[0] <= -0.95f && mx[0] >= 0.95f && mn[1] <= -0.95f && mx[1] >= 0.95f))) {
            g_vk_mm.pass_opaque_fs = 1; g_vk_mm_tr.cls = 'F';
            return vk_mm_switch_target(cb, 0);
        }
    } else {
        g_vk_mm_tr.cls = 'b';
    }
    uint8_t dest = wid ? vk_mm_wid_dest(wid) : KTGL_DEST_FALLBACK;
    if (dest != KTGL_DEST_FALLBACK) {
        int rc = vk_mm_select_tagged(cb, wid, dest, cnt, dc->st, dc->c0_mask, dc->blend_en, hb, mn, mx);
        if (rc >= 0) return rc;
    }
    if (!hb) return vk_mm_switch_target(cb, 0);
    return vk_mm_select_fallback(cb, cnt, dc->st, dc->c0_mask, dc->blend_en, mn, mx);
}

static int vk_minimap_want_ui_tex(void) {
    return g_vk_mm.in_hud_pass && !g_vk_mm.pass_opaque_fs && !g_vk_mm.pass_modal
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

static void vk_mm_stream_stage(vk_mm_stream *st, VkCommandBuffer cb, const vk_mm_rect *r) {
    if (atomic_load_explicit(&st->pending_valid, memory_order_acquire)) return;
    VkDeviceSize need = (VkDeviceSize)r->w * r->h * 4u;
    if ((g_vk_mm.frame_counter & 1u) != 0u || !r->w || !r->h || need > st->slot_bytes) return;
    if (!g_vk_mm.hud_ready || !vk_mm_ensure_stream_ring(st)) return;
    vk_barrier_all(cb);
    vk_mm_slot *dst = &st->slot[st->write_slot];
    VkBufferImageCopy rg = {
        .bufferOffset = 0, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
        .imageOffset = {r->x, r->y, 0}, .imageExtent = {r->w, r->h, 1u}
    };
    vkCmdCopyImageToBuffer(cb, g_vk_mm.hud_img, VK_IMAGE_LAYOUT_GENERAL, dst->buf, 1u, &rg);
    VkBufferMemoryBarrier2 bb = {
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2, .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT, .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
        .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .buffer = dst->buf, .offset = 0, .size = need
    };
    VkDependencyInfo di = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .bufferMemoryBarrierCount = 1u, .pBufferMemoryBarriers = &bb};
    vkCmdPipelineBarrier2(cb, &di);
    st->wrote_in_frame = 1; st->write_w = r->w; st->write_h = r->h; st->write_fmt = g_vk_mm.hud_fmt;
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

static void vk_minimap_pass_end(VkCommandBuffer cb, vk_tex *t, uint64_t serial) {
    int was = g_vk_mm.in_hud_pass, cleared = g_vk_mm.pass_hud_cleared;
    int mh = g_vk_mm.pass_mm_hit, oh = g_vk_mm.pass_obj_hit, op = g_vk_mm.pass_opaque_fs, md = g_vk_mm.pass_modal;
    int lh = (!op && g_vk_mm.hit_lhud.w >= 64u && g_vk_mm.hit_lhud.h >= 64u
        && (g_vk_mm.pass_lhud_tagged
            || (!md && !g_vk_mm.pass_dialog && g_vk_mm.pass_lhud_any && g_vk_mm.pass_lhud_top)));
    float oicx = g_vk_mm.obj_icon_cx;
    int obj_pair = (!op && !md && g_vk_mm.cand_obj_text && g_vk_mm.pass_obj_icon
        && fabsf(g_vk_mm.cand_text_cy - g_vk_mm.obj_icon_cy) <= (float)g_vk_mm.pass_h * 0.055f
        && g_vk_mm.cand_text_mn_x >= oicx - 12.f
        && g_vk_mm.cand_text_mn_x <= oicx + (float)g_vk_mm.pass_h * 0.10f);
    g_vk_mm.in_hud_pass = 0; g_vk_mm.cur_hud = 0;
    if (!was || !t || !V.dev || vk_tex_image(t) != g_vk_mm.pass_im) return;
    if (!atomic_load_explicit(&g_vk_mm.want_active, memory_order_acquire) || op || md) return;
    VkCommandBuffer copy_cb = cb ? cb : vk_cb();
    if (!copy_cb) return;
    VkImage im = vk_tex_image(t); uint32_t fr = g_vk_mm.frame_counter;
    if (obj_pair) {
        g_vk_mm.learn_obj_icon_x = oicx; g_vk_mm.learn_obj_text_x = g_vk_mm.cand_text_mn_x;
        g_vk_mm.learn_obj_cy = g_vk_mm.obj_icon_cy; g_vk_mm.saw_obj_frame = 1;
    } else if (oh && cleared) {
        g_vk_mm.saw_obj_frame = 1;
    }
    if (!cleared) return;
    if (mh) {
        vk_mm_find_cand(g_vk_mm.hit_mm.w, g_vk_mm.hit_mm.h, g_vk_mm.hud_fmt, serial);
        vk_mm_stream_stage(&g_vk_mm.mm, copy_cb, &g_vk_mm.hit_mm);
        g_vk_mm.saw_mm_frame = 1; g_vk_mm.frame_got_mm = fr; g_vk_mm.saved_hit_img = im;
    }
    if (oh) {
        if (g_vk_mm.hit_obj.w < 64u && g_vk_mm.prev_hit_obj.w >= 64u) g_vk_mm.hit_obj = g_vk_mm.prev_hit_obj;
        g_vk_mm.prev_hit_obj = g_vk_mm.hit_obj;
        vk_mm_stream_stage(&g_vk_mm.obj, copy_cb, &g_vk_mm.hit_obj);
        g_vk_mm.frame_got_obj = fr; g_vk_mm.saved_hit_img = im;
    }
    if (lh) {
        vk_mm_stream_stage(&g_vk_mm.lhud, copy_cb, &g_vk_mm.hit_lhud);
        g_vk_mm.saw_lhud_frame = 1; g_vk_mm.frame_got_lhud = fr; g_vk_mm.saved_hit_img = im;
    }
    if ((mh || oh || lh) && atomic_load_explicit(&g_vk_mm.ui_want, memory_order_relaxed) == 1) vk_mm_stage_ui_tex(copy_cb);
}

static void vk_minimap_pass_abort(void) {
    g_vk_mm.in_hud_pass = 0; g_vk_mm.cur_hud = 0; g_vk_mm.pass_hud_cleared = 0;
    g_vk_mm.pass_opaque_fs = 0; g_vk_mm.pass_modal = 0; g_vk_mm.pass_dialog = 0;
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
    vk_mm_stream_present(&g_vk_mm.lhud, fr, fq);
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
    if (g_vk_mm.saw_lhud_frame) g_vk_mm.lhud_ttl = VK_MM_TTL; else if (g_vk_mm.lhud_ttl > 0u) g_vk_mm.lhud_ttl--;
    g_vk_mm.saw_mm_frame = 0; g_vk_mm.saw_obj_frame = 0; g_vk_mm.saw_lhud_frame = 0;
    memcpy(g_vk_mm.wm_prev, g_vk_mm.wm_cur, sizeof g_vk_mm.wm_prev);
    g_vk_mm.nwm_prev = g_vk_mm.nwm_cur; g_vk_mm.nwm_cur = 0;
    g_vk_mm.prev_lhud_top = g_vk_mm.pass_lhud_top; g_vk_mm.pass_lhud_top = 0;
}

static _Atomic int g_vk_srgb_lut_ready;
static float g_vk_srgb_to_lin[256];
static float g_vk_lin_to_srgb[4097];

static void vk_mm_ensure_srgb_lut(void) {
    if (atomic_load_explicit(&g_vk_srgb_lut_ready, memory_order_acquire) == 2) return;
    int expected = 0;
    if (atomic_compare_exchange_strong(&g_vk_srgb_lut_ready, &expected, 1)) {
        for (uint32_t u = 0u; u < 256u; u++) {
            float x = (float)u * (1.0f / 255.0f);
            g_vk_srgb_to_lin[u] = x <= 0.04045f ? x * (1.0f / 12.92f) : powf((x + 0.055f) / 1.055f, 2.4f);
        }
        for (uint32_t i = 0u; i <= 4096u; i++) {
            float l = (float)i * (1.0f / 4096.0f);
            g_vk_lin_to_srgb[i] = l <= 0.0031308f ? l * 12.92f : 1.055f * powf(l, 1.0f / 2.4f) - 0.055f;
        }
        atomic_store_explicit(&g_vk_srgb_lut_ready, 2, memory_order_release);
        return;
    }
    for (int spin = 0; spin < 1000000; spin++) {
        if (atomic_load_explicit(&g_vk_srgb_lut_ready, memory_order_acquire) == 2) return;
    }
}

static float vk_mm_srgb_to_lin(uint8_t u) {
    return g_vk_srgb_to_lin[u];
}

static float vk_mm_lin_to_srgb(float l) {
    if (l <= 0.0f) return 0.0f;
    if (l >= 1.0f) return 1.0f;
    if (l <= 0.0031308f) return l * 12.92f;
    int idx = (int)(l * 4096.0f + 0.5f);
    if (idx < 0) idx = 0;
    if (idx > 4096) idx = 4096;
    return g_vk_lin_to_srgb[idx];
}

static void vk_mm_srgb_premul_pixel(uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) {
    uint8_t av = *a;
    if (av == 0u) {
        uint8_t mx = *r > *g ? (*r > *b ? *r : *b) : (*g > *b ? *g : *b);
        *a = mx;
        return;
    }
    if (av == 255u) return;
    float lr = vk_mm_srgb_to_lin(*r), lg = vk_mm_srgb_to_lin(*g), lb = vk_mm_srgb_to_lin(*b);
    float ml = lr > lg ? (lr > lb ? lr : lb) : (lg > lb ? lg : lb);
    float af = (float)av * (1.0f / 255.0f);
    if (ml > af * 1.05f + 0.006f) {
        af = ml < 1.0f ? ml : 1.0f;
        av = (uint8_t)lrintf(af * 255.0f);
        *a = av;
    }
    float ch[3] = {lr, lg, lb};
    uint8_t *dst[3] = {r, g, b};
    for (int i = 0; i < 3; i++) {
        float s = ch[i] / af;
        if (s > 1.0f) s = 1.0f;
        uint32_t v = (uint32_t)lrintf(vk_mm_lin_to_srgb(s) * (float)av);
        *dst[i] = (uint8_t)(v > av ? av : v);
    }
}

static void vk_mm_copy_rgba(uint8_t *dst, const uint8_t *src, uint32_t w, uint32_t h, VkFormat fmt, int alpha_mode) {
    int bgra = (fmt == VK_FORMAT_B8G8R8A8_UNORM || fmt == VK_FORMAT_B8G8R8A8_SRGB);
    int srgb = (fmt == VK_FORMAT_R8G8B8A8_SRGB || fmt == VK_FORMAT_B8G8R8A8_SRGB);
    if (alpha_mode == 2 && srgb) vk_mm_ensure_srgb_lut();
    size_t n = (size_t)w * h;
    for (size_t i = 0; i < n; i++) {
        size_t o = i * 4u;
        uint8_t r = src[o + (bgra ? 2 : 0)], g = src[o + 1], b = src[o + (bgra ? 0 : 2)], a = src[o + 3];
        if (alpha_mode == 0) a = 255u;
        else if (alpha_mode == 2 && srgb) vk_mm_srgb_premul_pixel(&r, &g, &b, &a);
        else if (alpha_mode == 2) {
            uint8_t mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
            if (a < mx) a = mx;
        }
        dst[o + 0] = r; dst[o + 1] = g; dst[o + 2] = b; dst[o + 3] = a;
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
    int active = (g_vk_mm.mm_ttl > 0u || g_vk_mm.saw_mm_frame) ? 1 : 0;
    int rslot = g_vk_mm.mm.ready_slot;
    uint32_t w = active ? g_vk_mm.mm.ready_w : 0u, h = active ? g_vk_mm.mm.ready_h : 0u, seq = g_vk_mm.mm.seq;
    if (active && rslot >= 0 && rslot < VK_MM_SLOTS && g_vk_mm.mm.slot[rslot].ptr && seq != client_seq
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

static int vk_mm_read_overlay_stream(
    vk_mm_stream *st, uint32_t ttl, int saw_frame, uint32_t max_w, uint32_t max_h,
    uint8_t *rgba_out, int max_bytes, int32_t meta[4]) {
    if (!meta) return 0;
    atomic_store_explicit(&g_vk_mm.want_active, 1, memory_order_release);
    uint32_t client_seq = (uint32_t)meta[2];
    int updated = 0;
    pthread_mutex_lock(&g_vk_mm.mu);
    int active = (ttl > 0u || saw_frame || atomic_load_explicit(&st->pending_valid, memory_order_relaxed)) ? 1 : 0;
    int rslot = st->ready_slot;
    uint32_t w = st->ready_w, h = st->ready_h, seq = st->seq;
    if (active && rslot >= 0 && rslot < VK_MM_SLOTS && st->slot[rslot].ptr && seq != client_seq
            && w > 0 && w <= max_w && h > 0 && h <= max_h) {
        size_t need = (size_t)w * h * 4u;
        if (rgba_out && need > 0 && max_bytes > 0 && (size_t)max_bytes >= need) {
            vk_mm_copy_rgba(rgba_out, st->slot[rslot].ptr, w, h, st->ready_fmt, 2);
            updated = 1;
        }
    }
    meta[0] = (int32_t)w; meta[1] = (int32_t)h; meta[2] = (int32_t)seq; meta[3] = active;
    pthread_mutex_unlock(&g_vk_mm.mu);
    return updated;
}

int vk_minimap_objective_read(uint8_t *rgba_out, int max_bytes, int32_t meta[4]) {
    return vk_mm_read_overlay_stream(&g_vk_mm.obj, g_vk_mm.obj_ttl, g_vk_mm.saw_obj_frame,
        VK_OBJ_MAX_W, VK_OBJ_MAX_H, rgba_out, max_bytes, meta);
}

int vk_minimap_lhud_read(uint8_t *rgba_out, int max_bytes, int32_t meta[4]) {
    return vk_mm_read_overlay_stream(&g_vk_mm.lhud, g_vk_mm.lhud_ttl, g_vk_mm.saw_lhud_frame,
        VK_LHUD_MAX_W, VK_LHUD_MAX_H, rgba_out, max_bytes, meta);
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
    if (!v) { g_vk_mm.mm_ttl = 0u; g_vk_mm.obj_ttl = 0u; g_vk_mm.lhud_ttl = 0u; g_vk_mm.prev_hit_obj = (vk_mm_rect){0, 0, 0u, 0u}; g_vk_mm.nwm_cur = 0; g_vk_mm.nwm_prev = 0; g_vk_mm.pass_lhud_top = 0; g_vk_mm.prev_lhud_top = 0; }
}
