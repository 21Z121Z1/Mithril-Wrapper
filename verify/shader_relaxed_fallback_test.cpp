// shader_relaxed_fallback_test.cpp — Root-cause reproduction + minimal-fix
// verification for Mithril-Wrapper's glsl_to_spirv third-level relaxed fallback.
//
// BACKGROUND (root cause):
//   Shader.cpp's strict path fails, it retries with a third fallback:
//     shader3.setEnvInput(..., EShClientOpenGL, ...) + setEnvInputVulkanRulesRelaxed()
//   But glslang::TranslateEnvironment (ShaderLang.cpp) only copies
//   environment->input.vulkanRulesRelaxed into spvVersion.vulkanRelaxed when the
//   *input dialect* is EShClientVulkan:
//
//     case EShClientVulkan: spvVersion.vulkanRelaxed = env->input.vulkanRulesRelaxed; break;
//     case EShClientOpenGL: spvVersion.openGl = ...;  // vulkanRelaxed left false
//
//   With EShClientOpenGL the flag is silently dropped, so setEnvInputVulkanRulesRelaxed()
//   is a no-op. Shaders needing relaxed Vulkan rules (layout(packed)/layout(shared)
//   UBOs, direct gl_VertexID/gl_InstanceID, GL-legacy constructs) then fail ALL THREE
//   fallback paths -> linked=false -> the draw is skipped -> black screen with audio
//   (the reported "大量着色器编译报错").
//
// FIX (minimal blast radius, applied in Shader.cpp shader3):
//   Switch only the input dialect OpenGL -> Vulkan. setEnvClient stays OpenGL/450,
//   output stays SPIR-V 1.5, and gl_VertexID/gl_InstanceID were already renamed to
//   gl_VertexIndex/gl_InstanceIndex by rewrite_desktop_builtins() before this point,
//   so Vulkan (0-based) semantics are preserved.
//
// This test, with the same source and config, asserts:
//   - strict mode must reject layout(packed)            (precondition)
//   - OpenGL input + relaxed must still FAIL            (reproduces the bug, pre-fix)
//   - Vulkan input + relaxed must PASS                  (the fix)
//   - real Minecraft fragment/vertex shaders must not regress under Vulkan input
//
// Build (against the glslang submodule):
//   clang++ -std=c++17 -O0 -I <glslang> \
//       -I <glslang>/build-test/External/spirv-tools/include \
//       verify/shader_relaxed_fallback_test.cpp \
//       <glslang>/build-test/glslang/libglslang.a \
//       <glslang>/build-test/glslang/libMachineIndependent.a \
//       <glslang>/build-test/glslang/libGenericCodeGen.a \
//       <glslang>/build-test/glslang/OSDependent/Unix/libOSDependent.a \
//       <glslang>/build-test/SPIRV/libSPIRV.a \
//       <glslang>/build-test/glslang/libglslang-default-resource-limits.a \
//       -lpthread -o shader_relaxed_fallback_test
//
// Exit code 0 => all checks pass (fix verified, no regression).
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>
#include <cstdio>
#include <string>
#include <vector>

// Mirrors Shader.cpp's shader3 relaxed fallback config (post-fix: Vulkan input).
// inputClient: glslang::EShClientOpenGL (pre-fix, reproduces bug) or
//              glslang::EShClientVulkan (post-fix).
static bool relaxed_fallback_compile(EShLanguage stage, int glsl_version,
                                     const std::string& src, int inputClient,
                                     std::string& err) {
    glslang::TShader sh(stage);
    const char* s = src.c_str();
    sh.setStrings(&s, 1);
    sh.setEnvInput(glslang::EShSourceGlsl, stage, (glslang::EShClient)inputClient, glsl_version);
    sh.setEnvClient(glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450);
    sh.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);
    sh.setEnvInputVulkanRulesRelaxed();   // key: accept GL legacy constructs
    sh.setAutoMapLocations(true);
    sh.setAutoMapBindings(true);
    auto msgs = (EShMessages)(EShMsgDefault | EShMsgSpvRules | EShMsgVulkanRules);
    if (!sh.parse(GetDefaultResources(), glsl_version, true, msgs)) { err = sh.getInfoLog(); return false; }
    glslang::TProgram prog;
    prog.addShader(&sh);
    if (!prog.link(msgs)) { err = prog.getInfoLog(); return false; }
    std::vector<unsigned int> spirv;
    glslang::GlslangToSpv(*prog.getIntermediate(stage), spirv);
    return !spirv.empty();
}

static bool strict_compile(EShLanguage stage, int glsl_version, const std::string& src, std::string& err) {
    glslang::TShader sh(stage);
    const char* s = src.c_str();
    sh.setStrings(&s, 1);
    sh.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientOpenGL, glsl_version);
    sh.setEnvClient(glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450);
    sh.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);
    sh.setAutoMapLocations(true);
    sh.setAutoMapBindings(true);
    auto msgs = (EShMessages)(EShMsgDefault | EShMsgSpvRules | EShMsgVulkanRules);
    if (!sh.parse(GetDefaultResources(), glsl_version, true, msgs)) { err = sh.getInfoLog(); return false; }
    glslang::TProgram prog;
    prog.addShader(&sh);
    if (!prog.link(msgs)) { err = prog.getInfoLog(); return false; }
    std::vector<unsigned int> spirv;
    glslang::GlslangToSpv(*prog.getIntermediate(stage), spirv);
    return !spirv.empty();
}

