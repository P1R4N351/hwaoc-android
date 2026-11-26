/* Standalone C11 host regression tests for games/totk/android/gfx/vk_minimap.h. */
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint64_t VkBuffer, VkDeviceMemory, VkImage, VkImageView, VkCommandBuffer, VkDevice, VkPhysicalDevice, VkDeviceSize;
typedef uint32_t VkFlags, VkImageUsageFlags, VkImageAspectFlags, VkMemoryPropertyFlags, VkFormat, VkImageLayout;
typedef int32_t VkResult;
#define VK_NULL_HANDLE 0ULL
#define VK_WHOLE_SIZE (~0ULL)
#define VK_FORMAT_UNDEFINED 0u
#define VK_FORMAT_R32_SFLOAT 100u
#define VK_FORMAT_R32G32_SFLOAT 103u
#define VK_FORMAT_R32G32B32_SFLOAT 106u
#define VK_FORMAT_R32G32B32A32_SFLOAT 109u
#define VK_FORMAT_R16_SFLOAT 76u
#define VK_FORMAT_R16G16_SFLOAT 83u
#define VK_FORMAT_R16G16B16_SFLOAT 90u
#define VK_FORMAT_R16G16B16A16_SFLOAT 97u
#define VK_FORMAT_R8_UNORM 9u
#define VK_FORMAT_R8_SNORM 10u
#define VK_FORMAT_R8G8_UNORM 16u
#define VK_FORMAT_R8G8_SNORM 17u
#define VK_FORMAT_R8G8B8A8_UNORM 37u
#define VK_FORMAT_R8G8B8A8_SRGB 43u
#define VK_FORMAT_B8G8R8A8_UNORM 44u
#define VK_FORMAT_B8G8R8A8_SRGB 50u
#define VK_FORMAT_R16_UNORM 70u
#define VK_FORMAT_R16G16_UNORM 77u
#define VK_FORMAT_BC4_UNORM_BLOCK 139u
#define VK_FORMAT_BC4_SNORM_BLOCK 140u
#define VK_FORMAT_BC5_UNORM_BLOCK 141u
#define VK_FORMAT_BC5_SNORM_BLOCK 142u
#define VK_FORMAT_FEATURE_BLIT_SRC_BIT 0x400u
#define VK_FORMAT_FEATURE_BLIT_DST_BIT 0x800u
#define VK_IMAGE_LAYOUT_UNDEFINED 0u
#define VK_IMAGE_LAYOUT_GENERAL 1u
#define VK_IMAGE_USAGE_TRANSFER_SRC_BIT 1u
#define VK_IMAGE_USAGE_TRANSFER_DST_BIT 2u
#define VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT 16u
#define VK_BUFFER_USAGE_TRANSFER_DST_BIT 2u
#define VK_IMAGE_ASPECT_COLOR_BIT 1u
#define VK_IMAGE_TYPE_2D 1u
#define VK_IMAGE_VIEW_TYPE_2D 1u
#define VK_SAMPLE_COUNT_1_BIT 1u
#define VK_IMAGE_TILING_OPTIMAL 0u
#define VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT 1u
#define VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT 2u
#define VK_MEMORY_PROPERTY_HOST_COHERENT_BIT 4u
#define VK_MEMORY_PROPERTY_HOST_CACHED_BIT 8u
#define VK_ATTACHMENT_LOAD_OP_LOAD 0u
#define VK_ATTACHMENT_STORE_OP_STORE 0u
#define VK_QUEUE_FAMILY_IGNORED (~0u)
#define VK_FILTER_LINEAR 1u
#define VK_PIPELINE_STAGE_2_NONE 0ULL
#define VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT 0x10000ULL
#define VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT 0x400ULL
#define VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT 0x100ULL
#define VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT 0x200ULL
#define VK_PIPELINE_STAGE_2_TRANSFER_BIT 0x1000ULL
#define VK_PIPELINE_STAGE_2_HOST_BIT 0x4000ULL
#define VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT 0x80ULL
#define VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT 0x100ULL
#define VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT 0x200ULL
#define VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT 0x400ULL
#define VK_ACCESS_2_TRANSFER_READ_BIT 0x800ULL
#define VK_ACCESS_2_TRANSFER_WRITE_BIT 0x1000ULL
#define VK_ACCESS_2_HOST_READ_BIT 0x2000ULL
#define VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO 12u
#define VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO 5u
#define VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO 14u
#define VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO 15u
#define VK_STRUCTURE_TYPE_RENDERING_INFO 44u
#define VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO 45u
#define VK_STRUCTURE_TYPE_DEPENDENCY_INFO 46u
#define VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 47u
#define VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 48u
#define VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 49u

typedef enum { VK_MEMORY_DIAGNOSTIC = 0, VK_MEMORY_READBACK = 1 } vk_memory_purpose;
typedef struct { int32_t x, y; } VkOffset2D;
typedef struct { uint32_t width, height; } VkExtent2D;
typedef struct { uint32_t width, height, depth; } VkExtent3D;
typedef struct { int32_t x, y, z; } VkOffset3D;
typedef struct { VkOffset2D offset; VkExtent2D extent; } VkRect2D;
typedef struct { uint32_t aspectMask, baseMipLevel, levelCount, baseArrayLayer, layerCount; } VkImageSubresourceRange;
typedef struct { uint32_t aspectMask, mipLevel, baseArrayLayer, layerCount; } VkImageSubresourceLayers;
typedef union { float float32[4]; int32_t int32[4]; uint32_t uint32[4]; } VkClearColorValue;
typedef struct { uint32_t sType; const void *pNext; VkFlags flags; VkDeviceSize size; uint32_t usage; } VkBufferCreateInfo;
typedef struct { uint64_t size, alignment; uint32_t memoryTypeBits; } VkMemoryRequirements;
typedef struct { uint32_t sType; const void *pNext; uint64_t allocationSize; uint32_t memoryTypeIndex; } VkMemoryAllocateInfo;
typedef struct { uint32_t sType; const void *pNext; VkFlags flags; uint32_t imageType; VkFormat format; VkExtent3D extent; uint32_t mipLevels, arrayLayers, samples, tiling, usage, initialLayout; } VkImageCreateInfo;
typedef struct { uint32_t sType; const void *pNext; VkFlags flags; VkImage image; uint32_t viewType; VkFormat format; VkImageSubresourceRange subresourceRange; } VkImageViewCreateInfo;
typedef struct { uint32_t sType; const void *pNext; uint64_t srcStageMask, srcAccessMask, dstStageMask, dstAccessMask; uint32_t oldLayout, newLayout, srcQueueFamilyIndex, dstQueueFamilyIndex; VkImage image; VkImageSubresourceRange subresourceRange; } VkImageMemoryBarrier2;
typedef struct { uint32_t sType; const void *pNext; uint64_t srcStageMask, srcAccessMask, dstStageMask, dstAccessMask; uint32_t srcQueueFamilyIndex, dstQueueFamilyIndex; VkBuffer buffer; uint64_t offset, size; } VkBufferMemoryBarrier2;
typedef struct { uint32_t sType; const void *pNext; uint64_t srcStageMask, srcAccessMask, dstStageMask, dstAccessMask; } VkMemoryBarrier2;
typedef struct { uint32_t sType; const void *pNext; VkFlags dependencyFlags; uint32_t memoryBarrierCount; const VkMemoryBarrier2 *pMemoryBarriers; uint32_t bufferMemoryBarrierCount; const VkBufferMemoryBarrier2 *pBufferMemoryBarriers; uint32_t imageMemoryBarrierCount; const VkImageMemoryBarrier2 *pImageMemoryBarriers; } VkDependencyInfo;
typedef struct { uint64_t bufferOffset; VkImageSubresourceLayers imageSubresource; VkOffset3D imageOffset; VkExtent3D imageExtent; } VkBufferImageCopy;
typedef struct { VkImageSubresourceLayers srcSubresource; VkOffset3D srcOffsets[2]; VkImageSubresourceLayers dstSubresource; VkOffset3D dstOffsets[2]; } VkImageBlit;
typedef struct { uint32_t linearTilingFeatures, optimalTilingFeatures, bufferFeatures; } VkFormatProperties;
typedef struct { uint32_t sType; const void *pNext; VkImageView imageView; uint32_t imageLayout, loadOp, storeOp; } VkRenderingAttachmentInfo;
typedef struct { uint32_t sType; const void *pNext; VkRect2D renderArea; uint32_t layerCount, colorAttachmentCount; const VkRenderingAttachmentInfo *pColorAttachments, *pDepthAttachment, *pStencilAttachment; } VkRenderingInfo;
typedef struct { int32_t prim, first, count, base_instance, instances; } p_draw;
typedef struct { int32_t prim, itype, count, base_vertex, base_instance, instances; uint64_t ibuf; } p_draw_idx;
typedef struct vk_tex { VkImage img; VkFormat fmt; uint64_t serial; uint32_t w, h, layers, levels, samples, aspect; } vk_tex;

