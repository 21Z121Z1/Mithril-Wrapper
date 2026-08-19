// Mithril-Wrapper GL entry points -- S2 shader/program/uniform domain
// (milestone M2-S2). Shader object lifecycle, link, use, the glUniform*
// setter families and the uniform getters, backed by mithril::shader,
// plus teardown of the lazily created native-backend program handles.

#include "internal.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>

#include <util/log.h>
#include <mithril/program_diagnostics.h>

namespace {

MithrilProgramPrewarmStatsV1 EmptyProgramPrewarmStats() {
    MithrilProgramPrewarmStatsV1 stats{};
    stats.version = MITHRIL_PROGRAM_PREWARM_STATS_VERSION;
    stats.struct_size = static_cast<uint32_t>(sizeof(stats));
    return stats;
}

MithrilProgramPrewarmStatsV1 g_program_prewarm_stats = EmptyProgramPrewarmStats();

} // namespace

uint64_t EnsureBackendProgram(mithril::shader::Program* program,
                              BackendProgramCreateSite site) {
    if (!program || !program->linked) return 0;
    auto cached = g_backend_programs.find(program->id);
    if (cached != g_backend_programs.end()) return cached->second;

    std::vector<std::string> uniform_names;
    uniform_names.reserve(program->uniforms.size());
    for (const auto& uniform : program->uniforms)
        uniform_names.push_back(uniform.name);

    const uint64_t handle = v::CreateProgram(
        program->vertex_spirv, program->fragment_spirv, uniform_names);
    if (!handle) {
        ++g_program_prewarm_stats.create_failures;
        return 0;
    }
    g_backend_programs[program->id] = handle;
    ++g_program_prewarm_stats.frontend_program_bindings;
    switch (site) {
        case BackendProgramCreateSite::Link:
            ++g_program_prewarm_stats.link_prewarms; break;
        case BackendProgramCreateSite::Use:
            ++g_program_prewarm_stats.use_prewarms; break;
        case BackendProgramCreateSite::Draw:
            ++g_program_prewarm_stats.draw_fallbacks; break;
    }
    return handle;
}

