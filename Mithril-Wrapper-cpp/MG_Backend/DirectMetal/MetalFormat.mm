// Mithril-Wrapper - MG_Backend/DirectMetal/MetalFormat.mm
// Enum translation tables: VkFormat tag -> MTLPixelFormat, GL (format,type)
// -> MTLVertexFormat, GL blend factors / compare funcs / stencil ops /
// wrap modes / filters -> their Metal counterparts. Pure logic + Metal enums,
// no device dependency.
#ifdef __APPLE__

#include "MetalDevice.h"
#include "../DirectVulkan/FormatMap.h" // mithril::vk::gl_internal_to_vk (shared)

namespace mithril {
namespace dmt {

// VkFormat tag (the neutral numbering shared with the frontend) -> MTLPixelFormat.
// MTLPixelFormatInvalid = unsupported.
MTLPixelFormat pixel_format_from_vk(VkFormat f) {
    switch (f) {
        case VK_FORMAT_R8_UNORM:            return MTLPixelFormatR8Unorm;
        case VK_FORMAT_R8_SNORM:            return MTLPixelFormatR8Snorm;
        case VK_FORMAT_R8_UINT:             return MTLPixelFormatR8Uint;
        case VK_FORMAT_R8_SINT:             return MTLPixelFormatR8Sint;
        case VK_FORMAT_R8G8_UNORM:          return MTLPixelFormatRG8Unorm;
        case VK_FORMAT_R8G8_SNORM:          return MTLPixelFormatRG8Snorm;
        case VK_FORMAT_R8G8_UINT:           return MTLPixelFormatRG8Uint;
        case VK_FORMAT_R8G8_SINT:           return MTLPixelFormatRG8Sint;
        case VK_FORMAT_R8G8B8A8_UNORM:      return MTLPixelFormatRGBA8Unorm;
        case VK_FORMAT_R8G8B8A8_SNORM:      return MTLPixelFormatRGBA8Snorm;
        case VK_FORMAT_R8G8B8A8_UINT:       return MTLPixelFormatRGBA8Uint;
        case VK_FORMAT_R8G8B8A8_SINT:       return MTLPixelFormatRGBA8Sint;
        case VK_FORMAT_B8G8R8A8_UNORM:      return MTLPixelFormatBGRA8Unorm;
        case VK_FORMAT_B8G8R8A8_SRGB:       return MTLPixelFormatBGRA8Unorm_sRGB;
        case VK_FORMAT_R8G8B8A8_SRGB:       return MTLPixelFormatRGBA8Unorm_sRGB;
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return MTLPixelFormatRGB10A2Unorm;
        case VK_FORMAT_A2R10G10B10_UNORM_PACK32: return MTLPixelFormatBGR10A2Unorm;
        case VK_FORMAT_R16_UNORM:           return MTLPixelFormatR16Unorm;
        case VK_FORMAT_R16_SNORM:           return MTLPixelFormatR16Snorm;
        case VK_FORMAT_R16_UINT:            return MTLPixelFormatR16Uint;
        case VK_FORMAT_R16_SINT:            return MTLPixelFormatR16Sint;
        case VK_FORMAT_R16_SFLOAT:          return MTLPixelFormatR16Float;
        case VK_FORMAT_R16G16_UNORM:        return MTLPixelFormatRG16Unorm;
        case VK_FORMAT_R16G16_SNORM:        return MTLPixelFormatRG16Snorm;
        case VK_FORMAT_R16G16_UINT:         return MTLPixelFormatRG16Uint;
        case VK_FORMAT_R16G16_SINT:         return MTLPixelFormatRG16Sint;
        case VK_FORMAT_R16G16_SFLOAT:       return MTLPixelFormatRG16Float;
        case VK_FORMAT_R16G16B16A16_UNORM:  return MTLPixelFormatRGBA16Unorm;
        case VK_FORMAT_R16G16B16A16_SNORM:  return MTLPixelFormatRGBA16Snorm;
        case VK_FORMAT_R16G16B16A16_UINT:   return MTLPixelFormatRGBA16Uint;
        case VK_FORMAT_R16G16B16A16_SINT:   return MTLPixelFormatRGBA16Sint;
        case VK_FORMAT_R16G16B16A16_SFLOAT: return MTLPixelFormatRGBA16Float;
        case VK_FORMAT_R32_UINT:            return MTLPixelFormatR32Uint;
        case VK_FORMAT_R32_SINT:            return MTLPixelFormatR32Sint;
        case VK_FORMAT_R32_SFLOAT:          return MTLPixelFormatR32Float;
        case VK_FORMAT_R32G32_UINT:         return MTLPixelFormatRG32Uint;
        case VK_FORMAT_R32G32_SINT:         return MTLPixelFormatRG32Sint;
        case VK_FORMAT_R32G32_SFLOAT:       return MTLPixelFormatRG32Float;
        case VK_FORMAT_R32G32B32A32_UINT:   return MTLPixelFormatRGBA32Uint;
        case VK_FORMAT_R32G32B32A32_SINT:   return MTLPixelFormatRGBA32Sint;
        case VK_FORMAT_R32G32B32A32_SFLOAT: return MTLPixelFormatRGBA32Float;
        case VK_FORMAT_D16_UNORM:           return MTLPixelFormatDepth32Float; // no D16 in Metal
        // Depth24Unorm_Stencil8 is unavailable on iOS. Preserve depth-only
        // semantics for X8_D24 using the portable 32-bit depth format, and use
        // Depth32Float_Stencil8 for the packed depth+stencil format.
        case VK_FORMAT_X8_D24_UNORM_PACK32: return MTLPixelFormatDepth32Float;
        case VK_FORMAT_D32_SFLOAT:          return MTLPixelFormatDepth32Float;
        case VK_FORMAT_S8_UINT:             return MTLPixelFormatStencil8;
        case VK_FORMAT_D24_UNORM_S8_UINT:   return MTLPixelFormatDepth32Float_Stencil8;
        case VK_FORMAT_D32_SFLOAT_S8_UINT:  return MTLPixelFormatDepth32Float_Stencil8;
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:  return MTLPixelFormatBC1_RGBA;
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:   return MTLPixelFormatBC1_RGBA_sRGB;
        case VK_FORMAT_BC2_UNORM_BLOCK:       return MTLPixelFormatBC2_RGBA;
        case VK_FORMAT_BC2_SRGB_BLOCK:        return MTLPixelFormatBC2_RGBA_sRGB;
        case VK_FORMAT_BC3_UNORM_BLOCK:       return MTLPixelFormatBC3_RGBA;
        case VK_FORMAT_BC3_SRGB_BLOCK:        return MTLPixelFormatBC3_RGBA_sRGB;
        case VK_FORMAT_BC4_UNORM_BLOCK:       return MTLPixelFormatBC4_RUnorm;
        case VK_FORMAT_BC4_SNORM_BLOCK:       return MTLPixelFormatBC4_RSnorm;
        case VK_FORMAT_BC5_UNORM_BLOCK:       return MTLPixelFormatBC5_RGUnorm;
        case VK_FORMAT_BC5_SNORM_BLOCK:       return MTLPixelFormatBC5_RGSnorm;
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:     return MTLPixelFormatBC6H_RGBUfloat;
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:     return MTLPixelFormatBC6H_RGBFloat;
        case VK_FORMAT_BC7_UNORM_BLOCK:       return MTLPixelFormatBC7_RGBAUnorm;
        case VK_FORMAT_BC7_SRGB_BLOCK:        return MTLPixelFormatBC7_RGBAUnorm_sRGB;
        // ETC2/EAC are natively supported on iOS Apple GPUs.
        case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:  return MTLPixelFormatETC2_RGB8;
        case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:   return MTLPixelFormatETC2_RGB8_sRGB;
        case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK:return MTLPixelFormatETC2_RGB8A1;
        case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK: return MTLPixelFormatETC2_RGB8A1_sRGB;
        case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:return MTLPixelFormatEAC_RGBA8;
        case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK: return MTLPixelFormatEAC_RGBA8_sRGB;
        case VK_FORMAT_EAC_R11_UNORM_BLOCK:      return MTLPixelFormatEAC_R11Unorm;
        case VK_FORMAT_EAC_R11_SNORM_BLOCK:      return MTLPixelFormatEAC_R11Snorm;
        case VK_FORMAT_EAC_R11G11_UNORM_BLOCK:   return MTLPixelFormatEAC_RG11Unorm;
        case VK_FORMAT_EAC_R11G11_SNORM_BLOCK:   return MTLPixelFormatEAC_RG11Snorm;
        case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:     return MTLPixelFormatASTC_4x4_LDR;
        case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:      return MTLPixelFormatASTC_4x4_sRGB;
        case VK_FORMAT_ASTC_5x5_UNORM_BLOCK:     return MTLPixelFormatASTC_5x5_LDR;
        case VK_FORMAT_ASTC_5x5_SRGB_BLOCK:      return MTLPixelFormatASTC_5x5_sRGB;
        case VK_FORMAT_ASTC_6x6_UNORM_BLOCK:     return MTLPixelFormatASTC_6x6_LDR;
        case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:      return MTLPixelFormatASTC_6x6_sRGB;
        case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:     return MTLPixelFormatASTC_8x8_LDR;
        case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:      return MTLPixelFormatASTC_8x8_sRGB;
        default:                             return MTLPixelFormatInvalid;
    }
}

bool format_has_stencil(VkFormat f) {
    return f == VK_FORMAT_S8_UINT || f == VK_FORMAT_D24_UNORM_S8_UINT ||
           f == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

// GL (type, size, normalized/integer) -> MTLVertexFormat. MTLVertexFormatInvalid
// on an unsupported combination.
MTLVertexFormat vertex_format_from_gl(GLenum type, int size, int normalized, int integer) {
    const bool norm = normalized && !integer;
    if (type == GL_FLOAT) {
        switch (size) {
            case 1: return MTLVertexFormatFloat;
            case 2: return MTLVertexFormatFloat2;
            case 3: return MTLVertexFormatFloat3;
            case 4: return MTLVertexFormatFloat4;
        }
        return MTLVertexFormatInvalid;
    }
    // Metal has no normalized 32-bit signed/unsigned integer vertex formats.
    // Do not substitute a non-normalized format: that would silently change
    // glVertexAttribPointer conversion semantics.
    if (type == GL_INT && !integer) {
        if (norm) return MTLVertexFormatInvalid;
        switch (size) {
            case 1: return MTLVertexFormatInt;
            case 2: return MTLVertexFormatInt2;
            case 3: return MTLVertexFormatInt3;
            case 4: return MTLVertexFormatInt4;
        }
    }
    if (type == GL_UNSIGNED_INT && !integer) {
        if (norm) return MTLVertexFormatInvalid;
        switch (size) {
            case 1: return MTLVertexFormatUInt;
            case 2: return MTLVertexFormatUInt2;
            case 3: return MTLVertexFormatUInt3;
            case 4: return MTLVertexFormatUInt4;
        }
    }
    if (type == GL_INT && integer) {
        switch (size) {
            case 1: return MTLVertexFormatInt;
            case 2: return MTLVertexFormatInt2;
            case 3: return MTLVertexFormatInt3;
            case 4: return MTLVertexFormatInt4;
        }
    }
    if (type == GL_UNSIGNED_INT && integer) {
        switch (size) {
            case 1: return MTLVertexFormatUInt;
            case 2: return MTLVertexFormatUInt2;
            case 3: return MTLVertexFormatUInt3;
            case 4: return MTLVertexFormatUInt4;
        }
    }
    if (type == GL_SHORT) {
        if (integer) {
            switch (size) {
                case 1: return MTLVertexFormatShort;
                case 2: return MTLVertexFormatShort2;
                case 3: return MTLVertexFormatShort3;
                case 4: return MTLVertexFormatShort4;
            }
        }
        switch (size) {
            case 1: return norm ? MTLVertexFormatShortNormalized : MTLVertexFormatShort;
            case 2: return norm ? MTLVertexFormatShort2Normalized : MTLVertexFormatShort2;
            case 3: return norm ? MTLVertexFormatShort3Normalized : MTLVertexFormatShort3;
            case 4: return norm ? MTLVertexFormatShort4Normalized : MTLVertexFormatShort4;
        }
    }
    if (type == GL_UNSIGNED_SHORT) {
        if (integer) {
            switch (size) {
                case 1: return MTLVertexFormatUShort;
                case 2: return MTLVertexFormatUShort2;
                case 3: return MTLVertexFormatUShort3;
                case 4: return MTLVertexFormatUShort4;
            }
        }
        switch (size) {
            case 1: return norm ? MTLVertexFormatUShortNormalized : MTLVertexFormatUShort;
            case 2: return norm ? MTLVertexFormatUShort2Normalized : MTLVertexFormatUShort2;
            case 3: return norm ? MTLVertexFormatUShort3Normalized : MTLVertexFormatUShort3;
            case 4: return norm ? MTLVertexFormatUShort4Normalized : MTLVertexFormatUShort4;
        }
    }
    if (type == GL_BYTE) {
        if (integer) {
            switch (size) {
                case 1: return MTLVertexFormatChar;
                case 2: return MTLVertexFormatChar2;
                case 3: return MTLVertexFormatChar3;
                case 4: return MTLVertexFormatChar4;
            }
        }
        switch (size) {
            case 1: return norm ? MTLVertexFormatCharNormalized : MTLVertexFormatChar;
            case 2: return norm ? MTLVertexFormatChar2Normalized : MTLVertexFormatChar2;
            case 3: return norm ? MTLVertexFormatChar3Normalized : MTLVertexFormatChar3;
            case 4: return norm ? MTLVertexFormatChar4Normalized : MTLVertexFormatChar4;
        }
    }
    if (type == GL_UNSIGNED_BYTE) {
        if (integer) {
            switch (size) {
                case 1: return MTLVertexFormatUChar;
                case 2: return MTLVertexFormatUChar2;
                case 3: return MTLVertexFormatUChar3;
                case 4: return MTLVertexFormatUChar4;
            }
        }
        switch (size) {
            case 1: return norm ? MTLVertexFormatUCharNormalized : MTLVertexFormatUChar;
            case 2: return norm ? MTLVertexFormatUChar2Normalized : MTLVertexFormatUChar2;
            case 3: return norm ? MTLVertexFormatUChar3Normalized : MTLVertexFormatUChar3;
            case 4: return norm ? MTLVertexFormatUChar4Normalized : MTLVertexFormatUChar4;
        }
    }
    if (type == GL_HALF_FLOAT) {
        switch (size) {
            case 1: return MTLVertexFormatHalf;
            case 2: return MTLVertexFormatHalf2;
            case 3: return MTLVertexFormatHalf3;
            case 4: return MTLVertexFormatHalf4;
        }
    }
    if (type == GL_UNSIGNED_INT_2_10_10_10_REV && size == 4)
        return norm ? MTLVertexFormatUInt1010102Normalized : MTLVertexFormatInvalid;
    if (type == GL_INT_2_10_10_10_REV && size == 4)
        return norm ? MTLVertexFormatInt1010102Normalized : MTLVertexFormatInvalid;
    return MTLVertexFormatInvalid;
}

// Per-format attribute byte size for the MTLVertexFormat values we emit.
NSUInteger vertex_format_bytes(MTLVertexFormat f) {
    switch (f) {
        case MTLVertexFormatUChar2Normalized: case MTLVertexFormatChar2Normalized:
        case MTLVertexFormatUChar2: case MTLVertexFormatChar2:
        case MTLVertexFormatHalf: case MTLVertexFormatUShortNormalized:
        case MTLVertexFormatShortNormalized: case MTLVertexFormatUShort:
        case MTLVertexFormatShort:
            return 2;
        case MTLVertexFormatUChar3Normalized: case MTLVertexFormatChar3Normalized:
        case MTLVertexFormatUChar3: case MTLVertexFormatChar3:
            return 3;
        case MTLVertexFormatUChar4Normalized: case MTLVertexFormatChar4Normalized:
        case MTLVertexFormatUChar4: case MTLVertexFormatChar4:
        case MTLVertexFormatUCharNormalized: case MTLVertexFormatCharNormalized:
        case MTLVertexFormatUChar: case MTLVertexFormatChar:
        case MTLVertexFormatHalf2: case MTLVertexFormatUShort2Normalized:
        case MTLVertexFormatShort2Normalized: case MTLVertexFormatUShort2:
        case MTLVertexFormatShort2: case MTLVertexFormatFloat:
        case MTLVertexFormatUInt: case MTLVertexFormatInt:
        case MTLVertexFormatUInt1010102Normalized:
        case MTLVertexFormatInt1010102Normalized:
            return 4;
        case MTLVertexFormatHalf3:
            return 6;
        case MTLVertexFormatHalf4: case MTLVertexFormatFloat2:
        case MTLVertexFormatUShort4Normalized: case MTLVertexFormatShort4Normalized:
        case MTLVertexFormatUShort4: case MTLVertexFormatShort4:
        case MTLVertexFormatUShort3Normalized: case MTLVertexFormatShort3Normalized:
        case MTLVertexFormatUShort3: case MTLVertexFormatShort3:
        case MTLVertexFormatUInt2: case MTLVertexFormatInt2:
            return 8;
        case MTLVertexFormatFloat3:
        case MTLVertexFormatUInt3: case MTLVertexFormatInt3:
            return 12;
        case MTLVertexFormatFloat4:
        case MTLVertexFormatUInt4: case MTLVertexFormatInt4:
            return 16;
        default:
            return 4;
    }
}

// GL blend factor -> MTLBlendFactor. Handles the separate-alpha factors the
// same way the Vulkan table does (SRC_ALPHA_SATURATE has no alpha-src form).
MTLBlendFactor blend_factor_from_gl(GLenum f, bool alphaChannel) {
    switch (f) {
        case GL_ZERO:                return MTLBlendFactorZero;
        case GL_ONE:                 return MTLBlendFactorOne;
        case GL_SRC_COLOR:           return alphaChannel ? MTLBlendFactorSourceAlpha
                                                         : MTLBlendFactorSourceColor;
        case GL_ONE_MINUS_SRC_COLOR: return alphaChannel ? MTLBlendFactorOneMinusSourceAlpha
                                                         : MTLBlendFactorOneMinusSourceColor;
        case GL_DST_COLOR:           return alphaChannel ? MTLBlendFactorDestinationAlpha
                                                         : MTLBlendFactorDestinationColor;
        case GL_ONE_MINUS_DST_COLOR: return alphaChannel ? MTLBlendFactorOneMinusDestinationAlpha
                                                         : MTLBlendFactorOneMinusDestinationColor;
        case GL_SRC_ALPHA:           return MTLBlendFactorSourceAlpha;
        case GL_ONE_MINUS_SRC_ALPHA: return MTLBlendFactorOneMinusSourceAlpha;
        case GL_DST_ALPHA:           return MTLBlendFactorDestinationAlpha;
        case GL_ONE_MINUS_DST_ALPHA: return MTLBlendFactorOneMinusDestinationAlpha;
        case GL_CONSTANT_COLOR:      return MTLBlendFactorBlendColor;
        case GL_ONE_MINUS_CONSTANT_COLOR: return MTLBlendFactorOneMinusBlendColor;
        case GL_CONSTANT_ALPHA:      return MTLBlendFactorBlendAlpha;
        case GL_ONE_MINUS_CONSTANT_ALPHA: return MTLBlendFactorOneMinusBlendAlpha;
        case GL_SRC_ALPHA_SATURATE:  return alphaChannel ? MTLBlendFactorOne
                                                         : MTLBlendFactorSourceAlphaSaturated;
        case GL_SRC1_COLOR:          return alphaChannel ? MTLBlendFactorSource1Alpha
                                                         : MTLBlendFactorSource1Color;
        case GL_ONE_MINUS_SRC1_COLOR:return alphaChannel ? MTLBlendFactorOneMinusSource1Alpha
                                                         : MTLBlendFactorOneMinusSource1Color;
        case GL_SRC1_ALPHA:          return MTLBlendFactorSource1Alpha;
        case GL_ONE_MINUS_SRC1_ALPHA:return MTLBlendFactorOneMinusSource1Alpha;
        default:                     return MTLBlendFactorOne;
    }
}

MTLCompareFunction compare_func_from_gl(GLenum f) {
    switch (f) {
        case GL_NEVER:    return MTLCompareFunctionNever;
        case GL_LESS:     return MTLCompareFunctionLess;
        case GL_EQUAL:    return MTLCompareFunctionEqual;
        case GL_LEQUAL:   return MTLCompareFunctionLessEqual;
        case GL_GREATER:  return MTLCompareFunctionGreater;
        case GL_NOTEQUAL: return MTLCompareFunctionNotEqual;
        case GL_GEQUAL:   return MTLCompareFunctionGreaterEqual;
        case GL_ALWAYS:   return MTLCompareFunctionAlways;
        default:          return MTLCompareFunctionLess;
    }
}

MTLStencilOperation stencil_op_from_gl(GLenum op) {
    switch (op) {
        case GL_KEEP:      return MTLStencilOperationKeep;
        case GL_ZERO:      return MTLStencilOperationZero;
        case GL_REPLACE:   return MTLStencilOperationReplace;
        case GL_INCR:      return MTLStencilOperationIncrementClamp;
        case GL_INCR_WRAP: return MTLStencilOperationIncrementWrap;
        case GL_DECR:      return MTLStencilOperationDecrementClamp;
        case GL_DECR_WRAP: return MTLStencilOperationDecrementWrap;
        case GL_INVERT:    return MTLStencilOperationInvert;
        default:           return MTLStencilOperationKeep;
    }
}

MTLSamplerAddressMode wrap_mode_from_gl(GLenum w) {
    switch (w) {
        case GL_REPEAT:           return MTLSamplerAddressModeRepeat;
        case GL_MIRRORED_REPEAT:  return MTLSamplerAddressModeMirrorRepeat;
        case GL_CLAMP_TO_EDGE:    return MTLSamplerAddressModeClampToEdge;
        case GL_CLAMP_TO_BORDER:  return MTLSamplerAddressModeClampToZero;
        case GL_MIRROR_CLAMP_TO_EDGE: return MTLSamplerAddressModeMirrorClampToEdge;
        default:                  return MTLSamplerAddressModeClampToEdge;
    }
}

MTLPrimitiveType primitive_from_gl(GLenum m) {
    switch (m) {
        case GL_POINTS:         return MTLPrimitiveTypePoint;
        case GL_LINES:          return MTLPrimitiveTypeLine;
        case GL_LINE_STRIP:     return MTLPrimitiveTypeLineStrip;
        case GL_TRIANGLES:      return MTLPrimitiveTypeTriangle;
        case GL_TRIANGLE_STRIP: return MTLPrimitiveTypeTriangleStrip;
        case GL_TRIANGLE_FAN:   return MTLPrimitiveTypeTriangle; // expanded at draw time
        case GL_LINE_LOOP:      return MTLPrimitiveTypeLine;     // expanded at draw time
        default:                return MTLPrimitiveTypeTriangle;
    }
}

MTLIndexType index_type_from_int(int t) { // 0=U16 1=U32 2=U8
    return t == 1 ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16;
}

} // namespace dmt
} // namespace mithril

#endif // __APPLE__