static uint64_t g_f = 10u, g_id = 0u;
static int g_ncib = 0;
static uint8_t g_bufs[16][1024 * 1024];
static void stub_fp(VkPhysicalDevice p, VkFormat f, VkFormatProperties *o) { (void)p; (void)f; o->optimalTilingFeatures = 0xfffu; }
static struct { VkDevice dev; VkPhysicalDevice pd; void (*get_format_props)(VkPhysicalDevice, VkFormat, VkFormatProperties *); } V = { 1ULL, 1ULL, stub_fp };

static VkResult vkCreateBuffer(VkDevice d, const VkBufferCreateInfo *c, const void *a, VkBuffer *o) { (void)d; (void)c; (void)a; *o = ++g_id; return 0; }
static void vkDestroyBuffer(VkDevice d, VkBuffer b, const void *a) { (void)d; (void)b; (void)a; }
static void vkGetBufferMemoryRequirements(VkDevice d, VkBuffer b, VkMemoryRequirements *r) { (void)d; (void)b; r->size = 1024u * 1024u; r->memoryTypeBits = 1u; }
static VkResult vkBindBufferMemory(VkDevice d, VkBuffer b, VkDeviceMemory m, uint64_t o) { (void)d; (void)b; (void)m; (void)o; return 0; }
static VkResult vkMapMemory(VkDevice d, VkDeviceMemory m, uint64_t o, uint64_t s, uint32_t fl, void **p) { (void)d; (void)o; (void)s; (void)fl; *p = g_bufs[m & 15u]; return 0; }
static void vkUnmapMemory(VkDevice d, VkDeviceMemory m) { (void)d; (void)m; }
static VkResult vkCreateImage(VkDevice d, const VkImageCreateInfo *c, const void *a, VkImage *o) { (void)d; (void)c; (void)a; *o = ++g_id; return 0; }
static void vkDestroyImage(VkDevice d, VkImage i, const void *a) { (void)d; (void)i; (void)a; }
static void vkGetImageMemoryRequirements(VkDevice d, VkImage i, VkMemoryRequirements *r) { (void)d; (void)i; r->size = 4096u; r->memoryTypeBits = 1u; }
static VkResult vkBindImageMemory(VkDevice d, VkImage i, VkDeviceMemory m, uint64_t o) { (void)d; (void)i; (void)m; (void)o; return 0; }
static VkResult vkCreateImageView(VkDevice d, const VkImageViewCreateInfo *c, const void *a, VkImageView *o) { (void)d; (void)c; (void)a; *o = ++g_id; return 0; }
static void vkDestroyImageView(VkDevice d, VkImageView v, const void *a) { (void)d; (void)v; (void)a; }
static void vkDeviceWaitIdle(VkDevice d) { (void)d; }
static void vkCmdPipelineBarrier2(VkCommandBuffer c, const VkDependencyInfo *di) { (void)c; (void)di; }
static void vkCmdClearColorImage(VkCommandBuffer c, VkImage i, uint32_t l, const VkClearColorValue *v, uint32_t r, const VkImageSubresourceRange *rr) { (void)c; (void)i; (void)l; (void)v; (void)r; (void)rr; }
static void vkCmdBeginRendering(VkCommandBuffer c, const VkRenderingInfo *ri) { (void)c; (void)ri; }
static void vkCmdEndRendering(VkCommandBuffer c) { (void)c; }
static void vkCmdCopyImageToBuffer(VkCommandBuffer c, VkImage i, uint32_t l, VkBuffer b, uint32_t r, const VkBufferImageCopy *rg) { (void)c; (void)i; (void)l; (void)b; (void)r; (void)rg; g_ncib++; }
static void vkCmdBlitImage(VkCommandBuffer c, VkImage s, uint32_t sl, VkImage d, uint32_t dl, uint32_t n, const VkImageBlit *r, uint32_t f) { (void)c; (void)s; (void)sl; (void)d; (void)dl; (void)n; (void)r; (void)f; }
static int vk_pick_type(uint32_t b, VkMemoryPropertyFlags w) { (void)w; return (b & 1u) ? 0 : -1; }
static VkResult vk_memory_allocate(const VkMemoryAllocateInfo *i, VkDeviceMemory *m, vk_memory_purpose p) { (void)i; (void)p; *m = ++g_id; return 0; }
static void vk_memory_release(VkDeviceMemory m, vk_memory_purpose p) { (void)m; (void)p; }
static VkCommandBuffer vk_cb(void) { return 1ULL; }
static void vk_barrier_all(VkCommandBuffer c) { (void)c; }
static void vk_occlusion_end(VkCommandBuffer c) { (void)c; }
static uint64_t mtl_last_fence(void) { return g_f; }
static int mtl_wait(uint64_t f, uint64_t ns) { (void)f; (void)ns; return 1; }
static int vk_resolve_span(uint64_t a, VkBuffer *b, uint64_t *o, uint64_t *av) { *b = 1ULL; *o = 0ULL; *av = a ? 4096ULL : 0ULL; return a != 0u; }
static void vk_ub_read(uint64_t a, void *dst, size_t n) { memcpy(dst, (const void *)(uintptr_t)a, n); }
static VkImage vk_tex_image(vk_tex *x) { return x->img; }
static uint64_t vk_tex_serial(vk_tex *x) { return x->serial; }
static uint32_t vk_tex_samples(vk_tex *x) { return x->samples; }
static VkFormat vk_tex_format(vk_tex *x) { return x->fmt; }
static uint32_t vk_tex_width(vk_tex *x, unsigned l) { (void)l; return x->w; }
static uint32_t vk_tex_height(vk_tex *x, unsigned l) { (void)l; return x->h; }
static uint32_t vk_tex_layers(vk_tex *x) { return x->layers; }
static uint32_t vk_tex_levels(vk_tex *x) { return x->levels; }
static int vk_tex_is3d(vk_tex *x) { (void)x; return 0; }
static VkImageAspectFlags vk_tex_aspect(vk_tex *x) { return x->aspect; }
#define LOGV(...) ((void)0)

