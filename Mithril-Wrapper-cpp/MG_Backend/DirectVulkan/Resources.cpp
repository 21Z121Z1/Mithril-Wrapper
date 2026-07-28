// Mithril-Wrapper - MG_Backend/DirectVulkan/Resources.cpp
// VkBuffer / VkImage / VkImageView / VkSampler lifecycle + GL internalFormat
// -> VkFormat mapping + staging upload. Implements the backend_get_or_create_*
// family declared in MG_Backend/Backend.h.
#include "Resources.h"
#include "Device.h"
#include "../Backend.h"
#include "../../MG_Impl/Log.h"

#include <cstring>
#include <vector>

namespace mithril {
namespace vk {

std::unordered_map<GLuint, BufferEntry>&  buffer_table()  { static std::unordered_map<GLuint, BufferEntry>  t; return t; }
std::unordered_map<GLuint, TextureEntry>& texture_table() { static std::unordered_map<GLuint, TextureEntry> t; return t; }
std::unordered_map<GLuint, SamplerEntry>& sampler_table() { static std::unordered_map<GLuint, SamplerEntry> t; return t; }

uint32_t find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags props) {
    Backend* b = backend();
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(b->physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    return 0xFFFFFFFFu;
}

bool create_buffer(BufferEntry& out, VkDeviceSize size,
                   VkBufferUsageFlags usage, const void* data) {
    Backend* b = backend();
    VkBufferCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size = size;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(b->device, &ci, nullptr, &out.buffer) != VK_SUCCESS) return false;

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(b->device, out.buffer, &req);
    uint32_t mt = find_memory_type(req.memoryTypeBits,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt == 0xFFFFFFFFu) { vkDestroyBuffer(b->device, out.buffer, nullptr); out.buffer = VK_NULL_HANDLE; return false; }
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = mt;
    if (vkAllocateMemory(b->device, &ai, nullptr, &out.memory) != VK_SUCCESS) {
        vkDestroyBuffer(b->device, out.buffer, nullptr);
        out.buffer = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(b->device, out.buffer, out.memory, 0);
    if (data) {
        void* dst = nullptr;
        vkMapMemory(b->device, out.memory, 0, size, 0, &dst);
        if (dst) { std::memcpy(dst, data, (size_t)size); vkUnmapMemory(b->device, out.memory); }
    }
    out.size = size;
    return true;
}

void destroy_buffer_entry(BufferEntry& e) {
    Backend* b = backend();
    if (!b->device) return;
    // Unmap immediately — mappings cannot outlive the frame in MoltenVK.
    if (e.mapped) { vkUnmapMemory(b->device, e.memory); e.mapped = nullptr; }
    if (e.buffer == VK_NULL_HANDLE && e.memory == VK_NULL_HANDLE) {
        e.size = 0;
        return;
    }
    // Defer actual vkDestroyBuffer / vkFreeMemory until the next successful
    // frame boundary (commit_frame success path). The current command buffer
    // may still reference this buffer; destroying it now would cause
    // kIOGPUCommandBufferCallbackErrorInvalidResource at submit time.
    b->pendingBufferReleases.push_back(std::move(e));
    e = BufferEntry{};
}

void destroy_texture_entry(TextureEntry& e) {
    Backend* b = backend();
    if (!b->device) return;
    if (e.image == VK_NULL_HANDLE && e.memory == VK_NULL_HANDLE && e.view == VK_NULL_HANDLE &&
        e.stagingBuffer == VK_NULL_HANDLE && e.stagingMemory == VK_NULL_HANDLE) {
        return;
    }
    // Defer actual vkDestroy* / vkFreeMemory until the next successful frame
    // boundary. The current command buffer may still reference this image
    // (e.g. as a shader sampling target); destroying it now would cause
    // kIOGPUCommandBufferCallbackErrorInvalidResource at submit time.
    b->pendingTextureReleases.push_back(std::move(e));
    e = TextureEntry{};
}

// ---- GL internalFormat -> VkFormat / host texel bytes / aspect mask ----
// (Moved to FormatMap.{h,cpp} so they can be unit-tested without linking the
// rest of the Vulkan backend. See FormatMap.h for the declarations.)

void stage_and_copy_image(TextureEntry& tex, int level, int x, int y, int z,
                          int w, int h, int d, const void* pixels,
                          int unpack_alignment, GLenum format, GLenum type) {
    Backend* b = backend();
    if (!b->commandBuffer) return;
    if (unpack_alignment <= 0) unpack_alignment = 4;  // GL default UNPACK_ALIGNMENT

    // Compute host-side bytes per pixel for this (format, type) pair and
    // honour GL_UNPACK_ALIGNMENT when computing the source row stride. The
    // staging buffer is repacked to be tightly packed, matching
    // VkBufferImageCopy.bufferRowLength == 0 below.
    int bpp = host_texel_bytes(format, type);
    if (bpp <= 0) bpp = 4;  // conservative fallback
    size_t tight_row = (size_t)w * (size_t)bpp;
    size_t mask = (size_t)unpack_alignment - 1;
    size_t src_stride = (tight_row + mask) & ~mask;
    size_t staging = tight_row * (size_t)h * (size_t)d;

    if (tex.stagingSize < staging) {
        if (tex.stagingBuffer) { vkDestroyBuffer(b->device, tex.stagingBuffer, nullptr); tex.stagingBuffer = VK_NULL_HANDLE; }
        if (tex.stagingMemory) { vkFreeMemory(b->device, tex.stagingMemory, nullptr); tex.stagingMemory = VK_NULL_HANDLE; }
        BufferEntry tmp;
        if (create_buffer(tmp, staging,
                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT, nullptr)) {
            tex.stagingBuffer = tmp.buffer;
            tex.stagingMemory = tmp.memory;
            tex.stagingSize = staging;
        } else {
            return;
        }
    }
    void* dst = nullptr;
    vkMapMemory(b->device, tex.stagingMemory, 0, staging, 0, &dst);
    if (dst && pixels) {
        if (src_stride == tight_row) {
            // Source rows are already tightly packed — single memcpy.
            std::memcpy(dst, pixels, staging);
        } else {
            // Source rows carry GL_UNPACK_ALIGNMENT padding; repack to tight
            // so VkBufferImageCopy.bufferRowLength == 0 (== w) is valid.
            char* d8 = (char*)dst;
            const char* s8 = (const char*)pixels;
            for (int layer = 0; layer < d; ++layer) {
                for (int row = 0; row < h; ++row) {
                    std::memcpy(d8, s8, tight_row);
                    d8 += tight_row;
                    s8 += src_stride;
                }
            }
        }
    }
    if (dst) vkUnmapMemory(b->device, tex.stagingMemory);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;     // tightly packed
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    if (tex.format == VK_FORMAT_D16_UNORM || tex.format == VK_FORMAT_D32_SFLOAT)
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (tex.format == VK_FORMAT_S8_UINT)
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
    if (tex.format == VK_FORMAT_D24_UNORM_S8_UINT || tex.format == VK_FORMAT_D32_SFLOAT_S8_UINT)
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;  // depth aspect only
    region.imageSubresource.mipLevel = level;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { x, y, z };
    region.imageExtent = { (uint32_t)w, (uint32_t)h, (uint32_t)d };

    // Transition the image layout to TRANSFER_DST for the copy.
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = tex.image;
    barrier.subresourceRange.aspectMask = region.imageSubresource.aspectMask;
    barrier.subresourceRange.baseMipLevel = level;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(b->commandBuffer,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);

    vkCmdCopyBufferToImage(b->commandBuffer, tex.stagingBuffer, tex.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition to SHADER_READ_ONLY so the texture can be sampled afterwards.
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkPipelineStageFlagBits dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    vkCmdPipelineBarrier(b->commandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         dstStage, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);
    tex.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

// aspect_for_format() moved to FormatMap.{h,cpp} (pure-logic helper).

// Stage masks for the source side of an image-memory barrier, keyed on the
// old layout. Returns 0 when the old layout is UNDEFINED or PREINITIALIZED
// (no prior work needs to be visible).
static VkAccessFlags src_access_for_layout(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VK_ACCESS_SHADER_READ_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return VK_ACCESS_TRANSFER_READ_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return VK_ACCESS_TRANSFER_WRITE_BIT;
        case VK_IMAGE_LAYOUT_GENERAL:
            return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        default:
            return 0;  // UNDEFINED / PREINITIALIZED
    }
}

// Stage masks for the destination side of an image-memory barrier, keyed on
// the new layout. Returns 0 when the new layout is PREINITIALIZED (invalid).
static VkAccessFlags dst_access_for_layout(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VK_ACCESS_SHADER_READ_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return VK_ACCESS_TRANSFER_READ_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return VK_ACCESS_TRANSFER_WRITE_BIT;
        case VK_IMAGE_LAYOUT_GENERAL:
            return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        default:
            return 0;
    }
}

// Source pipeline stage for an image-memory barrier, keyed on the old layout.
static VkPipelineStageFlags src_stage_for_layout(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            return VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return VK_PIPELINE_STAGE_TRANSFER_BIT;
        default:
            return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
}

// Destination pipeline stage for an image-memory barrier, keyed on the new layout.
static VkPipelineStageFlags dst_stage_for_layout(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return VK_PIPELINE_STAGE_TRANSFER_BIT;
        default:
            return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }
}

void transition_image_layout(TextureEntry& tex, VkImageLayout newLayout) {
    Backend* b = backend();
    if (!b->commandBuffer || tex.image == VK_NULL_HANDLE) return;
    if (tex.currentLayout == newLayout) return;  // already there

    VkImageAspectFlags aspect = aspect_for_format(tex.format);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = src_access_for_layout(tex.currentLayout);
    barrier.dstAccessMask = dst_access_for_layout(newLayout);
    barrier.oldLayout = tex.currentLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = tex.image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = (uint32_t)tex.levels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(b->commandBuffer,
                         src_stage_for_layout(tex.currentLayout),
                         dst_stage_for_layout(newLayout),
                         0,
                         0, nullptr, 0, nullptr, 1, &barrier);
    tex.currentLayout = newLayout;
}

// ---- GL filter/wrap -> VkFilter / VkSamplerAddressMode ----
static VkFilter to_vk_filter(GLenum f) {
    if (f == GL_NEAREST || f == GL_NEAREST_MIPMAP_NEAREST || f == GL_NEAREST_MIPMAP_LINEAR) return VK_FILTER_NEAREST;
    return VK_FILTER_LINEAR;
}
static VkSamplerMipmapMode to_vk_mipmap(GLenum f) {
    if (f == GL_NEAREST_MIPMAP_NEAREST || f == GL_LINEAR_MIPMAP_NEAREST) return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    return VK_SAMPLER_MIPMAP_MODE_LINEAR;
}
static VkSamplerAddressMode to_vk_wrap(GLenum w) {
    switch (w) {
        case GL_CLAMP_TO_EDGE:        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case GL_CLAMP_TO_BORDER:      return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        case GL_MIRRORED_REPEAT:      return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case GL_REPEAT:
        default:                      return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

} // namespace vk
} // namespace mithril

// ===========================================================================
// Public C API (declared in MG_Backend/Backend.h). These thin wrappers map GL
// names to the vk::* tables above and create/destroy Vulkan resources.
// ===========================================================================
extern "C" {

VkBuffer backend_get_or_create_buffer(GLuint name, const void* data, size_t size) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->initialized || name == 0 || size == 0) return VK_NULL_HANDLE;
    auto& tbl = mithril::vk::buffer_table();
    auto it = tbl.find(name);
    if (it != tbl.end() && it->second.size >= (VkDeviceSize)size && !data) {
        return it->second.buffer;
    }
    if (it != tbl.end()) mithril::vk::destroy_buffer_entry(it->second);
    mithril::vk::BufferEntry e;
    if (!mithril::vk::create_buffer(e, size,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            data)) {
        return VK_NULL_HANDLE;
    }
    tbl[name] = e;
    return e.buffer;
}

