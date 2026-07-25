#ifndef __gl_h_
#define __gl_h_

/*
 * Mithril-Wrapper: OpenGL header.
 * Pulls in the Core Profile surface (glcorearb.h) and additionally declares the
 * legacy fixed-function entry points that are provided as no-op stubs so that
 * applications still dlsym-ing GL 1.x/2.x symbols resolve cleanly.
 */

#include <KHR/khrplatform.h>
#include "glcorearb.h"

/*
 * Texture level parameter constants used by glGetTexLevelParameteriv and
 * glTexImage2D(GL_PROXY_TEXTURE_2D). These are standard GL values but
 * missing from our minimal glcorearb.h.
 */
#ifndef GL_PROXY_TEXTURE_2D
#define GL_PROXY_TEXTURE_2D          0x8064
#endif
#ifndef GL_TEXTURE_WIDTH
#define GL_TEXTURE_WIDTH             0x1000
#endif
#ifndef GL_TEXTURE_HEIGHT
#define GL_TEXTURE_HEIGHT            0x1001
#endif
#ifndef GL_TEXTURE_DEPTH
#define GL_TEXTURE_DEPTH             0x8071
#endif
#ifndef GL_TEXTURE_INTERNAL_FORMAT
#define GL_TEXTURE_INTERNAL_FORMAT   0x1003
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Legacy fixed-function (stubbed, see src/stubs.cpp) ---- */

/* Miscellaneous legacy */
GLAPI void GLAPIENTRY glClearIndex(GLfloat c);
GLAPI void GLAPIENTRY glIndexMask(GLuint mask);
GLAPI void GLAPIENTRY glAlphaFunc(GLenum func, GLclampf ref);
GLAPI void GLAPIENTRY glLogicOp(GLenum opcode);
GLAPI void GLAPIENTRY glLineStipple(GLint factor, GLushort pattern);
GLAPI void GLAPIENTRY glPolygonStipple(const GLubyte* mask);
GLAPI void GLAPIENTRY glGetPolygonStipple(GLubyte* mask);
GLAPI void GLAPIENTRY glEdgeFlag(GLboolean flag);
GLAPI void GLAPIENTRY glEdgeFlagv(const GLboolean* flag);
GLAPI void GLAPIENTRY glClipPlane(GLenum plane, const GLdouble* equation);
GLAPI void GLAPIENTRY glGetClipPlane(GLenum plane, GLdouble* equation);

/* Accumulation buffer */
GLAPI void GLAPIENTRY glClearAccum(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
GLAPI void GLAPIENTRY glAccum(GLenum op, GLfloat value);

/* Transformation (matrix stack) */
GLAPI void GLAPIENTRY glMatrixMode(GLenum mode);
GLAPI void GLAPIENTRY glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble near_val, GLdouble far_val);
GLAPI void GLAPIENTRY glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble near_val, GLdouble far_val);
GLAPI void GLAPIENTRY glPushMatrix(void);
GLAPI void GLAPIENTRY glPopMatrix(void);
GLAPI void GLAPIENTRY glLoadIdentity(void);
GLAPI void GLAPIENTRY glLoadMatrixd(const GLdouble* m);
GLAPI void GLAPIENTRY glLoadMatrixf(const GLfloat* m);
GLAPI void GLAPIENTRY glMultMatrixd(const GLdouble* m);
GLAPI void GLAPIENTRY glMultMatrixf(const GLfloat* m);
GLAPI void GLAPIENTRY glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z);
GLAPI void GLAPIENTRY glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
GLAPI void GLAPIENTRY glScaled(GLdouble x, GLdouble y, GLdouble z);
GLAPI void GLAPIENTRY glScalef(GLfloat x, GLfloat y, GLfloat z);
GLAPI void GLAPIENTRY glTranslated(GLdouble x, GLdouble y, GLdouble z);
GLAPI void GLAPIENTRY glTranslatef(GLfloat x, GLfloat y, GLfloat z);

/* Display lists */
GLAPI GLboolean GLAPIENTRY glIsList(GLuint list);
GLAPI void GLAPIENTRY glDeleteLists(GLuint list, GLsizei range);
GLAPI GLuint GLAPIENTRY glGenLists(GLsizei range);
GLAPI void GLAPIENTRY glNewList(GLuint list, GLenum mode);
GLAPI void GLAPIENTRY glEndList(void);
GLAPI void GLAPIENTRY glCallList(GLuint list);
GLAPI void GLAPIENTRY glCallLists(GLsizei n, GLenum type, const GLvoid* lists);
GLAPI void GLAPIENTRY glListBase(GLuint base);

