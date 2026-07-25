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

#include <cstdint>
#include <string>
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
