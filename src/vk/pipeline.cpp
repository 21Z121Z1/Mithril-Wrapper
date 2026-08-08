// Mithril-Wrapper Vulkan backend -- program/pipeline objects (M2-VK).
// SPIR-V module creation, UBO reflection (SPIRV-Cross), pipeline
// key building and the cached GetOrCreatePipeline factory.

#include "internal.h"

#include <algorithm>
#include <exception>
#include <string>

#include <spirv_cross.hpp>

namespace mithril::vk {

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

} // namespace mithril::vk