#include "vk_minimap.h"

typedef struct { float x, y, z, idx; } vtx_idx_pos;

static void init_fb_ortho(float fb[64]) {
    memset(fb, 0, sizeof(float) * 64u);
    fb[8 + 0] = 2.f / 1920.f; fb[8 + 5] = -2.f / 1080.f; fb[8 + 10] = 1.f;
    fb[8 + 12] = -1.f; fb[8 + 13] = 1.f; fb[8 + 15] = 1.f;
    fb[24 + 0] = 1.f; fb[24 + 5] = 1.f; fb[24 + 10] = 1.f;
}

static void make_quad(vtx_idx_pos *q, float x0, float y0, float x1, float y1) {
    float xs[6] = {x0, x1, x0, x1, x1, x0}, ys[6] = {y0, y0, y1, y0, y1, y1};
    for (int i = 0; i < 6; i++) q[i] = (vtx_idx_pos){xs[i], ys[i], 0.f, 0.f};
}

static vk_mm_draw_ctx make_dc(const void *vb, const void *ubo, uint32_t st, uint32_t blend) {
    vk_mm_draw_ctx dc;
    memset(&dc, 0, sizeof dc);
    dc.nattr = 2; dc.vin_mask = 3u; dc.st = st; dc.c0_mask = 0xfu; dc.blend_en = blend;
    dc.ax = 1.f; dc.ay = 1.f;
    dc.attr[0] = (vk_mm_attr){VK_FORMAT_R32_SFLOAT, 12u, 16u, 0u, (uint64_t)(uintptr_t)vb, 4096u};
    dc.attr[1] = (vk_mm_attr){VK_FORMAT_R32G32B32_SFLOAT, 0u, 16u, 0u, (uint64_t)(uintptr_t)vb, 4096u};
    dc.ubo_addr[0] = (uint64_t)(uintptr_t)ubo; dc.ubo_sz[0] = 256u;
    return dc;
}

static int sel_box(const float fb[64], const vtx_idx_pos *q, int cnt, uint32_t st, uint32_t blend) {
    vk_mm_draw_ctx dc = make_dc(q, fb, st, blend);
    p_draw dw = {0, 0, cnt, 0, 1};
    return vk_minimap_select_target(1ULL, &dc, 0, &dw, NULL);
}

static void test_world_ring_known_by_texture(void) {
    vk_tex im = {55ULL, VK_FORMAT_R8G8B8A8_UNORM, 4ULL, 1920u, 1080u, 1u, 1u, 1u, VK_IMAGE_ASPECT_COLOR_BIT};
    VkRenderingAttachmentInfo sp = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, NULL, 30ULL, VK_IMAGE_LAYOUT_GENERAL, 0u, 0u};
    vk_minimap_enable(0); vk_minimap_enable(1);
    g_f += 2u;
    vk_minimap_pass_begin(1ULL, &im, 0u, 0u, 1920u, 1080u, VK_FORMAT_R8G8B8A8_UNORM,
        (VkRect2D){{0, 0}, {1920u, 1080u}}, 10ULL, VK_IMAGE_LAYOUT_GENERAL, NULL, &sp, 1, 1);
    g_vk_mm.ring_tex = 77ULL; g_vk_mm.nwm_cur = 0; g_vk_mm.nwm_prev = 0;
    float r_sm_mn[2] = {296.f, 612.f}, r_sm_mx[2] = {333.f, 650.f};
    float h_rg_mn[2] = {29.f, 498.f}, h_rg_mx[2] = {111.f, 580.f};
    float ic_mn[2] = {283.f, 600.f}, ic_mx[2] = {345.f, 662.f};
    float cm_mn[2] = {218.f, 529.f}, cm_mx[2] = {271.f, 594.f};
    vk_minimap_note_draw_tex(77ULL);
    assert(vk_mm_is_world_marker(r_sm_mn, r_sm_mx, 12) == 1 && g_vk_mm.nwm_cur == 1);
    vk_minimap_note_draw_tex(5ULL);
    assert(vk_mm_is_world_marker(h_rg_mn, h_rg_mx, 12) == 0 && g_vk_mm.nwm_cur == 1);
    assert(vk_mm_is_world_marker(ic_mn, ic_mx, 6) == 1);
    assert(vk_mm_is_world_marker(cm_mn, cm_mx, 12) == 0);
    assert(vk_mm_is_world_marker(cm_mn, cm_mx, 6) == 0);
    float ring_b_mn[2] = {113.f, 930.f}, ring_b_mx[2] = {217.f, 1035.f};
    float glow_mn[2] = {81.f, 898.f}, glow_mx[2] = {249.f, 1066.f};
    float pill_mn[2] = {42.f, 256.f}, pill_mx[2] = {278.f, 320.f};
    float far_mn[2] = {300.f, 700.f}, far_mx[2] = {468.f, 868.f};
    vk_minimap_note_draw_tex(77ULL);
    assert(vk_mm_is_world_marker(ring_b_mn, ring_b_mx, 12) == 1);
    vk_minimap_note_draw_tex(5ULL);
    assert(vk_mm_is_world_marker(glow_mn, glow_mx, 6) == 1);
    assert(vk_mm_is_world_marker(pill_mn, pill_mx, 6) == 0);
    assert(vk_mm_is_world_marker(far_mn, far_mx, 6) == 0);
    vk_minimap_pass_end(1ULL, &im, 4ULL);
    vk_minimap_present();
    g_vk_mm.ring_tex = 0ULL;
}

