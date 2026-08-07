/* M2 shader-pipeline smoke test: dlopen libmithril.so, then exercise the S2
 * shader/program/uniform family end to end:
 *   - glCreateShader/glShaderSource/glCompileShader (GLSL 150 auto-upgrade)
 *   - glCreateProgram/glAttachShader/glLinkProgram/glUseProgram
 *   - glGetUniformLocation / glGetAttribLocation
 *   - glUniform* setters + glGetUniform* getters round-trip
 *   - compile-failure path (bad GLSL -> COMPILE_STATUS false + info log)
 *   - loose uniforms folded into the synthetic block (reflected by name)
 *
 * Build (from project root):
 *   gcc -o tests/shader_smoke tests/shader_smoke.c -ldl
 *   LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu ./tests/shader_smoke
 */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* GL 3.3 core constants used here (values from glcorearb.h) */
#define GL_VERTEX_SHADER   0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_SHADER_TYPE         0x8B4F
#define GL_COMPILE_STATUS      0x8B81
#define GL_LINK_STATUS         0x8B82
#define GL_INFO_LOG_LENGTH     0x8B84
#define GL_SHADER_SOURCE_LENGTH 0x8B88
#define GL_ACTIVE_UNIFORMS     0x8B86
#define GL_ACTIVE_ATTRIBUTES   0x8B89
#define GL_ATTACHED_SHADERS    0x8B85
#define GL_DELETE_STATUS       0x8B80

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLboolean;
typedef float GLfloat;
typedef char GLchar;

typedef GLuint  (*glCreateShader_fn)(GLenum);
typedef void    (*glDeleteShader_fn)(GLuint);
typedef void    (*glShaderSource_fn)(GLuint, GLsizei, const GLchar* const*, const GLint*);
typedef void    (*glCompileShader_fn)(GLuint);
typedef void    (*glGetShaderiv_fn)(GLuint, GLenum, GLint*);
typedef void    (*glGetShaderInfoLog_fn)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef GLuint  (*glCreateProgram_fn)(void);
typedef void    (*glDeleteProgram_fn)(GLuint);
typedef void    (*glAttachShader_fn)(GLuint, GLuint);
typedef void    (*glLinkProgram_fn)(GLuint);
typedef void    (*glUseProgram_fn)(GLuint);
typedef void    (*glGetProgramiv_fn)(GLuint, GLenum, GLint*);
typedef GLint   (*glGetUniformLocation_fn)(GLuint, const GLchar*);
typedef GLint   (*glGetAttribLocation_fn)(GLuint, const GLchar*);
typedef void    (*glUniform4f_fn)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void    (*glUniformMatrix4fv_fn)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void    (*glGetUniformfv_fn)(GLuint, GLint, GLfloat*);
typedef int     (*glGetError_fn)(void);

int fails = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
    else printf("ok  : %s\n", msg); \
} while (0)

