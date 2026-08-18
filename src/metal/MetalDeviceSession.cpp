#include "MetalDeviceSession.h"

#include "engine.h"

#include <utility>

namespace mithril::metal {

MetalDeviceSession& MetalDeviceSession::shared() {
    static MetalDeviceSession session;
    return session;
}

bool MetalDeviceSession::EnsureInit() {
    if (metal::IsInitialized()) return true;
    std::lock_guard<std::mutex> lock(initializationMutex_);
    return metal::IsInitialized() || metal::EnsureInit();
}

bool MetalDeviceSession::IsInitialized() const { return metal::IsInitialized(); }
bool MetalDeviceSession::SetTargetSize(uint32_t w, uint32_t h) { return metal::SetTargetSize(w, h); }
bool MetalDeviceSession::SetNativeWindow(void* window) { return metal::SetNativeWindow(window); }
bool MetalDeviceSession::Present() { return metal::Present(); }
uint32_t MetalDeviceSession::TargetWidth() const { return metal::TargetWidth(); }
uint32_t MetalDeviceSession::TargetHeight() const { return metal::TargetHeight(); }
uint32_t MetalDeviceSession::MaxColorTextureSamples() const { return metal::MaxColorTextureSamples(); }
bool MetalDeviceSession::SupportsDepthTextures() const { return metal::SupportsDepthTextures(); }
bool MetalDeviceSession::Clear(const backend::ClearParams& params) { return metal::Clear(params); }

uint64_t MetalDeviceSession::CreateProgram(
    const std::vector<uint32_t>& vs, const std::vector<uint32_t>& fs,
    const std::vector<std::string>& uniform_names) {
    return metal::CreateProgram(vs, fs, uniform_names);
}
void MetalDeviceSession::DestroyProgram(uint64_t program) { metal::DestroyProgram(program); }
bool MetalDeviceSession::Draw(backend::DrawParams params) { return metal::Draw(std::move(params)); }
void MetalDeviceSession::SubmitFlush(bool wait) { metal::SubmitFlush(wait); }
void MetalDeviceSession::ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, void* out) {
    metal::ReadPixels(x, y, width, height, out);
}

uint64_t MetalDeviceSession::CreateFence() { return metal::CreateFence(); }
void MetalDeviceSession::DestroyFence(uint64_t fence) { metal::DestroyFence(fence); }
backend::SyncWaitResult MetalDeviceSession::ClientWaitFence(uint64_t fence, uint64_t timeout) {
    return metal::ClientWaitFence(fence, timeout);
}
bool MetalDeviceSession::FenceSignaled(uint64_t fence) { return metal::FenceSignaled(fence); }
bool MetalDeviceSession::ServerWaitFence(uint64_t fence) { return metal::ServerWaitFence(fence); }

uint64_t MetalDeviceSession::CreateOcclusionQuery(bool booleanResult) {
    return metal::CreateOcclusionQuery(booleanResult);
}
void MetalDeviceSession::EndOcclusionQuery(uint64_t query) { metal::EndOcclusionQuery(query); }
void MetalDeviceSession::DestroyOcclusionQuery(uint64_t query) { metal::DestroyOcclusionQuery(query); }
bool MetalDeviceSession::OcclusionQueryAvailable(uint64_t query) {
    return metal::OcclusionQueryAvailable(query);
}
bool MetalDeviceSession::GetOcclusionQueryResult(uint64_t query, uint64_t* result) {
    return metal::GetOcclusionQueryResult(query, result);
}

void MetalDeviceSession::UploadTexture(uint64_t id, const backend::TexUpload& image) {
    metal::UploadTexture(id, image);
}
void MetalDeviceSession::DestroyResidentTexture(uint64_t id) { metal::DestroyResidentTexture(id); }
void MetalDeviceSession::DestroyBuffer(uint64_t id) { metal::DestroyBuffer(id); }
void MetalDeviceSession::CreateRenderbuffer(uint64_t id, GLenum format,
                                            uint32_t width, uint32_t height,
                                            uint32_t samples) {
    metal::CreateRenderbuffer(id, format, width, height, samples);
}
void MetalDeviceSession::DestroyRenderbuffer(uint64_t id) { metal::DestroyRenderbuffer(id); }
void MetalDeviceSession::SetFramebuffer(uint64_t id, const backend::FboSpec& spec) {
    metal::SetFramebuffer(id, spec);
}
void MetalDeviceSession::DestroyFramebuffer(uint64_t id) { metal::DestroyFramebuffer(id); }
void MetalDeviceSession::BindDrawFramebuffer(uint64_t id) { metal::BindDrawFramebuffer(id); }
void MetalDeviceSession::BindReadFramebuffer(uint64_t id) { metal::BindReadFramebuffer(id); }
uint32_t MetalDeviceSession::DrawTargetWidth() const { return metal::DrawTargetWidth(); }
uint32_t MetalDeviceSession::DrawTargetHeight() const { return metal::DrawTargetHeight(); }
void MetalDeviceSession::RefreshReadback() { metal::RefreshReadback(); }
void MetalDeviceSession::BlitFramebuffer(uint64_t src, uint64_t dst,
                                         GLint sx0, GLint sy0, GLint sx1, GLint sy1,
                                         GLint dx0, GLint dy0, GLint dx1, GLint dy1,
                                         GLbitfield mask, GLenum filter) {
    metal::BlitFramebuffer(src, dst, sx0, sy0, sx1, sy1,
                           dx0, dy0, dx1, dy1, mask, filter);
}

} // namespace mithril::metal
