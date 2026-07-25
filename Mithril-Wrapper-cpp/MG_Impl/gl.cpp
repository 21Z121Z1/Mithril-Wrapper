// Mithril-Wrapper - MG_Impl/gl.cpp
// Core state-control GL entry points: clear, enable/disable, viewport, blend,
// depth, stencil, rasterizer, pixel store, active texture, flush/finish.
//
// This is the Vulkan/MoltenVK rewrite of the former gl/gl.cpp. The Metal
// clear/render-pass/commit calls (metal_set_clear_color / metal_begin_render_pass
// / metal_commit / ...) are replaced with the Vulkan backend C API
// (backend_set_clear_color / backend_begin_render_pass / backend_commit / ...)
// declared in MG_Backend/Backend.h.
#include "includes.h"
#include "Framebuffer.h"

extern "C" {

/* ---- Clear ---- */
void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) {
    MITHRIL_ENSURE_INIT();
    g_state->clearColor[0] = r;
    g_state->clearColor[1] = g;
    g_state->clearColor[2] = b;
    g_state->clearColor[3] = a;
    backend_set_clear_color(r, g, b, a);
}

void glClearDepth(GLclampd d) {
    MITHRIL_ENSURE_INIT();
    g_state->clearDepth = d;
    backend_set_clear_depth(d);
}

void glClearDepthf(GLclampf d) {
    glClearDepth((GLclampd)d);
}

void glClearStencil(GLint s) {
    MITHRIL_ENSURE_INIT();
    g_state->clearStencil = s;
    backend_set_clear_stencil(s);
}

void glClear(GLbitfield mask) {
    MITHRIL_ENSURE_INIT();
    // Resolve current draw framebuffer attachments.
    VkImageView colors[8] = {VK_NULL_HANDLE};
    VkImageView depth = VK_NULL_HANDLE;
    int w = 0, h = 0;
    int n = mithril::collect_draw_fbo_attachments(colors, &depth, &w, &h);

    // Clear uses the Clear load action for the requested buffers.
    backend_set_load_clear();
    backend_begin_render_pass(colors, n, depth, w, h, 1);
    // Respect the mask: if only depth/stencil, the color attachment load
    // action is irrelevant (we still need a pass to clear depth). End
    // immediately — there is nothing to encode.
    backend_end_render_pass();
    backend_commit();
    (void)mask;
}

/* ---- Enable / Disable ---- */
void glEnable(GLenum cap) {
    MITHRIL_ENSURE_INIT();
    g_state->enabledCaps.insert(cap);
    switch (cap) {
        case GL_DEPTH_TEST:    g_state->depthTest = true; break;
        case GL_BLEND:         g_state->blend = true; break;
        case GL_STENCIL_TEST:  g_state->stencilTest = true; break;
        case GL_CULL_FACE:     g_state->cullFace = true; break;
        case GL_SCISSOR_TEST:  g_state->scissorTest = true; break;
        case GL_DITHER:        g_state->dither = true; break;
        case GL_MULTISAMPLE:   g_state->multisample = true; break;
        case GL_SAMPLE_ALPHA_TO_COVERAGE: g_state->sampleAlphaToCoverage = true; break;
        case GL_SAMPLE_COVERAGE:          g_state->sampleCoverage = true; break;
        case GL_POLYGON_OFFSET_FILL:      g_state->polygonOffsetFill = true; break;
        case GL_PROGRAM_POINT_SIZE:       g_state->programPointSize = true; break;
        case GL_PRIMITIVE_RESTART:        g_state->primitiveRestart = true; break;
        case GL_FRAMEBUFFER_SRGB:         g_state->framebufferSRGB = true; break;
        default: break;
    }
}

void glDisable(GLenum cap) {
    MITHRIL_ENSURE_INIT();
    g_state->enabledCaps.erase(cap);
    switch (cap) {
        case GL_DEPTH_TEST:    g_state->depthTest = false; break;
        case GL_BLEND:         g_state->blend = false; break;
        case GL_STENCIL_TEST:  g_state->stencilTest = false; break;
        case GL_CULL_FACE:     g_state->cullFace = false; break;
        case GL_SCISSOR_TEST:  g_state->scissorTest = false; break;
        case GL_DITHER:        g_state->dither = false; break;
        case GL_MULTISAMPLE:   g_state->multisample = false; break;
        case GL_SAMPLE_ALPHA_TO_COVERAGE: g_state->sampleAlphaToCoverage = false; break;
        case GL_SAMPLE_COVERAGE:          g_state->sampleCoverage = false; break;
        case GL_POLYGON_OFFSET_FILL:      g_state->polygonOffsetFill = false; break;
        case GL_PROGRAM_POINT_SIZE:       g_state->programPointSize = false; break;
        case GL_PRIMITIVE_RESTART:        g_state->primitiveRestart = false; break;
        case GL_FRAMEBUFFER_SRGB:         g_state->framebufferSRGB = false; break;
        default: break;
    }
}

