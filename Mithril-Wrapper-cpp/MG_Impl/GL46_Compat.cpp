// Mithril-Wrapper - MG_Impl/GL46_Compat.cpp
// Missing OpenGL 4.3-4.6 core profile function implementations.
//
// The wrapper translates OpenGL calls to Vulkan/MoltenVK. Many GL 4.3-4.6
// functions were missing, causing mods (Sodium, Iris) and Minecraft 1.21.1
// to fail capability checks. These functions are resolved via
// dlsym(RTLD_DEFAULT, name), so they just need to be extern "C" with default
// visibility.
//
// Strategy:
//  - DSA (Direct State Access) functions delegate to the non-DSA variants
//    using a save-bind-call-restore pattern.
//  - Query/reflection functions that the wrapper cannot meaningfully answer
//    return sensible defaults (0, GL_INVALID_INDEX, empty strings).
//  - No-op functions still call MITHRIL_ENSURE_INIT() so the renderer is
//    initialized, and include a comment explaining why they are no-ops.
#include "includes.h"
#include <cstring>

// GL enums absent from the project's minimal glcorearb.h.
#ifndef GL_INVALID_INDEX
#define GL_INVALID_INDEX 0xFFFFFFFFu
#endif
#ifndef GL_UNIFORM_TYPE
#define GL_UNIFORM_TYPE 0x8A37
#endif

extern "C" {

/* ====================================================================
 * CRITICAL - MC/Sodium/Iris commonly use these
 * ==================================================================== */

/* 1. glGetGraphicsResetStatus - MC probes this for GPU reset detection.
 * Vulkan/MoltenVK does not expose GPU reset status through GL; return
 * GL_NO_ERROR so MC never thinks a reset occurred. */
GLenum glGetGraphicsResetStatus(void) {
    MITHRIL_ENSURE_INIT();
    return GL_NO_ERROR;
}

/* 2. glGetActiveUniformsiv - UBO reflection. Return defaults for unknown
 * uniforms. For GL_UNIFORM_BLOCK_INDEX return -1 (not in a block). */
void glGetActiveUniformsiv(GLuint program, GLsizei uniformCount,
                           const GLuint* uniformIndices, GLenum pname,
                           GLint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params || !uniformIndices || uniformCount <= 0) return;
    for (GLsizei i = 0; i < uniformCount; ++i) {
        switch (pname) {
            case GL_UNIFORM_BLOCK_INDEX:
                params[i] = -1;
                break;
            case GL_UNIFORM_TYPE:
            case GL_UNIFORM_SIZE:
            case GL_UNIFORM_NAME_LENGTH:
            case GL_UNIFORM_OFFSET:
            case GL_UNIFORM_ARRAY_STRIDE:
            case GL_UNIFORM_MATRIX_STRIDE:
                params[i] = 0;
                break;
            case GL_UNIFORM_IS_ROW_MAJOR:
                params[i] = GL_FALSE;
                break;
            default:
                params[i] = 0;
                break;
        }
    }
}

/* 3. glGetUniformIndices - Set all indices to GL_INVALID_INDEX (no UBO
 * reflection in the wrapper; shaders use standalone uniforms). */
void glGetUniformIndices(GLuint program, GLsizei uniformCount,
                         const GLchar* const* uniformNames,
                         GLuint* uniformIndices) {
    MITHRIL_ENSURE_INIT();
    if (!uniformIndices || !uniformNames || uniformCount <= 0) return;
    for (GLsizei i = 0; i < uniformCount; ++i) {
        uniformIndices[i] = GL_INVALID_INDEX;
    }
}

/* 4. glGetActiveUniformName - Return empty string. */
void glGetActiveUniformName(GLuint program, GLuint uniformIndex,
                            GLsizei bufSize, GLsizei* length,
                            GLchar* uniformName) {
    MITHRIL_ENSURE_INIT();
    if (length) *length = 0;
    if (uniformName && bufSize > 0) uniformName[0] = '\0';
}

/* 5. glGetActiveUniformBlockName - Return empty string. */
void glGetActiveUniformBlockName(GLuint program, GLuint uniformBlockIndex,
                                 GLsizei bufSize, GLsizei* length,
                                 GLchar* uniformName) {
    MITHRIL_ENSURE_INIT();
    if (length) *length = 0;
    if (uniformName && bufSize > 0) uniformName[0] = '\0';
}

/* 6. glClearTexImage - No-op: Vulkan clears texture data on first use via
 * loadOp=UNDEFINED. The wrapper does not support explicit texture clears. */
void glClearTexImage(GLuint texture, GLint level, GLenum format,
                     GLenum type, const void* data) {
    MITHRIL_ENSURE_INIT();
    (void)texture; (void)level; (void)format; (void)type; (void)data;
}

/* 7. glClearTexSubImage - Same as above, sub-region. */
void glClearTexSubImage(GLuint texture, GLint level, GLint xoffset,
                        GLint yoffset, GLint zoffset, GLsizei width,
                        GLsizei height, GLsizei depth, GLenum format,
                        GLenum type, const void* data) {
    MITHRIL_ENSURE_INIT();
    (void)texture; (void)level; (void)xoffset; (void)yoffset; (void)zoffset;
    (void)width; (void)height; (void)depth; (void)format; (void)type; (void)data;
}

/* 8. glReadnPixels - Delegate to glReadPixels (bufSize ignored; the wrapper's
 * glReadPixels already bounds the output). */
void glReadnPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                   GLenum format, GLenum type, GLsizei bufSize, void* data) {
    MITHRIL_ENSURE_INIT();
    (void)bufSize;
    glReadPixels(x, y, width, height, format, type, data);
}

/* 9. glMemoryBarrierByRegion - Delegate to backend_memory_barrier. Region
 * granularity is not meaningful on Vulkan/MoltenVK (tiling is hidden). */
void glMemoryBarrierByRegion(GLbitfield barriers) {
    MITHRIL_ENSURE_INIT();
    backend_memory_barrier(barriers);
}

/* 10. glGetTexLevelParameterfv - Delegate to glGetTexLevelParameteriv and
 * cast to GLfloat. */
void glGetTexLevelParameterfv(GLenum target, GLint level, GLenum pname,
                              GLfloat* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    GLint iv = 0;
    glGetTexLevelParameteriv(target, level, pname, &iv);
    *params = (GLfloat)iv;
}

/* 11. glGetCompressedTexImage - Return zeros (no compressed texture readback
 * in the wrapper; rare in MC). */
void glGetCompressedTexImage(GLenum target, GLint level, void* img) {
    MITHRIL_ENSURE_INIT();
    (void)target; (void)level;
    if (img) {
        /* Cannot know the size without querying; caller provides a buffer
         * they believe is large enough. Zero a nominal amount. */
        memset(img, 0, 1);
    }
}

/* 12. glTexBufferRange - No-op: texture buffers are not supported on
 * MoltenVK. Delegate to glTexBuffer if it existed; since it does not,
 * this is a no-op. */
void glTexBufferRange(GLenum target, GLenum internalformat, GLuint buffer,
                      GLintptr offset, GLsizeiptr size) {
    MITHRIL_ENSURE_INIT();
    (void)target; (void)internalformat; (void)buffer; (void)offset; (void)size;
}

/* 13. glTextureView - No-op: texture views are not supported on MoltenVK. */
void glTextureView(GLuint texture, GLenum target, GLuint origtexture,
                   GLenum internalformat, GLuint minlevel, GLuint numlevels,
                   GLuint minlayer, GLuint numlayers) {
    MITHRIL_ENSURE_INIT();
    (void)texture; (void)target; (void)origtexture; (void)internalformat;
    (void)minlevel; (void)numlevels; (void)minlayer; (void)numlayers;
}

