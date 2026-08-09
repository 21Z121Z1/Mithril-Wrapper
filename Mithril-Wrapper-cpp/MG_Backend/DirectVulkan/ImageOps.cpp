// Mithril-Wrapper - MG_Backend/DirectVulkan/ImageOps.cpp
// Image-level operations that need their own command-buffer submission:
//   * backend_generate_mipmaps  — vkCmdBlitImage cascade across mip levels
//   * backend_read_pixels       — vkCmdCopyImage -> host-visible staging -> fence
//   * backend_blit_texture      — vkCmdBlitImage between two user textures
//
// These differ from the staging upload path in Resources.cpp because they
// cannot ride on the per-frame command buffer:
//   - glGenerateMipmap may be called outside a render pass and must complete
//     before the texture is sampled; recording onto the active command buffer
//     would delay the blits until eglSwapBuffers commits.
//   - glReadPixels must synchronously return host pixels, so it submits its
//     own one-shot command buffer and waits on a fence.
//   - glBlitFramebuffer between user FBOs similarly needs the blit to land
//     before subsequent draws read the destination.
//
// Each path allocates a transient VkCommandBuffer from the backend's pool,
// records + submits + waits on a dedicated fence, then frees the buffer.
#include "Device.h"
#include "Resources.h"
#include "../Backend.h"
#include "../../MG_State/State.h"
#include "../../MG_Impl/Log.h"

#include <cstring>
#include <vector>

namespace mithril {
namespace vk {

// host_texel_bytes is declared in Resources.h and defined in Resources.cpp.
// It is shared by the staging upload path and the readback path below.

namespace {

// One-shot command buffer + fence helper: records into a freshly allocated
// primary command buffer, submits it, waits for completion, and frees the
// buffer. The lambda returns false to abort the submit (e.g. recording error).
struct OneShotCtx {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    bool ok = false;
};

bool begin_one_shot(OneShotCtx& c) {
    Backend* b = backend();
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = b->commandPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(b->device, &ai, &c.cmd) != VK_SUCCESS) return false;

    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(b->device, &fi, nullptr, &c.fence) != VK_SUCCESS) {
        vkFreeCommandBuffers(b->device, b->commandPool, 1, &c.cmd);
        c.cmd = VK_NULL_HANDLE;
        return false;
    }

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(c.cmd, &bi) != VK_SUCCESS) return false;
    c.ok = true;
    return true;
}

void end_one_shot(OneShotCtx& c) {
    Backend* b = backend();
    if (!c.ok) {
        if (c.fence) { vkDestroyFence(b->device, c.fence, nullptr); c.fence = VK_NULL_HANDLE; }
        if (c.cmd)   { vkFreeCommandBuffers(b->device, b->commandPool, 1, &c.cmd); c.cmd = VK_NULL_HANDLE; }
        return;
    }
    vkEndCommandBuffer(c.cmd);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &c.cmd;
    vkQueueSubmit(b->graphicsQueue, 1, &si, c.fence);
    vkWaitForFences(b->device, 1, &c.fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(b->device, c.fence, nullptr);
    vkFreeCommandBuffers(b->device, b->commandPool, 1, &c.cmd);
    c.cmd = VK_NULL_HANDLE;
    c.fence = VK_NULL_HANDLE;
}

// aspect_for_format is declared in Resources.h and defined in Resources.cpp;
// we share the single canonical implementation across the backend.

} // namespace

