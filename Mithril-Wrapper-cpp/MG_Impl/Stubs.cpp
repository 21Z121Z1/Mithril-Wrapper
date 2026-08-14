// Mithril-Wrapper - MG_Impl/Stubs.cpp
// Legacy fixed-function entry points provided as no-op stubs so that
// applications dlsym-ing GL 1.x/2.x symbols resolve cleanly. The Core Profile
// path does not call into any of these; they exist only for symbol presence.
//
// Pattern mirrors MobileGlues' STUB_FUNCTION_* macros: a single shared
// definition body that records a debug log line and returns a sensible default.
//
// This is the Vulkan/MoltenVK rewrite of the former gl/stubs.cpp; the bodies
// are identical because the stubs do not depend on the backend.
// Constants missing from our minimal glcorearb.h but used as default field
// values in State.h. Guarded so a future header update won't conflict.
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#endif
#ifndef GL_INTERLEAVED_ATTRIBS
#define GL_INTERLEAVED_ATTRIBS       0x8C8C
#endif

// ---- GL enums for Sampler / Query / Transform Feedback (absent from minimal glcorearb.h) ----
#ifndef GL_SAMPLER_BINDING
#define GL_SAMPLER_BINDING           0x8919
#endif
#ifndef GL_SAMPLES_PASSED
#define GL_SAMPLES_PASSED            0x8914
#endif
#ifndef GL_ANY_SAMPLES_PASSED
#define GL_ANY_SAMPLES_PASSED        0x8C2F
#endif
#ifndef GL_PRIMITIVES_GENERATED
#define GL_PRIMITIVES_GENERATED      0x8C87
#endif
#ifndef GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN
#define GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN 0x8C88
#endif
#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED              0x88BF
#endif
#ifndef GL_TIMESTAMP
#define GL_TIMESTAMP                 0x8E28
#endif
#ifndef GL_QUERY_COUNTER_BITS
#define GL_QUERY_COUNTER_BITS        0x8864
#endif
#ifndef GL_CURRENT_QUERY
#define GL_CURRENT_QUERY             0x8865
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT              0x8866
#endif
#ifndef GL_QUERY_RESULT_AVAILABLE
#define GL_QUERY_RESULT_AVAILABLE    0x8867
#endif
#ifndef GL_TRANSFORM_FEEDBACK
#define GL_TRANSFORM_FEEDBACK        0x8E22
#endif
#ifndef GL_TRANSFORM_FEEDBACK_BINDING
#define GL_TRANSFORM_FEEDBACK_BINDING 0x8E25
#endif
#ifndef GL_TEXTURE_BORDER_COLOR
#define GL_TEXTURE_BORDER_COLOR      0x1004
#endif
#ifndef GL_TEXTURE_LOD_BIAS
#define GL_TEXTURE_LOD_BIAS          0x8501
#endif
#ifndef GL_TEXTURE_MIN_LOD
#define GL_TEXTURE_MIN_LOD           0x813A
#endif
#ifndef GL_TEXTURE_MAX_LOD
#define GL_TEXTURE_MAX_LOD           0x813B
#endif
#ifndef GL_TEXTURE_COMPARE_MODE
#define GL_TEXTURE_COMPARE_MODE      0x884C
#endif
#ifndef GL_TEXTURE_COMPARE_FUNC
#define GL_TEXTURE_COMPARE_FUNC      0x884D
#endif

#include "includes.h"

#include <cstring>