void backend_buffer_upload(GLuint name, GLintptr offset, const void* data, size_t size) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->initialized) return;
    auto& tbl = mithril::vk::buffer_table();
    auto it = tbl.find(name);
    if (it == tbl.end()) return;
    void* dst = nullptr;
    vkMapMemory(b->device, it->second.memory, offset, size, 0, &dst);
    if (dst) { std::memcpy(dst, data, size); vkUnmapMemory(b->device, it->second.memory); }
}

VkBuffer backend_get_buffer(GLuint name) {
    auto& tbl = mithril::vk::buffer_table();
    auto it = tbl.find(name);
    return it == tbl.end() ? VK_NULL_HANDLE : it->second.buffer;
}

void backend_delete_buffer(GLuint name) {
    auto& tbl = mithril::vk::buffer_table();
    auto it = tbl.find(name);
    if (it == tbl.end()) return;
    mithril::vk::destroy_buffer_entry(it->second);
    tbl.erase(it);
}

VkBuffer backend_get_zero_buffer(void) {
    static GLuint zero_name = 0x40000000u;  // sentinel name for the shared zero buffer
    static bool tried = false;
    if (!tried) {
        tried = true;
        static const uint8_t zeros[16] = {0};
        backend_get_or_create_buffer(zero_name, zeros, sizeof(zeros));
    }
    return backend_get_buffer(zero_name);
}

