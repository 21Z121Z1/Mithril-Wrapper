// Mithril-Wrapper - MG_Backend/DirectVulkan/Pipeline.cpp
// VkShaderModule creation (from SPIR-V) + VkGraphicsPipeline caching keyed by
// a hash signature built from (program, vertex format, attachment formats,
// blend state, primitive mode). Uses VK_KHR_dynamic_rendering so pipelines
// are created against a VkPipelineRenderingCreateInfo instead of a VkRenderPass.
#include "Pipeline.h"
#include "Device.h"
#include "Resources.h"
#include "../Backend.h"
#include "../../MG_Impl/Log.h"

#include <cstring>
#include <vector>

namespace mithril {
namespace vk {

std::unordered_map<GLuint, ProgramResources>& program_table() {
    static std::unordered_map<GLuint, ProgramResources> t;
    return t;
}

namespace {

// ---- GL blend factor -> VkBlendFactor ----
VkBlendFactor gl_blend_to_vk(GLenum f) {
    switch (f) {
        case GL_ZERO:                     return VK_BLEND_FACTOR_ZERO;
        case GL_ONE:                      return VK_BLEND_FACTOR_ONE;
        case GL_SRC_COLOR:                return VK_BLEND_FACTOR_SRC_COLOR;
        case GL_ONE_MINUS_SRC_COLOR:      return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case GL_DST_COLOR:                return VK_BLEND_FACTOR_DST_COLOR;
        case GL_ONE_MINUS_DST_COLOR:      return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case GL_SRC_ALPHA:                return VK_BLEND_FACTOR_SRC_ALPHA;
        case GL_ONE_MINUS_SRC_ALPHA:      return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case GL_DST_ALPHA:                return VK_BLEND_FACTOR_DST_ALPHA;
        case GL_ONE_MINUS_DST_ALPHA:      return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case GL_CONSTANT_COLOR:           return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case GL_ONE_MINUS_CONSTANT_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        case GL_CONSTANT_ALPHA:           return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        case GL_ONE_MINUS_CONSTANT_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        case GL_SRC_ALPHA_SATURATE:       return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        case GL_SRC1_COLOR:               return VK_BLEND_FACTOR_SRC1_COLOR;
        case GL_ONE_MINUS_SRC1_COLOR:     return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
        case GL_SRC1_ALPHA:               return VK_BLEND_FACTOR_SRC1_ALPHA;
        case GL_ONE_MINUS_SRC1_ALPHA:     return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
        default:                          return VK_BLEND_FACTOR_ONE;
    }
}

// ---- GL primitive mode -> VkPrimitiveTopology ----
VkPrimitiveTopology gl_prim_to_vk(GLenum m) {
    switch (m) {
        case GL_POINTS:         return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case GL_LINES:          return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case GL_LINE_STRIP:     return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case GL_LINE_LOOP:      return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP; // approx
        case GL_TRIANGLES:      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case GL_TRIANGLE_STRIP: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case GL_TRIANGLE_FAN:   return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
        default:                return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

// ---- GL attribute type -> VkFormat ----
VkFormat attrib_type_to_vk_format(GLenum type, int size, bool normalized, bool integer) {
    if (integer) {
        switch (type) {
            case GL_UNSIGNED_BYTE:  switch (size) { case 1: return VK_FORMAT_R8_UINT;   case 2: return VK_FORMAT_R8G8_UINT;   case 3: return VK_FORMAT_R8G8B8_UINT;   case 4: return VK_FORMAT_R8G8B8A8_UINT; }
            case GL_INT:            switch (size) { case 1: return VK_FORMAT_R32_SINT;  case 2: return VK_FORMAT_R32G32_SINT; case 3: return VK_FORMAT_R32G32B32_SINT; case 4: return VK_FORMAT_R32G32B32A32_SINT; }
            case GL_UNSIGNED_INT:   switch (size) { case 1: return VK_FORMAT_R32_UINT;  case 2: return VK_FORMAT_R32G32_UINT; case 3: return VK_FORMAT_R32G32B32_UINT; case 4: return VK_FORMAT_R32G32B32A32_UINT; }
            case GL_SHORT:          switch (size) { case 1: return VK_FORMAT_R16_SINT;  case 2: return VK_FORMAT_R16G16_SINT; case 3: return VK_FORMAT_R16G16B16_SINT; case 4: return VK_FORMAT_R16G16B16A16_SINT; }
            case GL_UNSIGNED_SHORT: switch (size) { case 1: return VK_FORMAT_R16_UINT;  case 2: return VK_FORMAT_R16G16_UINT; case 3: return VK_FORMAT_R16G16B16_UINT; case 4: return VK_FORMAT_R16G16B16A16_UINT; }
            default: break;
        }
    }
    switch (type) {
        case GL_FLOAT:          switch (size) { case 1: return VK_FORMAT_R32_SFLOAT;   case 2: return VK_FORMAT_R32G32_SFLOAT;   case 3: return VK_FORMAT_R32G32B32_SFLOAT;   case 4: return VK_FORMAT_R32G32B32A32_SFLOAT; }
        case GL_HALF_FLOAT:     switch (size) { case 1: return VK_FORMAT_R16_SFLOAT;   case 2: return VK_FORMAT_R16G16_SFLOAT;   case 3: return VK_FORMAT_R16G16B16_SFLOAT;   case 4: return VK_FORMAT_R16G16B16A16_SFLOAT; }
        case GL_DOUBLE:         switch (size) { case 1: return VK_FORMAT_R64_SFLOAT;   case 2: return VK_FORMAT_R64G64_SFLOAT;  case 3: return VK_FORMAT_R64G64B64_SFLOAT;  case 4: return VK_FORMAT_R64G64B64A64_SFLOAT; }
        case GL_UNSIGNED_BYTE:
            if (normalized) switch (size) { case 1: return VK_FORMAT_R8_UNORM;  case 2: return VK_FORMAT_R8G8_UNORM;  case 3: return VK_FORMAT_R8G8B8_UNORM;  case 4: return VK_FORMAT_R8G8B8A8_UNORM; }
            else            switch (size) { case 1: return VK_FORMAT_R8_UINT;   case 2: return VK_FORMAT_R8G8_UINT;   case 3: return VK_FORMAT_R8G8B8_UINT;   case 4: return VK_FORMAT_R8G8B8A8_UINT; }
        case GL_BYTE:
            if (normalized) switch (size) { case 1: return VK_FORMAT_R8_SNORM;  case 2: return VK_FORMAT_R8G8_SNORM;  case 3: return VK_FORMAT_R8G8B8_SNORM;  case 4: return VK_FORMAT_R8G8B8A8_SNORM; }
            else            switch (size) { case 1: return VK_FORMAT_R8_SINT;   case 2: return VK_FORMAT_R8G8_SINT;   case 3: return VK_FORMAT_R8G8B8_SINT;   case 4: return VK_FORMAT_R8G8B8A8_SINT; }
        case GL_UNSIGNED_SHORT:
            if (normalized) switch (size) { case 1: return VK_FORMAT_R16_UNORM; case 2: return VK_FORMAT_R16G16_UNORM; case 3: return VK_FORMAT_R16G16B16_UNORM; case 4: return VK_FORMAT_R16G16B16A16_UNORM; }
            else            switch (size) { case 1: return VK_FORMAT_R16_UINT;  case 2: return VK_FORMAT_R16G16_UINT;  case 3: return VK_FORMAT_R16G16B16_UINT;  case 4: return VK_FORMAT_R16G16B16A16_UINT; }
        case GL_SHORT:
            if (normalized) switch (size) { case 1: return VK_FORMAT_R16_SNORM; case 2: return VK_FORMAT_R16G16_SNORM; case 3: return VK_FORMAT_R16G16B16_SNORM; case 4: return VK_FORMAT_R16G16B16A16_SNORM; }
            else            switch (size) { case 1: return VK_FORMAT_R16_SINT;  case 2: return VK_FORMAT_R16G16_SINT;  case 3: return VK_FORMAT_R16G16B16_SINT;  case 4: return VK_FORMAT_R16G16B16A16_SINT; }
        case GL_INT_2_10_10_10_REV:
            if (normalized) return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
            return VK_FORMAT_A2B10G10R10_UINT_PACK32;
        case GL_UNSIGNED_INT_2_10_10_10_REV:
            if (normalized) return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
            return VK_FORMAT_A2B10G10R10_UINT_PACK32;
        default: break;
    }
    return VK_FORMAT_R32G32B32A32_SFLOAT;
}

// GL compare func -> VkCompareOp
VkCompareOp gl_compare_to_vk(GLenum f) {
    switch (f) {
        case GL_NEVER:    return VK_COMPARE_OP_NEVER;
        case GL_LESS:     return VK_COMPARE_OP_LESS;
        case GL_EQUAL:    return VK_COMPARE_OP_EQUAL;
        case GL_LEQUAL:   return VK_COMPARE_OP_LESS_OR_EQUAL;
        case GL_GREATER:  return VK_COMPARE_OP_GREATER;
        case GL_NOTEQUAL: return VK_COMPARE_OP_NOT_EQUAL;
        case GL_GEQUAL:   return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case GL_ALWAYS:   return VK_COMPARE_OP_ALWAYS;
        default:          return VK_COMPARE_OP_LESS;
    }
}

// FNV-1a 64-bit hash over the pipeline signature.
uint64_t hash_signature(GLuint program, const MGVertexAttrib* attribs, int attrib_count,
                        const VkFormat* color_formats, int color_count,
                        VkFormat depth_format, int blend_enabled,
                        GLenum blend_src, GLenum blend_dst, GLenum prim) {
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&](const void* p, size_t n) {
        const uint8_t* b = (const uint8_t*)p;
        for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ULL; }
    };
    mix(&program, sizeof(program));
    mix(&attrib_count, sizeof(attrib_count));
    for (int i = 0; i < attrib_count; ++i) {
        mix(&attribs[i].location, sizeof(attribs[i].location));
        mix(&attribs[i].size, sizeof(attribs[i].size));
        mix(&attribs[i].type, sizeof(attribs[i].type));
        mix(&attribs[i].normalized, sizeof(attribs[i].normalized));
        mix(&attribs[i].integer, sizeof(attribs[i].integer));
        mix(&attribs[i].stride, sizeof(attribs[i].stride));
    }
    mix(&color_count, sizeof(color_count));
    for (int i = 0; i < color_count; ++i) mix(&color_formats[i], sizeof(color_formats[i]));
    mix(&depth_format, sizeof(depth_format));
    mix(&blend_enabled, sizeof(blend_enabled));
    mix(&blend_src, sizeof(blend_src));
    mix(&blend_dst, sizeof(blend_dst));
    mix(&prim, sizeof(prim));
    return h;
}

VkShaderModule create_module(const uint32_t* spirv, int word_count) {
    if (!spirv || word_count <= 0) return VK_NULL_HANDLE;
    Backend* b = backend();
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = (size_t)word_count * sizeof(uint32_t);
    ci.pCode = spirv;
    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(b->device, &ci, nullptr, &mod) != VK_SUCCESS) {
        MITHRIL_LOG_WARN("vk", "vkCreateShaderModule failed (words=%d)", word_count);
        return VK_NULL_HANDLE;
    }
    return mod;
}

} // namespace

VkPipeline get_or_create_pipeline(GLuint program,
                                  const uint32_t* vertex_spirv, int vertex_word_count,
                                  const uint32_t* fragment_spirv, int fragment_word_count,
                                  const MGVertexAttrib* attribs, int attrib_count,
                                  const VkFormat* color_formats, int color_count,
                                  VkFormat depth_format,
                                  int blend_enabled, GLenum blend_src, GLenum blend_dst,
                                  GLenum gl_primitive_mode) {
    Backend* b = backend();
    if (!b->initialized || program == 0) return VK_NULL_HANDLE;

    auto& tbl = program_table();
    ProgramResources& pr = tbl[program];

    // (Re)build shader modules if missing.
    if (pr.vertexModule == VK_NULL_HANDLE && vertex_spirv && vertex_word_count > 0) {
        pr.vertexModule = create_module(vertex_spirv, vertex_word_count);
    }
    if (pr.fragmentModule == VK_NULL_HANDLE && fragment_spirv && fragment_word_count > 0) {
        pr.fragmentModule = create_module(fragment_spirv, fragment_word_count);
    }
    if (pr.vertexModule == VK_NULL_HANDLE) return VK_NULL_HANDLE;

    uint64_t sig = hash_signature(program, attribs, attrib_count, color_formats,
                                  color_count, depth_format, blend_enabled,
                                  blend_src, blend_dst, gl_primitive_mode);
    auto it = pr.pipelines.find(sig);
    if (it != pr.pipelines.end() && it->second != VK_NULL_HANDLE) return it->second;

    // ---- Vertex input state ----
    std::vector<VkVertexInputBindingDescription> bindDescs;
    std::vector<VkVertexInputAttributeDescription> attrDescs;
    // Group attributes by their backing buffer name (one binding per VBO).
    // Simplified: one binding per attribute (binding index == location).
    for (int i = 0; i < attrib_count; ++i) {
        const MGVertexAttrib& a = attribs[i];
        if (!a.enabled) continue;
        VkVertexInputBindingDescription bd{};
        bd.binding = (uint32_t)a.location;
        bd.stride = a.stride > 0 ? (uint32_t)a.stride : 0;
        bd.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        bindDescs.push_back(bd);

        VkVertexInputAttributeDescription ad{};
        ad.location = (uint32_t)a.location;
        ad.binding = (uint32_t)a.location;
        ad.format = attrib_type_to_vk_format(a.type, a.size, a.normalized != 0, a.integer != 0);
        ad.offset = (uint32_t)a.offset;
        attrDescs.push_back(ad);
    }

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = (uint32_t)bindDescs.size();
    vertexInput.pVertexBindingDescriptions = bindDescs.data();
    vertexInput.vertexAttributeDescriptionCount = (uint32_t)attrDescs.size();
    vertexInput.pVertexAttributeDescriptions = attrDescs.data();

    // ---- Input assembly ----
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = gl_prim_to_vk(gl_primitive_mode);
    ia.primitiveRestartEnable = VK_FALSE;

    // ---- Viewport / scissor (dynamic) ----
    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.pViewports = nullptr;   // dynamic
    vp.scissorCount = 1;
    vp.pScissors = nullptr;    // dynamic

    // ---- Rasterizer ----
    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.depthClampEnable = VK_FALSE;
    rs.rasterizerDiscardEnable = VK_FALSE;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;        // dynamic via vkCmdSetCullMode
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; // dynamic
    rs.depthBiasEnable = VK_FALSE;
    rs.lineWidth = 1.0f;

    // ---- Multisample ----
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    ms.minSampleShading = 1.0f;

    // ---- Depth / stencil ----
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;            // dynamic compare op + write mask
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;  // dynamic
    ds.depthBoundsTestEnable = VK_FALSE;
    ds.stencilTestEnable = VK_FALSE;

    // ---- Color blend ----
    VkPipelineColorBlendAttachmentState cbAttach{};
    cbAttach.blendEnable = blend_enabled ? VK_TRUE : VK_FALSE;
    if (blend_enabled) {
        cbAttach.srcColorBlendFactor = gl_blend_to_vk(blend_src);
        cbAttach.dstColorBlendFactor = gl_blend_to_vk(blend_dst);
        cbAttach.colorBlendOp = VK_BLEND_OP_ADD;
        cbAttach.srcAlphaBlendFactor = gl_blend_to_vk(blend_src);
        cbAttach.dstAlphaBlendFactor = gl_blend_to_vk(blend_dst);
        cbAttach.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    cbAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.logicOpEnable = VK_FALSE;
    cb.attachmentCount = color_count > 0 ? (uint32_t)color_count : 1;
    cb.pAttachments = &cbAttach;

    // ---- Dynamic state ----
    VkDynamicState dynStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_CULL_MODE,
        VK_DYNAMIC_STATE_FRONT_FACE,
        VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
        VK_DYNAMIC_STATE_BLEND_CONSTANTS,
        VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT,
    };
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = (uint32_t)(sizeof(dynStates) / sizeof(dynStates[0])) - 1; // skip COLOR_WRITE_ENABLE_EXT (extension)
    dyn.pDynamicStates = dynStates;

    // ---- Shader stages ----
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    VkPipelineShaderStageCreateInfo vsStage{};
    vsStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vsStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vsStage.module = pr.vertexModule;
    vsStage.pName = "main";
    stages.push_back(vsStage);
    if (pr.fragmentModule != VK_NULL_HANDLE) {
        VkPipelineShaderStageCreateInfo fsStage{};
        fsStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fsStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fsStage.module = pr.fragmentModule;
        fsStage.pName = "main";
        stages.push_back(fsStage);
    }

    // ---- Dynamic rendering attachment info (Vulkan 1.2 + VK_KHR_dynamic_rendering) ----
    VkFormat colorFmts[8] = {};
    for (int i = 0; i < color_count && i < 8; ++i) colorFmts[i] = color_formats[i];
    VkPipelineRenderingCreateInfo renderingCI{};
    renderingCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingCI.colorAttachmentCount = color_count > 0 ? (uint32_t)color_count : 1;
    renderingCI.pColorAttachmentFormats = colorFmts;
    renderingCI.depthAttachmentFormat = depth_format;
    renderingCI.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    // ---- Graphics pipeline ----
    VkGraphicsPipelineCreateInfo gi{};
    gi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gi.pNext = &renderingCI;
    gi.stageCount = (uint32_t)stages.size();
    gi.pStages = stages.data();
    gi.pVertexInputState = &vertexInput;
    gi.pInputAssemblyState = &ia;
    gi.pViewportState = &vp;
    gi.pRasterizationState = &rs;
    gi.pMultisampleState = &ms;
    gi.pDepthStencilState = &ds;
    gi.pColorBlendState = &cb;
    gi.pDynamicState = &dyn;
    gi.layout = VK_NULL_HANDLE;   // TODO: pipeline layout (see note below)
    gi.renderPass = VK_NULL_HANDLE;
    gi.subpass = 0;

    // NOTE: a real pipeline needs a VkPipelineLayout describing descriptor set
    // layouts (uniform buffers, sampled textures). For bring-up we create a
    // bare layout with zero sets so that vkCreateGraphicsPipelines succeeds
    // and the pipeline is usable for vertex-only draws. Full descriptor
    // binding is iterated post-merge.
    static VkPipelineLayout g_emptyLayout = VK_NULL_HANDLE;
    if (g_emptyLayout == VK_NULL_HANDLE) {
        VkPipelineLayoutCreateInfo plci{};
        plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        vkCreatePipelineLayout(b->device, &plci, nullptr, &g_emptyLayout);
    }
    gi.layout = g_emptyLayout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult r = vkCreateGraphicsPipelines(b->device, b->pipelineCache, 1, &gi,
                                           nullptr, &pipeline);
    if (r != VK_SUCCESS) {
        MITHRIL_LOG_WARN("vk", "vkCreateGraphicsPipelines failed (rc=%d)", (int)r);
        return VK_NULL_HANDLE;
    }
    pr.pipelines[sig] = pipeline;
    return pipeline;
}

void delete_program_resources(GLuint program) {
    Backend* b = backend();
    auto& tbl = program_table();
    auto it = tbl.find(program);
    if (it == tbl.end()) return;
    ProgramResources& pr = it->second;
    if (b->device) vkDeviceWaitIdle(b->device);
    for (auto& kv : pr.pipelines) {
        if (kv.second) vkDestroyPipeline(b->device, kv.second, nullptr);
    }
    pr.pipelines.clear();
    if (pr.vertexModule)   { vkDestroyShaderModule(b->device, pr.vertexModule, nullptr);   pr.vertexModule = VK_NULL_HANDLE; }
    if (pr.fragmentModule) { vkDestroyShaderModule(b->device, pr.fragmentModule, nullptr); pr.fragmentModule = VK_NULL_HANDLE; }
    tbl.erase(it);
}

} // namespace vk
} // namespace mithril

// ===========================================================================
// Public C API wrappers (declared in MG_Backend/Backend.h)
// ===========================================================================
extern "C" {

VkPipeline backend_get_or_create_pipeline(GLuint program,
                                          const uint32_t* vertex_spirv, int vertex_word_count,
                                          const uint32_t* fragment_spirv, int fragment_word_count,
                                          const MGVertexAttrib* attribs, int attrib_count,
                                          const VkFormat* color_formats, int color_count,
                                          VkFormat depth_format,
                                          int blend_enabled, GLenum blend_src, GLenum blend_dst,
                                          GLenum gl_primitive_mode) {
    return mithril::vk::get_or_create_pipeline(program, vertex_spirv, vertex_word_count,
                                               fragment_spirv, fragment_word_count,
                                               attribs, attrib_count,
                                               color_formats, color_count, depth_format,
                                               blend_enabled, blend_src, blend_dst,
                                               gl_primitive_mode);
}

void backend_delete_program_resources(GLuint program) {
    mithril::vk::delete_program_resources(program);
}

} // extern "C"