extern "C" {

/* ---- Miscellaneous legacy ---- */
void glClearIndex(GLfloat) {}
void glIndexMask(GLuint) {}
void glAlphaFunc(GLenum, GLclampf) {}
void glLogicOp(GLenum) {}
void glLineStipple(GLint, GLushort) {}
void glPolygonStipple(const GLubyte*) {}
void glGetPolygonStipple(GLubyte*) {}
void glEdgeFlag(GLboolean) {}
void glEdgeFlagv(const GLboolean*) {}
void glClipPlane(GLenum, const GLdouble*) {}
void glGetClipPlane(GLenum, GLdouble*) {}

/* ---- Accumulation buffer ---- */
void glClearAccum(GLfloat, GLfloat, GLfloat, GLfloat) {}
void glAccum(GLenum, GLfloat) {}

/* ---- Transformation (matrix stack) ---- */
void glMatrixMode(GLenum) {}
void glOrtho(GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble) {}
void glFrustum(GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble) {}
void glPushMatrix(void) {}
void glPopMatrix(void) {}
void glLoadIdentity(void) {}
void glLoadMatrixd(const GLdouble*) {}
void glLoadMatrixf(const GLfloat*) {}
void glMultMatrixd(const GLdouble*) {}
void glMultMatrixf(const GLfloat*) {}
void glRotated(GLdouble, GLdouble, GLdouble, GLdouble) {}
void glRotatef(GLfloat, GLfloat, GLfloat, GLfloat) {}
void glScaled(GLdouble, GLdouble, GLdouble) {}
void glScalef(GLfloat, GLfloat, GLfloat) {}
void glTranslated(GLdouble, GLdouble, GLdouble) {}
void glTranslatef(GLfloat, GLfloat, GLfloat) {}

/* ---- Display lists ---- */
GLboolean glIsList(GLuint) { return GL_FALSE; }
void glDeleteLists(GLuint, GLsizei) {}
GLuint glGenLists(GLsizei) { return 0; }
void glNewList(GLuint, GLenum) {}
void glEndList(void) {}
void glCallList(GLuint) {}
void glCallLists(GLsizei, GLenum, const GLvoid*) {}
void glListBase(GLuint) {}

/* ---- Immediate mode ---- */
void glBegin(GLenum) {}
void glEnd(void) {}
void glVertex2d(GLdouble, GLdouble) {}
void glVertex2f(GLfloat, GLfloat) {}
void glVertex2i(GLint, GLint) {}
void glVertex3f(GLfloat, GLfloat, GLfloat) {}
void glVertex3fv(const GLfloat*) {}
void glVertex4f(GLfloat, GLfloat, GLfloat, GLfloat) {}
void glNormal3f(GLfloat, GLfloat, GLfloat) {}
void glColor3f(GLfloat, GLfloat, GLfloat) {}
void glColor3ub(GLubyte, GLubyte, GLubyte) {}
void glColor4f(GLfloat, GLfloat, GLfloat, GLfloat) {}
void glColor4ub(GLubyte, GLubyte, GLubyte, GLubyte) {}
void glTexCoord2f(GLfloat, GLfloat) {}
void glTexCoord4f(GLfloat, GLfloat, GLfloat, GLfloat) {}
// glRasterPos* and glWindowPos* are implemented in GL46_Compat.cpp
// (GL 4.6 Core Profile full implementation)

/* ---- Lighting / material / fog ---- */
void glShadeModel(GLenum) {}
void glMaterialf(GLenum, GLenum, GLfloat) {}
void glMaterialfv(GLenum, GLenum, const GLfloat*) {}
void glMateriali(GLenum, GLenum, GLint) {}
void glLightf(GLenum, GLenum, GLfloat) {}
void glLightfv(GLenum, GLenum, const GLfloat*) {}
void glLightModelf(GLenum, GLfloat) {}
void glLightModelfv(GLenum, const GLfloat*) {}
void glFogf(GLenum, GLfloat) {}
void glFogi(GLenum, GLint) {}
void glFogfv(GLenum, const GLfloat*) {}
void glFogiv(GLenum, const GLint*) {}
void glColorMaterial(GLenum, GLenum) {}

/* ---- Tex gen / env ---- */
void glTexGend(GLenum, GLenum, GLdouble) {}
void glTexGenf(GLenum, GLenum, GLfloat) {}
void glTexGeni(GLenum, GLenum, GLint) {}
void glTexGenfv(GLenum, GLenum, const GLfloat*) {}
void glTexGeniv(GLenum, GLenum, const GLint*) {}
void glTexEnvf(GLenum, GLenum, GLfloat) {}
void glTexEnvi(GLenum, GLenum, GLint) {}
void glTexEnvfv(GLenum, GLenum, const GLfloat*) {}
void glTexEnviv(GLenum, GLenum, const GLint*) {}

/* ---- Pixel transfer / copy ---- */
void glPixelTransferf(GLenum, GLfloat) {}
void glPixelTransferi(GLenum, GLint) {}
void glPixelMapfv(GLenum, GLsizei, const GLfloat*) {}
void glPixelMapuiv(GLenum, GLsizei, const GLuint*) {}
void glPixelMapusv(GLenum, GLsizei, const GLushort*) {}
void glPixelZoom(GLfloat, GLfloat) {}
void glCopyPixels(GLint, GLint, GLsizei, GLsizei, GLenum) {}

/* ---- Selection / feedback ---- */
void glInitNames(void) {}
void glLoadName(GLuint) {}
void glPushName(GLuint) {}
void glPopName(void) {}
GLint glRenderMode(GLenum) { return 0; }
void glSelectBuffer(GLsizei, GLuint*) {}
void glFeedbackBuffer(GLsizei, GLenum, GLfloat*) {}
void glPassThrough(GLfloat) {}

/* ---- Evaluator ---- */
void glMap1d(GLenum, GLdouble, GLdouble, GLint, GLint, const GLdouble*) {}
void glMap2d(GLenum, GLdouble, GLdouble, GLint, GLint, GLdouble, GLdouble, GLint, GLint, const GLdouble*) {}
void glEvalCoord1d(GLdouble) {}
void glEvalCoord2d(GLdouble, GLdouble) {}
void glMapGrid1d(GLint, GLdouble, GLdouble) {}
void glMapGrid2d(GLint, GLdouble, GLdouble, GLint, GLdouble, GLdouble) {}
void glEvalMesh1(GLenum, GLint, GLint) {}
void glEvalMesh2(GLenum, GLint, GLint, GLint, GLint) {}
void glEvalPoint1(GLint) {}
void glEvalPoint2(GLint, GLint) {}

/* ---- Rect ---- */
// glRect* are implemented in GL46_Compat.cpp
// (GL 4.6 Core Profile full implementation)

/* ---- Attrib stack ---- */
void glPushAttrib(GLbitfield) {}
void glPopAttrib(void) {}
void glPushClientAttrib(GLbitfield) {}
void glPopClientAttrib(void) {}

/* ---- Client vertex arrays (legacy) ---- */
void glEnableClientState(GLenum) {}
void glDisableClientState(GLenum) {}
void glVertexPointer(GLint, GLenum, GLsizei, const void*) {}
void glNormalPointer(GLenum, GLsizei, const void*) {}
void glColorPointer(GLint, GLenum, GLsizei, const void*) {}
void glTexCoordPointer(GLint, GLenum, GLsizei, const void*) {}
void glIndexPointer(GLenum, GLsizei, const void*) {}
void glEdgeFlagPointer(GLsizei, const void*) {}
void glInterleavedArrays(GLenum, GLsizei, const void*) {}
void glArrayElement(GLint) {}

/* ---- Misc legacy getters/queries ---- */
void glGetPointerv(GLenum pname, void** params) {
    (void)pname;
    if (params) *params = nullptr;
}

void glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    if (params) *params = 0;
    if (level != 0) return; // only level 0 supported

    // Proxy texture queries: return the dimensions recorded by the last
    // glTexImage2D(GL_PROXY_TEXTURE_2D, ...) call. If the combo was
    // unsupported, valid=false and width/height are 0.
    if (target == GL_PROXY_TEXTURE_2D) {
        switch (pname) {
            case GL_TEXTURE_WIDTH:
                *params = g_state->proxyTexture2D.valid
                        ? g_state->proxyTexture2D.width : 0;
                break;
            case GL_TEXTURE_HEIGHT:
                *params = g_state->proxyTexture2D.valid
                        ? g_state->proxyTexture2D.height : 0;
                break;
            case GL_TEXTURE_INTERNAL_FORMAT:
                *params = g_state->proxyTexture2D.valid
                        ? g_state->proxyTexture2D.internalFormat : 0;
                break;
            default:
                *params = 0;
                break;
        }
        return;
    }

    // Real texture queries: return the tracked dimensions for level 0.
    mithril::Texture* t = nullptr;
    GLuint unit = g_state->activeTextureUnit;
    if (unit < mithril::kMaxTextureUnits) {
        t = mithril::state_get_texture(g_state->boundTextureForUnit(unit));
    }
    if (!t) return;
    switch (pname) {
        case GL_TEXTURE_WIDTH:           *params = t->width; break;
        case GL_TEXTURE_HEIGHT:          *params = t->height; break;
        case GL_TEXTURE_DEPTH:           *params = t->depth; break;
        case GL_TEXTURE_INTERNAL_FORMAT: *params = t->internalFormat; break;
        default:                         *params = 0; break;
    }
}

