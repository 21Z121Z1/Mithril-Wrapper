// Mithril-Wrapper - MG_Backend/DirectVulkan/FormatMap.cpp
// Pure-logic GL internalFormat -> VkFormat / host texel size / aspect mask
// helpers. Extracted from Resources.cpp so they can be unit-tested without
// linking the rest of the Vulkan backend.
//
// These functions are pure data tables: given a GLenum they return a VkFormat
// / byte count / VkImageAspectFlags. They do NOT call into Vulkan and are safe
// to invoke from a unit-test binary with no VkDevice available.
#include "FormatMap.h"

namespace mithril {
namespace vk {

// ---- GL internalFormat -> VkFormat ----
// Covers the formats exercised by Minecraft Java's modern pipeline.
VkFormat gl_internal_to_vk(GLenum internal) {
    switch (internal) {
        case GL_RGBA8:                return VK_FORMAT_R8G8B8A8_UNORM;
        case GL_SRGB8_ALPHA8:         return VK_FORMAT_R8G8B8A8_SRGB;
        case GL_SRGB8:                return VK_FORMAT_R8G8B8A8_SRGB;   // 3ch -> 4ch
        case GL_RGB8:                 return VK_FORMAT_R8G8B8A8_UNORM;  // 3ch -> 4ch
        case GL_RGB565:               return VK_FORMAT_R5G6B5_UNORM_PACK16;
        case GL_RGBA4:                return VK_FORMAT_R4G4B4A4_UNORM_PACK16;
        case GL_RGB5_A1:              return VK_FORMAT_R5G5B5A1_UNORM_PACK16;
        case GL_RGBA16F:              return VK_FORMAT_R16G16B16A16_SFLOAT;
        case GL_RGB16F:               return VK_FORMAT_R16G16B16A16_SFLOAT; // 3ch -> 4ch
        case GL_RGBA32F:              return VK_FORMAT_R32G32B32A32_SFLOAT;
        case GL_RGB32F:               return VK_FORMAT_R32G32B32A32_SFLOAT; // 3ch -> 4ch
        case GL_R8:                   return VK_FORMAT_R8_UNORM;
        case GL_R8_SNORM:             return VK_FORMAT_R8_SNORM;
        case GL_R16F:                 return VK_FORMAT_R16_SFLOAT;
        case GL_R32F:                 return VK_FORMAT_R32_SFLOAT;
        case GL_RG8:                  return VK_FORMAT_R8G8_UNORM;
        case GL_RG16F:                return VK_FORMAT_R16G16_SFLOAT;
        case GL_RG32F:                return VK_FORMAT_R32G32_SFLOAT;
        case GL_RGBA8_SNORM:          return VK_FORMAT_R8G8B8A8_SNORM;
        case GL_RGB10_A2:             return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        case GL_RGB10_A2UI:           return VK_FORMAT_A2B10G10R10_UINT_PACK32;
        case GL_RGBA16:               return VK_FORMAT_R16G16B16A16_UNORM;
        case GL_DEPTH_COMPONENT16:    return VK_FORMAT_D16_UNORM;
        case GL_DEPTH_COMPONENT24:    return VK_FORMAT_D24_UNORM_S8_UINT;
        case GL_DEPTH_COMPONENT32F:   return VK_FORMAT_D32_SFLOAT;
        case GL_DEPTH24_STENCIL8:     return VK_FORMAT_D24_UNORM_S8_UINT;
        case GL_DEPTH32F_STENCIL8:    return VK_FORMAT_D32_SFLOAT_S8_UINT;
        case GL_STENCIL_INDEX8:       return VK_FORMAT_S8_UINT;
        // Unsized internal formats (Minecraft passes these for many textures
        // and for depth). glslang/MoltenVK need a concrete VkFormat, so map
        // them to the natural sized equivalent. Without these, gl_internal_to_vk
        // returns VK_FORMAT_UNDEFINED and the texture falls back to a color
        // format — which is especially wrong for GL_DEPTH_COMPONENT.
        case GL_RGBA:             return VK_FORMAT_R8G8B8A8_UNORM;  // 0x1908
        case GL_RGB:              return VK_FORMAT_R8G8B8A8_UNORM;    // 0x1907, 3ch -> 4ch
        case GL_DEPTH_COMPONENT:  return VK_FORMAT_D32_SFLOAT;      // 0x1902
        case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
        case GL_COMPRESSED_RGB_S3TC_DXT1_EXT: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT: return VK_FORMAT_BC2_UNORM_BLOCK;
        case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT: return VK_FORMAT_BC3_UNORM_BLOCK;
        default:                      return VK_FORMAT_UNDEFINED;
    }
}

// Bytes per pixel for the (format,type) pair as seen on the host side. Used to
// size the staging buffer for glTexImage* uploads.
int host_texel_bytes(GLenum format, GLenum type) {
    int comp = 4;
    switch (format) {
        case GL_RED:
        case GL_RED_INTEGER:
        case GL_LUMINANCE:
        case GL_ALPHA:            comp = 1; break;
        case GL_RG:
        case GL_RG_INTEGER:
        case GL_LUMINANCE_ALPHA:  comp = 2; break;
        case GL_RGB:
        case GL_RGB_INTEGER:
        case GL_BGR:              comp = 3; break;
        case GL_RGBA:
        case GL_RGBA_INTEGER:
        case GL_BGRA:             comp = 4; break;
        default: break;
    }
    switch (type) {
        case GL_UNSIGNED_BYTE:           return comp;
        case GL_BYTE:                    return comp;
        case GL_UNSIGNED_SHORT:
        case GL_SHORT:
        case GL_HALF_FLOAT:              return comp * 2;
        case GL_UNSIGNED_INT:
        case GL_INT:
        case GL_FLOAT:                   return comp * 4;
        case GL_UNSIGNED_SHORT_5_6_5:
        case GL_UNSIGNED_SHORT_4_4_4_4:
        case GL_UNSIGNED_SHORT_5_5_5_1:  return 2;
        case GL_UNSIGNED_INT_8_8_8_8:
        case GL_UNSIGNED_INT_8_8_8_8_REV: return 4;
        default:                         return comp;
    }
}

// Bytes per texel for a VkFormat. Used by stage_and_copy_image to size the
// destination staging buffer and to detect when a 3-channel source must be
// expanded. 3-byte formats (R8G8B8 / B8G8R8 / R16G16B16 / R32G32B32) report 3
// so callers force a 4-byte expansion — Vulkan/MoltenVK do not reliably support
// 3-byte-per-texel transfers (the copy encoder reads past the staging buffer).
int vk_format_texel_bytes(VkFormat fmt) {
    switch (fmt) {
        case VK_FORMAT_R8_UNORM:
        case VK_FORMAT_R8_SNORM:
        case VK_FORMAT_R8_UINT:
        case VK_FORMAT_R8_SINT:
        case VK_FORMAT_R8_SRGB:
        case VK_FORMAT_S8_UINT:
            return 1;
        case VK_FORMAT_R8G8_UNORM:
        case VK_FORMAT_R8G8_SNORM:
        case VK_FORMAT_R8G8_UINT:
        case VK_FORMAT_R8G8_SINT:
        case VK_FORMAT_R8G8_SRGB:
        case VK_FORMAT_R5G6B5_UNORM_PACK16:
        case VK_FORMAT_R4G4B4A4_UNORM_PACK16:
        case VK_FORMAT_R5G5B5A1_UNORM_PACK16:
        case VK_FORMAT_B4G4R4A4_UNORM_PACK16:
        case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_R16_UNORM:
        case VK_FORMAT_R16_SNORM:
        case VK_FORMAT_R16_SFLOAT:
        case VK_FORMAT_R16_UINT:
        case VK_FORMAT_R16_SINT:
            return 2;
        case VK_FORMAT_R8G8B8_UNORM:
        case VK_FORMAT_R8G8B8_SNORM:
        case VK_FORMAT_R8G8B8_SRGB:
        case VK_FORMAT_B8G8R8_UNORM:
        case VK_FORMAT_B8G8R8_SNORM:
        case VK_FORMAT_B8G8R8_SRGB:
        case VK_FORMAT_R16G16B16_UNORM:
        case VK_FORMAT_R16G16B16_SFLOAT:
        case VK_FORMAT_R32G32B32_UNORM:
        case VK_FORMAT_R32G32B32_SFLOAT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return 3; // logical 3-byte; callers expand to 4 when used as upload target
        case VK_FORMAT_R16G16B16A16_UNORM:
        case VK_FORMAT_R16G16B16A16_SNORM:
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return 8;
        case VK_FORMAT_R32G32B32A32_UNORM:
        case VK_FORMAT_R32G32B32A32_SNORM:
        case VK_FORMAT_R32G32B32A32_SFLOAT:
        case VK_FORMAT_R32G32B32A32_UINT:
        case VK_FORMAT_R32G32B32A32_SINT:
            return 16;
        case VK_FORMAT_R32_SFLOAT:
        case VK_FORMAT_R32_UINT:
        case VK_FORMAT_R32_SINT:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_R11G11B10_UFLOAT_PACK32:
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        case VK_FORMAT_A2B10G10R10_UINT_PACK32:
            return 4;
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC4_SNORM_BLOCK:
            return 1; // block-compressed (not used for client-side uploads)
        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC6H_UNORM_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
            return 2; // block-compressed (not used for client-side uploads)
        default:
            return 4;
    }
}

// VkImageAspectFlags for a VkFormat (color / depth / depth+stencil / stencil).
// Mirrors the aspect-for-format helper in ImageOps.cpp (kept here so callers
// that only depend on Resources.cpp don't need to link ImageOps).
VkImageAspectFlags aspect_for_format(VkFormat fmt) {
    if (fmt == VK_FORMAT_D16_UNORM || fmt == VK_FORMAT_D32_SFLOAT)
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    if (fmt == VK_FORMAT_S8_UINT)
        return VK_IMAGE_ASPECT_STENCIL_BIT;
    if (fmt == VK_FORMAT_D24_UNORM_S8_UINT || fmt == VK_FORMAT_D32_SFLOAT_S8_UINT)
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

} // namespace vk
} // namespace mithril
