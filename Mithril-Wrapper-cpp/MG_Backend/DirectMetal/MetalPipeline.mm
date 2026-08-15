// Mithril-Wrapper - MG_Backend/DirectMetal/MetalPipeline.mm
// The heart of the DirectMetal backend:
//   * SPIR-V -> MSL translation via SPIRV-Cross (compile_msl_function),
//   * MTLRenderPipelineState / MTLComputePipelineState caches keyed by a
//     full pipeline signature (get_or_create_pipeline / _compute_pipeline),
//   * the per-draw resource binding (bind_program_descriptors) which
//     replicates DirectVulkan/DescriptorSet.cpp's UBO plan semantics on top
//     of Metal's per-stage [[buffer(N)]] / [[texture(N)]] / [[sampler(N)]]
//     argument tables,
//   * the built-in fullscreen-triangle clear and blit shaders.
//
// WHY NO COMPILATION-LEVEL MIRROR OF MOLTENVK: the frontend's SPIR-V already
// carries Vulkan conventions (Shader.cpp injects the Z remap and the
// default-FBO Y flip), so the MSL must NOT apply them a second time —
// CompilerGLSL's fixup_clipspace and flip_vert_y are forced off and the
// rasterizer-side convention is reproduced with negative-height viewports
// (see MetalDevice.h's RENDERING CONVENTION block).
//
// BINDING INDEX CONVENTION (must stay in lock-step with MetalPipeline.h):
//   Vertex stage   : [[attribute(L)]] is fed from vertex buffer index L
//                    (0..15) through the MTLVertexDescriptor, so shader
//                    buffers start at 16: the i-th VS buffer binding in
//                    ascending descriptor-binding order -> [[buffer(16 + i)]].
//   Fragment stage : [[buffer(B)]] by descriptor binding (no attribute
//                    buffers exist there).
//   Compute stage  : [[buffer(B)]] by descriptor binding.
//   Textures/samplers: [[texture(B)]] / [[sampler(B)]] by binding, all stages.
#ifdef __APPLE__

// WHY THIS ORDER: MetalPipeline.h uses Std140Slot (Std140.h) and
// mithril::vk::DescriptorBinding (Reflect.h) without including their headers
// (this TU is its only consumer), so they must be parsed BEFORE it.
#include "../DirectVulkan/Std140.h"    // Std140Slot / pack_std140 (shared)
#include "../DirectVulkan/Reflect.h"   // reflect_stage / merge_bindings (shared)
#include "MetalPipeline.h"
#include "MetalResources.h"
#include "../../MG_State/State.h"
#include "../../MG_Impl/Log.h"

#include <spirv_cross.hpp>
#include <spirv_msl.hpp>

#include <algorithm>
#include <cstring>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mithril {
namespace dmt {

// Defined in MetalResources.mm (internal, no header declaration yet).
MetalBuffer* dmt_internal_get_or_create_buffer(GLuint name, const void* data,
                                               size_t size);
MetalSampler* dmt_internal_get_or_create_sampler(GLuint name, GLint min_filter,
                                                 GLint mag_filter, GLint wrap_s,
                                                 GLint wrap_t, GLint wrap_r,
                                                 const float* border_color);
// Defined in MetalFormat.mm (pure enum translation, no public header).
MTLPixelFormat   pixel_format_from_vk(VkFormat f);
bool             format_has_stencil(VkFormat f);
MTLVertexFormat  vertex_format_from_gl(GLenum type, int size, int normalized,
                                       int integer);
NSUInteger       vertex_format_bytes(MTLVertexFormat f);
MTLBlendFactor   blend_factor_from_gl(GLenum f, bool alphaChannel);

namespace {

// ---- FNV-1a 64 (same constants as DirectVulkan/DescriptorSet.cpp) ----------
constexpr uint64_t kFnvBasis = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

inline uint64_t fnv1a(const void* data, size_t n, uint64_t h = kFnvBasis) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= kFnvPrime; }
    return h;
}
inline uint64_t fnv1a_u64(uint64_t v, uint64_t h) { return fnv1a(&v, sizeof(v), h); }
inline uint64_t fnv1a_i64(int64_t v, uint64_t h) { return fnv1a(&v, sizeof(v), h); }

// Metal's per-stage argument-table hard cap minus one; used to clamp the
// remapped indices so a pathological shader degrades to a skipped binding
// instead of a driver validation abort.
constexpr uint32_t kMaxMetalArgIndex = 30;

// SPIRV-Cross wants MSL versions in its own make_msl_version() encoding
// (major*10000 + minor*100), while msl_version_for_device() returns the raw
// MTLLanguageVersion ((major << 16) | minor). Translate once per compile.
uint32_t spv_msl_version_from_device() {
    const uint32_t v = msl_version_for_device();
    return spirv_cross::CompilerMSL::Options::make_msl_version(v >> 16, v & 0xFFFFu);
}

// ---- Program resource table ------------------------------------------------

std::unordered_map<GLuint, MetalProgramResources*>& program_tbl() {
    static std::unordered_map<GLuint, MetalProgramResources*> t;
    return t;
}

// Negative cache for graphics-pipeline signatures whose
// newRenderPipelineStateWithDescriptor failed. MetalProgramResources has no
// field for it (the struct is fixed in the header), so it lives beside the
// table. Cleared by purge_pipeline_caches so a recovered device can retry.
std::unordered_map<GLuint, std::unordered_set<uint64_t>>& failed_sig_tbl() {
    static std::unordered_map<GLuint, std::unordered_set<uint64_t>> t;
    return t;
}

// Builtin (clear/blit) shader machinery lives outside any program.
// The clear family is FOUR function variants, not one: run_clear_draw binds
// its per-aspect arguments conditionally (fragment float4 only under
// GL_COLOR_BUFFER_BIT, vertex float only under GL_DEPTH_BUFFER_BIT), and
// Metal's encoder validation rejects a draw whose shader declares a used
// [[buffer(N)]] that was never set. Selecting the variant whose signature
// matches the mask keeps every binding the encoder (doesn't) make legal.
struct BuiltinCache {
    id<MTLFunction> clearVSPlain = nil; // no args, z=0     (no depth clear)
    id<MTLFunction> clearVSDepth = nil; // [[buffer(15)]] depth param
    id<MTLFunction> clearFSColor = nil; // [[buffer(0)]] float4 color
    id<MTLFunction> clearFSNone  = nil; // no args, no outputs (stencil-only)
    id<MTLFunction> blitVS = nil;
    id<MTLFunction> blitFS = nil;
    bool functionsAttempted = false;
    bool functionsValid = false;

    std::unordered_map<uint64_t, MetalPipelineEntry*> clearPipes;
    id<MTLDepthStencilState> clearDSS[4] = {nil, nil, nil, nil};

    std::unordered_map<uint32_t, id<MTLRenderPipelineState>> blitPipes;
    id<MTLSamplerState> blitSamplers[2] = {nil, nil};
};
BuiltinCache& builtin_cache() {
    static BuiltinCache c;
    return c;
}

/* ---- SPIR-V -> MSL -------------------------------------------------------- */

/* Compile one SPIR-V stage into an MTLFunction, remapping every buffer /
 * texture / sampler argument onto the index convention above. The remap is
 * recorded into the program's bufIdxVs/bufIdxFs/bufIdxCs tables so
 * bind_program_descriptors binds by exactly the indices the MSL declares —
 * one source of truth, no re-derivation at draw time.
 *
 * Stage selects the buffer-index scheme:
 *   - Vertex: buffers get 16 + ordinal (ordinal = rank in ascending
 *     descriptor-binding order over ALL buffer resources of the stage).
 *   - Fragment / Compute: buffers keep their descriptor binding number.
 *
 * Returns false (with out.valid left false) on any failure; the caller caches
 * the failed entry keyed by the SPIR-V hash so a broken shader costs one
 * attempt, not one per draw. */
