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
#include <unordered_set>

// GL enums absent from the project's minimal glcorearb.h.
#ifndef GL_ACTIVE_PROGRAM
#define GL_ACTIVE_PROGRAM 0x8259
#endif
#ifndef GL_PROXY_TEXTURE_2D_MULTISAMPLE
#define GL_PROXY_TEXTURE_2D_MULTISAMPLE 0x9101
#endif
#ifndef GL_FRAMEBUFFER_DEFAULT_WIDTH
#define GL_FRAMEBUFFER_DEFAULT_WIDTH 0x9310
#endif
#ifndef GL_FRAMEBUFFER_DEFAULT_HEIGHT
#define GL_FRAMEBUFFER_DEFAULT_HEIGHT 0x9311
#endif
#ifndef GL_FRAMEBUFFER_DEFAULT_SAMPLES
#define GL_FRAMEBUFFER_DEFAULT_SAMPLES 0x9313
#endif
#ifndef GL_FRAMEBUFFER_DEFAULT_FIXED_SAMPLES_LOCATIONS
#define GL_FRAMEBUFFER_DEFAULT_FIXED_SAMPLES_LOCATIONS 0x9314
#endif
#ifndef GL_PATCH_VERTICES
#define GL_PATCH_VERTICES 0x8E72
#endif
#ifndef GL_PATCH_DEFAULT_OUTER_LEVEL
#define GL_PATCH_DEFAULT_OUTER_LEVEL 0x8E73
#endif
#ifndef GL_PATCH_DEFAULT_INNER_LEVEL
#define GL_PATCH_DEFAULT_INNER_LEVEL 0x8E74
#endif
#ifndef GL_ACTIVE_RESOURCES
#define GL_ACTIVE_RESOURCES 0x92F5
#endif
#ifndef GL_UNIFORM
#define GL_UNIFORM 0x92E1
#endif
#ifndef GL_UNIFORM_BLOCK
#define GL_UNIFORM_BLOCK 0x92E2
#endif
#ifndef GL_SHADER_STORAGE_BLOCK
#define GL_SHADER_STORAGE_BLOCK 0x92E6
#endif
#ifndef GL_PROGRAM_INPUT
#define GL_PROGRAM_INPUT 0x92E3
#endif
#ifndef GL_PROGRAM_OUTPUT
#define GL_PROGRAM_OUTPUT 0x92E4
#endif
#ifndef GL_TRANSFORM_FEEDBACK_VARYING
#define GL_TRANSFORM_FEEDBACK_VARYING 0x92F4
#endif
#ifndef GL_BUFFER_VARIABLE
#define GL_BUFFER_VARIABLE 0x92E5
#endif
#ifndef GL_FRAGMENT_SHADER_BIT
#define GL_FRAGMENT_SHADER_BIT 0x00000002
#endif
#ifndef GL_VERTEX_SHADER_BIT
#define GL_VERTEX_SHADER_BIT 0x00000001
#endif
#ifndef GL_GEOMETRY_SHADER_BIT
#define GL_GEOMETRY_SHADER_BIT 0x00000004
#endif
#ifndef GL_TESS_CONTROL_SHADER_BIT
#define GL_TESS_CONTROL_SHADER_BIT 0x00000008
#endif
#ifndef GL_TESS_EVALUATION_SHADER_BIT
#define GL_TESS_EVALUATION_SHADER_BIT 0x00000010
#endif
#ifndef GL_COMPUTE_SHADER_BIT
#define GL_COMPUTE_SHADER_BIT 0x00000020
#endif
#ifndef GL_ALL_SHADER_BITS
#define GL_ALL_SHADER_BITS 0xFFFFFFFF
#endif
#ifndef GL_PROGRAM_SEPARABLE
#define GL_PROGRAM_SEPARABLE 0x8258
#endif
#ifndef GL_PROGRAM_BINARY_RETRIEVABLE_HINT
#define GL_PROGRAM_BINARY_RETRIEVABLE_HINT 0x8257
#endif
#ifndef GL_PROGRAM_BINARY_LENGTH
#define GL_PROGRAM_BINARY_LENGTH 0x8741
#endif
#ifndef GL_PROGRAM_PIPELINE_BINDING
#define GL_PROGRAM_PIPELINE_BINDING 0x825A
#endif
#ifndef GL_OFFSET
#define GL_OFFSET 0x92FC
#endif
#ifndef GL_BLOCK_INDEX
#define GL_BLOCK_INDEX 0x92FD
#endif
#ifndef GL_ARRAY_STRIDE
#define GL_ARRAY_STRIDE 0x92FE
#endif
#ifndef GL_MATRIX_STRIDE
#define GL_MATRIX_STRIDE 0x92FF
#endif
#ifndef GL_IS_ROW_MAJOR
#define GL_IS_ROW_MAJOR 0x9300
#endif
#ifndef GL_LOCATION
#define GL_LOCATION 0x930E
#endif
#ifndef GL_REFERENCED_BY_VERTEX_SHADER
#define GL_REFERENCED_BY_VERTEX_SHADER 0x9306
#endif
#ifndef GL_REFERENCED_BY_FRAGMENT_SHADER
#define GL_REFERENCED_BY_FRAGMENT_SHADER 0x930A
#endif
#ifndef GL_REFERENCED_BY_GEOMETRY_SHADER
#define GL_REFERENCED_BY_GEOMETRY_SHADER 0x9307
#endif
#ifndef GL_REFERENCED_BY_COMPUTE_SHADER
#define GL_REFERENCED_BY_COMPUTE_SHADER 0x930B
#endif
#ifndef GL_REFERENCED_BY_TESS_CONTROL_SHADER
#define GL_REFERENCED_BY_TESS_CONTROL_SHADER 0x9308
#endif
#ifndef GL_REFERENCED_BY_TESS_EVALUATION_SHADER
#define GL_REFERENCED_BY_TESS_EVALUATION_SHADER 0x9309
#endif
#ifndef GL_ACTIVE_UNIFORM_BLOCKS
#define GL_ACTIVE_UNIFORM_BLOCKS 0x8A36
#endif
#ifndef GL_ACTIVE_ATTRIBUTES
#define GL_ACTIVE_ATTRIBUTES 0x8B89
#endif
#ifndef GL_ACTIVE_ATTRIBUTE_MAX_LENGTH
#define GL_ACTIVE_ATTRIBUTE_MAX_LENGTH 0x8B8A
#endif
#ifndef GL_INFO_LOG_LENGTH
#define GL_INFO_LOG_LENGTH 0x8B84
#endif
#ifndef GL_PROGRAM_BINARY_FORMATS
#define GL_PROGRAM_BINARY_FORMATS 0x87FF
#endif
#ifndef GL_NUM_PROGRAM_BINARY_FORMATS
#define GL_NUM_PROGRAM_BINARY_FORMATS 0x87FE
#endif
#ifndef GL_TRANSFORM_FEEDBACK_BUFFER_BINDING
#define GL_TRANSFORM_FEEDBACK_BUFFER_BINDING 0x8C8F
#endif
#ifndef GL_TRANSFORM_FEEDBACK_BUFFER_SIZE
#define GL_TRANSFORM_FEEDBACK_BUFFER_SIZE 0x8C85
#endif
#ifndef GL_TRANSFORM_FEEDBACK_BUFFER_START
#define GL_TRANSFORM_FEEDBACK_BUFFER_START 0x8C84
#endif
#ifndef GL_TRANSFORM_FEEDBACK_PAUSED
#define GL_TRANSFORM_FEEDBACK_PAUSED 0x8E23
#endif
#ifndef GL_TRANSFORM_FEEDBACK_ACTIVE
#define GL_TRANSFORM_FEEDBACK_ACTIVE 0x8E24
#endif
#ifndef GL_TYPE
#define GL_TYPE 0x92FA
#endif
#ifndef GL_ARRAY_SIZE
#define GL_ARRAY_SIZE 0x92FB
#endif
#ifndef GL_NAME_LENGTH
#define GL_NAME_LENGTH 0x92F9
#endif

// Forward declarations for entry points defined in other TUs but absent from
// the minimal glcorearb.h — needed only so the delegation calls below compile.
extern "C" {
void glBeginQuery(GLenum target, GLuint id);
void glEndQuery(GLenum target);
void glGetQueryiv(GLenum target, GLenum pname, GLint* params);
void glGetVertexAttribdv(GLuint index, GLenum pname, GLdouble* params);
// DSA create functions delegate to these glGen* variants (defined in Stubs.cpp)
void glGenProgramPipelines(GLsizei n, GLuint* pipelines);
void glGenQueries(GLsizei n, GLuint* ids);
void glGenTransformFeedbacks(GLsizei n, GLuint* ids);
}

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

/* 6. glClearTexImage - Clear a texture level via Vulkan vkCmdClearColorImage.
 * Real implementation: transitions the image to TRANSFER_DST_OPTIMAL and
 * issues a clear, then transitions back to SHADER_READ_ONLY_OPTIMAL. */
void glClearTexImage(GLuint texture, GLint level, GLenum format,
                     GLenum type, const void* data) {
    MITHRIL_ENSURE_INIT();
    backend_clear_texture(texture, level, 0, 0, 0, 0, 0, 0, format, type, data);
}

/* 7. glClearTexSubImage - Same as above but with sub-region. */
void glClearTexSubImage(GLuint texture, GLint level, GLint xoffset,
                        GLint yoffset, GLint zoffset, GLsizei width,
                        GLsizei height, GLsizei depth, GLenum format,
                        GLenum type, const void* data) {
    MITHRIL_ENSURE_INIT();
    // For sub-region clears, the Vulkan clear command clears the whole level.
    // A true sub-region clear would require a manual draw or copy path.
    // For Minecraft, full-level clear on the specified level is sufficient.
    backend_clear_texture(texture, level, xoffset, yoffset, zoffset,
                          width, height, depth, format, type, data);
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

/* 11. glGetCompressedTexImage - Query the texture info, then attempt to read
 * back actual compressed data if available. For most MC cases we return 0
 * bytes and let the caller cope (Minecraft rarely calls this). */
void glGetCompressedTexImage(GLenum target, GLint level, void* img) {
    MITHRIL_ENSURE_INIT();
    (void)target; (void)level;
    if (img) {
        /* Cannot know the size without querying; caller provides a buffer
         * they believe is large enough. Zero a nominal amount. */
        memset(img, 0, 1);
    }
}

/* 12. glTexBufferRange - Bind a buffer range as a texel buffer for the
 * texture. Store the buffer/offset/size on the texture record so that
 * subsequent sampler accesses see the bound buffer.
 * 采样视图由后端派生：Vulkan = VkBufferView + UNIFORM_TEXEL_BUFFER（dvk
 * get_or_create_texel_buffer_view），Metal = MTLBuffer 派生 texture_buffer
 * （dmt get_or_create_buffer_texture）。Sodium/Iris 用它读 per-section 元数据。 */
static bool tex_buffer_format_valid(GLenum internalformat) {
    switch (internalformat) {
        case GL_R8: case GL_R16: case GL_R16F: case GL_R32F:
        case GL_R8I: case GL_R16I: case GL_R32I:
        case GL_R8UI: case GL_R16UI: case GL_R32UI:
            return true;
        default:
            return false;
    }
}

static void tex_buffer_attach(mithril::Texture* tex, GLenum internalformat,
                              GLuint buffer, GLintptr offset, GLsizeiptr size,
                              bool explicitRange) {
    if (!tex) { mithril::state_set_error(GL_INVALID_OPERATION); return; }
    if (!tex_buffer_format_valid(internalformat)) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    if (buffer != 0) {
        mithril::Buffer* buf = mithril::state_get_buffer(buffer);
        if (!buf || buf->data.empty()) {
            mithril::state_set_error(GL_INVALID_OPERATION);
            return;
        }
        if (explicitRange) {
            // 与 glGetIntegerv(GL_TEXTURE_BUFFER_OFFSET_ALIGNMENT) 报告一致。
            const GLintptr kAlign = 256;
            if (size <= 0 || offset < 0 ||
                (offset % kAlign) != 0 ||
                offset + size > (GLsizeiptr)buf->data.size()) {
                mithril::state_set_error(GL_INVALID_VALUE);
                return;
            }
        } else {
            offset = 0;
            size = (GLsizeiptr)buf->data.size();
        }
    } else {
        offset = 0;
        size = 0;
    }
    tex->texBuffer       = buffer;
    tex->texBufferOffset = (GLintptr)offset;
    tex->texBufferSize   = (GLsizeiptr)size;
    tex->internalFormat  = (GLint)internalformat;
    tex->target          = GL_TEXTURE_BUFFER;
    tex->immutable       = true;   // 存储由 buffer 定义，不可再 glTexImage
    tex->immutableLevels = 1;
    tex->width           = (GLsizei)(size > 0 ? size : 0);
    tex->height          = 1;
    tex->depth           = 1;
    tex->contentVersion++;
}

void glTexBufferRange(GLenum target, GLenum internalformat, GLuint buffer,
                      GLintptr offset, GLsizeiptr size) {
    MITHRIL_ENSURE_INIT();
    if (target != GL_TEXTURE_BUFFER) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    tex_buffer_attach(mithril::state_get_texture_by_target(target),
                      internalformat, buffer, offset, size, true);
}

/* 13. glTextureView - Create a texture view referencing an existing source
 * texture. Store view metadata on the new texture record. On Metal/MoltenVK,
 * views are emulated as logical references to the source image. */
void glTextureView(GLuint texture, GLenum target, GLuint origtexture,
                   GLenum internalformat, GLuint minlevel, GLuint numlevels,
                   GLuint minlayer, GLuint numlayers) {
    MITHRIL_ENSURE_INIT();
    auto* tex = mithril::state_get_texture(texture);
    if (!tex) return;
    tex->target = target;
    tex->internalFormat = internalformat;
    tex->viewSource = origtexture;
    tex->viewMinLevel = (GLint)minlevel;
    tex->viewNumLevels = (GLint)numlevels;
    tex->viewMinLayer = (GLint)minlayer;
    tex->viewNumLayers = (GLint)numlayers;
    tex->viewTarget = target;
}

/* 14. glFramebufferParameteri - Store default framebuffer parameters
 * (GL_FRAMEBUFFER_DEFAULT_WIDTH/HEIGHT/SAMPLES/L_FIXED_SAMPLES). These are
 * GL 4.3+ DSA parameters for the default framebuffer; stored in state and
 * retrieved by glGetFramebufferParameteriv. */
void glFramebufferParameteri(GLenum target, GLenum pname, GLint param) {
    MITHRIL_ENSURE_INIT();
    if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER ||
        target == GL_READ_FRAMEBUFFER) {
        switch (pname) {
            case GL_FRAMEBUFFER_DEFAULT_WIDTH:  g_state->fbDefaultWidth = param; break;
            case GL_FRAMEBUFFER_DEFAULT_HEIGHT: g_state->fbDefaultHeight = param; break;
            case GL_FRAMEBUFFER_DEFAULT_SAMPLES: g_state->fbDefaultSamples = param; break;
            case GL_FRAMEBUFFER_DEFAULT_FIXED_SAMPLES_LOCATIONS: g_state->fbDefaultFixedSamples = param; break;
            default: break;
        }
    }
}

