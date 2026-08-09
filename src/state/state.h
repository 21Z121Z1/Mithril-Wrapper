#pragma once

#include <GL/glcorearb.h>
#include <array>
#include <cstdint>

namespace mithril::state {

// Single shared GL context (CHECKLIST section 4.1). One process-wide instance.
// Object identity tables (VAO/VBO/Texture/Program/FBO) arrive later.

constexpr uint32_t kMaxCaps = 64;
constexpr uint32_t kErrStack = 16;

// Capability name->bit mapping, registered lazily on first glEnable.
struct CapSet {
    std::array<bool, kMaxCaps> bits{};
    std::array<GLenum, kMaxCaps> names{};
    uint32_t count = 0;

    // Index of the register slot for `name`; if not yet registered, returns
    // `count` (i.e. next free slot) or kMaxCaps when full.
    uint32_t Normalize(GLenum name) {
        for (uint32_t i = 0; i < count; ++i)
            if (names[i] == name) return i;
        if (count < kMaxCaps) {
            names[count] = name;
            bits[count] = false;
            return count++;
        }
        return kMaxCaps;
    }
    bool Test(GLenum name) const {
        for (uint32_t i = 0; i < count; ++i)
            if (names[i] == name) return bits[i];
        return false;
    }
};

struct ViewportState {
    GLint x = 0, y = 0;
    GLsizei w = 0, h = 0;
    bool initialized = false;
};
struct ScissorState {
    GLint x = 0, y = 0;
    GLsizei w = 0, h = 0;
    bool initialized = false;
};

struct DepthState {
    GLenum func = GL_LESS;
    GLboolean mask = GL_TRUE;
    GLdouble range[2] = {0.0, 1.0};
};

struct StencilState {
    GLenum func = GL_ALWAYS;
    GLint ref = 0;
    GLuint mask = 0xFFFFFFFFu;   // read mask (glStencilFunc)
    GLuint write_mask = 0xFFFFFFFFu; // write mask (glStencilMask)
    GLenum op_fail = GL_KEEP;
    GLenum op_zfail = GL_KEEP;
    GLenum op_zpass = GL_KEEP;
};

struct BlendState {
    GLenum src_rgb = GL_ONE, dst_rgb = GL_ZERO;
    GLenum src_alpha = GL_ONE, dst_alpha = GL_ZERO;
    GLenum eq_rgb = GL_FUNC_ADD, eq_alpha = GL_FUNC_ADD;
    GLfloat color[4] = {0, 0, 0, 0};
};

// Circular FIFO per the GL spec: glGetError returns/clears the oldest entry.
struct ErrorQueue {
    std::array<GLenum, kErrStack> stack{};
    uint32_t head = 0;
    uint32_t count = 0;

    void Push(GLenum e);
    GLenum Pop();
    bool empty() const { return count == 0; }
    GLenum Peek() const { return count ? stack[head] : GL_NO_ERROR; }
};

struct PixelStore {
    GLint unpack_alignment = 4, pack_alignment = 4;
    GLint unpack_row_length = 0, pack_row_length = 0;
    GLint unpack_skip_pixels = 0, pack_skip_pixels = 0;
    GLint unpack_skip_rows = 0, pack_skip_rows = 0;
    GLint unpack_image_height = 0, pack_image_height = 0;
    GLint unpack_skip_images = 0;
};

struct GLState {
    CapSet caps;                 // glEnable/glDisable/glIsEnabled state
    ViewportState viewport;
    ScissorState scissor;        // GL_SCISSOR_TEST gating via caps
    GLfloat clear_color[4] = {0, 0, 0, 0};
    GLdouble clear_depth = 1.0;
    GLint clear_stencil = 0;
    DepthState depth;
    StencilState stencil_front, stencil_back;
    BlendState blend;
    GLenum cull_face = GL_BACK;
    GLenum front_face = GL_CCW;
    GLenum polygon_mode = GL_FILL;
    GLfloat line_width = 1.0f;
    GLfloat point_size = 1.0f;
    GLfloat poly_offset_factor = 0.0f, poly_offset_units = 0.0f;
    GLfloat sample_coverage_value = 0.5f;
    GLboolean sample_coverage_invert = GL_FALSE;
    std::array<GLboolean, 4> color_wmask{GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    GLenum logicop = GL_COPY;
    GLenum hint_derivative = GL_DONT_CARE;
    GLenum clamp_color_mode = GL_FIXED_ONLY;
    std::array<GLuint, 32> sample_masks{};
    PixelStore pixels;
    ErrorQueue errors;
    GLenum active_texture = GL_TEXTURE0;
    GLuint current_program = 0;   // set by glUseProgram; uniform setters target it
    GLuint primitive_restart_index = 0;
};

// The global context instance.
GLState& GetState();

} // namespace mithril::state
