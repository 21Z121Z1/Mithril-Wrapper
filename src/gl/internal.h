// Mithril-Wrapper GL layer internal shared state (split across the
// per-domain translation units: state.cpp / shader.cpp / vertex.cpp /
// draw.cpp). Each TU owns one domain and exposes its entry points through
// its own `extern "C"` block, so the MGL_IMPL list in gen_gl_stubs.py keeps
// excluding exactly the same symbols.
//
// Only the definitions actually shared between two or more TUs live here:
//  - object tables for VAO/VBO state (vertex + draw path),
//  - the lazy program->Vulkan-program map (shader + draw path),
//  - attribute fetch helpers (draw path, called through the header below).

#pragma once

#include <GL/glcorearb.h>

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <shader/shader.h>
#include <state/state.h>
#include <vk/engine.h>

namespace s = mithril::state;
namespace v = mithril::vk;

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

// GL program id -> Vulkan program handle (lazily created at first draw).
extern std::unordered_map<GLuint, uint64_t> g_vk_programs;

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