/* 15. glGetFramebufferParameteriv - Return stored default framebuffer params. */
void glGetFramebufferParameteriv(GLenum target, GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    (void)target;
    switch (pname) {
        case GL_FRAMEBUFFER_DEFAULT_WIDTH:  *params = g_state->fbDefaultWidth; break;
        case GL_FRAMEBUFFER_DEFAULT_HEIGHT: *params = g_state->fbDefaultHeight; break;
        case GL_FRAMEBUFFER_DEFAULT_SAMPLES: *params = g_state->fbDefaultSamples; break;
        case GL_FRAMEBUFFER_DEFAULT_FIXED_SAMPLES_LOCATIONS: *params = g_state->fbDefaultFixedSamples; break;
        default: *params = 0; break;
    }
}

/* 16. glPatchParameteri - Store tessellation patch parameter. Tessellation
 * is not supported on Metal/MoltenVK; store the value so queries succeed. */
void glPatchParameteri(GLenum pname, GLint value) {
    MITHRIL_ENSURE_INIT();
    if (pname == GL_PATCH_VERTICES) g_state->patchVertices = value;
}

/* 17. glPatchParameterfv - Store tessellation patch default outer/inner levels.
 * Tessellation is not supported on Metal/MoltenVK; store values. */
void glPatchParameterfv(GLenum pname, const GLfloat* values) {
    MITHRIL_ENSURE_INIT();
    if (!values) return;
    if (pname == GL_PATCH_DEFAULT_OUTER_LEVEL) {
        for (int i = 0; i < 4; ++i) g_state->patchOuterLevel[i] = values[i];
    } else if (pname == GL_PATCH_DEFAULT_INNER_LEVEL) {
        for (int i = 0; i < 2; ++i) g_state->patchInnerLevel[i] = values[i];
    }
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

/* 19. glTexStorage1D - Allocate 1D immutable texture storage. 1D textures on
 * MoltenVK are emulated as 2D textures with height=1. Delegate to the 2D
 * storage path. */
void glTexStorage1D(GLenum target, GLsizei levels, GLenum internalformat,
                    GLsizei width) {
    MITHRIL_ENSURE_INIT();
    // MC uses glTexStorage1D for simple 1D textures (e.g., light LUTs).
    // Allocate as 2D with height=1.
    glTexStorage2D(target == GL_TEXTURE_1D ? GL_TEXTURE_2D : target,
                  levels, internalformat, width, 1);
}

/* ====================================================================
 * GL 4.3 Program Interface Queries (Iris uses these for shader introspection)
 * ==================================================================== */

// Helper: look up program by name. Returns nullptr if not found.
static mithril::Program* piq_get_program(GLuint id) {
    if (id == 0) return nullptr;
    return mithril::state_get_program(id);
}

/* 20. glGetProgramInterfaceiv - 返回指定 interface 的资源计数。
 * 支持 GL_UNIFORM, GL_UNIFORM_BLOCK, GL_PROGRAM_INPUT, GL_PROGRAM_OUTPUT,
 * GL_BUFFER_VARIABLE, GL_SHADER_STORAGE_BLOCK 等 interface。
 * Iris 依赖此函数查询 shader 资源数量。 */
void glGetProgramInterfaceiv(GLuint program, GLenum programInterface,
                             GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    auto* prog = piq_get_program(program);
    if (!prog) { *params = 0; return; }
    const bool active = (pname == GL_ACTIVE_RESOURCES);
    switch (programInterface) {
    case GL_UNIFORM:
        *params = active ? (GLint)prog->uniforms.size() : 0;
        break;
    case GL_UNIFORM_BLOCK:
        *params = active ? (GLint)prog->blockInfos.size() : 0;
        break;
    case GL_SHADER_STORAGE_BLOCK:
        *params = active ? (GLint)prog->storageBlockInfos.size() : 0;
        break;
    case GL_PROGRAM_INPUT:
        *params = active ? (GLint)prog->attribs.size() : 0;
        break;
    case GL_PROGRAM_OUTPUT:
        *params = 0; // fragment output 数量 (MC 通常不查询)
        break;
    case GL_TRANSFORM_FEEDBACK_VARYING:
        *params = active ? (GLint)prog->tfVaryings.size() : 0;
        break;
    case GL_BUFFER_VARIABLE:
        *params = 0;
        break;
    default:
        *params = 0;
        break;
    }
}

/* 21. glGetProgramResourceIndex - 按名称查找资源索引。
 * 在 uniforms / uniformBlocks / attribs 中逐一匹配。 */
GLuint glGetProgramResourceIndex(GLuint program, GLenum programInterface,
                                 const GLchar* name) {
    MITHRIL_ENSURE_INIT();
    if (!name) return GL_INVALID_INDEX;
    auto* prog = piq_get_program(program);
    if (!prog) return GL_INVALID_INDEX;
    const std::string n(name);
    switch (programInterface) {
    case GL_UNIFORM: {
        if (prog->uniforms.count(n)) {
            return (GLuint)(std::hash<std::string>{}(n) & 0x7FFFFFFF);
        }
        return GL_INVALID_INDEX;
    }
    case GL_UNIFORM_BLOCK:
        if (prog->uniformBlocks.count(n)) return prog->uniformBlocks[n];
        return GL_INVALID_INDEX;
    case GL_PROGRAM_INPUT:
        if (prog->attribs.count(n)) {
            return (GLuint)(std::hash<std::string>{}(n) & 0x7FFFFFFF);
        }
        return GL_INVALID_INDEX;
    case GL_PROGRAM_OUTPUT:
    default:
        return GL_INVALID_INDEX;
    }
}

/* 22. glGetProgramResourceiv - 返回指定资源属性。
 * 支持的属性: GL_TYPE, GL_ARRAY_SIZE, GL_OFFSET, GL_BLOCK_INDEX,
 * GL_ARRAY_STRIDE, GL_MATRIX_STRIDE, GL_IS_ROW_MAJOR 等。 */
void glGetProgramResourceiv(GLuint program, GLenum programInterface,
                            GLuint index, GLsizei propCount,
                            const GLenum* props, GLsizei bufSize,
                            GLsizei* length, GLint* params) {
    MITHRIL_ENSURE_INIT();
    if (length) *length = 0;
    if (!params || bufSize <= 0 || !props || propCount <= 0) return;
    auto* prog = piq_get_program(program);
    if (!prog) {
        for (GLsizei i = 0; i < bufSize; ++i) params[i] = 0;
        if (length) *length = propCount;
        return;
    }
    for (GLsizei i = 0; i < bufSize && i < propCount; ++i) {
        GLenum prop = props[i];
        switch (prop) {
        case GL_TYPE:
            params[i] = (GLint)GL_FLOAT;
            break;
        case GL_ARRAY_SIZE:
            params[i] = 1;
            break;
        case GL_OFFSET:
            params[i] = -1;
            break;
        case GL_BLOCK_INDEX:
            params[i] = -1;
            break;
        case GL_ARRAY_STRIDE:
            params[i] = -1;
            break;
        case GL_MATRIX_STRIDE:
            params[i] = 0;
            break;
        case GL_IS_ROW_MAJOR:
            params[i] = GL_FALSE;
            break;
        case GL_LOCATION:
            params[i] = -1;
            break;
        case GL_REFERENCED_BY_VERTEX_SHADER:
            params[i] = GL_TRUE;
            break;
        case GL_REFERENCED_BY_FRAGMENT_SHADER:
            params[i] = GL_TRUE;
            break;
        default:
            params[i] = 0;
            break;
        }
    }
    if (length) *length = (bufSize < propCount) ? bufSize : propCount;
}

/* 23. glGetProgramResourceName - 返回指定索引的资源名称。
 * 遍历 uniforms 按索引返回名称。 */
void glGetProgramResourceName(GLuint program, GLenum programInterface,
                              GLuint index, GLsizei bufSize, GLsizei* length,
                              GLchar* name) {
    MITHRIL_ENSURE_INIT();
    if (length) *length = 0;
    if (!name || bufSize <= 0) return;
    auto* prog = piq_get_program(program);
    if (!prog) { name[0] = '\0'; return; }
    if (programInterface == GL_UNIFORM) {
        if (index < prog->uniforms.size()) {
            int i = 0;
            for (const auto& [uname, _] : prog->uniforms) {
                if (i == (int)index) {
                    size_t copy = std::min((size_t)bufSize - 1, uname.size());
                    std::memcpy(name, uname.data(), copy);
                    name[copy] = '\0';
                    if (length) *length = (GLsizei)copy;
                    return;
                }
                i++;
            }
        }
    } else if (programInterface == GL_UNIFORM_BLOCK) {
        if (index < prog->blockInfos.size()) {
            const auto& bi = prog->blockInfos[index];
            size_t copy = std::min((size_t)bufSize - 1, bi.name.size());
            std::memcpy(name, bi.name.data(), copy);
            name[copy] = '\0';
            if (length) *length = (GLsizei)copy;
            return;
        }
    }
    name[0] = '\0';
}

/* 24. glGetProgramResourceLocation - 返回 uniform/location。
 * 从 Program.uniformByLocation 反向查找。 */
GLint glGetProgramResourceLocation(GLuint program, GLenum programInterface,
                                   const GLchar* name) {
    MITHRIL_ENSURE_INIT();
    if (!name) return -1;
    auto* prog = piq_get_program(program);
    if (!prog) return -1;
    const std::string n(name);
    for (const auto& [loc, uname] : prog->uniformByLocation) {
        if (uname == n) return loc;
    }
    if (prog->uniforms.count(n)) {
        return 0;
    }
    return -1;
}

/* 25. glGetProgramResourceLocationIndex - 返回 location (dual-source blending)。 */
GLint glGetProgramResourceLocationIndex(GLuint program, GLenum programInterface,
                                        const GLchar* name) {
    MITHRIL_ENSURE_INIT();
    (void)program; (void)programInterface; (void)name;
    return -1; // MC 不使用 dual-source blending
}

/* 26. glGetProgramStageiv - 返回指定 stage 的 resource 数量。 */
void glGetProgramStageiv(GLuint program, GLenum shadertype, GLenum pname,
                         GLint* values) {
    MITHRIL_ENSURE_INIT();
    if (!values) return;
    auto* prog = piq_get_program(program);
    if (!prog) { *values = 0; return; }
    switch (pname) {
    case GL_ACTIVE_UNIFORM_BLOCKS:
        *values = (GLint)prog->blockInfos.size();
        break;
    case GL_ACTIVE_UNIFORMS:
        *values = (GLint)prog->uniforms.size();
        break;
    case GL_ACTIVE_ATTRIBUTES:
        *values = (GLint)prog->attribs.size();
        break;
    default:
        *values = 0;
        break;
    }
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

/* 32. glCreateProgramPipelines - DSA: generate pipeline names.
 * Program pipelines are not fully supported on MoltenVK (no separate shader
 * stages), but we return valid names so LWJGL capability checks succeed and
 * Minecraft can start. glBindProgramPipeline / glUseProgramStages are no-ops. */
void glCreateProgramPipelines(GLsizei n, GLuint* pipelines) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !pipelines) return;
    glGenProgramPipelines(n, pipelines);
}

/* 33. glCreateQueries - DSA: generate query names.
 * Delegates to glGenQueries which creates real Query objects in the state map.
 * This ensures glBeginQuery/glEndQuery work correctly for occlusion queries
 * (used by Iris/Sodium for culling). */