extern "C" {

// ---- shaders / programs / uniforms (S2) ------------------------------------

namespace {
namespace sh = mithril::shader;

struct UniformLocationRef {
    size_t uniform_index = 0;
    GLint element = 0;
};

// OpenGL uniform locations are opaque handles. Reflection keeps one Uniform
// object per active array while GL exposes a distinct location for each active
// element. Keep that element indirection beside the program instead of
// splitting the reflected object: the backend still receives one complete
// tight value vector and can apply its reflected array/matrix strides once.
std::unordered_map<GLuint, std::unordered_map<GLint, UniformLocationRef>>
    g_uniform_location_refs;

std::string ObservableUniformName(const sh::Uniform& uniform) {
    if (uniform.location >= 0 && uniform.size > 1 &&
        uniform.name.find('[') == std::string::npos)
        return uniform.name + "[0]";
    return uniform.name;
}

void RegisterUniformLocations(sh::Program& program) {
    auto& refs = g_uniform_location_refs[program.id];
    refs.clear();

    int64_t next_location = 0;
    for (const auto& entry : program.uniform_by_location)
        next_location = std::max<int64_t>(
            next_location, static_cast<int64_t>(entry.first) + 1);

    for (size_t uniform_index = 0;
         uniform_index < program.uniforms.size(); ++uniform_index) {
        sh::Uniform& uniform = program.uniforms[uniform_index];
        if (uniform.location < 0) continue;
        refs[uniform.location] = {uniform_index, 0};
        if (uniform.size <= 1) continue;

        // Per GL, the first element is addressable through either `name` or
        // `name[0]`. Subsequent locations need only be unique/opaque; callers
        // must query them rather than relying on arithmetic.
        program.uniform_by_name[uniform.name + "[0]"] = uniform.location;
        program.active_uniform_by_name[uniform.name + "[0]"] =
            static_cast<GLuint>(uniform_index);
        for (GLint element = 1; element < uniform.size; ++element) {
            if (next_location > std::numeric_limits<GLint>::max()) break;
            while (next_location <= std::numeric_limits<GLint>::max() &&
                   program.uniform_by_location.count(
                       static_cast<GLint>(next_location)))
                ++next_location;
            if (next_location > std::numeric_limits<GLint>::max()) break;
            const GLint location = static_cast<GLint>(next_location++);
            program.uniform_by_location[location] = uniform_index;
            program.uniform_by_name[uniform.name + "[" +
                                    std::to_string(element) + "]"] = location;
            program.active_uniform_by_name[uniform.name + "[" +
                                            std::to_string(element) + "]"] =
                static_cast<GLuint>(uniform_index);
            refs[location] = {uniform_index, element};
        }
    }
}

bool ResolveUniformLocation(sh::Program* program, GLint location,
                            sh::Uniform** uniform, GLint* element) {
    if (!program) return false;
    auto by_program = g_uniform_location_refs.find(program->id);
    if (by_program != g_uniform_location_refs.end()) {
        auto found = by_program->second.find(location);
        if (found != by_program->second.end() &&
            found->second.uniform_index < program->uniforms.size()) {
            if (uniform)
                *uniform = &program->uniforms[found->second.uniform_index];
            if (element) *element = found->second.element;
            return true;
        }
    }
    // Defensive fallback for programs linked before this bookkeeping was
    // introduced, and for scalar locations if a future path omits registration.
    auto reflected = program->uniform_by_location.find(location);
    if (reflected == program->uniform_by_location.end() ||
        reflected->second >= program->uniforms.size())
        return false;
    if (uniform) *uniform = &program->uniforms[reflected->second];
    if (element) *element = 0;
    return true;
}

size_t UniformComponentCount(GLenum type) {
    switch (type) {
        case GL_FLOAT_VEC2:
        case GL_INT_VEC2:
        case GL_UNSIGNED_INT_VEC2:
        case GL_BOOL_VEC2:
            return 2;
        case GL_FLOAT_VEC3:
        case GL_INT_VEC3:
        case GL_UNSIGNED_INT_VEC3:
        case GL_BOOL_VEC3:
            return 3;
        case GL_FLOAT_VEC4:
        case GL_INT_VEC4:
        case GL_UNSIGNED_INT_VEC4:
        case GL_BOOL_VEC4:
            return 4;
        case GL_FLOAT_MAT2: return 4;
        case GL_FLOAT_MAT3: return 9;
        case GL_FLOAT_MAT4: return 16;
        case GL_FLOAT_MAT2x3:
        case GL_FLOAT_MAT3x2:
            return 6;
        case GL_FLOAT_MAT2x4:
        case GL_FLOAT_MAT4x2:
            return 8;
        case GL_FLOAT_MAT3x4:
        case GL_FLOAT_MAT4x3:
            return 12;
        default:
            return 1;
    }
}

bool IsBooleanUniformType(GLenum type) {
    switch (type) {
        case GL_BOOL:
        case GL_BOOL_VEC2:
        case GL_BOOL_VEC3:
        case GL_BOOL_VEC4:
            return true;
        default:
            return false;
    }
}

bool IsSignedIntegerUniformType(GLenum type) {
    switch (type) {
        case GL_INT:
        case GL_INT_VEC2:
        case GL_INT_VEC3:
        case GL_INT_VEC4:
            return true;
        default:
            return false;
    }
}

bool IsUnsignedIntegerUniformType(GLenum type) {
    switch (type) {
        case GL_UNSIGNED_INT:
        case GL_UNSIGNED_INT_VEC2:
        case GL_UNSIGNED_INT_VEC3:
        case GL_UNSIGNED_INT_VEC4:
            return true;
        default:
            return false;
    }
}

bool IsSamplerUniformType(GLenum type) {
    switch (type) {
        case GL_SAMPLER_1D:
        case GL_SAMPLER_2D:
        case GL_SAMPLER_3D:
        case GL_SAMPLER_CUBE:
        case GL_SAMPLER_1D_SHADOW:
        case GL_SAMPLER_2D_SHADOW:
        case GL_SAMPLER_1D_ARRAY:
        case GL_SAMPLER_2D_ARRAY:
        case GL_SAMPLER_1D_ARRAY_SHADOW:
        case GL_SAMPLER_2D_ARRAY_SHADOW:
        case GL_SAMPLER_2D_MULTISAMPLE:
        case GL_SAMPLER_2D_MULTISAMPLE_ARRAY:
        case GL_SAMPLER_CUBE_SHADOW:
        case GL_SAMPLER_BUFFER:
        case GL_SAMPLER_2D_RECT:
        case GL_SAMPLER_2D_RECT_SHADOW:
        case GL_SAMPLER_CUBE_MAP_ARRAY:
        case GL_SAMPLER_CUBE_MAP_ARRAY_SHADOW:
        case GL_INT_SAMPLER_1D:
        case GL_INT_SAMPLER_2D:
        case GL_INT_SAMPLER_3D:
        case GL_INT_SAMPLER_CUBE:
        case GL_INT_SAMPLER_1D_ARRAY:
        case GL_INT_SAMPLER_2D_ARRAY:
        case GL_INT_SAMPLER_2D_MULTISAMPLE:
        case GL_INT_SAMPLER_2D_MULTISAMPLE_ARRAY:
        case GL_INT_SAMPLER_BUFFER:
        case GL_INT_SAMPLER_2D_RECT:
        case GL_INT_SAMPLER_CUBE_MAP_ARRAY:
        case GL_UNSIGNED_INT_SAMPLER_1D:
        case GL_UNSIGNED_INT_SAMPLER_2D:
        case GL_UNSIGNED_INT_SAMPLER_3D:
        case GL_UNSIGNED_INT_SAMPLER_CUBE:
        case GL_UNSIGNED_INT_SAMPLER_1D_ARRAY:
        case GL_UNSIGNED_INT_SAMPLER_2D_ARRAY:
        case GL_UNSIGNED_INT_SAMPLER_2D_MULTISAMPLE:
        case GL_UNSIGNED_INT_SAMPLER_2D_MULTISAMPLE_ARRAY:
        case GL_UNSIGNED_INT_SAMPLER_BUFFER:
        case GL_UNSIGNED_INT_SAMPLER_2D_RECT:
        case GL_UNSIGNED_INT_SAMPLER_CUBE_MAP_ARRAY:
            return true;
        default:
            return false;
    }
}

bool ReadRawSignedScalar(const sh::Uniform& uniform, size_t scalar,
                         GLint* value) {
    const size_t offset = scalar * sizeof(GLint);
    if (!value || offset > uniform.raw_value.size() ||
        sizeof(GLint) > uniform.raw_value.size() - offset)
        return false;
    std::memcpy(value, uniform.raw_value.data() + offset, sizeof(GLint));
    return true;
}

bool ReadRawUnsignedScalar(const sh::Uniform& uniform, size_t scalar,
                           GLuint* value) {
    const size_t offset = scalar * sizeof(GLuint);
    if (!value || offset > uniform.raw_value.size() ||
        sizeof(GLuint) > uniform.raw_value.size() - offset)
        return false;
    std::memcpy(value, uniform.raw_value.data() + offset, sizeof(GLuint));
    return true;
}

bool IsScalarVectorSetterType(GLenum type) {
    switch (type) {
        case GL_FLOAT:
        case GL_FLOAT_VEC2:
        case GL_FLOAT_VEC3:
        case GL_FLOAT_VEC4:
        case GL_INT:
        case GL_INT_VEC2:
        case GL_INT_VEC3:
        case GL_INT_VEC4:
        case GL_UNSIGNED_INT:
        case GL_UNSIGNED_INT_VEC2:
        case GL_UNSIGNED_INT_VEC3:
        case GL_UNSIGNED_INT_VEC4:
            return true;
        default:
            return false;
    }
}

bool UniformSetterMatches(GLenum uniform_type, GLenum setter_type) {
    if (IsBooleanUniformType(uniform_type)) {
        return IsScalarVectorSetterType(setter_type) &&
               UniformComponentCount(uniform_type) ==
                   UniformComponentCount(setter_type);
    }
    if (IsSamplerUniformType(uniform_type)) return setter_type == GL_INT;
    return uniform_type == setter_type;
}
} // namespace

GLuint APIENTRY glCreateShader(GLenum type) {
    if (type != GL_VERTEX_SHADER && type != GL_FRAGMENT_SHADER &&
        type != GL_GEOMETRY_SHADER && type != GL_TESS_CONTROL_SHADER &&
        type != GL_TESS_EVALUATION_SHADER && type != GL_COMPUTE_SHADER) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return 0;
    }
    GLuint id = sh::NewShader(type);
    ML_LOG_DEBUG("glCreateShader(0x%x) -> %u", (unsigned)type, id);
    return id;
}

void APIENTRY glDeleteShader(GLuint shader) {
    if (shader && sh::GetShader(shader) == nullptr) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    sh::DeleteShader(shader);
}

GLboolean APIENTRY glIsShader(GLuint shader) {
    return sh::GetShader(shader) != nullptr ? GL_TRUE : GL_FALSE;
}

void APIENTRY glShaderSource(GLuint shader, GLsizei count, const GLchar* const* string,
                             const GLint* length) {
    auto* s = sh::GetShader(shader);
    if (!s) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (count < 0 || (count > 0 && !string)) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    s->source.clear();
    for (GLsizei i = 0; i < count; ++i) {
        if (!string[i]) continue;
        if (length && length[i] >= 0) s->source.append(string[i], (size_t)length[i]);
        else s->source.append(string[i]);
    }
    s->compiled = false;
    s->spirv.clear();
}