VkImage backend_get_or_create_texture(GLuint name, int width, int height, int depth,
                                      int levels, GLenum internal_format, GLenum target,
                                      int samples) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->initialized || name == 0 || width <= 0 || height <= 0) return VK_NULL_HANDLE;
    VkFormat fmt = mithril::vk::gl_internal_to_vk(internal_format);
    if (fmt == VK_FORMAT_UNDEFINED) {
        MITHRIL_LOG_WARN("vk", "backend_get_or_create_texture: unsupported internalFormat 0x%x", internal_format);
        fmt = VK_FORMAT_R8G8B8A8_UNORM;
    }
    auto& tbl = mithril::vk::texture_table();
    auto it = tbl.find(name);
    if (it != tbl.end() && it->second.image != VK_NULL_HANDLE &&
        it->second.format == fmt &&
        it->second.width == width && it->second.height == height &&
        it->second.depth == depth) {
        return it->second.image;
    }
    if (it != tbl.end()) mithril::vk::destroy_texture_entry(it->second);

    mithril::vk::TextureEntry e;
    e.format = fmt;
    e.width = width; e.height = height; e.depth = depth;
    e.levels = levels > 0 ? levels : 1;
    e.target = target;

    VkImageType imgType = (target == GL_TEXTURE_3D) ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = imgType;
    ici.format = fmt;
    ici.extent = { (uint32_t)width, (uint32_t)height, (uint32_t)(imgType == VK_IMAGE_TYPE_3D ? depth : 1) };
    ici.mipLevels = e.levels;
    ici.arrayLayers = (imgType == VK_IMAGE_TYPE_3D) ? 1 : (target == GL_TEXTURE_CUBE_MAP ? 6 : 1);
    ici.samples = (VkSampleCountFlagBits)(samples > 1 ? samples : 1);
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    // Depth/stencil attachments also need to be renderable.
    if (fmt == VK_FORMAT_D16_UNORM || fmt == VK_FORMAT_D32_SFLOAT ||
        fmt == VK_FORMAT_D24_UNORM_S8_UINT || fmt == VK_FORMAT_D32_SFLOAT_S8_UINT) {
        ici.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(b->device, &ici, nullptr, &e.image) != VK_SUCCESS) return VK_NULL_HANDLE;

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(b->device, e.image, &req);
    uint32_t mt = mithril::vk::find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mt == 0xFFFFFFFFu) mt = mithril::vk::find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = mt;
    if (vkAllocateMemory(b->device, &ai, nullptr, &e.memory) != VK_SUCCESS) {
        vkDestroyImage(b->device, e.image, nullptr);
        return VK_NULL_HANDLE;
    }
    vkBindImageMemory(b->device, e.image, e.memory, 0);

    VkImageViewCreateInfo vci{};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = e.image;
    vci.viewType = (target == GL_TEXTURE_3D) ? VK_IMAGE_VIEW_TYPE_3D :
                   (target == GL_TEXTURE_CUBE_MAP ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D);
    vci.format = fmt;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    if (fmt == VK_FORMAT_D16_UNORM || fmt == VK_FORMAT_D32_SFLOAT)
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    else if (fmt == VK_FORMAT_D24_UNORM_S8_UINT || fmt == VK_FORMAT_D32_SFLOAT_S8_UINT)
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    else if (fmt == VK_FORMAT_S8_UINT)
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
    vci.subresourceRange.baseMipLevel = 0;
    vci.subresourceRange.levelCount = e.levels;
    vci.subresourceRange.baseArrayLayer = 0;
    vci.subresourceRange.layerCount = ici.arrayLayers;
    vci.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
    vkCreateImageView(b->device, &vci, nullptr, &e.view);

    tbl[name] = e;
    return e.image;
}

