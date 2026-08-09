// Mithril-Wrapper - MG_Backend/DirectVulkan/Pipeline.h
// VkShaderModule creation (from SPIR-V) + VkGraphicsPipeline caching keyed by
// a hash signature built from (program, vertex format, attachment formats,
// blend state, primitive mode). Implements backend_get_or_create_pipeline()
// and backend_delete_program_resources() declared in MG_Backend/Backend.h.
#ifndef MITHRIL_DIRECTVULKAN_PIPELINE_H
#define MITHRIL_DIRECTVULKAN_PIPELINE_H

#include <vulkan/vulkan.h>
#include <GL/gl.h>

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "DescriptorSet.h"
#include "Device.h"       // kMaxFramesInFlight
#include "Std140.h"       // Std140Slot (dependency-free; used by UboMemberPlan)
#include "../Backend.h"   // defines ::MGVertexAttrib (extern "C", global scope)

namespace mithril {

// Forward declaration only — Pipeline.h must not pull in MG_State/State.h
// (State.h includes the GL headers and the backend headers, which would make
// the include graph circular). UboMemberPlan just stores a pointer.
struct Uniform;

namespace vk {

/* ---- Fixed-up uniform packing plan (per UBO binding) ----
 *
 * Built ONCE per link (see ProgramResources::planLinkVersion) and consumed by
 * every draw afterwards. The point is to move all the string work off the
 * per-draw path: bind_program_descriptors() used to do one
 * std::unordered_map<std::string, Uniform> lookup — i.e. a full string hash
 * plus a bucket walk plus a memcmp — for EVERY member of EVERY UBO on EVERY
 * draw. Sodium issues several thousand draws a frame, so that is tens of
 * thousands of string hashes per frame for data that never moves.
 *
 * Resolving to `Uniform*` up front is safe because std::unordered_map
 * guarantees that pointers and references to elements stay valid across
 * inserts and rehashes; only erase() invalidates them. Program::uniforms is
 * only ever cleared wholesale by glLinkProgram (Program.cpp:340), which also
 * bumps linkVersion (Program.cpp:359) AND calls backend_delete_program_
 * resources() — so a stale plan cannot survive a relink.
 */
struct UboMemberPlan {
    Std140Slot slot;                     // std140 placement inside the block
    const mithril::Uniform* src = nullptr;  // resolved once; null = never registered
};

// What one VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER* binding needs, resolved at link
// time. Exactly one of the three sourcing modes applies.
struct UboBindingPlan {
    uint32_t binding = 0;
    uint32_t size = 16;      // std140 block size in bytes

    // (a) Application-declared block fed by glBindBufferBase/Range. The
    //     descriptor points straight at the app's VkBuffer; nothing is packed.
    bool     appBlock = false;
    uint32_t glBlockIndex = 0;

    // (b) One loose uniform whose value is the whole block (glslang emits a
    //     UBO per loose uniform when it does not aggregate). Raw memcpy.
    const mithril::Uniform* directSrc = nullptr;

    // (c) Aggregated $Global / mithril_GlobalBlock: pack each member into its
    //     std140 slot. Used when (a) and (b) do not apply, and also when (b)
    //     matched a uniform that has no value yet — matching the old
    //     find()/value.empty() fallthrough exactly.
    std::vector<UboMemberPlan> members;

