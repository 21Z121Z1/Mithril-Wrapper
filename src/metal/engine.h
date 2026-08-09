// Native Metal implementation of the backend-neutral renderer contract.

#pragma once

#include <backend/types.h>

namespace mithril::metal {

bool EnsureInit();
bool IsInitialized();
bool SetTargetSize(uint32_t w, uint32_t h);
bool SetNativeWindow(void* native_window);
bool Present();
uint32_t TargetWidth();
uint32_t TargetHeight();
void SetClearColor(float r, float g, float b, float a);
void SetClearMask(GLbitfield mask);
void SetClearDepth(double depth);
void SetClearStencil(GLint value);
void SetViewport(float x, float y, float w, float h);
void SetScissor(float x, float y, float w, float h);
uint64_t CreateProgram(const std::vector<uint32_t>& vs,
                       const std::vector<uint32_t>& fs);
void DestroyProgram(uint64_t program);
bool Draw(const backend::DrawParams& params);
void SubmitFlush(bool wait_for_completion);
void ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, void* out);

// The first DirectMetal slice deliberately does not claim texture/FBO support.
// These functions preserve backend state and emit an explicit diagnostic so a
// draw can fail rather than silently approximate the GL workload.
void UploadTexture(uint64_t gl_id, const backend::TexUpload& img,
                   const backend::TexSamplerInfo& sampler);
void UpdateTextureSampler(uint64_t gl_id,
                          const backend::TexSamplerInfo& sampler);
void DestroyResidentTexture(uint64_t gl_id);
void DestroyBuffer(uint64_t lifetime_id);
void CreateRenderbuffer(uint64_t rbo_id, GLenum internalformat,
                        uint32_t width, uint32_t height, uint32_t samples);
void DestroyRenderbuffer(uint64_t rbo_id);
void SetFramebuffer(uint64_t fbo_id, const backend::FboSpec& spec);
void DestroyFramebuffer(uint64_t fbo_id);
void BindDrawFramebuffer(uint64_t fbo_id);
void BindReadFramebuffer(uint64_t fbo_id);
uint32_t DrawTargetWidth();
uint32_t DrawTargetHeight();
void RefreshReadback();
void BlitFramebuffer(uint64_t src_fbo, uint64_t dst_fbo,
                     GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                     GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                     GLbitfield mask, GLenum filter);

} // namespace mithril::metal
