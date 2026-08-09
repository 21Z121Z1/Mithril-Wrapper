// Mithril DirectMetal backend: SPIR-V -> MSL programs, cached Metal render
// pipelines, three reusable frame upload arenas, command encoding, and
// offscreen readback. No Vulkan or MoltenVK entry point is used here.

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "engine.h"

#include <util/log.h>
#include <shader/shader.h>

#include <spirv_cross.hpp>
#include <spirv_msl.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mithril::metal {
namespace {

constexpr NSUInteger kDefaultWidth = 512;
constexpr NSUInteger kDefaultHeight = 512;
constexpr NSUInteger kFrameCount = 3;
constexpr NSUInteger kUniformBufferIndex = 16;
constexpr NSUInteger kInitialUploadCapacity = 1u << 20;
constexpr size_t kMaxPipelineCacheEntries = 512;
constexpr size_t kMaxClearPipelineCacheEntries = 64;

NSUInteger AlignUp(NSUInteger value, NSUInteger alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

uint64_t HashWords(const std::vector<uint32_t>& vs,
                   const std::vector<uint32_t>& fs) {
    uint64_t hash = 1469598103934665603ULL;
    auto mix = [&hash](uint32_t value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    for (uint32_t value : vs) mix(value);
    mix(0x4d455441u); // stage separator: "META"
    for (uint32_t value : fs) mix(value);
    return hash ? hash : 1;
}

struct UboMember {
    std::string name;
    uint32_t offset = 0;
    uint32_t size = 0;
};

struct ShaderStage {
    id<MTLLibrary> library = nil;
    id<MTLFunction> function = nil;
    std::vector<UboMember> members;
    uint32_t ubo_size = 0;
    bool uses_sampled_images = false;
};

struct Program {
    uint64_t handle = 0;
    uint32_t references = 1;
    ShaderStage vertex;
    ShaderStage fragment;
};

struct PipelineBundle {
    id<MTLRenderPipelineState> pipeline = nil;
    id<MTLDepthStencilState> depth_stencil = nil;
    uint64_t program = 0;
    uint64_t last_use = 0;
};

struct ClearPipeline {
    id<MTLLibrary> library = nil;
    id<MTLRenderPipelineState> pipeline = nil;
    id<MTLDepthStencilState> depth_stencil = nil;
};

struct ResidentBuffer {
    id<MTLBuffer> buffer = nil;
    uint64_t content_version = 0;
    size_t size = 0;
};

struct ResidentTexture {
    id<MTLTexture> texture = nil;
    id<MTLSamplerState> sampler = nil;
    uint64_t content_version = 0;
    uint64_t sampler_version = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 0;
    uint32_t levels = 0;
    bool is_3d = false;
    bool is_cube = false;
};

struct BoundTexture {
    NSUInteger slot = 0;
    id<MTLTexture> texture = nil;
    id<MTLSamplerState> sampler = nil;
};

struct BoundUniformBuffer {
    NSUInteger index = 0;
    NSUInteger offset = 0;
    id<MTLBuffer> buffer = nil;
    bool vertex_stage = false;
    bool fragment_stage = false;
};

struct Renderbuffer {
    id<MTLTexture> texture = nil;
    id<MTLTexture> resolve = nil;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t samples = 1;
    bool depth_stencil = false;
};

struct Framebuffer {
    backend::FboSpec spec;
    std::string signature;
};

struct ResolvedTarget {
    std::vector<id<MTLTexture>> colors;
    std::vector<id<MTLTexture>> resolve_colors;
    id<MTLTexture> depth_stencil = nil;
    NSUInteger width = 0;
    NSUInteger height = 0;
    NSUInteger samples = 1;
};

struct PendingDraw {
    backend::DrawParams params;
    // Strong references make GL deletion safe for already-recorded work.
    id<MTLBuffer> resident_vertex = nil;
    id<MTLBuffer> resident_instance = nil;
    std::vector<BoundUniformBuffer> uniform_buffers;
    std::vector<BoundTexture> textures;
};

struct FrameContext {
    id<MTLCommandBuffer> command = nil;
    id<MTLBuffer> upload = nil;
    NSUInteger upload_capacity = 0;
    id<MTLBuffer> readback = nil;
    NSUInteger readback_capacity = 0;
    NSUInteger readback_row_bytes = 0;
};

struct Engine {
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    CAMetalLayer* layer = nil;
    id<MTLTexture> color = nil;
    id<MTLTexture> depth_stencil = nil;
    id<MTLCommandBuffer> last_submitted = nil;
    std::array<FrameContext, kFrameCount> frames;
    NSUInteger next_frame = 0;
    NSUInteger width = kDefaultWidth;
    NSUInteger height = kDefaultHeight;
    std::unordered_map<uint64_t, Program> programs;
    std::unordered_map<std::string, PipelineBundle> pipelines;
    std::unordered_map<std::string, ClearPipeline> clear_pipelines;
    std::unordered_map<uint64_t, ResidentBuffer> resident_buffers;
    std::unordered_map<uint64_t, ResidentTexture> textures;
    std::unordered_map<uint64_t, Renderbuffer> renderbuffers;
    std::unordered_map<uint64_t, Framebuffer> framebuffers;
    uint64_t pipeline_clock = 0;
    std::vector<PendingDraw> draws;
    std::vector<uint8_t> readback_pixels;
    bool initialized = false;
    bool frame_dirty = false;
    bool color_initialized = false;
    bool depth_initialized = false;
    bool has_clear = false;
    backend::ClearParams clear;
    uint64_t bound_draw_fbo = 0;
    uint64_t bound_read_fbo = 0;
};

Engine& GetEngine() {
    static Engine engine;
    return engine;
}

void WarnUnsupported(const char* feature);

id<MTLBuffer> RetainResidentBytes(const uint8_t* source_data,
                                  size_t source_size,
                                  uint64_t lifetime_id,
                                  uint64_t content_version) {
    if (!source_data || !source_size || !lifetime_id) return nil;
    auto& engine = GetEngine();
    ResidentBuffer& resident = engine.resident_buffers[lifetime_id];
    if (!resident.buffer || resident.content_version != content_version ||
        resident.size != source_size) {
        resident.buffer = [engine.device newBufferWithBytes:source_data
                                                    length:source_size
                                                   options:MTLResourceStorageModeShared];
        if (!resident.buffer) {
            engine.resident_buffers.erase(lifetime_id);
            return nil;
        }
        resident.buffer.label = @"Mithril resident GL buffer";
        resident.content_version = content_version;
        resident.size = source_size;
        static bool logged_resident_path = false;
        if (!logged_resident_path) {
            ML_LOG_INFO("metal: resident GL buffer path active "
                        "(lifetime/version keyed)");
            logged_resident_path = true;
        }
    }
    return resident.buffer;
}

id<MTLBuffer> RetainResidentBuffer(const backend::VertexStream& stream) {
    if (!stream.HasResidentSource()) return nil;
    return RetainResidentBytes(stream.source_data, stream.source_size,
                               stream.source_lifetime_id,
                               stream.source_content_version);
}

MTLSamplerMinMagFilter SamplerFilter(backend::TexFilter filter) {
    return filter == backend::TexFilter::Nearest
        ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
}

MTLSamplerAddressMode SamplerAddress(GLenum wrap) {
    switch (wrap) {
        case GL_CLAMP_TO_EDGE: return MTLSamplerAddressModeClampToEdge;
        case GL_CLAMP_TO_BORDER: return MTLSamplerAddressModeClampToBorderColor;
        case GL_MIRRORED_REPEAT: return MTLSamplerAddressModeMirrorRepeat;
        default: return MTLSamplerAddressModeRepeat;
    }
}

id<MTLSamplerState> CreateSampler(const backend::TexSamplerInfo& info,
                                  NSUInteger levels) {
    MTLSamplerDescriptor* descriptor = [[MTLSamplerDescriptor alloc] init];
    descriptor.minFilter = SamplerFilter(info.min);
    descriptor.magFilter = SamplerFilter(info.mag);
    descriptor.mipFilter = info.mip ? MTLSamplerMipFilterLinear
                                    : MTLSamplerMipFilterNotMipmapped;
    descriptor.sAddressMode = SamplerAddress(info.wrap_s);
    descriptor.tAddressMode = SamplerAddress(info.wrap_t);
    descriptor.rAddressMode = SamplerAddress(info.wrap_r);
    descriptor.borderColor = MTLSamplerBorderColorOpaqueWhite;
    descriptor.lodMinClamp = 0.0f;
    descriptor.lodMaxClamp = levels ? (float)(levels - 1) : 0.0f;
    return [GetEngine().device newSamplerStateWithDescriptor:descriptor];
}

bool TextureShapeMatches(const ResidentTexture& resident,
                         const backend::TexUpload& image) {
    return resident.texture && resident.width == image.width &&
           resident.height == image.height && resident.depth == image.depth &&
           resident.levels == image.mip.size() &&
           resident.is_3d == image.is_3d && resident.is_cube == image.is_cube;
}

id<MTLTexture> CreateTexture(const backend::TexUpload& image) {
    if (!image.width || !image.height || image.mip.empty()) return nil;
    if (image.is_3d && image.mip.size() > 1) {
        WarnUnsupported("mipmapped 3D textures");
        return nil;
    }
    MTLTextureDescriptor* descriptor = [[MTLTextureDescriptor alloc] init];
    descriptor.pixelFormat = MTLPixelFormatRGBA8Unorm;
    descriptor.width = image.width;
    descriptor.height = image.height;
    descriptor.mipmapLevelCount = image.mip.size();
    descriptor.storageMode = MTLStorageModeShared;
    descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
    const NSUInteger slices = std::max<uint32_t>(image.depth, 1);
    if (image.is_cube) {
        descriptor.textureType = MTLTextureTypeCube;
        descriptor.arrayLength = 1;
    } else if (image.is_3d) {
        descriptor.textureType = MTLTextureType3D;
        descriptor.depth = slices;
    } else if (slices > 1) {
        descriptor.textureType = MTLTextureType2DArray;
        descriptor.arrayLength = slices;
    } else {
        descriptor.textureType = MTLTextureType2D;
    }
    id<MTLTexture> texture = [GetEngine().device newTextureWithDescriptor:descriptor];
    if (!texture) return nil;
    texture.label = @"Mithril resident GL texture";

    for (NSUInteger level = 0; level < image.mip.size(); ++level) {
        const NSUInteger width = std::max<NSUInteger>(1, image.width >> level);
        const NSUInteger height = std::max<NSUInteger>(1, image.height >> level);
        const NSUInteger row_bytes = width * 4;
        const NSUInteger plane_bytes = row_bytes * height;
        const auto& bytes = image.mip[level];
        if (image.is_3d) {
            const NSUInteger expected = plane_bytes * slices;
            if (bytes.size() != expected) return nil;
            [texture replaceRegion:MTLRegionMake3D(0, 0, 0, width, height, slices)
                       mipmapLevel:level
                              slice:0
                         withBytes:bytes.data()
                       bytesPerRow:row_bytes
                     bytesPerImage:plane_bytes];
        } else {
            const NSUInteger layer_count = image.is_cube ? 6 : slices;
            if (bytes.size() != plane_bytes * layer_count) return nil;
            for (NSUInteger slice = 0; slice < layer_count; ++slice) {
                [texture replaceRegion:MTLRegionMake2D(0, 0, width, height)
                           mipmapLevel:level
                                  slice:slice
                              withBytes:bytes.data() + slice * plane_bytes
                            bytesPerRow:row_bytes
                          bytesPerImage:plane_bytes];
            }
        }
    }
    return texture;
}

bool ResolveAttachment(const backend::FboAttach& attachment,
                       id<MTLTexture>* texture, bool* is_depth = nullptr) {
    auto& engine = GetEngine();
    if (attachment.is_texture) {
        auto found = engine.textures.find(attachment.tex_id);
        if (found == engine.textures.end() || attachment.level != 0 ||
            attachment.layer != 0)
            return false;
        *texture = found->second.texture;
        if (is_depth) *is_depth = false;
        return *texture != nil;
    }
    if (attachment.rbo_id) {
        auto found = engine.renderbuffers.find(attachment.rbo_id);
        if (found == engine.renderbuffers.end()) return false;
        *texture = found->second.texture;
        if (is_depth) *is_depth = found->second.depth_stencil;
        return *texture != nil;
    }
    return false;
}

bool ResolveTarget(uint64_t fbo_id, ResolvedTarget* target) {
    if (!target) return false;
    auto& engine = GetEngine();
    *target = {};
    if (!fbo_id) {
        target->colors.push_back(engine.color);
        target->resolve_colors.push_back(nil);
        target->depth_stencil = engine.depth_stencil;
        target->width = engine.width;
        target->height = engine.height;
        return engine.color != nil;
    }
    auto found = engine.framebuffers.find(fbo_id);
    if (found == engine.framebuffers.end() || !found->second.spec.width ||
        !found->second.spec.height)
        return false;
    target->width = found->second.spec.width;
    target->height = found->second.spec.height;
    for (const auto& color : found->second.spec.color) {
        id<MTLTexture> texture = nil;
        ResolveAttachment(color, &texture);
        target->colors.push_back(texture);
        id<MTLTexture> resolve = nil;
        if (!color.is_texture && color.rbo_id) {
            auto renderbuffer = engine.renderbuffers.find(color.rbo_id);
            if (renderbuffer != engine.renderbuffers.end()) {
                resolve = renderbuffer->second.resolve;
                if (target->samples != 1 &&
                    target->samples != renderbuffer->second.samples)
                    return false;
                target->samples = renderbuffer->second.samples;
            }
        }
        target->resolve_colors.push_back(resolve);
    }
    if (found->second.spec.has_depth) {
        bool depth = false;
        id<MTLTexture> depth_texture = nil;
        if (!ResolveAttachment(found->second.spec.depth,
                               &depth_texture, &depth) || !depth)
            return false;
        target->depth_stencil = depth_texture;
    }
    return !target->colors.empty() && target->colors.front() != nil;
}

std::string FramebufferSignature(const backend::FboSpec& spec) {
    std::ostringstream out;
    out << spec.width << 'x' << spec.height << '|' << spec.has_depth << '|'
        << spec.read_buf;
    auto append = [&out](const backend::FboAttach& attachment) {
        out << ':' << attachment.is_texture << ',' << attachment.tex_id << ','
            << attachment.level << ',' << attachment.layer << ','
            << attachment.rbo_id;
    };
    for (const auto& color : spec.color) append(color);
    if (spec.has_depth) append(spec.depth);
    out << "|d";
    for (GLenum draw_buffer : spec.draw_bufs) out << ':' << draw_buffer;
    return out.str();
}

bool CurrentTargetUsesTexture(uint64_t texture_id) {
    auto& engine = GetEngine();
    auto fbo = engine.framebuffers.find(engine.bound_draw_fbo);
    if (fbo == engine.framebuffers.end()) return false;
    for (const auto& color : fbo->second.spec.color)
        if (color.is_texture && color.tex_id == texture_id) return true;
    return fbo->second.spec.has_depth && fbo->second.spec.depth.is_texture &&
           fbo->second.spec.depth.tex_id == texture_id;
}

bool CurrentTargetUsesRenderbuffer(uint64_t renderbuffer_id) {
    auto& engine = GetEngine();
    auto fbo = engine.framebuffers.find(engine.bound_draw_fbo);
    if (fbo == engine.framebuffers.end()) return false;
    for (const auto& color : fbo->second.spec.color)
        if (!color.is_texture && color.rbo_id == renderbuffer_id) return true;
    return fbo->second.spec.has_depth && !fbo->second.spec.depth.is_texture &&
           fbo->second.spec.depth.rbo_id == renderbuffer_id;
}

void WarnUnsupported(const char* feature) {
    static std::unordered_set<std::string> warned;
    if (warned.emplace(feature).second)
        ML_LOG_ERROR("metal: %s is not supported by the current DirectMetal slice",
                     feature);
}

NSString* ToNSString(const std::string& string) {
    return [[NSString alloc] initWithBytes:string.data()
                                    length:string.size()
                                  encoding:NSUTF8StringEncoding];
}

bool HasUnsupportedResources(const spirv_cross::ShaderResources& resources) {
    return !resources.storage_buffers.empty() ||
           !resources.storage_images.empty() ||
           !resources.atomic_counters.empty() ||
           !resources.push_constant_buffers.empty() ||
           !resources.subpass_inputs.empty() ||
           !resources.separate_images.empty() ||
           !resources.separate_samplers.empty() ||
           !resources.acceleration_structures.empty();
}

bool TranslateStage(const std::vector<uint32_t>& words,
                    spv::ExecutionModel expected_model,
                    ShaderStage* output) {
    if (words.empty() || !output) return false;
    try {
        spirv_cross::CompilerMSL compiler(words);
        auto entry_points = compiler.get_entry_points_and_stages();
        if (entry_points.empty() || entry_points.front().execution_model != expected_model) {
            ML_LOG_ERROR("metal: SPIR-V stage does not match requested Metal stage");
            return false;
        }
        const std::string original_entry = entry_points.front().name;
        const auto resources = compiler.get_shader_resources();
        if (HasUnsupportedResources(resources)) {
            ML_LOG_ERROR("metal: shader uses a resource class not supported by this slice");
            return false;
        }
        if (resources.uniform_buffers.size() >
            shader::kMaxUserUniformBlocksPerStage + 1) {
            ML_LOG_ERROR("metal: shader exceeds the bounded per-stage uniform-block limit");
            return false;
        }

        for (const auto& block : resources.uniform_buffers) {
            const uint32_t set = compiler.get_decoration(
                block.id, spv::DecorationDescriptorSet);
            const uint32_t binding = compiler.get_decoration(
                block.id, spv::DecorationBinding);
            const bool loose = binding == shader::kLooseUniformBinding;
            const bool user = binding >= shader::kUserUniformBindingBase &&
                binding < shader::kUserUniformBindingBase +
                              shader::kMaxUserUniformBlocksPerStage;
            if (set != 0 || (!loose && !user)) {
                ML_LOG_ERROR("metal: uniform block uses an unknown internal binding");
                return false;
            }
            spirv_cross::MSLResourceBinding remap{};
            remap.stage = expected_model;
            remap.desc_set = set;
            remap.binding = binding;
            remap.msl_buffer = loose ? kUniformBufferIndex : binding;
            compiler.add_msl_resource_binding(remap);

            if (loose) {
                const auto& type = compiler.get_type(block.base_type_id);
                output->ubo_size = static_cast<uint32_t>(
                    compiler.get_declared_struct_size(type));
                for (uint32_t i = 0; i < type.member_types.size(); ++i) {
                    UboMember member;
                    member.name = compiler.get_member_name(block.base_type_id, i);
                    member.offset = compiler.get_member_decoration(
                        block.base_type_id, i, spv::DecorationOffset);
                    member.size = static_cast<uint32_t>(
                        compiler.get_declared_struct_member_size(type, i));
                    output->members.push_back(std::move(member));
                }
            }
        }

        output->uses_sampled_images = !resources.sampled_images.empty();
        for (const auto& sampled : resources.sampled_images) {
            const uint32_t set = compiler.get_decoration(
                sampled.id, spv::DecorationDescriptorSet);
            const uint32_t binding = compiler.get_decoration(
                sampled.id, spv::DecorationBinding);
            spirv_cross::MSLResourceBinding remap{};
            remap.stage = expected_model;
            remap.desc_set = set;
            remap.binding = binding;
            const uint32_t slot = binding ? binding - 1 : 0;
            remap.msl_texture = slot;
            remap.msl_sampler = slot;
            compiler.add_msl_resource_binding(remap);
        }

        spirv_cross::CompilerMSL::Options msl_options;
#if defined(MITHRIL_IOS)
        msl_options.platform = spirv_cross::CompilerMSL::Options::iOS;
#else
        msl_options.platform = spirv_cross::CompilerMSL::Options::macOS;
#endif
        msl_options.set_msl_version(2, 3);
        compiler.set_msl_options(msl_options);

        auto common_options = compiler.get_common_options();
        common_options.vertex.fixup_clipspace = true;
        compiler.set_common_options(common_options);

        const std::string source = compiler.compile();
        const std::string entry = compiler.get_cleansed_entry_point_name(
            original_entry, expected_model);

        NSError* error = nil;
        id<MTLLibrary> library = [GetEngine().device
            newLibraryWithSource:ToNSString(source)
                         options:nil
                           error:&error];
        if (!library) {
            ML_LOG_ERROR("metal: MSL compile failed: %s",
                         error.localizedDescription.UTF8String ?: "unknown error");
            return false;
        }
        id<MTLFunction> function = [library newFunctionWithName:ToNSString(entry)];
        if (!function) {
            ML_LOG_ERROR("metal: translated MSL entry '%s' was not found", entry.c_str());
            return false;
        }
        output->library = library;
        output->function = function;
        return true;
    } catch (const std::exception& error) {
        ML_LOG_ERROR("metal: SPIR-V to MSL failed: %s", error.what());
        return false;
    }
}

MTLVertexFormat VertexFormat(uint32_t components) {
    switch (components) {
        case 1: return MTLVertexFormatFloat;
        case 2: return MTLVertexFormatFloat2;
        case 3: return MTLVertexFormatFloat3;
        case 4: return MTLVertexFormatFloat4;
        default: return MTLVertexFormatInvalid;
    }
}

MTLCompareFunction CompareFunction(GLenum function) {
    switch (function) {
        case GL_NEVER: return MTLCompareFunctionNever;
        case GL_LESS: return MTLCompareFunctionLess;
        case GL_EQUAL: return MTLCompareFunctionEqual;
        case GL_LEQUAL: return MTLCompareFunctionLessEqual;
        case GL_GREATER: return MTLCompareFunctionGreater;
        case GL_NOTEQUAL: return MTLCompareFunctionNotEqual;
        case GL_GEQUAL: return MTLCompareFunctionGreaterEqual;
        default: return MTLCompareFunctionAlways;
    }
}

MTLStencilOperation StencilOperation(GLenum operation) {
    switch (operation) {
        case GL_ZERO: return MTLStencilOperationZero;
        case GL_REPLACE: return MTLStencilOperationReplace;
        case GL_INCR: return MTLStencilOperationIncrementClamp;
        case GL_DECR: return MTLStencilOperationDecrementClamp;
        case GL_INVERT: return MTLStencilOperationInvert;
        case GL_INCR_WRAP: return MTLStencilOperationIncrementWrap;
        case GL_DECR_WRAP: return MTLStencilOperationDecrementWrap;
        default: return MTLStencilOperationKeep;
    }
}

MTLBlendFactor BlendFactor(GLenum factor) {
    switch (factor) {
        case GL_ZERO: return MTLBlendFactorZero;
        case GL_ONE: return MTLBlendFactorOne;
        case GL_SRC_COLOR: return MTLBlendFactorSourceColor;
        case GL_ONE_MINUS_SRC_COLOR: return MTLBlendFactorOneMinusSourceColor;
        case GL_DST_COLOR: return MTLBlendFactorDestinationColor;
        case GL_ONE_MINUS_DST_COLOR: return MTLBlendFactorOneMinusDestinationColor;
        case GL_SRC_ALPHA: return MTLBlendFactorSourceAlpha;
        case GL_ONE_MINUS_SRC_ALPHA: return MTLBlendFactorOneMinusSourceAlpha;
        case GL_DST_ALPHA: return MTLBlendFactorDestinationAlpha;
        case GL_ONE_MINUS_DST_ALPHA: return MTLBlendFactorOneMinusDestinationAlpha;
        case GL_CONSTANT_COLOR: return MTLBlendFactorBlendColor;
        case GL_ONE_MINUS_CONSTANT_COLOR: return MTLBlendFactorOneMinusBlendColor;
        case GL_CONSTANT_ALPHA: return MTLBlendFactorBlendAlpha;
        case GL_ONE_MINUS_CONSTANT_ALPHA: return MTLBlendFactorOneMinusBlendAlpha;
        case GL_SRC_ALPHA_SATURATE: return MTLBlendFactorSourceAlphaSaturated;
        default: return MTLBlendFactorOne;
    }
}

MTLBlendOperation BlendOperation(GLenum operation) {
    switch (operation) {
        case GL_FUNC_SUBTRACT: return MTLBlendOperationSubtract;
        case GL_FUNC_REVERSE_SUBTRACT: return MTLBlendOperationReverseSubtract;
        case GL_MIN: return MTLBlendOperationMin;
        case GL_MAX: return MTLBlendOperationMax;
        default: return MTLBlendOperationAdd;
    }
}

MTLColorWriteMask ColorWriteMask(const backend::PipelineState& state) {
    MTLColorWriteMask mask = MTLColorWriteMaskNone;
    if (state.color_wmask_r) mask |= MTLColorWriteMaskRed;
    if (state.color_wmask_g) mask |= MTLColorWriteMaskGreen;
    if (state.color_wmask_b) mask |= MTLColorWriteMaskBlue;
    if (state.color_wmask_a) mask |= MTLColorWriteMaskAlpha;
    return mask;
}

void AppendPipelineState(std::ostringstream& key,
                         const backend::PipelineState& state) {
    key << '|'
        << state.depth_test << ':' << state.depth_func << ':' << (int)state.depth_write
        << '|' << state.stencil_test << ':' << state.stencil_front_func << ':'
        << state.stencil_back_func << ':' << state.stencil_front_read_mask << ':'
        << state.stencil_back_read_mask << ':' << state.stencil_front_write_mask << ':'
        << state.stencil_back_write_mask << ':' << state.stencil_front_op_fail << ':'
        << state.stencil_front_op_zfail << ':' << state.stencil_front_op_zpass << ':'
        << state.stencil_back_op_fail << ':' << state.stencil_back_op_zfail << ':'
        << state.stencil_back_op_zpass
        << '|' << state.blend_enable << ':' << state.blend_src_rgb << ':'
        << state.blend_dst_rgb << ':' << state.blend_src_alpha << ':'
        << state.blend_dst_alpha << ':' << state.blend_eq_rgb << ':'
        << state.blend_eq_alpha
        << '|' << (int)state.color_wmask_r << (int)state.color_wmask_g
        << (int)state.color_wmask_b << (int)state.color_wmask_a;
}

std::string PipelineKey(const backend::DrawParams& params) {
    std::ostringstream key;
    key << params.program << '|' << static_cast<int>(params.topology)
        << "|v" << params.vertex_stream.stride;
    for (const auto& attr : params.vertex_stream.attrs)
        key << ':' << attr.location << '@' << attr.offset << '/' << attr.components;
    key << "|i" << params.instance_stream.stride;
    for (const auto& attr : params.instance_stream.attrs)
        key << ':' << attr.location << '@' << attr.offset << '/' << attr.components;
    AppendPipelineState(key, params.pipeline);
    return key.str();
}

MTLStencilDescriptor* MakeStencilDescriptor(
    GLenum compare, GLenum fail, GLenum depth_fail, GLenum pass,
    GLuint read_mask, GLuint write_mask) {
    MTLStencilDescriptor* descriptor = [MTLStencilDescriptor new];
    descriptor.stencilCompareFunction = CompareFunction(compare);
    descriptor.stencilFailureOperation = StencilOperation(fail);
    descriptor.depthFailureOperation = StencilOperation(depth_fail);
    descriptor.depthStencilPassOperation = StencilOperation(pass);
    descriptor.readMask = read_mask;
    descriptor.writeMask = write_mask;
    return descriptor;
}

void EvictOldPipelineIfNeeded() {
    auto& engine = GetEngine();
    if (engine.pipelines.size() < kMaxPipelineCacheEntries) return;
    auto oldest = engine.pipelines.end();
    for (auto it = engine.pipelines.begin(); it != engine.pipelines.end(); ++it)
        if (oldest == engine.pipelines.end() ||
            it->second.last_use < oldest->second.last_use)
            oldest = it;
    if (oldest != engine.pipelines.end()) engine.pipelines.erase(oldest);
}

PipelineBundle* GetOrCreatePipeline(const backend::DrawParams& params) {
    auto& engine = GetEngine();
    ResolvedTarget target;
    if (!ResolveTarget(engine.bound_draw_fbo, &target)) return nullptr;
    std::ostringstream target_key;
    target_key << PipelineKey(params) << "|rt:" << target.colors.size() << ':'
               << (target.depth_stencil != nil)
               << ':' << target.samples;
    if (engine.bound_draw_fbo) {
        auto fbo = engine.framebuffers.find(engine.bound_draw_fbo);
        if (fbo != engine.framebuffers.end())
            for (GLenum draw_buffer : fbo->second.spec.draw_bufs)
                target_key << ':' << draw_buffer;
    }
    const std::string key = target_key.str();
    auto cached = engine.pipelines.find(key);
    if (cached != engine.pipelines.end()) {
        cached->second.last_use = ++engine.pipeline_clock;
        return &cached->second;
    }
    auto program_it = engine.programs.find(params.program);
    if (program_it == engine.programs.end()) return nullptr;

    MTLVertexDescriptor* vertex_descriptor = [MTLVertexDescriptor vertexDescriptor];
    auto add_stream = [&](const backend::VertexStream& stream, NSUInteger buffer_index,
                          MTLVertexStepFunction step) -> bool {
        if (stream.attrs.empty()) return true;
        if (!stream.stride) return false;
        vertex_descriptor.layouts[buffer_index].stride = stream.stride;
        vertex_descriptor.layouts[buffer_index].stepFunction = step;
        vertex_descriptor.layouts[buffer_index].stepRate = 1;
        for (const auto& attr : stream.attrs) {
            if (attr.location >= 31 || attr.offset >= stream.stride) return false;
            MTLVertexFormat format = VertexFormat(attr.components);
            if (format == MTLVertexFormatInvalid) return false;
            vertex_descriptor.attributes[attr.location].format = format;
            vertex_descriptor.attributes[attr.location].offset = attr.offset;
            vertex_descriptor.attributes[attr.location].bufferIndex = buffer_index;
        }
        return true;
    };
    if (!add_stream(params.vertex_stream, 0, MTLVertexStepFunctionPerVertex) ||
        !add_stream(params.instance_stream, 1, MTLVertexStepFunctionPerInstance)) {
        ML_LOG_ERROR("metal: invalid vertex stream description");
        return nullptr;
    }

    MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
    descriptor.vertexFunction = program_it->second.vertex.function;
    descriptor.fragmentFunction = program_it->second.fragment.function;
    descriptor.vertexDescriptor = vertex_descriptor;
    descriptor.rasterSampleCount = target.samples;
    descriptor.depthAttachmentPixelFormat = target.depth_stencil
        ? MTLPixelFormatDepth32Float_Stencil8 : MTLPixelFormatInvalid;
    descriptor.stencilAttachmentPixelFormat = target.depth_stencil
        ? MTLPixelFormatDepth32Float_Stencil8 : MTLPixelFormatInvalid;
    const backend::FboSpec* fbo_spec = nullptr;
    if (engine.bound_draw_fbo) {
        auto fbo = engine.framebuffers.find(engine.bound_draw_fbo);
        if (fbo != engine.framebuffers.end()) fbo_spec = &fbo->second.spec;
    }
    for (NSUInteger i = 0; i < target.colors.size(); ++i) {
        if (!target.colors[i]) continue;
        auto* color = descriptor.colorAttachments[i];
        color.pixelFormat = MTLPixelFormatRGBA8Unorm;
        bool enabled = true;
        if (fbo_spec && !fbo_spec->draw_bufs.empty()) {
            enabled = false;
            for (GLenum draw_buffer : fbo_spec->draw_bufs)
                if (draw_buffer == GL_COLOR_ATTACHMENT0 + i) enabled = true;
        }
        color.writeMask = enabled ? ColorWriteMask(params.pipeline)
                                  : MTLColorWriteMaskNone;
        color.blendingEnabled = params.pipeline.blend_enable;
        color.sourceRGBBlendFactor = BlendFactor(params.pipeline.blend_src_rgb);
        color.destinationRGBBlendFactor = BlendFactor(params.pipeline.blend_dst_rgb);
        color.sourceAlphaBlendFactor = BlendFactor(params.pipeline.blend_src_alpha);
        color.destinationAlphaBlendFactor = BlendFactor(params.pipeline.blend_dst_alpha);
        color.rgbBlendOperation = BlendOperation(params.pipeline.blend_eq_rgb);
        color.alphaBlendOperation = BlendOperation(params.pipeline.blend_eq_alpha);
    }

    NSError* error = nil;
    id<MTLRenderPipelineState> pipeline =
        [engine.device newRenderPipelineStateWithDescriptor:descriptor error:&error];
    if (!pipeline) {
        ML_LOG_ERROR("metal: render pipeline creation failed: %s",
                     error.localizedDescription.UTF8String ?: "unknown error");
        return nullptr;
    }

    MTLDepthStencilDescriptor* depth_descriptor = [MTLDepthStencilDescriptor new];
    depth_descriptor.depthCompareFunction = target.depth_stencil &&
                                             params.pipeline.depth_test
        ? CompareFunction(params.pipeline.depth_func) : MTLCompareFunctionAlways;
    depth_descriptor.depthWriteEnabled = target.depth_stencil &&
                                         params.pipeline.depth_test &&
                                         params.pipeline.depth_write;
    if (target.depth_stencil && params.pipeline.stencil_test) {
        depth_descriptor.frontFaceStencil = MakeStencilDescriptor(
            params.pipeline.stencil_front_func,
            params.pipeline.stencil_front_op_fail,
            params.pipeline.stencil_front_op_zfail,
            params.pipeline.stencil_front_op_zpass,
            params.pipeline.stencil_front_read_mask,
            params.pipeline.stencil_front_write_mask);
        depth_descriptor.backFaceStencil = MakeStencilDescriptor(
            params.pipeline.stencil_back_func,
            params.pipeline.stencil_back_op_fail,
            params.pipeline.stencil_back_op_zfail,
            params.pipeline.stencil_back_op_zpass,
            params.pipeline.stencil_back_read_mask,
            params.pipeline.stencil_back_write_mask);
    }
    id<MTLDepthStencilState> depth_state =
        [engine.device newDepthStencilStateWithDescriptor:depth_descriptor];
    if (!depth_state) return nullptr;

    EvictOldPipelineIfNeeded();
    PipelineBundle bundle;
    bundle.pipeline = pipeline;
    bundle.depth_stencil = depth_state;
    bundle.program = params.program;
    bundle.last_use = ++engine.pipeline_clock;
    auto inserted = engine.pipelines.emplace(key, std::move(bundle));
    return &inserted.first->second;
}

bool CreateTargets() {
    auto& engine = GetEngine();
    MTLTextureDescriptor* color = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                     width:engine.width
                                    height:engine.height
                                 mipmapped:NO];
    color.storageMode = MTLStorageModePrivate;
    color.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    engine.color = [engine.device newTextureWithDescriptor:color];

    MTLTextureDescriptor* depth = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8
                                     width:engine.width
                                    height:engine.height
                                 mipmapped:NO];
    depth.storageMode = MTLStorageModePrivate;
    depth.usage = MTLTextureUsageRenderTarget;
    engine.depth_stencil = [engine.device newTextureWithDescriptor:depth];
    engine.color_initialized = false;
    engine.depth_initialized = false;
    return engine.color != nil && engine.depth_stencil != nil;
}

void WaitForFrame(FrameContext& frame) {
    if (!frame.command) return;
    [frame.command waitUntilCompleted];
    frame.command = nil;
}

void WaitForAllFrames() {
    auto& engine = GetEngine();
    for (auto& frame : engine.frames) WaitForFrame(frame);
    if (engine.last_submitted) [engine.last_submitted waitUntilCompleted];
}

FrameContext& AcquireFrame(NSUInteger upload_bytes, bool needs_readback,
                           NSUInteger read_width, NSUInteger read_height) {
    auto& engine = GetEngine();
    FrameContext& frame = engine.frames[engine.next_frame];
    engine.next_frame = (engine.next_frame + 1) % kFrameCount;
    WaitForFrame(frame);

    if (upload_bytes > frame.upload_capacity) {
        NSUInteger capacity = std::max(kInitialUploadCapacity,
                                       frame.upload_capacity ?: kInitialUploadCapacity);
        while (capacity < upload_bytes) capacity *= 2;
        frame.upload = [engine.device newBufferWithLength:capacity
                                                   options:MTLResourceStorageModeShared];
        frame.upload_capacity = frame.upload ? capacity : 0;
    }
    if (needs_readback) {
        frame.readback_row_bytes = AlignUp(read_width * 4, 256);
        const NSUInteger bytes = frame.readback_row_bytes * read_height;
        if (bytes > frame.readback_capacity) {
            frame.readback = [engine.device newBufferWithLength:bytes
                                                        options:MTLResourceStorageModeShared];
            frame.readback_capacity = frame.readback ? bytes : 0;
        }
    }
    return frame;
}

NSUInteger UniformBytes(const ShaderStage& stage) {
    return stage.ubo_size ? AlignUp(stage.ubo_size, 256) : 0;
}

NSUInteger RequiredUploadBytes() {
    auto& engine = GetEngine();
    NSUInteger cursor = 0;
    auto add = [&cursor](NSUInteger size) {
        if (!size) return;
        cursor = AlignUp(cursor, 256);
        cursor += size;
    };
    for (const auto& pending : engine.draws) {
        const auto& draw = pending.params;
        if (!pending.resident_vertex)
            add(draw.vertex_stream.data.size() * sizeof(float));
        if (!pending.resident_instance)
            add(draw.instance_stream.data.size() * sizeof(float));
        if (draw.topology == backend::Topology::TriangleFan) {
            const NSUInteger source_count = draw.indices.empty()
                ? (draw.vertex_stream.record_count
                    ? draw.vertex_stream.record_count
                    : draw.vertex_stream.data.size() * sizeof(float) /
                          std::max<uint32_t>(draw.vertex_stream.stride, 1))
                : draw.indices.size();
            if (source_count >= 3) add((source_count - 2) * 3 * sizeof(uint32_t));
        } else {
            add(draw.indices.size() * sizeof(uint32_t));
        }
        auto program = engine.programs.find(draw.program);
        if (program != engine.programs.end()) {
            add(UniformBytes(program->second.vertex));
            add(UniformBytes(program->second.fragment));
        }
    }
    return cursor;
}

NSUInteger AllocateUpload(FrameContext& frame, NSUInteger* cursor,
                           const void* bytes, NSUInteger size) {
    if (!size) return 0;
    *cursor = AlignUp(*cursor, 256);
    const NSUInteger offset = *cursor;
    *cursor += size;
    if (!frame.upload || *cursor > frame.upload_capacity) return NSNotFound;
    if (bytes) std::memcpy(static_cast<uint8_t*>(frame.upload.contents) + offset,
                           bytes, size);
    else std::memset(static_cast<uint8_t*>(frame.upload.contents) + offset, 0, size);
    return offset;
}

NSUInteger PackUniforms(FrameContext& frame, NSUInteger* cursor,
                        const ShaderStage& stage,
                        const backend::DrawParams& draw,
                        std::unordered_map<std::string, NSUInteger>* memo) {
    if (!stage.ubo_size) return NSNotFound;
    std::vector<uint8_t> packed(AlignUp(stage.ubo_size, 256), 0);
    for (const auto& member : stage.members) {
        auto value = draw.uniforms.find(member.name);
        if (value == draw.uniforms.end() || value->second.empty()) continue;
        const size_t bytes = std::min<size_t>(
            member.size, value->second.size() * sizeof(float));
        if ((size_t)member.offset + bytes <= stage.ubo_size)
            std::memcpy(packed.data() + member.offset, value->second.data(), bytes);
    }
    // Exact byte identity is the only reuse criterion. The memo lives for one
    // frame arena, so offsets can never escape into a recycled frame context.
    std::string key(reinterpret_cast<const char*>(packed.data()), packed.size());
    auto existing = memo->find(key);
    if (existing != memo->end()) return existing->second;
    const NSUInteger offset = AllocateUpload(frame, cursor, packed.data(),
                                              packed.size());
    if (offset == NSNotFound) return NSNotFound;
    memo->emplace(std::move(key), offset);
    return offset;
}

std::vector<uint32_t> ExpandTriangleFan(const backend::DrawParams& draw) {
    const uint32_t vertex_count = draw.vertex_stream.record_count
        ? draw.vertex_stream.record_count
        : static_cast<uint32_t>(draw.vertex_stream.data.size() * sizeof(float) /
              std::max<uint32_t>(draw.vertex_stream.stride, 1));
    const uint32_t count = draw.indices.empty()
        ? vertex_count : static_cast<uint32_t>(draw.indices.size());
    std::vector<uint32_t> triangles;
    if (count < 3) return triangles;
    triangles.reserve((count - 2) * 3);
    auto at = [&](uint32_t index) {
        return draw.indices.empty() ? index : draw.indices[index];
    };
    for (uint32_t i = 1; i + 1 < count; ++i) {
        triangles.push_back(at(0));
        triangles.push_back(at(i));
        triangles.push_back(at(i + 1));
    }
    return triangles;
}

MTLPrimitiveType PrimitiveType(backend::Topology topology) {
    return topology == backend::Topology::TriangleStrip
        ? MTLPrimitiveTypeTriangleStrip : MTLPrimitiveTypeTriangle;
}

void ApplyDynamicState(id<MTLRenderCommandEncoder> encoder,
                       const backend::PipelineState& state,
                       const backend::DynamicState& dynamic,
                       NSUInteger target_width, NSUInteger target_height) {
    const double vx = std::clamp<double>(dynamic.viewport[0], 0, target_width);
    const double vy_bottom = std::clamp<double>(dynamic.viewport[1], 0, target_height);
    const double vw = std::clamp<double>(dynamic.viewport[2], 0, target_width - vx);
    const double vh = std::clamp<double>(dynamic.viewport[3], 0, target_height - vy_bottom);
    MTLViewport viewport{vx, target_height - (vy_bottom + vh), vw, vh, 0.0, 1.0};
    [encoder setViewport:viewport];

    MTLScissorRect scissor{0, 0, target_width, target_height};
    if (state.scissor_test) {
        const NSUInteger sx = std::clamp<NSInteger>((NSInteger)dynamic.scissor[0], 0,
                                                     (NSInteger)target_width);
        const NSUInteger sy_bottom = std::clamp<NSInteger>((NSInteger)dynamic.scissor[1], 0,
                                                            (NSInteger)target_height);
        const NSUInteger sw = std::min<NSUInteger>((NSUInteger)std::max(0.f, dynamic.scissor[2]),
                                                    target_width - sx);
        const NSUInteger sh = std::min<NSUInteger>((NSUInteger)std::max(0.f, dynamic.scissor[3]),
                                                    target_height - sy_bottom);
        scissor = {sx, target_height - (sy_bottom + sh), sw, sh};
    }
    [encoder setScissorRect:scissor];

    MTLCullMode cull = MTLCullModeNone;
    if (state.cull_test) {
        if (state.cull_face == GL_FRONT) cull = MTLCullModeFront;
        else if (state.cull_face == GL_BACK) cull = MTLCullModeBack;
    }
    [encoder setCullMode:cull];
    [encoder setFrontFacingWinding:state.front_face == GL_CCW
        ? MTLWindingCounterClockwise : MTLWindingClockwise];
    [encoder setTriangleFillMode:state.polygon_mode == GL_LINE
        ? MTLTriangleFillModeLines : MTLTriangleFillModeFill];
    [encoder setDepthBias:state.poly_offset_units
               slopeScale:state.poly_offset_factor
                    clamp:0.f];
    [encoder setBlendColorRed:state.blend_color[0]
                        green:state.blend_color[1]
                         blue:state.blend_color[2]
                        alpha:state.blend_color[3]];
    [encoder setStencilFrontReferenceValue:(uint32_t)state.stencil_front_ref
                        backReferenceValue:(uint32_t)state.stencil_back_ref];
}

bool ColorAttachmentEnabled(const backend::FboSpec* spec, NSUInteger index) {
    if (!spec || spec->draw_bufs.empty()) return true;
    for (GLenum draw_buffer : spec->draw_bufs)
        if (draw_buffer == GL_COLOR_ATTACHMENT0 + index) return true;
    return false;
}

MTLColorWriteMask ClearColorWriteMask(const backend::ClearParams& clear) {
    MTLColorWriteMask mask = MTLColorWriteMaskNone;
    if (clear.color_write[0]) mask |= MTLColorWriteMaskRed;
    if (clear.color_write[1]) mask |= MTLColorWriteMaskGreen;
    if (clear.color_write[2]) mask |= MTLColorWriteMaskBlue;
    if (clear.color_write[3]) mask |= MTLColorWriteMaskAlpha;
    return mask;
}

bool ClearCoversTarget(const backend::ClearParams& clear,
                       NSUInteger width, NSUInteger height) {
    if (!clear.scissor_test) return true;
    const int64_t x0 = std::max<int64_t>(0, clear.scissor[0]);
    const int64_t y0 = std::max<int64_t>(0, clear.scissor[1]);
    const int64_t x1 = std::min<int64_t>(width,
        static_cast<int64_t>(clear.scissor[0]) + clear.scissor[2]);
    const int64_t y1 = std::min<int64_t>(height,
        static_cast<int64_t>(clear.scissor[1]) + clear.scissor[3]);
    return x0 == 0 && y0 == 0 && x1 >= static_cast<int64_t>(width) &&
           y1 >= static_cast<int64_t>(height);
}

struct alignas(16) ClearUniforms {
    float color[4];
    float depth;
    float padding[3]{};
};

ClearPipeline* GetOrCreateClearPipeline(
    const ResolvedTarget& target, const backend::FboSpec* spec,
    const backend::ClearParams& clear, GLbitfield encoded_mask) {
    auto& engine = GetEngine();
    uint32_t color_output_mask = 0;
    if ((encoded_mask & GL_COLOR_BUFFER_BIT) &&
        ClearColorWriteMask(clear) != MTLColorWriteMaskNone) {
        for (NSUInteger i = 0; i < target.colors.size() && i < 32; ++i)
            if (target.colors[i] && ColorAttachmentEnabled(spec, i))
                color_output_mask |= 1u << i;
    }
    const bool clear_depth = target.depth_stencil &&
        (encoded_mask & GL_DEPTH_BUFFER_BIT) && clear.depth_write;
    const bool clear_stencil = target.depth_stencil &&
        (encoded_mask & GL_STENCIL_BUFFER_BIT) && clear.stencil_write_mask;
    if (!color_output_mask && !clear_depth && !clear_stencil) return nullptr;

    std::ostringstream key;
    key << color_output_mask << ':' << ClearColorWriteMask(clear) << ':'
        << clear_depth << ':' << clear_stencil << ':'
        << clear.stencil_write_mask << ':' << target.samples;
    for (id<MTLTexture> texture : target.colors)
        key << ':' << (texture ? texture.pixelFormat : MTLPixelFormatInvalid);
    key << ":d" << (target.depth_stencil
        ? target.depth_stencil.pixelFormat : MTLPixelFormatInvalid);
    const std::string cache_key = key.str();
    auto cached = engine.clear_pipelines.find(cache_key);
    if (cached != engine.clear_pipelines.end()) return &cached->second;

    std::ostringstream source;
    source << "#include <metal_stdlib>\nusing namespace metal;\n"
              "struct ClearUniforms { float4 color; float depth; };\n"
              "vertex float4 clear_vertex(uint id [[vertex_id]], "
              "constant ClearUniforms& u [[buffer(0)]]) {\n"
              "  const float2 p[3] = {float2(-1,-1), float2(3,-1), "
              "float2(-1,3)};\n"
              "  return float4(p[id], u.depth, 1.0);\n}\n";
    if (color_output_mask) {
        source << "struct ClearOutput {\n";
        for (NSUInteger i = 0; i < target.colors.size(); ++i)
            if (color_output_mask & (1u << i))
                source << "  float4 c" << i << " [[color(" << i << ")]];\n";
        source << "};\nfragment ClearOutput clear_fragment("
                  "constant ClearUniforms& u [[buffer(0)]]) {\n"
                  "  ClearOutput o;\n";
        for (NSUInteger i = 0; i < target.colors.size(); ++i)
            if (color_output_mask & (1u << i))
                source << "  o.c" << i << " = u.color;\n";
        source << "  return o;\n}\n";
    }

    NSError* library_error = nil;
    id<MTLLibrary> library = [engine.device
        newLibraryWithSource:ToNSString(source.str())
                     options:nil error:&library_error];
    if (!library) {
        ML_LOG_ERROR("metal: clear MSL compile failed: %s",
                     library_error.localizedDescription.UTF8String ?: "unknown error");
        return nullptr;
    }
    id<MTLFunction> vertex = [library newFunctionWithName:@"clear_vertex"];
    id<MTLFunction> fragment = color_output_mask
        ? [library newFunctionWithName:@"clear_fragment"] : nil;
    if (!vertex || (color_output_mask && !fragment)) return nullptr;

    MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
    descriptor.vertexFunction = vertex;
    descriptor.fragmentFunction = fragment;
    descriptor.rasterSampleCount = target.samples;
    if (target.depth_stencil) {
        descriptor.depthAttachmentPixelFormat = target.depth_stencil.pixelFormat;
        descriptor.stencilAttachmentPixelFormat = target.depth_stencil.pixelFormat;
    }
    for (NSUInteger i = 0; i < target.colors.size(); ++i) {
        if (!target.colors[i]) continue;
        descriptor.colorAttachments[i].pixelFormat = target.colors[i].pixelFormat;
        descriptor.colorAttachments[i].writeMask =
            (color_output_mask & (1u << i))
                ? ClearColorWriteMask(clear) : MTLColorWriteMaskNone;
    }
    NSError* pipeline_error = nil;
    id<MTLRenderPipelineState> pipeline =
        [engine.device newRenderPipelineStateWithDescriptor:descriptor
                                                       error:&pipeline_error];
    if (!pipeline) {
        ML_LOG_ERROR("metal: clear pipeline creation failed: %s",
                     pipeline_error.localizedDescription.UTF8String ?: "unknown error");
        return nullptr;
    }

    MTLDepthStencilDescriptor* depth = [MTLDepthStencilDescriptor new];
    depth.depthCompareFunction = MTLCompareFunctionAlways;
    depth.depthWriteEnabled = clear_depth;
    if (clear_stencil) {
        depth.frontFaceStencil = MakeStencilDescriptor(
            GL_ALWAYS, GL_KEEP, GL_KEEP, GL_REPLACE,
            0xFFFFFFFFu, clear.stencil_write_mask);
        depth.backFaceStencil = depth.frontFaceStencil;
    }
    id<MTLDepthStencilState> depth_state =
        [engine.device newDepthStencilStateWithDescriptor:depth];
    if (!depth_state) return nullptr;

    if (engine.clear_pipelines.size() >= kMaxClearPipelineCacheEntries)
        engine.clear_pipelines.erase(engine.clear_pipelines.begin());
    ClearPipeline bundle;
    bundle.library = library;
    bundle.pipeline = pipeline;
    bundle.depth_stencil = depth_state;
    auto inserted = engine.clear_pipelines.emplace(cache_key, std::move(bundle));
    return &inserted.first->second;
}

bool EncodeClear(id<MTLRenderCommandEncoder> encoder,
                 const ResolvedTarget& target, const backend::FboSpec* spec,
                 const backend::ClearParams& clear, GLbitfield encoded_mask) {
    if (!encoded_mask) return true;
    ClearPipeline* pipeline = GetOrCreateClearPipeline(
        target, spec, clear, encoded_mask);
    if (!pipeline) {
        const bool has_effective_clear =
            ((encoded_mask & GL_COLOR_BUFFER_BIT) &&
             ClearColorWriteMask(clear) != MTLColorWriteMaskNone) ||
            ((encoded_mask & GL_DEPTH_BUFFER_BIT) && clear.depth_write) ||
            ((encoded_mask & GL_STENCIL_BUFFER_BIT) && clear.stencil_write_mask);
        return !has_effective_clear;
    }

    MTLScissorRect scissor{0, 0, target.width, target.height};
    if (clear.scissor_test) {
        const int64_t x0 = std::clamp<int64_t>(clear.scissor[0], 0, target.width);
        const int64_t y0 = std::clamp<int64_t>(clear.scissor[1], 0, target.height);
        const int64_t x1 = std::clamp<int64_t>(
            static_cast<int64_t>(clear.scissor[0]) + clear.scissor[2],
            0, target.width);
        const int64_t y1 = std::clamp<int64_t>(
            static_cast<int64_t>(clear.scissor[1]) + clear.scissor[3],
            0, target.height);
        if (x1 <= x0 || y1 <= y0) return true;
        scissor = {static_cast<NSUInteger>(x0),
                   target.height - static_cast<NSUInteger>(y1),
                   static_cast<NSUInteger>(x1 - x0),
                   static_cast<NSUInteger>(y1 - y0)};
    }

    ClearUniforms uniforms{};
    std::copy(clear.color.begin(), clear.color.end(), uniforms.color);
    uniforms.depth = static_cast<float>(std::clamp(clear.depth, 0.0, 1.0));
    [encoder setRenderPipelineState:pipeline->pipeline];
    [encoder setDepthStencilState:pipeline->depth_stencil];
    [encoder setViewport:MTLViewport{0.0, 0.0, (double)target.width,
                                     (double)target.height, 0.0, 1.0}];
    [encoder setScissorRect:scissor];
    [encoder setCullMode:MTLCullModeNone];
    [encoder setStencilReferenceValue:clear.stencil];
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:0];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    return true;
}

