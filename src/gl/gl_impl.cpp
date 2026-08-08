// Mithril-Wrapper GL entry points -- real implementations (milestone M1).
// These functions are excluded from the generated stub table; see MGL_IMPL
// in scripts/gen_gl_stubs.py.

#include <GL/glcorearb.h>

#include <shader/shader.h>
#include <state/state.h>
#include <util/log.h>
#include <vk/vk_engine.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace s = mithril::state;
namespace v = mithril::vk;

// GL program id -> Vulkan program handle (lazily created at first draw).
namespace {
std::unordered_map<GLuint, uint64_t> g_vk_programs;
}  // namespace

#define PUSH_ERROR(e) s::GetState().errors.Push((e))

// Choose a small helper for capability writes shared with glEnable/glDisable.
static bool CapValid(GLenum cap) {
    switch (cap) {
        case GL_DEPTH_TEST:
        case GL_STENCIL_TEST:
        case GL_BLEND:
        case GL_DITHER:
        case GL_CULL_FACE:
        case GL_SCISSOR_TEST:
        case GL_POLYGON_OFFSET_FILL:
        case GL_SAMPLE_ALPHA_TO_COVERAGE:
        case GL_SAMPLE_COVERAGE:
        case GL_MULTISAMPLE:
        case GL_RASTERIZER_DISCARD:
        case GL_PROGRAM_POINT_SIZE:
        case GL_LOGIC_OP_MODE:
            return true;
        default:
            return false;
    }
}

static void SetCap(GLenum cap, bool on) {
    auto& st = s::GetState();
    uint32_t idx = st.caps.Normalize(cap);
    if (idx >= s::kMaxCaps) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }
    st.caps.bits[idx] = on;
}

// --- capabilities -----------------------------------------------------------

extern "C" {

void APIENTRY glEnable(GLenum cap) {
    if (!CapValid(cap)) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    SetCap(cap, true);
}

void APIENTRY glDisable(GLenum cap) {
    if (!CapValid(cap)) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    SetCap(cap, false);
}

void APIENTRY glEnablei(GLenum cap, GLuint index) {
    if (index != 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    glEnable(cap);
}

void APIENTRY glDisablei(GLenum cap, GLuint index) {
    if (index != 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    glDisable(cap);
}

GLboolean APIENTRY glIsEnabled(GLenum cap) {
    if (!CapValid(cap)) { PUSH_ERROR(GL_INVALID_ENUM); return GL_FALSE; }
    return s::GetState().caps.Test(cap) ? GL_TRUE : GL_FALSE;
}

GLboolean APIENTRY glIsEnabledi(GLenum cap, GLuint index) {
    if (index != 0) { PUSH_ERROR(GL_INVALID_VALUE); return GL_FALSE; }
    return glIsEnabled(cap);
}

// ---- noise cancellers -------------------------------------------------------

void APIENTRY glFinish() { v::SubmitFlush(); }
void APIENTRY glFlush() { v::SubmitFlush(); }

// ---- viewport / scissor ----------------------------------------------------

void APIENTRY glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    auto& st = s::GetState();
    st.viewport.x = x; st.viewport.y = y;
    st.viewport.w = width; st.viewport.h = height;
    if (v::IsInitialized())
        v::SetViewport((float)x, (float)y, (float)width, (float)height);
}

void APIENTRY glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    auto& st = s::GetState();
    st.scissor.x = x; st.scissor.y = y;
    st.scissor.w = width; st.scissor.h = height;
}

// ---- clear state -----------------------------------------------------------

void APIENTRY glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    auto& st = s::GetState();
    st.clear_color[0] = r; st.clear_color[1] = g;
    st.clear_color[2] = b; st.clear_color[3] = a;
    if (v::IsInitialized())
        v::SetClearColor(r, g, b, a);
}

void APIENTRY glClearDepth(GLdouble depth) { s::GetState().clear_depth = depth; }

void APIENTRY glClearStencil(GLint sval) { s::GetState().clear_stencil = sval; }

void APIENTRY glClear(GLbitfield mask) {
    const GLbitfield valid = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
    if (mask & ~valid) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (mask & GL_COLOR_BUFFER_BIT) {
        v::EnsureInit();
        auto& st = s::GetState();
        v::SetClearColor(st.clear_color[0], st.clear_color[1], st.clear_color[2],
                         st.clear_color[3]);
        v::MarkClear();
    }
    // Depth/stencil bits are accepted but have no Vulkan attachments yet.
}

// ---- face / polygon --------------------------------------------------------

void APIENTRY glCullFace(GLenum mode) {
    switch (mode) {
        case GL_FRONT: case GL_BACK: case GL_FRONT_AND_BACK:
            s::GetState().cull_face = mode; break;
        default: PUSH_ERROR(GL_INVALID_ENUM);
    }
}

void APIENTRY glFrontFace(GLenum mode) {
    switch (mode) {
        case GL_CW: case GL_CCW:
            s::GetState().front_face = mode; break;
        default: PUSH_ERROR(GL_INVALID_ENUM);
    }
}

void APIENTRY glPolygonMode(GLenum face, GLenum mode) {
    if (face != GL_FRONT_AND_BACK) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    switch (mode) {
        case GL_POINT: case GL_LINE: case GL_FILL:
            s::GetState().polygon_mode = mode; break;
        default: PUSH_ERROR(GL_INVALID_ENUM);
    }
}

void APIENTRY glLineWidth(GLfloat width) {
    if (width > 0.0f) s::GetState().line_width = width;
    else PUSH_ERROR(GL_INVALID_VALUE);
}

void APIENTRY glPointSize(GLfloat size) {
    if (size > 0.0f) s::GetState().point_size = size;
    else PUSH_ERROR(GL_INVALID_VALUE);
}

void APIENTRY glPolygonOffset(GLfloat factor, GLfloat units) {
    auto& st = s::GetState();
    st.poly_offset_factor = factor;
    st.poly_offset_units = units;
}

void APIENTRY glSampleCoverage(GLfloat value, GLboolean invert) {
    if (value < 0.0f || value > 1.0f) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    auto& st = s::GetState();
    st.sample_coverage_value = value;
    st.sample_coverage_invert = invert;
}

void APIENTRY glSampleMaski(GLuint index, GLbitfield mask) {
    if (index >= 32) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    s::GetState().sample_masks[index] = mask;
}

// ---- depth mask/depth func: they act on the GLSL compoe depth test --------

