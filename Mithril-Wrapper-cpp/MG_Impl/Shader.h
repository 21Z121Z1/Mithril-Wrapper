// Mithril-Wrapper - MG_Impl/Shader.h
// GLSL (desktop Core Profile) -> Vulkan SPIR-V translation via glslang.
//
// This is the Vulkan/MoltenVK rewrite of the former gl/shader.h. The old
// pipeline was GLSL -> SPIR-V (glslang, OpenGL client) -> MSL (SPIRV-Cross).
// The new pipeline is GLSL -> SPIR-V (glslang, Vulkan 1.2 client) only —
// MoltenVK cross-translates the Vulkan SPIR-V to MSL internally at
// vkCreateShaderModule time, so SPIRV-Cross is no longer needed here.
//
// Translation pipeline (mirrors MobileGL's ShaderCompiler/ProgramFactory):
//   1. Preprocess: inject MG_MITHRIL / MG_MITHRIL_VERSION macros so host
//      shaders can branch on the Mithril backend (mirrors MobileGlues'
//      MG_MOBILEGLUES injection). Upgrade GLSL versions below 460 so desktop
//      GLSL 150 shaders like Minecraft's blit_screen compile under the
//      Vulkan client.
//   2. Normalize Vulkan-incompatible layout qualifiers (layout(packed) ->
//      layout(std140) for UBOs, layout(std430) for SSBOs) and GL legacy
//      constructs (gl_FragColor -> synthetic named output). Both are
//      source-level, idempotent, comment-aware passes.
//   3. Inject GL->Vulkan position fixups (Z remap always; Y flip when
//      flip_y) by wrapping main() (vertex shaders only). This is the GLSL
//      equivalent of MobileGL's SPIRV-Tools GlToVulkanPositionFixPass.
//   4. glslang compiles under the Vulkan client dialect with
//      EXT_vulkan_glsl_relaxed (EShClientVulkan + VulkanRulesRelaxed):
//      loose non-opaque uniforms are automatically folded into the
//      `mithril_GlobalBlock` UBO (setGlobalUniformBlockName, the GLSL
//      analogue of ANGLE's ANGLE_DefaultUniformBlock / MobileGL's
//      MGL_GLOBAL_UBO), and desktop builtins such as gl_VertexID /
//      gl_InstanceID are accepted natively — no regex rewriting.
//   5. glLinkProgram compiles the vertex + fragment stages in ONE
//      glslang::TProgram and runs mapIO() with a custom
//      TIoMapResolver that pins glBindAttribLocation mappings onto the
//      stage-input locations (exactly like MobileGL's TMglGlslIoResolver).
//   6. The generated SPIR-V is post-processed by SPIRV-Reflect
//      (remap_descriptor_bindings): every descriptor binding is reassigned
//      deterministically across BOTH stages, keyed by (descriptor kind,
//      block/uniform name), so the vertex and fragment stages can never
//      collide on a binding. This replaces the old per-stage
//      layout(binding=N) regex injection + 64-slot stage-offset hack.
#ifndef MITHRIL_SHADER_H
#define MITHRIL_SHADER_H

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

#include <GL/gl.h>

namespace mithril {

// Result of one cross-stage link: the vertex SPIR-V in both its Y-flipped
// (default framebuffer / on-screen drawable) and non-flipped (user FBO)
// variants, plus the fragment SPIR-V.
struct ShaderLinkOutput {
    // NEGATIVE_ONE_TO_ONE: GL clip-Z [-w,+w] is remapped to Metal/Vulkan [0,w].
    std::vector<uint32_t> vertexSpirv;
    std::vector<uint32_t> vertexSpirvFlipped;
    // ZERO_TO_ONE: clip-Z is already [0,w], so no Z remap is legal.
    std::vector<uint32_t> vertexSpirvZeroToOne;
    std::vector<uint32_t> vertexSpirvZeroToOneFlipped;
    std::vector<uint32_t> fragmentSpirv;
};

// Validate a single stage's source (glCompileShader). Compiles with the same
// Vulkan-client relaxed dialect as the link path so the COMPILE_STATUS a host
// sees matches what a later link would do. No SPIR-V is retained here — the
// link path re-compiles the sources in one TProgram.
bool shader_compile_stage(GLenum gl_stage, const std::string& glsl_source,
                          std::string& out_info_log);

// Translate a linked vertex + fragment source pair into Vulkan SPIR-V.
//
// attrib_bindings maps attribute names to the locations the application
// requested via glBindAttribLocation(). When non-empty, a custom IO resolver
// pins those locations onto the SPIR-V stage inputs so they match the
// application's vertex descriptor (the MobileGL TMglGlslIoResolver pattern).
// Pass nullptr when no explicit bindings are needed.
//
// The vertex shader is emitted twice: once without the Y flip (for user FBO
// draws, whose textures are sampled with GL Y-up coords) and once with it
// (for default-framebuffer draws). Both variants share the same descriptor
// binding assignment (the resource set is identical), so switching
// framebuffers never rebinds shaders to different textures.
//
// Deep reference: MobileGL ShaderCompiler::CompileShader/LinkProgram +
// ProgramFactory::RemapDescriptorBindingsForVulkan.
bool shader_link_program(const std::string& vs_source, const std::string& fs_source,
                         const std::unordered_map<std::string, GLuint>* attrib_bindings,
                         ShaderLinkOutput& out, std::string& out_info_log);

// Generate a minimal fallback SPIR-V for a shader stage that failed to
// compile. The fallback renders geometry with a solid gray color so that
// draws are NOT skipped (which would leave only the application's clear
// color visible — the "red screen" symptom on Minecraft's loading screen).
//
// Vertex fallback: reads no vertex attributes and outputs a degenerate
// position (collapses to a point, nothing drawn, no page fault).
// Fragment fallback: outputs vec4(0.5, 0.5, 0.5, 1.0) (neutral gray).
// Other stages: returns false (no fallback for compute/geometry/etc).
//
// Deep reference: MobileGL VulkanRenderer fallback shader substitution
// (VulkanRenderer.cpp GetFallbackShader).
bool get_fallback_spirv(GLenum gl_stage, bool flip_y,
                        std::vector<uint32_t>& out_spirv);

} // namespace mithril

#endif // MITHRIL_SHADER_H
