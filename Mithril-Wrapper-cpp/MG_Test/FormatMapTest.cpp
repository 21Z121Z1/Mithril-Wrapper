// Mithril-Wrapper - MG_Test/FormatMapTest.cpp
// Unit tests for the pure-logic GL format mapping helpers in
// MG_Backend/DirectVulkan/FormatMap.cpp.
//
// Covers gl_internal_to_vk (GL internalFormat -> VkFormat), host_texel_bytes
// (bytes-per-pixel for a (format,type) pair), and aspect_for_format
// (VkImageAspectFlags for a VkFormat). These are pure data tables with no
// Vulkan state, so they are exercised directly with no VkDevice available.
#include <gtest/gtest.h>

#include "../MG_Backend/DirectVulkan/FormatMap.h"

#include <vulkan/vulkan.h>
#include <GL/gl.h>

using mithril::vk::aspect_for_format;
using mithril::vk::gl_internal_to_vk;
using mithril::vk::host_texel_bytes;

// ---- gl_internal_to_vk ----
TEST(GlInternalToVk, MapsCoreColorFormats) {
    EXPECT_EQ(gl_internal_to_vk(GL_RGBA8), VK_FORMAT_R8G8B8A8_UNORM);
    EXPECT_EQ(gl_internal_to_vk(GL_RGB8), VK_FORMAT_R8G8B8_UNORM);
    EXPECT_EQ(gl_internal_to_vk(GL_SRGB8_ALPHA8), VK_FORMAT_R8G8B8A8_SRGB);
    EXPECT_EQ(gl_internal_to_vk(GL_RGB565), VK_FORMAT_R5G6B5_UNORM_PACK16);
    EXPECT_EQ(gl_internal_to_vk(GL_RGBA4), VK_FORMAT_R4G4B4A4_UNORM_PACK16);
    EXPECT_EQ(gl_internal_to_vk(GL_RGB5_A1), VK_FORMAT_R5G5B5A1_UNORM_PACK16);
}