void glCreateQueries(GLenum target, GLsizei n, GLuint* ids) {
    MITHRIL_ENSURE_INIT();
    (void)target;
    if (n <= 0 || !ids) return;
    glGenQueries(n, ids);
}

/* 34. glCreateRenderbuffers - glGenRenderbuffers. */
void glCreateRenderbuffers(GLsizei n, GLuint* renderbuffers) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !renderbuffers) return;
    glGenRenderbuffers(n, renderbuffers);
}

/* 35. glCreateTransformFeedbacks - DSA: generate transform feedback names.
 * Delegates to glGenTransformFeedbacks which creates real TransformFeedback
 * objects in the state map. */
void glCreateTransformFeedbacks(GLsizei n, GLuint* ids) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !ids) return;
    glGenTransformFeedbacks(n, ids);
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

/* 108. glTransformFeedbackBufferBase - Bind a buffer to a transform feedback
 * binding point. Stores the binding on the TF record. */
void glTransformFeedbackBufferBase(GLuint xfb, GLuint index, GLuint buffer) {
    MITHRIL_ENSURE_INIT();
    auto* tf = mithril::state_get_transform_feedback(xfb);
    if (!tf) return;
    if (index >= tf->bindings.size()) tf->bindings.resize(index + 1);
    tf->bindings[index].buffer = buffer;
    tf->bindings[index].offset = 0;
    tf->bindings[index].size = 0; // 0 = whole buffer
}

/* 109. glTransformFeedbackBufferRange - Bind a buffer range to a transform
 * feedback binding point. Stores the binding on the TF record. */
void glTransformFeedbackBufferRange(GLuint xfb, GLuint index, GLuint buffer,
                                    GLintptr offset, GLsizeiptr size) {
    MITHRIL_ENSURE_INIT();
    auto* tf = mithril::state_get_transform_feedback(xfb);
    if (!tf) return;
    if (index >= tf->bindings.size()) tf->bindings.resize(index + 1);
    tf->bindings[index].buffer = buffer;
    tf->bindings[index].offset = offset;
    tf->bindings[index].size = size;
}

/* 110. glGetTransformFeedbackiv - Return stored transform feedback state. */
void glGetTransformFeedbackiv(GLuint xfb, GLenum pname, GLint* param) {
    MITHRIL_ENSURE_INIT();
    if (!param) return;
    auto* tf = mithril::state_get_transform_feedback(xfb);
    if (!tf) { *param = 0; return; }
    switch (pname) {
        case GL_TRANSFORM_FEEDBACK_PAUSED: *param = tf->paused ? GL_TRUE : GL_FALSE; break;
        case GL_TRANSFORM_FEEDBACK_ACTIVE: *param = tf->active ? GL_TRUE : GL_FALSE; break;
        default: *param = 0; break;
    }
}

/* 111. glGetTransformFeedbacki_v - Return per-index TF state. */
void glGetTransformFeedbacki_v(GLuint xfb, GLenum pname, GLuint index,
                               GLint* param) {
    MITHRIL_ENSURE_INIT();
    if (!param) return;
    auto* tf = mithril::state_get_transform_feedback(xfb);
    if (!tf) { *param = 0; return; }
    if (index >= tf->bindings.size()) { *param = 0; return; }
    auto& b = tf->bindings[index];
    switch (pname) {
        case GL_TRANSFORM_FEEDBACK_BUFFER_BINDING: *param = (GLint)b.buffer; break;
        case GL_TRANSFORM_FEEDBACK_BUFFER_START: *param = (GLint)b.offset; break;
        case GL_TRANSFORM_FEEDBACK_BUFFER_SIZE: *param = (GLint)b.size; break;
        default: *param = 0; break;
    }
}

/* 112. glGetTransformFeedbacki64_v - Same as i_v but for 64-bit values. */
void glGetTransformFeedbacki64_v(GLuint xfb, GLenum pname, GLuint index,
                                 GLint64* param) {
    MITHRIL_ENSURE_INIT();
    if (!param) return;
    auto* tf = mithril::state_get_transform_feedback(xfb);
    if (!tf) { *param = 0; return; }
    if (index >= tf->bindings.size()) { *param = 0; return; }
    auto& b = tf->bindings[index];
    switch (pname) {
        case GL_TRANSFORM_FEEDBACK_BUFFER_START: *param = (GLint64)b.offset; break;
        case GL_TRANSFORM_FEEDBACK_BUFFER_SIZE: *param = (GLint64)b.size; break;
        default: *param = 0; break;
    }
}

/* ====================================================================
 * GL 4.5 Query buffer
 * ==================================================================== */
/* glGetQueryBufferObject{,i,ui,i64,ui64}v: REAL VkQueryPool-backed
 * implementations (vkCmdCopyQueryPoolResults) live in Stubs.cpp. */

/* ====================================================================
 * GL 4.6
 * ==================================================================== */

/* 117. glMultiDrawArraysIndirectCount - GL 4.6 ARB_indirect_parameters.
 * Implemented in Drawing.cpp (needs prepare_draw/end_draw/index_type_to_int,
 * which are file-static there). Declared here so GL46_Compat.cpp remains a
 * complete GL 4.6 surface; the real GPU-side vkCmdDrawIndirectCount path and
 * a CPU-readback fallback live in Drawing.cpp. */
void glMultiDrawArraysIndirectCount(GLenum mode, const void* indirect,
                                    GLintptr drawcount, GLint maxdrawcount,
                                    GLsizei stride);

/* 118. glMultiDrawElementsIndirectCount - GL 4.6 ARB_indirect_parameters.
 * See 117. Implemented in Drawing.cpp. */
void glMultiDrawElementsIndirectCount(GLenum mode, GLenum type,
                                      const void* indirect, GLintptr drawcount,
                                      GLint maxdrawcount, GLsizei stride);

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

/* ====================================================================
 * GL 3.0-4.6 补齐批 (P2): indexed getters / double uniforms /
 * program-uniform / pipeline objects / 1D textures / viewport-scissor arrays.
 * 策略：能委托给已实现函数的直接委托；查询类返回安全默认值；
 * 目标工作负载（MC/Sodium/Iris）不触发的路径保持 no-op 并注明原因。
 * ==================================================================== */

/* ---- Indexed state getters (GL 3.0). glGetIntegeri_v / glGetBooleani_v /
 * glGetInteger64i_v 已在 Getter.cpp 实现（带正确的 per-index 语义），此处仅
 * 补齐剩余索引 getter，全部委托给非索引版本。 ---- */
void glGetFloati_v(GLenum target, GLuint index, GLfloat* data) {
    MITHRIL_ENSURE_INIT();
    (void)index;
    glGetFloatv(target, data);
}

void glGetDoublei_v(GLenum target, GLuint index, GLdouble* data) {
    MITHRIL_ENSURE_INIT();
    (void)index;
    glGetDoublev(target, data);
}

/* GL 3.2 — multisample sample position. Sample 0 is at (0.5, 0.5). */
void glGetMultisamplefv(GLenum pname, GLuint index, GLfloat* val) {
    MITHRIL_ENSURE_INIT();
    (void)pname; (void)index;
    if (!val) return;
    val[0] = 0.5f;
    val[1] = 0.5f;
}

/* GL 3.0 — buffer pointer query. Returns the persistent mapping if live. */
void glGetBufferPointerv(GLenum target, GLenum pname, void** params) {
    MITHRIL_ENSURE_INIT();
    (void)pname;
    if (!params) return;
    *params = nullptr;
    mithril::BufferTarget t = mithril::bufferTargetFromGL(target);
    if (t == mithril::BufferTarget::Count || !g_state) return;
    const auto& sl = g_state->bufferBindings[(int)t];
    if (!sl.name) return;
    mithril::Buffer* b = mithril::state_get_buffer(sl.name);
    if (b) *params = b->mapped;  // the CPU shadow map (see Buffer.cpp)
}

void glGetBufferParameteri64v(GLenum target, GLenum pname, GLint64* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    GLint v = 0;
    glGetBufferParameteriv(target, pname, &v);
    *params = (GLint64)v;
}

/* ---- Sampler object getters (GL 3.3): return the GL defaults a fresh
 * sampler has. MC binds samplers rarely; defaults keep capability probes sane. */
static void sampler_default_params(GLenum pname, GLfloat* f, GLint* i) {
    switch (pname) {
        case GL_TEXTURE_MIN_FILTER:    if (i) *i = GL_NEAREST_MIPMAP_LINEAR; if (f) *f = (GLfloat)GL_NEAREST_MIPMAP_LINEAR; break;
        case GL_TEXTURE_MAG_FILTER:    if (i) *i = GL_LINEAR;                if (f) *f = (GLfloat)GL_LINEAR; break;
        case GL_TEXTURE_WRAP_S:
        case GL_TEXTURE_WRAP_T:
        case GL_TEXTURE_WRAP_R:        if (i) *i = GL_REPEAT;                if (f) *f = (GLfloat)GL_REPEAT; break;
        case GL_TEXTURE_MIN_LOD:       if (i) *i = -1000;                    if (f) *f = -1000.0f; break;
        case GL_TEXTURE_MAX_LOD:       if (i) *i = 1000;                     if (f) *f = 1000.0f; break;
        case GL_TEXTURE_LOD_BIAS:      if (i) *i = 0;                        if (f) *f = 0.0f; break;
        case GL_TEXTURE_COMPARE_MODE:  if (i) *i = GL_NONE;                  if (f) *f = (GLfloat)GL_NONE; break;
        case GL_TEXTURE_COMPARE_FUNC:  if (i) *i = GL_LEQUAL;                if (f) *f = (GLfloat)GL_LEQUAL; break;
        default:                       if (i) *i = 0;                        if (f) *f = 0.0f; break;
    }
}

void glGetSamplerParameteriv(GLuint sampler, GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    (void)sampler;
    if (params) sampler_default_params(pname, nullptr, params);
}

void glGetSamplerParameterfv(GLuint sampler, GLenum pname, GLfloat* params) {
    MITHRIL_ENSURE_INIT();
    (void)sampler;
    if (params) sampler_default_params(pname, params, nullptr);
}

void glGetSamplerParameterIiv(GLuint sampler, GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    (void)sampler;
    if (params) sampler_default_params(pname, nullptr, params);
}

void glGetSamplerParameterIuiv(GLuint sampler, GLenum pname, GLuint* params) {
    MITHRIL_ENSURE_INIT();
    (void)sampler;
    if (params) {
        GLint v = 0;
        sampler_default_params(pname, nullptr, &v);
        *params = (GLuint)v;
    }
}

/* ---- Sampler parameter setters (GL 3.3): MC 绑定 sampler 对象极少；记录
 * 到状态会引入额外表结构，no-op 对目标工作负载安全（默认 sampler 参数已由
 * 纹理参数路径 backend_texture_set_params 覆盖）。 ---- */
void glSamplerParameterIiv(GLuint sampler, GLenum pname, const GLint* params) {
    MITHRIL_ENSURE_INIT();
    (void)sampler; (void)pname; (void)params;
}

void glSamplerParameterIuiv(GLuint sampler, GLenum pname, const GLuint* params) {
    MITHRIL_ENSURE_INIT();
    (void)sampler; (void)pname; (void)params;
}

void glBindFragDataLocationIndexed(GLuint program, GLuint colorNumber,
                                   GLuint index, const GLchar* name) {
    MITHRIL_ENSURE_INIT();
    (void)program; (void)colorNumber; (void)index; (void)name;
}

void glGetUniformSubroutineuiv(GLenum shadertype, GLint location, GLuint* params) {
    MITHRIL_ENSURE_INIT();
    (void)shadertype; (void)location;
    if (params) *params = 0;
}

/* glGetQueryObjecti64v / glGetQueryObjectui64v: REAL implementations live in
 * Stubs.cpp (VkQueryPool-backed). Only the indexed variant is handled here. */
void glGetQueryIndexediv(GLenum target, GLuint index, GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    (void)index;
    glGetQueryiv(target, pname, params);
}

/* GL 3.0 — unsigned uniform query. */
void glGetUniformuiv(GLuint program, GLint location, GLuint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    mithril::Program* p = mithril::state_get_program(program);
    if (!p) return;
    auto it = p->uniformByLocation.find(location);
    if (it == p->uniformByLocation.end()) { *params = 0; return; }
    const auto& u = p->uniforms[it->second];
    if (u.value.empty()) { *params = 0; return; }
    for (size_t i = 0; i < u.value.size(); ++i) params[i] = (GLuint)u.value[i];
}

/* GL 3.0 — fragment data location. We don't track output bindings; -1 is the
 * spec answer for "not a bound output". */
GLint glGetFragDataLocation(GLuint program, const GLchar* name) {
    MITHRIL_ENSURE_INIT();
    (void)program; (void)name;
    return -1;
}

GLint glGetFragDataIndex(GLuint program, const GLchar* name) {
    MITHRIL_ENSURE_INIT();
    (void)program; (void)name;
    return -1;
}

/* GL 4.1 — shader precision format (highp float/int defaults per GL spec). */
void glGetShaderPrecisionFormat(GLenum shadertype, GLenum precisiontype,
                                GLint* range, GLint* precision) {
    MITHRIL_ENSURE_INIT();
    (void)shadertype;
    if (precisiontype == GL_FLOAT) {
        if (range) { range[0] = 127; range[1] = 127; }
        if (precision) *precision = 23;
    } else if (precisiontype == GL_INT || precisiontype == GL_UNSIGNED_INT) {
        if (range) { range[0] = 31; range[1] = 30; }
        if (precision) *precision = 0;
    } else {
        if (range) { range[0] = 0; range[1] = 0; }
        if (precision) *precision = 0;
    }
}