    // ---- per-draw scratch + content-hash memo ----
    // `scratch` is the packed std140 payload. Kept alive across draws so the
    // pack step never allocates. `lastHash` is its FNV-1a hash from the
    // previous upload; an identical hash means the bytes are unchanged, so the
    // arena slice recorded in lastBuffer/lastOffset is reused and both the
    // bump-allocation and the memcpy are skipped.
    std::vector<uint8_t> scratch;
    uint64_t     lastHash = 0;
    bool         lastValid = false;
    VkBuffer     lastBuffer = VK_NULL_HANDLE;
    VkDeviceSize lastOffset = 0;
    // The slice above lives in one frame slot's arena and dies when that slot
    // is rewound, so the memo is only trusted within the same (slot, frame).
    int      lastSlot = -1;
    uint64_t lastFrameGen = 0;
};

// One entry of the per-program descriptor-set reuse memo. When a draw resolves
// to byte-identical descriptor content as a recent draw, its VkDescriptorSet
// is reused and both vkAllocateDescriptorSets and vkUpdateDescriptorSets are
// skipped — only the bind-time dynamic offsets differ, and those are not part
// of the set. Mirrors MobileGL UniformManager::m_descriptorReuseMemo.
struct DescriptorMemoEntry {
    uint64_t        signature = 0;
    VkDescriptorSet set = VK_NULL_HANDLE;
    bool            valid = false;
};

// Four slots, round-robin: Minecraft alternates between a small number of
// programs (terrain / entity / GUI), and a single slot would thrash back into
// a full allocate+write on every alternation.
constexpr int kDescriptorMemoSize = 4;

// Per-program owned Vulkan resources. Each Program GL object gets one of
// these; it holds the shader modules built from the linked SPIR-V, the
// descriptor set layout / pipeline layout / descriptor pool built from
// SPIRV-Cross reflection (see DescriptorSet.cpp), and the cache of pipelines
// derived from that program.
struct ProgramResources {
    VkShaderModule vertexModule = VK_NULL_HANDLE;        // non-flipped (user FBO)
    VkShaderModule vertexModuleFlipped = VK_NULL_HANDLE; // Y-flipped (default FBO)
    VkShaderModule fragmentModule = VK_NULL_HANDLE;
    // Compute stage. A compute pipeline has no vertex format, no attachments
    // and no blend state, so unlike the graphics pipelines below there is
    // nothing to key a cache on — one program yields exactly one pipeline.
    VkShaderModule computeModule = VK_NULL_HANDLE;
    VkPipeline     computePipeline = VK_NULL_HANDLE;
    // Cached pipelines keyed by a 64-bit signature (see Pipeline.cpp).
    std::unordered_map<uint64_t, VkPipeline> pipelines;

    // Negative cache: signatures for which vkCreateGraphicsPipelines has
    // failed. Without this, every draw call with a failing signature re-runs
    // vkCreateGraphicsPipelines (the success-cache lookup at line ~245 only
    // stores VK_NULL_HANDLE if we explicitly insert it, which we don't). On a
    // persistent failure (e.g. shader/format mismatch), this means N draw
    // calls/frame × M frames of redundant pipeline-creation attempts plus
    // log spam. The set is cleared when the program's shader modules are
    // rebuilt (delete_program_resources / re-link) so a fixed shader can
    // recover. Draws against a negatively-cached signature skip the creation
    // attempt and return VK_NULL_HANDLE immediately (draw is skipped, but
    // without the CPU cost and log spam of a real attempt).
    std::unordered_set<uint64_t> failedSignatures;

    // ---- Descriptor set management (built once by ensure_program_layouts) ----
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout      pipelineLayout = VK_NULL_HANDLE;
    // One pool per frame-in-flight slot, created with
    // VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT. bind_program_descriptors()
    // reuses cached sets via cursor rewind (see allocatedSets/setCursor below)
    // instead of vkResetDescriptorPool — the latter requires all in-flight sets
    // to be complete (VUID-vkResetDescriptorPool-descriptorPool-00313), and even
    // though ensure_command_buffer_recording() waits on the slot's fence before
    // we touch the pool, avoiding reset entirely is more robust against any
    // future code path that might allocate mid-frame. This mirrors MobileGL's
    // UniformManager (UniformManager.cpp:183,1026), which also avoids
    // vkResetDescriptorPool in favor of cursor rewind + set reuse.
    VkDescriptorPool      descriptorPools[kMaxFramesInFlight] = {};
    std::vector<DescriptorBinding> bindings;  // reflected VS+FS binding set
    bool layoutsBuilt = false;
    // Per-slot cached descriptor sets + rewind cursor. At the start of each
    // frame (detected via lastFrameGen != b->frameGeneration), the cursor is
    // rewound to 0. Subsequent draws in that frame reuse
    // allocatedSets[slot][cursor++] if available, or vkAllocateDescriptorSets
    // a new set and appends it. The pool's FREE_DESCRIPTOR_SET_BIT flag keeps
    // the option open to vkFreeDescriptorSets a destroyed layout's cached
    // sets, though in practice we never free individual sets — the pool is
    // destroyed wholesale on program deletion (which implicitly frees all
    // sets allocated from it). Rewind is safe because it runs after the
    // slot's fence wait in ensure_command_buffer_recording(), so no in-flight
    // command buffer references the reused sets.
    std::vector<VkDescriptorSet> allocatedSets[kMaxFramesInFlight];
    size_t    setCursor[kMaxFramesInFlight] = {};
    uint64_t  lastFrameGen[kMaxFramesInFlight] = {};

