/*
 * mc_shader_dump.c — 用 Minecraft 真实核心着色器源码走完整编译链，
 * 导出最终 SPIR-V（MITHRIL_DUMP_SPIRV 指定目录），供离线 spirv-val +
 * spirv-cross --msl 复核 MoltenVK 翻译路径。
 *
 * 覆盖加载界面/主菜单实际使用的着色器（mojang logo、进度条、背景、
 * 按钮文字、标题 splash、blit）+ 一个 Sodium 风格 SSBO 着色器。
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <GL/glcorearb.h>

typedef GLuint  (*createShader_fn)(GLenum);
typedef void    (*shaderSource_fn)(GLuint, GLsizei, const GLchar* const*, const GLint*);
typedef void    (*compileShader_fn)(GLuint);
typedef void    (*getShaderiv_fn)(GLuint, GLenum, GLint*);
typedef void    (*getShaderInfoLog_fn)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef GLuint  (*createProgram_fn)(void);
typedef void    (*attachShader_fn)(GLuint, GLuint);
typedef void    (*linkProgram_fn)(GLuint);
typedef void    (*getProgramiv_fn)(GLuint, GLenum, GLint*);
typedef void    (*getProgramInfoLog_fn)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void    (*deleteProgram_fn)(GLuint);
typedef void    (*deleteShader_fn)(GLuint);

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) { printf("ok : %s\n", msg); } \
    else      { printf("FAIL: %s\n", msg); ++failures; } \
} while (0)

/* ---- MC 核心着色器（assets/minecraft/shaders/core，1.17+ 原文） ---- */

/* position_color：LoadingOverlay 背景 fill、进度条边框 */
static const char kPositionColorVS[] =
    "#version 150\n"
    "in vec3 Position;\n"
    "in vec4 Color;\n"
    "uniform mat4 ModelViewMat;\n"
    "uniform mat4 ProjMat;\n"
    "out vec4 vertexColor;\n"
    "void main() {\n"
    "    gl_Position = ProjMat * ModelViewMat * vec4(Position, 1.0);\n"
    "    vertexColor = Color;\n"
    "}\n";
static const char kPositionColorFS[] =
    "#version 150\n"
    "in vec4 vertexColor;\n"
    "out vec4 fragColor;\n"
    "void main() { fragColor = vertexColor; }\n";

/* position_tex：mojang logo 贴图四边形 */
static const char kPositionTexVS[] =
    "#version 150\n"
    "in vec3 Position;\n"
    "in vec2 UV0;\n"
    "uniform mat4 ModelViewMat;\n"
    "uniform mat4 ProjMat;\n"
    "out vec2 texCoord0;\n"
    "void main() {\n"
    "    gl_Position = ProjMat * ModelViewMat * vec4(Position, 1.0);\n"
    "    texCoord0 = UV0;\n"
    "}\n";
static const char kPositionTexFS[] =
    "#version 150\n"
    "in vec2 texCoord0;\n"
    "out vec4 fragColor;\n"
    "uniform vec4 ColorModulator;\n"
    "uniform sampler2D Sampler0;\n"
    "void main() {\n"
    "    vec4 color = texture(Sampler0, texCoord0) * ColorModulator;\n"
    "    if (color.a == 0.0) discard;\n"
    "    fragColor = color;\n"
    "}\n";

/* position_tex_color：主菜单按钮/文字 */
static const char kPositionTexColorVS[] =
    "#version 150\n"
    "in vec3 Position;\n"
    "in vec4 Color;\n"
    "in vec2 UV0;\n"
    "uniform mat4 ModelViewMat;\n"
    "uniform mat4 ProjMat;\n"
    "out vec4 vertexColor;\n"
    "out vec2 texCoord0;\n"
    "void main() {\n"
    "    gl_Position = ProjMat * ModelViewMat * vec4(Position, 1.0);\n"
    "    vertexColor = Color;\n"
    "    texCoord0 = UV0;\n"
    "}\n";
static const char kPositionTexColorFS[] =
    "#version 150\n"
    "in vec4 vertexColor;\n"
    "in vec2 texCoord0;\n"
    "out vec4 fragColor;\n"
    "uniform vec4 ColorModulator;\n"
    "uniform sampler2D Sampler0;\n"
    "void main() {\n"
    "    vec4 color = texture(Sampler0, texCoord0) * vertexColor * ColorModulator;\n"
    "    if (color.a == 0.0) discard;\n"
    "    fragColor = color;\n"
    "}\n";