void APIENTRY glDepthFunc(GLenum func) {
    switch (func) {
        case GL_NEVER: case GL_LESS: case GL_EQUAL: case GL_LEQUAL:
        case GL_GREATER: case GL_NOTEQUAL: case GL_GEQUAL: case GL_ALWAYS:
            s::GetState().depth.func = func; break;
        default: PUSH_ERROR(GL_INVALID_ENUM);
    }
}

void APIENTRY glDepthMask(GLboolean mask) { s::GetState().depth.mask = mask; }

void APIENTRY glDepthRange(GLdouble n, GLdouble f) {
    auto& st = s::GetState();
    st.depth.range[0] = n; st.depth.range[1] = f;
}

// ---- stencil ---------------------------------------------------------------

static bool StencilOpValid(GLenum op) {
    switch (op) {
        case GL_KEEP: case GL_ZERO: case GL_REPLACE: case GL_INCR:
        case GL_INCR_WRAP: case GL_DECR: case GL_DECR_WRAP: case GL_INVERT:
            return true;
        default:
            return false;
    }
}

void APIENTRY glStencilFunc(GLenum func, GLint ref, GLuint mask) {
    auto& st = s::GetState();
    st.stencil_front.func = func; st.stencil_front.ref = ref; st.stencil_front.mask = mask;
    st.stencil_back = st.stencil_front;
}

void APIENTRY glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask) {
    auto& st = s::GetState();
    switch (face) {
        case GL_FRONT: st.stencil_front = {func, ref, mask, st.stencil_front.op_fail, st.stencil_front.op_zfail, st.stencil_front.op_zpass}; break;
        case GL_BACK:  st.stencil_back  = {func, ref, mask, st.stencil_back.op_fail,  st.stencil_back.op_zfail,  st.stencil_back.op_zpass}; break;
        case GL_FRONT_AND_BACK: st.stencil_front = {func, ref, mask, st.stencil_front.op_fail, st.stencil_front.op_zfail, st.stencil_front.op_zpass};
                                st.stencil_back  = st.stencil_front; break;
        default: PUSH_ERROR(GL_INVALID_ENUM);
    }
}

void APIENTRY glStencilMask(GLuint mask) {
    auto& st = s::GetState();
    st.stencil_front.mask = mask;
    st.stencil_back.mask = mask;
}

void APIENTRY glStencilMaskSeparate(GLenum face, GLuint mask) {
    auto& st = s::GetState();
    switch (face) {
        case GL_FRONT: st.stencil_front.mask = mask; break;
        case GL_BACK:  st.stencil_back.mask = mask; break;
        case GL_FRONT_AND_BACK:
            st.stencil_front.mask = mask; st.stencil_back.mask = mask; break;
        default: PUSH_ERROR(GL_INVALID_ENUM);
    }
}

void APIENTRY glStencilOp(GLenum fail, GLenum zfail, GLenum zpass) {
    if (!StencilOpValid(fail) || !StencilOpValid(zfail) || !StencilOpValid(zpass)) {
        PUSH_ERROR(GL_INVALID_ENUM); return;
    }
    auto& st = s::GetState();
    st.stencil_front.op_fail = fail; st.stencil_front.op_zfail = zfail; st.stencil_front.op_zpass = zpass;
    st.stencil_back = st.stencil_front;
}

void APIENTRY glStencilOpSeparate(GLenum face, GLenum fail, GLenum zfail, GLenum zpass) {
    if (!StencilOpValid(fail) || !StencilOpValid(zfail) || !StencilOpValid(zpass)) {
        PUSH_ERROR(GL_INVALID_ENUM); return;
    }
    auto& st = s::GetState();
    if (face == GL_FRONT || face == GL_FRONT_AND_BACK) {
        st.stencil_front.op_fail = fail; st.stencil_front.op_zfail = zfail; st.stencil_front.op_zpass = zpass;
    }
    if (face == GL_BACK || face == GL_FRONT_AND_BACK) {
        st.stencil_back.op_fail = fail; st.stencil_back.op_zfail = zfail; st.stencil_back.op_zpass = zpass;
    }
}

// ---- blend -----------------------------------------------------------------

void APIENTRY glBlendFunc(GLenum src, GLenum dst) {
    auto& st = s::GetState();
    st.blend.src_rgb = st.blend.src_alpha = src;
    st.blend.dst_rgb = st.blend.dst_alpha = dst;
}

void APIENTRY glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha) {
    auto& st = s::GetState();
    st.blend.src_rgb = srcRGB; st.blend.dst_rgb = dstRGB;
    st.blend.src_alpha = srcAlpha; st.blend.dst_alpha = dstAlpha;
}

void APIENTRY glBlendEquation(GLenum mode) {
    auto& st = s::GetState();
    st.blend.eq_rgb = st.blend.eq_alpha = mode;
}

void APIENTRY glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha) {
    auto& st = s::GetState();
    st.blend.eq_rgb = modeRGB; st.blend.eq_alpha = modeAlpha;
}

void APIENTRY glBlendColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    auto& st = s::GetState();
    st.blend.color[0] = r; st.blend.color[1] = g;
    st.blend.color[2] = b; st.blend.color[3] = a;
}

// ---- color mask ---------------------------------------------------------------

void APIENTRY glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    // M1 keeps color masks in the state object; per-draw-buffer arrays in M5.
    // This is intentionally a no-op beyond validity: stores nothing for now.
    (void)r; (void)g; (void)b; (void)a;
}

