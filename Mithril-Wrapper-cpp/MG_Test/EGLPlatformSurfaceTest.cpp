// Mithril-Wrapper - MG_Test/EGLPlatformSurfaceTest.cpp
// Unit tests for the EGL 1.5 Platform Surface API (Task 14).
//
// Exercises the four platform-surface entry points implemented in
// egl/egl.cpp:
//   - eglCreatePlatformWindowSurface   (surfaceless / headless path + X11
//                                       delegation path)
//   - eglCreatePlatformPixmapSurface   (state-layer-only, no swapchain)
//   - eglCreatePixmapSurface           (state-layer-only, no swapchain)
//   - eglCreatePbufferFromClientBuffer (OpenVG + unknown buftype rejections)
//
// Per the spec, the three required scenarios are covered:
//   1. SurfacelessPlatformWindowSurface      — a null native_window returns a
//      placeholder surface with no swapchain (EGL_MESA_platform_surfaceless
//      headless mode), no error posted.
//   2. X11PlatformWindowSurfaceWithNullWindowFails — a non-null bogus window
//      on a headless Linux host fails because surface_create() cannot resolve
//      libX11 / open the display, returning EGL_NO_SURFACE +
//      EGL_BAD_NATIVE_WINDOW.
//   3. PixmapAndPbufferFromClientBuffer — the two pixmap entry points return
//      state-layer-only surfaces, and eglCreatePbufferFromClientBuffer rejects
//      EGL_OPENVG_IMAGE with EGL_BAD_MATCH and any other buftype with
//      EGL_BAD_PARAMETER.
//
// Additionally: invalid display (EGL_BAD_DISPLAY) and invalid config
// (EGL_BAD_CONFIG) negative paths, plus eglDestroySurface cleanup for every
// successfully-created surface to avoid leaks.
//
// The display is the singleton returned by eglGetDisplay(EGL_DEFAULT_DISPLAY);
// valid_display() only checks pointer identity, so eglInitialize is not
// required (and would fail against the BackendStub anyway, since
// backend_available() returns 0). The config is &mithril::egl::g_configs[0],
// matching how egl.cpp hands out EGLConfig handles via (EGLConfig)&g_configs[i].
#include <gtest/gtest.h>

#include "../MG_Impl/EGLConfig.h"

#include <EGL/egl.h>

using mithril::egl::g_configs;

namespace {

// The singleton EGLDisplay. eglGetDisplay always returns the same Vulkan-
// backed display handle regardless of display_id.
EGLDisplay GetTestDisplay() {
    return eglGetDisplay(EGL_DEFAULT_DISPLAY);
}

// A valid EGLConfig: the first pre-baked entry in g_configs[], cast to EGLConfig
// exactly as egl.cpp's eglGetConfigs / eglChooseConfig do.
EGLConfig GetTestConfig() {
    return (EGLConfig)&g_configs[0];
}

} // namespace

// ===========================================================================
// Scenario 1: SurfacelessPlatformWindowSurface
// ===========================================================================

// eglCreatePlatformWindowSurface with a null native_window takes the
// surfaceless path (EGL_MESA_platform_surfaceless): it returns a placeholder
// EglSurface with no swapchain (state-layer only, headless mode) and posts no
// error. This path is platform-independent (it never calls surface_create()).
TEST(EGLPlatformSurface, SurfacelessPlatformWindowSurface) {
    EGLDisplay dpy = GetTestDisplay();
    EGLConfig  cfg = GetTestConfig();

    EGLSurface s = eglCreatePlatformWindowSurface(dpy, cfg, nullptr, nullptr);
    EXPECT_NE(s, EGL_NO_SURFACE);
    EXPECT_EQ(eglGetError(), EGL_SUCCESS);

    // Cleanup: destroy the created surface to avoid a leak.
    EXPECT_EQ(eglDestroySurface(dpy, s), EGL_TRUE);
}

// ===========================================================================
// Scenario 2: X11PlatformWindowSurfaceWithNullWindowFails
// ===========================================================================

// On a headless Linux host (no libX11 / no DISPLAY), eglCreatePlatformWindowSurface
// with a non-null window delegates to eglCreateWindowSurface, which calls
// surface_create(). SurfaceX11's surface_create() fails to resolve libX11 or
// cannot open the display, returning nullptr; egl.cpp then posts
// EGL_BAD_NATIVE_WINDOW and returns EGL_NO_SURFACE. This is the expected
// failure mode for a non-real window handle on a headless test runner.
//
// Guarded with #ifdef __linux__ since the X11 delegation path (SurfaceX11.cpp)
// is only compiled in on Linux. On other platforms the test is skipped.
TEST(EGLPlatformSurface, X11PlatformWindowSurfaceWithNullWindowFails) {
#ifdef __linux__
    EGLDisplay dpy = GetTestDisplay();
    EGLConfig  cfg = GetTestConfig();

    // An arbitrary non-null pointer that is not a valid X11 Window. The X11
    // surface helper will fail to query its geometry and return nullptr.
    void* bogus_window = (void*)(uintptr_t)0x1234;

    EGLSurface s = eglCreatePlatformWindowSurface(dpy, cfg, bogus_window, nullptr);
    EXPECT_EQ(s, EGL_NO_SURFACE);
    EXPECT_EQ(eglGetError(), EGL_BAD_NATIVE_WINDOW);
#else
    GTEST_SKIP() << "X11 window failure path is Linux-specific";
#endif
}

// ===========================================================================
// Scenario 3: PixmapAndPbufferFromClientBuffer
// ===========================================================================

