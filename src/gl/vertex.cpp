// Mithril-Wrapper GL entry points -- S3 vertex/buffer domain (M2-VK, M3).
// glGen/Bind/DeleteVertexArrays, glGen/Bind/DeleteBuffers, buffer uploads,
// mapping and queries, glVertexAttribPointer/Divisor/constant 1-4s and the
// attribute getters. Owns the shared VAO/VBO name tables (internal.h).

#include "internal.h"

#include <algorithm>
#include <cstring>
#include <utility>

// Shared tables (declared extern in internal.h; the draw path reads
// them through the header).
std::unordered_map<GLuint, VAOData> g_vaos;
std::unordered_map<GLuint, BufferData> g_buffers;
GLuint g_next_vao = 1, g_next_buffer = 1;
GLuint g_bound_vao = 0;
GLuint g_bound_array_buffer = 0;
GLuint g_bound_element_buffer = 0;

// program id -> Vulkan program handle (created lazily on first draw by the
// draw path; erased by the shader-lifecycle path on glDeleteProgram).
std::unordered_map<GLuint, uint64_t> g_vk_programs;

extern "C" {

// ---- vertex arrays / buffers / draw (milestone M2-VK) -----------------------

namespace {
GLuint NewName(std::unordered_map<GLuint, VAOData>& table, GLuint& next) {
    while (table.count(next)) ++next;
    table.emplace(next, VAOData{});
    return next++;
}

} // namespace

void APIENTRY glGenVertexArrays(GLsizei n, GLuint* arrays) {
    if (n < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    for (GLsizei i = 0; i < n; ++i) arrays[i] = NewName(g_vaos, g_next_vao);
}

void APIENTRY glDeleteVertexArrays(GLsizei n, const GLuint* arrays) {
    if (n < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    for (GLsizei i = 0; i < n; ++i) {
        auto it = g_vaos.find(arrays[i]);
        if (it == g_vaos.end()) continue;
        if (g_bound_vao == arrays[i]) g_bound_vao = 0;
        g_vaos.erase(it);
    }
}

void APIENTRY glBindVertexArray(GLuint array) {
    if (array != 0 && !g_vaos.count(array)) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    g_bound_vao = array;
}

GLboolean APIENTRY glIsVertexArray(GLuint array) {
    return g_vaos.count(array) ? GL_TRUE : GL_FALSE;
}

void APIENTRY glGenBuffers(GLsizei n, GLuint* buffers) {
    if (n < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    for (GLsizei i = 0; i < n; ++i) {
        while (g_buffers.count(g_next_buffer)) ++g_next_buffer;
        buffers[i] = g_next_buffer++;
        g_buffers.emplace(buffers[i], BufferData{});
    }
}

void APIENTRY glDeleteBuffers(GLsizei n, const GLuint* buffers) {
    if (n < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    for (GLsizei i = 0; i < n; ++i) {
        auto it = g_buffers.find(buffers[i]);
        if (it == g_buffers.end()) continue;
        if (g_bound_array_buffer == buffers[i]) g_bound_array_buffer = 0;
        if (g_bound_element_buffer == buffers[i]) g_bound_element_buffer = 0;
        g_buffers.erase(it);
    }
}

GLboolean APIENTRY glIsBuffer(GLuint buffer) {
    return g_buffers.count(buffer) ? GL_TRUE : GL_FALSE;
}

void APIENTRY glBindBuffer(GLenum target, GLuint buffer) {
    if (buffer != 0 && !g_buffers.count(buffer)) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    switch (target) {
        case GL_ARRAY_BUFFER: g_bound_array_buffer = buffer; break;
        case GL_ELEMENT_ARRAY_BUFFER: g_bound_element_buffer = buffer; break;
        default:
            PUSH_ERROR(GL_INVALID_ENUM);
            return;
    }
}

void APIENTRY glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
    (void)usage;
    GLuint* bound = nullptr;
    switch (target) {
        case GL_ARRAY_BUFFER: bound = &g_bound_array_buffer; break;
        case GL_ELEMENT_ARRAY_BUFFER: bound = &g_bound_element_buffer; break;
        default: PUSH_ERROR(GL_INVALID_ENUM); return;
    }
    if (*bound == 0) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    if (size < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    auto it = g_buffers.find(*bound);
    if (data) {
        it->second.data.assign((const uint8_t*)data, (const uint8_t*)data + size);
    } else {
        it->second.data.assign((size_t)size, 0);
    }
}

void APIENTRY glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data) {
    GLuint* bound = nullptr;
    switch (target) {
        case GL_ARRAY_BUFFER: bound = &g_bound_array_buffer; break;
        case GL_ELEMENT_ARRAY_BUFFER: bound = &g_bound_element_buffer; break;
        default: PUSH_ERROR(GL_INVALID_ENUM); return;
    }
    if (*bound == 0) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    if (offset < 0 || size < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    auto it = g_buffers.find(*bound);
    if (offset + size > (GLintptr)it->second.data.size()) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    std::memcpy(it->second.data.data() + offset, data, size);
}

// ---- buffer queries / mapping (M3) -----------------------------------------

BufferData* BoundBufferForTarget(GLenum target, GLenum* error) {
    GLuint* bound = nullptr;
    switch (target) {
        case GL_ARRAY_BUFFER: bound = &g_bound_array_buffer; break;
        case GL_ELEMENT_ARRAY_BUFFER: bound = &g_bound_element_buffer; break;
        default: *error = GL_INVALID_ENUM; return nullptr;
    }
    if (*bound == 0) { *error = GL_INVALID_OPERATION; return nullptr; }
    auto it = g_buffers.find(*bound);
    if (it == g_buffers.end()) { *error = GL_INVALID_OPERATION; return nullptr; }
    return &it->second;
}

void APIENTRY glCopyBufferSubData(GLenum readtarget, GLenum writetarget,
                                  GLintptr readoffset, GLintptr writeoffset,
                                  GLsizeiptr size) {
    GLenum err = GL_NO_ERROR;
    BufferData* src = BoundBufferForTarget(readtarget, &err);
    if (err) { PUSH_ERROR(err); return; }
    err = GL_NO_ERROR;
    BufferData* dst = BoundBufferForTarget(writetarget, &err);
    if (err) { PUSH_ERROR(err); return; }
    if (readoffset < 0 || writeoffset < 0 || size < 0 ||
        readoffset + size > (GLintptr)src->data.size() ||
        writeoffset + size > (GLintptr)dst->data.size()) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    std::memmove(dst->data.data() + writeoffset, src->data.data() + readoffset,
                 size);
}

void APIENTRY glGetBufferParameteriv(GLenum target, GLenum pname, GLint* params) {
    GLenum err = GL_NO_ERROR;
    BufferData* b = BoundBufferForTarget(target, &err);
    if (err) { PUSH_ERROR(err); return; }
    switch (pname) {
        case GL_BUFFER_SIZE: *params = (GLint)b->data.size(); break;
        case GL_BUFFER_USAGE: *params = GL_STATIC_DRAW; break;
        case GL_BUFFER_ACCESS: *params = GL_WRITE_ONLY; break;
        case GL_BUFFER_MAPPED: *params = GL_FALSE; break;
        default: PUSH_ERROR(GL_INVALID_ENUM);
    }
}

void APIENTRY glGetBufferParameteri64v(GLenum target, GLenum pname, GLint64* params) {
    switch (pname) {
        case GL_BUFFER_SIZE: {
            GLenum err = GL_NO_ERROR;
            BufferData* b = BoundBufferForTarget(target, &err);
            if (err) { PUSH_ERROR(err); return; }
            *params = (GLint64)b->data.size();
            break;
        }
        default: PUSH_ERROR(GL_INVALID_ENUM);
    }
}

void APIENTRY glGetBufferPointerv(GLenum target, GLenum pname, void** params) {
    if (pname != GL_BUFFER_MAP_POINTER) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    GLenum err = GL_NO_ERROR;
    BufferData* b = BoundBufferForTarget(target, &err);
    if (err) { PUSH_ERROR(err); return; }
    *params = b->data.data();
}

void APIENTRY glGetBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size,
                                 void* data) {
    GLenum err = GL_NO_ERROR;
    BufferData* b = BoundBufferForTarget(target, &err);
    if (err) { PUSH_ERROR(err); return; }
    if (offset < 0 || size < 0 || offset + size > (GLintptr)b->data.size()) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    std::memcpy(data, b->data.data() + offset, size);
}

void* APIENTRY glMapBuffer(GLenum target, GLenum access) {
    if (access != GL_READ_WRITE && access != GL_WRITE_ONLY && access != GL_READ_ONLY) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return nullptr;
    }
    GLenum err = GL_NO_ERROR;
    BufferData* b = BoundBufferForTarget(target, &err);
    if (err) { PUSH_ERROR(err); return nullptr; }
    if (b->data.empty()) { PUSH_ERROR(GL_OUT_OF_MEMORY); return nullptr; }
    return b->data.data();
}

void* APIENTRY glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length,
                                GLbitfield access) {
    (void)access;
    if (offset < 0 || length < 0) { PUSH_ERROR(GL_INVALID_VALUE); return nullptr; }
    GLenum err = GL_NO_ERROR;
    BufferData* b = BoundBufferForTarget(target, &err);
    if (err) { PUSH_ERROR(err); return nullptr; }
    if (offset + length > (GLintptr)b->data.size()) { PUSH_ERROR(GL_INVALID_VALUE); return nullptr; }
    return b->data.data() + offset;
}

GLboolean APIENTRY glUnmapBuffer(GLenum target) {
    GLenum err = GL_NO_ERROR;
    BoundBufferForTarget(target, &err);
    if (err) { PUSH_ERROR(err); return GL_FALSE; }
    return GL_TRUE;  // host-coherent staging: nothing to flush
}

void APIENTRY glFlushMappedBufferRange(GLenum target, GLintptr offset,
                                       GLsizeiptr length) {
    (void)target; (void)offset; (void)length;
}

void APIENTRY glEnableVertexAttribArray(GLuint index) {
    if (index >= kMaxAttribs) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    g_vaos[g_bound_vao].attribs[index].enabled = true;
}

void APIENTRY glDisableVertexAttribArray(GLuint index) {
    if (index >= kMaxAttribs) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    g_vaos[g_bound_vao].attribs[index].enabled = false;
}

void APIENTRY glVertexAttribPointer(GLuint index, GLint size, GLenum type,
                                    GLboolean normalized, GLsizei stride,
                                    const void* pointer) {
    if (index >= kMaxAttribs) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (size < 1 || size > 4) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (type != GL_FLOAT && type != GL_HALF_FLOAT && type != GL_DOUBLE &&
        type != GL_BYTE && type != GL_UNSIGNED_BYTE && type != GL_SHORT &&
        type != GL_UNSIGNED_SHORT && type != GL_INT && type != GL_UNSIGNED_INT) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }
    if (stride < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    AttribData& a = g_vaos[g_bound_vao].attribs[index];
    a.enabled = true;
    a.size = size;
    a.type = type;
    a.normalized = normalized;
    a.stride = stride;
    a.offset = (GLsizeiptr)pointer;
    a.buffer = g_bound_array_buffer;
    a.is_pointer = true;
}

void APIENTRY glVertexAttribIPointer(GLuint index, GLint size, GLenum type,
                                     GLsizei stride, const void* pointer) {
    if (index >= kMaxAttribs) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (size < 1 || size > 4) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (type != GL_BYTE && type != GL_UNSIGNED_BYTE && type != GL_SHORT &&
        type != GL_UNSIGNED_SHORT && type != GL_INT && type != GL_UNSIGNED_INT) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }
    if (stride < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    AttribData& a = g_vaos[g_bound_vao].attribs[index];
    a.enabled = true;
    a.size = size;
    a.type = type;
    a.normalized = GL_FALSE;
    a.stride = stride;
    a.offset = (GLsizeiptr)pointer;
    a.buffer = g_bound_array_buffer;
    a.is_pointer = true;
}

void APIENTRY glVertexAttribDivisor(GLuint index, GLuint divisor) {
    if (index >= kMaxAttribs) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    g_vaos[g_bound_vao].attribs[index].divisor = divisor;
}

// ---- generic (constant) vertex attributes -----------------------------------

// Constant values apply when the array is *disabled*; setting them must not
// change the enable bit (GL 4.46).
void SetConstantAttrib(GLuint index, const GLfloat* v, GLsizei n) {
    if (index >= kMaxAttribs || n < 1 || n > 4) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    AttribData& a = g_vaos[g_bound_vao].attribs[index];
    a.is_pointer = false;
    for (GLsizei i = 0; i < n; ++i) a.constant[i] = v[i];
    for (GLsizei i = n; i < 4; ++i) a.constant[i] = i == 3 ? 1.0f : 0.0f;
}

void APIENTRY glVertexAttrib1f(GLuint index, GLfloat x) {
    const GLfloat v[1] = {x}; SetConstantAttrib(index, v, 1); }
void APIENTRY glVertexAttrib2f(GLuint index, GLfloat x, GLfloat y) {
    const GLfloat v[2] = {x, y}; SetConstantAttrib(index, v, 2); }
void APIENTRY glVertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z) {
    const GLfloat v[3] = {x, y, z}; SetConstantAttrib(index, v, 3); }
void APIENTRY glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    const GLfloat v[4] = {x, y, z, w}; SetConstantAttrib(index, v, 4); }
void APIENTRY glVertexAttrib1fv(GLuint index, const GLfloat* v) { SetConstantAttrib(index, v, 1); }
void APIENTRY glVertexAttrib2fv(GLuint index, const GLfloat* v) { SetConstantAttrib(index, v, 2); }
void APIENTRY glVertexAttrib3fv(GLuint index, const GLfloat* v) { SetConstantAttrib(index, v, 3); }
void APIENTRY glVertexAttrib4fv(GLuint index, const GLfloat* v) { SetConstantAttrib(index, v, 4); }

