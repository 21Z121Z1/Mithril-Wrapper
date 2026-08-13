from pathlib import Path


def one(path, old, new):
    p = Path(path)
    s = p.read_text()
    n = s.count(old)
    if n != 1:
        raise SystemExit(f"{path}: expected one match, got {n}: {old[:100]!r}")
    p.write_text(s.replace(old, new, 1))


one(
    "Mithril-Wrapper-cpp/MG_State/State.h",
    "    void*        mapped = nullptr;\n    GLbitfield   mapAccess = 0;\n",
    "    void*        mapped = nullptr;\n"
    "    // True when mapped aliases the persistent Vulkan host mapping rather\n"
    "    // than the CPU shadow vector. Direct maps must not be re-uploaded.\n"
    "    bool         mappedDirect = false;\n"
    "    GLbitfield   mapAccess = 0;\n",
)

p = Path("Mithril-Wrapper-cpp/MG_Impl/Buffer.cpp")
s = p.read_text()
s = s.replace(
    "    b->mapped = nullptr;\n    // Recreate the VkBuffer",
    "    b->mapped = nullptr;\n    b->mappedDirect = false;\n    // Recreate the VkBuffer",
    1,
)
marker = "    b->storageFlags = flags;\n    b->data.assign((size_t)size, 0);\n"
if s.count(marker) != 1:
    raise SystemExit("Buffer.cpp storage marker mismatch")
s = s.replace(
    marker,
    "    b->storageFlags = flags;\n"
    "    b->mapped = nullptr;\n"
    "    b->mappedDirect = false;\n"
    "    b->data.assign((size_t)size, 0);\n",
    1,
)
old = (
    "    std::memmove(dst->data.data() + writeOffset, src->data.data() + readOffset, (size_t)size);\n"
    "    backend_buffer_upload(dst->id, writeOffset, dst->data.data() + writeOffset, (size_t)size);\n"
)
new = (
    "    const uint8_t* srcBytes = src->data.data();\n"
    "    if (void* directSrc = backend_get_buffer_mapped_pointer(src->id)) {\n"
    "        srcBytes = static_cast<const uint8_t*>(directSrc);\n"
    "    }\n"
    "    std::memmove(dst->data.data() + writeOffset, srcBytes + readOffset, (size_t)size);\n"
    "    backend_buffer_upload(dst->id, writeOffset, dst->data.data() + writeOffset, (size_t)size);\n"
)
if s.count(old) != 1:
    raise SystemExit("Buffer.cpp copy block mismatch")
s = s.replace(old, new, 1)