void APIENTRY glCompileShader(GLuint shader) {
    auto* s = sh::GetShader(shader);
    if (!s) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    s->compiled = false;
    s->spirv.clear();
    if (s->source.empty()) {
        s->info_log = "shader source is empty";
        ML_LOG_WARN("glCompileShader(%u): empty source", shader);
        return;
    }
    std::vector<uint32_t> spirv;
    std::string info;
    if (sh::CompileStage(s->type, s->source, spirv, info)) {
        s->compiled = true;
        s->spirv = std::move(spirv);
        s->info_log.clear();
        ML_LOG_DEBUG("glCompileShader(%u): ok, %zu SPIR-V words", shader, s->spirv.size());
    } else {
        s->info_log = info;
        ML_LOG_WARN("glCompileShader(%u): %s", shader, info.c_str());
    }
}

void APIENTRY glGetShaderiv(GLuint shader, GLenum pname, GLint* params) {
    if (!params) return;
    auto* s = sh::GetShader(shader);
    if (!s) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    switch (pname) {
        case GL_SHADER_TYPE:          *params = (GLint)s->type; return;
        case GL_COMPILE_STATUS:       *params = s->compiled ? GL_TRUE : GL_FALSE; return;
        case GL_INFO_LOG_LENGTH:      *params = (GLint)(s->info_log.size() + 1); return;
        case GL_SHADER_SOURCE_LENGTH: *params = (GLint)(s->source.size() + 1); return;
        case GL_DELETE_STATUS:        *params = GL_FALSE; return;
        default:                      PUSH_ERROR(GL_INVALID_ENUM);
    }
}

void APIENTRY glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei* length,
                                 GLchar* infoLog) {
    auto* s = sh::GetShader(shader);
    if (!s) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (!infoLog || bufSize <= 0) { if (length) *length = 0; return; }
    GLsizei n = (GLsizei)s->info_log.size();
    if (n > bufSize - 1) n = bufSize - 1;
    std::memcpy(infoLog, s->info_log.data(), (size_t)n);
    infoLog[n] = 0;
    if (length) *length = n;
}

void APIENTRY glGetShaderSource(GLuint shader, GLsizei bufSize, GLsizei* length,
                                GLchar* source) {
    auto* s = sh::GetShader(shader);
    if (!s) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (!source || bufSize <= 0) { if (length) *length = 0; return; }
    GLsizei n = (GLsizei)s->source.size();
    if (n > bufSize - 1) n = bufSize - 1;
    std::memcpy(source, s->source.data(), (size_t)n);
    source[n] = 0;
    if (length) *length = n;
}

GLuint APIENTRY glCreateProgram(void) {
    GLuint id = sh::NewProgram();
    ML_LOG_DEBUG("glCreateProgram -> %u", id);
    return id;
}

void APIENTRY glDeleteProgram(GLuint program) {
    if (program && sh::GetProgram(program) == nullptr) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    auto it = g_backend_programs.find(program);
    if (it != g_backend_programs.end()) {
        v::DestroyProgram(it->second);
        g_backend_programs.erase(it);
    }
    g_uniform_location_refs.erase(program);
    sh::DeleteProgram(program);
}

GLboolean APIENTRY glIsProgram(GLuint program) {
    return sh::GetProgram(program) != nullptr ? GL_TRUE : GL_FALSE;
}

void APIENTRY glAttachShader(GLuint program, GLuint shader) {
    auto* p = sh::GetProgram(program);
    auto* s = sh::GetShader(shader);
    if (!p || !s) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    for (GLuint id : p->attached) if (id == shader) return;
    p->attached.push_back(shader);
}

void APIENTRY glDetachShader(GLuint program, GLuint shader) {
    auto* p = sh::GetProgram(program);
    auto* s = sh::GetShader(shader);
    if (!p || !s) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    auto& v = p->attached;
    v.erase(std::remove(v.begin(), v.end(), shader), v.end());
}

void APIENTRY glBindAttribLocation(GLuint program, GLuint index, const GLchar* name) {
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    if (index >= kMaxAttribs || !name) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (std::strncmp(name, "gl_", 3) == 0) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    p->requested_attrib_locations[name] = index;
}

void APIENTRY glBindFragDataLocation(GLuint program, GLuint color,
                                     const GLchar* name) {
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    if (color >= 8 || !name) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (std::strncmp(name, "gl_", 3) == 0) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    p->requested_frag_data_locations[name] = color;
}

