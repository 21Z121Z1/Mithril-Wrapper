/*
 * glfw_default_glyph_smoke.cpp
 *
 * Deterministic end-to-end GUI/text gate on the SAME Cocoa/CAMetalLayer path
 * used by Minecraft in CI. The GLFW bridge creates a real Mithril EGL window
 * surface; this test then exercises:
 *   persistent/coherent PBO -> full GL unpack state -> RGBA glyph atlas ->
 *   GLSL sampling -> alpha blending -> Vulkan default framebuffer -> readback.
 *
 * The atlas contains recognizable G/I glyphs plus two opposite-corner color
 * sentinels. Sampling every atlas-cell center makes stale PBO bytes, row/skip
 * addressing bugs, missing glyphs, and a vertically inverted default FBO all
 * fail independently and deterministically.
 */
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GL/glcorearb.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef GL_MAP_PERSISTENT_BIT
#define GL_MAP_PERSISTENT_BIT 0x0040
#endif
#ifndef GL_MAP_COHERENT_BIT
#define GL_MAP_COHERENT_BIT 0x0080
#endif

template <typename T>
static T glp(const char* name) {
    auto p = glfwGetProcAddress(name);
    if (!p) std::fprintf(stderr, "missing GL symbol %s\n", name);
    return reinterpret_cast<T>(p);
}

static constexpr int AW = 16;
static constexpr int AH = 8;
static constexpr int ROW = 20;
static constexpr int SRC_H = 12;
static constexpr int PREFIX = 32;
static uint8_t atlas[AW * AH * 4];

static void set_px(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    uint8_t* p = atlas + (y * AW + x) * 4;
    p[0] = r; p[1] = g; p[2] = b; p[3] = a;
}

static void build_atlas() {
    std::memset(atlas, 0, sizeof(atlas));

    // Patterns are listed top-to-bottom, then stored into GL texture rows
    // bottom-to-top so the intended visual shape is upright in GL semantics.
    static const char* G[7] = {
        "01111",
        "11000",
        "11000",
        "11011",
        "11001",
        "11001",
        "01110",
    };
    static const char* I[7] = {
        "11111",
        "00100",
        "00100",
        "00100",
        "00100",
        "00100",
        "11111",
    };
    for (int top = 0; top < 7; ++top) {
        const int y = 6 - top;
        for (int x = 0; x < 5; ++x) {
            if (G[top][x] == '1') set_px(1 + x, y, 255, 224, 32);
            if (I[top][x] == '1') set_px(9 + x, y, 32, 216, 255);
        }
    }

    // Orientation sentinels. A default-FBO vertical inversion swaps these.
    set_px(15, 0, 32, 255, 64);
    set_px(15, 7, 255, 32, 224);
}

static bool rgb_close(const uint8_t got[4], const uint8_t* exp) {
    if (exp[3] == 0) {
        return got[0] <= 2 && got[1] <= 2 && got[2] <= 2;
    }
    return std::abs((int)got[0] - (int)exp[0]) <= 2 &&
           std::abs((int)got[1] - (int)exp[1]) <= 2 &&
           std::abs((int)got[2] - (int)exp[2]) <= 2;
}

