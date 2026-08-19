// Mithril-Wrapper shader module -- program/uniform/attribute reflection.
// SPIRV-Cross reflects the shared linked-stage representation. Loose
// uniforms remain location-addressable, while real uniform blocks keep their
// GL namespace and backend-internal stage bindings separate.

#include <shader/shader.h>

#include <GL/glcorearb.h>

#include <spirv_cross.hpp>

#include <util/log.h>

#include <algorithm>
#include <regex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mithril::shader {
namespace {

GLenum GLTypeFor(const spirv_cross::SPIRType& type) {
    using Base = spirv_cross::SPIRType::BaseType;
    if (type.columns > 1 && type.basetype == Base::Float) {
        static const GLenum matrices[3][3] = {
            {GL_FLOAT_MAT2, GL_FLOAT_MAT2x3, GL_FLOAT_MAT2x4},
            {GL_FLOAT_MAT3x2, GL_FLOAT_MAT3, GL_FLOAT_MAT3x4},
            {GL_FLOAT_MAT4x2, GL_FLOAT_MAT4x3, GL_FLOAT_MAT4},
        };
        if (type.columns >= 2 && type.columns <= 4 &&
            type.vecsize >= 2 && type.vecsize <= 4)
            return matrices[type.columns - 2][type.vecsize - 2];
    }
    const uint32_t width = std::clamp<uint32_t>(type.vecsize, 1, 4);
    switch (type.basetype) {
        case Base::Float: {
            static const GLenum values[] = {
                GL_FLOAT, GL_FLOAT_VEC2, GL_FLOAT_VEC3, GL_FLOAT_VEC4};
            return values[width - 1];
        }
        case Base::Int: {
            static const GLenum values[] = {
                GL_INT, GL_INT_VEC2, GL_INT_VEC3, GL_INT_VEC4};
            return values[width - 1];
        }
        case Base::UInt: {
            static const GLenum values[] = {GL_UNSIGNED_INT,
                GL_UNSIGNED_INT_VEC2, GL_UNSIGNED_INT_VEC3,
                GL_UNSIGNED_INT_VEC4};
            return values[width - 1];
        }
        case Base::Boolean: {
            static const GLenum values[] = {
                GL_BOOL, GL_BOOL_VEC2, GL_BOOL_VEC3, GL_BOOL_VEC4};
            return values[width - 1];
        }
        default: return GL_FLOAT;
    }
}

GLenum SamplerTypeFor(spirv_cross::Compiler& compiler,
                      const spirv_cross::Resource& resource) {
    using Base = spirv_cross::SPIRType::BaseType;
    const auto& type = compiler.get_type(resource.type_id);
    const Base sampled_base = compiler.get_type(type.image.type).basetype;
    auto scalar = [&](GLenum floating, GLenum signed_integer,
                      GLenum unsigned_integer) {
        if (sampled_base == Base::Int) return signed_integer;
        if (sampled_base == Base::UInt) return unsigned_integer;
        return floating;
    };
    switch (type.image.dim) {
        case spv::DimBuffer:
            return scalar(GL_SAMPLER_BUFFER, GL_INT_SAMPLER_BUFFER,
                          GL_UNSIGNED_INT_SAMPLER_BUFFER);
        case spv::Dim1D:
            if (type.image.arrayed)
                return type.image.depth
                    ? GL_SAMPLER_1D_ARRAY_SHADOW
                    : scalar(GL_SAMPLER_1D_ARRAY, GL_INT_SAMPLER_1D_ARRAY,
                             GL_UNSIGNED_INT_SAMPLER_1D_ARRAY);
            return type.image.depth
                ? GL_SAMPLER_1D_SHADOW
                : scalar(GL_SAMPLER_1D, GL_INT_SAMPLER_1D,
                         GL_UNSIGNED_INT_SAMPLER_1D);
        case spv::Dim2D:
            if (type.image.ms)
                return type.image.arrayed
                    ? scalar(GL_SAMPLER_2D_MULTISAMPLE_ARRAY,
                             GL_INT_SAMPLER_2D_MULTISAMPLE_ARRAY,
                             GL_UNSIGNED_INT_SAMPLER_2D_MULTISAMPLE_ARRAY)
                    : scalar(GL_SAMPLER_2D_MULTISAMPLE,
                             GL_INT_SAMPLER_2D_MULTISAMPLE,
                             GL_UNSIGNED_INT_SAMPLER_2D_MULTISAMPLE);
            if (type.image.arrayed)
                return type.image.depth
                    ? GL_SAMPLER_2D_ARRAY_SHADOW
                    : scalar(GL_SAMPLER_2D_ARRAY, GL_INT_SAMPLER_2D_ARRAY,
                             GL_UNSIGNED_INT_SAMPLER_2D_ARRAY);
            return type.image.depth
                ? GL_SAMPLER_2D_SHADOW
                : scalar(GL_SAMPLER_2D, GL_INT_SAMPLER_2D,
                         GL_UNSIGNED_INT_SAMPLER_2D);
        case spv::Dim3D:
            return scalar(GL_SAMPLER_3D, GL_INT_SAMPLER_3D,
                          GL_UNSIGNED_INT_SAMPLER_3D);
        case spv::DimCube:
            if (type.image.arrayed)
                return type.image.depth
                    ? GL_SAMPLER_CUBE_MAP_ARRAY_SHADOW
                    : scalar(GL_SAMPLER_CUBE_MAP_ARRAY,
                             GL_INT_SAMPLER_CUBE_MAP_ARRAY,
                             GL_UNSIGNED_INT_SAMPLER_CUBE_MAP_ARRAY);
            return type.image.depth
                ? GL_SAMPLER_CUBE_SHADOW
                : scalar(GL_SAMPLER_CUBE, GL_INT_SAMPLER_CUBE,
                         GL_UNSIGNED_INT_SAMPLER_CUBE);
        case spv::DimRect:
            return type.image.depth
                ? GL_SAMPLER_2D_RECT_SHADOW
                : scalar(GL_SAMPLER_2D_RECT, GL_INT_SAMPLER_2D_RECT,
                         GL_UNSIGNED_INT_SAMPLER_2D_RECT);
        default:
            return GL_SAMPLER_2D;
    }
}

GLint ArraySize(const spirv_cross::SPIRType& type) {
    uint64_t size = 1;
    for (uint32_t dimension : type.array) size *= std::max(dimension, 1u);
    return size > static_cast<uint64_t>(INT32_MAX)
        ? INT32_MAX : static_cast<GLint>(size);
}

Uniform ReflectMember(spirv_cross::Compiler& compiler,
                      const spirv_cross::SPIRType& block_type,
                      uint32_t member, const std::string& visible_name) {
    Uniform uniform;
    uniform.name = visible_name;
    const auto& member_type = compiler.get_type(block_type.member_types[member]);
    uniform.type = GLTypeFor(member_type);
    uniform.size = ArraySize(member_type);
    uniform.offset = static_cast<GLint>(compiler.get_member_decoration(
        block_type.self, member, spv::DecorationOffset));
    if (!member_type.array.empty())
        uniform.array_stride = static_cast<GLint>(
            compiler.type_struct_member_array_stride(block_type, member));
    if (member_type.columns > 1)
        uniform.matrix_stride = static_cast<GLint>(
            compiler.type_struct_member_matrix_stride(block_type, member));
    uniform.row_major = compiler.has_member_decoration(
        block_type.self, member, spv::DecorationRowMajor) ? GL_TRUE : GL_FALSE;
    return uniform;
}

} // namespace