bool EncodeDraws(id<MTLRenderCommandEncoder> encoder, FrameContext& frame) {
    auto& engine = GetEngine();
    ResolvedTarget target;
    if (!ResolveTarget(engine.bound_draw_fbo, &target)) return false;
    NSUInteger cursor = 0;
    std::unordered_map<std::string, NSUInteger> uniform_memo;
    for (const auto& pending : engine.draws) {
        const auto& draw = pending.params;
        PipelineBundle* pipeline = GetOrCreatePipeline(draw);
        auto program = engine.programs.find(draw.program);
        if (!pipeline || program == engine.programs.end()) return false;

        id<MTLBuffer> vertex_buffer = pending.resident_vertex;
        NSUInteger vertex_offset = (NSUInteger)draw.vertex_stream.binding_offset;
        const NSUInteger vertex_bytes = draw.vertex_stream.data.size() * sizeof(float);
        if (!vertex_buffer) {
            vertex_offset = AllocateUpload(
                frame, &cursor, draw.vertex_stream.data.data(), vertex_bytes);
            if (vertex_offset == NSNotFound) return false;
            vertex_buffer = frame.upload;
        }

        id<MTLBuffer> instance_buffer = pending.resident_instance;
        NSUInteger instance_offset = NSNotFound;
        if (instance_buffer) {
            instance_offset = (NSUInteger)draw.instance_stream.binding_offset;
        } else if (!draw.instance_stream.data.empty()) {
            instance_offset = AllocateUpload(
                frame, &cursor, draw.instance_stream.data.data(),
                draw.instance_stream.data.size() * sizeof(float));
            if (instance_offset == NSNotFound) return false;
            instance_buffer = frame.upload;
        }

        std::vector<uint32_t> fan_indices;
        const std::vector<uint32_t>* indices = &draw.indices;
        if (draw.topology == backend::Topology::TriangleFan) {
            fan_indices = ExpandTriangleFan(draw);
            indices = &fan_indices;
        }
        NSUInteger index_offset = NSNotFound;
        if (!indices->empty()) {
            index_offset = AllocateUpload(frame, &cursor, indices->data(),
                                           indices->size() * sizeof(uint32_t));
            if (index_offset == NSNotFound) return false;
        }

        const NSUInteger vertex_ubo = PackUniforms(
            frame, &cursor, program->second.vertex, draw, &uniform_memo);
        const NSUInteger fragment_ubo = PackUniforms(
            frame, &cursor, program->second.fragment, draw, &uniform_memo);

        [encoder setRenderPipelineState:pipeline->pipeline];
        [encoder setDepthStencilState:pipeline->depth_stencil];
        ApplyDynamicState(encoder, draw.pipeline, draw.dynamic,
                          target.width, target.height);
        [encoder setVertexBuffer:vertex_buffer offset:vertex_offset atIndex:0];
        if (instance_offset != NSNotFound)
            [encoder setVertexBuffer:instance_buffer offset:instance_offset atIndex:1];
        if (vertex_ubo != NSNotFound)
            [encoder setVertexBuffer:frame.upload offset:vertex_ubo
                             atIndex:kUniformBufferIndex];
        if (fragment_ubo != NSNotFound)
            [encoder setFragmentBuffer:frame.upload offset:fragment_ubo
                               atIndex:kUniformBufferIndex];
        for (const auto& binding : pending.uniform_buffers) {
            if (binding.vertex_stage)
                [encoder setVertexBuffer:binding.buffer offset:binding.offset
                                 atIndex:binding.index];
            if (binding.fragment_stage)
                [encoder setFragmentBuffer:binding.buffer offset:binding.offset
                                   atIndex:binding.index];
        }
        for (const auto& binding : pending.textures) {
            [encoder setVertexTexture:binding.texture atIndex:binding.slot];
            [encoder setVertexSamplerState:binding.sampler atIndex:binding.slot];
            [encoder setFragmentTexture:binding.texture atIndex:binding.slot];
            [encoder setFragmentSamplerState:binding.sampler atIndex:binding.slot];
        }

        const NSUInteger vertex_count = draw.vertex_stream.record_count
            ? draw.vertex_stream.record_count
            : vertex_bytes / std::max<uint32_t>(draw.vertex_stream.stride, 1);
        const NSUInteger instance_count = std::max<uint32_t>(draw.instance_count, 1);
        const MTLPrimitiveType primitive = PrimitiveType(draw.topology);
        if (index_offset != NSNotFound) {
            [encoder drawIndexedPrimitives:primitive
                                indexCount:indices->size()
                                 indexType:MTLIndexTypeUInt32
                               indexBuffer:frame.upload
                         indexBufferOffset:index_offset
                             instanceCount:instance_count];
        } else {
            [encoder drawPrimitives:primitive
                        vertexStart:0
                        vertexCount:vertex_count
                      instanceCount:instance_count];
        }
    }
    return true;
}

