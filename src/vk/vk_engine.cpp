// Mithril-Wrapper Vulkan backend implementation (milestone M2-VK).
//
// The loader is discovered at runtime through dlopen + vkGetInstanceProcAddr /
// vkGetDeviceProcAddr, so libmithril never links the loader and never leaks
// vk* symbols into the export table (macOS -exported_symbols_list keeps the
// GL/EGL surface clean; on iOS the same seam loads MoltenVK).

#include <vulkan/vulkan.h>

#include <util/log.h>

#include <dlfcn.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include <spirv_cross.hpp>

#include "vk_engine.h"

namespace mithril::vk {

namespace {

// ---------------------------------------------------------------------------
// Runtime dispatch table (never link the loader; never export vk*).
// ---------------------------------------------------------------------------

#define ML_FN(name) PFN_vk##name name = nullptr

struct FnTable {
    // instance-level
    ML_FN(CreateInstance);
    ML_FN(DestroyInstance);
    ML_FN(EnumeratePhysicalDevices);
    ML_FN(GetPhysicalDeviceProperties);
    ML_FN(GetPhysicalDeviceFeatures);
    ML_FN(GetPhysicalDeviceMemoryProperties);
    ML_FN(GetPhysicalDeviceQueueFamilyProperties);
    ML_FN(CreateDevice);
    ML_FN(DestroyDevice);
    ML_FN(EnumerateDeviceExtensionProperties);
    ML_FN(GetDeviceProcAddr);
    // device-level
    ML_FN(GetDeviceQueue);
    ML_FN(DeviceWaitIdle);
    ML_FN(CreateCommandPool);
    ML_FN(DestroyCommandPool);
    ML_FN(AllocateCommandBuffers);
    ML_FN(FreeCommandBuffers);
    ML_FN(BeginCommandBuffer);
    ML_FN(EndCommandBuffer);
    ML_FN(ResetCommandBuffer);
    ML_FN(QueueSubmit);
    ML_FN(QueueWaitIdle);
    ML_FN(CreateFence);
    ML_FN(DestroyFence);
    ML_FN(WaitForFences);
    ML_FN(ResetFences);
    ML_FN(CreateRenderPass);
    ML_FN(DestroyRenderPass);
    ML_FN(CreateImageView);
    ML_FN(DestroyImageView);
    ML_FN(CreateImage);
    ML_FN(DestroyImage);
    ML_FN(AllocateMemory);
    ML_FN(FreeMemory);
    ML_FN(BindImageMemory);
    ML_FN(BindBufferMemory);
    ML_FN(CreateBuffer);
    ML_FN(DestroyBuffer);
    ML_FN(GetBufferMemoryRequirements);
    ML_FN(GetImageMemoryRequirements);
    ML_FN(MapMemory);
    ML_FN(UnmapMemory);
    ML_FN(CreateFramebuffer);
    ML_FN(DestroyFramebuffer);
    ML_FN(CreateShaderModule);
    ML_FN(DestroyShaderModule);
    ML_FN(CreateDescriptorSetLayout);
    ML_FN(DestroyDescriptorSetLayout);
    ML_FN(CreateDescriptorPool);
    ML_FN(DestroyDescriptorPool);
    ML_FN(AllocateDescriptorSets);
    ML_FN(ResetDescriptorPool);
    ML_FN(UpdateDescriptorSets);
    ML_FN(CreatePipelineLayout);
    ML_FN(DestroyPipelineLayout);
    ML_FN(CreateGraphicsPipelines);
    ML_FN(DestroyPipeline);
    ML_FN(CmdBindPipeline);
    ML_FN(CmdBindVertexBuffers);
    ML_FN(CmdBindIndexBuffer);
    ML_FN(CmdBindDescriptorSets);
    ML_FN(CmdDraw);
    ML_FN(CmdDrawIndexed);
    ML_FN(CmdBeginRenderPass);
    ML_FN(CmdEndRenderPass);
    ML_FN(CmdClearColorImage);
    ML_FN(CmdPipelineBarrier);
    ML_FN(CmdCopyImageToBuffer);
    ML_FN(CmdSetViewport);
    ML_FN(CmdSetScissor);
};

#undef ML_FN

constexpr VkDeviceSize kUboPoolSize = 1u << 20;   // 1 MiB dynamic UBO pool

// One reflected mithril_GlobalBlock member (std140 offsets from SPIR-V).
struct UboMember {
    std::string name;
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
};

struct Program {
    VkShaderModule vs_mod = VK_NULL_HANDLE;
    VkShaderModule fs_mod = VK_NULL_HANDLE;
    std::vector<UboMember> members;
    VkDeviceSize ubo_size = 0;
    bool has_ubo = false;
};

struct DrawOp {
    uint64_t program = 0;
    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkDeviceMemory vertex_mem = VK_NULL_HANDLE;
    VkBuffer instance_buffer = VK_NULL_HANDLE;
    VkDeviceMemory instance_mem = VK_NULL_HANDLE;
    VkBuffer index_buffer = VK_NULL_HANDLE;
    VkDeviceMemory index_mem = VK_NULL_HANDLE;
    uint32_t vertex_count = 0;
    uint32_t index_count = 0;
    uint32_t instance_count = 1;
    uint32_t topology = 0;         // Topology index
    uint32_t v_stride = 0;         // per-vertex record bytes
    uint32_t i_stride = 0;         // per-instance record bytes
    std::vector<VertexAttr> v_attrs;
    std::vector<VertexAttr> i_attrs;
    std::string pipeline_key;
    VkDescriptorSet desc_set = VK_NULL_HANDLE;
    VkDeviceSize ubo_offset = 0;
    VkDeviceSize ubo_range = 0;
};

struct Engine {
    void* loader = nullptr;
    FnTable fn{};
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queue_family = 0;

    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    uint32_t width = 512, height = 512;
    VkImage target_image = VK_NULL_HANDLE;
    VkDeviceMemory target_mem = VK_NULL_HANDLE;
    VkImageView target_view = VK_NULL_HANDLE;
    VkFramebuffer target_fb = VK_NULL_HANDLE;
    VkImageLayout target_layout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkRenderPass renderpass = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;

    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkDescriptorPool desc_pool = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;

