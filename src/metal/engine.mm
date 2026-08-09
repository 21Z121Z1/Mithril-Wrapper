// Mithril DirectMetal backend: SPIR-V -> MSL programs, cached Metal render
// pipelines, three reusable frame upload arenas, command encoding, and
// offscreen readback. No Vulkan or MoltenVK entry point is used here.

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "engine.h"

#include <util/log.h>

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

struct ResidentBuffer {
    id<MTLBuffer> buffer = nil;
    uint64_t content_version = 0;
    size_t size = 0;
};

struct PendingDraw {
    backend::DrawParams params;
    // Strong references make GL deletion safe for already-recorded work.
    id<MTLBuffer> resident_vertex = nil;
    id<MTLBuffer> resident_instance = nil;
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
    id<MTLTexture> color = nil;
    id<MTLTexture> depth_stencil = nil;
    id<MTLCommandBuffer> last_submitted = nil;
    std::array<FrameContext, kFrameCount> frames;
    NSUInteger next_frame = 0;
    NSUInteger width = kDefaultWidth;
    NSUInteger height = kDefaultHeight;
    std::unordered_map<uint64_t, Program> programs;
    std::unordered_map<std::string, PipelineBundle> pipelines;
    std::unordered_map<uint64_t, ResidentBuffer> resident_buffers;
    uint64_t pipeline_clock = 0;
    std::vector<PendingDraw> draws;
    std::vector<uint8_t> readback_pixels;
    bool initialized = false;
    bool frame_dirty = false;
    bool color_initialized = false;
    bool depth_initialized = false;
    GLbitfield clear_mask = 0;
    float clear_color[4] = {0.f, 0.f, 0.f, 0.f};
    double clear_depth = 1.0;
    uint32_t clear_stencil = 0;
    float viewport[4] = {0.f, 0.f, (float)kDefaultWidth,
                         (float)kDefaultHeight};
    float scissor[4] = {0.f, 0.f, (float)kDefaultWidth,
                        (float)kDefaultHeight};
    uint64_t bound_draw_fbo = 0;
    uint64_t bound_read_fbo = 0;
};

Engine& GetEngine() {
    static Engine engine;
    return engine;
}

