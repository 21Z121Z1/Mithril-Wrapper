#pragma once

#include <backend/types.h>

#include <cstdint>
#include <mutex>
#include <vector>

namespace mithril::metal {

// Process-level DirectMetal device/session boundary.
//
// The mature renderer still lives in engine.mm today, but the GL/backend layer
// no longer reaches that global engine API directly. All production calls pass
// through this object, giving subsequent work one ownership seam for device,
// queue, frame scheduling and deferred retirement without rewriting GL state or
// resource semantics. This is intentionally a narrow migration layer: it does
// not pretend the current renderer already supports multiple independent Metal
// devices/sessions.
class MetalDeviceSession final {
public:
    static MetalDeviceSession& shared();

    MetalDeviceSession(const MetalDeviceSession&) = delete;
    MetalDeviceSession& operator=(const MetalDeviceSession&) = delete;

    bool EnsureInit();
    bool IsInitialized() const;
    bool SetTargetSize(uint32_t width, uint32_t height);
    bool SetNativeWindow(void* nativeWindow);
    bool Present();
    uint32_t TargetWidth() const;
    uint32_t TargetHeight() const;
    uint32_t MaxColorTextureSamples() const;
    bool SupportsDepthTextures() const;
    bool Clear(const backend::ClearParams& params);

    uint64_t CreateProgram(const std::vector<uint32_t>& vertex,
                           const std::vector<uint32_t>& fragment);
    void DestroyProgram(uint64_t program);
    bool Draw(backend::DrawParams params);
    void SubmitFlush(bool waitForCompletion);
    void ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, void* out);

    uint64_t CreateFence();
    void DestroyFence(uint64_t fence);
    backend::SyncWaitResult ClientWaitFence(uint64_t fence, uint64_t timeoutNs);
    bool FenceSignaled(uint64_t fence);
    bool ServerWaitFence(uint64_t fence);

    uint64_t CreateOcclusionQuery(bool booleanResult);
    void EndOcclusionQuery(uint64_t query);
    void DestroyOcclusionQuery(uint64_t query);
    bool OcclusionQueryAvailable(uint64_t query);
    bool GetOcclusionQueryResult(uint64_t query, uint64_t* result);

    void UploadTexture(uint64_t id, const backend::TexUpload& image);
    void DestroyResidentTexture(uint64_t id);
    void DestroyBuffer(uint64_t lifetimeId);
    void CreateRenderbuffer(uint64_t id, GLenum format,
                            uint32_t width, uint32_t height, uint32_t samples);
    void DestroyRenderbuffer(uint64_t id);
    void SetFramebuffer(uint64_t id, const backend::FboSpec& spec);
    void DestroyFramebuffer(uint64_t id);
    void BindDrawFramebuffer(uint64_t id);
    void BindReadFramebuffer(uint64_t id);
    uint32_t DrawTargetWidth() const;
    uint32_t DrawTargetHeight() const;
    void RefreshReadback();
    void BlitFramebuffer(uint64_t src, uint64_t dst,
                         GLint sx0, GLint sy0, GLint sx1, GLint sy1,
                         GLint dx0, GLint dy0, GLint dx1, GLint dy1,
                         GLbitfield mask, GLenum filter);

private:
    MetalDeviceSession() = default;
    mutable std::mutex initializationMutex_;
};

} // namespace mithril::metal