static void test_minimap_variant_without_stencil(void) {
    float fb[64]; init_fb_ortho(fb);
    vtx_idx_pos q_dot[6], q_back[6], q_map[6], q_mark[6], q_else[6];
    make_quad(q_dot, 1734.f, 882.f, 1738.f, 886.f);
    make_quad(q_back, 1494.f, 652.f, 1926.f, 1084.f);
    make_quad(q_map, 1530.f, 688.f, 1890.f, 1048.f);
    make_quad(q_mark, 1616.f, 884.f, 1688.f, 956.f);
    make_quad(q_else, 900.f, 500.f, 1000.f, 560.f);
    vk_tex im = {55ULL, VK_FORMAT_R8G8B8A8_UNORM, 4ULL, 1920u, 1080u, 1u, 1u, 1u, VK_IMAGE_ASPECT_COLOR_BIT};
    VkRenderingAttachmentInfo sp = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, NULL, 30ULL, VK_IMAGE_LAYOUT_GENERAL, 0u, 0u};
    g_vk_mm.mm_ttl = 0u; g_vk_mm.learn_mm_half = 0;
    for (int frame = 0; frame < 2; frame++) {
        g_f += 2u;
        vk_minimap_pass_begin(1ULL, &im, 0u, 0u, 1920u, 1080u, VK_FORMAT_R8G8B8A8_UNORM,
            (VkRect2D){{0, 0}, {1920u, 1080u}}, 10ULL, VK_IMAGE_LAYOUT_GENERAL, NULL, &sp, 1, 1);
        sel_box(fb, q_dot, 6, 0u, 1u);
        assert(g_vk_mm.cur_hud == frame);
        sel_box(fb, q_back, 6, 0u, 1u);
        assert(g_vk_mm.pass_mm_hit == 1 && g_vk_mm.cur_hud == 1);
        sel_box(fb, q_map, 6, 0u, 1u);
        assert(g_vk_mm.cur_hud == 1);
        sel_box(fb, q_mark, 6, 0u, 1u);
        assert(g_vk_mm.cur_hud == 1);
        sel_box(fb, q_else, 6, 0u, 1u);
        assert(g_vk_mm.cur_hud == 0);
        vk_minimap_pass_end(1ULL, &im, 4ULL);
        vk_minimap_present();
    }
}

static void test_backing_edge_and_unknown_ring(void) {
    float fb[64]; init_fb_ortho(fb);
    vk_tex im = {55ULL, VK_FORMAT_R8G8B8A8_UNORM, 4ULL, 1920u, 1080u, 1u, 1u, 1u, VK_IMAGE_ASPECT_COLOR_BIT};
    VkRenderingAttachmentInfo sp = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, NULL, 30ULL, VK_IMAGE_LAYOUT_GENERAL, 0u, 0u};
    vtx_idx_pos q_ht[6], q_edge[6], q_div[6], q_hl[12], q_mask[6], q_mring[12];
    make_quad(q_ht, 50.f, 25.f, 94.f, 69.f);
    make_quad(q_edge, 388.f, 514.f, 396.f, 626.f);
    make_quad(q_div, 400.f, 300.f, 408.f, 700.f);
    make_quad(&q_hl[0], 29.f, 498.f, 111.f, 580.f);
    make_quad(&q_hl[6], 29.f, 498.f, 111.f, 580.f);
    make_quad(q_mask, 1540.f, 698.f, 1880.f, 1038.f);
    make_quad(&q_mring[0], 1687.f, 722.f, 1798.f, 832.f);
    make_quad(&q_mring[6], 1687.f, 722.f, 1798.f, 832.f);
    vk_minimap_enable(0); vk_minimap_enable(1);
    g_vk_mm.ring_tex = 0ULL; g_f += 2u;
    vk_minimap_pass_begin(1ULL, &im, 0u, 0u, 1920u, 1080u, VK_FORMAT_R8G8B8A8_UNORM,
        (VkRect2D){{0, 0}, {1920u, 1080u}}, 10ULL, VK_IMAGE_LAYOUT_GENERAL, NULL, &sp, 1, 1);
    vk_minimap_note_draw_tex(5ULL);
    sel_box(fb, q_ht, 6, 0u, 1u); assert(g_vk_mm.cur_hud == 1);
    sel_box(fb, q_edge, 6, 0u, 1u); assert(g_vk_mm.cur_hud == 1);
    sel_box(fb, q_div, 6, 0u, 1u); assert(g_vk_mm.cur_hud == 0);
    vk_minimap_note_draw_tex(9ULL);
    sel_box(fb, q_hl, 12, 0u, 1u); assert(g_vk_mm.cur_hud == 1);
    {
        vk_mm_draw_ctx dc = make_dc(q_mask, fb, 1u, 1u);
        dc.c0_mask = 0x0u;
        p_draw dw = {0, 0, 6, 0, 1};
        vk_minimap_select_target(1ULL, &dc, 0, &dw, NULL);
        assert(g_vk_mm.pass_mm_hit == 1);
    }
    vk_minimap_note_draw_tex(77ULL);
    sel_box(fb, q_mring, 12, 0u, 1u); assert(g_vk_mm.cur_hud == 1 && g_vk_mm.ring_tex == 77ULL);
    vk_minimap_pass_end(1ULL, &im, 4ULL);
    vk_minimap_present();
    g_vk_mm.ring_tex = 0ULL;
}

static void test_late_centre_plate_keeps_staging(void) {
    float fb[64]; init_fb_ortho(fb);
    vk_tex im = {55ULL, VK_FORMAT_R8G8B8A8_UNORM, 4ULL, 1920u, 1080u, 1u, 1u, 1u, VK_IMAGE_ASPECT_COLOR_BIT};
    VkRenderingAttachmentInfo sp = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, NULL, 30ULL, VK_IMAGE_LAYOUT_GENERAL, 0u, 0u};
    vtx_idx_pos q_heart[6], q_plate[6], q_after[6];
    make_quad(q_heart, 50.f, 25.f, 94.f, 69.f);
    make_quad(q_plate, 859.f, 374.f, 1439.f, 587.f);
    make_quad(q_after, 80.f, 25.f, 124.f, 69.f);
    vk_minimap_enable(0); vk_minimap_enable(1);
    g_f += 2u;
    vk_minimap_pass_begin(1ULL, &im, 0u, 0u, 1920u, 1080u, VK_FORMAT_R8G8B8A8_UNORM,
        (VkRect2D){{0, 0}, {1920u, 1080u}}, 10ULL, VK_IMAGE_LAYOUT_GENERAL, NULL, &sp, 1, 1);
    vk_minimap_note_draw_wid(KTGL_WID_NONE);
    sel_box(fb, q_heart, 6, 0u, 1u);
    assert(g_vk_mm_tr.cls == 'l' && vk_minimap_on_hud_target());
    sel_box(fb, q_plate, 6, 0u, 1u);                       /* the plate itself stays on the first screen */
    assert(!vk_minimap_on_hud_target());
    sel_box(fb, q_after, 6, 0u, 1u);                       /* and what follows it in the pass is refused */
    assert(g_vk_mm_tr.cls == 'M' && !vk_minimap_on_hud_target());
    g_vk_mm.saw_lhud_frame = 0;
    vk_minimap_pass_end(1ULL, &im, 4ULL);
    assert(g_vk_mm.saw_lhud_frame == 1);                   /* what was captured before the plate is still staged */
    vk_minimap_present();
}

