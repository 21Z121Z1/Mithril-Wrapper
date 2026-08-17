#!/usr/bin/env python3
"""Apply phase 3: lazy CPU storage for glBufferData(NULL).

Every rewrite is anchored to the current canonical DirectMetal integration
source. Any source drift aborts before build/test rather than guessing.
"""
from pathlib import Path


def exact(path, old, new, count=1):
    p = Path(path)
    text = p.read_text()
    actual = text.count(old)
    if actual != count:
        raise SystemExit(f"{path}: expected {count}, found {actual}: {old[:100]!r}")
    p.write_text(text.replace(old, new, count))


# BufferData owns a logical GL size independently from its optional CPU mirror.
exact(
    "src/gl/internal.h",
    '''struct BufferData {
    std::vector<uint8_t> data;
    uint64_t lifetime_id = 0;
    uint64_t content_version = 0;
    uint64_t previous_content_version = 0;
    size_t update_offset = 0;
    size_t update_size = 0;
    bool update_is_partial = false;
    bool defined = false;
    bool mapped = false;
    bool map_writable = false;
    size_t map_offset = 0;

    void RecordUpdate(size_t offset, size_t size, bool partial) {
        previous_content_version = content_version;
        ++content_version;
        update_offset = offset;
        update_size = size;
        update_is_partial = partial;
    }
};
''',
    '''struct BufferData {
    // GL storage size is independent from whether undefined CPU bytes have
    // ever been materialized. glBufferData(NULL) therefore performs no eager
    // memset/zero-fill; undefined bytes are created only if a later CPU-visible
    // operation actually needs them.
    std::vector<uint8_t> data;
    size_t storage_size = 0;
    uint64_t lifetime_id = 0;
    uint64_t content_version = 0;
    uint64_t previous_content_version = 0;
    size_t update_offset = 0;
    size_t update_size = 0;
    bool update_is_partial = false;
    bool defined = false;
    bool mapped = false;
    bool map_writable = false;
    size_t map_offset = 0;

    size_t Size() const { return storage_size; }
    bool IsMaterialized() const { return data.size() == storage_size; }
    void EnsureMaterialized() {
        // Contents after glBufferData(NULL) are undefined. Zero is simply a
        // deterministic legal value when a later CPU read needs those bytes.
        if (!IsMaterialized()) data.assign(storage_size, 0);
    }

    void RecordUpdate(size_t offset, size_t size, bool partial) {
        previous_content_version = content_version;
        ++content_version;
        update_offset = offset;
        update_size = size;
        update_is_partial = partial;
    }
};
''')

# Pixel-pack destinations need actual CPU bytes only when they are written.
exact("src/gl/vertex.cpp",
'''        if (found == g_buffers.end() || found->second.mapped ||
            offset % datum_bytes != 0 || offset > found->second.data.size() ||
            base > found->second.data.size() - offset ||
            span > found->second.data.size() - offset - base) {
            PUSH_ERROR(GL_INVALID_OPERATION);
            return false;
        }
        output->buffer = &found->second;
        output->data = found->second.data.empty()
            ? nullptr : found->second.data.data() + offset + base;
''',
'''        if (found == g_buffers.end() || found->second.mapped ||
            offset % datum_bytes != 0 || offset > found->second.Size() ||
            base > found->second.Size() - offset ||
            span > found->second.Size() - offset - base) {
            PUSH_ERROR(GL_INVALID_OPERATION);
            return false;
        }
        found->second.EnsureMaterialized();
        output->buffer = &found->second;
        output->data = found->second.data.empty()
            ? nullptr : found->second.data.data() + offset + base;
''')
exact("src/gl/vertex.cpp",
'''    if (found == g_buffers.end() || found->second.mapped ||
        offset > found->second.data.size() ||
        byte_count > found->second.data.size() - offset) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return false;
    }
    output->buffer = &found->second;
    output->data = found->second.data.empty()
        ? nullptr : found->second.data.data() + offset;
''',
'''    if (found == g_buffers.end() || found->second.mapped ||
        offset > found->second.Size() ||
        byte_count > found->second.Size() - offset) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return false;
    }
    found->second.EnsureMaterialized();
    output->buffer = &found->second;
    output->data = found->second.data.empty()
        ? nullptr : found->second.data.data() + offset;
''')
exact("src/gl/vertex.cpp",
'''    destination->buffer->RecordUpdate(
        0, destination->buffer->data.size(), false);
''',
'''    destination->buffer->RecordUpdate(
        0, destination->buffer->Size(), false);
''')

