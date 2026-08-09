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

struct Program {
    GLuint id = 0;
    std::vector<GLuint> attached;          // attached shader ids
    bool linked = false;
    std::string info_log;
    std::vector<Uniform> uniforms;         // active uniforms (index == GL index)
    std::unordered_map<std::string, GLint> uniform_by_name;    // name -> location
    std::unordered_map<GLint, size_t> uniform_by_location;     // location -> uniforms idx
    std::unordered_map<std::string, GLint> attrib_locations;   // name -> location
    std::vector<SamplerRef> samplers;      // M4: active sampler uniforms
    std::vector<uint32_t> vertex_spirv;    // linked stage SPIR-V
    std::vector<uint32_t> fragment_spirv;
};

// Compile `source` for `stage` into SPIR-V words. Returns false and fills
// `info` with the glslang diagnostics on failure. Thread-safe; results cached
// by (stage, source) hash.
bool CompileStage(GLenum stage, const std::string& source,
                  std::vector<uint32_t>& out_spirv, std::string& out_info);

// Reflect a linked program: extract uniform + attribute names/locations from
// the stage SPIR-V via SPIRV-Cross. Always succeeds (empty result on malformed
// SPIR-V).
void ReflectProgram(Program& prog);

// Object tables (single shared context).
Shader* GetShader(GLuint id);
Program* GetProgram(GLuint id);
GLuint NewShader(GLenum type);
GLuint NewProgram();
void DeleteShader(GLuint id);
void DeleteProgram(GLuint id);

} // namespace mithril::shader