static void test_stream_modes(void) {
    float fb[64]; init_fb_ortho(fb);
    vk_tex im = {55ULL, VK_FORMAT_R8G8B8A8_UNORM, 4ULL, 1920u, 1080u, 1u, 1u, 1u, VK_IMAGE_ASPECT_COLOR_BIT};
    VkRenderingAttachmentInfo sp = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, NULL, 30ULL, VK_IMAGE_LAYOUT_GENERAL, 0u, 0u};
    vtx_idx_pos q_combo[6], q_bar[18];
    make_quad(q_combo, -60.f, 514.f, 396.f, 626.f);
    make_quad(&q_bar[0], 1420.f, 468.f, 1920.f, 524.f);
    make_quad(&q_bar[6], 1440.f, 472.f, 1920.f, 520.f);
    make_quad(&q_bar[12], 1460.f, 476.f, 1920.f, 516.f);
    vk_minimap_enable(0); vk_minimap_enable(1);
    g_f += 2u;
    vk_minimap_pass_begin(1ULL, &im, 0u, 0u, 1920u, 1080u, VK_FORMAT_R8G8B8A8_UNORM,
        (VkRect2D){{0, 0}, {1920u, 1080u}}, 10ULL, VK_IMAGE_LAYOUT_GENERAL, NULL, &sp, 1, 1);
    vk_minimap_note_draw_wid(KTGL_WID_COMBO);              /* default: the HUD column is captured */
    sel_box(fb, q_combo, 6, 0u, 1u);
    assert(vk_minimap_on_hud_target() && vk_minimap_skip_draw() == 0);
    vk_minimap_stream_mode(2, 1);                          /* kept on the first screen: drawn there, not skipped */
    sel_box(fb, q_combo, 6, 0u, 1u);
    assert(!vk_minimap_on_hud_target() && vk_minimap_skip_draw() == 0);
    vk_minimap_stream_mode(2, 2);                          /* hidden: drawn on neither */
    sel_box(fb, q_combo, 6, 0u, 1u);
    assert(!vk_minimap_on_hud_target() && vk_minimap_skip_draw() == 1);
    vk_minimap_note_draw_wid(KTGL_WID_OBJECTIVE);          /* the objective bar is a separate module */
    sel_box(fb, q_bar, 18, 0u, 1u);
    assert(vk_minimap_on_hud_target() && vk_minimap_skip_draw() == 0);
    vk_minimap_stream_mode(1, 1);
    sel_box(fb, q_bar, 18, 0u, 1u);
    assert(!vk_minimap_on_hud_target() && vk_minimap_skip_draw() == 0);
    vk_minimap_stream_mode(3, 1); vk_minimap_stream_mode(0, 7);   /* out of range: no change */
    vk_minimap_stream_mode(1, 0); vk_minimap_stream_mode(2, 0);
    vk_minimap_note_draw_wid(KTGL_WID_COMBO);
    sel_box(fb, q_combo, 6, 0u, 1u);
    assert(vk_minimap_on_hud_target() && vk_minimap_skip_draw() == 0);
    vk_minimap_note_draw_wid(KTGL_WID_NONE);
    vk_minimap_pass_end(1ULL, &im, 4ULL);
    vk_minimap_present();
}

