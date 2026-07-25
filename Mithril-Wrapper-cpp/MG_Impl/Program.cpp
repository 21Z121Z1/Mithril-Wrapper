// Mithril-Wrapper - MG_Impl/Program.cpp
// Shader / program object lifecycle: source, compile (GLSL->SPIR-V), link,
// use, uniform reflection + setters. Linked SPIR-V is cached on the program
// for the Vulkan pipeline cache (backend_get_or_create_pipeline) to consume.
//
// This is the Vulkan/MoltenVK rewrite of the former gl/program.cpp. The Metal
// MSL fields (vertexMSL/fragmentMSL) are replaced with SPIR-V word vectors
// (vertexSpirv/fragmentSpirv); MoltenVK cross-translates the SPIR-V to MSL
// internally at vkCreateShaderModule time.
#include "includes.h"
#include "Shader.h"

#include <algorithm>
#include <vector>

extern "C" {

GLuint glCreateShader(GLenum type) {
    MITHRIL_ENSURE_INIT();
    GLuint name = g_state->nextName++;
    mithril::Shader s{};
    s.id = name;
    s.type = type;
    g_state->shaders[name] = s;
    return name;
}

void glDeleteShader(GLuint shader) {
    MITHRIL_ENSURE_INIT();
    g_state->shaders.erase(shader);
}

GLuint glCreateProgram(void) {
    MITHRIL_ENSURE_INIT();
    GLuint name = g_state->nextName++;
    mithril::Program p{};
    p.id = name;
    g_state->programs[name] = p;
    return name;
}

void glDeleteProgram(GLuint program) {
    MITHRIL_ENSURE_INIT();
    if (g_state->currentProgram == program) g_state->currentProgram = 0;
    // Release the Vulkan shader modules + cached pipelines owned by this program.
    backend_delete_program_resources(program);
    g_state->programs.erase(program);
}

void glShaderSource(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length) {
    MITHRIL_ENSURE_INIT();
    mithril::Shader* s = mithril::state_get_shader(shader);
    if (!s || count <= 0 || !string) return;
    s->source.clear();
    for (GLsizei i = 0; i < count; ++i) {
        if (length && length[i] >= 0) {
            s->source.append(string[i], (size_t)length[i]);
        } else {
            s->source.append(string[i] ? string[i] : "");
        }
    }
}

void glShaderBinary(GLsizei, const GLuint*, GLenum, const void*, GLsizei) {
    MITHRIL_ENSURE_INIT();
    // Pre-compiled shader binaries are not supported.
}

void glCompileShader(GLuint shader) {
    MITHRIL_ENSURE_INIT();
    mithril::Shader* s = mithril::state_get_shader(shader);
    if (!s) return;
    std::string info;
    std::vector<uint32_t> spirv;
    bool ok = mithril::shader_translate(s->type, s->source, spirv, info);
    s->infoLog = info;
    if (ok) {
        s->compiled = true;
        s->spirv = std::move(spirv);
        MITHRIL_LOG_INFO("shader", "Compiled shader %u (%s) -> %zu SPIR-V words",
                         shader,
                         s->type == GL_VERTEX_SHADER ? "vertex" :
                         s->type == GL_FRAGMENT_SHADER ? "fragment" : "other",
                         s->spirv.size());
    } else {
        s->compiled = false;
        MITHRIL_LOG_ERROR("shader", "Failed to compile shader %u: %s",
                          shader, info.c_str());
    }
}

void glReleaseShaderCompiler(void) { MITHRIL_ENSURE_INIT(); }

void glAttachShader(GLuint program, GLuint shader) {
    MITHRIL_ENSURE_INIT();
    mithril::Program* p = mithril::state_get_program(program);
    if (!p) return;
    for (GLuint id : p->attachedShaders) if (id == shader) return;
    p->attachedShaders.push_back(shader);
}

void glDetachShader(GLuint program, GLuint shader) {
    MITHRIL_ENSURE_INIT();
    mithril::Program* p = mithril::state_get_program(program);
    if (!p) return;
    auto& v = p->attachedShaders;
    v.erase(std::remove(v.begin(), v.end(), shader), v.end());
}

void glLinkProgram(GLuint program) {
    MITHRIL_ENSURE_INIT();
    mithril::Program* p = mithril::state_get_program(program);
    if (!p) { mithril::state_set_error(GL_INVALID_OPERATION); return; }

    p->vertexSpirv.clear();
    p->fragmentSpirv.clear();
    p->uniforms.clear();
    p->uniformByLocation.clear();
    p->attribs.clear();
    p->uniformBlocks.clear();

    // If the application called glBindAttribLocation before linking, re-translate
    // the vertex shader with those location overrides so the SPIR-V stage_input
    // locations match the app's vertex descriptor. Fragment shaders are
    // unaffected by attribute bindings.
    const bool has_attrib_bindings = !p->attribBindings.empty();

    bool missing = false;
    for (GLuint sid : p->attachedShaders) {
        mithril::Shader* s = mithril::state_get_shader(sid);
        if (!s) continue;
        if (!s->compiled || s->spirv.empty()) { missing = true; continue; }
        if (s->type == GL_VERTEX_SHADER) {
            if (has_attrib_bindings) {
                // Re-translate with bindings (cache key includes bindings, so
                // a different binding set produces fresh SPIR-V).
                std::vector<uint32_t> spirv;
                std::string info;
                if (mithril::shader_translate(s->type, s->source, spirv, info, &p->attribBindings)) {
                    p->vertexSpirv = std::move(spirv);
                } else {
                    // Fall back to the auto-mapped SPIR-V from glCompileShader.
                    MITHRIL_LOG_ERROR("program", "Re-translation with attrib bindings "
                                      "failed for program %u: %s (using auto-mapped SPIR-V)",
                                      program, info.c_str());
                    p->vertexSpirv = s->spirv;
                }
            } else {
                p->vertexSpirv = s->spirv;
            }
        } else if (s->type == GL_FRAGMENT_SHADER) {
            p->fragmentSpirv = s->spirv;
        }
    }
    if (missing || (p->vertexSpirv.empty() && p->fragmentSpirv.empty())) {
        p->linked = false;
        p->infoLog = "link failed: a required stage was missing or uncompiled";
        MITHRIL_LOG_ERROR("program", "Link failed for program %u: missing stage", program);
        return;
    }
    p->linked = true;
    p->infoLog.clear();
    MITHRIL_LOG_INFO("program", "Linked program %u (VS=%zu SPIR-V words, FS=%zu SPIR-V words)",
                     program, p->vertexSpirv.size(), p->fragmentSpirv.size());
}

void glUseProgram(GLuint program) {
    MITHRIL_ENSURE_INIT();
    if (program != 0 && !mithril::state_get_program(program)) {
        mithril::state_set_error(GL_INVALID_OPERATION);
        return;
    }
    g_state->currentProgram = program;
}

void glValidateProgram(GLuint program) {
    MITHRIL_ENSURE_INIT();
    mithril::Program* p = mithril::state_get_program(program);
    if (!p) return;
    // Validation is a no-op for our purposes; report success if linked.
    (void)p;
}

void glGetShaderiv(GLuint shader, GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    mithril::Shader* s = mithril::state_get_shader(shader);
    if (!s) { *params = 0; return; }
    switch (pname) {
        case GL_SHADER_TYPE:        *params = (GLint)s->type; break;
        case GL_COMPILE_STATUS:     *params = s->compiled ? GL_TRUE : GL_FALSE; break;
        case GL_INFO_LOG_LENGTH:    *params = (GLint)s->infoLog.size(); break;
        case GL_SHADER_SOURCE_LENGTH:*params = (GLint)s->source.size() + 1; break;
        default:                    *params = 0; break;
    }
}

void glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog) {
    MITHRIL_ENSURE_INIT();
    mithril::Shader* s = mithril::state_get_shader(shader);
    if (!s || !infoLog || bufSize <= 0) { if (length) *length = 0; return; }
    GLsizei n = (GLsizei)s->infoLog.size();
    if (n > bufSize - 1) n = bufSize - 1;
    std::memcpy(infoLog, s->infoLog.data(), n);
    infoLog[n] = 0;
    if (length) *length = n;
}

