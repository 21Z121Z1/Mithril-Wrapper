// Mithril-Wrapper GL layer internal shared state (split across the
// per-domain translation units: state.cpp / shader.cpp / vertex.cpp /
// draw.cpp). Each TU owns one domain and exposes its entry points through
// its own `extern "C"` block, so the MGL_IMPL list in gen_gl_stubs.py keeps
// excluding exactly the same symbols.
//
// Only the definitions actually shared between two or more TUs live here:
//  - object tables for VAO/VBO state (vertex + draw path),
//  - the lazy program->backend-program map (shader + draw path),
//  - attribute fetch helpers (draw path, called through the header below).

#pragma once

// glcorearb.h hides the APIENTRY prototypes behind GL_GLEXT_PROTOTYPES;
// every GL-layer TU defines and calls gl* entry points, so expose them.
#define GL_GLEXT_PROTOTYPES 1

#include <GL/glcorearb.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <shader/shader.h>
#include <state/state.h>
#include <backend/backend.h>

namespace s = mithril::state;
namespace v = mithril::backend;

#define PUSH_ERROR(e) s::GetState().errors.Push((e))

// ---- shared vertex attribute state (vertex.cpp owns the storage) --------

constexpr GLuint kMaxAttribs = 16;

// One enabled/constant vertex attribute slot.
struct AttribData {
    bool enabled = false;
    GLint size = 0;            // 1..4 components
    GLenum type = GL_FLOAT;
    GLboolean normalized = GL_FALSE;
    GLsizei stride = 0;
    GLsizeiptr offset = 0;
    GLuint buffer = 0;         // GL_ARRAY_BUFFER bound at glVertexAttribPointer
    GLuint divisor = 0;        // glVertexAttribDivisor
    bool is_pointer = false;   // array (pointer) vs generic constant value
    std::array<GLfloat, 4> constant{0.0f, 0.0f, 0.0f, 1.0f};
};

struct VAOData {
    std::array<AttribData, kMaxAttribs> attribs{};
};

struct BufferData {
    std::vector<uint8_t> data;
};

// Storage lives in vertex.cpp; the draw path reads these through the header.
extern std::unordered_map<GLuint, VAOData> g_vaos;
extern std::unordered_map<GLuint, BufferData> g_buffers;
extern GLuint g_bound_vao;           // default VAO is 0
extern GLuint g_bound_array_buffer;
extern GLuint g_bound_element_buffer;

// GL program id -> selected-backend program handle (created on first draw).
extern std::unordered_map<GLuint, uint64_t> g_backend_programs;

// ---- shared texture state (texture.cpp owns the storage) ------------------

// Mirrors the backend contract's texture-unit count; GL uses units
// beyond this only as a no-op.
constexpr GLuint kMaxTexUnits = v::kMaxTextureUnits;

// CPU-side image of one GL texture object (M4). Pixels are kept as an RGBA8
// mip chain so TexSubImage/GenerateMipmap can rebuild the upload cheaply.
// Layered targets concatenate their slices inside each level buffer in the
// same order backend::TexUpload expects (3D: z; array: layers; cube: 6 faces).
struct TexState {
    GLenum target = GL_TEXTURE_2D;       // target bound at creation
    GLenum min_filter = GL_LINEAR;       // sampler state (GL enums)
    GLenum mag_filter = GL_LINEAR;
    GLenum wrap_s = GL_REPEAT, wrap_t = GL_REPEAT, wrap_r = GL_REPEAT;
    uint32_t width = 0, height = 0, depth = 1;  // depth: 3D z / array layers
    std::vector<std::vector<uint8_t>> mip;      // [level] slices concatenated
    bool has_image = false;                    // level 0 present
    // Compressed mirror: raw S3TC bytes per level (kept for GetCompressedTexImage).
    std::vector<std::vector<uint8_t>> comp;    // comp[level], empty = none
    bool has_comp = false;
    GLint comp_format = 0;
    // glTexBuffer attachment (buffer texture; mirror holds a 1xN 2D image).
    bool has_tex_buffer = false;
    GLuint tex_buffer = 0;
    GLenum tex_buffer_format = 0;

    bool IsCube() const { return target == GL_TEXTURE_CUBE_MAP; }
    bool Is3D() const { return target == GL_TEXTURE_3D; }
    size_t SliceCount() const {
        if (IsCube()) return 6;
        if (Is3D() || target == GL_TEXTURE_2D_ARRAY ||
            target == GL_TEXTURE_1D_ARRAY)
            return depth;
        return 1;
    }
    size_t SliceBytes(uint32_t level) const {
        uint32_t w = std::max<uint32_t>(1, width >> level);
        uint32_t h = std::max<uint32_t>(1, height >> level);
        return (size_t)w * h * 4;
    }
};

extern std::unordered_map<GLuint, TexState> g_textures;
extern std::array<GLuint, kMaxTexUnits> g_texture_units;  // unit -> texture id
extern GLuint g_next_texture;

// Texture ids whose CPU mirror changed while the backend was not yet
// initialized; flushed to the engine at the next draw (see DrawCommon).
extern std::unordered_set<GLuint> g_dirty_textures;
void FlushDirtyTextureUploads();   // defined in texture.cpp

// ---- draw-time attribute fetch helpers (defined in draw.cpp) ------------

// IEEE 754 half (binary16) -> float.
float HalfToFloat(uint16_t h);

// Byte size of one component of `type` (0 for unknown types).
uint32_t AttribTypeSize(GLenum type);

// Read `count` components of `type` at `p` into float, honouring the
// normalized flag. Integer types read the raw integer value as float32
// (kept exact for |v| < 2^24, which covers MC's attribute usage).
void FetchComponents(const uint8_t* p, GLenum type, GLboolean normalized,
                     float* out, GLuint count);

// Snapshot the current GL context into the backend pipeline state (M5).
v::PipelineState BuildPipelineState();
