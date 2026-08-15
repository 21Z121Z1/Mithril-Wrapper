// Mithril-Wrapper - MG_Impl/Getter.cpp
// GL state getters: glGet*v, glGetString / glGetStringi, glGetError.
//
// This is the Vulkan/MoltenVK rewrite of the former gl/getter.cpp. The GPU
// name / VRAM no longer come from MTLDevice — they come from the Vulkan
// backend via backend_physical_device_name() / backend_vram_bytes() (which
// read VkPhysicalDeviceProperties / VkPhysicalDeviceMemoryProperties under
// the hood). The F3 debug strings are built in Getter_gpu.mm.
//
// F3 debug info mapping (mirrors MobileGlues' approach):
//   GL_VERSION     — "3.3.0 Mithril-Wrapper 1.0 (Vulkan 1.2 / MoltenVK)"
//                    with Minecraft §b color highlight on the name.
//   GL_RENDERER    — GPU name | Vulkan 1.2 | Mithril-Wrapper (+ VRAM if known)
//   GL_VENDOR      — Project maintainers
//   GL_SHADING_LANGUAGE_VERSION — "3.30 Mithril-Wrapper (glslang -> SPIR-V)"
//
// Custom enums (private, Mithril-specific — probed by Minecraft mods / F3):
//   MITHRIL_SETTINGS (0x0402) — returns a multi-line dump of renderer config
//     (Vulkan device info, shader pipeline, depth/stencil format, etc.) so it
//     appears on Minecraft's F3 debug screen.
//   MITHRIL_BACKEND_GETTER (0x0401) — added to a standard GL enum to bypass
//     the OpenGL facade and query the real Vulkan backend string.
#include "includes.h"

#include <cstdio>
#include <sstream>
#include <cstring>
#include <cmath>

/* ---- Mithril custom enums (mirror MobileGlues' GL_SETTINGS_MG / GL_BACKEND_GETTER_MG) ---- */
#define MITHRIL_BACKEND_GETTER  0x0401
#define MITHRIL_SETTINGS        0x0402

/* GL_CONTEXT_FLAG_FORWARD_COMPATIBLE_BIT is not always defined in the
 * glcorearb.h we ship; define it here (standard GL value = 0x00000001). */
#ifndef GL_CONTEXT_FLAG_FORWARD_COMPATIBLE_BIT
#define GL_CONTEXT_FLAG_FORWARD_COMPATIBLE_BIT 0x00000001
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

/* ---- GPU info (Vulkan backend) ----
 * The GL_RENDERER / GL_VENDOR bypass strings are built on first query from
 * the live VkPhysicalDevice (via the backend_* C API in MG_Backend/Backend.h)
 * so Minecraft's F3 screen and crash reports show real GPU info. The helper
 * is implemented in Getter_gpu.mm (Objective-C++) so it can format the
 * Vulkan device name + VRAM into the F3-friendly multi-field string. On
 * non-Apple builds a static fallback is used.
 */
#if defined(__APPLE__)
extern "C" const char* mithril_get_gpu_renderer_string(void);
extern "C" const char* mithril_get_vulkan_device_name(void);
extern "C" const char* mithril_get_vulkan_api_string(void);
extern "C" uint64_t    mithril_get_vram_bytes(void);
extern "C" const char* mithril_get_settings_dump(void);
#endif

/* ---- Strings ---- */
// Vendor string lists the project developers (mirrors MobileGlues' pattern of
// putting the maintainer names in GL_VENDOR).
static const char* kVendor   = "EternityQwQ, yitenchen123";
#if defined(__APPLE__)
// GL_RENDERER is built on first query from the live VkPhysicalDevice (see
// Getter_gpu.mm). Falls back to the static string if Vulkan is unavailable.
#else
static const char* kRenderer = "Mithril-Wrapper (Vulkan 1.2 / MoltenVK backend)";
#endif
// GL semantic closure 2026-08-16: advertise only the semantic surface for which this branch has
// executable Minecraft/DirectMetal evidence.  Reporting GL 4.6 made every
// core 4.x command an implicit promise even when parts of that state machine
// were still compatibility stubs.  Keep the core contract at 3.3 and expose
// newer behavior only through individually proven extensions below.
static const char* kShadingLangVer = "3.30 Mithril-Wrapper (glslang -> SPIR-V)";
static const char* gl_version_string(void) {
    static std::string cached;
    if (cached.empty()) {
        cached = std::string("3.3.0 §bMithril-Wrapper§r 1.0 (")
               + backend_api_string() + ")";
    }
    return cached.c_str();
}

