// Mithril-Wrapper GL entry points -- S4 texture domain (M4).
// Texture object table (name pool + CPU RGBA8 mip mirror + sampler state),
// glTexImage*/TexSubImage* uploads through backend::UploadTexture, box-filtered
// mip generation, S3TC decompression, framebuffer copies, and the
// unit/binding bookkeeping the draw path needs to resolve sampler uniforms
// to textures.  Unsupported targets/formats stay in the CPU mirror but are
// not uploaded; the engine then binds its 1x1 white dummy at draw time (no
// crash, honest log).
//
// Layout of the CPU mirror (TexState::mip[level]): every "slice" of the
// level is a width*height*4 plane of RGBA8 rows; slices are concatenated in
// the same order backend::TexUpload expects (3D: z; 2D/1D arrays: layer; cubemap:
// the six faces in POSITIVE_X..NEGATIVE_Z order).

#include "internal.h"

#include <algorithm>
#include <cmath>
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
            case GL_BGR:
                for (GLsizei i = 0; i < width; ++i) {
                    dst[i * 4 + 0] = src[i * 3 + 2];
                    dst[i * 4 + 1] = src[i * 3 + 1];
                    dst[i * 4 + 2] = src[i * 3 + 0];
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

// Reverse of DecodeRowRGBA8 for glGetTexImage output.
void EncodeRowRGBA8(const uint8_t* src, uint8_t* dst, GLsizei width,
                    GLenum format, GLenum type) {
    if (type == GL_UNSIGNED_BYTE) {
        switch (format) {
            case GL_RGBA:
                std::memcpy(dst, src, (size_t)width * 4);
                return;
            case GL_BGRA:
                for (GLsizei i = 0; i < width; ++i) {
                    dst[i * 4 + 0] = src[i * 4 + 2];
                    dst[i * 4 + 1] = src[i * 4 + 1];
                    dst[i * 4 + 2] = src[i * 4 + 0];
                    dst[i * 4 + 3] = src[i * 4 + 3];
                }
                return;
            case GL_RGB:
                for (GLsizei i = 0; i < width; ++i) {
                    dst[i * 3 + 0] = src[i * 4 + 0];
                    dst[i * 3 + 1] = src[i * 4 + 1];
                    dst[i * 3 + 2] = src[i * 4 + 2];
                }
                return;
            case GL_BGR:
                for (GLsizei i = 0; i < width; ++i) {
                    dst[i * 3 + 0] = src[i * 4 + 2];
                    dst[i * 3 + 1] = src[i * 4 + 1];
                    dst[i * 3 + 2] = src[i * 4 + 0];
                }
                return;
            case GL_RED:
                for (GLsizei i = 0; i < width; ++i) dst[i] = src[i * 4];
                return;
            case GL_RG:
                for (GLsizei i = 0; i < width; ++i) {
                    dst[i * 2 + 0] = src[i * 4 + 0];
                    dst[i * 2 + 1] = src[i * 4 + 1];
                }
                return;
        }
    } else if (type == GL_FLOAT) {
        if (format == GL_RGBA || format == GL_RGB) {
            uint32_t n = format == GL_RGBA ? 4 : 3;
            for (GLsizei i = 0; i < width; ++i)
                for (uint32_t c = 0; c < n; ++c)
                    ((float*)dst)[i * n + c] = src[i * 4 + c] / 255.0f;
            return;
        }
    }
}

// Row stride of `w`-wide pixels using the UNPACK_* pixel store state; the
// PACK variant reads pack_* instead.
uint32_t RowBytes(uint32_t w, GLenum format, GLenum type, bool pack) {
    const s::PixelStore& ps = s::GetState().pixels;
    uint32_t row_px = (pack ? ps.pack_row_length : ps.unpack_row_length)
                          ? (pack ? (uint32_t)ps.pack_row_length
                                  : (uint32_t)ps.unpack_row_length)
                          : w;
    uint32_t bytes = row_px * FormatComponents(format) * TypeBytes(type);
    uint32_t align =
        std::max<uint32_t>(1, (uint32_t)(pack ? ps.pack_alignment
                                              : ps.unpack_alignment));
    return ((bytes + align - 1) / align) * align;
}

uint32_t UnpackRowBytes(uint32_t w, GLenum format, GLenum type) {
    return RowBytes(w, format, type, false);
}

// Rows per plane (uncompressed GL data): unpack/pack image height override.
uint32_t PlaneRows(uint32_t h, bool pack) {
    GLint ih = pack ? s::GetState().pixels.pack_image_height
                    : s::GetState().pixels.unpack_image_height;
    return ih > 0 ? (uint32_t)ih : h;
}

// Byte offset of slice `slice` inside `st.mip[level]`.
uint8_t* SlicePtr(TexState& st, uint32_t level, uint32_t slice) {
    return st.mip[level].data() + (size_t)slice * st.SliceBytes(level);
}

const uint8_t* SlicePtrC(const TexState& st, uint32_t level, uint32_t slice) {
    return st.mip[level].data() + (size_t)slice * st.SliceBytes(level);
}

// GL cubemap face -> layer index (POSITIVE_X=0 .. NEGATIVE_Z=5), matching
// the VkCubeMapFace layer order.
uint32_t CubeFaceIndex(GLenum target) {
    return (uint32_t)(target - GL_TEXTURE_CUBE_MAP_POSITIVE_X);
}

bool IsCubeFace(GLenum target) {
    return target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
           target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z;
}

// Zero-initialise level `level` of `st` for its full slice count. Missing
// or resized levels are (re)allocated; an already-matching buffer is left
// untouched so consecutive cube-face uploads keep earlier faces.
void AllocLevel(TexState& st, uint32_t level) {
    uint32_t lvl_w = std::max<uint32_t>(1, st.width >> level);
    uint32_t lvl_h = std::max<uint32_t>(1, st.height >> level);
    if (st.mip.size() <= level) st.mip.resize((size_t)level + 1);
    if (st.mip[level].size() != (size_t)st.SliceCount() * lvl_w * lvl_h * 4)
        st.mip[level].assign((size_t)st.SliceCount() * lvl_w * lvl_h * 4, 0);
}

// Write `count` planes of w x h pixels starting at slice `slice0`, at the
// destination rectangle (x, y).  `data` follows the UNPACK state (both the
// row padding and the per-plane image height).
void StoreSlices(TexState& st, uint32_t level, uint32_t slice0, uint32_t count,
                 uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                 GLenum format, GLenum type, const void* data) {
    if (!FormatComponents(format) || !TypeBytes(type)) return;
    uint32_t lvl_w = std::max<uint32_t>(1, st.width >> level);
    uint32_t row_src = UnpackRowBytes(w, format, type);
    uint32_t plane_rows = PlaneRows(h, false);
    for (uint32_t s = 0; s < count; ++s) {
        const uint8_t* plane = (const uint8_t*)data + (size_t)s * plane_rows * row_src;
        uint8_t* dst = SlicePtr(st, level, slice0 + s) + ((size_t)y * lvl_w + x) * 4;
        for (uint32_t r = 0; r < h; ++r)
            DecodeRowRGBA8(plane + (size_t)r * row_src, dst + (size_t)r * lvl_w * 4,
                           (GLsizei)w, format, type);
    }
}

// ---------------------------------------------------------------------------
// S3TC (DXT) decompression -> RGBA8
// ---------------------------------------------------------------------------

uint8_t Expand565(uint16_t v, uint32_t shift, uint32_t bits) {
    uint32_t m = (1u << bits) - 1;
    uint32_t c = (v >> shift) & m;
    return (uint8_t)((c * 255 + m / 2) / m);
}

// Decode one DXT1 color block (shared by DXT1/DXT3/DXT5) into `dst` rows
// (always a full 4x4; `rx`/`ry` clamp against the real texture edge).
void DecodeDXT1Colors(const uint8_t* blk, uint8_t* dst, uint32_t dst_w) {
    uint16_t c0 = blk[0] | (uint16_t)(blk[1] << 8);
    uint16_t c1 = blk[2] | (uint16_t)(blk[3] << 8);
    // RGB565 field shifts/masks: R 5 bits @11, G 6 bits @5, B 5 bits @0.
    static const uint32_t shifts[3] = {11, 5, 0};
    static const uint32_t bits[3] = {5, 6, 5};
    uint8_t col[4][4];
    for (int i = 0; i < 3; ++i)
        col[0][i] = Expand565(c0, shifts[i], bits[i]);
    col[0][3] = 255;
    for (int i = 0; i < 3; ++i)
        col[1][i] = Expand565(c1, shifts[i], bits[i]);
    col[1][3] = 255;
    if (c0 > c1) {
        for (int i = 0; i < 3; ++i)
            col[2][i] = (uint8_t)(((unsigned)col[0][i] * 2 + col[1][i]) / 3);
        col[2][3] = 255;
        for (int i = 0; i < 3; ++i)
            col[3][i] = (uint8_t)(((unsigned)col[0][i] + col[1][i] * 2) / 3);
        col[3][3] = 255;
    } else {
        for (int i = 0; i < 3; ++i)
            col[2][i] = (uint8_t)(((unsigned)col[0][i] + col[1][i]) / 2);
        col[2][3] = 255;
        col[3][0] = col[3][1] = col[3][2] = col[3][3] = 0;  // transparent black
    }
    uint32_t idx = blk[4] | (uint32_t)blk[5] << 8 | (uint32_t)blk[6] << 16 |
                   (uint32_t)blk[7] << 24;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            uint8_t* p = dst + ((size_t)y * dst_w + x) * 4;
            uint32_t i = (idx >> (2 * (y * 4 + x))) & 3;
            std::memcpy(p, col[i], 4);
        }
}

