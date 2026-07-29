// Mithril-Wrapper - MG_Backend/DirectVulkan/Device.cpp
// Vulkan 1.2 instance / physical-device / device / queue / command pool init.
// MoltenVK is statically linked, so vkCreateInstance etc. resolve at link time
// (no loader, no VK_ICD_FILENAMES).

// VK_EXT_metal_surface extension-name macro. The canonical definition lives in
// vulkan_metal.h, which vulkan.h only pulls in when VK_USE_PLATFORM_METAL_EXT
// is defined BEFORE the include. We intentionally do NOT define that macro in
// this .cpp (it drags in <Metal/Metal.h>, Objective-C only — would break the
// plain-C++ compile). SwapchainMetal.mm is the one TU that defines it. Here we
// supply the spec-mandated string literal directly so Device.cpp can request
// the instance extension by name. The value is fixed by the Vulkan spec and
// will never diverge; guarding with #ifndef keeps this a no-op if a future
// Vulkan header exposes the macro without the platform define.
#ifndef VK_EXT_METAL_SURFACE_EXTENSION_NAME
#define VK_EXT_METAL_SURFACE_EXTENSION_NAME "VK_EXT_metal_surface"
#endif

#include "Device.h"
#include "Resources.h"
#include "../../MG_Impl/Log.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

namespace mithril {
namespace vk {

Backend* backend() {
    static Backend b;
    return &b;
}

bool backend_is_device_lost() {
    return backend()->deviceLost;
}

namespace {

bool has_extension(const std::vector<VkExtensionProperties>& props, const char* name) {
    for (const auto& p : props) {
        if (std::strcmp(p.extensionName, name) == 0) return true;
    }
    return false;
}

bool has_layer(const std::vector<VkLayerProperties>& props, const char* name) {
    for (const auto& p : props) {
        if (std::strcmp(p.layerName, name) == 0) return true;
    }
    return false;
}

VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                              VkDebugUtilsMessageTypeFlagsEXT,
                                              const VkDebugUtilsMessengerCallbackDataEXT* data,
                                              void*) {
    if (!data || !data->pMessage) return VK_FALSE;

    // ERROR 级消息永不抑制：每次都输出。这是 d2ccb1b 引入的前缀 hash 缺陷
    // 的修复——VK_NOT_READY 消息前缀 "Command buffer cannot accept commands
    // before vkBeginCommandBuffer()" 相同，但消息体可能含不同上下文（不同
    // command buffer / 不同 vkCmd* 调用点），前 64 字节 hash 会把真实错误
    // 当作重复抑制，掩盖红屏根因。
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        MITHRIL_LOG_ERROR("vk", "%s", data->pMessage);
        return VK_FALSE;
    }

    // WARNING/PERFORMANCE 级用完整消息 hash 去重。GPU 故障时 MoltenVK 可能
    // 对每个 vkCmd* 调用发出相同消息（实测 3M+ 次，655MB 日志）。完整消息
    // hash 避免前缀碰撞导致的误抑制，同时仍限流。
    static std::atomic<uint64_t> lastHash{0};
    static std::atomic<uint64_t> repeatCount{0};

    uint64_t h = 1469598103934665603ull;  // FNV-1a 64-bit over full message
    const char* p = data->pMessage;
    while (*p) {
        h ^= (uint8_t)*p++;
        h *= 1099511628211ull;
    }

    if (h == lastHash.load(std::memory_order_relaxed)) {
        uint64_t n = repeatCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n <= 4) {
            MITHRIL_LOG_WARN("vk", "%s", data->pMessage);
        } else if (n % 1000 == 0) {
            MITHRIL_LOG_WARN("vk", "(vulkan message repeated %llu times) %.80s...",
                             (unsigned long long)n, data->pMessage);
        }
    } else {
        uint64_t prev = repeatCount.exchange(1, std::memory_order_relaxed);
        lastHash.store(h, std::memory_order_relaxed);
        if (prev > 4) {
            MITHRIL_LOG_WARN("vk", "(previous vulkan message repeated %llu times total)",
                             (unsigned long long)prev);
        }
        MITHRIL_LOG_WARN("vk", "%s", data->pMessage);
    }
    return VK_FALSE;
}

} // namespace