# glBufferData(NULL): logical allocation only. A full later overwrite assigns
# caller bytes directly and therefore never pays a preceding zero-fill pass.
exact("src/gl/vertex.cpp",
'''    if (data) {
        it->second.data.assign((const uint8_t*)data, (const uint8_t*)data + size);
    } else {
        it->second.data.assign((size_t)size, 0);
    }
    it->second.RecordUpdate(0, it->second.data.size(), false);
    it->second.defined = data != nullptr;
''',
'''    BufferData& buffer = it->second;
    buffer.storage_size = static_cast<size_t>(size);
    if (data) {
        buffer.data.assign((const uint8_t*)data, (const uint8_t*)data + size);
    } else {
        buffer.data.clear();
    }
    buffer.RecordUpdate(0, buffer.Size(), false);
    buffer.defined = data != nullptr;
''')
exact("src/gl/vertex.cpp",
'''    auto it = g_buffers.find(*bound);
    if (it->second.mapped) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    if (offset + size > (GLintptr)it->second.data.size()) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    if (it->second.defined && size > 0 &&
        std::memcmp(it->second.data.data() + offset, data,
                    static_cast<size_t>(size)) == 0)
        return;
    std::memcpy(it->second.data.data() + offset, data, size);
    const size_t write_offset = static_cast<size_t>(offset);
    const size_t write_size = static_cast<size_t>(size);
    const bool full_write = write_offset == 0 &&
                            write_size == it->second.data.size();
    it->second.RecordUpdate(write_offset, write_size, !full_write);
    it->second.defined = true;
''',
'''    auto it = g_buffers.find(*bound);
    if (it->second.mapped) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    BufferData& buffer = it->second;
    const size_t write_offset = static_cast<size_t>(offset);
    const size_t write_size = static_cast<size_t>(size);
    if (write_offset > buffer.Size() || write_size > buffer.Size() - write_offset) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    if (!write_size) return;
    const bool full_write = write_offset == 0 && write_size == buffer.Size();
    if (!buffer.IsMaterialized() && full_write) {
        buffer.data.assign((const uint8_t*)data,
                           (const uint8_t*)data + write_size);
    } else {
        buffer.EnsureMaterialized();
        if (buffer.defined &&
            std::memcmp(buffer.data.data() + write_offset, data, write_size) == 0)
            return;
        std::memcpy(buffer.data.data() + write_offset, data, write_size);
    }
    buffer.RecordUpdate(write_offset, write_size, !full_write);
    buffer.defined = true;
''')

# Logical size is the GL observable. Materialization is an implementation detail.
for old, new, count in (
    ("begin > found->second.data.size() ||\n        length > found->second.data.size() - begin",
     "begin > found->second.Size() ||\n        length > found->second.Size() - begin", 1),
    ("? static_cast<GLint>(found->second.data.size())",
     "? static_cast<GLint>(found->second.Size())", 1),
    ("? static_cast<GLint64>(found->second.data.size())",
     "? static_cast<GLint64>(found->second.Size())", 1),
    ("case GL_BUFFER_SIZE: *params = (GLint)b->data.size(); break;",
     "case GL_BUFFER_SIZE: *params = (GLint)b->Size(); break;", 1),
    ("*params = (GLint64)b->data.size();",
     "*params = (GLint64)b->Size();", 1),
):
    exact("src/gl/vertex.cpp", old, new, count)