void glGetShaderSource(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* source) {
    MITHRIL_ENSURE_INIT();
    mithril::Shader* s = mithril::state_get_shader(shader);
    if (!s || !source || bufSize <= 0) { if (length) *length = 0; return; }
    GLsizei n = (GLsizei)s->source.size();
    if (n > bufSize - 1) n = bufSize - 1;
    std::memcpy(source, s->source.data(), n);
    source[n] = 0;
    if (length) *length = n;
}

void glGetProgramiv(GLuint program, GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    mithril::Program* p = mithril::state_get_program(program);
    if (!p) { *params = 0; return; }
    switch (pname) {
        case GL_LINK_STATUS:     *params = p->linked ? GL_TRUE : GL_FALSE; break;
        case GL_VALIDATE_STATUS: *params = GL_TRUE; break;
        case GL_INFO_LOG_LENGTH: *params = (GLint)p->infoLog.size(); break;
        case GL_ACTIVE_UNIFORMS: *params = (GLint)p->uniforms.size(); break;
        case GL_ACTIVE_ATTRIBUTES: *params = (GLint)p->attribs.size(); break;
        case GL_ATTACHED_SHADERS: *params = (GLint)p->attachedShaders.size(); break;
        default:                 *params = 0; break;
    }
}

void glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog) {
    MITHRIL_ENSURE_INIT();
    mithril::Program* p = mithril::state_get_program(program);
    if (!p || !infoLog || bufSize <= 0) { if (length) *length = 0; return; }
    GLsizei n = (GLsizei)p->infoLog.size();
    if (n > bufSize - 1) n = bufSize - 1;
    std::memcpy(infoLog, p->infoLog.data(), n);
    infoLog[n] = 0;
    if (length) *length = n;
}