bool init_device() {
    Backend* b = backend();
    if (b->initialized) return true;

    // ---- Instance ----
    std::vector<VkExtensionProperties> instExtProps;
    uint32_t extCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
    instExtProps.resize(extCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, instExtProps.data());

    std::vector<const char*> instExts;
    // Portability enumeration is mandatory for MoltenVK-backed Vulkan.
    if (has_extension(instExtProps, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        instExts.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    }
    if (has_extension(instExtProps, VK_EXT_METAL_SURFACE_EXTENSION_NAME)) {
        instExts.push_back(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
    }
    if (has_extension(instExtProps, VK_KHR_SURFACE_EXTENSION_NAME)) {
        instExts.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    }
    // Debug utils optional.
    bool wantDebugUtils = has_extension(instExtProps, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (wantDebugUtils) instExts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Mithril-Wrapper";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Mithril-Wrapper";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo instCI{};
    instCI.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instCI.pApplicationInfo = &appInfo;
    instCI.enabledExtensionCount = (uint32_t)instExts.size();
    instCI.ppEnabledExtensionNames = instExts.data();
    // VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR is REQUIRED so
    // vkEnumeratePhysicalDevices returns the MoltenVK ICD.
    instCI.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;

    if (vkCreateInstance(&instCI, nullptr, &b->instance) != VK_SUCCESS) {
        MITHRIL_LOG_ERROR("vk", "vkCreateInstance failed");
        return false;
    }

    if (wantDebugUtils) {
        // Best-effort debug messenger (optional, never fatal).
        auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(b->instance, "vkCreateDebugUtilsMessengerEXT");
        if (fn) {
            VkDebugUtilsMessengerCreateInfoEXT mic{};
            mic.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            mic.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            mic.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            mic.pfnUserCallback = debug_callback;
            VkDebugUtilsMessengerEXT messenger;
            fn(b->instance, &mic, nullptr, &messenger);
            // Messenger intentionally leaked (process lifetime); the callback
            // stays armed for the whole session.
        }
    }

    // Resolve vkCreateMetalSurfaceEXT (used by SwapchainMetal.mm). Stored as
    // PFN_vkVoidFunction to avoid needing VK_USE_PLATFORM_METAL_EXT here
    // (that macro pulls in <Metal/Metal.h>, which is ObjC-only). The .mm
    // translation unit casts it to PFN_vkCreateMetalSurfaceEXT at the call.
    b->createMetalSurfaceEXT =
        vkGetInstanceProcAddr(b->instance, "vkCreateMetalSurfaceEXT");
    if (!b->createMetalSurfaceEXT) {
        // Diagnostic: this function is only exported when VK_EXT_metal_surface
        // is enabled at instance creation. Dump the enumerated instance
        // extensions so a null resolution is self-explaining on-device.
        MITHRIL_LOG_WARN("vk", "vkCreateMetalSurfaceEXT not resolved; "
                              "enumerated instance extensions:");
        for (const auto& e : instExtProps) {
            MITHRIL_LOG_WARN("vk", "  %s", e.extensionName);
        }
    }

    // ---- Physical device ----
    uint32_t gpuCount = 0;
    vkEnumeratePhysicalDevices(b->instance, &gpuCount, nullptr);
    if (gpuCount == 0) {
        MITHRIL_LOG_ERROR("vk", "No Vulkan physical devices (MoltenVK not linked?)");
        return false;
    }
    std::vector<VkPhysicalDevice> gpus(gpuCount);
    vkEnumeratePhysicalDevices(b->instance, &gpuCount, gpus.data());
    b->physicalDevice = gpus[0];  // iOS has exactly one Metal device.

    vkGetPhysicalDeviceProperties(b->physicalDevice, &b->props);
    MITHRIL_LOG_INFO("vk", "Physical device: %s (api 0x%x, driver 0x%x)",
                     b->props.deviceName, b->props.apiVersion, b->props.driverVersion);

    // ---- Queue family ----
    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(b->physicalDevice, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qfProps(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(b->physicalDevice, &qfCount, qfProps.data());
    b->graphicsFamily = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < qfCount; ++i) {
        if (qfProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            b->graphicsFamily = i;
            break;
        }
    }
    if (b->graphicsFamily == 0xFFFFFFFFu) {
        MITHRIL_LOG_ERROR("vk", "No graphics queue family");
        return false;
    }

    // ---- Device ----
    // Verify the portability-subset + swapchain extensions are available.
    std::vector<VkExtensionProperties> devExtProps;
    uint32_t devExtCount = 0;
    vkEnumerateDeviceExtensionProperties(b->physicalDevice, nullptr, &devExtCount, nullptr);
    devExtProps.resize(devExtCount);
    vkEnumerateDeviceExtensionProperties(b->physicalDevice, nullptr, &devExtCount, devExtProps.data());

    std::vector<const char*> devExts;
    if (has_extension(devExtProps, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
        devExts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }
    // VK_KHR_portability_subset MUST be enabled if present (MoltenVK always
    // advertises it).
    if (has_extension(devExtProps, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)) {
        devExts.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
    }
    // VK_KHR_dynamic_rendering: lets us create pipelines + record render passes
    // without a VkRenderPass object (simpler than managing compat render passes).
    if (has_extension(devExtProps, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)) {
        devExts.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    }
    // VK_EXT_extended_dynamic_state: vkCmdSetCullMode/FrontFace/DepthTestEnable/
    // DepthWriteEnable/DepthCompareOp etc. without rebuilding pipelines.
    if (has_extension(devExtProps, VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME)) {
        devExts.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME);
    }

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCI{};
    queueCI.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCI.queueFamilyIndex = b->graphicsFamily;
    queueCI.queueCount = 1;
    queueCI.pQueuePriorities = &queuePriority;

    // Feature chain: enable dynamic rendering + extended dynamic state so the
    // vkCmdBeginRendering / vkCmdSetCullMode etc. calls in CommandStream.cpp
    // are valid. Without these features a strict driver rejects pipeline
    // creation that carries VkPipelineRenderingCreateInfo and rejects the
    // dynamic-state vkCmdSet* calls. (MoltenVK is lenient but the spec
    // requires the features to be enabled.)
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extDynStateFeat{};
    extDynStateFeat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
    extDynStateFeat.extendedDynamicState = VK_TRUE;

    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynRenderFeat{};
    dynRenderFeat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
    dynRenderFeat.dynamicRendering = VK_TRUE;
    dynRenderFeat.pNext = &extDynStateFeat;

    VkDeviceCreateInfo devCI{};
    devCI.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    devCI.pNext = &dynRenderFeat;
    devCI.queueCreateInfoCount = 1;
    devCI.pQueueCreateInfos = &queueCI;
    devCI.enabledExtensionCount = (uint32_t)devExts.size();
    devCI.ppEnabledExtensionNames = devExts.data();

    if (vkCreateDevice(b->physicalDevice, &devCI, nullptr, &b->device) != VK_SUCCESS) {
        MITHRIL_LOG_ERROR("vk", "vkCreateDevice failed");
        return false;
    }
    vkGetDeviceQueue(b->device, b->graphicsFamily, 0, &b->graphicsQueue);

    // ---- Command pool + primary command buffer ----
    VkCommandPoolCreateInfo poolCI{};
    poolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolCI.queueFamilyIndex = b->graphicsFamily;
    if (vkCreateCommandPool(b->device, &poolCI, nullptr, &b->commandPool) != VK_SUCCESS) {
        MITHRIL_LOG_ERROR("vk", "vkCreateCommandPool failed");
        return false;
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = b->commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(b->device, &allocInfo, &b->commandBuffer) != VK_SUCCESS) {
        MITHRIL_LOG_ERROR("vk", "vkAllocateCommandBuffers failed");
        return false;
    }

    // ---- Per-frame fences ----
    VkFenceCreateInfo fenceCI{};
    fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // start signalled so first wait is a no-op
    for (int i = 0; i < kMaxFramesInFlight; ++i) {
        vkCreateFence(b->device, &fenceCI, nullptr, &b->frameFences[i]);
    }

    // Begin the primary command buffer in the recording state so that
    // pre-frame commands (e.g. layout transitions from glTexStorage2D, texture
    // uploads from glTexImage2D outside a render pass) have somewhere to record
    // before the first begin_render_pass. begin_render_pass does NOT reset the
    // buffer; only commit_frame resets + re-begins it after submission.
    VkCommandBufferBeginInfo cbBegin{};
    cbBegin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbBegin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(b->commandBuffer, &cbBegin) == VK_SUCCESS) {
        b->commandBufferRecording = true;
    } else {
        MITHRIL_LOG_ERROR("vk", "initial vkBeginCommandBuffer failed");
        return false;
    }

    // ---- Pipeline cache ----
    VkPipelineCacheCreateInfo cacheCI{};
    cacheCI.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    vkCreatePipelineCache(b->device, &cacheCI, nullptr, &b->pipelineCache);

    // ---- Dummy vertex buffer ----
    // 16-byte zero buffer for vertex attributes the shader declares but GL
    // has not enabled (see Pipeline.cpp's get_or_create_pipeline). Provides
    // valid backing for dummy attribute descriptions so SPIRV-Cross emits
    // [[attribute(N)]] for every stage_in field, avoiding the Metal
    // "invalid type ... stage_in" compile error.
    {
        static const uint8_t zeros[16] = {0};
        BufferEntry tmp{};
        if (create_buffer(tmp, 16, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, zeros)) {
            b->dummyVertexBuffer = tmp.buffer;
            b->dummyVertexMemory = tmp.memory;
        } else {
            MITHRIL_LOG_WARN("vk", "failed to allocate dummy vertex buffer");
        }
    }

    // ---- Dummy texture (1×1 RGBA8 transparent) ----
    // 持久占位纹理，用于纹理被删除（VkImageView=null）时填充
    // COMBINED_IMAGE_SAMPLER descriptor binding。Pipeline layout 声明的所有
    // binding 在 draw 时必须有有效 descriptor，否则 Metal 驱动读取野指针
    // IOSurface → IOSurfaceBindAccel SIGSEGV（dd972b9 的根因）。
    // 初始化：创建 image → 分配+绑定 memory → 创建 view → 创建 sampler。
    // 不在此处录制 clear/transition 命令——layout transition 延迟到
    // DescriptorSet.cpp 第一次使用 dummy texture 时执行，避免 init command
    // buffer 混入可能引发 InvalidResource 的命令。
    {
        VkImageCreateInfo dici{};
        dici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        dici.imageType = VK_IMAGE_TYPE_2D;
        dici.format = VK_FORMAT_R8G8B8A8_UNORM;
        dici.extent = { 1, 1, 1 };
        dici.mipLevels = 1;
        dici.arrayLayers = 1;
        dici.samples = VK_SAMPLE_COUNT_1_BIT;
        dici.tiling = VK_IMAGE_TILING_OPTIMAL;
        dici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        dici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        dici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(b->device, &dici, nullptr, &b->dummyTextureImage) == VK_SUCCESS) {
            VkMemoryRequirements req{};
            vkGetImageMemoryRequirements(b->device, b->dummyTextureImage, &req);
            uint32_t mt = find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (mt == 0xFFFFFFFFu) mt = find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
            bool ok = false;
            if (mt != 0xFFFFFFFFu) {
                VkMemoryAllocateInfo ai{};
                ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                ai.allocationSize = req.size;
                ai.memoryTypeIndex = mt;
                if (vkAllocateMemory(b->device, &ai, nullptr, &b->dummyTextureMemory) == VK_SUCCESS) {
                    if (vkBindImageMemory(b->device, b->dummyTextureImage, b->dummyTextureMemory, 0) == VK_SUCCESS) {
                        ok = true;
                    } else {
                        vkFreeMemory(b->device, b->dummyTextureMemory, nullptr);
                        b->dummyTextureMemory = VK_NULL_HANDLE;
                    }
                }
            }
            if (!ok) {
                vkDestroyImage(b->device, b->dummyTextureImage, nullptr);
                b->dummyTextureImage = VK_NULL_HANDLE;
            }
        }
        if (b->dummyTextureImage != VK_NULL_HANDLE) {
            VkImageViewCreateInfo vci{};
            vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vci.image = b->dummyTextureImage;
            vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vci.format = VK_FORMAT_R8G8B8A8_UNORM;
            vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vci.subresourceRange.baseMipLevel = 0;
            vci.subresourceRange.levelCount = 1;
            vci.subresourceRange.baseArrayLayer = 0;
            vci.subresourceRange.layerCount = 1;
            if (vkCreateImageView(b->device, &vci, nullptr, &b->dummyTextureView) != VK_SUCCESS) {
                b->dummyTextureView = VK_NULL_HANDLE;
            }

            VkSamplerCreateInfo sci{};
            sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            sci.magFilter = VK_FILTER_LINEAR;
            sci.minFilter = VK_FILTER_LINEAR;
            sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            sci.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
            sci.anisotropyEnable = VK_FALSE;
            sci.maxAnisotropy = 1.0f;
            sci.compareEnable = VK_FALSE;
            sci.compareOp = VK_COMPARE_OP_ALWAYS;
            sci.minLod = 0.0f;
            sci.maxLod = 0.0f;
            sci.unnormalizedCoordinates = VK_FALSE;
            if (vkCreateSampler(b->device, &sci, nullptr, &b->dummyTextureSampler) != VK_SUCCESS) {
                b->dummyTextureSampler = VK_NULL_HANDLE;
            }

            // 任一组件创建失败则全部销毁，确保 dummyTextureView 和
            // dummyTextureSampler 同时有效或同时为 null（DescriptorSet.cpp
            // 检查两者非 null 才使用 dummy 填充）。
            if (b->dummyTextureView == VK_NULL_HANDLE || b->dummyTextureSampler == VK_NULL_HANDLE) {
                if (b->dummyTextureSampler) { vkDestroySampler(b->device, b->dummyTextureSampler, nullptr); b->dummyTextureSampler = VK_NULL_HANDLE; }
                if (b->dummyTextureView) { vkDestroyImageView(b->device, b->dummyTextureView, nullptr); b->dummyTextureView = VK_NULL_HANDLE; }
                if (b->dummyTextureImage) { vkDestroyImage(b->device, b->dummyTextureImage, nullptr); b->dummyTextureImage = VK_NULL_HANDLE; }
                if (b->dummyTextureMemory) { vkFreeMemory(b->device, b->dummyTextureMemory, nullptr); b->dummyTextureMemory = VK_NULL_HANDLE; }
            }
        }
        if (b->dummyTextureView == VK_NULL_HANDLE) {
            MITHRIL_LOG_WARN("vk", "failed to create dummy texture — null "
                             "descriptor bindings will be unsafe");
        }
    }

    b->initialized = true;
    MITHRIL_LOG_INFO("vk", "Vulkan 1.2 backend initialised (MoltenVK static link)");
    return true;
}

void shutdown_device() {
    Backend* b = backend();
    if (!b->initialized) return;
    if (b->device) vkDeviceWaitIdle(b->device);
    // Drain deferred-release queues before tearing down per-resource state.
    // Any textures/buffers deleted mid-frame are still pending in these
    // queues; the GPU is now idle (vkDeviceWaitIdle above) so they can be
    // safely destroyed. Must run before the device is destroyed.
    for (auto& entry : b->pendingBufferReleases) {
        if (entry.buffer)  { vkDestroyBuffer(b->device, entry.buffer, nullptr); }
        if (entry.memory)  { vkFreeMemory(b->device, entry.memory, nullptr); }
    }
    b->pendingBufferReleases.clear();
    for (auto& entry : b->pendingTextureReleases) {
        if (entry.view)          { vkDestroyImageView(b->device, entry.view, nullptr); }
        if (entry.image)         { vkDestroyImage(b->device, entry.image, nullptr); }
        if (entry.memory)        { vkFreeMemory(b->device, entry.memory, nullptr); }
        if (entry.stagingBuffer) { vkDestroyBuffer(b->device, entry.stagingBuffer, nullptr); }
        if (entry.stagingMemory) { vkFreeMemory(b->device, entry.stagingMemory, nullptr); }
    }
    b->pendingTextureReleases.clear();
    if (b->pipelineCache) { vkDestroyPipelineCache(b->device, b->pipelineCache, nullptr); b->pipelineCache = VK_NULL_HANDLE; }
    for (int i = 0; i < kMaxFramesInFlight; ++i) {
        if (b->frameFences[i]) { vkDestroyFence(b->device, b->frameFences[i], nullptr); b->frameFences[i] = VK_NULL_HANDLE; }
    }
    if (b->commandBuffer) { vkFreeCommandBuffers(b->device, b->commandPool, 1, &b->commandBuffer); b->commandBuffer = VK_NULL_HANDLE; }
    if (b->commandPool) { vkDestroyCommandPool(b->device, b->commandPool, nullptr); b->commandPool = VK_NULL_HANDLE; }
    if (b->dummyVertexBuffer) { vkDestroyBuffer(b->device, b->dummyVertexBuffer, nullptr); b->dummyVertexBuffer = VK_NULL_HANDLE; }
    if (b->dummyVertexMemory) { vkFreeMemory(b->device, b->dummyVertexMemory, nullptr); b->dummyVertexMemory = VK_NULL_HANDLE; }
    if (b->dummyTextureSampler) { vkDestroySampler(b->device, b->dummyTextureSampler, nullptr); b->dummyTextureSampler = VK_NULL_HANDLE; }
    if (b->dummyTextureView) { vkDestroyImageView(b->device, b->dummyTextureView, nullptr); b->dummyTextureView = VK_NULL_HANDLE; }
    if (b->dummyTextureImage) { vkDestroyImage(b->device, b->dummyTextureImage, nullptr); b->dummyTextureImage = VK_NULL_HANDLE; }
    if (b->dummyTextureMemory) { vkFreeMemory(b->device, b->dummyTextureMemory, nullptr); b->dummyTextureMemory = VK_NULL_HANDLE; }
    if (b->device) { vkDestroyDevice(b->device, nullptr); b->device = VK_NULL_HANDLE; }
    if (b->instance) { vkDestroyInstance(b->instance, nullptr); b->instance = VK_NULL_HANDLE; }
    b->initialized = false;
}

} // namespace vk
} // namespace mithril

// ===========================================================================
// Public C API lifecycle functions (declared in MG_Backend/Backend.h)
// ===========================================================================
extern "C" {

void backend_init(void) {
    mithril::vk::init_device();
}

void backend_shutdown(void) {
    mithril::vk::shutdown_device();
}

int backend_available(void) {
    return mithril::vk::backend()->initialized ? 1 : 0;
}

const char* backend_physical_device_name(void) {
    mithril::vk::Backend* b = mithril::vk::backend();
    return b->initialized ? b->props.deviceName : "Vulkan (MoltenVK)";
}

uint64_t backend_vram_bytes(void) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->initialized) return 0;
    // MoltenVK reports maxMemoryAllocationCount but not total VRAM reliably;
    // approximate using the heap sizes from memory properties.
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(b->physicalDevice, &mp);
    uint64_t total = 0;
    for (uint32_t i = 0; i < mp.memoryHeapCount; ++i) {
        if (mp.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            total += mp.memoryHeaps[i].size;
        }
    }
    return total;
}

} // extern "C"