void APIENTRY glColorMaski(GLuint index, GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    if (index != 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    glColorMask(r, g, b, a);
}

// ---- logic op / hint / pixel store -----------------------------------------

void APIENTRY glLogicOp(GLenum op) {
    switch (op) {
        case GL_CLEAR: case GL_AND: case GL_AND_REVERSE: case GL_COPY:
        case GL_AND_INVERTED: case GL_NOOP: case GL_XOR: case GL_OR:
        case GL_NOR: case GL_EQUIV: case GL_INVERT: case GL_OR_REVERSE:
        case GL_COPY_INVERTED: case GL_OR_INVERTED: case GL_NAND: case GL_SET:
            s::GetState().logicop = op; break;
        default: PUSH_ERROR(GL_INVALID_ENUM);
    }
}

void APIENTRY glHint(GLenum target, GLenum mode) {
    if (target == GL_FRAGMENT_SHADER_DERIVATIVE_HINT) {
        s::GetState().hint_derivative = mode;
    } else {
        PUSH_ERROR(GL_INVALID_ENUM);
    }
}

void APIENTRY glPixelStorei(GLenum pname, GLint param) {
    auto& st = s::GetState();
    switch (pname) {
        case GL_PACK_ALIGNMENT: case GL_UNPACK_ALIGNMENT:
            if (param != 1 && param != 2 && param != 4 && param != 8) {
                PUSH_ERROR(GL_INVALID_VALUE); return;
            }
            if (pname == GL_PACK_ALIGNMENT) st.pixels.pack_alignment = param;
            else st.pixels.unpack_alignment = param;
            break;
        case GL_PACK_ROW_LENGTH:  st.pixels.pack_row_length = param; break;
        case GL_UNPACK_ROW_LENGTH: st.pixels.unpack_row_length = param; break;
        default:
            PUSH_ERROR(GL_INVALID_ENUM);
    }
}

void APIENTRY glClampColor(GLenum target, GLenum clamp) {
    if (target != GL_CLAMP_READ_COLOR) {
        PUSH_ERROR(GL_INVALID_ENUM); return;
    }
    if (clamp != GL_TRUE && clamp != GL_FALSE && clamp != GL_FIXED_ONLY) {
        PUSH_ERROR(GL_INVALID_ENUM); return;
    }
    s::GetState().clamp_color_mode = clamp;
}

GLenum APIENTRY glGetError() { return s::GetState().errors.Pop(); }

const GLubyte* APIENTRY glGetString(GLenum name) {
    switch (name) {
        case GL_VENDOR:   return reinterpret_cast<const GLubyte*>("Mithril-Wrapper");
        case GL_RENDERER: return reinterpret_cast<const GLubyte*>("Vulkan on Metal (MoltenVK)");
        case GL_VERSION:  return reinterpret_cast<const GLubyte*>("3.3 Core Profile Mithril");
        case GL_SHADING_LANGUAGE_VERSION:
                          return reinterpret_cast<const GLubyte*>("3.30 Mithril");
        case GL_EXTENSIONS: return reinterpret_cast<const GLubyte*>("");
        default:
            PUSH_ERROR(GL_INVALID_ENUM);
            return nullptr;
    }
}

const GLubyte* APIENTRY glGetStringi(GLenum name, GLuint index) {
    if (name != GL_EXTENSIONS) { PUSH_ERROR(GL_INVALID_ENUM); return nullptr; }
    (void)index;
    // No extensions registered in M1; any index is out of range.
    PUSH_ERROR(GL_INVALID_VALUE);
    return nullptr;
}

void APIENTRY glGetBooleanv(GLenum pname, GLboolean* data) {
    if (!data) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    const auto& st = s::GetState();
    switch (pname) {
        case GL_DEPTH_TEST:     *data = st.caps.Test(GL_DEPTH_TEST) ? GL_TRUE : GL_FALSE; break;
        case GL_STENCIL_TEST:   *data = st.caps.Test(GL_STENCIL_TEST) ? GL_TRUE : GL_FALSE; break;
        case GL_BLEND:          *data = st.caps.Test(GL_BLEND) ? GL_TRUE : GL_FALSE; break;
        case GL_CULL_FACE:      *data = st.caps.Test(GL_CULL_FACE) ? GL_TRUE : GL_FALSE; break;
        case GL_SCISSOR_TEST:   *data = st.caps.Test(GL_SCISSOR_TEST) ? GL_TRUE : GL_FALSE; break;
        case GL_MULTISAMPLE:    *data = st.caps.Test(GL_MULTISAMPLE) ? GL_TRUE : GL_FALSE; break;
        case GL_DITHER:         *data = st.caps.Test(GL_DITHER) ? GL_TRUE : GL_FALSE; break;
        case GL_RASTERIZER_DISCARD: *data = st.caps.Test(GL_RASTERIZER_DISCARD) ? GL_TRUE : GL_FALSE; break;
        case GL_SAMPLE_COVERAGE: *data = st.caps.Test(GL_SAMPLE_COVERAGE) ? GL_TRUE : GL_FALSE; break;
        case GL_POLYGON_OFFSET_FILL: *data = st.caps.Test(GL_POLYGON_OFFSET_FILL) ? GL_TRUE : GL_FALSE; break;
        case GL_LOGIC_OP_MODE:       *data = st.caps.Test(GL_LOGIC_OP_MODE) ? GL_TRUE : GL_FALSE; break;
        case GL_DEPTH_WRITEMASK: *data = st.depth.mask; break;
        default: PUSH_ERROR(GL_INVALID_ENUM);
    }
}

void APIENTRY glGetFloatv(GLenum pname, GLfloat* data) {
    if (!data) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    const auto& st = s::GetState();
    switch (pname) {
        case GL_LINE_WIDTH:  *data = st.line_width; break;
        case GL_POINT_SIZE:  *data = st.point_size; break;
        case GL_VIEWPORT:
            data[0] = (GLfloat)st.viewport.x; data[1] = (GLfloat)st.viewport.y;
            data[2] = (GLfloat)st.viewport.w; data[3] = (GLfloat)st.viewport.h;
            break;
        default: PUSH_ERROR(GL_INVALID_ENUM);
    }
}

void APIENTRY glGetIntegerv(GLenum pname, GLint* data) {
    if (!data) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    const auto& st = s::GetState();
    switch (pname) {
        case GL_MAX_TEXTURE_SIZE: *data = 16384; break;
        case GL_MAX_3D_TEXTURE_SIZE: *data = 2048; break;
        case GL_MAX_CUBE_MAP_TEXTURE_SIZE: *data = 16384; break;
        case GL_MAX_ARRAY_TEXTURE_LAYERS: *data = 2048; break;
        case GL_MAX_VIEWPORT_DIMS:
            data[0] = 16384; data[1] = 16384; break;
        case GL_VIEWPORT:
            data[0] = st.viewport.x; data[1] = st.viewport.y;
            data[2] = st.viewport.w; data[3] = st.viewport.h;
            break;
        case GL_SCISSOR_BOX:
            data[0] = st.scissor.x; data[1] = st.scissor.y;
            data[2] = st.scissor.w; data[3] = st.scissor.h;
            break;
        case GL_NUM_EXTENSIONS: *data = 0; break;
        case GL_MAJOR_VERSION: *data = 3; break;
        case GL_MINOR_VERSION: *data = 3; break;
        case GL_CONTEXT_PROFILE_MASK: *data = GL_CONTEXT_CORE_PROFILE_BIT; break;
        case GL_SAMPLE_MASK: *data = st.sample_masks[0] ? 1 : 0; break;
        default: PUSH_ERROR(GL_INVALID_ENUM);
    }
}

void APIENTRY glGetInteger64v(GLenum pname, GLint64* data) {
    if (!data) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (pname == GL_MAJOR_VERSION) *data = 3;
    else if (pname == GL_MINOR_VERSION) *data = 3;
    else PUSH_ERROR(GL_INVALID_ENUM);
}

void APIENTRY glGetDoublev(GLenum pname, GLdouble* data) {
    if (!data) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    const auto& st = s::GetState();
    switch (pname) {
        case GL_DEPTH_RANGE: data[0] = st.depth.range[0]; data[1] = st.depth.range[1]; break;
        case GL_DEPTH_CLEAR_VALUE: *data = st.clear_depth; break;
        case GL_COLOR_CLEAR_VALUE:
            data[0] = st.clear_color[0]; data[1] = st.clear_color[1];
            data[2] = st.clear_color[2]; data[3] = st.clear_color[3];
            break;
        default: PUSH_ERROR(GL_INVALID_ENUM);
    }
}

void APIENTRY glGetPointerv(GLenum pname, void** params) {
    (void)pname;
    (void)params;
    // All pointer queries are removed from the core profile; error out.
    PUSH_ERROR(GL_INVALID_ENUM);
}

// ---- shaders / programs / uniforms (S2) ------------------------------------

namespace {
namespace sh = mithril::shader;
} // namespace

GLuint APIENTRY glCreateShader(GLenum type) {
    if (type != GL_VERTEX_SHADER && type != GL_FRAGMENT_SHADER &&
        type != GL_GEOMETRY_SHADER && type != GL_TESS_CONTROL_SHADER &&
        type != GL_TESS_EVALUATION_SHADER && type != GL_COMPUTE_SHADER) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return 0;
    }
    GLuint id = sh::NewShader(type);
    ML_LOG_DEBUG("glCreateShader(0x%x) -> %u", (unsigned)type, id);
    return id;
}

