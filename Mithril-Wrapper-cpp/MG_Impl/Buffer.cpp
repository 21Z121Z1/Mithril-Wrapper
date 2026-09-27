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
#include <execinfo.h>

/* GL buffer parameter / query constants not always present in the minimal
 * glcorearb.h we ship. Standard GL 3.3 Core values. */
#ifndef GL_BUFFER_MAPPED
#define GL_BUFFER_MAPPED                0x88BC
#endif
#ifndef GL_BUFFER_MAP_OFFSET
#define GL_BUFFER_MAP_OFFSET            0x9121
#endif
#ifndef GL_BUFFER_MAP_LENGTH
#define GL_BUFFER_MAP_LENGTH            0x9120
#endif

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
        // Unbind from all non-indexed buffer binding slots.
        for (int s = 0; s < mithril::kBufferTargetCount; ++s) {
            if (g_state->bufferBindings[s].name == name) g_state->bufferBindings[s].bind(0);
        }
        // Unbind from all indexed buffer binding slots (UBO/SSBO/TF/AtomicCounter).
        for (int c = 0; c < mithril::kIndexedBufferCategoryCount; ++c) {
            for (int s = 0; s < mithril::kMaxIndexedBindings; ++s) {
                if (g_state->indexedBufferBindings[c][s].name == name) {
                    g_state->indexedBufferBindings[c][s].bind(0);
                }
            }
        }
        // ELEMENT_ARRAY_BUFFER lives in the current VAO — clear it if matched.
        if (mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO)) {
            if (vao->elementArrayBuffer == name) vao->elementArrayBuffer = 0;
        }
        // P2-7: cached GL_ARRAY_BUFFER-attrib-bind-time references live in every VAO.
        for (auto& kv : g_state->vaos) {
            for (int a = 0; a < mithril::kMaxVertexAttribs; ++a) {
                if (kv.second.attribs[a].boundBuffer == name) {
                    kv.second.attribs[a].boundBuffer = 0;
                }
            }
        }
        backend_delete_buffer(name);
        g_state->buffers.erase(name);
        g_state->bufferNames.release(name);
    }
}

static mithril::Buffer* bound_buffer_for_target(GLenum target) {
    // ELEMENT_ARRAY_BUFFER lives only in the current VAO (no global slot).
    if (target == GL_ELEMENT_ARRAY_BUFFER) {
        mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO);
        if (!vao) return nullptr;
        return mithril::state_get_buffer(vao->elementArrayBuffer);
    }
    mithril::BufferTarget t = mithril::bufferTargetFromGL(target);
    if (t == mithril::BufferTarget::Count) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return nullptr;
    }
    GLuint name = g_state->bufferBindings[(int)t].name;
    mithril::Buffer* b = mithril::state_get_buffer(name);
    if (!b && name != 0) {
        // The name was reserved by glGen* but not yet inserted into the table.
        g_state->buffers[name] = mithril::Buffer{};
        b = mithril::state_get_buffer(name);
        b->id = name;
    }
    return b;
}

void glBindBuffer(GLenum target, GLuint buffer) {
    MITHRIL_ENSURE_INIT();
    if (buffer != 0 && !mithril::state_get_buffer(buffer)) {
        g_state->buffers[buffer] = mithril::Buffer{};
        g_state->buffers[buffer].id = buffer;
    }
    if (target == 0x88EC /*GL_PIXEL_UNPACK_BUFFER*/) {
        fprintf(stderr,"[PUBIND] buf=%u\n",buffer);
    }
    if (target == GL_ELEMENT_ARRAY_BUFFER) {
        // ELEMENT_ARRAY_BUFFER is stored on the current VAO, never globally.
        mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO);
        if (vao) vao->elementArrayBuffer = buffer;
    } else {
        mithril::BufferTarget t = mithril::bufferTargetFromGL(target);
        if (t == mithril::BufferTarget::Count) {
            mithril::state_set_error(GL_INVALID_ENUM);
            return;
        }
        g_state->bufferBindings[(int)t].bind(buffer);
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
    b->immutable = false;  // glBufferData resets immutable flag
    b->storageFlags = 0;
    b->data.assign((size_t)size, 0);
    if (data && size > 0) std::memcpy(b->data.data(), data, (size_t)size);
    b->mapped = nullptr;
    if (b->id==33 && getenv("MITHRIL_B33")) fprintf(stderr,"[B33] BufferData size=%lld data=%p firstNZ=%d\n",(long long)size,(void*)data,(data?((const unsigned char*)data)[0]:-1));
    // Recreate the VkBuffer (allocates + uploads).
    backend_get_or_create_buffer(b->id, data && size ? b->data.data() : nullptr, (size_t)size);
}

