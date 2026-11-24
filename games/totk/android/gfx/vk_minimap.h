// SPDX-License-Identifier: MIT
#pragma once
// Native Vulkan offscreen minimap render-target capture (2-slot staging ring).
// Included only by vk_draw.c; exports vk_minimap_* for companion_jni.c.

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    VK_MM_MIN_SIDE = 128u,
    VK_MM_MAX_SIDE = 512u,
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
    vk_mm_cand cand[VK_MM_MAX_CAND];
    _Atomic int cand_count, want_active, pending_valid;
    _Atomic uint32_t cand_idx;
    uint32_t frame_counter, pending_frame;
    int write_slot, pending_slot, ready_slot, wrote_in_frame;
    uint32_t write_w, write_h, pending_w, pending_h, ready_w, ready_h, seq;
    VkFormat write_fmt, pending_fmt, ready_fmt;
    uint64_t pending_fence;
    int env_init;
    uint32_t env_w, env_h;
} g_vk_mm = {
    .mu = PTHREAD_MUTEX_INITIALIZER,
    .ready_slot = -1,
    .pending_slot = -1,
};

static int vk_mm_supported_fmt(VkFormat fmt) {
    return fmt == VK_FORMAT_R8G8B8A8_UNORM || fmt == VK_FORMAT_R8G8B8A8_SRGB
        || fmt == VK_FORMAT_B8G8R8A8_UNORM || fmt == VK_FORMAT_B8G8R8A8_SRGB;
}

static void vk_mm_init_env(void) {
    if (g_vk_mm.env_init) return;
    g_vk_mm.env_init = 1;
    const char *e = getenv("TOTK_MINIMAP_RT");
    unsigned w = 0, h = 0;
    if (e && sscanf(e, "%ux%u", &w, &h) == 2
            && w >= 64u && w <= VK_MM_MAX_SIDE && h >= 64u && h <= VK_MM_MAX_SIDE) {
        g_vk_mm.env_w = w;
        g_vk_mm.env_h = h;
        LOGV("[companion-rt] TOTK_MINIMAP_RT=%ux%u", w, h);
    }
}

static int vk_mm_match_size(uint32_t w, uint32_t h) {
    vk_mm_init_env();
    if (g_vk_mm.env_w) return w == g_vk_mm.env_w && h == g_vk_mm.env_h;
    return w == h && w >= VK_MM_MIN_SIDE && w <= VK_MM_MAX_SIDE;
}

static int vk_mm_find_cand(uint32_t w, uint32_t h, VkFormat fmt, uint64_t serial) {
    int cnt = atomic_load_explicit(&g_vk_mm.cand_count, memory_order_relaxed);
    for (int i = 0; i < cnt && i < VK_MM_MAX_CAND; i++) {
        vk_mm_cand *c = &g_vk_mm.cand[i];
        if (c->w == w && c->h == h && c->fmt == fmt && c->serial == serial) return i;
    }
    if (cnt >= VK_MM_MAX_CAND) return -1;
    g_vk_mm.cand[cnt] = (vk_mm_cand){w, h, fmt, serial};
    atomic_store_explicit(&g_vk_mm.cand_count, cnt + 1, memory_order_release);
    LOGV("[companion-rt] candidate #%d: %ux%u fmt=%d serial=%llu",
         cnt, w, h, (int)fmt, (unsigned long long)serial);
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

static void vk_minimap_pass_end(
    VkCommandBuffer cb, vk_tex *t, unsigned level, unsigned layer, VkFormat fmt, uint64_t serial) {
    if (!cb || !t || !V.dev || !vk_tex_image(t)) return;
    if (vk_tex_samples(t) > 1 || vk_tex_is3d(t) || !(vk_tex_aspect(t) & VK_IMAGE_ASPECT_COLOR_BIT)) return;
    if (level >= vk_tex_levels(t) || layer >= vk_tex_layers(t)) return;
    uint32_t w = vk_tex_width(t, level), h = vk_tex_height(t, level);
    if (!vk_mm_supported_fmt(fmt) || !vk_mm_match_size(w, h)) return;
    int ci = vk_mm_find_cand(w, h, fmt, serial);
    if (ci < 0 || !atomic_load_explicit(&g_vk_mm.want_active, memory_order_acquire)) return;
    if (g_vk_mm.wrote_in_frame || atomic_load_explicit(&g_vk_mm.pending_valid, memory_order_acquire)) return;
    if ((g_vk_mm.frame_counter & 3u) != 0u) return;
    int cnt = atomic_load_explicit(&g_vk_mm.cand_count, memory_order_relaxed);
    if (cnt <= 0 || ci != (int)(atomic_load_explicit(&g_vk_mm.cand_idx, memory_order_acquire) % (uint32_t)cnt)) return;
    if ((VkDeviceSize)w * h * 4u > VK_MM_BUF_BYTES || !vk_mm_ensure_ring()) return;

    vk_tex_native_read(t);
    VkCommandBuffer copy_cb = vk_cb();
    if (!copy_cb) return;
    vk_barrier_all(copy_cb);
    vk_mm_slot *dst = &g_vk_mm.slot[g_vk_mm.write_slot];
    VkBufferImageCopy rg = {
        .bufferOffset = 0, .bufferRowLength = 0, .bufferImageHeight = 0,
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, layer, 1},
        .imageOffset = {0, 0, 0},
        .imageExtent = {w, h, 1}
    };
    vkCmdCopyImageToBuffer(copy_cb, vk_tex_image(t), VK_IMAGE_LAYOUT_GENERAL, dst->buf, 1, &rg);
    VkBufferMemoryBarrier2 bb = {
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
        .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = dst->buf,
        .offset = 0,
        .size = (VkDeviceSize)w * h * 4u
    };
    VkDependencyInfo di = {
        VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = &bb
    };
    vkCmdPipelineBarrier2(copy_cb, &di);
    g_vk_mm.wrote_in_frame = 1;
    g_vk_mm.write_w = w;
    g_vk_mm.write_h = h;
    g_vk_mm.write_fmt = fmt;
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
    int done = (g_vk_mm.frame_counter - g_vk_mm.pending_frame) >= 2u
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
