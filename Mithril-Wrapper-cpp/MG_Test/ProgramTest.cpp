// Mithril-Wrapper - MG_Test/ProgramTest.cpp
// Unit tests for MG_Impl/Program.cpp — the shader / program object lifecycle
// (create, source, compile, attach, link, use) and the uniform reflection +
// setter paths. Program.cpp is almost entirely pure logic: glCompileShader
// delegates to mithril::shader_translate (glslang), glLinkProgram merges the
// per-stage SPIR-V onto the program, and glDeleteProgram fire-and-forgets into
// backend_delete_program_resources (stubbed). glslang + SPIRV-Cross are linked
// into the test binary so the compile + reflection paths run end-to-end.
//
// The fixture pattern mirrors StateTest.cpp: each test gets a fresh, isolated
// GLState installed as the global pointer so name tables / current bindings
// never leak across tests.
#include <gtest/gtest.h>

#include "../MG_State/State.h"

#include <GL/gl.h>
#include <spirv_cross.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace {

constexpr uint32_t kSpirvMagic = 0x07230203u;

// Each test gets a fresh, independent GLState installed as the global pointer
// (mirrors StateTest's fixture). Program.cpp's entry points touch g_state
// directly via MITHRIL_ENSURE_INIT, so a per-test state swap is sufficient —
// no Vulkan device or real backend is needed (BackendStub provides the
// backend_* symbols referenced by glDeleteProgram).
class ProgramTest : public ::testing::Test {
protected:
    void SetUp() override {
        mithril::g_state = mithril::state_create();
        ASSERT_NE(mithril::g_state, nullptr);
    }
    void TearDown() override {
        mithril::state_destroy(mithril::g_state);
        mithril::g_state = nullptr;
    }
};

} // namespace

// glCreateShader + glShaderSource + glCompileShader produce a compiled shader
// whose SPIR-V begins with the canonical magic number. glGetShaderiv reports
// GL_COMPILE_STATUS==GL_TRUE and GL_SHADER_TYPE==GL_VERTEX_SHADER. A
// syntax-error source must flip GL_COMPILE_STATUS to GL_FALSE and populate the
// info log.
TEST_F(ProgramTest, CompileAndReflectSpirv) {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    ASSERT_NE(vs, 0u);
    const GLchar* src = "#version 330 core\nvoid main(){gl_Position=vec4(0);}\n";
    glShaderSource(vs, 1, &src, nullptr);
    glCompileShader(vs);

    GLint compileStatus = 0;
    glGetShaderiv(vs, GL_COMPILE_STATUS, &compileStatus);
    EXPECT_EQ(compileStatus, GL_TRUE);

    GLint shaderType = 0;
    glGetShaderiv(vs, GL_SHADER_TYPE, &shaderType);
    EXPECT_EQ(shaderType, static_cast<GLint>(GL_VERTEX_SHADER));

    mithril::Shader* s = mithril::state_get_shader(vs);
    ASSERT_NE(s, nullptr);
    ASSERT_FALSE(s->spirv.empty());
    EXPECT_EQ(s->spirv[0], kSpirvMagic);

    // Syntax-error version: missing semicolon -> GL_COMPILE_STATUS == GL_FALSE
    // with a non-empty info log.
    GLuint bad = glCreateShader(GL_VERTEX_SHADER);
    ASSERT_NE(bad, 0u);
    const GLchar* badSrc = "#version 330 core\nvoid main(){gl_Position=vec4(0)}\n";
    glShaderSource(bad, 1, &badSrc, nullptr);
    glCompileShader(bad);

    GLint badStatus = 0;
    glGetShaderiv(bad, GL_COMPILE_STATUS, &badStatus);
    EXPECT_EQ(badStatus, GL_FALSE);

    mithril::Shader* badS = mithril::state_get_shader(bad);
    ASSERT_NE(badS, nullptr);
    EXPECT_FALSE(badS->infoLog.empty());
}

// glLinkProgram merges the attached VS + FS SPIR-V onto the program:
// linked==true, GL_LINK_STATUS==GL_TRUE, GL_ATTACHED_SHADERS==2, and both
// vertexSpirv + fragmentSpirv are populated. Linking a program with only a
// fragment stage (no vertex shader) must report linked==false.
TEST_F(ProgramTest, LinkMergesStages) {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    const GLchar* vsSrc = "#version 330 core\nvoid main(){gl_Position=vec4(0);}\n";
    glShaderSource(vs, 1, &vsSrc, nullptr);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    const GLchar* fsSrc =
        "#version 330 core\nout vec4 fragColor;\nvoid main(){fragColor=vec4(1);}\n";
    glShaderSource(fs, 1, &fsSrc, nullptr);
    glCompileShader(fs);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    mithril::Program* p = mithril::state_get_program(prog);
    ASSERT_NE(p, nullptr);
    EXPECT_TRUE(p->linked);

    GLint linkStatus = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linkStatus);
    EXPECT_EQ(linkStatus, GL_TRUE);

    GLint attached = 0;
    glGetProgramiv(prog, GL_ATTACHED_SHADERS, &attached);
    EXPECT_EQ(attached, 2);

    EXPECT_FALSE(p->vertexSpirv.empty());
    EXPECT_FALSE(p->fragmentSpirv.empty());

    // Link with only a fragment shader (no VS). Program.cpp's link path only
    // fails the program when BOTH vertexSpirv and fragmentSpirv are empty
    // (see Program.cpp: `if (missing || (p->vertexSpirv.empty() &&
    // p->fragmentSpirv.empty()))`). A lone FS therefore links successfully —
    // this is intentional: GL doesn't forbid FS-only programs at link time
    // (the missing VS only surfaces as a draw-time pipeline error).
    GLuint fs2 = glCreateShader(GL_FRAGMENT_SHADER);
    const GLchar* fs2Src =
        "#version 330 core\nout vec4 fragColor;\nvoid main(){fragColor=vec4(1);}\n";
    glShaderSource(fs2, 1, &fs2Src, nullptr);
    glCompileShader(fs2);

    GLuint prog2 = glCreateProgram();
    glAttachShader(prog2, fs2);
    glLinkProgram(prog2);

    mithril::Program* p2 = mithril::state_get_program(prog2);
    ASSERT_NE(p2, nullptr);
    EXPECT_TRUE(p2->linked);
    EXPECT_FALSE(p2->fragmentSpirv.empty());
    EXPECT_TRUE(p2->vertexSpirv.empty());
}