void APIENTRY glDeleteShader(GLuint shader) {
    if (shader && sh::GetShader(shader) == nullptr) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    sh::DeleteShader(shader);
}

GLboolean APIENTRY glIsShader(GLuint shader) {
    return sh::GetShader(shader) != nullptr ? GL_TRUE : GL_FALSE;
}

void APIENTRY glShaderSource(GLuint shader, GLsizei count, const GLchar* const* string,
                             const GLint* length) {
    auto* s = sh::GetShader(shader);
    if (!s) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (count < 0 || (count > 0 && !string)) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    s->source.clear();
    for (GLsizei i = 0; i < count; ++i) {
        if (!string[i]) continue;
        if (length && length[i] >= 0) s->source.append(string[i], (size_t)length[i]);
        else s->source.append(string[i]);
    }
    s->compiled = false;
    s->spirv.clear();
}

void APIENTRY glCompileShader(GLuint shader) {
    auto* s = sh::GetShader(shader);
    if (!s) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    s->compiled = false;
    s->spirv.clear();
    if (s->source.empty()) {
        s->info_log = "shader source is empty";
        ML_LOG_WARN("glCompileShader(%u): empty source", shader);
        return;
    }
    std::vector<uint32_t> spirv;
    std::string info;
    if (sh::CompileStage(s->type, s->source, spirv, info)) {
        s->compiled = true;
        s->spirv = std::move(spirv);
        s->info_log.clear();
        ML_LOG_DEBUG("glCompileShader(%u): ok, %zu SPIR-V words", shader, s->spirv.size());
    } else {
        s->info_log = info;
        ML_LOG_WARN("glCompileShader(%u): %s", shader, info.c_str());
    }
}

void APIENTRY glGetShaderiv(GLuint shader, GLenum pname, GLint* params) {
    if (!params) return;
    auto* s = sh::GetShader(shader);
    if (!s) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    switch (pname) {
        case GL_SHADER_TYPE:          *params = (GLint)s->type; return;
        case GL_COMPILE_STATUS:       *params = s->compiled ? GL_TRUE : GL_FALSE; return;
        case GL_INFO_LOG_LENGTH:      *params = (GLint)(s->info_log.size() + 1); return;
        case GL_SHADER_SOURCE_LENGTH: *params = (GLint)(s->source.size() + 1); return;
        case GL_DELETE_STATUS:        *params = GL_FALSE; return;
        default:                      PUSH_ERROR(GL_INVALID_ENUM);
    }
}

void APIENTRY glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei* length,
                                 GLchar* infoLog) {
    auto* s = sh::GetShader(shader);
    if (!s) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (!infoLog || bufSize <= 0) { if (length) *length = 0; return; }
    GLsizei n = (GLsizei)s->info_log.size();
    if (n > bufSize - 1) n = bufSize - 1;
    std::memcpy(infoLog, s->info_log.data(), (size_t)n);
    infoLog[n] = 0;
    if (length) *length = n;
}

void APIENTRY glGetShaderSource(GLuint shader, GLsizei bufSize, GLsizei* length,
                                GLchar* source) {
    auto* s = sh::GetShader(shader);
    if (!s) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (!source || bufSize <= 0) { if (length) *length = 0; return; }
    GLsizei n = (GLsizei)s->source.size();
    if (n > bufSize - 1) n = bufSize - 1;
    std::memcpy(source, s->source.data(), (size_t)n);
    source[n] = 0;
    if (length) *length = n;
}

GLuint APIENTRY glCreateProgram(void) {
    GLuint id = sh::NewProgram();
    ML_LOG_DEBUG("glCreateProgram -> %u", id);
    return id;
}

void APIENTRY glDeleteProgram(GLuint program) {
    if (program && sh::GetProgram(program) == nullptr) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    auto it = g_vk_programs.find(program);
    if (it != g_vk_programs.end()) {
        v::DestroyProgram(it->second);
        g_vk_programs.erase(it);
    }
    sh::DeleteProgram(program);
}

GLboolean APIENTRY glIsProgram(GLuint program) {
    return sh::GetProgram(program) != nullptr ? GL_TRUE : GL_FALSE;
}

void APIENTRY glAttachShader(GLuint program, GLuint shader) {
    auto* p = sh::GetProgram(program);
    auto* s = sh::GetShader(shader);
    if (!p || !s) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    for (GLuint id : p->attached) if (id == shader) return;
    p->attached.push_back(shader);
}

