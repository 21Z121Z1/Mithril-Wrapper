// Mithril-Wrapper - MG_Backend/DirectVulkan/DescriptorSet.cpp
// SPIR-V reflection (SPIRV-Cross) + VkDescriptorSetLayout / VkPipelineLayout /
// VkDescriptorPool construction + per-frame VkDescriptorSet allocation/write/bind.
//
// Reflection strategy: glslang compiles desktop GLSL with setAutoMapBindings(true),
// so each `uniform` global / `uniform sampler*` gets an auto-assigned Vulkan
// binding. We reflect both VS and FS, merge by (set,binding) OR-ing the stage
// masks, build one VkDescriptorSetLayout, and a VkPipelineLayout referencing it.
//
// UBO data sourcing: a reflected UBO is matched against Program.uniforms. If the
// UBO name matches a uniform directly (glslang emits one UBO per loose uniform)
// we upload that uniform's value. Otherwise (glslang aggregates loose uniforms
// into a single `$Global` block) we pack the block's members by name using the
// member offsets reported by SPIRV-Cross's get_active_buffer_ranges().
//
// Texture sourcing: a reflected combined-image-sampler at binding B is fed from
// g_state->boundTextures[B] (the GL texture bound to unit B), matching how the
// GL frontend binds textures by unit index.
#include "DescriptorSet.h"
#include "Device.h"
#include "Pipeline.h"
#include "../Backend.h"
#include "../../MG_State/State.h"
#include "../../MG_Impl/Log.h"