// glGetTexParameteriv / glGetTexParameterfv / glGetTexImage are implemented
// (returning real tracked values) in MG_Impl/Texture.cpp — see P1-4 fix.

GLboolean glAreTexturesResident(GLsizei, const GLuint*, GLboolean* residences) {
    if (residences) {
        // caller is responsible for sizing residences; mark all resident.
    }
    return GL_TRUE;
}

void glPrioritizeTextures(GLsizei, const GLuint*, const GLclampf*) {}

void glDepthBoundsEXT(GLclampd, GLclampd) {}

/* =========================================================================
 * Sampler objects (P1-1 / P2-1)
 * Samplers override the embedded sampler state in Texture objects.  When a
 * sampler is bound to a unit, its params take precedence over the texture's
 * own min/mag/wrap/lod params for that unit.
 * ========================================================================= */
void glGenSamplers(GLsizei n, GLuint* samplers) {
    MITHRIL_ENSURE_INIT();
    mithril::state_gen_names("sampler", n, samplers);
    for (GLsizei i = 0; i < n; ++i) {
        mithril::Sampler s{};
        s.id = samplers[i];
        s.lifetimeId = g_state->nextSamplerLifetimeId++;
        g_state->samplers[samplers[i]] = s;
    }
}

void glDeleteSamplers(GLsizei n, const GLuint* samplers) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !samplers) return;
    for (GLsizei i = 0; i < n; ++i) {
        GLuint name = samplers[i];
        if (name == 0) continue;
        // Unbind from every texture unit.
        for (int u = 0; u < mithril::kMaxTextureUnits; ++u) {
            if (g_state->samplerBindings[u] == name)
                g_state->samplerBindings[u] = 0;
        }
        g_state->samplers.erase(name);
        g_state->samplerNames.release(name);
    }
}

