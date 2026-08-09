// Backend selection and the backend-neutral renderer contract.

#pragma once

#include "types.h"

namespace mithril::backend {

enum class Kind { Vulkan, DirectMetal, Unavailable };

Kind ActiveKind();
const char* RendererName();

bool EnsureInit();
bool IsInitialized();
bool SetTargetSize(uint32_t w, uint32_t h);
bool SetNativeWindow(void* native_window);
bool SwapBuffers();
uint32_t TargetWidth();
uint32_t TargetHeight();
bool Clear(const ClearParams& params);

uint64_t CreateProgram(const std::vector<uint32_t>& vs,
                       const std::vector<uint32_t>& fs);
void DestroyProgram(uint64_t program);
bool Draw(const DrawParams& params);

// wait_for_completion=false is the glFlush/present path. A backend may keep
// work in flight. glFinish/readback pass true and establish CPU visibility.
void SubmitFlush(bool wait_for_completion);
void ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, void* out);

void UploadTexture(uint64_t gl_id, const TexUpload& img);
void DestroyResidentTexture(uint64_t gl_id);
void DestroyBuffer(uint64_t lifetime_id);
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

} // namespace mithril::backend