/* 14. glFramebufferParameteri - No-op: framebuffer parameters (e.g.
 * GL_FRAMEBUFFER_DEFAULT_WIDTH) are not critical for MC. */
void glFramebufferParameteri(GLenum target, GLenum pname, GLint param) {
    MITHRIL_ENSURE_INIT();
    (void)target; (void)pname; (void)param;
}

/* 15. glGetFramebufferParameteriv - Return 0. */
void glGetFramebufferParameteriv(GLenum target, GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    (void)target; (void)pname;
    if (params) *params = 0;
}

/* 16. glPatchParameteri - No-op: tessellation is not supported on MoltenVK. */
void glPatchParameteri(GLenum pname, GLint value) {
    MITHRIL_ENSURE_INIT();
    (void)pname; (void)value;
}

/* 17. glPatchParameterfv - No-op: tessellation is not supported on MoltenVK. */
void glPatchParameterfv(GLenum pname, const GLfloat* values) {
    MITHRIL_ENSURE_INIT();
    (void)pname; (void)values;
}

/* 18. glDrawElementsInstancedBaseVertexBaseInstance - Set base vertex and
 * base instance, delegate to glDrawElementsInstanced, then reset.
 * Mirrors the pattern in Drawing.cpp for glDrawElementsInstancedBaseVertex. */
void glDrawElementsInstancedBaseVertexBaseInstance(GLenum mode, GLsizei count,
                                                    GLenum type,
                                                    const void* indices,
                                                    GLsizei instancecount,
                                                    GLint basevertex,
                                                    GLuint baseinstance) {
    MITHRIL_ENSURE_INIT();
    g_state->currentBaseVertex = basevertex;
    g_state->currentBaseInstance = baseinstance;
    glDrawElementsInstanced(mode, count, type, indices, instancecount);
    g_state->currentBaseVertex = 0;
    g_state->currentBaseInstance = 0;
}

/* 19. glTexStorage1D - No-op: 1D textures are rare on MC. */
void glTexStorage1D(GLenum target, GLsizei levels, GLenum internalformat,
                    GLsizei width) {
    MITHRIL_ENSURE_INIT();
    (void)target; (void)levels; (void)internalformat; (void)width;
}

/* ====================================================================
 * GL 4.3 Program Interface Queries (Iris uses these)
 * ==================================================================== */

/* 20. glGetProgramInterfaceiv - Return 0. */
void glGetProgramInterfaceiv(GLuint program, GLenum programInterface,
                             GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    (void)program; (void)programInterface; (void)pname;
    if (params) *params = 0;
}

/* 21. glGetProgramResourceIndex - Return GL_INVALID_INDEX. */
GLuint glGetProgramResourceIndex(GLuint program, GLenum programInterface,
                                 const GLchar* name) {
    MITHRIL_ENSURE_INIT();
    (void)program; (void)programInterface; (void)name;
    return GL_INVALID_INDEX;
}

/* 22. glGetProgramResourceiv - Return 0 for all queried properties. */
void glGetProgramResourceiv(GLuint program, GLenum programInterface,
                            GLuint index, GLsizei propCount,
                            const GLenum* props, GLsizei bufSize,
                            GLsizei* length, GLint* params) {
    MITHRIL_ENSURE_INIT();
    (void)program; (void)programInterface; (void)index; (void)props;
    if (length) *length = 0;
    if (params && bufSize > 0) {
        for (GLsizei i = 0; i < bufSize; ++i) params[i] = 0;
    }
    if (length && propCount > 0) *length = propCount;
}

/* 23. glGetProgramResourceName - Return empty string. */
void glGetProgramResourceName(GLuint program, GLenum programInterface,
                              GLuint index, GLsizei bufSize, GLsizei* length,
                              GLchar* name) {
    MITHRIL_ENSURE_INIT();
    (void)program; (void)programInterface; (void)index;
    if (length) *length = 0;
    if (name && bufSize > 0) name[0] = '\0';
}

/* 24. glGetProgramResourceLocation - Return -1. */
GLint glGetProgramResourceLocation(GLuint program, GLenum programInterface,
                                   const GLchar* name) {
    MITHRIL_ENSURE_INIT();
    (void)program; (void)programInterface; (void)name;
    return -1;
}

/* 25. glGetProgramResourceLocationIndex - Return -1. */
GLint glGetProgramResourceLocationIndex(GLuint program, GLenum programInterface,
                                        const GLchar* name) {
    MITHRIL_ENSURE_INIT();
    (void)program; (void)programInterface; (void)name;
    return -1;
}

/* 26. glGetProgramStageiv - Return 0. */
void glGetProgramStageiv(GLuint program, GLenum shadertype, GLenum pname,
                         GLint* values) {
    MITHRIL_ENSURE_INIT();
    (void)program; (void)shadertype; (void)pname;
    if (values) *values = 0;
}

/* ====================================================================
 * GL 4.5 DSA Functions (delegate to non-DSA variants)
 * ==================================================================== */

/* 27. glCreateTextures - glGenTextures + bind each to target. */
void glCreateTextures(GLenum target, GLsizei n, GLuint* textures) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !textures) return;
    glGenTextures(n, textures);
    mithril::TextureTarget tt = mithril::textureTargetFromGL(target);
    GLuint prev = 0;
    int unit = g_state->activeTextureUnit;
    if (unit >= 0 && unit < mithril::kMaxTextureUnits &&
        (int)tt < mithril::kTextureTargetCount) {
        prev = g_state->textureBindings[unit][(int)tt].name;
    }
    for (GLsizei i = 0; i < n; ++i) {
        glBindTexture(target, textures[i]);
    }
    glBindTexture(target, prev);
}

/* 28. glCreateBuffers - glGenBuffers + bind each to GL_ARRAY_BUFFER. */
void glCreateBuffers(GLsizei n, GLuint* buffers) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !buffers) return;
    glGenBuffers(n, buffers);
    GLuint prev = g_state->bufferBindings[(int)mithril::BufferTarget::Array].name;
    for (GLsizei i = 0; i < n; ++i) {
        glBindBuffer(GL_ARRAY_BUFFER, buffers[i]);
    }
    glBindBuffer(GL_ARRAY_BUFFER, prev);
}

/* 29. glCreateFramebuffers - glGenFramebuffers. */
void glCreateFramebuffers(GLsizei n, GLuint* framebuffers) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !framebuffers) return;
    glGenFramebuffers(n, framebuffers);
}

/* 30. glCreateVertexArrays - glGenVertexArrays. */
void glCreateVertexArrays(GLsizei n, GLuint* arrays) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !arrays) return;
    glGenVertexArrays(n, arrays);
}

/* 31. glCreateSamplers - glGenSamplers. */
void glCreateSamplers(GLsizei n, GLuint* samplers) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !samplers) return;
    glGenSamplers(n, samplers);
}

/* 32. glCreateProgramPipelines - Return zeros (no program pipeline support). */
void glCreateProgramPipelines(GLsizei n, GLuint* pipelines) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !pipelines) return;
    for (GLsizei i = 0; i < n; ++i) pipelines[i] = 0;
}

