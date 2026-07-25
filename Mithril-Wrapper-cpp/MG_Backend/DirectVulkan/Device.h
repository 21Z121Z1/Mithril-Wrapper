// Mithril-Wrapper - MG_Backend/DirectVulkan/Device.h
// Global Vulkan 1.2 device state (VkInstance / VkPhysicalDevice / VkDevice /
// VkQueue / VkCommandPool / VkCommandBuffer). Accessed by the other
// DirectVulkan translation units via vk_backend().
//
// iOS surface creation uses VK_EXT_metal_surface (vkCreateMetalSurfaceEXT) —
// NOT the deprecated VK_MVK_metal_surface. Portability is required:
//   * VK_KHR_portability_enumeration instance extension +
//     VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
//   * VK_KHR_portability_subset device extension (always enabled alongside
//     VK_KHR_swapchain)
// MoltenVK is statically linked, so there is no Vulkan loader / ICD file.
#ifndef MITHRIL_DIRECTVULKAN_DEVICE_H
#define MITHRIL_DIRECTVULKAN_DEVICE_H

#include <vulkan/vulkan.h>

namespace mithril {
namespace vk {

// Minimum/maximum swapchain images in-flight (default 2; allows up to 3).
constexpr int kMaxFramesInFlight = 2;

// Global Vulkan backend state. A single instance lives for the process; it is
// created by backend_init() and torn down by backend_shutdown().
struct Backend {
    bool initialized = false;

    VkInstance       instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice         device = VK_NULL_HANDLE;
    VkQueue          graphicsQueue = VK_NULL_HANDLE;
    uint32_t         graphicsFamily = 0xFFFFFFFFu;

    VkCommandPool    commandPool = VK_NULL_HANDLE;
    // Primary command buffer reused across a frame's draws. Reset + begin in
    // begin_render_pass / commit, rerecorded each frame.
    VkCommandBuffer  commandBuffer = VK_NULL_HANDLE;

    VkPipelineCache  pipelineCache = VK_NULL_HANDLE;

    // Physical-device properties (cached on init for the GPU name string).
    VkPhysicalDeviceProperties props{};

    // vkCreateMetalSurfaceEXT function pointer (resolved from the instance).
    PFN_vkCreateMetalSurfaceEXT createMetalSurfaceEXT = nullptr;

    // Per-frame sync: a fence per in-flight frame so we can wait on the GPU
    // before reusing the command buffer.
    VkFence frameFences[kMaxFramesInFlight] = {};
    int     currentFrame = 0;

    // Monotonic frame generation counter, bumped once per commit_frame(). Used
    // by DescriptorSet.cpp to reset each program's descriptor pool exactly once
    // per frame (currentFrame cycles 0/1, so a program drawn only on every other
    // frame would never see a reset — the monotonic counter fixes that). The
    // value seen by every draw within a single frame is constant.
    uint64_t frameGeneration = 0;
};

// Access the singleton backend state. Allocated on first call.
Backend* backend();

// One-time init of the instance/device/queue/command pool/pipeline cache.
// Idempotent; sets Backend::initialized on success.
bool init_device();

// Tear down everything created by init_device() (instance-level resources).
// Resource/pipeline/swapchain objects are owned by their respective modules.
void shutdown_device();

} // namespace vk
} // namespace mithril

#endif // MITHRIL_DIRECTVULKAN_DEVICE_H
