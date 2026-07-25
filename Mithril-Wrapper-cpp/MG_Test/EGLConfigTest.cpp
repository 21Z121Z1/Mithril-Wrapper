// Mithril-Wrapper - MG_Test/EGLConfigTest.cpp
// Unit tests for the pure-logic EGL config matching / lookup helpers in
// MG_Impl/EGLConfig.cpp.
//
// Covers config_matches (attribute-list matching against an EglConfig),
// config_get_attr (single-attribute lookup), and the g_configs[] table
// contents. These helpers are pure data-table logic with no EGL display /
// surface / Vulkan state, so they are exercised directly.
#include <gtest/gtest.h>

#include "../MG_Impl/EGLConfig.h"

#include <EGL/egl.h>

using mithril::egl::config_get_attr;
using mithril::egl::config_matches;
using mithril::egl::g_configs;
using mithril::egl::kNumConfigs;

// ---- config_matches ----

TEST(ConfigMatches, NullAttribsMatchesEverything) {
    EXPECT_TRUE(config_matches(&g_configs[0], nullptr));
    EXPECT_TRUE(config_matches(&g_configs[3], nullptr));
}

TEST(ConfigMatches, RedSizeConstraint) {
    const EGLint match8[] = {EGL_RED_SIZE, 8, EGL_NONE};
    EXPECT_TRUE(config_matches(&g_configs[0], match8));

    // Every pre-baked config has redSize==8, so asking for 16 never matches.
    const EGLint match16[] = {EGL_RED_SIZE, 16, EGL_NONE};
    for (int i = 0; i < kNumConfigs; ++i) {
        EXPECT_FALSE(config_matches(&g_configs[i], match16))
            << "config index " << i;
    }
}

TEST(ConfigMatches, DontCareIsIgnored) {
    const EGLint dontCare[] = {EGL_RED_SIZE, EGL_DONT_CARE, EGL_NONE};
    EXPECT_TRUE(config_matches(&g_configs[0], dontCare));

    // A real constraint alongside EGL_DONT_CARE still applies.
    const EGLint mixed[] = {EGL_RED_SIZE, EGL_DONT_CARE,
                            EGL_GREEN_SIZE, 8, EGL_NONE};
    EXPECT_TRUE(config_matches(&g_configs[0], mixed));
}

TEST(ConfigMatches, SurfaceType) {
    const EGLint window[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_NONE};
    EXPECT_TRUE(config_matches(&g_configs[0], window));

    const EGLint pbuffer[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE};
    EXPECT_TRUE(config_matches(&g_configs[0], pbuffer));
}

TEST(ConfigMatches, RenderableType) {
    const EGLint opengl[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE};
    EXPECT_TRUE(config_matches(&g_configs[0], opengl));
}

TEST(ConfigMatches, TerminatedByEglNone) {
    // A well-formed list with multiple constraints terminated by EGL_NONE.
    const EGLint list[] = {EGL_RED_SIZE, 8,
                           EGL_ALPHA_SIZE, 8,
                           EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
                           EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                           EGL_NONE};
    EXPECT_TRUE(config_matches(&g_configs[0], list));

    // EGL_NONE appears immediately -> treated as empty (match-all) list.
    const EGLint empty[] = {EGL_NONE};
    EXPECT_TRUE(config_matches(&g_configs[0], empty));
}

// ---- config_get_attr ----

TEST(ConfigGetAttr, ReturnsColorChannelSizes) {
    const mithril::egl::EglConfig* cfg = &g_configs[0]; // RGBA8
    EXPECT_EQ(config_get_attr(cfg, EGL_RED_SIZE), 8);
    EXPECT_EQ(config_get_attr(cfg, EGL_GREEN_SIZE), 8);
    EXPECT_EQ(config_get_attr(cfg, EGL_BLUE_SIZE), 8);
    EXPECT_EQ(config_get_attr(cfg, EGL_ALPHA_SIZE), 8);
}

TEST(ConfigGetAttr, DepthSizeVariesByConfigId) {
    // config id=1 (g_configs[0]) has a 24-bit depth buffer.
    EXPECT_EQ(config_get_attr(&g_configs[0], EGL_DEPTH_SIZE), 24);
    // config id=4 (g_configs[3]) has no depth buffer.
    EXPECT_EQ(config_get_attr(&g_configs[3], EGL_DEPTH_SIZE), 0);
}

TEST(ConfigGetAttr, StencilSizeForFirstConfig) {
    EXPECT_EQ(config_get_attr(&g_configs[0], EGL_STENCIL_SIZE), 8);
}

TEST(ConfigGetAttr, ConfigIdMatchesIndexPlusOne) {
    for (int i = 0; i < kNumConfigs; ++i) {
        EXPECT_EQ(config_get_attr(&g_configs[i], EGL_CONFIG_ID), i + 1)
            << "config index " << i;
    }
}

TEST(ConfigGetAttr, SurfaceTypeExposesWindowAndPbuffer) {
    EXPECT_EQ(config_get_attr(&g_configs[0], EGL_SURFACE_TYPE),
              EGL_WINDOW_BIT | EGL_PBUFFER_BIT);
}

TEST(ConfigGetAttr, UnknownAttributeReturnsZero) {
    EXPECT_EQ(config_get_attr(&g_configs[0], 0x7FFFu), 0);
}

// ---- g_configs table ----

TEST(GConfigs, TableHasAtLeastFourEntries) {
    EXPECT_GE(kNumConfigs, 4);
}

TEST(GConfigs, FirstConfigHasIdOne) {
    EXPECT_EQ(g_configs[0].configId, 1);
}

TEST(GConfigs, AllConfigsAreRgba8) {
    for (int i = 0; i < kNumConfigs; ++i) {
        EXPECT_EQ(g_configs[i].redSize, 8) << "config index " << i;
        EXPECT_EQ(g_configs[i].greenSize, 8) << "config index " << i;
        EXPECT_EQ(g_configs[i].blueSize, 8) << "config index " << i;
        EXPECT_EQ(g_configs[i].alphaSize, 8) << "config index " << i;
    }
}