bool compile_msl_function(MetalProgramResources* pr,
                          const uint32_t* spirv, int words,
                          spv::ExecutionModel stage,
                          MetalCompiledFunction& out) {
    Backend* b = backend();
    if (!b->initialized || !spirv || words <= 0) return false;

    try {
        spirv_cross::CompilerMSL compiler(spirv, static_cast<size_t>(words));

        // MSL options: match the language version the MTLCompileOptions below
        // will request, and pick the platform profile from the memory model
        // (Apple-Silicon-style unified GPUs take the iOS profile, discrete
        // Mac GPUs the macOS one) — this mirrors how MoltenVK chooses.
        spirv_cross::CompilerMSL::Options mopts = compiler.get_msl_options();
        mopts.msl_version = spv_msl_version_from_device();
        mopts.platform = b->unifiedMemory
            ? spirv_cross::CompilerMSL::Options::iOS
            : spirv_cross::CompilerMSL::Options::macOS;
        compiler.set_msl_options(mopts);

        // The frontend's SPIR-V already remaps Z to [0,1] (Shader.cpp's
        // injected position fixups) and ships the default-FBO Y flip as a
        // separate module, so SPIRV-Cross must not apply either again — a
        // double flip would put every default-FBO frame upside down.
        spirv_cross::CompilerGLSL::Options copts = compiler.get_common_options();
        copts.vertex.fixup_clipspace = false;
        copts.vertex.flip_vert_y = false;
        compiler.set_common_options(copts);

        const spirv_cross::ShaderResources res = compiler.get_shader_resources();

        /* ---- Buffers (UBO + SSBO share one index space per stage) ----
         * The frontend guarantees binding uniqueness across descriptor KINDS
         * (Shader.cpp remap_descriptor_bindings), so a plain (set,binding)
         * sort+unique yields a stable ordinal assignment. */
        std::vector<std::pair<uint32_t, uint32_t>> bufKeys; // (set, binding)
        auto collect_buffers = [&](const spirv_cross::SmallVector<spirv_cross::Resource>& list) {
            for (const auto& r : list) {
                bufKeys.emplace_back(
                    compiler.get_decoration(r.id, spv::DecorationDescriptorSet),
                    compiler.get_decoration(r.id, spv::DecorationBinding));
            }
        };
        collect_buffers(res.uniform_buffers);
        collect_buffers(res.storage_buffers);
        std::sort(bufKeys.begin(), bufKeys.end());
        bufKeys.erase(std::unique(bufKeys.begin(), bufKeys.end()), bufKeys.end());

        uint32_t vsOrdinal = 0;
        for (const auto& key : bufKeys) {
            spirv_cross::MSLResourceBinding rb{};
            rb.stage = stage;
            rb.desc_set = key.first;
            rb.binding = key.second;
            rb.basetype = spirv_cross::SPIRType::Struct;
            rb.count = 1;
            if (stage == spv::ExecutionModelVertex) {
                // Attributes own vertex buffer indices 0..15 (see the
                // convention block) — shader buffers start right after.
                if (16u + vsOrdinal > kMaxMetalArgIndex) {
                    MITHRIL_LOG_ERROR("mtl", "VS buffer binding (set=%u binding=%u) "
                                      "would exceed Metal's 31-buffer argument "
                                      "table (ordinal %u) — stage compile aborted",
                                      key.first, key.second, vsOrdinal);
                    return false;
                }
                rb.msl_buffer = 16u + vsOrdinal++;
                pr->bufIdxVs[key.second] = rb.msl_buffer;
            } else {
                if (key.second > kMaxMetalArgIndex) {
                    MITHRIL_LOG_ERROR("mtl", "buffer binding %u exceeds Metal's "
                                      "31-buffer argument table — stage compile "
                                      "aborted", key.second);
                    return false;
                }
                rb.msl_buffer = key.second;
                if (stage == spv::ExecutionModelFragment) pr->bufIdxFs[key.second] = key.second;
                else                                      pr->bufIdxCs[key.second] = key.second;
            }
            compiler.add_msl_resource_binding(rb);
        }

        /* ---- Textures + samplers ----
         * [[texture(N)]] and [[sampler(N)]] are independent argument tables,
         * so a combined image sampler can use its binding number for both. */
        auto add_image_binding = [&](const spirv_cross::Resource& r, bool withSampler) {
            const uint32_t set = compiler.get_decoration(r.id, spv::DecorationDescriptorSet);
            const uint32_t bind = compiler.get_decoration(r.id, spv::DecorationBinding);
            if (bind > kMaxMetalArgIndex) {
                MITHRIL_LOG_ERROR("mtl", "texture/sampler binding %u exceeds "
                                  "Metal's argument table — stage compile aborted",
                                  bind);
                return false;
            }
            const spirv_cross::SPIRType& t = compiler.get_type(r.type_id);
            spirv_cross::MSLResourceBinding rb{};
            rb.stage = stage;
            rb.basetype = compiler.get_type(r.base_type_id).basetype;
            rb.desc_set = set;
            rb.binding = bind;
            rb.count = t.array.empty() ? 1u : (t.array[0] ? t.array[0] : 1u);
            rb.msl_texture = bind;
            if (withSampler) rb.msl_sampler = bind;
            compiler.add_msl_resource_binding(rb);
            return true;
        };
        for (const auto& r : res.sampled_images)
            if (!add_image_binding(r, true)) return false;
        for (const auto& r : res.separate_images)
            if (!add_image_binding(r, false)) return false;
        for (const auto& r : res.storage_images)
            if (!add_image_binding(r, false)) return false;
        for (const auto& r : res.separate_samplers) {
            const uint32_t bind = compiler.get_decoration(r.id, spv::DecorationBinding);
            if (bind > kMaxMetalArgIndex) {
                MITHRIL_LOG_ERROR("mtl", "sampler binding %u exceeds Metal's "
                                  "argument table — stage compile aborted", bind);
                return false;
            }
            spirv_cross::MSLResourceBinding rb{};
            rb.stage = stage;
            rb.basetype = compiler.get_type(r.base_type_id).basetype;
            rb.desc_set = compiler.get_decoration(r.id, spv::DecorationDescriptorSet);
            rb.binding = bind;
            rb.count = 1;
            rb.msl_sampler = bind;
            compiler.add_msl_resource_binding(rb);
        }

        const std::string msl = compiler.compile();

        if (stage == spv::ExecutionModelGLCompute) {
            // Reflect LocalSize so dispatch_compute can size its threadgroups;
            // Metal requires a concrete threadsPerThreadgroup at dispatch.
            for (int i = 0; i < 3; ++i) {
                uint32_t v = compiler.get_execution_mode_argument(
                    spv::ExecutionModeLocalSize, static_cast<uint32_t>(i));
                out.wgSize[i] = v ? v : 1u;
            }
        }

        MTLCompileOptions* mtlOpts = [[MTLCompileOptions alloc] init];
        mtlOpts.languageVersion = (MTLLanguageVersion)msl_version_for_device();
        mtlOpts.fastMathEnabled = NO; // match MoltenVK's conservative default

        NSError* err = nil;
        NSString* src = [NSString stringWithUTF8String:msl.c_str()];
        id<MTLLibrary> lib = [b->device newLibraryWithSource:src
                                                     options:mtlOpts
                                                       error:&err];
        if (lib == nil) {
            MITHRIL_LOG_ERROR("mtl", "MTLLibrary compile failed (%s stage): %s",
                              stage == spv::ExecutionModelVertex ? "vertex" :
                              stage == spv::ExecutionModelFragment ? "fragment" : "compute",
                              err ? err.localizedDescription.UTF8String : "unknown");
            return false;
        }
        out.function = [lib newFunctionWithName:@"main"]; // SPIRV-Cross entry name
        if (out.function == nil) {
            MITHRIL_LOG_ERROR("mtl", "newFunctionWithName:@\"main\" returned nil "
                              "(%s stage)",
                              stage == spv::ExecutionModelVertex ? "vertex" :
                              stage == spv::ExecutionModelFragment ? "fragment" : "compute");
            return false;
        }
        out.library = lib;
        out.valid = true;
        return true;
    } catch (const std::exception& e) {
        MITHRIL_LOG_ERROR("mtl", "SPIRV-Cross MSL translation threw (%s stage): %s",
                          stage == spv::ExecutionModelVertex ? "vertex" :
                          stage == spv::ExecutionModelFragment ? "fragment" : "compute",
                          e.what());
        return false;
    }
}

/* Fetch (or compile+cache) one stage's MTLFunction. The cache is keyed by the
 * SPIR-V content hash, which inherently separates the Y-flipped and non-
 * flipped vertex variants the frontend keeps as distinct modules. A failed
 * entry stays cached with valid=false so the failure is attempted once. */
MetalCompiledFunction* get_or_compile_stage(MetalProgramResources* pr,
                                            const uint32_t* spirv, int words,
                                            spv::ExecutionModel stage) {
    const uint64_t h = fnv1a(spirv, static_cast<size_t>(words) * sizeof(uint32_t));
    auto it = pr->fnCache.find(h);
    if (it != pr->fnCache.end()) return &it->second;

    MetalCompiledFunction cf;
    const bool ok = compile_msl_function(pr, spirv, words, stage, cf);
    auto res = pr->fnCache.emplace(h, std::move(cf));
    return ok ? &res.first->second : nullptr;
}

/* ---- UBO plans (replicates DirectVulkan DescriptorSet.cpp build_ubo_plans) */

void build_ubo_plans(MetalProgramResources& pr, const mithril::Program* prog) {
    pr.uboPlans.clear();
    for (const auto& db : pr.bindings) {
        if (db.type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) continue;
        pr.uboPlans.emplace_back();
        MetalUboPlan& plan = pr.uboPlans.back();
        plan.binding = db.binding;
        plan.size = db.bufferSize ? db.bufferSize : 16u;

        // (a) Application-declared block — the app owns the bytes through
        //     glBindBufferBase/Range; nothing is packed, only a MetalBuffer
        //     to look up at draw time.
        auto blk_it = prog->blockIndexForDescriptor.find(db.binding);
        if (blk_it != prog->blockIndexForDescriptor.end() &&
            blk_it->second < prog->blockInfos.size()) {
            plan.appBlock = true;
            plan.glBlockIndex = blk_it->second;
            continue;
        }

        // (b) One loose uniform whose value IS the block (glslang emits a
        //     UBO per loose uniform when it does not aggregate).
        auto uit = prog->uniforms.find(db.name);
        if (uit != prog->uniforms.end()) plan.directSrc = &uit->second;

        // (c) Aggregated $Global / mithril_GlobalBlock. Members with no
        //     matching uniform are dropped here instead of being re-looked-up
        //     and re-missed on every draw.
        plan.members.reserve(db.members.size());
        for (const auto& m : db.members) {
            auto mit = prog->uniforms.find(m.name);
            if (mit == prog->uniforms.end()) continue;
            MetalUboMemberPlan mp;
            mp.slot.offset       = m.offset;
            mp.slot.size         = m.size;
            mp.slot.columns      = m.columns;
            mp.slot.rows         = m.rows;
            mp.slot.arraySize    = m.arraySize;
            mp.slot.arrayStride  = m.arrayStride;
            mp.slot.matrixStride = m.matrixStride;
            mp.src = &mit->second;
            plan.members.push_back(mp);
        }
        plan.scratch.assign(plan.size, 0);
    }
    pr.planLinkVersion = prog->linkVersion;
}

/* Pack a layer-synthesised block, hash it and (when the bytes actually
 * changed, or the previous slice died with its frame slot) bump-allocate a
 * fresh 256-aligned slice from the per-frame UBO arena. The memo mirrors
 * DirectVulkan's UboBindingPlan cache; the Metal arena has no flush-
 * generation concept (no mid-frame command-buffer re-begin), so validity is
 * keyed on (slot, frameGeneration) only. */
bool resolve_ubo_slice(MetalUboPlan& plan, int slot, uint64_t frameGen,
                       UboSliceDmt& out) {
    if (plan.scratch.size() != plan.size) plan.scratch.assign(plan.size, 0);
    // Zero first: a member with no value yet must read as zero, not as
    // whatever the previous draw left behind in the scratch buffer.
    std::memset(plan.scratch.data(), 0, plan.scratch.size());
    if (plan.directSrc && !plan.directSrc->value.empty()) {
        const size_t bytes = plan.directSrc->value.size() * sizeof(float);
        std::memcpy(plan.scratch.data(), plan.directSrc->value.data(),
                    std::min(bytes, plan.scratch.size()));
    } else {
        for (const auto& mp : plan.members) {
            if (!mp.src || mp.src->value.empty()) continue;
            pack_std140(plan.scratch.data(), plan.scratch.size(), mp.slot,
                        mp.src->value.data(), mp.src->value.size());
        }
    }

    const uint64_t h = fnv1a(plan.scratch.data(), plan.scratch.size());
    const bool sliceLive = plan.lastValid &&
                           plan.lastBuffer != nil &&
                           plan.lastSlot == slot &&
                           plan.lastFrameGen == frameGen;
    if (!sliceLive || plan.lastHash != h) {
        UboSliceDmt sl;
        if (!ubo_upload(slot, plan.scratch.data(),
                        static_cast<NSUInteger>(plan.scratch.size()), sl)) {
            // Arena exhausted (≈a runaway, not a workload). Skipping just
            // this binding would leave the MSL reading a stale/absent buffer,
            // so the whole descriptor bind is dropped for this draw — same
            // policy as the Vulkan path.
            plan.lastValid = false;
            static int arenaFailCount = 0;
            if (++arenaFailCount <= 3) {
                MITHRIL_LOG_WARN("mtl", "ubo arena exhausted (binding %u, %zu bytes) "
                                 "— skipping the descriptor bind for this draw",
                                 plan.binding, plan.scratch.size());
            }
            return false;
        }
        plan.lastBuffer   = sl.buf;
        plan.lastOffset   = sl.offset;
        plan.lastHash     = h;
        plan.lastValid    = true;
        plan.lastSlot     = slot;
        plan.lastFrameGen = frameGen;
    }
    out.buf = plan.lastBuffer;
    out.offset = plan.lastOffset;
    out.size = plan.scratch.size();
    return true;
}