// Decode a DXT3 alpha nibble block onto the alpha channel of `dst`.
void DecodeDXT3Alpha(const uint8_t* blk, uint8_t* dst, uint32_t dst_w) {
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            uint8_t v = (blk[y * 2 + x / 2] >> (x % 2 ? 4 : 0)) & 0xF;
            dst[((size_t)y * dst_w + x) * 4 + 3] = (uint8_t)(v * 17);
        }
}

// Decode a DXT5 alpha block (two endpoints + interpolated 3-bit indices).
void DecodeDXT5Alpha(const uint8_t* blk, uint8_t* dst, uint32_t dst_w) {
    uint8_t a[8];
    uint8_t a0 = blk[0], a1 = blk[1];
    if (a0 > a1) {
        a[0] = a0; a[1] = a1;
        for (int i = 1; i <= 6; ++i)
            a[i + 1] = (uint8_t)(((8 - i) * a0 + i * a1 + 3) / 7);
    } else {
        a[0] = a0; a[1] = a1;
        a[2] = (uint8_t)((4 * a0 + a1 + 2) / 5);
        a[3] = (uint8_t)((3 * a0 + 2 * a1 + 2) / 5);
        a[4] = (uint8_t)((2 * a0 + 3 * a1 + 2) / 5);
        a[5] = (uint8_t)((a0 + 4 * a1 + 2) / 5);
        a[6] = 0; a[7] = 255;
    }
    // 48 bits of 3-bit indices (6 bytes, LSB first).
    uint64_t bits = (uint64_t)blk[2] | (uint64_t)blk[3] << 8 |
                    (uint64_t)blk[4] << 16 | (uint64_t)blk[5] << 24 |
                    (uint64_t)blk[6] << 32 | (uint64_t)blk[7] << 40;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            uint32_t i = (uint32_t)(bits >> (3 * (y * 4 + x))) & 7;
            dst[((size_t)y * dst_w + x) * 4 + 3] = a[i];
        }
}

// Decompress one S3TC-compressed slice plane into RGBA8. `compfmt` decides
// the block size; `w`/`h` are the decoded plane size.
void DecodeCompressedPlane(uint8_t* dst, const uint8_t* src, uint32_t w,
                           uint32_t h, GLenum compfmt) {
    uint32_t bw = std::max<uint32_t>(1, (w + 3) / 4);
    uint32_t bh = std::max<uint32_t>(1, (h + 3) / 4);
    bool dxt3 = compfmt == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
    bool dxt5 = compfmt == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    uint32_t stride = dxt3 || dxt5 ? 16 : 8;
    for (uint32_t by = 0; by < bh; ++by) {
        for (uint32_t bx = 0; bx < bw; ++bx) {
            const uint8_t* blk = src + (size_t)(by * bw + bx) * stride;
            uint8_t block[64];
            DecodeDXT1Colors(blk + (dxt3 || dxt5 ? 8 : 0), block, 4);
            if (dxt3)
                DecodeDXT3Alpha(blk, block, 4);
            else if (dxt5)
                DecodeDXT5Alpha(blk, block, 4);
            uint32_t px = std::min<uint32_t>(4, w - bx * 4);
            uint32_t py = std::min<uint32_t>(4, h - by * 4);
            for (uint32_t y = 0; y < py; ++y)
                for (uint32_t x = 0; x < px; ++x)
                    std::memcpy(dst + (((size_t)(by * 4 + y) * w + bx * 4 + x) * 4),
                                block + ((size_t)(y * 4 + x) * 4), 4);
        }
    }
}

GLsizei CompressedPlaneSize(uint32_t w, uint32_t h, GLenum compfmt) {
    uint32_t bw = std::max<uint32_t>(1, (w + 3) / 4);
    uint32_t bh = std::max<uint32_t>(1, (h + 3) / 4);
    bool dxt3 = compfmt == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
    bool dxt5 = compfmt == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    return (GLsizei)(bw * bh * (dxt3 || dxt5 ? 16 : 8));
}

bool IsS3TC(GLenum fmt) {
    return fmt == GL_COMPRESSED_RGB_S3TC_DXT1_EXT ||
           fmt == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT ||
           fmt == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT ||
           fmt == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
}

// ---------------------------------------------------------------------------
// Sampler state -> engine projection
// ---------------------------------------------------------------------------

template <typename T>
v::TexSamplerInfo ToSamplerInfo(const T& st) {
    v::TexSamplerInfo si;
    si.mag = st.mag_filter == GL_NEAREST ? v::TexFilter::Nearest
                                         : v::TexFilter::Linear;
    si.min = (st.min_filter == GL_NEAREST ||
              st.min_filter == GL_NEAREST_MIPMAP_NEAREST ||
              st.min_filter == GL_NEAREST_MIPMAP_LINEAR)
                 ? v::TexFilter::Nearest
                 : v::TexFilter::Linear;
    if (st.min_filter == GL_NEAREST_MIPMAP_NEAREST ||
        st.min_filter == GL_LINEAR_MIPMAP_NEAREST)
        si.mip = v::TexMipFilter::Nearest;
    else if (st.min_filter == GL_NEAREST_MIPMAP_LINEAR ||
             st.min_filter == GL_LINEAR_MIPMAP_LINEAR)
        si.mip = v::TexMipFilter::Linear;
    else
        si.mip = v::TexMipFilter::None;
    si.wrap_s = st.wrap_s;
    si.wrap_t = st.wrap_t;
    si.wrap_r = st.wrap_r;
    si.min_lod = st.min_lod;
    si.max_lod = st.max_lod;
    si.lod_bias = st.lod_bias;
    si.border_color = st.border_color;
    si.compare_mode = st.compare_mode;
    si.compare_func = st.compare_func;
    return si;
}

// Push the CPU mirror of `id` (level 0 + any explicit chain) to the engine.
void ReUpload(TexState& st, GLuint id) {
    if (!st.has_image || st.mip.empty()) return;
    bool served = st.target == GL_TEXTURE_2D || st.target == GL_TEXTURE_1D ||
                  st.target == GL_TEXTURE_3D || st.target == GL_TEXTURE_CUBE_MAP ||
                  st.target == GL_TEXTURE_2D_ARRAY ||
                  st.target == GL_TEXTURE_1D_ARRAY;
    if (!served) {
        ML_LOG_DEBUG("gl: target %x not served yet; texture %u stays dummy",
                     st.target, id);
        return;
    }
    v::TexUpload img;
    img.width = st.width;
    img.height = st.height;
    img.depth = st.IsCube() ? 1 : st.depth;
    img.is_3d = st.Is3D();
    img.is_cube = st.IsCube();
    img.content_version = st.content_version;
    uint32_t w = st.width, h = st.height;
    size_t slices = st.SliceCount();
    for (uint32_t l = 0; l < st.mip.size(); ++l) {
        if (st.mip[l].size() != slices * (size_t)w * h * 4) break;
        img.mip.push_back(st.mip[l]);
        w = std::max<uint32_t>(1, w / 2);
        h = std::max<uint32_t>(1, h / 2);
    }
    if (img.mip.empty()) return;
    ML_LOG_DEBUG("gl: upload texture %u (%ux%ux%u, %zu mips, %s%s)", id,
                 img.width, img.height, img.depth, img.mip.size(),
                 img.is_cube ? "cube" : img.is_3d ? "3d" : "2d",
                 slices > 1 ? " layered" : "");
    v::UploadTexture(id, img);
}

