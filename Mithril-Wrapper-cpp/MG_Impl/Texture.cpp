// Mithril-Wrapper - MG_Impl/Texture.cpp
// Texture object management: storage, upload, parameters, mipmap generation.
//
// This is the Vulkan/MoltenVK rewrite of the former gl/texture.cpp. The Metal
// MTLTexture calls are replaced with the Vulkan backend C API
// (backend_get_or_create_texture / backend_texture_upload /
// backend_texture_set_params / backend_delete_texture) declared in
// MG_Backend/Backend.h. Vulkan VkImage/VkImageView objects are owned by the
// backend and keyed by GL texture name.
#include "includes.h"

extern "C" {

void glGenTextures(GLsizei n, GLuint* textures) {
    MITHRIL_ENSURE_INIT();
    mithril::state_gen_names("texture", n, textures);
    for (GLsizei i = 0; i < n; ++i) {
        mithril::Texture t{};
        t.id = textures[i];
        g_state->textures[textures[i]] = t;
    }
}

void glDeleteTextures(GLsizei n, const GLuint* textures) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !textures) return;
    for (GLsizei i = 0; i < n; ++i) {
        GLuint name = textures[i];
        if (name == 0) continue;
        for (int u = 0; u < mithril::kMaxTextureUnits; ++u) {
            if (g_state->boundTextures[u] == name) g_state->boundTextures[u] = 0;
        }
        backend_delete_texture(name);
        g_state->textures.erase(name);
    }
}

void glBindTexture(GLenum target, GLuint texture) {
    MITHRIL_ENSURE_INIT();
    if (texture != 0 && !mithril::state_get_texture(texture)) {
        g_state->textures[texture] = mithril::Texture{};
        g_state->textures[texture].id = texture;
    }
    GLuint unit = g_state->activeTextureUnit;
    if (unit < mithril::kMaxTextureUnits) {
        g_state->boundTextures[unit] = texture;
        g_state->boundTextureTargets[unit] = target;
    }
    if (mithril::Texture* t = mithril::state_get_texture(texture)) {
        t->target = target;
    }
}

static mithril::Texture* bound_texture_for_unit() {
    GLuint unit = g_state->activeTextureUnit;
    if (unit >= mithril::kMaxTextureUnits) return nullptr;
    GLuint id = g_state->boundTextures[unit];
    return mithril::state_get_texture(id);
}

void glTexImage2D(GLenum target, GLint level, GLint internalFormat,
                  GLsizei width, GLsizei height, GLint border,
                  GLenum format, GLenum type, const void* pixels) {
    MITHRIL_ENSURE_INIT();
    if (border != 0) { mithril::state_set_error(GL_INVALID_VALUE); return; }

    // GL_PROXY_TEXTURE_2D: no real texture is created. Just record the
    // requested dimensions so glGetTexLevelParameteriv can report them.
    // Minecraft probes max texture size this way (GL_PROXY_TEXTURE_2D with
    // progressively larger sizes until the query returns 0).
    if (target == GL_PROXY_TEXTURE_2D) {
        // Accept the size if it's within our reported GL_MAX_TEXTURE_SIZE.
        // A size of 0 means "unsupported" per the GL spec.
        GLint maxSize = 16384; // matches GL_MAX_TEXTURE_SIZE in Getter.cpp
        if (width > 0 && height > 0 && width <= maxSize && height <= maxSize) {
            g_state->proxyTexture2D.width  = width;
            g_state->proxyTexture2D.height = height;
            g_state->proxyTexture2D.internalFormat = internalFormat;
            g_state->proxyTexture2D.valid = true;
        } else {
            g_state->proxyTexture2D.valid = false;
            g_state->proxyTexture2D.width = 0;
            g_state->proxyTexture2D.height = 0;
        }
        return;
    }

    mithril::Texture* t = bound_texture_for_unit();
    if (!t) return;
    if (level == 0) {
        t->internalFormat = internalFormat;
        t->width  = width;
        t->height = height;
        t->depth  = 1;
    }
    if (t->levels < level + 1) t->levels = level + 1;

    backend_get_or_create_texture(t->id, width, height, 1, t->levels,
                                  internalFormat, target, 1);
    if (pixels) {
        backend_texture_upload(t->id, level, 0, 0, 0, width, height, 1,
                               format, type, pixels, g_state->unpackAlignment);
    }
}