bool CommandSucceeded(id<MTLCommandBuffer> command) {
    if (!command) return false;
    if (command.status == MTLCommandBufferStatusError) {
        ML_LOG_ERROR("metal: command buffer failed: %s",
                     command.error.localizedDescription.UTF8String ?: "unknown error");
        return false;
    }
    return true;
}

bool SubmitInternal(bool wait_for_completion, bool copy_for_readback,
                    FrameContext** submitted_frame) {
    auto& engine = GetEngine();
    if (!engine.initialized) return false;
    const bool needs_render = engine.frame_dirty;
    if (!needs_render && !copy_for_readback) {
        if (wait_for_completion && engine.last_submitted) {
            [engine.last_submitted waitUntilCompleted];
            return CommandSucceeded(engine.last_submitted);
        }
        return true;
    }

    ResolvedTarget draw_target;
    ResolvedTarget read_target;
    if (needs_render && !ResolveTarget(engine.bound_draw_fbo, &draw_target)) {
        ML_LOG_ERROR("metal: draw framebuffer %llu is not renderable",
                     (unsigned long long)engine.bound_draw_fbo);
        return false;
    }
    id<MTLTexture> read_color = nil;
    if (copy_for_readback) {
        if (!ResolveTarget(engine.bound_read_fbo, &read_target)) return false;
        NSUInteger read_index = 0;
        if (engine.bound_read_fbo) {
            auto fbo = engine.framebuffers.find(engine.bound_read_fbo);
            if (fbo != engine.framebuffers.end() &&
                fbo->second.spec.read_buf >= GL_COLOR_ATTACHMENT0)
                read_index = fbo->second.spec.read_buf - GL_COLOR_ATTACHMENT0;
        }
        if (read_index >= read_target.colors.size()) return false;
        read_color = read_target.resolve_colors[read_index]
            ? read_target.resolve_colors[read_index]
            : read_target.colors[read_index];
        if (!read_color)
            return false;
    }

    FrameContext& frame = AcquireFrame(
        RequiredUploadBytes(), copy_for_readback,
        copy_for_readback ? read_target.width : 0,
        copy_for_readback ? read_target.height : 0);
    if ((RequiredUploadBytes() && !frame.upload) ||
        (copy_for_readback && !frame.readback)) {
        ML_LOG_ERROR("metal: frame buffer allocation failed");
        return false;
    }
    id<MTLCommandBuffer> command = [engine.queue commandBuffer];
    if (!command) return false;
    command.label = @"Mithril DirectMetal frame";

    if (needs_render) {
        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        const backend::FboSpec* draw_spec = nullptr;
        if (engine.bound_draw_fbo) {
            auto fbo = engine.framebuffers.find(engine.bound_draw_fbo);
            if (fbo != engine.framebuffers.end()) draw_spec = &fbo->second.spec;
        }
        const bool full_clear = engine.has_clear &&
            ClearCoversTarget(engine.clear, draw_target.width, draw_target.height);
        const bool load_color_clear = full_clear &&
            (engine.clear.mask & GL_COLOR_BUFFER_BIT) &&
            ClearColorWriteMask(engine.clear) == MTLColorWriteMaskAll;
        const bool load_depth_clear = full_clear &&
            (engine.clear.mask & GL_DEPTH_BUFFER_BIT) && engine.clear.depth_write;
        const bool load_stencil_clear = full_clear &&
            (engine.clear.mask & GL_STENCIL_BUFFER_BIT) &&
            engine.clear.stencil_write_mask == 0xFFFFFFFFu;
        GLbitfield encoded_clear_mask = engine.has_clear ? engine.clear.mask : 0;
        if (load_color_clear) encoded_clear_mask &= ~GL_COLOR_BUFFER_BIT;
        if (load_depth_clear) encoded_clear_mask &= ~GL_DEPTH_BUFFER_BIT;
        if (load_stencil_clear) encoded_clear_mask &= ~GL_STENCIL_BUFFER_BIT;
        for (NSUInteger i = 0; i < draw_target.colors.size(); ++i) {
            if (!draw_target.colors[i]) continue;
            auto* color = pass.colorAttachments[i];
            color.texture = draw_target.colors[i];
            if (draw_target.resolve_colors[i]) {
                color.resolveTexture = draw_target.resolve_colors[i];
                color.storeAction = MTLStoreActionMultisampleResolve;
            } else {
                color.storeAction = MTLStoreActionStore;
            }
            const bool draw_buffer_enabled = ColorAttachmentEnabled(draw_spec, i);
            if (load_color_clear && draw_buffer_enabled) {
                color.loadAction = MTLLoadActionClear;
                color.clearColor = MTLClearColorMake(engine.clear.color[0],
                                                      engine.clear.color[1],
                                                      engine.clear.color[2],
                                                      engine.clear.color[3]);
            } else {
                color.loadAction = (!engine.bound_draw_fbo && !engine.color_initialized)
                    ? MTLLoadActionDontCare : MTLLoadActionLoad;
            }
        }

        if (draw_target.depth_stencil) {
            pass.depthAttachment.texture = draw_target.depth_stencil;
            pass.depthAttachment.storeAction = MTLStoreActionStore;
            pass.depthAttachment.loadAction = load_depth_clear
                ? MTLLoadActionClear : MTLLoadActionLoad;
            pass.depthAttachment.clearDepth = engine.clear.depth;
            pass.stencilAttachment.texture = draw_target.depth_stencil;
            pass.stencilAttachment.storeAction = MTLStoreActionStore;
            pass.stencilAttachment.loadAction =
                load_stencil_clear
                    ? MTLLoadActionClear : MTLLoadActionLoad;
            pass.stencilAttachment.clearStencil = engine.clear.stencil;
        }

        id<MTLRenderCommandEncoder> encoder =
            [command renderCommandEncoderWithDescriptor:pass];
        if (!encoder) return false;
        encoder.label = @"Mithril DirectMetal GL render pass";
        if (engine.has_clear && !EncodeClear(
                encoder, draw_target, draw_spec, engine.clear,
                encoded_clear_mask)) {
            [encoder endEncoding];
            return false;
        }
        if (!EncodeDraws(encoder, frame)) {
            [encoder endEncoding];
            return false;
        }
        [encoder endEncoding];
        if (!engine.bound_draw_fbo) {
            engine.color_initialized = true;
            engine.depth_initialized = true;
        }
    }

    if (copy_for_readback) {
        id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
        [blit copyFromTexture:read_color
                 sourceSlice:0
                 sourceLevel:0
                sourceOrigin:MTLOriginMake(0, 0, 0)
                  sourceSize:MTLSizeMake(read_target.width, read_target.height, 1)
                    toBuffer:frame.readback
           destinationOffset:0
      destinationBytesPerRow:frame.readback_row_bytes
    destinationBytesPerImage:frame.readback_row_bytes * read_target.height];
        [blit endEncoding];
    }

    [command commit];
    frame.command = command;
    engine.last_submitted = command;
    engine.draws.clear();
    engine.frame_dirty = false;
    engine.has_clear = false;
    if (submitted_frame) *submitted_frame = &frame;

    if (wait_for_completion) {
        [command waitUntilCompleted];
        if (!CommandSucceeded(command)) return false;
    }
    return true;
}

} // namespace

