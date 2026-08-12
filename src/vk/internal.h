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
    ML_FN(CmdClearDepthStencilImage);
    ML_FN(CmdClearAttachments);
    ML_FN(CmdPipelineBarrier);
    ML_FN(CmdCopyImageToBuffer);
    ML_FN(CmdCopyImage);
    ML_FN(CmdBlitImage);
    ML_FN(CmdResolveImage);
    ML_FN(CmdSetViewport);
    ML_FN(CmdSetScissor);
};

#undef ML_FN

constexpr VkDeviceSize kUboPoolSize = 1u << 20;   // 1 MiB dynamic UBO pool

// One reflected mithril_GlobalBlock member (std140 offsets/strides from SPIR-V).
using UboMember = backend::UniformMemberLayout;

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
    uint32_t levels = 1;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;
    bool is_3d = false;
};

struct SamplerCacheEntry {
    VkSampler sampler = VK_NULL_HANDLE;
    uint64_t last_use = 0;
};

// S5: renderbuffer object (glRenderbufferStorage). Backed by a device-local
// image that the FBO attachment views reference.
struct RbObj {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t samples = 1;
};

// S5: one live render attachment for an FBO. Either a texture (tex_id, the
// resident TexObj image is referenced) or a renderbuffer (rbo_id into
// g.renderbuffers). `level`/`layer` select the mip/array slice for textures.
struct FboSlot {
    bool is_texture = false;
    uint64_t tex_id = 0;
    uint64_t rbo_id = 0;
    uint32_t level = 0;
    uint32_t layer = 0;

    FboSlot() = default;
    FboSlot(const FboAttach& a)
        : is_texture(a.is_texture), tex_id(a.tex_id), rbo_id(a.rbo_id),
          level(a.level), layer(a.layer) {}
};

// S5: a configured (GL) framebuffer object, keyed by gl id. The Vk
// framebuffer + render pass are rebuilt lazily before each draw flush so a
// texture re-upload pops a fresh image view automatically. MRT supports
// GL_COLOR_ATTACHMENT0..kMaxColorAtt (the render pass + framebuffer expose one
// colour attachment per attached colour image). MSAA: a multisampled color
// attachment gets a single-sample resolve image (colour attachment + resolve)
// so readback/blit see the resolved result.
constexpr uint32_t kMaxColorAtt = 8;
struct FboObj {
    std::vector<FboSlot> colors;         // one per color attachment slot
    std::vector<VkImageView> color_view; // views for the framebuffer
    std::vector<VkImageView> resolve_view;// single-sample resolve views (MSAA)
    std::vector<bool> color_msaa;        // per-slot multisampled flag
    bool has_depth = false;
    FboSlot depth;                 // depth/stencil attachment
    VkFormat color_fmt = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormat depth_fmt = VK_FORMAT_D24_UNORM_S8_UINT;
    uint32_t samples = 1;
    uint32_t width = 0, height = 0;
    VkImageView depth_view = VK_NULL_HANDLE;
    VkRenderPass pass = VK_NULL_HANDLE;
    VkFramebuffer fb = VK_NULL_HANDLE;
    std::vector<GLenum> draw_bufs;       // current draw buffer list (MRT)
    GLenum read_buf = GL_COLOR_ATTACHMENT0;
    std::string sig;               // render-pass signature (pipeline keys)
    uint64_t last_tex_gen = 0;      // cache stamp of the colour textures
    bool dirty = true;              // rebuild pass+framebuffer next use
    VkImageLayout color_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkImageLayout depth_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
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
    bool primitive_restart = false;
    uint32_t instance_count = 1;
    VkDeviceSize vertex_offset = 0;
    VkDeviceSize instance_offset = 0;
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
    // M5: pipeline-affecting state captured at draw-record time.
    PipelineState pipe;
    // Dynamic raster state is also captured at draw-record time; command
    // encoding may happen after the frontend changes it again.
    backend::DynamicState dynamic;
    // S5: render pass signature for the target this draw records into
    // (empty => default framebuffer). Included in the pipeline cache key and
    // the VkGraphicsPipelineCreateInfo.renderPass. `color_count`/`samples`
    // describe that render pass (multi-colour MRT + MSAA).
    bool has_render_pass = false;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    std::string rp_sig;
    uint32_t color_count = 1;
    uint32_t samples = 1;
    uint32_t draw_mask = 1;    // bit i set => colour attachment i receives the draw
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
    // M5: depth attachment for the default framebuffer (D24S8).
    VkFormat depth_format = VK_FORMAT_D24_UNORM_S8_UINT;
    VkImage depth_image = VK_NULL_HANDLE;
    VkDeviceMemory depth_mem = VK_NULL_HANDLE;
    VkImageView depth_view = VK_NULL_HANDLE;
    VkImageLayout depth_layout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkRenderPass renderpass = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;

