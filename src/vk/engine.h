// Vulkan implementation of the backend-neutral renderer contract.

#pragma once

#include <backend/types.h>

namespace mithril::vk {

using backend::DrawParams;
using backend::ClearParams;
using backend::FboAttach;
using backend::FboSpec;
using backend::PipelineState;
using backend::TexFilter;
using backend::TexSamplerInfo;
using backend::TexUpload;
using backend::Topology;
using backend::VertexAttr;
using backend::VertexStream;

inline constexpr uint32_t kMaxUnits = backend::kMaxTextureUnits;

void UploadTexture(uint64_t gl_id, const TexUpload& img,
                   const TexSamplerInfo& sampler);
void UpdateTextureSampler(uint64_t gl_id, const TexSamplerInfo& sampler);
void DestroyResidentTexture(uint64_t gl_id);
void DestroyBuffer(uint64_t lifetime_id);
bool EnsureInit();
bool IsInitialized();
bool SetTargetSize(uint32_t w, uint32_t h);
uint32_t TargetWidth();
uint32_t TargetHeight();
bool Clear(const ClearParams& params);
uint64_t CreateProgram(const std::vector<uint32_t>& vs,
                       const std::vector<uint32_t>& fs);
void DestroyProgram(uint64_t program);
void Draw(const DrawParams& params);
void SubmitFlush();
void ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, void* out);
void CreateRenderbuffer(uint64_t rbo_id, GLenum internalformat,
                        uint32_t width, uint32_t height, uint32_t samples);
void DestroyRenderbuffer(uint64_t rbo_id);
void SetFramebuffer(uint64_t fbo_id, const FboSpec& spec);
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

} // namespace mithril::vk
