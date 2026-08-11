// Mithril-Wrapper GL entry points -- S2 shader/program/uniform domain
// (milestone M2-S2). Shader object lifecycle, link, use, the glUniform*
// setter families and the uniform getters, backed by mithril::shader,
// plus teardown of the lazily created native-backend program handles.

#include "internal.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>

#include <util/log.h>

extern "C" {

// ---- shaders / programs / uniforms (S2) ------------------------------------

namespace {
namespace sh = mithril::shader;
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
                length = std::max(length, uniform.name.size() + 1);
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
    if (program != 0 && sh::GetProgram(program) == nullptr) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    s::GetState().current_program = program;
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
        GLsizei n = (GLsizei)u.name.size();
        if (n > bufSize - 1) n = bufSize - 1;
        std::memcpy(name, u.name.data(), (size_t)n);
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
    const std::string& name = p->uniforms[uniformIndex].name;
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
                params[i] = static_cast<GLint>(uniform.name.size() + 1); break;
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
    auto it = p->uniform_by_location.find(location);
    if (it == p->uniform_by_location.end()) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    sh::Uniform& u = p->uniforms[it->second];
    if (u.value.empty()) { *params = 0.0f; return; }
    std::memcpy(params, u.value.data(), u.value.size() * sizeof(float));
}

void APIENTRY glGetUniformiv(GLuint program, GLint location, GLint* params) {
    if (!params) return;
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    auto it = p->uniform_by_location.find(location);
    if (it == p->uniform_by_location.end()) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    sh::Uniform& u = p->uniforms[it->second];
    if (u.value.empty()) { *params = 0; return; }
    for (size_t i = 0; i < u.value.size() && i < 4; ++i) params[i] = (GLint)u.value[i];
}

void APIENTRY glGetUniformuiv(GLuint program, GLint location, GLuint* params) {
    if (!params) return;
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    auto it = p->uniform_by_location.find(location);
    if (it == p->uniform_by_location.end()) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    sh::Uniform& u = p->uniforms[it->second];
    if (u.value.empty()) { *params = 0; return; }
    for (size_t i = 0; i < u.value.size() && i < 4; ++i) params[i] = (GLuint)u.value[i];
}

// ---- uniform setters -------------------------------------------------------

namespace {
// Target program for uniform setters: the current program, else none.
sh::Program* CurrentProgramForUniform() {
    GLuint id = s::GetState().current_program;
    return id ? sh::GetProgram(id) : nullptr;
}

// Store `count` elements of `comps` components each into uniform `location`.
// Unreflected locations are ignored per GL semantics.
void StoreUniform(GLenum type, GLint location, const GLfloat* v, GLsizei count, int comps) {
    sh::Program* p = CurrentProgramForUniform();
    if (!p || location < 0 || !v || count <= 0) return;
    auto it = p->uniform_by_location.find(location);
    if (it == p->uniform_by_location.end()) return;
    sh::Uniform& u = p->uniforms[it->second];
    u.type = type;
    u.value.assign(v, v + (size_t)count * (size_t)comps);
}

void StoreUniformInt(GLenum type, GLint location, const GLint* v, GLsizei count, int comps) {
    sh::Program* p = CurrentProgramForUniform();
    if (!p || location < 0 || !v || count <= 0) return;
    auto it = p->uniform_by_location.find(location);
    if (it == p->uniform_by_location.end()) return;
    sh::Uniform& u = p->uniforms[it->second];
    u.type = type;
    u.value.clear();
    for (GLsizei i = 0; i < count * comps; ++i) u.value.push_back((float)v[i]);
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
    GLint v[1] = {(GLint)v0}; StoreUniformInt(GL_UNSIGNED_INT, location, v, 1, 1);
}
void APIENTRY glUniform2ui(GLint location, GLuint v0, GLuint v1) {
    GLint v[2] = {(GLint)v0, (GLint)v1}; StoreUniformInt(GL_UNSIGNED_INT_VEC2, location, v, 1, 2);
}
void APIENTRY glUniform3ui(GLint location, GLuint v0, GLuint v1, GLuint v2) {
    GLint v[3] = {(GLint)v0, (GLint)v1, (GLint)v2};
    StoreUniformInt(GL_UNSIGNED_INT_VEC3, location, v, 1, 3);
}
void APIENTRY glUniform4ui(GLint location, GLuint v0, GLuint v1, GLuint v2, GLuint v3) {
    GLint v[4] = {(GLint)v0, (GLint)v1, (GLint)v2, (GLint)v3};
    StoreUniformInt(GL_UNSIGNED_INT_VEC4, location, v, 1, 4);
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
    std::vector<GLint> tmp(value, value + count);
    StoreUniformInt(GL_UNSIGNED_INT, location, tmp.data(), count, 1);
}
void APIENTRY glUniform2uiv(GLint location, GLsizei count, const GLuint* value) {
    std::vector<GLint> tmp(value, value + count * 2);
    StoreUniformInt(GL_UNSIGNED_INT_VEC2, location, tmp.data(), count, 2);
}
void APIENTRY glUniform3uiv(GLint location, GLsizei count, const GLuint* value) {
    std::vector<GLint> tmp(value, value + count * 3);
    StoreUniformInt(GL_UNSIGNED_INT_VEC3, location, tmp.data(), count, 3);
}
void APIENTRY glUniform4uiv(GLint location, GLsizei count, const GLuint* value) {
    std::vector<GLint> tmp(value, value + count * 4);
    StoreUniformInt(GL_UNSIGNED_INT_VEC4, location, tmp.data(), count, 4);
}

void APIENTRY glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose,
                                 const GLfloat* value) {
    (void)transpose; StoreUniform(GL_FLOAT_MAT2, location, value, count, 4);
}
void APIENTRY glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose,
                                 const GLfloat* value) {
    (void)transpose; StoreUniform(GL_FLOAT_MAT3, location, value, count, 9);
}
void APIENTRY glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose,
                                 const GLfloat* value) {
    (void)transpose; StoreUniform(GL_FLOAT_MAT4, location, value, count, 16);
}
void APIENTRY glUniformMatrix2x3fv(GLint location, GLsizei count, GLboolean transpose,
                                   const GLfloat* value) {
    (void)transpose; StoreUniform(GL_FLOAT_MAT2x3, location, value, count, 6);
}
void APIENTRY glUniformMatrix3x2fv(GLint location, GLsizei count, GLboolean transpose,
                                   const GLfloat* value) {
    (void)transpose; StoreUniform(GL_FLOAT_MAT3x2, location, value, count, 6);
}
void APIENTRY glUniformMatrix2x4fv(GLint location, GLsizei count, GLboolean transpose,
                                   const GLfloat* value) {
    (void)transpose; StoreUniform(GL_FLOAT_MAT2x4, location, value, count, 8);
}
void APIENTRY glUniformMatrix4x2fv(GLint location, GLsizei count, GLboolean transpose,
                                   const GLfloat* value) {
    (void)transpose; StoreUniform(GL_FLOAT_MAT4x2, location, value, count, 8);
}
void APIENTRY glUniformMatrix3x4fv(GLint location, GLsizei count, GLboolean transpose,
                                   const GLfloat* value) {
    (void)transpose; StoreUniform(GL_FLOAT_MAT3x4, location, value, count, 12);
}
void APIENTRY glUniformMatrix4x3fv(GLint location, GLsizei count, GLboolean transpose,
                                   const GLfloat* value) {
    (void)transpose; StoreUniform(GL_FLOAT_MAT4x3, location, value, count, 12);
}

} // extern "C"