void glTexImage3D(GLenum target, GLint level, GLint internalFormat,
                  GLsizei width, GLsizei height, GLsizei depth, GLint border,
                  GLenum format, GLenum type, const void* pixels) {
    MITHRIL_ENSURE_INIT();
    if (border != 0) { mithril::state_set_error(GL_INVALID_VALUE); return; }
    mithril::Texture* t = bound_texture_for_unit();
    if (!t) return;
    if (level == 0) {
        t->internalFormat = internalFormat;
        t->width  = width;
        t->height = height;
        t->depth  = depth;
    }
    if (t->levels < level + 1) t->levels = level + 1;

    backend_get_or_create_texture(t->id, width, height, depth, t->levels,
                                  internalFormat, target, 1);
    if (pixels) {
        backend_texture_upload(t->id, level, 0, 0, 0, width, height, depth,
                               format, type, pixels, g_state->unpackAlignment);
    }
}

/*
 * glTexStorage2D / glTexStorage3D: allocate immutable storage for a texture.
 * Minecraft 1.21 uses these (rather than glTexImage2D) to create framebuffer
 * attachments, especially depth/stencil textures. We set the GL-level metadata
 * (internalFormat, dimensions, levels) and create the Vulkan texture with the
 * correct VkFormat up front. No pixel data is uploaded (immutable storage
 * starts uninitialised, like glTexImage2D with pixels=NULL).
 *
 * The Vulkan image is created with initialLayout = UNDEFINED. We immediately
 * transition it to SHADER_READ_ONLY_OPTIMAL so that:
 *   - If the texture is sampled before being rendered into, the layout is valid.
 *   - If the texture is attached to an FBO and rendered into, dynamic rendering
 *     will transition it to COLOR/DEPTH_STENCIL_ATTACHMENT_OPTIMAL (and the
 *     tracked layout lets our barrier code emit the correct oldLayout).
 * Without this transition, the texture sits in UNDEFINED and a subsequent
 * vkCmdBindDescriptorSets + draw that samples it would be a validation error.
 */