// GL 4.4 ARB_buffer_storage — immutable storage with persistent mapping.
// Sodium uses this for chunk mesh upload ring buffers (GL_MAP_PERSISTENT_BIT |
// GL_MAP_WRITE_BIT | GL_MAP_COHERENT_BIT). The backend create_buffer already
// supports persistent mapping via the `persistent` parameter.
void glBufferStorage(GLenum target, GLsizeiptr size, const void* data, GLbitfield flags) {
    MITHRIL_ENSURE_INIT();
    if (size < 0) { mithril::state_set_error(GL_INVALID_VALUE); return; }
    mithril::Buffer* b = bound_buffer_for_target(target);
    if (!b) { mithril::state_set_error(GL_INVALID_OPERATION); return; }
    if (b->immutable) {
        // GL_BUFFER_IMMUTABLE_STORAGE already set — glBufferStorage on immutable
        // buffer is an error (GL_INVALID_OPERATION).
        mithril::state_set_error(GL_INVALID_OPERATION);
        return;
    }
    b->size = size;
    b->usage = GL_STATIC_DRAW;  // not used for immutable buffers
    b->immutable = true;
    b->storageFlags = flags;
    b->data.assign((size_t)size, 0);
    if (data && size > 0) std::memcpy(b->data.data(), data, (size_t)size);

    // If GL_MAP_PERSISTENT_BIT is set, create a persistently-mapped VkBuffer.
    // The backend create_buffer (Resources.cpp) handles vkMapMemory + persistent
    // mapping when persistent=true and data != NULL or data == NULL with persistent.
    bool persistent = (flags & GL_MAP_PERSISTENT_BIT) != 0;
    bool coherent = (flags & GL_MAP_COHERENT_BIT) != 0;
    if (b->id==42) {
        void* bt[24]; int n=backtrace(bt,24); char** sym=backtrace_symbols(bt,n);
        fprintf(stderr,"[CREATE42] size=%lld flags=0x%x persistent=%d\n",(long long)size,(unsigned)flags,(int)persistent);
        for(int i=0;i<n;++i) fprintf(stderr,"  %s\n",sym[i]);
        free(sym);
    }
    if (getenv("MITHRIL_B33")) fprintf(stderr,"[B33] Storage id=%u size=%lld flags=0x%x persistent=%d coherent=%d\n",b->id,(long long)size,(unsigned)flags,persistent,coherent);
    if (persistent) {
        // Use backend_create_buffer_storage for persistent mapping (GL 4.4 path).
        // Upload initial data via glBufferSubData after creation.
        backend_create_buffer_storage(b->id, (VkDeviceSize)size, 0, persistent, coherent);
        if (data && size > 0) {
            backend_buffer_upload(b->id, 0, data, (size_t)size);
        }
    } else {
        // Non-persistent: use regular glBufferData path
        backend_get_or_create_buffer(b->id, data && size ? b->data.data() : nullptr, (size_t)size);
    }
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
    if (b->id==33 && getenv("MITHRIL_B33")) fprintf(stderr,"[B33] SubData off=%lld size=%lld first=(%f,%f,%f)\n",(long long)offset,(long long)size,((const float*)data)[0],((const float*)data)[1],((const float*)data)[2]);
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
    // FIX (root cause: glCopyBufferSubData vs persistent source): the source may
    // be a persistently-mapped buffer the app wrote through the LIVE backend
    // pointer (not the CPU shadow). Refresh the source shadow from the live
    // mapped range first (cheap host read, no GPU stall), otherwise a CPU copy
    // would propagate stale zeros.
    void* srcLive = backend_get_buffer_mapped_pointer(src->id);
    if (srcLive) {
        std::memcpy(src->data.data() + readOffset,
                    static_cast<uint8_t*>(srcLive) + readOffset, (size_t)size);
    }
    std::memmove(dst->data.data() + writeOffset, src->data.data() + readOffset, (size_t)size);
    // glCopyBufferSubData is a SERVER-side copy: issue a GPU vkCmdCopyBuffer
    // (ordered with barriers) rather than an in-place host upload, so the data
    // moves even when both buffers are in flight. Fall back to a host upload
    // only if the GPU copy could not be recorded.
    if (!backend_copy_buffer_gpu(src->id, dst->id, (VkDeviceSize)readOffset,
                                 (VkDeviceSize)writeOffset, (VkDeviceSize)size)) {
        backend_buffer_upload(dst->id, writeOffset, dst->data.data() + writeOffset, (size_t)size);
    }
    if (getenv("MITHRIL_B33") && dst->id==33){
      float df2[3]={0}; if(size>=12)memcpy(df2,dst->data.data()+writeOffset,12);
      fprintf(stderr,"[B33] CopyBuffer src=%u->dst=%u size=%lld dstShadowFirst=(%.3f,%.3f,%.3f)\n",src->id,dst->id,(long long)size,df2[0],df2[1],df2[2]);
    }
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
    // Persistent storage (glBufferStorage + GL_MAP_PERSISTENT_BIT): return the
    // backend's LIVE vkMapMemory pointer, not the CPU shadow (b->data). Apps
    // using GL_MAP_COHERENT_BIT do NOT call glFlushMappedBufferRange, so a
    // shadow pointer would never reach GPU-visible memory (matrices stayed
    // zero -> all vertex transforms clipped -> black frame).
    void* live = backend_get_buffer_mapped_pointer(b->id);
    if (live) {
        b->mapped = static_cast<uint8_t*>(live) + offset;
    } else {
        b->mapped = b->data.data() + offset;
    }
    if (b->id==33 && getenv("MITHRIL_B33")) fprintf(stderr,"[B33] MapRange off=%lld len=%lld access=0x%x live=%p ptr=%p\n",(long long)offset,(long long)length,(unsigned)access,live,b->mapped);
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
        case GL_BUFFER_SIZE:        *params = (GLint)b->size;  break;
        case GL_BUFFER_USAGE:       *params = (GLint)b->usage; break;
        case GL_BUFFER_ACCESS:      *params = (GLint)b->mapAccess; break;
        case GL_BUFFER_MAPPED:      *params = (b->mapped != nullptr) ? GL_TRUE : GL_FALSE; break;
        case GL_BUFFER_MAP_OFFSET:  *params = (GLint)b->mapOffset; break;
        case GL_BUFFER_MAP_LENGTH:  *params = (GLint)b->mapLength; break;
        default:                    *params = 0; break;
    }
}

void glGetBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, void* data) {
    MITHRIL_ENSURE_INIT();
    if (!data || size <= 0) return;
    mithril::Buffer* b = bound_buffer_for_target(target);
    if (!b) return;
    if (offset < 0 || offset + size > b->size) return;
    void* live = backend_get_buffer_mapped_pointer(b->id);
    if (live) std::memcpy(data, static_cast<uint8_t*>(live) + offset, (size_t)size);
    else       std::memcpy(data, b->data.data() + offset, (size_t)size);
}

void glBindBufferBase(GLenum target, GLuint index, GLuint buffer) {
    MITHRIL_ENSURE_INIT();
    mithril::IndexedBufferTarget cat = mithril::indexedBufferTargetFromGL(target);
    if (cat == mithril::IndexedBufferTarget::Count) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    if (index >= mithril::kMaxIndexedBindings) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    if (buffer != 0 && !mithril::state_get_buffer(buffer)) {
        g_state->buffers[buffer] = mithril::Buffer{};
        g_state->buffers[buffer].id = buffer;
    }
    g_state->indexedBufferBindings[(int)cat][index].bind(buffer);
    if (std::getenv("MITHRIL_UBO_TRACE") && target==GL_UNIFORM_BUFFER)
        fprintf(stderr,"UBO-BASE point=%u name=%u\n",index,buffer);
    if ((int)index + 1 > g_state->touchedIndexed[(int)cat]) {
        g_state->touchedIndexed[(int)cat] = (int)index + 1;
    }
    // Per GL spec, glBindBufferBase also binds to the generic (non-indexed) target.
    mithril::BufferTarget bt = mithril::bufferTargetFromGL(target);
    if (bt != mithril::BufferTarget::Count) {
        g_state->bufferBindings[(int)bt].bind(buffer);
    }
    if (mithril::Buffer* b = mithril::state_get_buffer(buffer)) {
        b->lastTarget = target;
    }
}

