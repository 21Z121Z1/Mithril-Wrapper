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
