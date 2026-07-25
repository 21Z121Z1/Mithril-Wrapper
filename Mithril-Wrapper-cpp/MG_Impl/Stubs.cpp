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
void glRasterPos2i(GLint, GLint) {}
void glRasterPos3f(GLfloat, GLfloat, GLfloat) {}
void glWindowPos2i(GLint, GLint) {}

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
void glRectd(GLdouble, GLdouble, GLdouble, GLdouble) {}
void glRectf(GLfloat, GLfloat, GLfloat, GLfloat) {}
void glRecti(GLint, GLint, GLint, GLint) {}

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
        t = mithril::state_get_texture(g_state->boundTextures[unit]);
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

void glGetTexParameteriv(GLenum, GLenum, GLint* params) { if (params) *params = 0; }
void glGetTexParameterfv(GLenum, GLenum, GLfloat* params) { if (params) *params = 0; }
void glGetTexImage(GLenum, GLint, GLenum, GLenum, void*) {}

GLboolean glAreTexturesResident(GLsizei, const GLuint*, GLboolean* residences) {
    if (residences) {
        // caller is responsible for sizing residences; mark all resident.
    }
    return GL_TRUE;
}

void glPrioritizeTextures(GLsizei, const GLuint*, const GLclampf*) {}

void glDepthBoundsEXT(GLclampd, GLclampd) {}

} // extern "C"
