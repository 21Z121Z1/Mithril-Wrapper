// Mithril-Wrapper - MG_Backend/DirectVulkan/Resources.h
// VkBuffer / VkImage / VkImageView / VkSampler management keyed by GL name.
// Also: GL internalFormat -> VkFormat mapping + staging upload path.
#ifndef MITHRIL_DIRECTVULKAN_RESOURCES_H
#define MITHRIL_DIRECTVULKAN_RESOURCES_H

#include <vulkan/vulkan.h>
#include <GL/gl.h>

#include <unordered_map>

namespace mithril {
namespace vk {

struct BufferEntry {
    VkBuffer       buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize   size = 0;
    void*          mapped = nullptr;   // host pointer if host-visible (persistently mapped)
};

struct TextureEntry {
    VkImage        image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView    view = VK_NULL_HANDLE;
    VkFormat       format = VK_FORMAT_UNDEFINED;
    int            width = 0;
    int            height = 0;
    int            depth = 1;
    int            levels = 1;
    GLenum         target = GL_TEXTURE_2D;
    // Current layout of the image subresource. Tracked across uploads, blits,
    // and render-pass attachment uses so we can emit valid memory barriers
    // (the dynamic-rendering API does NOT automatically transition images
    // between layouts — it only ensures the layout matches `imageLayout`
    // during the pass). New textures start in UNDEFINED.
    VkImageLayout  currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // Staging buffer used for the most recent upload (kept alive to avoid
    // per-texel allocation churn; recreated if too small).
    VkBuffer       stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    VkDeviceSize   stagingSize = 0;
};

struct SamplerEntry {
    VkSampler sampler = VK_NULL_HANDLE;
};

// Per-backend resource tables. Singleton accessors.
std::unordered_map<GLuint, BufferEntry>&  buffer_table();
std::unordered_map<GLuint, TextureEntry>& texture_table();
std::unordered_map<GLuint, SamplerEntry>& sampler_table();

// Find a memory type matching `requirements` and the desired property flags.
uint32_t find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags props);

// Create + bind a VkBuffer (host-visible/coherent) of the given size. On
// success fills out the entry. `data` (if non-null) is copied in.
bool create_buffer(BufferEntry& out, VkDeviceSize size,
                   VkBufferUsageFlags usage, const void* data);

// Destroy a buffer entry's Vulkan resources (does not erase the table slot).
void destroy_buffer_entry(BufferEntry& e);
// Destroy a texture entry's Vulkan resources (does not erase the table slot).
void destroy_texture_entry(TextureEntry& e);

// One-shot staging buffer -> image copy. Records into the active command
// buffer (caller must have a recording command buffer). `format`/`type` are
// the GL pixel format/type of `pixels` (used to size the staging region and
// honour GL_UNPACK_ALIGNMENT row padding).
void stage_and_copy_image(TextureEntry& tex, int level, int x, int y, int z,
                          int w, int h, int d, const void* pixels,
                          int unpack_alignment, GLenum format, GLenum type);

// Record an image-memory barrier transitioning `tex` from its current layout
// (tex.currentLayout) to `newLayout`. No-op if already in `newLayout`. Updates
// tex.currentLayout on success. Records into the active command buffer; the
// caller is responsible for committing. Use this from glTexStorage* (where
// there is no upload to drive the layout transition) and from any code path
// that needs the texture in a specific layout before issuing a command.
void transition_image_layout(TextureEntry& tex, VkImageLayout newLayout);

// VkImageAspectFlags for a VkFormat (color / depth / depth+stencil / stencil).
VkImageAspectFlags aspect_for_format(VkFormat fmt);

// GL internalFormat -> VkFormat. Returns VK_FORMAT_UNDEFINED if unsupported.
VkFormat gl_internal_to_vk(GLenum internal);

// Bytes per pixel for the (format,type) pair as seen on the host side. Used by
// staging buffer sizing for glTexImage* uploads and glReadPixels readback.
int host_texel_bytes(GLenum format, GLenum type);

} // namespace vk
} // namespace mithril

#endif // MITHRIL_DIRECTVULKAN_RESOURCES_H