TEST(GlInternalToVk, MapsBcCompressedFormats) {
    EXPECT_EQ(gl_internal_to_vk(GL_COMPRESSED_RGBA_S3TC_DXT1_EXT),
              VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
    EXPECT_EQ(gl_internal_to_vk(GL_COMPRESSED_RGBA_S3TC_DXT3_EXT),
              VK_FORMAT_BC2_UNORM_BLOCK);
    EXPECT_EQ(gl_internal_to_vk(GL_COMPRESSED_RGBA_S3TC_DXT5_EXT),
              VK_FORMAT_BC3_UNORM_BLOCK);
}

TEST(GlInternalToVk, UnknownFormatReturnsUndefined) {
    EXPECT_EQ(gl_internal_to_vk(0xDEADBEEFu), VK_FORMAT_UNDEFINED);
}

// ---- host_texel_bytes ----
TEST(HostTexelBytes, ComputesBytesPerPixel) {
    EXPECT_EQ(host_texel_bytes(GL_RGBA, GL_UNSIGNED_BYTE), 4);
    EXPECT_EQ(host_texel_bytes(GL_RGB, GL_UNSIGNED_BYTE), 3);
    EXPECT_EQ(host_texel_bytes(GL_RED, GL_UNSIGNED_BYTE), 1);
    EXPECT_EQ(host_texel_bytes(GL_RGBA, GL_FLOAT), 16);
    // Packed 16-bit types always report 2 bytes regardless of channel count.
    EXPECT_EQ(host_texel_bytes(GL_RGB, GL_UNSIGNED_SHORT_5_6_5), 2);
}

// GL_DEPTH_COMPONENT is not in the format switch, so it falls through to the
// default component count (4) and is then scaled by the type. For GL_FLOAT
// this yields 4 * 4 = 16 bytes per texel. (The spec hinted at 4 "if supported";
// the actual implementation returns 16, which this test pins.)
TEST(HostTexelBytes, DepthComponentFallsThroughToDefault) {
    EXPECT_EQ(host_texel_bytes(GL_DEPTH_COMPONENT, GL_FLOAT), 16);
    EXPECT_EQ(host_texel_bytes(GL_DEPTH_COMPONENT, GL_UNSIGNED_BYTE), 4);
}

// ---- aspect_for_format ----
TEST(AspectForFormat, ReturnsCorrectAspectMask) {
    EXPECT_EQ(aspect_for_format(VK_FORMAT_R8G8B8A8_UNORM),
              VK_IMAGE_ASPECT_COLOR_BIT);
    EXPECT_EQ(aspect_for_format(VK_FORMAT_D32_SFLOAT),
              VK_IMAGE_ASPECT_DEPTH_BIT);
    EXPECT_EQ(aspect_for_format(VK_FORMAT_D24_UNORM_S8_UINT),
              VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
}

TEST(AspectForFormat, TreatsUnknownFormatAsColor) {
    EXPECT_EQ(aspect_for_format(VK_FORMAT_UNDEFINED),
              VK_IMAGE_ASPECT_COLOR_BIT);
}

// ---- Exhaustive enumeration of gl_internal_to_vk (Task 4.3) ----
// Cover EVERY case label in the FormatMap.cpp switch plus the unknown-format
// fallback. The {GL internalFormat, expected VkFormat} table below is built
// directly from gl_internal_to_vk's switch statement; any newly added case in
// FormatMap.cpp should be mirrored here, and any case removed here will
// surface as a missing parameter.

namespace {
struct GlToVkEntry {
    GLenum gl_format;
    VkFormat expected_vk;
};
}  // namespace

class GlInternalToVkExhaustive
    : public ::testing::TestWithParam<GlToVkEntry> {};

// Verifies that every GL internalFormat handled by gl_internal_to_vk maps to a
// defined VkFormat (never VK_FORMAT_UNDEFINED) AND matches the exact expected
// VkFormat from the switch table in FormatMap.cpp.
TEST_P(GlInternalToVkExhaustive, AllKnownFormatsMapToNonUndefined) {
    const GlToVkEntry& entry = GetParam();
    const VkFormat result = gl_internal_to_vk(entry.gl_format);

    EXPECT_NE(result, VK_FORMAT_UNDEFINED)
        << "GL internalFormat 0x" << std::hex << entry.gl_format
        << " unexpectedly mapped to VK_FORMAT_UNDEFINED";
    EXPECT_EQ(result, entry.expected_vk)
        << "GL internalFormat 0x" << std::hex << entry.gl_format
        << " did not map to the expected VkFormat";
}

INSTANTIATE_TEST_SUITE_P(
    KnownFormats, GlInternalToVkExhaustive,
    ::testing::Values(
        // 8-bit UNORM / SNORM / sRGB color
        GlToVkEntry{GL_RGBA8,                              VK_FORMAT_R8G8B8A8_UNORM},
        GlToVkEntry{GL_RGBA8_SNORM,                        VK_FORMAT_R8G8B8A8_SNORM},
        GlToVkEntry{GL_SRGB8_ALPHA8,                       VK_FORMAT_R8G8B8A8_SRGB},
        GlToVkEntry{GL_RGB8,                               VK_FORMAT_R8G8B8_UNORM},
        GlToVkEntry{GL_R8,                                 VK_FORMAT_R8_UNORM},
        GlToVkEntry{GL_R8_SNORM,                           VK_FORMAT_R8_SNORM},
        GlToVkEntry{GL_RG8,                                VK_FORMAT_R8G8_UNORM},
        // Packed 16-bit color
        GlToVkEntry{GL_RGB565,                             VK_FORMAT_R5G6B5_UNORM_PACK16},
        GlToVkEntry{GL_RGBA4,                              VK_FORMAT_R4G4B4A4_UNORM_PACK16},
        GlToVkEntry{GL_RGB5_A1,                            VK_FORMAT_R5G5B5A1_UNORM_PACK16},
        // 10-bit packed
        GlToVkEntry{GL_RGB10_A2,                           VK_FORMAT_A2B10G10R10_UNORM_PACK32},
        GlToVkEntry{GL_RGB10_A2UI,                         VK_FORMAT_A2B10G10R10_UINT_PACK32},
        // 16-bit / 32-bit float and unorm
        GlToVkEntry{GL_RGBA16F,                            VK_FORMAT_R16G16B16A16_SFLOAT},
        GlToVkEntry{GL_RGB16F,                             VK_FORMAT_R16G16B16_SFLOAT},
        GlToVkEntry{GL_RGBA32F,                            VK_FORMAT_R32G32B32A32_SFLOAT},
        GlToVkEntry{GL_RGB32F,                             VK_FORMAT_R32G32B32_SFLOAT},
        GlToVkEntry{GL_R16F,                               VK_FORMAT_R16_SFLOAT},
        GlToVkEntry{GL_R32F,                               VK_FORMAT_R32_SFLOAT},
        GlToVkEntry{GL_RG16F,                              VK_FORMAT_R16G16_SFLOAT},
        GlToVkEntry{GL_RG32F,                              VK_FORMAT_R32G32_SFLOAT},
        GlToVkEntry{GL_RGBA16,                             VK_FORMAT_R16G16B16A16_UNORM},
        // Depth / stencil
        GlToVkEntry{GL_DEPTH_COMPONENT16,                  VK_FORMAT_D16_UNORM},
        GlToVkEntry{GL_DEPTH_COMPONENT24,                  VK_FORMAT_D24_UNORM_S8_UINT},
        GlToVkEntry{GL_DEPTH_COMPONENT32F,                 VK_FORMAT_D32_SFLOAT},
        GlToVkEntry{GL_DEPTH24_STENCIL8,                   VK_FORMAT_D24_UNORM_S8_UINT},
        GlToVkEntry{GL_DEPTH32F_STENCIL8,                  VK_FORMAT_D32_SFLOAT_S8_UINT},
        GlToVkEntry{GL_STENCIL_INDEX8,                     VK_FORMAT_S8_UINT},
        // BC (S3TC) compressed
        GlToVkEntry{GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,     VK_FORMAT_BC1_RGBA_UNORM_BLOCK},
        GlToVkEntry{GL_COMPRESSED_RGB_S3TC_DXT1_EXT,       VK_FORMAT_BC1_RGBA_UNORM_BLOCK},
        GlToVkEntry{GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,      VK_FORMAT_BC2_UNORM_BLOCK},
        GlToVkEntry{GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,      VK_FORMAT_BC3_UNORM_BLOCK}));

// Verifies that GL enums NOT present as case labels in gl_internal_to_vk fall
// through to `default:` and return VK_FORMAT_UNDEFINED. Complements the
// existing GlInternalToVk.UnknownFormatReturnsUndefined test (which uses
// 0xDEADBEEF) by exercising several additional made-up / out-of-range values.
TEST(GlInternalToVkExhaustive, UnknownFormatReturnsUndefined) {
    const GLenum unknown_formats[] = {
        0x0000u,        // never a valid GL internalFormat
        0x1234u,        // made-up
        0x9999u,        // made-up (per task example)
        0xAAAAu,        // made-up
        0xCAFEu,        // made-up
        0xFFFFFFFFu,    // out-of-range
    };
    for (GLenum fmt : unknown_formats) {
        EXPECT_EQ(gl_internal_to_vk(fmt), VK_FORMAT_UNDEFINED)
            << "GL enum 0x" << std::hex << fmt
            << " unexpectedly mapped to a non-UNDEFINED VkFormat";
    }
}