/* 33. glCreateQueries - Return zeros. */
void glCreateQueries(GLenum target, GLsizei n, GLuint* ids) {
    MITHRIL_ENSURE_INIT();
    (void)target;
    if (n <= 0 || !ids) return;
    for (GLsizei i = 0; i < n; ++i) ids[i] = 0;
}

/* 34. glCreateRenderbuffers - glGenRenderbuffers. */
void glCreateRenderbuffers(GLsizei n, GLuint* renderbuffers) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !renderbuffers) return;
    glGenRenderbuffers(n, renderbuffers);
}

/* 35. glCreateTransformFeedbacks - Return zeros. */
void glCreateTransformFeedbacks(GLsizei n, GLuint* ids) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !ids) return;
    for (GLsizei i = 0; i < n; ++i) ids[i] = 0;
}

/* 36. glNamedBufferStorage - Bind to GL_ARRAY_BUFFER, glBufferStorage, restore. */
void glNamedBufferStorage(GLuint buffer, GLsizeiptr size, const void* data,
                          GLbitfield flags) {
    MITHRIL_ENSURE_INIT();
    GLuint prev = g_state->bufferBindings[(int)mithril::BufferTarget::Array].name;
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferStorage(GL_ARRAY_BUFFER, size, data, flags);
    glBindBuffer(GL_ARRAY_BUFFER, prev);
}

/* 37. glNamedBufferData - Bind, glBufferData, restore. */
void glNamedBufferData(GLuint buffer, GLsizeiptr size, const void* data,
                       GLenum usage) {
    MITHRIL_ENSURE_INIT();
    GLuint prev = g_state->bufferBindings[(int)mithril::BufferTarget::Array].name;
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, size, data, usage);
    glBindBuffer(GL_ARRAY_BUFFER, prev);
}

/* 38. glNamedBufferSubData - Bind, glBufferSubData, restore. */
void glNamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size,
                          const void* data) {
    MITHRIL_ENSURE_INIT();
    GLuint prev = g_state->bufferBindings[(int)mithril::BufferTarget::Array].name;
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferSubData(GL_ARRAY_BUFFER, offset, size, data);
    glBindBuffer(GL_ARRAY_BUFFER, prev);
}

/* 39. glTextureStorage2D - Bind to GL_TEXTURE_2D, glTexStorage2D, restore. */
void glTextureStorage2D(GLuint texture, GLsizei levels, GLenum internalformat,
                        GLsizei width, GLsizei height) {
    MITHRIL_ENSURE_INIT();
    int unit = g_state->activeTextureUnit;
    GLuint prev = 0;
    if (unit >= 0 && unit < mithril::kMaxTextureUnits) {
        prev = g_state->textureBindings[unit][(int)mithril::TextureTarget::_2D].name;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexStorage2D(GL_TEXTURE_2D, levels, internalformat, width, height);
    glBindTexture(GL_TEXTURE_2D, prev);
}

/* 40. glTextureStorage3D - Bind to GL_TEXTURE_3D, glTexStorage3D, restore. */
void glTextureStorage3D(GLuint texture, GLsizei levels, GLenum internalformat,
                        GLsizei width, GLsizei height, GLsizei depth) {
    MITHRIL_ENSURE_INIT();
    int unit = g_state->activeTextureUnit;
    GLuint prev = 0;
    if (unit >= 0 && unit < mithril::kMaxTextureUnits) {
        prev = g_state->textureBindings[unit][(int)mithril::TextureTarget::_3D].name;
    }
    glBindTexture(GL_TEXTURE_3D, texture);
    glTexStorage3D(GL_TEXTURE_3D, levels, internalformat, width, height, depth);
    glBindTexture(GL_TEXTURE_3D, prev);
}

/* 41. glTextureStorage1D - Bind to GL_TEXTURE_1D, glTexStorage1D, restore. */
void glTextureStorage1D(GLuint texture, GLsizei levels, GLenum internalformat,
                        GLsizei width) {
    MITHRIL_ENSURE_INIT();
    int unit = g_state->activeTextureUnit;
    GLuint prev = 0;
    if (unit >= 0 && unit < mithril::kMaxTextureUnits) {
        prev = g_state->textureBindings[unit][(int)mithril::TextureTarget::_1D].name;
    }
    glBindTexture(GL_TEXTURE_1D, texture);
    glTexStorage1D(GL_TEXTURE_1D, levels, internalformat, width);
    glBindTexture(GL_TEXTURE_1D, prev);
}

/* 42. glTextureSubImage2D - Bind to GL_TEXTURE_2D, glTexSubImage2D, restore. */
void glTextureSubImage2D(GLuint texture, GLint level, GLint xoffset,
                         GLint yoffset, GLsizei width, GLsizei height,
                         GLenum format, GLenum type, const void* pixels) {
    MITHRIL_ENSURE_INIT();
    int unit = g_state->activeTextureUnit;
    GLuint prev = 0;
    if (unit >= 0 && unit < mithril::kMaxTextureUnits) {
        prev = g_state->textureBindings[unit][(int)mithril::TextureTarget::_2D].name;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexSubImage2D(GL_TEXTURE_2D, level, xoffset, yoffset, width, height,
                    format, type, pixels);
    glBindTexture(GL_TEXTURE_2D, prev);
}

/* 43. glTextureSubImage1D - No-op: 1D textures rare on MC, and glTexSubImage1D
 * is not implemented in the wrapper. */
void glTextureSubImage1D(GLuint texture, GLint level, GLint xoffset,
                         GLsizei width, GLenum format, GLenum type,
                         const void* pixels) {
    MITHRIL_ENSURE_INIT();
    (void)texture; (void)level; (void)xoffset; (void)width;
    (void)format; (void)type; (void)pixels;
}

/* 44. glTextureSubImage3D - Bind to GL_TEXTURE_3D, glTexSubImage3D, restore. */
void glTextureSubImage3D(GLuint texture, GLint level, GLint xoffset,
                         GLint yoffset, GLint zoffset, GLsizei width,
                         GLsizei height, GLsizei depth, GLenum format,
                         GLenum type, const void* pixels) {
    MITHRIL_ENSURE_INIT();
    int unit = g_state->activeTextureUnit;
    GLuint prev = 0;
    if (unit >= 0 && unit < mithril::kMaxTextureUnits) {
        prev = g_state->textureBindings[unit][(int)mithril::TextureTarget::_3D].name;
    }
    glBindTexture(GL_TEXTURE_3D, texture);
    glTexSubImage3D(GL_TEXTURE_3D, level, xoffset, yoffset, zoffset, width,
                    height, depth, format, type, pixels);
    glBindTexture(GL_TEXTURE_3D, prev);
}

/* 45. glTextureParameterf - Bind to GL_TEXTURE_2D, glTexParameterf, restore. */
void glTextureParameterf(GLuint texture, GLenum pname, GLfloat param) {
    MITHRIL_ENSURE_INIT();
    int unit = g_state->activeTextureUnit;
    GLuint prev = 0;
    if (unit >= 0 && unit < mithril::kMaxTextureUnits) {
        prev = g_state->textureBindings[unit][(int)mithril::TextureTarget::_2D].name;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameterf(GL_TEXTURE_2D, pname, param);
    glBindTexture(GL_TEXTURE_2D, prev);
}

/* 46. glTextureParameteri - Bind to GL_TEXTURE_2D, glTexParameteri, restore. */
void glTextureParameteri(GLuint texture, GLenum pname, GLint param) {
    MITHRIL_ENSURE_INIT();
    int unit = g_state->activeTextureUnit;
    GLuint prev = 0;
    if (unit >= 0 && unit < mithril::kMaxTextureUnits) {
        prev = g_state->textureBindings[unit][(int)mithril::TextureTarget::_2D].name;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, pname, param);
    glBindTexture(GL_TEXTURE_2D, prev);
}

/* 47. glTextureParameterfv - Bind to GL_TEXTURE_2D, glTexParameterfv, restore. */
void glTextureParameterfv(GLuint texture, GLenum pname, const GLfloat* params) {
    MITHRIL_ENSURE_INIT();
    int unit = g_state->activeTextureUnit;
    GLuint prev = 0;
    if (unit >= 0 && unit < mithril::kMaxTextureUnits) {
        prev = g_state->textureBindings[unit][(int)mithril::TextureTarget::_2D].name;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameterfv(GL_TEXTURE_2D, pname, params);
    glBindTexture(GL_TEXTURE_2D, prev);
}

/* 48. glTextureParameteriv - Bind to GL_TEXTURE_2D, glTexParameteriv, restore. */
void glTextureParameteriv(GLuint texture, GLenum pname, const GLint* params) {
    MITHRIL_ENSURE_INIT();
    int unit = g_state->activeTextureUnit;
    GLuint prev = 0;
    if (unit >= 0 && unit < mithril::kMaxTextureUnits) {
        prev = g_state->textureBindings[unit][(int)mithril::TextureTarget::_2D].name;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteriv(GL_TEXTURE_2D, pname, params);
    glBindTexture(GL_TEXTURE_2D, prev);
}

/* 49. glGenerateTextureMipmap - Bind to GL_TEXTURE_2D, glGenerateMipmap, restore. */
void glGenerateTextureMipmap(GLuint texture) {
    MITHRIL_ENSURE_INIT();
    int unit = g_state->activeTextureUnit;
    GLuint prev = 0;
    if (unit >= 0 && unit < mithril::kMaxTextureUnits) {
        prev = g_state->textureBindings[unit][(int)mithril::TextureTarget::_2D].name;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, prev);
}

/* 50. glBindTextureUnit - glActiveTexture + glBindTexture. */
void glBindTextureUnit(GLuint unit, GLuint texture) {
    MITHRIL_ENSURE_INIT();
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, texture);
}

/* 51. glNamedFramebufferTexture - Bind FBO, glFramebufferTexture, restore. */
void glNamedFramebufferTexture(GLuint framebuffer, GLenum attachment,
                               GLuint texture, GLint level) {
    MITHRIL_ENSURE_INIT();
    GLuint prevDraw = g_state->currentDrawFBO;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glFramebufferTexture(GL_DRAW_FRAMEBUFFER, attachment, texture, level);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDraw);
}

/* 52. glNamedFramebufferTextureLayer - Bind FBO, glFramebufferTextureLayer, restore. */
void glNamedFramebufferTextureLayer(GLuint framebuffer, GLenum attachment,
                                    GLuint texture, GLint level, GLint layer) {
    MITHRIL_ENSURE_INIT();
    GLuint prevDraw = g_state->currentDrawFBO;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, attachment, texture, level, layer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDraw);
}

/* 53. glNamedFramebufferDrawBuffer - Bind FBO, glDrawBuffer, restore. */
void glNamedFramebufferDrawBuffer(GLuint framebuffer, GLenum buf) {
    MITHRIL_ENSURE_INIT();
    GLuint prevDraw = g_state->currentDrawFBO;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glDrawBuffer(buf);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDraw);
}

/* 54. glNamedFramebufferDrawBuffers - Bind FBO, glDrawBuffers, restore. */
void glNamedFramebufferDrawBuffers(GLuint framebuffer, GLsizei n,
                                   const GLenum* bufs) {
    MITHRIL_ENSURE_INIT();
    GLuint prevDraw = g_state->currentDrawFBO;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glDrawBuffers(n, bufs);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDraw);
}

