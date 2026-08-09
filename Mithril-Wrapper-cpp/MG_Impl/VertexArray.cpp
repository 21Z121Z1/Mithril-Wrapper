// Mithril-Wrapper - MG_Impl/VertexArray.cpp
// Vertex Array Objects and vertex attribute pointer state.
//
// This is the Vulkan/MoltenVK rewrite of the former gl/vertexattrib.cpp. The
// VAO/attribute state machine is backend-agnostic (it lives entirely in
// mithril::GLState); the only backend-specific touchpoint is that the drawing
// path (Drawing.cpp) reads these attribs to build the VkPipelineVertexInputState.
#include "includes.h"

/* GL vertex-attrib query constants not always present in the minimal
 * glcorearb.h we ship. Standard GL 3.3 Core values. */
#ifndef GL_VERTEX_ATTRIB_ARRAY_ENABLED
#define GL_VERTEX_ATTRIB_ARRAY_ENABLED         0x8622
#endif
#ifndef GL_VERTEX_ATTRIB_ARRAY_SIZE
#define GL_VERTEX_ATTRIB_ARRAY_SIZE            0x8623
#endif
#ifndef GL_VERTEX_ATTRIB_ARRAY_STRIDE
#define GL_VERTEX_ATTRIB_ARRAY_STRIDE          0x8624
#endif
#ifndef GL_VERTEX_ATTRIB_ARRAY_TYPE
#define GL_VERTEX_ATTRIB_ARRAY_TYPE            0x8625
#endif
#ifndef GL_CURRENT_VERTEX_ATTRIB
#define GL_CURRENT_VERTEX_ATTRIB               0x8626
#endif
#ifndef GL_VERTEX_ATTRIB_ARRAY_POINTER
#define GL_VERTEX_ATTRIB_ARRAY_POINTER         0x8645
#endif
#ifndef GL_VERTEX_ATTRIB_ARRAY_NORMALIZED
#define GL_VERTEX_ATTRIB_ARRAY_NORMALIZED      0x886A
#endif
#ifndef GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING
#define GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING  0x889F
#endif
#ifndef GL_VERTEX_ATTRIB_ARRAY_INTEGER
#define GL_VERTEX_ATTRIB_ARRAY_INTEGER         0x88FD
#endif
#ifndef GL_VERTEX_ATTRIB_ARRAY_DIVISOR
#define GL_VERTEX_ATTRIB_ARRAY_DIVISOR         0x88FE
#endif

extern "C" {

void glGenVertexArrays(GLsizei n, GLuint* arrays) {
    MITHRIL_ENSURE_INIT();
    mithril::state_gen_names("vao", n, arrays);
    for (GLsizei i = 0; i < n; ++i) {
        mithril::VertexArray vao{};
        vao.id = arrays[i];
        g_state->vaos[arrays[i]] = vao;
    }
}

void glDeleteVertexArrays(GLsizei n, const GLuint* arrays) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !arrays) return;
    for (GLsizei i = 0; i < n; ++i) {
        GLuint name = arrays[i];
        if (name == 0) continue;
        if (g_state->currentVAO == name) g_state->currentVAO = 0;
        g_state->vaos.erase(name);
        g_state->vaoNames.release(name);
    }
}

void glBindVertexArray(GLuint array) {
    MITHRIL_ENSURE_INIT();
    if (array != 0 && !mithril::state_get_vao(array)) {
        g_state->vaos[array] = mithril::VertexArray{};
        g_state->vaos[array].id = array;
    }
    g_state->currentVAO = array;
}

void glEnableVertexAttribArray(GLuint index) {
    MITHRIL_ENSURE_INIT();
    if (index >= mithril::kMaxVertexAttribs) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO);
    if (!vao) return;
    vao->attribs[index].enabled = true;
}

void glDisableVertexAttribArray(GLuint index) {
    MITHRIL_ENSURE_INIT();
    if (index >= mithril::kMaxVertexAttribs) return;
    mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO);
    if (!vao) return;
    vao->attribs[index].enabled = false;
}

void glVertexAttribPointer(GLuint index, GLint size, GLenum type,
                           GLboolean normalized, GLsizei stride, const void* pointer) {
    MITHRIL_ENSURE_INIT();
    if (index >= mithril::kMaxVertexAttribs) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO);
    if (!vao) return;
    mithril::VertexAttrib& a = vao->attribs[index];
    a.size         = size;
    a.type         = type;
    a.normalized   = (normalized != 0);
    a.integer      = false;
    a.stride       = stride;
    a.pointer      = pointer;
    a.boundBuffer  = g_state->bufferBindings[(int)mithril::BufferTarget::Array].name;
}