void APIENTRY glDetachShader(GLuint program, GLuint shader) {
    auto* p = sh::GetProgram(program);
    auto* s = sh::GetShader(shader);
    if (!p || !s) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    auto& v = p->attached;
    v.erase(std::remove(v.begin(), v.end(), shader), v.end());
}

void APIENTRY glBindAttribLocation(GLuint program, GLuint index, const GLchar* name) {
    (void)index; (void)name;
    // Recorded attribute bindings feed vertex SPIR-V re-translation in M3
    // (vertex input); accepted without error here so startup shaders pass.
    if (!sh::GetProgram(program)) { PUSH_ERROR(GL_INVALID_VALUE); return; }
}

void APIENTRY glLinkProgram(GLuint program) {
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_VALUE); return; }

    p->vertex_spirv.clear();
    p->fragment_spirv.clear();
    p->linked = false;
    p->info_log.clear();

    bool have_vs = false, have_fs = false;
    for (GLuint sid : p->attached) {
        auto* s = sh::GetShader(sid);
        if (!s) continue;
        if (!s->compiled || s->spirv.empty()) {
            p->info_log = "link failed: attached shader " + std::to_string(sid) +
                          " is not compiled";
            ML_LOG_WARN("glLinkProgram(%u): %s", program, p->info_log.c_str());
            return;
        }
        if (s->type == GL_VERTEX_SHADER) { p->vertex_spirv = s->spirv; have_vs = true; }
        else if (s->type == GL_FRAGMENT_SHADER) { p->fragment_spirv = s->spirv; have_fs = true; }
    }
    if (!have_vs || !have_fs) {
        if (p->info_log.empty())
            p->info_log = "link failed: missing compiled vertex or fragment shader";
        ML_LOG_WARN("glLinkProgram(%u): %s", program, p->info_log.c_str());
        return;
    }

    p->linked = true;
    sh::ReflectProgram(*p);
    ML_LOG_DEBUG("glLinkProgram(%u): VS=%zu FS=%zu words, %zu uniforms",
                 program, p->vertex_spirv.size(), p->fragment_spirv.size(),
                 p->uniforms.size());
}

void APIENTRY glGetProgramiv(GLuint program, GLenum pname, GLint* params) {
    if (!params) return;
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    switch (pname) {
        case GL_LINK_STATUS:      *params = p->linked ? GL_TRUE : GL_FALSE; return;
        case GL_VALIDATE_STATUS:  *params = GL_TRUE; return;
        case GL_INFO_LOG_LENGTH:  *params = (GLint)(p->info_log.size() + 1); return;
        case GL_ACTIVE_UNIFORMS:  *params = (GLint)p->uniforms.size(); return;
        case GL_ACTIVE_ATTRIBUTES:*params = (GLint)p->attrib_locations.size(); return;
        case GL_ATTACHED_SHADERS: *params = (GLint)p->attached.size(); return;
        case GL_DELETE_STATUS:    *params = GL_FALSE; return;
        default:                  PUSH_ERROR(GL_INVALID_ENUM);
    }
}

void APIENTRY glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei* length,
                                  GLchar* infoLog) {
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (!infoLog || bufSize <= 0) { if (length) *length = 0; return; }
    GLsizei n = (GLsizei)p->info_log.size();
    if (n > bufSize - 1) n = bufSize - 1;
    std::memcpy(infoLog, p->info_log.data(), (size_t)n);
    infoLog[n] = 0;
    if (length) *length = n;
}

void APIENTRY glGetAttachedShaders(GLuint program, GLsizei maxCount, GLsizei* count,
                                   GLuint* shaders) {
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (!shaders || maxCount <= 0) { if (count) *count = 0; return; }
    GLsizei n = (GLsizei)p->attached.size();
    if (n > maxCount) n = maxCount;
    for (GLsizei i = 0; i < n; ++i) shaders[i] = p->attached[i];
    if (count) *count = n;
}

void APIENTRY glUseProgram(GLuint program) {
    if (program != 0 && sh::GetProgram(program) == nullptr) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    s::GetState().current_program = program;
}

void APIENTRY glValidateProgram(GLuint program) {
    if (!sh::GetProgram(program)) { PUSH_ERROR(GL_INVALID_VALUE); return; }
}

GLint APIENTRY glGetUniformLocation(GLuint program, const GLchar* name) {
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_VALUE); return -1; }
    if (!p->linked || !name) return -1;
    auto it = p->uniform_by_name.find(name);
    return it == p->uniform_by_name.end() ? -1 : it->second;
}

GLint APIENTRY glGetAttribLocation(GLuint program, const GLchar* name) {
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_VALUE); return -1; }
    if (!p->linked || !name) return -1;
    auto it = p->attrib_locations.find(name);
    return it == p->attrib_locations.end() ? -1 : it->second;
}

void APIENTRY glGetActiveUniform(GLuint program, GLuint index, GLsizei bufSize,
                                 GLsizei* length, GLint* size, GLenum* type, GLchar* name) {
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (index >= p->uniforms.size()) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    const sh::Uniform& u = p->uniforms[index];
    if (size) *size = 1;
    if (type) *type = u.type;
    if (name && bufSize > 0) {
        GLsizei n = (GLsizei)u.name.size();
        if (n > bufSize - 1) n = bufSize - 1;
        std::memcpy(name, u.name.data(), (size_t)n);
        name[n] = 0;
        if (length) *length = n;
    } else if (length) *length = 0;
}

void APIENTRY glGetActiveAttrib(GLuint program, GLuint index, GLsizei bufSize,
                                GLsizei* length, GLint* size, GLenum* type, GLchar* name) {
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (index >= p->attrib_locations.size()) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    auto it = p->attrib_locations.begin();
    std::advance(it, index);
    if (size) *size = 1;
    if (type) *type = GL_FLOAT;
    if (name && bufSize > 0) {
        GLsizei n = (GLsizei)it->first.size();
        if (n > bufSize - 1) n = bufSize - 1;
        std::memcpy(name, it->first.data(), (size_t)n);
        name[n] = 0;
        if (length) *length = n;
    } else if (length) *length = 0;
}