/* 55. glNamedFramebufferReadBuffer - Bind FBO, glReadBuffer, restore. */
void glNamedFramebufferReadBuffer(GLuint framebuffer, GLenum src) {
    MITHRIL_ENSURE_INIT();
    GLuint prevRead = g_state->currentReadFBO;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    glReadBuffer(src);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevRead);
}

/* 56. glNamedFramebufferRenderbuffer - Bind FBO, glFramebufferRenderbuffer, restore. */
void glNamedFramebufferRenderbuffer(GLuint framebuffer, GLenum attachment,
                                    GLenum renderbuffertarget, GLuint renderbuffer) {
    MITHRIL_ENSURE_INIT();
    GLuint prevDraw = g_state->currentDrawFBO;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, attachment,
                              renderbuffertarget, renderbuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDraw);
}

/* 57. glCheckNamedFramebufferStatus - Bind FBO, glCheckFramebufferStatus, restore. */
GLenum glCheckNamedFramebufferStatus(GLuint framebuffer, GLenum target) {
    MITHRIL_ENSURE_INIT();
    GLuint prevDraw = g_state->currentDrawFBO;
    GLuint prevRead = g_state->currentReadFBO;
    /* target is GL_DRAW_FRAMEBUFFER or GL_READ_FRAMEBUFFER; bind accordingly. */
    if (target == GL_READ_FRAMEBUFFER) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    } else {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    }
    GLenum status = glCheckFramebufferStatus(target);
    if (target == GL_READ_FRAMEBUFFER) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, prevRead);
    } else {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDraw);
    }
    return status;
}

/* 58. glGetNamedFramebufferAttachmentParameteriv - Bind, query, restore. */
void glGetNamedFramebufferAttachmentParameteriv(GLuint framebuffer,
                                                GLenum attachment, GLenum pname,
                                                GLint* params) {
    MITHRIL_ENSURE_INIT();
    GLuint prevDraw = g_state->currentDrawFBO;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, attachment,
                                          pname, params);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDraw);
}

/* 59. glGetNamedFramebufferParameteriv - Return 0. */
void glGetNamedFramebufferParameteriv(GLuint framebuffer, GLenum pname,
                                      GLint* params) {
    MITHRIL_ENSURE_INIT();
    (void)framebuffer; (void)pname;
    if (params) *params = 0;
}

/* 60. glClearNamedFramebufferiv - Bind FBO, glClearBufferiv, restore. */
void glClearNamedFramebufferiv(GLuint framebuffer, GLenum buffer,
                               GLint drawbuffer, const GLint* value) {
    MITHRIL_ENSURE_INIT();
    GLuint prevDraw = g_state->currentDrawFBO;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glClearBufferiv(buffer, drawbuffer, value);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDraw);
}

/* 61. glClearNamedFramebufferuiv - Bind FBO, glClearBufferuiv, restore. */
void glClearNamedFramebufferuiv(GLuint framebuffer, GLenum buffer,
                                GLint drawbuffer, const GLuint* value) {
    MITHRIL_ENSURE_INIT();
    GLuint prevDraw = g_state->currentDrawFBO;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glClearBufferuiv(buffer, drawbuffer, value);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDraw);
}

/* 62. glClearNamedFramebufferfv - Bind FBO, glClearBufferfv, restore. */
void glClearNamedFramebufferfv(GLuint framebuffer, GLenum buffer,
                               GLint drawbuffer, const GLfloat* value) {
    MITHRIL_ENSURE_INIT();
    GLuint prevDraw = g_state->currentDrawFBO;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glClearBufferfv(buffer, drawbuffer, value);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDraw);
}

