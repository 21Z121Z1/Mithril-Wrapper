// Mithril-Wrapper - MG_Impl/VertexArray.cpp
// Vertex Array Objects and vertex attribute pointer state.
//
// This is the Vulkan/MoltenVK rewrite of the former gl/vertexattrib.cpp. The
// VAO/attribute state machine is backend-agnostic (it lives entirely in
// mithril::GLState); the only backend-specific touchpoint is that the drawing
// path (Drawing.cpp) reads these attribs to build the VkPipelineVertexInputState.
#include "includes.h"

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
    }
}

void glBindVertexArray(GLuint array) {
    MITHRIL_ENSURE_INIT();
    if (array != 0 && !mithril::state_get_vao(array)) {
        g_state->vaos[array] = mithril::VertexArray{};
        g_state->vaos[array].id = array;
    }
    g_state->currentVAO = array;
    mithril::VertexArray* vao = mithril::state_get_vao(array);
    if (vao) {
        // The element array binding follows the VAO.
        g_state->currentIndexBuffer = vao->elementArrayBuffer;
    }
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

// Bytes per element for a GL vertex attrib type. Used to compute the tight
// (default) stride when the app passes stride==0, which in GL means "tightly
// packed" (size * sizeof(type)) — NOT Vulkan's stride==0 meaning "all vertices
// share one element". Returning the tight stride here prevents Vulkan from
// collapsing every vertex onto vertex 0.
static int attrib_element_bytes(GLenum type) {
    switch (type) {
        case GL_BYTE:
        case GL_UNSIGNED_BYTE:           return 1;
        case GL_SHORT:
        case GL_UNSIGNED_SHORT:
        case GL_HALF_FLOAT:              return 2;
        case GL_INT:
        case GL_UNSIGNED_INT:
        case GL_FLOAT:
        case GL_FIXED:
        case GL_INT_2_10_10_10_REV:
        case GL_UNSIGNED_INT_2_10_10_10_REV:
        case GL_UNSIGNED_INT_10F_11F_11F_REV: return 4;
        case GL_DOUBLE:                  return 8;
        default:                         return 4; // safe fallback
    }
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
    // GL stride==0 means "tightly packed" (size * sizeof(type)); Vulkan
    // stride==0 means "all vertices share one element". Convert stride==0
    // to the GL tight stride so Vulkan receives correct per-vertex data.
    a.stride       = (stride > 0) ? stride : (size * attrib_element_bytes(type));
    a.pointer      = pointer;
    a.boundBuffer  = g_state->currentArrayBuffer;
    a.divisor      = a.divisor; // preserve
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
    a.stride       = (stride > 0) ? stride : (size * attrib_element_bytes(type));
    a.pointer      = pointer;
    a.boundBuffer  = g_state->currentArrayBuffer;
}

void glVertexAttribDivisor(GLuint index, GLuint divisor) {
    MITHRIL_ENSURE_INIT();
    if (index >= mithril::kMaxVertexAttribs) return;
    mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO);
    if (!vao) return;
    vao->attribs[index].divisor = divisor;
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

} // extern "C"
