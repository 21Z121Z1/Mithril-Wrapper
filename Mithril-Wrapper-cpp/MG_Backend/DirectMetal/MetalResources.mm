// Mithril-Wrapper - MG_Backend/DirectMetal/MetalResources.mm
// Buffers / textures / samplers + the texture image operations. dmt_* wrappers
// live in MetalEntry.mm.
//
// LIFETIME NOTE: MTLObjects referenced by an encoded-but-uncommitted command
// buffer are retained by Metal until the buffer completes, so deleting a
// GL object (dropping our wrapper + the ARC ref) is safe even while the GPU
// is still reading it — no deferred-destruction queue is needed. Swapchain
// teardown is the one exception (drain_and_detach commits + waits first).
#ifdef __APPLE__

#include "MetalResources.h"
#include "MetalCommandStream.h"
#include "MetalFormat.h"
#include "../DirectVulkan/FormatMap.h"
#include "../../MG_State/State.h"
#include "../../MG_Impl/Log.h"

#include <unordered_map>

namespace mithril {
namespace dmt {

// ---- Tables ---------------------------------------------------------------

static std::unordered_map<GLuint, MetalBuffer*>& buffer_tbl() {
    static std::unordered_map<GLuint, MetalBuffer*> t;
    return t;
}
static std::unordered_map<GLuint, MetalTexture*>& texture_tbl() {
    static std::unordered_map<GLuint, MetalTexture*> t;
    return t;
}
static std::unordered_map<GLuint, MetalSampler*>& sampler_tbl() {
    static std::unordered_map<GLuint, MetalSampler*> t;
    return t;
}
std::unordered_map<GLuint, MetalBuffer*>& buffer_table() { return buffer_tbl(); }

MetalBuffer* buffer_table_get(GLuint name) {
    auto it = buffer_tbl().find(name);
    return it == buffer_tbl().end() ? nullptr : it->second;
}
void buffer_table_erase(GLuint name) {
    auto it = buffer_tbl().find(name);
    if (it == buffer_tbl().end()) return;
    MetalBuffer* b = it->second;
    buffer_tbl().erase(it);
    delete b; // releases the id<MTLBuffer> under ARC
}

MetalTexture* texture_table_get(GLuint name) {
    auto it = texture_tbl().find(name);
    return it == texture_tbl().end() ? nullptr : it->second;
}
void texture_table_erase(GLuint name) {
    auto it = texture_tbl().find(name);
    if (it == texture_tbl().end()) return;
    MetalTexture* t = it->second;
    texture_tbl().erase(it);
    sampler_tbl().erase(name); // per-texture cached sampler dies with it
    delete t;
}

MetalSampler* sampler_table_get(GLuint name) {
    auto it = sampler_tbl().find(name);
    return it == sampler_tbl().end() ? nullptr : it->second;
}
void sampler_table_erase(GLuint name) {
    auto it = sampler_tbl().find(name);
    if (it == sampler_tbl().end()) return;
    MetalSampler* s = it->second;
    sampler_tbl().erase(it);
    delete s; // releases the id<MTLSamplerState> under ARC
}

// ---- Buffers ---------------------------------------------------------------

static MetalBuffer* create_metal_buffer(GLuint name, const void* data,
                                        size_t size, bool persistent) {
    Backend* b = backend();
    if (!b->initialized || size == 0) return nullptr;
    NSUInteger cap = ((NSUInteger)size + 255u) & ~255u;

    MetalBuffer* mb = buffer_table_get(name);
    if (mb && mb->buf != nil && mb->capacity >= cap) {
        // Reuse the existing allocation (orphan-in-place semantics): the
        // frontend uploads fresh contents right after a glBufferData call.
        if (data && mb->contents) {
            std::memcpy(mb->contents, data, size);
            MITHRIL_DMT_SYNC(mb, 0, size);
        }
        return mb;
    }
    if (!mb) {
        mb = new MetalBuffer();
        buffer_tbl()[name] = mb;
    }
    mb->managed = !b->unifiedMemory;
    mb->persistent = persistent;
    mb->capacity = cap;
    mb->buf = [b->device newBufferWithLength:cap
                                      options:MITHRIL_DMT_STORAGE(mb->managed)];
    if (mb->buf == nil) { mb->capacity = 0; return nullptr; }
    mb->contents = mb->buf.contents;
    if (data && mb->contents) {
        std::memcpy(mb->contents, data, size);
        MITHRIL_DMT_SYNC(mb, 0, size);
    }
    return mb;
}

MetalBuffer* dmt_internal_get_or_create_buffer(GLuint name, const void* data,
                                               size_t size) {
    return create_metal_buffer(name, data, size, false);
}

// ---- GL_TEXTURE_BUFFER（samplerBuffer）-------------------------------------
// Metal 的 buffer texture：从 MTLBuffer 派生 texture_buffer 视图（原生支持，
// 无需拷贝）。缓存键 = 源 MTLBuffer + GL Buffer::contentVersion +
// (offset,size,format)，源变化即换新视图（ARC 释放旧 MTLTexture）。
namespace {
MTLPixelFormat buffer_texel_format(GLenum internalformat) {
    switch (internalformat) {
        case GL_R8:    return MTLPixelFormatR8Unorm;
        case GL_R16:   return MTLPixelFormatR16Unorm;
        case GL_R16F:  return MTLPixelFormatR16Float;
        case GL_R32F:  return MTLPixelFormatR32Float;
        case GL_R8I:   return MTLPixelFormatR8Sint;
        case GL_R16I:  return MTLPixelFormatR16Sint;
        case GL_R32I:  return MTLPixelFormatR32Sint;
        case GL_R8UI:  return MTLPixelFormatR8Uint;
        case GL_R16UI: return MTLPixelFormatR16Uint;
        case GL_R32UI: return MTLPixelFormatR32Uint;
        default:       return MTLPixelFormatInvalid;
    }
}
NSUInteger texel_bytes(MTLPixelFormat f) {
    switch (f) {
        case MTLPixelFormatR8Unorm: case MTLPixelFormatR8Sint:
        case MTLPixelFormatR8Uint:
            return 1;
        case MTLPixelFormatR16Unorm: case MTLPixelFormatR16Float:
        case MTLPixelFormatR16Sint: case MTLPixelFormatR16Uint:
            return 2;
        default:
            return 4;   // R32 家族
    }
}
struct BufferTexEntry {
    id<MTLTexture> tex = nil;
    id<MTLBuffer>  srcBuffer = nil;
    uint64_t       srcContentVersion = 0;
    GLintptr       offset = 0;
    GLsizeiptr     size = 0;
    GLenum         internalFormat = 0;
};
std::unordered_map<GLuint, BufferTexEntry>& buffer_tex_table() {
    static std::unordered_map<GLuint, BufferTexEntry> t;
    return t;
}
} // namespace

id<MTLTexture> get_or_create_buffer_texture(GLuint texName) {
    Backend* b = backend();
    if (!b->initialized || texName == 0) return nil;

    mithril::Texture* tex = mithril::state_get_texture(texName);
    if (!tex || tex->texBuffer == 0) return nil;
    mithril::Buffer* glbuf = mithril::state_get_buffer(tex->texBuffer);
    if (!glbuf || glbuf->data.empty()) {
        buffer_tex_table().erase(texName);
        return nil;
    }

    // 确保后端 MTLBuffer 存在且内容最新（首次绑定 / 内容更新时同步）。
    MetalBuffer* mb = dmt_internal_get_or_create_buffer(
        tex->texBuffer, glbuf->data.data(), (size_t)glbuf->data.size());
    if (!mb || mb->buf == nil) return nil;

    auto& tbl = buffer_tex_table();
    auto it = tbl.find(texName);
    if (it != tbl.end()) {
        if (it->second.tex != nil &&
            it->second.srcBuffer == mb->buf &&
            it->second.srcContentVersion == glbuf->contentVersion &&
            it->second.offset == tex->texBufferOffset &&
            it->second.size == tex->texBufferSize &&
            it->second.internalFormat == tex->internalFormat) {
            return it->second.tex;   // 缓存命中
        }
        tbl.erase(it);   // ARC 释放旧 MTLTexture（buffer 仍归 buffer_table 管）
    }

    const MTLPixelFormat pf = buffer_texel_format((GLenum)tex->internalFormat);
    if (pf == MTLPixelFormatInvalid) {
        MITHRIL_LOG_WARN("mtl", "tex buffer 0x%x: unsupported internalformat "
                         "0x%x", (unsigned)texName,
                         (unsigned)tex->internalFormat);
        return nil;
    }
    const NSUInteger bytes =
        (tex->texBufferSize > 0) ? (NSUInteger)tex->texBufferSize
                                 : (NSUInteger)glbuf->data.size();
    const NSUInteger width = bytes / texel_bytes(pf);
    if (width == 0) return nil;

    MTLTextureDescriptor* d = [[MTLTextureDescriptor alloc] init];
    d.textureType = MTLTextureTypeTextureBuffer;
    d.pixelFormat = pf;
    d.width = width;
    d.height = 1;
    d.depth = 1;
    d.mipmapLevelCount = 1;
    d.sampleCount = 1;
    d.storageMode = mb->buf.storageMode;   // 与源 buffer 一致（Shared/Managed）
    d.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> view =
        [mb->buf newTextureWithDescriptor:d
                                   offset:(NSUInteger)tex->texBufferOffset
                              bytesPerRow:0];
    if (view == nil) {
        MITHRIL_LOG_WARN("mtl", "newTextureWithDescriptor (texture_buffer) "
                         "failed for tex %u -> buf %u",
                         (unsigned)texName, (unsigned)tex->texBuffer);
        return nil;
    }

    BufferTexEntry e;
    e.tex = view;
    e.srcBuffer = mb->buf;
    e.srcContentVersion = glbuf->contentVersion;
    e.offset = tex->texBufferOffset;
    e.size = tex->texBufferSize;
    e.internalFormat = tex->internalFormat;
    tbl[texName] = e;
    return view;
}

MetalBuffer* dmt_internal_create_buffer_storage(GLuint name, NSUInteger size,
                                                bool persistent) {
    return create_metal_buffer(name, nullptr, size, persistent);
}

void dmt_internal_buffer_upload(GLuint name, GLintptr offset, const void* data,
                                size_t size) {
    MetalBuffer* mb = buffer_table_get(name);
    if (!mb || mb->buf == nil || !mb->contents) return;
    if (offset < 0 || (NSUInteger)offset + size > mb->capacity) {
        // Grow to fit (mirrors the Vulkan path's auto-grow safety net).
        NSUInteger need = ((NSUInteger)offset + size + 255u) & ~255u;
        MetalBuffer* grown = create_metal_buffer(name, nullptr, need, mb->persistent);
        if (!grown) return;
        mb = grown;
    }
    std::memcpy((uint8_t*)mb->contents + offset, data, size);
    MITHRIL_DMT_SYNC(mb, offset, size);
}

// ---- Samplers ---------------------------------------------------------------

MetalSampler* dmt_internal_get_or_create_sampler(GLuint name, GLint min_filter,
                                                 GLint mag_filter, GLint wrap_s,
                                                 GLint wrap_t, GLint wrap_r,
                                                 const float* border_color) {
    uint64_t sig = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { sig ^= v; sig *= 1099511628211ull; };
    mix((uint32_t)min_filter); mix((uint32_t)mag_filter);
    mix((uint32_t)wrap_s); mix((uint32_t)wrap_t); mix((uint32_t)wrap_r);
    if (border_color) {
        for (int i=0;i<4;++i) { uint32_t bits=0; std::memcpy(&bits,&border_color[i],sizeof(bits)); mix(bits); }
    }
    if (MetalSampler* existing = sampler_table_get(name)) {
        if (existing->smp != nil && existing->stateSignature == sig) return existing;
    }
    Backend* b = backend();
    if (!b->initialized) return nullptr;

    MTLSamplerDescriptor* d = [[MTLSamplerDescriptor alloc] init];
    bool minLinear = (min_filter == GL_LINEAR || min_filter == GL_LINEAR_MIPMAP_NEAREST ||
                      min_filter == GL_LINEAR_MIPMAP_LINEAR);
    bool mipLinear = (min_filter == GL_NEAREST_MIPMAP_LINEAR ||
                      min_filter == GL_LINEAR_MIPMAP_LINEAR);
    bool hasMip = (min_filter == GL_NEAREST_MIPMAP_NEAREST ||
                   min_filter == GL_NEAREST_MIPMAP_LINEAR ||
                   min_filter == GL_LINEAR_MIPMAP_NEAREST ||
                   min_filter == GL_LINEAR_MIPMAP_LINEAR);
    d.minFilter = minLinear ? MTLSamplerMinMagFilterLinear
                            : MTLSamplerMinMagFilterNearest;
    d.magFilter = (mag_filter == GL_LINEAR) ? MTLSamplerMinMagFilterLinear
                                            : MTLSamplerMinMagFilterNearest;
    d.mipFilter = hasMip ? (mipLinear ? MTLSamplerMipFilterLinear
                                       : MTLSamplerMipFilterNearest)
                         : MTLSamplerMipFilterNotMipmapped;
    d.sAddressMode = wrap_mode_from_gl(wrap_s);
    d.tAddressMode = wrap_mode_from_gl(wrap_t);
    d.rAddressMode = wrap_mode_from_gl(wrap_r);

    MetalSampler* ms = sampler_table_get(name);
    if (!ms) {
        ms = new MetalSampler();
        sampler_tbl()[name] = ms;
    }
    ms->smp = [b->device newSamplerStateWithDescriptor:d];
    if (ms->smp != nil) ms->stateSignature = sig;
    return ms->smp != nil ? ms : nullptr;
}

// ---- Default texture --------------------------------------------------------

static MetalTexture* g_default_tex = nullptr;
static MetalSampler* g_default_sampler = nullptr;

static void ensure_default_texture() {
    static bool attempted = false;
    if ((g_default_tex && g_default_sampler) || attempted) return;
    attempted = true;
    Backend* b = backend();
    if (!b->initialized) return;
    MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                  width:1 height:1 mipmapped:NO];
    d.usage = MTLTextureUsageShaderRead;
    d.storageMode = MITHRIL_DMT_STORAGE_MODE(!b->unifiedMemory);
    id<MTLTexture> tex = [b->device newTextureWithDescriptor:d];
    if (!tex) return;
    uint32_t black = 0;
    [tex replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0
           withBytes:&black bytesPerRow:4];
    g_default_tex = new MetalTexture();
    g_default_tex->tex = tex;
    g_default_tex->vkFormat = VK_FORMAT_R8G8B8A8_UNORM;
    g_default_tex->width = g_default_tex->height = g_default_tex->depth = 1;