/* 63. glClearNamedFramebufferfi - Bind FBO, glClearBufferfi, restore. */
void glClearNamedFramebufferfi(GLuint framebuffer, GLenum buffer,
                               GLfloat depth, GLint stencil) {
    MITHRIL_ENSURE_INIT();
    GLuint prevDraw = g_state->currentDrawFBO;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glClearBufferfi(buffer, 0, depth, stencil);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDraw);
}

/* 64. glBlitNamedFramebuffer - Bind read+draw, glBlitFramebuffer, restore. */
void glBlitNamedFramebuffer(GLuint readFramebuffer, GLuint drawFramebuffer,
                            GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                            GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                            GLbitfield mask, GLenum filter) {
    MITHRIL_ENSURE_INIT();
    GLuint prevRead = g_state->currentReadFBO;
    GLuint prevDraw = g_state->currentDrawFBO;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFramebuffer);
    glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1,
                      mask, filter);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevRead);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDraw);
}

/* 65. glCopyNamedBufferSubData - Bind read+write, glCopyBufferSubData, restore. */
void glCopyNamedBufferSubData(GLuint readBuffer, GLuint writeBuffer,
                              GLintptr readOffset, GLintptr writeOffset,
                              GLsizeiptr size) {
    MITHRIL_ENSURE_INIT();
    GLuint prevRead = g_state->bufferBindings[(int)mithril::BufferTarget::CopyRead].name;
    GLuint prevWrite = g_state->bufferBindings[(int)mithril::BufferTarget::CopyWrite].name;
    glBindBuffer(GL_COPY_READ_BUFFER, readBuffer);
    glBindBuffer(GL_COPY_WRITE_BUFFER, writeBuffer);
    glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER,
                        readOffset, writeOffset, size);
    glBindBuffer(GL_COPY_READ_BUFFER, prevRead);
    glBindBuffer(GL_COPY_WRITE_BUFFER, prevWrite);
}

/* 66. glClearNamedBufferData - Bind, glClearBufferData, restore. */
void glClearNamedBufferData(GLuint buffer, GLenum internalformat,
                            GLenum format, GLenum type, const void* data) {
    MITHRIL_ENSURE_INIT();
    GLuint prev = g_state->bufferBindings[(int)mithril::BufferTarget::Array].name;
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glClearBufferData(GL_ARRAY_BUFFER, internalformat, format, type, data);
    glBindBuffer(GL_ARRAY_BUFFER, prev);
}

/* 67. glClearNamedBufferSubData - Bind, glClearBufferSubData, restore. */
void glClearNamedBufferSubData(GLuint buffer, GLenum internalformat,
                               GLintptr offset, GLsizeiptr size, GLenum format,
                               GLenum type, const void* data) {
    MITHRIL_ENSURE_INIT();
    GLuint prev = g_state->bufferBindings[(int)mithril::BufferTarget::Array].name;
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glClearBufferSubData(GL_ARRAY_BUFFER, internalformat, offset, size,
                         format, type, data);
    glBindBuffer(GL_ARRAY_BUFFER, prev);
}

/* 68. glMapNamedBuffer - Bind, glMapBuffer, restore. Return result. */
void* glMapNamedBuffer(GLuint buffer, GLenum access) {
    MITHRIL_ENSURE_INIT();
    GLuint prev = g_state->bufferBindings[(int)mithril::BufferTarget::Array].name;
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    void* result = glMapBuffer(GL_ARRAY_BUFFER, access);
    glBindBuffer(GL_ARRAY_BUFFER, prev);
    return result;
}

/* 69. glMapNamedBufferRange - Bind, glMapBufferRange, restore. Return result. */
void* glMapNamedBufferRange(GLuint buffer, GLintptr offset, GLsizeiptr length,
                            GLbitfield access) {
    MITHRIL_ENSURE_INIT();
    GLuint prev = g_state->bufferBindings[(int)mithril::BufferTarget::Array].name;
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    void* result = glMapBufferRange(GL_ARRAY_BUFFER, offset, length, access);
    glBindBuffer(GL_ARRAY_BUFFER, prev);
    return result;
}

/* 70. glUnmapNamedBuffer - Bind, glUnmapBuffer, restore. Return result. */
GLboolean glUnmapNamedBuffer(GLuint buffer) {
    MITHRIL_ENSURE_INIT();
    GLuint prev = g_state->bufferBindings[(int)mithril::BufferTarget::Array].name;
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    GLboolean result = glUnmapBuffer(GL_ARRAY_BUFFER);
    glBindBuffer(GL_ARRAY_BUFFER, prev);
    return result;
}

/* 71. glFlushMappedNamedBufferRange - Bind, flush, restore. */
void glFlushMappedNamedBufferRange(GLuint buffer, GLintptr offset,
                                   GLsizeiptr length) {
    MITHRIL_ENSURE_INIT();
    GLuint prev = g_state->bufferBindings[(int)mithril::BufferTarget::Array].name;
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glFlushMappedBufferRange(GL_ARRAY_BUFFER, offset, length);
    glBindBuffer(GL_ARRAY_BUFFER, prev);
}

/* 72. glGetNamedBufferParameteriv - Bind, query, restore. */
void glGetNamedBufferParameteriv(GLuint buffer, GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    GLuint prev = g_state->bufferBindings[(int)mithril::BufferTarget::Array].name;
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glGetBufferParameteriv(GL_ARRAY_BUFFER, pname, params);
    glBindBuffer(GL_ARRAY_BUFFER, prev);
}

/* 73. glGetNamedBufferParameteri64v - Bind, query via glGetBufferParameteriv,
 * cast to GLint64, restore. glGetBufferParameteri64v is not implemented. */
void glGetNamedBufferParameteri64v(GLuint buffer, GLenum pname, GLint64* params) {
    MITHRIL_ENSURE_INIT();
    GLuint prev = g_state->bufferBindings[(int)mithril::BufferTarget::Array].name;
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    GLint iv = 0;
    glGetBufferParameteriv(GL_ARRAY_BUFFER, pname, &iv);
    if (params) *params = (GLint64)iv;
    glBindBuffer(GL_ARRAY_BUFFER, prev);
}

/* 74. glGetNamedBufferPointerv - Return nullptr (no mapped pointer tracking
 * via this API; glGetBufferPointerv is not implemented). */
void glGetNamedBufferPointerv(GLuint buffer, GLenum pname, void** params) {
    MITHRIL_ENSURE_INIT();
    (void)buffer; (void)pname;
    if (params) *params = nullptr;
}

/* 75. glGetNamedBufferSubData - Bind, query, restore. */
void glGetNamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size,
                             void* data) {
    MITHRIL_ENSURE_INIT();
    GLuint prev = g_state->bufferBindings[(int)mithril::BufferTarget::Array].name;
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glGetBufferSubData(GL_ARRAY_BUFFER, offset, size, data);
    glBindBuffer(GL_ARRAY_BUFFER, prev);
}