    // S5: FBO/renderbuffer tables + current draw/read framebuffer bindings.
    std::unordered_map<uint64_t, RbObj> renderbuffers;
    std::unordered_map<uint64_t, FboObj> framebuffers;
    // MSAA resolve images for multisampled colour FBO attachments; keyed by
    // the FboObj pointer (owns the single-sample resolve RbObj storage).
    std::unordered_map<const FboObj*, std::vector<RbObj>> fbo_msaa;
    uint64_t bound_draw_fbo = 0;   // 0 => default framebuffer
    uint64_t bound_read_fbo = 0;   // 0 => default framebuffer
    // Extra FBO render passes (framebuffers revive their own when formats
    // differ from the default color+depth); keyed by their signature.
    std::unordered_map<std::string, VkRenderPass> fbo_passes;

    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkDescriptorPool desc_pool = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;

    // M4 textures: gl texture id -> resident GPU image.
    std::unordered_map<uint64_t, TexObj> textures;
    TexObj dummy_tex;             // 1x1 white fallback for unbound units
    std::unordered_map<std::string, SamplerCacheEntry> samplers;
    uint64_t sampler_clock = 0;

    VkBuffer ubo = VK_NULL_HANDLE;
    VkDeviceMemory ubo_mem = VK_NULL_HANDLE;
    uint8_t* ubo_map = nullptr;
    VkDeviceSize ubo_next = 0;
    VkDeviceSize ubo_align = 256;

    VkBuffer readback = VK_NULL_HANDLE;
    VkDeviceMemory readback_mem = VK_NULL_HANDLE;
    uint8_t* readback_map = nullptr;
    uint32_t read_w = 0, read_h = 0;   // dims the readback buffer holds

    bool initialized = false;
    bool frame_dirty = false;
    bool pending_clear = false;
    ClearParams clear;
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

void TransitionLayoutAspect(VkCommandBuffer cb, VkImage image,
                            VkImageSubresourceRange range,
                            VkImageLayout old_layout,
                            VkImageLayout new_layout);

bool CreateRenderPass();

bool CreateTarget();

bool CreateDepthTarget();

void CreateDummyTexture();

// Resolve the currently bound draw framebuffer's Vk resources, rebuilding
// the framebuffer + render pass when dirty. Returns false if it can't render;
// when the default framebuffer is bound it returns true and leaves out=0.
bool ResolveDrawFbo(FboObj* out);

// Render pass for an FBO signature ("RGBA8:D24S8" -> the default pass).
VkRenderPass GetOrCreateFboPass(const std::string& sig, size_t n_color,
                                bool has_depth, uint32_t samples);

// Live image handle for an FBO colour/depth attachment (texture or
// renderbuffer resolution). Returns VK_NULL_HANDLE when unattached.
VkImage FboColorImage(const FboObj& f, int idx);
VkImage FboResolveImage(const FboObj& f, int idx);
VkImage FboDepthImage(const FboObj& f);

// ---- texture helpers (defined in texture.cpp) ----------------------------

TexObj* GetTexObj(uint64_t gl_id);
VkSampler ResolveSampler(const TexSamplerInfo& sampler, uint32_t levels);

// ---- pipeline helpers (defined in pipeline.cpp) --------------------------

std::string BuildPipelineKey(uint64_t program, uint32_t topology,
                             const std::vector<VertexAttr>& v_attrs,
                             uint32_t v_stride,
                             const std::vector<VertexAttr>& i_attrs,
                             uint32_t i_stride);

// Textual signature of the pipeline-affecting GL state (depth/blend/cull/
// stencil/color-mask/scissor/mode). Appended to the geometry key.
std::string StateSignature(const PipelineState& ps);

VkFormat AttrFormat(uint32_t components);

VkPipeline GetOrCreatePipeline(const Program& prog, const DrawOp& op);

// Map a GL sample count (1/2/4/8/...) to the Vulkan sample-count bit.
VkSampleCountFlagBits ToVkSampleCount(uint32_t samples);

} // namespace mithril::vk
