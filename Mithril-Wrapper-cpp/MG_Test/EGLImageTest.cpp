// Mithril-Wrapper - MG_Test/EGLImageTest.cpp
// Unit tests for the EGL 1.5 Image API shadow implementation in egl/egl.cpp.
//
// eglCreateImage / eglDestroyImage in Mithril-Wrapper are a *shadow*
// implementation: they record the (target, buffer) pair in a process-local
// handle table (g_images) but do NOT import a real VkImage. Real interop
// will land with the Vulkan Image bind. This file exercises the state-layer
// behaviour only:
//   - happy-path create + destroy round trip
//   - rejection of unknown targets (EGL_BAD_PARAMETER)
//   - rejection of unknown image handles on destroy (EGL_BAD_PARAMETER)
//   - rejection of EGL_NO_DISPLAY (EGL_BAD_DISPLAY)
//   - acceptance of all 4 spec-defined targets
//
// A valid EGLDisplay is obtained via eglGetDisplay(EGL_DEFAULT_DISPLAY), which
// returns the singleton g_display. valid_display() in egl.cpp checks pointer
// identity only (not g_display.initialized), so eglInitialize is NOT required
// for the shadow image API to accept the display handle. The ctx parameter is
// explicitly ignored by eglCreateImage ((void)ctx), so EGL_NO_CONTEXT is used
// throughout — no real GL context needs to exist.
#include <gtest/gtest.h>

#include <EGL/egl.h>

#include <cstdint>

namespace {

// Obtain the singleton EGLDisplay that valid_display() accepts. We do NOT
// call eglInitialize — valid_display() checks pointer identity only, so the
// display handle returned by eglGetDisplay(EGL_DEFAULT_DISPLAY) is sufficient
// for the shadow image API.
EGLDisplay GetTestDisplay() {
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EXPECT_NE(dpy, EGL_NO_DISPLAY);
    return dpy;
}

} // namespace

// Happy path: eglCreateImage with EGL_GL_TEXTURE_2D returns a non-zero
// handle; eglDestroyImage on that handle returns EGL_TRUE and clears the
// entry. The ctx parameter is ignored by the shadow implementation, so
// EGL_NO_CONTEXT works; textureId is an arbitrary integer cast to
// EGLClientBuffer.
TEST(EGLImage, CreateAndDestroyShadowImage) {
    EGLDisplay dpy = GetTestDisplay();

    const uintptr_t textureId = 42;
    EGLImage image = eglCreateImage(dpy, EGL_NO_CONTEXT, EGL_GL_TEXTURE_2D,
                                   (EGLClientBuffer)textureId, NULL);
    EXPECT_NE(image, EGL_NO_IMAGE);
    EXPECT_EQ(eglGetError(), EGL_SUCCESS);

    EXPECT_EQ(eglDestroyImage(dpy, image), EGL_TRUE);
    EXPECT_EQ(eglGetError(), EGL_SUCCESS);
}

// Unknown target -> EGL_NO_IMAGE + EGL_BAD_PARAMETER. 0x9999 is not one of
// EGL_GL_TEXTURE_2D / _3D / _CUBE_MAP_POSITIVE_X / _RENDERBUFFER.
TEST(EGLImage, RejectInvalidTarget) {
    EGLDisplay dpy = GetTestDisplay();

    EGLImage image = eglCreateImage(dpy, EGL_NO_CONTEXT, (EGLenum)0x9999,
                                   (EGLClientBuffer)nullptr, NULL);
    EXPECT_EQ(image, EGL_NO_IMAGE);
    EXPECT_EQ(eglGetError(), EGL_BAD_PARAMETER);
}

// Destroying a handle that was never created (or already destroyed) returns
// EGL_FALSE and sets EGL_BAD_PARAMETER — the g_images table lookup misses.
TEST(EGLImage, DestroyUnknownImage) {
    EGLDisplay dpy = GetTestDisplay();

    EXPECT_EQ(eglDestroyImage(dpy, (EGLImage)(uintptr_t)0xdeadbeef), EGL_FALSE);
    EXPECT_EQ(eglGetError(), EGL_BAD_PARAMETER);
}

// EGL_NO_DISPLAY is rejected before any image-state lookup, with
// EGL_BAD_DISPLAY (not EGL_BAD_PARAMETER). Both eglCreateImage and
// eglDestroyImage take the same early-out path on valid_display() failure.
TEST(EGLImage, InvalidDisplay) {
    EGLImage image = eglCreateImage(EGL_NO_DISPLAY, EGL_NO_CONTEXT,
                                    EGL_GL_TEXTURE_2D,
                                    (EGLClientBuffer)(uintptr_t)1, NULL);
    EXPECT_EQ(image, EGL_NO_IMAGE);
    EXPECT_EQ(eglGetError(), EGL_BAD_DISPLAY);

    EXPECT_EQ(eglDestroyImage(EGL_NO_DISPLAY, (EGLImage)(uintptr_t)1),
              EGL_FALSE);
    EXPECT_EQ(eglGetError(), EGL_BAD_DISPLAY);
}

// All 4 spec-accepted targets (EGL_GL_TEXTURE_2D, EGL_GL_TEXTURE_3D,
// EGL_GL_TEXTURE_CUBE_MAP_POSITIVE_X, EGL_GL_RENDERBUFFER) produce a valid
// image handle, and each is independently destroyable.
TEST(EGLImage, AcceptedTargets) {
    EGLDisplay dpy = GetTestDisplay();

    const EGLenum targets[] = {
        EGL_GL_TEXTURE_2D,
        EGL_GL_TEXTURE_3D,
        EGL_GL_TEXTURE_CUBE_MAP_POSITIVE_X,
        EGL_GL_RENDERBUFFER,
    };

    for (EGLenum target : targets) {
        EGLImage image = eglCreateImage(dpy, EGL_NO_CONTEXT, target,
                                        (EGLClientBuffer)(uintptr_t)1, NULL);
        EXPECT_NE(image, EGL_NO_IMAGE) << "target=0x" << std::hex << target;
        EXPECT_EQ(eglGetError(), EGL_SUCCESS) << "target=0x" << std::hex
                                              << target;

        EXPECT_EQ(eglDestroyImage(dpy, image), EGL_TRUE)
            << "target=0x" << std::hex << target;
        EXPECT_EQ(eglGetError(), EGL_SUCCESS) << "target=0x" << std::hex
                                              << target;
    }
}