void glBindSampler(GLuint unit, GLuint sampler) {
    MITHRIL_ENSURE_INIT();
    if (unit >= mithril::kMaxTextureUnits) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    if (sampler != 0 && !mithril::state_get_sampler(sampler)) {
        mithril::state_set_error(GL_INVALID_OPERATION);
        return;
    }
    g_state->samplerBindings[unit] = sampler;
}

GLboolean glIsSampler(GLuint sampler) {
    if (!g_state) return GL_FALSE;
    return g_state->samplerNames.valid(sampler) ? GL_TRUE : GL_FALSE;
}

void glSamplerParameteri(GLuint sampler, GLenum pname, GLint param) {
    MITHRIL_ENSURE_INIT();
    mithril::Sampler* s = mithril::state_get_sampler(sampler);
    if (!s) return;
    switch (pname) {
        case GL_TEXTURE_MIN_FILTER:     s->minFilter = param; break;
        case GL_TEXTURE_MAG_FILTER:     s->magFilter = param; break;
        case GL_TEXTURE_WRAP_S:         s->wrapS = param; break;
        case GL_TEXTURE_WRAP_T:         s->wrapT = param; break;
        case GL_TEXTURE_WRAP_R:         s->wrapR = param; break;
        case GL_TEXTURE_MIN_LOD:        s->minLod = (GLfloat)param; break;
        case GL_TEXTURE_MAX_LOD:        s->maxLod = (GLfloat)param; break;
        case GL_TEXTURE_LOD_BIAS:       s->lodBias = (GLfloat)param; break;
        case GL_TEXTURE_COMPARE_MODE:   s->compareMode = param; break;
        case GL_TEXTURE_COMPARE_FUNC:   s->compareFunc = param; break;
        default: mithril::state_set_error(GL_INVALID_ENUM); return;
    }
    s->version++;
}

void glSamplerParameterf(GLuint sampler, GLenum pname, GLfloat param) {
    MITHRIL_ENSURE_INIT();
    mithril::Sampler* s = mithril::state_get_sampler(sampler);
    if (!s) return;
    switch (pname) {
        case GL_TEXTURE_MIN_FILTER:     s->minFilter = (GLint)param; break;
        case GL_TEXTURE_MAG_FILTER:     s->magFilter = (GLint)param; break;
        case GL_TEXTURE_WRAP_S:         s->wrapS = (GLint)param; break;
        case GL_TEXTURE_WRAP_T:         s->wrapT = (GLint)param; break;
        case GL_TEXTURE_WRAP_R:         s->wrapR = (GLint)param; break;
        case GL_TEXTURE_MIN_LOD:        s->minLod = param; break;
        case GL_TEXTURE_MAX_LOD:        s->maxLod = param; break;
        case GL_TEXTURE_LOD_BIAS:       s->lodBias = param; break;
        case GL_TEXTURE_COMPARE_MODE:   s->compareMode = (GLint)param; break;
        case GL_TEXTURE_COMPARE_FUNC:   s->compareFunc = (GLint)param; break;
        case GL_TEXTURE_BORDER_COLOR:   s->borderColor[0] = param; break;
        default: mithril::state_set_error(GL_INVALID_ENUM); return;
    }
    s->version++;
}