    VkBuffer ubo = VK_NULL_HANDLE;
    VkDeviceMemory ubo_mem = VK_NULL_HANDLE;
    uint8_t* ubo_map = nullptr;
    VkDeviceSize ubo_next = 0;
    VkDeviceSize ubo_align = 256;

    VkBuffer readback = VK_NULL_HANDLE;
    VkDeviceMemory readback_mem = VK_NULL_HANDLE;
    uint8_t* readback_map = nullptr;

    bool initialized = false;
    bool frame_dirty = false;
    bool pending_clear = false;
    float clear_r = 0, clear_g = 0, clear_b = 0, clear_a = 0;
    float vp_x = 0, vp_y = 0, vp_w = 512, vp_h = 512;
    std::vector<DrawOp> frame_draws;
};

Engine g;
std::unordered_map<uint64_t, Program> g_programs;
std::unordered_map<std::string, VkPipeline> g_pipelines;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

VkDeviceSize AlignUp(VkDeviceSize v, VkDeviceSize a) {
    return (v + a - 1) / a * a;
}

VkResult FindMemoryType(uint32_t bits, VkMemoryPropertyFlags want,
                        uint32_t* out) {
    VkPhysicalDeviceMemoryProperties mem;
    g.fn.GetPhysicalDeviceMemoryProperties(g.physical, &mem);
    for (uint32_t i = 0; i < mem.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) &&
            (mem.memoryTypes[i].propertyFlags & want) == want) {
            *out = i;
            return VK_SUCCESS;
        }
    }
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

VkResult CreateHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                          VkBuffer* buf, VkDeviceMemory* mem) {
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (g.fn.CreateBuffer(g.device, &bi, nullptr, buf) != VK_SUCCESS)
        return VK_ERROR_INITIALIZATION_FAILED;

    VkMemoryRequirements req;
    g.fn.GetBufferMemoryRequirements(g.device, *buf, &req);
    uint32_t type = 0;
    if (FindMemoryType(req.memoryTypeBits,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       &type) != VK_SUCCESS) {
        g.fn.DestroyBuffer(g.device, *buf, nullptr);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = type;
    if (g.fn.AllocateMemory(g.device, &ai, nullptr, mem) != VK_SUCCESS) {
        g.fn.DestroyBuffer(g.device, *buf, nullptr);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    if (g.fn.BindBufferMemory(g.device, *buf, *mem, 0) != VK_SUCCESS) {
        g.fn.FreeMemory(g.device, *mem, nullptr);
        g.fn.DestroyBuffer(g.device, *buf, nullptr);
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return VK_SUCCESS;
}

VkResult CreateTargetImage(VkImage* img, VkDeviceMemory* mem) {
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = g.format;
    ii.extent = {g.width, g.height, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
               VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (g.fn.CreateImage(g.device, &ii, nullptr, img) != VK_SUCCESS)
        return VK_ERROR_INITIALIZATION_FAILED;

    VkMemoryRequirements req;
    g.fn.GetImageMemoryRequirements(g.device, *img, &req);
    uint32_t type = 0;
    if (FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                       &type) != VK_SUCCESS) {
        g.fn.DestroyImage(g.device, *img, nullptr);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = type;
    if (g.fn.AllocateMemory(g.device, &ai, nullptr, mem) != VK_SUCCESS) {
        g.fn.DestroyImage(g.device, *img, nullptr);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    if (g.fn.BindImageMemory(g.device, *img, *mem, 0) != VK_SUCCESS) {
        g.fn.FreeMemory(g.device, *mem, nullptr);
        g.fn.DestroyImage(g.device, *img, nullptr);
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return VK_SUCCESS;
}

void TransitionLayout(VkCommandBuffer cb, VkImage image,
                      VkImageLayout old_layout, VkImageLayout new_layout) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    // Coarse but correct for the milestone: everything waits on everything.
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    g.fn.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr,
                            0, nullptr, 1, &barrier);
}

// ---------------------------------------------------------------------------
// Init / teardown
// ---------------------------------------------------------------------------

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
    LOAD_DEV(CmdPipelineBarrier);
    LOAD_DEV(CmdCopyImageToBuffer);
    LOAD_DEV(CmdSetViewport);
    LOAD_DEV(CmdSetScissor);
#undef LOAD_DEV
}

bool CreateRenderPass() {
    VkAttachmentDescription att{};
    att.format = g.format;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;

    VkRenderPassCreateInfo ri{};
    ri.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ri.attachmentCount = 1;
    ri.pAttachments = &att;
    ri.subpassCount = 1;
    ri.pSubpasses = &sub;

    return g.fn.CreateRenderPass(g.device, &ri, nullptr, &g.renderpass) ==
           VK_SUCCESS;
}

bool CreateTarget() {
    VkImage img = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    if (CreateTargetImage(&img, &mem) != VK_SUCCESS) {
        ML_LOG_ERROR("vk: target image creation failed");
        return false;
    }
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = img;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = g.format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (g.fn.CreateImageView(g.device, &vi, nullptr, &g.target_view) !=
        VK_SUCCESS) {
        ML_LOG_ERROR("vk: target view creation failed");
        g.fn.DestroyImage(g.device, img, nullptr);
        g.fn.FreeMemory(g.device, mem, nullptr);
        return false;
    }
    VkFramebufferCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fi.renderPass = g.renderpass;
    fi.attachmentCount = 1;
    fi.pAttachments = &g.target_view;
    fi.width = g.width;
    fi.height = g.height;
    fi.layers = 1;
    if (g.fn.CreateFramebuffer(g.device, &fi, nullptr, &g.target_fb) !=
        VK_SUCCESS) {
        ML_LOG_ERROR("vk: target framebuffer creation failed");
        g.fn.DestroyImageView(g.device, g.target_view, nullptr);
        g.fn.DestroyImage(g.device, img, nullptr);
        g.fn.FreeMemory(g.device, mem, nullptr);
        return false;
    }
    g.target_image = img;
    g.target_mem = mem;
    g.target_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    return true;
}

// ---------------------------------------------------------------------------
// Pipelines
// ---------------------------------------------------------------------------

std::string BuildPipelineKey(uint64_t program, uint32_t topology,
                             const std::vector<VertexAttr>& v_attrs,
                             uint32_t v_stride,
                             const std::vector<VertexAttr>& i_attrs,
                             uint32_t i_stride) {
    std::string key = std::to_string(program) + "|T" + std::to_string(topology) +
                      "|V" + std::to_string(v_stride);
    for (const auto& a : v_attrs)
        key += "|" + std::to_string(a.location) + "@" +
               std::to_string(a.offset) + ":" + std::to_string(a.components);
    if (!i_attrs.empty()) {
        key += "|I" + std::to_string(i_stride);
        for (const auto& a : i_attrs)
            key += "|" + std::to_string(a.location) + "@" +
                   std::to_string(a.offset) + ":" + std::to_string(a.components);
    }
    return key;
}

VkFormat AttrFormat(uint32_t components) {
    switch (components) {
        case 1: return VK_FORMAT_R32_SFLOAT;
        case 2: return VK_FORMAT_R32G32_SFLOAT;
        case 3: return VK_FORMAT_R32G32B32_SFLOAT;
        default: return VK_FORMAT_R32G32B32A32_SFLOAT;
    }
}

VkPipeline GetOrCreatePipeline(const Program& prog, const DrawOp& op) {
    auto it = g_pipelines.find(op.pipeline_key);
    if (it != g_pipelines.end()) return it->second;

    // Binding 0: per-vertex stream; binding 1: per-instance stream.
    std::vector<VkVertexInputBindingDescription> vb;
    VkVertexInputBindingDescription v0{};
    v0.binding = 0;
    v0.stride = op.v_stride;
    v0.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    vb.push_back(v0);
    if (!op.i_attrs.empty()) {
        VkVertexInputBindingDescription v1{};
        v1.binding = 1;
        v1.stride = op.i_stride;
        v1.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
        vb.push_back(v1);
    }

    std::vector<VkVertexInputAttributeDescription> fa;
    for (const auto& a : op.v_attrs) {
        VkVertexInputAttributeDescription d{};
        d.location = a.location;
        d.binding = 0;
        d.format = AttrFormat(a.components);
        d.offset = a.offset;
        fa.push_back(d);
    }
    for (const auto& a : op.i_attrs) {
        VkVertexInputAttributeDescription d{};
        d.location = a.location;
        d.binding = 1;
        d.format = AttrFormat(a.components);
        d.offset = a.offset;
        fa.push_back(d);
    }

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = (uint32_t)vb.size();
    vi.pVertexBindingDescriptions = vb.data();
    vi.vertexAttributeDescriptionCount = (uint32_t)fa.size();
    vi.pVertexAttributeDescriptions = fa.data();

    static const VkPrimitiveTopology kTopologyMap[] = {
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
    };
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = kTopologyMap[op.topology % 3];

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cb_att{};
    cb_att.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cb_att;

    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn_s{};
    dyn_s.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn_s.dynamicStateCount = 2;
    dyn_s.pDynamicStates = dyn;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = prog.vs_mod;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = prog.fs_mod;
    stages[1].pName = "main";

    VkGraphicsPipelineCreateInfo pg{};
    pg.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pg.stageCount = 2;
    pg.pStages = stages;
    pg.pVertexInputState = &vi;
    pg.pInputAssemblyState = &ia;
    pg.pViewportState = &vp;
    pg.pRasterizationState = &rs;
    pg.pMultisampleState = &ms;
    pg.pColorBlendState = &cb;
    pg.pDynamicState = &dyn_s;
    pg.layout = g.pipeline_layout;
    pg.renderPass = g.renderpass;
    pg.subpass = 0;

    VkPipeline pipe = VK_NULL_HANDLE;
    if (g.fn.CreateGraphicsPipelines(g.device, VK_NULL_HANDLE, 1, &pg, nullptr,
                                     &pipe) != VK_SUCCESS) {
        ML_LOG_ERROR("vk: CreateGraphicsPipelines failed");
        return VK_NULL_HANDLE;
    }
    g_pipelines.emplace(op.pipeline_key, pipe);
    return pipe;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

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

    // One dynamic UBO binding shared by every pipeline.
    VkDescriptorSetLayoutBinding dslb{};
    dslb.binding = 0;
    dslb.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    dslb.descriptorCount = 1;
    dslb.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dsli{};
    dsli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsli.bindingCount = 1;
    dsli.pBindings = &dslb;
    if (g.fn.CreateDescriptorSetLayout(g.device, &dsli, nullptr,
                                       &g.set_layout) != VK_SUCCESS) {
        ML_LOG_ERROR("vk: CreateDescriptorSetLayout failed");
        return false;
    }

    VkDescriptorPoolSize dps{};
    dps.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    dps.descriptorCount = 256;
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 256;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &dps;
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
    ML_LOG_INFO("vk: engine ready (%ux%u offscreen)", g.width, g.height);
    return true;
}

bool IsInitialized() { return g.initialized; }

bool SetTargetSize(uint32_t w, uint32_t h) {
    if (!g.initialized) return false;
    if (w == g.width && h == g.height) return true;
    g.fn.DestroyFramebuffer(g.device, g.target_fb, nullptr);
    g.fn.DestroyImageView(g.device, g.target_view, nullptr);
    g.fn.DestroyImage(g.device, g.target_image, nullptr);
    g.fn.FreeMemory(g.device, g.target_mem, nullptr);
    g.width = w;
    g.height = h;
    return CreateTarget();
}

uint32_t TargetWidth() { return g.width; }
uint32_t TargetHeight() { return g.height; }

void SetClearColor(float r, float g2, float b, float a2) {
    g.clear_r = r;
    g.clear_g = g2;
    g.clear_b = b;
    g.clear_a = a2;
}

void MarkClear() {
    g.pending_clear = true;
    g.frame_dirty = true;
}

void SetViewport(float x, float y, float w, float h) {
    g.vp_x = x;
    g.vp_y = y;
    g.vp_w = w;
    g.vp_h = h;
}

uint64_t CreateProgram(const std::vector<uint32_t>& vs,
                       const std::vector<uint32_t>& fs) {
    if (!g.initialized || vs.empty() || fs.empty()) return 0;

    // Hash both modules to key the program cache.
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&h](uint32_t v) { h ^= v; h *= 1099511628211ULL; };
    for (uint32_t v : vs) mix(v);
    for (uint32_t v : fs) mix(v);
    if (g_programs.count(h)) return h;

    Program p;
    VkShaderModuleCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    sci.codeSize = vs.size() * sizeof(uint32_t);
    sci.pCode = vs.data();
    if (g.fn.CreateShaderModule(g.device, &sci, nullptr, &p.vs_mod) !=
        VK_SUCCESS)
        return 0;
    sci.codeSize = fs.size() * sizeof(uint32_t);
    sci.pCode = fs.data();
    if (g.fn.CreateShaderModule(g.device, &sci, nullptr, &p.fs_mod) !=
        VK_SUCCESS) {
        g.fn.DestroyShaderModule(g.device, p.vs_mod, nullptr);
        return 0;
    }

    // Reflect the UBO block from BOTH stages and merge members.
    try {
        auto reflect_stage = [&](const std::vector<uint32_t>& mod) {
            spirv_cross::Compiler comp(mod.data(), mod.size());
            auto res = comp.get_shader_resources();
            for (auto& ub : res.uniform_buffers) {
                const auto& t = comp.get_type(ub.base_type_id);
                for (uint32_t i = 0; i < t.member_types.size(); ++i) {
                    UboMember m;
                    m.name = comp.get_member_name(ub.base_type_id, i);
                    m.offset = comp.get_member_decoration(
                        ub.base_type_id, i, spv::DecorationOffset);
                    m.size = comp.get_declared_struct_member_size(t, i);
                    p.members.push_back(std::move(m));
                }
                p.ubo_size = std::max<uint32_t>(p.ubo_size,
                                                comp.get_declared_struct_size(t));
            }
        };
        reflect_stage(vs);
        reflect_stage(fs);
        p.has_ubo = !p.members.empty();
        if (p.has_ubo) {
            std::sort(p.members.begin(), p.members.end(),
                      [](const UboMember& a, const UboMember& b) {
                          return a.offset < b.offset;
                      });
            auto dup = std::unique(p.members.begin(), p.members.end(),
                                   [](const UboMember& a, const UboMember& b) {
                                       return a.name == b.name;
                                   });
            p.members.erase(dup, p.members.end());
            ML_LOG_DEBUG("vk: program %llu UBO %zu bytes (%zu members)",
                         (unsigned long long)h, (size_t)p.ubo_size,
                         p.members.size());
        }
    } catch (const std::exception& e) {
        ML_LOG_WARN("vk: UBO reflection failed: %s", e.what());
    }

    g_programs.emplace(h, std::move(p));
    return h;
}

void DestroyProgram(uint64_t program) {
    auto it = g_programs.find(program);
    if (it == g_programs.end()) return;
    g.fn.DestroyShaderModule(g.device, it->second.vs_mod, nullptr);
    g.fn.DestroyShaderModule(g.device, it->second.fs_mod, nullptr);
    g_programs.erase(it);
}

// ---------------------------------------------------------------------------
// Draw path
// ---------------------------------------------------------------------------

namespace {

// Create a host-visible staging buffer of `size` bytes and copy `data` in.
bool StageBytes(const void* data, VkDeviceSize size, VkBufferUsageFlags usage,
                VkBuffer* buf, VkDeviceMemory* mem) {
    if (CreateHostBuffer(size, usage, buf, mem) != VK_SUCCESS) {
        ML_LOG_ERROR("vk: draw staging allocation failed");
        return false;
    }
    void* map = nullptr;
    if (g.fn.MapMemory(g.device, *mem, 0, VK_WHOLE_SIZE, 0, &map) ==
        VK_SUCCESS) {
        std::memcpy(map, data, (size_t)size);
        g.fn.UnmapMemory(g.device, *mem);
    }
    return true;
}

// Stage a float32 stream into buf/mem (no-op for an empty stream).
bool StageStream(const VertexStream& stream, VkBuffer* buf,
                 VkDeviceMemory* mem) {
    if (stream.data.empty() || stream.stride == 0) return true;
    return StageBytes(stream.data.data(), stream.data.size() * sizeof(float),
                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, buf, mem);
}

} // namespace

void Draw(const DrawParams& params) {
    if (!g.initialized) return;
    auto prog_it = g_programs.find(params.program);
    if (prog_it == g_programs.end()) return;
    const Program& prog = prog_it->second;
    if (params.vertex_stream.data.empty()) return;

    DrawOp op;
    op.program = params.program;
    op.topology = (uint32_t)params.topology;
    op.v_stride = params.vertex_stream.stride;
    op.v_attrs = params.vertex_stream.attrs;
    op.i_stride = params.instance_stream.stride;
    op.i_attrs = params.instance_stream.attrs;
    op.instance_count = std::max<uint32_t>(params.instance_count, 1);
    op.vertex_count =
        (uint32_t)(params.vertex_stream.data.size() * sizeof(float) /
                   op.v_stride);
    op.index_count = (uint32_t)params.indices.size();
    op.pipeline_key =
        BuildPipelineKey(params.program, op.topology, op.v_attrs, op.v_stride,
                         op.i_attrs, op.i_stride);

    if (!StageStream(params.vertex_stream, &op.vertex_buffer,
                     &op.vertex_mem))
        return;
    if (!op.i_attrs.empty() &&
        !StageStream(params.instance_stream, &op.instance_buffer,
                     &op.instance_mem)) {
        g.fn.DestroyBuffer(g.device, op.vertex_buffer, nullptr);
        g.fn.FreeMemory(g.device, op.vertex_mem, nullptr);
        return;
    }
    if (op.index_count &&
        !StageBytes(params.indices.data(), op.index_count * sizeof(uint32_t),
                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &op.index_buffer,
                    &op.index_mem)) {
        g.fn.DestroyBuffer(g.device, op.vertex_buffer, nullptr);
        g.fn.FreeMemory(g.device, op.vertex_mem, nullptr);
        if (op.instance_buffer) {
            g.fn.DestroyBuffer(g.device, op.instance_buffer, nullptr);
            g.fn.FreeMemory(g.device, op.instance_mem, nullptr);
        }
        return;
    }

    // Compose the UBO from the reflected members + current uniform values.
    VkDeviceSize range = prog.has_ubo ? prog.ubo_size : 16;
    if (g.ubo_next + range > kUboPoolSize) {
        ML_LOG_WARN("vk: dynamic UBO exhausted; flushing and resetting");
        SubmitFlush();
    }
    op.ubo_offset = AlignUp(g.ubo_next, 16);
    op.ubo_range = range;
    g.ubo_next = op.ubo_offset + range;
    if (prog.has_ubo) {
        std::vector<uint8_t> bytes((size_t)prog.ubo_size, 0);
        for (const auto& m : prog.members) {
            auto it = params.uniforms.find(m.name);
            if (it == params.uniforms.end()) continue;
            size_t n = std::min<size_t>(m.size, it->second.size() * sizeof(float));
            std::memcpy(bytes.data() + m.offset, it->second.data(), n);
        }
        std::memcpy(g.ubo_map + op.ubo_offset, bytes.data(), bytes.size());
    } else {
        std::memset(g.ubo_map + op.ubo_offset, 0, 16);
    }

    // Descriptor for this draw.
    VkDescriptorSetAllocateInfo dsa{};
    dsa.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsa.descriptorPool = g.desc_pool;
    dsa.descriptorSetCount = 1;
    dsa.pSetLayouts = &g.set_layout;
    if (g.fn.AllocateDescriptorSets(g.device, &dsa, &op.desc_set) !=
        VK_SUCCESS) {
        ML_LOG_ERROR("vk: AllocateDescriptorSets failed");
        g.fn.DestroyBuffer(g.device, op.vertex_buffer, nullptr);
        g.fn.FreeMemory(g.device, op.vertex_mem, nullptr);
        if (op.instance_buffer) {
            g.fn.DestroyBuffer(g.device, op.instance_buffer, nullptr);
            g.fn.FreeMemory(g.device, op.instance_mem, nullptr);
        }
        if (op.index_buffer) {
            g.fn.DestroyBuffer(g.device, op.index_buffer, nullptr);
            g.fn.FreeMemory(g.device, op.index_mem, nullptr);
        }
        return;
    }
    VkDescriptorBufferInfo dbi{};
    dbi.buffer = g.ubo;
    dbi.range = op.ubo_range;
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = op.desc_set;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    w.pBufferInfo = &dbi;
    g.fn.UpdateDescriptorSets(g.device, 1, &w, 0, nullptr);

    g.frame_draws.push_back(std::move(op));
    g.frame_dirty = true;
}

void SubmitFlush() {
    if (!g.initialized || !g.frame_dirty) return;

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    g.fn.ResetCommandBuffer(g.cmd, 0);
    g.fn.BeginCommandBuffer(g.cmd, &bi);

    // Optional explicit clear.
    if (g.pending_clear) {
        if (g.target_layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            TransitionLayout(g.cmd, g.target_image, g.target_layout,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            g.target_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        }
        VkClearColorValue c{};
        c.float32[0] = g.clear_r;
        c.float32[1] = g.clear_g;
        c.float32[2] = g.clear_b;
        c.float32[3] = g.clear_a;
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        g.fn.CmdClearColorImage(g.cmd, g.target_image,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &c, 1,
                                &range);
    }
    if (g.target_layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        TransitionLayout(g.cmd, g.target_image, g.target_layout,
                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        g.target_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    if (!g.frame_draws.empty()) {
        VkRenderPassBeginInfo rbi{};
        rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rbi.renderPass = g.renderpass;
        rbi.framebuffer = g.target_fb;
        rbi.renderArea = {0, 0, g.width, g.height};
        g.fn.CmdBeginRenderPass(g.cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport vp{};
        vp.x = g.vp_x;
        vp.y = g.vp_y;
        vp.width = std::min<float>(g.vp_w, g.width);
        vp.height = std::min<float>(g.vp_h, g.height);
        vp.minDepth = 0.f;
        vp.maxDepth = 1.f;
        VkRect2D sc{};
        sc.extent = {g.width, g.height};
        g.fn.CmdSetViewport(g.cmd, 0, 1, &vp);
        g.fn.CmdSetScissor(g.cmd, 0, 1, &sc);

        for (const auto& op : g.frame_draws) {
            VkPipeline pipe =
                GetOrCreatePipeline(g_programs.at(op.program), op);
            if (pipe == VK_NULL_HANDLE) continue;
            g.fn.CmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);

            const VkBuffer binds[2] = {op.vertex_buffer, op.instance_buffer};
            const VkDeviceSize zeros[2] = {0, 0};
            uint32_t nb = op.instance_buffer ? 2 : 1;
            g.fn.CmdBindVertexBuffers(g.cmd, 0, nb, binds, zeros);

            if (op.index_count) {
                g.fn.CmdBindIndexBuffer(g.cmd, op.index_buffer, 0,
                                        VK_INDEX_TYPE_UINT32);
            }
            uint32_t dyn = (uint32_t)op.ubo_offset;
            g.fn.CmdBindDescriptorSets(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       g.pipeline_layout, 0, 1, &op.desc_set,
                                       1, &dyn);
            if (op.index_count) {
                g.fn.CmdDrawIndexed(g.cmd, op.index_count, op.instance_count,
                                    0, 0, 0);
            } else {
                g.fn.CmdDraw(g.cmd, op.vertex_count, op.instance_count, 0, 0);
            }
        }
        g.fn.CmdEndRenderPass(g.cmd);
    }

    // Copy the finished frame to the readback buffer (final layout).
    VkBufferImageCopy bic{};
    bic.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    bic.imageExtent = {g.width, g.height, 1};
    g.fn.CmdCopyImageToBuffer(g.cmd, g.target_image,
                              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g.readback,
                              1, &bic);
    TransitionLayout(g.cmd, g.target_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    g.target_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    g.fn.EndCommandBuffer(g.cmd);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &g.cmd;
    if (g.fn.QueueSubmit(g.queue, 1, &si, g.fence) != VK_SUCCESS) {
        ML_LOG_ERROR("vk: QueueSubmit failed");
        return;
    }
    g.fn.WaitForFences(g.device, 1, &g.fence, VK_TRUE, UINT64_MAX);
    g.fn.ResetFences(g.device, 1, &g.fence);

    for (auto& op : g.frame_draws) {
        g.fn.DestroyBuffer(g.device, op.vertex_buffer, nullptr);
        g.fn.FreeMemory(g.device, op.vertex_mem, nullptr);
        if (op.instance_buffer) {
            g.fn.DestroyBuffer(g.device, op.instance_buffer, nullptr);
            g.fn.FreeMemory(g.device, op.instance_mem, nullptr);
        }
        if (op.index_buffer) {
            g.fn.DestroyBuffer(g.device, op.index_buffer, nullptr);
            g.fn.FreeMemory(g.device, op.index_mem, nullptr);
        }
    }
    g.fn.ResetDescriptorPool(g.device, g.desc_pool, 0);
    g.ubo_next = 0;
    g.pending_clear = false;
    g.frame_draws.clear();
    g.frame_dirty = false;
}

void ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, void* out) {
    if (!g.initialized || !g.readback_map) return;
    x = std::max<GLint>(0, x);
    y = std::max<GLint>(0, y);
    width = std::min<GLsizei>(width, (GLsizei)g.width - x);
    height = std::min<GLsizei>(height, (GLsizei)g.height - y);
    if (width <= 0 || height <= 0) return;
    // Rows are copied upside-down to match GL's bottom-left framebuffer
    // origin (the Vulkan framebuffer has +Y down).
    for (GLsizei row = 0; row < height; ++row) {
        std::memcpy(reinterpret_cast<uint8_t*>(out) + (size_t)row * width * 4,
                    g.readback_map +
                        ((size_t)(g.height - 1 - (y + row)) * g.width + x) * 4,
                    (size_t)width * 4);
    }
}

} // namespace mithril::vk