/* Resolve an application-declared uniform block to the MetalBuffer the app
 * bound via glBindBufferBase/Range. Two GL indirections are walked:
 *   descriptor binding -> GL block index   (resolved at link time)
 *   GL block index     -> GL binding point (glUniformBlockBinding)
 *   GL binding point   -> GL buffer        (glBindBufferBase/Range)
 * Returns nil when nothing is bound; callers substitute a zero-filled buffer
 * so the shader reads defined (zero) bytes instead of undefined memory. */
MetalBuffer* resolve_app_ubo(GLuint program, const vk::DescriptorBinding& db,
                             const MetalUboPlan& plan,
                             const mithril::Program* prog,
                             NSUInteger& outOffset) {
    outOffset = 0;
    const mithril::UniformBlockInfo& info = prog->blockInfos[plan.glBlockIndex];

    GLuint point = info.bindingPoint;
    auto pit = prog->uniformBlockBindings.find(plan.glBlockIndex);
    if (pit != prog->uniformBlockBindings.end()) point = pit->second;

    if (point >= (GLuint)mithril::kMaxIndexedBindings) return nullptr;
    const auto& sl = mithril::g_state->indexedBufferBindings
                         [(int)mithril::IndexedBufferTarget::Uniform][point];
    if (sl.name == 0) return nullptr;

    MetalBuffer* mb = buffer_table_get(sl.name);
    if (!mb || mb->buf == nil) return nullptr;

    if (sl.hasExplicitRange) {
        // Defensive clamp: glBindBufferRange offset+size beyond the buffer's
        // real capacity would make the GPU read past the allocation (page
        // fault) — clamp into the actual MTLBuffer like the Vulkan path does.
        if ((NSUInteger)sl.offset >= mb->capacity) {
            static int oobWarn = 0;
            if (oobWarn < 10) {
                MITHRIL_LOG_WARN("mtl", "UBO range OOB prog=%u binding=%u "
                                  "off=%lld cap=%llu — binding offset 0",
                                  program, db.binding, (long long)sl.offset,
                                  (unsigned long long)mb->capacity);
            }
            oobWarn++;
            outOffset = 0;
            return mb;
        }
        outOffset = (NSUInteger)sl.offset;
        // Metal requires buffer offsets to be 256-byte aligned on macOS
        // (ubo_alignment()); an unaligned glBindBufferRange offset cannot be
        // passed through, so copy the block into an aligned arena slice.
        if (outOffset & (ubo_alignment() - 1)) {
            static int alignWarn = 0;
            if (alignWarn < 10) {
                MITHRIL_LOG_WARN("mtl", "UBO offset %llu not %llu-aligned "
                                  "(prog=%u binding=%u) — staging through the "
                                  "arena", (unsigned long long)outOffset,
                                  (unsigned long long)ubo_alignment(),
                                  program, db.binding);
            }
            alignWarn++;
            return nullptr; // caller stages through the arena
        }
    }
    return mb;
}

// Fallback zero buffer for unbound app blocks (same synthetic-name scheme as
// the Vulkan backend, so both backends can coexist on the same GL names).
MetalBuffer* zero_block_fallback(GLuint program, uint32_t binding, uint32_t size) {
    const uint32_t zsz = size ? size : 16u;
    std::vector<uint8_t> zeros(zsz, 0);
    const GLuint zname = program * 1000000u + binding + 1u;
    MetalBuffer* mb = dmt_internal_get_or_create_buffer(zname, zeros.data(), zeros.size());
    return mb;
}

/* Bind one resolved buffer onto the render encoder for every stage the
 * reflection recorded, using the exact MSL indices captured at compile time. */
void bind_render_buffer(id<MTLRenderCommandEncoder> enc,
                        const MetalProgramResources* pr,
                        const vk::DescriptorBinding& db,
                        id<MTLBuffer> buf, NSUInteger offset) {
    if (buf == nil) return;
    if (db.stageMask & VK_SHADER_STAGE_VERTEX_BIT) {
        auto it = pr->bufIdxVs.find(db.binding);
        if (it != pr->bufIdxVs.end())
            [enc setVertexBuffer:buf offset:offset atIndex:(NSUInteger)it->second];
    }
    if (db.stageMask & VK_SHADER_STAGE_FRAGMENT_BIT) {
        auto it = pr->bufIdxFs.find(db.binding);
        if (it != pr->bufIdxFs.end())
            [enc setFragmentBuffer:buf offset:offset atIndex:(NSUInteger)it->second];
    }
}

// Full graphics-pipeline signature: everything Metal bakes into a PSO plus
// the module identity. FNV-1a over the whole tuple; collisions are as
// unlikely as they are on the Vulkan side.
uint64_t pipeline_signature(GLuint program, uint64_t vsHash, uint64_t fsHash,
                             const MGVertexAttrib* attribs, int attrib_count,
                             const VkFormat* color_formats, int color_count,
                             VkFormat depth_format,
                             int blend_enabled, GLenum blend_src, GLenum blend_dst,
                             GLenum blend_src_alpha, GLenum blend_dst_alpha,
                             int color_write_mask, int is_default_fbo) {
    uint64_t h = kFnvBasis;
    h = fnv1a_u64(program, h);
    h = fnv1a_u64(vsHash, h);
    h = fnv1a_u64(fsHash, h);
    h = fnv1a_u64((uint64_t)is_default_fbo, h);
    h = fnv1a_u64((uint64_t)attrib_count, h);
    for (int i = 0; i < attrib_count; ++i) {
        const MGVertexAttrib& a = attribs[i];
        h = fnv1a_u64((uint64_t)a.location, h);
        h = fnv1a_u64((uint64_t)a.size, h);
        h = fnv1a_u64((uint64_t)a.type, h);
        h = fnv1a_u64((uint64_t)a.normalized, h);
        h = fnv1a_u64((uint64_t)a.integer, h);
        h = fnv1a_u64((uint64_t)a.stride, h);
        h = fnv1a_u64((uint64_t)a.offset, h);
        h = fnv1a_u64((uint64_t)a.enabled, h);
        h = fnv1a_u64((uint64_t)a.divisor, h);
        // buffer_name is deliberately NOT hashed: it selects the bound
        // vertex buffer (draw-time state), not the PSO's vertex layout.
    }
    h = fnv1a_u64((uint64_t)color_count, h);
    for (int i = 0; i < color_count; ++i) h = fnv1a_u64((uint64_t)color_formats[i], h);
    h = fnv1a_u64((uint64_t)depth_format, h);
    h = fnv1a_u64((uint64_t)blend_enabled, h);
    h = fnv1a_u64((uint64_t)blend_src, h);
    h = fnv1a_u64((uint64_t)blend_dst, h);
    h = fnv1a_u64((uint64_t)blend_src_alpha, h);
    h = fnv1a_u64((uint64_t)blend_dst_alpha, h);
    h = fnv1a_u64((uint64_t)color_write_mask, h);
    /* MSAA：rasterSampleCount 烘焙进 PSO，且 Metal 校验要求 PSO 采样数与
     * render pass 附件的纹理 sampleCount 一致 —— 必须参与缓存键，否则单采样
     * 与多采样 FBO 交替时复用错 PSO → Metal 校验失败 / 命令缓冲提交出错。
     * 与 PSO 构建的 d.rasterSampleCount 同一来源（draw_fbo_sample_count：
     * 当前绘制 FBO 附件的最大采样数，FBO 0 恒为 1）。 */
    h = fnv1a_u64((uint64_t)mithril::draw_fbo_sample_count(), h);
    return h;
}

// GL's 4-bit RGBA mask (bit0=R..bit3=A) -> MTLColorWriteMask, whose bit
// layout is Red=4 / Green=2 / Blue=1 / Alpha=8 — NOT the same order.
MTLColorWriteMask color_write_mask_from_gl_bits(int mask) {
    uint32_t m = MTLColorWriteMaskNone;
    if (mask & 1) m |= MTLColorWriteMaskRed;
    if (mask & 2) m |= MTLColorWriteMaskGreen;
    if (mask & 4) m |= MTLColorWriteMaskBlue;
    if (mask & 8) m |= MTLColorWriteMaskAlpha;
    return (MTLColorWriteMask)m;
}

/* Declare the depth/stencil attachment pixel formats on a pipeline so they
 * match the render pass exactly. Metal validation requires:
 *   * a packed format (Depth24Unorm_Stencil8 / Depth32Float_Stencil8) to be
 *     set on BOTH depthAttachmentPixelFormat and stencilAttachmentPixelFormat
 *     whenever the pass binds that texture — stencil writes (the clear path's
 *     Replace op included) are impossible otherwise;
 *   * stencil-only textures (MTLPixelFormatStencil8) to go to the stencil
 *     slot alone;
 *   * the pipeline's attachment formats to equal the pass's, so "declare only
 *     when the shader uses them" is not an option. */
void apply_depth_stencil_formats(MTLRenderPipelineDescriptor* d,
                                 VkFormat depth_format) {
    if (depth_format == VK_FORMAT_UNDEFINED) return;
    const MTLPixelFormat pf = pixel_format_from_vk(depth_format);
    if (pf == MTLPixelFormatInvalid) return;
    if (pf == MTLPixelFormatStencil8) {
        d.stencilAttachmentPixelFormat = pf;
        return;
    }
    d.depthAttachmentPixelFormat = pf;
    if (format_has_stencil(depth_format)) d.stencilAttachmentPixelFormat = pf;
}

} // namespace

/* ---- Program table + lifecycle ------------------------------------------- */

MetalProgramResources* resources_for(GLuint program, mithril::Program* prog) {
    (void)prog; // reserved: the table is keyed by the GL name alone
    auto& tbl = program_tbl();
    auto it = tbl.find(program);
    if (it != tbl.end()) return it->second;
    MetalProgramResources* pr = new MetalProgramResources();
    tbl.emplace(program, pr);
    return pr;
}