/* blit_screen：MC 引擎 blit（GUI 贴图绘制原语） */
static const char kBlitVS[] =
    "#version 150\n"
    "in vec3 Position;\n"
    "in vec2 UV;\n"
    "out vec2 texCoord;\n"
    "void main() { texCoord = UV; gl_Position = vec4(Position, 1.0); }\n";
static const char kBlitFS[] =
    "#version 150\n"
    "in vec2 texCoord;\n"
    "out vec4 fragColor;\n"
    "uniform sampler2D Sampler0;\n"
    "void main() { fragColor = texture(Sampler0, texCoord); }\n";

/* Sodium 风格 chunk 着色器：SSBO + 多采样 + fog（1.17+ 压力面） */
static const char kSodiumVS[] =
    "#version 460 core\n"
    "layout(location = 0) in vec3 aPos;\n"
    "layout(location = 1) in vec3 aColor;\n"
    "layout(location = 2) in vec2 aTex;\n"
    "layout(location = 3) in vec2 aLight;\n"
    "layout(std140, binding = 0) uniform Matrices {\n"
    "    mat4 uProj;\n"
    "    mat4 uModelView;\n"
    "};\n"
    "layout(std140, binding = 1) uniform Fog {\n"
    "    vec4 uFogColor;\n"
    "    float uFogStart;\n"
    "    float uFogEnd;\n"
    "};\n"
    "out vec3 vColor;\n"
    "out vec2 vTex;\n"
    "out float vFog;\n"
    "void main() {\n"
    "    vec4 view = uModelView * vec4(aPos, 1.0);\n"
    "    gl_Position = uProj * view;\n"
    "    vColor = aColor;\n"
    "    vTex = aTex;\n"
    "    float d = length(view.xyz);\n"
    "    vFog = clamp((uFogEnd - d) / (uFogEnd - uFogStart), 0.0, 1.0);\n"
    "}\n";
static const char kSodiumFS[] =
    "#version 460 core\n"
    "in vec3 vColor;\n"
    "in vec2 vTex;\n"
    "in float vFog;\n"
    "layout(std140, binding = 1) uniform Fog {\n"
    "    vec4 uFogColor;\n"
    "    float uFogStart;\n"
    "    float uFogEnd;\n"
    "};\n"
    "layout(binding = 0) uniform sampler2D uBlockTex;\n"
    "layout(binding = 1) uniform sampler2D uLightTex;\n"
    "layout(std430, binding = 2) readonly buffer ChunkData {\n"
    "    vec4 tint[];\n"
    "};\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    vec4 tex0 = texture(uBlockTex, vTex);\n"
    "    vec4 tex1 = texture(uLightTex, vTex * 0.5 + 0.25);\n"
    "    int idx = int(gl_FragCoord.x) % max(int(tint.length()), 1);\n"
    "    vec4 c = tex0 * tex1 * tint[idx] * vec4(vColor, 1.0);\n"
    "    fragColor = mix(uFogColor, c, vFog);\n"
    "}\n";

/* rendertype_solid 风格：顶点光照 + 多重纹理（实体渲染基础） */
static const char kEntityVS[] =
    "#version 150\n"
    "in vec3 Position;\n"
    "in vec4 Color;\n"
    "in vec2 UV0;\n"
    "in ivec2 UV2;\n"
    "in vec3 Normal;\n"
    "uniform mat4 ModelViewMat;\n"
    "uniform mat4 ProjMat;\n"
    "uniform mat3 NormalMat;\n"
    "uniform vec3 Light0_Direction;\n"
    "uniform vec3 Light1_Direction;\n"
    "out vec4 vertexColor;\n"
    "out vec2 texCoord0;\n"
    "out vec4 normal;\n"
    "void main() {\n"
    "    gl_Position = ProjMat * ModelViewMat * vec4(Position, 1.0);\n"
    "    vec4 c = Color;\n"
    "    texCoord0 = UV0;\n"
    "    normal = vec4(NormalMat * Normal, 0.0);\n"
    "    vertexColor = c;\n"
    "}\n";