void APIENTRY glBindFragDataLocationIndexed(GLuint program, GLuint color,
                                            GLuint index,
                                            const GLchar* name) {
    if (index != 0) {
        // Dual-source fragment outputs require matching Metal blend-factor and
        // pipeline support. Reject them until that path is implemented.
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    glBindFragDataLocation(program, color, name);
}

void APIENTRY glLinkProgram(GLuint program) {
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_VALUE); return; }

    g_uniform_location_refs.erase(program);
    p->vertex_spirv.clear();
    p->fragment_spirv.clear();
    p->linked = false;
    p->info_log.clear();
    bool have_vs = false, have_fs = false;
    std::string vertex_source, fragment_source;
    std::unordered_map<std::string, sh::UniformBlockDeclaration>
        block_declarations;
    for (GLuint sid : p->attached) {
        auto* s = sh::GetShader(sid);
        if (!s) continue;
        if (!s->compiled || s->spirv.empty()) {
            p->info_log = "link failed: attached shader " + std::to_string(sid) +
                          " is not compiled";
            ML_LOG_WARN("glLinkProgram(%u): %s", program, p->info_log.c_str());
            return;
        }
        if (s->type == GL_VERTEX_SHADER) {
            p->vertex_spirv = s->spirv;
            vertex_source = s->source;
            have_vs = true;
        } else if (s->type == GL_FRAGMENT_SHADER) {
            p->fragment_spirv = s->spirv;
            fragment_source = s->source;
            have_fs = true;
        }
        for (const auto& declaration : sh::DiscoverUniformBlocks(s->source)) {
            if (declaration.has_explicit_binding &&
                declaration.binding >= kMaxUniformBufferBindings) {
                p->info_log = "link failed: uniform block " + declaration.name +
                              " uses a binding beyond GL_MAX_UNIFORM_BUFFER_BINDINGS";
                ML_LOG_WARN("glLinkProgram(%u): %s", program,
                            p->info_log.c_str());
                return;
            }
            auto [it, inserted] = block_declarations.emplace(
                declaration.name, declaration);
            if (!inserted) {
                it->second.has_instance = it->second.has_instance ||
                                          declaration.has_instance;
                if (declaration.has_explicit_binding) {
                    if (it->second.has_explicit_binding &&
                        it->second.binding != declaration.binding) {
                        p->info_log = "link failed: conflicting layout(binding=) "
                                      "values for uniform block " + declaration.name;
                        ML_LOG_WARN("glLinkProgram(%u): %s", program,
                                    p->info_log.c_str());
                        return;
                    }
                    it->second.binding = declaration.binding;
                    it->second.has_explicit_binding = true;
                }
            }
        }
    }
    if (!have_vs || !have_fs) {
        if (p->info_log.empty())
            p->info_log = "link failed: missing compiled vertex or fragment shader";
        ML_LOG_WARN("glLinkProgram(%u): %s", program, p->info_log.c_str());
        return;
    }

    std::string reflection_error;
    if (!sh::ApplyStageLocationBindings(
            p->vertex_spirv, GL_VERTEX_SHADER, vertex_source,
            p->requested_attrib_locations, kMaxAttribs, reflection_error) ||
        !sh::ApplyStageLocationBindings(
            p->fragment_spirv, GL_FRAGMENT_SHADER, fragment_source,
            p->requested_frag_data_locations, 8, reflection_error)) {
        p->info_log = "link failed: " + reflection_error;
        ML_LOG_WARN("glLinkProgram(%u): %s", program, p->info_log.c_str());
        return;
    }
    if (!sh::AlignStageInterfaceLocations(
            p->vertex_spirv, p->fragment_spirv,
            vertex_source, fragment_source, reflection_error)) {
        p->info_log = "link failed: " + reflection_error;
        ML_LOG_WARN("glLinkProgram(%u): %s", program, p->info_log.c_str());
        return;
    }
    if (!sh::ReflectProgram(*p, reflection_error)) {
        p->info_log = "link failed: " + reflection_error;
        ML_LOG_WARN("glLinkProgram(%u): %s", program, p->info_log.c_str());
        return;
    }
    auto native = g_backend_programs.find(program);
    if (native != g_backend_programs.end()) {
        v::DestroyProgram(native->second);
        g_backend_programs.erase(native);
    }
    p->loose_uniform_views.resize(p->uniforms.size());
    for (size_t i = 0; i < p->uniforms.size(); ++i) {
        const auto& raw = p->uniforms[i].raw_value;
        p->loose_uniform_views[i] = {
            raw.empty() ? nullptr : raw.data(), static_cast<uint32_t>(raw.size())};
    }
    p->loose_uniform_version = 1;
    p->linked = true;
    for (auto& block : p->uniform_blocks) {
        const auto declaration = block_declarations.find(block.name);
        if (declaration == block_declarations.end()) continue;
        if (declaration->second.has_explicit_binding)
            block.binding = declaration->second.binding;
        if (declaration->second.has_instance) {
            for (GLuint uniform_index : block.active_uniform_indices) {
                sh::Uniform& uniform = p->uniforms[uniform_index];
                p->active_uniform_by_name.erase(uniform.name);
                const size_t dot = uniform.name.rfind('.');
                const std::string member = dot == std::string::npos
                    ? uniform.name : uniform.name.substr(dot + 1);
                uniform.name = block.name + "." + member;
                p->active_uniform_by_name[uniform.name] = uniform_index;
            }
        }
    }
    RegisterUniformLocations(*p);
    if (v::IsInitialized() &&
        !EnsureBackendProgram(p, BackendProgramCreateSite::Link)) {
        ML_LOG_WARN("glLinkProgram(%u): native program prewarm failed; "
                    "draw will retry", program);
    }
    ML_LOG_DEBUG("glLinkProgram(%u): VS=%zu FS=%zu words, %zu uniforms, "
                 "%zu uniform blocks",
                 program, p->vertex_spirv.size(), p->fragment_spirv.size(),
                 p->uniforms.size(), p->uniform_blocks.size());
}

void APIENTRY glGetProgramiv(GLuint program, GLenum pname, GLint* params) {
    if (!params) return;
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    switch (pname) {
        case GL_LINK_STATUS:      *params = p->linked ? GL_TRUE : GL_FALSE; return;
        case GL_VALIDATE_STATUS:  *params = GL_TRUE; return;
        case GL_INFO_LOG_LENGTH:  *params = (GLint)(p->info_log.size() + 1); return;
        case GL_ACTIVE_UNIFORMS:  *params = (GLint)p->uniforms.size(); return;
        case GL_ACTIVE_UNIFORM_MAX_LENGTH: {
            size_t length = 0;
            for (const auto& uniform : p->uniforms)
                length = std::max(length, ObservableUniformName(uniform).size() + 1);
            *params = static_cast<GLint>(length);
            return;
        }
        case GL_ACTIVE_UNIFORM_BLOCKS:
            *params = static_cast<GLint>(p->uniform_blocks.size()); return;
        case GL_ACTIVE_UNIFORM_BLOCK_MAX_NAME_LENGTH: {
            size_t length = 0;
            for (const auto& block : p->uniform_blocks)
                length = std::max(length, block.name.size() + 1);
            *params = static_cast<GLint>(length);
            return;
        }
        case GL_ACTIVE_ATTRIBUTES:*params = (GLint)p->attrib_locations.size(); return;
        case GL_ATTACHED_SHADERS: *params = (GLint)p->attached.size(); return;
        case GL_DELETE_STATUS:    *params = GL_FALSE; return;
        default:                  PUSH_ERROR(GL_INVALID_ENUM);
    }
}

void APIENTRY glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei* length,
                                  GLchar* infoLog) {
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (!infoLog || bufSize <= 0) { if (length) *length = 0; return; }
    GLsizei n = (GLsizei)p->info_log.size();
    if (n > bufSize - 1) n = bufSize - 1;
    std::memcpy(infoLog, p->info_log.data(), (size_t)n);
    infoLog[n] = 0;
    if (length) *length = n;
}

void APIENTRY glGetAttachedShaders(GLuint program, GLsizei maxCount, GLsizei* count,
                                   GLuint* shaders) {
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (!shaders || maxCount <= 0) { if (count) *count = 0; return; }
    GLsizei n = (GLsizei)p->attached.size();
    if (n > maxCount) n = maxCount;
    for (GLsizei i = 0; i < n; ++i) shaders[i] = p->attached[i];
    if (count) *count = n;
}

void APIENTRY glUseProgram(GLuint program) {
    sh::Program* linked = program ? sh::GetProgram(program) : nullptr;
    if (program != 0 && linked == nullptr) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    s::GetState().current_program = program;
    if (linked && linked->linked && v::IsInitialized() &&
        !EnsureBackendProgram(linked, BackendProgramCreateSite::Use)) {
        ML_LOG_WARN("glUseProgram(%u): native program prewarm failed; "
                    "draw will retry", program);
    }
}