/* 76. glGetTextureImage - Bind to GL_TEXTURE_2D, glGetTexImage, restore. */
void glGetTextureImage(GLuint texture, GLint level, GLenum format, GLenum type,
                       GLsizei bufSize, void* pixels) {
    MITHRIL_ENSURE_INIT();
    (void)bufSize;
    int unit = g_state->activeTextureUnit;
    GLuint prev = 0;
    if (unit >= 0 && unit < mithril::kMaxTextureUnits) {
        prev = g_state->textureBindings[unit][(int)mithril::TextureTarget::_2D].name;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexImage(GL_TEXTURE_2D, level, format, type, pixels);
    glBindTexture(GL_TEXTURE_2D, prev);
}

/* 77. glGetTextureLevelParameteriv - Bind, query, restore. */
void glGetTextureLevelParameteriv(GLuint texture, GLint level, GLenum pname,
                                  GLint* params) {
    MITHRIL_ENSURE_INIT();
    int unit = g_state->activeTextureUnit;
    GLuint prev = 0;
    if (unit >= 0 && unit < mithril::kMaxTextureUnits) {
        prev = g_state->textureBindings[unit][(int)mithril::TextureTarget::_2D].name;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, level, pname, params);
    glBindTexture(GL_TEXTURE_2D, prev);
}

/* 78. glGetTextureLevelParameterfv - Bind, query, restore. */
void glGetTextureLevelParameterfv(GLuint texture, GLint level, GLenum pname,
                                  GLfloat* params) {
    MITHRIL_ENSURE_INIT();
    int unit = g_state->activeTextureUnit;
    GLuint prev = 0;
    if (unit >= 0 && unit < mithril::kMaxTextureUnits) {
        prev = g_state->textureBindings[unit][(int)mithril::TextureTarget::_2D].name;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexLevelParameterfv(GL_TEXTURE_2D, level, pname, params);
    glBindTexture(GL_TEXTURE_2D, prev);
}

/* 79. glGetTextureParameteriv - Bind, query, restore. */
void glGetTextureParameteriv(GLuint texture, GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    int unit = g_state->activeTextureUnit;
    GLuint prev = 0;
    if (unit >= 0 && unit < mithril::kMaxTextureUnits) {
        prev = g_state->textureBindings[unit][(int)mithril::TextureTarget::_2D].name;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexParameteriv(GL_TEXTURE_2D, pname, params);
    glBindTexture(GL_TEXTURE_2D, prev);
}

/* 80. glGetTextureParameterfv - Bind, query, restore. */
void glGetTextureParameterfv(GLuint texture, GLenum pname, GLfloat* params) {
    MITHRIL_ENSURE_INIT();
    int unit = g_state->activeTextureUnit;
    GLuint prev = 0;
    if (unit >= 0 && unit < mithril::kMaxTextureUnits) {
        prev = g_state->textureBindings[unit][(int)mithril::TextureTarget::_2D].name;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexParameterfv(GL_TEXTURE_2D, pname, params);
    glBindTexture(GL_TEXTURE_2D, prev);
}

/* 81. glGetCompressedTextureImage - Bind, glGetCompressedTexImage, restore. */
void glGetCompressedTextureImage(GLuint texture, GLint level, GLsizei bufSize,
                                 void* pixels) {
    MITHRIL_ENSURE_INIT();
    (void)bufSize;
    int unit = g_state->activeTextureUnit;
    GLuint prev = 0;
    if (unit >= 0 && unit < mithril::kMaxTextureUnits) {
        prev = g_state->textureBindings[unit][(int)mithril::TextureTarget::_2D].name;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetCompressedTexImage(GL_TEXTURE_2D, level, pixels);
    glBindTexture(GL_TEXTURE_2D, prev);
}

/* 82. glGetTextureSubImage - ALREADY IMPLEMENTED in Texture.cpp */
/* 83. glTextureBarrier - ALREADY IMPLEMENTED in Drawing.cpp */

/* 84. glVertexArrayVertexBuffer - Bind VAO, glBindVertexBuffer, restore. */
void glVertexArrayVertexBuffer(GLuint vaobj, GLuint bindingindex, GLuint buffer,
                               GLintptr offset, GLsizei stride) {
    MITHRIL_ENSURE_INIT();
    GLuint prev = g_state->currentVAO;
    glBindVertexArray(vaobj);
    glBindVertexBuffer(bindingindex, buffer, offset, stride);
    glBindVertexArray(prev);
}

/* 85. glVertexArrayVertexBuffers - Bind VAO, loop glBindVertexBuffer, restore. */
void glVertexArrayVertexBuffers(GLuint vaobj, GLuint first, GLsizei count,
                                const GLuint* buffers, const GLintptr* offsets,
                                const GLsizei* strides) {
    MITHRIL_ENSURE_INIT();
    if (count <= 0) return;
    GLuint prev = g_state->currentVAO;
    glBindVertexArray(vaobj);
    for (GLsizei i = 0; i < count; ++i) {
        glBindVertexBuffer(first + i, buffers ? buffers[i] : 0,
                           offsets ? offsets[i] : 0,
                           strides ? strides[i] : 0);
    }
    glBindVertexArray(prev);
}

/* 86. glVertexArrayElementBuffer - Bind VAO, set element buffer, restore. */
void glVertexArrayElementBuffer(GLuint vaobj, GLuint buffer) {
    MITHRIL_ENSURE_INIT();
    GLuint prev = g_state->currentVAO;
    glBindVertexArray(vaobj);
    /* Element array buffer binding lives in the VAO state. */
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer);
    glBindVertexArray(prev);
}

/* 87. glVertexArrayAttribBinding - Bind VAO, glVertexAttribBinding, restore. */
void glVertexArrayAttribBinding(GLuint vaobj, GLuint attribindex,
                                GLuint bindingindex) {
    MITHRIL_ENSURE_INIT();
    GLuint prev = g_state->currentVAO;
    glBindVertexArray(vaobj);
    glVertexAttribBinding(attribindex, bindingindex);
    glBindVertexArray(prev);
}

/* 88. glVertexArrayAttribFormat - Bind VAO, glVertexAttribFormat, restore. */
void glVertexArrayAttribFormat(GLuint vaobj, GLuint attribindex, GLint size,
                               GLenum type, GLboolean normalized,
                               GLuint relativeoffset) {
    MITHRIL_ENSURE_INIT();
    GLuint prev = g_state->currentVAO;
    glBindVertexArray(vaobj);
    glVertexAttribFormat(attribindex, size, type, normalized, relativeoffset);
    glBindVertexArray(prev);
}

/* 89. glVertexArrayAttribIFormat - Bind VAO, glVertexAttribIFormat, restore. */
void glVertexArrayAttribIFormat(GLuint vaobj, GLuint attribindex, GLint size,
                                GLenum type, GLuint relativeoffset) {
    MITHRIL_ENSURE_INIT();
    GLuint prev = g_state->currentVAO;
    glBindVertexArray(vaobj);
    glVertexAttribIFormat(attribindex, size, type, relativeoffset);
    glBindVertexArray(prev);
}

/* 90. glVertexArrayAttribLFormat - Bind VAO, glVertexAttribLFormat, restore. */
void glVertexArrayAttribLFormat(GLuint vaobj, GLuint attribindex, GLint size,
                                GLenum type, GLuint relativeoffset) {
    MITHRIL_ENSURE_INIT();
    GLuint prev = g_state->currentVAO;
    glBindVertexArray(vaobj);
    glVertexAttribLFormat(attribindex, size, type, relativeoffset);
    glBindVertexArray(prev);
}

/* 91. glVertexArrayBindingDivisor - Bind VAO, glVertexBindingDivisor, restore. */
void glVertexArrayBindingDivisor(GLuint vaobj, GLuint bindingindex,
                                 GLuint divisor) {
    MITHRIL_ENSURE_INIT();
    GLuint prev = g_state->currentVAO;
    glBindVertexArray(vaobj);
    glVertexBindingDivisor(bindingindex, divisor);
    glBindVertexArray(prev);
}

/* 92. glEnableVertexArrayAttrib - Bind VAO, glEnableVertexAttribArray, restore. */
void glEnableVertexArrayAttrib(GLuint vaobj, GLuint index) {
    MITHRIL_ENSURE_INIT();
    GLuint prev = g_state->currentVAO;
    glBindVertexArray(vaobj);
    glEnableVertexAttribArray(index);
    glBindVertexArray(prev);
}

/* 93. glDisableVertexArrayAttrib - Bind VAO, glDisableVertexAttribArray, restore. */
void glDisableVertexArrayAttrib(GLuint vaobj, GLuint index) {
    MITHRIL_ENSURE_INIT();
    GLuint prev = g_state->currentVAO;
    glBindVertexArray(vaobj);
    glDisableVertexAttribArray(index);
    glBindVertexArray(prev);
}

/* 94. glGetVertexArrayiv - Return 0. */
void glGetVertexArrayiv(GLuint vaobj, GLenum pname, GLint* param) {
    MITHRIL_ENSURE_INIT();
    (void)vaobj; (void)pname;
    if (param) *param = 0;
}

/* 95. glGetVertexArrayIndexediv - Return 0. */
void glGetVertexArrayIndexediv(GLuint vaobj, GLuint index, GLenum pname,
                               GLint* param) {
    MITHRIL_ENSURE_INIT();
    (void)vaobj; (void)index; (void)pname;
    if (param) *param = 0;
}

/* 96. glGetVertexArrayIndexed64iv - Return 0. */
void glGetVertexArrayIndexed64iv(GLuint vaobj, GLuint index, GLenum pname,
                                 GLint64* param) {
    MITHRIL_ENSURE_INIT();
    (void)vaobj; (void)index; (void)pname;
    if (param) *param = 0;
}

/* 97. glNamedRenderbufferStorage - Bind, glRenderbufferStorage, restore. */
void glNamedRenderbufferStorage(GLuint renderbuffer, GLenum internalformat,
                                GLsizei width, GLsizei height) {
    MITHRIL_ENSURE_INIT();
    GLuint prev = g_state->currentRenderbuffer;
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, internalformat, width, height);
    glBindRenderbuffer(GL_RENDERBUFFER, prev);
}

/* 98. glNamedRenderbufferStorageMultisample - Bind, glRenderbufferStorageMultisample, restore. */
void glNamedRenderbufferStorageMultisample(GLuint renderbuffer, GLsizei samples,
                                           GLenum internalformat, GLsizei width,
                                           GLsizei height) {
    MITHRIL_ENSURE_INIT();
    GLuint prev = g_state->currentRenderbuffer;
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, internalformat,
                                     width, height);
    glBindRenderbuffer(GL_RENDERBUFFER, prev);
}

/* 99. glGetNamedRenderbufferParameteriv - Bind, query, restore. */
void glGetNamedRenderbufferParameteriv(GLuint renderbuffer, GLenum pname,
                                       GLint* params) {
    MITHRIL_ENSURE_INIT();
    GLuint prev = g_state->currentRenderbuffer;
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, pname, params);
    glBindRenderbuffer(GL_RENDERBUFFER, prev);
}

/* 100. glTextureStorage2DMultisample - Bind to GL_TEXTURE_2D_MULTISAMPLE,
 * glTexImage2DMultisample (glTexStorage2DMultisample not implemented), restore. */
void glTextureStorage2DMultisample(GLuint texture, GLsizei samples,
                                   GLenum internalformat, GLsizei width,
                                   GLsizei height, GLboolean fixedsamplelocations) {
    MITHRIL_ENSURE_INIT();
    int unit = g_state->activeTextureUnit;
    GLuint prev = 0;
    if (unit >= 0 && unit < mithril::kMaxTextureUnits) {
        prev = g_state->textureBindings[unit][(int)mithril::TextureTarget::_2DMultisample].name;
    }
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, texture);
    glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, internalformat,
                            width, height, fixedsamplelocations);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, prev);
}

