// Mithril-Wrapper - MG_Backend/DirectVulkan/Resources.h
// VkBuffer / VkImage / VkImageView / VkSampler management keyed by GL name.
// Also: GL internalFormat -> VkFormat mapping + staging upload path.
#ifndef MITHRIL_DIRECTVULKAN_RESOURCES_H
#define MITHRIL_DIRECTVULKAN_RESOURCES_H

#include <vulkan/vulkan.h>
#include <GL/gl.h>

#include <unordered_map>

#include "FormatMap.h"

namespace mithril {
namespace vk {

struct BufferEntry {
    VkBuffer       buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize   size = 0;
    void*          mapped = nullptr;   // host pointer if host-visible (persistently mapped)
    // True when the buffer was created with a permanent map (glBufferStorage +
    // MAP_PERSISTENT). In that case `mapped` is valid for the buffer's whole
    // lifetime and glMapBufferRange returns a slice of it directly.
    bool           persistentlyMapped = false;
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
    // FIX (MobileGL-style VRAM monitoring): byte size of the device-local memory
    // allocation backing this image, tracked so defer_destroy_texture_entry can
    // decrement Backend::currentVramBytes for accurate VRAM pressure monitoring.
    VkDeviceSize   memorySize = 0;
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

// FIX (显存耗尽根因 - OOM 主动 GC):
// 封装 vkAllocateMemory，在返回 VK_ERROR_OUT_OF_DEVICE_MEMORY 时触发一次
// 强制垃圾回收（vkDeviceWaitIdle + drain_all_disposal_queues），释放所有
// 延迟销毁队列中的 VkBuffer/VkImage/VkDeviceMemory，然后重试一次。
//
// 这解决了 Minecraft 纹理上传期间的显存耗尽问题：纹理 staging buffer 和
// 旧纹理（被 glTexImage2D 重新指定）都堆积在 disposalQueue 中等待 GPU
// 完成，但在高帧率下 disposalQueue 可能尚未被排空就再次尝试分配，
// 导致 vkAllocateMemory 失败 → 纹理创建失败 → 渲染异常。
//
// GC 后重试一次仍失败则返回原错误码，由调用方处理。
VkResult try_allocate_memory_with_gc(VkDevice device, const VkMemoryAllocateInfo* info,
                                     const VkAllocationCallbacks* allocator,
                                     VkDeviceMemory* memory);

// Create + bind a VkBuffer (host-visible/coherent) of the given size. On
// success fills out the entry. `data` (if non-null) is copied in.
//
// `persistent` keeps the memory mapped for the buffer's whole lifetime
// (glBufferStorage + GL_MAP_PERSISTENT_BIT — see BufferEntry::persistentlyMapped).
// Defaults to false: the ordinary path maps, uploads and unmaps, so staging
// buffers and one-shot uploads do not hold a mapping they never use.
//
// The default argument is what keeps the existing 4-argument call sites
// (Device.cpp / Resources.cpp / ImageOps.cpp) linking against the 5-parameter
// definition in Resources.cpp — the header had not been updated with it, which
// is exactly the undefined-symbol the iOS link step tripped over.
bool create_buffer(BufferEntry& out, VkDeviceSize size,
                   VkBufferUsageFlags usage, const void* data,
                   bool persistent = false);

// Destroy a buffer entry's Vulkan resources (does not erase the table slot).
// Immediate destruction — only safe when no in-flight command buffer references
// the buffer (e.g. create_buffer error paths, shutdown after vkDeviceWaitIdle).
void destroy_buffer_entry(BufferEntry& e);
// Destroy a texture entry's Vulkan resources (does not erase the table slot).
// Immediate destruction — same safety constraint as destroy_buffer_entry.
void destroy_texture_entry(TextureEntry& e);

// Deferred destruction: move the entry's Vulkan handles into the current
// frame slot's disposal queue and null out the entry. The handles are
// actually destroyed kMaxFramesInFlight later when the slot's fence is waited
// (see drain_disposal_queue in Device.cpp). Use this for any resource that
// might be referenced by an in-flight command buffer (i.e. any resource
// deleted during normal rendering — glDeleteBuffers, glDeleteTextures,
// glBufferData orphan-rename, texture re-specification, staging buffer resize).
void defer_destroy_buffer_entry(BufferEntry& e);
void defer_destroy_texture_entry(TextureEntry& e);
void defer_destroy_sampler_entry(SamplerEntry& e);

// One-shot staging buffer -> image copy. Records into the active command
// buffer (caller must have a recording command buffer). `format`/`type` are
// the GL pixel format/type of `pixels` (used to size the staging region and
// honour GL_UNPACK_ALIGNMENT row padding). `is_full_upload` (default true)
// hints that this upload covers the whole image, letting the staging/copy
// path use a single optimized buffer-image-copy instead of per-row strides.
void stage_and_copy_image(TextureEntry& tex, int level, int x, int y, int z,
                          int w, int h, int d, const void* pixels,
                          int unpack_alignment, GLenum format, GLenum type,
                          bool is_full_upload = true);

// Record an image-memory barrier transitioning `tex` from its current layout
// (tex.currentLayout) to `newLayout`. No-op if already in `newLayout`. Updates
// tex.currentLayout on success. Records into the active command buffer; the
// caller is responsible for committing. Use this from glTexStorage* (where
// there is no upload to drive the layout transition) and from any code path
// that needs the texture in a specific layout before issuing a command.
void transition_image_layout(TextureEntry& tex, VkImageLayout newLayout);

// gl_internal_to_vk / host_texel_bytes / aspect_for_format live in FormatMap.h
// (pure-logic helpers extracted for unit testing).

// FIX (P0-1): 把 FormatMap 给出的「理想」VkFormat 换成本设备真正支持的格式。
//
// FormatMap.cpp 是纯查表层，不能查询 VkPhysicalDevice，因此它对
// GL_DEPTH_COMPONENT24 / GL_DEPTH24_STENCIL8 一律返回
// VK_FORMAT_D24_UNORM_S8_UINT —— 而 Apple GPU（所有 iOS 设备 + Apple
// Silicon Mac）都不支持该格式，直接用会让 vkCreateImage 失败 → 深度附件
// 缺失 → 黑屏。
//
// 本函数按候选链探测 optimalTilingFeatures，返回第一个满足
// requiredFeatures 的格式；深度/模板语义会被保留（纯深度不会被无谓地
// 升级成 depth-stencil）。找不到任何替代时返回 VK_FORMAT_UNDEFINED，
// 由调用方决定降级策略。结果内部缓存，调用开销可忽略。
//
// 对照上游 MobileGL FindSupportedDepthStencilFormat
// (SwapchainObject.cpp:60-71)，但覆盖任意用户格式而非仅 swapchain 深度。
VkFormat resolve_supported_format(VkFormat requested, VkFormatFeatureFlags requiredFeatures);

// 该 VkFormat 是否属于 depth / stencil / depth-stencil 家族。
bool format_is_depth_stencil(VkFormat fmt);

} // namespace vk
} // namespace mithril

#endif // MITHRIL_DIRECTVULKAN_RESOURCES_H