void APIENTRY glValidateProgram(GLuint program) {
    if (!sh::GetProgram(program)) { PUSH_ERROR(GL_INVALID_VALUE); return; }
}

GLint APIENTRY glGetUniformLocation(GLuint program, const GLchar* name) {
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_VALUE); return -1; }
    if (!p->linked || !name) return -1;
    auto it = p->uniform_by_name.find(name);
    return it == p->uniform_by_name.end() ? -1 : it->second;
}

GLint APIENTRY glGetAttribLocation(GLuint program, const GLchar* name) {
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_VALUE); return -1; }
    if (!p->linked || !name) return -1;
    auto it = p->attrib_locations.find(name);
    return it == p->attrib_locations.end() ? -1 : it->second;
}

GLint APIENTRY glGetFragDataLocation(GLuint program, const GLchar* name) {
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_OPERATION); return -1; }
    if (!p->linked || !name) return -1;
    auto it = p->frag_data_locations.find(name);
    return it == p->frag_data_locations.end() ? -1 : it->second;
}

GLint APIENTRY glGetFragDataIndex(GLuint program, const GLchar* name) {
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_OPERATION); return -1; }
    if (!p->linked || !name) return -1;
    auto it = p->frag_data_indices.find(name);
    return it == p->frag_data_indices.end() ? -1 : it->second;
}

void APIENTRY glGetActiveUniform(GLuint program, GLuint index, GLsizei bufSize,
                                 GLsizei* length, GLint* size, GLenum* type, GLchar* name) {
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (index >= p->uniforms.size()) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    const sh::Uniform& u = p->uniforms[index];
    if (size) *size = u.size;
    if (type) *type = u.type;
    if (name && bufSize > 0) {
        const std::string observable_name = ObservableUniformName(u);
        GLsizei n = (GLsizei)observable_name.size();
        if (n > bufSize - 1) n = bufSize - 1;
        std::memcpy(name, observable_name.data(), (size_t)n);
        name[n] = 0;
        if (length) *length = n;
    } else if (length) *length = 0;
}

void APIENTRY glGetActiveUniformName(GLuint program, GLuint uniformIndex,
                                     GLsizei bufSize, GLsizei* length,
                                     GLchar* uniformName) {
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (uniformIndex >= p->uniforms.size()) {
        PUSH_ERROR(GL_INVALID_VALUE); return;
    }
    if (!uniformName || bufSize <= 0) {
        if (length) *length = 0;
        return;
    }
    const std::string name = ObservableUniformName(p->uniforms[uniformIndex]);
    GLsizei count = std::min<GLsizei>(static_cast<GLsizei>(name.size()),
                                     bufSize - 1);
    std::memcpy(uniformName, name.data(), static_cast<size_t>(count));
    uniformName[count] = 0;
    if (length) *length = count;
}

void APIENTRY glGetUniformIndices(GLuint program, GLsizei uniformCount,
                                  const GLchar* const* uniformNames,
                                  GLuint* uniformIndices) {
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (uniformCount < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (!uniformNames || !uniformIndices) return;
    for (GLsizei i = 0; i < uniformCount; ++i) {
        const auto found = uniformNames[i]
            ? p->active_uniform_by_name.find(uniformNames[i])
            : p->active_uniform_by_name.end();
        uniformIndices[i] = found == p->active_uniform_by_name.end()
            ? GL_INVALID_INDEX : found->second;
    }
}

void APIENTRY glGetActiveUniformsiv(GLuint program, GLsizei uniformCount,
                                    const GLuint* uniformIndices, GLenum pname,
                                    GLint* params) {
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (uniformCount < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (!uniformIndices || !params) return;
    for (GLsizei i = 0; i < uniformCount; ++i) {
        if (uniformIndices[i] >= p->uniforms.size()) {
            PUSH_ERROR(GL_INVALID_VALUE); return;
        }
        const sh::Uniform& uniform = p->uniforms[uniformIndices[i]];
        switch (pname) {
            case GL_UNIFORM_TYPE: params[i] = static_cast<GLint>(uniform.type); break;
            case GL_UNIFORM_SIZE: params[i] = uniform.size; break;
            case GL_UNIFORM_NAME_LENGTH:
                params[i] = static_cast<GLint>(
                    ObservableUniformName(uniform).size() + 1); break;
            case GL_UNIFORM_BLOCK_INDEX: params[i] = uniform.block_index; break;
            case GL_UNIFORM_OFFSET: params[i] = uniform.offset; break;
            case GL_UNIFORM_ARRAY_STRIDE: params[i] = uniform.array_stride; break;
            case GL_UNIFORM_MATRIX_STRIDE: params[i] = uniform.matrix_stride; break;
            case GL_UNIFORM_IS_ROW_MAJOR: params[i] = uniform.row_major; break;
            default: PUSH_ERROR(GL_INVALID_ENUM); return;
        }
    }
}

GLuint APIENTRY glGetUniformBlockIndex(GLuint program,
                                       const GLchar* uniformBlockName) {
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_VALUE); return GL_INVALID_INDEX; }
    if (!p->linked || !uniformBlockName) return GL_INVALID_INDEX;
    const auto found = p->uniform_block_by_name.find(uniformBlockName);
    return found == p->uniform_block_by_name.end()
        ? GL_INVALID_INDEX : found->second;
}

void APIENTRY glGetActiveUniformBlockName(GLuint program,
                                          GLuint uniformBlockIndex,
                                          GLsizei bufSize, GLsizei* length,
                                          GLchar* uniformBlockName) {
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (uniformBlockIndex >= p->uniform_blocks.size()) {
        PUSH_ERROR(GL_INVALID_VALUE); return;
    }
    if (!uniformBlockName || bufSize <= 0) {
        if (length) *length = 0;
        return;
    }
    const std::string& name = p->uniform_blocks[uniformBlockIndex].name;
    const GLsizei count = std::min<GLsizei>(
        static_cast<GLsizei>(name.size()), bufSize - 1);
    std::memcpy(uniformBlockName, name.data(), static_cast<size_t>(count));
    uniformBlockName[count] = 0;
    if (length) *length = count;
}

