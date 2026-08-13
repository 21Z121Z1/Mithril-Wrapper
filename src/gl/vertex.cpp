// Mithril-Wrapper GL entry points -- S3 vertex/buffer domain (M2-VK, M3).
// glGen/Bind/DeleteVertexArrays, glGen/Bind/DeleteBuffers, buffer uploads,
// mapping and queries, glVertexAttribPointer/Divisor/constant 1-4s and the
// attribute getters. Owns the shared VAO/VBO name tables (internal.h).

#include "internal.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include <util/log.h>

// Shared tables (declared extern in internal.h; the draw path reads
// them through the header).
std::unordered_map<GLuint, VAOData> g_vaos;
std::array<CurrentAttribData, kMaxAttribs> g_current_attribs{};
std::unordered_map<GLuint, BufferData> g_buffers;
GLuint g_next_vao = 1, g_next_buffer = 1;
uint64_t g_next_buffer_lifetime = 1;
GLuint g_bound_vao = 0;
GLuint g_bound_array_buffer = 0;
GLuint g_bound_element_buffer = 0;
GLuint g_bound_uniform_buffer = 0;
GLuint g_bound_copy_read_buffer = 0;
GLuint g_bound_copy_write_buffer = 0;
GLuint g_bound_pixel_pack_buffer = 0;
GLuint g_bound_pixel_unpack_buffer = 0;
std::array<IndexedBufferBinding, kMaxUniformBufferBindings>
    g_uniform_buffer_bindings{};

namespace {

void TraceBufferMutation(const char* operation, GLenum target, GLuint id,
                         GLintptr offset, GLsizeiptr length,
                         const void* source) {
    // GUI VBO 33 is the first persistent upload buffer used by Minecraft 26.2
    // on the iPad path. Keep this trace narrowly scoped so a long-running
    // world cannot flood latestlog.txt while we establish its write path.
    if (id != 33) return;
    const auto found = g_buffers.find(id);
    if (found == g_buffers.end()) return;
    const BufferData& buffer = found->second;
    const uint8_t* bytes = buffer.data.empty() ? nullptr : buffer.data.data();
    ML_LOG_INFO(
        "TRACE BUFFER op=%s target=0x%x id=%u offset=%lld length=%lld "
        "size=%zu defined=%d mapped=%d writable=%d source=%p first=%02x%02x%02x%02x%02x%02x%02x%02x",
        operation, target, id, (long long)offset, (long long)length,
        buffer.data.size(), buffer.defined, buffer.mapped,
        buffer.map_writable, source,
        bytes ? bytes[0] : 0, bytes ? bytes[1] : 0,
        bytes ? bytes[2] : 0, bytes ? bytes[3] : 0,
        bytes ? bytes[4] : 0, bytes ? bytes[5] : 0,
        bytes ? bytes[6] : 0, bytes ? bytes[7] : 0);
}

} // namespace

// program id -> selected-backend program handle (created lazily on first draw by the
// draw path; erased by the shader-lifecycle path on glDeleteProgram).
std::unordered_map<GLuint, uint64_t> g_backend_programs;

namespace {

bool AddPackProduct(uint64_t* total, uint64_t left, uint64_t right) {
    if (left && right > std::numeric_limits<uint64_t>::max() / left)
        return false;
    const uint64_t product = left * right;
    if (*total > std::numeric_limits<uint64_t>::max() - product) return false;
    *total += product;
    return true;
}

GLint FloatToSint(GLfloat value) {
    const double wide = static_cast<double>(value);
    if (std::isnan(wide)) return 0;
    if (wide <= static_cast<double>(std::numeric_limits<GLint>::min()))
        return std::numeric_limits<GLint>::min();
    if (wide >= static_cast<double>(std::numeric_limits<GLint>::max()))
        return std::numeric_limits<GLint>::max();
    return static_cast<GLint>(value);
}

GLuint FloatToUint(GLfloat value) {
    const double wide = static_cast<double>(value);
    if (std::isnan(wide) || wide <= 0.0) return 0;
    if (wide >= static_cast<double>(std::numeric_limits<GLuint>::max()))
        return std::numeric_limits<GLuint>::max();
    return static_cast<GLuint>(value);
}

} // namespace