MetalProgramResources* resources_get(GLuint program) {
    auto& tbl = program_tbl();
    auto it = tbl.find(program);
    return it == tbl.end() ? nullptr : it->second;
}

void delete_program_resources(GLuint program) {
    auto& tbl = program_tbl();
    auto it = tbl.find(program);
    if (it == tbl.end()) return;
    MetalProgramResources* pr = it->second;
    for (auto& kv : pr->pipes) delete kv.second;
    delete pr->computePipe;
    delete pr;
    tbl.erase(it);
    failed_sig_tbl().erase(program);
}

void purge_pipeline_caches() {
    // Drop every compiled function / PSO so a recovered device (deviceLost
    // reset, OOM drain) rebuilds them from scratch; Metal shader caches can
    // be poisoned by whatever killed the device in the first place.
    for (auto& kv : program_tbl()) {
        MetalProgramResources* pr = kv.second;
        for (auto& p : pr->pipes) delete p.second;
        pr->pipes.clear();
        delete pr->computePipe;
        pr->computePipe = nullptr;
        pr->fnCache.clear();
        pr->bufIdxVs.clear();
        pr->bufIdxFs.clear();
        pr->bufIdxCs.clear();
        pr->reflected = false;
        pr->bindings.clear();
        pr->uboPlans.clear();
        pr->planLinkVersion = 0xFFFFFFFFu;
    }
    failed_sig_tbl().clear();

    BuiltinCache& c = builtin_cache();
    for (auto& kv : c.clearPipes) delete kv.second;
    c.clearPipes.clear();
    for (int i = 0; i < 4; ++i) c.clearDSS[i] = nil;
    c.blitPipes.clear();
    c.blitSamplers[0] = nil;
    c.blitSamplers[1] = nil;
    // Keep the builtin MTLFunctions: they are device-compiled pure source
    // with no reflection coupling, and recompiling them is pure latency.
}

/* ---- Graphics pipeline ---------------------------------------------------- */

MetalPipeline* get_or_create_pipeline(
    GLuint program,
    const uint32_t* vertex_spirv, int vertex_word_count,
    const uint32_t* fragment_spirv, int fragment_word_count,
    const MGVertexAttrib* attribs, int attrib_count,
    const VkFormat* color_formats, int color_count,
    VkFormat depth_format,
    int blend_enabled, GLenum blend_src, GLenum blend_dst,
    GLenum blend_src_alpha, GLenum blend_dst_alpha,
    int color_write_mask, int is_default_fbo) {
    Backend* b = backend();
    if (!b->initialized || b->deviceLost.load()) return nullptr;
    if (!vertex_spirv || vertex_word_count <= 0) return nullptr;
    if (color_count < 0) color_count = 0;
    if (color_count > kMaxColorAttachments) color_count = kMaxColorAttachments;

    MetalProgramResources* pr = resources_for(program, nullptr);
    if (!pr) return nullptr;

    /* Reflect once per program. reflect_stage returns an empty vector on a
     * null/short module, so a missing fragment stage simply contributes
     * nothing. A reflection failure is also latched by `reflected` — the
     * Vulkan path does the same to avoid per-draw retry cost. */
    if (!pr->reflected) {
        pr->bindings = vk::reflect_stage(vertex_spirv, vertex_word_count,
                                         VK_SHADER_STAGE_VERTEX_BIT);
        vk::merge_bindings(pr->bindings,
                           vk::reflect_stage(fragment_spirv, fragment_word_count,
                                             VK_SHADER_STAGE_FRAGMENT_BIT));
        std::sort(pr->bindings.begin(), pr->bindings.end(),
                  [](const vk::DescriptorBinding& x, const vk::DescriptorBinding& y) {
                      if (x.set != y.set) return x.set < y.set;
                      return x.binding < y.binding;
                  });
        pr->reflected = true;
    }

    const uint64_t vsHash = fnv1a(vertex_spirv,
                                  static_cast<size_t>(vertex_word_count) * sizeof(uint32_t));
    uint64_t fsHash = 0;
    if (fragment_spirv && fragment_word_count > 0) {
        fsHash = fnv1a(fragment_spirv,
                       static_cast<size_t>(fragment_word_count) * sizeof(uint32_t));
    }

    // Compile (or fetch) both stages. The vertex variant (Y-flipped or not)
    // was already chosen by the frontend — the hash keys them apart here.
    MetalCompiledFunction* vf = get_or_compile_stage(pr, vertex_spirv,
                                                     vertex_word_count,
                                                     spv::ExecutionModelVertex);
    if (!vf || !vf->valid) return nullptr;
    MetalCompiledFunction* ff = nullptr;
    if (fragment_spirv && fragment_word_count > 0) {
        ff = get_or_compile_stage(pr, fragment_spirv, fragment_word_count,
                                  spv::ExecutionModelFragment);
        if (!ff || !ff->valid) return nullptr;
    }

    const uint64_t sig = pipeline_signature(
        program, vsHash, fsHash, attribs, attrib_count, color_formats,
        color_count, depth_format, blend_enabled, blend_src, blend_dst,
        blend_src_alpha, blend_dst_alpha, color_write_mask, is_default_fbo);

    auto it = pr->pipes.find(sig);
    if (it != pr->pipes.end()) return &it->second->pipe;
    if (failed_sig_tbl()[program].count(sig)) return nullptr;

    /* ---- Build the PSO descriptor ---- */
    MTLRenderPipelineDescriptor* d = [[MTLRenderPipelineDescriptor alloc] init];
    d.vertexFunction = vf->function;
    d.fragmentFunction = (ff && ff->valid) ? ff->function : nil;
    /* MSAA：PSO 采样数必须等于 render pass 附件纹理的 sampleCount（Metal 校验
     * 强制）。与缓存键 / Vulkan 的 rasterizationSamples 同一来源。 */
    d.rasterSampleCount = (NSUInteger)mithril::draw_fbo_sample_count();

    // Vertex layout: attribute L reads vertex buffer L, stride/divisor per
    // attribute (GL binds one VBO per attribute through the frontend's
    // per-slot set_vertex_buffer). Disabled attributes are skipped; the
    // command stream feeds those from the generic-attribute constant buffer.
    MTLVertexDescriptor* vd = nil;
    for (int i = 0; i < attrib_count; ++i) {
        const MGVertexAttrib& a = attribs[i];
        if (!a.enabled) continue;
        if (a.location < 0 || a.location >= kMaxVertexAttribSlots) continue;
        MTLVertexFormat vfmt = vertex_format_from_gl(a.type, a.size,
                                                     a.normalized, a.integer);
        if (vfmt == MTLVertexFormatInvalid) {
            MITHRIL_LOG_WARN("mtl", "attrib %d (loc %d) has no MTLVertexFormat "
                             "— skipped", i, a.location);
            continue;
        }
        if (vd == nil) vd = [[MTLVertexDescriptor alloc] init];
        MTLVertexAttributeDescriptor* ad = [[MTLVertexAttributeDescriptor alloc] init];
        ad.format = vfmt;
        ad.offset = (NSUInteger)a.offset;
        ad.bufferIndex = (NSUInteger)a.location;
        vd.attributes[a.location] = ad;

        MTLVertexBufferLayoutDescriptor* bl = [[MTLVertexBufferLayoutDescriptor alloc] init];
        NSUInteger minStride = vertex_format_bytes(vfmt);
        NSUInteger stride = (NSUInteger)(a.stride > 0 ? a.stride : (int)minStride);
        if (stride < minStride) stride = minStride; // Metal rejects too-small strides
        bl.stride = stride;
        if (a.divisor > 0) {
            bl.stepFunction = MTLVertexStepFunctionPerInstance;
            bl.stepRate = (NSUInteger)a.divisor;
        } else {
            bl.stepFunction = MTLVertexStepFunctionPerVertex;
            bl.stepRate = 1;
        }
        vd.layouts[a.location] = bl;
    }
    if (vd != nil) d.vertexDescriptor = vd;

    for (int i = 0; i < color_count; ++i) {
        MTLPixelFormat pf = pixel_format_from_vk(color_formats[i]);
        if (pf == MTLPixelFormatInvalid) {
            // An unsupported attachment format is latched into the failure
            // path below (Metal rejects the PSO) — warn once per signature.
            MITHRIL_LOG_WARN("mtl", "color attachment %d VkFormat=%d has no Metal "
                             "format (program %u)", i, (int)color_formats[i], program);
            continue;
        }
        MTLRenderPipelineColorAttachmentDescriptor* ca = d.colorAttachments[i];
        ca.pixelFormat = pf;
        // GL blend enums arrive pre-translated by MetalFormat.mm; the
        // separate-alpha factors are applied per channel like Vulkan.
        if (blend_enabled) {
            ca.blendingEnabled = YES;
            ca.sourceRGBBlendFactor = blend_factor_from_gl(blend_src, false);
            ca.destinationRGBBlendFactor = blend_factor_from_gl(blend_dst, false);
            ca.sourceAlphaBlendFactor = blend_factor_from_gl(blend_src_alpha, true);
            ca.destinationAlphaBlendFactor = blend_factor_from_gl(blend_dst_alpha, true);
            ca.rgbBlendOperation = MTLBlendOperationAdd;   // GL eq not in signature
            ca.alphaBlendOperation = MTLBlendOperationAdd; // (frontend clamps to ADD)
        }
        ca.writeMask = color_write_mask_from_gl_bits(color_write_mask);
    }

    // Depth/stencil formats must mirror the pass (packed formats go to BOTH
    // slots — see apply_depth_stencil_formats).
    apply_depth_stencil_formats(d, depth_format);

    NSError* err = nil;
    id<MTLRenderPipelineState> rps = [b->device newRenderPipelineStateWithDescriptor:d
                                                                               error:&err];
    if (rps == nil) {
        MITHRIL_LOG_ERROR("mtl", "newRenderPipelineState failed (program %u): %s",
                          program, err ? err.localizedDescription.UTF8String : "unknown");
        failed_sig_tbl()[program].insert(sig);
        return nullptr;
    }

    MetalPipelineEntry* entry = new MetalPipelineEntry();
    entry->pipe.rps = rps;
    // Kept for bind-time consumers (MetalCommandStream applies Y-flip
    // compensation and depth-state gating off these flags).
    entry->pipe.isDefaultFBO = is_default_fbo != 0;
    entry->pipe.hasDepth = depth_format != VK_FORMAT_UNDEFINED;
    pr->pipes.emplace(sig, entry);
    return &entry->pipe;
}

