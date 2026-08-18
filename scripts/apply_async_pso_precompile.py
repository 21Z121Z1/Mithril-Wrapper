#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    n = text.count(old)
    if n != 1:
        raise SystemExit(f"{path}: expected one anchor, found {n}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1))


def insert_before(path: str, anchor: str, payload: str) -> None:
    p = Path(path)
    text = p.read_text()
    n = text.count(anchor)
    if n != 1:
        raise SystemExit(f"{path}: expected one insertion anchor, found {n}")
    p.write_text(text.replace(anchor, payload + anchor, 1))

# Diagnostics distinguish async scheduling/reuse/resolution, encode-time waits,
# and the correctness-preserving synchronous fallback.
replace_once(
    "include/mithril/directmetal_diagnostics.h",
    '''void mithrilResetDirectMetalProgramStats(void);
int mithrilGetDirectMetalProgramStatsV1(
    MithrilDirectMetalProgramStatsV1* output, size_t output_size);

#ifdef __cplusplus
''',
    '''void mithrilResetDirectMetalProgramStats(void);
int mithrilGetDirectMetalProgramStatsV1(
    MithrilDirectMetalProgramStatsV1* output, size_t output_size);

#define MITHRIL_DIRECT_METAL_PIPELINE_STATS_VERSION 1u

typedef struct MithrilDirectMetalPipelineStatsV1 {
    uint32_t version;
    uint32_t struct_size;
    uint64_t async_requests;
    uint64_t async_reuses;
    uint64_t async_resolved;
    uint64_t encode_waits;
    uint64_t sync_fallbacks;
    uint64_t pipeline_cache_hits;
} MithrilDirectMetalPipelineStatsV1;

void mithrilResetDirectMetalPipelineStats(void);
int mithrilGetDirectMetalPipelineStatsV1(
    MithrilDirectMetalPipelineStatsV1* output, size_t output_size);

#ifdef __cplusplus
''')

# Future type is referenced by PendingDraw before the numeric key aliases.
replace_once(
    "src/metal/engine.mm",
    '''struct OcclusionQueryState;

struct PendingDraw {
''',
    '''struct OcclusionQueryState;
struct PendingPipelineCompile;

struct PendingDraw {
''')
replace_once(
    "src/metal/engine.mm",
    '''    std::shared_ptr<PackedUniformSnapshot> uniform_snapshot;
    InlineList<BoundUniformBuffer, kMaxPendingUniformBufferBindings>
''',
    '''    std::shared_ptr<PackedUniformSnapshot> uniform_snapshot;
    std::shared_ptr<PendingPipelineCompile> pipeline_compile;
    InlineList<BoundUniformBuffer, kMaxPendingUniformBufferBindings>
''')

# Pipeline stats initialization.
replace_once(
    "src/metal/engine.mm",
    '''MithrilDirectMetalProgramStatsV1 EmptyProgramStats() {
    MithrilDirectMetalProgramStatsV1 stats{};
    stats.version = MITHRIL_DIRECT_METAL_PROGRAM_STATS_VERSION;
    stats.struct_size = static_cast<uint32_t>(sizeof(stats));
    return stats;
}
''',
    '''MithrilDirectMetalProgramStatsV1 EmptyProgramStats() {
    MithrilDirectMetalProgramStatsV1 stats{};
    stats.version = MITHRIL_DIRECT_METAL_PROGRAM_STATS_VERSION;
    stats.struct_size = static_cast<uint32_t>(sizeof(stats));
    return stats;
}

MithrilDirectMetalPipelineStatsV1 EmptyPipelineStats() {
    MithrilDirectMetalPipelineStatsV1 stats{};
    stats.version = MITHRIL_DIRECT_METAL_PIPELINE_STATS_VERSION;
    stats.struct_size = static_cast<uint32_t>(sizeof(stats));
    return stats;
}
''')

# Future lives after the key aliases so it can carry the exact identity. The
# async callback mutates only this object under its own mutex.
replace_once(
    "src/metal/engine.mm",
    '''using PipelineCacheKey = FixedNumericKey<96>;
using PipelineCacheKeyHash = FixedNumericKeyHash<96>;
using SamplerCacheKey = FixedNumericKey<24>;
using SamplerCacheKeyHash = FixedNumericKeyHash<24>;

struct Engine {
''',
    '''using PipelineCacheKey = FixedNumericKey<96>;
using PipelineCacheKeyHash = FixedNumericKeyHash<96>;
using SamplerCacheKey = FixedNumericKey<24>;
using SamplerCacheKeyHash = FixedNumericKeyHash<24>;

struct PendingPipelineCompile {
    PipelineCacheKey key;
    std::mutex mutex;
    std::condition_variable condition;
    bool completed = false;
    id<MTLRenderPipelineState> pipeline = nil;
    id<MTLDepthStencilState> depth_stencil = nil;
    uint64_t program = 0;
    std::string error;
};

struct Engine {
''')

replace_once(
    "src/metal/engine.mm",
    '''    std::unordered_map<PipelineCacheKey, PipelineBundle, PipelineCacheKeyHash>
        pipelines;
    std::unordered_map<std::string, ClearPipeline> clear_pipelines;
''',
    '''    std::unordered_map<PipelineCacheKey, PipelineBundle, PipelineCacheKeyHash>
        pipelines;
    std::unordered_map<PipelineCacheKey, std::shared_ptr<PendingPipelineCompile>,
                       PipelineCacheKeyHash> pending_pipelines;
    std::unordered_map<std::string, ClearPipeline> clear_pipelines;
''')
replace_once(
    "src/metal/engine.mm",
    '''    MithrilDirectMetalUniformStatsV1 uniform_stats = EmptyUniformStats();
    MithrilDirectMetalProgramStatsV1 program_stats = EmptyProgramStats();
''',
    '''    MithrilDirectMetalUniformStatsV1 uniform_stats = EmptyUniformStats();
    MithrilDirectMetalProgramStatsV1 program_stats = EmptyProgramStats();
    MithrilDirectMetalPipelineStatsV1 pipeline_stats = EmptyPipelineStats();
''')

# Replace the synchronous pipeline function with shared descriptor construction,
# async scheduling, and future-aware encode resolution. Current target/FBO state
# is supplied by the caller so EncodeDraws does not re-resolve it per draw.
p = Path("src/metal/engine.mm")
text = p.read_text()
start = text.index("PipelineBundle* GetOrCreatePipeline(const backend::DrawParams& params) {")
end = text.index("\nbool CreateTargets() {", start)
new_block = r'''struct PipelineBuildInputs {
    PipelineCacheKey key;
    MTLRenderPipelineDescriptor* descriptor = nil;
    id<MTLDepthStencilState> depth_stencil = nil;
    uint64_t program = 0;
};

const backend::FboSpec* BoundDrawFboSpec() {
    auto& engine = GetEngine();
    if (!engine.bound_draw_fbo) return nullptr;
    auto found = engine.framebuffers.find(engine.bound_draw_fbo);
    return found == engine.framebuffers.end() ? nullptr : &found->second.spec;
}

bool BuildPipelineInputs(const backend::DrawParams& params,
                         const ResolvedTarget& target,
                         const backend::FboSpec* fbo_spec,
                         PipelineBuildInputs* output) {
    if (!output ||
        !BuildPipelineCacheKey(params, target, fbo_spec, &output->key)) {
        ML_LOG_ERROR("metal: pipeline key exceeds fixed hot-path capacity");
        return false;
    }
    auto& engine = GetEngine();
    auto program_it = engine.programs.find(params.program);
    if (program_it == engine.programs.end()) return false;

    MTLVertexDescriptor* vertex_descriptor = [MTLVertexDescriptor vertexDescriptor];
    auto add_stream = [&](const backend::VertexStream& stream, NSUInteger buffer_index,
                          MTLVertexStepFunction step) -> bool {
        if (stream.attrs.empty()) return true;
        if (!stream.stride) return false;
        vertex_descriptor.layouts[buffer_index].stride = stream.stride;
        vertex_descriptor.layouts[buffer_index].stepFunction = step;
        vertex_descriptor.layouts[buffer_index].stepRate = 1;
        for (const auto& attr : stream.attrs) {
            const uint64_t attribute_end = static_cast<uint64_t>(attr.offset) +
                static_cast<uint64_t>(attr.components) *
                    backend::VertexScalarBytes(attr.scalar_type);
            if (attr.location >= 31 || attr.offset >= stream.stride ||
                attribute_end > stream.stride)
                return false;
            MTLVertexFormat format = VertexFormat(attr);
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
        return false;
    }

    MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
    descriptor.vertexFunction = program_it->second.vertex.function;
    descriptor.fragmentFunction = program_it->second.fragment.function;
    descriptor.vertexDescriptor = vertex_descriptor;
    descriptor.rasterSampleCount = target.samples;
    descriptor.depthAttachmentPixelFormat = target.depth_stencil
        ? target.depth_stencil.pixelFormat : MTLPixelFormatInvalid;
    descriptor.stencilAttachmentPixelFormat = target.has_stencil
        ? target.depth_stencil.pixelFormat : MTLPixelFormatInvalid;
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

    MTLDepthStencilDescriptor* depth_descriptor = [MTLDepthStencilDescriptor new];
    depth_descriptor.depthCompareFunction = target.depth_stencil &&
                                             params.pipeline.depth_test
        ? CompareFunction(params.pipeline.depth_func) : MTLCompareFunctionAlways;
    depth_descriptor.depthWriteEnabled = target.depth_stencil &&
                                         params.pipeline.depth_test &&
                                         params.pipeline.depth_write;
    if (target.has_stencil && params.pipeline.stencil_test) {
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
    if (!depth_state) return false;

    output->descriptor = descriptor;
    output->depth_stencil = depth_state;
    output->program = params.program;
    return true;
}

std::shared_ptr<PendingPipelineCompile> PreparePipelineCompile(
    const backend::DrawParams& params, const ResolvedTarget& target,
    const backend::FboSpec* fbo_spec) {
    auto& engine = GetEngine();
    PipelineCacheKey key;
    if (!BuildPipelineCacheKey(params, target, fbo_spec, &key)) return nullptr;
    if (engine.pipelines.find(key) != engine.pipelines.end()) {
        ++engine.pipeline_stats.pipeline_cache_hits;
        return nullptr;
    }
    auto pending = engine.pending_pipelines.find(key);
    if (pending != engine.pending_pipelines.end()) {
        ++engine.pipeline_stats.async_reuses;
        return pending->second;
    }
    if (engine.pending_pipelines.size() >= kMaxPipelineCacheEntries)
        return nullptr;

    PipelineBuildInputs inputs;
    if (!BuildPipelineInputs(params, target, fbo_spec, &inputs)) return nullptr;
    auto future = std::make_shared<PendingPipelineCompile>();
    future->key = inputs.key;
    future->depth_stencil = inputs.depth_stencil;
    future->program = inputs.program;
    engine.pending_pipelines.emplace(future->key, future);
    ++engine.pipeline_stats.async_requests;

    [engine.device newRenderPipelineStateWithDescriptor:inputs.descriptor
        completionHandler:^(id<MTLRenderPipelineState> pipeline, NSError* error) {
            std::lock_guard<std::mutex> lock(future->mutex);
            future->pipeline = pipeline;
            if (!pipeline && error) {
                const char* message = error.localizedDescription.UTF8String;
                future->error = message ? message : "unknown error";
            }
            future->completed = true;
            future->condition.notify_all();
        }];
    return future;
}

PipelineBundle* ResolvePreparedPipeline(
    const PipelineCacheKey& key,
    const std::shared_ptr<PendingPipelineCompile>& future) {
    if (!future || !(future->key == key)) return nullptr;
    auto& engine = GetEngine();
    id<MTLRenderPipelineState> pipeline = nil;
    id<MTLDepthStencilState> depth_state = nil;
    uint64_t program = 0;
    std::string error;
    {
        std::unique_lock<std::mutex> lock(future->mutex);
        if (!future->completed) {
            ++engine.pipeline_stats.encode_waits;
            future->condition.wait(lock, [&future] { return future->completed; });
        }
        pipeline = future->pipeline;
        depth_state = future->depth_stencil;
        program = future->program;
        error = future->error;
    }
    auto pending = engine.pending_pipelines.find(key);
    if (pending != engine.pending_pipelines.end() && pending->second == future)
        engine.pending_pipelines.erase(pending);
    if (!pipeline || !depth_state) {
        if (!error.empty())
            ML_LOG_WARN("metal: async render pipeline compile failed: %s; "
                        "retrying synchronously", error.c_str());
        return nullptr;
    }

    EvictOldPipelineIfNeeded();
    PipelineBundle bundle;
    bundle.pipeline = pipeline;
    bundle.depth_stencil = depth_state;
    bundle.program = program;
    bundle.last_use = ++engine.pipeline_clock;
    auto inserted = engine.pipelines.emplace(key, std::move(bundle));
    ++engine.pipeline_stats.async_resolved;
    return &inserted.first->second;
}

PipelineBundle* GetOrCreatePipeline(
    const backend::DrawParams& params, const ResolvedTarget& target,
    const backend::FboSpec* fbo_spec,
    const std::shared_ptr<PendingPipelineCompile>& prepared) {
    auto& engine = GetEngine();
    PipelineCacheKey key;
    if (!BuildPipelineCacheKey(params, target, fbo_spec, &key)) {
        ML_LOG_ERROR("metal: pipeline key exceeds fixed hot-path capacity");
        return nullptr;
    }
    auto cached = engine.pipelines.find(key);
    if (cached != engine.pipelines.end()) {
        cached->second.last_use = ++engine.pipeline_clock;
        ++engine.pipeline_stats.pipeline_cache_hits;
        return &cached->second;
    }
    if (PipelineBundle* resolved = ResolvePreparedPipeline(key, prepared))
        return resolved;

    ++engine.pipeline_stats.sync_fallbacks;
    PipelineBuildInputs inputs;
    if (!BuildPipelineInputs(params, target, fbo_spec, &inputs)) return nullptr;
    NSError* error = nil;
    id<MTLRenderPipelineState> pipeline =
        [engine.device newRenderPipelineStateWithDescriptor:inputs.descriptor
                                                       error:&error];
    if (!pipeline) {
        ML_LOG_ERROR("metal: render pipeline creation failed: %s",
                     error.localizedDescription.UTF8String ?: "unknown error");
        return nullptr;
    }

    EvictOldPipelineIfNeeded();
    PipelineBundle bundle;
    bundle.pipeline = pipeline;
    bundle.depth_stencil = inputs.depth_stencil;
    bundle.program = inputs.program;
    bundle.last_use = ++engine.pipeline_clock;
    auto inserted = engine.pipelines.emplace(inputs.key, std::move(bundle));
    return &inserted.first->second;
}
'''
text = text[:start] + new_block + text[end:]
p.write_text(text)

# EncodeDraws already resolves the target once. Reuse it and resolve the FBO
# spec once, then consume each PendingDraw's prepared future.
replace_once(
    "src/metal/engine.mm",
    '''    ResolvedTarget target;
    if (!ResolveTarget(engine.bound_draw_fbo, &target)) return false;
    NSUInteger cursor = 0;
''',
    '''    ResolvedTarget target;
    if (!ResolveTarget(engine.bound_draw_fbo, &target)) return false;
    const backend::FboSpec* fbo_spec = BoundDrawFboSpec();
    NSUInteger cursor = 0;
''')
replace_once(
    "src/metal/engine.mm",
    '''        PipelineBundle* pipeline = GetOrCreatePipeline(draw);
''',
    '''        PipelineBundle* pipeline = GetOrCreatePipeline(
            draw, target, fbo_spec, pending.pipeline_compile);
''')

# Schedule after all draw/resource validation has succeeded, immediately before
# moving the frontend snapshot into deferred storage. No failure remains after
# this point, so every scheduled future belongs to a recorded draw.
replace_once(
    "src/metal/engine.mm",
    '''    // All borrowed source pointers have been retained above. Move the
    // rich frontend snapshot into deferred storage exactly once instead of
    // deep-copying its vectors/maps at every draw.
    pending.params = std::move(params);
''',
    '''    const backend::FboSpec* fbo_spec = BoundDrawFboSpec();
    pending.pipeline_compile = PreparePipelineCompile(
        params, draw_target, fbo_spec);
    // All borrowed source pointers have been retained above. Move the
    // rich frontend snapshot into deferred storage exactly once instead of
    // deep-copying its vectors/maps at every draw.
    pending.params = std::move(params);
''')

# Exports.
insert_before(
    "src/metal/engine.mm",
    '''} // namespace mithril::metal''',
    r'''extern "C" void mithrilResetDirectMetalPipelineStats(void) {
    GetEngine().pipeline_stats = EmptyPipelineStats();
}

extern "C" int mithrilGetDirectMetalPipelineStatsV1(
    MithrilDirectMetalPipelineStatsV1* output, size_t output_size) {
    if (!output || output_size < sizeof(*output)) return 0;
    *output = GetEngine().pipeline_stats;
    return 1;
}

''')

# Dedicated oracle. Completion does not insert into the cache, so 32 queued
# identical draws deterministically produce one request + 31 future reuses even
# if Metal finishes compiling before the second draw is issued.
Path("tests/directmetal_async_pso_smoke.c").write_text(r'''/* DirectMetal asynchronous PSO precompile regression. */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../include/mithril/directmetal_diagnostics.h"

#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_MAX_SAMPLES 0x8D57
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_FLOAT 0x1406
#define GL_FALSE 0
#define GL_TRIANGLES 0x0004
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_NO_ERROR 0

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef long long GLsizeiptr;
typedef unsigned char GLboolean;

typedef GLuint (*PFN_CreateShader)(GLenum);
typedef void (*PFN_ShaderSource)(GLuint,GLsizei,const char* const*,const GLint*);
typedef void (*PFN_CompileShader)(GLuint);
typedef void (*PFN_GetShaderiv)(GLuint,GLenum,GLint*);
typedef GLuint (*PFN_CreateProgram)(void);
typedef void (*PFN_AttachShader)(GLuint,GLuint);
typedef void (*PFN_LinkProgram)(GLuint);
typedef void (*PFN_GetProgramiv)(GLuint,GLenum,GLint*);
typedef void (*PFN_UseProgram)(GLuint);
typedef void (*PFN_GetIntegerv)(GLenum,GLint*);
typedef void (*PFN_GenVertexArrays)(GLsizei,GLuint*);
typedef void (*PFN_BindVertexArray)(GLuint);
typedef void (*PFN_GenBuffers)(GLsizei,GLuint*);
typedef void (*PFN_BindBuffer)(GLenum,GLuint);
typedef void (*PFN_BufferData)(GLenum,GLsizeiptr,const void*,GLenum);
typedef void (*PFN_EnableVertexAttribArray)(GLuint);
typedef void (*PFN_VertexAttribPointer)(GLuint,GLint,GLenum,GLboolean,GLsizei,const void*);
typedef void (*PFN_DrawArrays)(GLenum,GLint,GLsizei);
typedef void (*PFN_ClearColor)(float,float,float,float);
typedef void (*PFN_Clear)(unsigned int);
typedef void (*PFN_Finish)(void);
typedef void (*PFN_ReadPixels)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void*);
typedef GLenum (*PFN_GetError)(void);
typedef void (*PFN_ResetStats)(void);
typedef int (*PFN_GetStats)(MithrilDirectMetalPipelineStatsV1*,size_t);

static int failures;
#define CHECK(c, fmt, ...) do { if (c) printf("ok  : " fmt "\n", ##__VA_ARGS__); \
    else { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; } } while (0)

int main(void) {
    enum { N = 32 };
    const char* lib=getenv("MITHRIL_LIBRARY");
    if(!lib||!*lib) lib="./output/libmithril.dylib";
    void* h=dlopen(lib,RTLD_NOW|RTLD_GLOBAL);
    if(!h){fprintf(stderr,"dlopen: %s\n",dlerror());return 2;}
#define LOAD(type,name,sym) type name=(type)dlsym(h,sym)
    LOAD(PFN_CreateShader,createShader,"glCreateShader");
    LOAD(PFN_ShaderSource,shaderSource,"glShaderSource");
    LOAD(PFN_CompileShader,compileShader,"glCompileShader");
    LOAD(PFN_GetShaderiv,getShaderiv,"glGetShaderiv");
    LOAD(PFN_CreateProgram,createProgram,"glCreateProgram");
    LOAD(PFN_AttachShader,attachShader,"glAttachShader");
    LOAD(PFN_LinkProgram,linkProgram,"glLinkProgram");
    LOAD(PFN_GetProgramiv,getProgramiv,"glGetProgramiv");
    LOAD(PFN_UseProgram,useProgram,"glUseProgram");
    LOAD(PFN_GetIntegerv,getIntegerv,"glGetIntegerv");
    LOAD(PFN_GenVertexArrays,genVertexArrays,"glGenVertexArrays");
    LOAD(PFN_BindVertexArray,bindVertexArray,"glBindVertexArray");
    LOAD(PFN_GenBuffers,genBuffers,"glGenBuffers");
    LOAD(PFN_BindBuffer,bindBuffer,"glBindBuffer");
    LOAD(PFN_BufferData,bufferData,"glBufferData");
    LOAD(PFN_EnableVertexAttribArray,enableVertexAttribArray,"glEnableVertexAttribArray");
    LOAD(PFN_VertexAttribPointer,vertexAttribPointer,"glVertexAttribPointer");
    LOAD(PFN_DrawArrays,drawArrays,"glDrawArrays");
    LOAD(PFN_ClearColor,clearColor,"glClearColor");
    LOAD(PFN_Clear,clear,"glClear");
    LOAD(PFN_Finish,finish,"glFinish");
    LOAD(PFN_ReadPixels,readPixels,"glReadPixels");
    LOAD(PFN_GetError,getError,"glGetError");
    LOAD(PFN_ResetStats,resetStats,"mithrilResetDirectMetalPipelineStats");
    LOAD(PFN_GetStats,getStats,"mithrilGetDirectMetalPipelineStatsV1");
#undef LOAD
    CHECK(createShader&&shaderSource&&compileShader&&getShaderiv&&createProgram&&
          attachShader&&linkProgram&&getProgramiv&&useProgram&&getIntegerv&&
          genVertexArrays&&bindVertexArray&&genBuffers&&bindBuffer&&bufferData&&
          enableVertexAttribArray&&vertexAttribPointer&&drawArrays&&clearColor&&
          clear&&finish&&readPixels&&getError&&resetStats&&getStats,
          "required async PSO symbols resolve");
    if(failures) return failures;

    GLint samples=0; getIntegerv(GL_MAX_SAMPLES,&samples);
    CHECK(samples>0,"backend initialized before program link (%d samples)",samples);
    const char* vs_src="#version 150\nlayout(location=0) in vec2 pos;\nvoid main(){gl_Position=vec4(pos,0,1);}\n";
    const char* fs_src="#version 150\nlayout(location=0) out vec4 color;\nvoid main(){color=vec4(1,0,0,1);}\n";
    GLuint vs=createShader(GL_VERTEX_SHADER), fs=createShader(GL_FRAGMENT_SHADER);
    shaderSource(vs,1,&vs_src,NULL); shaderSource(fs,1,&fs_src,NULL);
    compileShader(vs); compileShader(fs); GLint ok=0;
    getShaderiv(vs,GL_COMPILE_STATUS,&ok); CHECK(ok,"vertex shader compiles");
    getShaderiv(fs,GL_COMPILE_STATUS,&ok); CHECK(ok,"fragment shader compiles");
    GLuint program=createProgram(); attachShader(program,vs); attachShader(program,fs);
    linkProgram(program); getProgramiv(program,GL_LINK_STATUS,&ok); CHECK(ok,"program links/prewarms");
    useProgram(program);

    const float vertices[6]={-0.8f,-0.8f,0.8f,-0.8f,0.0f,0.8f};
    GLuint vao=0,vbo=0; genVertexArrays(1,&vao); bindVertexArray(vao);
    genBuffers(1,&vbo); bindBuffer(GL_ARRAY_BUFFER,vbo);
    bufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    enableVertexAttribArray(0); vertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),0);

    clearColor(0,0,0,1); clear(GL_COLOR_BUFFER_BIT); resetStats();
    for(int i=0;i<N;++i) drawArrays(GL_TRIANGLES,0,3);
    MithrilDirectMetalPipelineStatsV1 stats={0};
    CHECK(getStats(&stats,sizeof(stats)),"pre-submit PSO stats read succeeds");
    CHECK(stats.async_requests==1,"32 identical draws schedule one async PSO (%llu)",
          (unsigned long long)stats.async_requests);
    CHECK(stats.async_reuses==N-1,"remaining draws reuse pending PSO future (%llu)",
          (unsigned long long)stats.async_reuses);
    CHECK(stats.sync_fallbacks==0,"no synchronous PSO fallback before submit");

    finish();
    uint8_t px[4]={0}; readPixels(256,256,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);
    stats=(MithrilDirectMetalPipelineStatsV1){0}; getStats(&stats,sizeof(stats));
    CHECK(px[0]>=252&&px[1]<=3&&px[2]<=3&&px[3]>=252,
          "async-precompiled pipeline renders expected red pixel");
    CHECK(stats.async_resolved==1,"encode resolves one async PSO (%llu)",
          (unsigned long long)stats.async_resolved);
    CHECK(stats.sync_fallbacks==0,"first submit uses no synchronous PSO compile");

    drawArrays(GL_TRIANGLES,0,3); finish();
    stats=(MithrilDirectMetalPipelineStatsV1){0}; getStats(&stats,sizeof(stats));
    CHECK(stats.async_requests==1,"later identical draw schedules no new PSO");
    CHECK(stats.pipeline_cache_hits>=1,"later draw hits resident pipeline cache (%llu)",
          (unsigned long long)stats.pipeline_cache_hits);
    CHECK(stats.sync_fallbacks==0,"cached draw keeps synchronous fallback at zero");
    CHECK(getError()==GL_NO_ERROR,"async PSO scenario leaves GL_NO_ERROR");
    dlclose(h);
    return failures?1:0;
}
''')

replace_once(
    "cmake/MithrilSmokeTests.cmake",
    '''    directmetal_program_prewarm_smoke
    lazy_buffer_storage_smoke)''',
    '''    directmetal_program_prewarm_smoke
    directmetal_async_pso_smoke
    lazy_buffer_storage_smoke)''')

print("async DirectMetal PSO precompile phase applied")