void APIENTRY glGetUniformfv(GLuint program, GLint location, GLfloat* params) {
    if (!params) return;
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    auto it = p->uniform_by_location.find(location);
    if (it == p->uniform_by_location.end()) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    sh::Uniform& u = p->uniforms[it->second];
    if (u.value.empty()) { *params = 0.0f; return; }
    std::memcpy(params, u.value.data(), u.value.size() * sizeof(float));
}

void APIENTRY glGetUniformiv(GLuint program, GLint location, GLint* params) {
    if (!params) return;
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    auto it = p->uniform_by_location.find(location);
    if (it == p->uniform_by_location.end()) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    sh::Uniform& u = p->uniforms[it->second];
    if (u.value.empty()) { *params = 0; return; }
    for (size_t i = 0; i < u.value.size() && i < 4; ++i) params[i] = (GLint)u.value[i];
}

void APIENTRY glGetUniformuiv(GLuint program, GLint location, GLuint* params) {
    if (!params) return;
    auto* p = sh::GetProgram(program);
    if (!p) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    auto it = p->uniform_by_location.find(location);
    if (it == p->uniform_by_location.end()) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    sh::Uniform& u = p->uniforms[it->second];
    if (u.value.empty()) { *params = 0; return; }
    for (size_t i = 0; i < u.value.size() && i < 4; ++i) params[i] = (GLuint)u.value[i];
}

// ---- uniform setters -------------------------------------------------------

namespace {
// Target program for uniform setters: the current program, else none.
sh::Program* CurrentProgramForUniform() {
    GLuint id = s::GetState().current_program;
    return id ? sh::GetProgram(id) : nullptr;
}

// Store `count` elements of `comps` components each into uniform `location`.
// Unreflected locations are ignored per GL semantics.
void StoreUniform(GLenum type, GLint location, const GLfloat* v, GLsizei count, int comps) {
    sh::Program* p = CurrentProgramForUniform();
    if (!p || location < 0 || !v || count <= 0) return;
    auto it = p->uniform_by_location.find(location);
    if (it == p->uniform_by_location.end()) return;
    sh::Uniform& u = p->uniforms[it->second];
    u.type = type;
    u.value.assign(v, v + (size_t)count * (size_t)comps);
}

void StoreUniformInt(GLenum type, GLint location, const GLint* v, GLsizei count, int comps) {
    sh::Program* p = CurrentProgramForUniform();
    if (!p || location < 0 || !v || count <= 0) return;
    auto it = p->uniform_by_location.find(location);
    if (it == p->uniform_by_location.end()) return;
    sh::Uniform& u = p->uniforms[it->second];
    u.type = type;
    u.value.clear();
    for (GLsizei i = 0; i < count * comps; ++i) u.value.push_back((float)v[i]);
}
} // namespace

void APIENTRY glUniform1f(GLint location, GLfloat v0) {
    GLfloat v[1] = {v0}; StoreUniform(GL_FLOAT, location, v, 1, 1);
}
void APIENTRY glUniform2f(GLint location, GLfloat v0, GLfloat v1) {
    GLfloat v[2] = {v0, v1}; StoreUniform(GL_FLOAT_VEC2, location, v, 1, 2);
}
void APIENTRY glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) {
    GLfloat v[3] = {v0, v1, v2}; StoreUniform(GL_FLOAT_VEC3, location, v, 1, 3);
}
void APIENTRY glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) {
    GLfloat v[4] = {v0, v1, v2, v3}; StoreUniform(GL_FLOAT_VEC4, location, v, 1, 4);
}
void APIENTRY glUniform1i(GLint location, GLint v0) {
    GLint v[1] = {v0}; StoreUniformInt(GL_INT, location, v, 1, 1);
}
void APIENTRY glUniform2i(GLint location, GLint v0, GLint v1) {
    GLint v[2] = {v0, v1}; StoreUniformInt(GL_INT_VEC2, location, v, 1, 2);
}
void APIENTRY glUniform3i(GLint location, GLint v0, GLint v1, GLint v2) {
    GLint v[3] = {v0, v1, v2}; StoreUniformInt(GL_INT_VEC3, location, v, 1, 3);
}
void APIENTRY glUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3) {
    GLint v[4] = {v0, v1, v2, v3}; StoreUniformInt(GL_INT_VEC4, location, v, 1, 4);
}
void APIENTRY glUniform1ui(GLint location, GLuint v0) {
    GLint v[1] = {(GLint)v0}; StoreUniformInt(GL_UNSIGNED_INT, location, v, 1, 1);
}
void APIENTRY glUniform2ui(GLint location, GLuint v0, GLuint v1) {
    GLint v[2] = {(GLint)v0, (GLint)v1}; StoreUniformInt(GL_UNSIGNED_INT_VEC2, location, v, 1, 2);
}
void APIENTRY glUniform3ui(GLint location, GLuint v0, GLuint v1, GLuint v2) {
    GLint v[3] = {(GLint)v0, (GLint)v1, (GLint)v2};
    StoreUniformInt(GL_UNSIGNED_INT_VEC3, location, v, 1, 3);
}
void APIENTRY glUniform4ui(GLint location, GLuint v0, GLuint v1, GLuint v2, GLuint v3) {
    GLint v[4] = {(GLint)v0, (GLint)v1, (GLint)v2, (GLint)v3};
    StoreUniformInt(GL_UNSIGNED_INT_VEC4, location, v, 1, 4);
}