namespace {

std::string WithoutComments(const std::string& source) {
    std::string output = source;
    bool line = false;
    bool block = false;
    for (size_t i = 0; i < output.size(); ++i) {
        if (line) {
            if (output[i] == '\n') line = false;
            else output[i] = ' ';
        } else if (block) {
            if (i + 1 < output.size() && output[i] == '*' &&
                output[i + 1] == '/') {
                output[i] = output[i + 1] = ' ';
                block = false;
                ++i;
            } else if (output[i] != '\n') {
                output[i] = ' ';
            }
        } else if (i + 1 < output.size() && output[i] == '/' &&
                   output[i + 1] == '/') {
            output[i] = output[i + 1] = ' ';
            line = true;
            ++i;
        } else if (i + 1 < output.size() && output[i] == '/' &&
                   output[i + 1] == '*') {
            output[i] = output[i + 1] = ' ';
            block = true;
            ++i;
        }
    }
    return output;
}

std::unordered_set<std::string> ExplicitLocationNames(
    const std::string& source, const char* storage) {
    const std::string clean = WithoutComments(source);
    static const std::regex location(
        R"(\blayout\s*\([^)]*\blocation\s*=\s*[0-9]+[^)]*\))",
        std::regex::optimize);
    static const std::regex identifier(
        R"([A-Za-z_]\w*)", std::regex::optimize);
    const std::regex storage_word(
        "\\b" + std::string(storage) + "\\b", std::regex::optimize);
    std::unordered_set<std::string> names;
    size_t begin = 0;
    while (begin < clean.size()) {
        const size_t semicolon = clean.find(';', begin);
        const std::string statement = clean.substr(
            begin, semicolon == std::string::npos ? std::string::npos
                                                   : semicolon - begin);
        if (std::regex_search(statement, location) &&
            std::regex_search(statement, storage_word)) {
            for (std::sregex_iterator it(statement.begin(), statement.end(),
                                         identifier), end;
                 it != end; ++it)
                names.insert(it->str());
        }
        if (semicolon == std::string::npos) break;
        begin = semicolon + 1;
    }
    return names;
}

GLenum BooleanTypeForName(const std::string& type) {
    if (type == "bool") return GL_BOOL;
    if (type == "bvec2") return GL_BOOL_VEC2;
    if (type == "bvec3") return GL_BOOL_VEC3;
    if (type == "bvec4") return GL_BOOL_VEC4;
    return 0;
}

bool CollectDeclaredBooleanUniforms(
    const Program& program,
    std::unordered_map<std::string, GLenum>& declared,
    std::string& error) {
    // Vulkan interface blocks cannot preserve GL's source-level bool type
    // reliably: glslang materializes bool/bvec members as integer storage.
    // Recover only this lost API-visible type from the original attached GLSL.
    // The SPIR-V reflection remains authoritative for activity, array size and
    // layout; this metadata is used solely to restore the OpenGL uniform type.
    static const std::regex declaration(
        R"(\buniform\s+(?:(?:highp|mediump|lowp)\s+)?(bool|bvec2|bvec3|bvec4)\s+([^;]+);)",
        std::regex::optimize);
    static const std::regex name(
        R"(^\s*([A-Za-z_]\w*))", std::regex::optimize);

    for (GLuint shader_id : program.attached) {
        const Shader* shader = GetShader(shader_id);
        if (!shader) continue;
        const std::string clean = WithoutComments(shader->source);
        for (std::sregex_iterator it(clean.begin(), clean.end(), declaration),
                                  end;
             it != end; ++it) {
            const GLenum type = BooleanTypeForName((*it)[1].str());
            std::string declarators = (*it)[2].str();
            size_t begin = 0;
            while (begin <= declarators.size()) {
                const size_t comma = declarators.find(',', begin);
                const std::string item = declarators.substr(
                    begin, comma == std::string::npos ? std::string::npos
                                                       : comma - begin);
                std::smatch matched;
                if (std::regex_search(item, matched, name)) {
                    const std::string uniform_name = matched[1].str();
                    auto [existing, inserted] = declared.emplace(
                        uniform_name, type);
                    if (!inserted && existing->second != type) {
                        error = "cross-stage source type mismatch for uniform " +
                                uniform_name;
                        return false;
                    }
                }
                if (comma == std::string::npos) break;
                begin = comma + 1;
            }
        }
    }
    return true;
}

uint32_t InterfaceLocationSpan(const spirv_cross::SPIRType& type) {
    uint64_t span = std::max<uint32_t>(type.columns, 1);
    for (uint32_t dimension : type.array)
        span *= std::max<uint32_t>(dimension, 1);
    return span > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(span);
}

bool RewriteLocation(std::vector<uint32_t>& words, uint32_t id,
                     uint32_t location) {
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

} // namespace

bool ApplyStageLocationBindings(
    std::vector<uint32_t>& words, GLenum stage, const std::string& source,
    const std::unordered_map<std::string, GLuint>& requested,
    uint32_t max_locations, std::string& error) {
    if (words.empty() || !max_locations ||
        (stage != GL_VERTEX_SHADER && stage != GL_FRAGMENT_SHADER)) {
        error = "invalid shader stage for interface-location binding";
        return false;
    }
    try {
        spirv_cross::Compiler compiler(words);
        const auto resources = compiler.get_shader_resources();
        const auto& interface = stage == GL_VERTEX_SHADER
            ? resources.stage_inputs : resources.stage_outputs;
        const auto explicit_names = ExplicitLocationNames(
            source, stage == GL_VERTEX_SHADER ? "in" : "out");
        struct Interface {
            uint32_t id = 0;
            std::string name;
            uint32_t current = 0;
            uint32_t desired = 0;
            uint32_t span = 1;
            bool explicit_location = false;
            bool requested_location = false;
        };
        std::vector<Interface> entries;
        entries.reserve(interface.size());
        for (const auto& resource : interface) {
            if (resource.name.empty()) continue;
            if (stage == GL_FRAGMENT_SHADER &&
                compiler.has_decoration(resource.id, spv::DecorationIndex) &&
                compiler.get_decoration(resource.id, spv::DecorationIndex) != 0) {
                error = "dual-source fragment outputs are not supported: " +
                        resource.name;
                return false;
            }
            Interface entry;
            entry.id = resource.id;
            entry.name = resource.name;
            entry.current = compiler.get_decoration(
                resource.id, spv::DecorationLocation);
            entry.desired = entry.current;
            entry.span = InterfaceLocationSpan(
                compiler.get_type(resource.type_id));
            entry.explicit_location = explicit_names.count(entry.name) != 0;
            auto binding = requested.find(entry.name);
            entry.requested_location = !entry.explicit_location &&
                                       binding != requested.end();
            if (entry.requested_location) entry.desired = binding->second;
            entries.push_back(std::move(entry));
        }

        std::vector<int> occupied(max_locations, -1);
        auto reserve = [&](size_t index, uint32_t location) {
            const uint32_t span = entries[index].span;
            if (!span || location >= max_locations ||
                span > max_locations - location)
                return false;
            for (uint32_t slot = location; slot < location + span; ++slot)
                if (occupied[slot] >= 0) return false;
            for (uint32_t slot = location; slot < location + span; ++slot)
                occupied[slot] = static_cast<int>(index);
            entries[index].desired = location;
            return true;
        };

        for (size_t i = 0; i < entries.size(); ++i) {
            if (!entries[i].explicit_location) continue;
            if (!reserve(i, entries[i].current)) {
                error = "conflicting explicit location for shader interface " +
                        entries[i].name;
                return false;
            }
        }
        for (size_t i = 0; i < entries.size(); ++i) {
            if (!entries[i].requested_location) continue;
            if (!reserve(i, entries[i].desired)) {
                error = "conflicting requested location for shader interface " +
                        entries[i].name;
                return false;
            }
        }
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].explicit_location || entries[i].requested_location)
                continue;
            if (reserve(i, entries[i].current)) continue;
            bool assigned = false;
            for (uint32_t location = 0; location < max_locations; ++location) {
                if (reserve(i, location)) {
                    assigned = true;
                    break;
                }
            }
            if (!assigned) {
                error = "no free location for shader interface " +
                        entries[i].name;
                return false;
            }
        }
        for (const auto& entry : entries) {
            if (entry.desired == entry.current) continue;
            if (!RewriteLocation(words, entry.id, entry.desired)) {
                error = "SPIR-V interface has no mutable Location decoration: " +
                        entry.name;
                return false;
            }
        }
        return true;
    } catch (const std::exception& exception) {
        error = std::string("SPIR-V interface remap failed: ") +
                exception.what();
        return false;
    }
}