void APIENTRY glVertexAttrib1d(GLuint index, GLdouble x) { GLfloat f[1]; f[0]=(GLfloat)x; SetConstantAttrib(index, f, 1); }
void APIENTRY glVertexAttrib2d(GLuint index, GLdouble x, GLdouble y) { GLfloat f[2]; f[0]=(GLfloat)x; f[1]=(GLfloat)y; SetConstantAttrib(index, f, 2); }
void APIENTRY glVertexAttrib3d(GLuint index, GLdouble x, GLdouble y, GLdouble z) { GLfloat f[3]; f[0]=(GLfloat)x; f[1]=(GLfloat)y; f[2]=(GLfloat)z; SetConstantAttrib(index, f, 3); }
void APIENTRY glVertexAttrib4d(GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w) { GLfloat f[4]; f[0]=(GLfloat)x; f[1]=(GLfloat)y; f[2]=(GLfloat)z; f[3]=(GLfloat)w; SetConstantAttrib(index, f, 4); }
void APIENTRY glVertexAttrib1dv(GLuint index, const GLdouble* v) { GLfloat f[1]; f[0]=(GLfloat)v[0]; SetConstantAttrib(index, f, 1); }
void APIENTRY glVertexAttrib2dv(GLuint index, const GLdouble* v) { GLfloat f[2]; f[0]=(GLfloat)v[0]; f[1]=(GLfloat)v[1]; SetConstantAttrib(index, f, 2); }
void APIENTRY glVertexAttrib3dv(GLuint index, const GLdouble* v) { GLfloat f[3]; f[0]=(GLfloat)v[0]; f[1]=(GLfloat)v[1]; f[2]=(GLfloat)v[2]; SetConstantAttrib(index, f, 3); }
void APIENTRY glVertexAttrib4dv(GLuint index, const GLdouble* v) { GLfloat f[4]; f[0]=(GLfloat)v[0]; f[1]=(GLfloat)v[1]; f[2]=(GLfloat)v[2]; f[3]=(GLfloat)v[3]; SetConstantAttrib(index, f, 4); }

