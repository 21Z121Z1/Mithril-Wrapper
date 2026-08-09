// Mithril-Wrapper - MG_Backend/DirectVulkan/FormatMap.h
// Pure-logic GL internalFormat -> VkFormat / host texel size / aspect mask
// helpers, extracted from Resources.cpp so they can be unit-tested without
// linking the rest of the Vulkan backend (no VkDevice / VkQueue / command
// buffer dependency).
//
// These functions are pure data tables: given a GLenum they return a VkFormat
// / byte count / VkImageAspectFlags. They do not touch any Vulkan state and
// are safe to call from a unit test binary linked against BackendStub.
#ifndef MITHRIL_DIRECTVULKAN_FORMATMAP_H
#define MITHRIL_DIRECTVULKAN_FORMATMAP_H

#include <vulkan/vulkan.h>
#include <GL/gl.h>

namespace mithril {
namespace vk {

// GL internalFormat -> VkFormat. Returns VK_FORMAT_UNDEFINED if unsupported.
// Covers the formats exercised by Minecraft Java's modern pipeline.
VkFormat gl_internal_to_vk(GLenum internal);

// Bytes per pixel for the (format,type) pair as seen on the host side. Used to
// size the staging buffer for glTexImage* uploads and the readback buffer for
// glReadPixels.
int host_texel_bytes(GLenum format, GLenum type);

// VkImageAspectFlags for a VkFormat (color / depth / depth+stencil / stencil).
VkImageAspectFlags aspect_for_format(VkFormat fmt);

// FIX (Root Cause AH - depth-stencil descriptor layout):
// Resolve the read-only image layout a sampled texture should be in, based on
// its VkFormat. Color formats use SHADER_READ_ONLY_OPTIMAL; depth-only formats
// (D16/D32/X8_D24) use DEPTH_READ_ONLY_OPTIMAL; depth-stencil formats
// (D24S8/D32S8/S8) use DEPTH_STENCIL_READ_ONLY_OPTIMAL.
//
// The descriptor's imageLayout field and the post-upload layout transition
// must both use this layout so the descriptor's declared layout matches the
// image's actual layout. Mirrors MobileGL ResolveSampledReadOnlyLayout
// (VkTextureManager.cpp:177).
VkImageLayout sampled_layout_for_format(VkFormat fmt);

} // namespace vk
} // namespace mithril

#endif // MITHRIL_DIRECTVULKAN_FORMATMAP_H
