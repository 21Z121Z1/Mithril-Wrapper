#include "backend.h"

#include <vk/engine.h>

#include <util/log.h>

#include <cstdlib>
#include <cstring>

#if defined(MITHRIL_HAS_DIRECT_METAL)
#include <metal/engine.h>
#endif

namespace mithril::backend {
namespace {

Kind ResolveKind() {
    const char* requested = std::getenv("MITHRIL_BACKEND");
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
}

Kind SelectedKind() {
    static const Kind kind = ResolveKind();
    return kind;
}

} // namespace

Kind ActiveKind() { return SelectedKind(); }

const char* RendererName() {
    switch (SelectedKind()) {
        case Kind::Vulkan: return "Mithril Vulkan reference backend";
        case Kind::DirectMetal: return "Mithril DirectMetal";
        default: return "Mithril backend unavailable";
    }
}

bool EnsureInit() {
    switch (SelectedKind()) {
        case Kind::Vulkan: return vk::EnsureInit();
#if defined(MITHRIL_HAS_DIRECT_METAL)
        case Kind::DirectMetal: return metal::EnsureInit();
#endif
        default: return false;
    }
}

bool IsInitialized() {
    switch (SelectedKind()) {
        case Kind::Vulkan: return vk::IsInitialized();
#if defined(MITHRIL_HAS_DIRECT_METAL)
        case Kind::DirectMetal: return metal::IsInitialized();
#endif
        default: return false;
    }
}

#define DISPATCH_RET(fn, fallback, ...)                                      \
    do {                                                                      \
        switch (SelectedKind()) {                                             \
            case Kind::Vulkan: return vk::fn(__VA_ARGS__);                    \
            /* DirectMetal arm is supplied only in Apple builds. */           \
            MITHRIL_METAL_RET_CASE(fn, __VA_ARGS__)                           \
            default: return fallback;                                         \
        }                                                                     \
    } while (false)

#define DISPATCH_VOID(fn, ...)                                                \
    do {                                                                      \
        switch (SelectedKind()) {                                             \
            case Kind::Vulkan: vk::fn(__VA_ARGS__); return;                   \
            MITHRIL_METAL_VOID_CASE(fn, __VA_ARGS__)                          \
            default: return;                                                  \
        }                                                                     \
    } while (false)

#if defined(MITHRIL_HAS_DIRECT_METAL)
#define MITHRIL_METAL_RET_CASE(fn, ...) case Kind::DirectMetal: return metal::fn(__VA_ARGS__);
#define MITHRIL_METAL_VOID_CASE(fn, ...) case Kind::DirectMetal: metal::fn(__VA_ARGS__); return;
#else
#define MITHRIL_METAL_RET_CASE(fn, ...)
#define MITHRIL_METAL_VOID_CASE(fn, ...)
#endif

bool SetTargetSize(uint32_t w, uint32_t h) { DISPATCH_RET(SetTargetSize, false, w, h); }
uint32_t TargetWidth() { DISPATCH_RET(TargetWidth, 0); }
uint32_t TargetHeight() { DISPATCH_RET(TargetHeight, 0); }
void SetClearColor(float r, float g, float b, float a) { DISPATCH_VOID(SetClearColor, r, g, b, a); }
void SetClearMask(GLbitfield mask) { DISPATCH_VOID(SetClearMask, mask); }
void SetClearDepth(double depth) { DISPATCH_VOID(SetClearDepth, depth); }
void SetClearStencil(GLint value) { DISPATCH_VOID(SetClearStencil, value); }
void SetViewport(float x, float y, float w, float h) { DISPATCH_VOID(SetViewport, x, y, w, h); }
void SetScissor(float x, float y, float w, float h) { DISPATCH_VOID(SetScissor, x, y, w, h); }
uint64_t CreateProgram(const std::vector<uint32_t>& vs, const std::vector<uint32_t>& fs) {
    DISPATCH_RET(CreateProgram, 0, vs, fs);
}
void DestroyProgram(uint64_t program) { DISPATCH_VOID(DestroyProgram, program); }

bool Draw(const DrawParams& params) {
    switch (SelectedKind()) {
        case Kind::Vulkan: vk::Draw(params); return true;
#if defined(MITHRIL_HAS_DIRECT_METAL)
        case Kind::DirectMetal: return metal::Draw(params);
#endif
        default: return false;
    }
}

void SubmitFlush(bool wait_for_completion) {
    switch (SelectedKind()) {
        case Kind::Vulkan:
            (void)wait_for_completion;
            vk::SubmitFlush();
            return;
#if defined(MITHRIL_HAS_DIRECT_METAL)
        case Kind::DirectMetal:
            metal::SubmitFlush(wait_for_completion);
            return;
#endif
        default: return;
    }
}

void ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, void* out) {
    DISPATCH_VOID(ReadPixels, x, y, width, height, out);
}
void UploadTexture(uint64_t id, const TexUpload& img, const TexSamplerInfo& sampler) {
    DISPATCH_VOID(UploadTexture, id, img, sampler);
}
void DestroyResidentTexture(uint64_t id) { DISPATCH_VOID(DestroyResidentTexture, id); }
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
#undef MITHRIL_METAL_RET_CASE
#undef MITHRIL_METAL_VOID_CASE

} // namespace mithril::backend