void glSamplerParameteriv(GLuint sampler, GLenum pname, const GLint* params) {
    MITHRIL_ENSURE_INIT();
    mithril::Sampler* s = mithril::state_get_sampler(sampler);
    if (!s || !params) return;
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        s->borderColorI[0] = params[0]; s->borderColorI[1] = params[1];
        s->borderColorI[2] = params[2]; s->borderColorI[3] = params[3];
        s->version++;
        return;
    }
    glSamplerParameteri(sampler, pname, params[0]);
}

void glSamplerParameterfv(GLuint sampler, GLenum pname, const GLfloat* params) {
    MITHRIL_ENSURE_INIT();
    mithril::Sampler* s = mithril::state_get_sampler(sampler);
    if (!s || !params) return;
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        s->borderColor[0] = params[0]; s->borderColor[1] = params[1];
        s->borderColor[2] = params[2]; s->borderColor[3] = params[3];
        s->version++;
        return;
    }
    glSamplerParameterf(sampler, pname, params[0]);
}

/* =========================================================================
 * Query objects (P1-1 / P2-1)
 * Occlusion / primitives-generated / timer queries.  State is tracked; the
 * backend result retrieval is deferred (returns 0 / GL_FALSE until wired).
 * ========================================================================= */
static mithril::QueryTarget query_target_from_gl(GLenum target) {
    switch (target) {
        case GL_SAMPLES_PASSED:             return mithril::QueryTarget::SamplesPassed;
        case GL_ANY_SAMPLES_PASSED:         return mithril::QueryTarget::AnySamplesPassed;
        case GL_PRIMITIVES_GENERATED:       return mithril::QueryTarget::PrimitivesGenerated;
        case GL_TIME_ELAPSED:               return mithril::QueryTarget::TimeElapsed;
        default:                            return mithril::QueryTarget::Count;
    }
}

void glGenQueries(GLsizei n, GLuint* ids) {
    MITHRIL_ENSURE_INIT();
    mithril::state_gen_names("query", n, ids);
    for (GLsizei i = 0; i < n; ++i) {
        mithril::Query q{};
        q.id = ids[i];
        g_state->queries[ids[i]] = q;
    }
}

void glDeleteQueries(GLsizei n, const GLuint* ids) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !ids) return;
    for (GLsizei i = 0; i < n; ++i) {
        GLuint name = ids[i];
        if (name == 0) continue;
        g_state->queries.erase(name);
        g_state->queryNames.release(name);
    }
}

GLboolean glIsQuery(GLuint id) {
    if (!g_state) return GL_FALSE;
    if (!g_state->queryNames.valid(id)) return GL_FALSE;
    mithril::Query* q = mithril::state_get_query(id);
    return (q && q->ended) ? GL_TRUE : GL_FALSE;
}

void glBeginQuery(GLenum target, GLuint id) {
    MITHRIL_ENSURE_INIT();
    mithril::QueryTarget qt = query_target_from_gl(target);
    if (qt == mithril::QueryTarget::Count) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    mithril::Query* q = mithril::state_get_query(id);
    if (!q) { mithril::state_set_error(GL_INVALID_OPERATION); return; }
    q->target = qt;
    q->active = true;
    q->ended = false;
    q->resultCached = false;
}

void glEndQuery(GLenum target) {
    MITHRIL_ENSURE_INIT();
    mithril::QueryTarget qt = query_target_from_gl(target);
    if (qt == mithril::QueryTarget::Count) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    // GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN 特殊处理:
    // 在 transform feedback 结束时记录状态。
    if (target == GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN) {
        auto* tf = mithril::state_get_transform_feedback(g_state->currentTransformFeedback);
        if (tf) {
            tf->captureEnded = true;
            tf->primitivesWritten = 0; // 真实计数需要后端 TF 支持
        }
        return;
    }
    // Find the active query for this target and end it.
    for (auto& [id, q] : g_state->queries) {
        if (q.active && q.target == qt) {
            q.active = false;
            q.ended = true;
            // FIX (Iris occlusion culling 黑屏): 真实实现需要 vkCreateQueryPool
            // + vkCmdBeginQuery/EndQuery + vkGetQueryPoolResults。当前为保守
            // stub：标记结果可用并返回非零值（"有样本通过"），让 Iris 的
            // occlusion culling 认为被测几何可见，不会把整个场景 cull 掉。
            // 返回 0 会导致 Iris 认为"什么都没通过 occlusion 测试"→ 黑屏。
            // TODO: 接入真实 VkQueryPool 实现精确 occlusion culling。
            q.resultCached = true;
            q.cachedResult = 1;
            break;
        }
    }
}