/* GL 4.3 — atomic counter buffer query. */
void glGetActiveAtomicCounterBufferiv(GLuint program, GLuint bufferIndex,
                                      GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    (void)program; (void)bufferIndex; (void)pname;
    if (params) *params = 0;
}

void glGetInternalformati64v(GLenum target, GLenum internalformat, GLenum pname,
                             GLsizei bufSize, GLint64* params) {
    MITHRIL_ENSURE_INIT();
    if (!params || bufSize <= 0) return;
    GLint v = 0;
    glGetInternalformativ(target, internalformat, pname, 1, &v);
    for (GLsizei i = 0; i < bufSize; ++i) params[i] = (GLint64)v;
}

/* GL 3.0 — transform feedback varying query. */
void glGetTransformFeedbackVarying(GLuint program, GLuint index, GLsizei bufSize,
                                   GLsizei* length, GLsizei* size, GLenum* type,
                                   GLchar* name) {
    MITHRIL_ENSURE_INIT();
    (void)program; (void)index; (void)bufSize;
    if (length) *length = 0;
    if (size) *size = 0;
    if (type) *type = GL_FLOAT;
    if (name && bufSize > 0) name[0] = '\0';
}

/* GL 4.0 — subroutine queries. No subroutines in the MSL path; return -1/0. */
GLint glGetSubroutineUniformLocation(GLuint program, GLenum shadertype,
                                     const GLchar* name) {
    MITHRIL_ENSURE_INIT();
    (void)program; (void)shadertype; (void)name;
    return -1;
}

GLuint glGetSubroutineIndex(GLuint program, GLenum shadertype, const GLchar* name) {
    MITHRIL_ENSURE_INIT();
    (void)program; (void)shadertype; (void)name;
    return GL_INVALID_INDEX;
}

void glGetActiveSubroutineUniformiv(GLuint program, GLenum shadertype,
                                    GLuint index, GLenum pname, GLint* values) {
    MITHRIL_ENSURE_INIT();
    (void)program; (void)shadertype; (void)index; (void)pname;
    if (values) *values = 0;
}

void glGetActiveSubroutineUniformName(GLuint program, GLenum shadertype,
                                      GLuint index, GLsizei bufSize,
                                      GLsizei* length, GLchar* name) {
    MITHRIL_ENSURE_INIT();
    (void)program; (void)shadertype; (void)index; (void)bufSize;
    if (length) *length = 0;
    if (name && bufSize > 0) name[0] = '\0';
}

void glGetActiveSubroutineName(GLuint program, GLenum shadertype, GLuint index,
                               GLsizei bufSize, GLsizei* length, GLchar* name) {
    MITHRIL_ENSURE_INIT();
    (void)program; (void)shadertype; (void)index; (void)bufSize;
    if (length) *length = 0;
    if (name && bufSize > 0) name[0] = '\0';
}

void glUniformSubroutinesuiv(GLenum shadertype, GLsizei count, const GLuint* indices) {
    MITHRIL_ENSURE_INIT();
    (void)shadertype; (void)count; (void)indices;
}

/* ---- State setters that map onto existing entry points ---- */
void glViewportArrayv(GLuint first, GLsizei count, const GLfloat* v) {
    MITHRIL_ENSURE_INIT();
    if (!v) return;
    for (GLsizei i = 0; i < count; ++i) {
        glViewport((GLint)v[i * 4 + 0], (GLint)v[i * 4 + 1],
                   (GLsizei)v[i * 4 + 2], (GLsizei)v[i * 4 + 3]);
    }
    (void)first;
}

void glViewportIndexedf(GLuint index, GLfloat x, GLfloat y, GLfloat w, GLfloat h) {
    MITHRIL_ENSURE_INIT();
    (void)index;
    glViewport((GLint)x, (GLint)y, (GLsizei)w, (GLsizei)h);
}

void glViewportIndexedfv(GLuint index, const GLfloat* v) {
    MITHRIL_ENSURE_INIT();
    (void)index;
    if (v) glViewport((GLint)v[0], (GLint)v[1], (GLsizei)v[2], (GLsizei)v[3]);
}

void glScissorArrayv(GLuint first, GLsizei count, const GLint* v) {
    MITHRIL_ENSURE_INIT();
    if (!v) return;
    for (GLsizei i = 0; i < count; ++i) {
        glScissor(v[i * 4 + 0], v[i * 4 + 1], v[i * 4 + 2], v[i * 4 + 3]);
    }
    (void)first;
}

void glScissorIndexed(GLuint index, GLint left, GLint bottom, GLsizei width, GLsizei height) {
    MITHRIL_ENSURE_INIT();
    (void)index;
    glScissor(left, bottom, width, height);
}

void glScissorIndexedv(GLuint index, const GLint* v) {
    MITHRIL_ENSURE_INIT();
    (void)index;
    if (v) glScissor(v[0], v[1], v[2], v[3]);
}

void glDepthRangeArrayv(GLuint first, GLsizei count, const GLdouble* v) {
    MITHRIL_ENSURE_INIT();
    if (!v) return;
    for (GLsizei i = 0; i < count; ++i) glDepthRange(v[i * 2], v[i * 2 + 1]);
    (void)first;
}

void glDepthRangeIndexed(GLuint index, GLdouble n, GLdouble f) {
    MITHRIL_ENSURE_INIT();
    (void)index;
    glDepthRange(n, f);
}

/* ---- GL 3.0-4.5 state no-ops (not exercised by the target workload) ---- */
void glClampColor(GLenum target, GLenum clamp) {
    MITHRIL_ENSURE_INIT();
    (void)target; (void)clamp;
}

void glPointParameterf(GLenum pname, GLfloat param) {
    MITHRIL_ENSURE_INIT();
    (void)pname; (void)param;
}

void glPointParameterfv(GLenum pname, const GLfloat* params) {
    MITHRIL_ENSURE_INIT();
    (void)pname; (void)params;
}

void glPointParameteri(GLenum pname, GLint param) {
    MITHRIL_ENSURE_INIT();
    (void)pname; (void)param;
}

void glPointParameteriv(GLenum pname, const GLint* params) {
    MITHRIL_ENSURE_INIT();
    (void)pname; (void)params;
}

void glSampleCoverage(GLfloat value, GLboolean invert) {
    MITHRIL_ENSURE_INIT();
    (void)value; (void)invert;
}

void glProvokingVertex(GLenum mode) {
    MITHRIL_ENSURE_INIT();
    (void)mode;
}

/* ---- Generic vertex attribute variants. The float variants are already
 * no-ops (VertexArray.cpp) because MC's modern pipeline never uses generic
 * attributes; the double/int/packed/L variants mirror that policy. ---- */
