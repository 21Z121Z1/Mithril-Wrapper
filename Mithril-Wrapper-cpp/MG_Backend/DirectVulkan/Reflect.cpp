// Mithril-Wrapper - MG_Backend/DirectVulkan/Reflect.cpp
// SPIR-V descriptor reflection (SPIRV-Cross) — pure-logic helpers extracted
// from DescriptorSet.cpp. Depends only on SPIRV-Cross + Vulkan headers, so it
// can be linked into the unit-test binary without pulling in Device/Pipeline/
// Backend (which would require a real VkInstance).
#include "Reflect.h"

// samplerTarget stores GLenum values; pull the constants from the project's own
// GL header so Reflect.h can stay free of a GL include (it is linked standalone
// by the unit-test binary).
#include <GL/glcorearb.h>

#include <spirv_cross.hpp>
// spirv_cross.hpp transitively pulls in SPIRV-Cross's bundled spirv.hpp,
// which defines the spv:: namespace (spv::DecorationBinding, etc.) used below.

#include <cstdio>
#include <algorithm>
#include <regex>
#include <unordered_map>
#include <unordered_set>

namespace mithril {
namespace vk {

namespace {

std::string without_comments(const std::string& source) {
    std::string output = source;
    bool line = false;
    bool block = false;
    for (size_t i = 0; i < output.size(); ++i) {
        if (line) {
            if (output[i] == '\n') line = false;
            else output[i] = ' ';
        } else if (block) {
            if (i + 1 < output.size() && output[i] == '*' && output[i + 1] == '/') {
                output[i] = output[i + 1] = ' ';
                block = false;
                ++i;
            } else if (output[i] != '\n') {
                output[i] = ' ';
            }
        } else if (i + 1 < output.size() && output[i] == '/' && output[i + 1] == '/') {
            output[i] = output[i + 1] = ' ';
            line = true;
            ++i;
        } else if (i + 1 < output.size() && output[i] == '/' && output[i + 1] == '*') {
            output[i] = output[i + 1] = ' ';
            block = true;
            ++i;
        }
    }
    return output;
}

std::unordered_set<std::string> explicit_location_names(const std::string& source,
                                                        const char* storage) {
    const std::string clean = without_comments(source);
    static const std::regex location(
        R"(\blayout\s*\([^)]*\blocation\s*=\s*[0-9]+[^)]*\))",
        std::regex::optimize);
    static const std::regex identifier(R"([A-Za-z_]\w*)", std::regex::optimize);
    const std::regex storage_word("\\b" + std::string(storage) + "\\b",
                                  std::regex::optimize);
    std::unordered_set<std::string> names;
    size_t begin = 0;
    while (begin < clean.size()) {
        const size_t semicolon = clean.find(';', begin);
        const std::string statement = clean.substr(
            begin, semicolon == std::string::npos ? std::string::npos : semicolon - begin);
        if (std::regex_search(statement, location) &&
            std::regex_search(statement, storage_word)) {
            for (std::sregex_iterator it(statement.begin(), statement.end(), identifier), end;
                 it != end; ++it) {
                names.insert(it->str());
            }
        }
        if (semicolon == std::string::npos) break;
        begin = semicolon + 1;
    }
    return names;
}

uint32_t interface_location_span(const spirv_cross::SPIRType& type) {
    uint64_t span = std::max<uint32_t>(type.columns, 1);
    for (uint32_t dimension : type.array)
        span *= std::max<uint32_t>(dimension, 1);
    return span > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(span);
}

bool rewrite_location(std::vector<uint32_t>& words, uint32_t id, uint32_t location) {
    for (size_t cursor = 5; cursor < words.size();) {
        const uint32_t header = words[cursor];
        const uint16_t count = static_cast<uint16_t>(header >> 16);
        const uint16_t opcode = static_cast<uint16_t>(header & 0xffffu);
        if (!count || cursor + count > words.size()) return false;
        if (opcode == spv::OpDecorate && count >= 4 &&
            words[cursor + 1] == id &&
            words[cursor + 2] == spv::DecorationLocation) {
            words[cursor + 3] = location;
            return true;
        }
        cursor += count;
    }
    return false;
}

struct InterfaceShape {
    uint32_t location = 0;
    uint32_t span = 1;
    spirv_cross::SPIRType::BaseType base = spirv_cross::SPIRType::Unknown;
    uint32_t vecsize = 1;
    uint32_t columns = 1;
    std::vector<uint32_t> array;
};

InterfaceShape interface_shape(spirv_cross::Compiler& compiler,
                               const spirv_cross::Resource& resource) {
    const auto& type = compiler.get_type(resource.type_id);
    InterfaceShape out;
    out.location = compiler.get_decoration(resource.id, spv::DecorationLocation);
    out.span = interface_location_span(type);
    out.base = type.basetype;
    out.vecsize = type.vecsize;
    out.columns = type.columns;
    out.array.assign(type.array.begin(), type.array.end());
    return out;
}

bool same_shape(const InterfaceShape& a, const InterfaceShape& b) {
    return a.span == b.span && a.base == b.base && a.vecsize == b.vecsize &&
           a.columns == b.columns && a.array == b.array;
}

} // namespace


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
            // FIX (Main-menu panorama cubemap GPU fault root cause - sampler
            // type): record the sampler's GL target type (2D/Cube/3D/Array)
            // derived from SPIR-V image.dim. The descriptor binder picks the
            // texture from the matching texture-unit slot by this type. The old
            // code used boundTextureForUnit which unconditionally preferred the
            // 2D slot: on the main menu unit 0 often still holds a GUI 2D
            // binding, so the panorama's samplerCube got a 2D view -> MoltenVK
            // viewType mismatch -> undefined sampling / GPU fault. zink selects
            // the slot by the declared type, which is why it renders.
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

bool align_stage_interface_locations(std::vector<uint32_t>& vertex_spirv,
                                     std::vector<uint32_t>& fragment_spirv,
                                     const std::string& vertex_source,
                                     const std::string& fragment_source,
                                     std::string& error) {
    if (vertex_spirv.empty() || fragment_spirv.empty()) {
        error = "missing stage SPIR-V for interface matching";
        return false;
    }
    try {
        spirv_cross::Compiler vertex(vertex_spirv);
        spirv_cross::Compiler fragment(fragment_spirv);
        const auto vertex_resources = vertex.get_shader_resources();
        const auto fragment_resources = fragment.get_shader_resources();
        const auto explicit_vertex = explicit_location_names(vertex_source, "out");
        const auto explicit_fragment = explicit_location_names(fragment_source, "in");

        struct Output {
            uint32_t id = 0;
            InterfaceShape shape;
            bool explicit_location = false;
        };
        std::unordered_map<std::string, Output> outputs;
        for (const auto& resource : vertex_resources.stage_outputs) {
            if (resource.name.empty() ||
                !vertex.has_decoration(resource.id, spv::DecorationLocation))
                continue;
            Output output;
            output.id = resource.id;
            output.shape = interface_shape(vertex, resource);
            output.explicit_location = explicit_vertex.count(resource.name) != 0;
            outputs[resource.name] = std::move(output);
        }

        for (const auto& resource : fragment_resources.stage_inputs) {
            if (resource.name.empty() ||
                !fragment.has_decoration(resource.id, spv::DecorationLocation))
                continue;
            auto output = outputs.find(resource.name);
            if (output == outputs.end()) continue;

            const InterfaceShape input = interface_shape(fragment, resource);
            const Output& linked = output->second;
            if (!same_shape(input, linked.shape)) {
                error = "cross-stage interface type mismatch for " + resource.name;
                return false;
            }

            const bool input_explicit = explicit_fragment.count(resource.name) != 0;
            if (linked.explicit_location && input_explicit) {
                if (input.location != linked.shape.location) {
                    error = "explicit cross-stage location mismatch for " + resource.name;
                    return false;
                }
                continue;
            }
            if (input.location == linked.shape.location) continue;

            if (input_explicit && !linked.explicit_location) {
                if (!rewrite_location(vertex_spirv, linked.id, input.location)) {
                    error = "vertex interface has no mutable Location decoration: " +
                            resource.name;
                    return false;
                }
            } else {
                if (!rewrite_location(fragment_spirv, resource.id,
                                      linked.shape.location)) {
                    error = "fragment interface has no mutable Location decoration: " +
                            resource.name;
                    return false;
                }
            }
        }
        return true;
    } catch (const std::exception& exception) {
        error = std::string("cross-stage interface remap failed: ") + exception.what();
        return false;
    }
}

bool align_vertex_output_locations(const std::vector<uint32_t>& reference_spirv,
                                   std::vector<uint32_t>& target_spirv,
                                   std::string& error) {
    if (reference_spirv.empty() || target_spirv.empty()) {
        error = "missing vertex SPIR-V variant for interface matching";
        return false;
    }
    try {
        spirv_cross::Compiler reference(reference_spirv);
        spirv_cross::Compiler target(target_spirv);
        const auto reference_resources = reference.get_shader_resources();
        const auto target_resources = target.get_shader_resources();

        std::unordered_map<std::string, InterfaceShape> canonical;
        for (const auto& resource : reference_resources.stage_outputs) {
            if (resource.name.empty() ||
                !reference.has_decoration(resource.id, spv::DecorationLocation))
                continue;
            canonical[resource.name] = interface_shape(reference, resource);
        }
        for (const auto& resource : target_resources.stage_outputs) {
            if (resource.name.empty() ||
                !target.has_decoration(resource.id, spv::DecorationLocation))
                continue;
            auto it = canonical.find(resource.name);
            if (it == canonical.end()) continue;
            const InterfaceShape current = interface_shape(target, resource);
            if (!same_shape(current, it->second)) {
                error = "vertex variant interface type mismatch for " + resource.name;
                return false;
            }
            if (current.location != it->second.location &&
                !rewrite_location(target_spirv, resource.id, it->second.location)) {
                error = "vertex variant has no mutable Location decoration: " +
                        resource.name;
                return false;
            }
        }
        return true;
    } catch (const std::exception& exception) {
        error = std::string("vertex variant interface remap failed: ") + exception.what();
        return false;
    }
}


} // namespace vk
} // namespace mithril