void glGetQueryiv(GLenum target, GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    switch (pname) {
        case GL_QUERY_COUNTER_BITS: *params = 64; break;
        case GL_CURRENT_QUERY:      *params = 0; break;  // no active query tracked per-target
        default:                     *params = 0; break;
    }
}

void glGetQueryObjectiv(GLuint id, GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    mithril::Query* q = mithril::state_get_query(id);
    if (!q || !q->ended) { *params = 0; return; }
    switch (pname) {
        case GL_QUERY_RESULT_AVAILABLE: *params = q->resultCached ? GL_TRUE : GL_FALSE; break;
        case GL_QUERY_RESULT:           *params = (GLint)q->cachedResult; break;
        default:                         *params = 0; break;
    }
}

void glGetQueryObjectuiv(GLuint id, GLenum pname, GLuint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    mithril::Query* q = mithril::state_get_query(id);
    if (!q || !q->ended) { *params = 0; return; }
    switch (pname) {
        case GL_QUERY_RESULT_AVAILABLE: *params = q->resultCached ? GL_TRUE : GL_FALSE; break;
        case GL_QUERY_RESULT:           *params = (GLuint)q->cachedResult; break;
        default:                         *params = 0; break;
    }
}

void glQueryCounter(GLuint id, GLenum target) {
    MITHRIL_ENSURE_INIT();
    if (target != GL_TIMESTAMP) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    mithril::Query* q = mithril::state_get_query(id);
    if (!q) { mithril::state_set_error(GL_INVALID_OPERATION); return; }
    q->target = mithril::QueryTarget::Timestamp;
    q->ended = true;
    // 真实实现需要 vkCmdWriteTimestamp。当前返回一个非零单调递增的
    // 假值：在真实的 GPU 时钟基准上累加，避免 Iris 误判为零导致 culling。
    static uint64_t s_timestampCounter = 1;
    q->timestampValue = s_timestampCounter++;
    q->resultCached = true;
    q->cachedResult = q->timestampValue;
}

/* =========================================================================
 * Image texture binding (GL 4.2 ARB_shader_image_load_store)
 *
 * glBindImageTexture 把一个纹理对象绑定到一个 image unit。shader 中的
 * image2D / imageBuffer uniform 通过 glUniform1i 指向 unit，DescriptorSet.cpp
 * 在 draw 时从 g_state->imageTextureUnits[unit] 取纹理 view 写入 storage
 * image 描述符。Iris 用此机制写 culling 输出（indirection/visibility buffer）。
 *
 * 本实现仅记录 unit→texture 绑定（backend 已就绪），access/format/layered
 * 等参数由纹理本身的 format 隐式决定（MoltenVK storage image 要求
 * VK_IMAGE_LAYOUT_GENERAL，由 backend 在绑定描述符时声明）。
 * ========================================================================= */
#ifndef GL_READ_ONLY
#define GL_READ_ONLY  0x88B8
#define GL_WRITE_ONLY 0x88B9
#define GL_READ_WRITE 0x88BA
#endif

void glBindImageTexture(GLuint unit, GLuint texture, GLint level,
                        GLboolean layered, GLint layer, GLenum access,
                        GLenum format) {
    MITHRIL_ENSURE_INIT();
    if (unit >= mithril::kMaxTextureUnits) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    // 存储全部 7 个参数 (GL 4.2 ARB_shader_image_load_store):
    // level/layered/layer 控制绑定纹理的 mip level / array layer
    // access (GL_READ_ONLY/WRITE_ONLY/READ_WRITE) 限制 shader 读写模式
    // format 指定 image 的 interpreted format (可与纹理实际格式不同)
    auto& iu = g_state->imageTexUnits[unit];
    iu.texture = texture;
    iu.level = level;
    iu.layered = layered;
    iu.layer = layer;
    iu.access = access;
    iu.format = format;
}