/* ---- Compute pipeline ----------------------------------------------------- */

MetalPipeline* get_or_create_compute_pipeline(GLuint program,
                                              mithril::Program* prog) {
    Backend* b = backend();
    if (!b->initialized || b->deviceLost.load()) return nullptr;
    if (!prog || prog->computeSpirv.empty()) return nullptr;

    MetalProgramResources* pr = resources_for(program, prog);
    if (!pr) return nullptr;
    if (pr->computePipe && pr->computePipe->cps != nil) return pr->computePipe;

    if (!pr->reflected) {
        pr->bindings = vk::reflect_stage(prog->computeSpirv.data(),
                                         (int)prog->computeSpirv.size(),
                                         VK_SHADER_STAGE_COMPUTE_BIT);
        std::sort(pr->bindings.begin(), pr->bindings.end(),
                  [](const vk::DescriptorBinding& x, const vk::DescriptorBinding& y) {
                      if (x.set != y.set) return x.set < y.set;
                      return x.binding < y.binding;
                  });
        pr->reflected = true;
    }

    MetalCompiledFunction* cf = get_or_compile_stage(
        pr, prog->computeSpirv.data(), (int)prog->computeSpirv.size(),
        spv::ExecutionModelGLCompute);
    if (!cf || !cf->valid) return nullptr;

    NSError* err = nil;
    id<MTLComputePipelineState> cps =
        [b->device newComputePipelineStateWithFunction:cf->function error:&err];
    if (cps == nil) {
        MITHRIL_LOG_ERROR("mtl", "newComputePipelineState failed (program %u): %s",
                          program, err ? err.localizedDescription.UTF8String : "unknown");
        return nullptr;
    }

    MetalPipeline* pipe = new MetalPipeline();
    pipe->cps = cps;
    pipe->wgSize[0] = cf->wgSize[0];
    pipe->wgSize[1] = cf->wgSize[1];
    pipe->wgSize[2] = cf->wgSize[2];
    pr->computePipe = pipe;
    return pipe;
}

/* ---- Per-draw resource binding (graphics) ---------------------------------- */

void bind_program_descriptors(GLuint program) {
    Backend* b = backend();
    if (!b->initialized || program == 0) return;
    id<MTLRenderCommandEncoder> enc = current_encoder();
    if (enc == nil) return;

    MetalProgramResources* pr = resources_get(program);
    if (!pr) return;

    /* Empty-bindings programs FIRST: a program with no reflected descriptors
     * is normal (trivial shaders), and skipping straight to the satisfied
     * flag keeps the descriptors_bound() draw guard from silently dropping
     * every draw of that program. */
    if (pr->bindings.empty()) {
        set_descriptors_bound(true);
        return;
    }

    mithril::Program* prog = mithril::state_get_program(program);
    if (!prog) return;

    // Plans are built lazily on the first draw after a (re)link — the same
    // gating the Vulkan path uses, because link-time reflection can run
    // before the frontend has registered every uniform.
    if (pr->planLinkVersion != prog->linkVersion) build_ubo_plans(*pr, prog);

    const int slot = b->currentFrame;
    const uint64_t frameGen = b->frameGeneration;

    size_t uboPlanIdx = 0;
    for (const auto& db : pr->bindings) {
        if (db.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            // uboPlans was built from this same filtered list, so a counter
            // replaces any lookup; running short means reflection and plans
            // disagree (linkVersion gating makes that impossible).
            if (uboPlanIdx >= pr->uboPlans.size()) return;
            MetalUboPlan& plan = pr->uboPlans[uboPlanIdx++];

            id<MTLBuffer> buf = nil;
            NSUInteger off = 0;

            if (plan.appBlock) {
                MetalBuffer* mb = resolve_app_ubo(program, db, plan, prog, off);
                if (mb && mb->buf != nil) {
                    buf = mb->buf;
                } else {
                    // Either nothing bound at that GL point, or the offset
                    // was un-alignable. For the unaligned case stage the
                    // block bytes through the arena (256-aligned slices);
                    // for the unbound case zero-fill so the shader reads
                    // defined zeros instead of undefined memory.
                    const uint32_t blockSize =
                        prog->blockInfos[plan.glBlockIndex].dataSize
                            ? prog->blockInfos[plan.glBlockIndex].dataSize
                            : plan.size;
                    if (mb && mb->contents &&
                        off + blockSize <= mb->capacity) {
                        UboSliceDmt sl;
                        if (ubo_upload(slot, (const uint8_t*)mb->contents + off,
                                       blockSize, sl)) {
                            buf = sl.buf;
                            off = sl.offset;
                        }
                    }
                    if (buf == nil) {
                        static int warned = 0;
                        if (warned < 5) {
                            ++warned;
                            MITHRIL_LOG_WARN("mtl", "uniform block (program %u, "
                                             "binding %u) has no buffer bound at "
                                             "its GL binding point; using zeros",
                                             program, db.binding);
                        }
                        MetalBuffer* zb = zero_block_fallback(program, db.binding,
                                                               blockSize);
                        if (zb && zb->buf != nil) { buf = zb->buf; off = 0; }
                    }
                }
            } else {
                // Layer-synthesised block: pack, hash, maybe upload.
                UboSliceDmt sl;
                if (!resolve_ubo_slice(plan, slot, frameGen, sl)) return;
                buf = sl.buf;
                off = sl.offset;
            }
            bind_render_buffer(enc, pr, db, buf, off);

        } else if (db.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
            /* SSBO: same two-hop resolution as the Vulkan path — descriptor
             * binding -> storage block index (storageBlockInfos) -> GL
             * binding point (storageBlockBindings) -> bound buffer. The
             * index spaces are distinct; keying the binding-point lookup by
             * the raw descriptor binding (the historical bug) would silently
             * cross-wire blocks once a shader uses non-contiguous
             * layout(binding=) values. */
            GLuint blockIndex = db.binding; // fallback: legacy behaviour
            for (size_t bi = 0; bi < prog->storageBlockInfos.size(); ++bi) {
                if (prog->storageBlockInfos[bi].descriptorBinding == db.binding) {
                    blockIndex = (GLuint)bi;
                    break;
                }
            }
            GLuint point = blockIndex;
            auto sit = prog->storageBlockBindings.find(blockIndex);
            if (sit != prog->storageBlockBindings.end()) point = sit->second;

            id<MTLBuffer> buf = nil;
            NSUInteger off = 0;
            if (point < (GLuint)mithril::kMaxIndexedBindings) {
                const auto& sl = mithril::g_state->indexedBufferBindings
                                     [(int)mithril::IndexedBufferTarget::ShaderStorage][point];
                if (sl.name) {
                    MetalBuffer* mb = buffer_table_get(sl.name);
                    if (mb && mb->buf != nil) {
                        if (sl.hasExplicitRange) {
                            if ((NSUInteger)sl.offset < mb->capacity)
                                off = (NSUInteger)sl.offset;
                            if (off & (ubo_alignment() - 1)) {
                                // Unaligned range offset — stage through the
                                // arena like the UBO path (Metal's 256-byte
                                // offset alignment is a hard requirement).
                                const uint32_t sz = db.bufferSize ? db.bufferSize : 16u;
                                const NSUInteger take = std::min<NSUInteger>(sz, mb->capacity - off);
                                if (mb->contents && take > 0) {
                                    UboSliceDmt asl;
                                    if (ubo_upload(slot, (const uint8_t*)mb->contents + off,
                                                   take, asl)) {
                                        buf = asl.buf;
                                        off = asl.offset;
                                    }
                                }
                            } else {
                                buf = mb->buf;
                            }
                        } else {
                            buf = mb->buf;
                            off = 0;
                        }
                    }
                }
            }
            if (buf == nil) {
                static int warnedSsbo = 0;
                if (warnedSsbo < 5) {
                    ++warnedSsbo;
                    MITHRIL_LOG_WARN("mtl", "storage block (program %u, binding %u) "
                                     "has no buffer bound at GL binding point %u; "
                                     "using zeros", program, db.binding, point);
                }
                MetalBuffer* zb = zero_block_fallback(program, db.binding, db.bufferSize);
                if (zb && zb->buf != nil) { buf = zb->buf; off = 0; }
            }
            bind_render_buffer(enc, pr, db, buf, off);

        } else if (db.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
            /* descriptor binding -> GL texture unit via glUniform1i
             * (samplerUnitForBinding); the binding number itself is only a
             * legacy fallback. The texture is then fetched from the unit's
             * slot matching the shader-declared sampler TARGET (samplerCube
             * -> CubeMap slot), so a stale 2D bind on the unit cannot feed a
             * cubemap. */
            GLint unit = -1;
            auto uit = prog->samplerUnitForBinding.find(db.binding);
            // -1 = "unset" per State.h — fall back to binding-as-unit.
            if (uit != prog->samplerUnitForBinding.end() && uit->second >= 0)
                unit = uit->second;
            else
                unit = (GLint)db.binding;

            GLuint tex_id = 0;
            if (unit >= 0 && unit < mithril::kMaxTextureUnits) {
                mithril::TextureTarget tt = mithril::textureTargetFromGL(db.samplerTarget);
                if (tt != mithril::TextureTarget::Count) {
                    tex_id = mithril::g_state->boundTextureForUnit((GLuint)unit, tt);
                }
                if (tex_id == 0 && tt != mithril::TextureTarget::_2D) {
                    tex_id = mithril::g_state->boundTextureForUnit((GLuint)unit);
                }
            }

            // GL_TEXTURE_BUFFER（samplerBuffer）：纹理句柄是 MTLBuffer 派生的
            // texture_buffer 视图，不是 texture_table 里的采样纹理；MSL 侧
            // texture_buffer 不消费 sampler，无需采样器。共享 reflect_stage 把
            // Dim=Buffer 标成 UNIFORM_TEXEL_BUFFER，Metal 不区分 descriptor
            // 类型 —— 统一在这一支绑定。
            if (db.samplerTarget == GL_TEXTURE_BUFFER && tex_id != 0) {
                id<MTLTexture> btex = get_or_create_buffer_texture(tex_id);
                if (btex != nil) {
                    const uint32_t btCount =
                        db.descriptorCount ? db.descriptorCount : 1u;
                    for (uint32_t e = 0; e < btCount; ++e) {
                        const NSUInteger idx = (NSUInteger)(db.binding + e);
                        if (idx > kMaxMetalArgIndex) break;
                        if (db.stageMask & VK_SHADER_STAGE_VERTEX_BIT)
                            [enc setVertexTexture:btex atIndex:idx];
                        if (db.stageMask & VK_SHADER_STAGE_FRAGMENT_BIT)
                            [enc setFragmentTexture:btex atIndex:idx];
                    }
                    continue;
                }
                // 视图不可用（格式不支持/源 buffer 空）：走 1x1 黑纹理兜底，
                // 绝不让 texture 槽位保持未绑定。
            }

            MetalTexture* mt = tex_id ? texture_table_get(tex_id) : nullptr;
            MetalTexture* fallbackTex = default_texture_tex();
            id<MTLTexture> tex = (mt && mt->tex != nil)
                ? mt->tex
                : (fallbackTex ? fallbackTex->tex : nil); // 1x1 black fallback
            id<MTLSamplerState> smp = nil;
            if (tex_id) {
                // Sampler params come from the GL texture object so the
                // pixel-art NEAREST / atlas CLAMP_TO_EDGE cases survive.
                mithril::Texture* gtex = mithril::state_get_texture(tex_id);
                GLenum minF  = gtex ? (GLenum)gtex->minFilter : GL_NEAREST_MIPMAP_LINEAR;
                GLenum magF  = gtex ? (GLenum)gtex->magFilter : GL_LINEAR;
                GLenum wrapS = gtex ? (GLenum)gtex->wrapS : GL_REPEAT;
                GLenum wrapT = gtex ? (GLenum)gtex->wrapT : GL_REPEAT;
                GLenum wrapR = gtex ? (GLenum)gtex->wrapR : GL_REPEAT;
                if (MetalSampler* ms = dmt_internal_get_or_create_sampler(
                        tex_id, minF, magF, wrapS, wrapT, wrapR, nullptr)) {
                    smp = ms->smp;
                }
            }
            if (smp == nil) {
                if (MetalSampler* ds = default_texture_sampler()) smp = ds->smp;
            }
            if (tex == nil || smp == nil) continue;

            // Array bindings occupy consecutive argument slots; element 0 is
            // the only one with real data (matching the Vulkan path).
            const uint32_t count = db.descriptorCount ? db.descriptorCount : 1u;
            for (uint32_t e = 0; e < count; ++e) {
                const NSUInteger idx = (NSUInteger)(db.binding + e);
                if (idx > kMaxMetalArgIndex) break;
                if (db.stageMask & VK_SHADER_STAGE_VERTEX_BIT) {
                    [enc setVertexTexture:tex atIndex:idx];
                    [enc setVertexSamplerState:smp atIndex:idx];
                }
                if (db.stageMask & VK_SHADER_STAGE_FRAGMENT_BIT) {
                    [enc setFragmentTexture:tex atIndex:idx];
                    [enc setFragmentSamplerState:smp atIndex:idx];
                }
            }
        }
        // VK_DESCRIPTOR_TYPE_STORAGE_IMAGE (glBindImageTexture units) is not
        // produced by the shared reflect_stage yet; when Reflect.cpp grows
        // that branch this switch gains it (imageTexUnits -> texture table).
    }

    set_descriptors_bound(true);
}