// Extension advertisement is a capability contract, not a symbol-presence
// compatibility list.  Every entry here must have a matching manifest record,
// exported symbols, state semantics, and an executable oracle.  The contract
// gate in ci/e2e/check_gl_semantic_contract.py enforces exact equality.
static const char* kExtensions[] = {
    "GL_ARB_vertex_attrib_binding",
    "GL_ARB_clip_control",
    "GL_ARB_multi_draw_indirect",
    "GL_ARB_indirect_parameters",
    "GL_MITHRIL_wrapper",
};

extern "C" {

GLenum glGetError(void) {
    MITHRIL_ENSURE_INIT();
    // GL semantic closure: expose the error state recorded by the translation
    // layer.  Silently draining it made every conformance assertion false-green.
    return mithril::state_take_error();
}

void glGetBooleanv(GLenum pname, GLboolean* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    switch (pname) {
        case GL_DEPTH_WRITEMASK: *params = g_state->depthMask ? GL_TRUE : GL_FALSE; break;
        case GL_DEPTH_TEST:      *params = g_state->depthTest ? GL_TRUE : GL_FALSE; break;
        case GL_BLEND:           *params = g_state->blends[0].enabled ? GL_TRUE : GL_FALSE; break;
        case GL_STENCIL_TEST:    *params = g_state->stencilTest ? GL_TRUE : GL_FALSE; break;
        case GL_CULL_FACE:       *params = g_state->cullFace ? GL_TRUE : GL_FALSE; break;
        case GL_SCISSOR_TEST:    *params = g_state->scissorTest ? GL_TRUE : GL_FALSE; break;
        case GL_DITHER:          *params = g_state->dither ? GL_TRUE : GL_FALSE; break;
        case GL_COLOR_WRITEMASK:
            params[0] = g_state->colorMask[0][0] ? GL_TRUE : GL_FALSE;
            params[1] = g_state->colorMask[0][1] ? GL_TRUE : GL_FALSE;
            params[2] = g_state->colorMask[0][2] ? GL_TRUE : GL_FALSE;
            params[3] = g_state->colorMask[0][3] ? GL_TRUE : GL_FALSE;
            break;
        default: *params = GL_FALSE; break;
    }
}

void glGetIntegerv(GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    // NOTE: Do NOT apply a blanket `pname >= MITHRIL_BACKEND_GETTER` offset here.
    // MITHRIL_BACKEND_GETTER is 0x0401, but nearly every standard GL enum is a
    // *larger* value (e.g. GL_MAJOR_VERSION = 0x821B, GL_VIEWPORT = 0x0BA2,
    // GL_MAX_TEXTURE_SIZE = 0x0D33). A `>=` test would hijack all of them,
    // subtract 0x0401, land in `default`, and return 0 — silently breaking
    // glGetIntegerv for Minecraft (which queries GL_MAJOR_VERSION, GL_MAX_*,
    // GL_VIEWPORT, ... on every frame). glGetString has no such blanket bypass:
    // it matches `case MITHRIL_BACKEND_GETTER + GL_*` explicitly. glGetIntegerv
    // has no backend-getter numeric cases, so no bypass is needed here at all.
    switch (pname) {
        /* ---- FIX (P1): GL_MAX_* 改为查询真实的 VkPhysicalDeviceLimits ----
         *
         * 这些值原先全是硬编码。GL_MAX_TEXTURE_SIZE 写死 16384 在 A9/A10 这
         * 类只支持 8192 的 iOS GPU 上是**谎报**：Sodium 和 Iris 会照着这个
         * 数字去分配阴影贴图和图集，vkCreateImage 直接失败 → 纹理丢失/崩溃。
         *
         * GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS 原本写 80，却和内部
         * kMaxTextureUnits=32 自相矛盾 —— 上层按 80 绑定，后端只有 32 个槽，
         * 多出来的静默丢弃。backend_device_limit 会同时夹到设备上限和内部
         * 数组容量两者的较小值。
         *
         * 第二个参数是后端未初始化时的 fallback，保持与修改前一致的取值，
         * 保证不会比原来更差。
         */
        case GL_MAX_TEXTURE_SIZE:
            *params = backend_device_limit(MITHRIL_LIMIT_MAX_TEXTURE_SIZE, 16384); break;
        case GL_MAX_3D_TEXTURE_SIZE:
            *params = backend_device_limit(MITHRIL_LIMIT_MAX_3D_TEXTURE_SIZE, 2048); break;
        case GL_MAX_CUBE_MAP_TEXTURE_SIZE:
            *params = backend_device_limit(MITHRIL_LIMIT_MAX_CUBE_MAP_TEXTURE_SIZE, 16384); break;
        case GL_MAX_ARRAY_TEXTURE_LAYERS:
            *params = backend_device_limit(MITHRIL_LIMIT_MAX_ARRAY_TEXTURE_LAYERS, 2048); break;
        case GL_MAX_TEXTURE_IMAGE_UNITS:
        case GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS:
            *params = backend_device_limit(MITHRIL_LIMIT_MAX_TEXTURE_IMAGE_UNITS,
                                           mithril::kMaxTextureUnits); break;
        case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS:
            *params = backend_device_limit(MITHRIL_LIMIT_MAX_COMBINED_TEX_UNITS,
                                           mithril::kMaxTextureUnits); break;
        case GL_MAX_VERTEX_ATTRIBS:
            *params = backend_device_limit(MITHRIL_LIMIT_MAX_VERTEX_ATTRIBS,
                                           mithril::kMaxVertexAttribs); break;
        case GL_MAX_VERTEX_UNIFORM_COMPONENTS:*params = 4096; break;
        case GL_MAX_FRAGMENT_UNIFORM_COMPONENTS:*params = 4096; break;
        case GL_MAX_VIEWPORT_DIMS:
            params[0] = backend_device_limit(MITHRIL_LIMIT_MAX_VIEWPORT_WIDTH, 16384);
            params[1] = backend_device_limit(MITHRIL_LIMIT_MAX_VIEWPORT_HEIGHT, 16384);
            break;
        case GL_MAX_RENDERBUFFER_SIZE:
            *params = backend_device_limit(MITHRIL_LIMIT_MAX_RENDERBUFFER_SIZE, 16384); break;
        case GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT:
            *params = (GLint)std::lround(
                backend_device_max_sampler_anisotropy(1.0f));
            break;
        case GL_MAX_ELEMENTS_VERTICES:        *params = 1 << 24; break;
        case GL_MAX_ELEMENTS_INDICES:         *params = 1 << 24; break;
        case GL_SUBPIXEL_BITS:                *params = 4; break;
        case GL_RED_BITS:                     *params = 8; break;
        case GL_GREEN_BITS:                   *params = 8; break;
        case GL_BLUE_BITS:                    *params = 8; break;
        case GL_ALPHA_BITS:                   *params = 8; break;
        case GL_DEPTH_BITS:                   *params = 24; break;
        case GL_STENCIL_BITS:                 *params = 8; break;
        case GL_NUM_EXTENSIONS:
            *params = (GLint)(sizeof(kExtensions)/sizeof(kExtensions[0]));
            break;
        case GL_MAJOR_VERSION:                *params = 4; break;
        case GL_MINOR_VERSION:                *params = 6; break;
        case GL_CONTEXT_FLAGS:
            *params = GL_CONTEXT_FLAG_FORWARD_COMPATIBLE_BIT;
            break;
        case GL_CONTEXT_PROFILE_MASK:         *params = GL_CONTEXT_CORE_PROFILE_BIT; break;
        case GL_DOUBLEBUFFER:                 *params = GL_TRUE; break;
        case GL_STEREO:                       *params = GL_FALSE; break;
        case GL_MAX_DRAW_BUFFERS:
            *params = backend_device_limit(MITHRIL_LIMIT_MAX_COLOR_ATTACHMENTS, 8); break;
        case GL_MAX_COLOR_ATTACHMENTS:
            *params = backend_device_limit(MITHRIL_LIMIT_MAX_COLOR_ATTACHMENTS,
                                           mithril::kMaxColorAttachments); break;
        case GL_MAX_TEXTURE_UNITS:            *params = mithril::kMaxTextureUnits; break;
        /* ---- 4.x capacity limits Sodium / Iris read ---- */
        case GL_MAX_UNIFORM_BUFFER_BINDINGS:
            *params = backend_device_limit(MITHRIL_LIMIT_MAX_UNIFORM_BUFFER_BINDINGS,
                                           mithril::kMaxIndexedBindings); break;
        case GL_MAX_UNIFORM_BLOCK_SIZE:
            *params = backend_device_limit(MITHRIL_LIMIT_MAX_UNIFORM_BLOCK_SIZE, 64 * 1024); break;
        /* UBO 偏移对齐必须报设备真实值。MoltenVK 上常见 16 或 256，
         * 报小了 Sodium 会按更细的粒度打包 UBO → vkCmdBindDescriptorSets
         * 的 dynamic offset 触发 VUID 校验失败（offset 未对齐）。 */
        case GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT:
            *params = backend_device_limit(MITHRIL_LIMIT_UNIFORM_BUFFER_ALIGNMENT, 256); break;
        case GL_MAX_VERTEX_UNIFORM_BLOCKS:    *params = 14; break;
        case GL_MAX_FRAGMENT_UNIFORM_BLOCKS:  *params = 14; break;
        case GL_MAX_GEOMETRY_UNIFORM_BLOCKS:  *params = 14; break;
        case GL_MAX_COMBINED_UNIFORM_BLOCKS:  *params = 40; break;
        case GL_MAX_VERTEX_OUTPUT_COMPONENTS: *params = 64; break;
        case GL_MAX_FRAGMENT_INPUT_COMPONENTS: *params = 64; break;
        case GL_MAX_SERVER_WAIT_TIMEOUT:      *params = 0x0000FFFF; break;
        /* MSAA 上限来自 framebufferColorSampleCounts & framebufferDepthSampleCounts
         * 的交集。写死 4x 在只支持 2x 的低端 iOS GPU 上会让 MC 的抗锯齿选项
         * 建出无法创建的 multisample 附件。 */
        case GL_MAX_SAMPLES:
        case GL_MAX_COLOR_TEXTURE_SAMPLES:
        case GL_MAX_DEPTH_TEXTURE_SAMPLES:
        case GL_MAX_INTEGER_SAMPLES:
            *params = backend_device_limit(MITHRIL_LIMIT_MAX_SAMPLES, 4); break;
        case GL_MAX_ATOMIC_COUNTER_BUFFER_BINDINGS: *params = 8; break;
        case GL_MAX_COMBINED_ATOMIC_COUNTERS: *params = 8; break;
        case GL_MAX_VERTEX_ATOMIC_COUNTERS:   *params = 8; break;
        case GL_MAX_FRAGMENT_ATOMIC_COUNTERS: *params = 8; break;
        /* ---- Shader storage / compute (Iris) ---- */
        case GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS:
            *params = backend_device_limit(MITHRIL_LIMIT_MAX_SSBO_BINDINGS,
                                           mithril::kMaxIndexedBindings); break;
        case GL_MAX_COMBINED_SHADER_STORAGE_BLOCKS: *params = 96; break;
        case GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS: *params = 16; break;
        case GL_MAX_FRAGMENT_SHADER_STORAGE_BLOCKS: *params = 16; break;
        case GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS: *params = 16; break;
        case GL_MAX_SHADER_STORAGE_BLOCK_SIZE:
            *params = backend_device_limit(MITHRIL_LIMIT_MAX_SSBO_SIZE, 128 * 1024 * 1024); break;
        case GL_MAX_COMBINED_IMAGE_UNIFORMS:  *params = 192; break;
        case GL_MAX_IMAGE_UNITS:              *params = 32; break;
        case GL_MAX_VERTEX_IMAGE_UNIFORMS:    *params = 32; break;
        case GL_MAX_FRAGMENT_IMAGE_UNIFORMS:  *params = 32; break;
        case GL_MAX_COMPUTE_IMAGE_UNIFORMS:   *params = 32; break;
        case GL_MAX_COMBINED_IMAGE_UNITS_AND_FRAGMENT_OUTPUTS: *params = 192; break;
        case GL_MAX_IMAGE_SAMPLES:            *params = 4; break;
        /* Compute 上限直接决定 Iris 的 compute shader 能否 dispatch。
         * Metal 的 threadgroup 上限比桌面小得多（常见 512 而非 1024），
         * 报高了 vkCmdDispatch 会静默失败或触发 GPU hang。 */
        case GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS:
            *params = backend_device_limit(MITHRIL_LIMIT_MAX_COMPUTE_WG_INVOCATIONS, 1024); break;
        case GL_MAX_COMPUTE_WORK_GROUP_COUNT: {
            int c = backend_device_limit(MITHRIL_LIMIT_MAX_COMPUTE_WG_COUNT_X, 65535);
            params[0] = c; params[1] = c; params[2] = c;
            break;
        }
        case GL_MAX_COMPUTE_WORK_GROUP_SIZE: {
            int s = backend_device_limit(MITHRIL_LIMIT_MAX_COMPUTE_WG_SIZE_X, 256);
            params[0] = s;
            params[1] = s;
            params[2] = s < 64 ? s : 64;
            break;
        }
        case GL_MAX_UNIFORM_LOCATIONS:        *params = 1024; break;
        case GL_MAX_VERTEX_ATTRIB_STRIDE:     *params = 2048; break;
        case GL_MAX_VERTEX_ATTRIB_RELATIVE_OFFSET: *params = 0x7FFFFFFF; break;
        case GL_MAX_VERTEX_ATTRIB_BINDINGS:   *params = 16; break;
        case GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS: *params = 4; break;
        case GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS: *params = 64; break;
        case GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_COMPONENTS: *params = 16; break;
        case GL_MAX_CULL_DISTANCES:           *params = 8; break;
        case GL_MAX_COMBINED_CLIP_AND_CULL_DISTANCES: *params = 8; break;
        case GL_FRAGMENT_INTERPOLATION_OFFSET_BITS: *params = 4; break;
        case GL_ACTIVE_TEXTURE:               *params = (GLint)(GL_TEXTURE0 + g_state->activeTextureUnit); break;
        case GL_ARRAY_BUFFER_BINDING:         *params = (GLint)g_state->bufferBindings[(int)mithril::BufferTarget::Array].name; break;
        case GL_ELEMENT_ARRAY_BUFFER_BINDING: {
            mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO);
            *params = vao ? (GLint)vao->elementArrayBuffer : 0;
            break;
        }
        case GL_UNIFORM_BUFFER_BINDING:       *params = (GLint)g_state->bufferBindings[(int)mithril::BufferTarget::Uniform].name; break;
        case GL_VERTEX_ARRAY_BINDING:         *params = (GLint)g_state->currentVAO; break;
        case GL_CURRENT_PROGRAM:              *params = (GLint)g_state->currentProgram; break;
        // GL_DRAW_FRAMEBUFFER_BINDING and GL_FRAMEBUFFER_BINDING share the same
        // numeric value (0x8CA6) per the GL spec, so a single case covers both.
        case GL_FRAMEBUFFER_BINDING:          *params = (GLint)g_state->currentDrawFBO; break;
        case GL_READ_FRAMEBUFFER_BINDING:     *params = (GLint)g_state->currentReadFBO; break;
        case GL_VIEWPORT:
            params[0] = g_state->viewportX; params[1] = g_state->viewportY;
            params[2] = g_state->viewportW; params[3] = g_state->viewportH;
            break;
        case GL_SCISSOR_BOX:
            params[0] = g_state->scissorX; params[1] = g_state->scissorY;
            params[2] = g_state->scissorW; params[3] = g_state->scissorH;
            break;
        case GL_COLOR_CLEAR_VALUE:
            for (int i = 0; i < 4; ++i) params[i] = (GLint)g_state->clearColor[i];
            break;
        case GL_DEPTH_FUNC:                   *params = (GLint)g_state->depthFunc; break;
        case GL_CULL_FACE_MODE:               *params = (GLint)g_state->cullMode; break;
        case GL_FRONT_FACE:                   *params = (GLint)g_state->frontFace; break;
        case GL_POLYGON_MODE:                 params[0] = (GLint)g_state->polygonModeFront;
                                                 params[1] = (GLint)g_state->polygonModeBack; break;
        case GL_LINE_WIDTH:                   *params = (GLint)g_state->lineWidth; break;
        case GL_POINT_SIZE:                   *params = (GLint)g_state->pointSize; break;
        // Pixel transfer state is observable GL state.  Minecraft and our E2E
        // control both need to save/restore it exactly; returning zero for an
        // unsupported getter silently corrupts subsequent uploads/readbacks.
        case GL_PIXEL_PACK_BUFFER_BINDING:
            *params = (GLint)g_state->bufferBindings[(int)mithril::BufferTarget::PixelPack].name; break;
        case GL_PIXEL_UNPACK_BUFFER_BINDING:
            *params = (GLint)g_state->bufferBindings[(int)mithril::BufferTarget::PixelUnpack].name; break;
        case GL_UNPACK_ALIGNMENT:             *params = g_state->pixelStore.unpackAlignment; break;
        case GL_UNPACK_ROW_LENGTH:            *params = g_state->pixelStore.unpackRowLength; break;
        case GL_UNPACK_IMAGE_HEIGHT:          *params = g_state->pixelStore.unpackImageHeight; break;
        case GL_UNPACK_SKIP_ROWS:             *params = g_state->pixelStore.unpackSkipRows; break;
        case GL_UNPACK_SKIP_PIXELS:           *params = g_state->pixelStore.unpackSkipPixels; break;
        case GL_UNPACK_SKIP_IMAGES:           *params = g_state->pixelStore.unpackSkipImages; break;
        case GL_UNPACK_SWAP_BYTES:            *params = g_state->pixelStore.unpackSwapBytes ? GL_TRUE : GL_FALSE; break;
        case GL_UNPACK_LSB_FIRST:             *params = g_state->pixelStore.unpackLSBFirst ? GL_TRUE : GL_FALSE; break;
        case GL_PACK_ALIGNMENT:               *params = g_state->pixelStore.packAlignment; break;
        case GL_PACK_ROW_LENGTH:              *params = g_state->pixelStore.packRowLength; break;
        case GL_PACK_IMAGE_HEIGHT:            *params = g_state->pixelStore.packImageHeight; break;
        case GL_PACK_SKIP_ROWS:               *params = g_state->pixelStore.packSkipRows; break;
        case GL_PACK_SKIP_PIXELS:             *params = g_state->pixelStore.packSkipPixels; break;
        case GL_PACK_SKIP_IMAGES:             *params = g_state->pixelStore.packSkipImages; break;
        case GL_PACK_SWAP_BYTES:              *params = g_state->pixelStore.packSwapBytes ? GL_TRUE : GL_FALSE; break;
        case GL_PACK_LSB_FIRST:               *params = g_state->pixelStore.packLSBFirst ? GL_TRUE : GL_FALSE; break;
        case GL_TEXTURE_BINDING_2D:           *params = (GLint)g_state->textureBindings[g_state->activeTextureUnit][(int)mithril::TextureTarget::_2D].name; break;
        case GL_BLEND_SRC_RGB:                *params = (GLint)g_state->blends[0].srcRGB; break;
        case GL_BLEND_DST_RGB:                *params = (GLint)g_state->blends[0].dstRGB; break;
        case GL_BLEND_SRC_ALPHA:              *params = (GLint)g_state->blends[0].srcA; break;
        case GL_BLEND_DST_ALPHA:              *params = (GLint)g_state->blends[0].dstA; break;
        case GL_BLEND_EQUATION_RGB:           *params = (GLint)g_state->blends[0].eqRGB; break;
        case GL_BLEND_EQUATION_ALPHA:         *params = (GLint)g_state->blends[0].eqA; break;
        case GL_STENCIL_WRITEMASK:            *params = (GLint)g_state->stencilMask; break;
        case GL_STENCIL_BACK_WRITEMASK:       *params = (GLint)g_state->stencilBackMask; break;
        case GL_STENCIL_FUNC:                 *params = (GLint)g_state->stencilFunc; break;
        case GL_STENCIL_REF:                  *params = g_state->stencilRef; break;
        case GL_STENCIL_VALUE_MASK:           *params = (GLint)g_state->stencilValueMask; break;
        case GL_STENCIL_FAIL:                 *params = (GLint)g_state->stencilSfail; break;
        case GL_STENCIL_PASS_DEPTH_FAIL:     *params = (GLint)g_state->stencilDpfail; break;
        case GL_STENCIL_PASS_DEPTH_PASS:     *params = (GLint)g_state->stencilDppass; break;
        case GL_SHADING_LANGUAGE_VERSION:     *params = 460; break;
        /* GL 4.5 ARB_clip_control: queryable clip volume state.
         * Required for completeness since we advertise GL_ARB_clip_control and
         * implement glClipControl. MC/Sodium may query these to decide whether
         * to apply its own Y-flip / depth remap. */
        case 0x935C: /*GL_CLIP_ORIGIN*/       *params = (GLint)g_state->clipOrigin; break;
        case 0x935D: /*GL_CLIP_DEPTH_MODE*/   *params = (GLint)g_state->clipDepthMode; break;
        default:
            // GL spec: an unrecognised pname must raise GL_INVALID_ENUM rather
            // than silently returning 0 (Minecraft relies on the error flag to
            // detect unsupported queries). Keep *params deterministic too.
            *params = 0;
            mithril::state_set_error(GL_INVALID_ENUM);
            break;
    }
}

