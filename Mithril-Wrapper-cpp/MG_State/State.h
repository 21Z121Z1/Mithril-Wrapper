// Mithril-Wrapper - MG_State/State.h
// OpenGL Core Profile state machine. Holds all current GL state and the
// object name -> object tables. Vulkan-side handles are stored as opaque
// pointers / VkImageViews here and managed by the DirectVulkan backend
// (MG_Backend/DirectVulkan) through the backend_* C API.
//
// This is the Vulkan/MoltenVK rewrite of the former gl/state.h. All Metal
// fields (metalTexture / msl / metalVertexLib / metalPipeline) have been
// removed; shaders now carry SPIR-V words and the EGL default framebuffer
// carries VkImageView handles.
#ifndef MITHRIL_STATE_H
#define MITHRIL_STATE_H

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <vulkan/vulkan.h>

#include <GL/gl.h>
#include "../MG_Impl/Log.h"

namespace mithril {

constexpr int kMaxVertexAttribs = 16;
constexpr int kMaxColorAttachments = 8;
constexpr int kMaxTextureUnits = 32;

// ---- Vertex attribute array state (per VAO) ----
struct VertexAttrib {
    bool enabled = false;
    GLint size = 4;
    GLenum type = GL_FLOAT;
    bool normalized = false;
    bool integer = false;
    GLsizei stride = 0;
    const void* pointer = nullptr;     // offset when a VBO is bound
    GLuint boundBuffer = 0;            // GL_ARRAY_BUFFER at bind time
    GLuint divisor = 0;
};

struct VertexArray {
    GLuint id = 0;
    VertexAttrib attribs[kMaxVertexAttribs];
    GLuint elementArrayBuffer = 0;     // GL_ELEMENT_ARRAY_BUFFER bound into the VAO
};

// ---- Buffer ----
struct Buffer {
    GLuint id = 0;
    GLenum lastTarget = GL_ARRAY_BUFFER;
    GLsizeiptr size = 0;
    GLenum usage = GL_STATIC_DRAW;
    std::vector<uint8_t> data;
    void* mapped = nullptr;
    GLbitfield mapAccess = 0;
    GLintptr mapOffset = 0;
    GLsizeiptr mapLength = 0;
};

// ---- Texture ----
struct Texture {
    GLuint id = 0;
    GLenum target = GL_TEXTURE_2D;
    GLint internalFormat = GL_RGBA8;
    GLsizei width = 0;
    GLsizei height = 0;
    GLsizei depth = 1;
    GLint levels = 1;
    bool isCompressed = false;

    GLint minFilter = GL_NEAREST_MIPMAP_LINEAR;
    GLint magFilter = GL_LINEAR;
    GLint wrapS = GL_REPEAT;
    GLint wrapT = GL_REPEAT;
    GLint wrapR = GL_REPEAT;
    GLfloat borderColor[4] = {0, 0, 0, 0};
    bool generateMipmaps = false;
};

// ---- Shader ----
// GLSL -> SPIR-V translation result. The Vulkan backend consumes the SPIR-V
// words directly (vkCreateShaderModule), so there is no MSL stage here —
// MoltenVK cross-translates SPIR-V -> MSL internally.
struct Shader {
    GLuint id = 0;
    GLenum type = GL_VERTEX_SHADER;
    std::string source;
    bool compiled = false;
    std::string infoLog;
    std::vector<uint32_t> spirv;
};

// ---- Program ----
struct Uniform {
    std::string name;
    GLenum type = 0;
    GLint size = 0;
    GLint location = -1;
    GLint blockIndex = -1;
    GLint offset = -1;
    GLint arrayStride = 0;
    GLint matrixStride = 0;
    bool rowMajor = false;
    // current value cache (floats)
    std::vector<float> value;
};

struct Attrib {
    std::string name;
    GLenum type = 0;
    GLint size = 0;
    GLint location = -1;
};

struct Program {
    GLuint id = 0;
    std::vector<GLuint> attachedShaders;
    bool linked = false;
    std::string infoLog;
    std::unordered_map<std::string, Uniform> uniforms;
    std::unordered_map<GLint, std::string> uniformByLocation;
    std::unordered_map<std::string, Attrib> attribs;
    std::unordered_map<std::string, GLuint> uniformBlocks;
    // Attribute name -> location overrides set via glBindAttribLocation before
    // linking. Consumed by glLinkProgram -> shader_translate so that the
    // generated SPIR-V stage_input locations match the app's vertex descriptor.
    std::unordered_map<std::string, GLuint> attribBindings;
    // SPIR-V for each linked stage (consumed by backend_get_or_create_pipeline).
    std::vector<uint32_t> vertexSpirv;
    std::vector<uint32_t> fragmentSpirv;
};

// ---- Framebuffer ----
struct FBOAttachment {
    GLuint texture = 0;
    GLenum textarget = 0;
    GLint level = 0;
    GLint layer = 0;
    bool layered = false;
    GLuint renderbuffer = 0;
};

struct Framebuffer {
    GLuint id = 0;
    FBOAttachment colors[kMaxColorAttachments];
    FBOAttachment depth;
    FBOAttachment stencil;
    GLenum drawBuffers[kMaxColorAttachments] = {GL_NONE};
    GLsizei drawBufferCount = 0;
    GLenum readBuffer = GL_NONE;
};

// ---- Global GL state ----
struct GLState {
    bool initialized = false;

