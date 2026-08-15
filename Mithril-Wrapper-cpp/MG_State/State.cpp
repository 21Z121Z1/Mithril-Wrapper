// Mithril-Wrapper - MG_State/State.cpp
// Implementation of the rewritten GL state machine.
// See State.h header comment and specs/rewrite-gl-state-machine/spec.md
// for design rationale.
#include "State.h"

namespace mithril {

// ---- thread_local current context ----
thread_local GLState* g_state = nullptr;

// ---- EGL initialized flag ----
bool g_eglInitialized = false;

// =========================================================================
// Enum conversions
// =========================================================================
BufferTarget bufferTargetFromGL(GLenum target) noexcept {
    switch (target) {
        case GL_ARRAY_BUFFER:               return BufferTarget::Array;
        case GL_UNIFORM_BUFFER:             return BufferTarget::Uniform;
        case GL_COPY_READ_BUFFER:           return BufferTarget::CopyRead;
        case GL_COPY_WRITE_BUFFER:          return BufferTarget::CopyWrite;
        case GL_PIXEL_PACK_BUFFER:          return BufferTarget::PixelPack;
        case GL_PIXEL_UNPACK_BUFFER:        return BufferTarget::PixelUnpack;
        case GL_TRANSFORM_FEEDBACK_BUFFER:  return BufferTarget::TransformFeedback;
        case GL_ATOMIC_COUNTER_BUFFER:      return BufferTarget::AtomicCounter;
        case GL_SHADER_STORAGE_BUFFER:      return BufferTarget::ShaderStorage;
        case GL_DRAW_INDIRECT_BUFFER:       return BufferTarget::DrawIndirect;
        case GL_DISPATCH_INDIRECT_BUFFER:   return BufferTarget::DispatchIndirect;
        case GL_QUERY_BUFFER:               return BufferTarget::Query;
        case GL_PARAMETER_BUFFER:           return BufferTarget::Parameter;
        case GL_TEXTURE_BUFFER:             return BufferTarget::Texture;
        default:                            return BufferTarget::Count;
    }
}

GLenum bufferTargetToGL(BufferTarget t) noexcept {
    switch (t) {
        case BufferTarget::Array:              return GL_ARRAY_BUFFER;
        case BufferTarget::Uniform:            return GL_UNIFORM_BUFFER;
        case BufferTarget::CopyRead:           return GL_COPY_READ_BUFFER;
        case BufferTarget::CopyWrite:          return GL_COPY_WRITE_BUFFER;
        case BufferTarget::PixelPack:          return GL_PIXEL_PACK_BUFFER;
        case BufferTarget::PixelUnpack:        return GL_PIXEL_UNPACK_BUFFER;
        case BufferTarget::TransformFeedback:  return GL_TRANSFORM_FEEDBACK_BUFFER;
        case BufferTarget::AtomicCounter:      return GL_ATOMIC_COUNTER_BUFFER;
        case BufferTarget::ShaderStorage:      return GL_SHADER_STORAGE_BUFFER;
        case BufferTarget::DrawIndirect:       return GL_DRAW_INDIRECT_BUFFER;
        case BufferTarget::DispatchIndirect:   return GL_DISPATCH_INDIRECT_BUFFER;
        case BufferTarget::Query:              return GL_QUERY_BUFFER;
        case BufferTarget::Parameter:          return GL_PARAMETER_BUFFER;
        case BufferTarget::Texture:            return GL_TEXTURE_BUFFER;
        default:                               return 0;
    }
}

TextureTarget textureTargetFromGL(GLenum target) noexcept {
    switch (target) {
        case GL_TEXTURE_1D:                   return TextureTarget::_1D;
        case GL_TEXTURE_2D:                   return TextureTarget::_2D;
        case GL_TEXTURE_3D:                   return TextureTarget::_3D;
        case GL_TEXTURE_CUBE_MAP:             return TextureTarget::CubeMap;
        // FIX (主菜单 panorama cubemap GPU fault 根因 - face target 未识别):
        // GL 规范允许 glTexImage2D / glTexSubImage2D 以 6 个面 target
        // （GL_TEXTURE_CUBE_MAP_POSITIVE_X .. NEGATIVE_Z）逐面上传 cubemap。
        // MC 1.21.1 的主菜单背景（Cubemap）正是这样上传 6 张 panorama 图。
        // 旧实现不认识这些 target → bound_texture_for_target 返回 nullptr →
        // 上传被静默丢弃 → cubemap 纹理内容全空 → 主菜单首帧采样未初始化
        // 纹理层 → MoltenVK/A11 GPU Address Fault（zink 正常上传因此正常）。
        case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
        case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
        case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
        case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
        case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
        case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:  return TextureTarget::CubeMap;
        case GL_TEXTURE_RECTANGLE:            return TextureTarget::Rectangle;
        case GL_TEXTURE_2D_MULTISAMPLE:       return TextureTarget::_2DMultisample;
        case GL_TEXTURE_BUFFER:               return TextureTarget::Buffer;
        case GL_TEXTURE_1D_ARRAY:             return TextureTarget::_1DArray;
        case GL_TEXTURE_2D_ARRAY:             return TextureTarget::_2DArray;
        case GL_TEXTURE_CUBE_MAP_ARRAY:       return TextureTarget::CubeMapArray;
        case GL_TEXTURE_2D_MULTISAMPLE_ARRAY: return TextureTarget::_2DMultisampleArray;
        default:                              return TextureTarget::Count;
    }
}

GLenum textureTargetToGL(TextureTarget t) noexcept {
    switch (t) {
        case TextureTarget::_1D:                   return GL_TEXTURE_1D;
        case TextureTarget::_2D:                   return GL_TEXTURE_2D;
        case TextureTarget::_3D:                   return GL_TEXTURE_3D;
        case TextureTarget::CubeMap:               return GL_TEXTURE_CUBE_MAP;
        case TextureTarget::Rectangle:             return GL_TEXTURE_RECTANGLE;
        case TextureTarget::_2DMultisample:        return GL_TEXTURE_2D_MULTISAMPLE;
        case TextureTarget::Buffer:                return GL_TEXTURE_BUFFER;
        case TextureTarget::_1DArray:              return GL_TEXTURE_1D_ARRAY;
        case TextureTarget::_2DArray:              return GL_TEXTURE_2D_ARRAY;
        case TextureTarget::CubeMapArray:          return GL_TEXTURE_CUBE_MAP_ARRAY;
        case TextureTarget::_2DMultisampleArray:   return GL_TEXTURE_2D_MULTISAMPLE_ARRAY;
        default:                                   return 0;
    }
}

IndexedBufferTarget indexedBufferTargetFromGL(GLenum target) noexcept {
    switch (target) {
        case GL_UNIFORM_BUFFER:             return IndexedBufferTarget::Uniform;
        case GL_TRANSFORM_FEEDBACK_BUFFER:  return IndexedBufferTarget::TransformFeedback;
        case GL_ATOMIC_COUNTER_BUFFER:      return IndexedBufferTarget::AtomicCounter;
        case GL_SHADER_STORAGE_BUFFER:      return IndexedBufferTarget::ShaderStorage;
        default:                            return IndexedBufferTarget::Count;
    }
}

// =========================================================================
// NameAllocator
// =========================================================================
void NameAllocator::generate(GLsizei n, GLuint* out) {
    if (n <= 0 || !out) return;
    for (GLsizei i = 0; i < n; ++i) {
        if (!m_freeList.empty()) {
            GLuint id = m_freeList.back();
            m_freeList.pop_back();
            // Re-mark as valid.
            if (id < m_valid.size()) m_valid[id] = true;
            out[i] = id;
        } else {
            GLuint id = m_next++;
            // Grow valid vector as needed.
            if (id >= m_valid.size()) m_valid.resize(id + 1, false);
            m_valid[id] = true;
            out[i] = id;
        }
    }
}

bool NameAllocator::insert(GLuint id) {
    if (id >= m_valid.size()) m_valid.resize(id + 1, false);
    if (m_valid[id]) return false;  // already valid
    m_valid[id] = true;
    return true;
}

void NameAllocator::release(GLuint id) {
    if (id == 0) return;  // default objects are immortal
    if (id >= m_valid.size() || !m_valid[id]) return;
    m_valid[id] = false;
    m_freeList.push_back(id);
}

bool NameAllocator::valid(GLuint id) const {
    if (id >= m_valid.size()) return false;
    return m_valid[id];
}

void NameAllocator::clear() {
    m_next = 1;
    m_freeList.clear();
    m_valid.clear();
}

// =========================================================================
// ErrorState
// =========================================================================
void ErrorState::recordGL(GLenum err) {
    if (err == GL_NO_ERROR) return;
    if (glErrors.size() >= kErrorQueueLimit) {
        // Drop the newest to preserve early errors for diagnosis.
        return;
    }
    glErrors.push_back(err);
}

GLenum ErrorState::popGL() {
    if (glErrors.empty()) return GL_NO_ERROR;
    GLenum e = glErrors.front();
    glErrors.pop_front();
    return e;
}

void ErrorState::recordInternal(std::string msg) {
    if (internalErrors.size() >= kErrorQueueLimit) return;
    internalErrors.push_back(std::move(msg));
}

bool ErrorState::hasInternal() const {
    return !internalErrors.empty();
}

std::string ErrorState::popInternal() {
    if (internalErrors.empty()) return {};
    std::string s = std::move(internalErrors.front());
    internalErrors.pop_front();
    return s;
}

void ErrorState::clear() {
    glErrors.clear();
    internalErrors.clear();
}

// =========================================================================
// GLState capability dispatch (P0-6: single source of truth)
// =========================================================================
void GLState::setCapability(GLenum cap, bool enabled) {
    switch (cap) {
        case GL_DEPTH_TEST:              depthTest = enabled; break;
        case GL_BLEND:                   blends[0].enabled = enabled; break;
        case GL_STENCIL_TEST:            stencilTest = enabled; break;
        case GL_CULL_FACE:               cullFace = enabled; break;
        case GL_SCISSOR_TEST:            scissorTest = enabled; break;
        case GL_DITHER:                  dither = enabled; break;
        case GL_MULTISAMPLE:             multisample = enabled; break;
        case GL_SAMPLE_ALPHA_TO_COVERAGE: sampleAlphaToCoverage = enabled; break;
        case GL_SAMPLE_COVERAGE:         sampleCoverage = enabled; break;
        case GL_SAMPLE_MASK:             sampleMask = enabled; break;
        case GL_PROGRAM_POINT_SIZE:      programPointSize = enabled; break;
        case GL_PRIMITIVE_RESTART:       primitiveRestart = enabled; break;
        case GL_FRAMEBUFFER_SRGB:        framebufferSRGB = enabled; break;
        case GL_POLYGON_OFFSET_FILL:     polygonOffsetFill = enabled; break;
        case GL_DEPTH_CLAMP:             depthClamp = enabled; break;
        case GL_RASTERIZER_DISCARD:      rasterizerDiscard = enabled; break;
        case GL_TEXTURE_CUBE_MAP_SEAMLESS: textureCubeMapSeamless = enabled; break;
        case GL_CLIP_DISTANCE0: case GL_CLIP_DISTANCE1: case GL_CLIP_DISTANCE2:
        case GL_CLIP_DISTANCE3: case GL_CLIP_DISTANCE4: case GL_CLIP_DISTANCE5:
        case GL_CLIP_DISTANCE6: case GL_CLIP_DISTANCE7:
            clipDistance[cap - GL_CLIP_DISTANCE0] = enabled; break;
        default: break;  // unknown caps silently ignored (no error for compat)
    }
    bumpRenderVersion();
}

bool GLState::isKnownCapability(GLenum cap) const {
    switch (cap) {
        case GL_DEPTH_TEST: case GL_BLEND: case GL_STENCIL_TEST:
        case GL_CULL_FACE: case GL_SCISSOR_TEST: case GL_DITHER:
        case GL_MULTISAMPLE: case GL_SAMPLE_ALPHA_TO_COVERAGE:
        case GL_SAMPLE_COVERAGE: case GL_SAMPLE_MASK:
        case GL_PROGRAM_POINT_SIZE: case GL_PRIMITIVE_RESTART:
        case GL_FRAMEBUFFER_SRGB: case GL_POLYGON_OFFSET_FILL:
        case GL_DEPTH_CLAMP: case GL_RASTERIZER_DISCARD:
        case GL_TEXTURE_CUBE_MAP_SEAMLESS:
        case GL_CLIP_DISTANCE0: case GL_CLIP_DISTANCE1: case GL_CLIP_DISTANCE2:
        case GL_CLIP_DISTANCE3: case GL_CLIP_DISTANCE4: case GL_CLIP_DISTANCE5:
        case GL_CLIP_DISTANCE6: case GL_CLIP_DISTANCE7:
            return true;
        default:
            return false;
    }
}

bool GLState::isCapabilityEnabled(GLenum cap) const {
    switch (cap) {
        case GL_DEPTH_TEST:              return depthTest;
        case GL_BLEND:                   return blends[0].enabled;
        case GL_STENCIL_TEST:            return stencilTest;
        case GL_CULL_FACE:               return cullFace;
        case GL_SCISSOR_TEST:            return scissorTest;
        case GL_DITHER:                  return dither;
        case GL_MULTISAMPLE:             return multisample;
        case GL_SAMPLE_ALPHA_TO_COVERAGE: return sampleAlphaToCoverage;
        case GL_SAMPLE_COVERAGE:         return sampleCoverage;
        case GL_SAMPLE_MASK:             return sampleMask;
        case GL_PROGRAM_POINT_SIZE:      return programPointSize;
        case GL_PRIMITIVE_RESTART:       return primitiveRestart;
        case GL_FRAMEBUFFER_SRGB:        return framebufferSRGB;
        case GL_POLYGON_OFFSET_FILL:     return polygonOffsetFill;
        case GL_DEPTH_CLAMP:             return depthClamp;
        case GL_RASTERIZER_DISCARD:      return rasterizerDiscard;
        case GL_TEXTURE_CUBE_MAP_SEAMLESS: return textureCubeMapSeamless;
        case GL_CLIP_DISTANCE0: case GL_CLIP_DISTANCE1: case GL_CLIP_DISTANCE2:
        case GL_CLIP_DISTANCE3: case GL_CLIP_DISTANCE4: case GL_CLIP_DISTANCE5:
        case GL_CLIP_DISTANCE6: case GL_CLIP_DISTANCE7:
            return clipDistance[cap - GL_CLIP_DISTANCE0];
        default: return false;
    }
}

GLuint GLState::boundTextureForUnit(GLuint unit) const {
    if (unit >= kMaxTextureUnits) return 0;
    // Prefer GL_TEXTURE_2D (Minecraft main path) — avoid returning a
    // CubeMap/3D texture bound to the same unit, which would produce a
    // VkImageView with mismatched viewType and fail MoltenVK validation.
    int t2d = static_cast<int>(TextureTarget::_2D);
    GLuint name = textureBindings[unit][t2d].name;
    if (name != 0) return name;
    // Fall back to other targets if 2D is unbound.
    for (int t = 0; t < kTextureTargetCount; ++t) {
        if (t == t2d) continue;
        GLuint n = textureBindings[unit][t].name;
        if (n != 0) return n;
    }
    return 0;
}

// =========================================================================
// State lifecycle
// =========================================================================
bool state_init() {
    if (g_state && g_state->initialized) return true;
    if (!g_state) g_state = state_create();
    g_state->initialized = true;
    return true;
}

GLState* state_create() {
    GLState* s = new GLState{};
    s->initialized = true;

    // Default VAO (name 0) — real, pre-bound, immortal.
    {
        VertexArray vao{};
        vao.id = 0;
        s->vaos[0] = vao;
        s->vaoNames.insert(0);
    }

    // Default framebuffer (name 0) — real, pre-bound, immortal.
    {
        Framebuffer fbo{};
        fbo.id = 0;
        fbo.drawBuffers[0] = GL_COLOR_ATTACHMENT0;
        fbo.drawBufferCount = 1;
        fbo.readBuffer = GL_COLOR_ATTACHMENT0;
        s->framebuffers[0] = fbo;
        s->fboNames.insert(0);
    }

    // Default transform feedback (name 0) — real, pre-bound, immortal.
    {
        TransformFeedback tf{};
        tf.id = 0;
        s->transformFeedbacks[0] = tf;
        s->tfNames.insert(0);
    }

    // Default textures (name 0, one per target) — immortal, pre-bound to
    // every texture unit.  They do NOT go into the `textures` map, so
    // glIsTexture(0) returns false (name 0 is never produced by glGenTextures).
    for (int t = 0; t < kTextureTargetCount; ++t) {
        s->defaultTextures[t].id = 0;
        s->defaultTextures[t].target = textureTargetToGL(static_cast<TextureTarget>(t));
    }
    s->textureNames.insert(0);

    // Pre-bind default textures to every unit's every target slot.
    // The BindingSlot.name for default textures is 0 (which means "bound to
    // default").  This is correct: glBindTexture(target, 0) restores the
    // default texture, and textureBindings[unit][target].name == 0 means
    // "default texture is bound".
    // (Slots are already 0-initialized, so no explicit action needed.)

    return s;
}

void state_destroy(GLState* s) {
    if (!s) return;
    // EGL default color/depth VkImageViews are owned by the EGLSurface
    // (swapchain), not by the GLState.
    s->eglDefaultColor = VK_NULL_HANDLE;
    s->eglDefaultDepth = VK_NULL_HANDLE;
    delete s;
}

// =========================================================================
// Object accessors
// =========================================================================
VertexArray* state_get_vao(GLuint id) {
    if (!g_state) return nullptr;
    auto it = g_state->vaos.find(id);
    return it == g_state->vaos.end() ? nullptr : &it->second;
}

Buffer* state_get_buffer(GLuint id) {
    if (!g_state || id == 0) return nullptr;
    auto it = g_state->buffers.find(id);
    return it == g_state->buffers.end() ? nullptr : &it->second;
}

Texture* state_get_texture(GLuint id) {
    if (!g_state || id == 0) return nullptr;
    auto it = g_state->textures.find(id);
    return it == g_state->textures.end() ? nullptr : &it->second;
}

Texture* state_get_texture_by_target(GLenum target) {
    if (!g_state) return nullptr;
    // GL_TEXTURE_BUFFER is the target used by glTexBufferRange/TexBuffer.
    // The active texture unit's binding for that target gives us the texture.
    int unit = g_state->activeTextureUnit;
    if (unit >= mithril::kMaxTextureUnits) return nullptr;
    TextureTarget tt;
    switch (target) {
        case GL_TEXTURE_2D:          tt = TextureTarget::_2D; break;
        case GL_TEXTURE_3D:          tt = TextureTarget::_3D; break;
        case GL_TEXTURE_CUBE_MAP:    tt = TextureTarget::CubeMap; break;
        case GL_TEXTURE_1D:          tt = TextureTarget::_1D; break;
        case GL_TEXTURE_2D_ARRAY:    tt = TextureTarget::_2DArray; break;
        case GL_TEXTURE_BUFFER:      tt = TextureTarget::Buffer; break;
        default:                     tt = TextureTarget::_2D; break;
    }
    GLuint name = g_state->textureBindings[unit][static_cast<int>(tt)].name;
    if (name == 0) return nullptr;
    return state_get_texture(name);
}

Shader* state_get_shader(GLuint id) {
    if (!g_state || id == 0) return nullptr;
    auto it = g_state->shaders.find(id);
    return it == g_state->shaders.end() ? nullptr : &it->second;
}

Program* state_get_program(GLuint id) {
    if (!g_state || id == 0) return nullptr;
    auto it = g_state->programs.find(id);
    return it == g_state->programs.end() ? nullptr : &it->second;
}

Program* state_create_program(uint32_t name) {
    if (!g_state) return nullptr;
    if (name == 0) {
        mithril::state_gen_names("program", 1, &name);
    }
    mithril::Program p{};
    p.id = name;
    g_state->programs[name] = p;
    return &g_state->programs[name];
}

Framebuffer* state_get_framebuffer(GLuint id) {
    if (!g_state) return nullptr;
    auto it = g_state->framebuffers.find(id);
    return it == g_state->framebuffers.end() ? nullptr : &it->second;
}

Sampler* state_get_sampler(GLuint id) {
    if (!g_state || id == 0) return nullptr;
    auto it = g_state->samplers.find(id);
    return it == g_state->samplers.end() ? nullptr : &it->second;
}

Renderbuffer* state_get_renderbuffer(GLuint id) {
    if (!g_state || id == 0) return nullptr;
    auto it = g_state->renderbuffers.find(id);
    return it == g_state->renderbuffers.end() ? nullptr : &it->second;
}

TransformFeedback* state_get_transform_feedback(GLuint id) {
    if (!g_state) return nullptr;
    auto it = g_state->transformFeedbacks.find(id);
    return it == g_state->transformFeedbacks.end() ? nullptr : &it->second;
}

Query* state_get_query(GLuint id) {
    if (!g_state || id == 0) return nullptr;
    auto it = g_state->queries.find(id);
    return it == g_state->queries.end() ? nullptr : &it->second;
}

// ---- GL 查询对象：软件图元计数（GL_PRIMITIVES_GENERATED /
// GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN）----
// Metal 无管线统计查询（MoltenVK pipelineStatisticsQuery=VK_FALSE），但 draw
// 参数在录制期已知，图元数可精确推导（与 GPU 图元装配规则一致）。由
// backend_draw_* 家族汇入；glEndQuery 时收割进查询对象。indirect draw 的
// count 在 GPU buffer 里 CPU 不可见，无法计入（文档化限制）。
void account_draw_primitives(int gl_mode, int64_t count, int64_t instances) {
    if (!g_state || count <= 0 || instances <= 0) return;
    const bool wantPrim = g_state->activeQuery[(int)QueryTarget::PrimitivesGenerated] != 0;
    const bool wantTfb  = g_state->activeQuery[(int)QueryTarget::TfbPrimsWritten] != 0;
    if (!wantPrim && !wantTfb) return;

    int64_t prims = 0;
    switch (gl_mode) {
        case 0x0000: prims = count; break;                        // GL_POINTS
        case 0x0001: prims = count / 2; break;                    // GL_LINES
        case 0x0002: prims = (count > 2) ? count : 0; break;      // GL_LINE_LOOP：n 顶点 n 段
        case 0x0003: prims = (count > 1) ? count - 1 : 0; break;  // GL_LINE_STRIP
        case 0x0004: prims = count / 3; break;                    // GL_TRIANGLES
        case 0x0005: prims = (count > 2) ? count - 2 : 0; break;  // GL_TRIANGLE_STRIP
        case 0x0006: prims = (count > 2) ? count - 2 : 0; break;  // GL_TRIANGLE_FAN
        case 0x0007: prims = count / 4 * 2; break;                // GL_QUADS→双三角（compat）
        default:      prims = count / 3; break;                   // 保守
    }
    prims *= instances;
    if (wantPrim) g_state->swPrimAccum += (uint64_t)prims;
    if (wantTfb)  g_state->swTfbWrittenAccum += (uint64_t)prims;
}

// ---- Program pipeline helpers (GL 4.1 ARB_separate_shader_objects) ----
ProgramPipeline* state_get_program_pipeline(GLuint id) {
    if (!g_state || id == 0) return nullptr;
    auto it = g_state->programPipelines.find(id);
    return it == g_state->programPipelines.end() ? nullptr : &it->second;
}

ProgramPipeline* state_create_program_pipeline(GLuint id) {
    if (!g_state || id == 0) return nullptr;
    auto& ppipe = g_state->programPipelines[id];
    ppipe.id = id;
    return &ppipe;
}

// =========================================================================
// Error helpers (P0-2: queue-based, not single slot)
// =========================================================================
void state_set_error(GLenum err) {
    if (!g_state) return;
    g_state->errors.recordGL(err);
}

GLenum state_take_error() {
    if (!g_state) return GL_NO_ERROR;
    return g_state->errors.popGL();
}

// =========================================================================
// Name allocation (P2-4: unified via NameAllocator, with reuse)
// =========================================================================
void state_gen_names(const char* kind, GLsizei n, GLuint* out) {
    if (!g_state || n <= 0 || !out) {
        if (out && n > 0) {
            for (GLsizei i = 0; i < n; ++i) out[i] = 0;
        }
        return;
    }
    NameAllocator* alloc = nullptr;
    if (kind) {
        if (std::strcmp(kind, "buffer") == 0)        alloc = &g_state->bufferNames;
        else if (std::strcmp(kind, "texture") == 0)   alloc = &g_state->textureNames;
        else if (std::strcmp(kind, "sampler") == 0)   alloc = &g_state->samplerNames;
        else if (std::strcmp(kind, "vao") == 0)       alloc = &g_state->vaoNames;
        else if (std::strcmp(kind, "fbo") == 0)       alloc = &g_state->fboNames;
        else if (std::strcmp(kind, "program") == 0)   alloc = &g_state->programNames;
        else if (std::strcmp(kind, "shader") == 0)    alloc = &g_state->shaderNames;
        else if (std::strcmp(kind, "renderbuffer") == 0) alloc = &g_state->renderbufferNames;
        else if (std::strcmp(kind, "query") == 0)     alloc = &g_state->queryNames;
        else if (std::strcmp(kind, "tf") == 0)        alloc = &g_state->tfNames;
    }
    if (alloc) {
        alloc->generate(n, out);
    } else {
        // Fallback: monotonic (should not happen in practice).
        for (GLsizei i = 0; i < n; ++i) out[i] = 0;
    }
}

// =========================================================================
// Capability check (P0-6: single source of truth)
// =========================================================================
bool state_is_capability_enabled(GLenum cap) {
    if (!g_state) return false;
    return g_state->isCapabilityEnabled(cap);
}

} // namespace mithril
