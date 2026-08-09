// Mithril-Wrapper Vulkan backend -- loader/instance dispatch (M2-VK).
// dlopens the Vulkan loader, resolves the instance/device function
// tables against the live handles, and owns EnsureInit: the one-time
// pipeline setup composed from the target.cpp helpers.

#include "internal.h"

#include <dlfcn.h>

#include <cstring>
#include <vector>

namespace mithril::vk {

void LoadDeviceFunctions() {
#define LOAD_DEV(NAME)                                                        \
    g.fn.NAME = reinterpret_cast<PFN_vk##NAME>(                              \
        g.fn.GetDeviceProcAddr(g.device, "vk" #NAME))
    LOAD_DEV(GetDeviceQueue);
    LOAD_DEV(DeviceWaitIdle);
    LOAD_DEV(CreateCommandPool);
    LOAD_DEV(DestroyCommandPool);
    LOAD_DEV(AllocateCommandBuffers);
    LOAD_DEV(FreeCommandBuffers);
    LOAD_DEV(BeginCommandBuffer);
    LOAD_DEV(EndCommandBuffer);
    LOAD_DEV(ResetCommandBuffer);
    LOAD_DEV(QueueSubmit);
    LOAD_DEV(QueueWaitIdle);
    LOAD_DEV(CreateFence);
    LOAD_DEV(DestroyFence);
    LOAD_DEV(WaitForFences);
    LOAD_DEV(ResetFences);
    LOAD_DEV(CreateRenderPass);
    LOAD_DEV(DestroyRenderPass);
    LOAD_DEV(CreateImageView);
    LOAD_DEV(DestroyImageView);
    LOAD_DEV(CreateImage);
    LOAD_DEV(DestroyImage);
    LOAD_DEV(AllocateMemory);
    LOAD_DEV(FreeMemory);
    LOAD_DEV(BindImageMemory);
    LOAD_DEV(BindBufferMemory);
    LOAD_DEV(CreateBuffer);
    LOAD_DEV(DestroyBuffer);
    LOAD_DEV(GetBufferMemoryRequirements);
    LOAD_DEV(GetImageMemoryRequirements);
    LOAD_DEV(MapMemory);
    LOAD_DEV(UnmapMemory);
    LOAD_DEV(CreateFramebuffer);
    LOAD_DEV(DestroyFramebuffer);
    LOAD_DEV(CreateShaderModule);
    LOAD_DEV(DestroyShaderModule);
    LOAD_DEV(CreateDescriptorSetLayout);
    LOAD_DEV(DestroyDescriptorSetLayout);
    LOAD_DEV(CreateDescriptorPool);
    LOAD_DEV(DestroyDescriptorPool);
    LOAD_DEV(AllocateDescriptorSets);
    LOAD_DEV(ResetDescriptorPool);
    LOAD_DEV(UpdateDescriptorSets);
    LOAD_DEV(CreatePipelineLayout);
    LOAD_DEV(DestroyPipelineLayout);
    LOAD_DEV(CreateGraphicsPipelines);
    LOAD_DEV(DestroyPipeline);
    LOAD_DEV(CmdBindPipeline);
    LOAD_DEV(CmdBindVertexBuffers);
    LOAD_DEV(CmdBindIndexBuffer);
    LOAD_DEV(CmdBindDescriptorSets);
    LOAD_DEV(CmdDraw);
    LOAD_DEV(CmdDrawIndexed);
    LOAD_DEV(CmdBeginRenderPass);
    LOAD_DEV(CmdEndRenderPass);
    LOAD_DEV(CmdClearColorImage);
    LOAD_DEV(CmdClearDepthStencilImage);
    LOAD_DEV(CmdPipelineBarrier);
    LOAD_DEV(CmdCopyImageToBuffer);
    LOAD_DEV(CmdCopyBufferToImage);
    LOAD_DEV(CmdCopyImage);
    LOAD_DEV(CmdBlitImage);
    LOAD_DEV(CmdResolveImage);
    LOAD_DEV(CreateSampler);
    LOAD_DEV(DestroySampler);
    LOAD_DEV(CmdSetViewport);
    LOAD_DEV(CmdSetScissor);
#undef LOAD_DEV
}

bool EnsureInit() {
    if (g.initialized) return true;

    static const char* kLoaders[] = {
        "libvulkan.so.1", "libvulkan.so", "libMoltenVK.dylib",
    };
    for (const char* name : kLoaders) {
        g.loader = dlopen(name, RTLD_NOW | RTLD_LOCAL);
        if (g.loader) break;
    }
    if (!g.loader) {
        ML_LOG_WARN("vk: no Vulkan loader found -- GL stays validation-only");
        return false;
    }

    auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        dlsym(g.loader, "vkGetInstanceProcAddr"));
    auto gdpa = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        dlsym(g.loader, "vkGetDeviceProcAddr"));
    auto create_inst = reinterpret_cast<PFN_vkCreateInstance>(
        dlsym(g.loader, "vkCreateInstance"));
    if (!gipa || !gdpa || !create_inst) {
        ML_LOG_ERROR("vk: loader missing core entry points");
        dlclose(g.loader);
        g.loader = nullptr;
        return false;
    }
    g.fn.CreateInstance = create_inst;
    g.fn.GetDeviceProcAddr = gdpa;

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "Mithril-Wrapper";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    if (g.fn.CreateInstance(&ici, nullptr, &g.instance) != VK_SUCCESS) {
        ML_LOG_ERROR("vk: vkCreateInstance failed");
        dlclose(g.loader);
        g.loader = nullptr;
        return false;
    }

