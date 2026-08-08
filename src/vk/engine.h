// Mithril-Wrapper Vulkan backend (milestone M2-VK, M3-VK).
//
// Owns the Vulkan device (loaded through dlopen so the dylib never exports
// vk* symbols), an offscreen render target, and the pipeline/descriptor
// machinery the GL layer feeds through src/gl/. The offscreen target is the
// seam where a swapchain (CAMetalLayer on iOS) lands later.
//
// The GL layer resolves VAO/VBO/vertex-attrib state into interleaved CPU
// payloads, so the engine stays free of GL object state:
//  - one per-vertex stream (binding 0),
//  - one optional per-instance stream (binding 1),
//  - one optional UINT32 index stream (everything else is expanded on CPU,
//    including UNSIGNED_BYTE/SHORT indices and baseVertex/baseInstance).

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <GL/glcorearb.h>

namespace mithril::vk {

// GL 3.3 core guarantees 16 fragment texture units; the engine mirrors that.
constexpr uint32_t kMaxUnits = 16;

// GL primitive modes we translate to Vulkan topology (GL_TRIANGLES/STRIP/FAN).
enum class Topology {
    Triangles = 0,
    TriangleStrip = 1,
    TriangleFan = 2,
};

// One enabled vertex location inside a stream.
struct VertexAttr {
    uint32_t location = 0;    // GL attribute location
    uint32_t components = 0;  // 1..4 (staged as float32)
    uint32_t offset = 0;      // byte offset within one interleaved record
};

// Interleaved float32 payload; every component is already converted on the
// CPU side (normalization, half/double->float), so the engine only sees
// R32G32B32A32-style formats.
struct VertexStream {
    std::vector<float> data;       // empty => no stream
    uint32_t stride = 0;           // bytes per record
    std::vector<VertexAttr> attrs; // sorted by offset, unique locations
};

// Everything one GL draw call needs. `uniforms` maps mithril_GlobalBlock
// member name -> flat float values. `sampler_binds` holds the active
// sampler assignments for this program: Vulkan descriptor binding ->
// GL texture id (0 = nothing bound, resolved to a 1x1 white fallback).
struct DrawParams {
    uint64_t program = 0;
    VertexStream vertex_stream;                    // binding 0 (per-vertex)
    VertexStream instance_stream;                  // binding 1 (per-instance)
    std::vector<uint32_t> indices;                 // empty => non-indexed
    uint32_t instance_count = 1;
    Topology topology = Topology::Triangles;
    std::unordered_map<std::string, std::vector<float>> uniforms;
    std::vector<std::pair<uint32_t, uint64_t>> sampler_binds; // (binding, gl id)
};

// GL texture mip chain ready for upload (M4). Each level is R8G8B8A8
// row-major, bottom-up (GL convention); mip[0] is the base level.
// Layered textures (3D / 2D array / cubemap) concatenate every "slice" of
// the level into one buffer: 3D stores z in order, arrays store the layers
// in order, cubemaps store the six faces in VkCubeMapFace order
// (POSITIVE_X, NEGATIVE_X, POSITIVE_Y, NEGATIVE_Y, POSITIVE_Z, NEGATIVE_Z).
// Each slice is `width*height*4` bytes; slice 0 starts at buffer offset 0.
struct TexUpload {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;    // 3D thickness / array layer count (1 for 2D/cube)
    bool is_3d = false;    // image type 3D (slices are the z axis)
    bool is_cube = false;  // six face slices, cube-compatible 2D array
    std::vector<std::vector<uint8_t>> mip;   // mip[level] pixels
};

// Upload (or replace) the image of texture `gl_id` from CPU pixels. The
// engine keeps the image resident until the next upload or teardown; sampler
// state (wrap/filter/mip mode) comes with it.
enum class TexFilter { Nearest = 0, Linear = 1 };
struct TexSamplerInfo {
    TexFilter mag = TexFilter::Linear;
    TexFilter min = TexFilter::Linear;
    bool mip = false;              // use mipmapped min filter
    GLenum wrap_s = GL_REPEAT, wrap_t = GL_REPEAT, wrap_r = GL_REPEAT;
};
void UploadTexture(uint64_t gl_id, const TexUpload& img,
                   const TexSamplerInfo& sampler);

// Drop the resident GPU image of `gl_id` (glDeleteTextures path). The next
// UploadTexture rebuilds it from scratch.
void DestroyResidentTexture(uint64_t gl_id);

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

// Record a draw into the pending frame (submitted by SubmitFlush).
void Draw(const DrawParams& params);

// Execute the pending frame (clear + draws). Blocks until the GPU is idle,
// then copies the frame into the readback buffer for ReadPixels.
void SubmitFlush();

// Copy a finished frame region (RGBA8, GL-style bottom-up origin) into `out`.
void ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, void* out);

} // namespace mithril::vk