void APIENTRY glGetActiveUniformBlockiv(GLuint program,
                                        GLuint uniformBlockIndex, GLenum pname,
                                        GLint* params) {
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (uniformBlockIndex >= p->uniform_blocks.size()) {
        PUSH_ERROR(GL_INVALID_VALUE); return;
    }
    if (!params) return;
    const sh::UniformBlock& block = p->uniform_blocks[uniformBlockIndex];
    switch (pname) {
        case GL_UNIFORM_BLOCK_BINDING:
            *params = static_cast<GLint>(block.binding); return;
        case GL_UNIFORM_BLOCK_DATA_SIZE: *params = block.data_size; return;
        case GL_UNIFORM_BLOCK_NAME_LENGTH:
            *params = static_cast<GLint>(block.name.size() + 1); return;
        case GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS:
            *params = static_cast<GLint>(block.active_uniform_indices.size()); return;
        case GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES:
            for (size_t i = 0; i < block.active_uniform_indices.size(); ++i)
                params[i] = static_cast<GLint>(block.active_uniform_indices[i]);
            return;
        case GL_UNIFORM_BLOCK_REFERENCED_BY_VERTEX_SHADER:
            *params = block.referenced_vertex ? GL_TRUE : GL_FALSE; return;
        case GL_UNIFORM_BLOCK_REFERENCED_BY_FRAGMENT_SHADER:
            *params = block.referenced_fragment ? GL_TRUE : GL_FALSE; return;
        case GL_UNIFORM_BLOCK_REFERENCED_BY_GEOMETRY_SHADER:
            *params = GL_FALSE; return;
        default: PUSH_ERROR(GL_INVALID_ENUM); return;
    }
}

void APIENTRY glUniformBlockBinding(GLuint program, GLuint uniformBlockIndex,
                                    GLuint uniformBlockBinding) {
    auto* p = sh::GetProgram(program);
    if (!p || !p->linked) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    if (uniformBlockIndex >= p->uniform_blocks.size() ||
        uniformBlockBinding >= kMaxUniformBufferBindings) {
        PUSH_ERROR(GL_INVALID_VALUE); return;
    }
    p->uniform_blocks[uniformBlockIndex].binding = uniformBlockBinding;
}

void APIENTRY glGetActiveAttrib(GLuint program, GLuint index, GLsizei bufSize,
                                GLsizei* length, GLint* size, GLenum* type, GLchar* name) {
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (index >= p->attrib_locations.size()) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    auto it = p->attrib_locations.begin();
    std::advance(it, index);
    if (size) *size = 1;
    if (type) *type = GL_FLOAT;
    if (name && bufSize > 0) {
        GLsizei n = (GLsizei)it->first.size();
        if (n > bufSize - 1) n = bufSize - 1;
        std::memcpy(name, it->first.data(), (size_t)n);
        name[n] = 0;
        if (length) *length = n;
    } else if (length) *length = 0;
}

void APIENTRY glGetUniformfv(GLuint program, GLint location, GLfloat* params) {
    if (!params) return;
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    sh::Uniform* uniform = nullptr;
    GLint element = 0;
    if (!ResolveUniformLocation(p, location, &uniform, &element)) {
        PUSH_ERROR(GL_INVALID_OPERATION); return;
    }
    const size_t components = UniformComponentCount(uniform->type);
    const size_t start = static_cast<size_t>(element) * components;
    for (size_t i = 0; i < components; ++i)
        params[i] = start + i < uniform->value.size()
            ? uniform->value[start + i] : 0.0f;
}

void APIENTRY glGetUniformiv(GLuint program, GLint location, GLint* params) {
    if (!params) return;
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    sh::Uniform* uniform = nullptr;
    GLint element = 0;
    if (!ResolveUniformLocation(p, location, &uniform, &element)) {
        PUSH_ERROR(GL_INVALID_OPERATION); return;
    }
    const size_t components = UniformComponentCount(uniform->type);
    const size_t start = static_cast<size_t>(element) * components;
    for (size_t i = 0; i < components; ++i) {
        if (IsSignedIntegerUniformType(uniform->type) ||
            IsSamplerUniformType(uniform->type)) {
            GLint exact = 0;
            ReadRawSignedScalar(*uniform, start + i, &exact);
            params[i] = exact;
        } else if (IsBooleanUniformType(uniform->type)) {
            GLuint exact = 0;
            ReadRawUnsignedScalar(*uniform, start + i, &exact);
            params[i] = static_cast<GLint>(exact);
        } else {
            params[i] = start + i < uniform->value.size()
                ? static_cast<GLint>(uniform->value[start + i]) : 0;
        }
    }
}

void APIENTRY glGetUniformuiv(GLuint program, GLint location, GLuint* params) {
    if (!params) return;
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    sh::Uniform* uniform = nullptr;
    GLint element = 0;
    if (!ResolveUniformLocation(p, location, &uniform, &element)) {
        PUSH_ERROR(GL_INVALID_OPERATION); return;
    }
    const size_t components = UniformComponentCount(uniform->type);
    const size_t start = static_cast<size_t>(element) * components;
    for (size_t i = 0; i < components; ++i) {
        if (IsUnsignedIntegerUniformType(uniform->type) ||
            IsBooleanUniformType(uniform->type)) {
            GLuint exact = 0;
            ReadRawUnsignedScalar(*uniform, start + i, &exact);
            params[i] = exact;
        } else {
            params[i] = start + i < uniform->value.size()
                ? static_cast<GLuint>(uniform->value[start + i]) : 0u;
        }
    }
}

// ---- uniform setters -------------------------------------------------------

namespace {
// Target program for uniform setters: the current program, else none.
sh::Program* CurrentProgramForUniform() {
    GLuint id = s::GetState().current_program;
    return id ? sh::GetProgram(id) : nullptr;
}

void CommitLooseUniformWrite(sh::Uniform* uniform) {
    sh::Program* program = CurrentProgramForUniform();
    if (!program || !uniform || program->uniforms.empty()) return;
    const ptrdiff_t index = uniform - program->uniforms.data();
    if (index < 0 || static_cast<size_t>(index) >= program->uniforms.size()) return;
    if (program->loose_uniform_views.size() != program->uniforms.size())
        program->loose_uniform_views.resize(program->uniforms.size());
    const auto& raw = uniform->raw_value;
    program->loose_uniform_views[static_cast<size_t>(index)] = {
        raw.empty() ? nullptr : raw.data(), static_cast<uint32_t>(raw.size())};
    if (!IsSamplerUniformType(uniform->type)) ++program->loose_uniform_version;
}

bool ResolveUniformWrite(GLenum setter_type, GLint location, GLsizei count,
                         int comps, sh::Uniform** uniform,
                         size_t* scalar_offset, GLsizei* effective_count) {
    if (count < 0) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return false;
    }
    if (location == -1 || count == 0) return false;
    sh::Program* program = CurrentProgramForUniform();
    if (!program) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return false;
    }
    sh::Uniform* resolved = nullptr;
    GLint element = 0;
    if (!ResolveUniformLocation(program, location, &resolved, &element)) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return false;
    }
    if (element < 0 || element >= resolved->size) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return false;
    }
    if (!UniformSetterMatches(resolved->type, setter_type) ||
        UniformComponentCount(resolved->type) != static_cast<size_t>(comps)) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return false;
    }
    if (count > 1 && resolved->size <= 1) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return false;
    }
    const GLsizei remaining = resolved->size - element;
    *effective_count = std::min(count, remaining);
    *scalar_offset = static_cast<size_t>(element) * static_cast<size_t>(comps);
    *uniform = resolved;
    return *effective_count > 0;
}