void APIENTRY glVertexAttrib1s(GLuint index, GLshort x) { GLfloat f[1]; f[0]=(GLfloat)x; SetConstantAttrib(index, f, 1); }
void APIENTRY glVertexAttrib2s(GLuint index, GLshort x, GLshort y) { GLfloat f[2]; f[0]=(GLfloat)x; f[1]=(GLfloat)y; SetConstantAttrib(index, f, 2); }
void APIENTRY glVertexAttrib3s(GLuint index, GLshort x, GLshort y, GLshort z) { GLfloat f[3]; f[0]=(GLfloat)x; f[1]=(GLfloat)y; f[2]=(GLfloat)z; SetConstantAttrib(index, f, 3); }
void APIENTRY glVertexAttrib4s(GLuint index, GLshort x, GLshort y, GLshort z, GLshort w) { GLfloat f[4]; f[0]=(GLfloat)x; f[1]=(GLfloat)y; f[2]=(GLfloat)z; f[3]=(GLfloat)w; SetConstantAttrib(index, f, 4); }
void APIENTRY glVertexAttrib1sv(GLuint index, const GLshort* v) { GLfloat f[1]; f[0]=(GLfloat)v[0]; SetConstantAttrib(index, f, 1); }
void APIENTRY glVertexAttrib2sv(GLuint index, const GLshort* v) { GLfloat f[2]; f[0]=(GLfloat)v[0]; f[1]=(GLfloat)v[1]; SetConstantAttrib(index, f, 2); }
void APIENTRY glVertexAttrib3sv(GLuint index, const GLshort* v) { GLfloat f[3]; f[0]=(GLfloat)v[0]; f[1]=(GLfloat)v[1]; f[2]=(GLfloat)v[2]; SetConstantAttrib(index, f, 3); }
void APIENTRY glVertexAttrib4sv(GLuint index, const GLshort* v) { GLfloat f[4]; f[0]=(GLfloat)v[0]; f[1]=(GLfloat)v[1]; f[2]=(GLfloat)v[2]; f[3]=(GLfloat)v[3]; SetConstantAttrib(index, f, 4); }