/* 101. glTextureStorage3DMultisample - No-op: 3D multisample textures are
 * extremely rare and glTexStorage3DMultisample is not implemented. */
void glTextureStorage3DMultisample(GLuint texture, GLsizei samples,
                                   GLenum internalformat, GLsizei width,
                                   GLsizei height, GLsizei depth,
                                   GLboolean fixedsamplelocations) {
    MITHRIL_ENSURE_INIT();
    (void)texture; (void)samples; (void)internalformat; (void)width;
    (void)height; (void)depth; (void)fixedsamplelocations;
}

/* ====================================================================
 * GL 4.4 Bind-multiple functions
 * ==================================================================== */

/* 102. glBindBuffersBase - Loop glBindBufferBase. */
void glBindBuffersBase(GLenum target, GLuint first, GLsizei count,
                       const GLuint* buffers) {
    MITHRIL_ENSURE_INIT();
    if (count <= 0) return;
    if (buffers) {
        for (GLsizei i = 0; i < count; ++i) {
            glBindBufferBase(target, first + i, buffers[i]);
        }
    } else {
        for (GLsizei i = 0; i < count; ++i) {
            glBindBufferBase(target, first + i, 0);
        }
    }
}

/* 103. glBindBuffersRange - Loop glBindBufferRange. */
void glBindBuffersRange(GLenum target, GLuint first, GLsizei count,
                        const GLuint* buffers, const GLintptr* offsets,
                        const GLsizeiptr* sizes) {
    MITHRIL_ENSURE_INIT();
    if (count <= 0 || !buffers) return;
    for (GLsizei i = 0; i < count; ++i) {
        glBindBufferRange(target, first + i, buffers[i],
                          offsets ? offsets[i] : 0,
                          sizes ? sizes[i] : 0);
    }
}