extern "C++" {
template <typename T>
void StoreBooleanScalars(sh::Uniform* uniform, size_t scalar_offset,
                         const T* values, size_t scalars,
                         size_t total_scalars) {
    if (uniform->value.size() < total_scalars)
        uniform->value.resize(total_scalars, 0.0f);
    const size_t total_bytes = total_scalars * sizeof(GLuint);
    if (uniform->raw_value.size() < total_bytes)
        uniform->raw_value.resize(total_bytes, 0);
    for (size_t i = 0; i < scalars; ++i) {
        const GLuint normalized = values[i] == static_cast<T>(0) ? 0u : 1u;
        uniform->value[scalar_offset + i] = static_cast<float>(normalized);
        std::memcpy(uniform->raw_value.data() +
                        (scalar_offset + i) * sizeof(normalized),
                    &normalized, sizeof(normalized));
    }
}
} // extern "C++"

// Store `count` elements of `comps` components each into uniform `location`.
// Array element locations resolve back to one reflected object so partial
// writes preserve the other elements and backend snapshots remain complete.
void StoreUniform(GLenum type, GLint location, const GLfloat* v, GLsizei count,
                  int comps) {
    if (!v && count > 0) return;
    sh::Uniform* uniform = nullptr;
    size_t scalar_offset = 0;
    GLsizei effective_count = 0;
    if (!ResolveUniformWrite(type, location, count, comps, &uniform,
                             &scalar_offset, &effective_count))
        return;
    const size_t total_scalars = static_cast<size_t>(uniform->size) * comps;
    const size_t scalars = static_cast<size_t>(effective_count) * comps;
    if (IsBooleanUniformType(uniform->type)) {
        StoreBooleanScalars(uniform, scalar_offset, v, scalars, total_scalars);
        CommitLooseUniformWrite(uniform);
        return;
    }
    if (uniform->value.size() < total_scalars)
        uniform->value.resize(total_scalars, 0.0f);
    std::copy(v, v + scalars, uniform->value.begin() + scalar_offset);
    const size_t total_bytes = total_scalars * sizeof(*v);
    if (uniform->raw_value.size() < total_bytes)
        uniform->raw_value.resize(total_bytes, 0);
    std::memcpy(uniform->raw_value.data() + scalar_offset * sizeof(*v), v,
                scalars * sizeof(*v));
    CommitLooseUniformWrite(uniform);
}

void StoreUniformInt(GLenum type, GLint location, const GLint* v,
                     GLsizei count, int comps) {
    if (!v && count > 0) return;
    sh::Uniform* uniform = nullptr;
    size_t scalar_offset = 0;
    GLsizei effective_count = 0;
    if (!ResolveUniformWrite(type, location, count, comps, &uniform,
                             &scalar_offset, &effective_count))
        return;
    const size_t total_scalars = static_cast<size_t>(uniform->size) * comps;
    const size_t scalars = static_cast<size_t>(effective_count) * comps;
    if (IsBooleanUniformType(uniform->type)) {
        StoreBooleanScalars(uniform, scalar_offset, v, scalars, total_scalars);
        CommitLooseUniformWrite(uniform);
        return;
    }
    if (uniform->value.size() < total_scalars)
        uniform->value.resize(total_scalars, 0.0f);
    for (size_t i = 0; i < scalars; ++i)
        uniform->value[scalar_offset + i] = static_cast<float>(v[i]);
    const size_t total_bytes = total_scalars * sizeof(*v);
    if (uniform->raw_value.size() < total_bytes)
        uniform->raw_value.resize(total_bytes, 0);
    std::memcpy(uniform->raw_value.data() + scalar_offset * sizeof(*v), v,
                scalars * sizeof(*v));
    CommitLooseUniformWrite(uniform);
}

void StoreUniformUInt(GLenum type, GLint location, const GLuint* v,
                      GLsizei count, int comps) {
    if (!v && count > 0) return;
    sh::Uniform* uniform = nullptr;
    size_t scalar_offset = 0;
    GLsizei effective_count = 0;
    if (!ResolveUniformWrite(type, location, count, comps, &uniform,
                             &scalar_offset, &effective_count))
        return;
    const size_t total_scalars = static_cast<size_t>(uniform->size) * comps;
    const size_t scalars = static_cast<size_t>(effective_count) * comps;
    if (IsBooleanUniformType(uniform->type)) {
        StoreBooleanScalars(uniform, scalar_offset, v, scalars, total_scalars);
        CommitLooseUniformWrite(uniform);
        return;
    }
    if (uniform->value.size() < total_scalars)
        uniform->value.resize(total_scalars, 0.0f);
    for (size_t i = 0; i < scalars; ++i)
        uniform->value[scalar_offset + i] = static_cast<float>(v[i]);
    const size_t total_bytes = total_scalars * sizeof(*v);
    if (uniform->raw_value.size() < total_bytes)
        uniform->raw_value.resize(total_bytes, 0);
    std::memcpy(uniform->raw_value.data() + scalar_offset * sizeof(*v), v,
                scalars * sizeof(*v));
    CommitLooseUniformWrite(uniform);
}

void StoreUniformMatrix(GLenum type, GLint location, GLsizei count,
                        GLboolean transpose, const GLfloat* value,
                        int columns, int rows) {
    if (count < 0) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    if (location == -1 || !value || count == 0) return;
    const int components = columns * rows;
    if (transpose == GL_FALSE) {
        StoreUniform(type, location, value, count, components);
        return;
    }
    // GL accepts row-major input when transpose is true but stores the same
    // mathematical matrix as a normalized column-major scalar sequence.
    std::vector<GLfloat> column_major(
        static_cast<size_t>(count) * components);
    for (GLsizei matrix = 0; matrix < count; ++matrix) {
        const GLfloat* source = value +
            static_cast<size_t>(matrix) * components;
        GLfloat* destination = column_major.data() +
            static_cast<size_t>(matrix) * components;
        for (int column = 0; column < columns; ++column)
            for (int row = 0; row < rows; ++row)
                destination[column * rows + row] =
                    source[row * columns + column];
    }
    StoreUniform(type, location, column_major.data(), count, components);
}
} // namespace