    // Resolve instance-level functions against the live instance (global
    // GIPA only guarantees global commands).
#define LOAD_INST(NAME)                                                       \
    g.fn.NAME = reinterpret_cast<PFN_vk##NAME>(gipa(g.instance, "vk" #NAME))
    LOAD_INST(DestroyInstance);
    LOAD_INST(EnumeratePhysicalDevices);
    LOAD_INST(GetPhysicalDeviceProperties);
    LOAD_INST(GetPhysicalDeviceFeatures);
    LOAD_INST(GetPhysicalDeviceMemoryProperties);
    LOAD_INST(GetPhysicalDeviceQueueFamilyProperties);
    LOAD_INST(CreateDevice);
    LOAD_INST(EnumerateDeviceExtensionProperties);
#undef LOAD_INST

    uint32_t n = 0;
    if (g.fn.EnumeratePhysicalDevices(g.instance, &n, nullptr) != VK_SUCCESS ||
        n == 0) {
        ML_LOG_ERROR("vk: no physical device");
        return false;
    }
    std::vector<VkPhysicalDevice> devs(n);
    g.fn.EnumeratePhysicalDevices(g.instance, &n, devs.data());
    g.physical = devs[0];

    VkPhysicalDeviceProperties props;
    g.fn.GetPhysicalDeviceProperties(g.physical, &props);
    ML_LOG_INFO("vk: physical device: %s", props.deviceName);
    if (props.limits.minUniformBufferOffsetAlignment > 16)
        g.ubo_align = props.limits.minUniformBufferOffsetAlignment;