static void test_wid_routing_and_stability(void) {
    float fb[64]; init_fb_ortho(fb);
    vk_tex im = {55ULL, VK_FORMAT_R8G8B8A8_UNORM, 4ULL, 1920u, 1080u, 1u, 1u, 1u, VK_IMAGE_ASPECT_COLOR_BIT};
    VkRenderingAttachmentInfo sp = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, NULL, 30ULL, VK_IMAGE_LAYOUT_GENERAL, 0u, 0u};
    vtx_idx_pos q_dummy[6], q_combo[6], q_heart_menu[6], q_speech[6], q_wm[12];
    vtx_idx_pos q_bar[18], q_txt[36], q_ic[6], q_ring0[12], q_ring1[12], q_mm[6];
    make_quad(q_dummy, 960.f, 540.f, 961.f, 541.f);
    make_quad(q_combo, -60.f, 514.f, 396.f, 626.f);
    make_quad(q_heart_menu, 50.f, 25.f, 94.f, 69.f);
    make_quad(q_speech, 520.f, 820.f, 1400.f, 910.f);
    make_quad(&q_wm[0], 1450.f, 420.f, 1570.f, 540.f);
    make_quad(&q_wm[6], 1450.f, 420.f, 1570.f, 540.f);
    make_quad(&q_bar[0], 1420.f, 468.f, 1920.f, 524.f);
    make_quad(&q_bar[6], 1440.f, 472.f, 1920.f, 520.f);
    make_quad(&q_bar[12], 1460.f, 476.f, 1920.f, 516.f);
    for (int i = 0; i < 36; i += 6) make_quad(&q_txt[i], 1493.f, 478.f, 1781.f, 514.f);
    make_quad(q_ic, 1443.f, 476.f, 1483.f, 516.f);
    make_quad(&q_ring0[0], 1433.f, 466.f, 1493.f, 526.f);
    make_quad(&q_ring0[6], 1433.f, 466.f, 1493.f, 526.f);
    make_quad(&q_ring1[0], 1363.f, 396.f, 1563.f, 596.f);
    make_quad(&q_ring1[6], 1363.f, 396.f, 1563.f, 596.f);
    make_quad(q_mm, 1540.f, 698.f, 1880.f, 1038.f);
    vk_minimap_enable(0); vk_minimap_enable(1);
    g_vk_mm.mm_ttl = 0u; g_vk_mm.obj_ttl = 0u; g_f += 2u;
    vk_minimap_pass_begin(1ULL, &im, 0u, 0u, 1920u, 1080u, VK_FORMAT_R8G8B8A8_UNORM,
        (VkRect2D){{0, 0}, {1920u, 1080u}}, 10ULL, VK_IMAGE_LAYOUT_GENERAL, NULL, &sp, 1, 1);
    vk_minimap_note_draw_wid((uint8_t)(KTGL_WID_COMBO | KTGL_WID_FLAG_BG));
    assert(sel_box(fb, q_combo, 6, 0u, 1u) == 0 && vk_minimap_skip_draw() == 1);
    assert(g_vk_mm_tr.cls == 'l' && g_vk_mm_tr.wid == KTGL_WID_COMBO && g_vk_mm.pass_lhud_any == 0);
    vk_minimap_note_draw_wid(KTGL_WID_COMBO);
    sel_box(fb, q_combo, 6, 0u, 1u);
    assert(g_vk_mm.cur_hud == 1 && vk_minimap_skip_draw() == 0);
    assert(g_vk_mm_tr.cls == 'l' && g_vk_mm_tr.wid == KTGL_WID_COMBO);
    vk_minimap_note_draw_wid(KTGL_WID_SPEECH);
    sel_box(fb, q_speech, 6, 0u, 1u); assert(g_vk_mm.cur_hud == 0 && !vk_minimap_on_hud_target());
    vk_minimap_note_draw_wid(KTGL_WID_MENU);
    sel_box(fb, q_heart_menu, 6, 0u, 1u); assert(g_vk_mm.cur_hud == 0);
    vk_minimap_note_draw_wid(KTGL_WID_WORLD_MARKER);
    sel_box(fb, q_wm, 12, 0u, 1u); assert(g_vk_mm.cur_hud == 0);
    vk_minimap_note_draw_wid(KTGL_WID_OBJECTIVE);
    sel_box(fb, q_bar, 18, 0u, 1u); assert(g_vk_mm.cur_hud == 1 && vk_minimap_on_hud_target());
    sel_box(fb, q_txt, 36, 0u, 1u); sel_box(fb, q_ic, 6, 0u, 1u); assert(g_vk_mm.cur_hud == 1);
    vk_mm_rect obj_r0 = g_vk_mm.hit_obj;
    sel_box(fb, q_ring0, 12, 0u, 1u); assert(g_vk_mm.cur_hud == 1 && memcmp(&obj_r0, &g_vk_mm.hit_obj, sizeof obj_r0) == 0);
    sel_box(fb, q_ring1, 12, 0u, 1u); assert(g_vk_mm.cur_hud == 1 && memcmp(&obj_r0, &g_vk_mm.hit_obj, sizeof obj_r0) == 0);
    vk_minimap_note_draw_wid(KTGL_WID_MINIMAP);
    sel_box(fb, q_mm, 6, 0u, 1u); assert(g_vk_mm.cur_hud == 1);
    vk_mm_rect mm_r0 = g_vk_mm.hit_mm;
    sel_box(fb, q_dummy, 6, 0u, 1u);
    assert(g_vk_mm.cur_hud == 0 && g_vk_mm_tr.cls == '-' && memcmp(&mm_r0, &g_vk_mm.hit_mm, sizeof mm_r0) == 0);
    vtx_idx_pos q_mm_in[6], q_mm_left[6], q_allies[6], q_ko_ctr[6], q_obj_left[6], q_obj_ext[6];
    make_quad(q_mm_in, 1700.f, 860.f, 1720.f, 880.f);
    make_quad(q_mm_left, 100.f, 100.f, 200.f, 200.f);
    make_quad(q_allies, 40.f, 200.f, 590.f, 300.f);
    make_quad(q_ko_ctr, 820.f, 100.f, 1100.f, 150.f);
    make_quad(q_obj_left, 80.f, 100.f, 260.f, 160.f);
    make_quad(q_obj_ext, 1320.f, 390.f, 1500.f, 430.f);
    sel_box(fb, q_mm_in, 6, 0u, 1u);
    assert(g_vk_mm.cur_hud == 1 && memcmp(&mm_r0, &g_vk_mm.hit_mm, sizeof mm_r0) == 0);
    sel_box(fb, q_mm_left, 6, 0u, 1u);
    assert(g_vk_mm.cur_hud == 1 && g_vk_mm_tr.cls == 'l');
    vtx_idx_pos q_cleft[6], q_gzero[6], q_non_hud[6], q_bar_empty[6];
    make_quad(q_cleft, -60.f, 506.f, -52.f, 514.f);
    make_quad(q_gzero, 118.f, 88.f, 118.f, 88.f);
    make_quad(q_non_hud, 700.f, 100.f, 850.f, 160.f);
    make_quad(q_bar_empty, -118.f, 1008.f, 382.f, 1040.f);
    sel_box(fb, q_non_hud, 6, 0u, 1u);
    assert(g_vk_mm.cur_hud == 0 && g_vk_mm_tr.cls == '-');
    vk_minimap_note_draw_wid(KTGL_WID_COMBO);
    sel_box(fb, q_cleft, 6, 0u, 1u);
    assert(g_vk_mm.cur_hud == 1 && g_vk_mm_tr.cls == 'l');
    vk_minimap_note_draw_wid(KTGL_WID_LEFT_OTHER);
    sel_box(fb, q_bar_empty, 6, 0u, 1u);
    assert(g_vk_mm.cur_hud == 1 && g_vk_mm_tr.cls == 'l');
    vk_minimap_note_draw_wid(KTGL_WID_GAUGE);
    sel_box(fb, q_gzero, 6, 0u, 1u);
    assert(g_vk_mm.cur_hud == 1 && g_vk_mm_tr.cls == 'l');
    vk_minimap_note_draw_wid(KTGL_WID_ALLIES);
    sel_box(fb, q_allies, 6, 0u, 1u);
    assert(g_vk_mm.cur_hud == 1 && g_vk_mm.hit_lhud.w == 596u);
    vk_minimap_note_draw_wid(KTGL_WID_KO_COUNT);
    sel_box(fb, q_ko_ctr, 6, 0u, 1u);
    assert(g_vk_mm.cur_hud == 0 && g_vk_mm_tr.cls == '-');
    vk_minimap_note_draw_wid(KTGL_WID_OBJECTIVE);
    sel_box(fb, q_obj_left, 6, 0u, 1u);
    assert(g_vk_mm.cur_hud == 1 && g_vk_mm_tr.cls == 'l');
    sel_box(fb, q_non_hud, 6, 0u, 1u);
    assert(g_vk_mm.cur_hud == 0 && g_vk_mm_tr.cls == '-');
    sel_box(fb, q_obj_ext, 6, 0u, 1u);
    assert(g_vk_mm.cur_hud == 1 && g_vk_mm.hit_obj.x <= 1320 && g_vk_mm.hit_obj.y <= 390);
    uint8_t saved_hide = g_vk_mm.wid_dest[KTGL_WID_ITEMS];
    g_vk_mm.wid_dest[KTGL_WID_ITEMS] = KTGL_DEST_HIDE;
    vk_minimap_note_draw_wid(KTGL_WID_ITEMS);
    sel_box(fb, q_combo, 6, 0u, 1u);
    assert(g_vk_mm_tr.cls == 'h' && vk_minimap_skip_draw() == 1);
    g_vk_mm.wid_dest[KTGL_WID_ITEMS] = saved_hide;
    vk_minimap_note_draw_wid(KTGL_WID_COMBO);
    sel_box(fb, q_combo, 6, 0u, 1u);
    assert(g_vk_mm.cur_hud == 1 && vk_minimap_skip_draw() == 0);
    vk_minimap_note_draw_wid(KTGL_WID_NONE);
    vk_minimap_pass_end(1ULL, &im, 4ULL);
    assert(g_vk_mm.saw_lhud_frame == 1 && g_vk_mm.saw_mm_frame == 1);
    vk_minimap_present();
}