// Covers the state-layer-only surface constructors and the
// eglCreatePbufferFromClientBuffer rejection paths:
//   - eglCreatePlatformPixmapSurface(dpy, cfg, NULL, NULL) -> non-null surface
//   - eglCreatePixmapSurface(dpy, cfg, NULL, NULL)          -> non-null surface
//   - eglCreatePbufferFromClientBuffer(... EGL_OPENVG_IMAGE ...) -> EGL_NO_SURFACE + EGL_BAD_MATCH
//   - eglCreatePbufferFromClientBuffer(... 0x9999 ...)           -> EGL_NO_SURFACE + EGL_BAD_PARAMETER
// The two pixmap surfaces are recorded in the state layer only (no swapchain);
// both are destroyed at the end to avoid leaks.
TEST(EGLPlatformSurface, PixmapAndPbufferFromClientBuffer) {
    EGLDisplay dpy = GetTestDisplay();
    EGLConfig  cfg = GetTestConfig();

    // --- Pixmap paths: state-layer-only surfaces (no swapchain) ---

    EGLSurface pixmap1 = eglCreatePlatformPixmapSurface(dpy, cfg, nullptr, nullptr);
    EXPECT_NE(pixmap1, EGL_NO_SURFACE);
    EXPECT_EQ(eglGetError(), EGL_SUCCESS);

    EGLSurface pixmap2 = eglCreatePixmapSurface(dpy, cfg, nullptr, nullptr);
    EXPECT_NE(pixmap2, EGL_NO_SURFACE);
    EXPECT_EQ(eglGetError(), EGL_SUCCESS);

    // --- eglCreatePbufferFromClientBuffer rejections ---

    // EGL_OPENVG_IMAGE: OpenVG is not supported -> EGL_BAD_MATCH.
    EGLSurface pbuf_ovg = eglCreatePbufferFromClientBuffer(
        dpy, EGL_OPENVG_IMAGE, nullptr, cfg, nullptr);
    EXPECT_EQ(pbuf_ovg, EGL_NO_SURFACE);
    EXPECT_EQ(eglGetError(), EGL_BAD_MATCH);

    // Unknown buftype (0x9999) -> EGL_BAD_PARAMETER.
    EGLSurface pbuf_unk = eglCreatePbufferFromClientBuffer(
        dpy, 0x9999, nullptr, cfg, nullptr);
    EXPECT_EQ(pbuf_unk, EGL_NO_SURFACE);
    EXPECT_EQ(eglGetError(), EGL_BAD_PARAMETER);

    // Cleanup: destroy the two successfully-created pixmap surfaces.
    EXPECT_EQ(eglDestroySurface(dpy, pixmap1), EGL_TRUE);
    EXPECT_EQ(eglDestroySurface(dpy, pixmap2), EGL_TRUE);
}

// ===========================================================================
// Additional: InvalidDisplay
// ===========================================================================

// Every platform-surface constructor rejects EGL_NO_DISPLAY with
// EGL_BAD_DISPLAY and returns EGL_NO_SURFACE. valid_display() is checked
// before the config / buftype, so a valid config is passed but irrelevant.
TEST(EGLPlatformSurface, InvalidDisplayRejected) {
    EGLDisplay bad_dpy = EGL_NO_DISPLAY;
    EGLConfig  cfg     = GetTestConfig();

    // Surfaceless path (null native_window) — platform-independent.
    EXPECT_EQ(eglCreatePlatformWindowSurface(bad_dpy, cfg, nullptr, nullptr),
              EGL_NO_SURFACE);
    EXPECT_EQ(eglGetError(), EGL_BAD_DISPLAY);

    EXPECT_EQ(eglCreatePlatformPixmapSurface(bad_dpy, cfg, nullptr, nullptr),
              EGL_NO_SURFACE);
    EXPECT_EQ(eglGetError(), EGL_BAD_DISPLAY);

    EXPECT_EQ(eglCreatePixmapSurface(bad_dpy, cfg, nullptr, nullptr),
              EGL_NO_SURFACE);
    EXPECT_EQ(eglGetError(), EGL_BAD_DISPLAY);

    // eglCreatePbufferFromClientBuffer checks display before buftype.
    EXPECT_EQ(eglCreatePbufferFromClientBuffer(
                  bad_dpy, EGL_OPENVG_IMAGE, nullptr, cfg, nullptr),
              EGL_NO_SURFACE);
    EXPECT_EQ(eglGetError(), EGL_BAD_DISPLAY);
}

// ===========================================================================
// Additional: InvalidConfig
// ===========================================================================

// The surfaceless / pixmap constructors reject an invalid EGLConfig with
// EGL_BAD_CONFIG. (eglCreatePbufferFromClientBuffer does not validate config
// in this implementation — it checks display then buftype — so it is not
// exercised here; its rejection paths are covered by scenario 3 above.)
TEST(EGLPlatformSurface, InvalidConfigRejected) {
    EGLDisplay dpy = GetTestDisplay();

    // An address that cannot be &g_configs[i] (the global config table), so
    // valid_config() iterates the table and finds no match.
    int dummy = 0;
    EGLConfig bad_cfg = (EGLConfig)&dummy;

    EXPECT_EQ(eglCreatePlatformWindowSurface(dpy, bad_cfg, nullptr, nullptr),
              EGL_NO_SURFACE);
    EXPECT_EQ(eglGetError(), EGL_BAD_CONFIG);

    EXPECT_EQ(eglCreatePlatformPixmapSurface(dpy, bad_cfg, nullptr, nullptr),
              EGL_NO_SURFACE);
    EXPECT_EQ(eglGetError(), EGL_BAD_CONFIG);

    EXPECT_EQ(eglCreatePixmapSurface(dpy, bad_cfg, nullptr, nullptr),
              EGL_NO_SURFACE);
    EXPECT_EQ(eglGetError(), EGL_BAD_CONFIG);
}