bool AlignStageInterfaceLocations(
    std::vector<uint32_t>& vertex_spirv,
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
        const auto explicit_vertex =
            ExplicitLocationNames(vertex_source, "out");
        const auto explicit_fragment =
            ExplicitLocationNames(fragment_source, "in");

        struct InterfaceOutput {
            uint32_t id = 0;
            uint32_t location = 0;
            uint32_t span = 1;
            spirv_cross::SPIRType::BaseType base =
                spirv_cross::SPIRType::Unknown;
            uint32_t vecsize = 1;
            uint32_t columns = 1;
            std::vector<uint32_t> array;
            bool explicit_location = false;
        };

        std::unordered_map<std::string, InterfaceOutput> outputs;
        for (const auto& resource : vertex_resources.stage_outputs) {
            if (resource.name.empty() ||
                !vertex.has_decoration(resource.id, spv::DecorationLocation))
                continue;
            const auto& type = vertex.get_type(resource.type_id);
            InterfaceOutput output;
            output.id = resource.id;
            output.location = vertex.get_decoration(
                resource.id, spv::DecorationLocation);
            output.span = InterfaceLocationSpan(type);
            output.base = type.basetype;
            output.vecsize = type.vecsize;
            output.columns = type.columns;
            output.array.assign(type.array.begin(), type.array.end());
            output.explicit_location =
                explicit_vertex.count(resource.name) != 0;
            outputs[resource.name] = std::move(output);
        }

        for (const auto& resource : fragment_resources.stage_inputs) {
            if (resource.name.empty() ||
                !fragment.has_decoration(resource.id, spv::DecorationLocation))
                continue;
            auto output = outputs.find(resource.name);
            if (output == outputs.end()) continue;

            const auto& input_type = fragment.get_type(resource.type_id);
            const auto& linked = output->second;
            if (input_type.basetype != linked.base ||
                input_type.vecsize != linked.vecsize ||
                input_type.columns != linked.columns ||
                input_type.array.size() != linked.array.size() ||
                !std::equal(input_type.array.begin(), input_type.array.end(),
                            linked.array.begin()) ||
                InterfaceLocationSpan(input_type) != linked.span) {
                error = "cross-stage interface type mismatch for " +
                        resource.name;
                return false;
            }

            const uint32_t input_location = fragment.get_decoration(
                resource.id, spv::DecorationLocation);
            const bool input_explicit =
                explicit_fragment.count(resource.name) != 0;

            // If both sides explicitly selected locations, location matching is
            // authoritative; do not rewrite either stage merely because names
            // happen to match. Otherwise make the automatic side follow the
            // explicit side, or (when both are automatic) make FS follow VS.
            if (linked.explicit_location && input_explicit) continue;
            if (input_location == linked.location) continue;

            if (input_explicit && !linked.explicit_location) {
                if (!RewriteLocation(vertex_spirv, linked.id, input_location)) {
                    error = "vertex interface has no mutable Location decoration: " +
                            resource.name;
                    return false;
                }
            } else {
                if (!RewriteLocation(fragment_spirv, resource.id,
                                     linked.location)) {
                    error = "fragment interface has no mutable Location decoration: " +
                            resource.name;
                    return false;
                }
            }
        }
        return true;
    } catch (const std::exception& exception) {
        error = std::string("cross-stage interface remap failed: ") +
                exception.what();
        return false;
    }
}

