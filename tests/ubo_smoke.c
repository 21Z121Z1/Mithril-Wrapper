/* DirectMetal UBO vertical smoke.
 *
 * Proves a real GL uniform-block workload traverses source reflection,
 * indexed/ranged buffer binding, persistent native MTLBuffer storage, shader
 * resource binding, draw encoding, and synchronous readback. The Vulkan
 * reference backend deliberately rejects user UBOs until its descriptor seam
 * is extended, so this focused smoke is DirectMetal-only for now.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_ACTIVE_UNIFORM_BLOCKS 0x8A36
#define GL_UNIFORM_BLOCK_BINDING 0x8A3F
#define GL_UNIFORM_BLOCK_DATA_SIZE 0x8A40
#define GL_UNIFORM_BLOCK_NAME_LENGTH 0x8A41
#define GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS 0x8A42
#define GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES 0x8A43
#define GL_UNIFORM_BLOCK_REFERENCED_BY_VERTEX_SHADER 0x8A44
#define GL_UNIFORM_BLOCK_REFERENCED_BY_FRAGMENT_SHADER 0x8A46
#define GL_UNIFORM_BLOCK_INDEX 0x8A3A
#define GL_UNIFORM_OFFSET 0x8A3B
#define GL_INVALID_INDEX 0xFFFFFFFFu
#define GL_UNIFORM_BUFFER 0x8A11
#define GL_UNIFORM_BUFFER_BINDING 0x8A28
#define GL_UNIFORM_BUFFER_START 0x8A29
#define GL_UNIFORM_BUFFER_SIZE 0x8A2A
#define GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT 0x8A34
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_FLOAT 0x1406
#define GL_FALSE 0
#define GL_TRIANGLES 0x0004
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_RENDERER 0x1F01
#define GL_NO_ERROR 0

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef intptr_t GLintptr;
typedef intptr_t GLsizeiptr;
typedef unsigned char GLboolean;

typedef GLuint (*PFN_CreateShader)(GLenum);
typedef void (*PFN_ShaderSource)(GLuint, GLsizei, const char* const*, const GLint*);
typedef void (*PFN_CompileShader)(GLuint);
typedef void (*PFN_GetShaderiv)(GLuint, GLenum, GLint*);
typedef GLuint (*PFN_CreateProgram)(void);
typedef void (*PFN_AttachShader)(GLuint, GLuint);
typedef void (*PFN_LinkProgram)(GLuint);
typedef void (*PFN_GetProgramiv)(GLuint, GLenum, GLint*);
typedef void (*PFN_UseProgram)(GLuint);
typedef GLuint (*PFN_GetUniformBlockIndex)(GLuint, const char*);
typedef void (*PFN_GetActiveUniformBlockiv)(GLuint, GLuint, GLenum, GLint*);
typedef void (*PFN_GetActiveUniformBlockName)(GLuint, GLuint, GLsizei, GLsizei*, char*);
typedef void (*PFN_UniformBlockBinding)(GLuint, GLuint, GLuint);
typedef GLint (*PFN_GetUniformLocation)(GLuint, const char*);
typedef void (*PFN_GetUniformIndices)(GLuint, GLsizei, const char* const*, GLuint*);
typedef void (*PFN_GetActiveUniformsiv)(GLuint, GLsizei, const GLuint*, GLenum, GLint*);
typedef void (*PFN_GetActiveUniformName)(GLuint, GLuint, GLsizei, GLsizei*, char*);
typedef void (*PFN_GenBuffers)(GLsizei, GLuint*);
typedef void (*PFN_BindBuffer)(GLenum, GLuint);
typedef void (*PFN_BufferData)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void (*PFN_BufferSubData)(GLenum, GLintptr, GLsizeiptr, const void*);
typedef void (*PFN_BindBufferBase)(GLenum, GLuint, GLuint);
typedef void (*PFN_BindBufferRange)(GLenum, GLuint, GLuint, GLintptr, GLsizeiptr);
typedef void (*PFN_GetIntegerv)(GLenum, GLint*);
typedef void (*PFN_GetIntegeri_v)(GLenum, GLuint, GLint*);
typedef void (*PFN_GenVertexArrays)(GLsizei, GLuint*);
typedef void (*PFN_BindVertexArray)(GLuint);
typedef void (*PFN_EnableVertexAttribArray)(GLuint);
typedef void (*PFN_VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
typedef void (*PFN_ClearColor)(float, float, float, float);
typedef void (*PFN_Clear)(GLbitfield);
typedef void (*PFN_DrawArrays)(GLenum, GLint, GLsizei);
typedef void (*PFN_Finish)(void);
typedef void (*PFN_ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
typedef GLenum (*PFN_GetError)(void);
typedef const unsigned char* (*PFN_GetString)(GLenum);

static int failures;
#define CHECK(condition, ...) do {                                           \
    if (condition) { printf("ok  : "); } else { printf("FAIL: "); ++failures; } \
    printf(__VA_ARGS__); printf("\n");                                      \
} while (0)

static int pixel_is(const unsigned char* pixel, int r, int g, int b, int a) {
    return abs((int)pixel[0] - r) <= 3 && abs((int)pixel[1] - g) <= 3 &&
           abs((int)pixel[2] - b) <= 3 && abs((int)pixel[3] - a) <= 3;
}

static const char* VS =
    "#version 330 core\n"
    "layout(location=0) in vec2 position;\n"
    "layout(std140) uniform TransformBlock { vec4 offset; } transformData;\n"
    "void main() { gl_Position = vec4(position + transformData.offset.xy, 0.0, 1.0); }\n";

static const char* FS =
    "#version 420 core\n"
    "layout(std140, binding=3) uniform ColorBlock { vec4 color; } colorData;\n"
    "layout(location=0) out vec4 fragmentColor;\n"
    "void main() { fragmentColor = colorData.color; }\n";

#define LOAD(name, type, symbol) type name = (type)dlsym(library, symbol)

int main(void) {
    const char* path = getenv("MITHRIL_LIBRARY");
    if (!path || !*path) path = "./output/libmithril.dylib";
    void* library = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
    if (!library) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }

    LOAD(createShader, PFN_CreateShader, "glCreateShader");
    LOAD(shaderSource, PFN_ShaderSource, "glShaderSource");
    LOAD(compileShader, PFN_CompileShader, "glCompileShader");
    LOAD(getShaderiv, PFN_GetShaderiv, "glGetShaderiv");
    LOAD(createProgram, PFN_CreateProgram, "glCreateProgram");
    LOAD(attachShader, PFN_AttachShader, "glAttachShader");
    LOAD(linkProgram, PFN_LinkProgram, "glLinkProgram");
    LOAD(getProgramiv, PFN_GetProgramiv, "glGetProgramiv");
    LOAD(useProgram, PFN_UseProgram, "glUseProgram");
    LOAD(getBlockIndex, PFN_GetUniformBlockIndex, "glGetUniformBlockIndex");
    LOAD(getBlockiv, PFN_GetActiveUniformBlockiv, "glGetActiveUniformBlockiv");
    LOAD(getBlockName, PFN_GetActiveUniformBlockName, "glGetActiveUniformBlockName");
    LOAD(blockBinding, PFN_UniformBlockBinding, "glUniformBlockBinding");
    LOAD(getUniformLocation, PFN_GetUniformLocation, "glGetUniformLocation");
    LOAD(getUniformIndices, PFN_GetUniformIndices, "glGetUniformIndices");
    LOAD(getActiveUniformsiv, PFN_GetActiveUniformsiv, "glGetActiveUniformsiv");
    LOAD(getActiveUniformName, PFN_GetActiveUniformName, "glGetActiveUniformName");
    LOAD(genBuffers, PFN_GenBuffers, "glGenBuffers");
    LOAD(bindBuffer, PFN_BindBuffer, "glBindBuffer");
    LOAD(bufferData, PFN_BufferData, "glBufferData");
    LOAD(bufferSubData, PFN_BufferSubData, "glBufferSubData");
    LOAD(bindBufferBase, PFN_BindBufferBase, "glBindBufferBase");
    LOAD(bindBufferRange, PFN_BindBufferRange, "glBindBufferRange");
    LOAD(getIntegerv, PFN_GetIntegerv, "glGetIntegerv");
    LOAD(getIntegeri, PFN_GetIntegeri_v, "glGetIntegeri_v");
    LOAD(genVertexArrays, PFN_GenVertexArrays, "glGenVertexArrays");
    LOAD(bindVertexArray, PFN_BindVertexArray, "glBindVertexArray");
    LOAD(enableVertexAttribArray, PFN_EnableVertexAttribArray, "glEnableVertexAttribArray");
    LOAD(vertexAttribPointer, PFN_VertexAttribPointer, "glVertexAttribPointer");
    LOAD(clearColor, PFN_ClearColor, "glClearColor");
    LOAD(clear, PFN_Clear, "glClear");
    LOAD(drawArrays, PFN_DrawArrays, "glDrawArrays");
    LOAD(finish, PFN_Finish, "glFinish");
    LOAD(readPixels, PFN_ReadPixels, "glReadPixels");
    LOAD(getError, PFN_GetError, "glGetError");
    LOAD(getString, PFN_GetString, "glGetString");

    CHECK(createShader && shaderSource && compileShader && getShaderiv &&
          createProgram && attachShader && linkProgram && getProgramiv &&
          useProgram && getBlockIndex && getBlockiv && getBlockName &&
          blockBinding && getUniformIndices && getActiveUniformsiv &&
          getActiveUniformName &&
          bindBufferBase && bindBufferRange && getIntegeri &&
          drawArrays && readPixels, "required UBO symbols resolve");
    const char* renderer = getString ? (const char*)getString(GL_RENDERER) : 0;
    CHECK(renderer && strstr(renderer, "DirectMetal"),
          "context is explicitly DirectMetal (%s)", renderer ? renderer : "null");

    GLuint vs = createShader(GL_VERTEX_SHADER);
    GLuint fs = createShader(GL_FRAGMENT_SHADER);
    shaderSource(vs, 1, &VS, 0);
    shaderSource(fs, 1, &FS, 0);
    compileShader(vs);
    compileShader(fs);
    GLint status = 0;
    getShaderiv(vs, GL_COMPILE_STATUS, &status);
    CHECK(status, "vertex uniform-block shader compiles");
    getShaderiv(fs, GL_COMPILE_STATUS, &status);
    CHECK(status, "fragment uniform-block shader compiles");
    GLuint program = createProgram();
    attachShader(program, vs);
    attachShader(program, fs);
    linkProgram(program);
    getProgramiv(program, GL_LINK_STATUS, &status);
    CHECK(status, "uniform-block program links");
    useProgram(program);

    GLint block_count = 0;
    getProgramiv(program, GL_ACTIVE_UNIFORM_BLOCKS, &block_count);
    CHECK(block_count == 2, "two active uniform blocks reflected (%d)", block_count);
    GLuint transform_block = getBlockIndex(program, "TransformBlock");
    GLuint color_block = getBlockIndex(program, "ColorBlock");
    CHECK(transform_block != GL_INVALID_INDEX && color_block != GL_INVALID_INDEX,
          "block names preserve the GL resource namespace");
    GLint value = -1;
    getBlockiv(program, color_block, GL_UNIFORM_BLOCK_BINDING, &value);
    CHECK(value == 3, "source layout(binding=3) survives internal lowering (%d)", value);
    getBlockiv(program, transform_block,
               GL_UNIFORM_BLOCK_REFERENCED_BY_VERTEX_SHADER, &value);
    CHECK(value == 1, "TransformBlock is vertex-stage referenced");
    getBlockiv(program, color_block,
               GL_UNIFORM_BLOCK_REFERENCED_BY_FRAGMENT_SHADER, &value);
    CHECK(value == 1, "ColorBlock is fragment-stage referenced");
    getBlockiv(program, color_block, GL_UNIFORM_BLOCK_DATA_SIZE, &value);
    CHECK(value == 16, "std140 ColorBlock size is reflected (%d)", value);
    getBlockiv(program, color_block, GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS, &value);
    CHECK(value == 1, "ColorBlock exposes one active member (%d)", value);
    GLint reflected_member_index = -1;
    getBlockiv(program, color_block,
               GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES,
               &reflected_member_index);
    char reflected_member_name[64] = {0};
    getActiveUniformName(program, (GLuint)reflected_member_index,
                         sizeof(reflected_member_name), 0,
                         reflected_member_name);
    CHECK(strcmp(reflected_member_name, "ColorBlock.color") == 0,
          "active member uses the API block-qualified name (%s)",
          reflected_member_name);
    char block_name[64] = {0};
    getBlockName(program, color_block, sizeof(block_name), 0, block_name);
    CHECK(strcmp(block_name, "ColorBlock") == 0,
          "active block name query returns ColorBlock (%s)", block_name);
    CHECK(getUniformLocation(program, "ColorBlock.color") == -1,
          "uniform-block members are not location-addressable");
    const char* member_name = "ColorBlock.color";
    GLuint member_index = GL_INVALID_INDEX;
    getUniformIndices(program, 1, &member_name, &member_index);
    CHECK(member_index != GL_INVALID_INDEX,
          "uniform-block member has an active-uniform index");
    getActiveUniformsiv(program, 1, &member_index,
                        GL_UNIFORM_BLOCK_INDEX, &value);
    CHECK(value == (GLint)color_block,
          "member reports its owning block index (%d)", value);
    getActiveUniformsiv(program, 1, &member_index, GL_UNIFORM_OFFSET, &value);
    CHECK(value == 0, "std140 vec4 member offset is reflected (%d)", value);

    GLint alignment = 0;
    getIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &alignment);
    CHECK(alignment == 256, "reported UBO range alignment is explicit (%d)", alignment);

    const float positions[6] = {-0.8f, -0.8f, 0.8f, -0.8f, 0.0f, 0.8f};
    GLuint vao = 0, vertex_buffer = 0;
    genVertexArrays(1, &vao);
    bindVertexArray(vao);
    genBuffers(1, &vertex_buffer);
    bindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
    bufferData(GL_ARRAY_BUFFER, sizeof(positions), positions, GL_STATIC_DRAW);
    enableVertexAttribArray(0);
    vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), 0);

    GLuint transform_buffer = 0, color_buffer = 0;
    const float transform[4] = {0, 0, 0, 0};
    unsigned char ranged_color[272] = {0};
    const float red[4] = {1, 0, 0, 1};
    memcpy(ranged_color + 256, red, sizeof(red));
    genBuffers(1, &transform_buffer);
    bindBuffer(GL_UNIFORM_BUFFER, transform_buffer);
    bufferData(GL_UNIFORM_BUFFER, sizeof(transform), transform, GL_STATIC_DRAW);
    bindBufferBase(GL_UNIFORM_BUFFER, 0, transform_buffer);
    genBuffers(1, &color_buffer);
    bindBuffer(GL_UNIFORM_BUFFER, color_buffer);
    bufferData(GL_UNIFORM_BUFFER, sizeof(ranged_color), ranged_color, GL_STATIC_DRAW);
    bindBufferRange(GL_UNIFORM_BUFFER, 1, color_buffer, 256, 16);
    blockBinding(program, transform_block, 0);
    blockBinding(program, color_block, 1);
    getBlockiv(program, color_block, GL_UNIFORM_BLOCK_BINDING, &value);
    CHECK(value == 1, "glUniformBlockBinding updates frontend state (%d)", value);
    getIntegeri(GL_UNIFORM_BUFFER_BINDING, 1, &value);
    CHECK(value == (GLint)color_buffer, "indexed UBO binding query returns buffer %u", color_buffer);
    getIntegeri(GL_UNIFORM_BUFFER_START, 1, &value);
    CHECK(value == 256, "indexed UBO range starts at byte 256 (%d)", value);
    getIntegeri(GL_UNIFORM_BUFFER_SIZE, 1, &value);
    CHECK(value == 16, "indexed UBO range spans 16 bytes (%d)", value);
    CHECK(getError() == GL_NO_ERROR, "UBO setup produces no GL error");

    clearColor(0, 0, 0, 1);
    clear(GL_COLOR_BUFFER_BIT);
    drawArrays(GL_TRIANGLES, 0, 3);
    finish();
    unsigned char pixel[4] = {0};
    readPixels(256, 256, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    CHECK(pixel_is(pixel, 255, 0, 0, 255),
          "ranged resident UBO drives a red Metal draw (%u,%u,%u,%u)",
          pixel[0], pixel[1], pixel[2], pixel[3]);

    const float green[4] = {0, 1, 0, 1};
    bindBuffer(GL_UNIFORM_BUFFER, color_buffer);
    bufferSubData(GL_UNIFORM_BUFFER, 256, sizeof(green), green);
    clear(GL_COLOR_BUFFER_BIT);
    drawArrays(GL_TRIANGLES, 0, 3);
    finish();
    readPixels(256, 256, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    CHECK(pixel_is(pixel, 0, 255, 0, 255),
          "content-version update refreshes resident UBO (%u,%u,%u,%u)",
          pixel[0], pixel[1], pixel[2], pixel[3]);
    CHECK(getError() == GL_NO_ERROR, "draw and readback finish without GL errors");

    dlclose(library);
    printf("\nubo_smoke: %s (%d failure%s)\n", failures ? "FAIL" : "PASS",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
