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

} // namespace vk
} // namespace mithril

#endif // MITHRIL_DIRECTVULKAN_FORMATMAP_H