    MTLSamplerDescriptor* sd = [[MTLSamplerDescriptor alloc] init];
    sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterNearest;
    sd.mipFilter = MTLSamplerMipFilterNotMipmapped;
    sd.sAddressMode = sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
    g_default_sampler = new MetalSampler();
    g_default_sampler->smp = [b->device newSamplerStateWithDescriptor:sd];
}

MetalTexture* default_texture_tex() { ensure_default_texture(); return g_default_tex; }
MetalSampler* default_texture_sampler() { ensure_default_texture(); return g_default_sampler; }

// ---- Textures ----------------------------------------------------------------

// OpenGL names the six cubemap faces as distinct TexImage targets, while
// Metal allocates one cube texture and addresses faces as slices.  Normalize
// the face enums at the allocation boundary so subsequent face uploads do not
// accidentally create ordinary 2D textures and reinterpret the face index as
// a texture-depth Z coordinate.
static GLenum normalize_metal_texture_target(GLenum target) {
    if (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
        target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z) {
        return GL_TEXTURE_CUBE_MAP;
    }
    return target;
}

MetalTexture* get_or_create_texture(GLuint name, int width, int height,
                                    int depth, int levels, GLenum internal_format,
                                    GLenum target, int samples) {
    Backend* b = backend();
    if (!b->initialized || name == 0 || width <= 0) return nullptr;
    const GLenum storageTarget = normalize_metal_texture_target(target);
    if (storageTarget == GL_TEXTURE_2D || storageTarget == GL_TEXTURE_2D_ARRAY ||
        storageTarget == GL_TEXTURE_RECTANGLE || storageTarget == GL_TEXTURE_1D) {
        if (height <= 0) height = 1;
    }
    if (depth <= 0) depth = 1;
    if (levels <= 0) levels = 1;

    VkFormat vkf = mithril::vk::gl_internal_to_vk(internal_format);
    MTLPixelFormat pf = pixel_format_from_vk(vkf);
    if (pf == MTLPixelFormatInvalid) {
        MITHRIL_LOG_WARN("mtl", "texture %u: GL internal 0x%x has no Metal format",
                         name, internal_format);
        return nullptr;
    }

    MetalTexture* mt = texture_table_get(name);
    /* 复用条件必须比较 samples：同名纹理（renderbuffer 的影子纹理在
     * glRenderbufferStorage 重分配时就是同名不同采样数）以不同采样数重新
     * 分配存储时，旧的单/多采样 MTLTexture 绝不能复用 —— MTLTextureType2D vs
     * 2DMultisample 是不同的纹理类型，Metal 校验会拒绝错误的组合（镜像修复
     * Vulkan 侧 get_or_create_texture 的同类 bug）。 */
    if (mt && mt->tex != nil && mt->width == width && mt->height == height &&
        mt->depth == depth && mt->levels >= levels && mt->glTarget == storageTarget &&
        mt->samples == (samples > 1 ? samples : 1) &&
        mt->vkFormat == vkf) {
        return mt; // existing allocation fits
    }
    if (!mt) {
        mt = new MetalTexture();
        texture_tbl()[name] = mt;
    }

    MTLTextureDescriptor* d = [[MTLTextureDescriptor alloc] init];
    d.pixelFormat = pf;
    d.width = (NSUInteger)width;
    d.height = (NSUInteger)height;
    d.depth = (NSUInteger)depth;
    d.mipmapLevelCount = (NSUInteger)levels;
    d.sampleCount = samples > 1 ? samples : 1;
    switch (storageTarget) {
        case GL_TEXTURE_3D:
            d.textureType = MTLTextureType3D;
            break;
        case GL_TEXTURE_CUBE_MAP:
            d.textureType = MTLTextureTypeCube;
            d.depth = 1;
            d.arrayLength = 1;
            break;
        case GL_TEXTURE_2D_ARRAY:
            d.textureType = MTLTextureType2DArray;
            d.depth = 1;
            d.arrayLength = (NSUInteger)depth;
            break;
        case GL_TEXTURE_1D:
            d.textureType = MTLTextureType2D; // emulate 1D as 2D height=1
            d.height = 1;
            d.depth = 1;
            break;
        case GL_TEXTURE_1D_ARRAY:
            // GL represents a 1D array as width x layer-count in TexImage2D.
            d.textureType = MTLTextureType2DArray;
            d.height = 1;
            d.depth = 1;
            d.arrayLength = (NSUInteger)height;
            break;
        case GL_TEXTURE_2D_MULTISAMPLE:
            d.textureType = MTLTextureType2DMultisample;
            d.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
            break;
        default:                      d.textureType = MTLTextureType2D; break;
    }
    // Attachments need RenderTarget; sampled ones need ShaderRead; mipmap
    // generation needs ShaderWrite. Cover all three — costs nothing.
    d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite |
              MTLTextureUsageRenderTarget;
    d.storageMode = MITHRIL_DMT_STORAGE_MODE(!b->unifiedMemory);

    id<MTLTexture> tex = [b->device newTextureWithDescriptor:d];
    if (tex == nil) {
        MITHRIL_LOG_ERROR("mtl", "newTexture (%dx%d levels=%d fmt=%u) failed for %u",
                          width, height, levels, (unsigned)pf, name);
        return nullptr;
    }
    mt->tex = tex;
    mt->vkFormat = vkf;
    mt->width = width; mt->height = height; mt->depth = depth;
    mt->levels = levels; mt->glTarget = storageTarget; mt->samples = samples;
    return mt;
}

void dmt_internal_texture_upload(GLuint name, int level, int x, int y, int z,
                                 int w, int h, int d, GLenum format, GLenum type,
                                 const void* pixels, const MGUnpackParams* up,
                                 int is_full_upload) {
    (void)is_full_upload;
    MetalTexture* mt = texture_table_get(name);
    if (!mt || mt->tex == nil || !pixels) return;

    int bpp = mithril::vk::host_texel_bytes(format, type);
    if (bpp <= 0) bpp = 4;
    // Unpack state (row length / alignment / skips).
    GLint rowLen = (up && up->unpackRowLength > 0) ? up->unpackRowLength : w;
    NSUInteger rowBytes = (NSUInteger)rowLen * bpp;
    NSUInteger align = (up && up->unpackAlignment > 1) ? (NSUInteger)up->unpackAlignment : 4;
    rowBytes = (rowBytes + align - 1) & ~(align - 1);
    int skipPixels = (up && up->unpackSkipPixels > 0) ? up->unpackSkipPixels : 0;
    int skipRows = (up && up->unpackSkipRows > 0) ? up->unpackSkipRows : 0;
    int skipImages = (up && up->unpackSkipImages > 0) ? up->unpackSkipImages : 0;
    const uint8_t* base = (const uint8_t*)pixels +
                          (NSUInteger)(skipPixels * bpp) +
                          (NSUInteger)skipRows * rowBytes;
    const NSUInteger imageHeight =
        (up && up->unpackImageHeight > 0) ? (NSUInteger)up->unpackImageHeight
                                          : (NSUInteger)h;
    NSUInteger imageBytes = rowBytes * imageHeight;
    if (skipImages) base += (NSUInteger)skipImages * imageBytes;

    MTLRegion region;
    region.origin = MTLOriginMake(x, y, z);
    region.size = MTLSizeMake(w, h, d);

    // Per-face upload for cube maps: z is the face index.
    if (mt->glTarget == GL_TEXTURE_CUBE_MAP) {
        region.origin.z = 0;
        region.size.depth = 1;
        NSUInteger slice = (NSUInteger)((z >= 0) ? (z % 6) : 0);
        [mt->tex replaceRegion:region mipmapLevel:(NSUInteger)level slice:slice
                      withBytes:base bytesPerRow:rowBytes bytesPerImage:imageBytes];
    } else if (mt->glTarget == GL_TEXTURE_2D_ARRAY) {
        // Array layers are Metal slices, never region.depth.
        region.origin.z = 0;
        region.size.depth = 1;
        for (int layer = 0; layer < d; ++layer) {
            [mt->tex replaceRegion:region mipmapLevel:(NSUInteger)level
                             slice:(NSUInteger)(z + layer)
                         withBytes:base + (NSUInteger)layer * imageBytes
                        bytesPerRow:rowBytes bytesPerImage:imageBytes];
        }
    } else if (mt->glTarget == GL_TEXTURE_1D_ARRAY) {
        // GL 1D-array y/height select layers; each Metal slice is height=1.
        region.origin = MTLOriginMake(x, 0, 0);
        region.size = MTLSizeMake(w, 1, 1);
        for (int layer = 0; layer < h; ++layer) {
            [mt->tex replaceRegion:region mipmapLevel:(NSUInteger)level
                             slice:(NSUInteger)(y + layer)
                         withBytes:base + (NSUInteger)layer * rowBytes
                        bytesPerRow:rowBytes bytesPerImage:rowBytes];
        }
    } else if (mt->glTarget == GL_TEXTURE_3D) {
        [mt->tex replaceRegion:region mipmapLevel:(NSUInteger)level slice:0
                      withBytes:base bytesPerRow:rowBytes bytesPerImage:imageBytes];
    } else {
        region.origin.z = 0;
        region.size.depth = 1;
        [mt->tex replaceRegion:region mipmapLevel:(NSUInteger)level
                      withBytes:base bytesPerRow:rowBytes];
    }
}

void dmt_internal_texture_upload_compressed(GLuint name, int level, int x, int y,
                                            int z, int w, int h, int d,
                                            GLenum internalFormat, GLsizei dataLen,
                                            const void* pixels, int is_full_upload) {
    (void)is_full_upload;
    MetalTexture* mt = texture_table_get(name);
    if (!mt || mt->tex == nil || !pixels || dataLen <= 0) return;

    // Compressed payloads pass through verbatim: bytesPerRow/bytesPerImage = 0
    // tells Metal the buffer is tightly packed in the format's block layout.
    MTLRegion region;
    region.origin = MTLOriginMake(x, y, z);
    region.size = MTLSizeMake(w, h, d);
    const uint8_t* base = static_cast<const uint8_t*>(pixels);
    if (mt->glTarget == GL_TEXTURE_CUBE_MAP) {
        region.origin.z = 0;
        region.size.depth = 1;
        [mt->tex replaceRegion:region mipmapLevel:(NSUInteger)level
                         slice:(NSUInteger)z
                     withBytes:base bytesPerRow:0 bytesPerImage:0];
    } else if (mt->glTarget == GL_TEXTURE_2D_ARRAY) {
        region.origin.z = 0;
        region.size.depth = 1;
        if (d <= 0 || dataLen % d != 0) {
            MITHRIL_LOG_WARN("mtl", "compressed 2D-array upload has invalid layer payload: bytes=%d layers=%d",
                             (int)dataLen, d);
            return;
        }
        const NSUInteger layerBytes = (NSUInteger)dataLen / (NSUInteger)d;
        for (int layer = 0; layer < d; ++layer) {
            [mt->tex replaceRegion:region mipmapLevel:(NSUInteger)level
                             slice:(NSUInteger)(z + layer)
                         withBytes:base + (NSUInteger)layer * layerBytes
                        bytesPerRow:0 bytesPerImage:0];
        }
    } else if (mt->glTarget == GL_TEXTURE_1D_ARRAY) {
        region.origin = MTLOriginMake(x, 0, 0);
        region.size = MTLSizeMake(w, 1, 1);
        if (h <= 0 || dataLen % h != 0) {
            MITHRIL_LOG_WARN("mtl", "compressed 1D-array upload has invalid layer payload: bytes=%d layers=%d",
                             (int)dataLen, h);
            return;
        }
        const NSUInteger layerBytes = (NSUInteger)dataLen / (NSUInteger)h;
        for (int layer = 0; layer < h; ++layer) {
            [mt->tex replaceRegion:region mipmapLevel:(NSUInteger)level
                             slice:(NSUInteger)(y + layer)
                         withBytes:base + (NSUInteger)layer * layerBytes
                        bytesPerRow:0 bytesPerImage:0];
        }
    } else if (mt->glTarget == GL_TEXTURE_3D) {
        [mt->tex replaceRegion:region mipmapLevel:(NSUInteger)level slice:0
                      withBytes:base bytesPerRow:0 bytesPerImage:0];
    } else {
        region.origin.z = 0;
        region.size.depth = 1;
        [mt->tex replaceRegion:region mipmapLevel:(NSUInteger)level
                      withBytes:base bytesPerRow:0];
    }
}

void dmt_internal_clear_texture(GLuint name, int level, int x, int y, int z,
                                int w, int h, int d, GLenum format, GLenum type,
                                const void* data) {
    MetalTexture* mt = texture_table_get(name);
    if (!mt || mt->tex == nil) return;
    int bpp = mithril::vk::host_texel_bytes(format, type);
    if (bpp <= 0) bpp = 4;
    if (w <= 0 || h <= 0 || d <= 0) {
        w = mt->width; h = mt->height; d = mt->depth;
    }

    // Build a complete 2D image for one layer/depth slice.  Passing a
    // single row for a region whose height > 1 lets Metal read past the row.
    std::vector<uint8_t> clearTexel(bpp, 0);
    if (data) std::memcpy(clearTexel.data(), data, bpp);
    const NSUInteger rowBytes = (NSUInteger)w * bpp;
    const NSUInteger imageRows = (mt->glTarget == GL_TEXTURE_1D_ARRAY) ? 1u : (NSUInteger)h;
    std::vector<uint8_t> image(rowBytes * imageRows);
    for (NSUInteger row = 0; row < imageRows; ++row) {
        for (int col = 0; col < w; ++col) {
            std::memcpy(image.data() + row * rowBytes + (NSUInteger)col * bpp,
                        clearTexel.data(), bpp);
        }
    }

    if (mt->glTarget == GL_TEXTURE_2D_ARRAY) {
        MTLRegion region = MTLRegionMake2D(x, y, w, h);
        for (int layer = 0; layer < d; ++layer) {
            [mt->tex replaceRegion:region mipmapLevel:(NSUInteger)level
                             slice:(NSUInteger)(z + layer)
                         withBytes:image.data() bytesPerRow:rowBytes
                        bytesPerImage:rowBytes * (NSUInteger)h];
        }
    } else if (mt->glTarget == GL_TEXTURE_1D_ARRAY) {
        MTLRegion region = MTLRegionMake2D(x, 0, w, 1);
        for (int layer = 0; layer < h; ++layer) {
            [mt->tex replaceRegion:region mipmapLevel:(NSUInteger)level
                             slice:(NSUInteger)(y + layer)
                         withBytes:image.data() bytesPerRow:rowBytes
                        bytesPerImage:rowBytes];
        }
    } else if (mt->glTarget == GL_TEXTURE_CUBE_MAP) {
        MTLRegion region = MTLRegionMake2D(x, y, w, h);
        [mt->tex replaceRegion:region mipmapLevel:(NSUInteger)level
                         slice:(NSUInteger)z
                     withBytes:image.data() bytesPerRow:rowBytes
                    bytesPerImage:rowBytes * (NSUInteger)h];
    } else if (mt->glTarget == GL_TEXTURE_3D) {
        MTLRegion region;
        region.origin = MTLOriginMake(x, y, z);
        region.size = MTLSizeMake(w, h, d);
        std::vector<uint8_t> volume(image.size() * (NSUInteger)d);
        for (int layer = 0; layer < d; ++layer)
            std::memcpy(volume.data() + image.size() * (NSUInteger)layer,
                        image.data(), image.size());
        [mt->tex replaceRegion:region mipmapLevel:(NSUInteger)level slice:0
                      withBytes:volume.data() bytesPerRow:rowBytes
                     bytesPerImage:rowBytes * (NSUInteger)h];
    } else {
        MTLRegion region = MTLRegionMake2D(x, y, w, h);
        [mt->tex replaceRegion:region mipmapLevel:(NSUInteger)level
                      withBytes:image.data() bytesPerRow:rowBytes];
    }
}

void dmt_internal_generate_mipmaps(GLuint name) {
    MetalTexture* mt = texture_table_get(name);
    if (!mt || mt->tex == nil || mt->levels <= 1) return;
    if (!ensure_command_buffer()) return;
    id<MTLBlitCommandEncoder> blit = [backend()->cmd blitCommandEncoder];
    [blit generateMipmapsForTexture:mt->tex];
    [blit endEncoding];
    note_non_render_commands();
}

// ---- Blits -------------------------------------------------------------------

void dmt_internal_blit_images_raw(MetalTexture* src, MetalTexture* dst,
                                  int srcX0, int srcY0, int srcX1, int srcY1,
                                  int dstX0, int dstY0, int dstX1, int dstY1,
                                  GLbitfield mask, GLenum filter,
                                  int is_dst_default_fbo, int dst_height) {
    if (!src || !dst || !src->tex || !dst->tex) return;
    bool depth = (mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) != 0;

    // Vulkan-convention destination rect → Metal top-left rect. The frontend
    // already emits top-left coordinates for the default FBO; user-FBO rects
    // come in GL bottom-left orientation and must flip (mirrors the Vulkan
    // blit's row mapping).
    if (!is_dst_default_fbo && dst_height > 0) {
        int newY0 = dst_height - dstY1;
        int newY1 = dst_height - dstY0;
        dstY0 = newY0; dstY1 = newY1;
    }

    const bool sameSize = (srcX1 - srcX0 == dstX1 - dstX0) &&
                          (srcY1 - srcY0 == dstY1 - dstY0);
    if (depth || (sameSize && filter != GL_LINEAR)) {
        // 1:1 copy path — blit encoder, works for depth too.
        NSUInteger w = (NSUInteger)(dstX1 - dstX0);
        NSUInteger h = (NSUInteger)(dstY1 - dstY0);
        if (w > (NSUInteger)src->tex.width)  w = src->tex.width;
        if (h > (NSUInteger)src->tex.height) h = src->tex.height;
        if (w > (NSUInteger)dst->tex.width)  w = dst->tex.width;
        if (h > (NSUInteger)dst->tex.height) h = dst->tex.height;
        if (w == 0 || h == 0) return;
        if (!ensure_command_buffer()) return;
        id<MTLBlitCommandEncoder> blit = [backend()->cmd blitCommandEncoder];
        [blit copyFromTexture:src->tex
                  sourceSlice:0 sourceLevel:0
                 sourceOrigin:MTLOriginMake(srcX0, srcY0, 0)
                   sourceSize:MTLSizeMake(w, h, 1)
                    toTexture:dst->tex
               destinationSlice:0 destinationLevel:0
              destinationOrigin:MTLOriginMake(dstX0, dstY0, 0)];
        [blit endEncoding];
        note_non_render_commands();
        return;
    }
    // Scaled color blit via the shader path.
    BlitParams p;
    p.srcX0 = (float)srcX0; p.srcY0 = (float)srcY0;
    p.srcX1 = (float)srcX1; p.srcY1 = (float)srcY1;
    p.dstX0 = (float)dstX0; p.dstY0 = (float)dstY0;
    p.dstX1 = (float)dstX1; p.dstY1 = (float)dstY1;
    p.srcW = (float)src->tex.width;  p.srcH = (float)src->tex.height;
    p.dstW = (float)dst->tex.width;  p.dstH = (float)dst->tex.height;
    run_scaled_blit(src, dst, p, filter == GL_LINEAR);
}

/* MSAA resolve（glBlitFramebuffer 多采样 → 单采样）。Metal 通过 render-pass
 * attachment 的 resolveTexture + MTLStoreActionMultisampleResolve 完成解析；
 * MTLBlitCommandEncoder 没有纹理 resolve API。
 *
 * 限制：blit resolve 只有整纹理形式（无子矩形 API）。GL 的子矩形 resolve 在此
 * 退化为整纹理解析 —— 实际用法（MC/Sodium 的 cascade、后处理 downsample）几乎
 * 全是全 FBO resolve，且源/目标尺寸一致，行为等价。 */
void dmt_internal_resolve_images(MetalTexture* src, MetalTexture* dst,
                                 int x, int y, int width, int height) {
    if (!src || !dst || src->tex == nil || dst->tex == nil) return;
    if (width <= 0 || height <= 0) return;
    if (src->tex.sampleCount <= 1) {
        static bool warnedSS = false;
        if (!warnedSS) {
            warnedSS = true;
            MITHRIL_LOG_WARN("mtl", "dmt_internal_resolve_images: source is "
                             "single-sample — nothing to resolve");
        }
        return;
    }
    if (x != 0 || y != 0 ||
        width  < (int)src->tex.width || height < (int)src->tex.height) {
        static bool warnedSub = false;
        if (!warnedSub) {
            warnedSub = true;
            MITHRIL_LOG_WARN("mtl", "dmt_internal_resolve_images: sub-rect "
                             "resolve falls back to whole-texture (Metal blit "
                             "resolve has no sub-rect form)");
        }
    }
    if (!ensure_command_buffer()) return;
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    const bool hasStencil = format_has_stencil(src->vkFormat);
    const bool isDepth = src->vkFormat == VK_FORMAT_D16_UNORM ||
                         src->vkFormat == VK_FORMAT_X8_D24_UNORM_PACK32 ||
                         src->vkFormat == VK_FORMAT_D32_SFLOAT ||
                         src->vkFormat == VK_FORMAT_D24_UNORM_S8_UINT ||
                         src->vkFormat == VK_FORMAT_D32_SFLOAT_S8_UINT;
    if (isDepth) {
        pass.depthAttachment.texture = src->tex;
        pass.depthAttachment.resolveTexture = dst->tex;
        pass.depthAttachment.loadAction = MTLLoadActionLoad;
        pass.depthAttachment.storeAction = MTLStoreActionMultisampleResolve;
    } else if (!hasStencil) {
        pass.colorAttachments[0].texture = src->tex;
        pass.colorAttachments[0].resolveTexture = dst->tex;
        pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
        pass.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
    }
    if (hasStencil) {
        pass.stencilAttachment.texture = src->tex;
        pass.stencilAttachment.resolveTexture = dst->tex;
        pass.stencilAttachment.loadAction = MTLLoadActionLoad;
        pass.stencilAttachment.storeAction = MTLStoreActionMultisampleResolve;
    }
    id<MTLRenderCommandEncoder> enc =
        [backend()->cmd renderCommandEncoderWithDescriptor:pass];
    if (enc == nil) {
        MITHRIL_LOG_WARN("mtl", "MSAA resolve encoder creation failed");
        return;
    }
    [enc endEncoding];
    note_non_render_commands();
}

// ---- Readback -----------------------------------------------------------------

int dmt_internal_read_pixels(MetalTexture* colorSrc, int x, int y, int w, int h,
                             GLenum format, GLenum type, void* out_pixels,
                             int flip_y) {
    if (!colorSrc || colorSrc->tex == nil || !out_pixels || w <= 0 || h <= 0) return 0;
    Backend* b = backend();
    if (!b->initialized) return 0;

    // Clamp the rect into bounds.
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)colorSrc->tex.width)  w = (int)colorSrc->tex.width - x;
    if (y + h > (int)colorSrc->tex.height) h = (int)colorSrc->tex.height - y;
    if (w <= 0 || h <= 0) return 0;

    NSUInteger rowBytes = (NSUInteger)w * 4;
    NSUInteger total = rowBytes * (NSUInteger)h;
    id<MTLBuffer> rb = [b->device newBufferWithLength:total
                                              options:MITHRIL_DMT_STORAGE(!b->unifiedMemory)];
    if (rb == nil) return 0;

    // Synchronous one-shot command buffer (does not touch the frame buffer).
    id<MTLCommandBuffer> cb = [b->queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    [blit copyFromTexture:colorSrc->tex
             sourceSlice:0 sourceLevel:0
            sourceOrigin:MTLOriginMake(x, y, 0)
              sourceSize:MTLSizeMake(w, h, 1)
                toBuffer:rb
        destinationOffset:0
   destinationBytesPerRow:rowBytes
 destinationBytesPerImage:total];
#if TARGET_OS_OSX
    if (!b->unifiedMemory) [blit synchronizeResource:rb];
#endif
    [blit endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    if (cb.error != nil) return 0;

    // User FBO textures keep GL bottom-left row order in storage. The
    // drawable/default framebuffer is presentation-oriented and therefore
    // requires the historical row reversal. The caller knows which source
    // class is being read and passes flip_y explicitly.
    const uint8_t* src = (const uint8_t*)rb.contents;
    bool bgra = (colorSrc->vkFormat == VK_FORMAT_B8G8R8A8_UNORM);
    for (int r = 0; r < h; ++r) {
        const uint8_t* srow = src + (NSUInteger)r * rowBytes;
        const int dstRow = flip_y ? (h - 1 - r) : r;
        uint8_t* drow = (uint8_t*)out_pixels + (NSUInteger)dstRow * rowBytes;
        if (bgra) {
            for (int c = 0; c < w; ++c) {
                drow[c * 4 + 0] = srow[c * 4 + 2];
                drow[c * 4 + 1] = srow[c * 4 + 1];
                drow[c * 4 + 2] = srow[c * 4 + 0];
                drow[c * 4 + 3] = srow[c * 4 + 3];
            }
        } else {
            std::memcpy(drow, srow, rowBytes);
        }
    }
    return 1;
}

} // namespace dmt
} // namespace mithril

#endif // __APPLE__