# Copy semantics materialize only when needed. A full overwrite into an
# unmaterialized different destination assigns source bytes directly.
exact("src/gl/vertex.cpp",
'''    if (readoffset < 0 || writeoffset < 0 || size < 0 ||
        readoffset + size > (GLintptr)src->data.size() ||
        writeoffset + size > (GLintptr)dst->data.size()) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    if (src->mapped || dst->mapped) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    std::memmove(dst->data.data() + writeoffset, src->data.data() + readoffset,
                 size);
    const bool full_write = writeoffset == 0 &&
        static_cast<size_t>(size) == dst->data.size();
    dst->RecordUpdate(static_cast<size_t>(writeoffset),
                      static_cast<size_t>(size), !full_write);
    dst->defined = true;
''',
'''    if (readoffset < 0 || writeoffset < 0 || size < 0 ||
        readoffset + size > (GLintptr)src->Size() ||
        writeoffset + size > (GLintptr)dst->Size()) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    if (src->mapped || dst->mapped) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    const size_t copy_size = static_cast<size_t>(size);
    if (!copy_size) return;
    src->EnsureMaterialized();
    const bool full_write = writeoffset == 0 && copy_size == dst->Size();
    if (src != dst && full_write && !dst->IsMaterialized()) {
        dst->data.assign(src->data.data() + readoffset,
                         src->data.data() + readoffset + copy_size);
    } else {
        dst->EnsureMaterialized();
        std::memmove(dst->data.data() + writeoffset,
                     src->data.data() + readoffset, copy_size);
    }
    dst->RecordUpdate(static_cast<size_t>(writeoffset), copy_size, !full_write);
    dst->defined = true;
''')

exact("src/gl/vertex.cpp",
'''    if (offset < 0 || size < 0 || offset + size > (GLintptr)b->data.size()) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    std::memcpy(data, b->data.data() + offset, size);
''',
'''    if (offset < 0 || size < 0 || offset + size > (GLintptr)b->Size()) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    b->EnsureMaterialized();
    std::memcpy(data, b->data.data() + offset, size);
''')
exact("src/gl/vertex.cpp",
'''    if (b->mapped) { PUSH_ERROR(GL_INVALID_OPERATION); return nullptr; }
    if (b->data.empty()) { PUSH_ERROR(GL_OUT_OF_MEMORY); return nullptr; }
    b->mapped = true;
    b->map_offset = 0;
    b->map_writable = access != GL_READ_ONLY;
    if (b->map_writable) {
        b->RecordUpdate(0, b->data.size(), false);
        b->defined = true;
    }
    return b->data.data();
''',
'''    if (b->mapped) { PUSH_ERROR(GL_INVALID_OPERATION); return nullptr; }
    if (!b->Size()) { PUSH_ERROR(GL_OUT_OF_MEMORY); return nullptr; }
    b->EnsureMaterialized();
    b->mapped = true;
    b->map_offset = 0;
    b->map_writable = access != GL_READ_ONLY;
    if (b->map_writable) {
        b->RecordUpdate(0, b->Size(), false);
        b->defined = true;
    }
    return b->data.data();
''')
exact("src/gl/vertex.cpp",
'''    if (b->mapped) { PUSH_ERROR(GL_INVALID_OPERATION); return nullptr; }
    if (offset + length > (GLintptr)b->data.size()) { PUSH_ERROR(GL_INVALID_VALUE); return nullptr; }
    b->mapped = true;
''',
'''    if (b->mapped) { PUSH_ERROR(GL_INVALID_OPERATION); return nullptr; }
    if (offset + length > (GLintptr)b->Size()) { PUSH_ERROR(GL_INVALID_VALUE); return nullptr; }
    b->EnsureMaterialized();
    b->mapped = true;
''')
exact("src/gl/vertex.cpp",
'''        const bool full_write = map_offset == 0 &&
                                map_length == b->data.size();
''',
'''        const bool full_write = map_offset == 0 &&
                                map_length == b->Size();
''')
exact("src/gl/vertex.cpp",
'''    if (offset < 0 || length < 0 ||
        offset + length > (GLintptr)b->data.size()) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    b->RecordUpdate(0, b->data.size(), false);
''',
'''    if (offset < 0 || length < 0 ||
        offset + length > (GLintptr)b->Size()) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    b->RecordUpdate(0, b->Size(), false);
''')

