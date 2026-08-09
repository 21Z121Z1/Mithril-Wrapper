// Mithril-Wrapper GL entry points -- S3 draw domain (M3).
// DrawCommon/LoadIndices/DrawElementsImpl plus the draw-entry families
// (DrawArrays/DrawElements/MultiDraw) and glReadPixels. The attribute
// fetch helpers live here because fetching only happens at draw time
// (their declarations are in internal.h).

#include "internal.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace {
namespace sh = mithril::shader;
float HalfToFloat(uint16_t h) {
    uint32_t sign = (uint32_t)(h & 0x8000u) << 16;
    uint32_t exp = (h >> 10) & 0x1Fu;
    uint32_t mant = h & 0x3FFu;
    uint32_t f;
    if (exp == 0) {  // subnormal / zero
        if (mant == 0) { f = sign; }
        else {
            int e = -14;
            while (!(mant & 0x400u)) { mant <<= 1; --e; }
            mant &= 0x3FFu;
            f = sign | ((uint32_t)(e + 127) << 23) | (mant << 13);
        }
    } else if (exp == 31) {  // inf / NaN
        f = sign | 0x7F800000u | (mant << 13);
    } else {
        f = sign | ((exp - 15 + 127) << 23) | (mant << 13);
    }
    float out;
    std::memcpy(&out, &f, 4);
    return out;
}

// ---- shared attribute-state helpers (M3) ------------------------------------

uint32_t AttribTypeSize(GLenum type) {
    switch (type) {
        case GL_BYTE: case GL_UNSIGNED_BYTE: return 1;
        case GL_SHORT: case GL_UNSIGNED_SHORT: case GL_HALF_FLOAT: return 2;
        case GL_FLOAT: case GL_INT: case GL_UNSIGNED_INT: return 4;
        case GL_DOUBLE: return 8;
        default: return 0;
    }
}

// Read `count` components of `type` at `p` into float, honouring the
// normalized flag. Integer types read the raw integer value as float32
// (kept exact for |v| < 2^24, which covers MC's attribute usage).
void FetchComponents(const uint8_t* p, GLenum type, GLboolean normalized,
                     float* out, GLuint count) {
    switch (type) {
        case GL_FLOAT:
            for (GLuint i = 0; i < count; ++i) out[i] = ((const float*)p)[i];
            break;
        case GL_HALF_FLOAT:
            for (GLuint i = 0; i < count; ++i)
                out[i] = static_cast<float>(
                    HalfToFloat(((const uint16_t*)p)[i]));
            break;
        case GL_DOUBLE:
            for (GLuint i = 0; i < count; ++i)
                out[i] = (float)((const double*)p)[i];
            break;
        case GL_BYTE:
            for (GLuint i = 0; i < count; ++i)
                out[i] = normalized ? std::max(-1.0f, ((const int8_t*)p)[i] / 127.0f)
                                    : (float)((const int8_t*)p)[i];
            break;
        case GL_UNSIGNED_BYTE:
            for (GLuint i = 0; i < count; ++i)
                out[i] = normalized ? ((const uint8_t*)p)[i] / 255.0f
                                    : (float)((const uint8_t*)p)[i];
            break;
        case GL_SHORT:
            for (GLuint i = 0; i < count; ++i)
                out[i] = normalized ? std::max(-1.0f, ((const int16_t*)p)[i] / 32767.0f)
                                    : (float)((const int16_t*)p)[i];
            break;
        case GL_UNSIGNED_SHORT:
            for (GLuint i = 0; i < count; ++i)
                out[i] = normalized ? ((const uint16_t*)p)[i] / 65535.0f
                                    : (float)((const uint16_t*)p)[i];
            break;
        case GL_INT:
            for (GLuint i = 0; i < count; ++i)
                out[i] = normalized ? std::max(-1.0f, ((const int32_t*)p)[i] / 2147483647.0f)
                                    : (float)((const int32_t*)p)[i];
            break;
        case GL_UNSIGNED_INT:
            for (GLuint i = 0; i < count; ++i)
                out[i] = normalized ? ((const uint32_t*)p)[i] / 4294967295.0f
                                    : (float)((const uint32_t*)p)[i];
            break;
        default:
            for (GLuint i = 0; i < count; ++i) out[i] = 0.0f;
            break;
    }
}

} // namespace