static const char kEntityFS[] =
    "#version 150\n"
    "in vec4 vertexColor;\n"
    "in vec2 texCoord0;\n"
    "in vec4 normal;\n"
    "out vec4 fragColor;\n"
    "uniform vec4 ColorModulator;\n"
    "uniform vec3 Light0_Direction;\n"
    "uniform vec3 Light1_Direction;\n"
    "uniform sampler2D Sampler0;\n"
    "uniform sampler2D Sampler1;\n"
    "uniform sampler2D Sampler2;\n"
    "void main() {\n"
    "    vec4 color = texture(Sampler0, texCoord0) * vertexColor * ColorModulator;\n"
    "    vec4 light = texture(Sampler2, texCoord0);\n"
    "    color *= mix(vec4(1.0), light, 0.5);\n"
    "    if (color.a < 0.1) discard;\n"
    "    fragColor = color;\n"
    "}\n";

typedef struct { const char* name; const char* vs; const char* fs; } Prog;
static const Prog kProgs[] = {
    {"position_color",     kPositionColorVS,    kPositionColorFS},
    {"position_tex",       kPositionTexVS,      kPositionTexFS},
    {"position_tex_color", kPositionTexColorVS, kPositionTexColorFS},
    {"blit_screen",        kBlitVS,             kBlitFS},
    {"sodium_chunk",       kSodiumVS,           kSodiumFS},
    {"entity_solid",       kEntityVS,           kEntityFS},
};

int main(int argc, char** argv) {
    const char* lib = argc > 1 ? argv[1] : "./libmithril.so";
    void* h = dlopen(lib, RTLD_NOW | RTLD_GLOBAL);
    if (!h) { fprintf(stderr, "dlopen(%s): %s\n", lib, dlerror()); return 2; }

    createShader_fn createShader = (createShader_fn)dlsym(h, "glCreateShader");
    shaderSource_fn shaderSource = (shaderSource_fn)dlsym(h, "glShaderSource");
    compileShader_fn compileShader = (compileShader_fn)dlsym(h, "glCompileShader");
    getShaderiv_fn getShaderiv = (getShaderiv_fn)dlsym(h, "glGetShaderiv");
    getShaderInfoLog_fn getShaderInfoLog = (getShaderInfoLog_fn)dlsym(h, "glGetShaderInfoLog");
    createProgram_fn createProgram = (createProgram_fn)dlsym(h, "glCreateProgram");
    attachShader_fn attachShader = (attachShader_fn)dlsym(h, "glAttachShader");
    linkProgram_fn linkProgram = (linkProgram_fn)dlsym(h, "glLinkProgram");
    getProgramiv_fn getProgramiv = (getProgramiv_fn)dlsym(h, "glGetProgramiv");
    getProgramInfoLog_fn getProgramInfoLog = (getProgramInfoLog_fn)dlsym(h, "glGetProgramInfoLog");
    deleteProgram_fn deleteProgram = (deleteProgram_fn)dlsym(h, "glDeleteProgram");
    deleteShader_fn deleteShader = (deleteShader_fn)dlsym(h, "glDeleteShader");
    if (!createShader || !linkProgram) { fprintf(stderr, "missing symbols\n"); return 2; }

    char log[4096];
    for (size_t i = 0; i < sizeof(kProgs) / sizeof(kProgs[0]); ++i) {
        const Prog* p = &kProgs[i];
        GLuint vs = createShader(GL_VERTEX_SHADER);
        shaderSource(vs, 1, &p->vs, NULL);
        compileShader(vs);
        GLint ok = 0;
        getShaderiv(vs, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            log[0] = 0; GLsizei n = 0;
            getShaderInfoLog(vs, sizeof(log), &n, log);
            printf("FAIL: %s VS compile: %s\n", p->name, log);
            ++failures;
            continue;
        }
        GLuint fs = createShader(GL_FRAGMENT_SHADER);
        shaderSource(fs, 1, &p->fs, NULL);
        compileShader(fs);
        getShaderiv(fs, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            log[0] = 0; GLsizei n = 0;
            getShaderInfoLog(fs, sizeof(log), &n, log);
            printf("FAIL: %s FS compile: %s\n", p->name, log);
            ++failures;
            continue;
        }
        GLuint prog = createProgram();
        attachShader(prog, vs);
        attachShader(prog, fs);
        linkProgram(prog);
        getProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok) {
            log[0] = 0; GLsizei n = 0;
            getProgramInfoLog(prog, sizeof(log), &n, log);
            printf("FAIL: %s link: %s\n", p->name, log);
            ++failures;
        } else {
            printf("ok : %s linked\n", p->name);
        }
        deleteShader(vs);
        deleteShader(fs);
        deleteProgram(prog);
    }

    printf(failures ? "MC SHADER DUMP FAILED\n" : "MC SHADER DUMP OK\n");
    return failures ? 1 : 0;
}