void glGetFloatv(GLenum pname, GLfloat* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    GLint ip[4] = {0,0,0,0};
    glGetIntegerv(pname, ip);
    switch (pname) {
        case GL_COLOR_CLEAR_VALUE:
            for (int i = 0; i < 4; ++i) params[i] = g_state->clearColor[i];
            return;
        case GL_LINE_WIDTH:        *params = g_state->lineWidth; return;
        case GL_POINT_SIZE:        *params = g_state->pointSize; return;
        case GL_POLYGON_OFFSET_FACTOR: *params = g_state->polygonOffsetFactor; return;
        case GL_POLYGON_OFFSET_UNITS:  *params = g_state->polygonOffsetUnits;  return;
        case GL_BLEND_COLOR:
            for (int i = 0; i < 4; ++i) params[i] = g_state->blendColor[i];
            return;
        case GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT:
            *params = backend_device_max_sampler_anisotropy(1.0f);
            return;
        default:
            for (int i = 0; i < 4; ++i) params[i] = (GLfloat)ip[i];
            return;
    }
}

void glGetDoublev(GLenum pname, GLdouble* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    GLfloat f[4] = {0,0,0,0};
    glGetFloatv(pname, f);
    for (int i = 0; i < 4; ++i) params[i] = (GLdouble)f[i];
}