static void test_left_world_marker_stays(void) {
    float fb[64]; init_fb_ortho(fb);
    vk_tex im = {55ULL, VK_FORMAT_R8G8B8A8_UNORM, 4ULL, 1920u, 1080u, 1u, 1u, 1u, VK_IMAGE_ASPECT_COLOR_BIT};
    VkRenderingAttachmentInfo sp = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, NULL, 30ULL, VK_IMAGE_LAYOUT_GENERAL, 0u, 0u};
    vtx_idx_pos q_ht[6], q_ic[6], q_rg[12];
    make_quad(q_ht, 50.f, 25.f, 94.f, 69.f);
    make_quad(q_ic, 221.f, 341.f, 290.f, 410.f);
    make_quad(&q_rg[0], 200.f, 320.f, 311.f, 431.f);
    make_quad(&q_rg[6], 200.f, 320.f, 311.f, 431.f);
    vk_minimap_enable(0); vk_minimap_enable(1);
    g_f += 2u;
    vk_minimap_pass_begin(1ULL, &im, 0u, 0u, 1920u, 1080u, VK_FORMAT_R8G8B8A8_UNORM,
        (VkRect2D){{0, 0}, {1920u, 1080u}}, 10ULL, VK_IMAGE_LAYOUT_GENERAL, NULL, &sp, 1, 1);
    g_vk_mm.ring_tex = 77ULL;
    vk_minimap_note_draw_tex(5ULL);
    sel_box(fb, q_ht, 6, 0u, 1u); assert(g_vk_mm.cur_hud == 1);
    vk_minimap_note_draw_tex(77ULL);
    sel_box(fb, q_ic, 6, 0u, 1u); assert(g_vk_mm.cur_hud == 1);
    sel_box(fb, q_rg, 12, 0u, 1u); assert(g_vk_mm.cur_hud == 0);
    vk_minimap_pass_end(1ULL, &im, 4ULL);
    vk_minimap_present();
    g_f += 2u;
    vk_minimap_pass_begin(1ULL, &im, 0u, 0u, 1920u, 1080u, VK_FORMAT_R8G8B8A8_UNORM,
        (VkRect2D){{0, 0}, {1920u, 1080u}}, 10ULL, VK_IMAGE_LAYOUT_GENERAL, NULL, &sp, 1, 1);
    sel_box(fb, q_ht, 6, 0u, 1u); assert(g_vk_mm.cur_hud == 1);
    vk_minimap_note_draw_tex(77ULL);
    sel_box(fb, q_ic, 6, 0u, 1u); assert(g_vk_mm.cur_hud == 0);
    vk_minimap_pass_end(1ULL, &im, 4ULL);
    vk_minimap_present();
    g_vk_mm.ring_tex = 0ULL;
}

static void test_obj_and_subtitle(void) {
    float fb[64]; init_fb_ortho(fb);
    vk_tex im = {55ULL, VK_FORMAT_R8G8B8A8_UNORM, 4ULL, 1920u, 1080u, 1u, 1u, 1u, VK_IMAGE_ASPECT_COLOR_BIT};
    VkRenderingAttachmentInfo sp = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, NULL, 30ULL, VK_IMAGE_LAYOUT_GENERAL, 0u, 0u};
    vtx_idx_pos q_bar[18], q_txt[180], q_ic[6], q_ring[12], q_sub[18];
    make_quad(&q_bar[0], 1420.f, 468.f, 1920.f, 524.f);
    make_quad(&q_bar[6], 1440.f, 472.f, 1920.f, 520.f);
    make_quad(&q_bar[12], 1460.f, 476.f, 1920.f, 516.f);
    for (int i = 0; i < 180; i += 6) make_quad(&q_txt[i], 1493.f, 478.f, 1781.f, 514.f);
    make_quad(q_ic, 1443.f, 476.f, 1483.f, 516.f);
    make_quad(&q_ring[0], 1383.f, 416.f, 1543.f, 576.f);
    make_quad(&q_ring[6], 1383.f, 416.f, 1543.f, 576.f);
    for (int i = 0; i < 18; i += 6) make_quad(&q_sub[i], 620.f, 840.f, 1300.f, 900.f);
    vk_minimap_enable(1); g_f += 2u;
    vk_minimap_pass_begin(1ULL, &im, 0u, 0u, 1920u, 1080u, VK_FORMAT_R8G8B8A8_UNORM,
        (VkRect2D){{0, 0}, {1920u, 1080u}}, 10ULL, VK_IMAGE_LAYOUT_GENERAL, NULL, &sp, 1, 1);
    vk_minimap_note_draw_tex(10ULL);
    sel_box(fb, q_bar, 18, 0u, 1u); assert(g_vk_mm.cur_hud == 1 && g_vk_mm.pass_obj_pill == 1);
    sel_box(fb, q_txt, 174, 0u, 1u); assert(g_vk_mm.cur_hud == 1 && g_vk_mm.pass_obj_text == 1);
    sel_box(fb, q_ic, 6, 0u, 1u); assert(g_vk_mm.cur_hud == 1 && g_vk_mm.pass_obj_icon == 1);
    vk_minimap_note_draw_tex(77ULL);
    sel_box(fb, q_ring, 12, 0u, 1u); assert(g_vk_mm.cur_hud == 1 && g_vk_mm.ring_tex == 77ULL);
    sel_box(fb, q_sub, 18, 0u, 1u); assert(g_vk_mm.cur_hud == 0 && g_vk_mm.pass_dialog == 1);
    vk_minimap_pass_end(1ULL, &im, 4ULL);
    vk_minimap_present();
    assert(!vk_minimap_in_hud_pass());
    (void)&vk_minimap_bind_main;
    (void)&vk_minimap_note_fs_tex;
    (void)&vk_minimap_pass_abort;
    (void)&vk_minimap_trace_blend;
    (void)&vk_minimap_trace_draw;
}

static void test_travel_safely_subtitle_stays(void) {
    float fb[64]; init_fb_ortho(fb);
    vk_tex im = {55ULL, VK_FORMAT_R8G8B8A8_UNORM, 4ULL, 1920u, 1080u, 1u, 1u, 1u, VK_IMAGE_ASPECT_COLOR_BIT};
    VkRenderingAttachmentInfo sp = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, NULL, 30ULL, VK_IMAGE_LAYOUT_GENERAL, 0u, 0u};
    vtx_idx_pos q_ht[6], q_back[18], q_txt[180];
    make_quad(q_ht, 50.f, 25.f, 94.f, 69.f);
    for (int i = 0; i < 18; i += 6) make_quad(&q_back[i], 520.f, 820.f, 1400.f, 910.f);
    for (int i = 0; i < 180; i += 6) make_quad(&q_txt[i], 760.f, 845.f, 1160.f, 895.f);
    vk_minimap_enable(1); g_f += 2u;
    vk_minimap_pass_begin(1ULL, &im, 0u, 0u, 1920u, 1080u, VK_FORMAT_R8G8B8A8_UNORM,
        (VkRect2D){{0, 0}, {1920u, 1080u}}, 10ULL, VK_IMAGE_LAYOUT_GENERAL, NULL, &sp, 1, 1);
    sel_box(fb, q_ht, 6, 0u, 1u); assert(g_vk_mm.cur_hud == 1);
    sel_box(fb, q_back, 18, 0u, 1u); assert(g_vk_mm.cur_hud == 0 && g_vk_mm.pass_dialog == 1);
    sel_box(fb, q_txt, 174, 0u, 1u); assert(g_vk_mm.cur_hud == 0);
    vk_minimap_pass_end(1ULL, &im, 4ULL);
    vk_minimap_present();
}