int main() {
    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return 2;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(160, 80, "mithril-glyph-smoke", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return 3;
    }
    glfwMakeContextCurrent(window);

    auto glGetString_ = glp<const GLubyte* (*)(GLenum)>("glGetString");
    auto glGetError_ = glp<GLenum (*)(void)>("glGetError");
    auto glGenBuffers_ = glp<void (*)(GLsizei, GLuint*)>("glGenBuffers");
    auto glBindBuffer_ = glp<void (*)(GLenum, GLuint)>("glBindBuffer");
    auto glBufferStorage_ = glp<void (*)(GLenum, GLsizeiptr, const void*, GLbitfield)>("glBufferStorage");
    auto glMapBufferRange_ = glp<void* (*)(GLenum, GLintptr, GLsizeiptr, GLbitfield)>("glMapBufferRange");
    auto glGenTextures_ = glp<void (*)(GLsizei, GLuint*)>("glGenTextures");
    auto glBindTexture_ = glp<void (*)(GLenum, GLuint)>("glBindTexture");
    auto glTexParameteri_ = glp<void (*)(GLenum, GLenum, GLint)>("glTexParameteri");
    auto glTexImage2D_ = glp<void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)>("glTexImage2D");
    auto glTexSubImage2D_ = glp<void (*)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*)>("glTexSubImage2D");
    auto glPixelStorei_ = glp<void (*)(GLenum, GLint)>("glPixelStorei");
    auto glCreateShader_ = glp<GLuint (*)(GLenum)>("glCreateShader");
    auto glShaderSource_ = glp<void (*)(GLuint, GLsizei, const GLchar* const*, const GLint*)>("glShaderSource");
    auto glCompileShader_ = glp<void (*)(GLuint)>("glCompileShader");
    auto glGetShaderiv_ = glp<void (*)(GLuint, GLenum, GLint*)>("glGetShaderiv");
    auto glCreateProgram_ = glp<GLuint (*)(void)>("glCreateProgram");
    auto glAttachShader_ = glp<void (*)(GLuint, GLuint)>("glAttachShader");
    auto glLinkProgram_ = glp<void (*)(GLuint)>("glLinkProgram");
    auto glGetProgramiv_ = glp<void (*)(GLuint, GLenum, GLint*)>("glGetProgramiv");
    auto glUseProgram_ = glp<void (*)(GLuint)>("glUseProgram");
    auto glGetUniformLocation_ = glp<GLint (*)(GLuint, const GLchar*)>("glGetUniformLocation");
    auto glUniform1i_ = glp<void (*)(GLint, GLint)>("glUniform1i");
    auto glGenVertexArrays_ = glp<void (*)(GLsizei, GLuint*)>("glGenVertexArrays");
    auto glBindVertexArray_ = glp<void (*)(GLuint)>("glBindVertexArray");
    auto glBindFramebuffer_ = glp<void (*)(GLenum, GLuint)>("glBindFramebuffer");
    auto glViewport_ = glp<void (*)(GLint, GLint, GLsizei, GLsizei)>("glViewport");
    auto glDisable_ = glp<void (*)(GLenum)>("glDisable");
    auto glEnable_ = glp<void (*)(GLenum)>("glEnable");
    auto glBlendFunc_ = glp<void (*)(GLenum, GLenum)>("glBlendFunc");
    auto glClearColor_ = glp<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto glClear_ = glp<void (*)(GLbitfield)>("glClear");
    auto glDrawArrays_ = glp<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
    auto glFinish_ = glp<void (*)(void)>("glFinish");
    auto glReadPixels_ = glp<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");

    if (!glGetString_ || !glGetError_ || !glGenBuffers_ || !glBindBuffer_ ||
        !glBufferStorage_ || !glMapBufferRange_ || !glGenTextures_ || !glBindTexture_ ||
        !glTexParameteri_ || !glTexImage2D_ || !glTexSubImage2D_ || !glPixelStorei_ ||
        !glCreateShader_ || !glShaderSource_ || !glCompileShader_ || !glGetShaderiv_ ||
        !glCreateProgram_ || !glAttachShader_ || !glLinkProgram_ || !glGetProgramiv_ ||
        !glUseProgram_ || !glGetUniformLocation_ || !glUniform1i_ || !glGenVertexArrays_ ||
        !glBindVertexArray_ || !glBindFramebuffer_ || !glViewport_ || !glDisable_ ||
        !glEnable_ || !glBlendFunc_ || !glClearColor_ || !glClear_ || !glDrawArrays_ ||
        !glFinish_ || !glReadPixels_) {
        return 4;
    }

    build_atlas();

    constexpr int backingBytes = ROW * SRC_H * 4;
    constexpr int totalBytes = PREFIX + backingBytes;
    GLuint pbo = 0;
    glGenBuffers_(1, &pbo);
    glBindBuffer_(GL_PIXEL_UNPACK_BUFFER, pbo);
    glBufferStorage_(GL_PIXEL_UNPACK_BUFFER, totalBytes, nullptr,
                     GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
    auto* mapped = static_cast<uint8_t*>(glMapBufferRange_(
        GL_PIXEL_UNPACK_BUFFER, 0, totalBytes,
        GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT));
    if (!mapped) {
        std::fprintf(stderr, "persistent PBO map failed\n");
        return 5;
    }
    std::memset(mapped, 0x5a, totalBytes);
    uint8_t* padded = mapped + PREFIX;
    for (int y = 0; y < AH; ++y) {
        for (int x = 0; x < AW; ++x) {
            std::memcpy(padded + ((y + 2) * ROW + (x + 2)) * 4,
                        atlas + (y * AW + x) * 4, 4);
        }
    }

    // Allocate with PBO unbound so NULL means no initial pixels, then perform
    // the real Minecraft-style subimage from a persistent PBO byte offset.
    glBindBuffer_(GL_PIXEL_UNPACK_BUFFER, 0);
    GLuint texture = 0;
    glGenTextures_(1, &texture);
    glBindTexture_(GL_TEXTURE_2D, texture);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D_(GL_TEXTURE_2D, 0, GL_RGBA8, AW, AH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindBuffer_(GL_PIXEL_UNPACK_BUFFER, pbo);
    glPixelStorei_(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei_(GL_UNPACK_ROW_LENGTH, ROW);
    glPixelStorei_(GL_UNPACK_SKIP_PIXELS, 2);
    glPixelStorei_(GL_UNPACK_SKIP_ROWS, 2);
    glTexSubImage2D_(GL_TEXTURE_2D, 0, 0, 0, AW, AH,
                     GL_RGBA, GL_UNSIGNED_BYTE,
                     reinterpret_cast<const void*>(static_cast<uintptr_t>(PREFIX)));
    glPixelStorei_(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei_(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei_(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei_(GL_UNPACK_ALIGNMENT, 4);
    glBindBuffer_(GL_PIXEL_UNPACK_BUFFER, 0);

    const char* vs =
        "#version 330 core\n"
        "out vec2 uv;\n"
        "void main(){\n"
        " vec2 p=(gl_VertexID==0)?vec2(-1,-1):((gl_VertexID==1)?vec2(3,-1):vec2(-1,3));\n"
        " uv=p*0.5+0.5; gl_Position=vec4(p,0,1);\n"
        "}";
    const char* fs =
        "#version 330 core\n"
        "in vec2 uv; uniform sampler2D atlasTex; out vec4 color;\n"
        "void main(){ color=texture(atlasTex,uv); }";
    GLuint v = glCreateShader_(GL_VERTEX_SHADER);
    GLuint f = glCreateShader_(GL_FRAGMENT_SHADER);
    glShaderSource_(v, 1, &vs, nullptr); glCompileShader_(v);
    glShaderSource_(f, 1, &fs, nullptr); glCompileShader_(f);
    GLint ok = 0;
    glGetShaderiv_(v, GL_COMPILE_STATUS, &ok); if (!ok) return 6;
    glGetShaderiv_(f, GL_COMPILE_STATUS, &ok); if (!ok) return 7;
    GLuint program = glCreateProgram_();
    glAttachShader_(program, v); glAttachShader_(program, f); glLinkProgram_(program);
    glGetProgramiv_(program, GL_LINK_STATUS, &ok); if (!ok) return 8;
    glUseProgram_(program);
    GLint sampler = glGetUniformLocation_(program, "atlasTex");
    if (sampler < 0) return 9;
    glUniform1i_(sampler, 0);

    GLuint vao = 0;
    glGenVertexArrays_(1, &vao);
    glBindVertexArray_(vao);
    glBindFramebuffer_(GL_FRAMEBUFFER, 0);
    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(window, &fbw, &fbh);
    if (fbw < AW || fbh < AH) {
        std::fprintf(stderr, "invalid framebuffer size %dx%d\n", fbw, fbh);
        return 10;
    }
    glViewport_(0, 0, fbw, fbh);
    glDisable_(GL_SCISSOR_TEST);
    glDisable_(GL_CULL_FACE);
    glDisable_(GL_DEPTH_TEST);
    glEnable_(GL_BLEND);
    glBlendFunc_(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor_(0, 0, 0, 1);
    glClear_(GL_COLOR_BUFFER_BIT);
    glBindTexture_(GL_TEXTURE_2D, texture);
    glDrawArrays_(GL_TRIANGLES, 0, 3);
    glFinish_();

    int mismatches = 0;
    for (int y = 0; y < AH; ++y) {
        for (int x = 0; x < AW; ++x) {
            const int px = ((2 * x + 1) * fbw) / (2 * AW);
            const int py = ((2 * y + 1) * fbh) / (2 * AH);
            uint8_t got[4] = {0,0,0,0};
            glReadPixels_(px, py, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, got);
            const uint8_t* exp = atlas + (y * AW + x) * 4;
            if (!rgb_close(got, exp)) {
                if (mismatches < 16) {
                    std::fprintf(stderr,
                        "cell(%d,%d) got=(%u,%u,%u,%u) exp=(%u,%u,%u,%u)\n",
                        x, y, got[0], got[1], got[2], got[3],
                        exp[0], exp[1], exp[2], exp[3]);
                }
                ++mismatches;
            }
        }
    }
    const GLenum err = glGetError_();
    std::printf("backend=%s\n", reinterpret_cast<const char*>(glGetString_(GL_VERSION)));
    std::printf("framebuffer=%dx%d glyph_cell_mismatches=%d err=0x%04x\n",
                fbw, fbh, mismatches, (unsigned)err);

    // Present once as an additional swapchain/presentation sanity check; the
    // authoritative gate above is the synchronous pre-present readback.
    glfwSwapBuffers(window);
    glfwDestroyWindow(window);
    glfwTerminate();

    if (err != GL_NO_ERROR || mismatches != 0) {
        std::fprintf(stderr, "GLFW DEFAULT G/I GLYPH SMOKE FAILED\n");
        return 1;
    }
    std::printf("GLFW DEFAULT G/I GLYPH SMOKE PASSED\n");
    return 0;
}