void glGetAttachedShaders(GLuint program, GLsizei maxCount, GLsizei* count, GLuint* shaders) {
    MITHRIL_ENSURE_INIT();
    mithril::Program* p = mithril::state_get_program(program);
    if (!p || !shaders) { if (count) *count = 0; return; }
    GLsizei n = (GLsizei)p->attachedShaders.size();
    if (n > maxCount) n = maxCount;
    for (GLsizei i = 0; i < n; ++i) shaders[i] = p->attachedShaders[i];
    if (count) *count = n;
}

GLint glGetUniformLocation(GLuint program, const GLchar* name) {
    MITHRIL_ENSURE_INIT();
    mithril::Program* p = mithril::state_get_program(program);
    if (!p || !p->linked || !name) return -1;
    auto it = p->uniforms.find(name);
    if (it == p->uniforms.end()) {
        // Allocate a synthetic location on first query so subsequent setters work.
        mithril::Uniform u{};
        u.name = name;
        u.location = (GLint)p->uniforms.size();
        p->uniforms[name] = u;
        p->uniformByLocation[u.location] = name;
        return u.location;
    }
    return it->second.location;
}

void glGetActiveUniform(GLuint program, GLuint index, GLsizei bufSize,
                        GLsizei* length, GLint* size, GLenum* type, GLchar* name) {
    MITHRIL_ENSURE_INIT();
    mithril::Program* p = mithril::state_get_program(program);
    if (!p || !name || bufSize <= 0) { if (length) *length = 0; return; }
    if (index >= p->uniforms.size()) { if (length) *length = 0; return; }
    // Linear scan to the index-th entry.
    GLuint i = 0;
    for (auto& kv : p->uniforms) {
        if (i == index) {
            GLsizei n = (GLsizei)kv.first.size();
            if (n > bufSize - 1) n = bufSize - 1;
            std::memcpy(name, kv.first.data(), n);
            name[n] = 0;
            if (length) *length = n;
            if (size) *size = 1;
            if (type) *type = GL_FLOAT;
            return;
        }
        ++i;
    }
    if (length) *length = 0;
}

void glGetActiveAttrib(GLuint program, GLuint index, GLsizei bufSize,
                       GLsizei* length, GLint* size, GLenum* type, GLchar* name) {
    MITHRIL_ENSURE_INIT();
    mithril::Program* p = mithril::state_get_program(program);
    if (!p || !name || bufSize <= 0) { if (length) *length = 0; return; }
    if (index >= p->attribs.size()) { if (length) *length = 0; return; }
    GLuint i = 0;
    for (auto& kv : p->attribs) {
        if (i == index) {
            GLsizei n = (GLsizei)kv.first.size();
            if (n > bufSize - 1) n = bufSize - 1;
            std::memcpy(name, kv.first.data(), n);
            name[n] = 0;
            if (length) *length = n;
            if (size) *size = 1;
            if (type) *type = GL_FLOAT;
            return;
        }
        ++i;
    }
    if (length) *length = 0;
}

