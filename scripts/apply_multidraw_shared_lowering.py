#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}: {old[:100]!r}")
    p.write_text(text.replace(old, new, 1))


def insert_before(path: str, anchor: str, payload: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(anchor)
    if count != 1:
        raise SystemExit(f"{path}: expected one insertion anchor, found {count}")
    p.write_text(text.replace(anchor, payload + anchor, 1))

# Public diagnostic contract for the frontend lowering shape.
Path("include/mithril/draw_diagnostics.h").write_text(r'''#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MITHRIL_DRAW_LOWERING_STATS_VERSION 1u

typedef struct MithrilDrawLoweringStatsV1 {
    uint32_t version;
    uint32_t struct_size;
    uint64_t shared_state_resolves;
    uint64_t geometry_lowerings;
    uint64_t multi_draw_calls;
    uint64_t multi_draw_subdraws;
} MithrilDrawLoweringStatsV1;

void mithrilResetDrawLoweringStats(void);
int mithrilGetDrawLoweringStatsV1(
    MithrilDrawLoweringStatsV1* output, size_t output_size);

#ifdef __cplusplus
}
#endif
''')

# Test the algorithmic shape separately from renderer correctness.
Path("tests/directmetal_multidraw_lowering_smoke.c").write_text(r'''/* MultiDraw shared-state lowering regression.
 *
 * Pixels remain the semantic oracle. Frontend diagnostics prove that each
 * MultiDraw call resolves invariant GL/backend state once while retaining one
 * geometry lowering per subdraw.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../include/mithril/draw_diagnostics.h"

#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_SHORT 0x1403
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
typedef void (*PFN_GenVertexArrays)(GLsizei,GLuint*);
typedef void (*PFN_BindVertexArray)(GLuint);
typedef void (*PFN_GenBuffers)(GLsizei,GLuint*);
typedef void (*PFN_BindBuffer)(GLenum,GLuint);
typedef void (*PFN_BufferData)(GLenum,GLsizeiptr,const void*,GLenum);
typedef void (*PFN_EnableVertexAttribArray)(GLuint);
typedef void (*PFN_VertexAttribPointer)(GLuint,GLint,GLenum,GLboolean,GLsizei,const void*);
typedef void (*PFN_DrawArrays)(GLenum,GLint,GLsizei);
typedef void (*PFN_MultiDrawArrays)(GLenum,const GLint*,const GLsizei*,GLsizei);
typedef void (*PFN_MultiDrawElements)(GLenum,const GLsizei*,GLenum,const void* const*,GLsizei);
typedef void (*PFN_MultiDrawElementsBaseVertex)(GLenum,const GLsizei*,GLenum,const void* const*,GLsizei,const GLint*);
typedef void (*PFN_ClearColor)(float,float,float,float);
typedef void (*PFN_Clear)(unsigned int);
typedef void (*PFN_Finish)(void);
typedef void (*PFN_ReadPixels)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void*);
typedef GLenum (*PFN_GetError)(void);
typedef void (*PFN_ResetStats)(void);
typedef int (*PFN_GetStats)(MithrilDrawLoweringStatsV1*,size_t);

static int failures;
#define CHECK(c, fmt, ...) do { if (c) printf("ok  : " fmt "\n", ##__VA_ARGS__); \
    else { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; } } while (0)

static int red_pixel(const uint8_t p[4]) {
    return p[0] >= 252 && p[1] <= 3 && p[2] <= 3 && p[3] >= 252;
}

static const char* VS =
    "#version 150\n"
    "layout(location=0) in vec2 pos;\n"
    "void main(){ gl_Position=vec4(pos,0.0,1.0); }\n";
static const char* FS =
    "#version 150\n"
    "layout(location=0) out vec4 color;\n"
    "void main(){ color=vec4(1.0,0.0,0.0,1.0); }\n";

int main(void) {
    enum { N = 16 };
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
    LOAD(PFN_GenVertexArrays,genVertexArrays,"glGenVertexArrays");
    LOAD(PFN_BindVertexArray,bindVertexArray,"glBindVertexArray");
    LOAD(PFN_GenBuffers,genBuffers,"glGenBuffers");
    LOAD(PFN_BindBuffer,bindBuffer,"glBindBuffer");
    LOAD(PFN_BufferData,bufferData,"glBufferData");
    LOAD(PFN_EnableVertexAttribArray,enableVertexAttribArray,"glEnableVertexAttribArray");
    LOAD(PFN_VertexAttribPointer,vertexAttribPointer,"glVertexAttribPointer");
    LOAD(PFN_DrawArrays,drawArrays,"glDrawArrays");
    LOAD(PFN_MultiDrawArrays,multiDrawArrays,"glMultiDrawArrays");
    LOAD(PFN_MultiDrawElements,multiDrawElements,"glMultiDrawElements");
    LOAD(PFN_MultiDrawElementsBaseVertex,multiDrawElementsBaseVertex,"glMultiDrawElementsBaseVertex");
    LOAD(PFN_ClearColor,clearColor,"glClearColor");
    LOAD(PFN_Clear,clear,"glClear");
    LOAD(PFN_Finish,finish,"glFinish");
    LOAD(PFN_ReadPixels,readPixels,"glReadPixels");
    LOAD(PFN_GetError,getError,"glGetError");
    LOAD(PFN_ResetStats,resetStats,"mithrilResetDrawLoweringStats");
    LOAD(PFN_GetStats,getStats,"mithrilGetDrawLoweringStatsV1");
#undef LOAD
    CHECK(createShader&&shaderSource&&compileShader&&getShaderiv&&createProgram&&
          attachShader&&linkProgram&&getProgramiv&&useProgram&&genVertexArrays&&
          bindVertexArray&&genBuffers&&bindBuffer&&bufferData&&
          enableVertexAttribArray&&vertexAttribPointer&&drawArrays&&
          multiDrawArrays&&multiDrawElements&&multiDrawElementsBaseVertex&&
          clearColor&&clear&&finish&&readPixels&&getError&&resetStats&&getStats,
          "required multidraw symbols resolve");
    if(failures) return failures;

    GLuint vs=createShader(GL_VERTEX_SHADER), fs=createShader(GL_FRAGMENT_SHADER);
    shaderSource(vs,1,&VS,NULL); shaderSource(fs,1,&FS,NULL);
    compileShader(vs); compileShader(fs);
    GLint ok=0; getShaderiv(vs,GL_COMPILE_STATUS,&ok); CHECK(ok,"vertex shader compiles");
    getShaderiv(fs,GL_COMPILE_STATUS,&ok); CHECK(ok,"fragment shader compiles");
    GLuint program=createProgram(); attachShader(program,vs); attachShader(program,fs);
    linkProgram(program); getProgramiv(program,GL_LINK_STATUS,&ok); CHECK(ok,"program links");
    useProgram(program);

    const float vertices[6]={-0.8f,-0.8f, 0.8f,-0.8f, 0.0f,0.8f};
    const uint16_t indices[3]={0,1,2};
    GLuint vao=0,vbo=0,ebo=0; genVertexArrays(1,&vao); bindVertexArray(vao);
    genBuffers(1,&vbo); bindBuffer(GL_ARRAY_BUFFER,vbo);
    bufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    enableVertexAttribArray(0); vertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),0);
    genBuffers(1,&ebo); bindBuffer(GL_ELEMENT_ARRAY_BUFFER,ebo);
    bufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices),indices,GL_STATIC_DRAW);

    GLint first[N]; GLsizei counts[N]; const void* offsets[N]; GLint bases[N];
    for(int i=0;i<N;++i){first[i]=0;counts[i]=3;offsets[i]=(const void*)0;bases[i]=0;}
    uint8_t px[4]={0}; MithrilDrawLoweringStatsV1 stats={0};

    clearColor(0,0,0,1); clear(GL_COLOR_BUFFER_BIT); resetStats();
    multiDrawArrays(GL_TRIANGLES,first,counts,N); finish();
    readPixels(256,256,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);
    CHECK(red_pixel(px),"MultiDrawArrays renders expected pixel");
    CHECK(getStats(&stats,sizeof(stats)),"MultiDrawArrays stats read succeeds");
    CHECK(stats.multi_draw_calls==1 && stats.multi_draw_subdraws==N,
          "MultiDrawArrays records one batch / %d subdraws",N);
    CHECK(stats.shared_state_resolves==1 && stats.geometry_lowerings==N,
          "MultiDrawArrays resolves shared state once (%llu/%llu)",
          (unsigned long long)stats.shared_state_resolves,
          (unsigned long long)stats.geometry_lowerings);

    resetStats();
    for(int i=0;i<N;++i) drawArrays(GL_TRIANGLES,0,3);
    finish(); stats=(MithrilDrawLoweringStatsV1){0};
    CHECK(getStats(&stats,sizeof(stats)),"ordinary draw stats read succeeds");
    CHECK(stats.shared_state_resolves==N && stats.geometry_lowerings==N,
          "%d ordinary draws resolve state %d times (%llu)",N,N,
          (unsigned long long)stats.shared_state_resolves);

    clear(GL_COLOR_BUFFER_BIT); resetStats();
    multiDrawElements(GL_TRIANGLES,counts,GL_UNSIGNED_SHORT,offsets,N); finish();
    readPixels(256,256,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);
    stats=(MithrilDrawLoweringStatsV1){0}; getStats(&stats,sizeof(stats));
    CHECK(red_pixel(px),"MultiDrawElements renders expected pixel");
    CHECK(stats.shared_state_resolves==1 && stats.geometry_lowerings==N,
          "MultiDrawElements resolves shared state once (%llu/%llu)",
          (unsigned long long)stats.shared_state_resolves,
          (unsigned long long)stats.geometry_lowerings);

    clear(GL_COLOR_BUFFER_BIT); resetStats();
    multiDrawElementsBaseVertex(GL_TRIANGLES,counts,GL_UNSIGNED_SHORT,offsets,N,bases);
    finish(); readPixels(256,256,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);
    stats=(MithrilDrawLoweringStatsV1){0}; getStats(&stats,sizeof(stats));
    CHECK(red_pixel(px),"MultiDrawElementsBaseVertex renders expected pixel");
    CHECK(stats.shared_state_resolves==1 && stats.geometry_lowerings==N,
          "MultiDrawElementsBaseVertex resolves shared state once (%llu/%llu)",
          (unsigned long long)stats.shared_state_resolves,
          (unsigned long long)stats.geometry_lowerings);

    CHECK(getError()==GL_NO_ERROR,"multidraw lowering scenario leaves GL_NO_ERROR");
    dlclose(h);
    return failures?1:0;
}
''')

# Register the dedicated DirectMetal runtime oracle.
replace_once(
    "cmake/MithrilSmokeTests.cmake",
    "    directmetal_uniform_snapshot_smoke\n    lazy_buffer_storage_smoke)",
    "    directmetal_uniform_snapshot_smoke\n    directmetal_multidraw_lowering_smoke\n    lazy_buffer_storage_smoke)")

# draw.cpp diagnostics include.
replace_once(
    "src/gl/draw.cpp",
    "#include <util/log.h>\n",
    "#include <util/log.h>\n#include <mithril/draw_diagnostics.h>\n")

shared_helper = r'''
struct SharedDrawState {
    bool ready = false;
    bool failed = false;
    sh::Program* program = nullptr;
    const VAOData* vao = nullptr;
    uint64_t backend_program = 0;
    std::vector<GLuint> vertex_slots;
    std::vector<GLuint> instance_slots;
    std::vector<sh::VertexInput> constant_inputs;
    v::LooseUniformSource loose_uniforms;
    std::vector<v::UniformBufferBinding> uniform_buffers;
    std::vector<v::SampledTextureBinding> sampled_textures;
    v::PipelineState pipeline;
    v::DynamicState dynamic;
    uint64_t occlusion_query = 0;
    GLenum provoking_vertex = GL_LAST_VERTEX_CONVENTION;
};

MithrilDrawLoweringStatsV1 EmptyDrawLoweringStats() {
    MithrilDrawLoweringStatsV1 stats{};
    stats.version = MITHRIL_DRAW_LOWERING_STATS_VERSION;
    stats.struct_size = static_cast<uint32_t>(sizeof(stats));
    return stats;
}

MithrilDrawLoweringStatsV1 g_draw_lowering_stats = EmptyDrawLoweringStats();

bool ResolveDrawSharedState(SharedDrawState* shared) {
    if (!shared) return false;
    if (shared->ready) return true;
    if (shared->failed) return false;
    ++g_draw_lowering_stats.shared_state_resolves;

    sh::Program* prog = sh::GetProgram(s::GetState().current_program);
    if (!prog || !prog->linked) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        shared->failed = true;
        return false;
    }
    if (!v::EnsureInit()) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        shared->failed = true;
        return false;
    }
    if (!g_dirty_textures.empty()) FlushDirtyTextureUploads();
    const uint64_t backend_program = CreateBackendProgram(prog);
    if (!backend_program) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        shared->failed = true;
        return false;
    }

    shared->program = prog;
    shared->backend_program = backend_program;
    shared->vao = &g_vaos[g_bound_vao];
    shared->vertex_slots.clear();
    shared->instance_slots.clear();
    shared->vertex_slots.reserve(kMaxAttribs);
    shared->instance_slots.reserve(kMaxAttribs);
    for (GLuint slot = 0; slot < kMaxAttribs; ++slot) {
        const AttribData& a = shared->vao->attribs[slot];
        if (!a.enabled) continue;
        (a.divisor ? shared->instance_slots : shared->vertex_slots).push_back(slot);
    }
    shared->constant_inputs.clear();
    shared->constant_inputs.reserve(prog->vertex_inputs.size());
    for (const sh::VertexInput& input : prog->vertex_inputs) {
        if (input.location >= kMaxAttribs) {
            PUSH_ERROR(GL_INVALID_OPERATION);
            shared->failed = true;
            return false;
        }
        if (!shared->vao->attribs[input.location].enabled)
            shared->constant_inputs.push_back(input);
    }

    shared->loose_uniforms.values = prog->loose_uniform_views.empty()
        ? nullptr : prog->loose_uniform_views.data();
    shared->loose_uniforms.count =
        static_cast<uint32_t>(prog->loose_uniform_views.size());
    shared->loose_uniforms.version = prog->loose_uniform_version;

    shared->uniform_buffers.clear();
    for (const auto& block : prog->uniform_blocks) {
        if (block.binding >= kMaxUniformBufferBindings) {
            PUSH_ERROR(GL_INVALID_OPERATION);
            shared->failed = true;
            return false;
        }
        const IndexedBufferBinding& indexed =
            g_uniform_buffer_bindings[block.binding];
        auto buffer = g_buffers.find(indexed.buffer);
        if (!indexed.buffer || buffer == g_buffers.end()) {
            PUSH_ERROR(GL_INVALID_OPERATION);
            shared->failed = true;
            return false;
        }
        const uint64_t offset = static_cast<uint64_t>(indexed.offset);
        const uint64_t available = indexed.whole_buffer
            ? static_cast<uint64_t>(buffer->second.Size())
            : static_cast<uint64_t>(indexed.size);
        if (available < static_cast<uint64_t>(block.data_size) ||
            offset > buffer->second.Size() ||
            static_cast<uint64_t>(block.data_size) >
                buffer->second.Size() - offset) {
            ML_LOG_ERROR("uniform block %s needs %d bytes but binding %u "
                         "does not provide a complete range",
                         block.name.c_str(), block.data_size, block.binding);
            PUSH_ERROR(GL_INVALID_OPERATION);
            shared->failed = true;
            return false;
        }
        buffer->second.EnsureMaterialized();
        auto append_binding = [&](uint32_t internal_binding,
                                  bool vertex_stage,
                                  bool fragment_stage) {
            v::UniformBufferBinding binding;
            binding.internal_binding = internal_binding;
            binding.vertex_stage = vertex_stage;
            binding.fragment_stage = fragment_stage;
            binding.source_data = buffer->second.data.data();
            binding.source_size = buffer->second.Size();
            binding.source_lifetime_id = buffer->second.lifetime_id;
            binding.source_content_version = buffer->second.content_version;
            binding.source_previous_content_version =
                buffer->second.previous_content_version;
            binding.source_update_offset = buffer->second.update_offset;
            binding.source_update_size = buffer->second.update_size;
            binding.source_update_is_partial = buffer->second.update_is_partial;
            binding.offset = offset;
            binding.size = available;
            shared->uniform_buffers.push_back(binding);
        };
        if (block.referenced_vertex)
            append_binding(block.vertex_internal_binding, true, false);
        if (block.referenced_fragment)
            append_binding(block.fragment_internal_binding, false, true);
    }

    shared->pipeline = BuildPipelineState();
    const s::GLState& state = s::GetState();
    const uint32_t target_width = v::DrawTargetWidth();
    const uint32_t target_height = v::DrawTargetHeight();
    shared->dynamic.viewport = state.viewport.initialized
        ? std::array<float, 4>{(float)state.viewport.x, (float)state.viewport.y,
                              (float)state.viewport.w, (float)state.viewport.h}
        : std::array<float, 4>{0.f, 0.f, (float)target_width,
                              (float)target_height};
    shared->dynamic.scissor = state.scissor.initialized
        ? std::array<float, 4>{(float)state.scissor.x, (float)state.scissor.y,
                              (float)state.scissor.w, (float)state.scissor.h}
        : std::array<float, 4>{0.f, 0.f, (float)target_width,
                              (float)target_height};

    shared->sampled_textures.clear();
    for (const auto& smp : prog->samplers) {
        auto uit = prog->uniform_by_location.find(smp.location);
        const sh::Uniform* uniform = uit == prog->uniform_by_location.end()
            ? nullptr : &prog->uniforms[uit->second];
        const GLint element_count = std::max<GLint>(smp.size, 1);
        for (GLint element = 0; element < element_count; ++element) {
            GLint unit = 0;
            if (uniform && static_cast<size_t>(element) < uniform->value.size())
                unit = static_cast<GLint>(uniform->value[element]);
            const GLenum target = TextureTargetForSampler(smp.type);
            GLuint tex = unit >= 0
                ? TextureBindingForUnit(static_cast<GLuint>(unit), target) : 0;
            if (tex) PrepareTextureForDraw(tex);
            const auto texture = g_textures.find(tex);
            const TexState default_texture;
            const TexState& texture_state = texture == g_textures.end()
                ? default_texture : texture->second;
            const v::TexSamplerInfo sampler = ResolveSamplerInfo(
                unit >= 0 ? static_cast<GLuint>(unit) : kMaxTexUnits,
                texture_state);
            const bool shader_compares_depth = SamplerUsesDepthCompare(smp.type);
            const bool texture_is_depth = texture != g_textures.end() &&
                texture_state.image_backend_format == v::TexelFormat::Depth32Float;
            if ((shader_compares_depth &&
                 (!texture_is_depth ||
                  sampler.compare_mode != GL_COMPARE_REF_TO_TEXTURE)) ||
                (!shader_compares_depth && sampler.compare_mode != GL_NONE)) {
                PUSH_ERROR(GL_INVALID_OPERATION);
                shared->failed = true;
                return false;
            }

            const uint32_t vertex_binding = smp.vertex_binding == UINT32_MAX
                ? UINT32_MAX
                : smp.vertex_binding + static_cast<uint32_t>(element);
            const uint32_t fragment_binding = smp.fragment_binding == UINT32_MAX
                ? UINT32_MAX
                : smp.fragment_binding + static_cast<uint32_t>(element);
            if (vertex_binding != UINT32_MAX &&
                vertex_binding == fragment_binding) {
                shared->sampled_textures.push_back({
                    vertex_binding, tex, sampler, true, true});
            } else {
                if (vertex_binding != UINT32_MAX)
                    shared->sampled_textures.push_back({
                        vertex_binding, tex, sampler, true, false});
                if (fragment_binding != UINT32_MAX)
                    shared->sampled_textures.push_back({
                        fragment_binding, tex, sampler, false, true});
            }
        }
    }
    shared->occlusion_query = CurrentOcclusionQueryHandle();
    shared->provoking_vertex = state.provoking_vertex;
    shared->ready = true;
    return true;
}

'''
insert_before(
    "src/gl/draw.cpp",
    "// Core draw: resolve the current VAO into typed streams and hand them to the\n",
    shared_helper)

# Allow one MultiDraw call to borrow a lazily resolved shared state.
replace_once(
    "src/gl/draw.cpp",
    "                const v::ResidentIndexSource* resident_indices = nullptr,\n                uint32_t resident_max_index = 0) {",
    "                const v::ResidentIndexSource* resident_indices = nullptr,\n                uint32_t resident_max_index = 0,\n                SharedDrawState* shared_state = nullptr) {")

old_front = r'''    sh::Program* prog = sh::GetProgram(s::GetState().current_program);
    if (!prog || !prog->linked) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    if (!v::EnsureInit()) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    // Replay texture uploads that happened before the backend came up.
    if (!g_dirty_textures.empty()) FlushDirtyTextureUploads();
    if (!CreateBackendProgram(prog)) { PUSH_ERROR(GL_INVALID_OPERATION); return; }

    const VAOData& vao = g_vaos[g_bound_vao];

    std::vector<GLuint> vertex_slots;    // enabled, divisor == 0
    std::vector<GLuint> instance_slots;  // enabled, divisor != 0
    for (GLuint slot = 0; slot < kMaxAttribs; ++slot) {
        const AttribData& a = vao.attribs[slot];
        if (!a.enabled) continue;
        (a.divisor ? instance_slots : vertex_slots).push_back(slot);
    }
    std::vector<sh::VertexInput> constant_inputs;
    for (const sh::VertexInput& input : prog->vertex_inputs) {
        if (input.location >= kMaxAttribs) {
            PUSH_ERROR(GL_INVALID_OPERATION);
            return;
        }
        if (!vao.attribs[input.location].enabled)
            constant_inputs.push_back(input);
    }
'''
new_front = r'''    SharedDrawState local_shared;
    SharedDrawState* shared = shared_state ? shared_state : &local_shared;
    if (!ResolveDrawSharedState(shared)) return;
    ++g_draw_lowering_stats.geometry_lowerings;
    sh::Program* prog = shared->program;
    const VAOData& vao = *shared->vao;
    const auto& vertex_slots = shared->vertex_slots;
    const auto& instance_slots = shared->instance_slots;
    const auto& constant_inputs = shared->constant_inputs;
'''
replace_once("src/gl/draw.cpp", old_front, new_front)

# Replace all per-draw metadata resolution after geometry streams are built.
p = Path("src/gl/draw.cpp")
text = p.read_text()
fn_begin = text.index("void DrawCommon(")
fn_end = text.index("\nenum class ResidentIndexResult", fn_begin)
body = text[fn_begin:fn_end]
meta_begin = body.index("    v::DrawParams dp;\n")
meta_end = body.index("    if (prog->uses_flat_fragment_inputs", meta_begin)
new_meta = r'''    v::DrawParams dp;
    dp.program = shared->backend_program;
    dp.vertex_stream = std::move(vstream);
    dp.instance_stream = std::move(istream);
    dp.indices = idx;
    if (resident_indices) dp.resident_indices = *resident_indices;
    dp.primitive_restart = std::find(idx.begin(), idx.end(), UINT32_MAX) !=
                           idx.end();
    dp.occlusion_query = shared->occlusion_query;
    dp.instance_count = (uint32_t)instance_count;
    dp.topology = (v::Topology)topo;
    dp.loose_uniforms = shared->loose_uniforms;
    dp.uniform_buffers = shared->uniform_buffers;
    dp.sampled_textures = shared->sampled_textures;
    dp.pipeline = shared->pipeline;
    dp.dynamic = shared->dynamic;
'''
body = body[:meta_begin] + new_meta + body[meta_end:]
body = body.replace(
    "!LowerFlatPrimitives(state.provoking_vertex, &dp)",
    "!LowerFlatPrimitives(shared->provoking_vertex, &dp)")
if "state.provoking_vertex" in body:
    raise SystemExit("DrawCommon: stale per-draw GL state reference remains")
p.write_text(text[:fn_begin] + body + text[fn_end:])

# Thread the shared-state pointer through the internal draw family.
replace_once(
    "src/gl/draw.cpp",
    "void SubmitIndexSegment(GLenum mode, const std::vector<uint32_t>& segment,\n                        GLint base_vertex, GLsizei instance_count) {",
    "void SubmitIndexSegment(GLenum mode, const std::vector<uint32_t>& segment,\n                        GLint base_vertex, GLsizei instance_count,\n                        SharedDrawState* shared_state = nullptr) {")
replace_once(
    "src/gl/draw.cpp",
    "                       base_vertex, instance_count);\n        return;\n    }\n    DrawCommon(mode, segment, 0, static_cast<GLsizei>(segment.size()),\n               base_vertex, instance_count);",
    "                       base_vertex, instance_count, nullptr, 0, shared_state);\n        return;\n    }\n    DrawCommon(mode, segment, 0, static_cast<GLsizei>(segment.size()),\n               base_vertex, instance_count, nullptr, 0, shared_state);")

replace_once(
    "src/gl/draw.cpp",
    "void DrawArraysImpl(GLenum mode, GLint first, GLsizei count,\n                    GLsizei instance_count) {",
    "void DrawArraysImpl(GLenum mode, GLint first, GLsizei count,\n                    GLsizei instance_count,\n                    SharedDrawState* shared_state = nullptr) {")
replace_once(
    "src/gl/draw.cpp",
    "        DrawCommon(mode, {}, first, count, 0, instance_count);",
    "        DrawCommon(mode, {}, first, count, 0, instance_count, nullptr, 0,\n                   shared_state);")
replace_once(
    "src/gl/draw.cpp",
    "    SubmitIndexSegment(mode, loop, first, instance_count);",
    "    SubmitIndexSegment(mode, loop, first, instance_count, shared_state);")

replace_once(
    "src/gl/draw.cpp",
    "void DrawElementsImpl(GLenum mode, GLsizei count, GLenum type,\n                      const void* indices, GLint base_vertex,\n                      GLsizei instance_count, GLuint start, GLuint end) {",
    "void DrawElementsImpl(GLenum mode, GLsizei count, GLenum type,\n                      const void* indices, GLint base_vertex,\n                      GLsizei instance_count, GLuint start, GLuint end,\n                      SharedDrawState* shared_state = nullptr) {")
replace_once(
    "src/gl/draw.cpp",
    "        DrawCommon(mode, {}, 0, count, base_vertex, instance_count,\n                   &resident, resident_max);",
    "        DrawCommon(mode, {}, 0, count, base_vertex, instance_count,\n                   &resident, resident_max, shared_state);")
# There are two fallback SubmitIndexSegment call sites in DrawElementsImpl.
p = Path("src/gl/draw.cpp")
text = p.read_text()
fn_begin = text.index("void DrawElementsImpl(")
fn_end = text.index("\n}\n\n} // namespace", fn_begin) + 2
body = text[fn_begin:fn_end]
body = body.replace(
    "SubmitIndexSegment(mode, segment, base_vertex,\n                                   instance_count);",
    "SubmitIndexSegment(mode, segment, base_vertex,\n                                   instance_count, shared_state);")
body = body.replace(
    "SubmitIndexSegment(mode, idx, base_vertex, instance_count);",
    "SubmitIndexSegment(mode, idx, base_vertex, instance_count, shared_state);")
if body.count("shared_state") < 4:
    raise SystemExit("DrawElementsImpl: shared-state threading incomplete")
p.write_text(text[:fn_begin] + body + text[fn_end:])

# MultiDraw APIs own one lazy shared-state object for all subdraws.
replace_once(
    "src/gl/draw.cpp",
    "void APIENTRY glMultiDrawArrays(GLenum mode, const GLint* first,\n                                const GLsizei* count, GLsizei drawcount) {\n    if (drawcount < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }\n    for (GLsizei i = 0; i < drawcount; ++i)\n        DrawArraysImpl(mode, first[i], count[i], 1);\n}",
    "void APIENTRY glMultiDrawArrays(GLenum mode, const GLint* first,\n                                const GLsizei* count, GLsizei drawcount) {\n    if (drawcount < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }\n    ++g_draw_lowering_stats.multi_draw_calls;\n    g_draw_lowering_stats.multi_draw_subdraws += static_cast<uint64_t>(drawcount);\n    SharedDrawState shared;\n    for (GLsizei i = 0; i < drawcount; ++i)\n        DrawArraysImpl(mode, first[i], count[i], 1, &shared);\n}")
replace_once(
    "src/gl/draw.cpp",
    "void APIENTRY glMultiDrawElements(GLenum mode, const GLsizei* count, GLenum type,\n                                  const void* const* indices, GLsizei drawcount) {\n    if (drawcount < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }\n    for (GLsizei i = 0; i < drawcount; ++i)\n        DrawElementsImpl(mode, count[i], type, indices[i], 0, 1, 0, 0);\n}",
    "void APIENTRY glMultiDrawElements(GLenum mode, const GLsizei* count, GLenum type,\n                                  const void* const* indices, GLsizei drawcount) {\n    if (drawcount < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }\n    ++g_draw_lowering_stats.multi_draw_calls;\n    g_draw_lowering_stats.multi_draw_subdraws += static_cast<uint64_t>(drawcount);\n    SharedDrawState shared;\n    for (GLsizei i = 0; i < drawcount; ++i)\n        DrawElementsImpl(mode, count[i], type, indices[i], 0, 1, 0, 0, &shared);\n}")
replace_once(
    "src/gl/draw.cpp",
    "void APIENTRY glMultiDrawElementsBaseVertex(GLenum mode, const GLsizei* count,\n                                            GLenum type,\n                                            const void* const* indices,\n                                            GLsizei drawcount,\n                                            const GLint* basevertex) {\n    if (drawcount < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }\n    for (GLsizei i = 0; i < drawcount; ++i)\n        DrawElementsImpl(mode, count[i], type, indices[i], basevertex[i], 1, 0, 0);\n}",
    "void APIENTRY glMultiDrawElementsBaseVertex(GLenum mode, const GLsizei* count,\n                                            GLenum type,\n                                            const void* const* indices,\n                                            GLsizei drawcount,\n                                            const GLint* basevertex) {\n    if (drawcount < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }\n    ++g_draw_lowering_stats.multi_draw_calls;\n    g_draw_lowering_stats.multi_draw_subdraws += static_cast<uint64_t>(drawcount);\n    SharedDrawState shared;\n    for (GLsizei i = 0; i < drawcount; ++i)\n        DrawElementsImpl(mode, count[i], type, indices[i], basevertex[i], 1, 0, 0,\n                         &shared);\n}")

# Export diagnostics beside the public GL entry points.
insert_before(
    "src/gl/draw.cpp",
    "void APIENTRY glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,\n",
    r'''void mithrilResetDrawLoweringStats(void) {
    g_draw_lowering_stats = EmptyDrawLoweringStats();
}

int mithrilGetDrawLoweringStatsV1(
    MithrilDrawLoweringStatsV1* output, size_t output_size) {
    if (!output || output_size < sizeof(*output)) return 0;
    *output = g_draw_lowering_stats;
    return 1;
}

''')

print("multidraw shared-state lowering applied")