/* GL 时间源的"当前"值（Queries.cpp）：真实 GPU 时间戳，无计数器设备回退
 * CPU 单调时钟，与 glQueryCounter 同源。 */
extern "C" uint64_t backend_query_timestamp_now_ns(void);

void glGetInteger64v(GLenum pname, GLint64* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
#ifndef GL_TIMESTAMP
#define GL_TIMESTAMP 0x8E28
#endif
    if (pname == GL_TIMESTAMP) {
        params[0] = (GLint64)backend_query_timestamp_now_ns();
        return;
    }
    GLint ip[4] = {0,0,0,0};
    glGetIntegerv(pname, ip);
    for (int i = 0; i < 4; ++i) params[i] = (GLint64)ip[i];
}

void glGetIntegeri_v(GLenum pname, GLuint index, GLint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    *params = 0;
    if (index >= mithril::kMaxIndexedBindings) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    // Indexed buffer binding queries (UBO / TF / AtomicCounter / SSBO).
    // Determine which indexed category the pname belongs to.
    mithril::IndexedBufferTarget cat = mithril::IndexedBufferTarget::Count;
    bool isStart = false, isSize = false, isBinding = false;

    switch (pname) {
        case GL_UNIFORM_BUFFER_BINDING:        cat = mithril::IndexedBufferTarget::Uniform; isBinding = true; break;
        case 0x8F29 /*GL_UNIFORM_BUFFER_START*/:cat = mithril::IndexedBufferTarget::Uniform; isStart = true; break;
        case 0x8F2A /*GL_UNIFORM_BUFFER_SIZE*/: cat = mithril::IndexedBufferTarget::Uniform; isSize = true; break;
        case 0x8C7A /*GL_TRANSFORM_FEEDBACK_BUFFER_BINDING*/: cat = mithril::IndexedBufferTarget::TransformFeedback; isBinding = true; break;
        case 0x8C84 /*GL_TRANSFORM_FEEDBACK_BUFFER_START*/:   cat = mithril::IndexedBufferTarget::TransformFeedback; isStart = true; break;
        case 0x8C85 /*GL_TRANSFORM_FEEDBACK_BUFFER_SIZE*/:    cat = mithril::IndexedBufferTarget::TransformFeedback; isSize = true; break;
        case 0x92C1 /*GL_ATOMIC_COUNTER_BUFFER_BINDING*/:     cat = mithril::IndexedBufferTarget::AtomicCounter; isBinding = true; break;
        case 0x92C2 /*GL_ATOMIC_COUNTER_BUFFER_START*/:       cat = mithril::IndexedBufferTarget::AtomicCounter; isStart = true; break;
        case 0x92C3 /*GL_ATOMIC_COUNTER_BUFFER_SIZE*/:        cat = mithril::IndexedBufferTarget::AtomicCounter; isSize = true; break;
        case 0x90D3 /*GL_SHADER_STORAGE_BUFFER_BINDING*/:     cat = mithril::IndexedBufferTarget::ShaderStorage; isBinding = true; break;
        case 0x90D4 /*GL_SHADER_STORAGE_BUFFER_START*/:       cat = mithril::IndexedBufferTarget::ShaderStorage; isStart = true; break;
        case 0x90D5 /*GL_SHADER_STORAGE_BUFFER_SIZE*/:        cat = mithril::IndexedBufferTarget::ShaderStorage; isSize = true; break;
        default:
            // For non-indexed pnames (e.g. GL_COLOR_WRITEMASK with index), fall back.
            return;
    }
    if (cat == mithril::IndexedBufferTarget::Count) return;
    const mithril::IndexedBindingSlot& slot =
        g_state->indexedBufferBindings[(int)cat][index];
    if (isBinding) *params = (GLint)slot.name;
    else if (isStart) *params = (GLint)slot.offset;
    else if (isSize)  *params = (GLint)slot.size;
}