#include <spirv_cross.hpp>
// spirv_cross.hpp transitively pulls in SPIRV-Cross's bundled spirv.hpp,
// which defines the spv:: namespace (spv::DecorationBinding, etc.) used below.

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace mithril {
namespace vk {

namespace {

// Reflect one stage's SPIR-V. `stage` tags every discovered binding so the
// caller can OR masks when merging VS+FS.
std::vector<DescriptorBinding> reflect_stage(const uint32_t* spirv, int words,
                                             VkShaderStageFlags stage) {
    std::vector<DescriptorBinding> out;
    if (!spirv || words <= 0) return out;
    try {
        spirv_cross::Compiler compiler(spirv, static_cast<size_t>(words));
        spirv_cross::ShaderResources res = compiler.get_shader_resources();

        for (auto& r : res.uniform_buffers) {
            DescriptorBinding b{};
            b.set = compiler.get_decoration(r.id, spv::DecorationDescriptorSet);
            b.binding = compiler.get_decoration(r.id, spv::DecorationBinding);
            b.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            b.stageMask = stage;
            b.name = r.name;
            const spirv_cross::SPIRType& t = compiler.get_type(r.base_type_id);
            b.bufferSize = static_cast<uint32_t>(compiler.get_declared_struct_size(t));
            // Member layout (offsets + names) for the aggregated-block case.
            for (auto& rng : compiler.get_active_buffer_ranges(r.id)) {
                DescriptorBindingMember m;
                m.name = compiler.get_member_name(r.base_type_id, rng.index);
                m.offset = static_cast<uint32_t>(rng.offset);
                m.size = static_cast<uint32_t>(rng.range);
                b.members.push_back(std::move(m));
            }
            out.push_back(std::move(b));
        }
        for (auto& r : res.sampled_images) {
            DescriptorBinding b{};
            b.set = compiler.get_decoration(r.id, spv::DecorationDescriptorSet);
            b.binding = compiler.get_decoration(r.id, spv::DecorationBinding);
            b.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            b.stageMask = stage;
            b.name = r.name;
            const spirv_cross::SPIRType& t = compiler.get_type(r.type_id);
            b.descriptorCount = t.array.empty() ? 1u : static_cast<uint32_t>(t.array[0]);
            if (b.descriptorCount == 0) b.descriptorCount = 1;
            out.push_back(std::move(b));
        }
    } catch (const std::exception& e) {
        MITHRIL_LOG_WARN("vk", "SPIRV-Cross reflection failed: %s", e.what());
    }
    return out;
}

// Merge `src` bindings into `dst`. A binding with the same (set,binding,type)
// has its stageMask OR-ed in (members kept from the first occurrence; they are
// identical across stages for a well-formed program).
void merge_bindings(std::vector<DescriptorBinding>& dst,
                    const std::vector<DescriptorBinding>& src) {
    for (const auto& s : src) {
        auto it = std::find_if(dst.begin(), dst.end(), [&](const DescriptorBinding& d) {
            return d.set == s.set && d.binding == s.binding && d.type == s.type;
        });
        if (it == dst.end()) {
            dst.push_back(s);
        } else {
            it->stageMask |= s.stageMask;
        }
    }
}

} // namespace

void ensure_program_layouts(GLuint program,
                            const uint32_t* vs, int vs_words,
                            const uint32_t* fs, int fs_words) {
    Backend* b = backend();
    if (!b->initialized || program == 0) return;

    auto& tbl = program_table();
    ProgramResources& pr = tbl[program];
    if (pr.layoutsBuilt) return;

    // Reflect + merge both stages.
    pr.bindings.clear();
    pr.bindings = reflect_stage(vs, vs_words, VK_SHADER_STAGE_VERTEX_BIT);
    merge_bindings(pr.bindings, reflect_stage(fs, fs_words, VK_SHADER_STAGE_FRAGMENT_BIT));

    // Deterministic ordering for stable set-layout construction.
    std::sort(pr.bindings.begin(), pr.bindings.end(),
              [](const DescriptorBinding& a, const DescriptorBinding& c) {
                  if (a.set != c.set) return a.set < c.set;
                  return a.binding < c.binding;
              });

    pr.layoutsBuilt = true;  // set early so a reflection failure doesn't retry forever
    if (pr.bindings.empty()) {
        // No descriptors: caller falls back to the process-wide empty layout.
        return;
    }

    // ---- VkDescriptorSetLayout ----
    std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
    layoutBindings.reserve(pr.bindings.size());
    for (const auto& db : pr.bindings) {
        VkDescriptorSetLayoutBinding lb{};
        lb.binding = db.binding;
        lb.descriptorType = db.type;
        lb.descriptorCount = db.descriptorCount;
        lb.stageFlags = db.stageMask;
        lb.pImmutableSamplers = nullptr;
        layoutBindings.push_back(lb);
    }
    VkDescriptorSetLayoutCreateInfo dslci{};
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    dslci.pBindings = layoutBindings.data();
    if (vkCreateDescriptorSetLayout(b->device, &dslci, nullptr, &pr.descriptorSetLayout) != VK_SUCCESS) {
        MITHRIL_LOG_WARN("vk", "vkCreateDescriptorSetLayout failed (program %u)", program);
        pr.bindings.clear();
        return;
    }

    // ---- VkPipelineLayout (single set) ----
    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &pr.descriptorSetLayout;
    if (vkCreatePipelineLayout(b->device, &plci, nullptr, &pr.pipelineLayout) != VK_SUCCESS) {
        MITHRIL_LOG_WARN("vk", "vkCreatePipelineLayout failed (program %u)", program);
        vkDestroyDescriptorSetLayout(b->device, pr.descriptorSetLayout, nullptr);
        pr.descriptorSetLayout = VK_NULL_HANDLE;
        pr.bindings.clear();
        return;
    }

    // ---- VkDescriptorPool ----
    // maxSets=256, 256 descriptors per type (per task spec). The pool is reset
    // once per frame in bind_program_descriptors(), so 256 sets amortise across
    // a frame's draws; a mid-frame exhaustion triggers a reset+retry.
    bool hasUBO = false, hasImg = false;
    for (const auto& db : pr.bindings) {
        if (db.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) hasUBO = true;
        else if (db.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) hasImg = true;
    }
    std::vector<VkDescriptorPoolSize> poolSizes;
    if (hasUBO) {
        VkDescriptorPoolSize ps{};
        ps.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ps.descriptorCount = 256;
        poolSizes.push_back(ps);
    }
    if (hasImg) {
        VkDescriptorPoolSize ps{};
        ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ps.descriptorCount = 256;
        poolSizes.push_back(ps);
    }
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 256;
    dpci.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    dpci.pPoolSizes = poolSizes.data();
    if (vkCreateDescriptorPool(b->device, &dpci, nullptr, &pr.descriptorPool) != VK_SUCCESS) {
        MITHRIL_LOG_WARN("vk", "vkCreateDescriptorPool failed (program %u)", program);
        // Layout is still valid; bind_program_descriptors will no-op without a pool.
    }
}

void bind_program_descriptors(GLuint program) {
    Backend* b = backend();
    if (!b->initialized || !b->commandBuffer || program == 0) return;

    auto& tbl = program_table();
    auto it = tbl.find(program);
    if (it == tbl.end()) return;
    ProgramResources& pr = it->second;
    if (!pr.layoutsBuilt || pr.bindings.empty() ||
        pr.pipelineLayout == VK_NULL_HANDLE || pr.descriptorPool == VK_NULL_HANDLE) {
        return;
    }

    mithril::Program* prog = mithril::state_get_program(program);
    if (!prog) return;

    // Per-frame pool reset. commit_frame() waits on the previous frame's fence
    // (so the prior frame's sets are no longer in-flight) and then bumps the
    // monotonic frameGeneration. We reset this program's pool on the first draw
    // of each new generation and reuse it for the rest of the frame; a program
    // drawn only on every other frame still gets reset because the monotonic
    // counter never repeats (unlike the cycling currentFrame). Within a frame
    // frameGeneration is constant, so this never resets mid-frame.
    if (pr.lastResetGen != b->frameGeneration) {
        vkResetDescriptorPool(b->device, pr.descriptorPool, 0);
        pr.lastResetGen = b->frameGeneration;
    }

    // Allocate a fresh set for this draw.
    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = pr.descriptorPool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &pr.descriptorSetLayout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(b->device, &dsai, &set) != VK_SUCCESS) {
        // Pool exhausted mid-frame (more than 256 distinct sets this frame):
        // reset and retry once. Acceptable for bring-up; a fully correct impl
        // would grow the pool or pool-set per frame.
        vkResetDescriptorPool(b->device, pr.descriptorPool, 0);
        if (vkAllocateDescriptorSets(b->device, &dsai, &set) != VK_SUCCESS) {
            MITHRIL_LOG_WARN("vk", "vkAllocateDescriptorSets failed (program %u)", program);
            return;
        }
    }

    // Gather descriptor writes. The info vectors are reserved to the binding
    // count so they never reallocate (the VkWriteDescriptorSet structs hold
    // pointers into them).
    std::vector<VkWriteDescriptorSet> writes;
    std::vector<VkDescriptorBufferInfo> bufInfos;
    std::vector<VkDescriptorImageInfo> imgInfos;
    bufInfos.reserve(pr.bindings.size());
    imgInfos.reserve(pr.bindings.size());
    writes.reserve(pr.bindings.size());

    for (const auto& db : pr.bindings) {
        if (db.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            uint32_t sz = db.bufferSize ? db.bufferSize : 16u;
            std::vector<uint8_t> payload(sz, 0);

            // (1) Direct name match: one UBO per loose uniform.
            auto uit = prog->uniforms.find(db.name);
            if (uit != prog->uniforms.end() && !uit->second.value.empty()) {
                size_t bytes = uit->second.value.size() * sizeof(float);
                std::memcpy(payload.data(), uit->second.value.data(),
                            std::min(bytes, static_cast<size_t>(sz)));
            } else {
                // (2) Aggregated block ($Global): pack members by name/offset.
                for (const auto& m : db.members) {
                    auto mit = prog->uniforms.find(m.name);
                    if (mit != prog->uniforms.end() && !mit->second.value.empty()) {
                        size_t bytes = mit->second.value.size() * sizeof(float);
                        size_t n = std::min(bytes, static_cast<size_t>(m.size));
                        if (static_cast<size_t>(m.offset) + n <= payload.size()) {
                            std::memcpy(payload.data() + m.offset,
                                        mit->second.value.data(), n);
                        }
                    }
                }
            }

            // Stable per-(program,binding) GL name so the buffer is reused and
            // its contents updated each frame rather than reallocated.
            GLuint uname = program * 1000000u + db.binding + 1u;
            VkBuffer ubuf = backend_get_or_create_buffer(uname, payload.data(), payload.size());
            if (ubuf != VK_NULL_HANDLE) {
                VkDescriptorBufferInfo bi{};
                bi.buffer = ubuf;
                bi.offset = 0;
                bi.range = VK_WHOLE_SIZE;
                bufInfos.push_back(bi);
                VkWriteDescriptorSet w{};
                w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w.dstSet = set;
                w.dstBinding = db.binding;
                w.dstArrayElement = 0;
                w.descriptorCount = 1;
                w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                w.pBufferInfo = &bufInfos.back();
                writes.push_back(w);
            }
        } else if (db.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
            // Sampler binding B maps to GL texture unit B.
            GLuint tex_id = 0;
            if (db.binding < mithril::kMaxTextureUnits) {
                tex_id = mithril::g_state->boundTextures[db.binding];
            }
            if (!tex_id) {
                // Fallback: first bound texture, so the descriptor stays valid
                // (an unbound sampler binding would leave the set incomplete).
                for (int i = 0; i < mithril::kMaxTextureUnits; ++i) {
                    if (mithril::g_state->boundTextures[i]) {
                        tex_id = mithril::g_state->boundTextures[i];
                        break;
                    }
                }
            }
            if (tex_id) {
                VkImageView view = backend_get_texture_view(tex_id);
                VkSampler samp = backend_get_or_create_sampler(
                    tex_id, GL_LINEAR, GL_LINEAR,
                    GL_REPEAT, GL_REPEAT, GL_REPEAT, nullptr);
                if (view != VK_NULL_HANDLE && samp != VK_NULL_HANDLE) {
                    VkDescriptorImageInfo ii{};
                    ii.sampler = samp;
                    ii.imageView = view;
                    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    imgInfos.push_back(ii);
                    VkWriteDescriptorSet w{};
                    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    w.dstSet = set;
                    w.dstBinding = db.binding;
                    w.dstArrayElement = 0;
                    w.descriptorCount = 1;
                    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    w.pImageInfo = &imgInfos.back();
                    writes.push_back(w);
                }
            }
        }
    }

    if (!writes.empty()) {
        vkUpdateDescriptorSets(b->device, static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }
    vkCmdBindDescriptorSets(b->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pr.pipelineLayout, 0, 1, &set, 0, nullptr);
}

} // namespace vk
} // namespace mithril

// ===========================================================================
// Public C API wrappers (declared in MG_Backend/Backend.h)
// ===========================================================================
extern "C" {

void backend_ensure_program_layouts(GLuint program,
                                    const uint32_t* vs, int vs_words,
                                    const uint32_t* fs, int fs_words) {
    mithril::vk::ensure_program_layouts(program, vs, vs_words, fs, fs_words);
}

void backend_bind_program_descriptors(GLuint program) {
    mithril::vk::bind_program_descriptors(program);
}

} // extern "C"