/* ---- Compute dispatch ------------------------------------------------------ */

namespace {

/* Compute twin of bind_program_descriptors: same resolution logic, but the
 * compute encoder has a single argument table (no vertex/fragment split). */
void bind_compute_descriptors(GLuint program, mithril::Program* prog,
                              MetalProgramResources* pr,
                              id<MTLComputeCommandEncoder> cce) {
    Backend* b = backend();
    const int slot = b->currentFrame;
    const uint64_t frameGen = b->frameGeneration;

    if (pr->planLinkVersion != prog->linkVersion) build_ubo_plans(*pr, prog);

    size_t uboPlanIdx = 0;
    for (const auto& db : pr->bindings) {
        if (db.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            if (uboPlanIdx >= pr->uboPlans.size()) return;
            MetalUboPlan& plan = pr->uboPlans[uboPlanIdx++];
            id<MTLBuffer> buf = nil;
            NSUInteger off = 0;
            if (plan.appBlock) {
                MetalBuffer* mb = resolve_app_ubo(program, db, plan, prog, off);
                if (mb && mb->buf != nil && !(off & (ubo_alignment() - 1))) {
                    buf = mb->buf;
                } else {
                    const uint32_t blockSize =
                        prog->blockInfos[plan.glBlockIndex].dataSize
                            ? prog->blockInfos[plan.glBlockIndex].dataSize
                            : plan.size;
                    if (mb && mb->contents && !(off & (ubo_alignment() - 1)) &&
                        off + blockSize <= mb->capacity) {
                        UboSliceDmt sl;
                        if (ubo_upload(slot, (const uint8_t*)mb->contents + off,
                                       blockSize, sl)) {
                            buf = sl.buf;
                            off = sl.offset;
                        }
                    }
                    if (buf == nil) {
                        MetalBuffer* zb = zero_block_fallback(program, db.binding,
                                                               blockSize);
                        if (zb && zb->buf != nil) { buf = zb->buf; off = 0; }
                    }
                }
            } else {
                UboSliceDmt sl;
                if (!resolve_ubo_slice(plan, slot, frameGen, sl)) return;
                buf = sl.buf;
                off = sl.offset;
            }
            if (buf == nil) continue;
            auto it = pr->bufIdxCs.find(db.binding);
            if (it != pr->bufIdxCs.end())
                [cce setBuffer:buf offset:off atIndex:(NSUInteger)it->second];

        } else if (db.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
            GLuint blockIndex = db.binding;
            for (size_t bi = 0; bi < prog->storageBlockInfos.size(); ++bi) {
                if (prog->storageBlockInfos[bi].descriptorBinding == db.binding) {
                    blockIndex = (GLuint)bi;
                    break;
                }
            }
            GLuint point = blockIndex;
            auto sit = prog->storageBlockBindings.find(blockIndex);
            if (sit != prog->storageBlockBindings.end()) point = sit->second;

            id<MTLBuffer> buf = nil;
            NSUInteger off = 0;
            if (point < (GLuint)mithril::kMaxIndexedBindings) {
                const auto& sl = mithril::g_state->indexedBufferBindings
                                     [(int)mithril::IndexedBufferTarget::ShaderStorage][point];
                if (sl.name) {
                    MetalBuffer* mb = buffer_table_get(sl.name);
                    if (mb && mb->buf != nil) {
                        if (sl.hasExplicitRange &&
                            (NSUInteger)sl.offset < mb->capacity &&
                            !(((NSUInteger)sl.offset) & (ubo_alignment() - 1))) {
                            off = (NSUInteger)sl.offset;
                            buf = mb->buf;
                        } else if (!sl.hasExplicitRange) {
                            buf = mb->buf;
                            off = 0;
                        }
                    }
                }
            }
            if (buf == nil) {
                MetalBuffer* zb = zero_block_fallback(program, db.binding, db.bufferSize);
                if (zb && zb->buf != nil) { buf = zb->buf; off = 0; }
                else continue;
            }
            auto it = pr->bufIdxCs.find(db.binding);
            if (it != pr->bufIdxCs.end())
                [cce setBuffer:buf offset:off atIndex:(NSUInteger)it->second];

        } else if (db.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
            GLint unit = -1;
            auto uit = prog->samplerUnitForBinding.find(db.binding);
            // -1 = "unset" per State.h — fall back to binding-as-unit.
            if (uit != prog->samplerUnitForBinding.end() && uit->second >= 0)
                unit = uit->second;
            else
                unit = (GLint)db.binding;

            GLuint tex_id = 0;
            if (unit >= 0 && unit < mithril::kMaxTextureUnits) {
                mithril::TextureTarget tt = mithril::textureTargetFromGL(db.samplerTarget);
                if (tt != mithril::TextureTarget::Count) {
                    tex_id = mithril::g_state->boundTextureForUnit((GLuint)unit, tt);
                }
                if (tex_id == 0 && tt != mithril::TextureTarget::_2D) {
                    tex_id = mithril::g_state->boundTextureForUnit((GLuint)unit);
                }
            }
            MetalTexture* mt = tex_id ? texture_table_get(tex_id) : nullptr;
            MetalTexture* fallbackTex = default_texture_tex();
            id<MTLTexture> tex = (mt && mt->tex != nil)
                ? mt->tex : (fallbackTex ? fallbackTex->tex : nil);
            id<MTLSamplerState> smp = nil;
            if (tex_id) {
                mithril::Texture* gtex = mithril::state_get_texture(tex_id);
                GLenum minF  = gtex ? (GLenum)gtex->minFilter : GL_NEAREST_MIPMAP_LINEAR;
                GLenum magF  = gtex ? (GLenum)gtex->magFilter : GL_LINEAR;
                GLenum wrapS = gtex ? (GLenum)gtex->wrapS : GL_REPEAT;
                GLenum wrapT = gtex ? (GLenum)gtex->wrapT : GL_REPEAT;
                GLenum wrapR = gtex ? (GLenum)gtex->wrapR : GL_REPEAT;
                if (MetalSampler* ms = dmt_internal_get_or_create_sampler(
                        tex_id, minF, magF, wrapS, wrapT, wrapR, nullptr)) {
                    smp = ms->smp;
                }
            }
            if (smp == nil) {
                if (MetalSampler* ds = default_texture_sampler()) smp = ds->smp;
            }
            if (tex == nil || smp == nil) continue;

            const uint32_t count = db.descriptorCount ? db.descriptorCount : 1u;
            for (uint32_t e = 0; e < count; ++e) {
                const NSUInteger idx = (NSUInteger)(db.binding + e);
                if (idx > kMaxMetalArgIndex) break;
                [cce setTexture:tex atIndex:idx];
                [cce setSamplerState:smp atIndex:idx];
            }
        }
    }
}

} // namespace

void dispatch_compute(GLuint program, mithril::Program* prog,
                      uint32_t gx, uint32_t gy, uint32_t gz) {
    Backend* b = backend();
    if (!b->initialized || b->deviceLost.load() || !prog) return;
    if (gx == 0 || gy == 0 || gz == 0) return;

    MetalPipeline* pipe = get_or_create_compute_pipeline(program, prog);
    if (!pipe || pipe->cps == nil) return;

    // ensure_compute_encoder ends any active render pass first (Metal, like
    // Vulkan, forbids a compute encoder inside a render pass).
    id<MTLComputeCommandEncoder> cce = ensure_compute_encoder();
    if (cce == nil) return;

    [cce setComputePipelineState:pipe->cps];

    MetalProgramResources* pr = resources_get(program);
    if (pr && !pr->bindings.empty()) {
        bind_compute_descriptors(program, prog, pr, cce);
    }

    const MTLSize tg = MTLSizeMake(gx, gy, gz);
    MTLSize pt = MTLSizeMake(pipe->wgSize[0] ? pipe->wgSize[0] : 1,
                             pipe->wgSize[1] ? pipe->wgSize[1] : 1,
                             pipe->wgSize[2] ? pipe->wgSize[2] : 1);
    [cce dispatchThreadgroups:tg threadsPerThreadgroup:pt];
}