# Draw-time CPU readers materialize undefined bytes on demand. Resident VBOs
# remain eligible only when frontend state is defined, preserving old behavior.
exact("src/gl/draw.cpp",
'''    auto bit = g_buffers.find(a.buffer);
    if (bit == g_buffers.end()) return false;
    GLuint type_sz = AttribTypeSize(a.type);
''',
'''    auto bit = g_buffers.find(a.buffer);
    if (bit == g_buffers.end()) return false;
    bit->second.EnsureMaterialized();
    GLuint type_sz = AttribTypeSize(a.type);
''')
exact("src/gl/draw.cpp",
"if (src + (size_t)a.size * type_sz > bit->second.data.size()) return false;",
"if (src + (size_t)a.size * type_sz > bit->second.Size()) return false;")
exact("src/gl/draw.cpp",
'''    auto buffer = g_buffers.find(a.buffer);
    if (buffer == g_buffers.end()) return false;
    const uint32_t type_size = AttribTypeSize(a.type);
''',
'''    auto buffer = g_buffers.find(a.buffer);
    if (buffer == g_buffers.end()) return false;
    buffer->second.EnsureMaterialized();
    const uint32_t type_size = AttribTypeSize(a.type);
''')
exact("src/gl/draw.cpp",
'''    if (source_offset > buffer->second.data.size() ||
        source_bytes > buffer->second.data.size() - source_offset)
''',
'''    if (source_offset > buffer->second.Size() ||
        source_bytes > buffer->second.Size() - source_offset)
''')
exact("src/gl/draw.cpp",
'''        resident = resident && end >= start &&
                   end <= (uint64_t)bit->second.data.size();

        if (resident) {
''',
'''        resident = resident && end >= start &&
                   end <= (uint64_t)bit->second.Size();

        if (resident) {
            bit->second.EnsureMaterialized();
''')
exact("src/gl/draw.cpp",
'''            vstream.source_data = bit->second.data.data();
            vstream.source_size = bit->second.data.size();
''',
'''            vstream.source_data = bit->second.data.data();
            vstream.source_size = bit->second.Size();
''')
exact("src/gl/draw.cpp",
'''        const auto buffer = g_buffers.find(indexed.buffer);
        if (!indexed.buffer || buffer == g_buffers.end()) {
''',
'''        auto buffer = g_buffers.find(indexed.buffer);
        if (!indexed.buffer || buffer == g_buffers.end()) {
''')
exact("src/gl/draw.cpp",
'''        const uint64_t offset = static_cast<uint64_t>(indexed.offset);
        const uint64_t available = indexed.whole_buffer
            ? static_cast<uint64_t>(buffer->second.data.size())
            : static_cast<uint64_t>(indexed.size);
        if (available < static_cast<uint64_t>(block.data_size) ||
            offset > buffer->second.data.size() ||
            static_cast<uint64_t>(block.data_size) >
                buffer->second.data.size() - offset) {
''',
'''        const uint64_t offset = static_cast<uint64_t>(indexed.offset);
        const uint64_t available = indexed.whole_buffer
            ? static_cast<uint64_t>(buffer->second.Size())
            : static_cast<uint64_t>(indexed.size);
        if (available < static_cast<uint64_t>(block.data_size) ||
            offset > buffer->second.Size() ||
            static_cast<uint64_t>(block.data_size) >
                buffer->second.Size() - offset) {
''')
exact("src/gl/draw.cpp",
'''        auto append_binding = [&](uint32_t internal_binding,
                                  bool vertex_stage,
                                  bool fragment_stage) {
''',
'''        buffer->second.EnsureMaterialized();
        auto append_binding = [&](uint32_t internal_binding,
                                  bool vertex_stage,
                                  bool fragment_stage) {
''')
exact("src/gl/draw.cpp",
'''            binding.source_data = buffer->second.data.data();
            binding.source_size = buffer->second.data.size();
''',
'''            binding.source_data = buffer->second.data.data();
            binding.source_size = buffer->second.Size();
''')
exact("src/gl/draw.cpp",
'''    auto bit = g_buffers.find(g_bound_element_buffer);
    if (bit == g_buffers.end()) { *err = GL_INVALID_OPERATION; return out; }
    GLuint idx_sz;
''',
'''    auto bit = g_buffers.find(g_bound_element_buffer);
    if (bit == g_buffers.end()) { *err = GL_INVALID_OPERATION; return out; }
    bit->second.EnsureMaterialized();
    GLuint idx_sz;
''')