void APIENTRY glUniform1f(GLint location, GLfloat v0) {
    GLfloat v[1] = {v0}; StoreUniform(GL_FLOAT, location, v, 1, 1);
}
void APIENTRY glUniform2f(GLint location, GLfloat v0, GLfloat v1) {
    GLfloat v[2] = {v0, v1}; StoreUniform(GL_FLOAT_VEC2, location, v, 1, 2);
}
void APIENTRY glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) {
    GLfloat v[3] = {v0, v1, v2}; StoreUniform(GL_FLOAT_VEC3, location, v, 1, 3);
}
void APIENTRY glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) {
    GLfloat v[4] = {v0, v1, v2, v3}; StoreUniform(GL_FLOAT_VEC4, location, v, 1, 4);
}
void APIENTRY glUniform1i(GLint location, GLint v0) {
    GLint v[1] = {v0}; StoreUniformInt(GL_INT, location, v, 1, 1);
}
void APIENTRY glUniform2i(GLint location, GLint v0, GLint v1) {
    GLint v[2] = {v0, v1}; StoreUniformInt(GL_INT_VEC2, location, v, 1, 2);
}
void APIENTRY glUniform3i(GLint location, GLint v0, GLint v1, GLint v2) {
    GLint v[3] = {v0, v1, v2}; StoreUniformInt(GL_INT_VEC3, location, v, 1, 3);
}
void APIENTRY glUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3) {
    GLint v[4] = {v0, v1, v2, v3}; StoreUniformInt(GL_INT_VEC4, location, v, 1, 4);
}
void APIENTRY glUniform1ui(GLint location, GLuint v0) {
    GLuint v[1] = {v0}; StoreUniformUInt(GL_UNSIGNED_INT, location, v, 1, 1);
}
void APIENTRY glUniform2ui(GLint location, GLuint v0, GLuint v1) {
    GLuint v[2] = {v0, v1}; StoreUniformUInt(GL_UNSIGNED_INT_VEC2, location, v, 1, 2);
}
void APIENTRY glUniform3ui(GLint location, GLuint v0, GLuint v1, GLuint v2) {
    GLuint v[3] = {v0, v1, v2};
    StoreUniformUInt(GL_UNSIGNED_INT_VEC3, location, v, 1, 3);
}
void APIENTRY glUniform4ui(GLint location, GLuint v0, GLuint v1, GLuint v2, GLuint v3) {
    GLuint v[4] = {v0, v1, v2, v3};
    StoreUniformUInt(GL_UNSIGNED_INT_VEC4, location, v, 1, 4);
}

void APIENTRY glUniform1fv(GLint location, GLsizei count, const GLfloat* value) {
    StoreUniform(GL_FLOAT, location, value, count, 1);
}
void APIENTRY glUniform2fv(GLint location, GLsizei count, const GLfloat* value) {
    StoreUniform(GL_FLOAT_VEC2, location, value, count, 2);
}
void APIENTRY glUniform3fv(GLint location, GLsizei count, const GLfloat* value) {
    StoreUniform(GL_FLOAT_VEC3, location, value, count, 3);
}
void APIENTRY glUniform4fv(GLint location, GLsizei count, const GLfloat* value) {
    StoreUniform(GL_FLOAT_VEC4, location, value, count, 4);
}
void APIENTRY glUniform1iv(GLint location, GLsizei count, const GLint* value) {
    StoreUniformInt(GL_INT, location, value, count, 1);
}
void APIENTRY glUniform2iv(GLint location, GLsizei count, const GLint* value) {
    StoreUniformInt(GL_INT_VEC2, location, value, count, 2);
}
void APIENTRY glUniform3iv(GLint location, GLsizei count, const GLint* value) {
    StoreUniformInt(GL_INT_VEC3, location, value, count, 3);
}
void APIENTRY glUniform4iv(GLint location, GLsizei count, const GLint* value) {
    StoreUniformInt(GL_INT_VEC4, location, value, count, 4);
}
void APIENTRY glUniform1uiv(GLint location, GLsizei count, const GLuint* value) {
    StoreUniformUInt(GL_UNSIGNED_INT, location, value, count, 1);
}
void APIENTRY glUniform2uiv(GLint location, GLsizei count, const GLuint* value) {
    StoreUniformUInt(GL_UNSIGNED_INT_VEC2, location, value, count, 2);
}
void APIENTRY glUniform3uiv(GLint location, GLsizei count, const GLuint* value) {
    StoreUniformUInt(GL_UNSIGNED_INT_VEC3, location, value, count, 3);
}
void APIENTRY glUniform4uiv(GLint location, GLsizei count, const GLuint* value) {
    StoreUniformUInt(GL_UNSIGNED_INT_VEC4, location, value, count, 4);
}

void APIENTRY glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose,
                                 const GLfloat* value) {
    StoreUniformMatrix(GL_FLOAT_MAT2, location, count, transpose, value, 2, 2);
}
void APIENTRY glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose,
                                 const GLfloat* value) {
    StoreUniformMatrix(GL_FLOAT_MAT3, location, count, transpose, value, 3, 3);
}
void APIENTRY glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose,
                                 const GLfloat* value) {
    StoreUniformMatrix(GL_FLOAT_MAT4, location, count, transpose, value, 4, 4);
}
void APIENTRY glUniformMatrix2x3fv(GLint location, GLsizei count, GLboolean transpose,
                                   const GLfloat* value) {
    StoreUniformMatrix(
        GL_FLOAT_MAT2x3, location, count, transpose, value, 2, 3);
}
void APIENTRY glUniformMatrix3x2fv(GLint location, GLsizei count, GLboolean transpose,
                                   const GLfloat* value) {
    StoreUniformMatrix(
        GL_FLOAT_MAT3x2, location, count, transpose, value, 3, 2);
}
void APIENTRY glUniformMatrix2x4fv(GLint location, GLsizei count, GLboolean transpose,
                                   const GLfloat* value) {
    StoreUniformMatrix(
        GL_FLOAT_MAT2x4, location, count, transpose, value, 2, 4);
}
void APIENTRY glUniformMatrix4x2fv(GLint location, GLsizei count, GLboolean transpose,
                                   const GLfloat* value) {
    StoreUniformMatrix(
        GL_FLOAT_MAT4x2, location, count, transpose, value, 4, 2);
}
void APIENTRY glUniformMatrix3x4fv(GLint location, GLsizei count, GLboolean transpose,
                                   const GLfloat* value) {
    StoreUniformMatrix(
        GL_FLOAT_MAT3x4, location, count, transpose, value, 3, 4);
}
void APIENTRY glUniformMatrix4x3fv(GLint location, GLsizei count, GLboolean transpose,
                                   const GLfloat* value) {
    StoreUniformMatrix(
        GL_FLOAT_MAT4x3, location, count, transpose, value, 4, 3);
}

void mithrilResetProgramPrewarmStats(void) {
    g_program_prewarm_stats = EmptyProgramPrewarmStats();
}

int mithrilGetProgramPrewarmStatsV1(
    MithrilProgramPrewarmStatsV1* output, size_t output_size) {
    if (!output || output_size < sizeof(*output)) return 0;
    *output = g_program_prewarm_stats;
    return 1;
}

} // extern "C"