void dispatch_compute_indirect(GLuint program, mithril::Program* prog,
                               MetalBuffer* indirect, NSUInteger offset) {
    Backend* b = backend();
    if (!b->initialized || b->deviceLost.load() || !prog) return;
    if (!indirect || indirect->buf == nil) return;

    MetalPipeline* pipe = get_or_create_compute_pipeline(program, prog);
    if (!pipe || pipe->cps == nil) return;

    id<MTLComputeCommandEncoder> cce = ensure_compute_encoder();
    if (cce == nil) return;

    [cce setComputePipelineState:pipe->cps];

    MetalProgramResources* pr = resources_get(program);
    if (pr && !pr->bindings.empty()) {
        bind_compute_descriptors(program, prog, pr, cce);
    }

    // GL's dispatch-indirect payload is a {x,y,z} uint32 triple — identical
    // to Metal's indirect dispatch layout, so it passes through unchanged.
    MTLSize pt = MTLSizeMake(pipe->wgSize[0] ? pipe->wgSize[0] : 1,
                             pipe->wgSize[1] ? pipe->wgSize[1] : 1,
                             pipe->wgSize[2] ? pipe->wgSize[2] : 1);
    [cce dispatchThreadgroupsWithIndirectBuffer:indirect->buf
                              indirectBufferOffset:offset
                                 threadsPerThreadgroup:pt];
}

/* ---- Built-in shaders (MSL sources) ----------------------------------------
 *
 * These shaders render under the backend's Vulkan-convention rasterizer
 * (negative-height viewport => NDC +y maps DOWN, NDC(-1,-1) = top-left pixel,
 * depth [0,1]). Keeping them on the same convention as the translated app
 * shaders means MetalCommandStream needs no special-casing to drive them. */

/* Clear geometry: a fullscreen triangle generated from vertex_id — no vertex
 * buffers, so the clear pipeline needs no MTLVertexDescriptor and cannot
 * collide with a bound app VAO. Oversized corners (3,-1)/(-1,3) make one
 * triangle cover the whole viewport regardless of its size. */
static const char* kClearVertexPlainMSL = R"MSL(
#include <metal_stdlib>
using namespace metal;

// Variant used when the clear mask has no GL_DEPTH_BUFFER_BIT: zero
// parameters, so the encoder's conditional setVertexBytes is never missed.
struct ClearVaryings {
    float4 position [[position]];
};

vertex ClearVaryings mithril_clear_vs(uint vid [[vertex_id]]) {
    // vid 0 -> (-1,-1) top-left, vid 1 -> (3,-1) far right, vid 2 -> (-1,3) far bottom
    float2 p = float2(vid == 1 ? 3.0 : -1.0, vid == 2 ? 3.0 : -1.0);
    ClearVaryings o;
    o.position = float4(p, 0.0, 1.0);
    return o;
}
)MSL";

static const char* kClearVertexDepthMSL = R"MSL(
#include <metal_stdlib>
using namespace metal;

// Depth-clear variant: run_clear_draw passes the clear depth as a 4-byte
// vertex argument at [[buffer(15)]] (an index the app's vertex attributes
// 0..15 and remapped VS UBOs 16+ never touch inside THIS pipeline, which has
// no vertex descriptor). Writing it into position.z lets the rasterizer lay
// it into the depth attachment through the clear DSS — deliberately NOT a
// fragment [[depth]] output, which Metal's validation layer rejects on
// pipelines without a depth attachment.
struct ClearVsParams {
    float depth;
};

struct ClearVaryings {
    float4 position [[position]];
};

vertex ClearVaryings mithril_clear_vs_depth(uint vid [[vertex_id]],
                                            constant ClearVsParams& p [[buffer(15)]]) {
    float2 pos = float2(vid == 1 ? 3.0 : -1.0, vid == 2 ? 3.0 : -1.0);
    ClearVaryings o;
    o.position = float4(pos, p.depth, 1.0);
    return o;
}
)MSL";

static const char* kClearFragmentColorMSL = R"MSL(
#include <metal_stdlib>
using namespace metal;

// Color-clear variant: a 16-byte float4 argument at fragment [[buffer(0)]]
// (run_clear_draw's setFragmentBytes). Declares NO depth output — depth
// clears travel through the vertex shader's z — so the function is legal on
// pipelines with or without a depth attachment.
struct ClearFsParams {
    float4 color;
};

fragment float4 mithril_clear_fs_color(constant ClearFsParams& p [[buffer(0)]]) {
    return p.color;
}
)MSL";

static const char* kClearFragmentNoneMSL = R"MSL(
#include <metal_stdlib>
using namespace metal;

// Depth/stencil-only clears: no arguments, no outputs. Stencil writes happen
// through the clear DSS's Replace op driven by the encoder's stencil
// reference value; depth through the vertex shader's z. The pipeline still
// declares the pass's color attachment formats (writeMask None), which is
// legal for a fragment function without color outputs.
fragment void mithril_clear_fs_none() {
}
)MSL";

static const char* kBlitVertexMSL = R"MSL(
#include <metal_stdlib>
using namespace metal;

// Maps a destination sub-rect onto the viewport's NDC (Vulkan convention:
// y-down) and carries the matching source texel rect as interpolants.
// Per-vertex if/else (not array indexing) keeps older compilers honest.
struct BlitParams {
    float4 srcRect;   // srcX0, srcY0, srcX1, srcY1 (pixels, top-left origin)
    float4 dstRect;   // dstX0, dstY0, dstX1, dstY1 (pixels, top-left origin)
    float2 srcSize;   // source texture dimensions
    float2 dstSize;   // destination texture dimensions
};

struct BlitVaryings {
    float4 position [[position]];
    float2 uv;        // source texel coordinates in pixels
};

vertex BlitVaryings mithril_blit_vs(uint vid [[vertex_id]],
                                    constant BlitParams& p [[buffer(0)]]) {
    float2 dstTL = p.dstRect.xy;
    float2 dstBR = p.dstRect.zw;

    // NDC under the negative-height viewport: x -1..+1 left->right,
    // y -1 = TOP row, +1 = BOTTOM row.
    float2 nTL = float2(dstTL.x / p.dstSize.x * 2.0 - 1.0,
                        dstTL.y / p.dstSize.y * 2.0 - 1.0);
    float2 nBR = float2(dstBR.x / p.dstSize.x * 2.0 - 1.0,
                        dstBR.y / p.dstSize.y * 2.0 - 1.0);

    float2 srcTL = p.srcRect.xy;
    float2 srcBR = p.srcRect.zw;

    BlitVaryings o;
    if (vid == 0) {          // destination top-left  <-> source top-left
        o.position = float4(nTL.x, nTL.y, 0.0, 1.0);
        o.uv = srcTL;
    } else if (vid == 1) {   // destination top-right <-> source top-right
        o.position = float4(nBR.x, nTL.y, 0.0, 1.0);
        o.uv = float2(srcBR.x, srcTL.y);
    } else {                 // destination bottom-left <-> source bottom-left
        o.position = float4(nTL.x, nBR.y, 0.0, 1.0);
        o.uv = float2(srcTL.x, srcBR.y);
    }
    return o;
}
)MSL";

static const char* kBlitFragmentMSL = R"MSL(
#include <metal_stdlib>
using namespace metal;

// uv arrives in texel space; normalize by the source size. Sampling handles
// scaled (LINEAR) and nearest (NEAREST) filtering through the sampler state
// handed out by blit_sampler(). sRGB sources decode automatically because
// the texture keeps its sRGB pixel format.
struct BlitParams {
    float4 srcRect;
    float4 dstRect;
    float2 srcSize;
    float2 dstSize;
};

struct BlitVaryings {
    float4 position [[position]];
    float2 uv;
};

fragment float4 mithril_blit_fs(BlitVaryings in [[stage_in]],
                                texture2d<float> srcTex [[texture(0)]],
                                sampler srcSampler [[sampler(0)]],
                                constant BlitParams& p [[buffer(0)]]) {
    (void)p;
    float2 uv = in.uv / p.srcSize;
    return float4(srcTex.sample(srcSampler, uv));
}
)MSL";

namespace {

// Compile the four builtin functions once per process (re-attempted after a
// device purge only if they were never valid).
bool ensure_builtin_functions() {
    BuiltinCache& c = builtin_cache();
    if (c.functionsValid) return true;
    if (c.functionsAttempted) return false;
    c.functionsAttempted = true;

    Backend* b = backend();
    if (!b->initialized) return false;

    struct Stage {
        const char* src;
        id<MTLFunction> __strong *out;
        const char* name;
    };
    Stage stages[6] = {
        {kClearVertexPlainMSL,   &c.clearVSPlain, "mithril_clear_vs"},
        {kClearVertexDepthMSL,   &c.clearVSDepth, "mithril_clear_vs_depth"},
        {kClearFragmentColorMSL, &c.clearFSColor, "mithril_clear_fs_color"},
        {kClearFragmentNoneMSL,  &c.clearFSNone,  "mithril_clear_fs_none"},
        {kBlitVertexMSL,         &c.blitVS,       "mithril_blit_vs"},
        {kBlitFragmentMSL,       &c.blitFS,       "mithril_blit_fs"},
    };

    MTLCompileOptions* copts = [[MTLCompileOptions alloc] init];
    copts.languageVersion = (MTLLanguageVersion)msl_version_for_device();
    copts.fastMathEnabled = NO;

    for (auto& s : stages) {
        NSError* err = nil;
        id<MTLLibrary> lib = [b->device
            newLibraryWithSource:[NSString stringWithUTF8String:s.src]
                         options:copts error:&err];
        if (lib == nil) {
            MITHRIL_LOG_ERROR("mtl", "builtin shader '%s' failed to compile: %s",
                              s.name, err ? err.localizedDescription.UTF8String
                                          : "unknown");
            return false;
        }
        *s.out = [lib newFunctionWithName:[NSString stringWithUTF8String:s.name]];
        if (*s.out == nil) {
            MITHRIL_LOG_ERROR("mtl", "builtin function '%s' not found in library",
                              s.name);
            return false;
        }
    }
    c.functionsValid = true;
    return true;
}

uint64_t clear_pipeline_signature(const VkFormat* color_formats, int color_count,
                                  VkFormat depth_format, GLbitfield mask) {
    uint64_t h = kFnvBasis;
    h = fnv1a_u64((uint64_t)color_count, h);
    for (int i = 0; i < color_count; ++i) h = fnv1a_u64((uint64_t)color_formats[i], h);
    h = fnv1a_u64((uint64_t)depth_format, h);
    h = fnv1a_u64((uint64_t)mask, h);
    // MSAA：与主管线同一理由 —— rasterSampleCount 参与缓存键（PSO 采样数必须
    // 匹配 pass 附件纹理的 sampleCount）。
    h = fnv1a_u64((uint64_t)mithril::draw_fbo_sample_count(), h);
    return h;
}

} // namespace