# Pixel-unpack buffers and texture buffers use logical bounds and materialize
# only before CPU bytes are actually consumed.
exact("src/gl/texture.cpp",
'''        const auto buffer = g_buffers.find(g_bound_pixel_unpack_buffer);
        const uint64_t offset = reinterpret_cast<uintptr_t>(pixels);
        if (buffer == g_buffers.end() || buffer->second.mapped ||
            offset % type_bytes != 0 || offset > buffer->second.data.size() ||
            base > buffer->second.data.size() - offset ||
            span > buffer->second.data.size() - offset - base) {
            PUSH_ERROR(GL_INVALID_OPERATION);
            return false;
        }
        output->data = buffer->second.data.empty()
            ? nullptr : buffer->second.data.data() + offset + base;
''',
'''        auto buffer = g_buffers.find(g_bound_pixel_unpack_buffer);
        const uint64_t offset = reinterpret_cast<uintptr_t>(pixels);
        if (buffer == g_buffers.end() || buffer->second.mapped ||
            offset % type_bytes != 0 || offset > buffer->second.Size() ||
            base > buffer->second.Size() - offset ||
            span > buffer->second.Size() - offset - base) {
            PUSH_ERROR(GL_INVALID_OPERATION);
            return false;
        }
        buffer->second.EnsureMaterialized();
        output->data = buffer->second.data.empty()
            ? nullptr : buffer->second.data.data() + offset + base;
''')
exact("src/gl/texture.cpp",
'''    const auto buffer = g_buffers.find(g_bound_pixel_unpack_buffer);
    const uint64_t offset = reinterpret_cast<uintptr_t>(pointer);
    if (buffer == g_buffers.end() || buffer->second.mapped ||
        offset > buffer->second.data.size() ||
        byte_count > buffer->second.data.size() - offset) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return false;
    }
    output->data = buffer->second.data.empty()
        ? nullptr : buffer->second.data.data() + offset;
''',
'''    auto buffer = g_buffers.find(g_bound_pixel_unpack_buffer);
    const uint64_t offset = reinterpret_cast<uintptr_t>(pointer);
    if (buffer == g_buffers.end() || buffer->second.mapped ||
        offset > buffer->second.Size() ||
        byte_count > buffer->second.Size() - offset) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return false;
    }
    buffer->second.EnsureMaterialized();
    output->data = buffer->second.data.empty()
        ? nullptr : buffer->second.data.data() + offset;
''')
exact("src/gl/texture.cpp",
'''    const uint32_t bytes_per_texel = texture.tex_buffer_bytes_per_texel;
    const size_t available_texels = bytes_per_texel
        ? buffer->second.data.size() / bytes_per_texel : 0;
''',
'''    buffer->second.EnsureMaterialized();
    const uint32_t bytes_per_texel = texture.tex_buffer_bytes_per_texel;
    const size_t available_texels = bytes_per_texel
        ? buffer->second.Size() / bytes_per_texel : 0;
''')

# Register the backend-neutral storage regression in both reference paths.
exact("cmake/MithrilSmokeTests.cmake",
'''    fbo_smoke
    3d_smoke
    render3d_smoke)
''',
'''    fbo_smoke
    3d_smoke
    render3d_smoke
    lazy_buffer_storage_smoke)
''')
exact("cmake/MithrilSmokeTests.cmake",
'''    directmetal_fbo_smoke
    directmetal_buffer_streaming_smoke)
''',
'''    directmetal_fbo_smoke
    directmetal_buffer_streaming_smoke
    lazy_buffer_storage_smoke)
''')

# Fail closed on the eager-write anti-pattern and any remaining BufferData size
# coupling in the audited hot/transfer paths.
vertex = Path("src/gl/vertex.cpp").read_text()
internal = Path("src/gl/internal.h").read_text()
if "data.assign((size_t)size, 0)" in vertex:
    raise SystemExit("eager glBufferData(NULL) zero fill remains")
for required in (
    "size_t storage_size = 0;",
    "void EnsureMaterialized()",
    "buffer.data.clear();",
    "!buffer.IsMaterialized() && full_write",
):
    if required not in internal + vertex:
        raise SystemExit(f"missing lazy-storage invariant: {required}")

print("lazy GL buffer storage phase applied successfully")