bool ReflectProgram(Program& prog, std::string& error) {
    prog.uniforms.clear();
    prog.uniform_by_name.clear();
    prog.uniform_by_location.clear();
    prog.active_uniform_by_name.clear();
    prog.attrib_locations.clear();
    prog.vertex_inputs.clear();
    prog.frag_data_locations.clear();
    prog.frag_data_indices.clear();
    prog.uses_flat_fragment_inputs = false;
    prog.samplers.clear();
    prog.uniform_blocks.clear();
    prog.uniform_block_by_name.clear();

    std::unordered_map<std::string, Uniform> loose_uniforms;
    bool valid = true;
    auto fail = [&](const std::string& message) {
        if (valid) error = message;
        valid = false;
    };

    std::unordered_map<std::string, GLenum> declared_boolean_uniforms;
    if (!CollectDeclaredBooleanUniforms(
            prog, declared_boolean_uniforms, error))
        return false;

    auto reflect_stage = [&](const std::vector<uint32_t>& words,
                             bool vertex_stage) {
        if (words.empty()) return;
        try {
            spirv_cross::Compiler compiler(words.data(), words.size());
            spirv_cross::ShaderResources resources =
                compiler.get_shader_resources();

            for (auto& resource : resources.uniform_buffers) {
                const auto& type = compiler.get_type(resource.base_type_id);
                const std::string type_name =
                    compiler.get_name(resource.base_type_id);
                const uint32_t internal_binding = compiler.get_decoration(
                    resource.id, spv::DecorationBinding);
                if (type_name == "mithril_GlobalBlock" ||
                    internal_binding == kLooseUniformBinding) {
                    for (uint32_t i = 0; i < type.member_types.size(); ++i) {
                        std::string name = compiler.get_member_name(
                            resource.base_type_id, i);
                        if (name.empty()) continue;
                        Uniform reflected = ReflectMember(
                            compiler, type, i, name);
                        const auto declared_type =
                            declared_boolean_uniforms.find(name);
                        if (declared_type != declared_boolean_uniforms.end())
                            reflected.type = declared_type->second;
                        // Default-block uniforms are not buffer-backed in GL's
                        // observable namespace even though lowering packs them.
                        reflected.offset = -1;
                        reflected.array_stride = 0;
                        reflected.matrix_stride = 0;
                        reflected.row_major = GL_FALSE;
                        auto [existing, inserted] = loose_uniforms.emplace(
                            name, reflected);
                        if (!inserted &&
                            (existing->second.type != reflected.type ||
                             existing->second.size != reflected.size))
                            fail("cross-stage type mismatch for uniform " + name);
                    }
                    continue;
                }

                std::string block_name = type_name;
                if (block_name.empty()) block_name = resource.name;
                if (block_name.empty()) continue;
                GLuint block_index = 0;
                auto found = prog.uniform_block_by_name.find(block_name);
                if (found == prog.uniform_block_by_name.end()) {
                    block_index = static_cast<GLuint>(prog.uniform_blocks.size());
                    prog.uniform_block_by_name.emplace(block_name, block_index);
                    UniformBlock block;
                    block.name = block_name;
                    prog.uniform_blocks.push_back(std::move(block));
                } else {
                    block_index = found->second;
                }
                UniformBlock& block = prog.uniform_blocks[block_index];
                const bool existing_layout = block.referenced_vertex ||
                                             block.referenced_fragment;
                const GLint stage_size = static_cast<GLint>(
                    compiler.get_declared_struct_size(type));
                if (existing_layout && block.data_size != stage_size)
                    fail("cross-stage layout mismatch for uniform block " +
                         block_name);
                block.data_size = stage_size;
                if (vertex_stage) {
                    block.referenced_vertex = true;
                    block.vertex_internal_binding = internal_binding;
                } else {
                    block.referenced_fragment = true;
                    block.fragment_internal_binding = internal_binding;
                }

                if (existing_layout &&
                    block.members.size() != type.member_types.size())
                    fail("cross-stage member-count mismatch in uniform block " +
                         block_name);
                for (uint32_t i = 0; i < type.member_types.size(); ++i) {
                    const std::string member_name = compiler.get_member_name(
                        resource.base_type_id, i);
                    if (member_name.empty()) {
                        fail("unnamed member in uniform block " + block_name);
                        continue;
                    }
                    const Uniform reflected = ReflectMember(
                        compiler, type, i, member_name);
                    if (existing_layout) {
                        if (i >= block.members.size()) continue;
                        const Uniform& previous = block.members[i];
                        if (previous.name != member_name ||
                            previous.type != reflected.type ||
                            previous.size != reflected.size ||
                            previous.offset != reflected.offset ||
                            previous.array_stride != reflected.array_stride ||
                            previous.matrix_stride != reflected.matrix_stride ||
                            previous.row_major != reflected.row_major)
                            fail("cross-stage member mismatch in uniform block " +
                                 block_name + ": " + member_name);
                        continue;
                    }
                    block.members.push_back(reflected);
                }
            }

            auto add_sampler = [&](spirv_cross::Resource& resource) {
                const std::string name = resource.name;
                if (name.empty()) return;
                const auto& resource_type = compiler.get_type(resource.type_id);
                const GLenum sampler_type = SamplerTypeFor(compiler, resource);
                const GLint sampler_size = ArraySize(resource_type);
                const uint32_t binding = compiler.get_decoration(
                    resource.id, spv::DecorationBinding);
                auto existing = std::find_if(
                    prog.samplers.begin(), prog.samplers.end(),
                    [&](const SamplerRef& sampler) {
                        return sampler.name == name;
                    });
                if (existing == prog.samplers.end()) {
                    SamplerRef sampler;
                    sampler.name = name;
                    sampler.type = sampler_type;
                    sampler.size = sampler_size;
                    if (vertex_stage) sampler.vertex_binding = binding;
                    else sampler.fragment_binding = binding;
                    prog.samplers.push_back(std::move(sampler));
                } else {
                    if (existing->type != sampler_type ||
                        existing->size != sampler_size)
                        fail("cross-stage type/size mismatch for sampler " + name);
                    if (vertex_stage) existing->vertex_binding = binding;
                    else existing->fragment_binding = binding;
                }
                Uniform uniform;
                uniform.name = name;
                uniform.type = sampler_type;
                uniform.size = sampler_size;
                auto [uniform_it, inserted] = loose_uniforms.emplace(
                    name, std::move(uniform));
                if (!inserted &&
                    (uniform_it->second.type != sampler_type ||
                     uniform_it->second.size != sampler_size))
                    fail("cross-stage type/size mismatch for uniform " + name);
            };
            for (auto& resource : resources.sampled_images) add_sampler(resource);
            for (auto& resource : resources.separate_images) add_sampler(resource);
            for (auto& resource : resources.separate_samplers) add_sampler(resource);

            for (auto& resource : resources.stage_inputs) {
                if (!vertex_stage) {
                    bool flat = compiler.has_decoration(
                        resource.id, spv::DecorationFlat);
                    const auto& interface_type = compiler.get_type(
                        resource.base_type_id);
                    for (uint32_t member = 0;
                         !flat && member < interface_type.member_types.size();
                         ++member) {
                        flat = compiler.has_member_decoration(
                            resource.base_type_id, member,
                            spv::DecorationFlat);
                    }
                    prog.uses_flat_fragment_inputs |= flat;
                }
                if (!vertex_stage ||
                    !compiler.has_decoration(resource.id,
                                             spv::DecorationLocation))
                    continue;
                const uint32_t location = compiler.get_decoration(
                    resource.id, spv::DecorationLocation);
                if (!resource.name.empty())
                    prog.attrib_locations[resource.name] = location;
                const auto& type = compiler.get_type(resource.type_id);
                VertexInputScalar scalar = VertexInputScalar::Unsupported;
                if (type.width == 32) {
                    using Base = spirv_cross::SPIRType::BaseType;
                    if (type.basetype == Base::Float)
                        scalar = VertexInputScalar::Float32;
                    else if (type.basetype == Base::Int)
                        scalar = VertexInputScalar::Sint32;
                    else if (type.basetype == Base::UInt)
                        scalar = VertexInputScalar::Uint32;
                }
                const uint32_t span = InterfaceLocationSpan(type);
                const uint32_t components = std::clamp<uint32_t>(
                    type.vecsize, 1, 4);
                if (span == UINT32_MAX ||
                    (span && location > UINT32_MAX - (span - 1))) {
                    fail("vertex input location span overflow: " +
                         resource.name);
                    continue;
                }
                for (uint32_t index = 0; index < span; ++index)
                    prog.vertex_inputs.push_back(
                        {location + index, components, scalar});
            }
            for (auto& resource : resources.stage_outputs) {
                if (vertex_stage || resource.name.empty()) continue;
                const int location = static_cast<int>(compiler.get_decoration(
                    resource.id, spv::DecorationLocation));
                prog.frag_data_locations[resource.name] = location;
                prog.frag_data_indices[resource.name] =
                    compiler.has_decoration(resource.id, spv::DecorationIndex)
                    ? static_cast<int>(compiler.get_decoration(
                          resource.id, spv::DecorationIndex))
                    : 0;
            }
        } catch (const std::exception& exception) {
            ML_LOG_WARN("SPIRV-Cross reflection failed: %s", exception.what());
            fail(std::string("SPIRV-Cross reflection failed: ") +
                 exception.what());
        }
    };

    reflect_stage(prog.vertex_spirv, true);
    reflect_stage(prog.fragment_spirv, false);
    if (!valid) return false;

    std::sort(prog.vertex_inputs.begin(), prog.vertex_inputs.end(),
              [](const VertexInput& left, const VertexInput& right) {
                  return left.location < right.location;
              });

    std::vector<std::string> names;
    names.reserve(loose_uniforms.size());
    for (const auto& entry : loose_uniforms) names.push_back(entry.first);
    std::sort(names.begin(), names.end());
    for (const std::string& name : names) {
        Uniform uniform = loose_uniforms.at(name);
        uniform.location = static_cast<GLint>(prog.uniform_by_location.size());
        const size_t index = prog.uniforms.size();
        prog.uniform_by_location.emplace(uniform.location, index);
        prog.uniform_by_name.emplace(name, uniform.location);
        prog.active_uniform_by_name.emplace(name, static_cast<GLuint>(index));
        prog.uniforms.push_back(std::move(uniform));
    }

    for (GLuint block_index = 0;
         block_index < prog.uniform_blocks.size(); ++block_index) {
        UniformBlock& block = prog.uniform_blocks[block_index];
        for (Uniform member : block.members) {
            member.block_index = static_cast<GLint>(block_index);
            member.location = -1;
            const GLuint uniform_index =
                static_cast<GLuint>(prog.uniforms.size());
            block.active_uniform_indices.push_back(uniform_index);
            prog.active_uniform_by_name.emplace(member.name, uniform_index);
            prog.uniforms.push_back(std::move(member));
        }
        block.members.clear();
    }

    for (auto& sampler : prog.samplers) {
        const auto location = prog.uniform_by_name.find(sampler.name);
        sampler.location = location == prog.uniform_by_name.end()
            ? -1 : location->second;
    }
    error.clear();
    return true;
}

} // namespace mithril::shader
