// Mithril-Wrapper - MG_Backend/DirectMetal/MetalResources.h
// GL-object-keyed tables for MTLBuffer / MTLTexture / MTLSamplerState plus
// the texture image operations (upload / compressed upload / clear / mipmaps /
// blit / readback). The tables are keyed by the GL name exactly like
// DirectVulkan's, so the frontend's get_or_create + upload + delete sequence
// drives object lifetime identically.
#ifndef MITHRIL_DIRECTMETAL_RESOURCES_H
#define MITHRIL_DIRECTMETAL_RESOURCES_H

#ifdef __APPLE__

#include "MetalDevice.h"
#include "../BackendTypes.h"   // MGUnpackParams
#include <unordered_map>

namespace mithril {
namespace dmt {

/* ---- Tables (MetalResources.mm) ---- */
MetalBuffer*  buffer_table_get(GLuint name);
void          buffer_table_erase(GLuint name);
std::unordered_map<GLuint, MetalBuffer*>& buffer_table();

MetalTexture* texture_table_get(GLuint name);
void          texture_table_erase(GLuint name);

MetalSampler* sampler_table_get(GLuint name);
// Drop the MTLSamplerState cached for a texture name (glTexParameter /
// glSamplerParameter changed its filter/wrap state; the next fetch rebuilds).
void          sampler_table_erase(GLuint name);

// Default 1x1 black texture + sampler (descriptor completeness fallback).
MetalTexture* default_texture_tex();
MetalSampler* default_texture_sampler();

/* Blit shader machinery (MetalPipeline.mm exposes it; used by blits here). */
struct BlitParams {
    float srcX0, srcY0, srcX1, srcY1;   // source rect, pixels (top-left origin)
    float dstX0, dstY0, dstX1, dstY1;   // dest rect, pixels (top-left origin)
    float srcW, srcH;                   // source texture size
    float dstW, dstH;                   // dest texture size
};
// Scaled color blit via a fullscreen-triangle shader. Ends the active render
// encoder, runs a dedicated one, restores nothing (callers re-begin passes).
void run_scaled_blit(MetalTexture* src, MetalTexture* dst, const BlitParams& p,
                     bool linearFilter);

/* ---- Texture helpers ---- */
// Create (or resize-recreate) the MTLTexture for a GL name. Mirrors
// dvk_get_or_create_texture semantics.
MetalTexture* get_or_create_texture(GLuint name, int width, int height,
                                    int depth, int levels,
                                    GLenum internal_format, GLenum target,
                                    int samples);

/* ---- Internal entry-layer helpers (bodies in MetalResources.mm; called by
 * MetalBackend.mm's dmt_* wrappers) ---- */
MetalBuffer* dmt_internal_get_or_create_buffer(GLuint name, const void* data,
                                               size_t size);
// GL_TEXTURE_BUFFER（glTexBuffer/glTexBufferRange，samplerBuffer）：按纹理名
// 取（或建）MTLBuffer 派生的 texture_buffer 视图。缓存键 = 源 MTLBuffer +
// GL Buffer::contentVersion + (offset,size,format)，源变化即重建。给
// MetalPipeline.mm 的 descriptor 绑定路径用（对应 dvk 的
// get_or_create_texel_buffer_view）。
id<MTLTexture> get_or_create_buffer_texture(GLuint texName);
MetalBuffer* dmt_internal_create_buffer_storage(GLuint name, NSUInteger size,
                                                bool persistent);
void         dmt_internal_buffer_upload(GLuint name, GLintptr offset,
                                        const void* data, size_t size);
MetalSampler* dmt_internal_get_or_create_sampler(GLuint name, GLint min_filter,
                                                 GLint mag_filter, GLint wrap_s,
                                                 GLint wrap_t, GLint wrap_r,
                                                 const float* border_color);
void dmt_internal_texture_upload(GLuint name, int level, int x, int y, int z,
                                 int w, int h, int d, GLenum format, GLenum type,
                                 const void* pixels, const MGUnpackParams* up,
                                 int is_full_upload);
void dmt_internal_texture_upload_compressed(GLuint name, int level, int x, int y,
                                            int z, int w, int h, int d,
                                            GLenum internalFormat, GLsizei dataLen,
                                            const void* pixels, int is_full_upload);
void dmt_internal_clear_texture(GLuint name, int level, int x, int y, int z,
                                int w, int h, int d, GLenum format, GLenum type,
                                const void* data);
void dmt_internal_generate_mipmaps(GLuint name);
// Raw image-level blit between two MetalTexture wrappers. is_dst_default_fbo
// selects the default-FBO destination Y flip (Backend.h contract); the rects
// otherwise arrive exactly as the frontend emitted them (matching
// dvk_blit_texture / dvk_blit_images pass-through behaviour).
void dmt_internal_blit_images_raw(MetalTexture* src, MetalTexture* dst,
                                  int srcX0, int srcY0, int srcX1, int srcY1,
                                  int dstX0, int dstY0, int dstX1, int dstY1,
                                  GLbitfield mask, GLenum filter,
                                  int is_dst_default_fbo, int dst_height);
// Whole-texture MSAA resolve (multisample src -> single-sample dst) via the
// blit encoder's native resolveFromTexture:toTexture: (color AND depth). See
// MetalResources.mm for the sub-rect caveat.
void dmt_internal_resolve_images(MetalTexture* src, MetalTexture* dst,
                                 int x, int y, int width, int height);
// Synchronous RGBA8 (or Depth32Float) readback from a texture into the
// caller's buffer, GL bottom-left row order. Returns 1 on success.
int  dmt_internal_read_pixels(MetalTexture* colorSrc, int x, int y, int w, int h,
                              GLenum format, GLenum type, void* out_pixels);

} // namespace dmt
} // namespace mithril

#endif // __APPLE__
#endif // MITHRIL_DIRECTMETAL_RESOURCES_H
