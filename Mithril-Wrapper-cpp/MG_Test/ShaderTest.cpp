// Mithril-Wrapper - MG_Test/ShaderTest.cpp
// Unit tests for mithril::shader_translate (MG_Impl/Shader.cpp), which
// translates desktop GLSL Core Profile source into Vulkan SPIR-V via glslang.
//
// Covers the happy path (vertex + fragment), the SPIR-V magic-number contract,
// the failure path (syntax error -> non-empty info log), the #version 150 ->
// 330 auto-upgrade, and the no-crash contract for an empty source string.
#include <gtest/gtest.h>

#include "../MG_Impl/Shader.h"

#include <GL/gl.h>
#include <spirv_cross.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
constexpr uint32_t kSpirvMagic = 0x07230203u;
} // namespace

// A minimal vertex shader compiles to a non-empty SPIR-V blob.
TEST(Shader, TranslatesBasicVertexShader) {
    std::vector<uint32_t> spirv;
    std::string info;
    std::string src = "#version 330 core\nvoid main(){gl_Position=vec4(0);}\n";
    bool ok = mithril::shader_translate(GL_VERTEX_SHADER, src, spirv, info);
    EXPECT_TRUE(ok) << "info log: " << info;
    EXPECT_FALSE(spirv.empty());
}

// A minimal fragment shader compiles to a non-empty SPIR-V blob.
TEST(Shader, TranslatesBasicFragmentShader) {
    std::vector<uint32_t> spirv;
    std::string info;
    std::string src =
        "#version 330 core\nout vec4 fragColor;\nvoid main(){fragColor=vec4(1);}\n";
    bool ok = mithril::shader_translate(GL_FRAGMENT_SHADER, src, spirv, info);
    EXPECT_TRUE(ok) << "info log: " << info;
    EXPECT_FALSE(spirv.empty());
}

// The emitted SPIR-V begins with the canonical magic number (0x07230203).
TEST(Shader, SpirvHasValidMagicNumber) {
    std::vector<uint32_t> spirv;
    std::string info;
    std::string src = "#version 330 core\nvoid main(){gl_Position=vec4(0);}\n";
    ASSERT_TRUE(mithril::shader_translate(GL_VERTEX_SHADER, src, spirv, info));
    ASSERT_FALSE(spirv.empty());
    EXPECT_EQ(spirv[0], kSpirvMagic);
}

// A shader with a syntax error fails to compile; the info log is populated.
TEST(Shader, FailsOnInvalidShaderSyntax) {
    std::vector<uint32_t> spirv;
    std::string info;
    // Missing semicolon after vec4(0) -> parse error.
    std::string src = "#version 330 core\nvoid main(){gl_Position=vec4(0)}\n";
    bool ok = mithril::shader_translate(GL_VERTEX_SHADER, src, spirv, info);
    EXPECT_FALSE(ok);
    EXPECT_FALSE(info.empty());
}

// GLSL #version 150 (below the Vulkan minimum of 330) is auto-upgraded to 330
// so desktop shaders like Minecraft's blit_screen compile under the Vulkan
// client.
TEST(Shader, UpgradesVersion150To330) {
    std::vector<uint32_t> spirv;
    std::string info;
    std::string src = "#version 150\nvoid main(){gl_Position=vec4(0);}\n";
    bool ok = mithril::shader_translate(GL_VERTEX_SHADER, src, spirv, info);
    EXPECT_TRUE(ok) << "info log: " << info;
    EXPECT_FALSE(spirv.empty());
}

// An empty source string has no #version and no main(); behaviour is
// implementation-defined. The only contract is that the call returns without
// crashing.
TEST(Shader, EmptySourceDoesNotCrash) {
    std::vector<uint32_t> spirv;
    std::string info;
    mithril::shader_translate(GL_VERTEX_SHADER, "", spirv, info);
    SUCCEED();
}

// Explicit attrib_bindings are accepted by shader_translate and must not
// break compilation. SPIRV-Cross is linked to the test target, so we reflect
// the emitted SPIR-V to confirm the bare `in` declarations survive as
// stage_inputs AND that apply_attrib_bindings() injected the correct
// layout(location=N) qualifiers (Position->3, TexCoord->7).
TEST(Shader, AppliesExplicitAttribBindings) {
    std::string src =
        "#version 330 core\n"
        "in vec3 Position;\n"
        "in vec2 TexCoord;\n"
        "void main(){ gl_Position = vec4(Position, TexCoord.x); }\n";

    // WITH attrib_bindings (5-arg form): call must succeed and emit non-empty
    // SPIR-V.
    std::vector<uint32_t> spirv_with;
    std::string info_with;
    std::unordered_map<std::string, GLuint> bindings{{"Position", 3}, {"TexCoord", 7}};
    bool ok_with = mithril::shader_translate(GL_VERTEX_SHADER, src, spirv_with, info_with, &bindings);
    EXPECT_TRUE(ok_with) << "info log: " << info_with;
    ASSERT_FALSE(spirv_with.empty());

    // Reflect stage_inputs via SPIRV-Cross: both declarations must be present.
    spirv_cross::Compiler compiler(spirv_with.data(), spirv_with.size());
    auto resources = compiler.get_shader_resources();
    std::unordered_map<std::string, uint32_t> seen;
    for (const auto& res : resources.stage_inputs) {
        seen[res.name] = compiler.get_decoration(res.id, spv::DecorationLocation);
    }
    EXPECT_EQ(seen.count("Position"), 1u);
    EXPECT_EQ(seen.count("TexCoord"), 1u);
    // Verify the injected layout(location=N) qualifiers survived into the
    // SPIR-V: apply_attrib_bindings() should have pinned Position->3 and
    // TexCoord->7 from the bindings map.
    EXPECT_EQ(seen["Position"], 3u);
    EXPECT_EQ(seen["TexCoord"], 7u);

    // WITHOUT attrib_bindings (4-arg form, defaults to nullptr): the same
    // source must also compile, proving the bindings map does not break
    // compilation.
    std::vector<uint32_t> spirv_without;
    std::string info_without;
    bool ok_without = mithril::shader_translate(GL_VERTEX_SHADER, src, spirv_without, info_without);
    EXPECT_TRUE(ok_without) << "info log: " << info_without;
    EXPECT_FALSE(spirv_without.empty());
}

// Without attrib_bindings, glslang's setAutoMapLocations(true) /
// setAutoMapBindings(true) auto-assign locations to bare `in` declarations so
// the shader compiles without explicit layout() qualifiers.
TEST(Shader, AutoMapsBindingsWhenNoneProvided) {
    std::vector<uint32_t> spirv;
    std::string info;
    std::string src =
        "#version 330 core\n"
        "in vec3 a;\n"
        "in vec3 b;\n"
        "in vec3 c;\n"
        "void main(){ gl_Position = vec4(a + b + c, 1.0); }\n";
    bool ok = mithril::shader_translate(GL_VERTEX_SHADER, src, spirv, info);
    EXPECT_TRUE(ok) << "info log: " << info;
    EXPECT_FALSE(spirv.empty());
}