void APIENTRY glVertexAttrib1i(GLuint index, GLint x) { GLfloat f[1]; f[0]=(GLfloat)x; SetConstantAttrib(index, f, 1); }
void APIENTRY glVertexAttrib2i(GLuint index, GLint x, GLint y) { GLfloat f[2]; f[0]=(GLfloat)x; f[1]=(GLfloat)y; SetConstantAttrib(index, f, 2); }
void APIENTRY glVertexAttrib3i(GLuint index, GLint x, GLint y, GLint z) { GLfloat f[3]; f[0]=(GLfloat)x; f[1]=(GLfloat)y; f[2]=(GLfloat)z; SetConstantAttrib(index, f, 3); }
void APIENTRY glVertexAttrib4i(GLuint index, GLint x, GLint y, GLint z, GLint w) { GLfloat f[4]; f[0]=(GLfloat)x; f[1]=(GLfloat)y; f[2]=(GLfloat)z; f[3]=(GLfloat)w; SetConstantAttrib(index, f, 4); }
void APIENTRY glVertexAttrib1iv(GLuint index, const GLint* v) { GLfloat f[1]; f[0]=(GLfloat)v[0]; SetConstantAttrib(index, f, 1); }
void APIENTRY glVertexAttrib2iv(GLuint index, const GLint* v) { GLfloat f[2]; f[0]=(GLfloat)v[0]; f[1]=(GLfloat)v[1]; SetConstantAttrib(index, f, 2); }
void APIENTRY glVertexAttrib3iv(GLuint index, const GLint* v) { GLfloat f[3]; f[0]=(GLfloat)v[0]; f[1]=(GLfloat)v[1]; f[2]=(GLfloat)v[2]; SetConstantAttrib(index, f, 3); }
void APIENTRY glVertexAttrib4iv(GLuint index, const GLint* v) { GLfloat f[4]; f[0]=(GLfloat)v[0]; f[1]=(GLfloat)v[1]; f[2]=(GLfloat)v[2]; f[3]=(GLfloat)v[3]; SetConstantAttrib(index, f, 4); }