void glTexStorage2D(GLenum target, GLsizei levels, GLenum internalFormat,
                    GLsizei width, GLsizei height) {
    MITHRIL_ENSURE_INIT();
    mithril::Texture* t = bound_texture_for_unit();
    if (!t || levels <= 0) return;
    t->internalFormat = internalFormat;
    t->width  = width;
    t->height = height;
    t->depth  = 1;
    t->levels = levels;

    backend_get_or_create_texture(t->id, width, height, 1, levels,
                                  internalFormat, target, 1);
    // Transition UNDEFINED -> SHADER_READ_ONLY_OPTIMAL so the texture is in a
    // valid sampling layout before any draw references it.
    backend_transition_texture_layout(t->id, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void glTexStorage3D(GLenum target, GLsizei levels, GLenum internalFormat,
                    GLsizei width, GLsizei height, GLsizei depth) {
    MITHRIL_ENSURE_INIT();
    mithril::Texture* t = bound_texture_for_unit();
    if (!t || levels <= 0) return;
    t->internalFormat = internalFormat;
    t->width  = width;
    t->height = height;
    t->depth  = depth;
    t->levels = levels;

    backend_get_or_create_texture(t->id, width, height, depth, levels,
                                  internalFormat, target, 1);
    backend_transition_texture_layout(t->id, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                     GLsizei width, GLsizei height,
                     GLenum format, GLenum type, const void* pixels) {
    MITHRIL_ENSURE_INIT();
    (void)target;
    mithril::Texture* t = bound_texture_for_unit();
    if (!t || !pixels) return;
    backend_texture_upload(t->id, level, xoffset, yoffset, 0,
                           width, height, 1, format, type, pixels,
                           g_state->unpackAlignment);
}

void glTexSubImage3D(GLenum target, GLint level,
                     GLint xoffset, GLint yoffset, GLint zoffset,
                     GLsizei width, GLsizei height, GLsizei depth,
                     GLenum format, GLenum type, const void* pixels) {
    MITHRIL_ENSURE_INIT();
    (void)target;
    mithril::Texture* t = bound_texture_for_unit();
    if (!t || !pixels) return;
    backend_texture_upload(t->id, level, xoffset, yoffset, zoffset,
                           width, height, depth, format, type, pixels,
                           g_state->unpackAlignment);
}

void glTexImage2DMultisample(GLenum target, GLsizei samples, GLenum internalformat,
                             GLsizei width, GLsizei height,
                             GLboolean fixedsamplelocations) {
    MITHRIL_ENSURE_INIT();
    (void)fixedsamplelocations;
    mithril::Texture* t = bound_texture_for_unit();
    if (!t) return;
    t->internalFormat = internalformat;
    t->width  = width;
    t->height = height;
    t->depth  = 1;
    backend_get_or_create_texture(t->id, width, height, 1, 1,
                                  internalformat, target, samples > 1 ? samples : 1);
}

void glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
    MITHRIL_ENSURE_INIT();
    (void)target;
    mithril::Texture* t = bound_texture_for_unit();
    if (!t) return;
    GLint p = (GLint)param;
    switch (pname) {
        case GL_TEXTURE_MIN_FILTER: t->minFilter = p; break;
        case GL_TEXTURE_MAG_FILTER: t->magFilter = p; break;
        case GL_TEXTURE_WRAP_S:     t->wrapS = p; break;
        case GL_TEXTURE_WRAP_T:     t->wrapT = p; break;
        case GL_TEXTURE_WRAP_R:     t->wrapR = p; break;
        default: break;
    }
    backend_texture_set_params(t->id, t->minFilter, t->magFilter,
                               t->wrapS, t->wrapT, t->wrapR, t->borderColor);
}

void glTexParameteri(GLenum target, GLenum pname, GLint param) {
    glTexParameterf(target, pname, (GLfloat)param);
}

void glTexParameterfv(GLenum target, GLenum pname, const GLfloat* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        mithril::Texture* t = bound_texture_for_unit();
        if (!t) return;
        for (int i = 0; i < 4; ++i) t->borderColor[i] = params[i];
        backend_texture_set_params(t->id, t->minFilter, t->magFilter,
                                   t->wrapS, t->wrapT, t->wrapR, t->borderColor);
        return;
    }
    glTexParameterf(target, pname, params[0]);
}

void glTexParameteriv(GLenum target, GLenum pname, const GLint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    glTexParameterf(target, pname, (GLfloat)params[0]);
}

void glGenerateMipmap(GLenum target) {
    MITHRIL_ENSURE_INIT();
    (void)target;
    mithril::Texture* t = bound_texture_for_unit();
    if (!t) return;
    // Compute mip level count if the app never called glTexStorage*(levels=N).
    // glGenerateMipmap is the legacy way to request a full mip chain: the
    // driver allocates log2(max(w,h))+1 levels. Our Vulkan texture was
    // created with whatever level count was last set on the GL object; if
    // it's still 1, the backend's generate_mipmaps becomes a no-op, so the
    // app sees the base level only. This is acceptable for MC Java (its
    // modern pipeline uses glTexStorage2D for mipmapped textures).
    t->generateMipmaps = true;
    backend_generate_mipmaps(t->id);
}

void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                  GLenum format, GLenum type, void* pixels) {
    MITHRIL_ENSURE_INIT();
    if (!pixels || width <= 0 || height <= 0) return;
    // Delegate to the backend: it resolves the current colour attachment
    // (EGL default framebuffer or the user FBO's GL_COLOR_ATTACHMENT0),
    // transitions it to TRANSFER_SRC_OPTIMAL, copies into a host-visible
    // staging buffer via vkCmdCopyImageToBuffer, and synchronously maps +
    // memcpy's into the caller's buffer. Returns 0 if readback isn't
    // possible (e.g. no FBO bound to the default framebuffer).
    (void)backend_read_pixels((int)x, (int)y, (int)width, (int)height,
                              format, type, pixels);
}

void glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat,
                      GLint x, GLint y, GLsizei width, GLsizei height, GLint border) {
    MITHRIL_ENSURE_INIT();
    (void)target; (void)level; (void)internalformat;
    (void)x; (void)y; (void)width; (void)height; (void)border;
}

void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                         GLint x, GLint y, GLsizei width, GLsizei height) {
    MITHRIL_ENSURE_INIT();
    (void)target; (void)level; (void)xoffset; (void)yoffset;
    (void)x; (void)y; (void)width; (void)height;
}

} // extern "C"
