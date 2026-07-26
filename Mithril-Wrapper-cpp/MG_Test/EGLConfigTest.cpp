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

// ---- combination-property matching (Task 4.4) ----

// Multiple size constraints applied simultaneously. Every pre-baked config is
// RGBA8, but only id=1 (g_configs[0]) and id=2 (g_configs[1]) carry a 24-bit
// depth buffer; id=3 and id=4 have depthSize==0. The combined {RGBA8, depth=24}
// predicate must therefore select exactly configs 0 and 1 and reject 2 and 3.
TEST(ConfigMatches, CombinationRed8Green8Blue8Alpha8Depth24) {
    const EGLint match[] = {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_NONE};
    EXPECT_TRUE(config_matches(&g_configs[0], match));  // id=1, D24S8
    EXPECT_TRUE(config_matches(&g_configs[1], match));  // id=2, D24
    EXPECT_FALSE(config_matches(&g_configs[2], match)); // id=3, no depth
    EXPECT_FALSE(config_matches(&g_configs[3], match)); // id=4, no depth
}

// config_matches now has an explicit `case EGL_TRANSPARENT_TYPE:` that
// rejects any value other than EGL_NONE (no pre-baked config is transparent).
// So requesting EGL_TRANSPARENT_RGB filters out every config. config_get_attr
// reports EGL_NONE for EGL_TRANSPARENT_TYPE — the match predicate and the
// attribute lookup now agree.
TEST(ConfigMatches, TransparentTypeRgbIsRejected) {
    const EGLint match[] = {EGL_TRANSPARENT_TYPE, EGL_TRANSPARENT_RGB, EGL_NONE};
    int matched = 0;
    for (int i = 0; i < kNumConfigs; ++i) {
        if (config_matches(&g_configs[i], match)) ++matched;
        EXPECT_EQ(config_get_attr(&g_configs[i], EGL_TRANSPARENT_TYPE), EGL_NONE)
            << "config index " << i;
    }
    EXPECT_EQ(matched, 0);
}

// EGL_TRANSPARENT_TYPE = EGL_NONE is the value every pre-baked config reports
// (none is transparent), so the constraint matches the entire table.
TEST(ConfigMatches, TransparentTypeNoneMatchesAll) {
    const EGLint attribs[] = {EGL_TRANSPARENT_TYPE, EGL_NONE, EGL_NONE};
    int matched = 0;
    for (int i = 0; i < kNumConfigs; ++i) {
        if (config_matches(&g_configs[i], attribs)) ++matched;
    }
    EXPECT_EQ(matched, kNumConfigs);
}

// config_matches now has an explicit `case EGL_LUMINANCE_SIZE:` that only
// accepts 0 (no pre-baked config carries a luminance buffer — they are all
// RGB / RGBA). Requesting 8 therefore matches nothing; requesting 0 matches
// every config. config_get_attr reports 0 for EGL_LUMINANCE_SIZE on all
// configs, so the match predicate and the attribute lookup now agree.
TEST(ConfigMatches, LuminanceSizeIsHonored) {
    // EGL_LUMINANCE_SIZE = 8 matches no config (no luminance buffers).
    const EGLint match8[] = {EGL_LUMINANCE_SIZE, 8, EGL_NONE};
    int matched8 = 0;
    for (int i = 0; i < kNumConfigs; ++i) {
        if (config_matches(&g_configs[i], match8)) ++matched8;
        EXPECT_EQ(config_get_attr(&g_configs[i], EGL_LUMINANCE_SIZE), 0)
            << "config index " << i;
    }
    EXPECT_EQ(matched8, 0);

    // EGL_LUMINANCE_SIZE = 0 matches all configs.
    const EGLint match0[] = {EGL_LUMINANCE_SIZE, 0, EGL_NONE};
    int matched0 = 0;
    for (int i = 0; i < kNumConfigs; ++i) {
        if (config_matches(&g_configs[i], match0)) ++matched0;
    }
    EXPECT_EQ(matched0, kNumConfigs);
}

// EGL_LUMINANCE_SIZE = 0 is the value every pre-baked config reports, so the
// constraint matches the entire table.
TEST(ConfigMatches, LuminanceSizeZeroMatchesAll) {
    const EGLint attribs[] = {EGL_LUMINANCE_SIZE, 0, EGL_NONE};
    int matched = 0;
    for (int i = 0; i < kNumConfigs; ++i) {
        if (config_matches(&g_configs[i], attribs)) ++matched;
    }
    EXPECT_EQ(matched, kNumConfigs);
}

// Setting every attribute the host might query to EGL_DONT_CARE must yield a
// permissive predicate that matches the entire table. Exercises the
// `if (value == EGL_DONT_CARE) continue;` short-circuit across a broad set of
// attributes — both those config_matches actively checks (sizes, surface /
// renderable type, config id, color buffer type, transparent type, luminance
// size) and those it ignores via `default` (caveats, swap intervals, ...).
// Note: EGL_MAX_PBUFFER_HEIGHT / EGL_NATIVE_VISUAL_TYPE share token values
// with EGL_NATIVE_VISUAL_ID / EGL_SAMPLES respectively (Khronos spec aliasing),
// so only one of each aliased pair is listed to keep the array token-clean.
TEST(ConfigMatches, DontCareForAllPropertiesMatchesEverything) {
    const EGLint allDontCare[] = {
        EGL_RED_SIZE,             EGL_DONT_CARE,
        EGL_GREEN_SIZE,           EGL_DONT_CARE,
        EGL_BLUE_SIZE,            EGL_DONT_CARE,
        EGL_ALPHA_SIZE,           EGL_DONT_CARE,
        EGL_DEPTH_SIZE,           EGL_DONT_CARE,
        EGL_STENCIL_SIZE,         EGL_DONT_CARE,
        EGL_SURFACE_TYPE,         EGL_DONT_CARE,
        EGL_RENDERABLE_TYPE,      EGL_DONT_CARE,
        EGL_CONFORMANT,           EGL_DONT_CARE,
        EGL_TRANSPARENT_TYPE,     EGL_DONT_CARE,
        EGL_LUMINANCE_SIZE,       EGL_DONT_CARE,
        EGL_BUFFER_SIZE,          EGL_DONT_CARE,
        EGL_LEVEL,                EGL_DONT_CARE,
        EGL_COLOR_BUFFER_TYPE,    EGL_DONT_CARE,
        EGL_CONFIG_CAVEAT,        EGL_DONT_CARE,
        EGL_CONFIG_ID,            EGL_DONT_CARE,
        EGL_MAX_PBUFFER_WIDTH,    EGL_DONT_CARE,
        EGL_MAX_PBUFFER_PIXELS,   EGL_DONT_CARE,
        EGL_NATIVE_VISUAL_ID,     EGL_DONT_CARE,
        EGL_SAMPLE_BUFFERS,       EGL_DONT_CARE,
        EGL_SAMPLES,              EGL_DONT_CARE,
        EGL_BIND_TO_TEXTURE_RGB,  EGL_DONT_CARE,
        EGL_BIND_TO_TEXTURE_RGBA, EGL_DONT_CARE,
        EGL_MIN_SWAP_INTERVAL,    EGL_DONT_CARE,
        EGL_MAX_SWAP_INTERVAL,    EGL_DONT_CARE,
        EGL_NONE};
    for (int i = 0; i < kNumConfigs; ++i) {
        EXPECT_TRUE(config_matches(&g_configs[i], allDontCare))
            << "config index " << i;
    }
}
