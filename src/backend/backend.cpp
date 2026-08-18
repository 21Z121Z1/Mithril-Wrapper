#include "backend.h"

#if !defined(MITHRIL_DIRECT_ONLY)
#include <vk/engine.h>
#endif

#include <util/log.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <utility>

#if defined(MITHRIL_HAS_DIRECT_METAL)
#include <metal/MetalDeviceSession.h>
#endif

namespace mithril::backend {
namespace {

Kind ResolveKind() {
    const char* requested = std::getenv("MITHRIL_BACKEND");

#if defined(MITHRIL_DIRECT_ONLY)
    if (requested && *requested && std::strcmp(requested, "metal") != 0) {
        ML_LOG_ERROR("backend: '%s' requested from a DirectMetal-only build", requested);
        return Kind::Unavailable;
    }
#if defined(MITHRIL_HAS_DIRECT_METAL)
    return Kind::DirectMetal;
#else
    ML_LOG_ERROR("backend: DirectMetal-only build has no Metal backend");
    return Kind::Unavailable;
#endif
#else
    if (requested && std::strcmp(requested, "vulkan") == 0) return Kind::Vulkan;
    if (requested && std::strcmp(requested, "metal") == 0) {
#if defined(MITHRIL_HAS_DIRECT_METAL)
        return Kind::DirectMetal;
#else
        ML_LOG_ERROR("backend: DirectMetal requested but this build has no Metal backend");
        return Kind::Unavailable;
#endif
    }
    if (requested && *requested) {
        ML_LOG_ERROR("backend: unknown MITHRIL_BACKEND='%s'", requested);
        return Kind::Unavailable;
    }
#if defined(MITHRIL_HAS_DIRECT_METAL)
    return Kind::DirectMetal;
#else
    return Kind::Vulkan;
#endif
#endif
}

Kind SelectedKind() {
    static const Kind kind = ResolveKind();
    return kind;
}

#if defined(MITHRIL_HAS_DIRECT_METAL)
metal::MetalDeviceSession& DirectSession() {
    return metal::MetalDeviceSession::shared();
}
#endif

} // namespace

Kind ActiveKind() { return SelectedKind(); }

const char* RendererName() {
    switch (SelectedKind()) {
        case Kind::Vulkan: return "Mithril Vulkan reference backend";
        case Kind::DirectMetal: return "Mithril DirectMetal";
        default: return "Mithril backend unavailable";
    }
}

bool PackUniformValue(const UniformMemberLayout& layout,
                      const uint8_t* value_data, size_t value_size,
                      uint8_t* block, size_t block_size) {
    constexpr size_t kScalarBytes = sizeof(uint32_t);
    if (!block || layout.offset > block_size ||
        layout.size > block_size - layout.offset ||
        value_size % kScalarBytes != 0)
        return false;

    const size_t member_end = layout.offset + layout.size;
    const uint32_t rows = std::max(layout.vector_components, 1u);
    const uint32_t columns = std::max(layout.matrix_columns, 1u);
    const uint32_t elements = std::max(layout.array_elements, 1u);
    const size_t tight_element_bytes =
        static_cast<size_t>(rows) * columns * kScalarBytes;
    const size_t array_stride = layout.array_stride
        ? layout.array_stride : tight_element_bytes;
    const size_t matrix_stride = layout.matrix_stride
        ? layout.matrix_stride : static_cast<size_t>(rows) * kScalarBytes;

    auto copy_scalar = [&](size_t destination, size_t source) {
        if (source + kScalarBytes > value_size ||
            destination + kScalarBytes > member_end)
            return false;
        std::memcpy(block + destination, value_data + source, kScalarBytes);
        return true;
    };

    for (uint32_t element = 0; element < elements; ++element) {
        const size_t source_base =
            static_cast<size_t>(element) * tight_element_bytes;
        if (source_base >= value_size) break;
        const size_t destination_base =
            layout.offset + static_cast<size_t>(element) * array_stride;
        if (columns == 1) {
            const size_t bytes = std::min<size_t>(
                static_cast<size_t>(rows) * kScalarBytes,
                value_size - source_base);
            if (destination_base + bytes > member_end) return false;
            std::memcpy(block + destination_base,
                        value_data + source_base, bytes);
            continue;
        }
        if (!layout.row_major) {
            for (uint32_t column = 0; column < columns; ++column) {
                for (uint32_t row = 0; row < rows; ++row) {
                    const size_t scalar =
                        static_cast<size_t>(column) * rows + row;
                    if (!copy_scalar(
                            destination_base + column * matrix_stride +
                                row * kScalarBytes,
                            source_base + scalar * kScalarBytes))
                        return false;
                }
            }
        } else {
            for (uint32_t row = 0; row < rows; ++row) {
                for (uint32_t column = 0; column < columns; ++column) {
                    const size_t scalar =
                        static_cast<size_t>(column) * rows + row;
                    if (!copy_scalar(
                            destination_base + row * matrix_stride +
                                column * kScalarBytes,
                            source_base + scalar * kScalarBytes))
                        return false;
                }
            }
        }
    }
    return true;
}

bool EnsureInit() {
    switch (SelectedKind()) {
#if !defined(MITHRIL_DIRECT_ONLY)
        case Kind::Vulkan: return vk::EnsureInit();
#endif
#if defined(MITHRIL_HAS_DIRECT_METAL)
        case Kind::DirectMetal: return DirectSession().EnsureInit();
#endif
        default: return false;
    }
}

bool IsInitialized() {
    switch (SelectedKind()) {
#if !defined(MITHRIL_DIRECT_ONLY)
        case Kind::Vulkan: return vk::IsInitialized();
#endif
#if defined(MITHRIL_HAS_DIRECT_METAL)
        case Kind::DirectMetal: return DirectSession().IsInitialized();
#endif
        default: return false;
    }
}

#if !defined(MITHRIL_DIRECT_ONLY)
#define MITHRIL_VULKAN_RET_CASE(fn, ...) case Kind::Vulkan: return vk::fn(__VA_ARGS__);
#define MITHRIL_VULKAN_VOID_CASE(fn, ...) case Kind::Vulkan: vk::fn(__VA_ARGS__); return;
#else
#define MITHRIL_VULKAN_RET_CASE(fn, ...)
#define MITHRIL_VULKAN_VOID_CASE(fn, ...)
#endif

#if defined(MITHRIL_HAS_DIRECT_METAL)
#define MITHRIL_METAL_RET_CASE(fn, ...) case Kind::DirectMetal: return DirectSession().fn(__VA_ARGS__);
#define MITHRIL_METAL_VOID_CASE(fn, ...) case Kind::DirectMetal: DirectSession().fn(__VA_ARGS__); return;
#else
#define MITHRIL_METAL_RET_CASE(fn, ...)
#define MITHRIL_METAL_VOID_CASE(fn, ...)
#endif

#define DISPATCH_RET(fn, fallback, ...)                                      \
    do {                                                                      \
        switch (SelectedKind()) {                                             \
            MITHRIL_VULKAN_RET_CASE(fn, __VA_ARGS__)                          \
            MITHRIL_METAL_RET_CASE(fn, __VA_ARGS__)                           \
            default: return fallback;                                         \
        }                                                                     \
    } while (false)

#define DISPATCH_VOID(fn, ...)                                                \
    do {                                                                      \
        switch (SelectedKind()) {                                             \
            MITHRIL_VULKAN_VOID_CASE(fn, __VA_ARGS__)                         \
            MITHRIL_METAL_VOID_CASE(fn, __VA_ARGS__)                          \
            default: return;                                                  \
        }                                                                     \
    } while (false)

bool SetTargetSize(uint32_t w, uint32_t h) { DISPATCH_RET(SetTargetSize, false, w, h); }
bool SetNativeWindow(void* native_window) {
#if defined(MITHRIL_HAS_DIRECT_METAL)
    if (SelectedKind() == Kind::DirectMetal)
        return DirectSession().SetNativeWindow(native_window);
#endif
    (void)native_window;
    return true;
}
bool SwapBuffers() {
    switch (SelectedKind()) {
#if !defined(MITHRIL_DIRECT_ONLY)
        case Kind::Vulkan: vk::SubmitFlush(); return true;
#endif
#if defined(MITHRIL_HAS_DIRECT_METAL)
        case Kind::DirectMetal: return DirectSession().Present();
#endif
        default: return false;
    }
}
uint32_t TargetWidth() { DISPATCH_RET(TargetWidth, 0); }
uint32_t TargetHeight() { DISPATCH_RET(TargetHeight, 0); }
uint32_t MaxFramebufferSamples() {
    switch (SelectedKind()) {
#if !defined(MITHRIL_DIRECT_ONLY)
        case Kind::Vulkan: return vk::MaxFramebufferSamples();
#endif
#if defined(MITHRIL_HAS_DIRECT_METAL)
        case Kind::DirectMetal: return DirectSession().MaxColorTextureSamples();
#endif
        default: return 0;
    }
}
uint32_t MaxColorTextureSamples() {
    switch (SelectedKind()) {
#if !defined(MITHRIL_DIRECT_ONLY)
        case Kind::Vulkan: return 0;
#endif
#if defined(MITHRIL_HAS_DIRECT_METAL)
        case Kind::DirectMetal: return DirectSession().MaxColorTextureSamples();
#endif
        default: return 0;
    }
}
bool SupportsDepthTextures() {
    switch (SelectedKind()) {
#if !defined(MITHRIL_DIRECT_ONLY)
        case Kind::Vulkan: return false;
#endif
#if defined(MITHRIL_HAS_DIRECT_METAL)
        case Kind::DirectMetal: return DirectSession().SupportsDepthTextures();
#endif
        default: return false;
    }
}
bool Clear(const ClearParams& params) { DISPATCH_RET(Clear, false, params); }
uint64_t CreateProgram(const std::vector<uint32_t>& vs,
                       const std::vector<uint32_t>& fs,
                       const std::vector<std::string>& uniform_names) {
    DISPATCH_RET(CreateProgram, 0, vs, fs, uniform_names);
}
void DestroyProgram(uint64_t program) { DISPATCH_VOID(DestroyProgram, program); }

bool Draw(DrawParams params) {
    switch (SelectedKind()) {
#if !defined(MITHRIL_DIRECT_ONLY)
        case Kind::Vulkan: vk::Draw(params); return true;
#endif
#if defined(MITHRIL_HAS_DIRECT_METAL)
        case Kind::DirectMetal: return DirectSession().Draw(std::move(params));
#endif
        default: return false;
    }
}

void SubmitFlush(bool wait_for_completion) {
    switch (SelectedKind()) {
#if !defined(MITHRIL_DIRECT_ONLY)
        case Kind::Vulkan:
            (void)wait_for_completion;
            vk::SubmitFlush();
            return;
#endif
#if defined(MITHRIL_HAS_DIRECT_METAL)
        case Kind::DirectMetal:
            DirectSession().SubmitFlush(wait_for_completion);
            return;
#endif
        default: return;
    }
}

uint64_t CreateFence() { DISPATCH_RET(CreateFence, 0); }
void DestroyFence(uint64_t fence) { DISPATCH_VOID(DestroyFence, fence); }
SyncWaitResult ClientWaitFence(uint64_t fence, uint64_t timeout_ns) {
    DISPATCH_RET(ClientWaitFence, SyncWaitResult::Failed, fence, timeout_ns);
}
bool FenceSignaled(uint64_t fence) { DISPATCH_RET(FenceSignaled, false, fence); }
bool ServerWaitFence(uint64_t fence) { DISPATCH_RET(ServerWaitFence, false, fence); }

uint64_t CreateOcclusionQuery(bool boolean_result) {
    DISPATCH_RET(CreateOcclusionQuery, 0, boolean_result);
}
void EndOcclusionQuery(uint64_t query) { DISPATCH_VOID(EndOcclusionQuery, query); }
void DestroyOcclusionQuery(uint64_t query) { DISPATCH_VOID(DestroyOcclusionQuery, query); }
bool OcclusionQueryAvailable(uint64_t query) {
    DISPATCH_RET(OcclusionQueryAvailable, false, query);
}
bool GetOcclusionQueryResult(uint64_t query, uint64_t* result) {
    DISPATCH_RET(GetOcclusionQueryResult, false, query, result);
}

void ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, void* out) {
    switch (SelectedKind()) {
#if !defined(MITHRIL_DIRECT_ONLY)
        case Kind::Vulkan:
            vk::SubmitFlush();
            vk::ReadPixels(x, y, width, height, out);
            return;
#endif
#if defined(MITHRIL_HAS_DIRECT_METAL)
        case Kind::DirectMetal:
            DirectSession().ReadPixels(x, y, width, height, out);
            return;
#endif
        default: return;
    }
}
void UploadTexture(uint64_t id, const TexUpload& img) { DISPATCH_VOID(UploadTexture, id, img); }
void DestroyResidentTexture(uint64_t id) { DISPATCH_VOID(DestroyResidentTexture, id); }
void DestroyBuffer(uint64_t id) { DISPATCH_VOID(DestroyBuffer, id); }
void CreateRenderbuffer(uint64_t id, GLenum format, uint32_t w, uint32_t h, uint32_t samples) {
    DISPATCH_VOID(CreateRenderbuffer, id, format, w, h, samples);
}
void DestroyRenderbuffer(uint64_t id) { DISPATCH_VOID(DestroyRenderbuffer, id); }
void SetFramebuffer(uint64_t id, const FboSpec& spec) { DISPATCH_VOID(SetFramebuffer, id, spec); }
void DestroyFramebuffer(uint64_t id) { DISPATCH_VOID(DestroyFramebuffer, id); }
void BindDrawFramebuffer(uint64_t id) { DISPATCH_VOID(BindDrawFramebuffer, id); }
void BindReadFramebuffer(uint64_t id) { DISPATCH_VOID(BindReadFramebuffer, id); }
uint32_t DrawTargetWidth() { DISPATCH_RET(DrawTargetWidth, 0); }
uint32_t DrawTargetHeight() { DISPATCH_RET(DrawTargetHeight, 0); }
void RefreshReadback() { DISPATCH_VOID(RefreshReadback); }
void BlitFramebuffer(uint64_t src, uint64_t dst,
                     GLint sx0, GLint sy0, GLint sx1, GLint sy1,
                     GLint dx0, GLint dy0, GLint dx1, GLint dy1,
                     GLbitfield mask, GLenum filter) {
    DISPATCH_VOID(BlitFramebuffer, src, dst, sx0, sy0, sx1, sy1,
                  dx0, dy0, dx1, dy1, mask, filter);
}

#undef DISPATCH_RET
#undef DISPATCH_VOID
#undef MITHRIL_VULKAN_RET_CASE
#undef MITHRIL_VULKAN_VOID_CASE
#undef MITHRIL_METAL_RET_CASE
#undef MITHRIL_METAL_VOID_CASE

} // namespace mithril::backend
