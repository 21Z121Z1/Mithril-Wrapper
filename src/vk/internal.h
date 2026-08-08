// Mithril-Wrapper Vulkan backend internal shared state (split across
// dispatch.cpp / target.cpp / pipeline.cpp / draw.cpp and the
// thin public engine.cpp). Holds the runtime dispatch table, the engine
// globals, and the cross-TU helper declarations.
//
// The loader is discovered at runtime through dlopen + vkGetInstanceProcAddr /
// vkGetDeviceProcAddr, so libmithril never links the loader and never leaks
// vk* symbols into the export table (macOS -exported_symbols_list keeps the
// GL/EGL surface clean; on iOS the same seam loads MoltenVK).

#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <util/log.h>

#include "engine.h"

namespace mithril::vk {

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
    ML_FN(CreateSampler);
    ML_FN(DestroySampler);
    ML_FN(CmdCopyBufferToImage);
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
    // Sampler uniforms (descriptor binding mirrors the GLSL layout() we
    // inject in assign_sampler_bindings: binding = program listing).
    struct SamplerBind {
        std::string name;
        uint32_t binding = 0;   // descriptor binding (index into set 0)
    };
    std::vector<SamplerBind> samplers;
};

// Resident GPU texture (uploaded via UploadTexture).
struct TexObj {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    uint32_t levels = 1;
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
    // Sampler descriptor images for this draw (one per samper bound).
    std::vector<std::pair<uint32_t, VkDescriptorImageInfo>> tex_binds;
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

    // M4 textures: gl texture id -> resident GPU image.
    std::unordered_map<uint64_t, TexObj> textures;
    TexObj dummy_tex;             // 1x1 white fallback for unbound units

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

// Engine + program/pipeline caches (storage lives in engine.cpp).
extern Engine g;
extern std::unordered_map<uint64_t, Program> g_programs;
extern std::unordered_map<std::string, VkPipeline> g_pipelines;

// ---- shared helpers (defined in target.cpp) ------------------------------

VkDeviceSize AlignUp(VkDeviceSize v, VkDeviceSize a);

VkResult FindMemoryType(uint32_t bits, VkMemoryPropertyFlags want,
                        uint32_t* out);

VkResult CreateHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                          VkBuffer* buf, VkDeviceMemory* mem);

VkResult CreateTargetImage(VkImage* img, VkDeviceMemory* mem);

void TransitionLayout(VkCommandBuffer cb, VkImage image,
                      VkImageLayout old_layout, VkImageLayout new_layout);

bool CreateRenderPass();

bool CreateTarget();

void CreateDummyTexture();

// ---- texture helpers (defined in texture.cpp) ----------------------------

TexObj* GetTexObj(uint64_t gl_id);

// ---- pipeline helpers (defined in pipeline.cpp) --------------------------

std::string BuildPipelineKey(uint64_t program, uint32_t topology,
                             const std::vector<VertexAttr>& v_attrs,
                             uint32_t v_stride,
                             const std::vector<VertexAttr>& i_attrs,
                             uint32_t i_stride);

VkFormat AttrFormat(uint32_t components);

VkPipeline GetOrCreatePipeline(const Program& prog, const DrawOp& op);

} // namespace mithril::vk