int main(void) {
    void* h = dlopen("./output/libmithril.so", RTLD_NOW | RTLD_GLOBAL);
    if (!h) { printf("dlopen: %s\n", dlerror()); return 2; }

    glCreateShader_fn        createShader  = (glCreateShader_fn)dlsym(h, "glCreateShader");
    glDeleteShader_fn        deleteShader  = (glDeleteShader_fn)dlsym(h, "glDeleteShader");
    glShaderSource_fn        shaderSource  = (glShaderSource_fn)dlsym(h, "glShaderSource");
    glCompileShader_fn       compileShader = (glCompileShader_fn)dlsym(h, "glCompileShader");
    glGetShaderiv_fn         getShaderiv   = (glGetShaderiv_fn)dlsym(h, "glGetShaderiv");
    glGetShaderInfoLog_fn    getShaderInfoLog = (glGetShaderInfoLog_fn)dlsym(h, "glGetShaderInfoLog");
    glCreateProgram_fn       createProgram = (glCreateProgram_fn)dlsym(h, "glCreateProgram");
    glDeleteProgram_fn       deleteProgram = (glDeleteProgram_fn)dlsym(h, "glDeleteProgram");
    glAttachShader_fn        attachShader  = (glAttachShader_fn)dlsym(h, "glAttachShader");
    glLinkProgram_fn         linkProgram   = (glLinkProgram_fn)dlsym(h, "glLinkProgram");
    glUseProgram_fn          useProgram    = (glUseProgram_fn)dlsym(h, "glUseProgram");
    glGetProgramiv_fn        getProgramiv  = (glGetProgramiv_fn)dlsym(h, "glGetProgramiv");
    glGetUniformLocation_fn  getUniformLoc = (glGetUniformLocation_fn)dlsym(h, "glGetUniformLocation");
    glGetAttribLocation_fn   getAttribLoc  = (glGetAttribLocation_fn)dlsym(h, "glGetAttribLocation");
    glUniform4f_fn           uniform4f     = (glUniform4f_fn)dlsym(h, "glUniform4f");
    glUniformMatrix4fv_fn    uniformMat4   = (glUniformMatrix4fv_fn)dlsym(h, "glUniformMatrix4fv");
    glGetUniformfv_fn        getUniformfv  = (glGetUniformfv_fn)dlsym(h, "glGetUniformfv");
    glGetError_fn            getError      = (glGetError_fn)dlsym(h, "glGetError");

    if (!(createShader && deleteShader && shaderSource && compileShader && getShaderiv &&
          createProgram && deleteProgram && attachShader && linkProgram && useProgram &&
          getProgramiv && getUniformLoc && getAttribLoc && uniform4f && uniformMat4 &&
          getUniformfv)) {
        printf("missing core symbols\n"); return 3;
    }

    /* ---- compile a valid vertex shader (GLSL 150, auto-upgraded) ---------- */
    GLuint vs = createShader(GL_VERTEX_SHADER);
    CHECK(vs != 0, "glCreateShader(GL_VERTEX_SHADER)");
    const GLchar* vs_src =
        "#version 150\n"
        "in vec3 Position;\n"
        "uniform mat4 ModelViewProj;\n"
        "uniform vec4 tint;\n"
        "out vec4 v_color;\n"
        "void main() { v_color = tint; gl_Position = ModelViewProj * vec4(Position, 1.0); }\n";
    shaderSource(vs, 1, &vs_src, NULL);
    compileShader(vs);
    int ok = 0;
    getShaderiv(vs, GL_COMPILE_STATUS, &ok);
    CHECK(ok, "glCompileShader(vs) COMPILE_STATUS true");

    /* ---- fragment shader with a loose uniform + sampler ------------------- */
    GLuint fs = createShader(GL_FRAGMENT_SHADER);
    CHECK(fs != 0, "glCreateShader(GL_FRAGMENT_SHADER)");
    const GLchar* fs_src =
        "#version 150\n"
        "uniform vec4 tint;\n"
        "uniform sampler2D tex;\n"
        "in vec4 v_color;\n"
        "out vec4 fragColor;\n"
        "void main() { fragColor = v_color * tint + texture(tex, vec2(0.0)); }\n";
    shaderSource(fs, 1, &fs_src, NULL);
    compileShader(fs);
    getShaderiv(fs, GL_COMPILE_STATUS, &ok);
    CHECK(ok, "glCompileShader(fs) COMPILE_STATUS true");

    /* ---- link and query ---------------------------------------------------- */
    GLuint prog = createProgram();
    attachShader(prog, vs);
    attachShader(prog, fs);
    linkProgram(prog);
    getProgramiv(prog, GL_LINK_STATUS, &ok);
    CHECK(ok, "glLinkProgram LINK_STATUS true");

    int nuni = -1, natt = -1;
    getProgramiv(prog, GL_ACTIVE_UNIFORMS, &nuni);
    getProgramiv(prog, GL_ACTIVE_ATTRIBUTES, &natt);
    printf("     ACTIVE_UNIFORMS=%d ACTIVE_ATTRIBUTES=%d\n", nuni, natt);
    CHECK(nuni >= 2, "both loose uniform 'tint' and sampler 'tex' reflected");

    int loc_tint = getUniformLoc(prog, "tint");
    int loc_mat  = getUniformLoc(prog, "ModelViewProj");
    int loc_tex  = getUniformLoc(prog, "tex");
    printf("     locations: tint=%d ModelViewProj=%d tex=%d\n", loc_tint, loc_mat, loc_tex);
    CHECK(loc_tint >= 0 && loc_mat >= 0 && loc_tex >= 0, "all uniform locations resolved");
    CHECK(getUniformLoc(prog, "does_not_exist") == -1, "unknown uniform -> -1");

    int loc_pos = getAttribLoc(prog, "Position");
    CHECK(loc_pos >= 0, "glGetAttribLocation(Position) resolved");

    /* ---- uniform set/get round-trip through the current program ------------ */
    useProgram(prog);
    uniform4f(loc_tint, 0.25f, 0.5f, 0.75f, 1.0f);
    GLfloat back[4] = {0, 0, 0, 0};
    getUniformfv(prog, loc_tint, back);
    CHECK(back[0] == 0.25f && back[1] == 0.5f && back[2] == 0.75f && back[3] == 1.0f,
          "glUniform4f -> glGetUniformfv round-trip");

    static const GLfloat ident[16] = {
        1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
    };
    uniformMat4(loc_mat, 1, 0, ident);
    GLfloat mat_back[16] = {0};
    getUniformfv(prog, loc_mat, mat_back);
    CHECK(mat_back[0] == 1.0f && mat_back[5] == 1.0f && mat_back[10] == 1.0f,
          "glUniformMatrix4fv round-trip");

    /* ---- failure path: bad GLSL -------------------------------------------- */
    GLuint bad = createShader(GL_VERTEX_SHADER);
    const GLchar* bad_src = "#version 150\nvoid main() { this is not valid; }\n";
    shaderSource(bad, 1, &bad_src, NULL);
    compileShader(bad);
    getShaderiv(bad, GL_COMPILE_STATUS, &ok);
    CHECK(!ok, "bad GLSL -> COMPILE_STATUS false");
    GLint loglen = 0;
    getShaderiv(bad, GL_INFO_LOG_LENGTH, &loglen);
    char logbuf[256];
    GLsizei n = 0;
    getShaderInfoLog(bad, sizeof(logbuf), &n, logbuf);
    CHECK(loglen > 1 && n > 0, "bad shader produces a non-empty info log");

    /* ---- delete-program resets current program ----------------------------- */
    useProgram(0);

    deleteShader(vs); deleteShader(fs); deleteShader(bad);
    deleteProgram(prog);
    getError();  /* drain any accumulated errors */

    printf("\n%d failure(s)\n", fails);
    if (fails == 0) { printf("SHADER SMOKE ALL PASSED\n"); return 0; }
    return 1;
}