void APIENTRY glVertexAttrib1ui(GLuint index, GLuint x) { GLfloat f[1]; f[0]=(GLfloat)x; SetConstantAttrib(index, f, 1); }
void APIENTRY glVertexAttrib2ui(GLuint index, GLuint x, GLuint y) { GLfloat f[2]; f[0]=(GLfloat)x; f[1]=(GLfloat)y; SetConstantAttrib(index, f, 2); }
void APIENTRY glVertexAttrib3ui(GLuint index, GLuint x, GLuint y, GLuint z) { GLfloat f[3]; f[0]=(GLfloat)x; f[1]=(GLfloat)y; f[2]=(GLfloat)z; SetConstantAttrib(index, f, 3); }
void APIENTRY glVertexAttrib4ui(GLuint index, GLuint x, GLuint y, GLuint z, GLuint w) { GLfloat f[4]; f[0]=(GLfloat)x; f[1]=(GLfloat)y; f[2]=(GLfloat)z; f[3]=(GLfloat)w; SetConstantAttrib(index, f, 4); }
void APIENTRY glVertexAttrib1uiv(GLuint index, const GLuint* v) { GLfloat f[1]; f[0]=(GLfloat)v[0]; SetConstantAttrib(index, f, 1); }
void APIENTRY glVertexAttrib2uiv(GLuint index, const GLuint* v) { GLfloat f[2]; f[0]=(GLfloat)v[0]; f[1]=(GLfloat)v[1]; SetConstantAttrib(index, f, 2); }
void APIENTRY glVertexAttrib3uiv(GLuint index, const GLuint* v) { GLfloat f[3]; f[0]=(GLfloat)v[0]; f[1]=(GLfloat)v[1]; f[2]=(GLfloat)v[2]; SetConstantAttrib(index, f, 3); }
void APIENTRY glVertexAttrib4uiv(GLuint index, const GLuint* v) { GLfloat f[4]; f[0]=(GLfloat)v[0]; f[1]=(GLfloat)v[1]; f[2]=(GLfloat)v[2]; f[3]=(GLfloat)v[3]; SetConstantAttrib(index, f, 4); }

