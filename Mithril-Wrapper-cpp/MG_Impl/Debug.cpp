// Mithril-Wrapper - MG_Impl/Debug.cpp
// KHR_debug entry points (no-op stubs).
//
// GL_KHR_debug is advertised in the extension string, so host GL loaders
// (LWJGL) expect to dlsym these symbols. Minecraft 1.21.1's
// com.mojang.blaze3d.platform.GLX._init calls glDebugMessageControl during
// renderer init; if the symbol resolves to NULL, LWJGL's Checks.check() throws
// NullPointerException and crashes the game before it reaches the main menu.
//
// We don't produce real debug messages, so every entry point is a silent
// no-op. glGetDebugMessageLog returns 0 (no messages logged).
//
// This is the Vulkan/MoltenVK rewrite of the former gl/debug.cpp; the bodies
// are identical because the debug entry points do not depend on the backend.
#include "includes.h"

extern "C" {

void glDebugMessageControl(GLenum, GLenum, GLenum, GLsizei, const GLuint*, GLboolean) {
    MITHRIL_ENSURE_INIT();
    // No-op: we never emit debug messages, so filtering is irrelevant.
}

void glDebugMessageInsert(GLenum, GLenum, GLuint, GLenum, GLsizei, const GLchar*) {
    MITHRIL_ENSURE_INIT();
}

void glDebugMessageCallback(GLDEBUGPROC, const void*) {
    MITHRIL_ENSURE_INIT();
}

GLuint glGetDebugMessageLog(GLuint, GLsizei, GLenum*, GLenum*, GLuint*, GLenum*, GLsizei*, GLchar*) {
    MITHRIL_ENSURE_INIT();
    return 0; // no messages
}

void glPushDebugGroup(GLenum, GLuint, GLsizei, const GLchar*) {
    MITHRIL_ENSURE_INIT();
}

void glPopDebugGroup(void) {
    MITHRIL_ENSURE_INIT();
}

void glObjectLabel(GLenum, GLuint, GLsizei, const GLchar*) {
    MITHRIL_ENSURE_INIT();
}

void glGetObjectLabel(GLenum, GLuint, GLsizei, GLsizei* length, GLchar* label) {
    MITHRIL_ENSURE_INIT();
    if (length) *length = 0;
    if (label && length) label[0] = '\0';
}

void glObjectPtrLabel(const void*, GLsizei, const GLchar*) {
    MITHRIL_ENSURE_INIT();
}

void glGetObjectPtrLabel(const void*, GLsizei, GLsizei* length, GLchar* label) {
    MITHRIL_ENSURE_INIT();
    if (length) *length = 0;
    if (label && length) label[0] = '\0';
}

} // extern "C"
