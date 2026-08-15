// Mithril-Wrapper - MG_Backend/DirectMetal/MetalFormat.h
// Shared declarations for the enum-translation tables implemented in
// MetalFormat.mm (pure logic, no device dependency). Every DirectMetal TU
// that needs a VkFormat -> MTLPixelFormat / GL -> Metal enum mapping includes
// this header instead of re-declaring the functions locally.
#ifndef MITHRIL_DIRECTMETAL_FORMAT_H
#define MITHRIL_DIRECTMETAL_FORMAT_H

#ifdef __APPLE__

#import <Metal/Metal.h>

#include <vulkan/vulkan.h>
#include <GL/gl.h>

namespace mithril {
namespace dmt {

// VkFormat tag (the backend-neutral numbering shared with the frontend) ->
// MTLPixelFormat. MTLPixelFormatInvalid = unsupported.
MTLPixelFormat   pixel_format_from_vk(VkFormat f);

// True when the VkFormat tag carries a stencil aspect.
bool             format_has_stencil(VkFormat f);

// GL (type, size, normalized/integer) -> MTLVertexFormat.
// MTLVertexFormatInvalid on an unsupported combination.
MTLVertexFormat  vertex_format_from_gl(GLenum type, int size, int normalized,
                                       int integer);
// Per-format attribute byte size for the MTLVertexFormat values we emit.
NSUInteger       vertex_format_bytes(MTLVertexFormat f);

// GL blend factor -> MTLBlendFactor (alphaChannel picks the *_Alpha forms).
MTLBlendFactor   blend_factor_from_gl(GLenum f, bool alphaChannel);
MTLCompareFunction compare_func_from_gl(GLenum f);
MTLStencilOperation stencil_op_from_gl(GLenum op);
MTLSamplerAddressMode wrap_mode_from_gl(GLenum w);
MTLPrimitiveType primitive_from_gl(GLenum m);
// 0=U16, 1=U32, 2=U8 (U8 promoted to U16 by the expansion path at draw time).
MTLIndexType     index_type_from_int(int t);

} // namespace dmt
} // namespace mithril

#endif // __APPLE__
#endif // MITHRIL_DIRECTMETAL_FORMAT_H
