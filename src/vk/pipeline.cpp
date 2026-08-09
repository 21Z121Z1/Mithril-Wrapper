// Mithril-Wrapper Vulkan backend -- program/pipeline objects (M2-VK).
// SPIR-V module creation, UBO reflection (SPIRV-Cross), pipeline
// key building and the cached GetOrCreatePipeline factory.

#include "internal.h"

#include <algorithm>
#include <exception>
#include <string>

#include <spirv_cross.hpp>

namespace mithril::vk {

namespace {
// GL fog/depth/stencil/blend enums -> Vulkan. These are the only places the
// backend maps GL values; everything else arrives pre-interleaved.

VkCompareOp ToVkCompare(GLenum f) {
    switch (f) {
        case GL_NEVER: return VK_COMPARE_OP_NEVER;
        case GL_LESS: return VK_COMPARE_OP_LESS;
        case GL_EQUAL: return VK_COMPARE_OP_EQUAL;
        case GL_LEQUAL: return VK_COMPARE_OP_LESS_OR_EQUAL;
        case GL_GREATER: return VK_COMPARE_OP_GREATER;
        case GL_NOTEQUAL: return VK_COMPARE_OP_NOT_EQUAL;
        case GL_GEQUAL: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        default: return VK_COMPARE_OP_ALWAYS;
    }
}

VkStencilOp StencilOp(GLenum op) {
    switch (op) {
        case GL_KEEP: return VK_STENCIL_OP_KEEP;
        case GL_ZERO: return VK_STENCIL_OP_ZERO;
        case GL_REPLACE: return VK_STENCIL_OP_REPLACE;
        case GL_INCR: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        case GL_DECR: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        case GL_INVERT: return VK_STENCIL_OP_INVERT;
        case GL_INCR_WRAP: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
        default: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    }
}

VkColorComponentFlags ColorMask(const PipelineState& ps) {
    VkColorComponentFlags f = 0;
    if (ps.color_wmask_r) f |= VK_COLOR_COMPONENT_R_BIT;
    if (ps.color_wmask_g) f |= VK_COLOR_COMPONENT_G_BIT;
    if (ps.color_wmask_b) f |= VK_COLOR_COMPONENT_B_BIT;
    if (ps.color_wmask_a) f |= VK_COLOR_COMPONENT_A_BIT;
    return f;
}

VkBlendFactor ToVkBlend(GLenum f) {
    switch (f) {
        case GL_ZERO: return VK_BLEND_FACTOR_ZERO;
        case GL_ONE: return VK_BLEND_FACTOR_ONE;
        case GL_SRC_COLOR: return VK_BLEND_FACTOR_SRC_COLOR;
        case GL_ONE_MINUS_SRC_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case GL_DST_COLOR: return VK_BLEND_FACTOR_DST_COLOR;
        case GL_ONE_MINUS_DST_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case GL_SRC_ALPHA: return VK_BLEND_FACTOR_SRC_ALPHA;
        case GL_ONE_MINUS_SRC_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case GL_DST_ALPHA: return VK_BLEND_FACTOR_DST_ALPHA;
        case GL_ONE_MINUS_DST_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case GL_CONSTANT_COLOR: return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case GL_ONE_MINUS_CONSTANT_COLOR:
            return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        case GL_CONSTANT_ALPHA: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        case GL_ONE_MINUS_CONSTANT_ALPHA:
            return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        case GL_SRC_ALPHA_SATURATE: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        default: return VK_BLEND_FACTOR_ONE;
    }
}

VkBlendOp ToBlendOp(GLenum op) {
    switch (op) {
        case GL_FUNC_SUBTRACT: return VK_BLEND_OP_SUBTRACT;
        case GL_FUNC_REVERSE_SUBTRACT: return VK_BLEND_OP_REVERSE_SUBTRACT;
        case GL_MIN: return VK_BLEND_OP_MIN;
        case GL_MAX: return VK_BLEND_OP_MAX;
        default: return VK_BLEND_OP_ADD;
    }
}

} // namespace

// Stable textual signature of every pipeline-affecting GL state field.
// Appended to the geometry key so state changes bake a fresh pipeline.
std::string StateSignature(const PipelineState& ps) {
    std::string k;
    k += "|D" + std::string(ps.depth_test ? "1" : "0") +
         std::to_string(ps.depth_func) + (ps.depth_write ? "1" : "0");
    k += "|S" + std::string(ps.stencil_test ? "1" : "0") +
         std::to_string(ps.stencil_front_func) + std::to_string(ps.stencil_back_func) +
         std::to_string(ps.stencil_front_ref) + std::to_string(ps.stencil_back_ref) +
         std::to_string(ps.stencil_front_read_mask) +
         std::to_string(ps.stencil_back_read_mask) +
         std::to_string(ps.stencil_front_write_mask) +
         std::to_string(ps.stencil_back_write_mask) +
         std::to_string(ps.stencil_front_op_fail) +
         std::to_string(ps.stencil_front_op_zfail) +
         std::to_string(ps.stencil_front_op_zpass) +
         std::to_string(ps.stencil_back_op_fail) +
         std::to_string(ps.stencil_back_op_zfail) +
         std::to_string(ps.stencil_back_op_zpass);
    k += "|B" + std::string(ps.blend_enable ? "1" : "0") +
         std::to_string(ps.blend_src_rgb) + std::to_string(ps.blend_dst_rgb) +
         std::to_string(ps.blend_src_alpha) + std::to_string(ps.blend_dst_alpha) +
         std::to_string(ps.blend_eq_rgb) + std::to_string(ps.blend_eq_alpha);
    k += "|C" + std::string(ps.cull_test ? "1" : "0") +
         std::to_string(ps.cull_face) + std::to_string(ps.front_face);
    k += "|P" + std::to_string(ps.polygon_mode);
    k += "|W" + std::string(ps.color_wmask_r ? "1" : "0") +
         std::string(ps.color_wmask_g ? "1" : "0") +
         std::string(ps.color_wmask_b ? "1" : "0") +
         std::string(ps.color_wmask_a ? "1" : "0");
    k += "|R" + std::string(ps.scissor_test ? "1" : "0");
    return k;
}

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

VkSampleCountFlagBits ToVkSampleCount(uint32_t samples) {
    switch (samples) {
        case 1:  return VK_SAMPLE_COUNT_1_BIT;
        case 2:  return VK_SAMPLE_COUNT_2_BIT;
        case 4:  return VK_SAMPLE_COUNT_4_BIT;
        case 8:  return VK_SAMPLE_COUNT_8_BIT;
        case 16: return VK_SAMPLE_COUNT_16_BIT;
        case 32: return VK_SAMPLE_COUNT_32_BIT;
        case 64: return VK_SAMPLE_COUNT_64_BIT;
        default: return VK_SAMPLE_COUNT_1_BIT;
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
    rs.rasterizerDiscardEnable = VK_FALSE;
    rs.polygonMode = op.pipe.polygon_mode == GL_LINE   ? VK_POLYGON_MODE_LINE
                     : op.pipe.polygon_mode == GL_POINT ? VK_POLYGON_MODE_POINT
                                                        : VK_POLYGON_MODE_FILL;
    rs.cullMode =
        !op.pipe.cull_test ? VK_CULL_MODE_NONE
        : op.pipe.cull_face == GL_FRONT
            ? VK_CULL_MODE_FRONT_BIT
            : op.pipe.cull_face == GL_FRONT_AND_BACK
                  ? VK_CULL_MODE_FRONT_AND_BACK
                  : VK_CULL_MODE_BACK_BIT;
    // Vulkan's framebuffer is +Y-down (we keep the GL bottom-left flip in the
    // readout), so a GL front-face winding maps to the opposite Vk value.
    rs.frontFace = op.pipe.front_face == GL_CW ? VK_FRONT_FACE_COUNTER_CLOCKWISE
                                                : VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = ToVkSampleCount(op.samples);

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = op.pipe.depth_test ? VK_TRUE : VK_FALSE;
    ds.depthWriteEnable = op.pipe.depth_write ? VK_TRUE : VK_FALSE;
    ds.depthCompareOp = ToVkCompare(op.pipe.depth_func);
    ds.depthBoundsTestEnable = VK_FALSE;
    ds.stencilTestEnable = op.pipe.stencil_test ? VK_TRUE : VK_FALSE;

    VkStencilOpState front_st{};
    front_st.failOp = StencilOp(op.pipe.stencil_front_op_fail);
    front_st.passOp = StencilOp(op.pipe.stencil_front_op_zpass);
    front_st.depthFailOp = StencilOp(op.pipe.stencil_front_op_zfail);
    front_st.compareOp = ToVkCompare(op.pipe.stencil_front_func);
    front_st.compareMask = op.pipe.stencil_front_read_mask;
    front_st.writeMask = op.pipe.stencil_front_write_mask;
    front_st.reference = (uint32_t)op.pipe.stencil_front_ref;
    VkStencilOpState back_st = front_st;   // copy, override fields
    back_st.compareOp = ToVkCompare(op.pipe.stencil_back_func);
    back_st.compareMask = op.pipe.stencil_back_read_mask;
    back_st.writeMask = op.pipe.stencil_back_write_mask;
    back_st.failOp = StencilOp(op.pipe.stencil_back_op_fail);
    back_st.passOp = StencilOp(op.pipe.stencil_back_op_zpass);
    back_st.depthFailOp = StencilOp(op.pipe.stencil_back_op_zfail);
    back_st.reference = (uint32_t)op.pipe.stencil_back_ref;
    ds.front = front_st;
    ds.back = back_st;

    VkPipelineColorBlendAttachmentState cb_att{};
    cb_att.blendEnable = op.pipe.blend_enable ? VK_TRUE : VK_FALSE;
    cb_att.srcColorBlendFactor = ToVkBlend(op.pipe.blend_src_rgb);
    cb_att.dstColorBlendFactor = ToVkBlend(op.pipe.blend_dst_rgb);
    cb_att.colorBlendOp = ToBlendOp(op.pipe.blend_eq_rgb);
    cb_att.srcAlphaBlendFactor = ToVkBlend(op.pipe.blend_src_alpha);
    cb_att.dstAlphaBlendFactor = ToVkBlend(op.pipe.blend_dst_alpha);
    cb_att.alphaBlendOp = ToBlendOp(op.pipe.blend_eq_alpha);
    cb_att.colorWriteMask = ColorMask(op.pipe);
    // One blend state per colour attachment (MRT). Attachments that the
    // current draw buffers exclude get a zero write mask so they keep their
    // cleared/previous content.
    std::vector<VkPipelineColorBlendAttachmentState> cb_atts(
        op.color_count, cb_att);
    for (size_t i = 0; i < cb_atts.size(); ++i)
        if (!(op.draw_mask & (1u << i)))
            cb_atts[i].colorWriteMask = 0;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = op.color_count;
    cb.pAttachments = cb_atts.data();

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
    pg.pDepthStencilState = &ds;
    pg.pColorBlendState = &cb;
    pg.pDynamicState = &dyn_s;
    pg.layout = g.pipeline_layout;
    pg.renderPass = op.has_render_pass ? op.render_pass : g.renderpass;
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

uint64_t CreateProgram(const std::vector<uint32_t>& vs,
                       const std::vector<uint32_t>& fs) {
    if (!g.initialized || vs.empty() || fs.empty()) return 0;

    // The reference backend currently owns only binding 0 for Mithril's
    // synthetic loose-uniform block. Reject real GL uniform blocks explicitly
    // instead of silently packing them into the wrong descriptor allocation.
    try {
        auto has_user_uniform_block = [](const std::vector<uint32_t>& words) {
            spirv_cross::Compiler compiler(words.data(), words.size());
            for (const auto& block :
                 compiler.get_shader_resources().uniform_buffers) {
                if (compiler.get_decoration(
                        block.id, spv::DecorationBinding) != 0)
                    return true;
            }
            return false;
        };
        auto has_buffer_sampler = [](const std::vector<uint32_t>& words) {
            spirv_cross::Compiler compiler(words.data(), words.size());
            for (const auto& sampled :
                 compiler.get_shader_resources().sampled_images)
                if (compiler.get_type(sampled.type_id).image.dim ==
                    spv::DimBuffer)
                    return true;
            return false;
        };
        if (has_user_uniform_block(vs) || has_user_uniform_block(fs)) {
            ML_LOG_ERROR("vk: explicit GL uniform blocks are not supported by "
                         "the reference backend yet");
            return 0;
        }
        if (has_buffer_sampler(vs) || has_buffer_sampler(fs)) {
            ML_LOG_ERROR("vk: texture-buffer samplers are not supported by "
                         "the reference backend yet");
            return 0;
        }
    } catch (const std::exception& error) {
        ML_LOG_ERROR("vk: program resource reflection failed: %s", error.what());
        return 0;
    }

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

} // namespace mithril::vk