void generate_mipmaps(GLuint name) {
    Backend* b = backend();
    if (!b->initialized || name == 0) return;
    auto& tbl = texture_table();
    auto it = tbl.find(name);
    if (it == tbl.end()) return;
    TextureEntry& tex = it->second;
    if (tex.image == VK_NULL_HANDLE || tex.levels <= 1) return;

    OneShotCtx c;
    if (!begin_one_shot(c)) {
        // FIX (日志刷屏): 限流 — deviceLost 时每张纹理的 mipmap 生成都会失败
        static int mipmapFailCount = 0;
        mipmapFailCount++;
        if (mipmapFailCount <= 3 || mipmapFailCount % 100 == 0) {
            MITHRIL_LOG_WARN("vk", "generate_mipmaps: failed to begin one-shot "
                              "cmd (fail #%d)", mipmapFailCount);
        }
        return;
    }

    const VkImageAspectFlags aspect = aspect_for_format(tex.format);

    // Transition level 0 from its current layout (likely SHADER_READ_ONLY or
    // TRANSFER_DST after the most recent upload) to TRANSFER_SRC_OPTIMAL.
    VkImageMemoryBarrier b0{};
    b0.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b0.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b0.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b0.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;  // discard current contents of level 0's *layout*
    b0.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b0.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b0.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b0.image = tex.image;
    b0.subresourceRange.aspectMask = aspect;
    b0.subresourceRange.baseMipLevel = 0;
    b0.subresourceRange.levelCount = 1;
    b0.subresourceRange.baseArrayLayer = 0;
    b0.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(c.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &b0);

    // For each level L >= 1: blit from L-1 (TRANSFER_SRC_OPTIMAL) into L
    // (TRANSFER_DST_OPTIMAL), then transition L to SHADER_READ_ONLY_OPTIMAL.
    for (int L = 1; L < tex.levels; ++L) {
        int32_t w = tex.width  >> L; if (w < 1) w = 1;
        int32_t h = tex.height >> L; if (h < 1) h = 1;

        // Transition level L (currently UNDEFINED) to TRANSFER_DST_OPTIMAL.
        VkImageMemoryBarrier toDst{};
        toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toDst.srcAccessMask = 0;
        toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.image = tex.image;
        toDst.subresourceRange.aspectMask = aspect;
        toDst.subresourceRange.baseMipLevel = (uint32_t)L;
        toDst.subresourceRange.levelCount = 1;
        toDst.subresourceRange.baseArrayLayer = 0;
        toDst.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(c.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &toDst);

        // Blit from level L-1 (TRANSFER_SRC_OPTIMAL) to level L.
        VkImageBlit blit{};
        blit.srcSubresource.aspectMask = aspect;
        blit.srcSubresource.mipLevel = (uint32_t)(L - 1);
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { tex.width >> (L - 1) > 0 ? (tex.width >> (L - 1)) : 1,
                               tex.height >> (L - 1) > 0 ? (tex.height >> (L - 1)) : 1, 1 };
        blit.dstSubresource.aspectMask = aspect;
        blit.dstSubresource.mipLevel = (uint32_t)L;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;
        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { w, h, 1 };

        VkFilter filt = (aspect == VK_IMAGE_ASPECT_COLOR_BIT) ? VK_FILTER_LINEAR
                                                              : VK_FILTER_NEAREST;
        vkCmdBlitImage(c.cmd,
                       tex.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &blit, filt);

        // Transition level L to TRANSFER_SRC_OPTIMAL for the next iteration
        // (or to SHADER_READ_ONLY_OPTIMAL on the last level — handled below).
        VkImageMemoryBarrier toSrc{};
        toSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toSrc.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toSrc.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toSrc.image = tex.image;
        toSrc.subresourceRange.aspectMask = aspect;
        toSrc.subresourceRange.baseMipLevel = (uint32_t)L;
        toSrc.subresourceRange.levelCount = 1;
        toSrc.subresourceRange.baseArrayLayer = 0;
        toSrc.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(c.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &toSrc);
    }

    // After the cascade, transition ALL levels to SHADER_READ_ONLY_OPTIMAL so
    // the texture can be sampled. (Levels 0..N-1 are all TRANSFER_SRC_OPTIMAL
    // at this point, which is not a sampling layout.)
    VkImageMemoryBarrier toShader{};
    toShader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toShader.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShader.image = tex.image;
    toShader.subresourceRange.aspectMask = aspect;
    toShader.subresourceRange.baseMipLevel = 0;
    toShader.subresourceRange.levelCount = (uint32_t)tex.levels;
    toShader.subresourceRange.baseArrayLayer = 0;
    toShader.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(c.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &toShader);

    end_one_shot(c);

    // All mip levels are now in SHADER_READ_ONLY_OPTIMAL. Update the tracked
    // layout so subsequent operations (uploads, blits, attachment uses) emit
    // the correct oldLayout in their memory barriers.
    tex.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

int read_pixels(int x, int y, int w, int h, GLenum format, GLenum type, void* out_pixels) {
    Backend* b = backend();
    if (!b->initialized || !out_pixels || w <= 0 || h <= 0) return 0;

    // Source: the current colour attachment installed on g_state. This covers
    // both FBO 0 (the EGL default framebuffer's swapchain image) and user
    // FBOs (their GL_COLOR_ATTACHMENT0 texture).
    VkImage src_image = VK_NULL_HANDLE;
    VkFormat src_fmt = VK_FORMAT_UNDEFINED;

    if (g_state->currentDrawFBO == 0) {
        // EGL default framebuffer: read directly from the swapchain image.
        // The EGL layer installs both the VkImageView and the underlying
        // VkImage + format on g_state when a surface is made current.
        src_image = g_state->eglDefaultColorImage;
        src_fmt   = g_state->eglDefaultColorFormat;
    } else {
        mithril::Framebuffer* fbo = mithril::state_get_framebuffer(g_state->currentDrawFBO);
        if (!fbo || !fbo->colors[0].texture) return 0;
        src_image = backend_get_texture_image(fbo->colors[0].texture);
        mithril::Texture* t = mithril::state_get_texture(fbo->colors[0].texture);
        if (t) src_fmt = gl_internal_to_vk((GLenum)t->internalFormat);
    }
    if (src_image == VK_NULL_HANDLE) return 0;

    // Flush any pending rendering into the colour attachment so the readback
    // sees the latest pixels.
    backend_end_render_pass();
    backend_commit();

    // Pick a host-visible staging buffer format. We always copy as RGBA8 on
    // the host side and convert to the requested (format,type) afterwards if
    // needed. MoltenVK supports VK_FORMAT_R8G8B8A8_UNORM as a transfer source
    // for any colour-attachable format.
    VkFormat staging_fmt = (src_fmt != VK_FORMAT_UNDEFINED) ? src_fmt : VK_FORMAT_R8G8B8A8_UNORM;
    int src_bpp = 4;
    switch (staging_fmt) {
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB: src_bpp = 4; break;
        case VK_FORMAT_R8G8B8_UNORM:  src_bpp = 3; break;
        case VK_FORMAT_R5G6B5_UNORM_PACK16: src_bpp = 2; break;
        default: src_bpp = 4; break;
    }
    VkDeviceSize staging_size = (VkDeviceSize)w * (VkDeviceSize)h * (VkDeviceSize)src_bpp;

    // Create a host-visible staging buffer big enough for the copy.
    BufferEntry staging{};
    if (!create_buffer(staging, staging_size,
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT, nullptr)) {
        return 0;
    }

    OneShotCtx c;
    if (!begin_one_shot(c)) {
        destroy_buffer_entry(staging);
        return 0;
    }

    // Transition the source image to TRANSFER_SRC_OPTIMAL.
    // NOTE: named `bar` (not `b`) to avoid clashing with the `Backend* b`
    // declared at the top of read_pixels(); the backend pointer is reused
    // below (b->device) after this barrier block.
    VkImageMemoryBarrier bar{};
    bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    bar.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    bar.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    bar.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.image = src_image;
    bar.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bar.subresourceRange.baseMipLevel = 0;
    bar.subresourceRange.levelCount = 1;
    bar.subresourceRange.baseArrayLayer = 0;
    bar.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(c.cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &bar);

    // GL's origin is bottom-left; Vulkan's is top-left. Flip the Y axis by
    // adjusting the source offset: read from (x, imageHeight - y - h).
    // We don't know imageHeight here without an extra query, so we copy the
    // whole width x h strip starting at the top of the image and let the
    // caller interpret the rows. For the common MC Java case (reading the full
    // framebuffer) this is correct because y==0 and h==imageHeight.
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { x, y, 0 };
    region.imageExtent = { (uint32_t)w, (uint32_t)h, 1 };
    vkCmdCopyImageToBuffer(c.cmd, src_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           staging.buffer, 1, &region);

    // Transition the source image back to COLOR_ATTACHMENT_OPTIMAL so
    // subsequent draws can render into it again.
    bar.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    bar.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    bar.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    bar.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    vkCmdPipelineBarrier(c.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &bar);

    end_one_shot(c);

    // Map the staging buffer and convert/copy into out_pixels.
    void* mapped = nullptr;
    vkMapMemory(b->device, staging.memory, 0, staging_size, 0, &mapped);
    if (!mapped) {
        destroy_buffer_entry(staging);
        return 0;
    }

    int dst_bpp = host_texel_bytes(format, type);
    if (dst_bpp <= 0) dst_bpp = 4;

    if (src_bpp == dst_bpp) {
        std::memcpy(out_pixels, mapped, (size_t)staging_size);
    } else {
        // Best-effort: copy byte-for-byte up to the smaller of the two sizes.
        size_t n = std::min((size_t)staging_size, (size_t)w * h * dst_bpp);
        std::memcpy(out_pixels, mapped, n);
    }

    vkUnmapMemory(b->device, staging.memory);
    destroy_buffer_entry(staging);
    return 1;
}

void blit_texture(GLuint src_name, GLuint dst_name,
                  int srcX0, int srcY0, int srcX1, int srcY1,
                  int dstX0, int dstY0, int dstX1, int dstY1,
                  GLbitfield mask, GLenum filter) {
    Backend* b = backend();
    if (!b->initialized) return;
    auto& tbl = texture_table();
    auto sit = tbl.find(src_name);
    auto dit = tbl.find(dst_name);
    if (sit == tbl.end() || dit == tbl.end()) return;
    TextureEntry& src = sit->second;
    TextureEntry& dst = dit->second;
    if (src.image == VK_NULL_HANDLE || dst.image == VK_NULL_HANDLE) return;

    // Only colour-buffer blits are implemented; depth/stencil blits would
    // need VK_IMAGE_ASPECT_DEPTH_BIT and a NEAREST filter (Vulkan forbids
    // linear filtering on depth formats).
    if (!(mask & GL_COLOR_BUFFER_BIT)) return;

    OneShotCtx c;
    if (!begin_one_shot(c)) return;

    VkImageAspectFlags srcAspect = aspect_for_format(src.format);
    VkImageAspectFlags dstAspect = aspect_for_format(dst.format);

    // Transition source to TRANSFER_SRC_OPTIMAL.
    VkImageMemoryBarrier sb{};
    sb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    sb.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    sb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    sb.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    sb.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    sb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    sb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    sb.image = src.image;
    sb.subresourceRange.aspectMask = srcAspect;
    sb.subresourceRange.baseMipLevel = 0;
    sb.subresourceRange.levelCount = (uint32_t)src.levels;
    sb.subresourceRange.baseArrayLayer = 0;
    sb.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(c.cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &sb);

    // Transition destination to TRANSFER_DST_OPTIMAL.
    VkImageMemoryBarrier db{};
    db.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    db.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    db.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    db.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    db.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    db.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    db.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    db.image = dst.image;
    db.subresourceRange.aspectMask = dstAspect;
    db.subresourceRange.baseMipLevel = 0;
    db.subresourceRange.levelCount = (uint32_t)dst.levels;
    db.subresourceRange.baseArrayLayer = 0;
    db.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(c.cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &db);

    VkImageBlit blit{};
    blit.srcSubresource.aspectMask = srcAspect;
    blit.srcSubresource.mipLevel = 0;
    blit.srcSubresource.baseArrayLayer = 0;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[0] = { srcX0, srcY0, 0 };
    blit.srcOffsets[1] = { srcX1, srcY1, 1 };
    blit.dstSubresource.aspectMask = dstAspect;
    blit.dstSubresource.mipLevel = 0;
    blit.dstSubresource.baseArrayLayer = 0;
    blit.dstSubresource.layerCount = 1;
    blit.dstOffsets[0] = { dstX0, dstY0, 0 };
    blit.dstOffsets[1] = { dstX1, dstY1, 1 };

    VkFilter filt = (filter == GL_LINEAR) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    vkCmdBlitImage(c.cmd,
                   src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &blit, filt);

    // Transition both images back to SHADER_READ_ONLY_OPTIMAL.
    sb.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    sb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    sb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    sb.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(c.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &sb);

    db.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    db.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    db.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    db.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(c.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &db);

    end_one_shot(c);

    // Both images are back in SHADER_READ_ONLY_OPTIMAL after the blit.
    src.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    dst.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

/*
 * Image-level blit between two raw VkImage handles. Used by glBlitFramebuffer
 * when one or both sides is the EGL default framebuffer (whose swapchain
 * image is not tracked in the GL texture table). The caller specifies the
 * initial and final layouts of each image so this helper can emit correct
 * memory barriers without needing to know whether each image is a swapchain
 * drawable (COLOR_ATTACHMENT_OPTIMAL) or a user texture (SHADER_READ_ONLY).
 */
void blit_images_impl(VkImage src_image, VkFormat src_format,
                      VkImageLayout src_initial, VkImageLayout src_final,
                      VkImage dst_image, VkFormat dst_format,
                      VkImageLayout dst_initial, VkImageLayout dst_final,
                      int srcX0, int srcY0, int srcX1, int srcY1,
                      int dstX0, int dstY0, int dstX1, int dstY1,
                      GLbitfield mask, GLenum filter,
                      bool is_dst_default_fbo, int dst_height) {
    Backend* b = backend();
    if (!b->initialized) return;
    if (src_image == VK_NULL_HANDLE || dst_image == VK_NULL_HANDLE) return;
    // Only colour-buffer blits are implemented; depth/stencil blits would
    // need VK_IMAGE_ASPECT_DEPTH_BIT and a NEAREST filter (Vulkan forbids
    // linear filtering on depth formats).
    if (!(mask & GL_COLOR_BUFFER_BIT)) return;

    // Y-flip the destination rectangle when blitting TO the EGL default
    // framebuffer (swapchain drawable). The draw path flips vertex Y in the
    // shader for default-FBO rendering (so on-screen content is in Vulkan's
    // top-left orientation), but blits bypass the vertex shader. GL's blit
    // coords are bottom-left origin; Vulkan's VkImageBlit offsets are top-left
    // origin. Without this flip, blits to the default framebuffer produce an
    // upside-down image (black screen / misaligned content).
    //
    // Deep reference: MobileGL ApplyNativeBlitDefaultFramebufferTransform
    // (VulkanRenderer.cpp:1650-1665, identity branch):
    //   blitRegion.dstOffsets[0].y = extent.y - blitRegion.dstOffsets[0].y;
    //   blitRegion.dstOffsets[1].y = extent.y - blitRegion.dstOffsets[1].y;
    //
    // Source Y is NOT flipped (MobileGL never flips src Y). The source
    // content's orientation is determined by the draw path: user FBO textures
    // are in GL orientation (GL bottom at Vulkan top), so GL src coords map
    // directly; default FBO content is in Vulkan orientation, but GL src coords
    // reading from it still produce correct results because the content
    // orientation and coordinate mapping are consistent within the image.
    if (is_dst_default_fbo && dst_height > 0) {
        dstY0 = dst_height - dstY0;
        dstY1 = dst_height - dstY1;
    }

    OneShotCtx c;
    if (!begin_one_shot(c)) return;

    VkImageAspectFlags srcAspect = aspect_for_format(src_format);
    VkImageAspectFlags dstAspect = aspect_for_format(dst_format);

    // Transition source to TRANSFER_SRC_OPTIMAL.
    VkImageMemoryBarrier sb{};
    sb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    sb.srcAccessMask = (src_initial == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                        ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                        : VK_ACCESS_SHADER_READ_BIT;
    sb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    sb.oldLayout = src_initial;
    sb.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    sb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    sb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    sb.image = src_image;
    sb.subresourceRange.aspectMask = srcAspect;
    sb.subresourceRange.baseMipLevel = 0;
    sb.subresourceRange.levelCount = 1;
    sb.subresourceRange.baseArrayLayer = 0;
    sb.subresourceRange.layerCount = 1;
    VkPipelineStageFlags srcStage = (src_initial == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                        ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                        : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    vkCmdPipelineBarrier(c.cmd, srcStage,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &sb);

    // Transition destination to TRANSFER_DST_OPTIMAL.
    VkImageMemoryBarrier db{};
    db.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    db.srcAccessMask = (dst_initial == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                        ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                        : VK_ACCESS_SHADER_READ_BIT;
    db.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    db.oldLayout = dst_initial;
    db.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    db.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    db.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    db.image = dst_image;
    db.subresourceRange.aspectMask = dstAspect;
    db.subresourceRange.baseMipLevel = 0;
    db.subresourceRange.levelCount = 1;
    db.subresourceRange.baseArrayLayer = 0;
    db.subresourceRange.layerCount = 1;
    VkPipelineStageFlags dstStage = (dst_initial == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                        ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                        : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    vkCmdPipelineBarrier(c.cmd, dstStage,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &db);

    VkImageBlit blit{};
    blit.srcSubresource.aspectMask = srcAspect;
    blit.srcSubresource.mipLevel = 0;
    blit.srcSubresource.baseArrayLayer = 0;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[0] = { srcX0, srcY0, 0 };
    blit.srcOffsets[1] = { srcX1, srcY1, 1 };
    blit.dstSubresource.aspectMask = dstAspect;
    blit.dstSubresource.mipLevel = 0;
    blit.dstSubresource.baseArrayLayer = 0;
    blit.dstSubresource.layerCount = 1;
    blit.dstOffsets[0] = { dstX0, dstY0, 0 };
    blit.dstOffsets[1] = { dstX1, dstY1, 1 };

    VkFilter filt = (filter == GL_LINEAR) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    vkCmdBlitImage(c.cmd,
                   src_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   dst_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &blit, filt);

    // Transition both images back to their final layouts.
    sb.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    sb.dstAccessMask = (src_final == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                        ? (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)
                        : VK_ACCESS_SHADER_READ_BIT;
    sb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    sb.newLayout = src_final;
    VkPipelineStageFlags srcFinalStage = (src_final == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                        ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                        : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    vkCmdPipelineBarrier(c.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         srcFinalStage, 0,
                         0, nullptr, 0, nullptr, 1, &sb);

    db.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    db.dstAccessMask = (dst_final == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                        ? (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)
                        : VK_ACCESS_SHADER_READ_BIT;
    db.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    db.newLayout = dst_final;
    VkPipelineStageFlags dstFinalStage = (dst_final == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                        ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                        : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    vkCmdPipelineBarrier(c.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         dstFinalStage, 0,
                         0, nullptr, 0, nullptr, 1, &db);

    end_one_shot(c);
}

} // namespace vk
} // namespace mithril

// ===========================================================================
// Public C API wrappers (declared in MG_Backend/Backend.h)
// ===========================================================================
extern "C" {

void backend_generate_mipmaps(GLuint name) {
    mithril::vk::generate_mipmaps(name);
}

int backend_read_pixels(int x, int y, int w, int h,
                        GLenum format, GLenum type, void* out_pixels) {
    return mithril::vk::read_pixels(x, y, w, h, format, type, out_pixels);
}

void backend_blit_texture(GLuint src_name, GLuint dst_name,
                          int srcX0, int srcY0, int srcX1, int srcY1,
                          int dstX0, int dstY0, int dstX1, int dstY1,
                          GLbitfield mask, GLenum filter) {
    mithril::vk::blit_texture(src_name, dst_name,
                              srcX0, srcY0, srcX1, srcY1,
                              dstX0, dstY0, dstX1, dstY1,
                              mask, filter);
}

void backend_blit_images(VkImage src_image, VkFormat src_format,
                         VkImage dst_image, VkFormat dst_format,
                         int srcX0, int srcY0, int srcX1, int srcY1,
                         int dstX0, int dstY0, int dstX1, int dstY1,
                         GLbitfield mask, GLenum filter,
                         int is_dst_default_fbo, int dst_height) {
    // Both images are assumed to be in a sampling or attachment layout before
    // the blit. The swapchain color image is in COLOR_ATTACHMENT_OPTIMAL
    // (it was just rendered into, or will be rendered into next frame); user
    // FBO textures are in SHADER_READ_ONLY_OPTIMAL (after an upload or a
    // previous blit). We transition them back to those same layouts after the
    // blit so subsequent rendering / sampling continues to work.
    //
    // Heuristic: if the format is a color format (not depth/stencil), assume
    // COLOR_ATTACHMENT_OPTIMAL for the initial/final layout. This matches the
    // common glBlitFramebuffer case (blitting between render targets that are
    // actively being rendered into). For depth/stencil formats we would use
    // DEPTH_STENCIL_ATTACHMENT_OPTIMAL, but depth blits are not yet supported.
    VkImageLayout src_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkImageLayout dst_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    mithril::vk::blit_images_impl(src_image, src_format, src_layout, src_layout,
                                  dst_image, dst_format, dst_layout, dst_layout,
                                  srcX0, srcY0, srcX1, srcY1,
                                  dstX0, dstY0, dstX1, dstY1,
                                  mask, filter,
                                  is_dst_default_fbo != 0, dst_height);
}

} // extern "C"
