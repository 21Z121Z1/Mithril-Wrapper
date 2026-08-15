#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

struct Attempt {
    const char* name;
    std::vector<CGLPixelFormatAttribute> attrs;
};

struct Result {
    const char* name;
    bool pixel_format = false;
    bool context = false;
    GLint virtual_screens = 0;
    GLint accelerated = 0;
    GLint color_size = 0;
    GLint alpha_size = 0;
    GLint depth_size = 0;
    GLint stencil_size = 0;
    std::string gl_version;
    CGLError choose_error = kCGLNoError;
    CGLError context_error = kCGLNoError;
};

static Result run(const Attempt& attempt) {
    Result out{};
    out.name = attempt.name;
    CGLPixelFormatObj format = nullptr;
    GLint screens = 0;
    out.choose_error = CGLChoosePixelFormat(attempt.attrs.data(), &format, &screens);
    out.virtual_screens = screens;
    if (out.choose_error != kCGLNoError || !format) return out;
    out.pixel_format = true;
    CGLDescribePixelFormat(format, 0, kCGLPFAAccelerated, &out.accelerated);
    CGLDescribePixelFormat(format, 0, kCGLPFAColorSize, &out.color_size);
    CGLDescribePixelFormat(format, 0, kCGLPFAAlphaSize, &out.alpha_size);
    CGLDescribePixelFormat(format, 0, kCGLPFADepthSize, &out.depth_size);
    CGLDescribePixelFormat(format, 0, kCGLPFAStencilSize, &out.stencil_size);

    CGLContextObj ctx = nullptr;
    out.context_error = CGLCreateContext(format, nullptr, &ctx);
    if (out.context_error == kCGLNoError && ctx) {
        out.context = true;
        CGLContextObj old = CGLGetCurrentContext();
        if (CGLSetCurrentContext(ctx) == kCGLNoError) {
            const GLubyte* version = glGetString(GL_VERSION);
            if (version) out.gl_version = reinterpret_cast<const char*>(version);
            CGLSetCurrentContext(old);
        }
        CGLDestroyContext(ctx);
    }
    CGLDestroyPixelFormat(format);
    return out;
}

static const char* b(bool value) { return value ? "true" : "false"; }

int main(int argc, char** argv) {
    const char* output = argc > 1 ? argv[1] : "nsgl-capability.json";
    const CGLPixelFormatAttribute END = static_cast<CGLPixelFormatAttribute>(0);
    const CGLPixelFormatAttribute P41 = static_cast<CGLPixelFormatAttribute>(kCGLOGLPVersion_GL4_Core);
    const CGLPixelFormatAttribute P32 = static_cast<CGLPixelFormatAttribute>(kCGLOGLPVersion_3_2_Core);

    std::vector<Attempt> attempts = {
        {"accelerated_41_minimal", {kCGLPFAAccelerated, kCGLPFAOpenGLProfile, P41, END}},
        {"accelerated_41_minecraft_like", {
            kCGLPFAAccelerated, kCGLPFAClosestPolicy,
            kCGLPFAOpenGLProfile, P41,
            kCGLPFAColorSize, static_cast<CGLPixelFormatAttribute>(24),
            kCGLPFAAlphaSize, static_cast<CGLPixelFormatAttribute>(8),
            kCGLPFADepthSize, static_cast<CGLPixelFormatAttribute>(24),
            kCGLPFAStencilSize, static_cast<CGLPixelFormatAttribute>(8),
            kCGLPFADoubleBuffer,
            END}},
        {"accelerated_32_minimal", {kCGLPFAAccelerated, kCGLPFAOpenGLProfile, P32, END}},
        {"offline_41", {kCGLPFAAllowOfflineRenderers, kCGLPFAOpenGLProfile, P41, END}},
        {"unaccelerated_41", {kCGLPFAOpenGLProfile, P41, END}},
        {"unaccelerated_32", {kCGLPFAOpenGLProfile, P32, END}},
    };

    std::vector<Result> results;
    for (const auto& attempt : attempts) results.push_back(run(attempt));

    FILE* f = std::fopen(output, "w");
    if (!f) {
        std::perror("fopen");
        return 2;
    }
    std::fprintf(f, "{\n  \"schema_version\": \"1.0\",\n  \"attempts\": [\n");
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        std::fprintf(f,
            "    {\"name\":\"%s\",\"pixel_format\":%s,\"context\":%s,"
            "\"virtual_screens\":%d,\"accelerated\":%s,\"color_size\":%d,"
            "\"alpha_size\":%d,\"depth_size\":%d,\"stencil_size\":%d,"
            "\"choose_error\":%d,\"context_error\":%d,\"gl_version\":\"%s\"}%s\n",
            r.name, b(r.pixel_format), b(r.context), r.virtual_screens, b(r.accelerated != 0),
            r.color_size, r.alpha_size, r.depth_size, r.stencil_size,
            static_cast<int>(r.choose_error), static_cast<int>(r.context_error),
            r.gl_version.c_str(), i + 1 == results.size() ? "" : ",");
    }
    std::fprintf(f, "  ]\n}\n");
    std::fclose(f);

    for (const auto& r : results) {
        std::printf("NSGL_PROBE name=%s pixel_format=%d context=%d accelerated=%d screens=%d gl=%s choose=%d context_error=%d\n",
                    r.name, r.pixel_format, r.context, r.accelerated != 0, r.virtual_screens,
                    r.gl_version.c_str(), static_cast<int>(r.choose_error), static_cast<int>(r.context_error));
    }
    const auto& minecraft = results[1];
    std::printf("NSGL_MINECRAFT_LIKE_41=%s\n", minecraft.context && minecraft.accelerated ? "true" : "false");
    return 0;
}