void backend_texture_upload(GLuint name, int level, int x, int y, int z,
                            int w, int h, int d, GLenum format, GLenum type,
                            const void* pixels, int unpack_alignment) {
    auto& tbl = mithril::vk::texture_table();
    auto it = tbl.find(name);
    if (it == tbl.end() || !pixels) return;
    mithril::vk::stage_and_copy_image(it->second, level, x, y, z, w, h, d,
                                      pixels, unpack_alignment, format, type);
}

void backend_texture_set_params(GLuint name, GLint min_filter, GLint mag_filter,
                                GLint wrap_s, GLint wrap_t, GLint wrap_r,
                                const float* border_color) {
    // Vulkan samplers are immutable; params are applied when the sampler is
    // fetched via backend_get_or_create_sampler (which caches per (name,param)).
    // Record nothing here — the sampler table is keyed by name and rebuilt on
    // demand. (See backend_get_or_create_sampler.)
    (void)name; (void)min_filter; (void)mag_filter;
    (void)wrap_s; (void)wrap_t; (void)wrap_r; (void)border_color;
}

VkImageView backend_get_texture_view(GLuint name) {
    auto& tbl = mithril::vk::texture_table();
    auto it = tbl.find(name);
    return it == tbl.end() ? VK_NULL_HANDLE : it->second.view;
}