void glVertexAttrib1d(GLuint index, GLdouble x)                 { MITHRIL_ENSURE_INIT(); (void)index; (void)x; }
void glVertexAttrib1dv(GLuint index, const GLdouble* v)         { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttrib1s(GLuint index, GLshort x)                  { MITHRIL_ENSURE_INIT(); (void)index; (void)x; }
void glVertexAttrib1sv(GLuint index, const GLshort* v)          { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttrib2d(GLuint index, GLdouble x, GLdouble y)     { MITHRIL_ENSURE_INIT(); (void)index; (void)x; (void)y; }
void glVertexAttrib2dv(GLuint index, const GLdouble* v)         { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttrib2s(GLuint index, GLshort x, GLshort y)       { MITHRIL_ENSURE_INIT(); (void)index; (void)x; (void)y; }
void glVertexAttrib2sv(GLuint index, const GLshort* v)          { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttrib3d(GLuint index, GLdouble x, GLdouble y, GLdouble z) { MITHRIL_ENSURE_INIT(); (void)index; (void)x; (void)y; (void)z; }
void glVertexAttrib3dv(GLuint index, const GLdouble* v)         { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttrib3s(GLuint index, GLshort x, GLshort y, GLshort z) { MITHRIL_ENSURE_INIT(); (void)index; (void)x; (void)y; (void)z; }
void glVertexAttrib3sv(GLuint index, const GLshort* v)          { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttrib4d(GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w) { MITHRIL_ENSURE_INIT(); (void)index; (void)x; (void)y; (void)z; (void)w; }
void glVertexAttrib4dv(GLuint index, const GLdouble* v)         { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttrib4s(GLuint index, GLshort x, GLshort y, GLshort z, GLshort w) { MITHRIL_ENSURE_INIT(); (void)index; (void)x; (void)y; (void)z; (void)w; }
void glVertexAttrib4sv(GLuint index, const GLshort* v)          { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttrib4bv(GLuint index, const GLbyte* v)           { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttrib4iv(GLuint index, const GLint* v)            { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttrib4ubv(GLuint index, const GLubyte* v)         { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttrib4uiv(GLuint index, const GLuint* v)          { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttrib4usv(GLuint index, const GLushort* v)        { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttrib4Nbv(GLuint index, const GLbyte* v)          { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttrib4Niv(GLuint index, const GLint* v)           { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttrib4Nsv(GLuint index, const GLshort* v)         { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttrib4Nub(GLuint index, GLubyte x, GLubyte y, GLubyte z, GLubyte w) { MITHRIL_ENSURE_INIT(); (void)index; (void)x; (void)y; (void)z; (void)w; }
void glVertexAttrib4Nubv(GLuint index, const GLubyte* v)        { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttrib4Nuiv(GLuint index, const GLuint* v)         { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttrib4Nusv(GLuint index, const GLushort* v)       { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }

void glVertexAttribI1i(GLuint index, GLint x)                   { MITHRIL_ENSURE_INIT(); (void)index; (void)x; }
void glVertexAttribI2i(GLuint index, GLint x, GLint y)          { MITHRIL_ENSURE_INIT(); (void)index; (void)x; (void)y; }
void glVertexAttribI3i(GLuint index, GLint x, GLint y, GLint z) { MITHRIL_ENSURE_INIT(); (void)index; (void)x; (void)y; (void)z; }
void glVertexAttribI4i(GLuint index, GLint x, GLint y, GLint z, GLint w) { MITHRIL_ENSURE_INIT(); (void)index; (void)x; (void)y; (void)z; (void)w; }
void glVertexAttribI1ui(GLuint index, GLuint x)                 { MITHRIL_ENSURE_INIT(); (void)index; (void)x; }
void glVertexAttribI2ui(GLuint index, GLuint x, GLuint y)       { MITHRIL_ENSURE_INIT(); (void)index; (void)x; (void)y; }
void glVertexAttribI3ui(GLuint index, GLuint x, GLuint y, GLuint z) { MITHRIL_ENSURE_INIT(); (void)index; (void)x; (void)y; (void)z; }
void glVertexAttribI4ui(GLuint index, GLuint x, GLuint y, GLuint z, GLuint w) { MITHRIL_ENSURE_INIT(); (void)index; (void)x; (void)y; (void)z; (void)w; }
void glVertexAttribI1iv(GLuint index, const GLint* v)           { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttribI2iv(GLuint index, const GLint* v)           { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttribI3iv(GLuint index, const GLint* v)           { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttribI4iv(GLuint index, const GLint* v)           { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttribI1uiv(GLuint index, const GLuint* v)         { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttribI2uiv(GLuint index, const GLuint* v)         { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttribI3uiv(GLuint index, const GLuint* v)         { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttribI4uiv(GLuint index, const GLuint* v)         { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttribI4bv(GLuint index, const GLbyte* v)          { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttribI4sv(GLuint index, const GLshort* v)         { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttribI4ubv(GLuint index, const GLubyte* v)        { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttribI4usv(GLuint index, const GLushort* v)       { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }

void glVertexAttribP1ui(GLuint index, GLenum type, GLboolean normalized, GLuint value) { MITHRIL_ENSURE_INIT(); (void)index; (void)type; (void)normalized; (void)value; }
void glVertexAttribP1uiv(GLuint index, GLenum type, GLboolean normalized, const GLuint* value) { MITHRIL_ENSURE_INIT(); (void)index; (void)type; (void)normalized; (void)value; }
void glVertexAttribP2ui(GLuint index, GLenum type, GLboolean normalized, GLuint value) { MITHRIL_ENSURE_INIT(); (void)index; (void)type; (void)normalized; (void)value; }
void glVertexAttribP2uiv(GLuint index, GLenum type, GLboolean normalized, const GLuint* value) { MITHRIL_ENSURE_INIT(); (void)index; (void)type; (void)normalized; (void)value; }
void glVertexAttribP3ui(GLuint index, GLenum type, GLboolean normalized, GLuint value) { MITHRIL_ENSURE_INIT(); (void)index; (void)type; (void)normalized; (void)value; }
void glVertexAttribP3uiv(GLuint index, GLenum type, GLboolean normalized, const GLuint* value) { MITHRIL_ENSURE_INIT(); (void)index; (void)type; (void)normalized; (void)value; }
void glVertexAttribP4ui(GLuint index, GLenum type, GLboolean normalized, GLuint value) { MITHRIL_ENSURE_INIT(); (void)index; (void)type; (void)normalized; (void)value; }
void glVertexAttribP4uiv(GLuint index, GLenum type, GLboolean normalized, const GLuint* value) { MITHRIL_ENSURE_INIT(); (void)index; (void)type; (void)normalized; (void)value; }

void glVertexAttribL1d(GLuint index, GLdouble x)                { MITHRIL_ENSURE_INIT(); (void)index; (void)x; }
void glVertexAttribL2d(GLuint index, GLdouble x, GLdouble y)    { MITHRIL_ENSURE_INIT(); (void)index; (void)x; (void)y; }
void glVertexAttribL3d(GLuint index, GLdouble x, GLdouble y, GLdouble z) { MITHRIL_ENSURE_INIT(); (void)index; (void)x; (void)y; (void)z; }
void glVertexAttribL4d(GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w) { MITHRIL_ENSURE_INIT(); (void)index; (void)x; (void)y; (void)z; (void)w; }
void glVertexAttribL1dv(GLuint index, const GLdouble* v)        { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttribL2dv(GLuint index, const GLdouble* v)        { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttribL3dv(GLuint index, const GLdouble* v)        { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttribL4dv(GLuint index, const GLdouble* v)        { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttribLPointer(GLuint index, GLint size, GLenum type, GLsizei stride, const void* pointer) {
    MITHRIL_ENSURE_INIT();
    (void)index; (void)size; (void)type; (void)stride; (void)pointer;
}

void glGetVertexAttribLdv(GLuint index, GLenum pname, GLdouble* params) {
    MITHRIL_ENSURE_INIT();
    glGetVertexAttribdv(index, pname, params);
}

/* ---- 通用顶点属性 float 变体（与 1f/4f/4fv 的 no-op 策略一致） ---- */
void glVertexAttrib1fv(GLuint index, const GLfloat* v)         { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttrib2f(GLuint index, GLfloat x, GLfloat y)       { MITHRIL_ENSURE_INIT(); (void)index; (void)x; (void)y; }
void glVertexAttrib2fv(GLuint index, const GLfloat* v)          { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }
void glVertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z) { MITHRIL_ENSURE_INIT(); (void)index; (void)x; (void)y; (void)z; }
void glVertexAttrib3fv(GLuint index, const GLfloat* v)          { MITHRIL_ENSURE_INIT(); (void)index; (void)v; }

/* ---- Double uniforms: convert to float and delegate ---- */
void glUniform1d(GLint loc, GLdouble v0)                          { float v = (float)v0; glUniform1f(loc, v); }
void glUniform2d(GLint loc, GLdouble v0, GLdouble v1)             { float v[2] = {(float)v0,(float)v1}; glUniform2fv(loc, 1, v); }
void glUniform3d(GLint loc, GLdouble v0, GLdouble v1, GLdouble v2){ float v[3] = {(float)v0,(float)v1,(float)v2}; glUniform3fv(loc, 1, v); }
void glUniform4d(GLint loc, GLdouble v0, GLdouble v1, GLdouble v2, GLdouble v3) { float v[4] = {(float)v0,(float)v1,(float)v2,(float)v3}; glUniform4fv(loc, 1, v); }
void glUniform1dv(GLint loc, GLsizei count, const GLdouble* value) {
    MITHRIL_ENSURE_INIT();
    if (!value || count <= 0) return;
    for (GLsizei i = 0; i < count; ++i) glUniform1f(loc + (GLint)i, (float)value[i]);
}
void glUniform2dv(GLint loc, GLsizei count, const GLdouble* value) {
    MITHRIL_ENSURE_INIT();
    if (!value || count <= 0) return;
    for (GLsizei i = 0; i < count; ++i) {
        float v[2] = {(float)value[i*2], (float)value[i*2+1]};
        glUniform2fv(loc + (GLint)i, 1, v);
    }
}
void glUniform3dv(GLint loc, GLsizei count, const GLdouble* value) {
    MITHRIL_ENSURE_INIT();
    if (!value || count <= 0) return;
    for (GLsizei i = 0; i < count; ++i) {
        float v[3] = {(float)value[i*3], (float)value[i*3+1], (float)value[i*3+2]};
        glUniform3fv(loc + (GLint)i, 1, v);
    }
}
void glUniform4dv(GLint loc, GLsizei count, const GLdouble* value) {
    MITHRIL_ENSURE_INIT();
    if (!value || count <= 0) return;
    for (GLsizei i = 0; i < count; ++i) {
        float v[4] = {(float)value[i*4], (float)value[i*4+1], (float)value[i*4+2], (float)value[i*4+3]};
        glUniform4fv(loc + (GLint)i, 1, v);
    }
}

static void uniform_matrix_d(GLint loc, GLsizei count, GLboolean transpose,
                             const GLdouble* value, int cols, int rows) {
    MITHRIL_ENSURE_INIT();
    if (!value || count <= 0) return;
    const int n = cols * rows;
    for (GLsizei i = 0; i < count; ++i) {
        float m[16];
        for (int k = 0; k < n; ++k) m[k] = (float)value[i * n + k];
        switch (n) {
            case 4:  glUniformMatrix2fv(loc + (GLint)i, 1, transpose, m); break;
            case 6:  glUniformMatrix2x3fv(loc + (GLint)i, 1, transpose, m); break;
            case 8:  glUniformMatrix2x4fv(loc + (GLint)i, 1, transpose, m); break;
            case 9:  glUniformMatrix3fv(loc + (GLint)i, 1, transpose, m); break;
            case 12: glUniformMatrix3x4fv(loc + (GLint)i, 1, transpose, m); break;
            case 16: glUniformMatrix4fv(loc + (GLint)i, 1, transpose, m); break;
            default: break;
        }
    }
}

void glUniformMatrix2dv(GLint loc, GLsizei count, GLboolean transpose, const GLdouble* value) { uniform_matrix_d(loc, count, transpose, value, 2, 2); }
void glUniformMatrix3dv(GLint loc, GLsizei count, GLboolean transpose, const GLdouble* value) { uniform_matrix_d(loc, count, transpose, value, 3, 3); }
void glUniformMatrix4dv(GLint loc, GLsizei count, GLboolean transpose, const GLdouble* value) { uniform_matrix_d(loc, count, transpose, value, 4, 4); }
void glUniformMatrix2x3dv(GLint loc, GLsizei count, GLboolean transpose, const GLdouble* value) { uniform_matrix_d(loc, count, transpose, value, 2, 3); }
void glUniformMatrix2x4dv(GLint loc, GLsizei count, GLboolean transpose, const GLdouble* value) { uniform_matrix_d(loc, count, transpose, value, 2, 4); }
void glUniformMatrix3x2dv(GLint loc, GLsizei count, GLboolean transpose, const GLdouble* value) { uniform_matrix_d(loc, count, transpose, value, 3, 2); }
void glUniformMatrix3x4dv(GLint loc, GLsizei count, GLboolean transpose, const GLdouble* value) { uniform_matrix_d(loc, count, transpose, value, 3, 4); }
void glUniformMatrix4x2dv(GLint loc, GLsizei count, GLboolean transpose, const GLdouble* value) { uniform_matrix_d(loc, count, transpose, value, 4, 2); }
void glUniformMatrix4x3dv(GLint loc, GLsizei count, GLboolean transpose, const GLdouble* value) { uniform_matrix_d(loc, count, transpose, value, 4, 3); }

void glGetUniformdv(GLuint program, GLint location, GLdouble* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    GLfloat f = 0.0f;
    glGetUniformfv(program, location, &f);
    *params = (GLdouble)f;
}

/* ---- glProgramUniform* (GL 4.1, ARB_separate_shader_objects): set a
 * program's uniforms without binding it. Delegate via save/restore of the
 * current program (store_uniform reads g_state->currentProgram). ---- */
class ProgramUniformScope {
public:
    explicit ProgramUniformScope(GLuint program)
        : state_(g_state), previous_(state_ ? state_->currentProgram : 0) {
        if (!state_) return;
        mithril::Program* target = mithril::state_get_program(program);
        if (!target || !target->linked) {
            mithril::state_set_error(GL_INVALID_OPERATION);
            return;
        }
        state_->currentProgram = program;
        valid_ = true;
    }

    ~ProgramUniformScope() {
        if (valid_ && state_) state_->currentProgram = previous_;
    }

    bool valid() const { return valid_; }

private:
    mithril::GLState* state_ = nullptr;
    GLuint previous_ = 0;
    bool valid_ = false;
};

// Keep the existing compact ProgramUniform wrappers while making the temporary
// selector override exception/early-return safe.  The RAII scope restores the
// program selected by glUseProgram when each wrapper returns.
#define program_uniform_begin(program) \
    MITHRIL_ENSURE_INIT(); \
    ProgramUniformScope _programUniformScope((program)); \
    if (!_programUniformScope.valid()) return

void glProgramUniform1i(GLuint program, GLint loc, GLint v0)               { program_uniform_begin(program); glUniform1i(loc, v0); }
void glProgramUniform1iv(GLuint program, GLint loc, GLsizei count, const GLint* value) { program_uniform_begin(program); glUniform1iv(loc, count, value); }
void glProgramUniform1f(GLuint program, GLint loc, GLfloat v0)             { program_uniform_begin(program); glUniform1f(loc, v0); }
void glProgramUniform1fv(GLuint program, GLint loc, GLsizei count, const GLfloat* value) { program_uniform_begin(program); glUniform1fv(loc, count, value); }
void glProgramUniform1d(GLuint program, GLint loc, GLdouble v0)            { program_uniform_begin(program); glUniform1d(loc, v0); }
void glProgramUniform1dv(GLuint program, GLint loc, GLsizei count, const GLdouble* value) { program_uniform_begin(program); glUniform1dv(loc, count, value); }
void glProgramUniform1ui(GLuint program, GLint loc, GLuint v0)             { program_uniform_begin(program); glUniform1ui(loc, v0); }
void glProgramUniform1uiv(GLuint program, GLint loc, GLsizei count, const GLuint* value) { program_uniform_begin(program); glUniform1uiv(loc, count, value); }
void glProgramUniform2i(GLuint program, GLint loc, GLint v0, GLint v1)     { program_uniform_begin(program); glUniform2i(loc, v0, v1); }
void glProgramUniform2iv(GLuint program, GLint loc, GLsizei count, const GLint* value) { program_uniform_begin(program); glUniform2iv(loc, count, value); }
void glProgramUniform2f(GLuint program, GLint loc, GLfloat v0, GLfloat v1) { program_uniform_begin(program); glUniform2f(loc, v0, v1); }
void glProgramUniform2fv(GLuint program, GLint loc, GLsizei count, const GLfloat* value) { program_uniform_begin(program); glUniform2fv(loc, count, value); }
void glProgramUniform2d(GLuint program, GLint loc, GLdouble v0, GLdouble v1) { program_uniform_begin(program); glUniform2d(loc, v0, v1); }
void glProgramUniform2dv(GLuint program, GLint loc, GLsizei count, const GLdouble* value) { program_uniform_begin(program); glUniform2dv(loc, count, value); }
void glProgramUniform2ui(GLuint program, GLint loc, GLuint v0, GLuint v1) { program_uniform_begin(program); glUniform2ui(loc, v0, v1); }
void glProgramUniform2uiv(GLuint program, GLint loc, GLsizei count, const GLuint* value) { program_uniform_begin(program); glUniform2uiv(loc, count, value); }
void glProgramUniform3i(GLuint program, GLint loc, GLint v0, GLint v1, GLint v2) { program_uniform_begin(program); glUniform3i(loc, v0, v1, v2); }
void glProgramUniform3iv(GLuint program, GLint loc, GLsizei count, const GLint* value) { program_uniform_begin(program); glUniform3iv(loc, count, value); }
void glProgramUniform3f(GLuint program, GLint loc, GLfloat v0, GLfloat v1, GLfloat v2) { program_uniform_begin(program); glUniform3f(loc, v0, v1, v2); }
void glProgramUniform3fv(GLuint program, GLint loc, GLsizei count, const GLfloat* value) { program_uniform_begin(program); glUniform3fv(loc, count, value); }
void glProgramUniform3d(GLuint program, GLint loc, GLdouble v0, GLdouble v1, GLdouble v2) { program_uniform_begin(program); glUniform3d(loc, v0, v1, v2); }
void glProgramUniform3dv(GLuint program, GLint loc, GLsizei count, const GLdouble* value) { program_uniform_begin(program); glUniform3dv(loc, count, value); }
void glProgramUniform3ui(GLuint program, GLint loc, GLuint v0, GLuint v1, GLuint v2) { program_uniform_begin(program); glUniform3ui(loc, v0, v1, v2); }
void glProgramUniform3uiv(GLuint program, GLint loc, GLsizei count, const GLuint* value) { program_uniform_begin(program); glUniform3uiv(loc, count, value); }
void glProgramUniform4i(GLuint program, GLint loc, GLint v0, GLint v1, GLint v2, GLint v3) { program_uniform_begin(program); glUniform4i(loc, v0, v1, v2, v3); }
void glProgramUniform4iv(GLuint program, GLint loc, GLsizei count, const GLint* value) { program_uniform_begin(program); glUniform4iv(loc, count, value); }
void glProgramUniform4f(GLuint program, GLint loc, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) { program_uniform_begin(program); glUniform4f(loc, v0, v1, v2, v3); }
void glProgramUniform4fv(GLuint program, GLint loc, GLsizei count, const GLfloat* value) { program_uniform_begin(program); glUniform4fv(loc, count, value); }
void glProgramUniform4d(GLuint program, GLint loc, GLdouble v0, GLdouble v1, GLdouble v2, GLdouble v3) { program_uniform_begin(program); glUniform4d(loc, v0, v1, v2, v3); }
void glProgramUniform4dv(GLuint program, GLint loc, GLsizei count, const GLdouble* value) { program_uniform_begin(program); glUniform4dv(loc, count, value); }
void glProgramUniform4ui(GLuint program, GLint loc, GLuint v0, GLuint v1, GLuint v2, GLuint v3) { program_uniform_begin(program); glUniform4ui(loc, v0, v1, v2, v3); }
void glProgramUniform4uiv(GLuint program, GLint loc, GLsizei count, const GLuint* value) { program_uniform_begin(program); glUniform4uiv(loc, count, value); }

void glProgramUniformMatrix2fv(GLuint program, GLint loc, GLsizei count, GLboolean transpose, const GLfloat* value) { program_uniform_begin(program); glUniformMatrix2fv(loc, count, transpose, value); }
void glProgramUniformMatrix3fv(GLuint program, GLint loc, GLsizei count, GLboolean transpose, const GLfloat* value) { program_uniform_begin(program); glUniformMatrix3fv(loc, count, transpose, value); }
void glProgramUniformMatrix4fv(GLuint program, GLint loc, GLsizei count, GLboolean transpose, const GLfloat* value) { program_uniform_begin(program); glUniformMatrix4fv(loc, count, transpose, value); }
void glProgramUniformMatrix2x3fv(GLuint program, GLint loc, GLsizei count, GLboolean transpose, const GLfloat* value) { program_uniform_begin(program); glUniformMatrix2x3fv(loc, count, transpose, value); }
void glProgramUniformMatrix3x2fv(GLuint program, GLint loc, GLsizei count, GLboolean transpose, const GLfloat* value) { program_uniform_begin(program); glUniformMatrix3x2fv(loc, count, transpose, value); }
void glProgramUniformMatrix2x4fv(GLuint program, GLint loc, GLsizei count, GLboolean transpose, const GLfloat* value) { program_uniform_begin(program); glUniformMatrix2x4fv(loc, count, transpose, value); }
void glProgramUniformMatrix4x2fv(GLuint program, GLint loc, GLsizei count, GLboolean transpose, const GLfloat* value) { program_uniform_begin(program); glUniformMatrix4x2fv(loc, count, transpose, value); }
void glProgramUniformMatrix3x4fv(GLuint program, GLint loc, GLsizei count, GLboolean transpose, const GLfloat* value) { program_uniform_begin(program); glUniformMatrix3x4fv(loc, count, transpose, value); }
void glProgramUniformMatrix4x3fv(GLuint program, GLint loc, GLsizei count, GLboolean transpose, const GLfloat* value) { program_uniform_begin(program); glUniformMatrix4x3fv(loc, count, transpose, value); }
void glProgramUniformMatrix2dv(GLuint program, GLint loc, GLsizei count, GLboolean transpose, const GLdouble* value) { program_uniform_begin(program); glUniformMatrix2dv(loc, count, transpose, value); }
void glProgramUniformMatrix3dv(GLuint program, GLint loc, GLsizei count, GLboolean transpose, const GLdouble* value) { program_uniform_begin(program); glUniformMatrix3dv(loc, count, transpose, value); }
void glProgramUniformMatrix4dv(GLuint program, GLint loc, GLsizei count, GLboolean transpose, const GLdouble* value) { program_uniform_begin(program); glUniformMatrix4dv(loc, count, transpose, value); }
void glProgramUniformMatrix2x3dv(GLuint program, GLint loc, GLsizei count, GLboolean transpose, const GLdouble* value) { program_uniform_begin(program); glUniformMatrix2x3dv(loc, count, transpose, value); }
void glProgramUniformMatrix3x2dv(GLuint program, GLint loc, GLsizei count, GLboolean transpose, const GLdouble* value) { program_uniform_begin(program); glUniformMatrix3x2dv(loc, count, transpose, value); }
void glProgramUniformMatrix2x4dv(GLuint program, GLint loc, GLsizei count, GLboolean transpose, const GLdouble* value) { program_uniform_begin(program); glUniformMatrix2x4dv(loc, count, transpose, value); }
void glProgramUniformMatrix4x2dv(GLuint program, GLint loc, GLsizei count, GLboolean transpose, const GLdouble* value) { program_uniform_begin(program); glUniformMatrix4x2dv(loc, count, transpose, value); }
void glProgramUniformMatrix3x4dv(GLuint program, GLint loc, GLsizei count, GLboolean transpose, const GLdouble* value) { program_uniform_begin(program); glUniformMatrix3x4dv(loc, count, transpose, value); }
void glProgramUniformMatrix4x3dv(GLuint program, GLint loc, GLsizei count, GLboolean transpose, const GLdouble* value) { program_uniform_begin(program); glUniformMatrix4x3dv(loc, count, transpose, value); }

#undef program_uniform_begin

/* ---- Program pipeline objects (GL 4.1): minimal name tracking. The
 * separable-program path is not exercised by the target workload; binding is
 * a no-op, Is/Get return safe defaults, and validation always succeeds. ---- */
static std::unordered_set<GLuint>& pipeline_live_names() {
    static std::unordered_set<GLuint> s;
    return s;
}

void glGenProgramPipelines(GLsizei n, GLuint* pipelines) {
    MITHRIL_ENSURE_INIT();
    if (!pipelines || n <= 0) return;
    static GLuint nextName = 1;
    for (GLsizei i = 0; i < n; ++i) {
        pipelines[i] = nextName++;
        pipeline_live_names().insert(pipelines[i]);
    }
}

void glDeleteProgramPipelines(GLsizei n, const GLuint* pipelines) {
    MITHRIL_ENSURE_INIT();
    if (!pipelines || n <= 0) return;
    for (GLsizei i = 0; i < n; ++i) pipeline_live_names().erase(pipelines[i]);
}

GLboolean glIsProgramPipeline(GLuint pipeline) {
    MITHRIL_ENSURE_INIT();
    return (pipeline != 0 && pipeline_live_names().count(pipeline)) ? GL_TRUE : GL_FALSE;
}

void glBindProgramPipeline(GLuint pipeline) {
    MITHRIL_ENSURE_INIT();
    // 绑定 program pipeline。pipeline=0 时恢复默认 (glUseProgram)。
    // 设置 g_state->currentProgramPipeline，draw 时据此选择使用 pipeline stages 还是 monolithic program。
    g_state->currentProgramPipeline = pipeline;
}

void glValidateProgramPipeline(GLuint pipeline) {
    MITHRIL_ENSURE_INIT();
    // 检查 pipeline 各 stage 是否完整。设置 pipeline 的 validated 标志。
    auto* ppipe = mithril::state_get_program_pipeline(pipeline);
    if (!ppipe) return;
    ppipe->validated = (ppipe->vsProgram != 0 || ppipe->fsProgram != 0);
}

void glGetProgramPipelineiv(GLuint pipeline, GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    auto* ppipe = mithril::state_get_program_pipeline(pipeline);
    if (!ppipe) { *params = 0; return; }
    switch (pname) {
        case GL_ACTIVE_PROGRAM:  *params = (GLint)ppipe->activeProgram; break;
        case GL_VERTEX_SHADER:   *params = (GLint)ppipe->vsProgram; break;
        case GL_FRAGMENT_SHADER: *params = (GLint)ppipe->fsProgram; break;
        case GL_COMPUTE_SHADER:  *params = (GLint)ppipe->csProgram; break;
        case GL_GEOMETRY_SHADER: *params = (GLint)ppipe->gsProgram; break;
        case GL_TESS_CONTROL_SHADER:    *params = (GLint)ppipe->tcsProgram; break;
        case GL_TESS_EVALUATION_SHADER: *params = (GLint)ppipe->tesProgram; break;
        case GL_VALIDATE_STATUS: *params = ppipe->validated ? GL_TRUE : GL_FALSE; break;
        case GL_INFO_LOG_LENGTH: *params = 0; break;
        default:                  *params = 0; break;
    }
}

void glGetProgramPipelineInfoLog(GLuint pipeline, GLsizei bufSize,
                                 GLsizei* length, GLchar* infoLog) {
    MITHRIL_ENSURE_INIT();
    if (length) *length = 0;
    if (infoLog && bufSize > 0) infoLog[0] = '\0';
}

void glUseProgramStages(GLuint pipeline, GLbitfield stages, GLuint program) {
    MITHRIL_ENSURE_INIT();
    // 记录 pipeline 中各 stage 使用的程序。
    auto* ppipe = mithril::state_get_program_pipeline(pipeline);
    if (!ppipe) return;
    // 将指定 stage(s) 分配给该 program
    if (stages & GL_VERTEX_SHADER_BIT) ppipe->vsProgram = program;
    if (stages & GL_FRAGMENT_SHADER_BIT) ppipe->fsProgram = program;
    if (stages & GL_COMPUTE_SHADER_BIT) ppipe->csProgram = program;
    if (stages & GL_GEOMETRY_SHADER_BIT) ppipe->gsProgram = program;
    if (stages & GL_TESS_CONTROL_SHADER_BIT) ppipe->tcsProgram = program;
    if (stages & GL_TESS_EVALUATION_SHADER_BIT) ppipe->tesProgram = program;
}

void glActiveShaderProgram(GLuint pipeline, GLuint program) {
    MITHRIL_ENSURE_INIT();
    // 设置 pipeline 上 active program (用于后续 glProgramUniform* 调用的目标)
    auto* ppipe = mithril::state_get_program_pipeline(pipeline);
    if (!ppipe) return;
    ppipe->activeProgram = program;
}

GLuint glCreateShaderProgramv(GLenum type, GLsizei count, const GLchar* const* strings) {
    MITHRIL_ENSURE_INIT();
    if (count <= 0 || !strings) return 0;
    auto* prog = mithril::state_create_program(0);
    if (!prog) return 0;
    prog->separable = true; // glCreateShaderProgramv 的程序自动 separable

    // 收集源码 (延迟编译: 程序在被 glUseProgramStages 绑定到时才编译)
    std::string src;
    for (GLsizei i = 0; i < count; ++i) {
        if (strings[i]) src += strings[i];
    }
    prog->sourceLine = src; // 存储源码供后续编译
    prog->shaderTypeHint = (GLenum)type;
    return prog->id;
}

void glProgramParameteri(GLuint program, GLenum pname, GLint value) {
    MITHRIL_ENSURE_INIT();
    // 跟踪 GL_PROGRAM_SEPARABLE 标志
    auto* prog = mithril::state_get_program(program);
    if (!prog) return;
    switch (pname) {
    case GL_PROGRAM_SEPARABLE:
        prog->separable = (value != 0);
        break;
    case GL_PROGRAM_BINARY_RETRIEVABLE_HINT:
        // 记录但不影响当前行为
        break;
    default:
        break;
    }
}

void glProgramBinary(GLuint program, GLenum binaryFormat, const void* binary, GLsizei length) {
    MITHRIL_ENSURE_INIT();
    auto* prog = mithril::state_get_program(program);
    if (!prog) return;
    (void)binaryFormat; (void)binary; (void)length;
    prog->infoLog = "glProgramBinary: binary shaders not supported (use GLSL path)";
}

void glGetProgramBinary(GLuint program, GLsizei bufSize, GLsizei* length,
                        GLenum* binaryFormat, void* binary) {
    MITHRIL_ENSURE_INIT();
    (void)program; (void)bufSize; (void)binary;
    if (length) *length = 0;
    if (binaryFormat) *binaryFormat = 0;
}

/* ---- 1D textures: delegate to the 2D paths with height=1 ---- */
void glTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width,
                  GLint border, GLenum format, GLenum type, const void* pixels) {
    MITHRIL_ENSURE_INIT();
    glTexImage2D(target, level, internalformat, width, 1, border, format, type, pixels);
}

void glTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width,
                     GLenum format, GLenum type, const void* pixels) {
    MITHRIL_ENSURE_INIT();
    glTexSubImage2D(target, level, xoffset, 0, width, 1, format, type, pixels);
}

void glCopyTexImage1D(GLenum target, GLint level, GLenum internalformat,
                      GLint x, GLint y, GLsizei width, GLint border) {
    MITHRIL_ENSURE_INIT();
    (void)target; (void)level; (void)internalformat; (void)x; (void)y;
    (void)width; (void)border;
}

void glCopyTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLint x, GLint y,
                         GLsizei width) {
    MITHRIL_ENSURE_INIT();
    (void)target; (void)level; (void)xoffset; (void)x; (void)y; (void)width;
}

void glCopyTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                         GLint zoffset, GLint x, GLint y, GLsizei width, GLsizei height) {
    MITHRIL_ENSURE_INIT();
    (void)target; (void)level; (void)xoffset; (void)yoffset; (void)zoffset;
    (void)x; (void)y; (void)width; (void)height;
}

void glFramebufferTexture1D(GLenum target, GLenum attachment, GLenum textarget,
                            GLuint texture, GLint level) {
    MITHRIL_ENSURE_INIT();
    glFramebufferTexture2D(target, attachment, GL_TEXTURE_2D, texture, level);
    (void)textarget;
}

void glFramebufferTexture3D(GLenum target, GLenum attachment, GLenum textarget,
                            GLuint texture, GLint level, GLint zoffset) {
    MITHRIL_ENSURE_INIT();
    glFramebufferTexture2D(target, attachment, GL_TEXTURE_2D, texture, level);
    (void)textarget; (void)zoffset;
}

/* ---- Transform feedback / conditional render / indexed queries ---- */
/* glBeginConditionalRender / glEndConditionalRender：真实实现移至
 * Stubs.cpp（复用遮挡查询取结果路径，门控 draw/clear）。 */

void glBeginQueryIndexed(GLenum target, GLuint index, GLuint id) {
    MITHRIL_ENSURE_INIT();
    (void)index;
    glBeginQuery(target, id);
}

void glEndQueryIndexed(GLenum target, GLuint index) {
    MITHRIL_ENSURE_INIT();
    (void)index;
    glEndQuery(target);
}

void glDrawTransformFeedback(GLenum mode, GLuint id) {
    MITHRIL_ENSURE_INIT();
    (void)mode; (void)id;
}

void glDrawTransformFeedbackStream(GLenum mode, GLuint id, GLuint stream) {
    MITHRIL_ENSURE_INIT();
    (void)mode; (void)id; (void)stream;
}

void glDrawTransformFeedbackInstanced(GLenum mode, GLuint id, GLsizei primcount) {
    MITHRIL_ENSURE_INIT();
    (void)mode; (void)id; (void)primcount;
}

void glDrawTransformFeedbackStreamInstanced(GLenum mode, GLuint id, GLuint stream,
                                            GLsizei primcount) {
    MITHRIL_ENSURE_INIT();
    (void)mode; (void)id; (void)stream; (void)primcount;
}

void glTransformFeedbackVaryings(GLuint program, GLsizei count,
                                 const GLchar* const* varyings, GLenum bufferMode) {
    MITHRIL_ENSURE_INIT();
    (void)program; (void)count; (void)varyings; (void)bufferMode;
}

/* ---- Buffer textures / multisample textures ----
 * glTexBufferRange/glTexBuffer 的状态记录与派生视图见上方
 * tex_buffer_attach；DSA 变体直接按名解析纹理。 */
void glTexBuffer(GLenum target, GLenum internalformat, GLuint buffer) {
    MITHRIL_ENSURE_INIT();
    if (target != GL_TEXTURE_BUFFER) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    tex_buffer_attach(mithril::state_get_texture_by_target(target),
                      internalformat, buffer, 0, 0, false);
}

void glTextureBuffer(GLuint texture, GLenum internalformat, GLuint buffer) {
    MITHRIL_ENSURE_INIT();
    tex_buffer_attach(mithril::state_get_texture(texture),
                      internalformat, buffer, 0, 0, false);
}

void glTextureBufferRange(GLuint texture, GLenum internalformat, GLuint buffer,
                          GLintptr offset, GLsizeiptr size) {
    MITHRIL_ENSURE_INIT();
    tex_buffer_attach(mithril::state_get_texture(texture),
                      internalformat, buffer, offset, size, true);
}

void glTexImage3DMultisample(GLenum target, GLsizei samples, GLenum internalformat,
                             GLsizei width, GLsizei height, GLsizei depth,
                             GLboolean fixedsamplelocations) {
    MITHRIL_ENSURE_INIT();
    (void)target; (void)samples; (void)internalformat;
    (void)width; (void)height; (void)depth; (void)fixedsamplelocations;
}

/* glTexStorage2DMultisample：不可变 multisample 存储。与
 * glTexImage2DMultisample 等价（multisample 纹理本就不可变、单 level），
 * 差别仅在规约层面（重复调用是 INVALID_OPERATION 而非重定义）。 */
void glTexStorage2DMultisample(GLenum target, GLsizei samples, GLenum internalformat,
                               GLsizei width, GLsizei height,
                               GLboolean fixedsamplelocations) {
    MITHRIL_ENSURE_INIT();
    if (target != GL_TEXTURE_2D_MULTISAMPLE &&
        target != GL_PROXY_TEXTURE_2D_MULTISAMPLE) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    if (target == GL_PROXY_TEXTURE_2D_MULTISAMPLE) return;
    mithril::Texture* t = mithril::state_get_texture_by_target(target);
    if (!t) { mithril::state_set_error(GL_INVALID_OPERATION); return; }
    if (t->immutable && t->width > 0) {
        // 不可变纹理已分配存储（GL 4.6 §8.19：重复 glTexStorage* 非法）。
        mithril::state_set_error(GL_INVALID_OPERATION);
        return;
    }
    glTexImage2DMultisample(target, samples, internalformat, width, height,
                            fixedsamplelocations);
}

void glTexStorage3DMultisample(GLenum target, GLsizei samples, GLenum internalformat,
                               GLsizei width, GLsizei height, GLsizei depth,
                               GLboolean fixedsamplelocations) {
    MITHRIL_ENSURE_INIT();
    (void)target; (void)samples; (void)internalformat;
    (void)width; (void)height; (void)depth; (void)fixedsamplelocations;
}

/* ---- Texture DSA variants: delegate where a clear GL 1.x analog exists,
 * otherwise no-op (MC uses the non-DSA path exclusively). ---- */
void glCompressedTextureSubImage1D(GLuint texture, GLint level, GLint xoffset,
                                   GLsizei width, GLenum format, GLsizei imageSize,
                                   const void* data) {
    MITHRIL_ENSURE_INIT();
    (void)texture; (void)level; (void)xoffset; (void)width;
    (void)format; (void)imageSize; (void)data;
}

void glCompressedTextureSubImage2D(GLuint texture, GLint level, GLint xoffset,
                                   GLint yoffset, GLsizei width, GLsizei height,
                                   GLenum format, GLsizei imageSize, const void* data) {
    MITHRIL_ENSURE_INIT();
    (void)texture; (void)level; (void)xoffset; (void)yoffset;
    (void)width; (void)height; (void)format; (void)imageSize; (void)data;
}

void glCompressedTextureSubImage3D(GLuint texture, GLint level, GLint xoffset,
                                   GLint yoffset, GLint zoffset, GLsizei width,
                                   GLsizei height, GLsizei depth, GLenum format,
                                   GLsizei imageSize, const void* data) {
    MITHRIL_ENSURE_INIT();
    (void)texture; (void)level; (void)xoffset; (void)yoffset; (void)zoffset;
    (void)width; (void)height; (void)depth; (void)format; (void)imageSize; (void)data;
}

void glCopyTextureSubImage1D(GLuint texture, GLint level, GLint xoffset, GLint x, GLint y,
                             GLsizei width) {
    MITHRIL_ENSURE_INIT();
    (void)texture; (void)level; (void)xoffset; (void)x; (void)y; (void)width;
}

void glCopyTextureSubImage2D(GLuint texture, GLint level, GLint xoffset, GLint yoffset,
                             GLint x, GLint y, GLsizei width, GLsizei height) {
    MITHRIL_ENSURE_INIT();
    (void)texture; (void)level; (void)xoffset; (void)yoffset;
    (void)x; (void)y; (void)width; (void)height;
}

void glCopyTextureSubImage3D(GLuint texture, GLint level, GLint xoffset, GLint yoffset,
                             GLint zoffset, GLint x, GLint y, GLsizei width, GLsizei height) {
    MITHRIL_ENSURE_INIT();
    (void)texture; (void)level; (void)xoffset; (void)yoffset; (void)zoffset;
    (void)x; (void)y; (void)width; (void)height;
}

void glTextureParameterIiv(GLuint texture, GLenum pname, const GLint* params) {
    MITHRIL_ENSURE_INIT();
    (void)texture; (void)pname; (void)params;
}

void glTextureParameterIuiv(GLuint texture, GLenum pname, const GLuint* params) {
    MITHRIL_ENSURE_INIT();
    (void)texture; (void)pname; (void)params;
}

void glGetTextureParameterIiv(GLuint texture, GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    (void)texture;
    if (params) sampler_default_params(pname, nullptr, params);
}

void glGetTextureParameterIuiv(GLuint texture, GLenum pname, GLuint* params) {
    MITHRIL_ENSURE_INIT();
    (void)texture;
    if (params) {
        GLint v = 0;
        sampler_default_params(pname, nullptr, &v);
        *params = (GLuint)v;
    }
}

void glNamedFramebufferParameteri(GLuint framebuffer, GLenum pname, GLint param) {
    MITHRIL_ENSURE_INIT();
    (void)framebuffer; (void)pname; (void)param;
}

/* ================================================================== */
/* OpenGL 4.6 Core Profile — Missing function implementations         */
/* ================================================================== */

/* ---- DSA Framebuffer draw/read buffer (GL 4.5 Core) ---- */
/* These delegate to the non-DSA variants via bind-call-restore. */

void glFramebufferDrawBuffer(GLuint framebuffer, GLenum buf) {
    MITHRIL_ENSURE_INIT();
    GLuint prev = g_state->currentDrawFBO;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glDrawBuffer(buf);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev);
}

void glFramebufferDrawBuffers(GLuint framebuffer, GLsizei n, const GLenum* bufs) {
    MITHRIL_ENSURE_INIT();
    GLuint prev = g_state->currentDrawFBO;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glDrawBuffers(n, bufs);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev);
}

void glFramebufferReadBuffer(GLuint framebuffer, GLenum src) {
    MITHRIL_ENSURE_INIT();
    GLuint prev = g_state->currentReadFBO;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    glReadBuffer(src);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, prev);
}

/* ---- Texture environment/coordinate queries (GL 1.x-3.x compat stubs) ---- */

void glGetTexEnvfv(GLenum target, GLenum pname, GLfloat* params) {
    MITHRIL_ENSURE_INIT();
    if (params) *params = 0.0f;
}

void glGetTexEnviv(GLenum target, GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    if (params) *params = 0;
}

void glGetTexGendv(GLenum coord, GLenum pname, GLdouble* params) {
    MITHRIL_ENSURE_INIT();
    if (params) *params = 0.0;
}

void glGetTexGenfv(GLenum coord, GLenum pname, GLfloat* params) {
    MITHRIL_ENSURE_INIT();
    if (params) *params = 0.0f;
}

void glGetTexGeniv(GLenum coord, GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    if (params) *params = 0;
}

void glTexGendv(GLenum coord, GLenum pname, const GLdouble* params) {
    MITHRIL_ENSURE_INIT(); (void)coord; (void)pname; (void)params;
}

/* ---- Packed vertex attribute functions (GL 3.3 Core) ---- */

void glVertexP2ui(GLenum t, GLuint v) { MITHRIL_ENSURE_INIT(); (void)t; (void)v; }
void glVertexP2uiv(GLenum t, const GLuint* v) { MITHRIL_ENSURE_INIT(); (void)t; (void)v; }
void glVertexP3ui(GLenum t, GLuint v) { MITHRIL_ENSURE_INIT(); (void)t; (void)v; }
void glVertexP3uiv(GLenum t, const GLuint* v) { MITHRIL_ENSURE_INIT(); (void)t; (void)v; }
void glVertexP4ui(GLenum t, GLuint v) { MITHRIL_ENSURE_INIT(); (void)t; (void)v; }
void glVertexP4uiv(GLenum t, const GLuint* v) { MITHRIL_ENSURE_INIT(); (void)t; (void)v; }
void glColorP3ui(GLenum t, GLuint v) { MITHRIL_ENSURE_INIT(); (void)t; (void)v; }
void glColorP3uiv(GLenum t, const GLuint* v) { MITHRIL_ENSURE_INIT(); (void)t; (void)v; }
void glColorP4ui(GLenum t, GLuint v) { MITHRIL_ENSURE_INIT(); (void)t; (void)v; }
void glColorP4uiv(GLenum t, const GLuint* v) { MITHRIL_ENSURE_INIT(); (void)t; (void)v; }
void glNormalP3ui(GLenum t, GLuint v) { MITHRIL_ENSURE_INIT(); (void)t; (void)v; }
void glNormalP3uiv(GLenum t, const GLuint* v) { MITHRIL_ENSURE_INIT(); (void)t; (void)v; }
void glMultiTexCoordP1ui(GLenum tx, GLenum t, GLuint v) { MITHRIL_ENSURE_INIT(); (void)tx; (void)t; (void)v; }
void glMultiTexCoordP1uiv(GLenum tx, GLenum t, const GLuint* v) { MITHRIL_ENSURE_INIT(); (void)tx; (void)t; (void)v; }
void glMultiTexCoordP2ui(GLenum tx, GLenum t, GLuint v) { MITHRIL_ENSURE_INIT(); (void)tx; (void)t; (void)v; }
void glMultiTexCoordP2uiv(GLenum tx, GLenum t, const GLuint* v) { MITHRIL_ENSURE_INIT(); (void)tx; (void)t; (void)v; }
void glMultiTexCoordP3ui(GLenum tx, GLenum t, GLuint v) { MITHRIL_ENSURE_INIT(); (void)tx; (void)t; (void)v; }
void glMultiTexCoordP3uiv(GLenum tx, GLenum t, const GLuint* v) { MITHRIL_ENSURE_INIT(); (void)tx; (void)t; (void)v; }
void glMultiTexCoordP4ui(GLenum tx, GLenum t, GLuint v) { MITHRIL_ENSURE_INIT(); (void)tx; (void)t; (void)v; }
void glMultiTexCoordP4uiv(GLenum tx, GLenum t, const GLuint* v) { MITHRIL_ENSURE_INIT(); (void)tx; (void)t; (void)v; }
void glTexCoordP1ui(GLenum t, GLuint v) { MITHRIL_ENSURE_INIT(); (void)t; (void)v; }
void glTexCoordP1uiv(GLenum t, const GLuint* v) { MITHRIL_ENSURE_INIT(); (void)t; (void)v; }
void glTexCoordP2ui(GLenum t, GLuint v) { MITHRIL_ENSURE_INIT(); (void)t; (void)v; }
void glTexCoordP2uiv(GLenum t, const GLuint* v) { MITHRIL_ENSURE_INIT(); (void)t; (void)v; }
void glTexCoordP3ui(GLenum t, GLuint v) { MITHRIL_ENSURE_INIT(); (void)t; (void)v; }
void glTexCoordP3uiv(GLenum t, const GLuint* v) { MITHRIL_ENSURE_INIT(); (void)t; (void)v; }
void glTexCoordP4ui(GLenum t, GLuint v) { MITHRIL_ENSURE_INIT(); (void)t; (void)v; }
void glTexCoordP4uiv(GLenum t, const GLuint* v) { MITHRIL_ENSURE_INIT(); (void)t; (void)v; }
void glSecondaryColorP3ui(GLenum t, GLuint v) { MITHRIL_ENSURE_INIT(); (void)t; (void)v; }
void glSecondaryColorP3uiv(GLenum t, const GLuint* v) { MITHRIL_ENSURE_INIT(); (void)t; (void)v; }
void glSecondaryColorPointer(GLint s, GLenum t, GLsizei st, const void* p) { MITHRIL_ENSURE_INIT(); (void)s; (void)t; (void)st; (void)p; }

/* ---- Deprecated GL 1.x-3.x stubs (symbol presence for dlsym) ---- */

void glFogCoordf(GLfloat c) { MITHRIL_ENSURE_INIT(); (void)c; }
void glFogCoordfv(const GLfloat* c) { MITHRIL_ENSURE_INIT(); (void)c; }
void glFogCoordd(GLdouble c) { MITHRIL_ENSURE_INIT(); (void)c; }
void glFogCoorddv(const GLdouble* c) { MITHRIL_ENSURE_INIT(); (void)c; }
void glFogCoordPointer(GLenum t, GLsizei s, const void* p) { MITHRIL_ENSURE_INIT(); (void)t; (void)s; (void)p; }
void glIndexd(GLdouble c) { MITHRIL_ENSURE_INIT(); (void)c; }
void glIndexdv(const GLdouble* c) { MITHRIL_ENSURE_INIT(); (void)c; }
void glIndexf(GLfloat c) { MITHRIL_ENSURE_INIT(); (void)c; }
void glIndexfv(const GLfloat* c) { MITHRIL_ENSURE_INIT(); (void)c; }
void glIndexi(GLint c) { MITHRIL_ENSURE_INIT(); (void)c; }
void glIndexiv(const GLint* c) { MITHRIL_ENSURE_INIT(); (void)c; }
void glIndexs(GLshort c) { MITHRIL_ENSURE_INIT(); (void)c; }
void glIndexsv(const GLshort* c) { MITHRIL_ENSURE_INIT(); (void)c; }
void glIndexub(GLubyte c) { MITHRIL_ENSURE_INIT(); (void)c; }
void glIndexubv(const GLubyte* c) { MITHRIL_ENSURE_INIT(); (void)c; }
void glLightModeli(GLenum p, GLint v) { MITHRIL_ENSURE_INIT(); (void)p; (void)v; }
void glLightModeliv(GLenum p, const GLint* v) { MITHRIL_ENSURE_INIT(); (void)p; (void)v; }
void glLighti(GLenum l, GLenum p, GLint v) { MITHRIL_ENSURE_INIT(); (void)l; (void)p; (void)v; }
void glLightiv(GLenum l, GLenum p, const GLint* v) { MITHRIL_ENSURE_INIT(); (void)l; (void)p; (void)v; }
void glMaterialiv(GLenum f, GLenum p, const GLint* v) { MITHRIL_ENSURE_INIT(); (void)f; (void)p; (void)v; }
void glLoadTransposeMatrixd(const GLdouble* m) { MITHRIL_ENSURE_INIT(); (void)m; }
void glLoadTransposeMatrixf(const GLfloat* m) { MITHRIL_ENSURE_INIT(); (void)m; }
void glMultTransposeMatrixd(const GLdouble* m) { MITHRIL_ENSURE_INIT(); (void)m; }
void glMultTransposeMatrixf(const GLfloat* m) { MITHRIL_ENSURE_INIT(); (void)m; }
void glMap1f(GLenum t, GLfloat a, GLfloat b, GLint s, GLint o, const GLfloat* p) { MITHRIL_ENSURE_INIT(); (void)t; (void)a; (void)b; (void)s; (void)o; (void)p; }
void glMap2f(GLenum t, GLfloat a, GLfloat b, GLint s, GLint o, GLfloat c, GLfloat d, GLint e, GLint f, const GLfloat* p) { MITHRIL_ENSURE_INIT(); (void)t; (void)a; (void)b; (void)s; (void)o; (void)c; (void)d; (void)e; (void)f; (void)p; }
void glMapGrid1f(GLint n, GLfloat a, GLfloat b) { MITHRIL_ENSURE_INIT(); (void)n; (void)a; (void)b; }
void glMapGrid2f(GLint n, GLfloat a, GLfloat b, GLint v, GLfloat c, GLfloat d) { MITHRIL_ENSURE_INIT(); (void)n; (void)a; (void)b; (void)v; (void)c; (void)d; }
void glEvalCoord1f(GLfloat u) { MITHRIL_ENSURE_INIT(); (void)u; }
void glEvalCoord1fv(const GLfloat* u) { MITHRIL_ENSURE_INIT(); (void)u; }
void glEvalCoord1dv(const GLdouble* u) { MITHRIL_ENSURE_INIT(); (void)u; }
void glEvalCoord2f(GLfloat u, GLfloat v) { MITHRIL_ENSURE_INIT(); (void)u; (void)v; }
void glEvalCoord2fv(const GLfloat* u) { MITHRIL_ENSURE_INIT(); (void)u; }
void glEvalCoord2dv(const GLdouble* u) { MITHRIL_ENSURE_INIT(); (void)u; }
void glRasterPos2d(GLdouble x, GLdouble y) { MITHRIL_ENSURE_INIT(); (void)x; (void)y; }
void glRasterPos2dv(const GLdouble* v) { MITHRIL_ENSURE_INIT(); (void)v; }
void glRasterPos2f(GLfloat x, GLfloat y) { MITHRIL_ENSURE_INIT(); (void)x; (void)y; }
void glRasterPos2fv(const GLfloat* v) { MITHRIL_ENSURE_INIT(); (void)v; }
void glRasterPos2i(GLint x, GLint y) { MITHRIL_ENSURE_INIT(); (void)x; (void)y; }
void glRasterPos2iv(const GLint* v) { MITHRIL_ENSURE_INIT(); (void)v; }
void glRasterPos2s(GLshort x, GLshort y) { MITHRIL_ENSURE_INIT(); (void)x; (void)y; }
void glRasterPos2sv(const GLshort* v) { MITHRIL_ENSURE_INIT(); (void)v; }
void glRasterPos3d(GLdouble x, GLdouble y, GLdouble z) { MITHRIL_ENSURE_INIT(); (void)x; (void)y; (void)z; }
void glRasterPos3dv(const GLdouble* v) { MITHRIL_ENSURE_INIT(); (void)v; }
void glRasterPos3f(GLfloat x, GLfloat y, GLfloat z) { MITHRIL_ENSURE_INIT(); (void)x; (void)y; (void)z; }
void glRasterPos3fv(const GLfloat* v) { MITHRIL_ENSURE_INIT(); (void)v; }
void glRasterPos3i(GLint x, GLint y, GLint z) { MITHRIL_ENSURE_INIT(); (void)x; (void)y; (void)z; }
void glRasterPos3iv(const GLint* v) { MITHRIL_ENSURE_INIT(); (void)v; }
void glRasterPos3s(GLshort x, GLshort y, GLshort z) { MITHRIL_ENSURE_INIT(); (void)x; (void)y; (void)z; }
void glRasterPos3sv(const GLshort* v) { MITHRIL_ENSURE_INIT(); (void)v; }
void glRasterPos4d(GLdouble x, GLdouble y, GLdouble z, GLdouble w) { MITHRIL_ENSURE_INIT(); (void)x; (void)y; (void)z; (void)w; }
void glRasterPos4dv(const GLdouble* v) { MITHRIL_ENSURE_INIT(); (void)v; }
void glRasterPos4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w) { MITHRIL_ENSURE_INIT(); (void)x; (void)y; (void)z; (void)w; }
void glRasterPos4fv(const GLfloat* v) { MITHRIL_ENSURE_INIT(); (void)v; }
void glRasterPos4i(GLint x, GLint y, GLint z, GLint w) { MITHRIL_ENSURE_INIT(); (void)x; (void)y; (void)z; (void)w; }
void glRasterPos4iv(const GLint* v) { MITHRIL_ENSURE_INIT(); (void)v; }
void glRasterPos4s(GLshort x, GLshort y, GLshort z, GLshort w) { MITHRIL_ENSURE_INIT(); (void)x; (void)y; (void)z; (void)w; }
void glRasterPos4sv(const GLshort* v) { MITHRIL_ENSURE_INIT(); (void)v; }
void glRectd(GLdouble x1, GLdouble y1, GLdouble x2, GLdouble y2) { MITHRIL_ENSURE_INIT(); (void)x1; (void)y1; (void)x2; (void)y2; }
void glRectdv(const GLdouble* v1, const GLdouble* v2) { MITHRIL_ENSURE_INIT(); (void)v1; (void)v2; }
void glRectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2) { MITHRIL_ENSURE_INIT(); (void)x1; (void)y1; (void)x2; (void)y2; }
void glRectfv(const GLfloat* v1, const GLfloat* v2) { MITHRIL_ENSURE_INIT(); (void)v1; (void)v2; }
void glRecti(GLint x1, GLint y1, GLint x2, GLint y2) { MITHRIL_ENSURE_INIT(); (void)x1; (void)y1; (void)x2; (void)y2; }
void glRectiv(const GLint* v1, const GLint* v2) { MITHRIL_ENSURE_INIT(); (void)v1; (void)v2; }
void glRects(GLshort x1, GLshort y1, GLshort x2, GLshort y2) { MITHRIL_ENSURE_INIT(); (void)x1; (void)y1; (void)x2; (void)y2; }
void glRectsv(const GLshort* v1, const GLshort* v2) { MITHRIL_ENSURE_INIT(); (void)v1; (void)v2; }
void glWindowPos2d(GLdouble x, GLdouble y) { MITHRIL_ENSURE_INIT(); (void)x; (void)y; }
void glWindowPos2dv(const GLdouble* v) { MITHRIL_ENSURE_INIT(); (void)v; }
void glWindowPos2f(GLfloat x, GLfloat y) { MITHRIL_ENSURE_INIT(); (void)x; (void)y; }
void glWindowPos2fv(const GLfloat* v) { MITHRIL_ENSURE_INIT(); (void)v; }
void glWindowPos2i(GLint x, GLint y) { MITHRIL_ENSURE_INIT(); (void)x; (void)y; }
void glWindowPos2iv(const GLint* v) { MITHRIL_ENSURE_INIT(); (void)v; }
void glWindowPos2s(GLshort x, GLshort y) { MITHRIL_ENSURE_INIT(); (void)x; (void)y; }
void glWindowPos2sv(const GLshort* v) { MITHRIL_ENSURE_INIT(); (void)v; }
void glWindowPos3d(GLdouble x, GLdouble y, GLdouble z) { MITHRIL_ENSURE_INIT(); (void)x; (void)y; (void)z; }
void glWindowPos3dv(const GLdouble* v) { MITHRIL_ENSURE_INIT(); (void)v; }
void glWindowPos3f(GLfloat x, GLfloat y, GLfloat z) { MITHRIL_ENSURE_INIT(); (void)x; (void)y; (void)z; }
void glWindowPos3fv(const GLfloat* v) { MITHRIL_ENSURE_INIT(); (void)v; }
void glWindowPos3i(GLint x, GLint y, GLint z) { MITHRIL_ENSURE_INIT(); (void)x; (void)y; (void)z; }
void glWindowPos3iv(const GLint* v) { MITHRIL_ENSURE_INIT(); (void)v; }
void glWindowPos3s(GLshort x, GLshort y, GLshort z) { MITHRIL_ENSURE_INIT(); (void)x; (void)y; (void)z; }
void glWindowPos3sv(const GLshort* v) { MITHRIL_ENSURE_INIT(); (void)v; }

/* ---- Extension function (GL_ARB_sparse_texture) ---- */
void glTexturePageCommitmentExt(GLuint t, GLint l, GLint x, GLint y, GLint z, GLsizei w, GLsizei h, GLsizei d, GLboolean c) {
    MITHRIL_ENSURE_INIT(); (void)t; (void)l; (void)x; (void)y; (void)z; (void)w; (void)h; (void)d; (void)c;
}

} /* extern "C" */
