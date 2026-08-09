// Mithril-Wrapper shader module -- GLSL -> SPIR-V compilation and
// program/uniform/attribute reflection (milestone M2-S2).
//
// glslang compiles desktop GLSL (Core Profile) to shared SPIR-V; SPIRV-Cross
// reflects the linked program so glGetUniformLocation/glGetAttribLocation and
// the uniform getters can answer honestly. Native backends consume the same
// cached words (VkShaderModule for Vulkan, SPIRV-Cross MSL for DirectMetal).

#pragma once

#include <GL/glcorearb.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace mithril::shader {

// One GLSL shader object (GL_VERTEX_SHADER / GL_FRAGMENT_SHADER / ...).
struct Shader {
    GLuint id = 0;
    GLenum type = GL_VERTEX_SHADER;
    std::string source;
    bool compiled = false;
    std::string info_log;
    std::vector<uint32_t> spirv;   // SPIR-V words (empty until compiled)
};

// A reflected program uniform. `value` caches the last glUniform* write so
// glGetUniform* can answer; the selected backend later uploads it into a UBO.
struct Uniform {
    std::string name;
    GLenum type = GL_FLOAT;
    GLint location = -1;
    std::vector<float> value;
    GLint block_index = -1;
    GLint offset = -1;
    GLint array_stride = 0;
    GLint matrix_stride = 0;
    GLboolean row_major = GL_FALSE;
    GLint size = 1;
};

// A sampled-image uniform: the shared SPIR-V descriptor binding (assigned by
// assign_sampler_bindings during compilation, 1-based) and the GL texture
// unit value written by glUniform1i.
struct SamplerRef {
    std::string name;
    GLenum type = GL_SAMPLER_2D;
    uint32_t binding = 0;      // shared descriptor binding for this sampler
    GLint location = -1;
};

// Shared resource namespace emitted by the GLSL lowering pass. Binding 0 is
// reserved for the synthetic loose-uniform block. User blocks are kept away
// from vertex buffers (Metal indices 0/1) and the loose block (index 16).
// Twelve user blocks plus the loose block stay below Mac2's 14 constant-buffer
// argument limit while satisfying GL 3.3's per-stage minimum of 12 blocks.
constexpr uint32_t kLooseUniformBinding = 0;
constexpr uint32_t kUserUniformBindingBase = 17;
constexpr uint32_t kMaxUserUniformBlocksPerStage = 12;

struct UniformBlock {
    std::string name;
    GLuint binding = 0;                 // frontend GL indexed binding point
    GLint data_size = 0;
    bool referenced_vertex = false;
    bool referenced_fragment = false;
    uint32_t vertex_internal_binding = UINT32_MAX;
    uint32_t fragment_internal_binding = UINT32_MAX;
    std::vector<Uniform> members;
    std::vector<GLuint> active_uniform_indices;
};

// Source-level declaration metadata is separate from the internal SPIR-V
// binding namespace. This preserves the observable initial value of
// layout(binding=N) after lowering rewrites N for each native backend.
struct UniformBlockDeclaration {
    std::string name;
    GLuint binding = 0;
    bool has_explicit_binding = false;
    bool has_instance = false;
    bool is_array = false;
};

struct Program {
    GLuint id = 0;
    std::vector<GLuint> attached;          // attached shader ids
    bool linked = false;
    std::string info_log;
    std::vector<Uniform> uniforms;         // active uniforms (index == GL index)
    std::unordered_map<std::string, GLint> uniform_by_name;    // name -> location
    std::unordered_map<GLint, size_t> uniform_by_location;     // location -> uniforms idx
    std::unordered_map<std::string, GLuint> active_uniform_by_name;
    std::unordered_map<std::string, GLint> attrib_locations;   // name -> location
    std::vector<SamplerRef> samplers;      // M4: active sampler uniforms
    std::vector<UniformBlock> uniform_blocks;
    std::unordered_map<std::string, GLuint> uniform_block_by_name;
    std::vector<uint32_t> vertex_spirv;    // linked stage SPIR-V
    std::vector<uint32_t> fragment_spirv;
};

// Compile `source` for `stage` into SPIR-V words. Returns false and fills
// `info` with the glslang diagnostics on failure. Thread-safe; results cached
// by (stage, source) hash.
bool CompileStage(GLenum stage, const std::string& source,
                  std::vector<uint32_t>& out_spirv, std::string& out_info);

// Reflect a linked program and validate cross-stage uniform-block layouts.
// Returns false rather than exposing a program whose native resource views
// would disagree.
bool ReflectProgram(Program& prog, std::string& error);

// Parse only source-level uniform-block names and optional layout(binding=N)
// values. GLSL compilation uses a separate backend-neutral internal namespace.
std::vector<UniformBlockDeclaration> DiscoverUniformBlocks(
    const std::string& source);

// Object tables (single shared context).
Shader* GetShader(GLuint id);
Program* GetProgram(GLuint id);
GLuint NewShader(GLenum type);
GLuint NewProgram();
void DeleteShader(GLuint id);
void DeleteProgram(GLuint id);

} // namespace mithril::shader