GLboolean glIsEnabled(GLenum cap) {
    MITHRIL_ENSURE_INIT();
    return g_state->enabledCaps.count(cap) ? GL_TRUE : GL_FALSE;
}

void glEnablei(GLenum cap, GLuint index) { (void)index; glEnable(cap); }
void glDisablei(GLenum cap, GLuint index) { (void)index; glDisable(cap); }
GLboolean glIsEnabledi(GLenum cap, GLuint index) { (void)index; return glIsEnabled(cap); }

/* ---- Viewport / scissor / depth range ---- */
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    MITHRIL_ENSURE_INIT();
    g_state->viewportX = x;
    g_state->viewportY = y;
    g_state->viewportW = w;
    g_state->viewportH = h;
}

void glDepthRange(GLclampd n, GLclampd f) {
    MITHRIL_ENSURE_INIT();
    g_state->depthNear = n;
    g_state->depthFar  = f;
}

void glDepthRangef(GLclampf n, GLclampf f) { glDepthRange((GLclampd)n, (GLclampd)f); }

void glScissor(GLint x, GLint y, GLsizei w, GLsizei h) {
    MITHRIL_ENSURE_INIT();
    g_state->scissorX = x;
    g_state->scissorY = y;
    g_state->scissorW = w;
    g_state->scissorH = h;
}

/* ---- Blend ---- */
void glBlendFunc(GLenum sf, GLenum df) {
    MITHRIL_ENSURE_INIT();
    g_state->blendSrcRGB = g_state->blendSrcA = sf;
    g_state->blendDstRGB = g_state->blendDstA = df;
}

void glBlendFuncSeparate(GLenum sRGB, GLenum dRGB, GLenum sA, GLenum dA) {
    MITHRIL_ENSURE_INIT();
    g_state->blendSrcRGB = sRGB; g_state->blendDstRGB = dRGB;
    g_state->blendSrcA   = sA;   g_state->blendDstA   = dA;
}

void glBlendEquation(GLenum mode) {
    MITHRIL_ENSURE_INIT();
    g_state->blendEqRGB = g_state->blendEqA = mode;
}

void glBlendEquationSeparate(GLenum mRGB, GLenum mA) {
    MITHRIL_ENSURE_INIT();
    g_state->blendEqRGB = mRGB;
    g_state->blendEqA   = mA;
}

void glBlendColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) {
    MITHRIL_ENSURE_INIT();
    g_state->blendColor[0] = r;
    g_state->blendColor[1] = g;
    g_state->blendColor[2] = b;
    g_state->blendColor[3] = a;
}

void glBlendFunci(GLuint buf, GLenum src, GLenum dst) { (void)buf; glBlendFunc(src, dst); }
void glBlendFuncSeparatei(GLuint buf, GLenum sR, GLenum dR, GLenum sA, GLenum dA) {
    (void)buf; glBlendFuncSeparate(sR, dR, sA, dA);
}
void glBlendEquationi(GLuint buf, GLenum mode) { (void)buf; glBlendEquation(mode); }

/* ---- Depth / stencil / color mask ---- */
void glDepthFunc(GLenum func) {
    MITHRIL_ENSURE_INIT();
    g_state->depthFunc = func;
}

void glDepthMask(GLboolean flag) {
    MITHRIL_ENSURE_INIT();
    g_state->depthMask = (flag != 0);
}

void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    MITHRIL_ENSURE_INIT();
    g_state->colorMask[0] = (r != 0);
    g_state->colorMask[1] = (g != 0);
    g_state->colorMask[2] = (b != 0);
    g_state->colorMask[3] = (a != 0);
}

void glStencilMask(GLuint mask) {
    MITHRIL_ENSURE_INIT();
    g_state->stencilMask = mask;
    g_state->stencilBackMask = mask;
}

void glStencilFunc(GLenum func, GLint ref, GLuint mask) {
    MITHRIL_ENSURE_INIT();
    g_state->stencilFunc = func;
    g_state->stencilRef  = ref;
    g_state->stencilValueMask = mask;
    g_state->stencilBackFunc = func;
    g_state->stencilBackRef  = ref;
    g_state->stencilBackValueMask = mask;
}