VkImage backend_get_texture_image(GLuint name) {
    auto& tbl = mithril::vk::texture_table();
    auto it = tbl.find(name);
    return it == tbl.end() ? VK_NULL_HANDLE : it->second.image;
}

void backend_delete_texture(GLuint name) {
    auto& tbl = mithril::vk::texture_table();
    auto it = tbl.find(name);
    if (it == tbl.end()) return;
    mithril::vk::destroy_texture_entry(it->second);
    tbl.erase(it);
}

void backend_transition_texture_layout(GLuint name, VkImageLayout target_layout) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->initialized) return;
    auto& tbl = mithril::vk::texture_table();
    auto it = tbl.find(name);
    if (it == tbl.end()) return;
    mithril::vk::transition_image_layout(it->second, target_layout);
}

VkSampler backend_get_or_create_sampler(GLuint name, GLint min_filter, GLint mag_filter,
                                        GLint wrap_s, GLint wrap_t, GLint wrap_r,
                                        const float* border_color) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->initialized) return VK_NULL_HANDLE;
    auto& tbl = mithril::vk::sampler_table();
    auto it = tbl.find(name);
    if (it != tbl.end() && it->second.sampler != VK_NULL_HANDLE) return it->second.sampler;

    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = mithril::vk::to_vk_filter(mag_filter);
    sci.minFilter = mithril::vk::to_vk_filter(min_filter);
    sci.mipmapMode = mithril::vk::to_vk_mipmap(min_filter);
    sci.addressModeU = mithril::vk::to_vk_wrap(wrap_s);
    sci.addressModeV = mithril::vk::to_vk_wrap(wrap_t);
    sci.addressModeW = mithril::vk::to_vk_wrap(wrap_r);
    sci.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    if (border_color) {
        // Approximate border colour; only black/white matter for MoltenVK.
        bool white = border_color[0] >= 1.0f && border_color[1] >= 1.0f &&
                     border_color[2] >= 1.0f && border_color[3] >= 1.0f;
        sci.borderColor = white ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE
                                : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    }
    sci.anisotropyEnable = VK_FALSE;
    sci.maxAnisotropy = 1.0f;
    sci.compareEnable = VK_FALSE;
    sci.compareOp = VK_COMPARE_OP_ALWAYS;
    sci.minLod = 0.0f;
    sci.maxLod = 12.0f;
    sci.unnormalizedCoordinates = VK_FALSE;

    mithril::vk::SamplerEntry e;
    if (vkCreateSampler(b->device, &sci, nullptr, &e.sampler) != VK_SUCCESS) return VK_NULL_HANDLE;
    tbl[name] = e;
    return e.sampler;
}

VkFormat backend_vk_format_for_gl(GLenum internal_format) {
    return mithril::vk::gl_internal_to_vk(internal_format);
}

} // extern "C"
