// Mithril-Wrapper Vulkan backend (milestone M2-VK).
//
// Owns the Vulkan device (loaded through dlopen so the dylib never exports
// vk* symbols), an offscreen render target, and the pipeline/descriptor
// machinery the GL layer feeds from gl_impl.cpp. The offscreen target is the
// seam where a swapchain (CAMetalLayer on iOS) lands later.
//
// The GL layer resolves VAO/VBO/vertex-attrib state into an interleaved CPU
// payload, so the engine stays free of GL object state.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <GL/glcorearb.h>

namespace mithril::vk {

// Lazily create loader + instance + device + offscreen target. Idempotent.
bool EnsureInit();
bool IsInitialized();

// Recreate the offscreen color target (usually once at startup).
bool SetTargetSize(uint32_t w, uint32_t h);
uint32_t TargetWidth();
uint32_t TargetHeight();

// Colour used by the next render pass clear (glClearColor/glClear).
void SetClearColor(float r, float g, float b, float a);
// Mark the next flush to clear the target (glClear(GL_COLOR_BUFFER_BIT)).
void MarkClear();
// Viewport in target coordinates (glViewport), clamped to the target.
void SetViewport(float x, float y, float w, float h);

// Feed SPIR-V for a linked program; returns a stable handle.
uint64_t CreateProgram(const std::vector<uint32_t>& vs,
                       const std::vector<uint32_t>& fs);
void DestroyProgram(uint64_t program);

// Record a GL draw. `vertices` is an interleaved CPU payload; `stride` is the
// per-vertex byte size. `attr_counts`/`attr_offsets` describe each enabled
// vertex location (component count, offset within the stride). `uniforms`
// maps mithril_GlobalBlock member name -> flat float values.
void DrawInterleaved(uint64_t program, const std::vector<float>& vertices,
                     uint32_t stride,
                     const std::vector<uint32_t>& attr_counts,
                     const std::vector<uint32_t>& attr_offsets,
                     const std::unordered_map<std::string,
                                              std::vector<float>>& uniforms);

// Execute the pending frame (clear + draws). Blocks until the GPU is idle,
// then copies the frame into the readback buffer for ReadPixels.
void SubmitFlush();

// Copy a finished frame region (RGBA8, GL-style bottom-up origin) into `out`.
void ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, void* out);

} // namespace mithril::vk