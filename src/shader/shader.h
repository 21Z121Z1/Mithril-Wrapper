// Mithril-Wrapper shader module -- GLSL -> SPIR-V compilation and
// program/uniform/attribute reflection (milestone M2-S2).
//
// glslang compiles desktop GLSL (Core Profile) to shared SPIR-V; SPIRV-Cross
// reflects the linked program so glGetUniformLocation/glGetAttribLocation and
// the uniform getters can answer honestly. Native backends consume the same
// cached words (VkShaderModule for Vulkan, SPIRV-Cross MSL for DirectMetal).

#pragma once

#include <GL/glcorearb.h>
#include <backend/types.h>

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

// A reflected program uniform. `value` keeps a numeric cache for glGetUniform*
// while `raw_value` preserves the exact scalar representation that native
// backends upload. Storing GLint/GLuint values as float bytes would corrupt
// integer uniforms even though the observable getter still looked plausible.
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
    std::vector<uint8_t> raw_value;
};

// A sampled-image uniform: per-stage SPIR-V bindings (assigned during shader
// compilation, 1-based) and the shared GL texture-unit values written through
// its program-uniform locations. `size` is the active fixed array length;
// scalar samplers use 1. Declaration order can differ between stages, so
// bindings must not be collapsed merely because the GL name is shared.
struct SamplerRef {
    std::string name;
    GLenum type = GL_SAMPLER_2D;
    GLint size = 1;
    uint32_t vertex_binding = UINT32_MAX;
    uint32_t fragment_binding = UINT32_MAX;
    GLint location = -1;
};

enum class VertexInputScalar {
    Float32 = 0,
    Sint32,
    Uint32,
    Unsupported,
};

// One location consumed by the linked vertex stage. Matrices and arrays are
// expanded to their individual locations so the draw frontend can provide
// disabled-array current values without backend-specific shader patches.
struct VertexInput {
    GLuint location = 0;
    uint32_t components = 0;
    VertexInputScalar scalar = VertexInputScalar::Unsupported;
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
    std::vector<backend::UniformValueView> loose_uniform_views;
    uint64_t loose_uniform_version = 1;
    std::unordered_map<std::string, GLint> uniform_by_name;    // name -> location
    std::unordered_map<GLint, size_t> uniform_by_location;     // location -> uniforms idx
    std::unordered_map<std::string, GLuint> active_uniform_by_name;
    std::unordered_map<std::string, GLint> attrib_locations;   // name -> location
    std::unordered_map<std::string, GLuint> requested_attrib_locations;
    std::unordered_map<std::string, GLint> frag_data_locations;
    std::unordered_map<std::string, GLint> frag_data_indices;
    std::unordered_map<std::string, GLuint> requested_frag_data_locations;
    bool uses_flat_fragment_inputs = false;
    std::vector<VertexInput> vertex_inputs;
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

// Apply pre-link GL attribute/fragment-output bindings to the shared SPIR-V
// Location decorations. Explicit layout(location=) declarations retain
// precedence; unbound active interfaces are moved away from collisions.
bool ApplyStageLocationBindings(
    std::vector<uint32_t>& spirv, GLenum stage, const std::string& source,
    const std::unordered_map<std::string, GLuint>& requested,
    uint32_t max_locations, std::string& error);

// Reconcile the linked vertex-output / fragment-input interface after the two
// stages have been compiled independently. Automatic locations match by GLSL
// name; explicit layout(location=) declarations remain authoritative.
bool AlignStageInterfaceLocations(
    std::vector<uint32_t>& vertex_spirv,
    std::vector<uint32_t>& fragment_spirv,
    const std::string& vertex_source,
    const std::string& fragment_source,
    std::string& error);

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
