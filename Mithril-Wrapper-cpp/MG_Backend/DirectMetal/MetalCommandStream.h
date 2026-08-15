// Mithril-Wrapper - MG_Backend/DirectMetal/MetalCommandStream.h
// MTLRenderCommandEncoder lifecycle + shadow dynamic state + draw recording +
// frame commit/present. Mirrors DirectVulkan/CommandStream.h's role.
#ifndef MITHRIL_DIRECTMETAL_COMMANDSTREAM_H
#define MITHRIL_DIRECTMETAL_COMMANDSTREAM_H

#ifdef __APPLE__

#include "MetalDevice.h"

namespace mithril {

struct Uniform; // State.h (avoid circular include)

namespace dmt {

constexpr int kMaxVertexAttribSlots = 16;

// Shadow of every dynamic state that must survive encoder re-creation (a new
// MTLRenderCommandEncoder starts with default state, so begin_render_pass
// replays this onto it).
struct EncoderState {
    bool passActive = false;

    // Viewport (Vulkan convention: top-left origin, y down; applied as a
    // negative-height MTLViewport so NDC +y maps DOWN like Vulkan).
    int vpX = 0, vpY = 0, vpW = 0, vpH = 0;
    bool vpValid = false;
    int scX = 0, scY = 0, scW = 0, scH = 0;
    bool scValid = false;

    MTLPrimitiveType pendingPrimitive = MTLPrimitiveTypeTriangle; // not state, draw arg

    bool depthTest = false;
    bool depthWrite = true;
    MTLCompareFunction depthFunc = MTLCompareFunctionLess;

    bool stencilTest = false;
    MTLCompareFunction stencilFunc = MTLCompareFunctionAlways;
    uint32_t stencilRef = 0, stencilMask = 0xFFFFFFFFu;
    MTLStencilOperation stencilSfail = MTLStencilOperationKeep;
    MTLStencilOperation stencilDpfail = MTLStencilOperationKeep;
    MTLStencilOperation stencilDppass = MTLStencilOperationKeep;

    float blendColor[4] = {0, 0, 0, 0};
    bool depthBiasOn = false;
    float depthBiasSlope = 0, depthBiasClamp = 0;

    // Pending clear values for the next pass's load action.
    float clearColor[4] = {0, 0, 0, 0};
    double clearDepth = 1.0;
    uint32_t clearStencil = 0;
    bool loadClear = false;           // true = next pass loads with CLEAR
    uint32_t invalidateColorMask = 0; // bit i = discard color attachment i
    bool invalidateDepth = false;
    bool invalidateStencil = false;

    // Current pass attachments (for clear_attachments / pipeline validity).
    MetalTexture* colorViews[8] = {};
    int colorCount = 0;
    MetalTexture* depthView = nullptr;
    int passW = 0, passH = 0;

    MetalPipeline* boundPipeline = nullptr;
    bool descriptorsBound = false;    // draw guard (mirrors Vulkan path)
    bool hasCommands = false;         // anything encoded this frame?

    // Occlusion queries require the result buffer on the render-pass
    // descriptor before the encoder is created. The mode itself is dynamic.
    id<MTLBuffer> visibilityBuffer = nil;
    bool visibilityCounting = false;

    // Bound vertex buffers for encoder replay after clear-quad draws.
    id<MTLBuffer> vertBuf[kMaxVertexAttribSlots] = {};
    NSUInteger vertOff[kMaxVertexAttribSlots] = {};
};
EncoderState& enc();

struct MetalSwapchain;
MetalSwapchain* active_swapchain();
void set_active_swapchain(MetalSwapchain* sc);

bool render_pass_active();
void set_descriptors_bound(bool bound);
bool descriptors_bound();
void note_non_render_commands(); // blit/compute ops recorded → hasCommands

// Render pass lifecycle.
void begin_render_pass(MetalTexture** color_views, int color_count,
                       MetalTexture* depth_view, int width, int height,
                       int samples);
void end_render_pass();

// The live render encoder of the current pass (nil when no pass is active).
// bind_program_descriptors / clear quads draw through it.
id<MTLRenderCommandEncoder> current_encoder();
void set_visibility_query(id<MTLBuffer> buffer, bool counting);
void clear_visibility_query(id<MTLBuffer> buffer);

/* ---- Dynamic-state setters (back the dmt_set_* entry points) ----
 * Each stores into EncoderState AND applies to the live encoder when a pass
 * is active, so state survives encoder re-creation (begin_render_pass
 * replays the shadow onto every new encoder). */
void set_viewport(int x, int y, int w, int h, double znear, double zfar);
void set_scissor(int x, int y, int w, int h);
void set_cull_mode(int mode);        /* 0=None,1=Front,2=Back */
void set_front_face(int ccw);        /* 1=CCW, 0=CW (already Vulkan-convention) */
void set_depth_test(int enabled, int write_mask, int compare_func);
void set_stencil_state(int enabled, int func, int ref, int mask,
                       int sfail, int dpfail, int dppass);
void set_blend_color(float r, float g, float b, float a);
void set_depth_bias(float slope, float clamp);
void bind_pipeline(MetalPipeline* pipe);
void set_vertex_buffer(int slot, MetalBuffer* buf, NSUInteger offset);

// Clear family (inside a pass). Implemented with dedicated clear pipelines.
void clear_attachments(GLbitfield mask, int x, int y, int w, int h);
void clear_buffer_indexed(GLenum buffer, GLint drawbuffer, const float color[4],
                          float depth, GLuint stencil);

// Frame lifecycle.
bool ensure_command_buffer();
bool commit_frame(MetalSwapchain* present);
void reset_encoder_state();

// Draw recording (GL primitive enums; fan/loop expanded internally).
void draw_arrays(GLenum primitive, int first, int count);
void draw_arrays_instanced(GLenum primitive, int first, int count, int primcount);
void draw_indexed(GLenum primitive, int count, int index_type,
                  MetalBuffer* index_buffer, NSUInteger index_offset);
void draw_indexed_instanced(GLenum primitive, int count, int index_type,
                            MetalBuffer* index_buffer, NSUInteger index_offset,
                            int primcount);
void draw_indirect(GLenum primitive, MetalBuffer* indirect, NSUInteger offset,
                   int count, int stride);
void draw_indexed_indirect(GLenum primitive, int index_type,
                           MetalBuffer* index_buffer, NSUInteger index_offset,
                           MetalBuffer* indirect, NSUInteger offset,
                           int count, int stride);
void draw_indirect_count(GLenum primitive, MetalBuffer* indirect, NSUInteger offset,
                         MetalBuffer* count_buffer, NSUInteger count_offset,
                         int max_drawcount, int stride);
void draw_indexed_indirect_count(GLenum primitive, int index_type,
                                 MetalBuffer* index_buffer, NSUInteger index_offset,
                                 MetalBuffer* indirect, NSUInteger offset,
                                 MetalBuffer* count_buffer, NSUInteger count_offset,
                                 int max_drawcount, int stride);

// Compute. The full dispatch (pipeline + descriptor binding + threadgroups)
// lives in MetalPipeline.mm; this only provides the encoder, ending any
// active render pass first (Metal forbids compute inside a render pass —
// same rule as Vulkan).
id<MTLComputeCommandEncoder> ensure_compute_encoder();

// One-shot synchronous readback helper shared by queries/pixels.
id<MTLCommandBuffer> new_oneshot_command_buffer();

} // namespace dmt
} // namespace mithril

#endif // __APPLE__
#endif // MITHRIL_DIRECTMETAL_COMMANDSTREAM_H