static void test_zero_area_minimap_and_present_reset(void) {
    float fb[64]; init_fb_ortho(fb);
    vk_tex im = {55ULL, VK_FORMAT_R8G8B8A8_UNORM, 4ULL, 1920u, 1080u, 1u, 1u, 1u, VK_IMAGE_ASPECT_COLOR_BIT};
    VkRenderingAttachmentInfo sp = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, NULL, 30ULL, VK_IMAGE_LAYOUT_GENERAL, 0u, 0u};
    vtx_idx_pos q_mm[6], q_int[192], q_edge_mm[6], q_zero[6], q_sub[6];
    make_quad(q_mm, 1530.f, 688.f, 1890.f, 1048.f);
    for (int i = 0; i < 192; i += 6) make_quad(&q_int[i], 1669.f, 739.f, 1876.f, 962.f);
    make_quad(q_edge_mm, 1550.f, 860.f, 1603.f, 913.f);
    make_quad(q_zero, 960.f, 540.f, 960.f, 540.f);
    make_quad(q_sub, 960.f, 540.f, 960.7f, 540.7f);
    vtx_idx_pos q_bg[6], q_fs[6];
    make_quad(q_bg, 1494.f, 652.f, 1926.f, 1084.f);
    make_quad(q_fs, 0.f, 0.f, 1920.f, 1080.f);
    vk_minimap_enable(0); vk_minimap_enable(1);
    g_f += 2u;
    vk_minimap_pass_begin(1ULL, &im, 0u, 0u, 1920u, 1080u, VK_FORMAT_R8G8B8A8_UNORM,
        (VkRect2D){{0, 0}, {1920u, 1080u}}, 10ULL, VK_IMAGE_LAYOUT_GENERAL, NULL, &sp, 1, 1);
    vk_minimap_note_draw_wid(KTGL_WID_MENU);
    assert(sel_box(fb, q_fs, 6, 0u, 0u) == 0 && g_vk_mm_tr.cls == 'F' && g_vk_mm_tr.wid == KTGL_WID_MENU);
    vk_minimap_pass_end(1ULL, &im, 4ULL);
    vk_minimap_pass_begin(1ULL, &im, 0u, 0u, 1920u, 1080u, VK_FORMAT_R8G8B8A8_UNORM,
        (VkRect2D){{0, 0}, {1920u, 1080u}}, 10ULL, VK_IMAGE_LAYOUT_GENERAL, NULL, &sp, 1, 1);
    vk_minimap_note_draw_wid(KTGL_WID_MINIMAP);
    assert(sel_box(fb, q_bg, 6, 0u, 1u) == 1 && g_vk_mm.hit_mm.w == 432u);
    assert(sel_box(fb, q_mm, 6, 0u, 1u) == 0 && g_vk_mm.cur_hud == 1 && g_vk_mm.hit_mm.w == 360u);
    vk_mm_rect mm_r0 = g_vk_mm.hit_mm;
    assert(sel_box(fb, q_int, 192, 0u, 1u) == 0 && g_vk_mm.cur_hud == 1 && memcmp(&mm_r0, &g_vk_mm.hit_mm, sizeof mm_r0) == 0);
    assert(sel_box(fb, q_edge_mm, 6, 0u, 1u) == 0 && g_vk_mm.cur_hud == 1 && g_vk_mm_tr.cls == 'm');
    assert(sel_box(fb, q_zero, 6, 0u, 1u) == 0 && vk_minimap_skip_draw() == 1 && g_vk_mm_tr.cls == 'm');
    assert(sel_box(fb, q_sub, 6, 0u, 1u) == 0 && vk_minimap_skip_draw() == 1 && g_vk_mm_tr.cls == 'm');
    assert(memcmp(&mm_r0, &g_vk_mm.hit_mm, sizeof mm_r0) == 0);
    vk_minimap_note_draw_wid((uint8_t)(KTGL_WID_COMBO | KTGL_WID_FLAG_BG));
    assert(g_vk_mm.cur_wid == KTGL_WID_COMBO && g_vk_mm.cur_wid_bg == 1u);
    vk_minimap_pass_end(1ULL, &im, 4ULL);
    vk_minimap_present();
    assert(g_vk_mm.cur_wid == 0u && g_vk_mm.cur_wid_bg == 0u);
}

int main(void) {
    test_obj_and_subtitle();
    test_world_ring_known_by_texture();
    test_minimap_variant_without_stencil();
    test_backing_edge_and_unknown_ring();
    test_wid_routing_and_stability();
    test_zero_area_minimap_and_present_reset();
    test_stream_modes();
    test_late_centre_plate_keeps_staging();
    test_left_world_marker_stays();
    test_travel_safely_subtitle_stays();
    {
        uint32_t cm = 0x7u, sa = 2u, da = 1u, ea = 1u;
        assert(vk_hud_override_blend(1u, &cm, 5u, 6u, 1u, &sa, &da, &ea) == 1);
        assert(cm == 0xfu && sa == 2u && da == 6u && ea == 1u);
        uint8_t px[4] = {7u, 7u, 7u, 166u}, out[4] = {0};
        vk_mm_copy_rgba(out, px, 1u, 1u, VK_FORMAT_R8G8B8A8_UNORM, 2);
        assert(out[3] == 166u);
        cm = 0xfu; sa = 5u; da = 2u; ea = 0u;
        assert(vk_hud_override_blend(1u, &cm, 5u, 2u, 0u, &sa, &da, &ea) == 1);
        assert(cm == 0xfu && sa == 1u && da == 2u && ea == 1u);
        uint8_t glow[4] = {200u, 120u, 0u, 0u};
        vk_mm_copy_rgba(out, glow, 1u, 1u, VK_FORMAT_R8G8B8A8_UNORM, 2);
        assert(out[3] == 200u);
        cm = 0u; sa = 5u; da = 6u;
        assert(vk_hud_override_blend(1u, &cm, 5u, 6u, 1u, &sa, &da, &ea) == 0 && cm == 0u);
        cm = 0xfu;
        assert(vk_hud_override_blend(0u, &cm, 5u, 6u, 1u, &sa, &da, &ea) == 0 && sa == 5u);
        assert(vk_hud_override_blend(1u, &cm, 5u, 6u, 2u, &sa, &da, &ea) == 0 && sa == 5u);
        uint8_t srgb_frosted[4] = {109u, 109u, 109u, 38u};
        vk_mm_copy_rgba(out, srgb_frosted, 1u, 1u, VK_FORMAT_R8G8B8A8_SRGB, 2);
        assert(out[0] >= 37u && out[0] <= 39u && out[1] == out[0] && out[2] == out[0] && out[3] == 38u);
        uint8_t srgb_face[4] = {149u, 98u, 58u, 255u};
        vk_mm_copy_rgba(out, srgb_face, 1u, 1u, VK_FORMAT_R8G8B8A8_SRGB, 2);
        assert(out[0] == 149u && out[1] == 98u && out[2] == 58u && out[3] == 255u);
        vk_mm_copy_rgba(out, glow, 1u, 1u, VK_FORMAT_R8G8B8A8_SRGB, 2);
        assert(out[3] == 200u);
    }
    puts("vk_minimap_c_regression: PASS");
    return 0;
}