void glGetUniformfv(GLuint program, GLint location, GLfloat* params) {
    MITHRIL_ENSURE_INIT();
    mithril::Program* p = mithril::state_get_program(program);
    if (!p || !params) return;
    auto it = p->uniformByLocation.find(location);
    if (it == p->uniformByLocation.end()) { *params = 0; return; }
    auto& u = p->uniforms[it->second];
    if (!u.value.empty()) *params = u.value[0];
    else *params = 0;
}

void glGetUniformiv(GLuint program, GLint location, GLint* params) {
    MITHRIL_ENSURE_INIT();
    mithril::Program* p = mithril::state_get_program(program);
    if (!p || !params) return;
    auto it = p->uniformByLocation.find(location);
    if (it == p->uniformByLocation.end()) { *params = 0; return; }
    auto& u = p->uniforms[it->second];
    *params = u.value.empty() ? 0 : (GLint)u.value[0];
}

GLuint glGetUniformBlockIndex(GLuint program, const GLchar* uniformBlockName) {
    MITHRIL_ENSURE_INIT();
    mithril::Program* p = mithril::state_get_program(program);
    if (!p || !uniformBlockName) return 0xFFFFFFFFu;
    auto it = p->uniformBlocks.find(uniformBlockName);
    if (it == p->uniformBlocks.end()) return 0xFFFFFFFFu;
    return it->second;
}

void glGetActiveUniformBlockiv(GLuint program, GLuint uniformBlockIndex,
                               GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    (void)program; (void)uniformBlockIndex; (void)pname;
    if (params) *params = 0;
}

void glUniformBlockBinding(GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding) {
    MITHRIL_ENSURE_INIT();
    (void)program; (void)uniformBlockIndex; (void)uniformBlockBinding;
}

/* ---- Uniform setters ----
 * The Vulkan backend consumes uniform values via push constants or uniform
 * buffers bound by the draw path. Here we just cache the latest value on the
 * program so the draw path can push it into a uniform buffer.
 */
static mithril::Program* current_program() {
    return mithril::state_get_program(g_state->currentProgram);
}

static void store_uniform(GLint location, const GLfloat* v, int count, int comps) {
    mithril::Program* p = current_program();
    if (!p || location < 0 || !v) return;
    auto it = p->uniformByLocation.find(location);
    std::string name = (it != p->uniformByLocation.end()) ? it->second : "";
    mithril::Uniform& u = p->uniforms[name];
    u.name = name;
    u.location = location;
    u.type = GL_FLOAT;
    u.value.assign(v, v + (size_t)count * comps);
}

static void store_uniform_int(GLint location, const GLint* v, int count, int comps) {
    mithril::Program* p = current_program();
    if (!p || location < 0 || !v) return;
    auto it = p->uniformByLocation.find(location);
    std::string name = (it != p->uniformByLocation.end()) ? it->second : "";
    mithril::Uniform& u = p->uniforms[name];
    u.name = name;
    u.location = location;
    u.type = GL_INT;
    u.value.clear();
    for (int i = 0; i < count * comps; ++i) u.value.push_back((float)v[i]);
}

void glUniform1f(GLint loc, GLfloat v0)                                    { store_uniform(loc, &v0, 1, 1); }
void glUniform2f(GLint loc, GLfloat v0, GLfloat v1)                        { GLfloat v[2] = {v0,v1}; store_uniform(loc, v, 1, 2); }
void glUniform3f(GLint loc, GLfloat v0, GLfloat v1, GLfloat v2)            { GLfloat v[3] = {v0,v1,v2}; store_uniform(loc, v, 1, 3); }
void glUniform4f(GLint loc, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3){ GLfloat v[4] = {v0,v1,v2,v3}; store_uniform(loc, v, 1, 4); }

void glUniform1i(GLint loc, GLint v0)                                      { store_uniform_int(loc, &v0, 1, 1); }
void glUniform2i(GLint loc, GLint v0, GLint v1)                            { GLint v[2] = {v0,v1}; store_uniform_int(loc, v, 1, 2); }
void glUniform3i(GLint loc, GLint v0, GLint v1, GLint v2)                  { GLint v[3] = {v0,v1,v2}; store_uniform_int(loc, v, 1, 3); }
void glUniform4i(GLint loc, GLint v0, GLint v1, GLint v2, GLint v3)        { GLint v[4] = {v0,v1,v2,v3}; store_uniform_int(loc, v, 1, 4); }