/* Immediate mode */
GLAPI void GLAPIENTRY glBegin(GLenum mode);
GLAPI void GLAPIENTRY glEnd(void);
GLAPI void GLAPIENTRY glVertex2d(GLdouble x, GLdouble y);
GLAPI void GLAPIENTRY glVertex2f(GLfloat x, GLfloat y);
GLAPI void GLAPIENTRY glVertex2i(GLint x, GLint y);
GLAPI void GLAPIENTRY glVertex3f(GLfloat x, GLfloat y, GLfloat z);
GLAPI void GLAPIENTRY glVertex3fv(const GLfloat* v);
GLAPI void GLAPIENTRY glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w);
GLAPI void GLAPIENTRY glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz);
GLAPI void GLAPIENTRY glColor3f(GLfloat red, GLfloat green, GLfloat blue);
GLAPI void GLAPIENTRY glColor3ub(GLubyte r, GLubyte g, GLubyte b);
GLAPI void GLAPIENTRY glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
GLAPI void GLAPIENTRY glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a);
GLAPI void GLAPIENTRY glTexCoord2f(GLfloat s, GLfloat t);
GLAPI void GLAPIENTRY glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q);
GLAPI void GLAPIENTRY glRasterPos2i(GLint x, GLint y);
GLAPI void GLAPIENTRY glRasterPos3f(GLfloat x, GLfloat y, GLfloat z);
GLAPI void GLAPIENTRY glWindowPos2i(GLint x, GLint y);

/* Lighting / material / fog */
GLAPI void GLAPIENTRY glShadeModel(GLenum mode);
GLAPI void GLAPIENTRY glMaterialf(GLenum face, GLenum pname, GLfloat param);
GLAPI void GLAPIENTRY glMaterialfv(GLenum face, GLenum pname, const GLfloat* params);
GLAPI void GLAPIENTRY glMateriali(GLenum face, GLenum pname, GLint param);
GLAPI void GLAPIENTRY glLightf(GLenum light, GLenum pname, GLfloat param);
GLAPI void GLAPIENTRY glLightfv(GLenum light, GLenum pname, const GLfloat* params);
GLAPI void GLAPIENTRY glLightModelf(GLenum pname, GLfloat param);
GLAPI void GLAPIENTRY glLightModelfv(GLenum pname, const GLfloat* params);
GLAPI void GLAPIENTRY glFogf(GLenum pname, GLfloat param);
GLAPI void GLAPIENTRY glFogi(GLenum pname, GLint param);
GLAPI void GLAPIENTRY glFogfv(GLenum pname, const GLfloat* params);
GLAPI void GLAPIENTRY glFogiv(GLenum pname, const GLint* params);
GLAPI void GLAPIENTRY glColorMaterial(GLenum face, GLenum mode);

/* Tex gen / env */
GLAPI void GLAPIENTRY glTexGend(GLenum coord, GLenum pname, GLdouble param);
GLAPI void GLAPIENTRY glTexGenf(GLenum coord, GLenum pname, GLfloat param);
GLAPI void GLAPIENTRY glTexGeni(GLenum coord, GLenum pname, GLint param);
GLAPI void GLAPIENTRY glTexGenfv(GLenum coord, GLenum pname, const GLfloat* params);
GLAPI void GLAPIENTRY glTexGeniv(GLenum coord, GLenum pname, const GLint* params);
GLAPI void GLAPIENTRY glTexEnvf(GLenum target, GLenum pname, GLfloat param);
GLAPI void GLAPIENTRY glTexEnvi(GLenum target, GLenum pname, GLint param);
GLAPI void GLAPIENTRY glTexEnvfv(GLenum target, GLenum pname, const GLfloat* params);
GLAPI void GLAPIENTRY glTexEnviv(GLenum target, GLenum pname, const GLint* params);

/* Pixel transfer / copy */
GLAPI void GLAPIENTRY glPixelTransferf(GLenum pname, GLfloat param);
GLAPI void GLAPIENTRY glPixelTransferi(GLenum pname, GLint param);
GLAPI void GLAPIENTRY glPixelMapfv(GLenum map, GLsizei mapsize, const GLfloat* values);
GLAPI void GLAPIENTRY glPixelMapuiv(GLenum map, GLsizei mapsize, const GLuint* values);
GLAPI void GLAPIENTRY glPixelMapusv(GLenum map, GLsizei mapsize, const GLushort* values);
GLAPI void GLAPIENTRY glPixelZoom(GLfloat xfactor, GLfloat yfactor);
GLAPI void GLAPIENTRY glCopyPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum type);