// Box-filter one level into the next (average 2x2 neighbourhood) for every
// slice of the texture.
void GenerateNextMip(const std::vector<uint8_t>& src, uint32_t sw, uint32_t sh,
                     uint32_t slices, std::vector<uint8_t>& dst) {
    uint32_t dw = std::max<uint32_t>(1, sw / 2);
    uint32_t dh = std::max<uint32_t>(1, sh / 2);
    size_t d_plane = (size_t)dw * dh * 4;
    size_t s_plane = (size_t)sw * sh * 4;
    dst.assign((size_t)slices * d_plane, 0);
    for (uint32_t s = 0; s < slices; ++s) {
        const uint8_t* sbase = src.data() + (size_t)s * s_plane;
        uint8_t* dbase = dst.data() + (size_t)s * d_plane;
        for (uint32_t y = 0; y < dh; ++y) {
            for (uint32_t x = 0; x < dw; ++x) {
                uint32_t sum[4] = {0, 0, 0, 0};
                uint32_t n = 0;
                for (uint32_t dy = 0; dy < 2; ++dy) {
                    for (uint32_t dx = 0; dx < 2; ++dx) {
                        uint32_t sx = std::min<uint32_t>(sw - 1, x * 2 + dx);
                        uint32_t sy = std::min<uint32_t>(sh - 1, y * 2 + dy);
                        const uint8_t* p = &sbase[((size_t)sy * sw + sx) * 4];
                        for (uint32_t c = 0; c < 4; ++c) sum[c] += p[c];
                        ++n;
                    }
                }
                uint8_t* d = &dbase[((size_t)y * dw + x) * 4];
                for (uint32_t c = 0; c < 4; ++c) d[c] = (uint8_t)(sum[c] / n);
            }
        }
    }
}

// The texture bound to the active texture unit (0 when none).
GLuint ActiveBound() {
    GLuint unit = (GLuint)(s::GetState().active_texture - GL_TEXTURE0);
    return unit < kMaxTexUnits ? g_texture_units[unit] : 0;
}

bool ValidMinFilter(GLenum value) {
    return value == GL_NEAREST || value == GL_LINEAR ||
           value == GL_NEAREST_MIPMAP_NEAREST ||
           value == GL_LINEAR_MIPMAP_NEAREST ||
           value == GL_NEAREST_MIPMAP_LINEAR ||
           value == GL_LINEAR_MIPMAP_LINEAR;
}

bool ValidWrap(GLenum value) {
    return value == GL_REPEAT || value == GL_MIRRORED_REPEAT ||
           value == GL_CLAMP_TO_EDGE || value == GL_CLAMP_TO_BORDER;
}

bool ValidCompareFunc(GLenum value) {
    switch (value) {
        case GL_NEVER: case GL_LESS: case GL_EQUAL: case GL_LEQUAL:
        case GL_GREATER: case GL_NOTEQUAL: case GL_GEQUAL: case GL_ALWAYS:
            return true;
        default: return false;
    }
}

template <typename T>
bool SetSamplerScalar(T& state, GLenum pname, GLfloat value, GLenum* error) {
    auto set_enum = [&](GLenum& destination, GLenum next) {
        if (destination == next) return false;
        destination = next;
        return true;
    };
    auto set_float = [&](GLfloat& destination) {
        if (destination == value) return false;
        destination = value;
        return true;
    };
    const GLenum parameter = static_cast<GLenum>(value);
    switch (pname) {
        case GL_TEXTURE_MIN_FILTER:
            if (!ValidMinFilter(parameter)) { *error = GL_INVALID_ENUM; return false; }
            return set_enum(state.min_filter, parameter);
        case GL_TEXTURE_MAG_FILTER:
            if (parameter != GL_NEAREST && parameter != GL_LINEAR) {
                *error = GL_INVALID_ENUM; return false;
            }
            return set_enum(state.mag_filter, parameter);
        case GL_TEXTURE_WRAP_S:
            if (!ValidWrap(parameter)) { *error = GL_INVALID_ENUM; return false; }
            return set_enum(state.wrap_s, parameter);
        case GL_TEXTURE_WRAP_T:
            if (!ValidWrap(parameter)) { *error = GL_INVALID_ENUM; return false; }
            return set_enum(state.wrap_t, parameter);
        case GL_TEXTURE_WRAP_R:
            if (!ValidWrap(parameter)) { *error = GL_INVALID_ENUM; return false; }
            return set_enum(state.wrap_r, parameter);
        case GL_TEXTURE_MIN_LOD: return set_float(state.min_lod);
        case GL_TEXTURE_MAX_LOD: return set_float(state.max_lod);
        case GL_TEXTURE_LOD_BIAS: return set_float(state.lod_bias);
        case GL_TEXTURE_COMPARE_MODE:
            if (parameter != GL_NONE && parameter != GL_COMPARE_REF_TO_TEXTURE) {
                *error = GL_INVALID_ENUM; return false;
            }
            return set_enum(state.compare_mode, parameter);
        case GL_TEXTURE_COMPARE_FUNC:
            if (!ValidCompareFunc(parameter)) { *error = GL_INVALID_ENUM; return false; }
            return set_enum(state.compare_func, parameter);
        default: *error = GL_INVALID_ENUM; return false;
    }
}

template <typename T>
bool SetBorderColor(T& state, const GLfloat* color) {
    if (std::equal(state.border_color.begin(), state.border_color.end(), color))
        return false;
    std::copy(color, color + 4, state.border_color.begin());
    return true;
}

GLfloat SignedBorderToFloat(GLint value) {
    constexpr double denominator = 4294967295.0; // 2^32 - 1
    return static_cast<GLfloat>((2.0 * value + 1.0) / denominator);
}

GLint FloatBorderToSigned(GLfloat value) {
    constexpr double denominator = 4294967295.0;
    const double clamped = std::clamp<double>(value, -1.0, 1.0);
    const double mapped = (clamped * denominator - 1.0) * 0.5;
    return static_cast<GLint>(std::nearbyint(mapped));
}

template <typename T>
GLfloat GetSamplerScalar(const T& state, GLenum pname, GLenum* error) {
    switch (pname) {
        case GL_TEXTURE_MIN_FILTER: return static_cast<GLfloat>(state.min_filter);
        case GL_TEXTURE_MAG_FILTER: return static_cast<GLfloat>(state.mag_filter);
        case GL_TEXTURE_WRAP_S: return static_cast<GLfloat>(state.wrap_s);
        case GL_TEXTURE_WRAP_T: return static_cast<GLfloat>(state.wrap_t);
        case GL_TEXTURE_WRAP_R: return static_cast<GLfloat>(state.wrap_r);
        case GL_TEXTURE_MIN_LOD: return state.min_lod;
        case GL_TEXTURE_MAX_LOD: return state.max_lod;
        case GL_TEXTURE_LOD_BIAS: return state.lod_bias;
        case GL_TEXTURE_COMPARE_MODE: return static_cast<GLfloat>(state.compare_mode);
        case GL_TEXTURE_COMPARE_FUNC: return static_cast<GLfloat>(state.compare_func);
        default: *error = GL_INVALID_ENUM; return 0.0f;
    }
}

} // namespace