void glUniform1ui(GLint loc, GLuint v0)                                    { GLint v = (GLint)v0; store_uniform_int(loc, &v, 1, 1); }
void glUniform2ui(GLint loc, GLuint v0, GLuint v1)                         { GLint v[2] = {(GLint)v0,(GLint)v1}; store_uniform_int(loc, v, 1, 2); }
void glUniform3ui(GLint loc, GLuint v0, GLuint v1, GLuint v2)              { GLint v[3] = {(GLint)v0,(GLint)v1,(GLint)v2}; store_uniform_int(loc, v, 1, 3); }
void glUniform4ui(GLint loc, GLuint v0, GLuint v1, GLuint v2, GLuint v3)   { GLint v[4] = {(GLint)v0,(GLint)v1,(GLint)v2,(GLint)v3}; store_uniform_int(loc, v, 1, 4); }

void glUniform1fv(GLint loc, GLsizei c, const GLfloat* v) { store_uniform(loc, v, c, 1); }
void glUniform2fv(GLint loc, GLsizei c, const GLfloat* v) { store_uniform(loc, v, c, 2); }
void glUniform3fv(GLint loc, GLsizei c, const GLfloat* v) { store_uniform(loc, v, c, 3); }
void glUniform4fv(GLint loc, GLsizei c, const GLfloat* v) { store_uniform(loc, v, c, 4); }
void glUniform1iv(GLint loc, GLsizei c, const GLint* v)   { store_uniform_int(loc, v, c, 1); }
void glUniform2iv(GLint loc, GLsizei c, const GLint* v)   { store_uniform_int(loc, v, c, 2); }
void glUniform3iv(GLint loc, GLsizei c, const GLint* v)   { store_uniform_int(loc, v, c, 3); }
void glUniform4iv(GLint loc, GLsizei c, const GLint* v)   { store_uniform_int(loc, v, c, 4); }
void glUniform1uiv(GLint loc, GLsizei c, const GLuint* v) {
    std::vector<GLint> tmp(v, v + c); store_uniform_int(loc, tmp.data(), c, 1);
}
void glUniform2uiv(GLint loc, GLsizei c, const GLuint* v) {
    std::vector<GLint> tmp(v, v + c*2); store_uniform_int(loc, tmp.data(), c, 2);
}
void glUniform3uiv(GLint loc, GLsizei c, const GLuint* v) {
    std::vector<GLint> tmp(v, v + c*3); store_uniform_int(loc, tmp.data(), c, 3);
}
void glUniform4uiv(GLint loc, GLsizei c, const GLuint* v) {
    std::vector<GLint> tmp(v, v + c*4); store_uniform_int(loc, tmp.data(), c, 4);
}

void glUniformMatrix2fv(GLint loc, GLsizei c, GLboolean t, const GLfloat* v)   { (void)t; store_uniform(loc, v, c, 4); }
void glUniformMatrix3fv(GLint loc, GLsizei c, GLboolean t, const GLfloat* v)   { (void)t; store_uniform(loc, v, c, 9); }
void glUniformMatrix4fv(GLint loc, GLsizei c, GLboolean t, const GLfloat* v)   { (void)t; store_uniform(loc, v, c, 16); }
void glUniformMatrix2x3fv(GLint loc, GLsizei c, GLboolean t, const GLfloat* v) { (void)t; store_uniform(loc, v, c, 6); }
void glUniformMatrix3x2fv(GLint loc, GLsizei c, GLboolean t, const GLfloat* v) { (void)t; store_uniform(loc, v, c, 6); }
void glUniformMatrix2x4fv(GLint loc, GLsizei c, GLboolean t, const GLfloat* v) { (void)t; store_uniform(loc, v, c, 8); }
void glUniformMatrix4x2fv(GLint loc, GLsizei c, GLboolean t, const GLfloat* v) { (void)t; store_uniform(loc, v, c, 8); }
void glUniformMatrix3x4fv(GLint loc, GLsizei c, GLboolean t, const GLfloat* v) { (void)t; store_uniform(loc, v, c, 12); }
void glUniformMatrix4x3fv(GLint loc, GLsizei c, GLboolean t, const GLfloat* v) { (void)t; store_uniform(loc, v, c, 12); }

} // extern "C"