extern "C" {

// ---- draw (M3) -------------------------------------------------------------

namespace {

int GLModeToTopology(GLenum mode) {
    switch (mode) {
        case GL_TRIANGLES: return 0;
        case GL_TRIANGLE_STRIP: return 1;
        case GL_TRIANGLE_FAN: return 2;
        default: return -1;
    }
}

uint64_t CreateVProgram(sh::Program* prog) {
    auto it = g_vk_programs.find(prog->id);
    if (it != g_vk_programs.end()) return it->second;
    uint64_t handle = v::CreateProgram(prog->vertex_spirv, prog->fragment_spirv);
    if (handle) g_vk_programs.emplace(prog->id, handle);
    return handle;
}

std::unordered_map<std::string, std::vector<float>> ComposeUniforms(
    sh::Program* prog) {
    std::unordered_map<std::string, std::vector<float>> uniforms;
    for (const auto& u : prog->uniforms) uniforms[u.name] = u.value;
    return uniforms;
}

// Snapshot the current GL context into the backend's pipeline state. The
// engine bakes these into the pipeline cache key and the Vk*CreateInfo
// structs; values are forwarded as GL enums (backend maps them once).
v::PipelineState BuildPipelineState() {
    const s::GLState& st = s::GetState();
    v::PipelineState ps;
    ps.scissor_test = st.caps.Test(GL_SCISSOR_TEST);
    ps.depth_test = st.caps.Test(GL_DEPTH_TEST);
    ps.depth_func = st.depth.func;
    ps.depth_write = st.depth.mask;
    ps.stencil_test = st.caps.Test(GL_STENCIL_TEST);
    ps.stencil_front_func = st.stencil_front.func;
    ps.stencil_back_func = st.stencil_back.func;
    ps.stencil_front_ref = st.stencil_front.ref;
    ps.stencil_back_ref = st.stencil_back.ref;
    ps.stencil_front_read_mask = st.stencil_front.mask;
    ps.stencil_back_read_mask = st.stencil_back.mask;
    ps.stencil_front_write_mask = st.stencil_front.write_mask;
    ps.stencil_back_write_mask = st.stencil_back.write_mask;
    ps.stencil_front_op_fail = st.stencil_front.op_fail;
    ps.stencil_front_op_zfail = st.stencil_front.op_zfail;
    ps.stencil_front_op_zpass = st.stencil_front.op_zpass;
    ps.stencil_back_op_fail = st.stencil_back.op_fail;
    ps.stencil_back_op_zfail = st.stencil_back.op_zfail;
    ps.stencil_back_op_zpass = st.stencil_back.op_zpass;
    ps.blend_enable = st.caps.Test(GL_BLEND);
    ps.blend_src_rgb = st.blend.src_rgb;
    ps.blend_dst_rgb = st.blend.dst_rgb;
    ps.blend_src_alpha = st.blend.src_alpha;
    ps.blend_dst_alpha = st.blend.dst_alpha;
    ps.blend_eq_rgb = st.blend.eq_rgb;
    ps.blend_eq_alpha = st.blend.eq_alpha;
    ps.blend_color[0] = st.blend.color[0];
    ps.blend_color[1] = st.blend.color[1];
    ps.blend_color[2] = st.blend.color[2];
    ps.blend_color[3] = st.blend.color[3];
    ps.cull_test = st.caps.Test(GL_CULL_FACE);
    ps.cull_face = st.cull_face;
    ps.front_face = st.front_face;
    ps.polygon_mode = st.polygon_mode;
    ps.poly_offset_factor = st.poly_offset_factor;
    ps.poly_offset_units = st.poly_offset_units;
    ps.color_wmask_r = st.color_wmask[0];
    ps.color_wmask_g = st.color_wmask[1];
    ps.color_wmask_b = st.color_wmask[2];
    ps.color_wmask_a = st.color_wmask[3];
    return ps;
}

// Fetch `size` components of attribute `a` for source buffer row `row`.
// Applies buffer lookup, stride, type size, normalization, half/double
// conversion, or the generic constant when the array is disabled/unbound.
bool FetchAttribRow(const AttribData& a, GLint row, GLfloat* out) {
    if (!a.is_pointer || a.buffer == 0) {
        for (GLuint i = 0; i < (GLuint)a.size; ++i) out[i] = a.constant[i];
        return true;
    }
    if (row < 0) return false;
    auto bit = g_buffers.find(a.buffer);
    if (bit == g_buffers.end()) return false;
    GLuint type_sz = AttribTypeSize(a.type);
    GLsizei src_stride = a.stride ? a.stride : (GLsizei)(a.size * type_sz);
    size_t src = (size_t)a.offset + (size_t)row * src_stride;
    if (src + (size_t)a.size * type_sz > bit->second.data.size()) return false;
    FetchComponents(bit->second.data.data() + src, a.type, a.normalized, out,
                    (GLuint)a.size);
    return true;
}

// Core draw: resolve the current VAO into float32 streams and hand them to
// the Vulkan backend. `idx` holds raw indices (glDrawElements path); when
// empty, `first`/`count` describe a glDrawArrays-style range and `base_vertex`
// is ignored.
void DrawCommon(GLenum mode, const std::vector<uint32_t>& idx, GLint first,
                GLsizei count, GLint base_vertex, GLsizei instance_count) {
    if (count < 0 || first < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (instance_count < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    int topo = GLModeToTopology(mode);
    if (topo < 0) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    if (count == 0 || instance_count == 0) return;

    sh::Program* prog = sh::GetProgram(s::GetState().current_program);
    if (!prog || !prog->linked) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    if (!v::EnsureInit()) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    // Replay texture uploads that happened before the backend came up.
    if (!g_dirty_textures.empty()) FlushDirtyTextureUploads();
    if (!CreateVProgram(prog)) { PUSH_ERROR(GL_INVALID_OPERATION); return; }

    const VAOData& vao = g_vaos[g_bound_vao];

    std::vector<GLuint> vertex_slots;    // enabled, divisor == 0
    std::vector<GLuint> instance_slots;  // enabled, divisor != 0
    for (GLuint slot = 0; slot < kMaxAttribs; ++slot) {
        const AttribData& a = vao.attribs[slot];
        if (!a.enabled) continue;
        (a.divisor ? instance_slots : vertex_slots).push_back(slot);
    }
    if (vertex_slots.empty() && instance_slots.empty()) return;

    // Rows referenced by each payload record: glDrawArrays maps payload row
    // i to buffer row (first + i); glDrawElements maps payload row i to
    // buffer row (base_vertex + i) and builds `v_count` payload records
    // indexed by raw index value.
    GLint row_base = idx.empty() ? first : base_vertex;
    GLsizei v_count = 0;
    if (idx.empty()) {
        v_count = count;
    } else {
        uint32_t m = 0;
        for (uint32_t i : idx) m = std::max(m, i);
        v_count = (GLsizei)(m + 1);
    }

    v::VertexStream vstream;
    if (!vertex_slots.empty()) {
        uint32_t off = 0;
        for (GLuint slot : vertex_slots) {
            v::VertexAttr va;
            va.location = slot;
            va.components = (uint32_t)vao.attribs[slot].size;
            va.offset = off;
            off += (uint32_t)vao.attribs[slot].size * 4;
            vstream.attrs.push_back(va);
        }
        vstream.stride = off;
        std::vector<float> verts((size_t)v_count * off / 4);
        for (GLsizei i = 0; i < v_count; ++i) {
            size_t rec = (size_t)i * off / 4;
            for (size_t k = 0; k < vertex_slots.size(); ++k) {
                const AttribData& a = vao.attribs[vertex_slots[k]];
                float comps[4];
                if (!FetchAttribRow(a, row_base + i, comps)) {
                    PUSH_ERROR(GL_INVALID_OPERATION);
                    return;
                }
                size_t dst = rec + vstream.attrs[k].offset / 4;
                for (uint32_t c = 0; c < (uint32_t)a.size; ++c)
                    verts[dst + c] = comps[c];
            }
        }
        vstream.data = std::move(verts);
    }

    v::VertexStream istream;
    if (!instance_slots.empty()) {
        GLuint divisor = vao.attribs[instance_slots.front()].divisor;
        for (GLuint slot : instance_slots) {
            if (vao.attribs[slot].divisor != divisor) {
                PUSH_ERROR(GL_INVALID_OPERATION);  // mixed divisors
                return;
            }
            v::VertexAttr va;
            va.location = slot;
            va.components = (uint32_t)vao.attribs[slot].size;
            istream.attrs.push_back(va);
        }
        // Pack one record per instance: instance i reads attribute buffer
        // row (i / divisor), replicating values when divisor > 1 so the
        // Vulkan per-instance rate matches the GL stepping.
        uint32_t ioff = 0;
        for (auto& attr : istream.attrs) {
            attr.offset = ioff;
            ioff += attr.components * 4;
        }
        istream.stride = ioff;
        std::vector<float> inst((size_t)instance_count * ioff / 4);
        for (GLsizei i = 0; i < instance_count; ++i) {
            GLint src_row = divisor ? (GLint)(i / divisor) : 0;
            size_t rec = (size_t)i * ioff / 4;
            for (size_t k = 0; k < instance_slots.size(); ++k) {
                const AttribData& a = vao.attribs[instance_slots[k]];
                float comps[4];
                if (!FetchAttribRow(a, src_row, comps)) {
                    PUSH_ERROR(GL_INVALID_OPERATION);
                    return;
                }
                size_t dst = rec + istream.attrs[k].offset / 4;
                for (uint32_t c = 0; c < (uint32_t)a.size; ++c)
                    inst[dst + c] = comps[c];
            }
        }
        istream.data = std::move(inst);
    }

    v::DrawParams dp;
    dp.program = CreateVProgram(prog);
    dp.vertex_stream = std::move(vstream);
    dp.instance_stream = std::move(istream);
    dp.indices = idx;  // raw u32 indices into the payload rows
    dp.instance_count = (uint32_t)instance_count;
    dp.topology = (v::Topology)topo;
    dp.uniforms = ComposeUniforms(prog);
    dp.pipeline = BuildPipelineState();
    // Resolve each sampler uniform to the texture bound at its GL unit
    // (the framebelike value glUniform1i wrote; absent -> unit 0).
    dp.sampler_binds.clear();
    for (const auto& smp : prog->samplers) {
        GLint unit = 0;
        auto uit = prog->uniform_by_location.find(smp.location);
        if (uit != prog->uniform_by_location.end() &&
            !prog->uniforms[uit->second].value.empty())
            unit = (GLint)prog->uniforms[uit->second].value[0];
        GLuint tex =
            (unit >= 0 && (GLuint)unit < kMaxTexUnits) ? g_texture_units[unit] : 0;
        dp.sampler_binds.push_back({smp.binding, tex});
    }
    if (!dp.vertex_stream.data.empty()) v::Draw(dp);
}

// Expand element indices from the bound GL_ELEMENT_ARRAY_BUFFER into raw
// uint32 (payload space, base_vertex NOT applied).
std::vector<uint32_t> LoadIndices(GLenum type, const void* indices,
                                  GLsizei count, GLuint start, GLuint end,
                                  GLenum* err) {
    std::vector<uint32_t> out;
    if (g_bound_element_buffer == 0) { *err = GL_INVALID_OPERATION; return out; }
    auto bit = g_buffers.find(g_bound_element_buffer);
    if (bit == g_buffers.end()) { *err = GL_INVALID_OPERATION; return out; }
    GLuint idx_sz;
    switch (type) {
        case GL_UNSIGNED_BYTE: idx_sz = 1; break;
        case GL_UNSIGNED_SHORT: idx_sz = 2; break;
        case GL_UNSIGNED_INT: idx_sz = 4; break;
        default: *err = GL_INVALID_ENUM; return out;
    }
    const std::vector<uint8_t>& raw = bit->second.data;
    GLintptr off = (GLintptr)indices;
    if (off < 0 ||
        off + (GLintptr)count * idx_sz > (GLintptr)raw.size()) {
        *err = GL_INVALID_VALUE;
        return out;
    }
    const uint8_t* p = raw.data() + off;
    out.resize((size_t)count);
    for (GLsizei i = 0; i < count; ++i) {
        GLuint v;
        if (idx_sz == 1) v = p[i];
        else if (idx_sz == 2) v = ((const uint16_t*)p)[i];
        else v = ((const uint32_t*)p)[i];
        if (start != end && (v < start || v > end)) {
            *err = GL_INVALID_VALUE;
            return {};
        }
        out[i] = v;
    }
    return out;
}

void DrawElementsImpl(GLenum mode, GLsizei count, GLenum type,
                      const void* indices, GLint base_vertex,
                      GLsizei instance_count, GLuint start, GLuint end) {
    if (count < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    GLenum err = GL_NO_ERROR;
    std::vector<uint32_t> idx = LoadIndices(type, indices, count, start, end, &err);
    if (err) { PUSH_ERROR(err); return; }
    if (idx.empty()) return;
    DrawCommon(mode, idx, 0, count, base_vertex, instance_count);
}

} // namespace

void APIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    DrawCommon(mode, {}, first, count, 0, 1);
}

void APIENTRY glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count,
                                    GLsizei primcount) {
    DrawCommon(mode, {}, first, count, 0, primcount);
}

void APIENTRY glDrawElements(GLenum mode, GLsizei count, GLenum type,
                             const void* indices) {
    DrawElementsImpl(mode, count, type, indices, 0, 1, 0, 0);
}

void APIENTRY glDrawRangeElements(GLenum mode, GLuint start, GLuint end,
                                  GLsizei count, GLenum type,
                                  const void* indices) {
    DrawElementsImpl(mode, count, type, indices, 0, 1, start, end);
}

void APIENTRY glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type,
                                       const void* indices, GLint basevertex) {
    DrawElementsImpl(mode, count, type, indices, basevertex, 1, 0, 0);
}

void APIENTRY glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type,
                                      const void* indices, GLsizei primcount) {
    DrawElementsImpl(mode, count, type, indices, 0, primcount, 0, 0);
}