v::TexSamplerInfo ResolveSamplerInfo(GLuint unit, const TexState& texture) {
    if (unit < kMaxTexUnits) {
        const auto sampler = g_samplers.find(g_sampler_units[unit]);
        if (sampler != g_samplers.end())
            return ToSamplerInfo(sampler->second);
    }
    return ToSamplerInfo(texture);
}

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
// is already up. Uploads made before backend::EnsureInit are replayed by
// FlushDirtyTextureUploads at the first draw.
void MarkTextureDirty(TexState& st, GLuint id, bool sampler_only = false) {
    if (sampler_only) {
        // Native samplers are resolved and content-keyed at draw time. Image
        // ownership is unchanged by texture sampling-parameter mutations.
        return;
    }
    ++st.content_version;
    if (!st.has_image || st.mip.empty()) return;
    g_dirty_textures.insert(id);
    if (v::IsInitialized()) FlushDirtyTextureUploads();
}

extern "C" {

// ---- texture objects ------------------------------------------------------

void APIENTRY glGenSamplers(GLsizei count, GLuint* samplers) {
    if (count < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (!samplers || count == 0) return;
    for (GLsizei i = 0; i < count; ++i) {
        while (g_samplers.count(g_next_sampler)) ++g_next_sampler;
        samplers[i] = g_next_sampler++;
        g_samplers.emplace(samplers[i], SamplerData{});
    }
}

void APIENTRY glDeleteSamplers(GLsizei count, const GLuint* samplers) {
    if (count < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (!samplers) return;
    for (GLsizei i = 0; i < count; ++i) {
        if (!samplers[i]) continue;
        for (auto& unit : g_sampler_units)
            if (unit == samplers[i]) unit = 0;
        g_samplers.erase(samplers[i]);
    }
}

GLboolean APIENTRY glIsSampler(GLuint sampler) {
    return sampler && g_samplers.count(sampler) ? GL_TRUE : GL_FALSE;
}

void APIENTRY glBindSampler(GLuint unit, GLuint sampler) {
    if (unit >= kMaxTexUnits) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (sampler && !g_samplers.count(sampler)) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    g_sampler_units[unit] = sampler;
}

void APIENTRY glSamplerParameterf(GLuint sampler, GLenum pname, GLfloat param) {
    auto it = g_samplers.find(sampler);
    if (it == g_samplers.end()) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    GLenum error = GL_NO_ERROR;
    (void)SetSamplerScalar(it->second, pname, param, &error);
    if (error != GL_NO_ERROR) PUSH_ERROR(error);
}

void APIENTRY glSamplerParameteri(GLuint sampler, GLenum pname, GLint param) {
    glSamplerParameterf(sampler, pname, static_cast<GLfloat>(param));
}

void APIENTRY glSamplerParameterfv(GLuint sampler, GLenum pname,
                                   const GLfloat* param) {
    if (!param) return;
    if (pname != GL_TEXTURE_BORDER_COLOR) {
        glSamplerParameterf(sampler, pname, param[0]);
        return;
    }
    auto it = g_samplers.find(sampler);
    if (it == g_samplers.end()) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    (void)SetBorderColor(it->second, param);
}

void APIENTRY glSamplerParameteriv(GLuint sampler, GLenum pname,
                                   const GLint* param) {
    if (!param) return;
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        GLfloat color[4];
        for (int i = 0; i < 4; ++i)
            color[i] = SignedBorderToFloat(param[i]);
        glSamplerParameterfv(sampler, pname, color);
    } else {
        glSamplerParameteri(sampler, pname, param[0]);
    }
}

void APIENTRY glSamplerParameterIiv(GLuint sampler, GLenum pname,
                                    const GLint* param) {
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        // Integer texture formats are not represented by the current RGBA8
        // backend contract. Do not silently reinterpret integer border state.
        if (!g_samplers.count(sampler)) PUSH_ERROR(GL_INVALID_VALUE);
        else PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    glSamplerParameteriv(sampler, pname, param);
}

void APIENTRY glSamplerParameterIuiv(GLuint sampler, GLenum pname,
                                     const GLuint* param) {
    if (!param) return;
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        if (!g_samplers.count(sampler)) PUSH_ERROR(GL_INVALID_VALUE);
        else PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    GLint values[4];
    const int count = pname == GL_TEXTURE_BORDER_COLOR ? 4 : 1;
    for (int i = 0; i < count; ++i)
        values[i] = static_cast<GLint>(param[i]);
    glSamplerParameteriv(sampler, pname, values);
}

void APIENTRY glGetSamplerParameterfv(GLuint sampler, GLenum pname,
                                      GLfloat* params) {
    if (!params) return;
    auto it = g_samplers.find(sampler);
    if (it == g_samplers.end()) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        std::copy(it->second.border_color.begin(),
                  it->second.border_color.end(), params);
        return;
    }
    GLenum error = GL_NO_ERROR;
    *params = GetSamplerScalar(it->second, pname, &error);
    if (error != GL_NO_ERROR) PUSH_ERROR(error);
}

void APIENTRY glGetSamplerParameteriv(GLuint sampler, GLenum pname,
                                      GLint* params) {
    if (!params) return;
    GLfloat values[4]{};
    glGetSamplerParameterfv(sampler, pname, values);
    const int count = pname == GL_TEXTURE_BORDER_COLOR ? 4 : 1;
    for (int i = 0; i < count; ++i) {
        params[i] = pname == GL_TEXTURE_BORDER_COLOR
            ? FloatBorderToSigned(values[i]) : static_cast<GLint>(values[i]);
    }
}

void APIENTRY glGetSamplerParameterIiv(GLuint sampler, GLenum pname,
                                       GLint* params) {
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        if (!g_samplers.count(sampler)) PUSH_ERROR(GL_INVALID_VALUE);
        else PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    glGetSamplerParameteriv(sampler, pname, params);
}

void APIENTRY glGetSamplerParameterIuiv(GLuint sampler, GLenum pname,
                                        GLuint* params) {
    if (!params) return;
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        if (!g_samplers.count(sampler)) PUSH_ERROR(GL_INVALID_VALUE);
        else PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    GLint values[4]{};
    glGetSamplerParameteriv(sampler, pname, values);
    const int count = pname == GL_TEXTURE_BORDER_COLOR ? 4 : 1;
    for (int i = 0; i < count; ++i)
        params[i] = static_cast<GLuint>(values[i]);
}

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
                 target == GL_TEXTURE_1D_ARRAY || target == GL_TEXTURE_2D_ARRAY ||
                 target == GL_TEXTURE_BUFFER;
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
    bool face = IsCubeFace(target);
    if (target != GL_TEXTURE_2D && !face && target != GL_TEXTURE_1D_ARRAY) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }
    GLuint id = ActiveBound();
    if (id == 0) return;
    TexState& st = g_textures[id];

    if (level == 0) {
        if (st.has_image && !face && st.SliceCount() > 1) {
            PUSH_ERROR(GL_INVALID_OPERATION);   // array extent redefinition
            return;
        }
        if (st.has_image && face && st.IsCube() &&
            ((uint32_t)width != st.width || (uint32_t)height != st.height)) {
            PUSH_ERROR(GL_INVALID_OPERATION);   // faces must match
            return;
        }
        st.width = (uint32_t)width;
        st.height = target == GL_TEXTURE_1D_ARRAY ? 1 : (uint32_t)height;
        st.depth = target == GL_TEXTURE_1D_ARRAY ? (uint32_t)height : 1;
        st.has_image = width > 0 && height > 0;
    }
    if (!st.has_image) return;

    uint32_t slice = face ? CubeFaceIndex(target) : 0;
    if (slice >= st.SliceCount()) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    AllocLevel(st, (uint32_t)level);
    uint32_t lvl_w = std::max<uint32_t>(1, st.width >> level);
    uint32_t lvl_h = std::max<uint32_t>(1, st.height >> level);
    if (pixels && (uint32_t)width == lvl_w && (uint32_t)height == lvl_h &&
        face && st.IsCube())
        StoreSlices(st, (uint32_t)level, slice, 1, 0, 0, (uint32_t)width,
                    (uint32_t)height, format, type, pixels);
    else if (pixels && (uint32_t)width == lvl_w && !face &&
             target != GL_TEXTURE_1D_ARRAY)
        StoreSlices(st, (uint32_t)level, 0, 1, 0, 0, (uint32_t)width,
                    (uint32_t)height, format, type, pixels);
    else if (pixels && target == GL_TEXTURE_1D_ARRAY &&
             (uint32_t)width == lvl_w && (uint32_t)st.depth == (uint32_t)height)
        StoreSlices(st, (uint32_t)level, 0, st.depth, 0, 0, (uint32_t)width, 1,
                    format, type, pixels);

    MarkTextureDirty(st, id);
}

