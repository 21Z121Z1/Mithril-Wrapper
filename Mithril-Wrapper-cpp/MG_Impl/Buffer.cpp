// Mithril-Wrapper - MG_Impl/Buffer.cpp
// Buffer object (VBO/IBO/UBO) management. CPU-side shadow storage in
// mithril::Buffer::data plus a paired VkBuffer via backend_get_or_create_buffer.
//
// This is the Vulkan/MoltenVK rewrite of the former gl/buffer.cpp. The Metal
// MTLBuffer calls (metal_get_or_create_buffer / metal_buffer_upload /
// metal_get_buffer / metal_delete_buffer) are replaced with the Vulkan backend
// C API (backend_get_or_create_buffer / backend_buffer_upload / backend_get_buffer
// / backend_delete_buffer) declared in MG_Backend/Backend.h.
#include "includes.h"

extern "C" {

void glGenBuffers(GLsizei n, GLuint* buffers) {
    MITHRIL_ENSURE_INIT();
    mithril::state_gen_names("buffer", n, buffers);
    for (GLsizei i = 0; i < n; ++i) {
        mithril::Buffer b{};
        b.id = buffers[i];
        g_state->buffers[buffers[i]] = b;
    }
}

void glDeleteBuffers(GLsizei n, const GLuint* buffers) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !buffers) return;
    for (GLsizei i = 0; i < n; ++i) {
        GLuint name = buffers[i];
        if (name == 0) continue;
        if (g_state->currentArrayBuffer == name)   g_state->currentArrayBuffer = 0;
        if (g_state->currentIndexBuffer == name)   g_state->currentIndexBuffer = 0;
        if (g_state->currentUniformBuffer == name) g_state->currentUniformBuffer = 0;
        backend_delete_buffer(name);
        g_state->buffers.erase(name);
    }
}

static mithril::Buffer* bound_buffer_for_target(GLenum target) {
    GLuint* slot = nullptr;
    switch (target) {
        case GL_ARRAY_BUFFER:         slot = &g_state->currentArrayBuffer; break;
        case GL_ELEMENT_ARRAY_BUFFER: {
            mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO);
            if (!vao) return nullptr;
            return mithril::state_get_buffer(vao->elementArrayBuffer);
        }
        case GL_UNIFORM_BUFFER:         slot = &g_state->currentUniformBuffer; break;
        case GL_PIXEL_PACK_BUFFER:
        case GL_PIXEL_UNPACK_BUFFER:
        case GL_COPY_READ_BUFFER:
        case GL_COPY_WRITE_BUFFER:
        case GL_TRANSFORM_FEEDBACK_BUFFER:
        case GL_SHADER_STORAGE_BUFFER:
        case GL_ATOMIC_COUNTER_BUFFER:
        case GL_DRAW_INDIRECT_BUFFER:
            slot = &g_state->currentArrayBuffer; break;
        default:
            mithril::state_set_error(GL_INVALID_ENUM);
            return nullptr;
    }
    if (!slot) return nullptr;
    mithril::Buffer* b = mithril::state_get_buffer(*slot);
    if (!b && *slot != 0) {
        // The name was reserved by glGen* but not yet inserted into the table.
        g_state->buffers[*slot] = mithril::Buffer{};
        b = mithril::state_get_buffer(*slot);
        b->id = *slot;
    }
    return b;
}

void glBindBuffer(GLenum target, GLuint buffer) {
    MITHRIL_ENSURE_INIT();
    if (buffer != 0 && !mithril::state_get_buffer(buffer)) {
        g_state->buffers[buffer] = mithril::Buffer{};
        g_state->buffers[buffer].id = buffer;
    }
    switch (target) {
        case GL_ARRAY_BUFFER:         g_state->currentArrayBuffer = buffer; break;
        case GL_UNIFORM_BUFFER:       g_state->currentUniformBuffer = buffer; break;
        case GL_ELEMENT_ARRAY_BUFFER: {
            mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO);
            if (vao) vao->elementArrayBuffer = buffer;
            g_state->currentIndexBuffer = buffer;
            break;
        }
        default:
            // Other targets bind to the array slot for simplicity.
            g_state->currentArrayBuffer = buffer;
            break;
    }
    if (mithril::Buffer* b = mithril::state_get_buffer(buffer)) {
        b->lastTarget = target;
    }
}

void glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
    MITHRIL_ENSURE_INIT();
    if (size < 0) { mithril::state_set_error(GL_INVALID_VALUE); return; }
    mithril::Buffer* b = bound_buffer_for_target(target);
    if (!b) { mithril::state_set_error(GL_INVALID_OPERATION); return; }
    b->size  = size;
    b->usage = usage;
    b->data.assign((size_t)size, 0);
    if (data && size > 0) std::memcpy(b->data.data(), data, (size_t)size);
    b->mapped = nullptr;
    // Recreate the VkBuffer (allocates + uploads).
    backend_get_or_create_buffer(b->id, data && size ? b->data.data() : nullptr, (size_t)size);
}

