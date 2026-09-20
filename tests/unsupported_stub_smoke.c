/* Remaining exported GL 3.3 entry points are intentionally unsupported.
 * They must fail closed through glGetError instead of behaving as successful
 * no-ops, otherwise Minecraft/Iris can silently continue with wrong semantics.
 */
#include <dlfcn.h>
#include <stdio.h>

#define GL_NO_ERROR 0
#define GL_INVALID_OPERATION 0x0502
#define GL_POINTS 0x0000
#define GL_POINT_FADE_THRESHOLD_SIZE 0x8128
#define GL_POINT_SPRITE_COORD_ORIGIN 0x8CA0
#define GL_UPPER_LEFT 0x8CA2
#define GL_SAMPLE_POSITION 0x8E50
#define GL_TEXTURE_2D_MULTISAMPLE_ARRAY 0x9102
#define GL_RGBA8 0x8058
#define GL_INTERLEAVED_ATTRIBS 0x8C8C

typedef unsigned int (*fn_glGetError)(void);
typedef void (*fn_glBeginTransformFeedback)(unsigned int);
typedef void (*fn_glEndTransformFeedback)(void);
typedef void (*fn_glGetMultisamplefv)(unsigned int, unsigned int, float*);
typedef void (*fn_glGetTransformFeedbackVarying)(unsigned int, unsigned int,
                                                  int, int*, int*,
                                                  unsigned int*, char*);
typedef void (*fn_glPointParameterf)(unsigned int, float);
typedef void (*fn_glPointParameterfv)(unsigned int, const float*);
typedef void (*fn_glPointParameteri)(unsigned int, int);
typedef void (*fn_glPointParameteriv)(unsigned int, const int*);
typedef void (*fn_glTexImage3DMultisample)(unsigned int, int, unsigned int,
                                            int, int, int, unsigned char);
typedef void (*fn_glTransformFeedbackVaryings)(unsigned int, int,
                                                const char* const*,
                                                unsigned int);

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) printf("ok  : %s\n", msg); \
    else { printf("FAIL: %s\n", msg); ++failures; } \
} while (0)
#define LOAD(type, var, name) type var = (type)dlsym(handle, name)
#define CHECK_UNSUPPORTED(call, label) do { \
    CHECK(getError() == GL_NO_ERROR, "error queue clean before " label); \
    call; \
    CHECK(getError() == GL_INVALID_OPERATION, label " fails closed"); \
    CHECK(getError() == GL_NO_ERROR, label " error drains cleanly"); \
} while (0)

int main(void) {
#if defined(__APPLE__)
    const char* library = "./output/libmithril.dylib";
#else
    const char* library = "./output/libmithril.so";
#endif
    void* handle = dlopen(library, RTLD_NOW | RTLD_GLOBAL);
    if (!handle) { printf("dlopen: %s\n", dlerror()); return 2; }

    LOAD(fn_glGetError, getError, "glGetError");
    LOAD(fn_glBeginTransformFeedback, beginTransformFeedback, "glBeginTransformFeedback");
    LOAD(fn_glEndTransformFeedback, endTransformFeedback, "glEndTransformFeedback");
    LOAD(fn_glGetMultisamplefv, getMultisamplefv, "glGetMultisamplefv");
    LOAD(fn_glGetTransformFeedbackVarying, getTransformFeedbackVarying,
         "glGetTransformFeedbackVarying");
    LOAD(fn_glPointParameterf, pointParameterf, "glPointParameterf");
    LOAD(fn_glPointParameterfv, pointParameterfv, "glPointParameterfv");
    LOAD(fn_glPointParameteri, pointParameteri, "glPointParameteri");
    LOAD(fn_glPointParameteriv, pointParameteriv, "glPointParameteriv");
    LOAD(fn_glTexImage3DMultisample, texImage3DMultisample, "glTexImage3DMultisample");
    LOAD(fn_glTransformFeedbackVaryings, transformFeedbackVaryings,
         "glTransformFeedbackVaryings");

    CHECK(getError && beginTransformFeedback && endTransformFeedback &&
          getMultisamplefv && getTransformFeedbackVarying && pointParameterf &&
          pointParameterfv && pointParameteri && pointParameteriv &&
          texImage3DMultisample && transformFeedbackVaryings,
          "all ten unsupported GL 3.3 exports resolve");
    if (failures) return 1;

    float fp = 1.0f, sample[2] = {-1.0f, -1.0f};
    int ip = GL_UPPER_LEFT, len = -1, size = -1;
    unsigned int type = 0;
    char name[8] = {0};
    const char* varying = "v";

    CHECK_UNSUPPORTED(beginTransformFeedback(GL_POINTS), "glBeginTransformFeedback");
    CHECK_UNSUPPORTED(endTransformFeedback(), "glEndTransformFeedback");
    CHECK_UNSUPPORTED(getMultisamplefv(GL_SAMPLE_POSITION, 0, sample), "glGetMultisamplefv");
    CHECK_UNSUPPORTED(getTransformFeedbackVarying(1, 0, 8, &len, &size, &type, name),
                      "glGetTransformFeedbackVarying");
    CHECK_UNSUPPORTED(pointParameterf(GL_POINT_FADE_THRESHOLD_SIZE, fp), "glPointParameterf");
    CHECK_UNSUPPORTED(pointParameterfv(GL_POINT_FADE_THRESHOLD_SIZE, &fp), "glPointParameterfv");
    CHECK_UNSUPPORTED(pointParameteri(GL_POINT_SPRITE_COORD_ORIGIN, ip), "glPointParameteri");
    CHECK_UNSUPPORTED(pointParameteriv(GL_POINT_SPRITE_COORD_ORIGIN, &ip), "glPointParameteriv");
    CHECK_UNSUPPORTED(texImage3DMultisample(GL_TEXTURE_2D_MULTISAMPLE_ARRAY, 1,
                                            GL_RGBA8, 1, 1, 1, 1),
                      "glTexImage3DMultisample");
    CHECK_UNSUPPORTED(transformFeedbackVaryings(1, 1, &varying, GL_INTERLEAVED_ATTRIBS),
                      "glTransformFeedbackVaryings");

    printf("\nunsupported_stub_smoke: %s (%d failure%s)\n",
           failures ? "FAIL" : "PASS", failures, failures == 1 ? "" : "s");
    if (!failures) printf("UNSUPPORTED STUB SMOKE ALL PASSED\n");
    return failures ? 1 : 0;
}