bool EnsureInit() {
    auto& engine = GetEngine();
    if (engine.initialized) return true;
    @autoreleasepool {
        engine.device = MTLCreateSystemDefaultDevice();
        if (!engine.device) {
            ML_LOG_ERROR("metal: MTLCreateSystemDefaultDevice returned nil");
            return false;
        }
        engine.queue = [engine.device newCommandQueue];
        if (!engine.queue || !CreateTargets()) {
            ML_LOG_ERROR("metal: device/queue/offscreen target initialization failed");
            return false;
        }
        engine.initialized = true;
        backend::TexUpload dummy;
        dummy.width = dummy.height = dummy.depth = 1;
        dummy.mip.push_back({255, 255, 255, 255});
        dummy.content_version = 1;
        backend::TexSamplerInfo dummy_sampler;
        dummy_sampler.state_version = 1;
        UploadTexture(0, dummy, dummy_sampler);
        if (!engine.textures.count(0)) {
            engine.initialized = false;
            ML_LOG_ERROR("metal: failed to create fallback texture");
            return false;
        }
        ML_LOG_INFO("metal: DirectMetal initialized on %s",
                    engine.device.name.UTF8String ?: "unknown Metal device");
        return true;
    }
}

bool IsInitialized() { return GetEngine().initialized; }

