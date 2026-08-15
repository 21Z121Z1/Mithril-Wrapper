// Mithril-Wrapper - MG_Backend/BackendTypes.h
// Backend-neutral value types shared by the GL frontend (MG_Impl) and every
// backend implementation (DirectVulkan, DirectMetal).
//
// WHY THIS FILE EXISTS: the dual-backend dispatcher (Dispatch.cpp) includes
// Backend.h MULTIPLE times — once per backend — with the `backend_` prefix
// macro-redefined (backend_ -> dvk_ / dmt_) to generate each backend's
// entry-point declarations. Repeatedly re-including a header re-defines every
// struct/enum/#define that is not guarded by its own include guard, so all
// TYPE definitions live here with a stable guard (MITHRIL_BACKEND_TYPES_H)
// while Backend.h keeps only FUNCTION declarations.
#ifndef MITHRIL_BACKEND_TYPES_H
#define MITHRIL_BACKEND_TYPES_H

#include <cstdint>
#include <cstddef>

#include <vulkan/vulkan.h>
#include <GL/gl.h>

/*
 * Which backend the dispatcher routed to. Fixed at backend_init() time from
 * the MITHRIL_BACKEND environment variable (metal|vulkan; platform default
 * when unset: Metal on Apple, Vulkan elsewhere).
 */
typedef enum MGBackendKind {
    MITHRIL_BACKEND_KIND_NONE    = 0,
    MITHRIL_BACKEND_KIND_VULKAN  = 1,   /* DirectVulkan (Vulkan 1.2 / MoltenVK on Apple) */
    MITHRIL_BACKEND_KIND_METAL   = 2    /* DirectMetal  (Metal 3, direct CAMetalLayer)   */
} MGBackendKind;

/*
 * Unpack (pixel-store) parameters handed to backend_texture_upload. Mirrors the
 * glPixelStorei UNPACK_* state kept in g_state->pixelStore. The struct form lets
 * the MG_Impl layer build the params once and pass a stable pointer instead of
 * a single alignment integer, so the backend can honour
 * UNPACK_ROW_LENGTH / UNPACK_SKIP_* for sub-image uploads.
 */
struct MGUnpackParams {
    GLint unpackAlignment;
    GLint unpackRowLength;
    GLint unpackSkipPixels;
    GLint unpackSkipRows;
    GLint unpackImageHeight;
    GLint unpackSkipImages;
};

/*
 * Description of one bound vertex attribute used to build the pipeline's
 * vertex-input state (VkPipelineVertexInputStateCreateInfo on Vulkan /
 * MTLVertexDescriptor on Metal).
 */
struct MGVertexAttrib {
    int     location;     /* GL attribute index */
    int     size;         /* 1..4 */
    GLenum  type;         /* GL_FLOAT, GL_UNSIGNED_BYTE, etc. */
    int     normalized;   /* 0/1 */
    int     integer;      /* 0/1 (integer attribs) */
    int     stride;
    int     offset;       /* byte offset within the bound vertex buffer */
    int     enabled;      /* 0/1 */
    GLuint  buffer_name;  /* GL VBO name backing this attrib */
    int     divisor;      /* instance-step divisor (0 = per-vertex) */
};

/* GL query-object kinds (glGenQueries target backing). */
enum {
    MITHRIL_QUERY_OCCLUSION    = 0,
    MITHRIL_QUERY_TIMESTAMP    = 1,
    MITHRIL_QUERY_TIME_ELAPSED = 2
};

/*
 * backend_device_limit() selectors. Backends must never report MORE than the
 * device can do (over-reporting makes Sodium/Iris allocate oversized textures
 * that fail at creation).
 */
#define MITHRIL_LIMIT_MAX_TEXTURE_SIZE            1
#define MITHRIL_LIMIT_MAX_3D_TEXTURE_SIZE         2
#define MITHRIL_LIMIT_MAX_CUBE_MAP_TEXTURE_SIZE   3
#define MITHRIL_LIMIT_MAX_ARRAY_TEXTURE_LAYERS    4
#define MITHRIL_LIMIT_MAX_RENDERBUFFER_SIZE       5
#define MITHRIL_LIMIT_MAX_VIEWPORT_WIDTH          6
#define MITHRIL_LIMIT_MAX_VIEWPORT_HEIGHT         7
#define MITHRIL_LIMIT_MAX_TEXTURE_IMAGE_UNITS     8   /* per-stage sampled images */
#define MITHRIL_LIMIT_MAX_COMBINED_TEX_UNITS      9
#define MITHRIL_LIMIT_MAX_UNIFORM_BLOCK_SIZE      10
#define MITHRIL_LIMIT_UNIFORM_BUFFER_ALIGNMENT    11
#define MITHRIL_LIMIT_MAX_UNIFORM_BUFFER_BINDINGS 12
#define MITHRIL_LIMIT_MAX_COLOR_ATTACHMENTS       13
#define MITHRIL_LIMIT_MAX_SAMPLES                 14
#define MITHRIL_LIMIT_MAX_VERTEX_ATTRIBS          15
#define MITHRIL_LIMIT_MAX_SSBO_BINDINGS           16
#define MITHRIL_LIMIT_MAX_SSBO_SIZE               17
#define MITHRIL_LIMIT_MAX_COMPUTE_WG_INVOCATIONS  18
#define MITHRIL_LIMIT_MAX_COMPUTE_WG_COUNT_X      19
#define MITHRIL_LIMIT_MAX_COMPUTE_WG_SIZE_X       20

#endif // MITHRIL_BACKEND_TYPES_H