bool ResolvePixelPackDestination(void* pointer, uint32_t width,
                                 uint32_t height, uint32_t images,
                                 bool three_dimensional,
                                 size_t bytes_per_pixel, size_t datum_bytes,
                                 PixelPackDestination* output) {
    if (!output || !bytes_per_pixel || !datum_bytes) return false;
    *output = {};
    const s::PixelStore& store = s::GetState().pixels;
    const uint64_t row_pixels = store.pack_row_length > 0
        ? static_cast<uint64_t>(store.pack_row_length) : width;
    uint64_t row_bytes = 0;
    if (!AddPackProduct(&row_bytes, row_pixels, bytes_per_pixel)) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return false;
    }
    const uint64_t alignment = static_cast<uint64_t>(store.pack_alignment);
    if (row_bytes > std::numeric_limits<uint64_t>::max() - alignment + 1) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return false;
    }
    row_bytes = ((row_bytes + alignment - 1) / alignment) * alignment;
    const uint64_t image_rows = three_dimensional && store.pack_image_height > 0
        ? static_cast<uint64_t>(store.pack_image_height) : height;
    uint64_t image_stride = 0;
    if (!AddPackProduct(&image_stride, image_rows, row_bytes)) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return false;
    }
    uint64_t base = 0;
    if ((three_dimensional &&
         !AddPackProduct(&base,
                         static_cast<uint64_t>(store.pack_skip_images),
                         image_stride)) ||
        !AddPackProduct(&base, static_cast<uint64_t>(store.pack_skip_rows),
                        row_bytes) ||
        !AddPackProduct(&base, static_cast<uint64_t>(store.pack_skip_pixels),
                        bytes_per_pixel)) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return false;
    }
    uint64_t span = 0;
    if (width && height && images &&
        (!AddPackProduct(&span, images - 1, image_stride) ||
         !AddPackProduct(&span, height - 1, row_bytes) ||
         !AddPackProduct(&span, width, bytes_per_pixel))) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return false;
    }

    output->row_stride = static_cast<size_t>(row_bytes);
    output->image_stride = static_cast<size_t>(image_stride);
    if (g_bound_pixel_pack_buffer) {
        const auto found = g_buffers.find(g_bound_pixel_pack_buffer);
        const uint64_t offset = reinterpret_cast<uintptr_t>(pointer);
        if (found == g_buffers.end() || found->second.mapped ||
            offset % datum_bytes != 0 || offset > found->second.data.size() ||
            base > found->second.data.size() - offset ||
            span > found->second.data.size() - offset - base) {
            PUSH_ERROR(GL_INVALID_OPERATION);
            return false;
        }
        output->buffer = &found->second;
        output->data = found->second.data.empty()
            ? nullptr : found->second.data.data() + offset + base;
        output->provided = true;
        return true;
    }
    if (!pointer) return true;
    const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
    if (base > std::numeric_limits<uintptr_t>::max() - address) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return false;
    }
    output->data = reinterpret_cast<uint8_t*>(address + base);
    output->provided = true;
    return true;
}

bool ResolvePixelPackBytes(void* pointer, size_t byte_count,
                           PixelPackDestination* output) {
    if (!output) return false;
    *output = {};
    if (!g_bound_pixel_pack_buffer) {
        output->data = static_cast<uint8_t*>(pointer);
        output->provided = pointer != nullptr;
        return true;
    }
    const auto found = g_buffers.find(g_bound_pixel_pack_buffer);
    const uint64_t offset = reinterpret_cast<uintptr_t>(pointer);
    if (found == g_buffers.end() || found->second.mapped ||
        offset > found->second.data.size() ||
        byte_count > found->second.data.size() - offset) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return false;
    }
    output->buffer = &found->second;
    output->data = found->second.data.empty()
        ? nullptr : found->second.data.data() + offset;
    output->provided = true;
    return true;
}

void CommitPixelPackDestination(PixelPackDestination* destination) {
    if (!destination || !destination->buffer) return;
    ++destination->buffer->content_version;
    destination->buffer->defined = true;
}

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
        BufferData buffer;
        buffer.lifetime_id = g_next_buffer_lifetime++;
        g_buffers.emplace(buffers[i], std::move(buffer));
    }
}