void APIENTRY glTexImage1D(GLenum target, GLint level, GLint internalformat,
                           GLsizei width, GLint border, GLenum format,
                           GLenum type, const void* pixels) {
    (void)target;  // 1D images live in the same 2D slot on the engine side
    // Engine-side 1D and 2D share the same image; treat as height-1 2D.
    glTexImage2D(GL_TEXTURE_2D, level, internalformat, width, 1, border,
                 format, type, pixels);
}

void APIENTRY glTexImage3D(GLenum target, GLint level, GLint internalformat,
                           GLsizei width, GLsizei height, GLsizei depth,
                           GLint border, GLenum format, GLenum type,
                           const void* pixels) {
    (void)internalformat;
    if (border != 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }
    if (width < 0 || height < 0 || depth < 0 || level < 0) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    GLuint id = ActiveBound();
    if (id == 0) return;
    TexState& st = g_textures[id];
    // 2D array image height is the "width" of one layer; 3D keeps z as depth.
    if (level == 0) {
        st.width = (uint32_t)width;
        st.height = (uint32_t)height;
        st.depth = (uint32_t)depth;
        st.has_image = width > 0 && height > 0 && depth > 0;
    }
    if (!st.has_image) return;
    AllocLevel(st, (uint32_t)level);
    uint32_t lvl_w = std::max<uint32_t>(1, st.width >> level);
    uint32_t lvl_h = std::max<uint32_t>(1, st.height >> level);
    if (pixels && (uint32_t)width == lvl_w && (uint32_t)height == lvl_h &&
        (uint32_t)depth == st.depth)
        StoreSlices(st, (uint32_t)level, 0, st.depth, 0, 0, (uint32_t)width,
                    (uint32_t)height, format, type, pixels);
    MarkTextureDirty(st, id);
}

void APIENTRY glTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                              GLint yoffset, GLsizei width, GLsizei height,
                              GLenum format, GLenum type, const void* pixels) {
    bool face = IsCubeFace(target);
    if (target != GL_TEXTURE_2D && !face && target != GL_TEXTURE_1D_ARRAY) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }
    if (xoffset < 0 || yoffset < 0 || width < 0 || height < 0 || level < 0) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    GLuint id = ActiveBound();
    if (!id) return;
    TexState& st = g_textures[id];
    if (!st.has_image || st.mip.size() <= (size_t)level) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    uint32_t slice = face ? CubeFaceIndex(target) : 0;
    if (slice >= st.SliceCount()) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    uint32_t lvl_w = std::max<uint32_t>(1, st.width >> level);
    uint32_t lvl_h = std::max<uint32_t>(1, st.height >> level);
    if ((uint32_t)xoffset + (uint32_t)width > lvl_w ||
        (uint32_t)yoffset + (uint32_t)height > lvl_h) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    if (!st.mip[level].empty() && pixels)
        StoreSlices(st, (uint32_t)level, slice, 1, (uint32_t)xoffset,
                    (uint32_t)yoffset, (uint32_t)width, (uint32_t)height,
                    format, type, pixels);
    MarkTextureDirty(st, id);
}

void APIENTRY glTexSubImage3D(GLenum target, GLint level, GLint xoffset,
                              GLint yoffset, GLint zoffset, GLsizei width,
                              GLsizei height, GLsizei depth, GLenum format,
                              GLenum type, const void* pixels) {
    if (target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }
    if (xoffset < 0 || yoffset < 0 || zoffset < 0 || width < 0 || height < 0 ||
        depth < 0 || level < 0) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    GLuint id = ActiveBound();
    if (!id) return;
    TexState& st = g_textures[id];
    if (!st.has_image || st.mip.size() <= (size_t)level) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    uint32_t lvl_w = std::max<uint32_t>(1, st.width >> level);
    uint32_t lvl_h = std::max<uint32_t>(1, st.height >> level);
    if ((uint32_t)xoffset + (uint32_t)width > lvl_w ||
        (uint32_t)yoffset + (uint32_t)height > lvl_h ||
        (uint32_t)zoffset + (uint32_t)depth > st.depth) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    if (pixels)
        StoreSlices(st, (uint32_t)level, (uint32_t)zoffset, (uint32_t)depth,
                    (uint32_t)xoffset, (uint32_t)yoffset, (uint32_t)width,
                    (uint32_t)height, format, type, pixels);
    MarkTextureDirty(st, id);
}

void APIENTRY glTexSubImage1D(GLenum target, GLint level, GLint xoffset,
                              GLsizei width, GLenum format, GLenum type,
                              const void* pixels) {
    (void)target;   // 1D images live in the same 2D slot on the engine side
    glTexSubImage2D(GL_TEXTURE_2D, level, xoffset, 0, width, 1, format, type,
                    pixels);
}

void APIENTRY glGenerateMipmap(GLenum target) {
    if (target != GL_TEXTURE_2D && target != GL_TEXTURE_1D &&
        target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY &&
        target != GL_TEXTURE_1D_ARRAY && !IsCubeFace(target) &&
        target != GL_TEXTURE_CUBE_MAP) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }
    GLuint id = ActiveBound();
    if (id == 0) return;
    TexState& st = g_textures[id];
    if (!st.has_image || st.mip.empty()) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    uint32_t w = st.width, h = st.height;
    size_t slices = st.SliceCount();
    std::vector<uint8_t> cur = st.mip[0];
    st.mip.resize(1);
    while (w > 1 || h > 1) {
        std::vector<uint8_t> next;
        GenerateNextMip(cur, w, h, (uint32_t)slices, next);
        st.mip.push_back(std::move(next));
        cur = st.mip.back();
        w = std::max<uint32_t>(1, w / 2);
        h = std::max<uint32_t>(1, h / 2);
    }
    MarkTextureDirty(st, id);
}

// ---- glGetTexImage / level queries ----------------------------------------

void APIENTRY glGetTexImage(GLenum target, GLint level, GLenum format,
                            GLenum type, void* pixels) {
    if (!TypeBytes(type)) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    bool face = IsCubeFace(target);
    bool ok = target == GL_TEXTURE_2D || target == GL_TEXTURE_1D ||
              target == GL_TEXTURE_3D || target == GL_TEXTURE_2D_ARRAY ||
              target == GL_TEXTURE_1D_ARRAY || target == GL_TEXTURE_BUFFER ||
              face;
    if (!ok) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    if (!pixels) return;
    GLuint id = ActiveBound();
    if (!id) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    TexState& st = g_textures[id];
    if (!st.has_image || level < 0 || st.mip.size() <= (size_t)level) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    if (face && !st.IsCube()) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    // The queried target must match the texture's own target (1D textures
    // live in the engine's 2D slot, so GL_TEXTURE_2D also serves them; the
    // same applies to buffer textures, which mirror as 1xN 2D images).
    bool tgt_ok = face || target == st.target ||
                  (st.target == GL_TEXTURE_1D && target == GL_TEXTURE_2D) ||
                  (st.target == GL_TEXTURE_BUFFER && target == GL_TEXTURE_2D);
    if (!tgt_ok) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    // Only the requested face of a cubemap is returned per call.
    if (st.IsCube() && !face) { PUSH_ERROR(GL_INVALID_OPERATION); return; }

    uint32_t slice0 = face ? CubeFaceIndex(target) : 0;
    uint32_t count = 1;
    if (!face && (st.Is3D() || target == GL_TEXTURE_2D_ARRAY ||
                  target == GL_TEXTURE_1D_ARRAY))
        count = (uint32_t)st.SliceCount();
    if (slice0 + count > st.SliceCount()) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }

    uint32_t lvl_w = std::max<uint32_t>(1, st.width >> level);
    uint32_t lvl_h = std::max<uint32_t>(1, st.height >> level);
    uint32_t row_src = lvl_w * 4;
    uint32_t row_dst = RowBytes(lvl_w, format, type, /*pack=*/true);
    uint32_t plane_dst_rows = PlaneRows(lvl_h, /*pack=*/true);
    for (uint32_t s = 0; s < count; ++s) {
        const uint8_t* src = SlicePtrC(st, (uint32_t)level, slice0 + s);
        for (uint32_t y = 0; y < lvl_h; ++y) {
            EncodeRowRGBA8(src + (size_t)y * row_src,
                           (uint8_t*)pixels +
                               (size_t)(s * plane_dst_rows + y) * row_dst,
                           (GLsizei)lvl_w, format, type);
        }
    }
}