void glBindBufferRange(GLenum target, GLuint index, GLuint buffer,
                       GLintptr offset, GLsizeiptr size) {
    MITHRIL_ENSURE_INIT();
    mithril::IndexedBufferTarget cat = mithril::indexedBufferTargetFromGL(target);
    if (cat == mithril::IndexedBufferTarget::Count) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    if (index >= mithril::kMaxIndexedBindings) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    if (size <= 0) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    // Offset alignment must match what the driver reports via
    // GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT / GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT
    // (16 on MoltenVK/Metal). A hardcoded 256 wrongly rejected the 16-aligned
    // offsets the host legitimately computes from the reported alignment; the
    // dropped bind left every UBO unbound -> zero recorded draws / black frame.
    GLintptr needAlign = 16;
    if (target == GL_UNIFORM_BUFFER) {
        needAlign = backend_device_limit(MITHRIL_LIMIT_UNIFORM_BUFFER_ALIGNMENT, 256);
    }
    if ((target == GL_UNIFORM_BUFFER || target == GL_SHADER_STORAGE_BUFFER) &&
        needAlign > 0 && (offset % needAlign) != 0) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    if (buffer != 0 && !mithril::state_get_buffer(buffer)) {
        g_state->buffers[buffer] = mithril::Buffer{};
        g_state->buffers[buffer].id = buffer;
    }
    g_state->indexedBufferBindings[(int)cat][index].bindRange(buffer, offset, size);
    if (std::getenv("MITHRIL_UBO_TRACE") && target==GL_UNIFORM_BUFFER)
        fprintf(stderr,"UBO-RANGE point=%u name=%u off=%ld size=%ld\n",index,buffer,(long)offset,(long)size);
    if ((int)index + 1 > g_state->touchedIndexed[(int)cat]) {
        g_state->touchedIndexed[(int)cat] = (int)index + 1;
    }
    // Per GL spec, glBindBufferRange also binds to the generic (non-indexed) target.
    mithril::BufferTarget bt = mithril::bufferTargetFromGL(target);
    if (bt != mithril::BufferTarget::Count) {
        g_state->bufferBindings[(int)bt].bind(buffer);
    }
    if (mithril::Buffer* b = mithril::state_get_buffer(buffer)) {
        b->lastTarget = target;
    }
}

GLboolean glIsBuffer(GLuint buffer) {
    if (!g_state) return GL_FALSE;
    return g_state->bufferNames.valid(buffer) ? GL_TRUE : GL_FALSE;
}

} // extern "C"
