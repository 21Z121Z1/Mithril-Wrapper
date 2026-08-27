#!/usr/bin/env python3
from pathlib import Path

p = Path('Mithril-Wrapper-cpp/MG_Impl/Buffer.cpp')
s = p.read_text()
old = r'''void* glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access) {
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
'''
new = r'''void* glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access) {
    MITHRIL_ENSURE_INIT();
    mithril::Buffer* b = bound_buffer_for_target(target);
    if (!b) { mithril::state_set_error(GL_INVALID_OPERATION); return nullptr; }
    if (offset < 0 || length <= 0 || offset + length > b->size) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return nullptr;
    }

    // GL_ARB_buffer_storage persistent maps must alias the storage the GPU
    // actually consumes. DirectVulkan's backend_create_buffer_storage keeps
    // HOST_VISIBLE|HOST_COHERENT VkDeviceMemory permanently mapped; returning
    // Buffer::data here creates two independent byte arrays: Minecraft writes
    // the CPU shadow while vkCmdDraw reads the untouched VkBuffer allocation.
    // Prefer the backend mapping for persistent storage. DirectMetal may return
    // nullptr here, in which case the existing CPU-shadow path is preserved.
    uint8_t* backendBase = nullptr;
    if (b->storageFlags & GL_MAP_PERSISTENT_BIT) {
        backendBase = static_cast<uint8_t*>(backend_get_buffer_mapped_pointer(b->id));
    }
    if (access & GL_MAP_INVALIDATE_BUFFER_BIT) {
        std::memset(b->data.data(), 0, (size_t)b->size);
        if (backendBase) std::memset(backendBase, 0, (size_t)b->size);
    }
    b->mapAccess  = access;
    b->mapOffset  = offset;
    b->mapLength  = length;
    b->mapped     = backendBase ? (backendBase + offset) : (b->data.data() + offset);
    return b->mapped;
}

GLboolean glUnmapBuffer(GLenum target) {
    MITHRIL_ENSURE_INIT();
    mithril::Buffer* b = bound_buffer_for_target(target);
    if (!b || !b->mapped) return GL_FALSE;

    // A direct persistent mapping already aliases HOST_COHERENT backend memory;
    // uploading it back would be redundant and can turn into a self-memcpy in
    // DirectVulkan. Shadow-backed mappings still need the ordinary upload.
    uint8_t* backendBase = static_cast<uint8_t*>(backend_get_buffer_mapped_pointer(b->id));
    const bool directPersistent = backendBase &&
        b->mapped == backendBase + b->mapOffset;
    if (!directPersistent) {
        backend_buffer_upload(b->id, b->mapOffset, b->mapped, (size_t)b->mapLength);
    }
    b->mapped = nullptr;
    return GL_TRUE;
}

void glFlushMappedBufferRange(GLenum target, GLintptr offset, GLsizeiptr length) {
    MITHRIL_ENSURE_INIT();
    mithril::Buffer* b = bound_buffer_for_target(target);
    if (!b || !b->mapped) return;
    GLintptr base = b->mapOffset + offset;
    if (base < 0 || length <= 0 || base + length > b->size) return;

    uint8_t* backendBase = static_cast<uint8_t*>(backend_get_buffer_mapped_pointer(b->id));
    const bool directPersistent = backendBase &&
        b->mapped == backendBase + b->mapOffset;
    if (!directPersistent) {
        backend_buffer_upload(b->id, base, (uint8_t*)b->mapped + offset, (size_t)length);
    }
}
'''
assert s.count(old) == 1, s.count(old)
s = s.replace(old, new, 1)
p.write_text(s)