// ---- sampler parameters ---------------------------------------------------

void APIENTRY glTexParameteri(GLenum target, GLenum pname, GLint param) {
    bool faces_ok = IsCubeFace(target);
    if (target != GL_TEXTURE_2D && target != GL_TEXTURE_1D &&
        target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY &&
        target != GL_TEXTURE_1D_ARRAY && target != GL_TEXTURE_CUBE_MAP &&
        target != GL_TEXTURE_BUFFER && !faces_ok) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }
    GLuint id = ActiveBound();
    if (id == 0) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    TexState& st = g_textures[id];
    if (pname == GL_TEXTURE_BASE_LEVEL || pname == GL_TEXTURE_MAX_LEVEL) {
        // Image-level selection remains texture state. The CPU mirror already
        // owns the complete explicit chain; level-window lowering is future.
        return;
    }
    GLenum error = GL_NO_ERROR;
    const bool changed = SetSamplerScalar(st, pname,
                                          static_cast<GLfloat>(param), &error);
    if (error != GL_NO_ERROR) {
        PUSH_ERROR(error);
        return;
    }
    if (changed) MarkTextureDirty(st, id, true);
}

void APIENTRY glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
    bool valid = target == GL_TEXTURE_2D || target == GL_TEXTURE_1D ||
                 target == GL_TEXTURE_3D || target == GL_TEXTURE_2D_ARRAY ||
                 target == GL_TEXTURE_1D_ARRAY || target == GL_TEXTURE_CUBE_MAP ||
                 target == GL_TEXTURE_BUFFER || IsCubeFace(target);
    if (!valid) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    GLuint id = ActiveBound();
    if (!id) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    TexState& state = g_textures[id];
    if (pname == GL_TEXTURE_BASE_LEVEL || pname == GL_TEXTURE_MAX_LEVEL)
        return;
    GLenum error = GL_NO_ERROR;
    const bool changed = SetSamplerScalar(state, pname, param, &error);
    if (error != GL_NO_ERROR) { PUSH_ERROR(error); return; }
    if (changed) MarkTextureDirty(state, id, true);
}

void APIENTRY glTexParameterfv(GLenum target, GLenum pname, const GLfloat* param) {
    if (!param) return;
    if (pname != GL_TEXTURE_BORDER_COLOR) {
        glTexParameterf(target, pname, param[0]);
        return;
    }
    bool valid = target == GL_TEXTURE_2D || target == GL_TEXTURE_1D ||
                 target == GL_TEXTURE_3D || target == GL_TEXTURE_2D_ARRAY ||
                 target == GL_TEXTURE_1D_ARRAY || target == GL_TEXTURE_CUBE_MAP ||
                 target == GL_TEXTURE_BUFFER || IsCubeFace(target);
    if (!valid) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    GLuint id = ActiveBound();
    if (!id) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    TexState& state = g_textures[id];
    if (SetBorderColor(state, param)) MarkTextureDirty(state, id, true);
}

void APIENTRY glTexParameteriv(GLenum target, GLenum pname, const GLint* param) {
    if (!param) return;
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        GLfloat color[4];
        for (int i = 0; i < 4; ++i) color[i] = SignedBorderToFloat(param[i]);
        glTexParameterfv(target, pname, color);
    } else {
        glTexParameteri(target, pname, param[0]);
    }
}

void APIENTRY glTexParameterIiv(GLenum target, GLenum pname, const GLint* param) {
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    glTexParameteriv(target, pname, param);
}

void APIENTRY glTexParameterIuiv(GLenum target, GLenum pname, const GLuint* param) {
    if (!param) return;
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    GLint values[4];
    const int count = pname == GL_TEXTURE_BORDER_COLOR ? 4 : 1;
    for (int i = 0; i < count; ++i) values[i] = static_cast<GLint>(param[i]);
    glTexParameteriv(target, pname, values);
}

// ---- queries --------------------------------------------------------------

void APIENTRY glGetTexParameterfv(GLenum target, GLenum pname, GLfloat* params) {
    if (!params) return;
    bool valid = target == GL_TEXTURE_2D || target == GL_TEXTURE_1D ||
                 target == GL_TEXTURE_3D || target == GL_TEXTURE_2D_ARRAY ||
                 target == GL_TEXTURE_1D_ARRAY || target == GL_TEXTURE_CUBE_MAP ||
                 target == GL_TEXTURE_BUFFER || IsCubeFace(target);
    if (!valid) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    GLuint id = ActiveBound();
    if (!id) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    const TexState& state = g_textures[id];
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        std::copy(state.border_color.begin(), state.border_color.end(), params);
        return;
    }
    if (pname == GL_TEXTURE_BASE_LEVEL) { *params = 0.0f; return; }
    if (pname == GL_TEXTURE_MAX_LEVEL) { *params = 1000.0f; return; }
    GLenum error = GL_NO_ERROR;
    *params = GetSamplerScalar(state, pname, &error);
    if (error != GL_NO_ERROR) PUSH_ERROR(error);
}

void APIENTRY glGetTexParameteriv(GLenum target, GLenum pname, GLint* params) {
    if (!params) return;
    GLfloat values[4]{};
    glGetTexParameterfv(target, pname, values);
    const int count = pname == GL_TEXTURE_BORDER_COLOR ? 4 : 1;
    for (int i = 0; i < count; ++i) {
        params[i] = pname == GL_TEXTURE_BORDER_COLOR
            ? FloatBorderToSigned(values[i]) : static_cast<GLint>(values[i]);
    }
}

void APIENTRY glGetTexParameterIiv(GLenum target, GLenum pname, GLint* params) {
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    glGetTexParameteriv(target, pname, params);
}

void APIENTRY glGetTexParameterIuiv(GLenum target, GLenum pname, GLuint* params) {
    if (!params) return;
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    GLint values[4]{};
    glGetTexParameteriv(target, pname, values);
    const int count = pname == GL_TEXTURE_BORDER_COLOR ? 4 : 1;
    for (int i = 0; i < count; ++i) params[i] = static_cast<GLuint>(values[i]);
}

void APIENTRY glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname,
                                       GLint* params) {
    bool ok = target == GL_TEXTURE_2D || target == GL_TEXTURE_3D ||
              target == GL_TEXTURE_2D_ARRAY || target == GL_TEXTURE_CUBE_MAP ||
              IsCubeFace(target) || target == GL_TEXTURE_1D_ARRAY ||
              target == GL_TEXTURE_1D;
    if (!ok || level < 0) { PUSH_ERROR(GL_INVALID_ENUM); return; }
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
        case GL_TEXTURE_DEPTH:
            if (st.Is3D())
                *params = (GLint)std::max<uint32_t>(1, st.depth >> level);
            else if (st.IsCube())
                *params = (GLint)st.SliceCount();
            else
                *params = (GLint)st.depth;
            break;
        case GL_TEXTURE_INTERNAL_FORMAT:
            *params = st.has_comp ? (GLint)st.comp_format : GL_RGBA8;
            break;
        case GL_TEXTURE_COMPRESSED:
            *params = st.has_comp ? GL_TRUE : GL_FALSE;
            break;
        case GL_TEXTURE_COMPRESSED_IMAGE_SIZE:
            *params =
                st.has_comp && st.comp.size() > (size_t)level
                    ? (GLint)st.comp[level].size()
                    : 0;
            break;
        case GL_TEXTURE_RED_TYPE:
        case GL_TEXTURE_GREEN_TYPE:
        case GL_TEXTURE_BLUE_TYPE:
        case GL_TEXTURE_ALPHA_TYPE:
            *params = GL_UNSIGNED_NORMALIZED;
            break;
        default: PUSH_ERROR(GL_INVALID_ENUM); return;
    }
}