    // Object tables
    std::unordered_map<GLuint, VertexArray> vaos;
    std::unordered_map<GLuint, Buffer> buffers;
    std::unordered_map<GLuint, Texture> textures;
    std::unordered_map<GLuint, Shader> shaders;
    std::unordered_map<GLuint, Program> programs;
    std::unordered_map<GLuint, Framebuffer> framebuffers;
    std::unordered_set<GLenum> enabledCaps;

    // Bindings
    GLuint currentVAO = 0;
    GLuint currentArrayBuffer = 0;
    GLuint currentIndexBuffer = 0;
    GLuint currentUniformBuffer = 0;
    GLuint currentDrawFBO = 0;
    GLuint currentReadFBO = 0;
    GLuint currentProgram = 0;
    GLuint activeTextureUnit = 0; // GL_TEXTURE0..N
    GLuint boundTextures[kMaxTextureUnits] = {0}; // indexed by unit, target-less simplification
    GLenum boundTextureTargets[kMaxTextureUnits] = {GL_TEXTURE_2D};

    /*
     * EGL-backed default framebuffer. When the EGL layer (egl/egl.mm) makes a
     * surface current, it installs the current swapchain image's VkImageView
     * here so that GL commands issued against framebuffer 0 render directly
     * into the on-screen drawable. A depth/stencil VkImageView is also
     * installed when the EGLConfig requests one. These are owned by the
     * EGLSurface (swapchain), not by the GL object table, so the GL
     * texture/sampler paths never touch them.
     *
     * The underlying VkImage handles are also tracked so that image-level
     * operations (glBlitFramebuffer / glReadPixels involving FBO 0) can
     * reference them directly without going through the GL texture table.
     */
    VkImageView eglDefaultColor = VK_NULL_HANDLE;
    VkImageView eglDefaultDepth = VK_NULL_HANDLE;
    VkImage     eglDefaultColorImage = VK_NULL_HANDLE;
    VkImage     eglDefaultDepthImage = VK_NULL_HANDLE;
    VkFormat    eglDefaultColorFormat = VK_FORMAT_UNDEFINED;
    VkFormat    eglDefaultDepthFormat = VK_FORMAT_UNDEFINED;
    int   eglDefaultWidth  = 0;
    int   eglDefaultHeight = 0;

    // Clear values
    float clearColor[4] = {0, 0, 0, 0};
    double clearDepth = 1.0;
    GLint clearStencil = 0;

    // Depth
    bool depthTest = false;
    bool depthMask = true;
    GLenum depthFunc = GL_LESS;
    double depthNear = 0.0;
    double depthFar = 1.0;