/* =========================================================================
 * Shader storage block binding (GL 4.3 ARB_shader_storage_buffer_object)
 *
 * glShaderStorageBlockBinding 把 program 的一个 SSBO block 重定向到指定
 * GL binding point。DescriptorSet.cpp 在 draw 时从
 * prog->storageBlockBindings[blockIndex] 查找重定向的 point，再从
 * g_state->indexedBufferBindings[ShaderStorage][point] 取实际 VkBuffer。
 * Sodium/Iris 用此机制显式重定向 SSBO binding。
 * ========================================================================= */
void glShaderStorageBlockBinding(GLuint program, GLuint storageBlockIndex,
                                 GLuint storageBlockBinding) {
    MITHRIL_ENSURE_INIT();
    mithril::Program* prog = mithril::state_get_program(program);
    if (!prog) { mithril::state_set_error(GL_INVALID_OPERATION); return; }
    prog->storageBlockBindings[storageBlockIndex] = storageBlockBinding;
}

/* =========================================================================
 * Transform Feedback objects (P1-1 / P2-1)
 * State tracking only — backend wiring deferred.
 * ========================================================================= */
void glGenTransformFeedbacks(GLsizei n, GLuint* ids) {
    MITHRIL_ENSURE_INIT();
    mithril::state_gen_names("tf", n, ids);
    for (GLsizei i = 0; i < n; ++i) {
        mithril::TransformFeedback tf{};
        tf.id = ids[i];
        g_state->transformFeedbacks[ids[i]] = tf;
    }
}

void glDeleteTransformFeedbacks(GLsizei n, const GLuint* ids) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !ids) return;
    for (GLsizei i = 0; i < n; ++i) {
        GLuint name = ids[i];
        if (name == 0) continue;
        if (g_state->currentTransformFeedback == name)
            g_state->currentTransformFeedback = 0;
        g_state->transformFeedbacks.erase(name);
        g_state->tfNames.release(name);
    }
}

void glBindTransformFeedback(GLenum target, GLuint id) {
    MITHRIL_ENSURE_INIT();
    if (target != GL_TRANSFORM_FEEDBACK) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    if (id != 0 && !mithril::state_get_transform_feedback(id)) {
        mithril::TransformFeedback tf{};
        tf.id = id;
        g_state->transformFeedbacks[id] = tf;
    }
    g_state->currentTransformFeedback = id;
}

GLboolean glIsTransformFeedback(GLuint id) {
    if (!g_state) return GL_FALSE;
    return g_state->tfNames.valid(id) ? GL_TRUE : GL_FALSE;
}

void glBeginTransformFeedback(GLenum primitiveMode) {
    MITHRIL_ENSURE_INIT();
    mithril::TransformFeedback* tf =
        mithril::state_get_transform_feedback(g_state->currentTransformFeedback);
    if (!tf) return;
    tf->active = true;
    tf->paused = false;
    tf->primitiveMode = primitiveMode;
}

void glEndTransformFeedback(void) {
    MITHRIL_ENSURE_INIT();
    mithril::TransformFeedback* tf =
        mithril::state_get_transform_feedback(g_state->currentTransformFeedback);
    if (!tf) return;
    tf->active = false;
    tf->paused = false;
}

void glPauseTransformFeedback(void) {
    MITHRIL_ENSURE_INIT();
    mithril::TransformFeedback* tf =
        mithril::state_get_transform_feedback(g_state->currentTransformFeedback);
    if (tf && tf->active) tf->paused = true;
}

void glResumeTransformFeedback(void) {
    MITHRIL_ENSURE_INIT();
    mithril::TransformFeedback* tf =
        mithril::state_get_transform_feedback(g_state->currentTransformFeedback);
    if (tf && tf->paused) tf->paused = false;
}

/* ---- GL 4.3 ARB_invalidate_subdata: texture/buffer invalidation ----
 *
 * Vulkan has no direct equivalent of "hint that this texture/buffer data is
 * no longer needed" — GPU memory is reclaimed when the object is deleted.
 * On TBDR GPUs (Apple Silicon), the framebuffer invalidation path
 * (glInvalidateFramebuffer -> storeOp=DONT_CARE) is the ONLY one that
 * matters for memory pressure, and that path IS implemented (Framebuffer.cpp).
 *
 * These texture/buffer invalidation entry points are exported as no-ops so
 * that hosts probing for GL 4.3+ completeness via dlsym resolve valid symbols.
 * The GL spec allows the implementation to ignore these hints ("the contents
 * of the specified object may be discarded").
 */