void APIENTRY glGetTexLevelParameterfv(GLenum target, GLint level, GLenum pname,
                                       GLfloat* params) {
    GLint v = 0;
    glGetTexLevelParameteriv(target, level, pname, &v);
    if (params) *params = (GLfloat)v;
}

// ---- compressed textures ---------------------------------------------------

void APIENTRY glCompressedTexImage2D(GLenum target, GLint level,
                                     GLenum internalformat, GLsizei width,
                                     GLsizei height, GLint border,
                                     GLsizei imageSize, const void* data) {
    if (border != 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (!IsS3TC(internalformat)) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }
    bool face = IsCubeFace(target);
    if (target != GL_TEXTURE_2D && !face) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    if (width < 0 || height < 0 || level < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    GLuint id = ActiveBound();
    if (!id) return;
    TexState& st = g_textures[id];

    if (level == 0) {
        if (st.has_image && !face && st.SliceCount() > 1) {
            PUSH_ERROR(GL_INVALID_OPERATION);
            return;
        }
        st.width = (uint32_t)width;
        st.height = (uint32_t)height;
        st.depth = 1;
        st.has_image = width > 0 && height > 0;
    }
    if (!st.has_image) return;
    uint32_t slice = face ? CubeFaceIndex(target) : 0;
    AllocLevel(st, (uint32_t)level);
    if (data != nullptr) {
        // Keep the raw compressed bytes and decompress into the mirror.
        if (st.comp.size() <= (size_t)level) st.comp.resize(level + 1);
        st.comp[level].assign((const uint8_t*)data,
                              (const uint8_t*)data + (size_t)imageSize);
        st.has_comp = true;
        st.comp_format = internalformat;
        uint32_t lvl_w = std::max<uint32_t>(1, st.width >> level);
        uint32_t lvl_h = std::max<uint32_t>(1, st.height >> level);
        DecodeCompressedPlane(SlicePtr(st, level, slice), (const uint8_t*)data,
                              lvl_w, lvl_h, internalformat);
    }
    MarkTextureDirty(st, id);
}

void APIENTRY glCompressedTexImage1D(GLenum target, GLint level,
                                     GLenum internalformat, GLsizei width,
                                     GLint border, GLsizei imageSize,
                                     const void* data) {
    (void)target;
    glCompressedTexImage2D(GL_TEXTURE_2D, level, internalformat, width, 1,
                           border, imageSize, data);
}

void APIENTRY glCompressedTexImage3D(GLenum target, GLint level,
                                     GLenum internalformat, GLsizei width,
                                     GLsizei height, GLsizei depth,
                                     GLint border, GLsizei imageSize,
                                     const void* data) {
    if (border != 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (!IsS3TC(internalformat)) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    if (target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }
    if (width < 0 || height < 0 || depth < 0 || level < 0) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    GLuint id = ActiveBound();
    if (!id) return;
    TexState& st = g_textures[id];
    if (level == 0) {
        st.width = (uint32_t)width;
        st.height = (uint32_t)height;
        st.depth = (uint32_t)depth;
        st.has_image = width > 0 && height > 0 && depth > 0;
    }
    if (!st.has_image) return;
    AllocLevel(st, (uint32_t)level);
    if (data && st.comp.size() <= (size_t)level) {
        st.comp.resize(level + 1);
        st.comp[level].assign((const uint8_t*)data,
                              (const uint8_t*)data + (size_t)imageSize);
        st.has_comp = true;
        st.comp_format = internalformat;
    }
    MarkTextureDirty(st, id);
}

void APIENTRY glCompressedTexSubImage2D(GLenum target, GLint level,
                                        GLint xoffset, GLint yoffset,
                                        GLsizei width, GLsizei height,
                                        GLenum format, GLsizei imageSize,
                                        const void* data) {
    bool face = IsCubeFace(target);
    if (target != GL_TEXTURE_2D && !face) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    if (xoffset < 0 || yoffset < 0 || width < 0 || height < 0 || level < 0) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    GLuint id = ActiveBound();
    if (!id) return;
    TexState& st = g_textures[id];
    if (!st.has_image || st.mip.size() <= (size_t)level) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    uint32_t slice = face ? CubeFaceIndex(target) : 0;
    uint32_t lvl_w = std::max<uint32_t>(1, st.width >> level);
    uint32_t lvl_h = std::max<uint32_t>(1, st.height >> level);
    if ((uint32_t)xoffset + (uint32_t)width > lvl_w ||
        (uint32_t)yoffset + (uint32_t)height > lvl_h) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    if (data) {
        // Only fully-aligned sub-regions are decoded; anything else is
        // re-encoded on the fly is not worth it for MC.
        DecodeCompressedPlane(SlicePtr(st, level, slice) +
                              ((size_t)yoffset * lvl_w + xoffset) * 4,
                              (const uint8_t*)data,
                              (uint32_t)width, (uint32_t)height, format);
        if (st.comp.size() <= (size_t)level) {
            st.comp.resize(level + 1);
            st.comp[level].assign(
                (const uint8_t*)data, (const uint8_t*)data + (size_t)imageSize);
            st.has_comp = true;
            st.comp_format = format;
        }
    }
    MarkTextureDirty(st, id);
}

void APIENTRY glCompressedTexSubImage3D(GLenum target, GLint level,
                                        GLint xoffset, GLint yoffset,
                                        GLint zoffset, GLsizei width,
                                        GLsizei height, GLsizei depth,
                                        GLenum format, GLsizei imageSize,
                                        const void* data) {
    if (target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }
    if (xoffset < 0 || yoffset < 0 || zoffset < 0 || width < 0 || height < 0 ||
        depth < 0 || level < 0) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    GLuint id = ActiveBound();
    if (!id) return;
    TexState& st = g_textures[id];
    if (!st.has_image || st.mip.size() <= (size_t)level) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    if (data) {
        if (st.comp.size() <= (size_t)level) {
            st.comp.resize(level + 1);
            st.comp[level].assign((const uint8_t*)data,
                                  (const uint8_t*)data + (size_t)imageSize);
            st.has_comp = true;
            st.comp_format = format;
        }
        // Decode each plane of the requested z range.
        uint32_t lvl_w = std::max<uint32_t>(1, st.width >> level);
        for (GLsizei z = 0; z < depth; ++z) {
            GLsizei dsz = CompressedPlaneSize((uint32_t)width,
                                              (uint32_t)height, format);
            DecodeCompressedPlane(
                SlicePtr(st, level, (uint32_t)(zoffset + z)) +
                    ((size_t)yoffset * lvl_w + xoffset) * 4,
                (const uint8_t*)data + (size_t)z * dsz, (uint32_t)width,
                (uint32_t)height, format);
        }
    }
    MarkTextureDirty(st, id);
}

void APIENTRY glGetCompressedTexImage(GLenum target, GLint level, void* pixels) {
    (void)target;   // whole-level copy; cube faces share the level buffer
    if (!pixels) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    GLuint id = ActiveBound();
    if (!id) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    TexState& st = g_textures[id];
    if (!st.has_comp || st.comp.size() <= (size_t)level) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    std::memcpy(pixels, st.comp[level].data(), st.comp[level].size());
}

// ---- framebuffer copies ------------------------------------------------------

void CopyFramebufferToTexture(TexState& st, GLint level,
                              GLint xoffset, GLint yoffset,
                              GLint srcx, GLint srcy, GLsizei width,
                              GLsizei height) {
    if (width <= 0 || height <= 0) return;
    std::vector<uint8_t> tmp((size_t)width * height * 4);
    v::ReadPixels(srcx, srcy, width, height, tmp.data());
    uint32_t lvl_w = std::max<uint32_t>(1, st.width >> level);
    uint32_t lvl_h = std::max<uint32_t>(1, st.height >> level);
    if ((uint32_t)xoffset + (uint32_t)width > lvl_w ||
        (uint32_t)yoffset + (uint32_t)height > lvl_h) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    for (GLsizei y = 0; y < height; ++y)
        std::memcpy(SlicePtr(st, (uint32_t)level, 0) +
                        ((size_t)(yoffset + y) * lvl_w + xoffset) * 4,
                    tmp.data() + (size_t)y * width * 4, (size_t)width * 4);
}

void APIENTRY glCopyTexImage1D(GLenum target, GLint level,
                               GLenum internalformat, GLint x, GLint y,
                               GLsizei width, GLint border) {
    (void)target; (void)internalformat;
    if (border != 0 || width < 0 || level < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    GLuint id = ActiveBound();
    if (!id) return;
    TexState& st = g_textures[id];
    st.width = (uint32_t)width;
    st.height = 1;
    st.depth = 1;
    st.has_image = width > 0;
    if (!st.has_image) return;
    AllocLevel(st, (uint32_t)level);
    CopyFramebufferToTexture(st, level, 0, 0, x, y, width, 1);
    MarkTextureDirty(st, id);
}

void APIENTRY glCopyTexImage2D(GLenum target, GLint level,
                               GLenum internalformat, GLint x, GLint y,
                               GLsizei width, GLsizei height, GLint border) {
    (void)internalformat;
    if (border != 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    bool face = IsCubeFace(target);
    if (target != GL_TEXTURE_2D && !face) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    if (width < 0 || height < 0 || level < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    GLuint id = ActiveBound();
    if (!id) return;
    TexState& st = g_textures[id];
    uint32_t slice = face ? CubeFaceIndex(target) : 0;
    if (slice >= st.SliceCount()) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    if (level == 0) {
        if (face) {
            st.width = (uint32_t)width;
            st.height = (uint32_t)height;
            st.depth = 1;
        } else {
            if (st.has_image && st.SliceCount() > 1) {
                PUSH_ERROR(GL_INVALID_OPERATION);
                return;
            }
            st.width = (uint32_t)width;
            st.height = (uint32_t)height;
            st.depth = 1;
        }
        st.has_image = width > 0 && height > 0;
    }
    if (!st.has_image) return;
    AllocLevel(st, (uint32_t)level);
    if (face) {
        std::vector<uint8_t> tmp((size_t)width * height * 4);
        v::ReadPixels(x, y, width, height, tmp.data());
        for (GLsizei r = 0; r < height; ++r)
            std::memcpy(SlicePtr(st, (uint32_t)level, slice) + (size_t)r * width * 4,
                        tmp.data() + (size_t)r * width * 4, (size_t)width * 4);
    } else {
        CopyFramebufferToTexture(st, (GLint)level, 0, 0, x, y, width, height);
    }
    MarkTextureDirty(st, id);
}

void APIENTRY glCopyTexSubImage1D(GLenum target, GLint level, GLint xoffset,
                                  GLint x, GLint y, GLsizei width) {
    if (target != GL_TEXTURE_1D) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    GLuint id = ActiveBound();
    if (!id) return;
    TexState& st = g_textures[id];
    if (!st.has_image || st.mip.size() <= (size_t)level) {
        PUSH_ERROR(GL_INVALID_OPERATION);
    }
    CopyFramebufferToTexture(st, level, xoffset, 0, x, y, width, 1);
}

void APIENTRY glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                                  GLint yoffset, GLint x, GLint y,
                                  GLsizei width, GLsizei height) {
    if (target != GL_TEXTURE_2D && target != GL_TEXTURE_1D_ARRAY &&
        !IsCubeFace(target)) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }
    GLuint id = ActiveBound();
    if (!id) return;
    TexState& st = g_textures[id];
    if (!st.has_image || st.mip.size() <= (size_t)level) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    if (IsCubeFace(target)) {
        uint32_t slice = CubeFaceIndex(target);
        if (slice >= st.SliceCount()) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
        std::vector<uint8_t> tmp((size_t)width * height * 4);
        v::ReadPixels(x, y, width, height, tmp.data());
        uint32_t lvl_w = std::max<uint32_t>(1, st.width >> level);
        uint32_t lvl_h = std::max<uint32_t>(1, st.height >> level);
        if ((uint32_t)xoffset + (uint32_t)width > lvl_w ||
            (uint32_t)yoffset + (uint32_t)height > lvl_h) {
            PUSH_ERROR(GL_INVALID_VALUE);
            return;
        }
        for (GLsizei r = 0; r < height; ++r)
            std::memcpy(SlicePtr(st, (uint32_t)level, slice) +
                            ((size_t)(yoffset + r) * lvl_w + xoffset) * 4,
                        tmp.data() + (size_t)r * width * 4, (size_t)width * 4);
    } else {
        CopyFramebufferToTexture(st, level, xoffset, yoffset, x, y, width, height);
    }
    MarkTextureDirty(st, id);
}

void APIENTRY glCopyTexSubImage3D(GLenum target, GLint level, GLint xoffset,
                                  GLint yoffset, GLint zoffset, GLint x,
                                  GLint y, GLsizei width, GLsizei height) {
    if (target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY) {
        PUSH_ERROR(GL_INVALID_ENUM); return;
    }
    if (zoffset < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    GLuint id = ActiveBound();
    if (!id) return;
    TexState& st = g_textures[id];
    if (!st.has_image || st.mip.size() <= (size_t)level) {
        PUSH_ERROR(GL_INVALID_OPERATION); return;
    }
    uint32_t lvl_w = std::max<uint32_t>(1, st.width >> level);
    uint32_t lvl_h = std::max<uint32_t>(1, st.height >> level);
    if ((uint32_t)xoffset + (uint32_t)width > lvl_w ||
        (uint32_t)yoffset + (uint32_t)height > lvl_h ||
        (uint32_t)zoffset + 1 > st.depth) {
        PUSH_ERROR(GL_INVALID_VALUE); return;
    }
    std::vector<uint8_t> tmp((size_t)width * height * 4);
    v::ReadPixels(x, y, width, height, tmp.data());
    for (GLsizei r = 0; r < height; ++r)
        std::memcpy(SlicePtr(st, (uint32_t)level, (uint32_t)zoffset) +
                        ((size_t)(yoffset + r) * lvl_w + xoffset) * 4,
                    tmp.data() + (size_t)r * width * 4, (size_t)width * 4);
    MarkTextureDirty(st, id);
}

// ---- buffer textures (glTexBuffer) ------------------------------------------

void APIENTRY glTexBuffer(GLenum target, GLenum internalformat, GLuint buffer) {
    if (target != GL_TEXTURE_BUFFER) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    GLuint id = ActiveBound();
    if (!id) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    TexState& st = g_textures[id];
    auto it = g_buffers.find(buffer);
    if (buffer != 0 && it == g_buffers.end()) {
        PUSH_ERROR(GL_INVALID_VALUE);   // not the name of a buffer object
        return;
    }
    if (buffer == 0) {
        st.has_tex_buffer = false;
        st.tex_buffer = 0;
        st.has_image = false;
        st.mip.clear();
        st.depth = 1;
        if (v::IsInitialized()) {
            v::DestroyResidentTexture(id);
            g_dirty_textures.erase(id);
        }
        return;
    }
    // The buffer is mirrored as a 1xN RGBA8 image (texels assumed 4 bytes).
    const std::vector<uint8_t>& data = it->second.data;
    st.has_tex_buffer = true;
    st.tex_buffer = buffer;
    st.tex_buffer_format = internalformat;
    st.width = (uint32_t)(data.size() / 4);
    st.height = 1;
    st.depth = 1;
    st.has_image = !data.empty();
    st.mip.clear();
    if (st.has_image) {
        st.mip.push_back(data);   // RGBA8 texel stream == the mirror
        MarkTextureDirty(st, id);
    }
}

// ---- pixel store (float variant) -------------------------------------------

void APIENTRY glPixelStoref(GLenum pname, GLfloat param) {
    glPixelStorei(pname, (GLint)param);
}

} // extern "C"

// ---- shared table storage (declared in internal.h) ------------------------

std::unordered_map<GLuint, TexState> g_textures;
std::array<GLuint, kMaxTexUnits> g_texture_units{};
GLuint g_next_texture = 1;
std::unordered_map<GLuint, SamplerData> g_samplers;
std::array<GLuint, kMaxTexUnits> g_sampler_units{};
GLuint g_next_sampler = 1;
std::unordered_set<GLuint> g_dirty_textures;