id<MTLBuffer> RetainResidentBuffer(const backend::VertexStream& stream) {
    if (!stream.HasResidentSource()) return nil;
    auto& engine = GetEngine();
    ResidentBuffer& resident = engine.resident_buffers[stream.source_lifetime_id];
    if (!resident.buffer || resident.content_version != stream.source_content_version ||
        resident.size != stream.source_size) {
        resident.buffer = [engine.device newBufferWithBytes:stream.source_data
                                                    length:stream.source_size
                                                   options:MTLResourceStorageModeShared];
        if (!resident.buffer) {
            engine.resident_buffers.erase(stream.source_lifetime_id);
            return nil;
        }
        resident.buffer.label = @"Mithril resident GL buffer";
        resident.content_version = stream.source_content_version;
        resident.size = stream.source_size;
        static bool logged_resident_path = false;
        if (!logged_resident_path) {
            ML_LOG_INFO("metal: resident VBO path active (lifetime/version keyed)");
            logged_resident_path = true;
        }
    }
    return resident.buffer;
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
        if (resources.uniform_buffers.size() > 1) {
            ML_LOG_ERROR("metal: more than one uniform block per stage is not supported yet");
            return false;
        }

        for (const auto& block : resources.uniform_buffers) {
            const uint32_t set = compiler.get_decoration(
                block.id, spv::DecorationDescriptorSet);
            const uint32_t binding = compiler.get_decoration(
                block.id, spv::DecorationBinding);
            if (set != 0 || binding != 0) {
                ML_LOG_ERROR("metal: only the frontend loose-uniform block at set=0 binding=0 is supported");
                return false;
            }
            spirv_cross::MSLResourceBinding remap{};
            remap.stage = expected_model;
            remap.desc_set = set;
            remap.binding = binding;
            remap.msl_buffer = kUniformBufferIndex;
            compiler.add_msl_resource_binding(remap);

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
    const std::string key = PipelineKey(params);
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
    descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
    descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    descriptor.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    auto* color = descriptor.colorAttachments[0];
    color.writeMask = ColorWriteMask(params.pipeline);
    color.blendingEnabled = params.pipeline.blend_enable;
    color.sourceRGBBlendFactor = BlendFactor(params.pipeline.blend_src_rgb);
    color.destinationRGBBlendFactor = BlendFactor(params.pipeline.blend_dst_rgb);
    color.sourceAlphaBlendFactor = BlendFactor(params.pipeline.blend_src_alpha);
    color.destinationAlphaBlendFactor = BlendFactor(params.pipeline.blend_dst_alpha);
    color.rgbBlendOperation = BlendOperation(params.pipeline.blend_eq_rgb);
    color.alphaBlendOperation = BlendOperation(params.pipeline.blend_eq_alpha);

    NSError* error = nil;
    id<MTLRenderPipelineState> pipeline =
        [engine.device newRenderPipelineStateWithDescriptor:descriptor error:&error];
    if (!pipeline) {
        ML_LOG_ERROR("metal: render pipeline creation failed: %s",
                     error.localizedDescription.UTF8String ?: "unknown error");
        return nullptr;
    }

    MTLDepthStencilDescriptor* depth_descriptor = [MTLDepthStencilDescriptor new];
    depth_descriptor.depthCompareFunction = params.pipeline.depth_test
        ? CompareFunction(params.pipeline.depth_func) : MTLCompareFunctionAlways;
    depth_descriptor.depthWriteEnabled = params.pipeline.depth_test &&
                                         params.pipeline.depth_write;
    if (params.pipeline.stencil_test) {
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

FrameContext& AcquireFrame(NSUInteger upload_bytes, bool needs_readback) {
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
        frame.readback_row_bytes = AlignUp(engine.width * 4, 256);
        const NSUInteger bytes = frame.readback_row_bytes * engine.height;
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
                       const backend::PipelineState& state) {
    auto& engine = GetEngine();
    const double vx = std::clamp<double>(engine.viewport[0], 0, engine.width);
    const double vy_bottom = std::clamp<double>(engine.viewport[1], 0, engine.height);
    const double vw = std::clamp<double>(engine.viewport[2], 0, engine.width - vx);
    const double vh = std::clamp<double>(engine.viewport[3], 0, engine.height - vy_bottom);
    MTLViewport viewport{vx, engine.height - (vy_bottom + vh), vw, vh, 0.0, 1.0};
    [encoder setViewport:viewport];

    MTLScissorRect scissor{0, 0, engine.width, engine.height};
    if (state.scissor_test) {
        const NSUInteger sx = std::clamp<NSInteger>((NSInteger)engine.scissor[0], 0,
                                                     (NSInteger)engine.width);
        const NSUInteger sy_bottom = std::clamp<NSInteger>((NSInteger)engine.scissor[1], 0,
                                                            (NSInteger)engine.height);
        const NSUInteger sw = std::min<NSUInteger>((NSUInteger)std::max(0.f, engine.scissor[2]),
                                                    engine.width - sx);
        const NSUInteger sh = std::min<NSUInteger>((NSUInteger)std::max(0.f, engine.scissor[3]),
                                                    engine.height - sy_bottom);
        scissor = {sx, engine.height - (sy_bottom + sh), sw, sh};
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

bool EncodeDraws(id<MTLRenderCommandEncoder> encoder, FrameContext& frame) {
    auto& engine = GetEngine();
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
        ApplyDynamicState(encoder, draw.pipeline);
        [encoder setVertexBuffer:vertex_buffer offset:vertex_offset atIndex:0];
        if (instance_offset != NSNotFound)
            [encoder setVertexBuffer:instance_buffer offset:instance_offset atIndex:1];
        if (vertex_ubo != NSNotFound)
            [encoder setVertexBuffer:frame.upload offset:vertex_ubo
                             atIndex:kUniformBufferIndex];
        if (fragment_ubo != NSNotFound)
            [encoder setFragmentBuffer:frame.upload offset:fragment_ubo
                               atIndex:kUniformBufferIndex];

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

    FrameContext& frame = AcquireFrame(RequiredUploadBytes(), copy_for_readback);
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
        auto* color = pass.colorAttachments[0];
        color.texture = engine.color;
        color.storeAction = MTLStoreActionStore;
        if (engine.clear_mask & GL_COLOR_BUFFER_BIT) {
            color.loadAction = MTLLoadActionClear;
            color.clearColor = MTLClearColorMake(engine.clear_color[0],
                                                  engine.clear_color[1],
                                                  engine.clear_color[2],
                                                  engine.clear_color[3]);
        } else {
            color.loadAction = engine.color_initialized
                ? MTLLoadActionLoad : MTLLoadActionDontCare;
        }

        pass.depthAttachment.texture = engine.depth_stencil;
        pass.depthAttachment.storeAction = MTLStoreActionStore;
        pass.depthAttachment.loadAction = (engine.clear_mask & GL_DEPTH_BUFFER_BIT)
            ? MTLLoadActionClear
            : (engine.depth_initialized ? MTLLoadActionLoad : MTLLoadActionDontCare);
        pass.depthAttachment.clearDepth = engine.clear_depth;
        pass.stencilAttachment.texture = engine.depth_stencil;
        pass.stencilAttachment.storeAction = MTLStoreActionStore;
        pass.stencilAttachment.loadAction = (engine.clear_mask & GL_STENCIL_BUFFER_BIT)
            ? MTLLoadActionClear
            : (engine.depth_initialized ? MTLLoadActionLoad : MTLLoadActionDontCare);
        pass.stencilAttachment.clearStencil = engine.clear_stencil;

        id<MTLRenderCommandEncoder> encoder =
            [command renderCommandEncoderWithDescriptor:pass];
        if (!encoder) return false;
        encoder.label = @"Mithril DirectMetal GL render pass";
        if (!EncodeDraws(encoder, frame)) {
            [encoder endEncoding];
            return false;
        }
        [encoder endEncoding];
        engine.color_initialized = true;
        engine.depth_initialized = true;
    }

    if (copy_for_readback) {
        id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
        [blit copyFromTexture:engine.color
                 sourceSlice:0
                 sourceLevel:0
                sourceOrigin:MTLOriginMake(0, 0, 0)
                  sourceSize:MTLSizeMake(engine.width, engine.height, 1)
                    toBuffer:frame.readback
           destinationOffset:0
      destinationBytesPerRow:frame.readback_row_bytes
    destinationBytesPerImage:frame.readback_row_bytes * engine.height];
        [blit endEncoding];
    }

    [command commit];
    frame.command = command;
    engine.last_submitted = command;
    engine.draws.clear();
    engine.frame_dirty = false;
    engine.clear_mask = 0;
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
        ML_LOG_INFO("metal: DirectMetal initialized on %s",
                    engine.device.name.UTF8String ?: "unknown Metal device");
        return true;
    }
}

bool IsInitialized() { return GetEngine().initialized; }

bool SetTargetSize(uint32_t width, uint32_t height) {
    if (!width || !height || !EnsureInit()) return false;
    auto& engine = GetEngine();
    if (engine.width == width && engine.height == height) return true;
    WaitForAllFrames();
    engine.width = width;
    engine.height = height;
    engine.viewport[2] = width;
    engine.viewport[3] = height;
    engine.scissor[2] = width;
    engine.scissor[3] = height;
    engine.pipelines.clear();
    return CreateTargets();
}

uint32_t TargetWidth() { return static_cast<uint32_t>(GetEngine().width); }
uint32_t TargetHeight() { return static_cast<uint32_t>(GetEngine().height); }

void SetClearColor(float r, float g, float b, float a) {
    auto& engine = GetEngine();
    engine.clear_color[0] = r;
    engine.clear_color[1] = g;
    engine.clear_color[2] = b;
    engine.clear_color[3] = a;
}

void SetClearMask(GLbitfield mask) {
    auto& engine = GetEngine();
    engine.clear_mask |= mask;
    engine.frame_dirty = true;
}

void SetClearDepth(double depth) { GetEngine().clear_depth = depth; }
void SetClearStencil(GLint value) { GetEngine().clear_stencil = (uint32_t)value; }

void SetViewport(float x, float y, float width, float height) {
    auto& viewport = GetEngine().viewport;
    viewport[0] = x;
    viewport[1] = y;
    viewport[2] = width;
    viewport[3] = height;
}

void SetScissor(float x, float y, float width, float height) {
    auto& scissor = GetEngine().scissor;
    scissor[0] = x;
    scissor[1] = y;
    scissor[2] = width;
    scissor[3] = height;
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
    if (engine.bound_draw_fbo != 0) {
        WarnUnsupported("user framebuffer rendering");
        return false;
    }
    if (program->second.vertex.uses_sampled_images ||
        program->second.fragment.uses_sampled_images ||
        !params.sampler_binds.empty()) {
        WarnUnsupported("sampled textures and samplers");
        return false;
    }
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
    if (engine.bound_read_fbo != 0) {
        WarnUnsupported("user framebuffer readback");
        std::memset(output, 0, (size_t)width * height * 4);
        return;
    }
    @autoreleasepool {
        FrameContext* frame = nullptr;
        if (!SubmitInternal(true, true, &frame) || !frame || !frame->readback) {
            std::memset(output, 0, (size_t)width * height * 4);
            return;
        }
        engine.readback_pixels.resize(engine.width * engine.height * 4);
        const uint8_t* source = static_cast<const uint8_t*>(frame->readback.contents);
        for (NSUInteger row = 0; row < engine.height; ++row)
            std::memcpy(engine.readback_pixels.data() + row * engine.width * 4,
                        source + row * frame->readback_row_bytes,
                        engine.width * 4);

        uint8_t* destination = static_cast<uint8_t*>(output);
        std::memset(destination, 0, (size_t)width * height * 4);
        for (GLsizei row = 0; row < height; ++row) {
            const GLint gl_y = y + row;
            if (gl_y < 0 || gl_y >= (GLint)engine.height) continue;
            const GLint src_y = (GLint)engine.height - 1 - gl_y;
            const GLint left = std::max<GLint>(x, 0);
            const GLint right = std::min<GLint>(x + width, (GLint)engine.width);
            if (left >= right) continue;
            std::memcpy(destination + ((size_t)row * width + (left - x)) * 4,
                        engine.readback_pixels.data() +
                            ((size_t)src_y * engine.width + left) * 4,
                        (size_t)(right - left) * 4);
        }
    }
}

void UploadTexture(uint64_t, const backend::TexUpload&,
                   const backend::TexSamplerInfo&) {
    WarnUnsupported("texture upload");
}
void DestroyResidentTexture(uint64_t) {}
void CreateRenderbuffer(uint64_t, GLenum, uint32_t, uint32_t, uint32_t) {
    WarnUnsupported("renderbuffers");
}
void DestroyRenderbuffer(uint64_t) {}
void SetFramebuffer(uint64_t, const backend::FboSpec&) {
    WarnUnsupported("framebuffer attachments");
}
void DestroyFramebuffer(uint64_t) {}
void BindDrawFramebuffer(uint64_t id) { GetEngine().bound_draw_fbo = id; }
void BindReadFramebuffer(uint64_t id) { GetEngine().bound_read_fbo = id; }
uint32_t DrawTargetWidth() { return TargetWidth(); }
uint32_t DrawTargetHeight() { return TargetHeight(); }
void RefreshReadback() {}
void BlitFramebuffer(uint64_t, uint64_t, GLint, GLint, GLint, GLint,
                     GLint, GLint, GLint, GLint, GLbitfield, GLenum) {
    WarnUnsupported("framebuffer blit");
}

} // namespace mithril::metal
