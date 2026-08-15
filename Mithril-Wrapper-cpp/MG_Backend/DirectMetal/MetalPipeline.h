// Mithril-Wrapper - MG_Backend/DirectMetal/MetalPipeline.h
// SPIR-V -> MSL translation (SPIRV-Cross) + MTLRenderPipelineState /
// MTLComputePipelineState caches + the per-draw resource binding
// (bind_program_descriptors) + the built-in clear / blit shaders.
//
// BINDING INDEX CONVENTION (must match MetalPipeline.mm's MSL remap exactly):
//   * Vertex stage: [[attribute(L)]] is fed from vertex buffer index L (0..15)
//     via the MTLVertexDescriptor (bufferIndex == attribute location). Vertex
//     shader UBO/SSBO bindings therefore start at 16: the i-th VS buffer
//     binding (in ascending descriptor-binding order) -> [[buffer(16 + i)]].
//   * Fragment stage: [[buffer(B)]] where B is the descriptor binding number
//     (no attribute buffers exist in the fragment stage).
//   * Compute stage: [[buffer(B)]] by descriptor binding.
//   * Sampled/storage images: [[texture(B)]] / samplers [[sampler(B)]] by
//     descriptor binding, every stage.
// The mapping is recorded per-program in MetalProgramResources (bufIdxVs /
// bufIdxFs / bufIdxCs) at MSL compile time and consumed by
// bind_program_descriptors at draw time — one source of truth.
#ifndef MITHRIL_DIRECTMETAL_PIPELINE_H
#define MITHRIL_DIRECTMETAL_PIPELINE_H

#ifdef __APPLE__

#include "MetalDevice.h"
#include "MetalCommandStream.h"
#include "BackendTypes.h"   // MGVertexAttrib
#include "../DirectVulkan/Std140.h"  // Std140Slot (shared, dependency-free)
#include "../DirectVulkan/Reflect.h" // mithril::vk::DescriptorBinding (shared reflection)
#include "BackendMetalDecls.h"  // generated dmt_* prototypes (build dir)

#include <string>
#include <unordered_map>
#include <vector>

namespace mithril {
struct Program;

namespace dmt {

/* Per-UBO-binding draw-time source plan — mirrors DirectVulkan's
 * UboBindingPlan but stores resolved `const Uniform*` pointers against the
 * CURRENT link; linkVersion gates regeneration. */
struct MetalUboMemberPlan {
    Std140Slot slot;
    const mithril::Uniform* src = nullptr; // resolved at plan build time
};

struct MetalUboPlan {
    uint32_t binding = 0;
    uint32_t size = 16;
    bool appBlock = false;          // application-declared UBO (glBindBufferBase)
    GLuint  glBlockIndex = 0;       // for appBlock
    const mithril::Uniform* directSrc = nullptr; // single loose uniform == block
    std::vector<MetalUboMemberPlan> members;     // $Global-style aggregation
    std::vector<uint8_t> scratch;   // draw-time pack buffer
    // Arena slice reuse memo (same slot + frame generation + content hash).
    uint64_t lastHash = 0;
    bool     lastValid = false;
    id<MTLBuffer> lastBuffer = nil;
    NSUInteger   lastOffset = 0;
    int      lastSlot = -1;
    uint64_t lastFrameGen = 0;
};

struct MetalCompiledFunction {
    id<MTLLibrary> library = nil;
    id<MTLFunction> function = nil;
    uint32_t wgSize[3] = {1, 1, 1}; // compute only
    bool valid = false;
};

struct MetalPipelineEntry {
    MetalPipeline pipe;             // wrapper handed out as the VkPipeline cookie
};

struct MetalProgramResources {
    // Reflection (shared logic with DirectVulkan — Reflect.cpp compiled once).
    std::vector<mithril::vk::DescriptorBinding> bindings;
    bool reflected = false;
    uint32_t planLinkVersion = 0xFFFFFFFFu;
    std::vector<MetalUboPlan> uboPlans;

    // SPIR-V hash -> compiled MTL function (per stage/variant).
    std::unordered_map<uint64_t, MetalCompiledFunction> fnCache;

    // Descriptor binding -> MSL buffer index per stage.
    std::unordered_map<uint32_t, uint32_t> bufIdxVs; // 16 + ordinal
    std::unordered_map<uint32_t, uint32_t> bufIdxFs; // identity
    std::unordered_map<uint32_t, uint32_t> bufIdxCs; // identity

    // Graphics pipeline cache: signature -> entry.
    std::unordered_map<uint64_t, MetalPipelineEntry*> pipes;
    MetalPipeline* computePipe = nullptr;
};

// Program table + lifecycle.
MetalProgramResources* resources_for(GLuint program, mithril::Program* prog);
MetalProgramResources* resources_get(GLuint program);
void delete_program_resources(GLuint program);
void purge_pipeline_caches();

// Graphics pipeline (cache key covers everything Metal bakes into a PSO).
MetalPipeline* get_or_create_pipeline(
    GLuint program,
    const uint32_t* vertex_spirv, int vertex_word_count,
    const uint32_t* fragment_spirv, int fragment_word_count,
    const MGVertexAttrib* attribs, int attrib_count,
    const VkFormat* color_formats, int color_count,
    VkFormat depth_format,
    int blend_enabled, GLenum blend_src, GLenum blend_dst,
    GLenum blend_src_alpha, GLenum blend_dst_alpha,
    int color_write_mask, int is_default_fbo);

MetalPipeline* get_or_create_compute_pipeline(GLuint program,
                                              mithril::Program* prog);

// Per-draw binding: resolves UBO/SSBO buffers, textures and samplers from the
// frontend state and binds them on the ACTIVE render encoder.
void bind_program_descriptors(GLuint program);

// Compute dispatch (ends any active render encoder first).
void dispatch_compute(GLuint program, mithril::Program* prog,
                      uint32_t gx, uint32_t gy, uint32_t gz);
void dispatch_compute_indirect(GLuint program, mithril::Program* prog,
                               MetalBuffer* indirect, NSUInteger offset);

/* ---- Built-in clear machinery (used by MetalCommandStream.mm) ---- */
// mask: GL_COLOR/DEPTH/STENCIL bits. Formats describe the CURRENT pass
// attachments; the returned pipeline matches them.
MetalPipeline* get_clear_pipeline(const VkFormat* color_formats, int color_count,
                                  VkFormat depth_format, GLbitfield mask);
// Returns the DSS used for a clear with the given mask (depth/stencil writes).
id<MTLDepthStencilState> clear_depth_stencil_state(bool depth, bool stencil);

/* ---- Built-in blit machinery (used by MetalResources.mm) ---- */
id<MTLRenderPipelineState> get_blit_pipeline(MTLPixelFormat dst_format);
id<MTLSamplerState> blit_sampler(bool linear);

} // namespace dmt
} // namespace mithril

#endif // __APPLE__
#endif // MITHRIL_DIRECTMETAL_PIPELINE_H