void APIENTRY glVertexAttrib4bv(GLuint index, const GLbyte* v) { GLfloat f[4]; for (int i=0;i<4;++i) f[i]=(GLfloat)v[i]; SetConstantAttrib(index, f, 4); }
void APIENTRY glVertexAttrib4ubv(GLuint index, const GLubyte* v) { GLfloat f[4]; for (int i=0;i<4;++i) f[i]=(GLfloat)v[i]; SetConstantAttrib(index, f, 4); }
void APIENTRY glVertexAttrib4usv(GLuint index, const GLushort* v) { GLfloat f[4]; for (int i=0;i<4;++i) f[i]=(GLfloat)v[i]; SetConstantAttrib(index, f, 4); }
void APIENTRY glVertexAttrib4Nbv(GLuint index, const GLbyte* v) { GLfloat f[4]; for (int i=0;i<4;++i) f[i]=(GLfloat)v[i]/127.0f; SetConstantAttrib(index, f, 4); }
void APIENTRY glVertexAttrib4Nsv(GLuint index, const GLshort* v) { GLfloat f[4]; for (int i=0;i<4;++i) f[i]=(GLfloat)v[i]/32767.0f; SetConstantAttrib(index, f, 4); }
void APIENTRY glVertexAttrib4Niv(GLuint index, const GLint* v) { GLfloat f[4]; for (int i=0;i<4;++i) f[i]=(GLfloat)v[i]/2147483647.0f; SetConstantAttrib(index, f, 4); }
void APIENTRY glVertexAttrib4Nubv(GLuint index, const GLubyte* v) { GLfloat f[4]; for (int i=0;i<4;++i) f[i]=(GLfloat)v[i]/255.0f; SetConstantAttrib(index, f, 4); }
void APIENTRY glVertexAttrib4Nusv(GLuint index, const GLushort* v) { GLfloat f[4]; for (int i=0;i<4;++i) f[i]=(GLfloat)v[i]/65535.0f; SetConstantAttrib(index, f, 4); }
void APIENTRY glVertexAttrib4Nuiv(GLuint index, const GLuint* v) { GLfloat f[4]; for (int i=0;i<4;++i) f[i]=(GLfloat)v[i]/4294967295.0f; SetConstantAttrib(index, f, 4); }

// ---- attribute queries ------------------------------------------------------

