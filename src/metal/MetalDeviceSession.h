#pragma once

#include "engine.h"

#include <mutex>

namespace mithril::metal {

// Process-level DirectMetal device/session boundary.
//
// The mature renderer still lives in engine.mm, but the GL/backend layer no
// longer reaches that global API directly. Every production DirectMetal call
// passes through this object, creating one ownership seam for future device,
// queue, frame-scheduler and deferred-retirement migration without rewriting
// the existing GL state/resource implementation. It deliberately models the
// current process-level renderer; it does not claim multi-device semantics.
class MetalDeviceSession final {
public:
    static MetalDeviceSession& shared() {
        static MetalDeviceSession session;
        return session;
    }

    MetalDeviceSession(const MetalDeviceSession&) = delete;
    MetalDeviceSession& operator=(const MetalDeviceSession&) = delete;

    bool EnsureInit() {
        if (metal::IsInitialized()) return true;
        std::lock_guard<std::mutex> lock(initializationMutex_);
        return metal::IsInitialized() || metal::EnsureInit();
    }
    bool IsInitialized() const { return metal::IsInitialized(); }
    bool SetTargetSize(uint32_t w, uint32_t h) { return metal::SetTargetSize(w, h); }
    bool SetNativeWindow(void* window) { return metal::SetNativeWindow(window); }
    bool Present() { return metal::Present(); }
    uint32_t TargetWidth() const { return metal::TargetWidth(); }
    uint32_t TargetHeight() const { return metal::TargetHeight(); }
    uint32_t MaxColorTextureSamples() const { return metal::MaxColorTextureSamples(); }
    bool SupportsDepthTextures() const { return metal::SupportsDepthTextures(); }
    bool Clear(const backend::ClearParams& params) { return metal::Clear(params); }

    uint64_t CreateProgram(const std::vector<uint32_t>& vs,
                           const std::vector<uint32_t>& fs) {
        return metal::CreateProgram(vs, fs);
    }
    void DestroyProgram(uint64_t program) { metal::DestroyProgram(program); }
    bool Draw(const backend::DrawParams& params) { return metal::Draw(params); }
    void SubmitFlush(bool wait) { metal::SubmitFlush(wait); }
    void ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, void* out) {
        metal::ReadPixels(x, y, width, height, out);
    }

    uint64_t CreateFence() { return metal::CreateFence(); }
    void DestroyFence(uint64_t fence) { metal::DestroyFence(fence); }
    backend::SyncWaitResult ClientWaitFence(uint64_t fence, uint64_t timeout) {
        return metal::ClientWaitFence(fence, timeout);
    }
    bool FenceSignaled(uint64_t fence) { return metal::FenceSignaled(fence); }
    bool ServerWaitFence(uint64_t fence) { return metal::ServerWaitFence(fence); }

    uint64_t CreateOcclusionQuery(bool booleanResult) {
        return metal::CreateOcclusionQuery(booleanResult);
    }
    void EndOcclusionQuery(uint64_t query) { metal::EndOcclusionQuery(query); }
    void DestroyOcclusionQuery(uint64_t query) { metal::DestroyOcclusionQuery(query); }
    bool OcclusionQueryAvailable(uint64_t query) {
        return metal::OcclusionQueryAvailable(query);
    }
    bool GetOcclusionQueryResult(uint64_t query, uint64_t* result) {
        return metal::GetOcclusionQueryResult(query, result);
    }

    void UploadTexture(uint64_t id, const backend::TexUpload& image) {
        metal::UploadTexture(id, image);
    }
    void DestroyResidentTexture(uint64_t id) { metal::DestroyResidentTexture(id); }
    void DestroyBuffer(uint64_t id) { metal::DestroyBuffer(id); }
    void CreateRenderbuffer(uint64_t id, GLenum format,
                            uint32_t width, uint32_t height, uint32_t samples) {
        metal::CreateRenderbuffer(id, format, width, height, samples);
    }
    void DestroyRenderbuffer(uint64_t id) { metal::DestroyRenderbuffer(id); }
    void SetFramebuffer(uint64_t id, const backend::FboSpec& spec) {
        metal::SetFramebuffer(id, spec);
    }
    void DestroyFramebuffer(uint64_t id) { metal::DestroyFramebuffer(id); }
    void BindDrawFramebuffer(uint64_t id) { metal::BindDrawFramebuffer(id); }
    void BindReadFramebuffer(uint64_t id) { metal::BindReadFramebuffer(id); }
    uint32_t DrawTargetWidth() const { return metal::DrawTargetWidth(); }
    uint32_t DrawTargetHeight() const { return metal::DrawTargetHeight(); }
    void RefreshReadback() { metal::RefreshReadback(); }
    void BlitFramebuffer(uint64_t src, uint64_t dst,
                         GLint sx0, GLint sy0, GLint sx1, GLint sy1,
                         GLint dx0, GLint dy0, GLint dx1, GLint dy1,
                         GLbitfield mask, GLenum filter) {
        metal::BlitFramebuffer(src, dst, sx0, sy0, sx1, sy1,
                               dx0, dy0, dx1, dy1, mask, filter);
    }

private:
    MetalDeviceSession() = default;
    mutable std::mutex initializationMutex_;
};

} // namespace mithril::metal
