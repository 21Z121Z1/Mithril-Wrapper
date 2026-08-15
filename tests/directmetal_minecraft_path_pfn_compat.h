#ifndef MITHRIL_TEST_DIRECTMETAL_MINECRAFT_PATH_PFN_COMPAT_H
#define MITHRIL_TEST_DIRECTMETAL_MINECRAFT_PATH_PFN_COMPAT_H

/* Mithril's focused glcorearb.h intentionally declares the GL API surface but
 * does not carry Khronos PFNGL*PROC typedefs. Keep this test-local compatibility
 * layer out of the shipping headers. */
#include <GL/glcorearb.h>

typedef const GLubyte* (*PFNGLGETSTRINGPROC)(GLenum name);
typedef GLenum (*PFNGLGETERRORPROC)(void);
typedef void (*PFNGLFINISHPROC)(void);
typedef void (*PFNGLVIEWPORTPROC)(GLint x, GLint y, GLsizei width, GLsizei height);
typedef void (*PFNGLDISABLEPROC)(GLenum cap);
typedef void (*PFNGLCLEARCOLORPROC)(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
typedef void (*PFNGLCLEARPROC)(GLbitfield mask);
typedef void (*PFNGLREADPIXELSPROC)(GLint x, GLint y, GLsizei width, GLsizei height,
                                    GLenum format, GLenum type, void* data);
typedef void (*PFNGLGENTEXTURESPROC)(GLsizei n, GLuint* textures);
typedef void (*PFNGLBINDTEXTUREPROC)(GLenum target, GLuint texture);
typedef void (*PFNGLTEXIMAGE2DPROC)(GLenum target, GLint level, GLint internalformat,
                                    GLsizei width, GLsizei height, GLint border,
                                    GLenum format, GLenum type, const void* pixels);
typedef void (*PFNGLTEXPARAMETERIPROC)(GLenum target, GLenum pname, GLint param);
typedef void (*PFNGLGENFRAMEBUFFERSPROC)(GLsizei n, GLuint* framebuffers);
typedef void (*PFNGLBINDFRAMEBUFFERPROC)(GLenum target, GLuint framebuffer);
typedef void (*PFNGLFRAMEBUFFERTEXTURE2DPROC)(GLenum target, GLenum attachment,
                                              GLenum textarget, GLuint texture, GLint level);
typedef GLenum (*PFNGLCHECKFRAMEBUFFERSTATUSPROC)(GLenum target);
typedef void (*PFNGLBLITFRAMEBUFFERPROC)(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                                         GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                                         GLbitfield mask, GLenum filter);
typedef GLuint (*PFNGLCREATESHADERPROC)(GLenum type);
typedef void (*PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count,
                                      const GLchar* const* string, const GLint* length);
typedef void (*PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef void (*PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint* params);
typedef void (*PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei bufSize,
                                          GLsizei* length, GLchar* infoLog);
typedef GLuint (*PFNGLCREATEPROGRAMPROC)(void);
typedef void (*PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void (*PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void (*PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint* params);
typedef void (*PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei bufSize,
                                           GLsizei* length, GLchar* infoLog);
typedef void (*PFNGLUSEPROGRAMPROC)(GLuint program);
typedef void (*PFNGLDELETESHADERPROC)(GLuint shader);
typedef void (*PFNGLGENVERTEXARRAYSPROC)(GLsizei n, GLuint* arrays);
typedef void (*PFNGLBINDVERTEXARRAYPROC)(GLuint array);
typedef void (*PFNGLGENBUFFERSPROC)(GLsizei n, GLuint* buffers);
typedef void (*PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);
typedef void (*PFNGLBUFFERDATAPROC)(GLenum target, GLsizeiptr size, const void* data, GLenum usage);
typedef void (*PFNGLVERTEXATTRIBPOINTERPROC)(GLuint index, GLint size, GLenum type,
                                             GLboolean normalized, GLsizei stride,
                                             const void* pointer);
typedef void (*PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint index);
typedef void (*PFNGLMULTIDRAWELEMENTSINDIRECTPROC)(GLenum mode, GLenum type,
                                                   const void* indirect,
                                                   GLsizei drawcount, GLsizei stride);

#endif