void APIENTRY glUniform1fv(GLint location, GLsizei count, const GLfloat* value) {
    StoreUniform(GL_FLOAT, location, value, count, 1);
}
void APIENTRY glUniform2fv(GLint location, GLsizei count, const GLfloat* value) {
    StoreUniform(GL_FLOAT_VEC2, location, value, count, 2);
}
void APIENTRY glUniform3fv(GLint location, GLsizei count, const GLfloat* value) {
    StoreUniform(GL_FLOAT_VEC3, location, value, count, 3);
}
void APIENTRY glUniform4fv(GLint location, GLsizei count, const GLfloat* value) {
    StoreUniform(GL_FLOAT_VEC4, location, value, count, 4);
}
void APIENTRY glUniform1iv(GLint location, GLsizei count, const GLint* value) {
    StoreUniformInt(GL_INT, location, value, count, 1);
}
void APIENTRY glUniform2iv(GLint location, GLsizei count, const GLint* value) {
    StoreUniformInt(GL_INT_VEC2, location, value, count, 2);
}
void APIENTRY glUniform3iv(GLint location, GLsizei count, const GLint* value) {
    StoreUniformInt(GL_INT_VEC3, location, value, count, 3);
}
void APIENTRY glUniform4iv(GLint location, GLsizei count, const GLint* value) {
    StoreUniformInt(GL_INT_VEC4, location, value, count, 4);
}
void APIENTRY glUniform1uiv(GLint location, GLsizei count, const GLuint* value) {
    std::vector<GLint> tmp(value, value + count);
    StoreUniformInt(GL_UNSIGNED_INT, location, tmp.data(), count, 1);
}
void APIENTRY glUniform2uiv(GLint location, GLsizei count, const GLuint* value) {
    std::vector<GLint> tmp(value, value + count * 2);
    StoreUniformInt(GL_UNSIGNED_INT_VEC2, location, tmp.data(), count, 2);
}
void APIENTRY glUniform3uiv(GLint location, GLsizei count, const GLuint* value) {
    std::vector<GLint> tmp(value, value + count * 3);
    StoreUniformInt(GL_UNSIGNED_INT_VEC3, location, tmp.data(), count, 3);
}
void APIENTRY glUniform4uiv(GLint location, GLsizei count, const GLuint* value) {
    std::vector<GLint> tmp(value, value + count * 4);
    StoreUniformInt(GL_UNSIGNED_INT_VEC4, location, tmp.data(), count, 4);
}

void APIENTRY glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose,
                                 const GLfloat* value) {
    (void)transpose; StoreUniform(GL_FLOAT_MAT2, location, value, count, 4);
}
void APIENTRY glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose,
                                 const GLfloat* value) {
    (void)transpose; StoreUniform(GL_FLOAT_MAT3, location, value, count, 9);
}
void APIENTRY glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose,
                                 const GLfloat* value) {
    (void)transpose; StoreUniform(GL_FLOAT_MAT4, location, value, count, 16);
}
void APIENTRY glUniformMatrix2x3fv(GLint location, GLsizei count, GLboolean transpose,
                                   const GLfloat* value) {
    (void)transpose; StoreUniform(GL_FLOAT_MAT2x3, location, value, count, 6);
}
void APIENTRY glUniformMatrix3x2fv(GLint location, GLsizei count, GLboolean transpose,
                                   const GLfloat* value) {
    (void)transpose; StoreUniform(GL_FLOAT_MAT3x2, location, value, count, 6);
}
void APIENTRY glUniformMatrix2x4fv(GLint location, GLsizei count, GLboolean transpose,
                                   const GLfloat* value) {
    (void)transpose; StoreUniform(GL_FLOAT_MAT2x4, location, value, count, 8);
}
void APIENTRY glUniformMatrix4x2fv(GLint location, GLsizei count, GLboolean transpose,
                                   const GLfloat* value) {
    (void)transpose; StoreUniform(GL_FLOAT_MAT4x2, location, value, count, 8);
}
void APIENTRY glUniformMatrix3x4fv(GLint location, GLsizei count, GLboolean transpose,
                                   const GLfloat* value) {
    (void)transpose; StoreUniform(GL_FLOAT_MAT3x4, location, value, count, 12);
}
void APIENTRY glUniformMatrix4x3fv(GLint location, GLsizei count, GLboolean transpose,
                                   const GLfloat* value) {
    (void)transpose; StoreUniform(GL_FLOAT_MAT4x3, location, value, count, 12);
}

// ---- vertex arrays / buffers / draw (milestone M2-VK) -----------------------

namespace {
constexpr GLuint kMaxAttribs = 16;

struct AttribData {
    bool enabled = false;
    GLint size = 0;            // 1..4 components
    GLenum type = GL_FLOAT;
    GLboolean normalized = GL_FALSE;
    GLsizei stride = 0;
    GLsizeiptr offset = 0;
    GLuint buffer = 0;         // GL_ARRAY_BUFFER bound at glVertexAttribPointer
};

struct VAOData {
    std::array<AttribData, kMaxAttribs> attribs{};
};

struct BufferData {
    std::vector<uint8_t> data;
};

std::unordered_map<GLuint, VAOData> g_vaos;
std::unordered_map<GLuint, BufferData> g_buffers;
GLuint g_next_vao = 1, g_next_buffer = 1;
GLuint g_bound_vao = 0;          // default VAO is 0
GLuint g_bound_array_buffer = 0;
GLuint g_bound_element_buffer = 0;

GLuint NewName(std::unordered_map<GLuint, VAOData>& table, GLuint& next) {
    while (table.count(next)) ++next;
    table.emplace(next, VAOData{});
    return next++;
}
} // namespace