const GLubyte* glGetString(GLenum name) {
    MITHRIL_ENSURE_INIT();
    switch (name) {
        case GL_VENDOR:                   return (const GLubyte*)kVendor;
        case GL_RENDERER:
#if defined(__APPLE__)
            return (const GLubyte*)mithril_get_gpu_renderer_string();
#else
            return (const GLubyte*)kRenderer;
#endif
        case GL_VERSION:                  return (const GLubyte*)gl_version_string();
        case GL_SHADING_LANGUAGE_VERSION: return (const GLubyte*)kShadingLangVer;
        case GL_EXTENSIONS: {
            // Concatenate into a single space-separated string.
            static std::string all;
            if (all.empty()) {
                for (size_t i = 0; i < sizeof(kExtensions)/sizeof(kExtensions[0]); ++i) {
                    if (i) all += " ";
                    all += kExtensions[i];
                }
            }
            return (const GLubyte*)all.c_str();
        }
        /*
         * Mithril custom enums for F3 debug info mapping (mirrors MobileGlues'
         * GL_SETTINGS_MG / GL_BACKEND_GETTER_MG pattern). Minecraft mods can
         * probe these via glGetString to display Mithril's config on the F3
         * screen, or to bypass the OpenGL facade and get the real Vulkan info.
         */
        case MITHRIL_SETTINGS:
#if defined(__APPLE__)
            return (const GLubyte*)mithril_get_settings_dump();
#else
            return (const GLubyte*)"Mithril-Wrapper (non-Vulkan build)";
#endif
        case MITHRIL_BACKEND_GETTER + GL_RENDERER:
#if defined(__APPLE__)
            return (const GLubyte*)mithril_get_vulkan_device_name();
#else
            return (const GLubyte*)"Mithril-Wrapper (no device)";
#endif
        case MITHRIL_BACKEND_GETTER + GL_VERSION:
            return (const GLubyte*)backend_api_string();
        case MITHRIL_BACKEND_GETTER + GL_VENDOR:
            return (const GLubyte*)(backend_active_kind() == MITHRIL_BACKEND_KIND_METAL
                                        ? "Apple Metal"
                                        : "Khronos MoltenVK");
        case MITHRIL_BACKEND_GETTER + GL_SHADING_LANGUAGE_VERSION:
            return (const GLubyte*)"SPIR-V 1.5 (glslang -> SPIR-V)";
        default: return nullptr;
    }
}

const GLubyte* glGetStringi(GLenum name, GLuint index) {
    MITHRIL_ENSURE_INIT();
    if (name != GL_EXTENSIONS) return nullptr;
    if (index >= sizeof(kExtensions)/sizeof(kExtensions[0])) return nullptr;
    return (const GLubyte*)kExtensions[index];
}

/* ---- Indexed state queries, remaining widths (root cause AR) ----
 * Every indexed pname in GL 3.3 Core is integer- or boolean-valued, so these
 * reuse glGetIntegeri_v's table instead of duplicating its pname switch. */
void glGetBooleani_v(GLenum target, GLuint index, GLboolean* data) {
    MITHRIL_ENSURE_INIT();
    if (!data) return;
    GLint v = 0;
    glGetIntegeri_v(target, index, &v);
    *data = v ? GL_TRUE : GL_FALSE;
}

void glGetInteger64i_v(GLenum target, GLuint index, GLint64* data) {
    MITHRIL_ENSURE_INIT();
    if (!data) return;
    GLint v = 0;
    glGetIntegeri_v(target, index, &v);
    *data = (GLint64)v;
}

} // extern "C"
