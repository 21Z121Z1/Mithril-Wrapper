// Mithril-Wrapper - MG_Backend/DirectVulkan/Reflect.cpp
// SPIR-V descriptor reflection (SPIRV-Cross) — pure-logic helpers extracted
// from DescriptorSet.cpp. Depends only on SPIRV-Cross + Vulkan headers, so it
// can be linked into the unit-test binary without pulling in Device/Pipeline/
// Backend (which would require a real VkInstance).
#include "Reflect.h"

// samplerTarget 存 GLenum 数值；用项目自带 GL 头获取常量（Reflect.h 保持
// 无 GL 依赖以便独立链接 unit-test）。
#include <GL/glcorearb.h>

#include <spirv_cross.hpp>
// spirv_cross.hpp transitively pulls in SPIRV-Cross's bundled spirv.hpp,
// which defines the spv:: namespace (spv::DecorationBinding, etc.) used below.

#include <cstdio>

namespace mithril {
namespace vk {

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
            // FIX (主菜单 panorama cubemap GPU fault 根因 - sampler 类型):
            // 记录 sampler 的 GL target 类型（2D/Cube/3D/Array），descriptor
            // 绑定时按类型从对应的 texture unit slot 取纹理。旧实现用
            // boundTextureForUnit 无条件优先取 2D slot：主菜单里 unit 0 常
            // 残留 GUI 2D 纹理绑定，panorama 的 samplerCube 会错误绑定 2D
            // view → MoltenVK viewType 不匹配 → 采样 undefined/黑/GPU fault。
            // zink 按 shader 声明类型正确选 slot，因此正常。
            switch (t.image.dim) {
                case spv::Dim1D:    b.samplerTarget = t.image.arrayed ? GL_TEXTURE_1D_ARRAY : GL_TEXTURE_1D; break;
                case spv::Dim3D:    b.samplerTarget = GL_TEXTURE_3D; break;
                case spv::DimCube:  b.samplerTarget = t.image.arrayed ? 0x9009u /* GL_TEXTURE_CUBE_MAP_ARRAY */ : GL_TEXTURE_CUBE_MAP; break;
                case spv::Dim2D:
                default:            b.samplerTarget = t.image.arrayed ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D; break;
            }
            out.push_back(std::move(b));
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "mithril: SPIRV-Cross reflection failed: %s\n", e.what());
    }
    return out;
}

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

} // namespace vk
} // namespace mithril
