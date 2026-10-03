// Mithril-Wrapper - MG_Backend/DirectVulkan/VulkanDispatchAndroid.cpp
// GENERATED FILE - do not edit by hand.
//
// Android-only Vulkan dispatch shim.
//
// Why: on Android we used to link the platform loader (libvulkan.so) directly.
// That hard-binds every vk* call to whatever driver the platform loader
// discovers. The Android loader finds drivers through hw_get_module only - it
// never reads VK_ICD_FILENAMES or VK_DRIVER_FILES (Khronos
// LoaderDriverInterface, "Driver Discovery on Android": "The Android loader
// lives in the system library folder. The location cannot be changed... Due to
// security policies in Android, none of this can be modified under normal
// use"). Devices whose stock driver is stuck at Vulkan 1.1 therefore have no
// way to reach an out-of-tree driver such as Turnip.
//
// By dlopen-ing the driver ourselves we choose it at runtime. Each vk* symbol
// below is a thin forwarder that lazily resolves the real entry point from the
// chosen library, so the rest of the codebase keeps calling vkCreateInstance
// etc. directly and needs no changes.
//
// Selection order (first match wins):
//   MITHRIL_VULKAN_LIBRARY  - explicit path to a driver .so
//   MITHRIL_TURNIP=1        - libvulkan_freedreno.so (Turnip / freedreno ICD)
//   otherwise               - libvulkan.so, the platform loader

#if defined(__ANDROID__)

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <vulkan/vk_icd.h>

#include <dlfcn.h>
#include <cstdlib>
#include <cstdio>

namespace {

void* g_handle = nullptr;
PFN_vkGetInstanceProcAddr g_gipa = nullptr;
PFN_vk_icdGetInstanceProcAddr g_icd_gipa = nullptr;
bool g_ready = false;
int g_failures = 0;

void ensure_library() {
    if (g_ready) return;
    g_ready = true;

    const char* explicit_path = getenv("MITHRIL_VULKAN_LIBRARY");
    const char* turnip = getenv("MITHRIL_TURNIP");
    const char* path = "libvulkan.so";
    bool want_turnip = false;

    if (explicit_path && explicit_path[0]) {
        path = explicit_path;
    } else if (turnip && (turnip[0] == '1' || turnip[0] == 'y' || turnip[0] == 'Y')) {
        path = "libvulkan_freedreno.so";
        want_turnip = true;
    }

    g_handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!g_handle) {
        fprintf(stderr, "[mithril] vk-dispatch: dlopen(\"%s\") failed: %s\n", path, dlerror());
        return;
    }
    fprintf(stderr, "[mithril] vk-dispatch: loaded \"%s\"\n", path);

    // An ICD such as Turnip exposes discovery through vk_icdGetInstanceProcAddr
    // rather than the plain names; a loader exports the plain names.
    g_icd_gipa = (PFN_vk_icdGetInstanceProcAddr)dlsym(g_handle, "vk_icdGetInstanceProcAddr");
    g_gipa = (PFN_vkGetInstanceProcAddr)dlsym(g_handle, "vkGetInstanceProcAddr");
    if (want_turnip && !g_icd_gipa && !g_gipa) {
        fprintf(stderr, "[mithril] vk-dispatch: %s exports no discovery entrypoint\n", path);
    }
}

void* resolve(const char* name) {
    ensure_library();
    if (g_icd_gipa) {
        // An ICD answers for every entrypoint, device-level ones included,
        // because that is how the loader populates its dispatch tables.
        void* p = (void*)g_icd_gipa(nullptr, name);
        if (p) return p;
    }
    if (g_gipa) {
        void* p = (void*)g_gipa(nullptr, name);
        if (p) return p;
    }
    if (g_handle) {
        void* p = dlsym(g_handle, name);
        if (p) return p;
    }
    if (g_failures++ < 12) {
        fprintf(stderr, "[mithril] vk-dispatch: unresolved entrypoint %s\n", name);
    }
    return nullptr;
}

} // namespace

