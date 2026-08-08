// Mithril-Wrapper GL entry points -- S4 texture domain (M4).
// Texture object table (name pool + CPU RGBA8 mip mirror + sampler state),
// glTexImage2D-style uploads through vk::UploadTexture, box-filtered mip
// generation, and the unit/binding bookkeeping the draw path needs to
// resolve sampler uniforms to textures.  Unsupported targets/formats stay
// in the CPU mirror but are not uploaded; the engine then binds its 1x1
// white dummy at draw time (no crash, honest log).

#include "internal.h"

#include <algorithm>
#include <cstring>

#include <util/log.h>

namespace {

// ---------------------------------------------------------------------------
// Pixel decode helpers
// ---------------------------------------------------------------------------

uint32_t TypeBytes(GLenum type) {
    switch (type) {
        case GL_UNSIGNED_BYTE: return 1;
        case GL_UNSIGNED_SHORT: return 2;
        case GL_HALF_FLOAT: return 2;
        case GL_FLOAT: return 4;
        default: return 0;
    }
}

uint32_t FormatComponents(GLenum format) {
    switch (format) {
        case GL_RED: return 1;
        case GL_RG: return 2;
        case GL_RGB: case GL_BGR: return 3;
        case GL_RGBA: case GL_BGRA: return 4;
        default: return 0;
    }
}

// Decode one `width`-pixel row of (format,type) data into RGBA8 `dst`.
bool DecodeRowRGBA8(const uint8_t* src, uint8_t* dst, GLsizei width,
                    GLenum format, GLenum type) {
    if (type == GL_UNSIGNED_BYTE) {
        switch (format) {
            case GL_RGBA:
                std::memcpy(dst, src, (size_t)width * 4);
                return true;
            case GL_BGRA:
                for (GLsizei i = 0; i < width; ++i) {
                    dst[i * 4 + 0] = src[i * 4 + 2];
                    dst[i * 4 + 1] = src[i * 4 + 1];
                    dst[i * 4 + 2] = src[i * 4 + 0];
                    dst[i * 4 + 3] = src[i * 4 + 3];
                }
                return true;
            case GL_RGB:
                for (GLsizei i = 0; i < width; ++i) {
                    dst[i * 4 + 0] = src[i * 3 + 0];
                    dst[i * 4 + 1] = src[i * 3 + 1];
                    dst[i * 4 + 2] = src[i * 3 + 2];
                    dst[i * 4 + 3] = 255;
                }
                return true;
            case GL_RED:
                for (GLsizei i = 0; i < width; ++i) {
                    dst[i * 4 + 0] = dst[i * 4 + 1] = dst[i * 4 + 2] = src[i];
                    dst[i * 4 + 3] = 255;
                }
                return true;
            case GL_RG:
                for (GLsizei i = 0; i < width; ++i) {
                    dst[i * 4 + 0] = src[i * 2 + 0];
                    dst[i * 4 + 1] = src[i * 2 + 1];
                    dst[i * 4 + 2] = 0;
                    dst[i * 4 + 3] = 255;
                }
                return true;
        }
    } else if (type == GL_FLOAT) {
        if (format == GL_RGBA || format == GL_RGB) {
            uint32_t n = format == GL_RGBA ? 4 : 3;
            for (GLsizei i = 0; i < width; ++i) {
                for (uint32_t c = 0; c < n; ++c) {
                    float v = ((const float*)src)[i * n + c];
                    dst[i * 4 + c] =
                        (uint8_t)std::min<uint32_t>(255, (uint32_t)(v * 255.0f + 0.5f));
                }
                if (n == 3) dst[i * 4 + 3] = 255;
            }
            return true;
        }
    }
    return false;
}

// Source row stride for `w`-wide pixels given the current UNPACK_* state.
uint32_t UnpackRowBytes(uint32_t w, GLenum format, GLenum type) {
    const s::PixelStore& ps = s::GetState().pixels;
    uint32_t row_px = ps.unpack_row_length ? (uint32_t)ps.unpack_row_length : w;
    uint32_t bytes = row_px * FormatComponents(format) * TypeBytes(type);
    uint32_t align = std::max<uint32_t>(1, (uint32_t)ps.unpack_alignment);
    return ((bytes + align - 1) / align) * align;
}

// Copy a `w x h` (format,type) plane into mip `level` of `st` at (x,y).
bool StoreLevel(TexState& st, uint32_t level, uint32_t x, uint32_t y,
                uint32_t w, uint32_t h, GLenum format, GLenum type,
                const void* data) {
    if (!FormatComponents(format) || !TypeBytes(type)) return false;
    if (st.mip.size() <= level) st.mip.resize(level + 1);
    uint32_t lvl_w = std::max<uint32_t>(1, st.width >> level);
    uint32_t lvl_h = std::max<uint32_t>(1, st.height >> level);
    uint32_t row_src = UnpackRowBytes(w, format, type);
    uint8_t* dst = st.mip[level].data();
    for (uint32_t r = 0; r < h; ++r) {
        if (y + r >= lvl_h) break;
        DecodeRowRGBA8((const uint8_t*)data + (size_t)r * row_src,
                       dst + ((size_t)(y + r) * lvl_w + x) * 4, (GLsizei)w,
                       format, type);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Sampler state -> engine projection
// ---------------------------------------------------------------------------

v::TexSamplerInfo ToSamplerInfo(const TexState& st) {
    v::TexSamplerInfo si;
    si.mag = st.mag_filter == GL_NEAREST ? v::TexFilter::Nearest
                                         : v::TexFilter::Linear;
    bool mip = st.min_filter == GL_NEAREST_MIPMAP_NEAREST ||
               st.min_filter == GL_NEAREST_MIPMAP_LINEAR ||
               st.min_filter == GL_LINEAR_MIPMAP_NEAREST ||
               st.min_filter == GL_LINEAR_MIPMAP_LINEAR;
    si.mip = mip;
    si.min = (st.min_filter == GL_NEAREST ||
              st.min_filter == GL_NEAREST_MIPMAP_NEAREST)
                 ? v::TexFilter::Nearest
                 : v::TexFilter::Linear;
    si.wrap_s = st.wrap_s;
    si.wrap_t = st.wrap_t;
    return si;
}

// Push the CPU mirror of `id` (level 0 + any explicit chain) to the engine.
void ReUpload(TexState& st, GLuint id) {
    if (!st.has_image || st.mip.empty()) return;
    if (st.target != GL_TEXTURE_2D && st.target != GL_TEXTURE_1D) {
        ML_LOG_DEBUG("gl: target %x not served yet; texture %u stays dummy",
                     st.target, id);
        return;
    }
    v::TexUpload img;
    img.width = st.width;
    img.height = st.height;
    uint32_t w = st.width, h = st.height;
    for (uint32_t l = 0; l < st.mip.size(); ++l) {
        if (st.mip[l].size() != (size_t)w * h * 4) break;
        img.mip.push_back(st.mip[l]);
        w = std::max<uint32_t>(1, w / 2);
        h = std::max<uint32_t>(1, h / 2);
        if (w == 1 && h == 1) break;
    }
    if (img.mip.empty()) return;
    ML_LOG_DEBUG("gl: upload texture %u (%ux%u, %zu mips)", id, img.width,
                 img.height, img.mip.size());
    v::UploadTexture(id, img, ToSamplerInfo(st));
}

// Box-filter one level into the next (average 2x2 neighbourhood).
void GenerateNextMip(const std::vector<uint8_t>& src, uint32_t sw, uint32_t sh,
                     std::vector<uint8_t>& dst) {
    uint32_t dw = std::max<uint32_t>(1, sw / 2);
    uint32_t dh = std::max<uint32_t>(1, sh / 2);
    dst.assign((size_t)dw * dh * 4, 0);
    for (uint32_t y = 0; y < dh; ++y) {
        for (uint32_t x = 0; x < dw; ++x) {
            uint32_t sum[4] = {0, 0, 0, 0};
            uint32_t n = 0;
            for (uint32_t dy = 0; dy < 2; ++dy) {
                for (uint32_t dx = 0; dx < 2; ++dx) {
                    uint32_t sx = std::min<uint32_t>(sw - 1, x * 2 + dx);
                    uint32_t sy = std::min<uint32_t>(sh - 1, y * 2 + dy);
                    const uint8_t* p = &src[((size_t)sy * sw + sx) * 4];
                    for (uint32_t c = 0; c < 4; ++c) sum[c] += p[c];
                    ++n;
                }
            }
            uint8_t* d = &dst[((size_t)y * dw + x) * 4];
            for (uint32_t c = 0; c < 4; ++c) d[c] = (uint8_t)(sum[c] / n);
        }
    }
}

// The texture bound to the active texture unit (0 when none).
GLuint ActiveBound() {
    GLuint unit = (GLuint)(s::GetState().active_texture - GL_TEXTURE0);
    return unit < kMaxTexUnits ? g_texture_units[unit] : 0;
}

} // namespace

void FlushDirtyTextureUploads() {
    std::vector<GLuint> ids(g_dirty_textures.begin(), g_dirty_textures.end());
    for (GLuint id : ids) {
        auto it = g_textures.find(id);
        if (it == g_textures.end()) { g_dirty_textures.erase(id); continue; }
        // ReUpload is a no-op until the backend exists; ids uploaded while
        // !IsInitialized() stay dirty for the flush at the first draw.
        ReUpload(it->second, id);
        if (v::IsInitialized()) g_dirty_textures.erase(id);
    }
}

// Mark `id` as needing a (re)upload and try it immediately when the backend
// is already up. Uploads made before vk::EnsureInit are replayed by
// FlushDirtyTextureUploads at the first draw.
void MarkTextureDirty(TexState& st, GLuint id) {
    if (!st.has_image || st.mip.empty()) return;
    g_dirty_textures.insert(id);
    if (v::IsInitialized()) FlushDirtyTextureUploads();
}

extern "C" {

// ---- texture objects ------------------------------------------------------

void APIENTRY glGenTextures(GLsizei n, GLuint* textures) {
    if (n < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (!textures || n == 0) return;
    for (GLsizei i = 0; i < n; ++i) {
        while (g_textures.count(g_next_texture)) ++g_next_texture;
        textures[i] = g_next_texture++;
        g_textures.emplace(textures[i], TexState{});
    }
}

void APIENTRY glDeleteTextures(GLsizei n, const GLuint* textures) {
    if (n < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (!textures) return;
    for (GLsizei i = 0; i < n; ++i) {
        for (auto& unit : g_texture_units)
            if (unit == textures[i]) unit = 0;
        g_textures.erase(textures[i]);
        v::DestroyResidentTexture(textures[i]);
    }
}

GLboolean APIENTRY glIsTexture(GLuint texture) {
    return g_textures.count(texture) ? GL_TRUE : GL_FALSE;
}

void APIENTRY glBindTexture(GLenum target, GLuint texture) {
    bool valid = target == GL_TEXTURE_1D || target == GL_TEXTURE_2D ||
                 target == GL_TEXTURE_3D || target == GL_TEXTURE_CUBE_MAP ||
                 target == GL_TEXTURE_1D_ARRAY || target == GL_TEXTURE_2D_ARRAY;
    if (!valid) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    if (texture != 0 && !g_textures.count(texture)) {
        PUSH_ERROR(GL_INVALID_OPERATION);   // not a generated name
        return;
    }
    GLuint unit = (GLuint)(s::GetState().active_texture - GL_TEXTURE0);
    if (texture == 0) {
        if (unit < kMaxTexUnits) g_texture_units[unit] = 0;
        return;
    }
    TexState& st = g_textures[texture];
    if (st.target != GL_TEXTURE_2D && st.target != GL_TEXTURE_CUBE_MAP &&
        st.target != target) {
        PUSH_ERROR(GL_INVALID_OPERATION);   // first bind fixes the target
        return;
    }
    st.target = target;
    if (unit < kMaxTexUnits) g_texture_units[unit] = texture;
}

void APIENTRY glActiveTexture(GLenum texture) {
    if (texture < GL_TEXTURE0 ||
        texture >= GL_TEXTURE0 + (GLenum)kMaxTexUnits) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }
    s::GetState().active_texture = texture;
}

// ---- texture image upload -------------------------------------------------

void APIENTRY glTexImage2D(GLenum target, GLint level, GLint internalformat,
                           GLsizei width, GLsizei height, GLint border,
                           GLenum format, GLenum type, const void* pixels) {
    (void)internalformat;   // everything is normalized to RGBA8 in the mirror
    if (border != 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (width < 0 || height < 0 || level < 0) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    if (target != GL_TEXTURE_2D) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    GLuint id = ActiveBound();
    if (id == 0) return;
    TexState& st = g_textures[id];

    if (level == 0) {
        st.width = (uint32_t)width;
        st.height = (uint32_t)height;
        st.has_image = width > 0 && height > 0;
    }
    if (!st.has_image) return;

    st.mip.resize(std::max<size_t>(st.mip.size(), (size_t)level + 1));
    uint32_t lvl_w = std::max<uint32_t>(1, st.width >> level);
    uint32_t lvl_h = std::max<uint32_t>(1, st.height >> level);
    st.mip[level].assign((size_t)lvl_w * lvl_h * 4, 0);
    if (pixels && (uint32_t)width == lvl_w && (uint32_t)height == lvl_h)
        StoreLevel(st, (uint32_t)level, 0, 0, (uint32_t)width, (uint32_t)height,
                   format, type, pixels);

    if (level == 0) MarkTextureDirty(st, id);
}

void APIENTRY glTexImage1D(GLenum target, GLint level, GLint internalformat,
                           GLsizei width, GLint border, GLenum format,
                           GLenum type, const void* pixels) {
    (void)target;  // 1D images live in the same 2D slot on the engine side
    // Engine-side 1D and 2D share the same image; treat as height-1 2D.
    glTexImage2D(GL_TEXTURE_2D, level, internalformat, width, 1, border,
                 format, type, pixels);
}

void APIENTRY glTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                              GLint yoffset, GLsizei width, GLsizei height,
                              GLenum format, GLenum type, const void* pixels) {
    if (target != GL_TEXTURE_2D) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    if (xoffset < 0 || yoffset < 0 || width < 0 || height < 0 || level < 0) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    GLuint id = ActiveBound();
    if (id == 0) return;
    TexState& st = g_textures[id];
    if (!st.has_image || st.mip.size() <= (size_t)level) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    uint32_t lvl_w = std::max<uint32_t>(1, st.width >> level);
    uint32_t lvl_h = std::max<uint32_t>(1, st.height >> level);
    if ((uint32_t)xoffset + (uint32_t)width > lvl_w ||
        (uint32_t)yoffset + (uint32_t)height > lvl_h) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    if (!pixels) return;
    StoreLevel(st, (uint32_t)level, (uint32_t)xoffset, (uint32_t)yoffset,
               (uint32_t)width, (uint32_t)height, format, type, pixels);
    if (level == 0) MarkTextureDirty(st, id);
}

void APIENTRY glTexSubImage1D(GLenum target, GLint level, GLint xoffset,
                              GLsizei width, GLenum format, GLenum type,
                              const void* pixels) {
    (void)target;   // 1D images live in the same 2D slot on the engine side
    glTexSubImage2D(GL_TEXTURE_2D, level, xoffset, 0, width, 1, format, type,
                    pixels);
}

void APIENTRY glGenerateMipmap(GLenum target) {
    if (target != GL_TEXTURE_2D) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    GLuint id = ActiveBound();
    if (id == 0) return;
    TexState& st = g_textures[id];
    if (!st.has_image || st.mip.empty()) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    uint32_t w = st.width, h = st.height;
    while (w > 1 || h > 1) {
        std::vector<uint8_t> next;
        GenerateNextMip(st.mip.back(), w, h, next);
        w = std::max<uint32_t>(1, w / 2);
        h = std::max<uint32_t>(1, h / 2);
        st.mip.push_back(std::move(next));
    }
    MarkTextureDirty(st, id);
}

// ---- sampler parameters ---------------------------------------------------

void APIENTRY glTexParameteri(GLenum target, GLenum pname, GLint param) {
    if (target != GL_TEXTURE_2D && target != GL_TEXTURE_CUBE_MAP) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }
    GLuint id = ActiveBound();
    if (id == 0) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    TexState& st = g_textures[id];
    switch (pname) {
        case GL_TEXTURE_MIN_FILTER: st.min_filter = (GLenum)param; break;
        case GL_TEXTURE_MAG_FILTER: st.mag_filter = (GLenum)param; break;
        case GL_TEXTURE_WRAP_S: st.wrap_s = (GLenum)param; break;
        case GL_TEXTURE_WRAP_T: st.wrap_t = (GLenum)param; break;
        case GL_TEXTURE_WRAP_R: st.wrap_r = (GLenum)param; break;
        case GL_TEXTURE_MIN_LOD:
        case GL_TEXTURE_MAX_LOD:
        case GL_TEXTURE_LOD_BIAS:
        case GL_TEXTURE_BASE_LEVEL:
        case GL_TEXTURE_MAX_LEVEL:
            break;   // accepted; mip chain is rebuilt from CPU mirror anyway
        default:
            PUSH_ERROR(GL_INVALID_ENUM);
            return;
    }
    MarkTextureDirty(st, id);
}

void APIENTRY glTexParameteriv(GLenum target, GLenum pname, const GLint* param) {
    if (!param) return;
    glTexParameteri(target, pname, param[0]);
}

void APIENTRY glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
    glTexParameteri(target, pname, (GLint)param);
}

void APIENTRY glTexParameterfv(GLenum target, GLenum pname, const GLfloat* param) {
    if (!param) return;
    glTexParameteri(target, pname, (GLint)param[0]);
}

void APIENTRY glTexParameterIiv(GLenum target, GLenum pname, const GLint* param) {
    glTexParameteriv(target, pname, param);
}

void APIENTRY glTexParameterIuiv(GLenum target, GLenum pname, const GLuint* param) {
    if (!param) return;
    glTexParameteri(target, pname, (GLint)param[0]);
}

// ---- queries --------------------------------------------------------------

void APIENTRY glGetTexParameteriv(GLenum target, GLenum pname, GLint* params) {
    if (target != GL_TEXTURE_2D && target != GL_TEXTURE_CUBE_MAP) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }
    if (!params) return;
    GLuint id = ActiveBound();
    if (id == 0) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    const TexState& st = g_textures[id];
    switch (pname) {
        case GL_TEXTURE_MIN_FILTER: *params = st.min_filter; break;
        case GL_TEXTURE_MAG_FILTER: *params = st.mag_filter; break;
        case GL_TEXTURE_WRAP_S: *params = st.wrap_s; break;
        case GL_TEXTURE_WRAP_T: *params = st.wrap_t; break;
        case GL_TEXTURE_WRAP_R: *params = st.wrap_r; break;
        case GL_TEXTURE_MIN_LOD: *params = 0; break;
        case GL_TEXTURE_MAX_LOD: *params = 1000; break;
        case GL_TEXTURE_LOD_BIAS: *params = 0; break;
        default: PUSH_ERROR(GL_INVALID_ENUM); return;
    }
}

void APIENTRY glGetTexParameterfv(GLenum target, GLenum pname, GLfloat* params) {
    GLint v = 0;
    glGetTexParameteriv(target, pname, &v);
    if (params) *params = (GLfloat)v;
}

void APIENTRY glGetTexParameterIiv(GLenum target, GLenum pname, GLint* params) {
    glGetTexParameteriv(target, pname, params);
}

void APIENTRY glGetTexParameterIuiv(GLenum target, GLenum pname, GLuint* params) {
    GLint v = 0;
    glGetTexParameteriv(target, pname, &v);
    if (params) *params = (GLuint)v;
}

void APIENTRY glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname,
                                       GLint* params) {
    if (target != GL_TEXTURE_2D || level < 0) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    if (!params) return;
    GLuint id = ActiveBound();
    if (id == 0) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    const TexState& st = g_textures[id];
    if (st.mip.size() <= (size_t)level) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    uint32_t lvl_w = std::max<uint32_t>(1, st.width >> level);
    uint32_t lvl_h = std::max<uint32_t>(1, st.height >> level);
    switch (pname) {
        case GL_TEXTURE_WIDTH: *params = (GLint)lvl_w; break;
        case GL_TEXTURE_HEIGHT: *params = (GLint)lvl_h; break;
        case GL_TEXTURE_INTERNAL_FORMAT: *params = GL_RGBA8; break;
        default: PUSH_ERROR(GL_INVALID_ENUM); return;
    }
}

void APIENTRY glGetTexLevelParameterfv(GLenum target, GLint level, GLenum pname,
                                       GLfloat* params) {
    GLint v = 0;
    glGetTexLevelParameteriv(target, level, pname, &v);
    if (params) *params = (GLfloat)v;
}

} // extern "C"

// ---- shared table storage (declared in internal.h) ------------------------

std::unordered_map<GLuint, TexState> g_textures;
std::array<GLuint, kMaxTexUnits> g_texture_units{};
GLuint g_next_texture = 1;
std::unordered_set<GLuint> g_dirty_textures;