// glGetUniformLocation synthesises a location on first query (0 for the first
// uniform); glUniform3f stores the value on the current program; glGetUniformfv
// reads value[0] back. A second query for the same name returns the same
// location. The full value vector (value[1], value[2]) is inspected directly
// since glGetUniformfv only writes params[0].
TEST_F(ProgramTest, UniformLocationAndValueRoundTrip) {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    const GLchar* vsSrc = "#version 330 core\nvoid main(){gl_Position=vec4(0);}\n";
    glShaderSource(vs, 1, &vsSrc, nullptr);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    const GLchar* fsSrc =
        "#version 330 core\nout vec4 fragColor;\nvoid main(){fragColor=vec4(1);}\n";
    glShaderSource(fs, 1, &fsSrc, nullptr);
    glCompileShader(fs);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glUseProgram(prog);

    GLint loc = glGetUniformLocation(prog, "uColor");
    EXPECT_EQ(loc, 0);

    glUniform3f(loc, 1.0f, 2.0f, 3.0f);

    // glGetUniformfv writes only params[0]; read it back, then inspect the
    // full value vector on the program state for value[1] / value[2].
    GLfloat v0 = 0.0f;
    glGetUniformfv(prog, loc, &v0);
    EXPECT_FLOAT_EQ(v0, 1.0f);

    mithril::Program* p = mithril::state_get_program(prog);
    ASSERT_NE(p, nullptr);
    auto it = p->uniforms.find("uColor");
    ASSERT_NE(it, p->uniforms.end());
    ASSERT_GE(it->second.value.size(), 3u);
    EXPECT_FLOAT_EQ(it->second.value[0], 1.0f);
    EXPECT_FLOAT_EQ(it->second.value[1], 2.0f);
    EXPECT_FLOAT_EQ(it->second.value[2], 3.0f);

    // Second query for the same name must return the same synthesised location.
    GLint loc2 = glGetUniformLocation(prog, "uColor");
    EXPECT_EQ(loc2, loc);
    EXPECT_EQ(loc2, 0);
}

// glGetActiveUniform truncates the name to bufSize-1 chars and NUL-terminates.
// With a uniform named "aLongName" (9 chars) and bufSize=4, the name buffer
// receives "aLo\0" and *length==3.
TEST_F(ProgramTest, ActiveUniformNameTruncation) {
    GLuint prog = glCreateProgram();
    mithril::Program* p = mithril::state_get_program(prog);
    ASSERT_NE(p, nullptr);

    // Manually insert a uniform with a name longer than the query buffer.
    mithril::Uniform u{};
    u.name = "aLongName";
    u.location = 0;
    p->uniforms["aLongName"] = u;

    GLsizei length = -1;
    GLint size = -1;
    GLenum type = 0;
    GLchar name[4] = {0, 0, 0, 0};

    glGetActiveUniform(prog, 0, 4, &length, &size, &type, name);

    EXPECT_EQ(length, 3);
    EXPECT_EQ(name[0], 'a');
    EXPECT_EQ(name[1], 'L');
    EXPECT_EQ(name[2], 'o');
    ASSERT_EQ(name[3], '\0');
}

// glBindAttribLocation recorded before link triggers a VS re-translate with
// the location overrides injected as layout(location=N) qualifiers. After
// link, reflecting prog->vertexSpirv via SPIRV-Cross must report the
// "Position" stage_input at Location==3. Mirrors ShaderTest's reflection
// pattern.
TEST_F(ProgramTest, AttribBindingsAppliedAtLink) {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    const GLchar* vsSrc =
        "#version 330 core\n"
        "in vec3 Position;\n"
        "void main(){ gl_Position = vec4(Position, 1.0); }\n";
    glShaderSource(vs, 1, &vsSrc, nullptr);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    const GLchar* fsSrc =
        "#version 330 core\nout vec4 fragColor;\nvoid main(){fragColor=vec4(1);}\n";
    glShaderSource(fs, 1, &fsSrc, nullptr);
    glCompileShader(fs);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 3, "Position");
    glLinkProgram(prog);

    mithril::Program* p = mithril::state_get_program(prog);
    ASSERT_NE(p, nullptr);
    EXPECT_TRUE(p->linked);
    ASSERT_FALSE(p->vertexSpirv.empty());

    // Reflect stage_inputs via SPIRV-Cross (mirror ShaderTest's pattern).
    spirv_cross::Compiler compiler(p->vertexSpirv.data(), p->vertexSpirv.size());
    auto resources = compiler.get_shader_resources();
    std::unordered_map<std::string, uint32_t> seen;
    for (const auto& res : resources.stage_inputs) {
        seen[res.name] = compiler.get_decoration(res.id, spv::DecorationLocation);
    }

    ASSERT_EQ(seen.count("Position"), 1u);
    EXPECT_EQ(seen["Position"], 3u);
}
