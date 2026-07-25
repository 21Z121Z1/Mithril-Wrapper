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
// buffer (caller must have a recording command buffer).
void stage_and_copy_image(TextureEntry& tex, int level, int x, int y, int z,
                          int w, int h, int d, const void* pixels,
                          int unpack_alignment);

// GL internalFormat -> VkFormat. Returns VK_FORMAT_UNDEFINED if unsupported.
VkFormat gl_internal_to_vk(GLenum internal);

} // namespace vk
} // namespace mithril

#endif // MITHRIL_DIRECTVULKAN_RESOURCES_H