/* Selection / feedback */
GLAPI void GLAPIENTRY glInitNames(void);
GLAPI void GLAPIENTRY glLoadName(GLuint name);
GLAPI void GLAPIENTRY glPushName(GLuint name);
GLAPI void GLAPIENTRY glPopName(void);
GLAPI GLint GLAPIENTRY glRenderMode(GLenum mode);
GLAPI void GLAPIENTRY glSelectBuffer(GLsizei size, GLuint* buffer);
GLAPI void GLAPIENTRY glFeedbackBuffer(GLsizei size, GLenum type, GLfloat* buffer);
GLAPI void GLAPIENTRY glPassThrough(GLfloat token);

/* Evaluator */
GLAPI void GLAPIENTRY glMap1d(GLenum target, GLdouble u1, GLdouble u2, GLint stride, GLint order, const GLdouble* points);
GLAPI void GLAPIENTRY glMap2d(GLenum target, GLdouble u1, GLdouble u2, GLint ustride, GLint uorder, GLdouble v1, GLdouble v2, GLint vstride, GLint vorder, const GLdouble* points);
GLAPI void GLAPIENTRY glEvalCoord1d(GLdouble u);
GLAPI void GLAPIENTRY glEvalCoord2d(GLdouble u, GLdouble v);
GLAPI void GLAPIENTRY glMapGrid1d(GLint un, GLdouble u1, GLdouble u2);
GLAPI void GLAPIENTRY glMapGrid2d(GLint un, GLdouble u1, GLdouble u2, GLint vn, GLdouble v1, GLdouble v2);
GLAPI void GLAPIENTRY glEvalMesh1(GLenum mode, GLint i1, GLint i2);
GLAPI void GLAPIENTRY glEvalMesh2(GLenum mode, GLint i1, GLint i2, GLint j1, GLint j2);
GLAPI void GLAPIENTRY glEvalPoint1(GLint i);
GLAPI void GLAPIENTRY glEvalPoint2(GLint i, GLint j);

/* Rect */
GLAPI void GLAPIENTRY glRectd(GLdouble x1, GLdouble y1, GLdouble x2, GLdouble y2);
GLAPI void GLAPIENTRY glRectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2);
GLAPI void GLAPIENTRY glRecti(GLint x1, GLint y1, GLint x2, GLint y2);

/* Attrib stack */
GLAPI void GLAPIENTRY glPushAttrib(GLbitfield mask);
GLAPI void GLAPIENTRY glPopAttrib(void);
GLAPI void GLAPIENTRY glPushClientAttrib(GLbitfield mask);
GLAPI void GLAPIENTRY glPopClientAttrib(void);

/* Client vertex arrays (legacy) */
GLAPI void GLAPIENTRY glEnableClientState(GLenum cap);
GLAPI void GLAPIENTRY glDisableClientState(GLenum cap);
GLAPI void GLAPIENTRY glVertexPointer(GLint size, GLenum type, GLsizei stride, const void* pointer);
GLAPI void GLAPIENTRY glNormalPointer(GLenum type, GLsizei stride, const void* pointer);
GLAPI void GLAPIENTRY glColorPointer(GLint size, GLenum type, GLsizei stride, const void* pointer);
GLAPI void GLAPIENTRY glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void* pointer);
GLAPI void GLAPIENTRY glIndexPointer(GLenum type, GLsizei stride, const void* pointer);
GLAPI void GLAPIENTRY glEdgeFlagPointer(GLsizei stride, const void* pointer);
GLAPI void GLAPIENTRY glInterleavedArrays(GLenum format, GLsizei stride, const void* pointer);
GLAPI void GLAPIENTRY glArrayElement(GLint i);

/* Misc legacy getters/queries */
GLAPI void GLAPIENTRY glGetDoublev(GLenum pname, GLdouble* params);  /* also legacy-compat */
GLAPI void GLAPIENTRY glGetPointerv(GLenum pname, void** params);
GLAPI void GLAPIENTRY glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint* params);
GLAPI void GLAPIENTRY glGetTexParameteriv(GLenum target, GLenum pname, GLint* params);
GLAPI void GLAPIENTRY glGetTexParameterfv(GLenum target, GLenum pname, GLfloat* params);
GLAPI void GLAPIENTRY glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, void* pixels);
GLAPI GLboolean GLAPIENTRY glAreTexturesResident(GLsizei n, const GLuint* textures, GLboolean* residences);
GLAPI void GLAPIENTRY glPrioritizeTextures(GLsizei n, const GLuint* textures, const GLclampf* priorities);
GLAPI void GLAPIENTRY glDepthBoundsEXT(GLclampd zmin, GLclampd zmax);

#ifdef __cplusplus
}
#endif

#endif /* __gl_h_ */