    /* ---- Dynamic-UBO mode (MoltenVK degradation gate) ----
     *
     * True when this program's UBO bindings were declared as
     * VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, so a draw addresses its arena
     * slice through pDynamicOffsets and the descriptor set itself stays
     * constant (which is what makes descMemo hit).
     *
     * False when the program declares more UBOs than
     * ubo_arena_max_dynamic_ubos() allows — MoltenVK commonly reports 8. The
     * bindings are then plain VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER and the arena
     * offset goes into VkDescriptorBufferInfo::offset instead. Still correct
     * (each draw gets fresh arena bytes, so the aliasing bug stays fixed);
     * only the descriptor-set reuse rate drops, because a moving offset now
     * changes the set's contents.
     *
     * Decided once in ensure_program_layouts() because the descriptor type is
     * baked into the VkDescriptorSetLayout.
     */
    bool uboDynamic = false;

    /* The UBO bindings that were actually declared DYNAMIC in
     * descriptorSetLayout, ascending. This — not any re-derivation at draw
     * time — is the authoritative answer to "what descriptorType must this
     * write use", so a write can never disagree with the layout it targets.
     * Empty when uboDynamic is false. */
    std::vector<uint32_t> dynamicUboBindings;

    /* Fixed-up uniform packing plans, one per UBO binding, in the same order
     * as the UBO bindings appear in `bindings`. Built lazily on the first draw
     * after a link (planLinkVersion != Program::linkVersion) — not in
     * ensure_program_layouts(), because that can run before the frontend has
     * finished registering every uniform. */
    std::vector<UboBindingPlan> uboPlans;
    uint32_t planLinkVersion = 0xFFFFFFFFu;  // sentinel: no plan built yet

    // Per-slot descriptor-set reuse memo. Reset when the slot's set cursor is
    // rewound (start of frame), because that is when the cached sets become
    // eligible for rewriting.
    DescriptorMemoEntry descMemo[kMaxFramesInFlight][kDescriptorMemoSize];
    int                 descMemoNext[kMaxFramesInFlight] = {};
};

// Accessor for the per-program resource table (keyed by GL program name).
std::unordered_map<GLuint, ProgramResources>& program_table();

// Build (or fetch from cache) a VkPipeline for the given configuration.
// All arguments mirror backend_get_or_create_pipeline() in Backend.h.
//   color_write_mask : 4-bit RGBA mask (bit0=R, bit1=G, bit2=B, bit3=A) from
//                      g_state->colorMask; part of the pipeline signature so
//                      glColorMask changes create distinct pipelines.
//   blend_src_alpha / blend_dst_alpha : GL blend factors for the alpha
//                      channel (independent from RGB); part of the signature.
//   is_default_fbo  : 1 when drawing to the default framebuffer (FBO 0),
//                      which selects the Y-flipped vertex shader module and
//                      is part of the pipeline signature hash so the two
//                      SPIR-V variants get distinct cache entries.
VkPipeline get_or_create_pipeline(GLuint program,
                                  const uint32_t* vertex_spirv, int vertex_word_count,
                                  const uint32_t* fragment_spirv, int fragment_word_count,
                                  const ::MGVertexAttrib* attribs, int attrib_count,
                                  const VkFormat* color_formats, int color_count,
                                  VkFormat depth_format,
                                  int blend_enabled, GLenum blend_src, GLenum blend_dst,
                                  GLenum blend_src_alpha, GLenum blend_dst_alpha,
                                  int color_write_mask,
                                  GLenum gl_primitive_mode,
                                  int is_default_fbo);

// Build (or fetch from cache) the single VkComputePipeline of a program.
// Mirrors get_or_create_pipeline(): builds the shader module on first use,
// runs ensure_program_layouts() so the descriptor/pipeline layout exists, then
// creates the pipeline and caches it in ProgramResources::computePipeline.
// Returns VK_NULL_HANDLE if the module or the pipeline cannot be created.
VkPipeline get_or_create_compute_pipeline(GLuint program,
                                          const uint32_t* compute_spirv,
                                          int compute_word_count);

// Release all Vulkan resources owned by a program (shader modules + pipelines).
void delete_program_resources(GLuint program);

// FIX (红屏根因): 清除所有 program 的 failedSignatures 负缓存 + 销毁所有
// 已创建的 VkPipeline。在 backend_reset_device_lost() 时调用，让着色器在
// 设备恢复后有机会重新编译。deviceLost 期间 MoltenVK 着色器缓存可能损坏，
// 不清除会导致 draw 永久跳过 → 红屏/黑屏。
void clear_all_pipeline_caches();

} // namespace vk
} // namespace mithril

#endif // MITHRIL_DIRECTVULKAN_PIPELINE_H