void glVertexAttribIPointer(GLuint index, GLint size, GLenum type,
                            GLsizei stride, const void* pointer) {
    MITHRIL_ENSURE_INIT();
    if (index >= mithril::kMaxVertexAttribs) return;
    mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO);
    if (!vao) return;
    mithril::VertexAttrib& a = vao->attribs[index];
    a.size         = size;
    a.type         = type;
    a.normalized   = false;
    a.integer      = true;
    a.stride       = stride;
    a.pointer      = pointer;
    a.boundBuffer  = g_state->bufferBindings[(int)mithril::BufferTarget::Array].name;
}

void glVertexAttribDivisor(GLuint index, GLuint divisor) {
    MITHRIL_ENSURE_INIT();
    if (index >= mithril::kMaxVertexAttribs) return;
    mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO);
    if (!vao) return;
    /* The GL spec defines glVertexAttribDivisor(i, d) as shorthand for
     * glVertexBindingDivisor(binding of attrib i, d). The backend reads the
     * divisor off the *binding* (Pipeline.cpp attrib_divisor), so writing only
     * the attribute copy would leave instanced draws stepping per-vertex —
     * silently wrong geometry rather than a clean failure. Keep both in sync:
     * the attribute copy still backs glGetVertexAttribiv(..._DIVISOR) and the
     * MGVertexAttrib fill in Drawing.cpp. */
    vao->attribs[index].divisor = divisor;
    const GLuint bi = vao->attribs[index].bindingIndex;
    if (bi < (GLuint)mithril::kMaxVertexBindings) {
        vao->bindings[bi].divisor = divisor;
    }
}

// ---------------------------------------------------------------------------
// GL 4.3 Separate Attribute Format API (GL_ARB_vertex_attrib_binding).
//
// 深度参考 MobileGL VertexArrayState 的 GL 4.3 路径：attribute 只声明
// (format, relativeOffset)，不绑定 buffer；buffer 绑定到 binding index，
// binding 持有 (buffer, offset, stride, divisor)。Pipeline.cpp 已读
// bindings[attribs[loc].bindingIndex] 获取 stride/divisor/buffer。
//
// 经典 glVertexAttribPointer 等价于：
//   glVertexAttribFormat(loc, size, type, norm, 0);
//   glVertexAttribBinding(loc, loc);
//   glBindVertexBuffer(loc, buffer, pointer, stride);
//   glVertexAttribDivisor(loc, divisor);
//
// 这些入口让走 GL 4.3 API 的 mod（如 Iris 部分路径）能正常工作。
// ---------------------------------------------------------------------------

void glBindVertexBuffer(GLuint bindingindex, GLuint buffer, GLintptr offset, GLsizei stride) {
    MITHRIL_ENSURE_INIT();
    if (bindingindex >= (GLuint)mithril::kMaxVertexBindings) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO);
    if (!vao) return;
    mithril::VertexBinding& vb = vao->bindings[bindingindex];
    vb.buffer = buffer;
    vb.offset = offset;
    vb.stride = stride;
    // bump configVersion so pipeline re-evaluates vertex input state
    vao->configVersion++;
}

void glVertexAttribBinding(GLuint attribindex, GLuint bindingindex) {
    MITHRIL_ENSURE_INIT();
    if (attribindex >= (GLuint)mithril::kMaxVertexAttribs ||
        bindingindex >= (GLuint)mithril::kMaxVertexBindings) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO);
    if (!vao) return;
    vao->attribs[attribindex].bindingIndex = bindingindex;
    vao->attribVersions[attribindex]++;
    vao->configVersion++;
}

void glVertexAttribFormat(GLuint attribindex, GLint size, GLenum type,
                          GLboolean normalized, GLuint relativeoffset) {
    MITHRIL_ENSURE_INIT();
    if (attribindex >= (GLuint)mithril::kMaxVertexAttribs) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO);
    if (!vao) return;
    mithril::VertexAttrib& a = vao->attribs[attribindex];
    a.size = size;
    a.type = type;
    a.normalized = (normalized != 0);
    a.integer = false;
    // relativeoffset is the byte offset within the vertex buffer binding's
    // stride where this attribute's data begins. Stored in pointer field
    // (same as glVertexAttribPointer's pointer-as-offset convention).
    a.pointer = (const void*)(uintptr_t)relativeoffset;
    vao->attribVersions[attribindex]++;
    vao->configVersion++;
}

void glVertexAttribIFormat(GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset) {
    MITHRIL_ENSURE_INIT();
    if (attribindex >= (GLuint)mithril::kMaxVertexAttribs) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO);
    if (!vao) return;
    mithril::VertexAttrib& a = vao->attribs[attribindex];
    a.size = size;
    a.type = type;
    a.normalized = false;
    a.integer = true;
    a.pointer = (const void*)(uintptr_t)relativeoffset;
    vao->attribVersions[attribindex]++;
    vao->configVersion++;
}