void APIENTRY glDeleteBuffers(GLsizei n, const GLuint* buffers) {
    if (n < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    for (GLsizei i = 0; i < n; ++i) {
        auto it = g_buffers.find(buffers[i]);
        if (it == g_buffers.end()) continue;
        DetachBufferTextures(buffers[i]);
        v::DestroyBuffer(it->second.lifetime_id);
        if (g_bound_array_buffer == buffers[i]) g_bound_array_buffer = 0;
        if (g_bound_element_buffer == buffers[i]) g_bound_element_buffer = 0;
        if (g_bound_uniform_buffer == buffers[i]) g_bound_uniform_buffer = 0;
        if (g_bound_copy_read_buffer == buffers[i]) g_bound_copy_read_buffer = 0;
        if (g_bound_copy_write_buffer == buffers[i]) g_bound_copy_write_buffer = 0;
        if (g_bound_pixel_pack_buffer == buffers[i])
            g_bound_pixel_pack_buffer = 0;
        if (g_bound_pixel_unpack_buffer == buffers[i])
            g_bound_pixel_unpack_buffer = 0;
        for (auto& binding : g_uniform_buffer_bindings)
            if (binding.buffer == buffers[i]) binding = {};
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
        case GL_UNIFORM_BUFFER: g_bound_uniform_buffer = buffer; break;
        case GL_COPY_READ_BUFFER: g_bound_copy_read_buffer = buffer; break;
        case GL_COPY_WRITE_BUFFER: g_bound_copy_write_buffer = buffer; break;
        case GL_PIXEL_PACK_BUFFER: g_bound_pixel_pack_buffer = buffer; break;
        case GL_PIXEL_UNPACK_BUFFER: g_bound_pixel_unpack_buffer = buffer; break;
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
        case GL_UNIFORM_BUFFER: bound = &g_bound_uniform_buffer; break;
        case GL_COPY_READ_BUFFER: bound = &g_bound_copy_read_buffer; break;
        case GL_COPY_WRITE_BUFFER: bound = &g_bound_copy_write_buffer; break;
        case GL_PIXEL_PACK_BUFFER: bound = &g_bound_pixel_pack_buffer; break;
        case GL_PIXEL_UNPACK_BUFFER: bound = &g_bound_pixel_unpack_buffer; break;
        default: PUSH_ERROR(GL_INVALID_ENUM); return;
    }
    if (*bound == 0) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    if (size < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    auto it = g_buffers.find(*bound);
    if (it->second.mapped) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    if (data) {
        it->second.data.assign((const uint8_t*)data, (const uint8_t*)data + size);
    } else {
        it->second.data.assign((size_t)size, 0);
    }
    ++it->second.content_version;
    it->second.defined = data != nullptr;
    TraceBufferMutation("data", target, *bound, 0, size, data);
}

void APIENTRY glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data) {
    GLuint* bound = nullptr;
    switch (target) {
        case GL_ARRAY_BUFFER: bound = &g_bound_array_buffer; break;
        case GL_ELEMENT_ARRAY_BUFFER: bound = &g_bound_element_buffer; break;
        case GL_UNIFORM_BUFFER: bound = &g_bound_uniform_buffer; break;
        case GL_COPY_READ_BUFFER: bound = &g_bound_copy_read_buffer; break;
        case GL_COPY_WRITE_BUFFER: bound = &g_bound_copy_write_buffer; break;
        case GL_PIXEL_PACK_BUFFER: bound = &g_bound_pixel_pack_buffer; break;
        case GL_PIXEL_UNPACK_BUFFER: bound = &g_bound_pixel_unpack_buffer; break;
        default: PUSH_ERROR(GL_INVALID_ENUM); return;
    }
    if (*bound == 0) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    if (offset < 0 || size < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    auto it = g_buffers.find(*bound);
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
    ++it->second.content_version;
    it->second.defined = true;
    TraceBufferMutation("subdata", target, *bound, offset, size, data);
}

// ---- buffer queries / mapping (M3) -----------------------------------------

BufferData* BoundBufferForTarget(GLenum target, GLenum* error) {
    GLuint* bound = nullptr;
    switch (target) {
        case GL_ARRAY_BUFFER: bound = &g_bound_array_buffer; break;
        case GL_ELEMENT_ARRAY_BUFFER: bound = &g_bound_element_buffer; break;
        case GL_UNIFORM_BUFFER: bound = &g_bound_uniform_buffer; break;
        case GL_COPY_READ_BUFFER: bound = &g_bound_copy_read_buffer; break;
        case GL_COPY_WRITE_BUFFER: bound = &g_bound_copy_write_buffer; break;
        case GL_PIXEL_PACK_BUFFER: bound = &g_bound_pixel_pack_buffer; break;
        case GL_PIXEL_UNPACK_BUFFER: bound = &g_bound_pixel_unpack_buffer; break;
        default: *error = GL_INVALID_ENUM; return nullptr;
    }
    if (*bound == 0) { *error = GL_INVALID_OPERATION; return nullptr; }
    auto it = g_buffers.find(*bound);
    if (it == g_buffers.end()) { *error = GL_INVALID_OPERATION; return nullptr; }
    return &it->second;
}

void APIENTRY glBindBufferBase(GLenum target, GLuint index, GLuint buffer) {
    if (target != GL_UNIFORM_BUFFER) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    if (index >= kMaxUniformBufferBindings) {
        PUSH_ERROR(GL_INVALID_VALUE); return;
    }
    if (buffer != 0 && !g_buffers.count(buffer)) {
        PUSH_ERROR(GL_INVALID_VALUE); return;
    }
    g_bound_uniform_buffer = buffer;
    IndexedBufferBinding binding;
    binding.buffer = buffer;
    binding.whole_buffer = buffer != 0;
    g_uniform_buffer_bindings[index] = binding;
}

void APIENTRY glBindBufferRange(GLenum target, GLuint index, GLuint buffer,
                                GLintptr offset, GLsizeiptr size) {
    if (target != GL_UNIFORM_BUFFER) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    if (index >= kMaxUniformBufferBindings) {
        PUSH_ERROR(GL_INVALID_VALUE); return;
    }
    if (buffer == 0) {
        g_bound_uniform_buffer = 0;
        g_uniform_buffer_bindings[index] = {};
        return;
    }
    const auto found = g_buffers.find(buffer);
    if (found == g_buffers.end()) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (offset < 0 || size <= 0 ||
        offset % kUniformBufferOffsetAlignment != 0) {
        PUSH_ERROR(GL_INVALID_VALUE); return;
    }
    const uint64_t begin = static_cast<uint64_t>(offset);
    const uint64_t length = static_cast<uint64_t>(size);
    if (begin > found->second.data.size() ||
        length > found->second.data.size() - begin) {
        PUSH_ERROR(GL_INVALID_VALUE); return;
    }
    g_bound_uniform_buffer = buffer;
    g_uniform_buffer_bindings[index] = {buffer, offset, size, false};
}

void APIENTRY glGetIntegeri_v(GLenum target, GLuint index, GLint* data) {
    if (!data) return;
    if (index >= kMaxUniformBufferBindings) {
        PUSH_ERROR(GL_INVALID_VALUE); return;
    }
    const IndexedBufferBinding& binding = g_uniform_buffer_bindings[index];
    switch (target) {
        case GL_UNIFORM_BUFFER_BINDING:
            *data = static_cast<GLint>(binding.buffer); return;
        case GL_UNIFORM_BUFFER_START:
            *data = static_cast<GLint>(binding.offset); return;
        case GL_UNIFORM_BUFFER_SIZE: {
            const auto found = g_buffers.find(binding.buffer);
            *data = binding.whole_buffer && found != g_buffers.end()
                ? static_cast<GLint>(found->second.data.size())
                : static_cast<GLint>(binding.size);
            return;
        }
        default: PUSH_ERROR(GL_INVALID_ENUM); return;
    }
}

void APIENTRY glGetInteger64i_v(GLenum target, GLuint index, GLint64* data) {
    if (!data) return;
    if (index >= kMaxUniformBufferBindings) {
        PUSH_ERROR(GL_INVALID_VALUE); return;
    }
    const IndexedBufferBinding& binding = g_uniform_buffer_bindings[index];
    switch (target) {
        case GL_UNIFORM_BUFFER_BINDING:
            *data = static_cast<GLint64>(binding.buffer); return;
        case GL_UNIFORM_BUFFER_START:
            *data = static_cast<GLint64>(binding.offset); return;
        case GL_UNIFORM_BUFFER_SIZE: {
            const auto found = g_buffers.find(binding.buffer);
            *data = binding.whole_buffer && found != g_buffers.end()
                ? static_cast<GLint64>(found->second.data.size())
                : static_cast<GLint64>(binding.size);
            return;
        }
        default: PUSH_ERROR(GL_INVALID_ENUM); return;
    }
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
    if (src->mapped || dst->mapped) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    std::memmove(dst->data.data() + writeoffset, src->data.data() + readoffset,
                 size);
    ++dst->content_version;
    dst->defined = true;
    TraceBufferMutation("copy", writetarget, g_bound_copy_write_buffer,
                        writeoffset, size, src->data.data() + readoffset);
}

void APIENTRY glGetBufferParameteriv(GLenum target, GLenum pname, GLint* params) {
    GLenum err = GL_NO_ERROR;
    BufferData* b = BoundBufferForTarget(target, &err);
    if (err) { PUSH_ERROR(err); return; }
    switch (pname) {
        case GL_BUFFER_SIZE: *params = (GLint)b->data.size(); break;
        case GL_BUFFER_USAGE: *params = GL_STATIC_DRAW; break;
        case GL_BUFFER_ACCESS: *params = GL_WRITE_ONLY; break;
        case GL_BUFFER_MAPPED: *params = b->mapped ? GL_TRUE : GL_FALSE; break;
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
    *params = b->mapped ? b->data.data() + b->map_offset : nullptr;
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
    if (b->mapped) { PUSH_ERROR(GL_INVALID_OPERATION); return nullptr; }
    if (b->data.empty()) { PUSH_ERROR(GL_OUT_OF_MEMORY); return nullptr; }
    b->mapped = true;
    b->map_offset = 0;
    b->map_writable = access != GL_READ_ONLY;
    if (b->map_writable) {
        ++b->content_version;
        b->defined = true;
    }
    GLuint id = 0;
    switch (target) {
        case GL_ARRAY_BUFFER: id = g_bound_array_buffer; break;
        case GL_ELEMENT_ARRAY_BUFFER: id = g_bound_element_buffer; break;
        case GL_UNIFORM_BUFFER: id = g_bound_uniform_buffer; break;
        case GL_COPY_READ_BUFFER: id = g_bound_copy_read_buffer; break;
        case GL_COPY_WRITE_BUFFER: id = g_bound_copy_write_buffer; break;
        case GL_PIXEL_PACK_BUFFER: id = g_bound_pixel_pack_buffer; break;
        case GL_PIXEL_UNPACK_BUFFER: id = g_bound_pixel_unpack_buffer; break;
        default: break;
    }
    TraceBufferMutation("map", target, id, 0,
                        static_cast<GLsizeiptr>(b->data.size()), nullptr);
    return b->data.data();
}

void* APIENTRY glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length,
                                GLbitfield access) {
    if (offset < 0 || length < 0) { PUSH_ERROR(GL_INVALID_VALUE); return nullptr; }
    GLenum err = GL_NO_ERROR;
    BufferData* b = BoundBufferForTarget(target, &err);
    if (err) { PUSH_ERROR(err); return nullptr; }
    if (b->mapped) { PUSH_ERROR(GL_INVALID_OPERATION); return nullptr; }
    if (offset + length > (GLintptr)b->data.size()) { PUSH_ERROR(GL_INVALID_VALUE); return nullptr; }
    b->mapped = true;
    b->map_offset = static_cast<size_t>(offset);
    b->map_writable = access != GL_MAP_READ_BIT;
    if (b->map_writable) {
        ++b->content_version;
        b->defined = true;
    }
    GLuint id = 0;
    switch (target) {
        case GL_ARRAY_BUFFER: id = g_bound_array_buffer; break;
        case GL_ELEMENT_ARRAY_BUFFER: id = g_bound_element_buffer; break;
        case GL_UNIFORM_BUFFER: id = g_bound_uniform_buffer; break;
        case GL_COPY_READ_BUFFER: id = g_bound_copy_read_buffer; break;
        case GL_COPY_WRITE_BUFFER: id = g_bound_copy_write_buffer; break;
        case GL_PIXEL_PACK_BUFFER: id = g_bound_pixel_pack_buffer; break;
        case GL_PIXEL_UNPACK_BUFFER: id = g_bound_pixel_unpack_buffer; break;
        default: break;
    }
    TraceBufferMutation("map_range", target, id, offset, length, nullptr);
    return b->data.data() + offset;
}

GLboolean APIENTRY glUnmapBuffer(GLenum target) {
    GLenum err = GL_NO_ERROR;
    BufferData* b = BoundBufferForTarget(target, &err);
    if (err) { PUSH_ERROR(err); return GL_FALSE; }
    if (!b->mapped) { PUSH_ERROR(GL_INVALID_OPERATION); return GL_FALSE; }
    GLuint id = 0;
    switch (target) {
        case GL_ARRAY_BUFFER: id = g_bound_array_buffer; break;
        case GL_ELEMENT_ARRAY_BUFFER: id = g_bound_element_buffer; break;
        case GL_UNIFORM_BUFFER: id = g_bound_uniform_buffer; break;
        case GL_COPY_READ_BUFFER: id = g_bound_copy_read_buffer; break;
        case GL_COPY_WRITE_BUFFER: id = g_bound_copy_write_buffer; break;
        case GL_PIXEL_PACK_BUFFER: id = g_bound_pixel_pack_buffer; break;
        case GL_PIXEL_UNPACK_BUFFER: id = g_bound_pixel_unpack_buffer; break;
        default: break;
    }
    TraceBufferMutation("unmap", target, id, b->map_offset, 0, nullptr);
    b->mapped = false;
    b->map_writable = false;
    b->map_offset = 0;
    return GL_TRUE;  // host-coherent staging: nothing to flush
}

void APIENTRY glFlushMappedBufferRange(GLenum target, GLintptr offset,
                                       GLsizeiptr length) {
    GLenum err = GL_NO_ERROR;
    BufferData* b = BoundBufferForTarget(target, &err);
    if (err) { PUSH_ERROR(err); return; }
    if (offset < 0 || length < 0 ||
        offset + length > (GLintptr)b->data.size()) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    ++b->content_version;
    b->defined = true;
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
    a.size = size;
    a.type = type;
    a.normalized = normalized;
    a.stride = stride;
    a.offset = (GLsizeiptr)pointer;
    a.buffer = g_bound_array_buffer;
    a.is_pointer = true;
    a.integer = false;
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
    a.size = size;
    a.type = type;
    a.normalized = GL_FALSE;
    a.stride = stride;
    a.offset = (GLsizeiptr)pointer;
    a.buffer = g_bound_array_buffer;
    a.is_pointer = true;
    a.integer = true;
}

void APIENTRY glVertexAttribDivisor(GLuint index, GLuint divisor) {
    if (index >= kMaxAttribs) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    g_vaos[g_bound_vao].attribs[index].divisor = divisor;
}

// ---- generic (constant) vertex attributes -----------------------------------

// Constant values apply when the array is *disabled*; setting them must not
// change the enable bit or the array pointer/format state (GL 4.46).
void SetConstantAttrib(GLuint index, const GLfloat* v, GLsizei n) {
    if (index >= kMaxAttribs || n < 1 || n > 4) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    CurrentAttribData& a = g_current_attribs[index];
    for (GLsizei i = 0; i < 4; ++i) {
        const GLfloat value = i < n ? v[i] : (i == 3 ? 1.0f : 0.0f);
        a.constant[i] = value;
        a.constant_sint[i] = FloatToSint(value);
        a.constant_uint[i] = FloatToUint(value);
    }
}

void SetConstantAttribI(GLuint index, const GLint* values, GLsizei count) {
    if (index >= kMaxAttribs || count < 1 || count > 4) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    CurrentAttribData& attribute = g_current_attribs[index];
    for (GLsizei component = 0; component < 4; ++component) {
        const GLint value = component < count
            ? values[component] : (component == 3 ? 1 : 0);
        attribute.constant_sint[component] = value;
        attribute.constant_uint[component] = static_cast<GLuint>(value);
        attribute.constant[component] = static_cast<GLfloat>(value);
    }
}

void SetConstantAttribUI(GLuint index, const GLuint* values, GLsizei count) {
    if (index >= kMaxAttribs || count < 1 || count > 4) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    CurrentAttribData& attribute = g_current_attribs[index];
    for (GLsizei component = 0; component < 4; ++component) {
        const GLuint value = component < count
            ? values[component] : (component == 3 ? 1u : 0u);
        attribute.constant_uint[component] = value;
        attribute.constant_sint[component] = static_cast<GLint>(value);
        attribute.constant[component] = static_cast<GLfloat>(value);
    }
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
void APIENTRY glVertexAttrib4Nbv(GLuint index, const GLbyte* v) { GLfloat f[4]; for (int i=0;i<4;++i) f[i]=std::max(-1.0f, (GLfloat)v[i]/127.0f); SetConstantAttrib(index, f, 4); }
void APIENTRY glVertexAttrib4Nsv(GLuint index, const GLshort* v) { GLfloat f[4]; for (int i=0;i<4;++i) f[i]=std::max(-1.0f, (GLfloat)v[i]/32767.0f); SetConstantAttrib(index, f, 4); }
void APIENTRY glVertexAttrib4Niv(GLuint index, const GLint* v) { GLfloat f[4]; for (int i=0;i<4;++i) f[i]=std::max(-1.0f, (GLfloat)v[i]/2147483647.0f); SetConstantAttrib(index, f, 4); }
void APIENTRY glVertexAttrib4Nubv(GLuint index, const GLubyte* v) { GLfloat f[4]; for (int i=0;i<4;++i) f[i]=(GLfloat)v[i]/255.0f; SetConstantAttrib(index, f, 4); }
void APIENTRY glVertexAttrib4Nusv(GLuint index, const GLushort* v) { GLfloat f[4]; for (int i=0;i<4;++i) f[i]=(GLfloat)v[i]/65535.0f; SetConstantAttrib(index, f, 4); }
void APIENTRY glVertexAttrib4Nuiv(GLuint index, const GLuint* v) { GLfloat f[4]; for (int i=0;i<4;++i) f[i]=(GLfloat)v[i]/4294967295.0f; SetConstantAttrib(index, f, 4); }

void APIENTRY glVertexAttribI1i(GLuint index, GLint x) {
    const GLint values[1] = {x}; SetConstantAttribI(index, values, 1); }
void APIENTRY glVertexAttribI2i(GLuint index, GLint x, GLint y) {
    const GLint values[2] = {x, y}; SetConstantAttribI(index, values, 2); }
void APIENTRY glVertexAttribI3i(GLuint index, GLint x, GLint y, GLint z) {
    const GLint values[3] = {x, y, z}; SetConstantAttribI(index, values, 3); }
void APIENTRY glVertexAttribI4i(GLuint index, GLint x, GLint y, GLint z, GLint w) {
    const GLint values[4] = {x, y, z, w}; SetConstantAttribI(index, values, 4); }
void APIENTRY glVertexAttribI1iv(GLuint index, const GLint* values) {
    SetConstantAttribI(index, values, 1); }
void APIENTRY glVertexAttribI2iv(GLuint index, const GLint* values) {
    SetConstantAttribI(index, values, 2); }
void APIENTRY glVertexAttribI3iv(GLuint index, const GLint* values) {
    SetConstantAttribI(index, values, 3); }
void APIENTRY glVertexAttribI4iv(GLuint index, const GLint* values) {
    SetConstantAttribI(index, values, 4); }

void APIENTRY glVertexAttribI1ui(GLuint index, GLuint x) {
    const GLuint values[1] = {x}; SetConstantAttribUI(index, values, 1); }
void APIENTRY glVertexAttribI2ui(GLuint index, GLuint x, GLuint y) {
    const GLuint values[2] = {x, y}; SetConstantAttribUI(index, values, 2); }
void APIENTRY glVertexAttribI3ui(GLuint index, GLuint x, GLuint y, GLuint z) {
    const GLuint values[3] = {x, y, z}; SetConstantAttribUI(index, values, 3); }
void APIENTRY glVertexAttribI4ui(GLuint index, GLuint x, GLuint y, GLuint z,
                                 GLuint w) {
    const GLuint values[4] = {x, y, z, w}; SetConstantAttribUI(index, values, 4); }
void APIENTRY glVertexAttribI1uiv(GLuint index, const GLuint* values) {
    SetConstantAttribUI(index, values, 1); }
void APIENTRY glVertexAttribI2uiv(GLuint index, const GLuint* values) {
    SetConstantAttribUI(index, values, 2); }
void APIENTRY glVertexAttribI3uiv(GLuint index, const GLuint* values) {
    SetConstantAttribUI(index, values, 3); }
void APIENTRY glVertexAttribI4uiv(GLuint index, const GLuint* values) {
    SetConstantAttribUI(index, values, 4); }

void APIENTRY glVertexAttribI4bv(GLuint index, const GLbyte* values) {
    GLint converted[4];
    for (int i = 0; i < 4; ++i) converted[i] = values[i];
    SetConstantAttribI(index, converted, 4);
}
void APIENTRY glVertexAttribI4sv(GLuint index, const GLshort* values) {
    GLint converted[4];
    for (int i = 0; i < 4; ++i) converted[i] = values[i];
    SetConstantAttribI(index, converted, 4);
}
void APIENTRY glVertexAttribI4ubv(GLuint index, const GLubyte* values) {
    GLuint converted[4];
    for (int i = 0; i < 4; ++i) converted[i] = values[i];
    SetConstantAttribUI(index, converted, 4);
}
void APIENTRY glVertexAttribI4usv(GLuint index, const GLushort* values) {
    GLuint converted[4];
    for (int i = 0; i < 4; ++i) converted[i] = values[i];
    SetConstantAttribUI(index, converted, 4);
}

// ---- packed generic current attributes ------------------------------------

// GL 3.3's VertexAttribP* entry points always produce floating-point current
// attributes.  The suffix selects how many leading components are consumed
// from the 2_10_10_10_REV word; unconsumed components retain the conventional
// (0, 0, 0, 1) expansion rather than leaking the remaining packed fields.
static GLint SignExtendPacked(GLuint value, GLuint bits) {
    const GLuint modulus = 1u << bits;
    const GLuint sign = 1u << (bits - 1u);
    value &= modulus - 1u;
    return (value & sign)
        ? static_cast<GLint>(value) - static_cast<GLint>(modulus)
        : static_cast<GLint>(value);
}

static void SetConstantAttribP(GLuint index, GLenum type,
                               GLboolean normalized, GLuint packed,
                               GLsizei component_count) {
    if (index >= kMaxAttribs) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    if (type != GL_INT_2_10_10_10_REV &&
        type != GL_UNSIGNED_INT_2_10_10_10_REV) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }

    const GLuint fields[4] = {
        packed & 0x3FFu,
        (packed >> 10u) & 0x3FFu,
        (packed >> 20u) & 0x3FFu,
        (packed >> 30u) & 0x3u,
    };
    GLfloat decoded[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    for (GLsizei component = 0; component < component_count; ++component) {
        const GLuint bits = component == 3 ? 2u : 10u;
        if (type == GL_UNSIGNED_INT_2_10_10_10_REV) {
            decoded[component] = normalized
                ? static_cast<GLfloat>(fields[component]) /
                    static_cast<GLfloat>((1u << bits) - 1u)
                : static_cast<GLfloat>(fields[component]);
        } else {
            const GLint value = SignExtendPacked(fields[component], bits);
            decoded[component] = normalized
                ? std::max(-1.0f,
                    static_cast<GLfloat>(value) /
                    static_cast<GLfloat>((1u << (bits - 1u)) - 1u))
                : static_cast<GLfloat>(value);
        }
    }
    SetConstantAttrib(index, decoded, component_count);
}

void APIENTRY glVertexAttribP1ui(GLuint index, GLenum type,
                                 GLboolean normalized, GLuint value) {
    SetConstantAttribP(index, type, normalized, value, 1);
}
void APIENTRY glVertexAttribP2ui(GLuint index, GLenum type,
                                 GLboolean normalized, GLuint value) {
    SetConstantAttribP(index, type, normalized, value, 2);
}
void APIENTRY glVertexAttribP3ui(GLuint index, GLenum type,
                                 GLboolean normalized, GLuint value) {
    SetConstantAttribP(index, type, normalized, value, 3);
}
void APIENTRY glVertexAttribP4ui(GLuint index, GLenum type,
                                 GLboolean normalized, GLuint value) {
    SetConstantAttribP(index, type, normalized, value, 4);
}
void APIENTRY glVertexAttribP1uiv(GLuint index, GLenum type,
                                  GLboolean normalized, const GLuint* value) {
    SetConstantAttribP(index, type, normalized, *value, 1);
}
void APIENTRY glVertexAttribP2uiv(GLuint index, GLenum type,
                                  GLboolean normalized, const GLuint* value) {
    SetConstantAttribP(index, type, normalized, *value, 2);
}
void APIENTRY glVertexAttribP3uiv(GLuint index, GLenum type,
                                  GLboolean normalized, const GLuint* value) {
    SetConstantAttribP(index, type, normalized, *value, 3);
}
void APIENTRY glVertexAttribP4uiv(GLuint index, GLenum type,
                                  GLboolean normalized, const GLuint* value) {
    SetConstantAttribP(index, type, normalized, *value, 4);
}

// ---- attribute queries ------------------------------------------------------

void GetConstantAttrib(const CurrentAttribData& a, GLfloat* out) {
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
        case GL_VERTEX_ATTRIB_ARRAY_INTEGER: params[0] = a.integer ? 1.f : 0.f; break;
        case GL_CURRENT_VERTEX_ATTRIB:
            GetConstantAttrib(g_current_attribs[index], params); break;
        default: PUSH_ERROR(GL_INVALID_ENUM);
    }
}
void APIENTRY glGetVertexAttribdv(GLuint index, GLenum pname, GLdouble* params) {
    GLfloat f[4]; glGetVertexAttribfv(index, pname, f);
    int n = (pname == GL_VERTEX_ATTRIB_ARRAY_ENABLED || pname == GL_VERTEX_ATTRIB_ARRAY_SIZE ||
             pname == GL_VERTEX_ATTRIB_ARRAY_STRIDE || pname == GL_VERTEX_ATTRIB_ARRAY_TYPE ||
             pname == GL_VERTEX_ATTRIB_ARRAY_NORMALIZED ||
             pname == GL_VERTEX_ATTRIB_ARRAY_INTEGER) ? 1 : 4;
    for (int i = 0; i < n; ++i) params[i] = (GLdouble)f[i];
}
void APIENTRY glGetVertexAttribiv(GLuint index, GLenum pname, GLint* params) {
    GLfloat f[4]; glGetVertexAttribfv(index, pname, f);
    int n = (pname == GL_VERTEX_ATTRIB_ARRAY_ENABLED || pname == GL_VERTEX_ATTRIB_ARRAY_SIZE ||
             pname == GL_VERTEX_ATTRIB_ARRAY_STRIDE || pname == GL_VERTEX_ATTRIB_ARRAY_TYPE ||
             pname == GL_VERTEX_ATTRIB_ARRAY_NORMALIZED ||
             pname == GL_VERTEX_ATTRIB_ARRAY_INTEGER) ? 1 : 4;
    for (int i = 0; i < n; ++i) params[i] = (GLint)f[i];
}
void APIENTRY glGetVertexAttribIiv(GLuint index, GLenum pname, GLint* params) {
    if (index >= kMaxAttribs) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (pname == GL_CURRENT_VERTEX_ATTRIB) {
        const CurrentAttribData& attribute = g_current_attribs[index];
        for (int component = 0; component < 4; ++component)
            params[component] = attribute.constant_sint[component];
        return;
    }
    GLfloat f[4]; glGetVertexAttribfv(index, pname, f);
    int n = (pname == GL_VERTEX_ATTRIB_ARRAY_ENABLED || pname == GL_VERTEX_ATTRIB_ARRAY_SIZE ||
             pname == GL_VERTEX_ATTRIB_ARRAY_STRIDE || pname == GL_VERTEX_ATTRIB_ARRAY_TYPE ||
             pname == GL_VERTEX_ATTRIB_ARRAY_NORMALIZED ||
             pname == GL_VERTEX_ATTRIB_ARRAY_INTEGER) ? 1 : 4;
    for (int i = 0; i < n; ++i) params[i] = (GLint)f[i];
}
void APIENTRY glGetVertexAttribIuiv(GLuint index, GLenum pname, GLuint* params) {
    if (index >= kMaxAttribs) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (pname == GL_CURRENT_VERTEX_ATTRIB) {
        const CurrentAttribData& attribute = g_current_attribs[index];
        for (int component = 0; component < 4; ++component)
            params[component] = attribute.constant_uint[component];
        return;
    }
    GLfloat f[4]; glGetVertexAttribfv(index, pname, f);
    int n = (pname == GL_VERTEX_ATTRIB_ARRAY_ENABLED || pname == GL_VERTEX_ATTRIB_ARRAY_SIZE ||
             pname == GL_VERTEX_ATTRIB_ARRAY_STRIDE || pname == GL_VERTEX_ATTRIB_ARRAY_TYPE ||
             pname == GL_VERTEX_ATTRIB_ARRAY_NORMALIZED ||
             pname == GL_VERTEX_ATTRIB_ARRAY_INTEGER) ? 1 : 4;
    for (int i = 0; i < n; ++i) params[i] = (GLuint)f[i];
}
void APIENTRY glGetVertexAttribPointerv(GLuint index, GLenum pname, void** pointer) {
    if (index >= kMaxAttribs) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (pname != GL_VERTEX_ATTRIB_ARRAY_POINTER) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    *pointer = (void*)(uintptr_t)g_vaos[g_bound_vao].attribs[index].offset;
}

} // extern "C"
