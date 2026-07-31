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
#include "Reflect.h"  // reflect_stage / merge_bindings (pure-logic, unit-tested)
#include "../Backend.h"
#include "../../MG_State/State.h"
#include "../../MG_Impl/Log.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace mithril {
namespace vk {

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

// Resolve the GL texture unit a sampler binding reads from. Per GL semantics
// the unit is set by the app via glUniform1i(samplerLoc, unit) and cached in
// Program::uniforms[name].value[0] (as float). glslang's auto-assigned SPIR-V
// binding number is NOT guaranteed to equal that unit, so prefer the uniform
// value and fall back to db.binding only when no value was set / out of range.
// (Matches MobileGL's lastAssignedUnit semantics.) Returns the unit index.
static int resolve_sampler_unit(const mithril::Program* prog,
                                const mithril::vk::DescriptorBinding& db) {
    if (prog) {
        auto it = prog->uniforms.find(db.name);
        if (it != prog->uniforms.end() && !it->second.value.empty()) {
            int u = (int)std::lround(it->second.value[0]);
            if (u >= 0 && u < mithril::kMaxTextureUnits) return u;
        }
    }
    return (int)db.binding;
}

void bind_program_descriptors(GLuint program) {
    Backend* b = backend();
    if (!b->initialized || !b->commandBuffer || program == 0) return;
    // vkCmdBindDescriptorSets on a non-recording buffer is UB (VK_NOT_READY
    // spam under GPU fault) — same guard as the CommandStream entry points.
    if (!b->commandBufferRecording) {
        // d2ccb1b 守卫静默跳过时埋点，使 descriptor 未绑定导致的红屏可见。
        static int skip_count = 0;
        if (skip_count < 4 || skip_count % 1000 == 0) {
            MITHRIL_LOG_WARN("vk", "bind_program_descriptors skipped: "
                              "program=%u commandBufferRecording=false (count=%d)",
                              program, skip_count);
        }
        skip_count++;
        return;
    }

    auto& tbl = program_table();
    auto it = tbl.find(program);
    if (it == tbl.end()) return;
    ProgramResources& pr = it->second;

    // 为 pipeline 创建时记录的 dummy 顶点 binding 槽位绑定 dummyVertexBuffer。
    // commit 64aedfa 为着色器声明但 GL 未 enable 的 location 追加了 dummy
    // binding/attribute description，但 draw 时从未绑定 backing buffer，导致
    // Vulkan 未定义行为 → MoltenVK 读取 stale Metal buffer → IOSurfaceBindAccel
    // SIGSEGV (UAF)。此处放在描述符早返回之前，确保 binding-less 程序（无 UBO/
    // 纹理，pr.bindings 为空）也能绑定 dummy 槽位。
    if (!pr.dummyBindings.empty()) {
        if (b->dummyVertexBuffer != VK_NULL_HANDLE) {
            VkDeviceSize zeroOffset = 0;
            for (uint32_t binding : pr.dummyBindings) {
                // dummy 槽位号不连续，逐个绑定（count=1）。
                vkCmdBindVertexBuffers(b->commandBuffer, binding, 1,
                                       &b->dummyVertexBuffer, &zeroOffset);
            }
        } else {
            static bool warned = false;
            if (!warned) {
                warned = true;
                MITHRIL_LOG_WARN("vk",
                    "dummyVertexBuffer not allocated; dummy vertex bindings have no backing buffer (program %u)",
                    program);
            }
        }
    }

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
    VkResult rc = vkAllocateDescriptorSets(b->device, &dsai, &set);
    if (rc != VK_SUCCESS) {
        // Pool exhausted mid-frame (more than 256 distinct sets this frame):
        // reset and retry once. Acceptable for bring-up; a fully correct impl
        // would grow the pool or pool-set per frame.
        vkResetDescriptorPool(b->device, pr.descriptorPool, 0);
        rc = vkAllocateDescriptorSets(b->device, &dsai, &set);
        if (rc != VK_SUCCESS) {
            MITHRIL_LOG_ERROR("vk", "vkAllocateDescriptorSets failed (program %u rc=%d bindings=%zu)",
                              program, (int)rc, pr.bindings.size());
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
            // Update in-place when the buffer already exists and is large
            // enough. Destroying+recreating every frame (the old code path)
            // floods pendingBufferReleases with thousands of entries,
            // eventually corrupting MoltenVK's internal hash table -> SIGSEGV.
            auto& btable = buffer_table();
            auto bit = btable.find(uname);
            VkBuffer ubuf = VK_NULL_HANDLE;
            if (bit != btable.end() &&
                bit->second.buffer != VK_NULL_HANDLE &&
                bit->second.size >= (VkDeviceSize)payload.size()) {
                backend_buffer_upload(uname, 0, payload.data(), payload.size());
                ubuf = bit->second.buffer;
            } else {
                ubuf = backend_get_or_create_buffer(uname, payload.data(), payload.size());
            }
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
            int unit = resolve_sampler_unit(prog, db);
            GLuint tex_id = 0;
            if (unit >= 0 && unit < mithril::kMaxTextureUnits) {
                tex_id = mithril::g_state->boundTextures[unit];
            }
            if (unit != (int)db.binding) {
                static std::unordered_map<GLuint, int> remapCount;
                int& rc = remapCount[program];
                if (rc < 3) {
                    rc++;
                    MITHRIL_LOG_INFO("vk", "sampler unit remap: program=%u binding=%u name=%s unit=%d (binding!=unit)",
                                     program, db.binding, db.name.c_str(), unit);
                }
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
                } else if (view == VK_NULL_HANDLE) {
                    // 纹理被删除（VkImageView=null）但 shader 仍声明该 binding。
                    // 不能跳过 descriptor 写入——pipeline layout 声明的所有 binding
                    // 在 draw 时必须有有效 descriptor，否则 Metal 驱动读取野指针
                    // IOSurface → IOSurfaceBindAccel SIGSEGV（dd972b9 的根因）。
                    // 用持久的 dummy texture view + dummy sampler 填充，让 draw
                    // 读取全零/透明纹素而非崩溃。
                    if (b->dummyTextureView != VK_NULL_HANDLE && b->dummyTextureSampler != VK_NULL_HANDLE) {
                        // 第一次使用 dummy texture 时，在当前 command buffer 录制
                        // UNDEFINED → SHADER_READ_ONLY_OPTIMAL 的 layout transition。
                        // init_device() 不再录制此命令（避免 init command buffer
                        // 混入可能引发 InvalidResource 的命令）。后续使用直接以
                        // SHADER_READ_ONLY_OPTIMAL 填充，不重复 transition。
                        // 标志存储在 Backend 成员而非 static bool，确保 command
                        // buffer reset（submit 失败）时能被重置，否则 barrier 丢失
                        // 后标志仍为 true，后续帧以错误 layout 引用 dummy image →
                        // kIOGPUCommandBufferCallbackErrorInvalidResource。
                        if (!b->dummyTextureLayoutInitialized) {
                            VkImageMemoryBarrier dbar{};
                            dbar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                            dbar.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                            dbar.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                            dbar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                            dbar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                            dbar.image = b->dummyTextureImage;
                            dbar.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                            dbar.subresourceRange.baseMipLevel = 0;
                            dbar.subresourceRange.levelCount = 1;
                            dbar.subresourceRange.baseArrayLayer = 0;
                            dbar.subresourceRange.layerCount = 1;
                            dbar.srcAccessMask = 0;
                            dbar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                            vkCmdPipelineBarrier(b->commandBuffer,
                                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                                 0, nullptr, 0, nullptr, 1, &dbar);
                            b->dummyTextureLayoutInitialized = true;
                        }
                        VkDescriptorImageInfo ii{};
                        ii.sampler = b->dummyTextureSampler;
                        ii.imageView = b->dummyTextureView;
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
    }

    if (!writes.empty()) {
        vkUpdateDescriptorSets(b->device, static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }

    // ---- Binding completeness validation (diagnostic only; does not block draw).
    // Mirrors the write loop's sourcing logic to flag bindings whose backing
    // resource is absent at draw time: an unbound/deleted texture unit, or a
    // UBO whose uniform data was never provided (buffer will be zero-filled).
    // The write loop substitutes fallbacks (first bound texture / dummy view /
    // zero UBO) so the descriptor set stays valid; this pass just makes those
    // substitutions visible. Hash-deduped (FNV-1a over program+binding+type)
    // so a persistently missing binding doesn't flood the log: first 4
    // occurrences logged in full, then a summary every 1000th repeat — same
    // pattern as the vulkan debug messenger in Device.cpp.
    static std::atomic<uint64_t> missLastHash{0};
    static std::atomic<uint64_t> missRepCount{0};
    for (const auto& db : pr.bindings) {
        bool missing = false;
        if (db.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
            int unit = resolve_sampler_unit(prog, db);
            GLuint tex_id = 0;
            if (unit >= 0 && unit < mithril::kMaxTextureUnits) {
                tex_id = mithril::g_state->boundTextures[unit];
            }
            if (!tex_id) {
                // No texture bound at this unit — write loop falls back to the
                // first bound texture or the dummy view.
                missing = true;
            } else {
                VkImageView view = backend_get_texture_view(tex_id);
                if (view == VK_NULL_HANDLE) {
                    // Texture was deleted (VkImageView gone) but the shader
                    // still references this binding — write loop will fill it
                    // with the dummy texture view.
                    missing = true;
                }
            }
        } else if (db.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            // Reuse the write loop's uniform lookup: direct name match first,
            // then aggregated-block member matches.
            auto uit = prog->uniforms.find(db.name);
            bool found = (uit != prog->uniforms.end() && !uit->second.value.empty());
            if (!found) {
                for (const auto& m : db.members) {
                    auto mit = prog->uniforms.find(m.name);
                    if (mit != prog->uniforms.end() && !mit->second.value.empty()) {
                        found = true;
                        break;
                    }
                }
            }
            if (!found) missing = true;
        }
        if (!missing) continue;

        uint64_t h = 1469598103934665603ull;  // FNV-1a 64-bit over (program,binding,type)
        h ^= (uint64_t)program; h *= 1099511628211ull;
        h ^= (uint64_t)db.binding; h *= 1099511628211ull;
        h ^= (uint64_t)db.type; h *= 1099511628211ull;

        if (h == missLastHash.load(std::memory_order_relaxed)) {
            uint64_t n = missRepCount.fetch_add(1, std::memory_order_relaxed) + 1;
            if (n <= 4) {
                MITHRIL_LOG_ERROR("vk", "missing descriptor binding: program=%u binding=%u type=%d",
                                  program, db.binding, (int)db.type);
            } else if (n % 1000 == 0) {
                MITHRIL_LOG_ERROR("vk", "missing descriptor binding repeated %llu times: program=%u binding=%u type=%d",
                                  (unsigned long long)n, program, db.binding, (int)db.type);
            }
        } else {
            uint64_t prev = missRepCount.exchange(1, std::memory_order_relaxed);
            missLastHash.store(h, std::memory_order_relaxed);
            if (prev > 4) {
                MITHRIL_LOG_ERROR("vk", "(previous missing binding repeated %llu times total)",
                                  (unsigned long long)prev);
            }
            MITHRIL_LOG_ERROR("vk", "missing descriptor binding: program=%u binding=%u type=%d",
                              program, db.binding, (int)db.type);
        }
    }

    vkCmdBindDescriptorSets(b->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pr.pipelineLayout, 0, 1, &set, 0, nullptr);

    // One-shot-per-boot summary of the first few successful binds, so the
    // program id / binding count / set index reach the log at least once
    // per session for sanity-checking the reflection + bind path.
    static int bindCount = 0;
    if (bindCount < 5) {
        MITHRIL_LOG_INFO("vk", "descriptor set bound: program=%u bindings=%zu setIndex=0",
                         program, pr.bindings.size());
        bindCount++;
    }
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