void APIENTRY glDrawElementsInstancedBaseVertex(GLenum mode, GLsizei count,
                                                GLenum type,
                                                const void* indices,
                                                GLsizei primcount,
                                                GLint basevertex) {
    DrawElementsImpl(mode, count, type, indices, basevertex, primcount, 0, 0);
}

void APIENTRY glDrawRangeElementsBaseVertex(GLenum mode, GLuint start, GLuint end,
                                            GLsizei count, GLenum type,
                                            const void* indices, GLint basevertex) {
    DrawElementsImpl(mode, count, type, indices, basevertex, 1, start, end);
}

void APIENTRY glMultiDrawArrays(GLenum mode, const GLint* first,
                                const GLsizei* count, GLsizei drawcount) {
    if (drawcount < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    for (GLsizei i = 0; i < drawcount; ++i)
        DrawCommon(mode, {}, first[i], count[i], 0, 1);
}

void APIENTRY glMultiDrawElements(GLenum mode, const GLsizei* count, GLenum type,
                                  const void* const* indices, GLsizei drawcount) {
    if (drawcount < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    for (GLsizei i = 0; i < drawcount; ++i)
        DrawElementsImpl(mode, count[i], type, indices[i], 0, 1, 0, 0);
}

void APIENTRY glMultiDrawElementsBaseVertex(GLenum mode, const GLsizei* count,
                                            GLenum type,
                                            const void* const* indices,
                                            GLsizei drawcount,
                                            const GLint* basevertex) {
    if (drawcount < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    for (GLsizei i = 0; i < drawcount; ++i)
        DrawElementsImpl(mode, count[i], type, indices[i], basevertex[i], 1, 0, 0);
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