void glVertexAttribLFormat(GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset) {
    // L-format = double-precision. Treated as I-format for now (MC doesn't use
    // double-precision vertex attributes; this satisfies symbol resolution).
    glVertexAttribIFormat(attribindex, size, type, relativeoffset);
}

void glVertexBindingDivisor(GLuint bindingindex, GLuint divisor) {
    MITHRIL_ENSURE_INIT();
    if (bindingindex >= (GLuint)mithril::kMaxVertexBindings) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO);
    if (!vao) return;
    vao->bindings[bindingindex].divisor = divisor;
    vao->configVersion++;
}

void glVertexAttrib1f(GLuint index, GLfloat x) {
    MITHRIL_ENSURE_INIT();
    (void)index; (void)x;
    // Generic vertex attributes are not used by Minecraft's modern pipeline.
}

void glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    MITHRIL_ENSURE_INIT();
    (void)index; (void)x; (void)y; (void)z; (void)w;
}

void glVertexAttrib4fv(GLuint index, const GLfloat* v) {
    MITHRIL_ENSURE_INIT();
    (void)index; (void)v;
}

void glBindAttribLocation(GLuint program, GLuint index, const GLchar* name) {
    MITHRIL_ENSURE_INIT();
    mithril::Program* p = mithril::state_get_program(program);
    if (!p || !name) return;
    // Record the name -> location mapping. It is consumed by glLinkProgram
    // when translating GLSL to SPIR-V so the generated stage_input locations
    // match the application's vertex descriptor. GL spec: bindings take
    // effect at link time, replacing any previous binding for the same name.
    p->attribBindings[name] = index;
}

void glBindFragDataLocation(GLuint program, GLuint color, const GLchar* name) {
    MITHRIL_ENSURE_INIT();
    (void)program; (void)color; (void)name;
}

GLint glGetAttribLocation(GLuint program, const GLchar* name) {
    MITHRIL_ENSURE_INIT();
    mithril::Program* p = mithril::state_get_program(program);
    if (!p || !p->linked) return -1;
    auto it = p->attribs.find(name ? name : "");
    if (it == p->attribs.end()) return -1;
    return it->second.location;
}

GLboolean glIsVertexArray(GLuint array) {
    if (!g_state) return GL_FALSE;
    return g_state->vaoNames.valid(array) ? GL_TRUE : GL_FALSE;
}

void glGetVertexAttribiv(GLuint index, GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    if (index >= mithril::kMaxVertexAttribs) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO);
    if (!vao) { *params = 0; return; }
    const mithril::VertexAttrib& a = vao->attribs[index];
    switch (pname) {
        case GL_VERTEX_ATTRIB_ARRAY_ENABLED:        *params = a.enabled ? GL_TRUE : GL_FALSE; break;
        case GL_VERTEX_ATTRIB_ARRAY_SIZE:           *params = a.size; break;
        case GL_VERTEX_ATTRIB_ARRAY_TYPE:           *params = (GLint)a.type; break;
        case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:     *params = a.normalized ? GL_TRUE : GL_FALSE; break;
        case GL_VERTEX_ATTRIB_ARRAY_STRIDE:         *params = a.stride; break;
        case GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING: *params = (GLint)a.boundBuffer; break;
        case GL_VERTEX_ATTRIB_ARRAY_DIVISOR:        *params = (GLint)a.divisor; break;
        case GL_VERTEX_ATTRIB_ARRAY_INTEGER:        *params = a.integer ? GL_TRUE : GL_FALSE; break;
        default:                                    *params = 0; break;
    }
}

void glGetVertexAttribfv(GLuint index, GLenum pname, GLfloat* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    GLint iv = 0;
    glGetVertexAttribiv(index, pname, &iv);
    *params = (GLfloat)iv;
}

void glGetVertexAttribdv(GLuint index, GLenum pname, GLdouble* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    GLint iv = 0;
    glGetVertexAttribiv(index, pname, &iv);
    *params = (GLdouble)iv;
}

void glGetVertexAttribIiv(GLuint index, GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    glGetVertexAttribiv(index, pname, params);
}

void glGetVertexAttribIuiv(GLuint index, GLenum pname, GLuint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    GLint iv = 0;
    glGetVertexAttribiv(index, pname, &iv);
    *params = (GLuint)iv;
}

void glGetVertexAttribPointerv(GLuint index, GLenum pname, void** pointer) {
    MITHRIL_ENSURE_INIT();
    if (!pointer) return;
    if (index >= mithril::kMaxVertexAttribs) {
        mithril::state_set_error(GL_INVALID_VALUE);
        *pointer = nullptr;
        return;
    }
    if (pname != GL_VERTEX_ATTRIB_ARRAY_POINTER) {
        *pointer = nullptr;
        return;
    }
    mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO);
    if (!vao) { *pointer = nullptr; return; }
    *pointer = const_cast<void*>(vao->attribs[index].pointer);
}

} // extern "C"
