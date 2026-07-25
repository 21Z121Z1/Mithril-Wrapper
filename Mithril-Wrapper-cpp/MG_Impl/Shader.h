// Mithril-Wrapper - MG_Impl/Shader.h
// GLSL (desktop Core Profile) -> SPIR-V translation via glslang.
//
// This is the Vulkan/MoltenVK rewrite of the former gl/shader.h. The old
// pipeline was GLSL -> SPIR-V (glslang, OpenGL client) -> MSL (SPIRV-Cross).
// The new pipeline is GLSL -> SPIR-V (glslang, Vulkan 1.2 client) only —
// MoltenVK cross-translates the Vulkan SPIR-V to MSL internally at
// vkCreateShaderModule time, so SPIRV-Cross is no longer needed here.
//
// The preprocessor injects the MG_MITHRIL / MG_MITHRIL_VERSION macros so host
// shaders can branch on the Mithril backend (mirrors MobileGlues' MG_MOBILEGLUES
// injection), and upgrades GLSL versions below 330 (the Vulkan minimum) so
// desktop GLSL 150 shaders like Minecraft's blit_screen compile under the
// Vulkan client.
#ifndef MITHRIL_SHADER_H
#define MITHRIL_SHADER_H

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

#include <GL/gl.h>

namespace mithril {

// Translate a desktop GLSL Core Profile source string into Vulkan SPIR-V
// words. Returns true on success. On failure, out_info_log is populated.
// Results are cached by (stage, source hash, attrib bindings hash).
//
// attrib_bindings maps attribute names to the location the application
// requested via glBindAttribLocation(). When non-empty, the translator injects
// `layout(location=N)` qualifiers into the GLSL source before compilation so
// that the SPIR-V stage_input locations match the application's vertex
// descriptor layout. Pass nullptr when no explicit bindings are needed (falls
// back to glslang auto-mapping).
bool shader_translate(GLenum gl_stage, const std::string& glsl_source,
                      std::vector<uint32_t>& out_spirv, std::string& out_info_log,
                      const std::unordered_map<std::string, GLuint>* attrib_bindings = nullptr);

} // namespace mithril

#endif // MITHRIL_SHADER_H