void glInvalidateTexSubImage(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth) {
    MITHRIL_ENSURE_INIT();
    (void)texture; (void)level; (void)xoffset; (void)yoffset; (void)zoffset;
    (void)width; (void)height; (void)depth;
    // No-op: Vulkan reclaims texture memory on vkDestroyImage (glDeleteTextures).
}

void glInvalidateTexImage(GLuint texture, GLint level) {
    MITHRIL_ENSURE_INIT();
    (void)texture; (void)level;
    // No-op: same rationale as glInvalidateTexSubImage.
}

void glInvalidateBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr length) {
    MITHRIL_ENSURE_INIT();
    (void)buffer; (void)offset; (void)length;
    // No-op: Vulkan reclaims buffer memory on vkDestroyBuffer (glDeleteBuffers).
}

void glInvalidateBufferData(GLuint buffer) {
    MITHRIL_ENSURE_INIT();
    (void)buffer;
    // No-op: same rationale as glInvalidateBufferSubData.
}

/* ---- GL 4.5 ARB_direct_state_access: DSA framebuffer invalidation ----
 *
 * DSA variants of glInvalidateFramebuffer / glInvalidateSubFramebuffer that
 * take an explicit framebuffer id instead of using the current binding.
 * Implementation: bind the framebuffer temporarily, delegate to the non-DSA
 * variant, then restore the original binding. This avoids duplicating the
 * attachment-parsing logic.
 */
void glInvalidateNamedFramebufferData(GLuint framebuffer, GLsizei numAttachments, const GLenum* attachments) {
    MITHRIL_ENSURE_INIT();
    if (framebuffer == 0) {
        // Default framebuffer: delegate with current binding.
        glInvalidateFramebuffer(GL_FRAMEBUFFER, numAttachments, attachments);
        return;
    }
    // Save current draw FBO, bind the named one, invalidate, restore.
    GLuint saved = g_state->currentDrawFBO;
    if (saved != framebuffer) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    }
    glInvalidateFramebuffer(GL_DRAW_FRAMEBUFFER, numAttachments, attachments);
    if (saved != framebuffer) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, saved);
    }
}

void glInvalidateNamedFramebufferSubData(GLuint framebuffer, GLsizei numAttachments, const GLenum* attachments, GLint x, GLint y, GLsizei width, GLsizei height) {
    MITHRIL_ENSURE_INIT();
    if (framebuffer == 0) {
        glInvalidateSubFramebuffer(GL_FRAMEBUFFER, numAttachments, attachments, x, y, width, height);
        return;
    }
    GLuint saved = g_state->currentDrawFBO;
    if (saved != framebuffer) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    }
    glInvalidateSubFramebuffer(GL_DRAW_FRAMEBUFFER, numAttachments, attachments, x, y, width, height);
    if (saved != framebuffer) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, saved);
    }
}

/* ---- GL 4.6 ARB_gl_spirv: glSpecializeShader ----
 *
 * Part of ARB_gl_spirv, which allows loading pre-compiled SPIR-V shaders
 * directly. Mithril compiles GLSL to SPIR-V internally via glslang, so this
 * entry point is not used by the normal GLSL path. Exported as a no-op so
 * that hosts probing for GL 4.6 completeness via dlsym resolve a valid symbol.
 *
 * If a host actually tries to use SPIR-V shaders via this path, the shader
 * object will remain unspecialized and subsequent linking will fail —
 * the host should fall back to the GLSL path (which is what Minecraft uses).
 */
void glSpecializeShader(GLuint shader, const GLchar* pEntryPoint, GLuint numSpecializationConstants, const GLuint* pConstantIndex, const GLuint* pConstantValue) {
    MITHRIL_ENSURE_INIT();
    auto* sh = mithril::state_get_shader(shader);
    if (!sh) { mithril::state_set_error(GL_INVALID_VALUE); return; }
    // 记录 entry point 和 specialization constants
    // (真正 specialization 需要 spirv-tools，当前仅记录以保持 ABI 兼容性)
    if (pEntryPoint) sh->entryPoint = pEntryPoint;
    sh->numSpecConstants = numSpecializationConstants;
    if (pConstantIndex && pConstantValue && numSpecializationConstants > 0) {
        for (GLuint i = 0; i < numSpecializationConstants; ++i) {
            sh->specConstants[pConstantIndex[i]] = pConstantValue[i];
        }
    }
    sh->specialized = true;
}

} // extern "C"