extern "C" {

VKAPI_ATTR VkResult VKAPI_CALL vkAcquireDrmDisplayEXT(VkPhysicalDevice physicalDevice, int32_t drmFd, VkDisplayKHR display) {
    static PFN_vkAcquireDrmDisplayEXT fp = nullptr;
    if (!fp) fp = (PFN_vkAcquireDrmDisplayEXT)resolve("vkAcquireDrmDisplayEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, drmFd, display);
}
VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImage2KHR(VkDevice device, const VkAcquireNextImageInfoKHR* pAcquireInfo, uint32_t* pImageIndex) {
    static PFN_vkAcquireNextImage2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkAcquireNextImage2KHR)resolve("vkAcquireNextImage2KHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pAcquireInfo, pImageIndex);
}
VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex) {
    static PFN_vkAcquireNextImageKHR fp = nullptr;
    if (!fp) fp = (PFN_vkAcquireNextImageKHR)resolve("vkAcquireNextImageKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, swapchain, timeout, semaphore, fence, pImageIndex);
}
VKAPI_ATTR VkResult VKAPI_CALL vkAcquirePerformanceConfigurationINTEL(VkDevice device, const VkPerformanceConfigurationAcquireInfoINTEL* pAcquireInfo, VkPerformanceConfigurationINTEL* pConfiguration) {
    static PFN_vkAcquirePerformanceConfigurationINTEL fp = nullptr;
    if (!fp) fp = (PFN_vkAcquirePerformanceConfigurationINTEL)resolve("vkAcquirePerformanceConfigurationINTEL");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pAcquireInfo, pConfiguration);
}
VKAPI_ATTR VkResult VKAPI_CALL vkAcquireProfilingLockKHR(VkDevice device, const VkAcquireProfilingLockInfoKHR* pInfo) {
    static PFN_vkAcquireProfilingLockKHR fp = nullptr;
    if (!fp) fp = (PFN_vkAcquireProfilingLockKHR)resolve("vkAcquireProfilingLockKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo, VkCommandBuffer* pCommandBuffers) {
    static PFN_vkAllocateCommandBuffers fp = nullptr;
    if (!fp) fp = (PFN_vkAllocateCommandBuffers)resolve("vkAllocateCommandBuffers");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pAllocateInfo, pCommandBuffers);
}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* pAllocateInfo, VkDescriptorSet* pDescriptorSets) {
    static PFN_vkAllocateDescriptorSets fp = nullptr;
    if (!fp) fp = (PFN_vkAllocateDescriptorSets)resolve("vkAllocateDescriptorSets");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pAllocateInfo, pDescriptorSets);
}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory) {
    static PFN_vkAllocateMemory fp = nullptr;
    if (!fp) fp = (PFN_vkAllocateMemory)resolve("vkAllocateMemory");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pAllocateInfo, pAllocator, pMemory);
}
VKAPI_ATTR void VKAPI_CALL vkAntiLagUpdateAMD(VkDevice device, const VkAntiLagDataAMD* pData) {
    static PFN_vkAntiLagUpdateAMD fp = nullptr;
    if (!fp) fp = (PFN_vkAntiLagUpdateAMD)resolve("vkAntiLagUpdateAMD");
    if (!fp) { return; }
    fp(device, pData);
}
VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo) {
    static PFN_vkBeginCommandBuffer fp = nullptr;
    if (!fp) fp = (PFN_vkBeginCommandBuffer)resolve("vkBeginCommandBuffer");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(commandBuffer, pBeginInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkBindAccelerationStructureMemoryNV(VkDevice device, uint32_t bindInfoCount, const VkBindAccelerationStructureMemoryInfoNV* pBindInfos) {
    static PFN_vkBindAccelerationStructureMemoryNV fp = nullptr;
    if (!fp) fp = (PFN_vkBindAccelerationStructureMemoryNV)resolve("vkBindAccelerationStructureMemoryNV");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, bindInfoCount, pBindInfos);
}
VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset) {
    static PFN_vkBindBufferMemory fp = nullptr;
    if (!fp) fp = (PFN_vkBindBufferMemory)resolve("vkBindBufferMemory");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, buffer, memory, memoryOffset);
}
VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo* pBindInfos) {
    static PFN_vkBindBufferMemory2 fp = nullptr;
    if (!fp) fp = (PFN_vkBindBufferMemory2)resolve("vkBindBufferMemory2");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, bindInfoCount, pBindInfos);
}
VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory2KHR(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo* pBindInfos) {
    static PFN_vkBindBufferMemory2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkBindBufferMemory2KHR)resolve("vkBindBufferMemory2KHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, bindInfoCount, pBindInfos);
}
VKAPI_ATTR VkResult VKAPI_CALL vkBindDataGraphPipelineSessionMemoryARM(VkDevice device, uint32_t bindInfoCount, const VkBindDataGraphPipelineSessionMemoryInfoARM* pBindInfos) {
    static PFN_vkBindDataGraphPipelineSessionMemoryARM fp = nullptr;
    if (!fp) fp = (PFN_vkBindDataGraphPipelineSessionMemoryARM)resolve("vkBindDataGraphPipelineSessionMemoryARM");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, bindInfoCount, pBindInfos);
}
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset) {
    static PFN_vkBindImageMemory fp = nullptr;
    if (!fp) fp = (PFN_vkBindImageMemory)resolve("vkBindImageMemory");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, image, memory, memoryOffset);
}
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo* pBindInfos) {
    static PFN_vkBindImageMemory2 fp = nullptr;
    if (!fp) fp = (PFN_vkBindImageMemory2)resolve("vkBindImageMemory2");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, bindInfoCount, pBindInfos);
}
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory2KHR(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo* pBindInfos) {
    static PFN_vkBindImageMemory2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkBindImageMemory2KHR)resolve("vkBindImageMemory2KHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, bindInfoCount, pBindInfos);
}
VKAPI_ATTR VkResult VKAPI_CALL vkBindOpticalFlowSessionImageNV(VkDevice device, VkOpticalFlowSessionNV session, VkOpticalFlowSessionBindingPointNV bindingPoint, VkImageView view, VkImageLayout layout) {
    static PFN_vkBindOpticalFlowSessionImageNV fp = nullptr;
    if (!fp) fp = (PFN_vkBindOpticalFlowSessionImageNV)resolve("vkBindOpticalFlowSessionImageNV");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, session, bindingPoint, view, layout);
}
VKAPI_ATTR VkResult VKAPI_CALL vkBindTensorMemoryARM(VkDevice device, uint32_t bindInfoCount, const VkBindTensorMemoryInfoARM* pBindInfos) {
    static PFN_vkBindTensorMemoryARM fp = nullptr;
    if (!fp) fp = (PFN_vkBindTensorMemoryARM)resolve("vkBindTensorMemoryARM");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, bindInfoCount, pBindInfos);
}
VKAPI_ATTR VkResult VKAPI_CALL vkBindVideoSessionMemoryKHR(VkDevice device, VkVideoSessionKHR videoSession, uint32_t bindSessionMemoryInfoCount, const VkBindVideoSessionMemoryInfoKHR* pBindSessionMemoryInfos) {
    static PFN_vkBindVideoSessionMemoryKHR fp = nullptr;
    if (!fp) fp = (PFN_vkBindVideoSessionMemoryKHR)resolve("vkBindVideoSessionMemoryKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, videoSession, bindSessionMemoryInfoCount, pBindSessionMemoryInfos);
}
VKAPI_ATTR VkResult VKAPI_CALL vkBuildAccelerationStructuresKHR(VkDevice device, VkDeferredOperationKHR deferredOperation, uint32_t infoCount, const VkAccelerationStructureBuildGeometryInfoKHR* pInfos, const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos) {
    static PFN_vkBuildAccelerationStructuresKHR fp = nullptr;
    if (!fp) fp = (PFN_vkBuildAccelerationStructuresKHR)resolve("vkBuildAccelerationStructuresKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, deferredOperation, infoCount, pInfos, ppBuildRangeInfos);
}
VKAPI_ATTR VkResult VKAPI_CALL vkBuildMicromapsEXT(VkDevice device, VkDeferredOperationKHR deferredOperation, uint32_t infoCount, const VkMicromapBuildInfoEXT* pInfos) {
    static PFN_vkBuildMicromapsEXT fp = nullptr;
    if (!fp) fp = (PFN_vkBuildMicromapsEXT)resolve("vkBuildMicromapsEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, deferredOperation, infoCount, pInfos);
}
VKAPI_ATTR void VKAPI_CALL vkClearShaderInstrumentationMetricsARM(VkDevice device, VkShaderInstrumentationARM instrumentation) {
    static PFN_vkClearShaderInstrumentationMetricsARM fp = nullptr;
    if (!fp) fp = (PFN_vkClearShaderInstrumentationMetricsARM)resolve("vkClearShaderInstrumentationMetricsARM");
    if (!fp) { return; }
    fp(device, instrumentation);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginConditionalRendering2EXT(VkCommandBuffer commandBuffer, const VkConditionalRenderingBeginInfo2EXT* pConditionalRenderingBegin) {
    static PFN_vkCmdBeginConditionalRendering2EXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBeginConditionalRendering2EXT)resolve("vkCmdBeginConditionalRendering2EXT");
    if (!fp) { return; }
    fp(commandBuffer, pConditionalRenderingBegin);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginConditionalRenderingEXT(VkCommandBuffer commandBuffer, const VkConditionalRenderingBeginInfoEXT* pConditionalRenderingBegin) {
    static PFN_vkCmdBeginConditionalRenderingEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBeginConditionalRenderingEXT)resolve("vkCmdBeginConditionalRenderingEXT");
    if (!fp) { return; }
    fp(commandBuffer, pConditionalRenderingBegin);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginCustomResolveEXT(VkCommandBuffer commandBuffer, const VkBeginCustomResolveInfoEXT* pBeginCustomResolveInfo) {
    static PFN_vkCmdBeginCustomResolveEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBeginCustomResolveEXT)resolve("vkCmdBeginCustomResolveEXT");
    if (!fp) { return; }
    fp(commandBuffer, pBeginCustomResolveInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginDebugUtilsLabelEXT(VkCommandBuffer commandBuffer, const VkDebugUtilsLabelEXT* pLabelInfo) {
    static PFN_vkCmdBeginDebugUtilsLabelEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBeginDebugUtilsLabelEXT)resolve("vkCmdBeginDebugUtilsLabelEXT");
    if (!fp) { return; }
    fp(commandBuffer, pLabelInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCmdBeginGpaSampleAMD(VkCommandBuffer commandBuffer, VkGpaSessionAMD gpaSession, const VkGpaSampleBeginInfoAMD* pGpaSampleBeginInfo, uint32_t* pSampleID) {
    static PFN_vkCmdBeginGpaSampleAMD fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBeginGpaSampleAMD)resolve("vkCmdBeginGpaSampleAMD");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(commandBuffer, gpaSession, pGpaSampleBeginInfo, pSampleID);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCmdBeginGpaSessionAMD(VkCommandBuffer commandBuffer, VkGpaSessionAMD gpaSession) {
    static PFN_vkCmdBeginGpaSessionAMD fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBeginGpaSessionAMD)resolve("vkCmdBeginGpaSessionAMD");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(commandBuffer, gpaSession);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginPerTileExecutionQCOM(VkCommandBuffer commandBuffer, const VkPerTileBeginInfoQCOM* pPerTileBeginInfo) {
    static PFN_vkCmdBeginPerTileExecutionQCOM fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBeginPerTileExecutionQCOM)resolve("vkCmdBeginPerTileExecutionQCOM");
    if (!fp) { return; }
    fp(commandBuffer, pPerTileBeginInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags) {
    static PFN_vkCmdBeginQuery fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBeginQuery)resolve("vkCmdBeginQuery");
    if (!fp) { return; }
    fp(commandBuffer, queryPool, query, flags);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginQueryIndexedEXT(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags, uint32_t index) {
    static PFN_vkCmdBeginQueryIndexedEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBeginQueryIndexedEXT)resolve("vkCmdBeginQueryIndexedEXT");
    if (!fp) { return; }
    fp(commandBuffer, queryPool, query, flags, index);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents) {
    static PFN_vkCmdBeginRenderPass fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBeginRenderPass)resolve("vkCmdBeginRenderPass");
    if (!fp) { return; }
    fp(commandBuffer, pRenderPassBegin, contents);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass2(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, const VkSubpassBeginInfo* pSubpassBeginInfo) {
    static PFN_vkCmdBeginRenderPass2 fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBeginRenderPass2)resolve("vkCmdBeginRenderPass2");
    if (!fp) { return; }
    fp(commandBuffer, pRenderPassBegin, pSubpassBeginInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass2KHR(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, const VkSubpassBeginInfo* pSubpassBeginInfo) {
    static PFN_vkCmdBeginRenderPass2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBeginRenderPass2KHR)resolve("vkCmdBeginRenderPass2KHR");
    if (!fp) { return; }
    fp(commandBuffer, pRenderPassBegin, pSubpassBeginInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginRendering(VkCommandBuffer commandBuffer, const VkRenderingInfo* pRenderingInfo) {
    static PFN_vkCmdBeginRendering fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBeginRendering)resolve("vkCmdBeginRendering");
    if (!fp) { return; }
    fp(commandBuffer, pRenderingInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderingKHR(VkCommandBuffer commandBuffer, const VkRenderingInfo* pRenderingInfo) {
    static PFN_vkCmdBeginRenderingKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBeginRenderingKHR)resolve("vkCmdBeginRenderingKHR");
    if (!fp) { return; }
    fp(commandBuffer, pRenderingInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginShaderInstrumentationARM(VkCommandBuffer commandBuffer, VkShaderInstrumentationARM instrumentation) {
    static PFN_vkCmdBeginShaderInstrumentationARM fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBeginShaderInstrumentationARM)resolve("vkCmdBeginShaderInstrumentationARM");
    if (!fp) { return; }
    fp(commandBuffer, instrumentation);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginTransformFeedback2EXT(VkCommandBuffer commandBuffer, uint32_t firstCounterRange, uint32_t counterRangeCount, const VkBindTransformFeedbackBuffer2InfoEXT* pCounterInfos) {
    static PFN_vkCmdBeginTransformFeedback2EXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBeginTransformFeedback2EXT)resolve("vkCmdBeginTransformFeedback2EXT");
    if (!fp) { return; }
    fp(commandBuffer, firstCounterRange, counterRangeCount, pCounterInfos);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginTransformFeedbackEXT(VkCommandBuffer commandBuffer, uint32_t firstCounterBuffer, uint32_t counterBufferCount, const VkBuffer* pCounterBuffers, const VkDeviceSize* pCounterBufferOffsets) {
    static PFN_vkCmdBeginTransformFeedbackEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBeginTransformFeedbackEXT)resolve("vkCmdBeginTransformFeedbackEXT");
    if (!fp) { return; }
    fp(commandBuffer, firstCounterBuffer, counterBufferCount, pCounterBuffers, pCounterBufferOffsets);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginVideoCodingKHR(VkCommandBuffer commandBuffer, const VkVideoBeginCodingInfoKHR* pBeginInfo) {
    static PFN_vkCmdBeginVideoCodingKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBeginVideoCodingKHR)resolve("vkCmdBeginVideoCodingKHR");
    if (!fp) { return; }
    fp(commandBuffer, pBeginInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorBufferEmbeddedSamplers2EXT(VkCommandBuffer commandBuffer, const VkBindDescriptorBufferEmbeddedSamplersInfoEXT* pBindDescriptorBufferEmbeddedSamplersInfo) {
    static PFN_vkCmdBindDescriptorBufferEmbeddedSamplers2EXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBindDescriptorBufferEmbeddedSamplers2EXT)resolve("vkCmdBindDescriptorBufferEmbeddedSamplers2EXT");
    if (!fp) { return; }
    fp(commandBuffer, pBindDescriptorBufferEmbeddedSamplersInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorBufferEmbeddedSamplersEXT(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set) {
    static PFN_vkCmdBindDescriptorBufferEmbeddedSamplersEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBindDescriptorBufferEmbeddedSamplersEXT)resolve("vkCmdBindDescriptorBufferEmbeddedSamplersEXT");
    if (!fp) { return; }
    fp(commandBuffer, pipelineBindPoint, layout, set);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorBuffersEXT(VkCommandBuffer commandBuffer, uint32_t bufferCount, const VkDescriptorBufferBindingInfoEXT* pBindingInfos) {
    static PFN_vkCmdBindDescriptorBuffersEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBindDescriptorBuffersEXT)resolve("vkCmdBindDescriptorBuffersEXT");
    if (!fp) { return; }
    fp(commandBuffer, bufferCount, pBindingInfos);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets) {
    static PFN_vkCmdBindDescriptorSets fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBindDescriptorSets)resolve("vkCmdBindDescriptorSets");
    if (!fp) { return; }
    fp(commandBuffer, pipelineBindPoint, layout, firstSet, descriptorSetCount, pDescriptorSets, dynamicOffsetCount, pDynamicOffsets);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets2(VkCommandBuffer commandBuffer, const VkBindDescriptorSetsInfo* pBindDescriptorSetsInfo) {
    static PFN_vkCmdBindDescriptorSets2 fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBindDescriptorSets2)resolve("vkCmdBindDescriptorSets2");
    if (!fp) { return; }
    fp(commandBuffer, pBindDescriptorSetsInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets2KHR(VkCommandBuffer commandBuffer, const VkBindDescriptorSetsInfo* pBindDescriptorSetsInfo) {
    static PFN_vkCmdBindDescriptorSets2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBindDescriptorSets2KHR)resolve("vkCmdBindDescriptorSets2KHR");
    if (!fp) { return; }
    fp(commandBuffer, pBindDescriptorSetsInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType) {
    static PFN_vkCmdBindIndexBuffer fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBindIndexBuffer)resolve("vkCmdBindIndexBuffer");
    if (!fp) { return; }
    fp(commandBuffer, buffer, offset, indexType);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer2(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size, VkIndexType indexType) {
    static PFN_vkCmdBindIndexBuffer2 fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBindIndexBuffer2)resolve("vkCmdBindIndexBuffer2");
    if (!fp) { return; }
    fp(commandBuffer, buffer, offset, size, indexType);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer2KHR(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size, VkIndexType indexType) {
    static PFN_vkCmdBindIndexBuffer2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBindIndexBuffer2KHR)resolve("vkCmdBindIndexBuffer2KHR");
    if (!fp) { return; }
    fp(commandBuffer, buffer, offset, size, indexType);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer3KHR(VkCommandBuffer commandBuffer, const VkBindIndexBuffer3InfoKHR* pInfo) {
    static PFN_vkCmdBindIndexBuffer3KHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBindIndexBuffer3KHR)resolve("vkCmdBindIndexBuffer3KHR");
    if (!fp) { return; }
    fp(commandBuffer, pInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindInvocationMaskHUAWEI(VkCommandBuffer commandBuffer, VkImageView imageView, VkImageLayout imageLayout) {
    static PFN_vkCmdBindInvocationMaskHUAWEI fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBindInvocationMaskHUAWEI)resolve("vkCmdBindInvocationMaskHUAWEI");
    if (!fp) { return; }
    fp(commandBuffer, imageView, imageLayout);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline) {
    static PFN_vkCmdBindPipeline fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBindPipeline)resolve("vkCmdBindPipeline");
    if (!fp) { return; }
    fp(commandBuffer, pipelineBindPoint, pipeline);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindPipelineShaderGroupNV(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline, uint32_t groupIndex) {
    static PFN_vkCmdBindPipelineShaderGroupNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBindPipelineShaderGroupNV)resolve("vkCmdBindPipelineShaderGroupNV");
    if (!fp) { return; }
    fp(commandBuffer, pipelineBindPoint, pipeline, groupIndex);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindResourceHeapEXT(VkCommandBuffer commandBuffer, const VkBindHeapInfoEXT* pBindInfo) {
    static PFN_vkCmdBindResourceHeapEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBindResourceHeapEXT)resolve("vkCmdBindResourceHeapEXT");
    if (!fp) { return; }
    fp(commandBuffer, pBindInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindSamplerHeapEXT(VkCommandBuffer commandBuffer, const VkBindHeapInfoEXT* pBindInfo) {
    static PFN_vkCmdBindSamplerHeapEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBindSamplerHeapEXT)resolve("vkCmdBindSamplerHeapEXT");
    if (!fp) { return; }
    fp(commandBuffer, pBindInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindShadersEXT(VkCommandBuffer commandBuffer, uint32_t stageCount, const VkShaderStageFlagBits* pStages, const VkShaderEXT* pShaders) {
    static PFN_vkCmdBindShadersEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBindShadersEXT)resolve("vkCmdBindShadersEXT");
    if (!fp) { return; }
    fp(commandBuffer, stageCount, pStages, pShaders);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindShadingRateImageNV(VkCommandBuffer commandBuffer, VkImageView imageView, VkImageLayout imageLayout) {
    static PFN_vkCmdBindShadingRateImageNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBindShadingRateImageNV)resolve("vkCmdBindShadingRateImageNV");
    if (!fp) { return; }
    fp(commandBuffer, imageView, imageLayout);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindTileMemoryQCOM(VkCommandBuffer commandBuffer, const VkTileMemoryBindInfoQCOM* pTileMemoryBindInfo) {
    static PFN_vkCmdBindTileMemoryQCOM fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBindTileMemoryQCOM)resolve("vkCmdBindTileMemoryQCOM");
    if (!fp) { return; }
    fp(commandBuffer, pTileMemoryBindInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindTransformFeedbackBuffers2EXT(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBindTransformFeedbackBuffer2InfoEXT* pBindingInfos) {
    static PFN_vkCmdBindTransformFeedbackBuffers2EXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBindTransformFeedbackBuffers2EXT)resolve("vkCmdBindTransformFeedbackBuffers2EXT");
    if (!fp) { return; }
    fp(commandBuffer, firstBinding, bindingCount, pBindingInfos);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindTransformFeedbackBuffersEXT(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets, const VkDeviceSize* pSizes) {
    static PFN_vkCmdBindTransformFeedbackBuffersEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBindTransformFeedbackBuffersEXT)resolve("vkCmdBindTransformFeedbackBuffersEXT");
    if (!fp) { return; }
    fp(commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets, pSizes);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets) {
    static PFN_vkCmdBindVertexBuffers fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBindVertexBuffers)resolve("vkCmdBindVertexBuffers");
    if (!fp) { return; }
    fp(commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers2(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets, const VkDeviceSize* pSizes, const VkDeviceSize* pStrides) {
    static PFN_vkCmdBindVertexBuffers2 fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBindVertexBuffers2)resolve("vkCmdBindVertexBuffers2");
    if (!fp) { return; }
    fp(commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets, pSizes, pStrides);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers2EXT(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets, const VkDeviceSize* pSizes, const VkDeviceSize* pStrides) {
    static PFN_vkCmdBindVertexBuffers2EXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBindVertexBuffers2EXT)resolve("vkCmdBindVertexBuffers2EXT");
    if (!fp) { return; }
    fp(commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets, pSizes, pStrides);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers3KHR(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBindVertexBuffer3InfoKHR* pBindingInfos) {
    static PFN_vkCmdBindVertexBuffers3KHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBindVertexBuffers3KHR)resolve("vkCmdBindVertexBuffers3KHR");
    if (!fp) { return; }
    fp(commandBuffer, firstBinding, bindingCount, pBindingInfos);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter) {
    static PFN_vkCmdBlitImage fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBlitImage)resolve("vkCmdBlitImage");
    if (!fp) { return; }
    fp(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions, filter);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage2(VkCommandBuffer commandBuffer, const VkBlitImageInfo2* pBlitImageInfo) {
    static PFN_vkCmdBlitImage2 fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBlitImage2)resolve("vkCmdBlitImage2");
    if (!fp) { return; }
    fp(commandBuffer, pBlitImageInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage2KHR(VkCommandBuffer commandBuffer, const VkBlitImageInfo2* pBlitImageInfo) {
    static PFN_vkCmdBlitImage2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBlitImage2KHR)resolve("vkCmdBlitImage2KHR");
    if (!fp) { return; }
    fp(commandBuffer, pBlitImageInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBuildAccelerationStructureNV(VkCommandBuffer commandBuffer, const VkAccelerationStructureInfoNV* pInfo, VkBuffer instanceData, VkDeviceSize instanceOffset, VkBool32 update, VkAccelerationStructureNV dst, VkAccelerationStructureNV src, VkBuffer scratch, VkDeviceSize scratchOffset) {
    static PFN_vkCmdBuildAccelerationStructureNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBuildAccelerationStructureNV)resolve("vkCmdBuildAccelerationStructureNV");
    if (!fp) { return; }
    fp(commandBuffer, pInfo, instanceData, instanceOffset, update, dst, src, scratch, scratchOffset);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBuildAccelerationStructuresIndirectKHR(VkCommandBuffer commandBuffer, uint32_t infoCount, const VkAccelerationStructureBuildGeometryInfoKHR* pInfos, const VkDeviceAddress* pIndirectDeviceAddresses, const uint32_t* pIndirectStrides, const uint32_t* const* ppMaxPrimitiveCounts) {
    static PFN_vkCmdBuildAccelerationStructuresIndirectKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBuildAccelerationStructuresIndirectKHR)resolve("vkCmdBuildAccelerationStructuresIndirectKHR");
    if (!fp) { return; }
    fp(commandBuffer, infoCount, pInfos, pIndirectDeviceAddresses, pIndirectStrides, ppMaxPrimitiveCounts);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBuildAccelerationStructuresKHR(VkCommandBuffer commandBuffer, uint32_t infoCount, const VkAccelerationStructureBuildGeometryInfoKHR* pInfos, const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos) {
    static PFN_vkCmdBuildAccelerationStructuresKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBuildAccelerationStructuresKHR)resolve("vkCmdBuildAccelerationStructuresKHR");
    if (!fp) { return; }
    fp(commandBuffer, infoCount, pInfos, ppBuildRangeInfos);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBuildClusterAccelerationStructureIndirectNV(VkCommandBuffer commandBuffer, const VkClusterAccelerationStructureCommandsInfoNV* pCommandInfos) {
    static PFN_vkCmdBuildClusterAccelerationStructureIndirectNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBuildClusterAccelerationStructureIndirectNV)resolve("vkCmdBuildClusterAccelerationStructureIndirectNV");
    if (!fp) { return; }
    fp(commandBuffer, pCommandInfos);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBuildMicromapsEXT(VkCommandBuffer commandBuffer, uint32_t infoCount, const VkMicromapBuildInfoEXT* pInfos) {
    static PFN_vkCmdBuildMicromapsEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBuildMicromapsEXT)resolve("vkCmdBuildMicromapsEXT");
    if (!fp) { return; }
    fp(commandBuffer, infoCount, pInfos);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBuildPartitionedAccelerationStructuresNV(VkCommandBuffer commandBuffer, const VkBuildPartitionedAccelerationStructureInfoNV* pBuildInfo) {
    static PFN_vkCmdBuildPartitionedAccelerationStructuresNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdBuildPartitionedAccelerationStructuresNV)resolve("vkCmdBuildPartitionedAccelerationStructuresNV");
    if (!fp) { return; }
    fp(commandBuffer, pBuildInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount, const VkClearAttachment* pAttachments, uint32_t rectCount, const VkClearRect* pRects) {
    static PFN_vkCmdClearAttachments fp = nullptr;
    if (!fp) fp = (PFN_vkCmdClearAttachments)resolve("vkCmdClearAttachments");
    if (!fp) { return; }
    fp(commandBuffer, attachmentCount, pAttachments, rectCount, pRects);
}
VKAPI_ATTR void VKAPI_CALL vkCmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearColorValue* pColor, uint32_t rangeCount, const VkImageSubresourceRange* pRanges) {
    static PFN_vkCmdClearColorImage fp = nullptr;
    if (!fp) fp = (PFN_vkCmdClearColorImage)resolve("vkCmdClearColorImage");
    if (!fp) { return; }
    fp(commandBuffer, image, imageLayout, pColor, rangeCount, pRanges);
}
VKAPI_ATTR void VKAPI_CALL vkCmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearDepthStencilValue* pDepthStencil, uint32_t rangeCount, const VkImageSubresourceRange* pRanges) {
    static PFN_vkCmdClearDepthStencilImage fp = nullptr;
    if (!fp) fp = (PFN_vkCmdClearDepthStencilImage)resolve("vkCmdClearDepthStencilImage");
    if (!fp) { return; }
    fp(commandBuffer, image, imageLayout, pDepthStencil, rangeCount, pRanges);
}
VKAPI_ATTR void VKAPI_CALL vkCmdControlVideoCodingKHR(VkCommandBuffer commandBuffer, const VkVideoCodingControlInfoKHR* pCodingControlInfo) {
    static PFN_vkCmdControlVideoCodingKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdControlVideoCodingKHR)resolve("vkCmdControlVideoCodingKHR");
    if (!fp) { return; }
    fp(commandBuffer, pCodingControlInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdConvertCooperativeVectorMatrixNV(VkCommandBuffer commandBuffer, uint32_t infoCount, const VkConvertCooperativeVectorMatrixInfoNV* pInfos) {
    static PFN_vkCmdConvertCooperativeVectorMatrixNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdConvertCooperativeVectorMatrixNV)resolve("vkCmdConvertCooperativeVectorMatrixNV");
    if (!fp) { return; }
    fp(commandBuffer, infoCount, pInfos);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyAccelerationStructureKHR(VkCommandBuffer commandBuffer, const VkCopyAccelerationStructureInfoKHR* pInfo) {
    static PFN_vkCmdCopyAccelerationStructureKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdCopyAccelerationStructureKHR)resolve("vkCmdCopyAccelerationStructureKHR");
    if (!fp) { return; }
    fp(commandBuffer, pInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyAccelerationStructureNV(VkCommandBuffer commandBuffer, VkAccelerationStructureNV dst, VkAccelerationStructureNV src, VkCopyAccelerationStructureModeKHR mode) {
    static PFN_vkCmdCopyAccelerationStructureNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdCopyAccelerationStructureNV)resolve("vkCmdCopyAccelerationStructureNV");
    if (!fp) { return; }
    fp(commandBuffer, dst, src, mode);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyAccelerationStructureToMemoryKHR(VkCommandBuffer commandBuffer, const VkCopyAccelerationStructureToMemoryInfoKHR* pInfo) {
    static PFN_vkCmdCopyAccelerationStructureToMemoryKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdCopyAccelerationStructureToMemoryKHR)resolve("vkCmdCopyAccelerationStructureToMemoryKHR");
    if (!fp) { return; }
    fp(commandBuffer, pInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions) {
    static PFN_vkCmdCopyBuffer fp = nullptr;
    if (!fp) fp = (PFN_vkCmdCopyBuffer)resolve("vkCmdCopyBuffer");
    if (!fp) { return; }
    fp(commandBuffer, srcBuffer, dstBuffer, regionCount, pRegions);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer2(VkCommandBuffer commandBuffer, const VkCopyBufferInfo2* pCopyBufferInfo) {
    static PFN_vkCmdCopyBuffer2 fp = nullptr;
    if (!fp) fp = (PFN_vkCmdCopyBuffer2)resolve("vkCmdCopyBuffer2");
    if (!fp) { return; }
    fp(commandBuffer, pCopyBufferInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer2KHR(VkCommandBuffer commandBuffer, const VkCopyBufferInfo2* pCopyBufferInfo) {
    static PFN_vkCmdCopyBuffer2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdCopyBuffer2KHR)resolve("vkCmdCopyBuffer2KHR");
    if (!fp) { return; }
    fp(commandBuffer, pCopyBufferInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions) {
    static PFN_vkCmdCopyBufferToImage fp = nullptr;
    if (!fp) fp = (PFN_vkCmdCopyBufferToImage)resolve("vkCmdCopyBufferToImage");
    if (!fp) { return; }
    fp(commandBuffer, srcBuffer, dstImage, dstImageLayout, regionCount, pRegions);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage2(VkCommandBuffer commandBuffer, const VkCopyBufferToImageInfo2* pCopyBufferToImageInfo) {
    static PFN_vkCmdCopyBufferToImage2 fp = nullptr;
    if (!fp) fp = (PFN_vkCmdCopyBufferToImage2)resolve("vkCmdCopyBufferToImage2");
    if (!fp) { return; }
    fp(commandBuffer, pCopyBufferToImageInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage2KHR(VkCommandBuffer commandBuffer, const VkCopyBufferToImageInfo2* pCopyBufferToImageInfo) {
    static PFN_vkCmdCopyBufferToImage2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdCopyBufferToImage2KHR)resolve("vkCmdCopyBufferToImage2KHR");
    if (!fp) { return; }
    fp(commandBuffer, pCopyBufferToImageInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyGpaSessionResultsAMD(VkCommandBuffer commandBuffer, VkGpaSessionAMD gpaSession) {
    static PFN_vkCmdCopyGpaSessionResultsAMD fp = nullptr;
    if (!fp) fp = (PFN_vkCmdCopyGpaSessionResultsAMD)resolve("vkCmdCopyGpaSessionResultsAMD");
    if (!fp) { return; }
    fp(commandBuffer, gpaSession);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy* pRegions) {
    static PFN_vkCmdCopyImage fp = nullptr;
    if (!fp) fp = (PFN_vkCmdCopyImage)resolve("vkCmdCopyImage");
    if (!fp) { return; }
    fp(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage2(VkCommandBuffer commandBuffer, const VkCopyImageInfo2* pCopyImageInfo) {
    static PFN_vkCmdCopyImage2 fp = nullptr;
    if (!fp) fp = (PFN_vkCmdCopyImage2)resolve("vkCmdCopyImage2");
    if (!fp) { return; }
    fp(commandBuffer, pCopyImageInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage2KHR(VkCommandBuffer commandBuffer, const VkCopyImageInfo2* pCopyImageInfo) {
    static PFN_vkCmdCopyImage2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdCopyImage2KHR)resolve("vkCmdCopyImage2KHR");
    if (!fp) { return; }
    fp(commandBuffer, pCopyImageInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions) {
    static PFN_vkCmdCopyImageToBuffer fp = nullptr;
    if (!fp) fp = (PFN_vkCmdCopyImageToBuffer)resolve("vkCmdCopyImageToBuffer");
    if (!fp) { return; }
    fp(commandBuffer, srcImage, srcImageLayout, dstBuffer, regionCount, pRegions);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer2(VkCommandBuffer commandBuffer, const VkCopyImageToBufferInfo2* pCopyImageToBufferInfo) {
    static PFN_vkCmdCopyImageToBuffer2 fp = nullptr;
    if (!fp) fp = (PFN_vkCmdCopyImageToBuffer2)resolve("vkCmdCopyImageToBuffer2");
    if (!fp) { return; }
    fp(commandBuffer, pCopyImageToBufferInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer2KHR(VkCommandBuffer commandBuffer, const VkCopyImageToBufferInfo2* pCopyImageToBufferInfo) {
    static PFN_vkCmdCopyImageToBuffer2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdCopyImageToBuffer2KHR)resolve("vkCmdCopyImageToBuffer2KHR");
    if (!fp) { return; }
    fp(commandBuffer, pCopyImageToBufferInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToMemoryKHR(VkCommandBuffer commandBuffer, const VkCopyDeviceMemoryImageInfoKHR* pCopyMemoryInfo) {
    static PFN_vkCmdCopyImageToMemoryKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdCopyImageToMemoryKHR)resolve("vkCmdCopyImageToMemoryKHR");
    if (!fp) { return; }
    fp(commandBuffer, pCopyMemoryInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyMemoryIndirectKHR(VkCommandBuffer commandBuffer, const VkCopyMemoryIndirectInfoKHR* pCopyMemoryIndirectInfo) {
    static PFN_vkCmdCopyMemoryIndirectKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdCopyMemoryIndirectKHR)resolve("vkCmdCopyMemoryIndirectKHR");
    if (!fp) { return; }
    fp(commandBuffer, pCopyMemoryIndirectInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyMemoryIndirectNV(VkCommandBuffer commandBuffer, VkDeviceAddress copyBufferAddress, uint32_t copyCount, uint32_t stride) {
    static PFN_vkCmdCopyMemoryIndirectNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdCopyMemoryIndirectNV)resolve("vkCmdCopyMemoryIndirectNV");
    if (!fp) { return; }
    fp(commandBuffer, copyBufferAddress, copyCount, stride);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyMemoryKHR(VkCommandBuffer commandBuffer, const VkCopyDeviceMemoryInfoKHR* pCopyMemoryInfo) {
    static PFN_vkCmdCopyMemoryKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdCopyMemoryKHR)resolve("vkCmdCopyMemoryKHR");
    if (!fp) { return; }
    fp(commandBuffer, pCopyMemoryInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyMemoryToAccelerationStructureKHR(VkCommandBuffer commandBuffer, const VkCopyMemoryToAccelerationStructureInfoKHR* pInfo) {
    static PFN_vkCmdCopyMemoryToAccelerationStructureKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdCopyMemoryToAccelerationStructureKHR)resolve("vkCmdCopyMemoryToAccelerationStructureKHR");
    if (!fp) { return; }
    fp(commandBuffer, pInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyMemoryToImageIndirectKHR(VkCommandBuffer commandBuffer, const VkCopyMemoryToImageIndirectInfoKHR* pCopyMemoryToImageIndirectInfo) {
    static PFN_vkCmdCopyMemoryToImageIndirectKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdCopyMemoryToImageIndirectKHR)resolve("vkCmdCopyMemoryToImageIndirectKHR");
    if (!fp) { return; }
    fp(commandBuffer, pCopyMemoryToImageIndirectInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyMemoryToImageIndirectNV(VkCommandBuffer commandBuffer, VkDeviceAddress copyBufferAddress, uint32_t copyCount, uint32_t stride, VkImage dstImage, VkImageLayout dstImageLayout, const VkImageSubresourceLayers* pImageSubresources) {
    static PFN_vkCmdCopyMemoryToImageIndirectNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdCopyMemoryToImageIndirectNV)resolve("vkCmdCopyMemoryToImageIndirectNV");
    if (!fp) { return; }
    fp(commandBuffer, copyBufferAddress, copyCount, stride, dstImage, dstImageLayout, pImageSubresources);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyMemoryToImageKHR(VkCommandBuffer commandBuffer, const VkCopyDeviceMemoryImageInfoKHR* pCopyMemoryInfo) {
    static PFN_vkCmdCopyMemoryToImageKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdCopyMemoryToImageKHR)resolve("vkCmdCopyMemoryToImageKHR");
    if (!fp) { return; }
    fp(commandBuffer, pCopyMemoryInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyMemoryToMicromapEXT(VkCommandBuffer commandBuffer, const VkCopyMemoryToMicromapInfoEXT* pInfo) {
    static PFN_vkCmdCopyMemoryToMicromapEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdCopyMemoryToMicromapEXT)resolve("vkCmdCopyMemoryToMicromapEXT");
    if (!fp) { return; }
    fp(commandBuffer, pInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyMicromapEXT(VkCommandBuffer commandBuffer, const VkCopyMicromapInfoEXT* pInfo) {
    static PFN_vkCmdCopyMicromapEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdCopyMicromapEXT)resolve("vkCmdCopyMicromapEXT");
    if (!fp) { return; }
    fp(commandBuffer, pInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyMicromapToMemoryEXT(VkCommandBuffer commandBuffer, const VkCopyMicromapToMemoryInfoEXT* pInfo) {
    static PFN_vkCmdCopyMicromapToMemoryEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdCopyMicromapToMemoryEXT)resolve("vkCmdCopyMicromapToMemoryEXT");
    if (!fp) { return; }
    fp(commandBuffer, pInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyQueryPoolResults(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize stride, VkQueryResultFlags flags) {
    static PFN_vkCmdCopyQueryPoolResults fp = nullptr;
    if (!fp) fp = (PFN_vkCmdCopyQueryPoolResults)resolve("vkCmdCopyQueryPoolResults");
    if (!fp) { return; }
    fp(commandBuffer, queryPool, firstQuery, queryCount, dstBuffer, dstOffset, stride, flags);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyQueryPoolResultsToMemoryKHR(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, const VkStridedDeviceAddressRangeKHR* pDstRange, VkAddressCommandFlagsKHR dstFlags, VkQueryResultFlags queryResultFlags) {
    static PFN_vkCmdCopyQueryPoolResultsToMemoryKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdCopyQueryPoolResultsToMemoryKHR)resolve("vkCmdCopyQueryPoolResultsToMemoryKHR");
    if (!fp) { return; }
    fp(commandBuffer, queryPool, firstQuery, queryCount, pDstRange, dstFlags, queryResultFlags);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyTensorARM(VkCommandBuffer commandBuffer, const VkCopyTensorInfoARM* pCopyTensorInfo) {
    static PFN_vkCmdCopyTensorARM fp = nullptr;
    if (!fp) fp = (PFN_vkCmdCopyTensorARM)resolve("vkCmdCopyTensorARM");
    if (!fp) { return; }
    fp(commandBuffer, pCopyTensorInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCuLaunchKernelNVX(VkCommandBuffer commandBuffer, const VkCuLaunchInfoNVX* pLaunchInfo) {
    static PFN_vkCmdCuLaunchKernelNVX fp = nullptr;
    if (!fp) fp = (PFN_vkCmdCuLaunchKernelNVX)resolve("vkCmdCuLaunchKernelNVX");
    if (!fp) { return; }
    fp(commandBuffer, pLaunchInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDebugMarkerBeginEXT(VkCommandBuffer commandBuffer, const VkDebugMarkerMarkerInfoEXT* pMarkerInfo) {
    static PFN_vkCmdDebugMarkerBeginEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDebugMarkerBeginEXT)resolve("vkCmdDebugMarkerBeginEXT");
    if (!fp) { return; }
    fp(commandBuffer, pMarkerInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDebugMarkerEndEXT(VkCommandBuffer commandBuffer) {
    static PFN_vkCmdDebugMarkerEndEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDebugMarkerEndEXT)resolve("vkCmdDebugMarkerEndEXT");
    if (!fp) { return; }
    fp(commandBuffer);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDebugMarkerInsertEXT(VkCommandBuffer commandBuffer, const VkDebugMarkerMarkerInfoEXT* pMarkerInfo) {
    static PFN_vkCmdDebugMarkerInsertEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDebugMarkerInsertEXT)resolve("vkCmdDebugMarkerInsertEXT");
    if (!fp) { return; }
    fp(commandBuffer, pMarkerInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDecodeVideoKHR(VkCommandBuffer commandBuffer, const VkVideoDecodeInfoKHR* pDecodeInfo) {
    static PFN_vkCmdDecodeVideoKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDecodeVideoKHR)resolve("vkCmdDecodeVideoKHR");
    if (!fp) { return; }
    fp(commandBuffer, pDecodeInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDecompressMemoryEXT(VkCommandBuffer commandBuffer, const VkDecompressMemoryInfoEXT* pDecompressMemoryInfoEXT) {
    static PFN_vkCmdDecompressMemoryEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDecompressMemoryEXT)resolve("vkCmdDecompressMemoryEXT");
    if (!fp) { return; }
    fp(commandBuffer, pDecompressMemoryInfoEXT);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDecompressMemoryIndirectCountEXT(VkCommandBuffer commandBuffer, VkMemoryDecompressionMethodFlagsEXT decompressionMethod, VkDeviceAddress indirectCommandsAddress, VkDeviceAddress indirectCommandsCountAddress, uint32_t maxDecompressionCount, uint32_t stride) {
    static PFN_vkCmdDecompressMemoryIndirectCountEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDecompressMemoryIndirectCountEXT)resolve("vkCmdDecompressMemoryIndirectCountEXT");
    if (!fp) { return; }
    fp(commandBuffer, decompressionMethod, indirectCommandsAddress, indirectCommandsCountAddress, maxDecompressionCount, stride);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDecompressMemoryIndirectCountNV(VkCommandBuffer commandBuffer, VkDeviceAddress indirectCommandsAddress, VkDeviceAddress indirectCommandsCountAddress, uint32_t stride) {
    static PFN_vkCmdDecompressMemoryIndirectCountNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDecompressMemoryIndirectCountNV)resolve("vkCmdDecompressMemoryIndirectCountNV");
    if (!fp) { return; }
    fp(commandBuffer, indirectCommandsAddress, indirectCommandsCountAddress, stride);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDecompressMemoryNV(VkCommandBuffer commandBuffer, uint32_t decompressRegionCount, const VkDecompressMemoryRegionNV* pDecompressMemoryRegions) {
    static PFN_vkCmdDecompressMemoryNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDecompressMemoryNV)resolve("vkCmdDecompressMemoryNV");
    if (!fp) { return; }
    fp(commandBuffer, decompressRegionCount, pDecompressMemoryRegions);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    static PFN_vkCmdDispatch fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDispatch)resolve("vkCmdDispatch");
    if (!fp) { return; }
    fp(commandBuffer, groupCountX, groupCountY, groupCountZ);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDispatchBase(VkCommandBuffer commandBuffer, uint32_t baseGroupX, uint32_t baseGroupY, uint32_t baseGroupZ, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    static PFN_vkCmdDispatchBase fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDispatchBase)resolve("vkCmdDispatchBase");
    if (!fp) { return; }
    fp(commandBuffer, baseGroupX, baseGroupY, baseGroupZ, groupCountX, groupCountY, groupCountZ);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDispatchBaseKHR(VkCommandBuffer commandBuffer, uint32_t baseGroupX, uint32_t baseGroupY, uint32_t baseGroupZ, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    static PFN_vkCmdDispatchBaseKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDispatchBaseKHR)resolve("vkCmdDispatchBaseKHR");
    if (!fp) { return; }
    fp(commandBuffer, baseGroupX, baseGroupY, baseGroupZ, groupCountX, groupCountY, groupCountZ);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDispatchDataGraphARM(VkCommandBuffer commandBuffer, VkDataGraphPipelineSessionARM session, const VkDataGraphPipelineDispatchInfoARM* pInfo) {
    static PFN_vkCmdDispatchDataGraphARM fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDispatchDataGraphARM)resolve("vkCmdDispatchDataGraphARM");
    if (!fp) { return; }
    fp(commandBuffer, session, pInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset) {
    static PFN_vkCmdDispatchIndirect fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDispatchIndirect)resolve("vkCmdDispatchIndirect");
    if (!fp) { return; }
    fp(commandBuffer, buffer, offset);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDispatchIndirect2KHR(VkCommandBuffer commandBuffer, const VkDispatchIndirect2InfoKHR* pInfo) {
    static PFN_vkCmdDispatchIndirect2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDispatchIndirect2KHR)resolve("vkCmdDispatchIndirect2KHR");
    if (!fp) { return; }
    fp(commandBuffer, pInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDispatchTileQCOM(VkCommandBuffer commandBuffer, const VkDispatchTileInfoQCOM* pDispatchTileInfo) {
    static PFN_vkCmdDispatchTileQCOM fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDispatchTileQCOM)resolve("vkCmdDispatchTileQCOM");
    if (!fp) { return; }
    fp(commandBuffer, pDispatchTileInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    static PFN_vkCmdDraw fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDraw)resolve("vkCmdDraw");
    if (!fp) { return; }
    fp(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawClusterHUAWEI(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    static PFN_vkCmdDrawClusterHUAWEI fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDrawClusterHUAWEI)resolve("vkCmdDrawClusterHUAWEI");
    if (!fp) { return; }
    fp(commandBuffer, groupCountX, groupCountY, groupCountZ);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawClusterIndirectHUAWEI(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset) {
    static PFN_vkCmdDrawClusterIndirectHUAWEI fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDrawClusterIndirectHUAWEI)resolve("vkCmdDrawClusterIndirectHUAWEI");
    if (!fp) { return; }
    fp(commandBuffer, buffer, offset);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    static PFN_vkCmdDrawIndexed fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDrawIndexed)resolve("vkCmdDrawIndexed");
    if (!fp) { return; }
    fp(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride) {
    static PFN_vkCmdDrawIndexedIndirect fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDrawIndexedIndirect)resolve("vkCmdDrawIndexedIndirect");
    if (!fp) { return; }
    fp(commandBuffer, buffer, offset, drawCount, stride);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirect2KHR(VkCommandBuffer commandBuffer, const VkDrawIndirect2InfoKHR* pInfo) {
    static PFN_vkCmdDrawIndexedIndirect2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDrawIndexedIndirect2KHR)resolve("vkCmdDrawIndexedIndirect2KHR");
    if (!fp) { return; }
    fp(commandBuffer, pInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride) {
    static PFN_vkCmdDrawIndexedIndirectCount fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDrawIndexedIndirectCount)resolve("vkCmdDrawIndexedIndirectCount");
    if (!fp) { return; }
    fp(commandBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirectCount2KHR(VkCommandBuffer commandBuffer, const VkDrawIndirectCount2InfoKHR* pInfo) {
    static PFN_vkCmdDrawIndexedIndirectCount2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDrawIndexedIndirectCount2KHR)resolve("vkCmdDrawIndexedIndirectCount2KHR");
    if (!fp) { return; }
    fp(commandBuffer, pInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirectCountAMD(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride) {
    static PFN_vkCmdDrawIndexedIndirectCountAMD fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDrawIndexedIndirectCountAMD)resolve("vkCmdDrawIndexedIndirectCountAMD");
    if (!fp) { return; }
    fp(commandBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirectCountKHR(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride) {
    static PFN_vkCmdDrawIndexedIndirectCountKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDrawIndexedIndirectCountKHR)resolve("vkCmdDrawIndexedIndirectCountKHR");
    if (!fp) { return; }
    fp(commandBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride) {
    static PFN_vkCmdDrawIndirect fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDrawIndirect)resolve("vkCmdDrawIndirect");
    if (!fp) { return; }
    fp(commandBuffer, buffer, offset, drawCount, stride);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirect2KHR(VkCommandBuffer commandBuffer, const VkDrawIndirect2InfoKHR* pInfo) {
    static PFN_vkCmdDrawIndirect2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDrawIndirect2KHR)resolve("vkCmdDrawIndirect2KHR");
    if (!fp) { return; }
    fp(commandBuffer, pInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirectByteCount2EXT(VkCommandBuffer commandBuffer, uint32_t instanceCount, uint32_t firstInstance, const VkBindTransformFeedbackBuffer2InfoEXT* pCounterInfo, uint32_t counterOffset, uint32_t vertexStride) {
    static PFN_vkCmdDrawIndirectByteCount2EXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDrawIndirectByteCount2EXT)resolve("vkCmdDrawIndirectByteCount2EXT");
    if (!fp) { return; }
    fp(commandBuffer, instanceCount, firstInstance, pCounterInfo, counterOffset, vertexStride);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirectByteCountEXT(VkCommandBuffer commandBuffer, uint32_t instanceCount, uint32_t firstInstance, VkBuffer counterBuffer, VkDeviceSize counterBufferOffset, uint32_t counterOffset, uint32_t vertexStride) {
    static PFN_vkCmdDrawIndirectByteCountEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDrawIndirectByteCountEXT)resolve("vkCmdDrawIndirectByteCountEXT");
    if (!fp) { return; }
    fp(commandBuffer, instanceCount, firstInstance, counterBuffer, counterBufferOffset, counterOffset, vertexStride);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride) {
    static PFN_vkCmdDrawIndirectCount fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDrawIndirectCount)resolve("vkCmdDrawIndirectCount");
    if (!fp) { return; }
    fp(commandBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirectCount2KHR(VkCommandBuffer commandBuffer, const VkDrawIndirectCount2InfoKHR* pInfo) {
    static PFN_vkCmdDrawIndirectCount2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDrawIndirectCount2KHR)resolve("vkCmdDrawIndirectCount2KHR");
    if (!fp) { return; }
    fp(commandBuffer, pInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirectCountAMD(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride) {
    static PFN_vkCmdDrawIndirectCountAMD fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDrawIndirectCountAMD)resolve("vkCmdDrawIndirectCountAMD");
    if (!fp) { return; }
    fp(commandBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirectCountKHR(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride) {
    static PFN_vkCmdDrawIndirectCountKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDrawIndirectCountKHR)resolve("vkCmdDrawIndirectCountKHR");
    if (!fp) { return; }
    fp(commandBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksEXT(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    static PFN_vkCmdDrawMeshTasksEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDrawMeshTasksEXT)resolve("vkCmdDrawMeshTasksEXT");
    if (!fp) { return; }
    fp(commandBuffer, groupCountX, groupCountY, groupCountZ);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksIndirect2EXT(VkCommandBuffer commandBuffer, const VkDrawIndirect2InfoKHR* pInfo) {
    static PFN_vkCmdDrawMeshTasksIndirect2EXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDrawMeshTasksIndirect2EXT)resolve("vkCmdDrawMeshTasksIndirect2EXT");
    if (!fp) { return; }
    fp(commandBuffer, pInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksIndirectCount2EXT(VkCommandBuffer commandBuffer, const VkDrawIndirectCount2InfoKHR* pInfo) {
    static PFN_vkCmdDrawMeshTasksIndirectCount2EXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDrawMeshTasksIndirectCount2EXT)resolve("vkCmdDrawMeshTasksIndirectCount2EXT");
    if (!fp) { return; }
    fp(commandBuffer, pInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksIndirectCountEXT(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride) {
    static PFN_vkCmdDrawMeshTasksIndirectCountEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDrawMeshTasksIndirectCountEXT)resolve("vkCmdDrawMeshTasksIndirectCountEXT");
    if (!fp) { return; }
    fp(commandBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksIndirectCountNV(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride) {
    static PFN_vkCmdDrawMeshTasksIndirectCountNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDrawMeshTasksIndirectCountNV)resolve("vkCmdDrawMeshTasksIndirectCountNV");
    if (!fp) { return; }
    fp(commandBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksIndirectEXT(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride) {
    static PFN_vkCmdDrawMeshTasksIndirectEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDrawMeshTasksIndirectEXT)resolve("vkCmdDrawMeshTasksIndirectEXT");
    if (!fp) { return; }
    fp(commandBuffer, buffer, offset, drawCount, stride);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksIndirectNV(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride) {
    static PFN_vkCmdDrawMeshTasksIndirectNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDrawMeshTasksIndirectNV)resolve("vkCmdDrawMeshTasksIndirectNV");
    if (!fp) { return; }
    fp(commandBuffer, buffer, offset, drawCount, stride);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksNV(VkCommandBuffer commandBuffer, uint32_t taskCount, uint32_t firstTask) {
    static PFN_vkCmdDrawMeshTasksNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDrawMeshTasksNV)resolve("vkCmdDrawMeshTasksNV");
    if (!fp) { return; }
    fp(commandBuffer, taskCount, firstTask);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawMultiEXT(VkCommandBuffer commandBuffer, uint32_t drawCount, const VkMultiDrawInfoEXT* pVertexInfo, uint32_t instanceCount, uint32_t firstInstance, uint32_t stride) {
    static PFN_vkCmdDrawMultiEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDrawMultiEXT)resolve("vkCmdDrawMultiEXT");
    if (!fp) { return; }
    fp(commandBuffer, drawCount, pVertexInfo, instanceCount, firstInstance, stride);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawMultiIndexedEXT(VkCommandBuffer commandBuffer, uint32_t drawCount, const VkMultiDrawIndexedInfoEXT* pIndexInfo, uint32_t instanceCount, uint32_t firstInstance, uint32_t stride, const int32_t* pVertexOffset) {
    static PFN_vkCmdDrawMultiIndexedEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdDrawMultiIndexedEXT)resolve("vkCmdDrawMultiIndexedEXT");
    if (!fp) { return; }
    fp(commandBuffer, drawCount, pIndexInfo, instanceCount, firstInstance, stride, pVertexOffset);
}
VKAPI_ATTR void VKAPI_CALL vkCmdEncodeVideoKHR(VkCommandBuffer commandBuffer, const VkVideoEncodeInfoKHR* pEncodeInfo) {
    static PFN_vkCmdEncodeVideoKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdEncodeVideoKHR)resolve("vkCmdEncodeVideoKHR");
    if (!fp) { return; }
    fp(commandBuffer, pEncodeInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdEndConditionalRenderingEXT(VkCommandBuffer commandBuffer) {
    static PFN_vkCmdEndConditionalRenderingEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdEndConditionalRenderingEXT)resolve("vkCmdEndConditionalRenderingEXT");
    if (!fp) { return; }
    fp(commandBuffer);
}
VKAPI_ATTR void VKAPI_CALL vkCmdEndDebugUtilsLabelEXT(VkCommandBuffer commandBuffer) {
    static PFN_vkCmdEndDebugUtilsLabelEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdEndDebugUtilsLabelEXT)resolve("vkCmdEndDebugUtilsLabelEXT");
    if (!fp) { return; }
    fp(commandBuffer);
}
VKAPI_ATTR void VKAPI_CALL vkCmdEndGpaSampleAMD(VkCommandBuffer commandBuffer, VkGpaSessionAMD gpaSession, uint32_t sampleID) {
    static PFN_vkCmdEndGpaSampleAMD fp = nullptr;
    if (!fp) fp = (PFN_vkCmdEndGpaSampleAMD)resolve("vkCmdEndGpaSampleAMD");
    if (!fp) { return; }
    fp(commandBuffer, gpaSession, sampleID);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCmdEndGpaSessionAMD(VkCommandBuffer commandBuffer, VkGpaSessionAMD gpaSession) {
    static PFN_vkCmdEndGpaSessionAMD fp = nullptr;
    if (!fp) fp = (PFN_vkCmdEndGpaSessionAMD)resolve("vkCmdEndGpaSessionAMD");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(commandBuffer, gpaSession);
}
VKAPI_ATTR void VKAPI_CALL vkCmdEndPerTileExecutionQCOM(VkCommandBuffer commandBuffer, const VkPerTileEndInfoQCOM* pPerTileEndInfo) {
    static PFN_vkCmdEndPerTileExecutionQCOM fp = nullptr;
    if (!fp) fp = (PFN_vkCmdEndPerTileExecutionQCOM)resolve("vkCmdEndPerTileExecutionQCOM");
    if (!fp) { return; }
    fp(commandBuffer, pPerTileEndInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query) {
    static PFN_vkCmdEndQuery fp = nullptr;
    if (!fp) fp = (PFN_vkCmdEndQuery)resolve("vkCmdEndQuery");
    if (!fp) { return; }
    fp(commandBuffer, queryPool, query);
}
VKAPI_ATTR void VKAPI_CALL vkCmdEndQueryIndexedEXT(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, uint32_t index) {
    static PFN_vkCmdEndQueryIndexedEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdEndQueryIndexedEXT)resolve("vkCmdEndQueryIndexedEXT");
    if (!fp) { return; }
    fp(commandBuffer, queryPool, query, index);
}
VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass(VkCommandBuffer commandBuffer) {
    static PFN_vkCmdEndRenderPass fp = nullptr;
    if (!fp) fp = (PFN_vkCmdEndRenderPass)resolve("vkCmdEndRenderPass");
    if (!fp) { return; }
    fp(commandBuffer);
}
VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass2(VkCommandBuffer commandBuffer, const VkSubpassEndInfo* pSubpassEndInfo) {
    static PFN_vkCmdEndRenderPass2 fp = nullptr;
    if (!fp) fp = (PFN_vkCmdEndRenderPass2)resolve("vkCmdEndRenderPass2");
    if (!fp) { return; }
    fp(commandBuffer, pSubpassEndInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass2KHR(VkCommandBuffer commandBuffer, const VkSubpassEndInfo* pSubpassEndInfo) {
    static PFN_vkCmdEndRenderPass2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdEndRenderPass2KHR)resolve("vkCmdEndRenderPass2KHR");
    if (!fp) { return; }
    fp(commandBuffer, pSubpassEndInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdEndRendering(VkCommandBuffer commandBuffer) {
    static PFN_vkCmdEndRendering fp = nullptr;
    if (!fp) fp = (PFN_vkCmdEndRendering)resolve("vkCmdEndRendering");
    if (!fp) { return; }
    fp(commandBuffer);
}
VKAPI_ATTR void VKAPI_CALL vkCmdEndRendering2EXT(VkCommandBuffer commandBuffer, const VkRenderingEndInfoKHR* pRenderingEndInfo) {
    static PFN_vkCmdEndRendering2EXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdEndRendering2EXT)resolve("vkCmdEndRendering2EXT");
    if (!fp) { return; }
    fp(commandBuffer, pRenderingEndInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdEndRendering2KHR(VkCommandBuffer commandBuffer, const VkRenderingEndInfoKHR* pRenderingEndInfo) {
    static PFN_vkCmdEndRendering2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdEndRendering2KHR)resolve("vkCmdEndRendering2KHR");
    if (!fp) { return; }
    fp(commandBuffer, pRenderingEndInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderingKHR(VkCommandBuffer commandBuffer) {
    static PFN_vkCmdEndRenderingKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdEndRenderingKHR)resolve("vkCmdEndRenderingKHR");
    if (!fp) { return; }
    fp(commandBuffer);
}
VKAPI_ATTR void VKAPI_CALL vkCmdEndShaderInstrumentationARM(VkCommandBuffer commandBuffer) {
    static PFN_vkCmdEndShaderInstrumentationARM fp = nullptr;
    if (!fp) fp = (PFN_vkCmdEndShaderInstrumentationARM)resolve("vkCmdEndShaderInstrumentationARM");
    if (!fp) { return; }
    fp(commandBuffer);
}
VKAPI_ATTR void VKAPI_CALL vkCmdEndTransformFeedback2EXT(VkCommandBuffer commandBuffer, uint32_t firstCounterRange, uint32_t counterRangeCount, const VkBindTransformFeedbackBuffer2InfoEXT* pCounterInfos) {
    static PFN_vkCmdEndTransformFeedback2EXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdEndTransformFeedback2EXT)resolve("vkCmdEndTransformFeedback2EXT");
    if (!fp) { return; }
    fp(commandBuffer, firstCounterRange, counterRangeCount, pCounterInfos);
}
VKAPI_ATTR void VKAPI_CALL vkCmdEndTransformFeedbackEXT(VkCommandBuffer commandBuffer, uint32_t firstCounterBuffer, uint32_t counterBufferCount, const VkBuffer* pCounterBuffers, const VkDeviceSize* pCounterBufferOffsets) {
    static PFN_vkCmdEndTransformFeedbackEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdEndTransformFeedbackEXT)resolve("vkCmdEndTransformFeedbackEXT");
    if (!fp) { return; }
    fp(commandBuffer, firstCounterBuffer, counterBufferCount, pCounterBuffers, pCounterBufferOffsets);
}
VKAPI_ATTR void VKAPI_CALL vkCmdEndVideoCodingKHR(VkCommandBuffer commandBuffer, const VkVideoEndCodingInfoKHR* pEndCodingInfo) {
    static PFN_vkCmdEndVideoCodingKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdEndVideoCodingKHR)resolve("vkCmdEndVideoCodingKHR");
    if (!fp) { return; }
    fp(commandBuffer, pEndCodingInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdExecuteCommands(VkCommandBuffer commandBuffer, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers) {
    static PFN_vkCmdExecuteCommands fp = nullptr;
    if (!fp) fp = (PFN_vkCmdExecuteCommands)resolve("vkCmdExecuteCommands");
    if (!fp) { return; }
    fp(commandBuffer, commandBufferCount, pCommandBuffers);
}
VKAPI_ATTR void VKAPI_CALL vkCmdExecuteGeneratedCommandsEXT(VkCommandBuffer commandBuffer, VkBool32 isPreprocessed, const VkGeneratedCommandsInfoEXT* pGeneratedCommandsInfo) {
    static PFN_vkCmdExecuteGeneratedCommandsEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdExecuteGeneratedCommandsEXT)resolve("vkCmdExecuteGeneratedCommandsEXT");
    if (!fp) { return; }
    fp(commandBuffer, isPreprocessed, pGeneratedCommandsInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdExecuteGeneratedCommandsNV(VkCommandBuffer commandBuffer, VkBool32 isPreprocessed, const VkGeneratedCommandsInfoNV* pGeneratedCommandsInfo) {
    static PFN_vkCmdExecuteGeneratedCommandsNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdExecuteGeneratedCommandsNV)resolve("vkCmdExecuteGeneratedCommandsNV");
    if (!fp) { return; }
    fp(commandBuffer, isPreprocessed, pGeneratedCommandsInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data) {
    static PFN_vkCmdFillBuffer fp = nullptr;
    if (!fp) fp = (PFN_vkCmdFillBuffer)resolve("vkCmdFillBuffer");
    if (!fp) { return; }
    fp(commandBuffer, dstBuffer, dstOffset, size, data);
}
VKAPI_ATTR void VKAPI_CALL vkCmdFillMemoryKHR(VkCommandBuffer commandBuffer, const VkDeviceAddressRangeKHR* pDstRange, VkAddressCommandFlagsKHR dstFlags, uint32_t data) {
    static PFN_vkCmdFillMemoryKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdFillMemoryKHR)resolve("vkCmdFillMemoryKHR");
    if (!fp) { return; }
    fp(commandBuffer, pDstRange, dstFlags, data);
}
VKAPI_ATTR void VKAPI_CALL vkCmdInsertDebugUtilsLabelEXT(VkCommandBuffer commandBuffer, const VkDebugUtilsLabelEXT* pLabelInfo) {
    static PFN_vkCmdInsertDebugUtilsLabelEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdInsertDebugUtilsLabelEXT)resolve("vkCmdInsertDebugUtilsLabelEXT");
    if (!fp) { return; }
    fp(commandBuffer, pLabelInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents) {
    static PFN_vkCmdNextSubpass fp = nullptr;
    if (!fp) fp = (PFN_vkCmdNextSubpass)resolve("vkCmdNextSubpass");
    if (!fp) { return; }
    fp(commandBuffer, contents);
}
VKAPI_ATTR void VKAPI_CALL vkCmdNextSubpass2(VkCommandBuffer commandBuffer, const VkSubpassBeginInfo* pSubpassBeginInfo, const VkSubpassEndInfo* pSubpassEndInfo) {
    static PFN_vkCmdNextSubpass2 fp = nullptr;
    if (!fp) fp = (PFN_vkCmdNextSubpass2)resolve("vkCmdNextSubpass2");
    if (!fp) { return; }
    fp(commandBuffer, pSubpassBeginInfo, pSubpassEndInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdNextSubpass2KHR(VkCommandBuffer commandBuffer, const VkSubpassBeginInfo* pSubpassBeginInfo, const VkSubpassEndInfo* pSubpassEndInfo) {
    static PFN_vkCmdNextSubpass2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdNextSubpass2KHR)resolve("vkCmdNextSubpass2KHR");
    if (!fp) { return; }
    fp(commandBuffer, pSubpassBeginInfo, pSubpassEndInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdOpticalFlowExecuteNV(VkCommandBuffer commandBuffer, VkOpticalFlowSessionNV session, const VkOpticalFlowExecuteInfoNV* pExecuteInfo) {
    static PFN_vkCmdOpticalFlowExecuteNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdOpticalFlowExecuteNV)resolve("vkCmdOpticalFlowExecuteNV");
    if (!fp) { return; }
    fp(commandBuffer, session, pExecuteInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers) {
    static PFN_vkCmdPipelineBarrier fp = nullptr;
    if (!fp) fp = (PFN_vkCmdPipelineBarrier)resolve("vkCmdPipelineBarrier");
    if (!fp) { return; }
    fp(commandBuffer, srcStageMask, dstStageMask, dependencyFlags, memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers);
}
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier2(VkCommandBuffer commandBuffer, const VkDependencyInfo* pDependencyInfo) {
    static PFN_vkCmdPipelineBarrier2 fp = nullptr;
    if (!fp) fp = (PFN_vkCmdPipelineBarrier2)resolve("vkCmdPipelineBarrier2");
    if (!fp) { return; }
    fp(commandBuffer, pDependencyInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier2KHR(VkCommandBuffer commandBuffer, const VkDependencyInfo* pDependencyInfo) {
    static PFN_vkCmdPipelineBarrier2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdPipelineBarrier2KHR)resolve("vkCmdPipelineBarrier2KHR");
    if (!fp) { return; }
    fp(commandBuffer, pDependencyInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdPreprocessGeneratedCommandsEXT(VkCommandBuffer commandBuffer, const VkGeneratedCommandsInfoEXT* pGeneratedCommandsInfo, VkCommandBuffer stateCommandBuffer) {
    static PFN_vkCmdPreprocessGeneratedCommandsEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdPreprocessGeneratedCommandsEXT)resolve("vkCmdPreprocessGeneratedCommandsEXT");
    if (!fp) { return; }
    fp(commandBuffer, pGeneratedCommandsInfo, stateCommandBuffer);
}
VKAPI_ATTR void VKAPI_CALL vkCmdPreprocessGeneratedCommandsNV(VkCommandBuffer commandBuffer, const VkGeneratedCommandsInfoNV* pGeneratedCommandsInfo) {
    static PFN_vkCmdPreprocessGeneratedCommandsNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdPreprocessGeneratedCommandsNV)resolve("vkCmdPreprocessGeneratedCommandsNV");
    if (!fp) { return; }
    fp(commandBuffer, pGeneratedCommandsInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues) {
    static PFN_vkCmdPushConstants fp = nullptr;
    if (!fp) fp = (PFN_vkCmdPushConstants)resolve("vkCmdPushConstants");
    if (!fp) { return; }
    fp(commandBuffer, layout, stageFlags, offset, size, pValues);
}
VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants2(VkCommandBuffer commandBuffer, const VkPushConstantsInfo* pPushConstantsInfo) {
    static PFN_vkCmdPushConstants2 fp = nullptr;
    if (!fp) fp = (PFN_vkCmdPushConstants2)resolve("vkCmdPushConstants2");
    if (!fp) { return; }
    fp(commandBuffer, pPushConstantsInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants2KHR(VkCommandBuffer commandBuffer, const VkPushConstantsInfo* pPushConstantsInfo) {
    static PFN_vkCmdPushConstants2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdPushConstants2KHR)resolve("vkCmdPushConstants2KHR");
    if (!fp) { return; }
    fp(commandBuffer, pPushConstantsInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdPushDataEXT(VkCommandBuffer commandBuffer, const VkPushDataInfoEXT* pPushDataInfo) {
    static PFN_vkCmdPushDataEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdPushDataEXT)resolve("vkCmdPushDataEXT");
    if (!fp) { return; }
    fp(commandBuffer, pPushDataInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSet(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites) {
    static PFN_vkCmdPushDescriptorSet fp = nullptr;
    if (!fp) fp = (PFN_vkCmdPushDescriptorSet)resolve("vkCmdPushDescriptorSet");
    if (!fp) { return; }
    fp(commandBuffer, pipelineBindPoint, layout, set, descriptorWriteCount, pDescriptorWrites);
}
VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSet2(VkCommandBuffer commandBuffer, const VkPushDescriptorSetInfo* pPushDescriptorSetInfo) {
    static PFN_vkCmdPushDescriptorSet2 fp = nullptr;
    if (!fp) fp = (PFN_vkCmdPushDescriptorSet2)resolve("vkCmdPushDescriptorSet2");
    if (!fp) { return; }
    fp(commandBuffer, pPushDescriptorSetInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSet2KHR(VkCommandBuffer commandBuffer, const VkPushDescriptorSetInfo* pPushDescriptorSetInfo) {
    static PFN_vkCmdPushDescriptorSet2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdPushDescriptorSet2KHR)resolve("vkCmdPushDescriptorSet2KHR");
    if (!fp) { return; }
    fp(commandBuffer, pPushDescriptorSetInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites) {
    static PFN_vkCmdPushDescriptorSetKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdPushDescriptorSetKHR)resolve("vkCmdPushDescriptorSetKHR");
    if (!fp) { return; }
    fp(commandBuffer, pipelineBindPoint, layout, set, descriptorWriteCount, pDescriptorWrites);
}
VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSetWithTemplate(VkCommandBuffer commandBuffer, VkDescriptorUpdateTemplate descriptorUpdateTemplate, VkPipelineLayout layout, uint32_t set, const void* pData) {
    static PFN_vkCmdPushDescriptorSetWithTemplate fp = nullptr;
    if (!fp) fp = (PFN_vkCmdPushDescriptorSetWithTemplate)resolve("vkCmdPushDescriptorSetWithTemplate");
    if (!fp) { return; }
    fp(commandBuffer, descriptorUpdateTemplate, layout, set, pData);
}
VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSetWithTemplate2(VkCommandBuffer commandBuffer, const VkPushDescriptorSetWithTemplateInfo* pPushDescriptorSetWithTemplateInfo) {
    static PFN_vkCmdPushDescriptorSetWithTemplate2 fp = nullptr;
    if (!fp) fp = (PFN_vkCmdPushDescriptorSetWithTemplate2)resolve("vkCmdPushDescriptorSetWithTemplate2");
    if (!fp) { return; }
    fp(commandBuffer, pPushDescriptorSetWithTemplateInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSetWithTemplate2KHR(VkCommandBuffer commandBuffer, const VkPushDescriptorSetWithTemplateInfo* pPushDescriptorSetWithTemplateInfo) {
    static PFN_vkCmdPushDescriptorSetWithTemplate2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdPushDescriptorSetWithTemplate2KHR)resolve("vkCmdPushDescriptorSetWithTemplate2KHR");
    if (!fp) { return; }
    fp(commandBuffer, pPushDescriptorSetWithTemplateInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer, VkDescriptorUpdateTemplate descriptorUpdateTemplate, VkPipelineLayout layout, uint32_t set, const void* pData) {
    static PFN_vkCmdPushDescriptorSetWithTemplateKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdPushDescriptorSetWithTemplateKHR)resolve("vkCmdPushDescriptorSetWithTemplateKHR");
    if (!fp) { return; }
    fp(commandBuffer, descriptorUpdateTemplate, layout, set, pData);
}
VKAPI_ATTR void VKAPI_CALL vkCmdResetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask) {
    static PFN_vkCmdResetEvent fp = nullptr;
    if (!fp) fp = (PFN_vkCmdResetEvent)resolve("vkCmdResetEvent");
    if (!fp) { return; }
    fp(commandBuffer, event, stageMask);
}
VKAPI_ATTR void VKAPI_CALL vkCmdResetEvent2(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags2 stageMask) {
    static PFN_vkCmdResetEvent2 fp = nullptr;
    if (!fp) fp = (PFN_vkCmdResetEvent2)resolve("vkCmdResetEvent2");
    if (!fp) { return; }
    fp(commandBuffer, event, stageMask);
}
VKAPI_ATTR void VKAPI_CALL vkCmdResetEvent2KHR(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags2 stageMask) {
    static PFN_vkCmdResetEvent2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdResetEvent2KHR)resolve("vkCmdResetEvent2KHR");
    if (!fp) { return; }
    fp(commandBuffer, event, stageMask);
}
VKAPI_ATTR void VKAPI_CALL vkCmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount) {
    static PFN_vkCmdResetQueryPool fp = nullptr;
    if (!fp) fp = (PFN_vkCmdResetQueryPool)resolve("vkCmdResetQueryPool");
    if (!fp) { return; }
    fp(commandBuffer, queryPool, firstQuery, queryCount);
}
VKAPI_ATTR void VKAPI_CALL vkCmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageResolve* pRegions) {
    static PFN_vkCmdResolveImage fp = nullptr;
    if (!fp) fp = (PFN_vkCmdResolveImage)resolve("vkCmdResolveImage");
    if (!fp) { return; }
    fp(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions);
}
VKAPI_ATTR void VKAPI_CALL vkCmdResolveImage2(VkCommandBuffer commandBuffer, const VkResolveImageInfo2* pResolveImageInfo) {
    static PFN_vkCmdResolveImage2 fp = nullptr;
    if (!fp) fp = (PFN_vkCmdResolveImage2)resolve("vkCmdResolveImage2");
    if (!fp) { return; }
    fp(commandBuffer, pResolveImageInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdResolveImage2KHR(VkCommandBuffer commandBuffer, const VkResolveImageInfo2* pResolveImageInfo) {
    static PFN_vkCmdResolveImage2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdResolveImage2KHR)resolve("vkCmdResolveImage2KHR");
    if (!fp) { return; }
    fp(commandBuffer, pResolveImageInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetAlphaToCoverageEnableEXT(VkCommandBuffer commandBuffer, VkBool32 alphaToCoverageEnable) {
    static PFN_vkCmdSetAlphaToCoverageEnableEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetAlphaToCoverageEnableEXT)resolve("vkCmdSetAlphaToCoverageEnableEXT");
    if (!fp) { return; }
    fp(commandBuffer, alphaToCoverageEnable);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetAlphaToOneEnableEXT(VkCommandBuffer commandBuffer, VkBool32 alphaToOneEnable) {
    static PFN_vkCmdSetAlphaToOneEnableEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetAlphaToOneEnableEXT)resolve("vkCmdSetAlphaToOneEnableEXT");
    if (!fp) { return; }
    fp(commandBuffer, alphaToOneEnable);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetAttachmentFeedbackLoopEnableEXT(VkCommandBuffer commandBuffer, VkImageAspectFlags aspectMask) {
    static PFN_vkCmdSetAttachmentFeedbackLoopEnableEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetAttachmentFeedbackLoopEnableEXT)resolve("vkCmdSetAttachmentFeedbackLoopEnableEXT");
    if (!fp) { return; }
    fp(commandBuffer, aspectMask);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetBlendConstants(VkCommandBuffer commandBuffer, const float blendConstants[4]) {
    static PFN_vkCmdSetBlendConstants fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetBlendConstants)resolve("vkCmdSetBlendConstants");
    if (!fp) { return; }
    fp(commandBuffer, blendConstants);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetCheckpointNV(VkCommandBuffer commandBuffer, const void* pCheckpointMarker) {
    static PFN_vkCmdSetCheckpointNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetCheckpointNV)resolve("vkCmdSetCheckpointNV");
    if (!fp) { return; }
    fp(commandBuffer, pCheckpointMarker);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetCoarseSampleOrderNV(VkCommandBuffer commandBuffer, VkCoarseSampleOrderTypeNV sampleOrderType, uint32_t customSampleOrderCount, const VkCoarseSampleOrderCustomNV* pCustomSampleOrders) {
    static PFN_vkCmdSetCoarseSampleOrderNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetCoarseSampleOrderNV)resolve("vkCmdSetCoarseSampleOrderNV");
    if (!fp) { return; }
    fp(commandBuffer, sampleOrderType, customSampleOrderCount, pCustomSampleOrders);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetColorBlendAdvancedEXT(VkCommandBuffer commandBuffer, uint32_t firstAttachment, uint32_t attachmentCount, const VkColorBlendAdvancedEXT* pColorBlendAdvanced) {
    static PFN_vkCmdSetColorBlendAdvancedEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetColorBlendAdvancedEXT)resolve("vkCmdSetColorBlendAdvancedEXT");
    if (!fp) { return; }
    fp(commandBuffer, firstAttachment, attachmentCount, pColorBlendAdvanced);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetColorBlendEnableEXT(VkCommandBuffer commandBuffer, uint32_t firstAttachment, uint32_t attachmentCount, const VkBool32* pColorBlendEnables) {
    static PFN_vkCmdSetColorBlendEnableEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetColorBlendEnableEXT)resolve("vkCmdSetColorBlendEnableEXT");
    if (!fp) { return; }
    fp(commandBuffer, firstAttachment, attachmentCount, pColorBlendEnables);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetColorBlendEquationEXT(VkCommandBuffer commandBuffer, uint32_t firstAttachment, uint32_t attachmentCount, const VkColorBlendEquationEXT* pColorBlendEquations) {
    static PFN_vkCmdSetColorBlendEquationEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetColorBlendEquationEXT)resolve("vkCmdSetColorBlendEquationEXT");
    if (!fp) { return; }
    fp(commandBuffer, firstAttachment, attachmentCount, pColorBlendEquations);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetColorWriteEnableEXT(VkCommandBuffer commandBuffer, uint32_t attachmentCount, const VkBool32* pColorWriteEnables) {
    static PFN_vkCmdSetColorWriteEnableEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetColorWriteEnableEXT)resolve("vkCmdSetColorWriteEnableEXT");
    if (!fp) { return; }
    fp(commandBuffer, attachmentCount, pColorWriteEnables);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetColorWriteMaskEXT(VkCommandBuffer commandBuffer, uint32_t firstAttachment, uint32_t attachmentCount, const VkColorComponentFlags* pColorWriteMasks) {
    static PFN_vkCmdSetColorWriteMaskEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetColorWriteMaskEXT)resolve("vkCmdSetColorWriteMaskEXT");
    if (!fp) { return; }
    fp(commandBuffer, firstAttachment, attachmentCount, pColorWriteMasks);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetComputeOccupancyPriorityNV(VkCommandBuffer commandBuffer, const VkComputeOccupancyPriorityParametersNV* pParameters) {
    static PFN_vkCmdSetComputeOccupancyPriorityNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetComputeOccupancyPriorityNV)resolve("vkCmdSetComputeOccupancyPriorityNV");
    if (!fp) { return; }
    fp(commandBuffer, pParameters);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetConservativeRasterizationModeEXT(VkCommandBuffer commandBuffer, VkConservativeRasterizationModeEXT conservativeRasterizationMode) {
    static PFN_vkCmdSetConservativeRasterizationModeEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetConservativeRasterizationModeEXT)resolve("vkCmdSetConservativeRasterizationModeEXT");
    if (!fp) { return; }
    fp(commandBuffer, conservativeRasterizationMode);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetCoverageModulationModeNV(VkCommandBuffer commandBuffer, VkCoverageModulationModeNV coverageModulationMode) {
    static PFN_vkCmdSetCoverageModulationModeNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetCoverageModulationModeNV)resolve("vkCmdSetCoverageModulationModeNV");
    if (!fp) { return; }
    fp(commandBuffer, coverageModulationMode);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetCoverageModulationTableEnableNV(VkCommandBuffer commandBuffer, VkBool32 coverageModulationTableEnable) {
    static PFN_vkCmdSetCoverageModulationTableEnableNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetCoverageModulationTableEnableNV)resolve("vkCmdSetCoverageModulationTableEnableNV");
    if (!fp) { return; }
    fp(commandBuffer, coverageModulationTableEnable);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetCoverageModulationTableNV(VkCommandBuffer commandBuffer, uint32_t coverageModulationTableCount, const float* pCoverageModulationTable) {
    static PFN_vkCmdSetCoverageModulationTableNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetCoverageModulationTableNV)resolve("vkCmdSetCoverageModulationTableNV");
    if (!fp) { return; }
    fp(commandBuffer, coverageModulationTableCount, pCoverageModulationTable);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetCoverageReductionModeNV(VkCommandBuffer commandBuffer, VkCoverageReductionModeNV coverageReductionMode) {
    static PFN_vkCmdSetCoverageReductionModeNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetCoverageReductionModeNV)resolve("vkCmdSetCoverageReductionModeNV");
    if (!fp) { return; }
    fp(commandBuffer, coverageReductionMode);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetCoverageToColorEnableNV(VkCommandBuffer commandBuffer, VkBool32 coverageToColorEnable) {
    static PFN_vkCmdSetCoverageToColorEnableNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetCoverageToColorEnableNV)resolve("vkCmdSetCoverageToColorEnableNV");
    if (!fp) { return; }
    fp(commandBuffer, coverageToColorEnable);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetCoverageToColorLocationNV(VkCommandBuffer commandBuffer, uint32_t coverageToColorLocation) {
    static PFN_vkCmdSetCoverageToColorLocationNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetCoverageToColorLocationNV)resolve("vkCmdSetCoverageToColorLocationNV");
    if (!fp) { return; }
    fp(commandBuffer, coverageToColorLocation);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetCullMode(VkCommandBuffer commandBuffer, VkCullModeFlags cullMode) {
    static PFN_vkCmdSetCullMode fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetCullMode)resolve("vkCmdSetCullMode");
    if (!fp) { return; }
    fp(commandBuffer, cullMode);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetCullModeEXT(VkCommandBuffer commandBuffer, VkCullModeFlags cullMode) {
    static PFN_vkCmdSetCullModeEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetCullModeEXT)resolve("vkCmdSetCullModeEXT");
    if (!fp) { return; }
    fp(commandBuffer, cullMode);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBias(VkCommandBuffer commandBuffer, float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor) {
    static PFN_vkCmdSetDepthBias fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetDepthBias)resolve("vkCmdSetDepthBias");
    if (!fp) { return; }
    fp(commandBuffer, depthBiasConstantFactor, depthBiasClamp, depthBiasSlopeFactor);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBias2EXT(VkCommandBuffer commandBuffer, const VkDepthBiasInfoEXT* pDepthBiasInfo) {
    static PFN_vkCmdSetDepthBias2EXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetDepthBias2EXT)resolve("vkCmdSetDepthBias2EXT");
    if (!fp) { return; }
    fp(commandBuffer, pDepthBiasInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBiasEnable(VkCommandBuffer commandBuffer, VkBool32 depthBiasEnable) {
    static PFN_vkCmdSetDepthBiasEnable fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetDepthBiasEnable)resolve("vkCmdSetDepthBiasEnable");
    if (!fp) { return; }
    fp(commandBuffer, depthBiasEnable);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBiasEnableEXT(VkCommandBuffer commandBuffer, VkBool32 depthBiasEnable) {
    static PFN_vkCmdSetDepthBiasEnableEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetDepthBiasEnableEXT)resolve("vkCmdSetDepthBiasEnableEXT");
    if (!fp) { return; }
    fp(commandBuffer, depthBiasEnable);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBounds(VkCommandBuffer commandBuffer, float minDepthBounds, float maxDepthBounds) {
    static PFN_vkCmdSetDepthBounds fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetDepthBounds)resolve("vkCmdSetDepthBounds");
    if (!fp) { return; }
    fp(commandBuffer, minDepthBounds, maxDepthBounds);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBoundsTestEnable(VkCommandBuffer commandBuffer, VkBool32 depthBoundsTestEnable) {
    static PFN_vkCmdSetDepthBoundsTestEnable fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetDepthBoundsTestEnable)resolve("vkCmdSetDepthBoundsTestEnable");
    if (!fp) { return; }
    fp(commandBuffer, depthBoundsTestEnable);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBoundsTestEnableEXT(VkCommandBuffer commandBuffer, VkBool32 depthBoundsTestEnable) {
    static PFN_vkCmdSetDepthBoundsTestEnableEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetDepthBoundsTestEnableEXT)resolve("vkCmdSetDepthBoundsTestEnableEXT");
    if (!fp) { return; }
    fp(commandBuffer, depthBoundsTestEnable);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthClampEnableEXT(VkCommandBuffer commandBuffer, VkBool32 depthClampEnable) {
    static PFN_vkCmdSetDepthClampEnableEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetDepthClampEnableEXT)resolve("vkCmdSetDepthClampEnableEXT");
    if (!fp) { return; }
    fp(commandBuffer, depthClampEnable);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthClampRangeEXT(VkCommandBuffer commandBuffer, VkDepthClampModeEXT depthClampMode, const VkDepthClampRangeEXT* pDepthClampRange) {
    static PFN_vkCmdSetDepthClampRangeEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetDepthClampRangeEXT)resolve("vkCmdSetDepthClampRangeEXT");
    if (!fp) { return; }
    fp(commandBuffer, depthClampMode, pDepthClampRange);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthClipEnableEXT(VkCommandBuffer commandBuffer, VkBool32 depthClipEnable) {
    static PFN_vkCmdSetDepthClipEnableEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetDepthClipEnableEXT)resolve("vkCmdSetDepthClipEnableEXT");
    if (!fp) { return; }
    fp(commandBuffer, depthClipEnable);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthClipNegativeOneToOneEXT(VkCommandBuffer commandBuffer, VkBool32 negativeOneToOne) {
    static PFN_vkCmdSetDepthClipNegativeOneToOneEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetDepthClipNegativeOneToOneEXT)resolve("vkCmdSetDepthClipNegativeOneToOneEXT");
    if (!fp) { return; }
    fp(commandBuffer, negativeOneToOne);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthCompareOp(VkCommandBuffer commandBuffer, VkCompareOp depthCompareOp) {
    static PFN_vkCmdSetDepthCompareOp fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetDepthCompareOp)resolve("vkCmdSetDepthCompareOp");
    if (!fp) { return; }
    fp(commandBuffer, depthCompareOp);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthCompareOpEXT(VkCommandBuffer commandBuffer, VkCompareOp depthCompareOp) {
    static PFN_vkCmdSetDepthCompareOpEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetDepthCompareOpEXT)resolve("vkCmdSetDepthCompareOpEXT");
    if (!fp) { return; }
    fp(commandBuffer, depthCompareOp);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthTestEnable(VkCommandBuffer commandBuffer, VkBool32 depthTestEnable) {
    static PFN_vkCmdSetDepthTestEnable fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetDepthTestEnable)resolve("vkCmdSetDepthTestEnable");
    if (!fp) { return; }
    fp(commandBuffer, depthTestEnable);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthTestEnableEXT(VkCommandBuffer commandBuffer, VkBool32 depthTestEnable) {
    static PFN_vkCmdSetDepthTestEnableEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetDepthTestEnableEXT)resolve("vkCmdSetDepthTestEnableEXT");
    if (!fp) { return; }
    fp(commandBuffer, depthTestEnable);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthWriteEnable(VkCommandBuffer commandBuffer, VkBool32 depthWriteEnable) {
    static PFN_vkCmdSetDepthWriteEnable fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetDepthWriteEnable)resolve("vkCmdSetDepthWriteEnable");
    if (!fp) { return; }
    fp(commandBuffer, depthWriteEnable);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthWriteEnableEXT(VkCommandBuffer commandBuffer, VkBool32 depthWriteEnable) {
    static PFN_vkCmdSetDepthWriteEnableEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetDepthWriteEnableEXT)resolve("vkCmdSetDepthWriteEnableEXT");
    if (!fp) { return; }
    fp(commandBuffer, depthWriteEnable);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetDescriptorBufferOffsets2EXT(VkCommandBuffer commandBuffer, const VkSetDescriptorBufferOffsetsInfoEXT* pSetDescriptorBufferOffsetsInfo) {
    static PFN_vkCmdSetDescriptorBufferOffsets2EXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetDescriptorBufferOffsets2EXT)resolve("vkCmdSetDescriptorBufferOffsets2EXT");
    if (!fp) { return; }
    fp(commandBuffer, pSetDescriptorBufferOffsetsInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetDescriptorBufferOffsetsEXT(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t setCount, const uint32_t* pBufferIndices, const VkDeviceSize* pOffsets) {
    static PFN_vkCmdSetDescriptorBufferOffsetsEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetDescriptorBufferOffsetsEXT)resolve("vkCmdSetDescriptorBufferOffsetsEXT");
    if (!fp) { return; }
    fp(commandBuffer, pipelineBindPoint, layout, firstSet, setCount, pBufferIndices, pOffsets);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetDeviceMask(VkCommandBuffer commandBuffer, uint32_t deviceMask) {
    static PFN_vkCmdSetDeviceMask fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetDeviceMask)resolve("vkCmdSetDeviceMask");
    if (!fp) { return; }
    fp(commandBuffer, deviceMask);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetDeviceMaskKHR(VkCommandBuffer commandBuffer, uint32_t deviceMask) {
    static PFN_vkCmdSetDeviceMaskKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetDeviceMaskKHR)resolve("vkCmdSetDeviceMaskKHR");
    if (!fp) { return; }
    fp(commandBuffer, deviceMask);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetDiscardRectangleEXT(VkCommandBuffer commandBuffer, uint32_t firstDiscardRectangle, uint32_t discardRectangleCount, const VkRect2D* pDiscardRectangles) {
    static PFN_vkCmdSetDiscardRectangleEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetDiscardRectangleEXT)resolve("vkCmdSetDiscardRectangleEXT");
    if (!fp) { return; }
    fp(commandBuffer, firstDiscardRectangle, discardRectangleCount, pDiscardRectangles);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetDiscardRectangleEnableEXT(VkCommandBuffer commandBuffer, VkBool32 discardRectangleEnable) {
    static PFN_vkCmdSetDiscardRectangleEnableEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetDiscardRectangleEnableEXT)resolve("vkCmdSetDiscardRectangleEnableEXT");
    if (!fp) { return; }
    fp(commandBuffer, discardRectangleEnable);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetDiscardRectangleModeEXT(VkCommandBuffer commandBuffer, VkDiscardRectangleModeEXT discardRectangleMode) {
    static PFN_vkCmdSetDiscardRectangleModeEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetDiscardRectangleModeEXT)resolve("vkCmdSetDiscardRectangleModeEXT");
    if (!fp) { return; }
    fp(commandBuffer, discardRectangleMode);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetDispatchParametersARM(VkCommandBuffer commandBuffer, const VkDispatchParametersARM* pDispatchParameters) {
    static PFN_vkCmdSetDispatchParametersARM fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetDispatchParametersARM)resolve("vkCmdSetDispatchParametersARM");
    if (!fp) { return; }
    fp(commandBuffer, pDispatchParameters);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask) {
    static PFN_vkCmdSetEvent fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetEvent)resolve("vkCmdSetEvent");
    if (!fp) { return; }
    fp(commandBuffer, event, stageMask);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetEvent2(VkCommandBuffer commandBuffer, VkEvent event, const VkDependencyInfo* pDependencyInfo) {
    static PFN_vkCmdSetEvent2 fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetEvent2)resolve("vkCmdSetEvent2");
    if (!fp) { return; }
    fp(commandBuffer, event, pDependencyInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetEvent2KHR(VkCommandBuffer commandBuffer, VkEvent event, const VkDependencyInfo* pDependencyInfo) {
    static PFN_vkCmdSetEvent2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetEvent2KHR)resolve("vkCmdSetEvent2KHR");
    if (!fp) { return; }
    fp(commandBuffer, event, pDependencyInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetExclusiveScissorEnableNV(VkCommandBuffer commandBuffer, uint32_t firstExclusiveScissor, uint32_t exclusiveScissorCount, const VkBool32* pExclusiveScissorEnables) {
    static PFN_vkCmdSetExclusiveScissorEnableNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetExclusiveScissorEnableNV)resolve("vkCmdSetExclusiveScissorEnableNV");
    if (!fp) { return; }
    fp(commandBuffer, firstExclusiveScissor, exclusiveScissorCount, pExclusiveScissorEnables);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetExclusiveScissorNV(VkCommandBuffer commandBuffer, uint32_t firstExclusiveScissor, uint32_t exclusiveScissorCount, const VkRect2D* pExclusiveScissors) {
    static PFN_vkCmdSetExclusiveScissorNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetExclusiveScissorNV)resolve("vkCmdSetExclusiveScissorNV");
    if (!fp) { return; }
    fp(commandBuffer, firstExclusiveScissor, exclusiveScissorCount, pExclusiveScissors);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetExtraPrimitiveOverestimationSizeEXT(VkCommandBuffer commandBuffer, float extraPrimitiveOverestimationSize) {
    static PFN_vkCmdSetExtraPrimitiveOverestimationSizeEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetExtraPrimitiveOverestimationSizeEXT)resolve("vkCmdSetExtraPrimitiveOverestimationSizeEXT");
    if (!fp) { return; }
    fp(commandBuffer, extraPrimitiveOverestimationSize);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetFragmentShadingRateEnumNV(VkCommandBuffer commandBuffer, VkFragmentShadingRateNV shadingRate, const VkFragmentShadingRateCombinerOpKHR combinerOps[2]) {
    static PFN_vkCmdSetFragmentShadingRateEnumNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetFragmentShadingRateEnumNV)resolve("vkCmdSetFragmentShadingRateEnumNV");
    if (!fp) { return; }
    fp(commandBuffer, shadingRate, combinerOps);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetFragmentShadingRateKHR(VkCommandBuffer commandBuffer, const VkExtent2D* pFragmentSize, const VkFragmentShadingRateCombinerOpKHR combinerOps[2]) {
    static PFN_vkCmdSetFragmentShadingRateKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetFragmentShadingRateKHR)resolve("vkCmdSetFragmentShadingRateKHR");
    if (!fp) { return; }
    fp(commandBuffer, pFragmentSize, combinerOps);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetFrontFace(VkCommandBuffer commandBuffer, VkFrontFace frontFace) {
    static PFN_vkCmdSetFrontFace fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetFrontFace)resolve("vkCmdSetFrontFace");
    if (!fp) { return; }
    fp(commandBuffer, frontFace);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetFrontFaceEXT(VkCommandBuffer commandBuffer, VkFrontFace frontFace) {
    static PFN_vkCmdSetFrontFaceEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetFrontFaceEXT)resolve("vkCmdSetFrontFaceEXT");
    if (!fp) { return; }
    fp(commandBuffer, frontFace);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetLineRasterizationModeEXT(VkCommandBuffer commandBuffer, VkLineRasterizationModeEXT lineRasterizationMode) {
    static PFN_vkCmdSetLineRasterizationModeEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetLineRasterizationModeEXT)resolve("vkCmdSetLineRasterizationModeEXT");
    if (!fp) { return; }
    fp(commandBuffer, lineRasterizationMode);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetLineStipple(VkCommandBuffer commandBuffer, uint32_t lineStippleFactor, uint16_t lineStipplePattern) {
    static PFN_vkCmdSetLineStipple fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetLineStipple)resolve("vkCmdSetLineStipple");
    if (!fp) { return; }
    fp(commandBuffer, lineStippleFactor, lineStipplePattern);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetLineStippleEXT(VkCommandBuffer commandBuffer, uint32_t lineStippleFactor, uint16_t lineStipplePattern) {
    static PFN_vkCmdSetLineStippleEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetLineStippleEXT)resolve("vkCmdSetLineStippleEXT");
    if (!fp) { return; }
    fp(commandBuffer, lineStippleFactor, lineStipplePattern);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetLineStippleEnableEXT(VkCommandBuffer commandBuffer, VkBool32 stippledLineEnable) {
    static PFN_vkCmdSetLineStippleEnableEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetLineStippleEnableEXT)resolve("vkCmdSetLineStippleEnableEXT");
    if (!fp) { return; }
    fp(commandBuffer, stippledLineEnable);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetLineStippleKHR(VkCommandBuffer commandBuffer, uint32_t lineStippleFactor, uint16_t lineStipplePattern) {
    static PFN_vkCmdSetLineStippleKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetLineStippleKHR)resolve("vkCmdSetLineStippleKHR");
    if (!fp) { return; }
    fp(commandBuffer, lineStippleFactor, lineStipplePattern);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth) {
    static PFN_vkCmdSetLineWidth fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetLineWidth)resolve("vkCmdSetLineWidth");
    if (!fp) { return; }
    fp(commandBuffer, lineWidth);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetLogicOpEXT(VkCommandBuffer commandBuffer, VkLogicOp logicOp) {
    static PFN_vkCmdSetLogicOpEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetLogicOpEXT)resolve("vkCmdSetLogicOpEXT");
    if (!fp) { return; }
    fp(commandBuffer, logicOp);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetLogicOpEnableEXT(VkCommandBuffer commandBuffer, VkBool32 logicOpEnable) {
    static PFN_vkCmdSetLogicOpEnableEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetLogicOpEnableEXT)resolve("vkCmdSetLogicOpEnableEXT");
    if (!fp) { return; }
    fp(commandBuffer, logicOpEnable);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetPatchControlPointsEXT(VkCommandBuffer commandBuffer, uint32_t patchControlPoints) {
    static PFN_vkCmdSetPatchControlPointsEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetPatchControlPointsEXT)resolve("vkCmdSetPatchControlPointsEXT");
    if (!fp) { return; }
    fp(commandBuffer, patchControlPoints);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCmdSetPerformanceMarkerINTEL(VkCommandBuffer commandBuffer, const VkPerformanceMarkerInfoINTEL* pMarkerInfo) {
    static PFN_vkCmdSetPerformanceMarkerINTEL fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetPerformanceMarkerINTEL)resolve("vkCmdSetPerformanceMarkerINTEL");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(commandBuffer, pMarkerInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCmdSetPerformanceOverrideINTEL(VkCommandBuffer commandBuffer, const VkPerformanceOverrideInfoINTEL* pOverrideInfo) {
    static PFN_vkCmdSetPerformanceOverrideINTEL fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetPerformanceOverrideINTEL)resolve("vkCmdSetPerformanceOverrideINTEL");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(commandBuffer, pOverrideInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCmdSetPerformanceStreamMarkerINTEL(VkCommandBuffer commandBuffer, const VkPerformanceStreamMarkerInfoINTEL* pMarkerInfo) {
    static PFN_vkCmdSetPerformanceStreamMarkerINTEL fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetPerformanceStreamMarkerINTEL)resolve("vkCmdSetPerformanceStreamMarkerINTEL");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(commandBuffer, pMarkerInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetPolygonModeEXT(VkCommandBuffer commandBuffer, VkPolygonMode polygonMode) {
    static PFN_vkCmdSetPolygonModeEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetPolygonModeEXT)resolve("vkCmdSetPolygonModeEXT");
    if (!fp) { return; }
    fp(commandBuffer, polygonMode);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetPrimitiveRestartEnable(VkCommandBuffer commandBuffer, VkBool32 primitiveRestartEnable) {
    static PFN_vkCmdSetPrimitiveRestartEnable fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetPrimitiveRestartEnable)resolve("vkCmdSetPrimitiveRestartEnable");
    if (!fp) { return; }
    fp(commandBuffer, primitiveRestartEnable);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetPrimitiveRestartEnableEXT(VkCommandBuffer commandBuffer, VkBool32 primitiveRestartEnable) {
    static PFN_vkCmdSetPrimitiveRestartEnableEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetPrimitiveRestartEnableEXT)resolve("vkCmdSetPrimitiveRestartEnableEXT");
    if (!fp) { return; }
    fp(commandBuffer, primitiveRestartEnable);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetPrimitiveRestartIndexEXT(VkCommandBuffer commandBuffer, uint32_t primitiveRestartIndex) {
    static PFN_vkCmdSetPrimitiveRestartIndexEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetPrimitiveRestartIndexEXT)resolve("vkCmdSetPrimitiveRestartIndexEXT");
    if (!fp) { return; }
    fp(commandBuffer, primitiveRestartIndex);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetPrimitiveTopology(VkCommandBuffer commandBuffer, VkPrimitiveTopology primitiveTopology) {
    static PFN_vkCmdSetPrimitiveTopology fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetPrimitiveTopology)resolve("vkCmdSetPrimitiveTopology");
    if (!fp) { return; }
    fp(commandBuffer, primitiveTopology);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetPrimitiveTopologyEXT(VkCommandBuffer commandBuffer, VkPrimitiveTopology primitiveTopology) {
    static PFN_vkCmdSetPrimitiveTopologyEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetPrimitiveTopologyEXT)resolve("vkCmdSetPrimitiveTopologyEXT");
    if (!fp) { return; }
    fp(commandBuffer, primitiveTopology);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetProvokingVertexModeEXT(VkCommandBuffer commandBuffer, VkProvokingVertexModeEXT provokingVertexMode) {
    static PFN_vkCmdSetProvokingVertexModeEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetProvokingVertexModeEXT)resolve("vkCmdSetProvokingVertexModeEXT");
    if (!fp) { return; }
    fp(commandBuffer, provokingVertexMode);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetRasterizationSamplesEXT(VkCommandBuffer commandBuffer, VkSampleCountFlagBits rasterizationSamples) {
    static PFN_vkCmdSetRasterizationSamplesEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetRasterizationSamplesEXT)resolve("vkCmdSetRasterizationSamplesEXT");
    if (!fp) { return; }
    fp(commandBuffer, rasterizationSamples);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetRasterizationStreamEXT(VkCommandBuffer commandBuffer, uint32_t rasterizationStream) {
    static PFN_vkCmdSetRasterizationStreamEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetRasterizationStreamEXT)resolve("vkCmdSetRasterizationStreamEXT");
    if (!fp) { return; }
    fp(commandBuffer, rasterizationStream);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetRasterizerDiscardEnable(VkCommandBuffer commandBuffer, VkBool32 rasterizerDiscardEnable) {
    static PFN_vkCmdSetRasterizerDiscardEnable fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetRasterizerDiscardEnable)resolve("vkCmdSetRasterizerDiscardEnable");
    if (!fp) { return; }
    fp(commandBuffer, rasterizerDiscardEnable);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetRasterizerDiscardEnableEXT(VkCommandBuffer commandBuffer, VkBool32 rasterizerDiscardEnable) {
    static PFN_vkCmdSetRasterizerDiscardEnableEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetRasterizerDiscardEnableEXT)resolve("vkCmdSetRasterizerDiscardEnableEXT");
    if (!fp) { return; }
    fp(commandBuffer, rasterizerDiscardEnable);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetRayTracingPipelineStackSizeKHR(VkCommandBuffer commandBuffer, uint32_t pipelineStackSize) {
    static PFN_vkCmdSetRayTracingPipelineStackSizeKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetRayTracingPipelineStackSizeKHR)resolve("vkCmdSetRayTracingPipelineStackSizeKHR");
    if (!fp) { return; }
    fp(commandBuffer, pipelineStackSize);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetRenderingAttachmentLocations(VkCommandBuffer commandBuffer, const VkRenderingAttachmentLocationInfo* pLocationInfo) {
    static PFN_vkCmdSetRenderingAttachmentLocations fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetRenderingAttachmentLocations)resolve("vkCmdSetRenderingAttachmentLocations");
    if (!fp) { return; }
    fp(commandBuffer, pLocationInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetRenderingAttachmentLocationsKHR(VkCommandBuffer commandBuffer, const VkRenderingAttachmentLocationInfo* pLocationInfo) {
    static PFN_vkCmdSetRenderingAttachmentLocationsKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetRenderingAttachmentLocationsKHR)resolve("vkCmdSetRenderingAttachmentLocationsKHR");
    if (!fp) { return; }
    fp(commandBuffer, pLocationInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetRenderingInputAttachmentIndices(VkCommandBuffer commandBuffer, const VkRenderingInputAttachmentIndexInfo* pInputAttachmentIndexInfo) {
    static PFN_vkCmdSetRenderingInputAttachmentIndices fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetRenderingInputAttachmentIndices)resolve("vkCmdSetRenderingInputAttachmentIndices");
    if (!fp) { return; }
    fp(commandBuffer, pInputAttachmentIndexInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetRenderingInputAttachmentIndicesKHR(VkCommandBuffer commandBuffer, const VkRenderingInputAttachmentIndexInfo* pInputAttachmentIndexInfo) {
    static PFN_vkCmdSetRenderingInputAttachmentIndicesKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetRenderingInputAttachmentIndicesKHR)resolve("vkCmdSetRenderingInputAttachmentIndicesKHR");
    if (!fp) { return; }
    fp(commandBuffer, pInputAttachmentIndexInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetRepresentativeFragmentTestEnableNV(VkCommandBuffer commandBuffer, VkBool32 representativeFragmentTestEnable) {
    static PFN_vkCmdSetRepresentativeFragmentTestEnableNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetRepresentativeFragmentTestEnableNV)resolve("vkCmdSetRepresentativeFragmentTestEnableNV");
    if (!fp) { return; }
    fp(commandBuffer, representativeFragmentTestEnable);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetSampleLocationsEXT(VkCommandBuffer commandBuffer, const VkSampleLocationsInfoEXT* pSampleLocationsInfo) {
    static PFN_vkCmdSetSampleLocationsEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetSampleLocationsEXT)resolve("vkCmdSetSampleLocationsEXT");
    if (!fp) { return; }
    fp(commandBuffer, pSampleLocationsInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetSampleLocationsEnableEXT(VkCommandBuffer commandBuffer, VkBool32 sampleLocationsEnable) {
    static PFN_vkCmdSetSampleLocationsEnableEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetSampleLocationsEnableEXT)resolve("vkCmdSetSampleLocationsEnableEXT");
    if (!fp) { return; }
    fp(commandBuffer, sampleLocationsEnable);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetSampleMaskEXT(VkCommandBuffer commandBuffer, VkSampleCountFlagBits samples, const VkSampleMask* pSampleMask) {
    static PFN_vkCmdSetSampleMaskEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetSampleMaskEXT)resolve("vkCmdSetSampleMaskEXT");
    if (!fp) { return; }
    fp(commandBuffer, samples, pSampleMask);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D* pScissors) {
    static PFN_vkCmdSetScissor fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetScissor)resolve("vkCmdSetScissor");
    if (!fp) { return; }
    fp(commandBuffer, firstScissor, scissorCount, pScissors);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetScissorWithCount(VkCommandBuffer commandBuffer, uint32_t scissorCount, const VkRect2D* pScissors) {
    static PFN_vkCmdSetScissorWithCount fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetScissorWithCount)resolve("vkCmdSetScissorWithCount");
    if (!fp) { return; }
    fp(commandBuffer, scissorCount, pScissors);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetScissorWithCountEXT(VkCommandBuffer commandBuffer, uint32_t scissorCount, const VkRect2D* pScissors) {
    static PFN_vkCmdSetScissorWithCountEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetScissorWithCountEXT)resolve("vkCmdSetScissorWithCountEXT");
    if (!fp) { return; }
    fp(commandBuffer, scissorCount, pScissors);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetShadingRateImageEnableNV(VkCommandBuffer commandBuffer, VkBool32 shadingRateImageEnable) {
    static PFN_vkCmdSetShadingRateImageEnableNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetShadingRateImageEnableNV)resolve("vkCmdSetShadingRateImageEnableNV");
    if (!fp) { return; }
    fp(commandBuffer, shadingRateImageEnable);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilCompareMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t compareMask) {
    static PFN_vkCmdSetStencilCompareMask fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetStencilCompareMask)resolve("vkCmdSetStencilCompareMask");
    if (!fp) { return; }
    fp(commandBuffer, faceMask, compareMask);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilOp(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, VkStencilOp failOp, VkStencilOp passOp, VkStencilOp depthFailOp, VkCompareOp compareOp) {
    static PFN_vkCmdSetStencilOp fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetStencilOp)resolve("vkCmdSetStencilOp");
    if (!fp) { return; }
    fp(commandBuffer, faceMask, failOp, passOp, depthFailOp, compareOp);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilOpEXT(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, VkStencilOp failOp, VkStencilOp passOp, VkStencilOp depthFailOp, VkCompareOp compareOp) {
    static PFN_vkCmdSetStencilOpEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetStencilOpEXT)resolve("vkCmdSetStencilOpEXT");
    if (!fp) { return; }
    fp(commandBuffer, faceMask, failOp, passOp, depthFailOp, compareOp);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilReference(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t reference) {
    static PFN_vkCmdSetStencilReference fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetStencilReference)resolve("vkCmdSetStencilReference");
    if (!fp) { return; }
    fp(commandBuffer, faceMask, reference);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilTestEnable(VkCommandBuffer commandBuffer, VkBool32 stencilTestEnable) {
    static PFN_vkCmdSetStencilTestEnable fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetStencilTestEnable)resolve("vkCmdSetStencilTestEnable");
    if (!fp) { return; }
    fp(commandBuffer, stencilTestEnable);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilTestEnableEXT(VkCommandBuffer commandBuffer, VkBool32 stencilTestEnable) {
    static PFN_vkCmdSetStencilTestEnableEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetStencilTestEnableEXT)resolve("vkCmdSetStencilTestEnableEXT");
    if (!fp) { return; }
    fp(commandBuffer, stencilTestEnable);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilWriteMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t writeMask) {
    static PFN_vkCmdSetStencilWriteMask fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetStencilWriteMask)resolve("vkCmdSetStencilWriteMask");
    if (!fp) { return; }
    fp(commandBuffer, faceMask, writeMask);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetTessellationDomainOriginEXT(VkCommandBuffer commandBuffer, VkTessellationDomainOrigin domainOrigin) {
    static PFN_vkCmdSetTessellationDomainOriginEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetTessellationDomainOriginEXT)resolve("vkCmdSetTessellationDomainOriginEXT");
    if (!fp) { return; }
    fp(commandBuffer, domainOrigin);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetVertexInputEXT(VkCommandBuffer commandBuffer, uint32_t vertexBindingDescriptionCount, const VkVertexInputBindingDescription2EXT* pVertexBindingDescriptions, uint32_t vertexAttributeDescriptionCount, const VkVertexInputAttributeDescription2EXT* pVertexAttributeDescriptions) {
    static PFN_vkCmdSetVertexInputEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetVertexInputEXT)resolve("vkCmdSetVertexInputEXT");
    if (!fp) { return; }
    fp(commandBuffer, vertexBindingDescriptionCount, pVertexBindingDescriptions, vertexAttributeDescriptionCount, pVertexAttributeDescriptions);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* pViewports) {
    static PFN_vkCmdSetViewport fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetViewport)resolve("vkCmdSetViewport");
    if (!fp) { return; }
    fp(commandBuffer, firstViewport, viewportCount, pViewports);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetViewportShadingRatePaletteNV(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkShadingRatePaletteNV* pShadingRatePalettes) {
    static PFN_vkCmdSetViewportShadingRatePaletteNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetViewportShadingRatePaletteNV)resolve("vkCmdSetViewportShadingRatePaletteNV");
    if (!fp) { return; }
    fp(commandBuffer, firstViewport, viewportCount, pShadingRatePalettes);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetViewportSwizzleNV(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewportSwizzleNV* pViewportSwizzles) {
    static PFN_vkCmdSetViewportSwizzleNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetViewportSwizzleNV)resolve("vkCmdSetViewportSwizzleNV");
    if (!fp) { return; }
    fp(commandBuffer, firstViewport, viewportCount, pViewportSwizzles);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetViewportWScalingEnableNV(VkCommandBuffer commandBuffer, VkBool32 viewportWScalingEnable) {
    static PFN_vkCmdSetViewportWScalingEnableNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetViewportWScalingEnableNV)resolve("vkCmdSetViewportWScalingEnableNV");
    if (!fp) { return; }
    fp(commandBuffer, viewportWScalingEnable);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetViewportWScalingNV(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewportWScalingNV* pViewportWScalings) {
    static PFN_vkCmdSetViewportWScalingNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetViewportWScalingNV)resolve("vkCmdSetViewportWScalingNV");
    if (!fp) { return; }
    fp(commandBuffer, firstViewport, viewportCount, pViewportWScalings);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetViewportWithCount(VkCommandBuffer commandBuffer, uint32_t viewportCount, const VkViewport* pViewports) {
    static PFN_vkCmdSetViewportWithCount fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetViewportWithCount)resolve("vkCmdSetViewportWithCount");
    if (!fp) { return; }
    fp(commandBuffer, viewportCount, pViewports);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetViewportWithCountEXT(VkCommandBuffer commandBuffer, uint32_t viewportCount, const VkViewport* pViewports) {
    static PFN_vkCmdSetViewportWithCountEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSetViewportWithCountEXT)resolve("vkCmdSetViewportWithCountEXT");
    if (!fp) { return; }
    fp(commandBuffer, viewportCount, pViewports);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSubpassShadingHUAWEI(VkCommandBuffer commandBuffer) {
    static PFN_vkCmdSubpassShadingHUAWEI fp = nullptr;
    if (!fp) fp = (PFN_vkCmdSubpassShadingHUAWEI)resolve("vkCmdSubpassShadingHUAWEI");
    if (!fp) { return; }
    fp(commandBuffer);
}
VKAPI_ATTR void VKAPI_CALL vkCmdTraceRaysIndirect2KHR(VkCommandBuffer commandBuffer, VkDeviceAddress indirectDeviceAddress) {
    static PFN_vkCmdTraceRaysIndirect2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdTraceRaysIndirect2KHR)resolve("vkCmdTraceRaysIndirect2KHR");
    if (!fp) { return; }
    fp(commandBuffer, indirectDeviceAddress);
}
VKAPI_ATTR void VKAPI_CALL vkCmdTraceRaysIndirectKHR(VkCommandBuffer commandBuffer, const VkStridedDeviceAddressRegionKHR* pRaygenShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pMissShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pHitShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pCallableShaderBindingTable, VkDeviceAddress indirectDeviceAddress) {
    static PFN_vkCmdTraceRaysIndirectKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdTraceRaysIndirectKHR)resolve("vkCmdTraceRaysIndirectKHR");
    if (!fp) { return; }
    fp(commandBuffer, pRaygenShaderBindingTable, pMissShaderBindingTable, pHitShaderBindingTable, pCallableShaderBindingTable, indirectDeviceAddress);
}
VKAPI_ATTR void VKAPI_CALL vkCmdTraceRaysKHR(VkCommandBuffer commandBuffer, const VkStridedDeviceAddressRegionKHR* pRaygenShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pMissShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pHitShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pCallableShaderBindingTable, uint32_t width, uint32_t height, uint32_t depth) {
    static PFN_vkCmdTraceRaysKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdTraceRaysKHR)resolve("vkCmdTraceRaysKHR");
    if (!fp) { return; }
    fp(commandBuffer, pRaygenShaderBindingTable, pMissShaderBindingTable, pHitShaderBindingTable, pCallableShaderBindingTable, width, height, depth);
}
VKAPI_ATTR void VKAPI_CALL vkCmdTraceRaysNV(VkCommandBuffer commandBuffer, VkBuffer raygenShaderBindingTableBuffer, VkDeviceSize raygenShaderBindingOffset, VkBuffer missShaderBindingTableBuffer, VkDeviceSize missShaderBindingOffset, VkDeviceSize missShaderBindingStride, VkBuffer hitShaderBindingTableBuffer, VkDeviceSize hitShaderBindingOffset, VkDeviceSize hitShaderBindingStride, VkBuffer callableShaderBindingTableBuffer, VkDeviceSize callableShaderBindingOffset, VkDeviceSize callableShaderBindingStride, uint32_t width, uint32_t height, uint32_t depth) {
    static PFN_vkCmdTraceRaysNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdTraceRaysNV)resolve("vkCmdTraceRaysNV");
    if (!fp) { return; }
    fp(commandBuffer, raygenShaderBindingTableBuffer, raygenShaderBindingOffset, missShaderBindingTableBuffer, missShaderBindingOffset, missShaderBindingStride, hitShaderBindingTableBuffer, hitShaderBindingOffset, hitShaderBindingStride, callableShaderBindingTableBuffer, callableShaderBindingOffset, callableShaderBindingStride, width, height, depth);
}
VKAPI_ATTR void VKAPI_CALL vkCmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize dataSize, const void* pData) {
    static PFN_vkCmdUpdateBuffer fp = nullptr;
    if (!fp) fp = (PFN_vkCmdUpdateBuffer)resolve("vkCmdUpdateBuffer");
    if (!fp) { return; }
    fp(commandBuffer, dstBuffer, dstOffset, dataSize, pData);
}
VKAPI_ATTR void VKAPI_CALL vkCmdUpdateMemoryKHR(VkCommandBuffer commandBuffer, const VkDeviceAddressRangeKHR* pDstRange, VkAddressCommandFlagsKHR dstFlags, VkDeviceSize dataSize, const void* pData) {
    static PFN_vkCmdUpdateMemoryKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdUpdateMemoryKHR)resolve("vkCmdUpdateMemoryKHR");
    if (!fp) { return; }
    fp(commandBuffer, pDstRange, dstFlags, dataSize, pData);
}
VKAPI_ATTR void VKAPI_CALL vkCmdUpdatePipelineIndirectBufferNV(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline) {
    static PFN_vkCmdUpdatePipelineIndirectBufferNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdUpdatePipelineIndirectBufferNV)resolve("vkCmdUpdatePipelineIndirectBufferNV");
    if (!fp) { return; }
    fp(commandBuffer, pipelineBindPoint, pipeline);
}
VKAPI_ATTR void VKAPI_CALL vkCmdWaitEvents(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers) {
    static PFN_vkCmdWaitEvents fp = nullptr;
    if (!fp) fp = (PFN_vkCmdWaitEvents)resolve("vkCmdWaitEvents");
    if (!fp) { return; }
    fp(commandBuffer, eventCount, pEvents, srcStageMask, dstStageMask, memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers);
}
VKAPI_ATTR void VKAPI_CALL vkCmdWaitEvents2(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents, const VkDependencyInfo* pDependencyInfos) {
    static PFN_vkCmdWaitEvents2 fp = nullptr;
    if (!fp) fp = (PFN_vkCmdWaitEvents2)resolve("vkCmdWaitEvents2");
    if (!fp) { return; }
    fp(commandBuffer, eventCount, pEvents, pDependencyInfos);
}
VKAPI_ATTR void VKAPI_CALL vkCmdWaitEvents2KHR(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents, const VkDependencyInfo* pDependencyInfos) {
    static PFN_vkCmdWaitEvents2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdWaitEvents2KHR)resolve("vkCmdWaitEvents2KHR");
    if (!fp) { return; }
    fp(commandBuffer, eventCount, pEvents, pDependencyInfos);
}
VKAPI_ATTR void VKAPI_CALL vkCmdWriteAccelerationStructuresPropertiesKHR(VkCommandBuffer commandBuffer, uint32_t accelerationStructureCount, const VkAccelerationStructureKHR* pAccelerationStructures, VkQueryType queryType, VkQueryPool queryPool, uint32_t firstQuery) {
    static PFN_vkCmdWriteAccelerationStructuresPropertiesKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdWriteAccelerationStructuresPropertiesKHR)resolve("vkCmdWriteAccelerationStructuresPropertiesKHR");
    if (!fp) { return; }
    fp(commandBuffer, accelerationStructureCount, pAccelerationStructures, queryType, queryPool, firstQuery);
}
VKAPI_ATTR void VKAPI_CALL vkCmdWriteAccelerationStructuresPropertiesNV(VkCommandBuffer commandBuffer, uint32_t accelerationStructureCount, const VkAccelerationStructureNV* pAccelerationStructures, VkQueryType queryType, VkQueryPool queryPool, uint32_t firstQuery) {
    static PFN_vkCmdWriteAccelerationStructuresPropertiesNV fp = nullptr;
    if (!fp) fp = (PFN_vkCmdWriteAccelerationStructuresPropertiesNV)resolve("vkCmdWriteAccelerationStructuresPropertiesNV");
    if (!fp) { return; }
    fp(commandBuffer, accelerationStructureCount, pAccelerationStructures, queryType, queryPool, firstQuery);
}
VKAPI_ATTR void VKAPI_CALL vkCmdWriteBufferMarker2AMD(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 stage, VkBuffer dstBuffer, VkDeviceSize dstOffset, uint32_t marker) {
    static PFN_vkCmdWriteBufferMarker2AMD fp = nullptr;
    if (!fp) fp = (PFN_vkCmdWriteBufferMarker2AMD)resolve("vkCmdWriteBufferMarker2AMD");
    if (!fp) { return; }
    fp(commandBuffer, stage, dstBuffer, dstOffset, marker);
}
VKAPI_ATTR void VKAPI_CALL vkCmdWriteBufferMarkerAMD(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage, VkBuffer dstBuffer, VkDeviceSize dstOffset, uint32_t marker) {
    static PFN_vkCmdWriteBufferMarkerAMD fp = nullptr;
    if (!fp) fp = (PFN_vkCmdWriteBufferMarkerAMD)resolve("vkCmdWriteBufferMarkerAMD");
    if (!fp) { return; }
    fp(commandBuffer, pipelineStage, dstBuffer, dstOffset, marker);
}
VKAPI_ATTR void VKAPI_CALL vkCmdWriteMarkerToMemoryAMD(VkCommandBuffer commandBuffer, const VkMemoryMarkerInfoAMD* pInfo) {
    static PFN_vkCmdWriteMarkerToMemoryAMD fp = nullptr;
    if (!fp) fp = (PFN_vkCmdWriteMarkerToMemoryAMD)resolve("vkCmdWriteMarkerToMemoryAMD");
    if (!fp) { return; }
    fp(commandBuffer, pInfo);
}
VKAPI_ATTR void VKAPI_CALL vkCmdWriteMicromapsPropertiesEXT(VkCommandBuffer commandBuffer, uint32_t micromapCount, const VkMicromapEXT* pMicromaps, VkQueryType queryType, VkQueryPool queryPool, uint32_t firstQuery) {
    static PFN_vkCmdWriteMicromapsPropertiesEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCmdWriteMicromapsPropertiesEXT)resolve("vkCmdWriteMicromapsPropertiesEXT");
    if (!fp) { return; }
    fp(commandBuffer, micromapCount, pMicromaps, queryType, queryPool, firstQuery);
}
VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage, VkQueryPool queryPool, uint32_t query) {
    static PFN_vkCmdWriteTimestamp fp = nullptr;
    if (!fp) fp = (PFN_vkCmdWriteTimestamp)resolve("vkCmdWriteTimestamp");
    if (!fp) { return; }
    fp(commandBuffer, pipelineStage, queryPool, query);
}
VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp2(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 stage, VkQueryPool queryPool, uint32_t query) {
    static PFN_vkCmdWriteTimestamp2 fp = nullptr;
    if (!fp) fp = (PFN_vkCmdWriteTimestamp2)resolve("vkCmdWriteTimestamp2");
    if (!fp) { return; }
    fp(commandBuffer, stage, queryPool, query);
}
VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp2KHR(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 stage, VkQueryPool queryPool, uint32_t query) {
    static PFN_vkCmdWriteTimestamp2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkCmdWriteTimestamp2KHR)resolve("vkCmdWriteTimestamp2KHR");
    if (!fp) { return; }
    fp(commandBuffer, stage, queryPool, query);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCompileDeferredNV(VkDevice device, VkPipeline pipeline, uint32_t shader) {
    static PFN_vkCompileDeferredNV fp = nullptr;
    if (!fp) fp = (PFN_vkCompileDeferredNV)resolve("vkCompileDeferredNV");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pipeline, shader);
}
VKAPI_ATTR VkResult VKAPI_CALL vkConvertCooperativeVectorMatrixNV(VkDevice device, const VkConvertCooperativeVectorMatrixInfoNV* pInfo) {
    static PFN_vkConvertCooperativeVectorMatrixNV fp = nullptr;
    if (!fp) fp = (PFN_vkConvertCooperativeVectorMatrixNV)resolve("vkConvertCooperativeVectorMatrixNV");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCopyAccelerationStructureKHR(VkDevice device, VkDeferredOperationKHR deferredOperation, const VkCopyAccelerationStructureInfoKHR* pInfo) {
    static PFN_vkCopyAccelerationStructureKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCopyAccelerationStructureKHR)resolve("vkCopyAccelerationStructureKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, deferredOperation, pInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCopyAccelerationStructureToMemoryKHR(VkDevice device, VkDeferredOperationKHR deferredOperation, const VkCopyAccelerationStructureToMemoryInfoKHR* pInfo) {
    static PFN_vkCopyAccelerationStructureToMemoryKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCopyAccelerationStructureToMemoryKHR)resolve("vkCopyAccelerationStructureToMemoryKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, deferredOperation, pInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCopyImageToImage(VkDevice device, const VkCopyImageToImageInfo* pCopyImageToImageInfo) {
    static PFN_vkCopyImageToImage fp = nullptr;
    if (!fp) fp = (PFN_vkCopyImageToImage)resolve("vkCopyImageToImage");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCopyImageToImageInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCopyImageToImageEXT(VkDevice device, const VkCopyImageToImageInfo* pCopyImageToImageInfo) {
    static PFN_vkCopyImageToImageEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCopyImageToImageEXT)resolve("vkCopyImageToImageEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCopyImageToImageInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCopyImageToMemory(VkDevice device, const VkCopyImageToMemoryInfo* pCopyImageToMemoryInfo) {
    static PFN_vkCopyImageToMemory fp = nullptr;
    if (!fp) fp = (PFN_vkCopyImageToMemory)resolve("vkCopyImageToMemory");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCopyImageToMemoryInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCopyImageToMemoryEXT(VkDevice device, const VkCopyImageToMemoryInfo* pCopyImageToMemoryInfo) {
    static PFN_vkCopyImageToMemoryEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCopyImageToMemoryEXT)resolve("vkCopyImageToMemoryEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCopyImageToMemoryInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCopyMemoryToAccelerationStructureKHR(VkDevice device, VkDeferredOperationKHR deferredOperation, const VkCopyMemoryToAccelerationStructureInfoKHR* pInfo) {
    static PFN_vkCopyMemoryToAccelerationStructureKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCopyMemoryToAccelerationStructureKHR)resolve("vkCopyMemoryToAccelerationStructureKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, deferredOperation, pInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCopyMemoryToImage(VkDevice device, const VkCopyMemoryToImageInfo* pCopyMemoryToImageInfo) {
    static PFN_vkCopyMemoryToImage fp = nullptr;
    if (!fp) fp = (PFN_vkCopyMemoryToImage)resolve("vkCopyMemoryToImage");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCopyMemoryToImageInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCopyMemoryToImageEXT(VkDevice device, const VkCopyMemoryToImageInfo* pCopyMemoryToImageInfo) {
    static PFN_vkCopyMemoryToImageEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCopyMemoryToImageEXT)resolve("vkCopyMemoryToImageEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCopyMemoryToImageInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCopyMemoryToMicromapEXT(VkDevice device, VkDeferredOperationKHR deferredOperation, const VkCopyMemoryToMicromapInfoEXT* pInfo) {
    static PFN_vkCopyMemoryToMicromapEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCopyMemoryToMicromapEXT)resolve("vkCopyMemoryToMicromapEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, deferredOperation, pInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCopyMicromapEXT(VkDevice device, VkDeferredOperationKHR deferredOperation, const VkCopyMicromapInfoEXT* pInfo) {
    static PFN_vkCopyMicromapEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCopyMicromapEXT)resolve("vkCopyMicromapEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, deferredOperation, pInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCopyMicromapToMemoryEXT(VkDevice device, VkDeferredOperationKHR deferredOperation, const VkCopyMicromapToMemoryInfoEXT* pInfo) {
    static PFN_vkCopyMicromapToMemoryEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCopyMicromapToMemoryEXT)resolve("vkCopyMicromapToMemoryEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, deferredOperation, pInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateAccelerationStructure2KHR(VkDevice device, const VkAccelerationStructureCreateInfo2KHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkAccelerationStructureKHR* pAccelerationStructure) {
    static PFN_vkCreateAccelerationStructure2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkCreateAccelerationStructure2KHR)resolve("vkCreateAccelerationStructure2KHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pAccelerationStructure);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateAccelerationStructureKHR(VkDevice device, const VkAccelerationStructureCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkAccelerationStructureKHR* pAccelerationStructure) {
    static PFN_vkCreateAccelerationStructureKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCreateAccelerationStructureKHR)resolve("vkCreateAccelerationStructureKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pAccelerationStructure);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateAccelerationStructureNV(VkDevice device, const VkAccelerationStructureCreateInfoNV* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkAccelerationStructureNV* pAccelerationStructure) {
    static PFN_vkCreateAccelerationStructureNV fp = nullptr;
    if (!fp) fp = (PFN_vkCreateAccelerationStructureNV)resolve("vkCreateAccelerationStructureNV");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pAccelerationStructure);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateAndroidSurfaceKHR(VkInstance instance, const VkAndroidSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) {
    static PFN_vkCreateAndroidSurfaceKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCreateAndroidSurfaceKHR)resolve("vkCreateAndroidSurfaceKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(instance, pCreateInfo, pAllocator, pSurface);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer) {
    static PFN_vkCreateBuffer fp = nullptr;
    if (!fp) fp = (PFN_vkCreateBuffer)resolve("vkCreateBuffer");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pBuffer);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateBufferView(VkDevice device, const VkBufferViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBufferView* pView) {
    static PFN_vkCreateBufferView fp = nullptr;
    if (!fp) fp = (PFN_vkCreateBufferView)resolve("vkCreateBufferView");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pView);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkCommandPool* pCommandPool) {
    static PFN_vkCreateCommandPool fp = nullptr;
    if (!fp) fp = (PFN_vkCreateCommandPool)resolve("vkCreateCommandPool");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pCommandPool);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines) {
    static PFN_vkCreateComputePipelines fp = nullptr;
    if (!fp) fp = (PFN_vkCreateComputePipelines)resolve("vkCreateComputePipelines");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateCuFunctionNVX(VkDevice device, const VkCuFunctionCreateInfoNVX* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkCuFunctionNVX* pFunction) {
    static PFN_vkCreateCuFunctionNVX fp = nullptr;
    if (!fp) fp = (PFN_vkCreateCuFunctionNVX)resolve("vkCreateCuFunctionNVX");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pFunction);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateCuModuleNVX(VkDevice device, const VkCuModuleCreateInfoNVX* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkCuModuleNVX* pModule) {
    static PFN_vkCreateCuModuleNVX fp = nullptr;
    if (!fp) fp = (PFN_vkCreateCuModuleNVX)resolve("vkCreateCuModuleNVX");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pModule);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDataGraphPipelineSessionARM(VkDevice device, const VkDataGraphPipelineSessionCreateInfoARM* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDataGraphPipelineSessionARM* pSession) {
    static PFN_vkCreateDataGraphPipelineSessionARM fp = nullptr;
    if (!fp) fp = (PFN_vkCreateDataGraphPipelineSessionARM)resolve("vkCreateDataGraphPipelineSessionARM");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pSession);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDataGraphPipelinesARM(VkDevice device, VkDeferredOperationKHR deferredOperation, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkDataGraphPipelineCreateInfoARM* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines) {
    static PFN_vkCreateDataGraphPipelinesARM fp = nullptr;
    if (!fp) fp = (PFN_vkCreateDataGraphPipelinesARM)resolve("vkCreateDataGraphPipelinesARM");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, deferredOperation, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback) {
    static PFN_vkCreateDebugReportCallbackEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCreateDebugReportCallbackEXT)resolve("vkCreateDebugReportCallbackEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(instance, pCreateInfo, pAllocator, pCallback);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pMessenger) {
    static PFN_vkCreateDebugUtilsMessengerEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCreateDebugUtilsMessengerEXT)resolve("vkCreateDebugUtilsMessengerEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(instance, pCreateInfo, pAllocator, pMessenger);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDeferredOperationKHR(VkDevice device, const VkAllocationCallbacks* pAllocator, VkDeferredOperationKHR* pDeferredOperation) {
    static PFN_vkCreateDeferredOperationKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCreateDeferredOperationKHR)resolve("vkCreateDeferredOperationKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pAllocator, pDeferredOperation);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorPool* pDescriptorPool) {
    static PFN_vkCreateDescriptorPool fp = nullptr;
    if (!fp) fp = (PFN_vkCreateDescriptorPool)resolve("vkCreateDescriptorPool");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pDescriptorPool);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorSetLayout* pSetLayout) {
    static PFN_vkCreateDescriptorSetLayout fp = nullptr;
    if (!fp) fp = (PFN_vkCreateDescriptorSetLayout)resolve("vkCreateDescriptorSetLayout");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pSetLayout);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorUpdateTemplate(VkDevice device, const VkDescriptorUpdateTemplateCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorUpdateTemplate* pDescriptorUpdateTemplate) {
    static PFN_vkCreateDescriptorUpdateTemplate fp = nullptr;
    if (!fp) fp = (PFN_vkCreateDescriptorUpdateTemplate)resolve("vkCreateDescriptorUpdateTemplate");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pDescriptorUpdateTemplate);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorUpdateTemplateKHR(VkDevice device, const VkDescriptorUpdateTemplateCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorUpdateTemplate* pDescriptorUpdateTemplate) {
    static PFN_vkCreateDescriptorUpdateTemplateKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCreateDescriptorUpdateTemplateKHR)resolve("vkCreateDescriptorUpdateTemplateKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pDescriptorUpdateTemplate);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
    static PFN_vkCreateDevice fp = nullptr;
    if (!fp) fp = (PFN_vkCreateDevice)resolve("vkCreateDevice");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, pCreateInfo, pAllocator, pDevice);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDisplayModeKHR(VkPhysicalDevice physicalDevice, VkDisplayKHR display, const VkDisplayModeCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDisplayModeKHR* pMode) {
    static PFN_vkCreateDisplayModeKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCreateDisplayModeKHR)resolve("vkCreateDisplayModeKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, display, pCreateInfo, pAllocator, pMode);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDisplayPlaneSurfaceKHR(VkInstance instance, const VkDisplaySurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) {
    static PFN_vkCreateDisplayPlaneSurfaceKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCreateDisplayPlaneSurfaceKHR)resolve("vkCreateDisplayPlaneSurfaceKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(instance, pCreateInfo, pAllocator, pSurface);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateEvent(VkDevice device, const VkEventCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkEvent* pEvent) {
    static PFN_vkCreateEvent fp = nullptr;
    if (!fp) fp = (PFN_vkCreateEvent)resolve("vkCreateEvent");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pEvent);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateExternalComputeQueueNV(VkDevice device, const VkExternalComputeQueueCreateInfoNV* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkExternalComputeQueueNV* pExternalQueue) {
    static PFN_vkCreateExternalComputeQueueNV fp = nullptr;
    if (!fp) fp = (PFN_vkCreateExternalComputeQueueNV)resolve("vkCreateExternalComputeQueueNV");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pExternalQueue);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(VkDevice device, const VkFenceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFence* pFence) {
    static PFN_vkCreateFence fp = nullptr;
    if (!fp) fp = (PFN_vkCreateFence)resolve("vkCreateFence");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pFence);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFramebuffer* pFramebuffer) {
    static PFN_vkCreateFramebuffer fp = nullptr;
    if (!fp) fp = (PFN_vkCreateFramebuffer)resolve("vkCreateFramebuffer");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pFramebuffer);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateGpaSessionAMD(VkDevice device, const VkGpaSessionCreateInfoAMD* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkGpaSessionAMD* pGpaSession) {
    static PFN_vkCreateGpaSessionAMD fp = nullptr;
    if (!fp) fp = (PFN_vkCreateGpaSessionAMD)resolve("vkCreateGpaSessionAMD");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pGpaSession);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines) {
    static PFN_vkCreateGraphicsPipelines fp = nullptr;
    if (!fp) fp = (PFN_vkCreateGraphicsPipelines)resolve("vkCreateGraphicsPipelines");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateHeadlessSurfaceEXT(VkInstance instance, const VkHeadlessSurfaceCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) {
    static PFN_vkCreateHeadlessSurfaceEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCreateHeadlessSurfaceEXT)resolve("vkCreateHeadlessSurfaceEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(instance, pCreateInfo, pAllocator, pSurface);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage) {
    static PFN_vkCreateImage fp = nullptr;
    if (!fp) fp = (PFN_vkCreateImage)resolve("vkCreateImage");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pImage);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice device, const VkImageViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImageView* pView) {
    static PFN_vkCreateImageView fp = nullptr;
    if (!fp) fp = (PFN_vkCreateImageView)resolve("vkCreateImageView");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pView);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateIndirectCommandsLayoutEXT(VkDevice device, const VkIndirectCommandsLayoutCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkIndirectCommandsLayoutEXT* pIndirectCommandsLayout) {
    static PFN_vkCreateIndirectCommandsLayoutEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCreateIndirectCommandsLayoutEXT)resolve("vkCreateIndirectCommandsLayoutEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pIndirectCommandsLayout);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateIndirectCommandsLayoutNV(VkDevice device, const VkIndirectCommandsLayoutCreateInfoNV* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkIndirectCommandsLayoutNV* pIndirectCommandsLayout) {
    static PFN_vkCreateIndirectCommandsLayoutNV fp = nullptr;
    if (!fp) fp = (PFN_vkCreateIndirectCommandsLayoutNV)resolve("vkCreateIndirectCommandsLayoutNV");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pIndirectCommandsLayout);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateIndirectExecutionSetEXT(VkDevice device, const VkIndirectExecutionSetCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkIndirectExecutionSetEXT* pIndirectExecutionSet) {
    static PFN_vkCreateIndirectExecutionSetEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCreateIndirectExecutionSetEXT)resolve("vkCreateIndirectExecutionSetEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pIndirectExecutionSet);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) {
    static PFN_vkCreateInstance fp = nullptr;
    if (!fp) fp = (PFN_vkCreateInstance)resolve("vkCreateInstance");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(pCreateInfo, pAllocator, pInstance);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateMicromapEXT(VkDevice device, const VkMicromapCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkMicromapEXT* pMicromap) {
    static PFN_vkCreateMicromapEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCreateMicromapEXT)resolve("vkCreateMicromapEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pMicromap);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateOpticalFlowSessionNV(VkDevice device, const VkOpticalFlowSessionCreateInfoNV* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkOpticalFlowSessionNV* pSession) {
    static PFN_vkCreateOpticalFlowSessionNV fp = nullptr;
    if (!fp) fp = (PFN_vkCreateOpticalFlowSessionNV)resolve("vkCreateOpticalFlowSessionNV");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pSession);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineBinariesKHR(VkDevice device, const VkPipelineBinaryCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineBinaryHandlesInfoKHR* pBinaries) {
    static PFN_vkCreatePipelineBinariesKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCreatePipelineBinariesKHR)resolve("vkCreatePipelineBinariesKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pBinaries);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineCache(VkDevice device, const VkPipelineCacheCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineCache* pPipelineCache) {
    static PFN_vkCreatePipelineCache fp = nullptr;
    if (!fp) fp = (PFN_vkCreatePipelineCache)resolve("vkCreatePipelineCache");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pPipelineCache);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineLayout* pPipelineLayout) {
    static PFN_vkCreatePipelineLayout fp = nullptr;
    if (!fp) fp = (PFN_vkCreatePipelineLayout)resolve("vkCreatePipelineLayout");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pPipelineLayout);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreatePrivateDataSlot(VkDevice device, const VkPrivateDataSlotCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPrivateDataSlot* pPrivateDataSlot) {
    static PFN_vkCreatePrivateDataSlot fp = nullptr;
    if (!fp) fp = (PFN_vkCreatePrivateDataSlot)resolve("vkCreatePrivateDataSlot");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pPrivateDataSlot);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreatePrivateDataSlotEXT(VkDevice device, const VkPrivateDataSlotCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPrivateDataSlot* pPrivateDataSlot) {
    static PFN_vkCreatePrivateDataSlotEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCreatePrivateDataSlotEXT)resolve("vkCreatePrivateDataSlotEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pPrivateDataSlot);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkQueryPool* pQueryPool) {
    static PFN_vkCreateQueryPool fp = nullptr;
    if (!fp) fp = (PFN_vkCreateQueryPool)resolve("vkCreateQueryPool");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pQueryPool);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateRayTracingPipelinesKHR(VkDevice device, VkDeferredOperationKHR deferredOperation, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkRayTracingPipelineCreateInfoKHR* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines) {
    static PFN_vkCreateRayTracingPipelinesKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCreateRayTracingPipelinesKHR)resolve("vkCreateRayTracingPipelinesKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, deferredOperation, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateRayTracingPipelinesNV(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkRayTracingPipelineCreateInfoNV* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines) {
    static PFN_vkCreateRayTracingPipelinesNV fp = nullptr;
    if (!fp) fp = (PFN_vkCreateRayTracingPipelinesNV)resolve("vkCreateRayTracingPipelinesNV");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass(VkDevice device, const VkRenderPassCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass) {
    static PFN_vkCreateRenderPass fp = nullptr;
    if (!fp) fp = (PFN_vkCreateRenderPass)resolve("vkCreateRenderPass");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pRenderPass);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass2(VkDevice device, const VkRenderPassCreateInfo2* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass) {
    static PFN_vkCreateRenderPass2 fp = nullptr;
    if (!fp) fp = (PFN_vkCreateRenderPass2)resolve("vkCreateRenderPass2");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pRenderPass);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass2KHR(VkDevice device, const VkRenderPassCreateInfo2* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass) {
    static PFN_vkCreateRenderPass2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkCreateRenderPass2KHR)resolve("vkCreateRenderPass2KHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pRenderPass);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSampler(VkDevice device, const VkSamplerCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSampler* pSampler) {
    static PFN_vkCreateSampler fp = nullptr;
    if (!fp) fp = (PFN_vkCreateSampler)resolve("vkCreateSampler");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pSampler);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSamplerYcbcrConversion(VkDevice device, const VkSamplerYcbcrConversionCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSamplerYcbcrConversion* pYcbcrConversion) {
    static PFN_vkCreateSamplerYcbcrConversion fp = nullptr;
    if (!fp) fp = (PFN_vkCreateSamplerYcbcrConversion)resolve("vkCreateSamplerYcbcrConversion");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pYcbcrConversion);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSamplerYcbcrConversionKHR(VkDevice device, const VkSamplerYcbcrConversionCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSamplerYcbcrConversion* pYcbcrConversion) {
    static PFN_vkCreateSamplerYcbcrConversionKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCreateSamplerYcbcrConversionKHR)resolve("vkCreateSamplerYcbcrConversionKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pYcbcrConversion);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSemaphore* pSemaphore) {
    static PFN_vkCreateSemaphore fp = nullptr;
    if (!fp) fp = (PFN_vkCreateSemaphore)resolve("vkCreateSemaphore");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pSemaphore);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderInstrumentationARM(VkDevice device, const VkShaderInstrumentationCreateInfoARM* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkShaderInstrumentationARM* pInstrumentation) {
    static PFN_vkCreateShaderInstrumentationARM fp = nullptr;
    if (!fp) fp = (PFN_vkCreateShaderInstrumentationARM)resolve("vkCreateShaderInstrumentationARM");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pInstrumentation);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkShaderModule* pShaderModule) {
    static PFN_vkCreateShaderModule fp = nullptr;
    if (!fp) fp = (PFN_vkCreateShaderModule)resolve("vkCreateShaderModule");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pShaderModule);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateShadersEXT(VkDevice device, uint32_t createInfoCount, const VkShaderCreateInfoEXT* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkShaderEXT* pShaders) {
    static PFN_vkCreateShadersEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCreateShadersEXT)resolve("vkCreateShadersEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, createInfoCount, pCreateInfos, pAllocator, pShaders);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSharedSwapchainsKHR(VkDevice device, uint32_t swapchainCount, const VkSwapchainCreateInfoKHR* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchains) {
    static PFN_vkCreateSharedSwapchainsKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCreateSharedSwapchainsKHR)resolve("vkCreateSharedSwapchainsKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, swapchainCount, pCreateInfos, pAllocator, pSwapchains);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain) {
    static PFN_vkCreateSwapchainKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCreateSwapchainKHR)resolve("vkCreateSwapchainKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pSwapchain);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateTensorARM(VkDevice device, const VkTensorCreateInfoARM* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkTensorARM* pTensor) {
    static PFN_vkCreateTensorARM fp = nullptr;
    if (!fp) fp = (PFN_vkCreateTensorARM)resolve("vkCreateTensorARM");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pTensor);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateTensorViewARM(VkDevice device, const VkTensorViewCreateInfoARM* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkTensorViewARM* pView) {
    static PFN_vkCreateTensorViewARM fp = nullptr;
    if (!fp) fp = (PFN_vkCreateTensorViewARM)resolve("vkCreateTensorViewARM");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pView);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateValidationCacheEXT(VkDevice device, const VkValidationCacheCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkValidationCacheEXT* pValidationCache) {
    static PFN_vkCreateValidationCacheEXT fp = nullptr;
    if (!fp) fp = (PFN_vkCreateValidationCacheEXT)resolve("vkCreateValidationCacheEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pValidationCache);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateVideoSessionKHR(VkDevice device, const VkVideoSessionCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkVideoSessionKHR* pVideoSession) {
    static PFN_vkCreateVideoSessionKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCreateVideoSessionKHR)resolve("vkCreateVideoSessionKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pVideoSession);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateVideoSessionParametersKHR(VkDevice device, const VkVideoSessionParametersCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkVideoSessionParametersKHR* pVideoSessionParameters) {
    static PFN_vkCreateVideoSessionParametersKHR fp = nullptr;
    if (!fp) fp = (PFN_vkCreateVideoSessionParametersKHR)resolve("vkCreateVideoSessionParametersKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pCreateInfo, pAllocator, pVideoSessionParameters);
}
VKAPI_ATTR VkResult VKAPI_CALL vkDebugMarkerSetObjectNameEXT(VkDevice device, const VkDebugMarkerObjectNameInfoEXT* pNameInfo) {
    static PFN_vkDebugMarkerSetObjectNameEXT fp = nullptr;
    if (!fp) fp = (PFN_vkDebugMarkerSetObjectNameEXT)resolve("vkDebugMarkerSetObjectNameEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pNameInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkDebugMarkerSetObjectTagEXT(VkDevice device, const VkDebugMarkerObjectTagInfoEXT* pTagInfo) {
    static PFN_vkDebugMarkerSetObjectTagEXT fp = nullptr;
    if (!fp) fp = (PFN_vkDebugMarkerSetObjectTagEXT)resolve("vkDebugMarkerSetObjectTagEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pTagInfo);
}
VKAPI_ATTR void VKAPI_CALL vkDebugReportMessageEXT(VkInstance instance, VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage) {
    static PFN_vkDebugReportMessageEXT fp = nullptr;
    if (!fp) fp = (PFN_vkDebugReportMessageEXT)resolve("vkDebugReportMessageEXT");
    if (!fp) { return; }
    fp(instance, flags, objectType, object, location, messageCode, pLayerPrefix, pMessage);
}
VKAPI_ATTR VkResult VKAPI_CALL vkDeferredOperationJoinKHR(VkDevice device, VkDeferredOperationKHR operation) {
    static PFN_vkDeferredOperationJoinKHR fp = nullptr;
    if (!fp) fp = (PFN_vkDeferredOperationJoinKHR)resolve("vkDeferredOperationJoinKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, operation);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyAccelerationStructureKHR(VkDevice device, VkAccelerationStructureKHR accelerationStructure, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyAccelerationStructureKHR fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyAccelerationStructureKHR)resolve("vkDestroyAccelerationStructureKHR");
    if (!fp) { return; }
    fp(device, accelerationStructure, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyAccelerationStructureNV(VkDevice device, VkAccelerationStructureNV accelerationStructure, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyAccelerationStructureNV fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyAccelerationStructureNV)resolve("vkDestroyAccelerationStructureNV");
    if (!fp) { return; }
    fp(device, accelerationStructure, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyBuffer fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyBuffer)resolve("vkDestroyBuffer");
    if (!fp) { return; }
    fp(device, buffer, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyBufferView(VkDevice device, VkBufferView bufferView, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyBufferView fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyBufferView)resolve("vkDestroyBufferView");
    if (!fp) { return; }
    fp(device, bufferView, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyCommandPool fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyCommandPool)resolve("vkDestroyCommandPool");
    if (!fp) { return; }
    fp(device, commandPool, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyCuFunctionNVX(VkDevice device, VkCuFunctionNVX function, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyCuFunctionNVX fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyCuFunctionNVX)resolve("vkDestroyCuFunctionNVX");
    if (!fp) { return; }
    fp(device, function, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyCuModuleNVX(VkDevice device, VkCuModuleNVX module, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyCuModuleNVX fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyCuModuleNVX)resolve("vkDestroyCuModuleNVX");
    if (!fp) { return; }
    fp(device, module, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDataGraphPipelineSessionARM(VkDevice device, VkDataGraphPipelineSessionARM session, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyDataGraphPipelineSessionARM fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyDataGraphPipelineSessionARM)resolve("vkDestroyDataGraphPipelineSessionARM");
    if (!fp) { return; }
    fp(device, session, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyDebugReportCallbackEXT fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyDebugReportCallbackEXT)resolve("vkDestroyDebugReportCallbackEXT");
    if (!fp) { return; }
    fp(instance, callback, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT messenger, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyDebugUtilsMessengerEXT fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyDebugUtilsMessengerEXT)resolve("vkDestroyDebugUtilsMessengerEXT");
    if (!fp) { return; }
    fp(instance, messenger, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDeferredOperationKHR(VkDevice device, VkDeferredOperationKHR operation, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyDeferredOperationKHR fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyDeferredOperationKHR)resolve("vkDestroyDeferredOperationKHR");
    if (!fp) { return; }
    fp(device, operation, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyDescriptorPool fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyDescriptorPool)resolve("vkDestroyDescriptorPool");
    if (!fp) { return; }
    fp(device, descriptorPool, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyDescriptorSetLayout fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyDescriptorSetLayout)resolve("vkDestroyDescriptorSetLayout");
    if (!fp) { return; }
    fp(device, descriptorSetLayout, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorUpdateTemplate(VkDevice device, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyDescriptorUpdateTemplate fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyDescriptorUpdateTemplate)resolve("vkDestroyDescriptorUpdateTemplate");
    if (!fp) { return; }
    fp(device, descriptorUpdateTemplate, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorUpdateTemplateKHR(VkDevice device, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyDescriptorUpdateTemplateKHR fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyDescriptorUpdateTemplateKHR)resolve("vkDestroyDescriptorUpdateTemplateKHR");
    if (!fp) { return; }
    fp(device, descriptorUpdateTemplate, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyDevice fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyDevice)resolve("vkDestroyDevice");
    if (!fp) { return; }
    fp(device, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyEvent(VkDevice device, VkEvent event, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyEvent fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyEvent)resolve("vkDestroyEvent");
    if (!fp) { return; }
    fp(device, event, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyExternalComputeQueueNV(VkDevice device, VkExternalComputeQueueNV externalQueue, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyExternalComputeQueueNV fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyExternalComputeQueueNV)resolve("vkDestroyExternalComputeQueueNV");
    if (!fp) { return; }
    fp(device, externalQueue, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyFence fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyFence)resolve("vkDestroyFence");
    if (!fp) { return; }
    fp(device, fence, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyFramebuffer fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyFramebuffer)resolve("vkDestroyFramebuffer");
    if (!fp) { return; }
    fp(device, framebuffer, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyGpaSessionAMD(VkDevice device, VkGpaSessionAMD gpaSession, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyGpaSessionAMD fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyGpaSessionAMD)resolve("vkDestroyGpaSessionAMD");
    if (!fp) { return; }
    fp(device, gpaSession, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyImage fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyImage)resolve("vkDestroyImage");
    if (!fp) { return; }
    fp(device, image, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyImageView fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyImageView)resolve("vkDestroyImageView");
    if (!fp) { return; }
    fp(device, imageView, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyIndirectCommandsLayoutEXT(VkDevice device, VkIndirectCommandsLayoutEXT indirectCommandsLayout, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyIndirectCommandsLayoutEXT fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyIndirectCommandsLayoutEXT)resolve("vkDestroyIndirectCommandsLayoutEXT");
    if (!fp) { return; }
    fp(device, indirectCommandsLayout, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyIndirectCommandsLayoutNV(VkDevice device, VkIndirectCommandsLayoutNV indirectCommandsLayout, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyIndirectCommandsLayoutNV fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyIndirectCommandsLayoutNV)resolve("vkDestroyIndirectCommandsLayoutNV");
    if (!fp) { return; }
    fp(device, indirectCommandsLayout, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyIndirectExecutionSetEXT(VkDevice device, VkIndirectExecutionSetEXT indirectExecutionSet, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyIndirectExecutionSetEXT fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyIndirectExecutionSetEXT)resolve("vkDestroyIndirectExecutionSetEXT");
    if (!fp) { return; }
    fp(device, indirectExecutionSet, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyInstance fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyInstance)resolve("vkDestroyInstance");
    if (!fp) { return; }
    fp(instance, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyMicromapEXT(VkDevice device, VkMicromapEXT micromap, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyMicromapEXT fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyMicromapEXT)resolve("vkDestroyMicromapEXT");
    if (!fp) { return; }
    fp(device, micromap, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyOpticalFlowSessionNV(VkDevice device, VkOpticalFlowSessionNV session, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyOpticalFlowSessionNV fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyOpticalFlowSessionNV)resolve("vkDestroyOpticalFlowSessionNV");
    if (!fp) { return; }
    fp(device, session, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyPipeline fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyPipeline)resolve("vkDestroyPipeline");
    if (!fp) { return; }
    fp(device, pipeline, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineBinaryKHR(VkDevice device, VkPipelineBinaryKHR pipelineBinary, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyPipelineBinaryKHR fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyPipelineBinaryKHR)resolve("vkDestroyPipelineBinaryKHR");
    if (!fp) { return; }
    fp(device, pipelineBinary, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineCache(VkDevice device, VkPipelineCache pipelineCache, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyPipelineCache fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyPipelineCache)resolve("vkDestroyPipelineCache");
    if (!fp) { return; }
    fp(device, pipelineCache, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyPipelineLayout fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyPipelineLayout)resolve("vkDestroyPipelineLayout");
    if (!fp) { return; }
    fp(device, pipelineLayout, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyPrivateDataSlot(VkDevice device, VkPrivateDataSlot privateDataSlot, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyPrivateDataSlot fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyPrivateDataSlot)resolve("vkDestroyPrivateDataSlot");
    if (!fp) { return; }
    fp(device, privateDataSlot, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyPrivateDataSlotEXT(VkDevice device, VkPrivateDataSlot privateDataSlot, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyPrivateDataSlotEXT fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyPrivateDataSlotEXT)resolve("vkDestroyPrivateDataSlotEXT");
    if (!fp) { return; }
    fp(device, privateDataSlot, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyQueryPool(VkDevice device, VkQueryPool queryPool, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyQueryPool fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyQueryPool)resolve("vkDestroyQueryPool");
    if (!fp) { return; }
    fp(device, queryPool, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyRenderPass(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyRenderPass fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyRenderPass)resolve("vkDestroyRenderPass");
    if (!fp) { return; }
    fp(device, renderPass, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroySampler fp = nullptr;
    if (!fp) fp = (PFN_vkDestroySampler)resolve("vkDestroySampler");
    if (!fp) { return; }
    fp(device, sampler, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroySamplerYcbcrConversion(VkDevice device, VkSamplerYcbcrConversion ycbcrConversion, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroySamplerYcbcrConversion fp = nullptr;
    if (!fp) fp = (PFN_vkDestroySamplerYcbcrConversion)resolve("vkDestroySamplerYcbcrConversion");
    if (!fp) { return; }
    fp(device, ycbcrConversion, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroySamplerYcbcrConversionKHR(VkDevice device, VkSamplerYcbcrConversion ycbcrConversion, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroySamplerYcbcrConversionKHR fp = nullptr;
    if (!fp) fp = (PFN_vkDestroySamplerYcbcrConversionKHR)resolve("vkDestroySamplerYcbcrConversionKHR");
    if (!fp) { return; }
    fp(device, ycbcrConversion, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroySemaphore(VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroySemaphore fp = nullptr;
    if (!fp) fp = (PFN_vkDestroySemaphore)resolve("vkDestroySemaphore");
    if (!fp) { return; }
    fp(device, semaphore, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyShaderEXT(VkDevice device, VkShaderEXT shader, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyShaderEXT fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyShaderEXT)resolve("vkDestroyShaderEXT");
    if (!fp) { return; }
    fp(device, shader, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyShaderInstrumentationARM(VkDevice device, VkShaderInstrumentationARM instrumentation, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyShaderInstrumentationARM fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyShaderInstrumentationARM)resolve("vkDestroyShaderInstrumentationARM");
    if (!fp) { return; }
    fp(device, instrumentation, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyShaderModule fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyShaderModule)resolve("vkDestroyShaderModule");
    if (!fp) { return; }
    fp(device, shaderModule, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroySurfaceKHR fp = nullptr;
    if (!fp) fp = (PFN_vkDestroySurfaceKHR)resolve("vkDestroySurfaceKHR");
    if (!fp) { return; }
    fp(instance, surface, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroySwapchainKHR fp = nullptr;
    if (!fp) fp = (PFN_vkDestroySwapchainKHR)resolve("vkDestroySwapchainKHR");
    if (!fp) { return; }
    fp(device, swapchain, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyTensorARM(VkDevice device, VkTensorARM tensor, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyTensorARM fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyTensorARM)resolve("vkDestroyTensorARM");
    if (!fp) { return; }
    fp(device, tensor, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyTensorViewARM(VkDevice device, VkTensorViewARM tensorView, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyTensorViewARM fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyTensorViewARM)resolve("vkDestroyTensorViewARM");
    if (!fp) { return; }
    fp(device, tensorView, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyValidationCacheEXT(VkDevice device, VkValidationCacheEXT validationCache, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyValidationCacheEXT fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyValidationCacheEXT)resolve("vkDestroyValidationCacheEXT");
    if (!fp) { return; }
    fp(device, validationCache, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyVideoSessionKHR(VkDevice device, VkVideoSessionKHR videoSession, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyVideoSessionKHR fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyVideoSessionKHR)resolve("vkDestroyVideoSessionKHR");
    if (!fp) { return; }
    fp(device, videoSession, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyVideoSessionParametersKHR(VkDevice device, VkVideoSessionParametersKHR videoSessionParameters, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkDestroyVideoSessionParametersKHR fp = nullptr;
    if (!fp) fp = (PFN_vkDestroyVideoSessionParametersKHR)resolve("vkDestroyVideoSessionParametersKHR");
    if (!fp) { return; }
    fp(device, videoSessionParameters, pAllocator);
}
VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice device) {
    static PFN_vkDeviceWaitIdle fp = nullptr;
    if (!fp) fp = (PFN_vkDeviceWaitIdle)resolve("vkDeviceWaitIdle");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device);
}
VKAPI_ATTR VkResult VKAPI_CALL vkDisplayPowerControlEXT(VkDevice device, VkDisplayKHR display, const VkDisplayPowerInfoEXT* pDisplayPowerInfo) {
    static PFN_vkDisplayPowerControlEXT fp = nullptr;
    if (!fp) fp = (PFN_vkDisplayPowerControlEXT)resolve("vkDisplayPowerControlEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, display, pDisplayPowerInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer commandBuffer) {
    static PFN_vkEndCommandBuffer fp = nullptr;
    if (!fp) fp = (PFN_vkEndCommandBuffer)resolve("vkEndCommandBuffer");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(commandBuffer);
}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) {
    static PFN_vkEnumerateDeviceExtensionProperties fp = nullptr;
    if (!fp) fp = (PFN_vkEnumerateDeviceExtensionProperties)resolve("vkEnumerateDeviceExtensionProperties");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, pLayerName, pPropertyCount, pProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkLayerProperties* pProperties) {
    static PFN_vkEnumerateDeviceLayerProperties fp = nullptr;
    if (!fp) fp = (PFN_vkEnumerateDeviceLayerProperties)resolve("vkEnumerateDeviceLayerProperties");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, pPropertyCount, pProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) {
    static PFN_vkEnumerateInstanceExtensionProperties fp = nullptr;
    if (!fp) fp = (PFN_vkEnumerateInstanceExtensionProperties)resolve("vkEnumerateInstanceExtensionProperties");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(pLayerName, pPropertyCount, pProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* pProperties) {
    static PFN_vkEnumerateInstanceLayerProperties fp = nullptr;
    if (!fp) fp = (PFN_vkEnumerateInstanceLayerProperties)resolve("vkEnumerateInstanceLayerProperties");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(pPropertyCount, pProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceVersion(uint32_t* pApiVersion) {
    static PFN_vkEnumerateInstanceVersion fp = nullptr;
    if (!fp) fp = (PFN_vkEnumerateInstanceVersion)resolve("vkEnumerateInstanceVersion");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(pApiVersion);
}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t* pPhysicalDeviceGroupCount, VkPhysicalDeviceGroupProperties* pPhysicalDeviceGroupProperties) {
    static PFN_vkEnumeratePhysicalDeviceGroups fp = nullptr;
    if (!fp) fp = (PFN_vkEnumeratePhysicalDeviceGroups)resolve("vkEnumeratePhysicalDeviceGroups");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(instance, pPhysicalDeviceGroupCount, pPhysicalDeviceGroupProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDeviceGroupsKHR(VkInstance instance, uint32_t* pPhysicalDeviceGroupCount, VkPhysicalDeviceGroupProperties* pPhysicalDeviceGroupProperties) {
    static PFN_vkEnumeratePhysicalDeviceGroupsKHR fp = nullptr;
    if (!fp) fp = (PFN_vkEnumeratePhysicalDeviceGroupsKHR)resolve("vkEnumeratePhysicalDeviceGroupsKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(instance, pPhysicalDeviceGroupCount, pPhysicalDeviceGroupProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDeviceQueueFamilyPerformanceCountersByRegionARM(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, uint32_t* pCounterCount, VkPerformanceCounterARM* pCounters, VkPerformanceCounterDescriptionARM* pCounterDescriptions) {
    static PFN_vkEnumeratePhysicalDeviceQueueFamilyPerformanceCountersByRegionARM fp = nullptr;
    if (!fp) fp = (PFN_vkEnumeratePhysicalDeviceQueueFamilyPerformanceCountersByRegionARM)resolve("vkEnumeratePhysicalDeviceQueueFamilyPerformanceCountersByRegionARM");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, queueFamilyIndex, pCounterCount, pCounters, pCounterDescriptions);
}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, uint32_t* pCounterCount, VkPerformanceCounterKHR* pCounters, VkPerformanceCounterDescriptionKHR* pCounterDescriptions) {
    static PFN_vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR fp = nullptr;
    if (!fp) fp = (PFN_vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR)resolve("vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, queueFamilyIndex, pCounterCount, pCounters, pCounterDescriptions);
}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDeviceShaderInstrumentationMetricsARM(VkPhysicalDevice physicalDevice, uint32_t* pDescriptionCount, VkShaderInstrumentationMetricDescriptionARM* pDescriptions) {
    static PFN_vkEnumeratePhysicalDeviceShaderInstrumentationMetricsARM fp = nullptr;
    if (!fp) fp = (PFN_vkEnumeratePhysicalDeviceShaderInstrumentationMetricsARM)resolve("vkEnumeratePhysicalDeviceShaderInstrumentationMetricsARM");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, pDescriptionCount, pDescriptions);
}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices) {
    static PFN_vkEnumeratePhysicalDevices fp = nullptr;
    if (!fp) fp = (PFN_vkEnumeratePhysicalDevices)resolve("vkEnumeratePhysicalDevices");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(instance, pPhysicalDeviceCount, pPhysicalDevices);
}
VKAPI_ATTR VkResult VKAPI_CALL vkFlushMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges) {
    static PFN_vkFlushMappedMemoryRanges fp = nullptr;
    if (!fp) fp = (PFN_vkFlushMappedMemoryRanges)resolve("vkFlushMappedMemoryRanges");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, memoryRangeCount, pMemoryRanges);
}
VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers) {
    static PFN_vkFreeCommandBuffers fp = nullptr;
    if (!fp) fp = (PFN_vkFreeCommandBuffers)resolve("vkFreeCommandBuffers");
    if (!fp) { return; }
    fp(device, commandPool, commandBufferCount, pCommandBuffers);
}
VKAPI_ATTR VkResult VKAPI_CALL vkFreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets) {
    static PFN_vkFreeDescriptorSets fp = nullptr;
    if (!fp) fp = (PFN_vkFreeDescriptorSets)resolve("vkFreeDescriptorSets");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, descriptorPool, descriptorSetCount, pDescriptorSets);
}
VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkFreeMemory fp = nullptr;
    if (!fp) fp = (PFN_vkFreeMemory)resolve("vkFreeMemory");
    if (!fp) { return; }
    fp(device, memory, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkGetAccelerationStructureBuildSizesKHR(VkDevice device, VkAccelerationStructureBuildTypeKHR buildType, const VkAccelerationStructureBuildGeometryInfoKHR* pBuildInfo, const uint32_t* pMaxPrimitiveCounts, VkAccelerationStructureBuildSizesInfoKHR* pSizeInfo) {
    static PFN_vkGetAccelerationStructureBuildSizesKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetAccelerationStructureBuildSizesKHR)resolve("vkGetAccelerationStructureBuildSizesKHR");
    if (!fp) { return; }
    fp(device, buildType, pBuildInfo, pMaxPrimitiveCounts, pSizeInfo);
}
VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetAccelerationStructureDeviceAddressKHR(VkDevice device, const VkAccelerationStructureDeviceAddressInfoKHR* pInfo) {
    static PFN_vkGetAccelerationStructureDeviceAddressKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetAccelerationStructureDeviceAddressKHR)resolve("vkGetAccelerationStructureDeviceAddressKHR");
    if (!fp) { return 0; }
    return fp(device, pInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetAccelerationStructureHandleNV(VkDevice device, VkAccelerationStructureNV accelerationStructure, size_t dataSize, void* pData) {
    static PFN_vkGetAccelerationStructureHandleNV fp = nullptr;
    if (!fp) fp = (PFN_vkGetAccelerationStructureHandleNV)resolve("vkGetAccelerationStructureHandleNV");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, accelerationStructure, dataSize, pData);
}
VKAPI_ATTR void VKAPI_CALL vkGetAccelerationStructureMemoryRequirementsNV(VkDevice device, const VkAccelerationStructureMemoryRequirementsInfoNV* pInfo, VkMemoryRequirements2* pMemoryRequirements) {
    static PFN_vkGetAccelerationStructureMemoryRequirementsNV fp = nullptr;
    if (!fp) fp = (PFN_vkGetAccelerationStructureMemoryRequirementsNV)resolve("vkGetAccelerationStructureMemoryRequirementsNV");
    if (!fp) { return; }
    fp(device, pInfo, pMemoryRequirements);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetAccelerationStructureOpaqueCaptureDescriptorDataEXT(VkDevice device, const VkAccelerationStructureCaptureDescriptorDataInfoEXT* pInfo, void* pData) {
    static PFN_vkGetAccelerationStructureOpaqueCaptureDescriptorDataEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetAccelerationStructureOpaqueCaptureDescriptorDataEXT)resolve("vkGetAccelerationStructureOpaqueCaptureDescriptorDataEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pInfo, pData);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetAndroidHardwareBufferPropertiesANDROID(VkDevice device, const struct AHardwareBuffer* buffer, VkAndroidHardwareBufferPropertiesANDROID* pProperties) {
    static PFN_vkGetAndroidHardwareBufferPropertiesANDROID fp = nullptr;
    if (!fp) fp = (PFN_vkGetAndroidHardwareBufferPropertiesANDROID)resolve("vkGetAndroidHardwareBufferPropertiesANDROID");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, buffer, pProperties);
}
VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetBufferDeviceAddress(VkDevice device, const VkBufferDeviceAddressInfo* pInfo) {
    static PFN_vkGetBufferDeviceAddress fp = nullptr;
    if (!fp) fp = (PFN_vkGetBufferDeviceAddress)resolve("vkGetBufferDeviceAddress");
    if (!fp) { return 0; }
    return fp(device, pInfo);
}
VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetBufferDeviceAddressEXT(VkDevice device, const VkBufferDeviceAddressInfo* pInfo) {
    static PFN_vkGetBufferDeviceAddressEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetBufferDeviceAddressEXT)resolve("vkGetBufferDeviceAddressEXT");
    if (!fp) { return 0; }
    return fp(device, pInfo);
}
VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetBufferDeviceAddressKHR(VkDevice device, const VkBufferDeviceAddressInfo* pInfo) {
    static PFN_vkGetBufferDeviceAddressKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetBufferDeviceAddressKHR)resolve("vkGetBufferDeviceAddressKHR");
    if (!fp) { return 0; }
    return fp(device, pInfo);
}
VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements) {
    static PFN_vkGetBufferMemoryRequirements fp = nullptr;
    if (!fp) fp = (PFN_vkGetBufferMemoryRequirements)resolve("vkGetBufferMemoryRequirements");
    if (!fp) { return; }
    fp(device, buffer, pMemoryRequirements);
}
VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements2(VkDevice device, const VkBufferMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements) {
    static PFN_vkGetBufferMemoryRequirements2 fp = nullptr;
    if (!fp) fp = (PFN_vkGetBufferMemoryRequirements2)resolve("vkGetBufferMemoryRequirements2");
    if (!fp) { return; }
    fp(device, pInfo, pMemoryRequirements);
}
VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements2KHR(VkDevice device, const VkBufferMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements) {
    static PFN_vkGetBufferMemoryRequirements2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetBufferMemoryRequirements2KHR)resolve("vkGetBufferMemoryRequirements2KHR");
    if (!fp) { return; }
    fp(device, pInfo, pMemoryRequirements);
}
VKAPI_ATTR uint64_t VKAPI_CALL vkGetBufferOpaqueCaptureAddress(VkDevice device, const VkBufferDeviceAddressInfo* pInfo) {
    static PFN_vkGetBufferOpaqueCaptureAddress fp = nullptr;
    if (!fp) fp = (PFN_vkGetBufferOpaqueCaptureAddress)resolve("vkGetBufferOpaqueCaptureAddress");
    if (!fp) { return 0; }
    return fp(device, pInfo);
}
VKAPI_ATTR uint64_t VKAPI_CALL vkGetBufferOpaqueCaptureAddressKHR(VkDevice device, const VkBufferDeviceAddressInfo* pInfo) {
    static PFN_vkGetBufferOpaqueCaptureAddressKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetBufferOpaqueCaptureAddressKHR)resolve("vkGetBufferOpaqueCaptureAddressKHR");
    if (!fp) { return 0; }
    return fp(device, pInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetBufferOpaqueCaptureDescriptorDataEXT(VkDevice device, const VkBufferCaptureDescriptorDataInfoEXT* pInfo, void* pData) {
    static PFN_vkGetBufferOpaqueCaptureDescriptorDataEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetBufferOpaqueCaptureDescriptorDataEXT)resolve("vkGetBufferOpaqueCaptureDescriptorDataEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pInfo, pData);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetCalibratedTimestampsEXT(VkDevice device, uint32_t timestampCount, const VkCalibratedTimestampInfoKHR* pTimestampInfos, uint64_t* pTimestamps, uint64_t* pMaxDeviation) {
    static PFN_vkGetCalibratedTimestampsEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetCalibratedTimestampsEXT)resolve("vkGetCalibratedTimestampsEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, timestampCount, pTimestampInfos, pTimestamps, pMaxDeviation);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetCalibratedTimestampsKHR(VkDevice device, uint32_t timestampCount, const VkCalibratedTimestampInfoKHR* pTimestampInfos, uint64_t* pTimestamps, uint64_t* pMaxDeviation) {
    static PFN_vkGetCalibratedTimestampsKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetCalibratedTimestampsKHR)resolve("vkGetCalibratedTimestampsKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, timestampCount, pTimestampInfos, pTimestamps, pMaxDeviation);
}
VKAPI_ATTR void VKAPI_CALL vkGetClusterAccelerationStructureBuildSizesNV(VkDevice device, const VkClusterAccelerationStructureInputInfoNV* pInfo, VkAccelerationStructureBuildSizesInfoKHR* pSizeInfo) {
    static PFN_vkGetClusterAccelerationStructureBuildSizesNV fp = nullptr;
    if (!fp) fp = (PFN_vkGetClusterAccelerationStructureBuildSizesNV)resolve("vkGetClusterAccelerationStructureBuildSizesNV");
    if (!fp) { return; }
    fp(device, pInfo, pSizeInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetDataGraphPipelineAvailablePropertiesARM(VkDevice device, const VkDataGraphPipelineInfoARM* pPipelineInfo, uint32_t* pPropertiesCount, VkDataGraphPipelinePropertyARM* pProperties) {
    static PFN_vkGetDataGraphPipelineAvailablePropertiesARM fp = nullptr;
    if (!fp) fp = (PFN_vkGetDataGraphPipelineAvailablePropertiesARM)resolve("vkGetDataGraphPipelineAvailablePropertiesARM");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pPipelineInfo, pPropertiesCount, pProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetDataGraphPipelinePropertiesARM(VkDevice device, const VkDataGraphPipelineInfoARM* pPipelineInfo, uint32_t propertiesCount, VkDataGraphPipelinePropertyQueryResultARM* pProperties) {
    static PFN_vkGetDataGraphPipelinePropertiesARM fp = nullptr;
    if (!fp) fp = (PFN_vkGetDataGraphPipelinePropertiesARM)resolve("vkGetDataGraphPipelinePropertiesARM");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pPipelineInfo, propertiesCount, pProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetDataGraphPipelineSessionBindPointRequirementsARM(VkDevice device, const VkDataGraphPipelineSessionBindPointRequirementsInfoARM* pInfo, uint32_t* pBindPointRequirementCount, VkDataGraphPipelineSessionBindPointRequirementARM* pBindPointRequirements) {
    static PFN_vkGetDataGraphPipelineSessionBindPointRequirementsARM fp = nullptr;
    if (!fp) fp = (PFN_vkGetDataGraphPipelineSessionBindPointRequirementsARM)resolve("vkGetDataGraphPipelineSessionBindPointRequirementsARM");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pInfo, pBindPointRequirementCount, pBindPointRequirements);
}
VKAPI_ATTR void VKAPI_CALL vkGetDataGraphPipelineSessionMemoryRequirementsARM(VkDevice device, const VkDataGraphPipelineSessionMemoryRequirementsInfoARM* pInfo, VkMemoryRequirements2* pMemoryRequirements) {
    static PFN_vkGetDataGraphPipelineSessionMemoryRequirementsARM fp = nullptr;
    if (!fp) fp = (PFN_vkGetDataGraphPipelineSessionMemoryRequirementsARM)resolve("vkGetDataGraphPipelineSessionMemoryRequirementsARM");
    if (!fp) { return; }
    fp(device, pInfo, pMemoryRequirements);
}
VKAPI_ATTR uint32_t VKAPI_CALL vkGetDeferredOperationMaxConcurrencyKHR(VkDevice device, VkDeferredOperationKHR operation) {
    static PFN_vkGetDeferredOperationMaxConcurrencyKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetDeferredOperationMaxConcurrencyKHR)resolve("vkGetDeferredOperationMaxConcurrencyKHR");
    if (!fp) { return 0; }
    return fp(device, operation);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetDeferredOperationResultKHR(VkDevice device, VkDeferredOperationKHR operation) {
    static PFN_vkGetDeferredOperationResultKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetDeferredOperationResultKHR)resolve("vkGetDeferredOperationResultKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, operation);
}
VKAPI_ATTR void VKAPI_CALL vkGetDescriptorEXT(VkDevice device, const VkDescriptorGetInfoEXT* pDescriptorInfo, size_t dataSize, void* pDescriptor) {
    static PFN_vkGetDescriptorEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetDescriptorEXT)resolve("vkGetDescriptorEXT");
    if (!fp) { return; }
    fp(device, pDescriptorInfo, dataSize, pDescriptor);
}
VKAPI_ATTR void VKAPI_CALL vkGetDescriptorSetHostMappingVALVE(VkDevice device, VkDescriptorSet descriptorSet, void** ppData) {
    static PFN_vkGetDescriptorSetHostMappingVALVE fp = nullptr;
    if (!fp) fp = (PFN_vkGetDescriptorSetHostMappingVALVE)resolve("vkGetDescriptorSetHostMappingVALVE");
    if (!fp) { return; }
    fp(device, descriptorSet, ppData);
}
VKAPI_ATTR void VKAPI_CALL vkGetDescriptorSetLayoutBindingOffsetEXT(VkDevice device, VkDescriptorSetLayout layout, uint32_t binding, VkDeviceSize* pOffset) {
    static PFN_vkGetDescriptorSetLayoutBindingOffsetEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetDescriptorSetLayoutBindingOffsetEXT)resolve("vkGetDescriptorSetLayoutBindingOffsetEXT");
    if (!fp) { return; }
    fp(device, layout, binding, pOffset);
}
VKAPI_ATTR void VKAPI_CALL vkGetDescriptorSetLayoutHostMappingInfoVALVE(VkDevice device, const VkDescriptorSetBindingReferenceVALVE* pBindingReference, VkDescriptorSetLayoutHostMappingInfoVALVE* pHostMapping) {
    static PFN_vkGetDescriptorSetLayoutHostMappingInfoVALVE fp = nullptr;
    if (!fp) fp = (PFN_vkGetDescriptorSetLayoutHostMappingInfoVALVE)resolve("vkGetDescriptorSetLayoutHostMappingInfoVALVE");
    if (!fp) { return; }
    fp(device, pBindingReference, pHostMapping);
}
VKAPI_ATTR void VKAPI_CALL vkGetDescriptorSetLayoutSizeEXT(VkDevice device, VkDescriptorSetLayout layout, VkDeviceSize* pLayoutSizeInBytes) {
    static PFN_vkGetDescriptorSetLayoutSizeEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetDescriptorSetLayoutSizeEXT)resolve("vkGetDescriptorSetLayoutSizeEXT");
    if (!fp) { return; }
    fp(device, layout, pLayoutSizeInBytes);
}
VKAPI_ATTR void VKAPI_CALL vkGetDescriptorSetLayoutSupport(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, VkDescriptorSetLayoutSupport* pSupport) {
    static PFN_vkGetDescriptorSetLayoutSupport fp = nullptr;
    if (!fp) fp = (PFN_vkGetDescriptorSetLayoutSupport)resolve("vkGetDescriptorSetLayoutSupport");
    if (!fp) { return; }
    fp(device, pCreateInfo, pSupport);
}
VKAPI_ATTR void VKAPI_CALL vkGetDescriptorSetLayoutSupportKHR(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, VkDescriptorSetLayoutSupport* pSupport) {
    static PFN_vkGetDescriptorSetLayoutSupportKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetDescriptorSetLayoutSupportKHR)resolve("vkGetDescriptorSetLayoutSupportKHR");
    if (!fp) { return; }
    fp(device, pCreateInfo, pSupport);
}
VKAPI_ATTR void VKAPI_CALL vkGetDeviceAccelerationStructureCompatibilityKHR(VkDevice device, const VkAccelerationStructureVersionInfoKHR* pVersionInfo, VkAccelerationStructureCompatibilityKHR* pCompatibility) {
    static PFN_vkGetDeviceAccelerationStructureCompatibilityKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetDeviceAccelerationStructureCompatibilityKHR)resolve("vkGetDeviceAccelerationStructureCompatibilityKHR");
    if (!fp) { return; }
    fp(device, pVersionInfo, pCompatibility);
}
VKAPI_ATTR void VKAPI_CALL vkGetDeviceBufferMemoryRequirements(VkDevice device, const VkDeviceBufferMemoryRequirements* pInfo, VkMemoryRequirements2* pMemoryRequirements) {
    static PFN_vkGetDeviceBufferMemoryRequirements fp = nullptr;
    if (!fp) fp = (PFN_vkGetDeviceBufferMemoryRequirements)resolve("vkGetDeviceBufferMemoryRequirements");
    if (!fp) { return; }
    fp(device, pInfo, pMemoryRequirements);
}
VKAPI_ATTR void VKAPI_CALL vkGetDeviceBufferMemoryRequirementsKHR(VkDevice device, const VkDeviceBufferMemoryRequirements* pInfo, VkMemoryRequirements2* pMemoryRequirements) {
    static PFN_vkGetDeviceBufferMemoryRequirementsKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetDeviceBufferMemoryRequirementsKHR)resolve("vkGetDeviceBufferMemoryRequirementsKHR");
    if (!fp) { return; }
    fp(device, pInfo, pMemoryRequirements);
}
VKAPI_ATTR uint64_t VKAPI_CALL vkGetDeviceCombinedImageSamplerIndexNVX(VkDevice device, uint64_t imageViewIndex, uint64_t samplerIndex) {
    static PFN_vkGetDeviceCombinedImageSamplerIndexNVX fp = nullptr;
    if (!fp) fp = (PFN_vkGetDeviceCombinedImageSamplerIndexNVX)resolve("vkGetDeviceCombinedImageSamplerIndexNVX");
    if (!fp) { return 0; }
    return fp(device, imageViewIndex, samplerIndex);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceFaultDebugInfoKHR(VkDevice device, VkDeviceFaultDebugInfoKHR* pDebugInfo) {
    static PFN_vkGetDeviceFaultDebugInfoKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetDeviceFaultDebugInfoKHR)resolve("vkGetDeviceFaultDebugInfoKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pDebugInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceFaultInfoEXT(VkDevice device, VkDeviceFaultCountsEXT* pFaultCounts, VkDeviceFaultInfoEXT* pFaultInfo) {
    static PFN_vkGetDeviceFaultInfoEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetDeviceFaultInfoEXT)resolve("vkGetDeviceFaultInfoEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pFaultCounts, pFaultInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceFaultReportsKHR(VkDevice device, uint64_t timeout, uint32_t* pFaultCounts, VkDeviceFaultInfoKHR* pFaultInfo) {
    static PFN_vkGetDeviceFaultReportsKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetDeviceFaultReportsKHR)resolve("vkGetDeviceFaultReportsKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, timeout, pFaultCounts, pFaultInfo);
}
VKAPI_ATTR void VKAPI_CALL vkGetDeviceGroupPeerMemoryFeatures(VkDevice device, uint32_t heapIndex, uint32_t localDeviceIndex, uint32_t remoteDeviceIndex, VkPeerMemoryFeatureFlags* pPeerMemoryFeatures) {
    static PFN_vkGetDeviceGroupPeerMemoryFeatures fp = nullptr;
    if (!fp) fp = (PFN_vkGetDeviceGroupPeerMemoryFeatures)resolve("vkGetDeviceGroupPeerMemoryFeatures");
    if (!fp) { return; }
    fp(device, heapIndex, localDeviceIndex, remoteDeviceIndex, pPeerMemoryFeatures);
}
VKAPI_ATTR void VKAPI_CALL vkGetDeviceGroupPeerMemoryFeaturesKHR(VkDevice device, uint32_t heapIndex, uint32_t localDeviceIndex, uint32_t remoteDeviceIndex, VkPeerMemoryFeatureFlags* pPeerMemoryFeatures) {
    static PFN_vkGetDeviceGroupPeerMemoryFeaturesKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetDeviceGroupPeerMemoryFeaturesKHR)resolve("vkGetDeviceGroupPeerMemoryFeaturesKHR");
    if (!fp) { return; }
    fp(device, heapIndex, localDeviceIndex, remoteDeviceIndex, pPeerMemoryFeatures);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceGroupPresentCapabilitiesKHR(VkDevice device, VkDeviceGroupPresentCapabilitiesKHR* pDeviceGroupPresentCapabilities) {
    static PFN_vkGetDeviceGroupPresentCapabilitiesKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetDeviceGroupPresentCapabilitiesKHR)resolve("vkGetDeviceGroupPresentCapabilitiesKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pDeviceGroupPresentCapabilities);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceGroupSurfacePresentModesKHR(VkDevice device, VkSurfaceKHR surface, VkDeviceGroupPresentModeFlagsKHR* pModes) {
    static PFN_vkGetDeviceGroupSurfacePresentModesKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetDeviceGroupSurfacePresentModesKHR)resolve("vkGetDeviceGroupSurfacePresentModesKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, surface, pModes);
}
VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageMemoryRequirements(VkDevice device, const VkDeviceImageMemoryRequirements* pInfo, VkMemoryRequirements2* pMemoryRequirements) {
    static PFN_vkGetDeviceImageMemoryRequirements fp = nullptr;
    if (!fp) fp = (PFN_vkGetDeviceImageMemoryRequirements)resolve("vkGetDeviceImageMemoryRequirements");
    if (!fp) { return; }
    fp(device, pInfo, pMemoryRequirements);
}
VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageMemoryRequirementsKHR(VkDevice device, const VkDeviceImageMemoryRequirements* pInfo, VkMemoryRequirements2* pMemoryRequirements) {
    static PFN_vkGetDeviceImageMemoryRequirementsKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetDeviceImageMemoryRequirementsKHR)resolve("vkGetDeviceImageMemoryRequirementsKHR");
    if (!fp) { return; }
    fp(device, pInfo, pMemoryRequirements);
}
VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageSparseMemoryRequirements(VkDevice device, const VkDeviceImageMemoryRequirements* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2* pSparseMemoryRequirements) {
    static PFN_vkGetDeviceImageSparseMemoryRequirements fp = nullptr;
    if (!fp) fp = (PFN_vkGetDeviceImageSparseMemoryRequirements)resolve("vkGetDeviceImageSparseMemoryRequirements");
    if (!fp) { return; }
    fp(device, pInfo, pSparseMemoryRequirementCount, pSparseMemoryRequirements);
}
VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageSparseMemoryRequirementsKHR(VkDevice device, const VkDeviceImageMemoryRequirements* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2* pSparseMemoryRequirements) {
    static PFN_vkGetDeviceImageSparseMemoryRequirementsKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetDeviceImageSparseMemoryRequirementsKHR)resolve("vkGetDeviceImageSparseMemoryRequirementsKHR");
    if (!fp) { return; }
    fp(device, pInfo, pSparseMemoryRequirementCount, pSparseMemoryRequirements);
}
VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageSubresourceLayout(VkDevice device, const VkDeviceImageSubresourceInfo* pInfo, VkSubresourceLayout2* pLayout) {
    static PFN_vkGetDeviceImageSubresourceLayout fp = nullptr;
    if (!fp) fp = (PFN_vkGetDeviceImageSubresourceLayout)resolve("vkGetDeviceImageSubresourceLayout");
    if (!fp) { return; }
    fp(device, pInfo, pLayout);
}
VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageSubresourceLayoutKHR(VkDevice device, const VkDeviceImageSubresourceInfo* pInfo, VkSubresourceLayout2* pLayout) {
    static PFN_vkGetDeviceImageSubresourceLayoutKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetDeviceImageSubresourceLayoutKHR)resolve("vkGetDeviceImageSubresourceLayoutKHR");
    if (!fp) { return; }
    fp(device, pInfo, pLayout);
}
VKAPI_ATTR void VKAPI_CALL vkGetDeviceMemoryCommitment(VkDevice device, VkDeviceMemory memory, VkDeviceSize* pCommittedMemoryInBytes) {
    static PFN_vkGetDeviceMemoryCommitment fp = nullptr;
    if (!fp) fp = (PFN_vkGetDeviceMemoryCommitment)resolve("vkGetDeviceMemoryCommitment");
    if (!fp) { return; }
    fp(device, memory, pCommittedMemoryInBytes);
}
VKAPI_ATTR uint64_t VKAPI_CALL vkGetDeviceMemoryOpaqueCaptureAddress(VkDevice device, const VkDeviceMemoryOpaqueCaptureAddressInfo* pInfo) {
    static PFN_vkGetDeviceMemoryOpaqueCaptureAddress fp = nullptr;
    if (!fp) fp = (PFN_vkGetDeviceMemoryOpaqueCaptureAddress)resolve("vkGetDeviceMemoryOpaqueCaptureAddress");
    if (!fp) { return 0; }
    return fp(device, pInfo);
}
VKAPI_ATTR uint64_t VKAPI_CALL vkGetDeviceMemoryOpaqueCaptureAddressKHR(VkDevice device, const VkDeviceMemoryOpaqueCaptureAddressInfo* pInfo) {
    static PFN_vkGetDeviceMemoryOpaqueCaptureAddressKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetDeviceMemoryOpaqueCaptureAddressKHR)resolve("vkGetDeviceMemoryOpaqueCaptureAddressKHR");
    if (!fp) { return 0; }
    return fp(device, pInfo);
}
VKAPI_ATTR void VKAPI_CALL vkGetDeviceMicromapCompatibilityEXT(VkDevice device, const VkMicromapVersionInfoEXT* pVersionInfo, VkAccelerationStructureCompatibilityKHR* pCompatibility) {
    static PFN_vkGetDeviceMicromapCompatibilityEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetDeviceMicromapCompatibilityEXT)resolve("vkGetDeviceMicromapCompatibilityEXT");
    if (!fp) { return; }
    fp(device, pVersionInfo, pCompatibility);
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* pName) {
    static PFN_vkGetDeviceProcAddr fp = nullptr;
    if (!fp) fp = (PFN_vkGetDeviceProcAddr)resolve("vkGetDeviceProcAddr");
    if (!fp) { return (PFN_vkVoidFunction)0; }
    return fp(device, pName);
}
VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue) {
    static PFN_vkGetDeviceQueue fp = nullptr;
    if (!fp) fp = (PFN_vkGetDeviceQueue)resolve("vkGetDeviceQueue");
    if (!fp) { return; }
    fp(device, queueFamilyIndex, queueIndex, pQueue);
}
VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* pQueueInfo, VkQueue* pQueue) {
    static PFN_vkGetDeviceQueue2 fp = nullptr;
    if (!fp) fp = (PFN_vkGetDeviceQueue2)resolve("vkGetDeviceQueue2");
    if (!fp) { return; }
    fp(device, pQueueInfo, pQueue);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceSubpassShadingMaxWorkgroupSizeHUAWEI(VkDevice device, VkRenderPass renderpass, VkExtent2D* pMaxWorkgroupSize) {
    static PFN_vkGetDeviceSubpassShadingMaxWorkgroupSizeHUAWEI fp = nullptr;
    if (!fp) fp = (PFN_vkGetDeviceSubpassShadingMaxWorkgroupSizeHUAWEI)resolve("vkGetDeviceSubpassShadingMaxWorkgroupSizeHUAWEI");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, renderpass, pMaxWorkgroupSize);
}
VKAPI_ATTR void VKAPI_CALL vkGetDeviceTensorMemoryRequirementsARM(VkDevice device, const VkDeviceTensorMemoryRequirementsARM* pInfo, VkMemoryRequirements2* pMemoryRequirements) {
    static PFN_vkGetDeviceTensorMemoryRequirementsARM fp = nullptr;
    if (!fp) fp = (PFN_vkGetDeviceTensorMemoryRequirementsARM)resolve("vkGetDeviceTensorMemoryRequirementsARM");
    if (!fp) { return; }
    fp(device, pInfo, pMemoryRequirements);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetDisplayModeProperties2KHR(VkPhysicalDevice physicalDevice, VkDisplayKHR display, uint32_t* pPropertyCount, VkDisplayModeProperties2KHR* pProperties) {
    static PFN_vkGetDisplayModeProperties2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetDisplayModeProperties2KHR)resolve("vkGetDisplayModeProperties2KHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, display, pPropertyCount, pProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetDisplayModePropertiesKHR(VkPhysicalDevice physicalDevice, VkDisplayKHR display, uint32_t* pPropertyCount, VkDisplayModePropertiesKHR* pProperties) {
    static PFN_vkGetDisplayModePropertiesKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetDisplayModePropertiesKHR)resolve("vkGetDisplayModePropertiesKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, display, pPropertyCount, pProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetDisplayPlaneCapabilities2KHR(VkPhysicalDevice physicalDevice, const VkDisplayPlaneInfo2KHR* pDisplayPlaneInfo, VkDisplayPlaneCapabilities2KHR* pCapabilities) {
    static PFN_vkGetDisplayPlaneCapabilities2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetDisplayPlaneCapabilities2KHR)resolve("vkGetDisplayPlaneCapabilities2KHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, pDisplayPlaneInfo, pCapabilities);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetDisplayPlaneCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkDisplayModeKHR mode, uint32_t planeIndex, VkDisplayPlaneCapabilitiesKHR* pCapabilities) {
    static PFN_vkGetDisplayPlaneCapabilitiesKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetDisplayPlaneCapabilitiesKHR)resolve("vkGetDisplayPlaneCapabilitiesKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, mode, planeIndex, pCapabilities);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetDisplayPlaneSupportedDisplaysKHR(VkPhysicalDevice physicalDevice, uint32_t planeIndex, uint32_t* pDisplayCount, VkDisplayKHR* pDisplays) {
    static PFN_vkGetDisplayPlaneSupportedDisplaysKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetDisplayPlaneSupportedDisplaysKHR)resolve("vkGetDisplayPlaneSupportedDisplaysKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, planeIndex, pDisplayCount, pDisplays);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetDrmDisplayEXT(VkPhysicalDevice physicalDevice, int32_t drmFd, uint32_t connectorId, VkDisplayKHR* display) {
    static PFN_vkGetDrmDisplayEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetDrmDisplayEXT)resolve("vkGetDrmDisplayEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, drmFd, connectorId, display);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetDynamicRenderingTilePropertiesQCOM(VkDevice device, const VkRenderingInfo* pRenderingInfo, VkTilePropertiesQCOM* pProperties) {
    static PFN_vkGetDynamicRenderingTilePropertiesQCOM fp = nullptr;
    if (!fp) fp = (PFN_vkGetDynamicRenderingTilePropertiesQCOM)resolve("vkGetDynamicRenderingTilePropertiesQCOM");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pRenderingInfo, pProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetEncodedVideoSessionParametersKHR(VkDevice device, const VkVideoEncodeSessionParametersGetInfoKHR* pVideoSessionParametersInfo, VkVideoEncodeSessionParametersFeedbackInfoKHR* pFeedbackInfo, size_t* pDataSize, void* pData) {
    static PFN_vkGetEncodedVideoSessionParametersKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetEncodedVideoSessionParametersKHR)resolve("vkGetEncodedVideoSessionParametersKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pVideoSessionParametersInfo, pFeedbackInfo, pDataSize, pData);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetEventStatus(VkDevice device, VkEvent event) {
    static PFN_vkGetEventStatus fp = nullptr;
    if (!fp) fp = (PFN_vkGetEventStatus)resolve("vkGetEventStatus");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, event);
}
VKAPI_ATTR void VKAPI_CALL vkGetExternalComputeQueueDataNV(VkExternalComputeQueueNV externalQueue, VkExternalComputeQueueDataParamsNV* params, void* pData) {
    static PFN_vkGetExternalComputeQueueDataNV fp = nullptr;
    if (!fp) fp = (PFN_vkGetExternalComputeQueueDataNV)resolve("vkGetExternalComputeQueueDataNV");
    if (!fp) { return; }
    fp(externalQueue, params, pData);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceFdKHR(VkDevice device, const VkFenceGetFdInfoKHR* pGetFdInfo, int* pFd) {
    static PFN_vkGetFenceFdKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetFenceFdKHR)resolve("vkGetFenceFdKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pGetFdInfo, pFd);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceStatus(VkDevice device, VkFence fence) {
    static PFN_vkGetFenceStatus fp = nullptr;
    if (!fp) fp = (PFN_vkGetFenceStatus)resolve("vkGetFenceStatus");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, fence);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetFramebufferTilePropertiesQCOM(VkDevice device, VkFramebuffer framebuffer, uint32_t* pPropertiesCount, VkTilePropertiesQCOM* pProperties) {
    static PFN_vkGetFramebufferTilePropertiesQCOM fp = nullptr;
    if (!fp) fp = (PFN_vkGetFramebufferTilePropertiesQCOM)resolve("vkGetFramebufferTilePropertiesQCOM");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, framebuffer, pPropertiesCount, pProperties);
}
VKAPI_ATTR void VKAPI_CALL vkGetGeneratedCommandsMemoryRequirementsEXT(VkDevice device, const VkGeneratedCommandsMemoryRequirementsInfoEXT* pInfo, VkMemoryRequirements2* pMemoryRequirements) {
    static PFN_vkGetGeneratedCommandsMemoryRequirementsEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetGeneratedCommandsMemoryRequirementsEXT)resolve("vkGetGeneratedCommandsMemoryRequirementsEXT");
    if (!fp) { return; }
    fp(device, pInfo, pMemoryRequirements);
}
VKAPI_ATTR void VKAPI_CALL vkGetGeneratedCommandsMemoryRequirementsNV(VkDevice device, const VkGeneratedCommandsMemoryRequirementsInfoNV* pInfo, VkMemoryRequirements2* pMemoryRequirements) {
    static PFN_vkGetGeneratedCommandsMemoryRequirementsNV fp = nullptr;
    if (!fp) fp = (PFN_vkGetGeneratedCommandsMemoryRequirementsNV)resolve("vkGetGeneratedCommandsMemoryRequirementsNV");
    if (!fp) { return; }
    fp(device, pInfo, pMemoryRequirements);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetGpaDeviceClockInfoAMD(VkDevice device, VkGpaDeviceGetClockInfoAMD* pInfo) {
    static PFN_vkGetGpaDeviceClockInfoAMD fp = nullptr;
    if (!fp) fp = (PFN_vkGetGpaDeviceClockInfoAMD)resolve("vkGetGpaDeviceClockInfoAMD");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetGpaSessionResultsAMD(VkDevice device, VkGpaSessionAMD gpaSession, uint32_t sampleID, size_t* pSizeInBytes, void* pData) {
    static PFN_vkGetGpaSessionResultsAMD fp = nullptr;
    if (!fp) fp = (PFN_vkGetGpaSessionResultsAMD)resolve("vkGetGpaSessionResultsAMD");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, gpaSession, sampleID, pSizeInBytes, pData);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetGpaSessionStatusAMD(VkDevice device, VkGpaSessionAMD gpaSession) {
    static PFN_vkGetGpaSessionStatusAMD fp = nullptr;
    if (!fp) fp = (PFN_vkGetGpaSessionStatusAMD)resolve("vkGetGpaSessionStatusAMD");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, gpaSession);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetImageDrmFormatModifierPropertiesEXT(VkDevice device, VkImage image, VkImageDrmFormatModifierPropertiesEXT* pProperties) {
    static PFN_vkGetImageDrmFormatModifierPropertiesEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetImageDrmFormatModifierPropertiesEXT)resolve("vkGetImageDrmFormatModifierPropertiesEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, image, pProperties);
}
VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements) {
    static PFN_vkGetImageMemoryRequirements fp = nullptr;
    if (!fp) fp = (PFN_vkGetImageMemoryRequirements)resolve("vkGetImageMemoryRequirements");
    if (!fp) { return; }
    fp(device, image, pMemoryRequirements);
}
VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements2(VkDevice device, const VkImageMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements) {
    static PFN_vkGetImageMemoryRequirements2 fp = nullptr;
    if (!fp) fp = (PFN_vkGetImageMemoryRequirements2)resolve("vkGetImageMemoryRequirements2");
    if (!fp) { return; }
    fp(device, pInfo, pMemoryRequirements);
}
VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements2KHR(VkDevice device, const VkImageMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements) {
    static PFN_vkGetImageMemoryRequirements2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetImageMemoryRequirements2KHR)resolve("vkGetImageMemoryRequirements2KHR");
    if (!fp) { return; }
    fp(device, pInfo, pMemoryRequirements);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetImageOpaqueCaptureDataEXT(VkDevice device, uint32_t imageCount, const VkImage* pImages, VkHostAddressRangeEXT* pDatas) {
    static PFN_vkGetImageOpaqueCaptureDataEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetImageOpaqueCaptureDataEXT)resolve("vkGetImageOpaqueCaptureDataEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, imageCount, pImages, pDatas);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetImageOpaqueCaptureDescriptorDataEXT(VkDevice device, const VkImageCaptureDescriptorDataInfoEXT* pInfo, void* pData) {
    static PFN_vkGetImageOpaqueCaptureDescriptorDataEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetImageOpaqueCaptureDescriptorDataEXT)resolve("vkGetImageOpaqueCaptureDescriptorDataEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pInfo, pData);
}
VKAPI_ATTR void VKAPI_CALL vkGetImageSparseMemoryRequirements(VkDevice device, VkImage image, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements* pSparseMemoryRequirements) {
    static PFN_vkGetImageSparseMemoryRequirements fp = nullptr;
    if (!fp) fp = (PFN_vkGetImageSparseMemoryRequirements)resolve("vkGetImageSparseMemoryRequirements");
    if (!fp) { return; }
    fp(device, image, pSparseMemoryRequirementCount, pSparseMemoryRequirements);
}
VKAPI_ATTR void VKAPI_CALL vkGetImageSparseMemoryRequirements2(VkDevice device, const VkImageSparseMemoryRequirementsInfo2* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2* pSparseMemoryRequirements) {
    static PFN_vkGetImageSparseMemoryRequirements2 fp = nullptr;
    if (!fp) fp = (PFN_vkGetImageSparseMemoryRequirements2)resolve("vkGetImageSparseMemoryRequirements2");
    if (!fp) { return; }
    fp(device, pInfo, pSparseMemoryRequirementCount, pSparseMemoryRequirements);
}
VKAPI_ATTR void VKAPI_CALL vkGetImageSparseMemoryRequirements2KHR(VkDevice device, const VkImageSparseMemoryRequirementsInfo2* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2* pSparseMemoryRequirements) {
    static PFN_vkGetImageSparseMemoryRequirements2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetImageSparseMemoryRequirements2KHR)resolve("vkGetImageSparseMemoryRequirements2KHR");
    if (!fp) { return; }
    fp(device, pInfo, pSparseMemoryRequirementCount, pSparseMemoryRequirements);
}
VKAPI_ATTR void VKAPI_CALL vkGetImageSubresourceLayout(VkDevice device, VkImage image, const VkImageSubresource* pSubresource, VkSubresourceLayout* pLayout) {
    static PFN_vkGetImageSubresourceLayout fp = nullptr;
    if (!fp) fp = (PFN_vkGetImageSubresourceLayout)resolve("vkGetImageSubresourceLayout");
    if (!fp) { return; }
    fp(device, image, pSubresource, pLayout);
}
VKAPI_ATTR void VKAPI_CALL vkGetImageSubresourceLayout2(VkDevice device, VkImage image, const VkImageSubresource2* pSubresource, VkSubresourceLayout2* pLayout) {
    static PFN_vkGetImageSubresourceLayout2 fp = nullptr;
    if (!fp) fp = (PFN_vkGetImageSubresourceLayout2)resolve("vkGetImageSubresourceLayout2");
    if (!fp) { return; }
    fp(device, image, pSubresource, pLayout);
}
VKAPI_ATTR void VKAPI_CALL vkGetImageSubresourceLayout2EXT(VkDevice device, VkImage image, const VkImageSubresource2* pSubresource, VkSubresourceLayout2* pLayout) {
    static PFN_vkGetImageSubresourceLayout2EXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetImageSubresourceLayout2EXT)resolve("vkGetImageSubresourceLayout2EXT");
    if (!fp) { return; }
    fp(device, image, pSubresource, pLayout);
}
VKAPI_ATTR void VKAPI_CALL vkGetImageSubresourceLayout2KHR(VkDevice device, VkImage image, const VkImageSubresource2* pSubresource, VkSubresourceLayout2* pLayout) {
    static PFN_vkGetImageSubresourceLayout2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetImageSubresourceLayout2KHR)resolve("vkGetImageSubresourceLayout2KHR");
    if (!fp) { return; }
    fp(device, image, pSubresource, pLayout);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetImageViewAddressNVX(VkDevice device, VkImageView imageView, VkImageViewAddressPropertiesNVX* pProperties) {
    static PFN_vkGetImageViewAddressNVX fp = nullptr;
    if (!fp) fp = (PFN_vkGetImageViewAddressNVX)resolve("vkGetImageViewAddressNVX");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, imageView, pProperties);
}
VKAPI_ATTR uint64_t VKAPI_CALL vkGetImageViewHandle64NVX(VkDevice device, const VkImageViewHandleInfoNVX* pInfo) {
    static PFN_vkGetImageViewHandle64NVX fp = nullptr;
    if (!fp) fp = (PFN_vkGetImageViewHandle64NVX)resolve("vkGetImageViewHandle64NVX");
    if (!fp) { return 0; }
    return fp(device, pInfo);
}
VKAPI_ATTR uint32_t VKAPI_CALL vkGetImageViewHandleNVX(VkDevice device, const VkImageViewHandleInfoNVX* pInfo) {
    static PFN_vkGetImageViewHandleNVX fp = nullptr;
    if (!fp) fp = (PFN_vkGetImageViewHandleNVX)resolve("vkGetImageViewHandleNVX");
    if (!fp) { return 0; }
    return fp(device, pInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetImageViewOpaqueCaptureDescriptorDataEXT(VkDevice device, const VkImageViewCaptureDescriptorDataInfoEXT* pInfo, void* pData) {
    static PFN_vkGetImageViewOpaqueCaptureDescriptorDataEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetImageViewOpaqueCaptureDescriptorDataEXT)resolve("vkGetImageViewOpaqueCaptureDescriptorDataEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pInfo, pData);
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* pName) {
    static PFN_vkGetInstanceProcAddr fp = nullptr;
    if (!fp) fp = (PFN_vkGetInstanceProcAddr)resolve("vkGetInstanceProcAddr");
    if (!fp) { return (PFN_vkVoidFunction)0; }
    return fp(instance, pName);
}
VKAPI_ATTR void VKAPI_CALL vkGetLatencyTimingsLegacyNV(VkDevice device, void* pTimings) {
    static PFN_vkGetLatencyTimingsLegacyNV fp = nullptr;
    if (!fp) fp = (PFN_vkGetLatencyTimingsLegacyNV)resolve("vkGetLatencyTimingsLegacyNV");
    if (!fp) { return; }
    fp(device, pTimings);
}
VKAPI_ATTR void VKAPI_CALL vkGetLatencyTimingsNV(VkDevice device, VkSwapchainKHR swapchain, VkGetLatencyMarkerInfoNV* pLatencyMarkerInfo) {
    static PFN_vkGetLatencyTimingsNV fp = nullptr;
    if (!fp) fp = (PFN_vkGetLatencyTimingsNV)resolve("vkGetLatencyTimingsNV");
    if (!fp) { return; }
    fp(device, swapchain, pLatencyMarkerInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetMemoryAndroidHardwareBufferANDROID(VkDevice device, const VkMemoryGetAndroidHardwareBufferInfoANDROID* pInfo, struct AHardwareBuffer** pBuffer) {
    static PFN_vkGetMemoryAndroidHardwareBufferANDROID fp = nullptr;
    if (!fp) fp = (PFN_vkGetMemoryAndroidHardwareBufferANDROID)resolve("vkGetMemoryAndroidHardwareBufferANDROID");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pInfo, pBuffer);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetMemoryFdKHR(VkDevice device, const VkMemoryGetFdInfoKHR* pGetFdInfo, int* pFd) {
    static PFN_vkGetMemoryFdKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetMemoryFdKHR)resolve("vkGetMemoryFdKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pGetFdInfo, pFd);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetMemoryFdPropertiesKHR(VkDevice device, VkExternalMemoryHandleTypeFlagBits handleType, int fd, VkMemoryFdPropertiesKHR* pMemoryFdProperties) {
    static PFN_vkGetMemoryFdPropertiesKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetMemoryFdPropertiesKHR)resolve("vkGetMemoryFdPropertiesKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, handleType, fd, pMemoryFdProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetMemoryHostPointerPropertiesEXT(VkDevice device, VkExternalMemoryHandleTypeFlagBits handleType, const void* pHostPointer, VkMemoryHostPointerPropertiesEXT* pMemoryHostPointerProperties) {
    static PFN_vkGetMemoryHostPointerPropertiesEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetMemoryHostPointerPropertiesEXT)resolve("vkGetMemoryHostPointerPropertiesEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, handleType, pHostPointer, pMemoryHostPointerProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetMemoryRemoteAddressNV(VkDevice device, const VkMemoryGetRemoteAddressInfoNV* pMemoryGetRemoteAddressInfo, VkRemoteAddressNV* pAddress) {
    static PFN_vkGetMemoryRemoteAddressNV fp = nullptr;
    if (!fp) fp = (PFN_vkGetMemoryRemoteAddressNV)resolve("vkGetMemoryRemoteAddressNV");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pMemoryGetRemoteAddressInfo, pAddress);
}
VKAPI_ATTR void VKAPI_CALL vkGetMicromapBuildSizesEXT(VkDevice device, VkAccelerationStructureBuildTypeKHR buildType, const VkMicromapBuildInfoEXT* pBuildInfo, VkMicromapBuildSizesInfoEXT* pSizeInfo) {
    static PFN_vkGetMicromapBuildSizesEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetMicromapBuildSizesEXT)resolve("vkGetMicromapBuildSizesEXT");
    if (!fp) { return; }
    fp(device, buildType, pBuildInfo, pSizeInfo);
}
VKAPI_ATTR void VKAPI_CALL vkGetPartitionedAccelerationStructuresBuildSizesNV(VkDevice device, const VkPartitionedAccelerationStructureInstancesInputNV* pInfo, VkAccelerationStructureBuildSizesInfoKHR* pSizeInfo) {
    static PFN_vkGetPartitionedAccelerationStructuresBuildSizesNV fp = nullptr;
    if (!fp) fp = (PFN_vkGetPartitionedAccelerationStructuresBuildSizesNV)resolve("vkGetPartitionedAccelerationStructuresBuildSizesNV");
    if (!fp) { return; }
    fp(device, pInfo, pSizeInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPastPresentationTimingEXT(VkDevice device, const VkPastPresentationTimingInfoEXT* pPastPresentationTimingInfo, VkPastPresentationTimingPropertiesEXT* pPastPresentationTimingProperties) {
    static PFN_vkGetPastPresentationTimingEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetPastPresentationTimingEXT)resolve("vkGetPastPresentationTimingEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pPastPresentationTimingInfo, pPastPresentationTimingProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPastPresentationTimingGOOGLE(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pPresentationTimingCount, VkPastPresentationTimingGOOGLE* pPresentationTimings) {
    static PFN_vkGetPastPresentationTimingGOOGLE fp = nullptr;
    if (!fp) fp = (PFN_vkGetPastPresentationTimingGOOGLE)resolve("vkGetPastPresentationTimingGOOGLE");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, swapchain, pPresentationTimingCount, pPresentationTimings);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPerformanceParameterINTEL(VkDevice device, VkPerformanceParameterTypeINTEL parameter, VkPerformanceValueINTEL* pValue) {
    static PFN_vkGetPerformanceParameterINTEL fp = nullptr;
    if (!fp) fp = (PFN_vkGetPerformanceParameterINTEL)resolve("vkGetPerformanceParameterINTEL");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, parameter, pValue);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceCalibrateableTimeDomainsEXT(VkPhysicalDevice physicalDevice, uint32_t* pTimeDomainCount, VkTimeDomainKHR* pTimeDomains) {
    static PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT)resolve("vkGetPhysicalDeviceCalibrateableTimeDomainsEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, pTimeDomainCount, pTimeDomains);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceCalibrateableTimeDomainsKHR(VkPhysicalDevice physicalDevice, uint32_t* pTimeDomainCount, VkTimeDomainKHR* pTimeDomains) {
    static PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsKHR)resolve("vkGetPhysicalDeviceCalibrateableTimeDomainsKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, pTimeDomainCount, pTimeDomains);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceCooperativeMatrixFlexibleDimensionsPropertiesNV(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkCooperativeMatrixFlexibleDimensionsPropertiesNV* pProperties) {
    static PFN_vkGetPhysicalDeviceCooperativeMatrixFlexibleDimensionsPropertiesNV fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceCooperativeMatrixFlexibleDimensionsPropertiesNV)resolve("vkGetPhysicalDeviceCooperativeMatrixFlexibleDimensionsPropertiesNV");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, pPropertyCount, pProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceCooperativeMatrixProperties2EXT(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceCooperativeMatrixInfo2EXT* pCooperativeMatrixInfo, uint32_t* pPropertyCount, VkCooperativeMatrixProperties2EXT* pProperties) {
    static PFN_vkGetPhysicalDeviceCooperativeMatrixProperties2EXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceCooperativeMatrixProperties2EXT)resolve("vkGetPhysicalDeviceCooperativeMatrixProperties2EXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, pCooperativeMatrixInfo, pPropertyCount, pProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkCooperativeMatrixPropertiesKHR* pProperties) {
    static PFN_vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR)resolve("vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, pPropertyCount, pProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceCooperativeMatrixPropertiesNV(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkCooperativeMatrixPropertiesNV* pProperties) {
    static PFN_vkGetPhysicalDeviceCooperativeMatrixPropertiesNV fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceCooperativeMatrixPropertiesNV)resolve("vkGetPhysicalDeviceCooperativeMatrixPropertiesNV");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, pPropertyCount, pProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceCooperativeVectorPropertiesNV(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkCooperativeVectorPropertiesNV* pProperties) {
    static PFN_vkGetPhysicalDeviceCooperativeVectorPropertiesNV fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceCooperativeVectorPropertiesNV)resolve("vkGetPhysicalDeviceCooperativeVectorPropertiesNV");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, pPropertyCount, pProperties);
}
VKAPI_ATTR VkDeviceSize VKAPI_CALL vkGetPhysicalDeviceDescriptorSizeEXT(VkPhysicalDevice physicalDevice, VkDescriptorType descriptorType) {
    static PFN_vkGetPhysicalDeviceDescriptorSizeEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceDescriptorSizeEXT)resolve("vkGetPhysicalDeviceDescriptorSizeEXT");
    if (!fp) { return 0; }
    return fp(physicalDevice, descriptorType);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceDisplayPlaneProperties2KHR(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkDisplayPlaneProperties2KHR* pProperties) {
    static PFN_vkGetPhysicalDeviceDisplayPlaneProperties2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceDisplayPlaneProperties2KHR)resolve("vkGetPhysicalDeviceDisplayPlaneProperties2KHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, pPropertyCount, pProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceDisplayPlanePropertiesKHR(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkDisplayPlanePropertiesKHR* pProperties) {
    static PFN_vkGetPhysicalDeviceDisplayPlanePropertiesKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceDisplayPlanePropertiesKHR)resolve("vkGetPhysicalDeviceDisplayPlanePropertiesKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, pPropertyCount, pProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceDisplayProperties2KHR(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkDisplayProperties2KHR* pProperties) {
    static PFN_vkGetPhysicalDeviceDisplayProperties2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceDisplayProperties2KHR)resolve("vkGetPhysicalDeviceDisplayProperties2KHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, pPropertyCount, pProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceDisplayPropertiesKHR(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkDisplayPropertiesKHR* pProperties) {
    static PFN_vkGetPhysicalDeviceDisplayPropertiesKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceDisplayPropertiesKHR)resolve("vkGetPhysicalDeviceDisplayPropertiesKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, pPropertyCount, pProperties);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalBufferProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfo* pExternalBufferInfo, VkExternalBufferProperties* pExternalBufferProperties) {
    static PFN_vkGetPhysicalDeviceExternalBufferProperties fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceExternalBufferProperties)resolve("vkGetPhysicalDeviceExternalBufferProperties");
    if (!fp) { return; }
    fp(physicalDevice, pExternalBufferInfo, pExternalBufferProperties);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalBufferPropertiesKHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfo* pExternalBufferInfo, VkExternalBufferProperties* pExternalBufferProperties) {
    static PFN_vkGetPhysicalDeviceExternalBufferPropertiesKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceExternalBufferPropertiesKHR)resolve("vkGetPhysicalDeviceExternalBufferPropertiesKHR");
    if (!fp) { return; }
    fp(physicalDevice, pExternalBufferInfo, pExternalBufferProperties);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalFenceProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalFenceInfo* pExternalFenceInfo, VkExternalFenceProperties* pExternalFenceProperties) {
    static PFN_vkGetPhysicalDeviceExternalFenceProperties fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceExternalFenceProperties)resolve("vkGetPhysicalDeviceExternalFenceProperties");
    if (!fp) { return; }
    fp(physicalDevice, pExternalFenceInfo, pExternalFenceProperties);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalFencePropertiesKHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalFenceInfo* pExternalFenceInfo, VkExternalFenceProperties* pExternalFenceProperties) {
    static PFN_vkGetPhysicalDeviceExternalFencePropertiesKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceExternalFencePropertiesKHR)resolve("vkGetPhysicalDeviceExternalFencePropertiesKHR");
    if (!fp) { return; }
    fp(physicalDevice, pExternalFenceInfo, pExternalFenceProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceExternalImageFormatPropertiesNV(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkExternalMemoryHandleTypeFlagsNV externalHandleType, VkExternalImageFormatPropertiesNV* pExternalImageFormatProperties) {
    static PFN_vkGetPhysicalDeviceExternalImageFormatPropertiesNV fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceExternalImageFormatPropertiesNV)resolve("vkGetPhysicalDeviceExternalImageFormatPropertiesNV");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, format, type, tiling, usage, flags, externalHandleType, pExternalImageFormatProperties);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalSemaphoreProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalSemaphoreInfo* pExternalSemaphoreInfo, VkExternalSemaphoreProperties* pExternalSemaphoreProperties) {
    static PFN_vkGetPhysicalDeviceExternalSemaphoreProperties fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceExternalSemaphoreProperties)resolve("vkGetPhysicalDeviceExternalSemaphoreProperties");
    if (!fp) { return; }
    fp(physicalDevice, pExternalSemaphoreInfo, pExternalSemaphoreProperties);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalSemaphorePropertiesKHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalSemaphoreInfo* pExternalSemaphoreInfo, VkExternalSemaphoreProperties* pExternalSemaphoreProperties) {
    static PFN_vkGetPhysicalDeviceExternalSemaphorePropertiesKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceExternalSemaphorePropertiesKHR)resolve("vkGetPhysicalDeviceExternalSemaphorePropertiesKHR");
    if (!fp) { return; }
    fp(physicalDevice, pExternalSemaphoreInfo, pExternalSemaphoreProperties);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalTensorPropertiesARM(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalTensorInfoARM* pExternalTensorInfo, VkExternalTensorPropertiesARM* pExternalTensorProperties) {
    static PFN_vkGetPhysicalDeviceExternalTensorPropertiesARM fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceExternalTensorPropertiesARM)resolve("vkGetPhysicalDeviceExternalTensorPropertiesARM");
    if (!fp) { return; }
    fp(physicalDevice, pExternalTensorInfo, pExternalTensorProperties);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures* pFeatures) {
    static PFN_vkGetPhysicalDeviceFeatures fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceFeatures)resolve("vkGetPhysicalDeviceFeatures");
    if (!fp) { return; }
    fp(physicalDevice, pFeatures);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2* pFeatures) {
    static PFN_vkGetPhysicalDeviceFeatures2 fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceFeatures2)resolve("vkGetPhysicalDeviceFeatures2");
    if (!fp) { return; }
    fp(physicalDevice, pFeatures);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2* pFeatures) {
    static PFN_vkGetPhysicalDeviceFeatures2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceFeatures2KHR)resolve("vkGetPhysicalDeviceFeatures2KHR");
    if (!fp) { return; }
    fp(physicalDevice, pFeatures);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties* pFormatProperties) {
    static PFN_vkGetPhysicalDeviceFormatProperties fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceFormatProperties)resolve("vkGetPhysicalDeviceFormatProperties");
    if (!fp) { return; }
    fp(physicalDevice, format, pFormatProperties);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties2* pFormatProperties) {
    static PFN_vkGetPhysicalDeviceFormatProperties2 fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceFormatProperties2)resolve("vkGetPhysicalDeviceFormatProperties2");
    if (!fp) { return; }
    fp(physicalDevice, format, pFormatProperties);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties2KHR(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties2* pFormatProperties) {
    static PFN_vkGetPhysicalDeviceFormatProperties2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceFormatProperties2KHR)resolve("vkGetPhysicalDeviceFormatProperties2KHR");
    if (!fp) { return; }
    fp(physicalDevice, format, pFormatProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceFragmentShadingRatesKHR(VkPhysicalDevice physicalDevice, uint32_t* pFragmentShadingRateCount, VkPhysicalDeviceFragmentShadingRateKHR* pFragmentShadingRates) {
    static PFN_vkGetPhysicalDeviceFragmentShadingRatesKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceFragmentShadingRatesKHR)resolve("vkGetPhysicalDeviceFragmentShadingRatesKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, pFragmentShadingRateCount, pFragmentShadingRates);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties* pImageFormatProperties) {
    static PFN_vkGetPhysicalDeviceImageFormatProperties fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceImageFormatProperties)resolve("vkGetPhysicalDeviceImageFormatProperties");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, format, type, tiling, usage, flags, pImageFormatProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo, VkImageFormatProperties2* pImageFormatProperties) {
    static PFN_vkGetPhysicalDeviceImageFormatProperties2 fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceImageFormatProperties2)resolve("vkGetPhysicalDeviceImageFormatProperties2");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, pImageFormatInfo, pImageFormatProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo, VkImageFormatProperties2* pImageFormatProperties) {
    static PFN_vkGetPhysicalDeviceImageFormatProperties2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceImageFormatProperties2KHR)resolve("vkGetPhysicalDeviceImageFormatProperties2KHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, pImageFormatInfo, pImageFormatProperties);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties) {
    static PFN_vkGetPhysicalDeviceMemoryProperties fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceMemoryProperties)resolve("vkGetPhysicalDeviceMemoryProperties");
    if (!fp) { return; }
    fp(physicalDevice, pMemoryProperties);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2* pMemoryProperties) {
    static PFN_vkGetPhysicalDeviceMemoryProperties2 fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceMemoryProperties2)resolve("vkGetPhysicalDeviceMemoryProperties2");
    if (!fp) { return; }
    fp(physicalDevice, pMemoryProperties);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2* pMemoryProperties) {
    static PFN_vkGetPhysicalDeviceMemoryProperties2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceMemoryProperties2KHR)resolve("vkGetPhysicalDeviceMemoryProperties2KHR");
    if (!fp) { return; }
    fp(physicalDevice, pMemoryProperties);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMultisamplePropertiesEXT(VkPhysicalDevice physicalDevice, VkSampleCountFlagBits samples, VkMultisamplePropertiesEXT* pMultisampleProperties) {
    static PFN_vkGetPhysicalDeviceMultisamplePropertiesEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceMultisamplePropertiesEXT)resolve("vkGetPhysicalDeviceMultisamplePropertiesEXT");
    if (!fp) { return; }
    fp(physicalDevice, samples, pMultisampleProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceOpticalFlowImageFormatsNV(VkPhysicalDevice physicalDevice, const VkOpticalFlowImageFormatInfoNV* pOpticalFlowImageFormatInfo, uint32_t* pFormatCount, VkOpticalFlowImageFormatPropertiesNV* pImageFormatProperties) {
    static PFN_vkGetPhysicalDeviceOpticalFlowImageFormatsNV fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceOpticalFlowImageFormatsNV)resolve("vkGetPhysicalDeviceOpticalFlowImageFormatsNV");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, pOpticalFlowImageFormatInfo, pFormatCount, pImageFormatProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDevicePresentRectanglesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pRectCount, VkRect2D* pRects) {
    static PFN_vkGetPhysicalDevicePresentRectanglesKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDevicePresentRectanglesKHR)resolve("vkGetPhysicalDevicePresentRectanglesKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, surface, pRectCount, pRects);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties) {
    static PFN_vkGetPhysicalDeviceProperties fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceProperties)resolve("vkGetPhysicalDeviceProperties");
    if (!fp) { return; }
    fp(physicalDevice, pProperties);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2* pProperties) {
    static PFN_vkGetPhysicalDeviceProperties2 fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceProperties2)resolve("vkGetPhysicalDeviceProperties2");
    if (!fp) { return; }
    fp(physicalDevice, pProperties);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2* pProperties) {
    static PFN_vkGetPhysicalDeviceProperties2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceProperties2KHR)resolve("vkGetPhysicalDeviceProperties2KHR");
    if (!fp) { return; }
    fp(physicalDevice, pProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceQueueFamilyDataGraphEngineOperationPropertiesARM(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, const VkQueueFamilyDataGraphPropertiesARM* pQueueFamilyDataGraphProperties, VkBaseOutStructure* pProperties) {
    static PFN_vkGetPhysicalDeviceQueueFamilyDataGraphEngineOperationPropertiesARM fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceQueueFamilyDataGraphEngineOperationPropertiesARM)resolve("vkGetPhysicalDeviceQueueFamilyDataGraphEngineOperationPropertiesARM");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, queueFamilyIndex, pQueueFamilyDataGraphProperties, pProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceQueueFamilyDataGraphOpticalFlowImageFormatsARM(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, const VkQueueFamilyDataGraphPropertiesARM* pQueueFamilyDataGraphProperties, const VkDataGraphOpticalFlowImageFormatInfoARM* pOpticalFlowImageFormatInfo, uint32_t* pFormatCount, VkDataGraphOpticalFlowImageFormatPropertiesARM* pImageFormatProperties) {
    static PFN_vkGetPhysicalDeviceQueueFamilyDataGraphOpticalFlowImageFormatsARM fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceQueueFamilyDataGraphOpticalFlowImageFormatsARM)resolve("vkGetPhysicalDeviceQueueFamilyDataGraphOpticalFlowImageFormatsARM");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, queueFamilyIndex, pQueueFamilyDataGraphProperties, pOpticalFlowImageFormatInfo, pFormatCount, pImageFormatProperties);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyDataGraphProcessingEnginePropertiesARM(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceQueueFamilyDataGraphProcessingEngineInfoARM* pQueueFamilyDataGraphProcessingEngineInfo, VkQueueFamilyDataGraphProcessingEnginePropertiesARM* pQueueFamilyDataGraphProcessingEngineProperties) {
    static PFN_vkGetPhysicalDeviceQueueFamilyDataGraphProcessingEnginePropertiesARM fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceQueueFamilyDataGraphProcessingEnginePropertiesARM)resolve("vkGetPhysicalDeviceQueueFamilyDataGraphProcessingEnginePropertiesARM");
    if (!fp) { return; }
    fp(physicalDevice, pQueueFamilyDataGraphProcessingEngineInfo, pQueueFamilyDataGraphProcessingEngineProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceQueueFamilyDataGraphPropertiesARM(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, uint32_t* pQueueFamilyDataGraphPropertyCount, VkQueueFamilyDataGraphPropertiesARM* pQueueFamilyDataGraphProperties) {
    static PFN_vkGetPhysicalDeviceQueueFamilyDataGraphPropertiesARM fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceQueueFamilyDataGraphPropertiesARM)resolve("vkGetPhysicalDeviceQueueFamilyDataGraphPropertiesARM");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, queueFamilyIndex, pQueueFamilyDataGraphPropertyCount, pQueueFamilyDataGraphProperties);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR(VkPhysicalDevice physicalDevice, const VkQueryPoolPerformanceCreateInfoKHR* pPerformanceQueryCreateInfo, uint32_t* pNumPasses) {
    static PFN_vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR)resolve("vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR");
    if (!fp) { return; }
    fp(physicalDevice, pPerformanceQueryCreateInfo, pNumPasses);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties* pQueueFamilyProperties) {
    static PFN_vkGetPhysicalDeviceQueueFamilyProperties fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)resolve("vkGetPhysicalDeviceQueueFamilyProperties");
    if (!fp) { return; }
    fp(physicalDevice, pQueueFamilyPropertyCount, pQueueFamilyProperties);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties2* pQueueFamilyProperties) {
    static PFN_vkGetPhysicalDeviceQueueFamilyProperties2 fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceQueueFamilyProperties2)resolve("vkGetPhysicalDeviceQueueFamilyProperties2");
    if (!fp) { return; }
    fp(physicalDevice, pQueueFamilyPropertyCount, pQueueFamilyProperties);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties2KHR(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties2* pQueueFamilyProperties) {
    static PFN_vkGetPhysicalDeviceQueueFamilyProperties2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceQueueFamilyProperties2KHR)resolve("vkGetPhysicalDeviceQueueFamilyProperties2KHR");
    if (!fp) { return; }
    fp(physicalDevice, pQueueFamilyPropertyCount, pQueueFamilyProperties);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkSampleCountFlagBits samples, VkImageUsageFlags usage, VkImageTiling tiling, uint32_t* pPropertyCount, VkSparseImageFormatProperties* pProperties) {
    static PFN_vkGetPhysicalDeviceSparseImageFormatProperties fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceSparseImageFormatProperties)resolve("vkGetPhysicalDeviceSparseImageFormatProperties");
    if (!fp) { return; }
    fp(physicalDevice, format, type, samples, usage, tiling, pPropertyCount, pProperties);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceSparseImageFormatProperties2(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2* pFormatInfo, uint32_t* pPropertyCount, VkSparseImageFormatProperties2* pProperties) {
    static PFN_vkGetPhysicalDeviceSparseImageFormatProperties2 fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceSparseImageFormatProperties2)resolve("vkGetPhysicalDeviceSparseImageFormatProperties2");
    if (!fp) { return; }
    fp(physicalDevice, pFormatInfo, pPropertyCount, pProperties);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceSparseImageFormatProperties2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2* pFormatInfo, uint32_t* pPropertyCount, VkSparseImageFormatProperties2* pProperties) {
    static PFN_vkGetPhysicalDeviceSparseImageFormatProperties2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceSparseImageFormatProperties2KHR)resolve("vkGetPhysicalDeviceSparseImageFormatProperties2KHR");
    if (!fp) { return; }
    fp(physicalDevice, pFormatInfo, pPropertyCount, pProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSupportedFramebufferMixedSamplesCombinationsNV(VkPhysicalDevice physicalDevice, uint32_t* pCombinationCount, VkFramebufferMixedSamplesCombinationNV* pCombinations) {
    static PFN_vkGetPhysicalDeviceSupportedFramebufferMixedSamplesCombinationsNV fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceSupportedFramebufferMixedSamplesCombinationsNV)resolve("vkGetPhysicalDeviceSupportedFramebufferMixedSamplesCombinationsNV");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, pCombinationCount, pCombinations);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilities2EXT(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilities2EXT* pSurfaceCapabilities) {
    static PFN_vkGetPhysicalDeviceSurfaceCapabilities2EXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceSurfaceCapabilities2EXT)resolve("vkGetPhysicalDeviceSurfaceCapabilities2EXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, surface, pSurfaceCapabilities);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, VkSurfaceCapabilities2KHR* pSurfaceCapabilities) {
    static PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR)resolve("vkGetPhysicalDeviceSurfaceCapabilities2KHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, pSurfaceInfo, pSurfaceCapabilities);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* pSurfaceCapabilities) {
    static PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)resolve("vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, surface, pSurfaceCapabilities);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, uint32_t* pSurfaceFormatCount, VkSurfaceFormat2KHR* pSurfaceFormats) {
    static PFN_vkGetPhysicalDeviceSurfaceFormats2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceSurfaceFormats2KHR)resolve("vkGetPhysicalDeviceSurfaceFormats2KHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, pSurfaceInfo, pSurfaceFormatCount, pSurfaceFormats);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pSurfaceFormatCount, VkSurfaceFormatKHR* pSurfaceFormats) {
    static PFN_vkGetPhysicalDeviceSurfaceFormatsKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)resolve("vkGetPhysicalDeviceSurfaceFormatsKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, surface, pSurfaceFormatCount, pSurfaceFormats);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pPresentModeCount, VkPresentModeKHR* pPresentModes) {
    static PFN_vkGetPhysicalDeviceSurfacePresentModesKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceSurfacePresentModesKHR)resolve("vkGetPhysicalDeviceSurfacePresentModesKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, surface, pPresentModeCount, pPresentModes);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32* pSupported) {
    static PFN_vkGetPhysicalDeviceSurfaceSupportKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceSurfaceSupportKHR)resolve("vkGetPhysicalDeviceSurfaceSupportKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, queueFamilyIndex, surface, pSupported);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceToolProperties(VkPhysicalDevice physicalDevice, uint32_t* pToolCount, VkPhysicalDeviceToolProperties* pToolProperties) {
    static PFN_vkGetPhysicalDeviceToolProperties fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceToolProperties)resolve("vkGetPhysicalDeviceToolProperties");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, pToolCount, pToolProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceToolPropertiesEXT(VkPhysicalDevice physicalDevice, uint32_t* pToolCount, VkPhysicalDeviceToolProperties* pToolProperties) {
    static PFN_vkGetPhysicalDeviceToolPropertiesEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceToolPropertiesEXT)resolve("vkGetPhysicalDeviceToolPropertiesEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, pToolCount, pToolProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceVideoCapabilitiesKHR(VkPhysicalDevice physicalDevice, const VkVideoProfileInfoKHR* pVideoProfile, VkVideoCapabilitiesKHR* pCapabilities) {
    static PFN_vkGetPhysicalDeviceVideoCapabilitiesKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceVideoCapabilitiesKHR)resolve("vkGetPhysicalDeviceVideoCapabilitiesKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, pVideoProfile, pCapabilities);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceVideoEncodeQualityLevelPropertiesKHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceVideoEncodeQualityLevelInfoKHR* pQualityLevelInfo, VkVideoEncodeQualityLevelPropertiesKHR* pQualityLevelProperties) {
    static PFN_vkGetPhysicalDeviceVideoEncodeQualityLevelPropertiesKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceVideoEncodeQualityLevelPropertiesKHR)resolve("vkGetPhysicalDeviceVideoEncodeQualityLevelPropertiesKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, pQualityLevelInfo, pQualityLevelProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceVideoFormatPropertiesKHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceVideoFormatInfoKHR* pVideoFormatInfo, uint32_t* pVideoFormatPropertyCount, VkVideoFormatPropertiesKHR* pVideoFormatProperties) {
    static PFN_vkGetPhysicalDeviceVideoFormatPropertiesKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPhysicalDeviceVideoFormatPropertiesKHR)resolve("vkGetPhysicalDeviceVideoFormatPropertiesKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, pVideoFormatInfo, pVideoFormatPropertyCount, pVideoFormatProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineBinaryDataKHR(VkDevice device, const VkPipelineBinaryDataInfoKHR* pInfo, VkPipelineBinaryKeyKHR* pPipelineBinaryKey, size_t* pPipelineBinaryDataSize, void* pPipelineBinaryData) {
    static PFN_vkGetPipelineBinaryDataKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPipelineBinaryDataKHR)resolve("vkGetPipelineBinaryDataKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pInfo, pPipelineBinaryKey, pPipelineBinaryDataSize, pPipelineBinaryData);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineCacheData(VkDevice device, VkPipelineCache pipelineCache, size_t* pDataSize, void* pData) {
    static PFN_vkGetPipelineCacheData fp = nullptr;
    if (!fp) fp = (PFN_vkGetPipelineCacheData)resolve("vkGetPipelineCacheData");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pipelineCache, pDataSize, pData);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineExecutableInternalRepresentationsKHR(VkDevice device, const VkPipelineExecutableInfoKHR* pExecutableInfo, uint32_t* pInternalRepresentationCount, VkPipelineExecutableInternalRepresentationKHR* pInternalRepresentations) {
    static PFN_vkGetPipelineExecutableInternalRepresentationsKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPipelineExecutableInternalRepresentationsKHR)resolve("vkGetPipelineExecutableInternalRepresentationsKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pExecutableInfo, pInternalRepresentationCount, pInternalRepresentations);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineExecutablePropertiesKHR(VkDevice device, const VkPipelineInfoKHR* pPipelineInfo, uint32_t* pExecutableCount, VkPipelineExecutablePropertiesKHR* pProperties) {
    static PFN_vkGetPipelineExecutablePropertiesKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPipelineExecutablePropertiesKHR)resolve("vkGetPipelineExecutablePropertiesKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pPipelineInfo, pExecutableCount, pProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineExecutableStatisticsKHR(VkDevice device, const VkPipelineExecutableInfoKHR* pExecutableInfo, uint32_t* pStatisticCount, VkPipelineExecutableStatisticKHR* pStatistics) {
    static PFN_vkGetPipelineExecutableStatisticsKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPipelineExecutableStatisticsKHR)resolve("vkGetPipelineExecutableStatisticsKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pExecutableInfo, pStatisticCount, pStatistics);
}
VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetPipelineIndirectDeviceAddressNV(VkDevice device, const VkPipelineIndirectDeviceAddressInfoNV* pInfo) {
    static PFN_vkGetPipelineIndirectDeviceAddressNV fp = nullptr;
    if (!fp) fp = (PFN_vkGetPipelineIndirectDeviceAddressNV)resolve("vkGetPipelineIndirectDeviceAddressNV");
    if (!fp) { return 0; }
    return fp(device, pInfo);
}
VKAPI_ATTR void VKAPI_CALL vkGetPipelineIndirectMemoryRequirementsNV(VkDevice device, const VkComputePipelineCreateInfo* pCreateInfo, VkMemoryRequirements2* pMemoryRequirements) {
    static PFN_vkGetPipelineIndirectMemoryRequirementsNV fp = nullptr;
    if (!fp) fp = (PFN_vkGetPipelineIndirectMemoryRequirementsNV)resolve("vkGetPipelineIndirectMemoryRequirementsNV");
    if (!fp) { return; }
    fp(device, pCreateInfo, pMemoryRequirements);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineKeyKHR(VkDevice device, const VkPipelineCreateInfoKHR* pPipelineCreateInfo, VkPipelineBinaryKeyKHR* pPipelineKey) {
    static PFN_vkGetPipelineKeyKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetPipelineKeyKHR)resolve("vkGetPipelineKeyKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pPipelineCreateInfo, pPipelineKey);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelinePropertiesEXT(VkDevice device, const VkPipelineInfoKHR* pPipelineInfo, VkBaseOutStructure* pPipelineProperties) {
    static PFN_vkGetPipelinePropertiesEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetPipelinePropertiesEXT)resolve("vkGetPipelinePropertiesEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pPipelineInfo, pPipelineProperties);
}
VKAPI_ATTR void VKAPI_CALL vkGetPrivateData(VkDevice device, VkObjectType objectType, uint64_t objectHandle, VkPrivateDataSlot privateDataSlot, uint64_t* pData) {
    static PFN_vkGetPrivateData fp = nullptr;
    if (!fp) fp = (PFN_vkGetPrivateData)resolve("vkGetPrivateData");
    if (!fp) { return; }
    fp(device, objectType, objectHandle, privateDataSlot, pData);
}
VKAPI_ATTR void VKAPI_CALL vkGetPrivateDataEXT(VkDevice device, VkObjectType objectType, uint64_t objectHandle, VkPrivateDataSlot privateDataSlot, uint64_t* pData) {
    static PFN_vkGetPrivateDataEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetPrivateDataEXT)resolve("vkGetPrivateDataEXT");
    if (!fp) { return; }
    fp(device, objectType, objectHandle, privateDataSlot, pData);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetQueryPoolResults(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, size_t dataSize, void* pData, VkDeviceSize stride, VkQueryResultFlags flags) {
    static PFN_vkGetQueryPoolResults fp = nullptr;
    if (!fp) fp = (PFN_vkGetQueryPoolResults)resolve("vkGetQueryPoolResults");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, queryPool, firstQuery, queryCount, dataSize, pData, stride, flags);
}
VKAPI_ATTR void VKAPI_CALL vkGetQueueCheckpointData2NV(VkQueue queue, uint32_t* pCheckpointDataCount, VkCheckpointData2NV* pCheckpointData) {
    static PFN_vkGetQueueCheckpointData2NV fp = nullptr;
    if (!fp) fp = (PFN_vkGetQueueCheckpointData2NV)resolve("vkGetQueueCheckpointData2NV");
    if (!fp) { return; }
    fp(queue, pCheckpointDataCount, pCheckpointData);
}
VKAPI_ATTR void VKAPI_CALL vkGetQueueCheckpointDataNV(VkQueue queue, uint32_t* pCheckpointDataCount, VkCheckpointDataNV* pCheckpointData) {
    static PFN_vkGetQueueCheckpointDataNV fp = nullptr;
    if (!fp) fp = (PFN_vkGetQueueCheckpointDataNV)resolve("vkGetQueueCheckpointDataNV");
    if (!fp) { return; }
    fp(queue, pCheckpointDataCount, pCheckpointData);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetRayTracingCaptureReplayShaderGroupHandlesKHR(VkDevice device, VkPipeline pipeline, uint32_t firstGroup, uint32_t groupCount, size_t dataSize, void* pData) {
    static PFN_vkGetRayTracingCaptureReplayShaderGroupHandlesKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetRayTracingCaptureReplayShaderGroupHandlesKHR)resolve("vkGetRayTracingCaptureReplayShaderGroupHandlesKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pipeline, firstGroup, groupCount, dataSize, pData);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetRayTracingShaderGroupHandlesKHR(VkDevice device, VkPipeline pipeline, uint32_t firstGroup, uint32_t groupCount, size_t dataSize, void* pData) {
    static PFN_vkGetRayTracingShaderGroupHandlesKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetRayTracingShaderGroupHandlesKHR)resolve("vkGetRayTracingShaderGroupHandlesKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pipeline, firstGroup, groupCount, dataSize, pData);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetRayTracingShaderGroupHandlesNV(VkDevice device, VkPipeline pipeline, uint32_t firstGroup, uint32_t groupCount, size_t dataSize, void* pData) {
    static PFN_vkGetRayTracingShaderGroupHandlesNV fp = nullptr;
    if (!fp) fp = (PFN_vkGetRayTracingShaderGroupHandlesNV)resolve("vkGetRayTracingShaderGroupHandlesNV");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pipeline, firstGroup, groupCount, dataSize, pData);
}
VKAPI_ATTR VkDeviceSize VKAPI_CALL vkGetRayTracingShaderGroupStackSizeKHR(VkDevice device, VkPipeline pipeline, uint32_t group, VkShaderGroupShaderKHR groupShader) {
    static PFN_vkGetRayTracingShaderGroupStackSizeKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetRayTracingShaderGroupStackSizeKHR)resolve("vkGetRayTracingShaderGroupStackSizeKHR");
    if (!fp) { return 0; }
    return fp(device, pipeline, group, groupShader);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetRefreshCycleDurationGOOGLE(VkDevice device, VkSwapchainKHR swapchain, VkRefreshCycleDurationGOOGLE* pDisplayTimingProperties) {
    static PFN_vkGetRefreshCycleDurationGOOGLE fp = nullptr;
    if (!fp) fp = (PFN_vkGetRefreshCycleDurationGOOGLE)resolve("vkGetRefreshCycleDurationGOOGLE");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, swapchain, pDisplayTimingProperties);
}
VKAPI_ATTR void VKAPI_CALL vkGetRenderAreaGranularity(VkDevice device, VkRenderPass renderPass, VkExtent2D* pGranularity) {
    static PFN_vkGetRenderAreaGranularity fp = nullptr;
    if (!fp) fp = (PFN_vkGetRenderAreaGranularity)resolve("vkGetRenderAreaGranularity");
    if (!fp) { return; }
    fp(device, renderPass, pGranularity);
}
VKAPI_ATTR void VKAPI_CALL vkGetRenderingAreaGranularity(VkDevice device, const VkRenderingAreaInfo* pRenderingAreaInfo, VkExtent2D* pGranularity) {
    static PFN_vkGetRenderingAreaGranularity fp = nullptr;
    if (!fp) fp = (PFN_vkGetRenderingAreaGranularity)resolve("vkGetRenderingAreaGranularity");
    if (!fp) { return; }
    fp(device, pRenderingAreaInfo, pGranularity);
}
VKAPI_ATTR void VKAPI_CALL vkGetRenderingAreaGranularityKHR(VkDevice device, const VkRenderingAreaInfo* pRenderingAreaInfo, VkExtent2D* pGranularity) {
    static PFN_vkGetRenderingAreaGranularityKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetRenderingAreaGranularityKHR)resolve("vkGetRenderingAreaGranularityKHR");
    if (!fp) { return; }
    fp(device, pRenderingAreaInfo, pGranularity);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetSamplerOpaqueCaptureDescriptorDataEXT(VkDevice device, const VkSamplerCaptureDescriptorDataInfoEXT* pInfo, void* pData) {
    static PFN_vkGetSamplerOpaqueCaptureDescriptorDataEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetSamplerOpaqueCaptureDescriptorDataEXT)resolve("vkGetSamplerOpaqueCaptureDescriptorDataEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pInfo, pData);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetSemaphoreCounterValue(VkDevice device, VkSemaphore semaphore, uint64_t* pValue) {
    static PFN_vkGetSemaphoreCounterValue fp = nullptr;
    if (!fp) fp = (PFN_vkGetSemaphoreCounterValue)resolve("vkGetSemaphoreCounterValue");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, semaphore, pValue);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetSemaphoreCounterValueKHR(VkDevice device, VkSemaphore semaphore, uint64_t* pValue) {
    static PFN_vkGetSemaphoreCounterValueKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetSemaphoreCounterValueKHR)resolve("vkGetSemaphoreCounterValueKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, semaphore, pValue);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetSemaphoreFdKHR(VkDevice device, const VkSemaphoreGetFdInfoKHR* pGetFdInfo, int* pFd) {
    static PFN_vkGetSemaphoreFdKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetSemaphoreFdKHR)resolve("vkGetSemaphoreFdKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pGetFdInfo, pFd);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetShaderBinaryDataEXT(VkDevice device, VkShaderEXT shader, size_t* pDataSize, void* pData) {
    static PFN_vkGetShaderBinaryDataEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetShaderBinaryDataEXT)resolve("vkGetShaderBinaryDataEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, shader, pDataSize, pData);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetShaderInfoAMD(VkDevice device, VkPipeline pipeline, VkShaderStageFlagBits shaderStage, VkShaderInfoTypeAMD infoType, size_t* pInfoSize, void* pInfo) {
    static PFN_vkGetShaderInfoAMD fp = nullptr;
    if (!fp) fp = (PFN_vkGetShaderInfoAMD)resolve("vkGetShaderInfoAMD");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pipeline, shaderStage, infoType, pInfoSize, pInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetShaderInstrumentationValuesARM(VkDevice device, VkShaderInstrumentationARM instrumentation, uint32_t* pMetricBlockCount, void* pMetricValues, VkShaderInstrumentationValuesFlagsARM flags) {
    static PFN_vkGetShaderInstrumentationValuesARM fp = nullptr;
    if (!fp) fp = (PFN_vkGetShaderInstrumentationValuesARM)resolve("vkGetShaderInstrumentationValuesARM");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, instrumentation, pMetricBlockCount, pMetricValues, flags);
}
VKAPI_ATTR void VKAPI_CALL vkGetShaderModuleCreateInfoIdentifierEXT(VkDevice device, const VkShaderModuleCreateInfo* pCreateInfo, VkShaderModuleIdentifierEXT* pIdentifier) {
    static PFN_vkGetShaderModuleCreateInfoIdentifierEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetShaderModuleCreateInfoIdentifierEXT)resolve("vkGetShaderModuleCreateInfoIdentifierEXT");
    if (!fp) { return; }
    fp(device, pCreateInfo, pIdentifier);
}
VKAPI_ATTR void VKAPI_CALL vkGetShaderModuleIdentifierEXT(VkDevice device, VkShaderModule shaderModule, VkShaderModuleIdentifierEXT* pIdentifier) {
    static PFN_vkGetShaderModuleIdentifierEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetShaderModuleIdentifierEXT)resolve("vkGetShaderModuleIdentifierEXT");
    if (!fp) { return; }
    fp(device, shaderModule, pIdentifier);
}
VKAPI_ATTR void VKAPI_CALL vkGetSleepStatusLegacyNV(VkDevice device, VkBool32* pLowLatencyMode) {
    static PFN_vkGetSleepStatusLegacyNV fp = nullptr;
    if (!fp) fp = (PFN_vkGetSleepStatusLegacyNV)resolve("vkGetSleepStatusLegacyNV");
    if (!fp) { return; }
    fp(device, pLowLatencyMode);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainCounterEXT(VkDevice device, VkSwapchainKHR swapchain, VkSurfaceCounterFlagBitsEXT counter, uint64_t* pCounterValue) {
    static PFN_vkGetSwapchainCounterEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetSwapchainCounterEXT)resolve("vkGetSwapchainCounterEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, swapchain, counter, pCounterValue);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount, VkImage* pSwapchainImages) {
    static PFN_vkGetSwapchainImagesKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetSwapchainImagesKHR)resolve("vkGetSwapchainImagesKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, swapchain, pSwapchainImageCount, pSwapchainImages);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainStatusKHR(VkDevice device, VkSwapchainKHR swapchain) {
    static PFN_vkGetSwapchainStatusKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetSwapchainStatusKHR)resolve("vkGetSwapchainStatusKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, swapchain);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainTimeDomainPropertiesEXT(VkDevice device, VkSwapchainKHR swapchain, VkSwapchainTimeDomainPropertiesEXT* pSwapchainTimeDomainProperties, uint64_t* pTimeDomainsCounter) {
    static PFN_vkGetSwapchainTimeDomainPropertiesEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetSwapchainTimeDomainPropertiesEXT)resolve("vkGetSwapchainTimeDomainPropertiesEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, swapchain, pSwapchainTimeDomainProperties, pTimeDomainsCounter);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainTimingPropertiesEXT(VkDevice device, VkSwapchainKHR swapchain, VkSwapchainTimingPropertiesEXT* pSwapchainTimingProperties, uint64_t* pSwapchainTimingPropertiesCounter) {
    static PFN_vkGetSwapchainTimingPropertiesEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetSwapchainTimingPropertiesEXT)resolve("vkGetSwapchainTimingPropertiesEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, swapchain, pSwapchainTimingProperties, pSwapchainTimingPropertiesCounter);
}
VKAPI_ATTR void VKAPI_CALL vkGetTensorMemoryRequirementsARM(VkDevice device, const VkTensorMemoryRequirementsInfoARM* pInfo, VkMemoryRequirements2* pMemoryRequirements) {
    static PFN_vkGetTensorMemoryRequirementsARM fp = nullptr;
    if (!fp) fp = (PFN_vkGetTensorMemoryRequirementsARM)resolve("vkGetTensorMemoryRequirementsARM");
    if (!fp) { return; }
    fp(device, pInfo, pMemoryRequirements);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetTensorOpaqueCaptureDataARM(VkDevice device, uint32_t tensorCount, const VkTensorARM* pTensors, VkHostAddressRangeEXT* pDatas) {
    static PFN_vkGetTensorOpaqueCaptureDataARM fp = nullptr;
    if (!fp) fp = (PFN_vkGetTensorOpaqueCaptureDataARM)resolve("vkGetTensorOpaqueCaptureDataARM");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, tensorCount, pTensors, pDatas);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetTensorOpaqueCaptureDescriptorDataARM(VkDevice device, const VkTensorCaptureDescriptorDataInfoARM* pInfo, void* pData) {
    static PFN_vkGetTensorOpaqueCaptureDescriptorDataARM fp = nullptr;
    if (!fp) fp = (PFN_vkGetTensorOpaqueCaptureDescriptorDataARM)resolve("vkGetTensorOpaqueCaptureDescriptorDataARM");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pInfo, pData);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetTensorViewOpaqueCaptureDescriptorDataARM(VkDevice device, const VkTensorViewCaptureDescriptorDataInfoARM* pInfo, void* pData) {
    static PFN_vkGetTensorViewOpaqueCaptureDescriptorDataARM fp = nullptr;
    if (!fp) fp = (PFN_vkGetTensorViewOpaqueCaptureDescriptorDataARM)resolve("vkGetTensorViewOpaqueCaptureDescriptorDataARM");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pInfo, pData);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetValidationCacheDataEXT(VkDevice device, VkValidationCacheEXT validationCache, size_t* pDataSize, void* pData) {
    static PFN_vkGetValidationCacheDataEXT fp = nullptr;
    if (!fp) fp = (PFN_vkGetValidationCacheDataEXT)resolve("vkGetValidationCacheDataEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, validationCache, pDataSize, pData);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetVideoSessionMemoryRequirementsKHR(VkDevice device, VkVideoSessionKHR videoSession, uint32_t* pMemoryRequirementsCount, VkVideoSessionMemoryRequirementsKHR* pMemoryRequirements) {
    static PFN_vkGetVideoSessionMemoryRequirementsKHR fp = nullptr;
    if (!fp) fp = (PFN_vkGetVideoSessionMemoryRequirementsKHR)resolve("vkGetVideoSessionMemoryRequirementsKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, videoSession, pMemoryRequirementsCount, pMemoryRequirements);
}
VKAPI_ATTR VkResult VKAPI_CALL vkImportFenceFdKHR(VkDevice device, const VkImportFenceFdInfoKHR* pImportFenceFdInfo) {
    static PFN_vkImportFenceFdKHR fp = nullptr;
    if (!fp) fp = (PFN_vkImportFenceFdKHR)resolve("vkImportFenceFdKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pImportFenceFdInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkImportSemaphoreFdKHR(VkDevice device, const VkImportSemaphoreFdInfoKHR* pImportSemaphoreFdInfo) {
    static PFN_vkImportSemaphoreFdKHR fp = nullptr;
    if (!fp) fp = (PFN_vkImportSemaphoreFdKHR)resolve("vkImportSemaphoreFdKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pImportSemaphoreFdInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkInitializePerformanceApiINTEL(VkDevice device, const VkInitializePerformanceApiInfoINTEL* pInitializeInfo) {
    static PFN_vkInitializePerformanceApiINTEL fp = nullptr;
    if (!fp) fp = (PFN_vkInitializePerformanceApiINTEL)resolve("vkInitializePerformanceApiINTEL");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pInitializeInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkInvalidateMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges) {
    static PFN_vkInvalidateMappedMemoryRanges fp = nullptr;
    if (!fp) fp = (PFN_vkInvalidateMappedMemoryRanges)resolve("vkInvalidateMappedMemoryRanges");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, memoryRangeCount, pMemoryRanges);
}
VKAPI_ATTR void VKAPI_CALL vkLatencySleepLegacyNV(VkDevice device, VkSemaphore signalSemaphore, uint64_t value) {
    static PFN_vkLatencySleepLegacyNV fp = nullptr;
    if (!fp) fp = (PFN_vkLatencySleepLegacyNV)resolve("vkLatencySleepLegacyNV");
    if (!fp) { return; }
    fp(device, signalSemaphore, value);
}
VKAPI_ATTR VkResult VKAPI_CALL vkLatencySleepNV(VkDevice device, VkSwapchainKHR swapchain, const VkLatencySleepInfoNV* pSleepInfo) {
    static PFN_vkLatencySleepNV fp = nullptr;
    if (!fp) fp = (PFN_vkLatencySleepNV)resolve("vkLatencySleepNV");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, swapchain, pSleepInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** ppData) {
    static PFN_vkMapMemory fp = nullptr;
    if (!fp) fp = (PFN_vkMapMemory)resolve("vkMapMemory");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, memory, offset, size, flags, ppData);
}
VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory2(VkDevice device, const VkMemoryMapInfo* pMemoryMapInfo, void** ppData) {
    static PFN_vkMapMemory2 fp = nullptr;
    if (!fp) fp = (PFN_vkMapMemory2)resolve("vkMapMemory2");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pMemoryMapInfo, ppData);
}
VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory2KHR(VkDevice device, const VkMemoryMapInfo* pMemoryMapInfo, void** ppData) {
    static PFN_vkMapMemory2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkMapMemory2KHR)resolve("vkMapMemory2KHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pMemoryMapInfo, ppData);
}
VKAPI_ATTR VkResult VKAPI_CALL vkMergePipelineCaches(VkDevice device, VkPipelineCache dstCache, uint32_t srcCacheCount, const VkPipelineCache* pSrcCaches) {
    static PFN_vkMergePipelineCaches fp = nullptr;
    if (!fp) fp = (PFN_vkMergePipelineCaches)resolve("vkMergePipelineCaches");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, dstCache, srcCacheCount, pSrcCaches);
}
VKAPI_ATTR VkResult VKAPI_CALL vkMergeValidationCachesEXT(VkDevice device, VkValidationCacheEXT dstCache, uint32_t srcCacheCount, const VkValidationCacheEXT* pSrcCaches) {
    static PFN_vkMergeValidationCachesEXT fp = nullptr;
    if (!fp) fp = (PFN_vkMergeValidationCachesEXT)resolve("vkMergeValidationCachesEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, dstCache, srcCacheCount, pSrcCaches);
}
VKAPI_ATTR void VKAPI_CALL vkQueueBeginDebugUtilsLabelEXT(VkQueue queue, const VkDebugUtilsLabelEXT* pLabelInfo) {
    static PFN_vkQueueBeginDebugUtilsLabelEXT fp = nullptr;
    if (!fp) fp = (PFN_vkQueueBeginDebugUtilsLabelEXT)resolve("vkQueueBeginDebugUtilsLabelEXT");
    if (!fp) { return; }
    fp(queue, pLabelInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueBindSparse(VkQueue queue, uint32_t bindInfoCount, const VkBindSparseInfo* pBindInfo, VkFence fence) {
    static PFN_vkQueueBindSparse fp = nullptr;
    if (!fp) fp = (PFN_vkQueueBindSparse)resolve("vkQueueBindSparse");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(queue, bindInfoCount, pBindInfo, fence);
}
VKAPI_ATTR void VKAPI_CALL vkQueueEndDebugUtilsLabelEXT(VkQueue queue) {
    static PFN_vkQueueEndDebugUtilsLabelEXT fp = nullptr;
    if (!fp) fp = (PFN_vkQueueEndDebugUtilsLabelEXT)resolve("vkQueueEndDebugUtilsLabelEXT");
    if (!fp) { return; }
    fp(queue);
}
VKAPI_ATTR void VKAPI_CALL vkQueueInsertDebugUtilsLabelEXT(VkQueue queue, const VkDebugUtilsLabelEXT* pLabelInfo) {
    static PFN_vkQueueInsertDebugUtilsLabelEXT fp = nullptr;
    if (!fp) fp = (PFN_vkQueueInsertDebugUtilsLabelEXT)resolve("vkQueueInsertDebugUtilsLabelEXT");
    if (!fp) { return; }
    fp(queue, pLabelInfo);
}
VKAPI_ATTR void VKAPI_CALL vkQueueNotifyOutOfBandLegacyNV(VkQueue queue, uint32_t queueType) {
    static PFN_vkQueueNotifyOutOfBandLegacyNV fp = nullptr;
    if (!fp) fp = (PFN_vkQueueNotifyOutOfBandLegacyNV)resolve("vkQueueNotifyOutOfBandLegacyNV");
    if (!fp) { return; }
    fp(queue, queueType);
}
VKAPI_ATTR void VKAPI_CALL vkQueueNotifyOutOfBandNV(VkQueue queue, const VkOutOfBandQueueTypeInfoNV* pQueueTypeInfo) {
    static PFN_vkQueueNotifyOutOfBandNV fp = nullptr;
    if (!fp) fp = (PFN_vkQueueNotifyOutOfBandNV)resolve("vkQueueNotifyOutOfBandNV");
    if (!fp) { return; }
    fp(queue, pQueueTypeInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
    static PFN_vkQueuePresentKHR fp = nullptr;
    if (!fp) fp = (PFN_vkQueuePresentKHR)resolve("vkQueuePresentKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(queue, pPresentInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSetPerfHintQCOM(VkQueue queue, const VkPerfHintInfoQCOM* pPerfHintInfo) {
    static PFN_vkQueueSetPerfHintQCOM fp = nullptr;
    if (!fp) fp = (PFN_vkQueueSetPerfHintQCOM)resolve("vkQueueSetPerfHintQCOM");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(queue, pPerfHintInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSetPerformanceConfigurationINTEL(VkQueue queue, VkPerformanceConfigurationINTEL configuration) {
    static PFN_vkQueueSetPerformanceConfigurationINTEL fp = nullptr;
    if (!fp) fp = (PFN_vkQueueSetPerformanceConfigurationINTEL)resolve("vkQueueSetPerformanceConfigurationINTEL");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(queue, configuration);
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence) {
    static PFN_vkQueueSubmit fp = nullptr;
    if (!fp) fp = (PFN_vkQueueSubmit)resolve("vkQueueSubmit");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(queue, submitCount, pSubmits, fence);
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit2(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2* pSubmits, VkFence fence) {
    static PFN_vkQueueSubmit2 fp = nullptr;
    if (!fp) fp = (PFN_vkQueueSubmit2)resolve("vkQueueSubmit2");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(queue, submitCount, pSubmits, fence);
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit2KHR(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2* pSubmits, VkFence fence) {
    static PFN_vkQueueSubmit2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkQueueSubmit2KHR)resolve("vkQueueSubmit2KHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(queue, submitCount, pSubmits, fence);
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueWaitIdle(VkQueue queue) {
    static PFN_vkQueueWaitIdle fp = nullptr;
    if (!fp) fp = (PFN_vkQueueWaitIdle)resolve("vkQueueWaitIdle");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(queue);
}
VKAPI_ATTR VkResult VKAPI_CALL vkRegisterCustomBorderColorEXT(VkDevice device, const VkSamplerCustomBorderColorCreateInfoEXT* pBorderColor, VkBool32 requestIndex, uint32_t* pIndex) {
    static PFN_vkRegisterCustomBorderColorEXT fp = nullptr;
    if (!fp) fp = (PFN_vkRegisterCustomBorderColorEXT)resolve("vkRegisterCustomBorderColorEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pBorderColor, requestIndex, pIndex);
}
VKAPI_ATTR VkResult VKAPI_CALL vkRegisterDeviceEventEXT(VkDevice device, const VkDeviceEventInfoEXT* pDeviceEventInfo, const VkAllocationCallbacks* pAllocator, VkFence* pFence) {
    static PFN_vkRegisterDeviceEventEXT fp = nullptr;
    if (!fp) fp = (PFN_vkRegisterDeviceEventEXT)resolve("vkRegisterDeviceEventEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pDeviceEventInfo, pAllocator, pFence);
}
VKAPI_ATTR VkResult VKAPI_CALL vkRegisterDisplayEventEXT(VkDevice device, VkDisplayKHR display, const VkDisplayEventInfoEXT* pDisplayEventInfo, const VkAllocationCallbacks* pAllocator, VkFence* pFence) {
    static PFN_vkRegisterDisplayEventEXT fp = nullptr;
    if (!fp) fp = (PFN_vkRegisterDisplayEventEXT)resolve("vkRegisterDisplayEventEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, display, pDisplayEventInfo, pAllocator, pFence);
}
VKAPI_ATTR VkResult VKAPI_CALL vkReleaseCapturedPipelineDataKHR(VkDevice device, const VkReleaseCapturedPipelineDataInfoKHR* pInfo, const VkAllocationCallbacks* pAllocator) {
    static PFN_vkReleaseCapturedPipelineDataKHR fp = nullptr;
    if (!fp) fp = (PFN_vkReleaseCapturedPipelineDataKHR)resolve("vkReleaseCapturedPipelineDataKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pInfo, pAllocator);
}
VKAPI_ATTR VkResult VKAPI_CALL vkReleaseDisplayEXT(VkPhysicalDevice physicalDevice, VkDisplayKHR display) {
    static PFN_vkReleaseDisplayEXT fp = nullptr;
    if (!fp) fp = (PFN_vkReleaseDisplayEXT)resolve("vkReleaseDisplayEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(physicalDevice, display);
}
VKAPI_ATTR VkResult VKAPI_CALL vkReleasePerformanceConfigurationINTEL(VkDevice device, VkPerformanceConfigurationINTEL configuration) {
    static PFN_vkReleasePerformanceConfigurationINTEL fp = nullptr;
    if (!fp) fp = (PFN_vkReleasePerformanceConfigurationINTEL)resolve("vkReleasePerformanceConfigurationINTEL");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, configuration);
}
VKAPI_ATTR void VKAPI_CALL vkReleaseProfilingLockKHR(VkDevice device) {
    static PFN_vkReleaseProfilingLockKHR fp = nullptr;
    if (!fp) fp = (PFN_vkReleaseProfilingLockKHR)resolve("vkReleaseProfilingLockKHR");
    if (!fp) { return; }
    fp(device);
}
VKAPI_ATTR VkResult VKAPI_CALL vkReleaseSwapchainImagesEXT(VkDevice device, const VkReleaseSwapchainImagesInfoKHR* pReleaseInfo) {
    static PFN_vkReleaseSwapchainImagesEXT fp = nullptr;
    if (!fp) fp = (PFN_vkReleaseSwapchainImagesEXT)resolve("vkReleaseSwapchainImagesEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pReleaseInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkReleaseSwapchainImagesKHR(VkDevice device, const VkReleaseSwapchainImagesInfoKHR* pReleaseInfo) {
    static PFN_vkReleaseSwapchainImagesKHR fp = nullptr;
    if (!fp) fp = (PFN_vkReleaseSwapchainImagesKHR)resolve("vkReleaseSwapchainImagesKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pReleaseInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags) {
    static PFN_vkResetCommandBuffer fp = nullptr;
    if (!fp) fp = (PFN_vkResetCommandBuffer)resolve("vkResetCommandBuffer");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(commandBuffer, flags);
}
VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags) {
    static PFN_vkResetCommandPool fp = nullptr;
    if (!fp) fp = (PFN_vkResetCommandPool)resolve("vkResetCommandPool");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, commandPool, flags);
}
VKAPI_ATTR VkResult VKAPI_CALL vkResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags flags) {
    static PFN_vkResetDescriptorPool fp = nullptr;
    if (!fp) fp = (PFN_vkResetDescriptorPool)resolve("vkResetDescriptorPool");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, descriptorPool, flags);
}
VKAPI_ATTR VkResult VKAPI_CALL vkResetEvent(VkDevice device, VkEvent event) {
    static PFN_vkResetEvent fp = nullptr;
    if (!fp) fp = (PFN_vkResetEvent)resolve("vkResetEvent");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, event);
}
VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences) {
    static PFN_vkResetFences fp = nullptr;
    if (!fp) fp = (PFN_vkResetFences)resolve("vkResetFences");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, fenceCount, pFences);
}
VKAPI_ATTR VkResult VKAPI_CALL vkResetGpaSessionAMD(VkDevice device, VkGpaSessionAMD gpaSession) {
    static PFN_vkResetGpaSessionAMD fp = nullptr;
    if (!fp) fp = (PFN_vkResetGpaSessionAMD)resolve("vkResetGpaSessionAMD");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, gpaSession);
}
VKAPI_ATTR void VKAPI_CALL vkResetQueryPool(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount) {
    static PFN_vkResetQueryPool fp = nullptr;
    if (!fp) fp = (PFN_vkResetQueryPool)resolve("vkResetQueryPool");
    if (!fp) { return; }
    fp(device, queryPool, firstQuery, queryCount);
}
VKAPI_ATTR void VKAPI_CALL vkResetQueryPoolEXT(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount) {
    static PFN_vkResetQueryPoolEXT fp = nullptr;
    if (!fp) fp = (PFN_vkResetQueryPoolEXT)resolve("vkResetQueryPoolEXT");
    if (!fp) { return; }
    fp(device, queryPool, firstQuery, queryCount);
}
VKAPI_ATTR VkResult VKAPI_CALL vkSetDebugUtilsObjectNameEXT(VkDevice device, const VkDebugUtilsObjectNameInfoEXT* pNameInfo) {
    static PFN_vkSetDebugUtilsObjectNameEXT fp = nullptr;
    if (!fp) fp = (PFN_vkSetDebugUtilsObjectNameEXT)resolve("vkSetDebugUtilsObjectNameEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pNameInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkSetDebugUtilsObjectTagEXT(VkDevice device, const VkDebugUtilsObjectTagInfoEXT* pTagInfo) {
    static PFN_vkSetDebugUtilsObjectTagEXT fp = nullptr;
    if (!fp) fp = (PFN_vkSetDebugUtilsObjectTagEXT)resolve("vkSetDebugUtilsObjectTagEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pTagInfo);
}
VKAPI_ATTR void VKAPI_CALL vkSetDeviceMemoryPriorityEXT(VkDevice device, VkDeviceMemory memory, float priority) {
    static PFN_vkSetDeviceMemoryPriorityEXT fp = nullptr;
    if (!fp) fp = (PFN_vkSetDeviceMemoryPriorityEXT)resolve("vkSetDeviceMemoryPriorityEXT");
    if (!fp) { return; }
    fp(device, memory, priority);
}
VKAPI_ATTR VkResult VKAPI_CALL vkSetEvent(VkDevice device, VkEvent event) {
    static PFN_vkSetEvent fp = nullptr;
    if (!fp) fp = (PFN_vkSetEvent)resolve("vkSetEvent");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, event);
}
VKAPI_ATTR VkResult VKAPI_CALL vkSetGpaDeviceClockModeAMD(VkDevice device, VkGpaDeviceClockModeInfoAMD* pInfo) {
    static PFN_vkSetGpaDeviceClockModeAMD fp = nullptr;
    if (!fp) fp = (PFN_vkSetGpaDeviceClockModeAMD)resolve("vkSetGpaDeviceClockModeAMD");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pInfo);
}
VKAPI_ATTR void VKAPI_CALL vkSetHdrMetadataEXT(VkDevice device, uint32_t swapchainCount, const VkSwapchainKHR* pSwapchains, const VkHdrMetadataEXT* pMetadata) {
    static PFN_vkSetHdrMetadataEXT fp = nullptr;
    if (!fp) fp = (PFN_vkSetHdrMetadataEXT)resolve("vkSetHdrMetadataEXT");
    if (!fp) { return; }
    fp(device, swapchainCount, pSwapchains, pMetadata);
}
VKAPI_ATTR void VKAPI_CALL vkSetLatencyMarkerLegacyNV(VkDevice device, uint64_t frameID, uint32_t marker) {
    static PFN_vkSetLatencyMarkerLegacyNV fp = nullptr;
    if (!fp) fp = (PFN_vkSetLatencyMarkerLegacyNV)resolve("vkSetLatencyMarkerLegacyNV");
    if (!fp) { return; }
    fp(device, frameID, marker);
}
VKAPI_ATTR void VKAPI_CALL vkSetLatencyMarkerNV(VkDevice device, VkSwapchainKHR swapchain, const VkSetLatencyMarkerInfoNV* pLatencyMarkerInfo) {
    static PFN_vkSetLatencyMarkerNV fp = nullptr;
    if (!fp) fp = (PFN_vkSetLatencyMarkerNV)resolve("vkSetLatencyMarkerNV");
    if (!fp) { return; }
    fp(device, swapchain, pLatencyMarkerInfo);
}
VKAPI_ATTR void VKAPI_CALL vkSetLatencySleepModeLegacyNV(VkDevice device, VkBool32 lowLatencyMode, VkBool32 lowLatencyBoost, uint32_t minimumIntervalUs) {
    static PFN_vkSetLatencySleepModeLegacyNV fp = nullptr;
    if (!fp) fp = (PFN_vkSetLatencySleepModeLegacyNV)resolve("vkSetLatencySleepModeLegacyNV");
    if (!fp) { return; }
    fp(device, lowLatencyMode, lowLatencyBoost, minimumIntervalUs);
}
VKAPI_ATTR VkResult VKAPI_CALL vkSetLatencySleepModeNV(VkDevice device, VkSwapchainKHR swapchain, const VkLatencySleepModeInfoNV* pSleepModeInfo) {
    static PFN_vkSetLatencySleepModeNV fp = nullptr;
    if (!fp) fp = (PFN_vkSetLatencySleepModeNV)resolve("vkSetLatencySleepModeNV");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, swapchain, pSleepModeInfo);
}
VKAPI_ATTR void VKAPI_CALL vkSetLocalDimmingAMD(VkDevice device, VkSwapchainKHR swapChain, VkBool32 localDimmingEnable) {
    static PFN_vkSetLocalDimmingAMD fp = nullptr;
    if (!fp) fp = (PFN_vkSetLocalDimmingAMD)resolve("vkSetLocalDimmingAMD");
    if (!fp) { return; }
    fp(device, swapChain, localDimmingEnable);
}
VKAPI_ATTR VkResult VKAPI_CALL vkSetPrivateData(VkDevice device, VkObjectType objectType, uint64_t objectHandle, VkPrivateDataSlot privateDataSlot, uint64_t data) {
    static PFN_vkSetPrivateData fp = nullptr;
    if (!fp) fp = (PFN_vkSetPrivateData)resolve("vkSetPrivateData");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, objectType, objectHandle, privateDataSlot, data);
}
VKAPI_ATTR VkResult VKAPI_CALL vkSetPrivateDataEXT(VkDevice device, VkObjectType objectType, uint64_t objectHandle, VkPrivateDataSlot privateDataSlot, uint64_t data) {
    static PFN_vkSetPrivateDataEXT fp = nullptr;
    if (!fp) fp = (PFN_vkSetPrivateDataEXT)resolve("vkSetPrivateDataEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, objectType, objectHandle, privateDataSlot, data);
}
VKAPI_ATTR VkResult VKAPI_CALL vkSetSwapchainPresentTimingQueueSizeEXT(VkDevice device, VkSwapchainKHR swapchain, uint32_t size) {
    static PFN_vkSetSwapchainPresentTimingQueueSizeEXT fp = nullptr;
    if (!fp) fp = (PFN_vkSetSwapchainPresentTimingQueueSizeEXT)resolve("vkSetSwapchainPresentTimingQueueSizeEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, swapchain, size);
}
VKAPI_ATTR void VKAPI_CALL vkShutdownLatencyDeviceLegacyNV(VkDevice device) {
    static PFN_vkShutdownLatencyDeviceLegacyNV fp = nullptr;
    if (!fp) fp = (PFN_vkShutdownLatencyDeviceLegacyNV)resolve("vkShutdownLatencyDeviceLegacyNV");
    if (!fp) { return; }
    fp(device);
}
VKAPI_ATTR VkResult VKAPI_CALL vkSignalSemaphore(VkDevice device, const VkSemaphoreSignalInfo* pSignalInfo) {
    static PFN_vkSignalSemaphore fp = nullptr;
    if (!fp) fp = (PFN_vkSignalSemaphore)resolve("vkSignalSemaphore");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pSignalInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkSignalSemaphoreKHR(VkDevice device, const VkSemaphoreSignalInfo* pSignalInfo) {
    static PFN_vkSignalSemaphoreKHR fp = nullptr;
    if (!fp) fp = (PFN_vkSignalSemaphoreKHR)resolve("vkSignalSemaphoreKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pSignalInfo);
}
VKAPI_ATTR void VKAPI_CALL vkSubmitDebugUtilsMessageEXT(VkInstance instance, VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData) {
    static PFN_vkSubmitDebugUtilsMessageEXT fp = nullptr;
    if (!fp) fp = (PFN_vkSubmitDebugUtilsMessageEXT)resolve("vkSubmitDebugUtilsMessageEXT");
    if (!fp) { return; }
    fp(instance, messageSeverity, messageTypes, pCallbackData);
}
VKAPI_ATTR VkResult VKAPI_CALL vkTransitionImageLayout(VkDevice device, uint32_t transitionCount, const VkHostImageLayoutTransitionInfo* pTransitions) {
    static PFN_vkTransitionImageLayout fp = nullptr;
    if (!fp) fp = (PFN_vkTransitionImageLayout)resolve("vkTransitionImageLayout");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, transitionCount, pTransitions);
}
VKAPI_ATTR VkResult VKAPI_CALL vkTransitionImageLayoutEXT(VkDevice device, uint32_t transitionCount, const VkHostImageLayoutTransitionInfo* pTransitions) {
    static PFN_vkTransitionImageLayoutEXT fp = nullptr;
    if (!fp) fp = (PFN_vkTransitionImageLayoutEXT)resolve("vkTransitionImageLayoutEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, transitionCount, pTransitions);
}
VKAPI_ATTR void VKAPI_CALL vkTrimCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolTrimFlags flags) {
    static PFN_vkTrimCommandPool fp = nullptr;
    if (!fp) fp = (PFN_vkTrimCommandPool)resolve("vkTrimCommandPool");
    if (!fp) { return; }
    fp(device, commandPool, flags);
}
VKAPI_ATTR void VKAPI_CALL vkTrimCommandPoolKHR(VkDevice device, VkCommandPool commandPool, VkCommandPoolTrimFlags flags) {
    static PFN_vkTrimCommandPoolKHR fp = nullptr;
    if (!fp) fp = (PFN_vkTrimCommandPoolKHR)resolve("vkTrimCommandPoolKHR");
    if (!fp) { return; }
    fp(device, commandPool, flags);
}
VKAPI_ATTR void VKAPI_CALL vkUninitializePerformanceApiINTEL(VkDevice device) {
    static PFN_vkUninitializePerformanceApiINTEL fp = nullptr;
    if (!fp) fp = (PFN_vkUninitializePerformanceApiINTEL)resolve("vkUninitializePerformanceApiINTEL");
    if (!fp) { return; }
    fp(device);
}
VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice device, VkDeviceMemory memory) {
    static PFN_vkUnmapMemory fp = nullptr;
    if (!fp) fp = (PFN_vkUnmapMemory)resolve("vkUnmapMemory");
    if (!fp) { return; }
    fp(device, memory);
}
VKAPI_ATTR VkResult VKAPI_CALL vkUnmapMemory2(VkDevice device, const VkMemoryUnmapInfo* pMemoryUnmapInfo) {
    static PFN_vkUnmapMemory2 fp = nullptr;
    if (!fp) fp = (PFN_vkUnmapMemory2)resolve("vkUnmapMemory2");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pMemoryUnmapInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkUnmapMemory2KHR(VkDevice device, const VkMemoryUnmapInfo* pMemoryUnmapInfo) {
    static PFN_vkUnmapMemory2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkUnmapMemory2KHR)resolve("vkUnmapMemory2KHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pMemoryUnmapInfo);
}
VKAPI_ATTR void VKAPI_CALL vkUnregisterCustomBorderColorEXT(VkDevice device, uint32_t index) {
    static PFN_vkUnregisterCustomBorderColorEXT fp = nullptr;
    if (!fp) fp = (PFN_vkUnregisterCustomBorderColorEXT)resolve("vkUnregisterCustomBorderColorEXT");
    if (!fp) { return; }
    fp(device, index);
}
VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSetWithTemplate(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const void* pData) {
    static PFN_vkUpdateDescriptorSetWithTemplate fp = nullptr;
    if (!fp) fp = (PFN_vkUpdateDescriptorSetWithTemplate)resolve("vkUpdateDescriptorSetWithTemplate");
    if (!fp) { return; }
    fp(device, descriptorSet, descriptorUpdateTemplate, pData);
}
VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSetWithTemplateKHR(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const void* pData) {
    static PFN_vkUpdateDescriptorSetWithTemplateKHR fp = nullptr;
    if (!fp) fp = (PFN_vkUpdateDescriptorSetWithTemplateKHR)resolve("vkUpdateDescriptorSetWithTemplateKHR");
    if (!fp) { return; }
    fp(device, descriptorSet, descriptorUpdateTemplate, pData);
}
VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies) {
    static PFN_vkUpdateDescriptorSets fp = nullptr;
    if (!fp) fp = (PFN_vkUpdateDescriptorSets)resolve("vkUpdateDescriptorSets");
    if (!fp) { return; }
    fp(device, descriptorWriteCount, pDescriptorWrites, descriptorCopyCount, pDescriptorCopies);
}
VKAPI_ATTR void VKAPI_CALL vkUpdateIndirectExecutionSetPipelineEXT(VkDevice device, VkIndirectExecutionSetEXT indirectExecutionSet, uint32_t executionSetWriteCount, const VkWriteIndirectExecutionSetPipelineEXT* pExecutionSetWrites) {
    static PFN_vkUpdateIndirectExecutionSetPipelineEXT fp = nullptr;
    if (!fp) fp = (PFN_vkUpdateIndirectExecutionSetPipelineEXT)resolve("vkUpdateIndirectExecutionSetPipelineEXT");
    if (!fp) { return; }
    fp(device, indirectExecutionSet, executionSetWriteCount, pExecutionSetWrites);
}
VKAPI_ATTR void VKAPI_CALL vkUpdateIndirectExecutionSetShaderEXT(VkDevice device, VkIndirectExecutionSetEXT indirectExecutionSet, uint32_t executionSetWriteCount, const VkWriteIndirectExecutionSetShaderEXT* pExecutionSetWrites) {
    static PFN_vkUpdateIndirectExecutionSetShaderEXT fp = nullptr;
    if (!fp) fp = (PFN_vkUpdateIndirectExecutionSetShaderEXT)resolve("vkUpdateIndirectExecutionSetShaderEXT");
    if (!fp) { return; }
    fp(device, indirectExecutionSet, executionSetWriteCount, pExecutionSetWrites);
}
VKAPI_ATTR VkResult VKAPI_CALL vkUpdateVideoSessionParametersKHR(VkDevice device, VkVideoSessionParametersKHR videoSessionParameters, const VkVideoSessionParametersUpdateInfoKHR* pUpdateInfo) {
    static PFN_vkUpdateVideoSessionParametersKHR fp = nullptr;
    if (!fp) fp = (PFN_vkUpdateVideoSessionParametersKHR)resolve("vkUpdateVideoSessionParametersKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, videoSessionParameters, pUpdateInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences, VkBool32 waitAll, uint64_t timeout) {
    static PFN_vkWaitForFences fp = nullptr;
    if (!fp) fp = (PFN_vkWaitForFences)resolve("vkWaitForFences");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, fenceCount, pFences, waitAll, timeout);
}
VKAPI_ATTR VkResult VKAPI_CALL vkWaitForPresent2KHR(VkDevice device, VkSwapchainKHR swapchain, const VkPresentWait2InfoKHR* pPresentWait2Info) {
    static PFN_vkWaitForPresent2KHR fp = nullptr;
    if (!fp) fp = (PFN_vkWaitForPresent2KHR)resolve("vkWaitForPresent2KHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, swapchain, pPresentWait2Info);
}
VKAPI_ATTR VkResult VKAPI_CALL vkWaitForPresentKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t presentId, uint64_t timeout) {
    static PFN_vkWaitForPresentKHR fp = nullptr;
    if (!fp) fp = (PFN_vkWaitForPresentKHR)resolve("vkWaitForPresentKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, swapchain, presentId, timeout);
}
VKAPI_ATTR VkResult VKAPI_CALL vkWaitSemaphores(VkDevice device, const VkSemaphoreWaitInfo* pWaitInfo, uint64_t timeout) {
    static PFN_vkWaitSemaphores fp = nullptr;
    if (!fp) fp = (PFN_vkWaitSemaphores)resolve("vkWaitSemaphores");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pWaitInfo, timeout);
}
VKAPI_ATTR VkResult VKAPI_CALL vkWaitSemaphoresKHR(VkDevice device, const VkSemaphoreWaitInfo* pWaitInfo, uint64_t timeout) {
    static PFN_vkWaitSemaphoresKHR fp = nullptr;
    if (!fp) fp = (PFN_vkWaitSemaphoresKHR)resolve("vkWaitSemaphoresKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, pWaitInfo, timeout);
}
VKAPI_ATTR VkResult VKAPI_CALL vkWriteAccelerationStructuresPropertiesKHR(VkDevice device, uint32_t accelerationStructureCount, const VkAccelerationStructureKHR* pAccelerationStructures, VkQueryType queryType, size_t dataSize, void* pData, size_t stride) {
    static PFN_vkWriteAccelerationStructuresPropertiesKHR fp = nullptr;
    if (!fp) fp = (PFN_vkWriteAccelerationStructuresPropertiesKHR)resolve("vkWriteAccelerationStructuresPropertiesKHR");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, accelerationStructureCount, pAccelerationStructures, queryType, dataSize, pData, stride);
}
VKAPI_ATTR VkResult VKAPI_CALL vkWriteMicromapsPropertiesEXT(VkDevice device, uint32_t micromapCount, const VkMicromapEXT* pMicromaps, VkQueryType queryType, size_t dataSize, void* pData, size_t stride) {
    static PFN_vkWriteMicromapsPropertiesEXT fp = nullptr;
    if (!fp) fp = (PFN_vkWriteMicromapsPropertiesEXT)resolve("vkWriteMicromapsPropertiesEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, micromapCount, pMicromaps, queryType, dataSize, pData, stride);
}
VKAPI_ATTR VkResult VKAPI_CALL vkWriteResourceDescriptorsEXT(VkDevice device, uint32_t resourceCount, const VkResourceDescriptorInfoEXT* pResources, const VkHostAddressRangeEXT* pDescriptors) {
    static PFN_vkWriteResourceDescriptorsEXT fp = nullptr;
    if (!fp) fp = (PFN_vkWriteResourceDescriptorsEXT)resolve("vkWriteResourceDescriptorsEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, resourceCount, pResources, pDescriptors);
}
VKAPI_ATTR VkResult VKAPI_CALL vkWriteSamplerDescriptorsEXT(VkDevice device, uint32_t samplerCount, const VkSamplerCreateInfo* pSamplers, const VkHostAddressRangeEXT* pDescriptors) {
    static PFN_vkWriteSamplerDescriptorsEXT fp = nullptr;
    if (!fp) fp = (PFN_vkWriteSamplerDescriptorsEXT)resolve("vkWriteSamplerDescriptorsEXT");
    if (!fp) { return VK_ERROR_UNKNOWN; }
    return fp(device, samplerCount, pSamplers, pDescriptors);
}
} // extern "C"

#endif // __ANDROID__