void GetConstantAttrib(const AttribData& a, GLfloat* out) {
    out[0] = a.constant[0]; out[1] = a.constant[1];
    out[2] = a.constant[2]; out[3] = a.constant[3];
}
void APIENTRY glGetVertexAttribfv(GLuint index, GLenum pname, GLfloat* params) {
    if (index >= kMaxAttribs) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    const AttribData& a = g_vaos[g_bound_vao].attribs[index];
    switch (pname) {
        case GL_VERTEX_ATTRIB_ARRAY_ENABLED: params[0] = a.enabled ? 1.f : 0.f; break;
        case GL_VERTEX_ATTRIB_ARRAY_SIZE: params[0] = (GLfloat)a.size; break;
        case GL_VERTEX_ATTRIB_ARRAY_STRIDE: params[0] = (GLfloat)a.stride; break;
        case GL_VERTEX_ATTRIB_ARRAY_TYPE: params[0] = (GLfloat)a.type; break;
        case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED: params[0] = a.normalized ? 1.f : 0.f; break;
        default: {
            const GLfloat* v = a.constant.data();
            for (int i = 0; i < 4; ++i) params[i] = v[i];
        }
    }
}
void APIENTRY glGetVertexAttribdv(GLuint index, GLenum pname, GLdouble* params) {
    GLfloat f[4]; glGetVertexAttribfv(index, pname, f);
    int n = (pname == GL_VERTEX_ATTRIB_ARRAY_ENABLED || pname == GL_VERTEX_ATTRIB_ARRAY_SIZE ||
             pname == GL_VERTEX_ATTRIB_ARRAY_STRIDE || pname == GL_VERTEX_ATTRIB_ARRAY_TYPE ||
             pname == GL_VERTEX_ATTRIB_ARRAY_NORMALIZED) ? 1 : 4;
    for (int i = 0; i < n; ++i) params[i] = (GLdouble)f[i];
}
void APIENTRY glGetVertexAttribiv(GLuint index, GLenum pname, GLint* params) {
    GLfloat f[4]; glGetVertexAttribfv(index, pname, f);
    int n = (pname == GL_VERTEX_ATTRIB_ARRAY_ENABLED || pname == GL_VERTEX_ATTRIB_ARRAY_SIZE ||
             pname == GL_VERTEX_ATTRIB_ARRAY_STRIDE || pname == GL_VERTEX_ATTRIB_ARRAY_TYPE ||
             pname == GL_VERTEX_ATTRIB_ARRAY_NORMALIZED) ? 1 : 4;
    for (int i = 0; i < n; ++i) params[i] = (GLint)f[i];
}
void APIENTRY glGetVertexAttribIiv(GLuint index, GLenum pname, GLint* params) {
    GLfloat f[4]; glGetVertexAttribfv(index, pname, f);
    int n = (pname == GL_VERTEX_ATTRIB_ARRAY_ENABLED || pname == GL_VERTEX_ATTRIB_ARRAY_SIZE ||
             pname == GL_VERTEX_ATTRIB_ARRAY_STRIDE || pname == GL_VERTEX_ATTRIB_ARRAY_TYPE ||
             pname == GL_VERTEX_ATTRIB_ARRAY_NORMALIZED) ? 1 : 4;
    for (int i = 0; i < n; ++i) params[i] = (GLint)f[i];
}
void APIENTRY glGetVertexAttribIuiv(GLuint index, GLenum pname, GLuint* params) {
    GLfloat f[4]; glGetVertexAttribfv(index, pname, f);
    int n = (pname == GL_VERTEX_ATTRIB_ARRAY_ENABLED || pname == GL_VERTEX_ATTRIB_ARRAY_SIZE ||
             pname == GL_VERTEX_ATTRIB_ARRAY_STRIDE || pname == GL_VERTEX_ATTRIB_ARRAY_TYPE ||
             pname == GL_VERTEX_ATTRIB_ARRAY_NORMALIZED) ? 1 : 4;
    for (int i = 0; i < n; ++i) params[i] = (GLuint)f[i];
}
void APIENTRY glGetVertexAttribPointerv(GLuint index, GLenum pname, void** pointer) {
    if (index >= kMaxAttribs) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (pname != GL_VERTEX_ATTRIB_ARRAY_POINTER) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    *pointer = (void*)(uintptr_t)g_vaos[g_bound_vao].attribs[index].offset;
}

} // extern "C"