int main() {
    glslang::InitializeProcess();
    int failures = 0, checks = 0;
#define CHECK(cond, fmt, ...) do { ++checks; if (cond) printf("ok  : " fmt "\n", ##__VA_ARGS__); \
    else { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; } } while (0)

    const int GLSL_VER = 420;
    std::string err;

    // ---- Case A: layout(packed) UBO needs relaxed Vulkan rules ----
    const std::string packed_ubo =
        "#version 420 core\n"
        "layout(packed) uniform Matrices {\n    mat4 MVP;\n} _m;\n"
        "in vec2 texCoord0;\n"
        "out vec4 fragColor;\n"
        "void main() { fragColor = vec4(texCoord0 * _m.MVP[3].xy, 0.0, 1.0); }\n";

    bool strict_packed = strict_compile(EShLangFragment, GLSL_VER, packed_ubo, err);
    printf("strict layout(packed)            : %s%s\n", strict_packed ? "OK" : "FAIL",
           strict_packed ? "" : (" - " + err).c_str());
    bool before = relaxed_fallback_compile(EShLangFragment, GLSL_VER, packed_ubo,
                                           glslang::EShClientOpenGL, err);
    printf("pre-fix  OpenGL input + relaxed  : %s%s\n", before ? "OK" : "FAIL",
           before ? "" : (" - " + err).c_str());
    bool after = relaxed_fallback_compile(EShLangFragment, GLSL_VER, packed_ubo,
                                          glslang::EShClientVulkan, err);
    printf("post-fix Vulkan input + relaxed  : %s%s\n", after ? "OK" : "FAIL",
           after ? "" : (" - " + err).c_str());

    CHECK(!strict_packed, "strict mode must reject layout(packed) (precondition for the relaxed fallback)");
    CHECK(!before, "REPRODUCE: pre-fix relaxed fallback is a no-op, layout(packed) still fails");
    CHECK(after, "FIX: with Vulkan input dialect relaxed is effective, layout(packed) compiles");

    // ---- Case B: real Minecraft fragment shader (rendertype_text.frag) ----
    const std::string mc_text_fs =
        "#version 420 core\n"
        "uniform mithril_GlobalBlock {\n"
        "    vec4 ColorModulator;\n    float FogStart;\n    float FogEnd;\n    vec4 FogColor;\n"
        "} _m;\n"
        "#define ColorModulator _m.ColorModulator\n"
        "#define FogStart _m.FogStart\n"
        "#define FogEnd _m.FogEnd\n"
        "#define FogColor _m.FogColor\n"
        "in vec2 texCoord0;\nin vec2 texCoord2;\n"
        "uniform sampler2D Sampler0;\n"
        "out vec4 fragColor;\n"
        "void main(){ vec4 c=texture(Sampler0,texCoord0)*ColorModulator; "
        "c=mix(c,FogColor,clamp((FogEnd-gl_FragCoord.z)/(FogEnd-FogStart),0.0,1.0)); fragColor=c; }\n";
    bool fs_new = relaxed_fallback_compile(EShLangFragment, GLSL_VER, mc_text_fs,
                                           glslang::EShClientVulkan, err);
    printf("MC fragment shader, Vulkan input : %s\n", fs_new ? "OK" : "FAIL");
    CHECK(fs_new, "no regression: real MC fragment shader still compiles post-fix");

    // ---- Case C: real Minecraft vertex shaders (incl. gl_VertexID) ----
    const std::string mc_text_vs =
        "#version 420 core\n"
        "in vec3 Position;\nin vec2 UV0;\nin vec2 UV2;\n"
        "uniform mat4 ModelViewMat;\nuniform mat4 ProjMat;\nuniform sampler2D Sampler2;\n"
        "out vec2 texCoord0;\nout vec2 texCoord2;\n"
        "void main(){ gl_Position=ProjMat*ModelViewMat*vec4(Position,1.0); texCoord0=UV0; texCoord2=UV2; }\n";
    const std::string mc_lines_vs =
        "#version 420 core\n"
        "in vec3 Position;\nin vec4 Color;\nin vec3 Normal;\n"
        "uniform mat4 ModelViewMat;\nuniform mat4 ProjMat;\nuniform vec4 ColorModulator;\n"
        "out vec4 vertexColor;\n"
        "void main(){ int id=gl_VertexID; vec4 p=ProjMat*ModelViewMat*vec4(Position,1.0); "
        "gl_Position=p; vertexColor=Color*ColorModulator; }\n";
    bool vs_new = relaxed_fallback_compile(EShLangVertex, GLSL_VER, mc_text_vs,
                                           glslang::EShClientVulkan, err);
    bool lines_new = relaxed_fallback_compile(EShLangVertex, GLSL_VER, mc_lines_vs,
                                              glslang::EShClientVulkan, err);
    printf("MC text.vert  Vulkan input       : %s\n", vs_new ? "OK" : "FAIL");
    printf("MC lines.vert Vulkan input       : %s\n", lines_new ? "OK" : "FAIL");
    CHECK(vs_new && lines_new, "no regression: real MC vertex shaders (incl. gl_VertexID) still compile post-fix");

    printf("\nchecks=%d failures=%d\n", checks, failures);
    glslang::FinalizeProcess();
    return failures ? 1 : 0;
}