void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data) {
    MITHRIL_ENSURE_INIT();
    if (!data || size <= 0) return;
    mithril::Buffer* b = bound_buffer_for_target(target);
    if (!b) { mithril::state_set_error(GL_INVALID_OPERATION); return; }
    if (offset < 0 || offset + size > b->size) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    std::memcpy(b->data.data() + offset, data, (size_t)size);
    backend_buffer_upload(b->id, offset, data, (size_t)size);
}

void glCopyBufferSubData(GLenum readTarget, GLenum writeTarget,
                         GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size) {
    MITHRIL_ENSURE_INIT();
    if (size <= 0) return;
    mithril::Buffer* src = bound_buffer_for_target(readTarget);
    mithril::Buffer* dst = bound_buffer_for_target(writeTarget);
    if (!src || !dst) { mithril::state_set_error(GL_INVALID_OPERATION); return; }
    if (readOffset < 0 || writeOffset < 0 ||
        readOffset + size > src->size || writeOffset + size > dst->size) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    std::memmove(dst->data.data() + writeOffset, src->data.data() + readOffset, (size_t)size);
    backend_buffer_upload(dst->id, writeOffset, dst->data.data() + writeOffset, (size_t)size);
}

void* glMapBuffer(GLenum target, GLenum access) {
    MITHRIL_ENSURE_INIT();
    mithril::Buffer* b = bound_buffer_for_target(target);
    if (!b) { mithril::state_set_error(GL_INVALID_OPERATION); return nullptr; }
    b->mapAccess  = access;
    b->mapOffset  = 0;
    b->mapLength  = b->size;
    b->mapped     = b->data.data();
    return b->mapped;
}

void* glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access) {
    MITHRIL_ENSURE_INIT();
    mithril::Buffer* b = bound_buffer_for_target(target);
    if (!b) { mithril::state_set_error(GL_INVALID_OPERATION); return nullptr; }
    if (offset < 0 || length <= 0 || offset + length > b->size) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return nullptr;
    }
    if (access & GL_MAP_INVALIDATE_BUFFER_BIT) {
        std::memset(b->data.data(), 0, (size_t)b->size);
    }
    b->mapAccess  = access;
    b->mapOffset  = offset;
    b->mapLength  = length;
    b->mapped     = b->data.data() + offset;
    return b->mapped;
}

GLboolean glUnmapBuffer(GLenum target) {
    MITHRIL_ENSURE_INIT();
    mithril::Buffer* b = bound_buffer_for_target(target);
    if (!b || !b->mapped) return GL_FALSE;
    // Upload the (possibly) modified range to the VkBuffer.
    backend_buffer_upload(b->id, b->mapOffset, b->mapped, (size_t)b->mapLength);
    b->mapped = nullptr;
    return GL_TRUE;
}

void glFlushMappedBufferRange(GLenum target, GLintptr offset, GLsizeiptr length) {
    MITHRIL_ENSURE_INIT();
    mithril::Buffer* b = bound_buffer_for_target(target);
    if (!b || !b->mapped) return;
    GLintptr base = b->mapOffset + offset;
    if (base < 0 || length <= 0 || base + length > b->size) return;
    backend_buffer_upload(b->id, base, (uint8_t*)b->mapped + offset, (size_t)length);
}

void glGetBufferParameteriv(GLenum target, GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    mithril::Buffer* b = bound_buffer_for_target(target);
    if (!b) { *params = 0; return; }
    switch (pname) {
        case GL_BUFFER_SIZE:  *params = (GLint)b->size;  break;
        case GL_BUFFER_USAGE: *params = (GLint)b->usage; break;
        case GL_BUFFER_ACCESS:*params = (GLint)b->mapAccess; break;
        default:              *params = 0; break;
    }
}

void glGetBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, void* data) {
    MITHRIL_ENSURE_INIT();
    if (!data || size <= 0) return;
    mithril::Buffer* b = bound_buffer_for_target(target);
    if (!b) return;
    if (offset < 0 || offset + size > b->size) return;
    std::memcpy(data, b->data.data() + offset, (size_t)size);
}

void glBindBufferBase(GLenum target, GLuint index, GLuint buffer) {
    MITHRIL_ENSURE_INIT();
    (void)index;
    glBindBuffer(target, buffer);
}

void glBindBufferRange(GLenum target, GLuint index, GLuint buffer,
                       GLintptr offset, GLsizeiptr size) {
    MITHRIL_ENSURE_INIT();
    (void)index; (void)offset; (void)size;
    glBindBuffer(target, buffer);
}

} // extern "C"