    uint32_t n_fam = 0;
    g.fn.GetPhysicalDeviceQueueFamilyProperties(g.physical, &n_fam, nullptr);
    std::vector<VkQueueFamilyProperties> fam(n_fam);
    g.fn.GetPhysicalDeviceQueueFamilyProperties(g.physical, &n_fam, fam.data());
    bool have_graphics = false;
    for (uint32_t i = 0; i < n_fam; ++i) {
        if (fam[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            g.queue_family = i;
            have_graphics = true;
            break;
        }
    }
    if (!have_graphics) {
        ML_LOG_ERROR("vk: no graphics queue family");
        return false;
    }

    float prio = 1.0f;
    VkDeviceQueueCreateInfo dqc{};
    dqc.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    dqc.queueFamilyIndex = g.queue_family;
    dqc.queueCount = 1;
    dqc.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &dqc;
    if (g.fn.CreateDevice(g.physical, &dci, nullptr, &g.device) != VK_SUCCESS) {
        ML_LOG_ERROR("vk: vkCreateDevice failed");
        return false;
    }
    LoadDeviceFunctions();
    g.fn.GetDeviceQueue(g.device, g.queue_family, 0, &g.queue);

    // Dynamic UBO pool (host visible).
    if (CreateHostBuffer(kUboPoolSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         &g.ubo, &g.ubo_mem) != VK_SUCCESS ||
        g.fn.MapMemory(g.device, g.ubo_mem, 0, VK_WHOLE_SIZE, 0,
                       reinterpret_cast<void**>(&g.ubo_map)) != VK_SUCCESS) {
        ML_LOG_ERROR("vk: dynamic UBO pool allocation failed");
        return false;
    }

    // One dynamic UBO binding (0) + one combined image sampler per GL unit
    // (bindings 1..kMaxUnits) shared by every pipeline.
    std::array<VkDescriptorSetLayoutBinding, 1 + kMaxUnits> dslb{};
    dslb[0].binding = 0;
    dslb[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    dslb[0].descriptorCount = 1;
    dslb[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    for (uint32_t i = 1; i <= kMaxUnits; ++i) {
        dslb[i].binding = i;
        dslb[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        dslb[i].descriptorCount = 1;
        dslb[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dsli{};
    dsli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsli.bindingCount = dslb.size();
    dsli.pBindings = dslb.data();
    if (g.fn.CreateDescriptorSetLayout(g.device, &dsli, nullptr,
                                       &g.set_layout) != VK_SUCCESS) {
        ML_LOG_ERROR("vk: CreateDescriptorSetLayout failed");
        return false;
    }

    std::array<VkDescriptorPoolSize, 2> dps{};
    dps[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    dps[0].descriptorCount = 256;
    dps[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    dps[1].descriptorCount = 256 * kMaxUnits;
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 256;
    dpci.poolSizeCount = dps.size();
    dpci.pPoolSizes = dps.data();
    if (g.fn.CreateDescriptorPool(g.device, &dpci, nullptr, &g.desc_pool) !=
        VK_SUCCESS) {
        ML_LOG_ERROR("vk: CreateDescriptorPool failed");
        return false;
    }

    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &g.set_layout;
    if (g.fn.CreatePipelineLayout(g.device, &plci, nullptr,
                                  &g.pipeline_layout) != VK_SUCCESS) {
        ML_LOG_ERROR("vk: CreatePipelineLayout failed");
        return false;
    }

    VkCommandPoolCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = g.queue_family;
    if (g.fn.CreateCommandPool(g.device, &cpci, nullptr, &g.pool) != VK_SUCCESS)
        return false;
    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = g.pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    if (g.fn.AllocateCommandBuffers(g.device, &cbai, &g.cmd) != VK_SUCCESS)
        return false;
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (g.fn.CreateFence(g.device, &fci, nullptr, &g.fence) != VK_SUCCESS)
        return false;

    if (!CreateRenderPass()) {
        ML_LOG_ERROR("vk: CreateRenderPass failed");
        return false;
    }
    if (!CreateTarget()) {
        ML_LOG_ERROR("vk: target creation failed");
        return false;
    }

    // Bring the fresh image to COLOR_ATTACHMENT_OPTIMAL.
    {
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (g.fn.BeginCommandBuffer(g.cmd, &bi) == VK_SUCCESS) {
            TransitionLayout(g.cmd, g.target_image, VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            g.fn.EndCommandBuffer(g.cmd);
            VkSubmitInfo si{};
            si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.commandBufferCount = 1;
            si.pCommandBuffers = &g.cmd;
            g.fn.QueueSubmit(g.queue, 1, &si, g.fence);
            g.fn.WaitForFences(g.device, 1, &g.fence, VK_TRUE, UINT64_MAX);
            g.fn.ResetFences(g.device, 1, &g.fence);
            g.target_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
    }

    if (CreateHostBuffer(g.width * g.height * 4,
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT, &g.readback,
                         &g.readback_mem) != VK_SUCCESS ||
        g.fn.MapMemory(g.device, g.readback_mem, 0, VK_WHOLE_SIZE, 0,
                       reinterpret_cast<void**>(&g.readback_map)) !=
            VK_SUCCESS) {
        ML_LOG_ERROR("vk: readback staging allocation failed");
        return false;
    }

    g.initialized = true;

    // The 1x1 white fallback image; now that initialization is complete the
    // texture path (which early-outs on !g.initialized) can upload it.
    CreateDummyTexture();

    ML_LOG_INFO("vk: engine ready (%ux%u offscreen)", g.width, g.height);
    return true;
}
} // namespace mithril::vk