start = s.index("void* glMapBuffer(GLenum target, GLenum access) {")
end = s.index("\nvoid glGetBufferParameteriv(", start)
mapping = r'''void* glMapBuffer(GLenum target, GLenum access) {
    MITHRIL_ENSURE_INIT();
    mithril::Buffer* b = bound_buffer_for_target(target);
    if (!b) { mithril::state_set_error(GL_INVALID_OPERATION); return nullptr; }
    if (b->mapped) { mithril::state_set_error(GL_INVALID_OPERATION); return nullptr; }
    b->mapAccess = access;
    b->mapOffset = 0;
    b->mapLength = b->size;
    if (void* direct = backend_get_buffer_mapped_pointer(b->id)) {
        b->mapped = direct;
        b->mappedDirect = true;
    } else {
        b->mapped = b->data.data();
        b->mappedDirect = false;
    }
    return b->mapped;
}

void* glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access) {
    MITHRIL_ENSURE_INIT();
    mithril::Buffer* b = bound_buffer_for_target(target);
    if (!b) { mithril::state_set_error(GL_INVALID_OPERATION); return nullptr; }
    if (b->mapped) { mithril::state_set_error(GL_INVALID_OPERATION); return nullptr; }
    if (offset < 0 || length <= 0 || offset + length > b->size) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return nullptr;
    }
    b->mapAccess = access;
    b->mapOffset = offset;
    b->mapLength = length;
    if (void* direct = backend_get_buffer_mapped_pointer(b->id)) {
        // INVALIDATE is a reuse hint, not a zero-fill guarantee. Do not clear
        // live persistently-mapped VkDeviceMemory.
        b->mapped = static_cast<uint8_t*>(direct) + offset;
        b->mappedDirect = true;
    } else {
        if (access & GL_MAP_INVALIDATE_BUFFER_BIT) {
            std::memset(b->data.data(), 0, (size_t)b->size);
        }
        b->mapped = b->data.data() + offset;
        b->mappedDirect = false;
    }
    return b->mapped;
}

GLboolean glUnmapBuffer(GLenum target) {
    MITHRIL_ENSURE_INIT();
    mithril::Buffer* b = bound_buffer_for_target(target);
    if (!b || !b->mapped) return GL_FALSE;
    if (!b->mappedDirect) {
        backend_buffer_upload(b->id, b->mapOffset, b->mapped, (size_t)b->mapLength);
    }
    b->mapped = nullptr;
    b->mappedDirect = false;
    return GL_TRUE;
}

void glFlushMappedBufferRange(GLenum target, GLintptr offset, GLsizeiptr length) {
    MITHRIL_ENSURE_INIT();
    mithril::Buffer* b = bound_buffer_for_target(target);
    if (!b || !b->mapped) return;
    GLintptr base = b->mapOffset + offset;
    if (offset < 0 || base < 0 || length <= 0 || offset + length > b->mapLength ||
        base + length > b->size) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    if (b->mappedDirect) {
        // Persistent GL storage currently uses HOST_COHERENT Vulkan memory.
        return;
    }
    backend_buffer_upload(b->id, base, static_cast<uint8_t*>(b->mapped) + offset,
                          (size_t)length);
}
'''
s = s[:start] + mapping + s[end:]
old = (
    "    if (offset < 0 || offset + size > b->size) return;\n"
    "    std::memcpy(data, b->data.data() + offset, (size_t)size);\n"
)
new = (
    "    if (offset < 0 || offset + size > b->size) return;\n"
    "    const uint8_t* src = b->data.data();\n"
    "    if (void* direct = backend_get_buffer_mapped_pointer(b->id)) {\n"
    "        src = static_cast<const uint8_t*>(direct);\n"
    "    }\n"
    "    std::memcpy(data, src + offset, (size_t)size);\n"
)
if s.count(old) != 1:
    raise SystemExit("Buffer.cpp GetBufferSubData mismatch")
p.write_text(s.replace(old, new, 1))

p = Path("Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/Resources.cpp")
s = p.read_text()
old = (
    "                void* dst = nullptr;\n"
    "                vkMapMemory(b->device, it->second.memory, (VkDeviceSize)offset,\n"
    "                            (VkDeviceSize)size, 0, &dst);\n"
    "                if (dst) { std::memcpy(dst, data, (size_t)size); vkUnmapMemory(b->device, it->second.memory); }\n"
    "                mithril::vk::stamp_buffer_write(it->second);\n"
)
new = (
    "                if (it->second.persistentlyMapped && it->second.mapped) {\n"
    "                    std::memcpy(static_cast<uint8_t*>(it->second.mapped) + offset, data, size);\n"
    "                } else {\n"
    "                    void* dst = nullptr;\n"
    "                    vkMapMemory(b->device, it->second.memory, (VkDeviceSize)offset,\n"
    "                                (VkDeviceSize)size, 0, &dst);\n"
    "                    if (dst) {\n"
    "                        std::memcpy(dst, data, (size_t)size);\n"
    "                        vkUnmapMemory(b->device, it->second.memory);\n"
    "                    }\n"
    "                }\n"
    "                mithril::vk::stamp_buffer_write(it->second);\n"
)
if s.count(old) != 1:
    raise SystemExit(f"Resources fallback map matches={s.count(old)}")
s = s.replace(old, new, 1)
old = (
    "    // Buffer 不在飞：可以安全原地更新。\n"
    "    void* dst = nullptr;\n"
    "    if (data && size > 0) {\n"
    "        vkMapMemory(b->device, it->second.memory, offset, size, 0, &dst);\n"
    "        if (dst) { std::memcpy(dst, data, size); vkUnmapMemory(b->device, it->second.memory); }\n"
    "    }\n"
    "    mithril::vk::stamp_buffer_write(it->second);\n"
)
new = (
    "    // Buffer 不在飞：可以安全原地更新。 Persistent storage is already\n"
    "    // mapped for its lifetime, so mapping it again would violate Vulkan.\n"
    "    if (data && size > 0) {\n"
    "        if (it->second.persistentlyMapped && it->second.mapped) {\n"
    "            std::memcpy(static_cast<uint8_t*>(it->second.mapped) + offset, data, size);\n"
    "        } else {\n"
    "            void* dst = nullptr;\n"
    "            vkMapMemory(b->device, it->second.memory, offset, size, 0, &dst);\n"
    "            if (dst) {\n"
    "                std::memcpy(dst, data, size);\n"
    "                vkUnmapMemory(b->device, it->second.memory);\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "    mithril::vk::stamp_buffer_write(it->second);\n"
)
if s.count(old) != 1:
    raise SystemExit("Resources normal upload map mismatch")