    // Blend
    bool blend = false;
    GLenum blendSrcRGB = GL_ONE, blendDstRGB = GL_ZERO;
    GLenum blendSrcA = GL_ONE, blendDstA = GL_ZERO;
    GLenum blendEqRGB = GL_FUNC_ADD, blendEqA = GL_FUNC_ADD;
    float blendColor[4] = {0, 0, 0, 0};

    // Color mask
    bool colorMask[4] = {true, true, true, true};

    // Stencil
    bool stencilTest = false;
    GLuint stencilMask = ~0u;
    GLuint stencilBackMask = ~0u;
    GLenum stencilFunc = GL_ALWAYS, stencilBackFunc = GL_ALWAYS;
    GLint stencilRef = 0, stencilBackRef = 0;
    GLuint stencilValueMask = ~0u, stencilBackValueMask = ~0u;
    GLenum stencilSfail = GL_KEEP, stencilDpfail = GL_KEEP, stencilDppass = GL_KEEP;
    GLenum stencilBackSfail = GL_KEEP, stencilBackDpfail = GL_KEEP, stencilBackDppass = GL_KEEP;

    // Rasterizer
    bool cullFace = false;
    GLenum cullMode = GL_BACK;
    GLenum frontFace = GL_CCW;
    GLenum polygonModeFront = GL_FILL;
    GLenum polygonModeBack = GL_FILL;
    float polygonOffsetFactor = 0.0f;
    float polygonOffsetUnits = 0.0f;
    bool polygonOffsetFill = false;
    float lineWidth = 1.0f;
    float pointSize = 1.0f;
    bool scissorTest = false;
    bool dither = true;
    bool multisample = false;
    bool sampleAlphaToCoverage = false;
    bool sampleCoverage = false;
    bool programPointSize = false;
    bool primitiveRestart = false;
    GLuint primitiveRestartIndex = 0;
    bool framebufferSRGB = false;

    // Viewport / scissor
    GLint viewportX = 0, viewportY = 0;
    GLsizei viewportW = 0, viewportH = 0;
    GLint scissorX = 0, scissorY = 0;
    GLsizei scissorW = 0, scissorH = 0;

    // Pixel storage
    GLint unpackAlignment = 4;
    GLint packAlignment = 4;
    GLint unpackRowLength = 0;
    GLint unpackImageHeight = 0;
    GLint unpackSkipRows = 0;
    GLint unpackSkipPixels = 0;
    GLint unpackSkipImages = 0;

    // Error
    GLenum error = GL_NO_ERROR;

    // Name allocator
    GLuint nextName = 1;

    /*
     * Proxy texture state. glTexImage2D(GL_PROXY_TEXTURE_2D, ...) doesn't
     * create a real texture; it just validates the format/size combo. The
     * result is queried via glGetTexLevelParameteriv(GL_PROXY_TEXTURE_2D,
     * 0, GL_TEXTURE_WIDTH, &w). If the combo is unsupported, w == 0.
     * Minecraft uses this to probe the max texture size.
     */
    struct ProxyTextureState {
        GLint width = 0;
        GLint height = 0;
        GLint depth = 0;
        GLint internalFormat = 0;
        bool valid = false;
    } proxyTexture2D;
};

extern GLState* g_state;

// Initialise the state machine (idempotent). Returns true on success.
bool state_init();

/*
 * Allocate a fresh, independent GLState (used by the EGL layer so each
 * EGLContext owns its own state). The returned pointer is heap-owned and
 * must be released with state_destroy(). Does NOT touch the global
 * `g_state` — the EGL layer is responsible for swapping g_state to point
 * at the chosen context's state inside eglMakeCurrent.
 */
GLState* state_create();
void state_destroy(GLState* s);

VertexArray* state_get_vao(GLuint id);
Buffer* state_get_buffer(GLuint id);
Texture* state_get_texture(GLuint id);
Shader* state_get_shader(GLuint id);
Program* state_get_program(GLuint id);
Framebuffer* state_get_framebuffer(GLuint id);

void state_set_error(GLenum err);
GLenum state_take_error();

// Allocate `n` fresh names into `out` for a given table type.
void state_gen_names(const char* kind, GLsizei n, GLuint* out);

} // namespace mithril

#endif // MITHRIL_STATE_H