void glStencilOp(GLenum sfail, GLenum dpfail, GLenum dppass) {
    MITHRIL_ENSURE_INIT();
    g_state->stencilSfail  = sfail;
    g_state->stencilDpfail = dpfail;
    g_state->stencilDppass = dppass;
    g_state->stencilBackSfail  = sfail;
    g_state->stencilBackDpfail = dpfail;
    g_state->stencilBackDppass = dppass;
}

void glStencilMaskSeparate(GLenum face, GLuint mask) {
    MITHRIL_ENSURE_INIT();
    if (face == GL_FRONT) g_state->stencilMask = mask;
    else if (face == GL_BACK) g_state->stencilBackMask = mask;
    else { g_state->stencilMask = mask; g_state->stencilBackMask = mask; }
}

void glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask) {
    MITHRIL_ENSURE_INIT();
    if (face == GL_FRONT || face == GL_FRONT_AND_BACK) {
        g_state->stencilFunc = func;
        g_state->stencilRef  = ref;
        g_state->stencilValueMask = mask;
    }
    if (face == GL_BACK || face == GL_FRONT_AND_BACK) {
        g_state->stencilBackFunc = func;
        g_state->stencilBackRef  = ref;
        g_state->stencilBackValueMask = mask;
    }
}

void glStencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass) {
    MITHRIL_ENSURE_INIT();
    if (face == GL_FRONT || face == GL_FRONT_AND_BACK) {
        g_state->stencilSfail = sfail;
        g_state->stencilDpfail = dpfail;
        g_state->stencilDppass = dppass;
    }
    if (face == GL_BACK || face == GL_FRONT_AND_BACK) {
        g_state->stencilBackSfail = sfail;
        g_state->stencilBackDpfail = dpfail;
        g_state->stencilBackDppass = dppass;
    }
}

/* ---- Rasterizer ---- */
void glCullFace(GLenum mode) {
    MITHRIL_ENSURE_INIT();
    g_state->cullMode = mode;
}

void glFrontFace(GLenum mode) {
    MITHRIL_ENSURE_INIT();
    g_state->frontFace = mode;
}

void glPolygonMode(GLenum face, GLenum mode) {
    MITHRIL_ENSURE_INIT();
    if (face == GL_FRONT || face == GL_FRONT_AND_BACK) g_state->polygonModeFront = mode;
    if (face == GL_BACK  || face == GL_FRONT_AND_BACK) g_state->polygonModeBack  = mode;
}

void glPolygonOffset(GLfloat factor, GLfloat units) {
    MITHRIL_ENSURE_INIT();
    g_state->polygonOffsetFactor = factor;
    g_state->polygonOffsetUnits  = units;
}

void glLineWidth(GLfloat w) { MITHRIL_ENSURE_INIT(); g_state->lineWidth = w; }
void glPointSize(GLfloat s) { MITHRIL_ENSURE_INIT(); g_state->pointSize = s; }
void glHint(GLenum, GLenum) { MITHRIL_ENSURE_INIT(); /* hints are advisory */ }

/* ---- Pixel store ---- */
void glPixelStorei(GLenum pname, GLint param) {
    MITHRIL_ENSURE_INIT();
    switch (pname) {
        case GL_UNPACK_ALIGNMENT:      g_state->unpackAlignment = param; break;
        case GL_PACK_ALIGNMENT:        g_state->packAlignment   = param; break;
        case GL_UNPACK_ROW_LENGTH:     g_state->unpackRowLength = param; break;
        case GL_UNPACK_IMAGE_HEIGHT:   g_state->unpackImageHeight = param; break;
        case GL_UNPACK_SKIP_ROWS:      g_state->unpackSkipRows = param; break;
        case GL_UNPACK_SKIP_PIXELS:    g_state->unpackSkipPixels = param; break;
        case GL_UNPACK_SKIP_IMAGES:    g_state->unpackSkipImages = param; break;
        default: break;
    }
}

void glPixelStoref(GLenum pname, GLfloat param) { glPixelStorei(pname, (GLint)param); }

/* ---- Active texture ---- */
void glActiveTexture(GLenum texture) {
    MITHRIL_ENSURE_INIT();
    if (texture >= GL_TEXTURE0 && texture < GL_TEXTURE0 + mithril::kMaxTextureUnits) {
        g_state->activeTextureUnit = texture - GL_TEXTURE0;
    }
}

/* ---- Flush / finish ---- */
void glFlush(void) {
    MITHRIL_ENSURE_INIT();
    backend_end_render_pass();
    backend_commit();
}

void glFinish(void) {
    MITHRIL_ENSURE_INIT();
    backend_end_render_pass();
    backend_commit();
}

void glPrimitiveRestartIndex(GLuint index) {
    MITHRIL_ENSURE_INIT();
    g_state->primitiveRestartIndex = index;
}

} // extern "C"