/* 104. glBindTextures - Loop: glActiveTexture + glBindTexture. */
void glBindTextures(GLuint first, GLsizei count, const GLuint* textures) {
    MITHRIL_ENSURE_INIT();
    if (count <= 0) return;
    GLint prevUnit = g_state->activeTextureUnit;
    if (textures) {
        for (GLsizei i = 0; i < count; ++i) {
            glActiveTexture(GL_TEXTURE0 + first + i);
            glBindTexture(GL_TEXTURE_2D, textures[i]);
        }
    } else {
        for (GLsizei i = 0; i < count; ++i) {
            glActiveTexture(GL_TEXTURE0 + first + i);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }
    glActiveTexture(GL_TEXTURE0 + prevUnit);
}

/* 105. glBindSamplers - Loop glBindSampler. */
void glBindSamplers(GLuint first, GLsizei count, const GLuint* samplers) {
    MITHRIL_ENSURE_INIT();
    if (count <= 0) return;
    if (samplers) {
        for (GLsizei i = 0; i < count; ++i) {
            glBindSampler(first + i, samplers[i]);
        }
    } else {
        for (GLsizei i = 0; i < count; ++i) {
            glBindSampler(first + i, 0);
        }
    }
}

/* 106. glBindImageTextures - Loop glBindImageTexture. */
void glBindImageTextures(GLuint first, GLsizei count, const GLuint* textures) {
    MITHRIL_ENSURE_INIT();
    if (count <= 0) return;
    if (textures) {
        for (GLsizei i = 0; i < count; ++i) {
            glBindImageTexture(first + i, textures[i], 0, GL_FALSE, 0,
                               GL_READ_WRITE, GL_RGBA8);
        }
    } else {
        for (GLsizei i = 0; i < count; ++i) {
            glBindImageTexture(first + i, 0, 0, GL_FALSE, 0,
                               GL_READ_ONLY, GL_RGBA8);
        }
    }
}

/* 107. glBindVertexBuffers - Loop glBindVertexBuffer. */
void glBindVertexBuffers(GLuint first, GLsizei count, const GLuint* buffers,
                         const GLintptr* offsets, const GLsizei* strides) {
    MITHRIL_ENSURE_INIT();
    if (count <= 0) return;
    for (GLsizei i = 0; i < count; ++i) {
        glBindVertexBuffer(first + i, buffers ? buffers[i] : 0,
                           offsets ? offsets[i] : 0,
                           strides ? strides[i] : 0);
    }
}

/* ====================================================================
 * GL 4.5 Transform Feedback DSA
 * ==================================================================== */

/* 108. glTransformFeedbackBufferBase - No-op (transform feedback limited on MoltenVK). */
void glTransformFeedbackBufferBase(GLuint xfb, GLuint index, GLuint buffer) {
    MITHRIL_ENSURE_INIT();
    (void)xfb; (void)index; (void)buffer;
}

/* 109. glTransformFeedbackBufferRange - No-op. */
void glTransformFeedbackBufferRange(GLuint xfb, GLuint index, GLuint buffer,
                                    GLintptr offset, GLsizeiptr size) {
    MITHRIL_ENSURE_INIT();
    (void)xfb; (void)index; (void)buffer; (void)offset; (void)size;
}

/* 110. glGetTransformFeedbackiv - Return 0. */
void glGetTransformFeedbackiv(GLuint xfb, GLenum pname, GLint* param) {
    MITHRIL_ENSURE_INIT();
    (void)xfb; (void)pname;
    if (param) *param = 0;
}

/* 111. glGetTransformFeedbacki_v - Return 0. */
void glGetTransformFeedbacki_v(GLuint xfb, GLenum pname, GLuint index,
                               GLint* param) {
    MITHRIL_ENSURE_INIT();
    (void)xfb; (void)pname; (void)index;
    if (param) *param = 0;
}

/* 112. glGetTransformFeedbacki64_v - Return 0. */
void glGetTransformFeedbacki64_v(GLuint xfb, GLenum pname, GLuint index,
                                 GLint64* param) {
    MITHRIL_ENSURE_INIT();
    (void)xfb; (void)pname; (void)index;
    if (param) *param = 0;
}

/* ====================================================================
 * GL 4.5 Query buffer
 * ==================================================================== */

/* 113. glGetQueryBufferObjecti64v - No-op. */
void glGetQueryBufferObjecti64v(GLuint id, GLuint buffer, GLenum pname,
                                GLintptr offset) {
    MITHRIL_ENSURE_INIT();
    (void)id; (void)buffer; (void)pname; (void)offset;
}

/* 114. glGetQueryBufferObjectiv - No-op. */
void glGetQueryBufferObjectiv(GLuint id, GLuint buffer, GLenum pname,
                              GLintptr offset) {
    MITHRIL_ENSURE_INIT();
    (void)id; (void)buffer; (void)pname; (void)offset;
}

/* 115. glGetQueryBufferObjectui64v - No-op. */
void glGetQueryBufferObjectui64v(GLuint id, GLuint buffer, GLenum pname,
                                 GLintptr offset) {
    MITHRIL_ENSURE_INIT();
    (void)id; (void)buffer; (void)pname; (void)offset;
}

/* 116. glGetQueryBufferObjectuiv - No-op. */
void glGetQueryBufferObjectuiv(GLuint id, GLuint buffer, GLenum pname,
                               GLintptr offset) {
    MITHRIL_ENSURE_INIT();
    (void)id; (void)buffer; (void)pname; (void)offset;
}

/* ====================================================================
 * GL 4.6
 * ==================================================================== */

/* 117. glMultiDrawArraysIndirectCount - No-op (rare; fallback to
 * glMultiDrawArraysIndirect would require parsing the drawcount buffer). */
void glMultiDrawArraysIndirectCount(GLenum mode, const void* indirect,
                                    GLintptr drawcount, GLint maxdrawcount,
                                    GLsizei stride) {
    MITHRIL_ENSURE_INIT();
    (void)mode; (void)indirect; (void)drawcount; (void)maxdrawcount; (void)stride;
}

/* 118. glMultiDrawElementsIndirectCount - No-op. */
void glMultiDrawElementsIndirectCount(GLenum mode, GLenum type,
                                      const void* indirect, GLintptr drawcount,
                                      GLint maxdrawcount, GLsizei stride) {
    MITHRIL_ENSURE_INIT();
    (void)mode; (void)type; (void)indirect; (void)drawcount;
    (void)maxdrawcount; (void)stride;
}

/* ====================================================================
 * GL 4.5 Robustness
 * ==================================================================== */

/* 119. glGetnCompressedTexImage - Delegate to glGetCompressedTexImage. */
void glGetnCompressedTexImage(GLenum target, GLint level, GLsizei bufSize,
                              void* img) {
    MITHRIL_ENSURE_INIT();
    (void)bufSize;
    glGetCompressedTexImage(target, level, img);
}

/* 120. glGetnTexImage - Delegate to glGetTexImage. */
void glGetnTexImage(GLenum target, GLint level, GLenum format, GLenum type,
                    GLsizei bufSize, void* img) {
    MITHRIL_ENSURE_INIT();
    (void)bufSize;
    glGetTexImage(target, level, format, type, img);
}

/* 121. glGetnUniformdv - Return zeros (glGetUniformdv not implemented; double
 * uniforms are extremely rare in MC/mods). */
void glGetnUniformdv(GLuint program, GLint location, GLsizei bufSize,
                     GLdouble* params) {
    MITHRIL_ENSURE_INIT();
    (void)program; (void)location; (void)bufSize;
    if (params) {
        for (GLsizei i = 0; i < bufSize; ++i) params[i] = 0.0;
    }
}

/* 122. glGetnUniformfv - Delegate to glGetUniformfv. */
void glGetnUniformfv(GLuint program, GLint location, GLsizei bufSize,
                     GLfloat* params) {
    MITHRIL_ENSURE_INIT();
    (void)bufSize;
    glGetUniformfv(program, location, params);
}

/* 123. glGetnUniformiv - Delegate to glGetUniformiv. */
void glGetnUniformiv(GLuint program, GLint location, GLsizei bufSize,
                     GLint* params) {
    MITHRIL_ENSURE_INIT();
    (void)bufSize;
    glGetUniformiv(program, location, params);
}

/* 124. glGetnUniformuiv - Return zeros (glGetUniformuiv not implemented). */
void glGetnUniformuiv(GLuint program, GLint location, GLsizei bufSize,
                      GLuint* params) {
    MITHRIL_ENSURE_INIT();
    (void)program; (void)location; (void)bufSize;
    if (params) {
        for (GLsizei i = 0; i < bufSize; ++i) params[i] = 0;
    }
}

/* 125. glGetCompressedTextureSubImage - Return zeros. */
void glGetCompressedTextureSubImage(GLuint texture, GLint level, GLint xoffset,
                                    GLint yoffset, GLint zoffset, GLsizei width,
                                    GLsizei height, GLsizei depth,
                                    GLsizei bufSize, void* pixels) {
    MITHRIL_ENSURE_INIT();
    (void)texture; (void)level; (void)xoffset; (void)yoffset; (void)zoffset;
    (void)width; (void)height; (void)depth;
    if (pixels && bufSize > 0) {
        memset(pixels, 0, (size_t)bufSize);
    }
}

} /* extern "C" */