static bool LayerDrawableSize(CAMetalLayer* layer, uint32_t* width,
                              uint32_t* height) {
    if (!layer || layer.drawableSize.width <= 0 ||
        layer.drawableSize.height <= 0 ||
        layer.drawableSize.width > std::numeric_limits<uint32_t>::max() ||
        layer.drawableSize.height > std::numeric_limits<uint32_t>::max())
        return false;
    *width = static_cast<uint32_t>(layer.drawableSize.width);
    *height = static_cast<uint32_t>(layer.drawableSize.height);
    return *width != 0 && *height != 0;
}

static bool SyncLayerTargetSize() {
    auto& engine = GetEngine();
    uint32_t width = 0;
    uint32_t height = 0;
    if (!LayerDrawableSize(engine.layer, &width, &height)) {
        ML_LOG_ERROR("metal: CAMetalLayer has no drawable size");
        return false;
    }
    return SetTargetSize(width, height);
}

bool SetNativeWindow(void* native_window) {
    if (!native_window) {
        GetEngine().layer = nil;
        return true;
    }
    if ((reinterpret_cast<uintptr_t>(native_window) & (alignof(void*) - 1)) != 0)
        return false;
    if (!EnsureInit()) return false;
    id object = (__bridge id)native_window;
    if (![object isKindOfClass:[CAMetalLayer class]]) {
        ML_LOG_ERROR("metal: EGL native window is not a CAMetalLayer");
        return false;
    }
    auto& engine = GetEngine();
    CAMetalLayer* layer = (CAMetalLayer*)object;
    layer.device = engine.device;
    layer.pixelFormat = MTLPixelFormatRGBA8Unorm;
    // The current presentation seam keeps the GL default framebuffer in a
    // private texture and copies it into the drawable.  A framebuffer-only
    // drawable cannot be a blit destination; this can become YES once the
    // default framebuffer renders directly into the CAMetalDrawable.
    layer.framebufferOnly = NO;
    if (layer.drawableSize.width <= 0 || layer.drawableSize.height <= 0)
        layer.drawableSize = CGSizeMake(engine.width, engine.height);
    engine.layer = layer;
    if (!SyncLayerTargetSize()) {
        engine.layer = nil;
        return false;
    }
    return true;
}

