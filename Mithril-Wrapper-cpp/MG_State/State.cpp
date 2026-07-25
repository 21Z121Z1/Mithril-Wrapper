// Mithril-Wrapper - MG_State/State.cpp
// Implementation of the GL state machine (Vulkan/MoltenVK rewrite).
#include "State.h"

namespace mithril {

GLState* g_state = nullptr;

bool state_init() {
    if (g_state && g_state->initialized) return true;
    if (!g_state) g_state = state_create();
    g_state->initialized = true;
    return true;
}

/*
 * Build a fresh GLState with the default VAO (name 0) and default framebuffer
 * (name 0) pre-populated. Used both by state_init() for the implicit global
 * state and by the EGL layer (egl/egl.mm) which allocates one GLState per
 * EGLContext so contexts do not share GL object tables.
 */
GLState* state_create() {
    GLState* s = new GLState{};
    s->initialized = true;

    VertexArray vao{};
    vao.id = 0;
    s->vaos[0] = vao;

    Framebuffer fbo{};
    fbo.id = 0;
    fbo.drawBuffers[0] = GL_COLOR_ATTACHMENT0;
    fbo.drawBufferCount = 1;
    fbo.readBuffer = GL_COLOR_ATTACHMENT0;
    s->framebuffers[0] = fbo;
    return s;
}

void state_destroy(GLState* s) {
    if (!s) return;
    /* EGL default color/depth VkImageViews are owned by the EGLSurface
     * (swapchain), not by the GLState, so we do not release them here. */
    s->eglDefaultColor = VK_NULL_HANDLE;
    s->eglDefaultDepth = VK_NULL_HANDLE;
    delete s;
}

VertexArray* state_get_vao(GLuint id) {
    auto it = g_state->vaos.find(id);
    return it == g_state->vaos.end() ? nullptr : &it->second;
}

Buffer* state_get_buffer(GLuint id) {
    if (id == 0) return nullptr;
    auto it = g_state->buffers.find(id);
    return it == g_state->buffers.end() ? nullptr : &it->second;
}

Texture* state_get_texture(GLuint id) {
    if (id == 0) return nullptr;
    auto it = g_state->textures.find(id);
    return it == g_state->textures.end() ? nullptr : &it->second;
}

Shader* state_get_shader(GLuint id) {
    if (id == 0) return nullptr;
    auto it = g_state->shaders.find(id);
    return it == g_state->shaders.end() ? nullptr : &it->second;
}

Program* state_get_program(GLuint id) {
    if (id == 0) return nullptr;
    auto it = g_state->programs.find(id);
    return it == g_state->programs.end() ? nullptr : &it->second;
}

Framebuffer* state_get_framebuffer(GLuint id) {
    auto it = g_state->framebuffers.find(id);
    return it == g_state->framebuffers.end() ? nullptr : &it->second;
}

void state_set_error(GLenum err) {
    if (!g_state) return;
    if (g_state->error == GL_NO_ERROR) g_state->error = err;
}

GLenum state_take_error() {
    if (!g_state) return GL_NO_ERROR;
    GLenum e = g_state->error;
    g_state->error = GL_NO_ERROR;
    return e;
}

void state_gen_names(const char* /*kind*/, GLsizei n, GLuint* out) {
    if (!g_state || n <= 0 || !out) {
        if (out && n > 0) {
            for (GLsizei i = 0; i < n; ++i) out[i] = 0;
        }
        return;
    }
    for (GLsizei i = 0; i < n; ++i) {
        out[i] = g_state->nextName++;
    }
}

} // namespace mithril