/* ---- Built-in clear machinery ---------------------------------------------- */

MetalPipeline* get_clear_pipeline(const VkFormat* color_formats, int color_count,
                                  VkFormat depth_format, GLbitfield mask) {
    Backend* b = backend();
    if (!b->initialized || b->deviceLost.load()) return nullptr;
    if (!ensure_builtin_functions()) return nullptr;
    if (color_count < 0) color_count = 0;
    if (color_count > kMaxColorAttachments) color_count = kMaxColorAttachments;

    const uint64_t sig = clear_pipeline_signature(color_formats, color_count,
                                                  depth_format, mask);
    BuiltinCache& c = builtin_cache();
    auto it = c.clearPipes.find(sig);
    if (it != c.clearPipes.end()) return &it->second->pipe;

    MTLRenderPipelineDescriptor* d = [[MTLRenderPipelineDescriptor alloc] init];
    // Variant selection mirrors run_clear_draw's CONDITIONAL argument binds:
    // the encoder only sets the fragment float4 under GL_COLOR_BUFFER_BIT and
    // the vertex float under GL_DEPTH_BUFFER_BIT, so a function that declares
    // an argument the encoder never sets would trip Metal's missing-binding
    // validation. The mask is part of the cache signature, so this stays
    // consistent across lookups.
    d.vertexFunction = (mask & GL_DEPTH_BUFFER_BIT) ? c.clearVSDepth : c.clearVSPlain;
    d.fragmentFunction = (mask & GL_COLOR_BUFFER_BIT) ? c.clearFSColor : c.clearFSNone;
    // MSAA：clear 也是 pass 内的 draw，PSO 采样数必须匹配附件纹理（同上）。
    d.rasterSampleCount = (NSUInteger)mithril::draw_fbo_sample_count();

    // Every pass color attachment keeps its format so the PSO matches the
    // render pass; a clear without GL_COLOR_BUFFER_BIT simply masks the
    // writes off instead of dropping attachments (Metal wants the pipeline's
    // declared attachment formats to line up with the pass).
    for (int i = 0; i < color_count; ++i) {
        MTLPixelFormat pf = pixel_format_from_vk(color_formats[i]);
        if (pf == MTLPixelFormatInvalid) continue;
        MTLRenderPipelineColorAttachmentDescriptor* ca = d.colorAttachments[i];
        ca.pixelFormat = pf;
        ca.writeMask = (mask & GL_COLOR_BUFFER_BIT)
            ? MTLColorWriteMaskAll : MTLColorWriteMaskNone;
    }

    // Depth/stencil formats always mirror the pass (packed formats on BOTH
    // slots — the stencil Replace op needs stencilAttachmentPixelFormat, and
    // Metal validates pipeline-vs-pass attachment equality). Actual writes
    // are gated by clear_depth_stencil_state + the VS variant.
    apply_depth_stencil_formats(d, depth_format);

    NSError* err = nil;
    id<MTLRenderPipelineState> rps = [b->device newRenderPipelineStateWithDescriptor:d
                                                                               error:&err];
    if (rps == nil) {
        MITHRIL_LOG_ERROR("mtl", "clear pipeline creation failed (mask=0x%x): %s",
                          (unsigned)mask,
                          err ? err.localizedDescription.UTF8String : "unknown");
        return nullptr;
    }

    MetalPipelineEntry* entry = new MetalPipelineEntry();
    entry->pipe.rps = rps;
    entry->pipe.hasDepth = depth_format != VK_FORMAT_UNDEFINED;
    c.clearPipes.emplace(sig, entry);
    return &entry->pipe;
}

id<MTLDepthStencilState> clear_depth_stencil_state(bool depth, bool stencil) {
    Backend* b = backend();
    if (!b->initialized) return nil;

    BuiltinCache& c = builtin_cache();
    const int idx = (depth ? 1 : 0) | (stencil ? 2 : 0);
    if (c.clearDSS[idx] != nil) return c.clearDSS[idx];

    MTLDepthStencilDescriptor* d = [[MTLDepthStencilDescriptor alloc] init];
    // Always-pass + (optional) depth write lets the clear triangle's
    // interpolated z (the VS's clear-depth parameter) land regardless of the
    // app's depth test, which a glClear must ignore.
    d.depthCompareFunction = MTLCompareFunctionAlways;
    d.depthWriteEnabled = depth;

    if (stencil) {
        // Replace-on-everything writes the encoder's stencil reference
        // value (the clear value) across everything the quad covers.
        MTLStencilDescriptor* s = [[MTLStencilDescriptor alloc] init];
        s.stencilCompareFunction = MTLCompareFunctionAlways;
        s.stencilFailureOperation = MTLStencilOperationReplace;
        s.depthFailureOperation = MTLStencilOperationReplace;
        s.depthStencilPassOperation = MTLStencilOperationReplace;
        s.readMask = 0xFFFFFFFFu;
        s.writeMask = 0xFFFFFFFFu;
        d.frontFaceStencil = s;
        d.backFaceStencil = s;
    }

    c.clearDSS[idx] = [b->device newDepthStencilStateWithDescriptor:d];
    return c.clearDSS[idx];
}

/* ---- Built-in blit machinery ------------------------------------------------ */

id<MTLRenderPipelineState> get_blit_pipeline(MTLPixelFormat dst_format) {
    Backend* b = backend();
    if (!b->initialized || dst_format == MTLPixelFormatInvalid) return nil;
    if (!ensure_builtin_functions()) return nil;

    BuiltinCache& c = builtin_cache();
    auto it = c.blitPipes.find((uint32_t)dst_format);
    if (it != c.blitPipes.end()) return it->second;

    MTLRenderPipelineDescriptor* d = [[MTLRenderPipelineDescriptor alloc] init];
    d.vertexFunction = c.blitVS;
    d.fragmentFunction = c.blitFS;
    d.colorAttachments[0].pixelFormat = dst_format;

    NSError* err = nil;
    id<MTLRenderPipelineState> rps = [b->device newRenderPipelineStateWithDescriptor:d
                                                                               error:&err];
    if (rps == nil) {
        MITHRIL_LOG_ERROR("mtl", "blit pipeline creation failed (format=%u): %s",
                          (unsigned)dst_format,
                          err ? err.localizedDescription.UTF8String : "unknown");
        return nil;
    }
    c.blitPipes.emplace((uint32_t)dst_format, rps);
    return rps;
}

id<MTLSamplerState> blit_sampler(bool linear) {
    Backend* b = backend();
    if (!b->initialized) return nil;

    BuiltinCache& c = builtin_cache();
    const int idx = linear ? 1 : 0;
    if (c.blitSamplers[idx] != nil) return c.blitSamplers[idx];

    MTLSamplerDescriptor* d = [[MTLSamplerDescriptor alloc] init];
    d.minFilter = linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    d.magFilter = d.minFilter;
    d.mipFilter = MTLSamplerMipFilterNotMipmapped; // single-level blits
    d.sAddressMode = MTLSamplerAddressModeClampToEdge;
    d.tAddressMode = MTLSamplerAddressModeClampToEdge;
    d.rAddressMode = MTLSamplerAddressModeClampToEdge;

    c.blitSamplers[idx] = [b->device newSamplerStateWithDescriptor:d];
    return c.blitSamplers[idx];
}

void run_scaled_blit(MetalTexture* src, MetalTexture* dst, const BlitParams& p,
                     bool linearFilter) {
    Backend* b = backend();
    if (!b->initialized || !src || !dst || src->tex == nil || dst->tex == nil) return;
    if (!ensure_command_buffer()) return;

    // Metal allows exactly one active encoder: end the render pass before
    // running the blit's dedicated one. Callers re-begin their pass.
    end_render_pass();

    id<MTLRenderPipelineState> rps = get_blit_pipeline(dst->tex.pixelFormat);
    id<MTLSamplerState> smp = blit_sampler(linearFilter);
    if (rps == nil || smp == nil) return;

    // Uniform payload must match the MSL BlitParams layout exactly:
    // float4 srcRect, float4 dstRect, float2 srcSize, float2 dstSize.
    struct BlitUniforms {
        float srcX0, srcY0, srcX1, srcY1;
        float dstX0, dstY0, dstX1, dstY1;
        float srcW, srcH, dstW, dstH;
    } u;
    u.srcX0 = p.srcX0; u.srcY0 = p.srcY0; u.srcX1 = p.srcX1; u.srcY1 = p.srcY1;
    u.dstX0 = p.dstX0; u.dstY0 = p.dstY0; u.dstX1 = p.dstX1; u.dstY1 = p.dstY1;
    u.srcW = p.srcW;   u.srcH = p.srcH;   u.dstW = p.dstW;   u.dstH = p.dstH;

    // Per-blit arena slice: a persistent shared buffer would let a second
    // blit in the same frame overwrite the parameters before the GPU has
    // executed the first one (CPU writes land before commit, GPU reads after).
    UboSliceDmt sl;
    if (!ubo_upload(b->currentFrame, &u, sizeof(u), sl)) return;

    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = dst->tex;
    rp.colorAttachments[0].loadAction = MTLLoadActionLoad;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLRenderCommandEncoder> enc = [b->cmd renderCommandEncoderWithDescriptor:rp];
    [enc setRenderPipelineState:rps];
    // Negative-height viewport = the backend-wide Vulkan NDC convention.
    const MTLViewport vp = {0.0, (double)dst->tex.height,
                            (double)dst->tex.width, -(double)dst->tex.height,
                            0.0, 1.0};
    [enc setViewport:vp];
    // Both stages read BlitParams at [[buffer(0)]] (the VS for the rect
    // mapping, the FS for srcSize) — bind both or the FS trips Metal's
    // missing-binding validation.
    [enc setVertexBuffer:sl.buf offset:sl.offset atIndex:0];
    [enc setFragmentBuffer:sl.buf offset:sl.offset atIndex:0];
    [enc setFragmentTexture:src->tex atIndex:0];
    [enc setFragmentSamplerState:smp atIndex:0];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [enc endEncoding];
    note_non_render_commands();
}

} // namespace dmt
} // namespace mithril

#endif // __APPLE__