bool Present() {
    auto& engine = GetEngine();
    if (!engine.initialized || !engine.layer) {
        ML_LOG_ERROR("metal: eglSwapBuffers has no CAMetalLayer surface");
        return false;
    }
    @autoreleasepool {
        // Draws are recorded until submit, so resizing here re-targets the
        // complete pending GL frame without rendering an intermediate size.
        if (!SyncLayerTargetSize()) return false;
        if (!SubmitInternal(false, false, nullptr)) return false;
        id<CAMetalDrawable> drawable = [engine.layer nextDrawable];
        if (!drawable) {
            ML_LOG_ERROR("metal: CAMetalLayer returned no drawable");
            return false;
        }
        if (drawable.texture.width != engine.width ||
            drawable.texture.height != engine.height) {
            ML_LOG_ERROR("metal: drawable size changed without a surface resize");
            return false;
        }
        id<MTLCommandBuffer> command = [engine.queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
        [blit copyFromTexture:engine.color sourceSlice:0 sourceLevel:0
                 sourceOrigin:MTLOriginMake(0, 0, 0)
                   sourceSize:MTLSizeMake(engine.width, engine.height, 1)
                    toTexture:drawable.texture destinationSlice:0
             destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
        [blit endEncoding];
        [command presentDrawable:drawable];
        [command commit];
        engine.last_submitted = command;
        return true;
    }
}

bool SetTargetSize(uint32_t width, uint32_t height) {
    if (!width || !height || !EnsureInit()) return false;
    auto& engine = GetEngine();
    if (engine.width == width && engine.height == height) return true;
    WaitForAllFrames();
    engine.width = width;
    engine.height = height;
    engine.pipelines.clear();
    return CreateTargets();
}

uint32_t TargetWidth() {
    auto& engine = GetEngine();
    uint32_t width = 0;
    uint32_t height = 0;
    return LayerDrawableSize(engine.layer, &width, &height)
        ? width : static_cast<uint32_t>(engine.width);
}
uint32_t TargetHeight() {
    auto& engine = GetEngine();
    uint32_t width = 0;
    uint32_t height = 0;
    return LayerDrawableSize(engine.layer, &width, &height)
        ? height : static_cast<uint32_t>(engine.height);
}

bool Clear(const backend::ClearParams& params) {
    auto& engine = GetEngine();
    // Keep clears ordered with already-recorded draws while preserving the
    // common one-clear-per-frame path as a single render submission.
    if (engine.frame_dirty && !SubmitInternal(false, false, nullptr))
        return false;
    engine.clear = params;
    engine.has_clear = true;
    engine.frame_dirty = true;
    return true;
}

uint64_t CreateProgram(const std::vector<uint32_t>& vs,
                       const std::vector<uint32_t>& fs) {
    if (!EnsureInit()) return 0;
    auto& engine = GetEngine();
    const uint64_t handle = HashWords(vs, fs);
    auto existing = engine.programs.find(handle);
    if (existing != engine.programs.end()) {
        ++existing->second.references;
        return handle;
    }
    @autoreleasepool {
        Program program;
        program.handle = handle;
        if (!TranslateStage(vs, spv::ExecutionModelVertex, &program.vertex) ||
            !TranslateStage(fs, spv::ExecutionModelFragment, &program.fragment))
            return 0;
        engine.programs.emplace(handle, std::move(program));
        ML_LOG_DEBUG("metal: created native program %llu",
                     (unsigned long long)handle);
        return handle;
    }
}

void DestroyProgram(uint64_t handle) {
    auto& engine = GetEngine();
    auto program = engine.programs.find(handle);
    if (program == engine.programs.end()) return;
    if (--program->second.references) return;
    for (auto it = engine.pipelines.begin(); it != engine.pipelines.end();) {
        if (it->second.program == handle) it = engine.pipelines.erase(it);
        else ++it;
    }
    engine.programs.erase(program);
}

void DestroyBuffer(uint64_t lifetime_id) {
    // PendingDraw owns strong references, so erasing the reusable memo never
    // invalidates commands that have already captured this GL object.
    GetEngine().resident_buffers.erase(lifetime_id);
}

bool Draw(const backend::DrawParams& params) {
    auto& engine = GetEngine();
    if (!engine.initialized || !params.program || !params.vertex_stream.HasStorage())
        return false;
    auto program = engine.programs.find(params.program);
    if (program == engine.programs.end()) return false;
    ResolvedTarget draw_target;
    if (!ResolveTarget(engine.bound_draw_fbo, &draw_target)) return false;
    const bool uses_sampled_images = program->second.vertex.uses_sampled_images ||
                                     program->second.fragment.uses_sampled_images;
    if (uses_sampled_images && params.sampler_binds.empty()) return false;
    if (params.pipeline.polygon_mode == GL_POINT) {
        WarnUnsupported("GL_POINT polygon mode");
        return false;
    }
    if (params.pipeline.cull_test && params.pipeline.cull_face == GL_FRONT_AND_BACK) {
        WarnUnsupported("simultaneous front-and-back culling");
        return false;
    }
    PendingDraw pending;
    pending.params = params;
    if (params.vertex_stream.HasResidentSource()) {
        pending.resident_vertex = RetainResidentBuffer(params.vertex_stream);
        if (!pending.resident_vertex) return false;
        pending.params.vertex_stream.source_data = nullptr;
        pending.params.vertex_stream.source_size = 0;
    }
    if (params.instance_stream.HasResidentSource()) {
        pending.resident_instance = RetainResidentBuffer(params.instance_stream);
        if (!pending.resident_instance) return false;
        pending.params.instance_stream.source_data = nullptr;
        pending.params.instance_stream.source_size = 0;
    }
    for (size_t i = 0; i < params.uniform_buffers.size(); ++i) {
        const auto& binding = params.uniform_buffers[i];
        if (binding.internal_binding < shader::kUserUniformBindingBase ||
            binding.internal_binding >= shader::kUserUniformBindingBase +
                                            shader::kMaxUserUniformBlocksPerStage ||
            (!binding.vertex_stage && !binding.fragment_stage) ||
            binding.offset > binding.source_size ||
            binding.size > binding.source_size - binding.offset) {
            ML_LOG_ERROR("metal: invalid resolved uniform-buffer binding");
            return false;
        }
        id<MTLBuffer> resident = RetainResidentBytes(
            binding.source_data, binding.source_size,
            binding.source_lifetime_id, binding.source_content_version);
        if (!resident) return false;
        pending.uniform_buffers.push_back({
            static_cast<NSUInteger>(binding.internal_binding),
            static_cast<NSUInteger>(binding.offset), resident,
            binding.vertex_stage, binding.fragment_stage});
        pending.params.uniform_buffers[i].source_data = nullptr;
        pending.params.uniform_buffers[i].source_size = 0;
    }
    for (const auto& bind : params.sampler_binds) {
        const NSUInteger slot = bind.first ? bind.first - 1 : 0;
        if (slot >= backend::kMaxTextureUnits) {
            WarnUnsupported("sampled-image binding beyond the frontend limit");
            return false;
        }
        auto texture = engine.textures.find(bind.second);
        if (texture == engine.textures.end() || !texture->second.texture ||
            !texture->second.sampler) {
            ML_LOG_ERROR("metal: texture %llu is not resident for binding %u",
                         (unsigned long long)bind.second, bind.first);
            return false;
        }
        pending.textures.push_back(
            {slot, texture->second.texture, texture->second.sampler});
    }
    engine.draws.push_back(std::move(pending));
    engine.frame_dirty = true;
    return true;
}

void SubmitFlush(bool wait_for_completion) {
    @autoreleasepool {
        (void)SubmitInternal(wait_for_completion, false, nullptr);
    }
}

void ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, void* output) {
    if (!output || width <= 0 || height <= 0 || !EnsureInit()) return;
    auto& engine = GetEngine();
    @autoreleasepool {
        ResolvedTarget read_target;
        if (!ResolveTarget(engine.bound_read_fbo, &read_target)) {
            std::memset(output, 0, (size_t)width * height * 4);
            return;
        }
        FrameContext* frame = nullptr;
        if (!SubmitInternal(true, true, &frame) || !frame || !frame->readback) {
            std::memset(output, 0, (size_t)width * height * 4);
            return;
        }
        engine.readback_pixels.resize(read_target.width * read_target.height * 4);
        const uint8_t* source = static_cast<const uint8_t*>(frame->readback.contents);
        for (NSUInteger row = 0; row < read_target.height; ++row)
            std::memcpy(engine.readback_pixels.data() + row * read_target.width * 4,
                        source + row * frame->readback_row_bytes,
                        read_target.width * 4);

        uint8_t* destination = static_cast<uint8_t*>(output);
        std::memset(destination, 0, (size_t)width * height * 4);
        for (GLsizei row = 0; row < height; ++row) {
            const GLint gl_y = y + row;
            if (gl_y < 0 || gl_y >= (GLint)read_target.height) continue;
            const GLint src_y = (GLint)read_target.height - 1 - gl_y;
            const GLint left = std::max<GLint>(x, 0);
            const GLint right = std::min<GLint>(x + width, (GLint)read_target.width);
            if (left >= right) continue;
            std::memcpy(destination + ((size_t)row * width + (left - x)) * 4,
                        engine.readback_pixels.data() +
                            ((size_t)src_y * read_target.width + left) * 4,
                        (size_t)(right - left) * 4);
        }
    }
}

void UploadTexture(uint64_t texture_id, const backend::TexUpload& image,
                   const backend::TexSamplerInfo& sampler_info) {
    auto& engine = GetEngine();
    if (!engine.initialized || image.mip.empty()) return;
    if (engine.frame_dirty && CurrentTargetUsesTexture(texture_id) &&
        !SubmitInternal(false, false, nullptr))
        return;
    @autoreleasepool {
        auto existing = engine.textures.find(texture_id);
        const bool same_shape = existing != engine.textures.end() &&
                                TextureShapeMatches(existing->second, image);
        const bool needs_texture = !same_shape ||
            existing->second.content_version != image.content_version;
        const bool needs_sampler = existing == engine.textures.end() ||
            !existing->second.sampler ||
            existing->second.sampler_version != sampler_info.state_version ||
            existing->second.levels != image.mip.size();

        id<MTLTexture> texture = needs_texture ? CreateTexture(image)
            : existing->second.texture;
        id<MTLSamplerState> sampler = needs_sampler
            ? CreateSampler(sampler_info, image.mip.size())
            : existing->second.sampler;
        if (!texture || !sampler) {
            ML_LOG_ERROR("metal: failed to make texture %llu resident",
                         (unsigned long long)texture_id);
            engine.textures.erase(texture_id);
            return;
        }

        ResidentTexture resident;
        resident.texture = texture;
        resident.sampler = sampler;
        resident.content_version = image.content_version;
        resident.sampler_version = sampler_info.state_version;
        resident.width = image.width;
        resident.height = image.height;
        resident.depth = image.depth;
        resident.levels = image.mip.size();
        resident.is_3d = image.is_3d;
        resident.is_cube = image.is_cube;
        engine.textures[texture_id] = std::move(resident);
    }
}
void UpdateTextureSampler(uint64_t texture_id,
                          const backend::TexSamplerInfo& sampler_info) {
    auto& engine = GetEngine();
    auto texture = engine.textures.find(texture_id);
    if (texture == engine.textures.end()) return;
    @autoreleasepool {
        id<MTLSamplerState> sampler = CreateSampler(
            sampler_info, texture->second.levels);
        if (!sampler) {
            ML_LOG_ERROR("metal: failed to update sampler for texture %llu",
                         (unsigned long long)texture_id);
            return;
        }
        texture->second.sampler = sampler;
        texture->second.sampler_version = sampler_info.state_version;
    }
}
void DestroyResidentTexture(uint64_t id) {
    auto& engine = GetEngine();
    if (engine.frame_dirty && CurrentTargetUsesTexture(id))
        (void)SubmitInternal(false, false, nullptr);
    engine.textures.erase(id);
}
void CreateRenderbuffer(uint64_t renderbuffer_id, GLenum format,
                        uint32_t width, uint32_t height, uint32_t samples) {
    auto& engine = GetEngine();
    if (!engine.initialized || !width || !height) return;
    if (engine.frame_dirty && CurrentTargetUsesRenderbuffer(renderbuffer_id) &&
        !SubmitInternal(false, false, nullptr))
        return;
    const uint32_t sample_count = std::max<uint32_t>(samples, 1);
    if (![engine.device supportsTextureSampleCount:sample_count]) {
        WarnUnsupported("requested renderbuffer sample count");
        engine.renderbuffers.erase(renderbuffer_id);
        return;
    }
    const bool depth = format == GL_DEPTH_COMPONENT ||
                       format == GL_DEPTH_COMPONENT16 ||
                       format == GL_DEPTH_COMPONENT24 ||
                       format == GL_DEPTH_COMPONENT32F ||
                       format == GL_DEPTH24_STENCIL8 ||
                       format == GL_DEPTH32F_STENCIL8 ||
                       format == GL_DEPTH_STENCIL;
    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:depth
            ? MTLPixelFormatDepth32Float_Stencil8 : MTLPixelFormatRGBA8Unorm
                                     width:width height:height mipmapped:NO];
    descriptor.storageMode = MTLStorageModePrivate;
    descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    if (sample_count > 1) {
        descriptor.textureType = MTLTextureType2DMultisample;
        descriptor.sampleCount = sample_count;
        descriptor.mipmapLevelCount = 1;
    }
    id<MTLTexture> texture = [engine.device newTextureWithDescriptor:descriptor];
    if (!texture) {
        engine.renderbuffers.erase(renderbuffer_id);
        return;
    }
    Renderbuffer renderbuffer;
    renderbuffer.texture = texture;
    if (sample_count > 1 && !depth) {
        MTLTextureDescriptor* resolve = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                         width:width height:height mipmapped:NO];
        resolve.storageMode = MTLStorageModePrivate;
        resolve.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        renderbuffer.resolve = [engine.device newTextureWithDescriptor:resolve];
        if (!renderbuffer.resolve) {
            engine.renderbuffers.erase(renderbuffer_id);
            return;
        }
    }
    renderbuffer.width = width;
    renderbuffer.height = height;
    renderbuffer.samples = sample_count;
    renderbuffer.depth_stencil = depth;
    engine.renderbuffers[renderbuffer_id] = std::move(renderbuffer);
}
void DestroyRenderbuffer(uint64_t id) {
    auto& engine = GetEngine();
    if (engine.frame_dirty && CurrentTargetUsesRenderbuffer(id))
        (void)SubmitInternal(false, false, nullptr);
    engine.renderbuffers.erase(id);
}
void SetFramebuffer(uint64_t id, const backend::FboSpec& spec) {
    auto& engine = GetEngine();
    const std::string signature = FramebufferSignature(spec);
    auto existing = engine.framebuffers.find(id);
    if (existing != engine.framebuffers.end() &&
        existing->second.signature == signature)
        return;
    if (engine.bound_draw_fbo == id && engine.frame_dirty)
        (void)SubmitInternal(false, false, nullptr);
    Framebuffer& framebuffer = engine.framebuffers[id];
    framebuffer.spec = spec;
    framebuffer.signature = signature;
}
void DestroyFramebuffer(uint64_t id) { GetEngine().framebuffers.erase(id); }
void BindDrawFramebuffer(uint64_t id) {
    auto& engine = GetEngine();
    if (engine.bound_draw_fbo != id && engine.frame_dirty)
        (void)SubmitInternal(false, false, nullptr);
    engine.bound_draw_fbo = id;
}
void BindReadFramebuffer(uint64_t id) { GetEngine().bound_read_fbo = id; }
uint32_t DrawTargetWidth() {
    ResolvedTarget target;
    return ResolveTarget(GetEngine().bound_draw_fbo, &target)
        ? (uint32_t)target.width : 0;
}
uint32_t DrawTargetHeight() {
    ResolvedTarget target;
    return ResolveTarget(GetEngine().bound_draw_fbo, &target)
        ? (uint32_t)target.height : 0;
}
void RefreshReadback() {}
void BlitFramebuffer(uint64_t src_id, uint64_t dst_id,
                     GLint sx0, GLint sy0, GLint sx1, GLint sy1,
                     GLint dx0, GLint dy0, GLint dx1, GLint dy1,
                     GLbitfield mask, GLenum filter) {
    auto& engine = GetEngine();
    if (mask != GL_COLOR_BUFFER_BIT) {
        WarnUnsupported("depth/stencil or combined framebuffer blit");
        return;
    }
    const GLint source_width = std::abs(sx1 - sx0);
    const GLint source_height = std::abs(sy1 - sy0);
    const GLint destination_width = std::abs(dx1 - dx0);
    const GLint destination_height = std::abs(dy1 - dy0);
    if (sx1 < sx0 || sy1 < sy0 || dx1 < dx0 || dy1 < dy0 ||
        filter != GL_NEAREST || source_width != destination_width ||
        source_height != destination_height || source_width <= 0 ||
        source_height <= 0) {
        WarnUnsupported("scaled or filtered framebuffer blit");
        return;
    }
    if (engine.frame_dirty && !SubmitInternal(false, false, nullptr)) return;

    ResolvedTarget source;
    ResolvedTarget destination;
    if (!ResolveTarget(src_id, &source) || !ResolveTarget(dst_id, &destination))
        return;
    if (source.samples != 1 || destination.samples != 1) {
        WarnUnsupported("multisample framebuffer blit");
        return;
    }
    NSUInteger source_index = 0;
    auto src_fbo = engine.framebuffers.find(src_id);
    if (src_id && src_fbo != engine.framebuffers.end() &&
        src_fbo->second.spec.read_buf >= GL_COLOR_ATTACHMENT0)
        source_index = src_fbo->second.spec.read_buf - GL_COLOR_ATTACHMENT0;
    NSUInteger destination_index = 0;
    auto dst_fbo = engine.framebuffers.find(dst_id);
    if (dst_id && dst_fbo != engine.framebuffers.end() &&
        !dst_fbo->second.spec.draw_bufs.empty() &&
        dst_fbo->second.spec.draw_bufs.front() >= GL_COLOR_ATTACHMENT0)
        destination_index = dst_fbo->second.spec.draw_bufs.front() -
                            GL_COLOR_ATTACHMENT0;
    if (source_index >= source.colors.size() ||
        destination_index >= destination.colors.size() ||
        !source.colors[source_index] || !destination.colors[destination_index])
        return;

    id<MTLCommandBuffer> command = [engine.queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
    const NSUInteger source_x = std::min(sx0, sx1);
    const NSUInteger source_y = source.height - std::max(sy0, sy1);
    const NSUInteger destination_x = std::min(dx0, dx1);
    const NSUInteger destination_y = destination.height - std::max(dy0, dy1);
    [blit copyFromTexture:source.colors[source_index]
              sourceSlice:0 sourceLevel:0
             sourceOrigin:MTLOriginMake(source_x, source_y, 0)
               sourceSize:MTLSizeMake(source_width, source_height, 1)
                toTexture:destination.colors[destination_index]
         destinationSlice:0 destinationLevel:0
        destinationOrigin:MTLOriginMake(destination_x, destination_y, 0)];
    [blit endEncoding];
    [command commit];
    engine.last_submitted = command;
}

} // namespace mithril::metal