p.write_text(s.replace(old, new, 1))

p = Path("tests/render_smoke.c")
s = p.read_text()
s = s.replace(
    "typedef void      (*bufferData_fn)(GLenum, GLsizeiptr, const void*, GLenum);\n",
    "typedef void      (*bufferData_fn)(GLenum, GLsizeiptr, const void*, GLenum);\n"
    "typedef void      (*bufferStorage_fn)(GLenum, GLsizeiptr, const void*, GLbitfield);\n"
    "typedef void*     (*mapBufferRange_fn)(GLenum, GLintptr, GLsizeiptr, GLbitfield);\n",
    1,
)
s = s.replace(
    "    bufferData_fn         bufferData         = NULL;\n",
    "    bufferData_fn         bufferData         = NULL;\n"
    "    bufferStorage_fn      bufferStorage      = NULL;\n"
    "    mapBufferRange_fn     mapBufferRange     = NULL;\n",
    1,
)
s = s.replace(
    '    RESOLVE(bufferData, "glBufferData");\n',
    '    RESOLVE(bufferData, "glBufferData");\n'
    '    RESOLVE(bufferStorage, "glBufferStorage");\n'
    '    RESOLVE(mapBufferRange, "glMapBufferRange");\n',
    1,
)
marker = (
    "    /* =====================================================================\n"
    "     * 扩展测试：贴图采样 + mipmap + 动态 UBO + 多帧 glFinish 稳定性\n"
)
test = r'''    /* ---- persistent coherent VBO: Sodium upload-ring semantics ----------- */
    {
        GLuint persistentVao = 0, persistentVbo = 0;
        genVertexArrays(1, &persistentVao);
        bindVertexArray(persistentVao);
        genBuffers(1, &persistentVbo);
        bindBuffer(GL_ARRAY_BUFFER, persistentVbo);
        bufferStorage(GL_ARRAY_BUFFER, sizeof(verts), NULL,
                      GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT |
                      GL_MAP_COHERENT_BIT | GL_DYNAMIC_STORAGE_BIT);
        GLfloat* mappedVerts = (GLfloat*)mapBufferRange(
            GL_ARRAY_BUFFER, 0, sizeof(verts),
            GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
        CHECK(mappedVerts != NULL, "persistent coherent VBO returns a live map");
        if (mappedVerts) memcpy(mappedVerts, verts, sizeof(verts));
        vertexAttribPtr(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), (const void*)0);
        enableAttrib(0);
        bindFramebuffer(GL_FRAMEBUFFER, fbo);
        viewport(0, 0, R, C);
        clearColor(0.0f, 0.0f, 0.0f, 1.0f);
        clear(GL_COLOR_BUFFER_BIT);
        useProgram(prog);
        // Deliberately no FlushMappedBufferRange and no UnmapBuffer.
        drawArrays(GL_TRIANGLES, 0, 3);
        finish();
        unsigned char persistentPx[4] = {0,0,0,0};
        readPixels(R / 2, C / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, persistentPx);
        CHECK(persistentPx[0] > 128 && persistentPx[1] < 32 && persistentPx[2] < 32,
              "persistent coherent VBO direct write reaches GPU (rgba=%d,%d,%d,%d)",
              persistentPx[0], persistentPx[1], persistentPx[2], persistentPx[3]);
        CHECK(getError() == GL_NO_ERROR, "persistent-map draw leaves no GL error");
        bindVertexArray(vao);
        bindBuffer(GL_ARRAY_BUFFER, vbo);
    }

'''
if s.count(marker) != 1:
    raise SystemExit("render_smoke persistent insertion marker mismatch")
p.write_text(s.replace(marker, test + marker, 1))
