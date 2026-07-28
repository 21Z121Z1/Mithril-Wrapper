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

    // Tracks whether commandBuffer is in a usable (recording or reset) state.
    // Set false when vkEndCommandBuffer succeeds (buffer is now in
    // pending/executable state and CANNOT accept new vkCmd* calls until
    // vkResetCommandBuffer + vkBeginCommandBuffer run again). Set true once
    // vkBeginCommandBuffer succeeds.
    //
    // Without this flag, a failed vkQueueSubmit path that returns after
    // vkEndCommandBuffer leaves the buffer in executable state; the next
    // begin_render_pass() calls vkCmdBeginRendering on a non-recording buffer,
    // which is UB and — under the GPU OOM condition reported in the field —
    // causes vkBeginCommandBuffer itself to return VK_NOT_READY on the next
    // commit_frame's reset+begin, trapping the render thread in a tight loop
    // of failed begins.
    bool             commandBufferRecording = false;

    VkPipelineCache  pipelineCache = VK_NULL_HANDLE;

    // Physical-device properties (cached on init for the GPU name string).
    VkPhysicalDeviceProperties props{};

    // vkCreateMetalSurfaceEXT function pointer (resolved from the instance).
    // Stored as PFN_vkVoidFunction (always declared by vulkan_core.h) so this
    // header does NOT need VK_USE_PLATFORM_METAL_EXT — that macro pulls in
    // <Metal/Metal.h> (Objective-C only) and would break every .cpp that
    // includes this header. SwapchainMetal.mm casts to PFN_vkCreateMetalSurfaceEXT
    // at the call site, where the metal platform define is active.
    PFN_vkVoidFunction createMetalSurfaceEXT = nullptr;

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

    // 默认 16 字节 zero buffer，用于着色器声明但 GL 未 enable 的 vertex attribute
    // binding。Pipeline.cpp 的 get_or_create_pipeline 为这些 location 提供 dummy
    // attribute description 指向此 buffer，让 SPIRV-Cross 为每个 stage_in 字段生成
    // [[attribute(N)]] 限定符，避免 Metal 编译报错。
    VkBuffer         dummyVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory   dummyVertexMemory = VK_NULL_HANDLE;

    // 持久 1×1 RGBA8 透明纹理 + 线性 sampler，用于纹理被删除（VkImageView 为
    // VK_NULL_HANDLE）时填充 COMBINED_IMAGE_SAMPLER descriptor binding。Pipeline
    // layout 声明的所有 binding 在 draw 时必须有有效 descriptor，否则 Metal 驱动
    // 读取野指针 IOSurface → IOSurfaceBindAccel SIGSEGV（dd972b9 的根因）。
    VkImage          dummyTextureImage = VK_NULL_HANDLE;
    VkDeviceMemory   dummyTextureMemory = VK_NULL_HANDLE;
    VkImageView      dummyTextureView = VK_NULL_HANDLE;
    VkSampler        dummyTextureSampler = VK_NULL_HANDLE;

    // 持久性 GPU 故障检测。vkQueueSubmit / vkQueuePresentKHR 致命失败时自增
    // consecutiveSubmitFailures，成功时清零。≥3 时置 deviceLost=true。
    // deviceLost 为真时 commit_frame / present / acquire / eglSwapBuffers 全部
    // 跳过实际 GPU 操作，避免日志死循环刷屏。
    bool             deviceLost = false;
    int              consecutiveSubmitFailures = 0;
};

// Access the singleton backend state. Allocated on first call.
Backend* backend();

// 返回 backend 是否进入持久性故障状态。一旦置位，渲染线程的 submit/present/
// acquire/swapchain-rebuild 全部跳过，避免死循环刷屏。
bool backend_is_device_lost();

// One-time init of the instance/device/queue/command pool/pipeline cache.
// Idempotent; sets Backend::initialized on success.
bool init_device();

// Tear down everything created by init_device() (instance-level resources).
// Resource/pipeline/swapchain objects are owned by their respective modules.
void shutdown_device();

} // namespace vk
} // namespace mithril

#endif // MITHRIL_DIRECTVULKAN_DEVICE_H