void APIENTRY glGenVertexArrays(GLsizei n, GLuint* arrays) {
    if (n < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    for (GLsizei i = 0; i < n; ++i) arrays[i] = NewName(g_vaos, g_next_vao);
}

void APIENTRY glDeleteVertexArrays(GLsizei n, const GLuint* arrays) {
    if (n < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    for (GLsizei i = 0; i < n; ++i) {
        auto it = g_vaos.find(arrays[i]);
        if (it == g_vaos.end()) continue;
        if (g_bound_vao == arrays[i]) g_bound_vao = 0;
        g_vaos.erase(it);
    }
}

void APIENTRY glBindVertexArray(GLuint array) {
    if (array != 0 && !g_vaos.count(array)) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    g_bound_vao = array;
}

GLboolean APIENTRY glIsVertexArray(GLuint array) {
    return g_vaos.count(array) ? GL_TRUE : GL_FALSE;
}

void APIENTRY glGenBuffers(GLsizei n, GLuint* buffers) {
    if (n < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    for (GLsizei i = 0; i < n; ++i) {
        while (g_buffers.count(g_next_buffer)) ++g_next_buffer;
        buffers[i] = g_next_buffer++;
        g_buffers.emplace(buffers[i], BufferData{});
    }
}

void APIENTRY glDeleteBuffers(GLsizei n, const GLuint* buffers) {
    if (n < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    for (GLsizei i = 0; i < n; ++i) {
        auto it = g_buffers.find(buffers[i]);
        if (it == g_buffers.end()) continue;
        if (g_bound_array_buffer == buffers[i]) g_bound_array_buffer = 0;
        if (g_bound_element_buffer == buffers[i]) g_bound_element_buffer = 0;
        g_buffers.erase(it);
    }
}

GLboolean APIENTRY glIsBuffer(GLuint buffer) {
    return g_buffers.count(buffer) ? GL_TRUE : GL_FALSE;
}

void APIENTRY glBindBuffer(GLenum target, GLuint buffer) {
    if (buffer != 0 && !g_buffers.count(buffer)) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    switch (target) {
        case GL_ARRAY_BUFFER: g_bound_array_buffer = buffer; break;
        case GL_ELEMENT_ARRAY_BUFFER: g_bound_element_buffer = buffer; break;
        default:
            PUSH_ERROR(GL_INVALID_ENUM);
            return;
    }
}

void APIENTRY glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
    (void)usage;
    GLuint* bound = nullptr;
    switch (target) {
        case GL_ARRAY_BUFFER: bound = &g_bound_array_buffer; break;
        case GL_ELEMENT_ARRAY_BUFFER: bound = &g_bound_element_buffer; break;
        default: PUSH_ERROR(GL_INVALID_ENUM); return;
    }
    if (*bound == 0) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    if (size < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    auto it = g_buffers.find(*bound);
    if (data) {
        it->second.data.assign((const uint8_t*)data, (const uint8_t*)data + size);
    } else {
        it->second.data.assign((size_t)size, 0);
    }
}

void APIENTRY glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data) {
    GLuint* bound = nullptr;
    switch (target) {
        case GL_ARRAY_BUFFER: bound = &g_bound_array_buffer; break;
        case GL_ELEMENT_ARRAY_BUFFER: bound = &g_bound_element_buffer; break;
        default: PUSH_ERROR(GL_INVALID_ENUM); return;
    }
    if (*bound == 0) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    if (offset < 0 || size < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    auto it = g_buffers.find(*bound);
    if (offset + size > (GLintptr)it->second.data.size()) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    std::memcpy(it->second.data.data() + offset, data, size);
}

void APIENTRY glEnableVertexAttribArray(GLuint index) {
    if (index >= kMaxAttribs) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    g_vaos[g_bound_vao].attribs[index].enabled = true;
}

void APIENTRY glDisableVertexAttribArray(GLuint index) {
    if (index >= kMaxAttribs) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    g_vaos[g_bound_vao].attribs[index].enabled = false;
}

void APIENTRY glVertexAttribPointer(GLuint index, GLint size, GLenum type,
                                    GLboolean normalized, GLsizei stride,
                                    const void* pointer) {
    if (index >= kMaxAttribs) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (size < 1 || size > 4) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (type != GL_FLOAT && type != GL_HALF_FLOAT && type != GL_DOUBLE &&
        type != GL_BYTE && type != GL_UNSIGNED_BYTE && type != GL_SHORT &&
        type != GL_UNSIGNED_SHORT && type != GL_INT && type != GL_UNSIGNED_INT) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }
    if (stride < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    AttribData& a = g_vaos[g_bound_vao].attribs[index];
    a.enabled = true;
    a.size = size;
    a.type = type;
    a.normalized = normalized;
    a.stride = stride;
    a.offset = (GLsizeiptr)pointer;
    a.buffer = g_bound_array_buffer;
}

void APIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    if (count < 0 || first < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (mode != GL_TRIANGLES) {
        // Only triangles for the milestone; other modes are forwarded as-is
        // to the Vulkan backend which maps them to a triangle list.
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }
    if (count == 0) return;

    sh::Program* prog = sh::GetProgram(s::GetState().current_program);
    if (!prog || !prog->linked) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    if (!v::EnsureInit()) { PUSH_ERROR(GL_INVALID_OPERATION); return; }

    // Interleave enabled attribute payloads into one CPU buffer.
    std::vector<GLuint> enabled_slots;
    std::vector<uint32_t> attr_counts, attr_offsets;
    uint32_t pack_off = 0;
    const VAOData& vao = g_vaos[g_bound_vao];

    for (GLuint slot = 0; slot < kMaxAttribs; ++slot) {
        const AttribData& a = vao.attribs[slot];
        if (!a.enabled) continue;
        if (a.type != GL_FLOAT) {
            PUSH_ERROR(GL_INVALID_OPERATION);  // integer attribs unsupported yet
            return;
        }
        auto bit = g_buffers.find(a.buffer);
        if (bit == g_buffers.end()) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
        GLsizei src_stride = a.stride ? a.stride : a.size * 4;
        GLsizeiptr need = a.offset + (GLsizeiptr)(first + count - 1) * src_stride +
                          a.size * 4;
        if (need > (GLsizeiptr)bit->second.data.size()) {
            PUSH_ERROR(GL_INVALID_OPERATION);
            return;
        }
        enabled_slots.push_back(slot);
        attr_counts.push_back((uint32_t)a.size);
        attr_offsets.push_back(pack_off);
        pack_off += (uint32_t)a.size * 4;
    }
    if (attr_counts.empty()) return;

    uint32_t stride = pack_off;
    std::vector<GLfloat> verts((size_t)count * stride / 4);
    for (size_t k = 0; k < enabled_slots.size(); ++k) {
        const AttribData& a = vao.attribs[enabled_slots[k]];
        GLsizei src_stride = a.stride ? a.stride : a.size * 4;
        const uint8_t* src = g_buffers[a.buffer].data.data() + a.offset;
        uint32_t dst_off = attr_offsets[k];
        for (GLsizei i = 0; i < count; ++i) {
            std::memcpy(verts.data() + (size_t)i * stride / 4 + dst_off / 4,
                        src + (size_t)(first + i) * src_stride, (size_t)a.size * 4);
        }
    }

    // vk program handle, created lazily from the linked SPIR-V.
    auto vit = g_vk_programs.find(prog->id);
    uint64_t vprog = 0;
    if (vit == g_vk_programs.end()) {
        vprog = v::CreateProgram(prog->vertex_spirv, prog->fragment_spirv);
        if (!vprog) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
        g_vk_programs.emplace(prog->id, vprog);
    } else {
        vprog = vit->second;
    }

    // uniform values by name (empty vectors are skipped downstream).
    std::unordered_map<std::string, std::vector<float>> uniforms;
    for (const auto& u : prog->uniforms)
        uniforms[u.name] = u.value;

    v::DrawInterleaved(vprog, verts, stride, attr_counts, attr_offsets, uniforms);
}

void APIENTRY glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                           GLenum format, GLenum type, void* pixels) {
    if (format != GL_RGBA || type != GL_UNSIGNED_BYTE || width < 0 || height < 0) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    if (!pixels) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    if (!v::EnsureInit()) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    v::SubmitFlush();
    v::ReadPixels(x, y, width, height, pixels);
}

} // extern "C"