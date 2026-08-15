// Mithril-Wrapper - MG_Backend/DirectMetal/MetalCommandStream.mm
// MTLRenderCommandEncoder lifecycle + shadow dynamic state + draw recording +
// frame commit/present. Mirrors DirectVulkan/CommandStream.cpp's behaviour on
// Metal's encoder model:
//
//   * One command buffer per frame slot (MITHRIL_DMT_MAX_FRAMES_IN_FLIGHT=2),
//     created lazily by ensure_command_buffer() which FIRST waits out the
//     slot's previous command buffer, THEN rewinds the slot's arenas, and only
//     then asks the queue for a fresh buffer. commit_frame() closes the
//     current buffer, presents the swapchain drawable, installs a completion
//     handler for the serial watermark / deviceLost tracking, commits, and
//     advances the slot.
//
//   * A Metal command buffer allows exactly ONE live encoder at a time, and a
//     freshly created MTLRenderCommandEncoder starts with DEFAULT state. Every
//     dynamic state therefore lives in the EncoderState shadow (enc()) and is
//     replayed onto each new encoder by begin_render_pass(). The set_*
//     entry points write the shadow AND apply to the live encoder so state
//     changes mid-pass take effect immediately.
//
//   * Render convention: see MetalDevice.h — the frontend SPIR-V already
//     carries Vulkan conventions; we reproduce Vulkan rasterization with a
//     NEGATIVE-height MTLViewport {x, y+h, w, -h} so NDC +y maps DOWN (the
//     trick MoltenVK uses), depth range [0,1] (Metal native), scissor in
//     top-left origin. Viewport/scissor values arriving here are already in
//     Vulkan convention (top-left origin): the entry layer converts the GL
//     bottom-origin coordinates the same way dvk_set_viewport does
//     (vk_y = fbHeight - gl_y - gl_h).
//
// Clearing has no vkCmdClearAttachments equivalent on Metal — clear_attachments
// / clear_buffer_indexed draw a fullscreen triangle with a dedicated clear
// pipeline (get_clear_pipeline) instead. GL_TRIANGLE_FAN / GL_LINE_LOOP have no
// Metal primitive either and are expanded through a per-slot CPU-writable
// scratch index buffer.
//
// THREADING: like the whole DirectMetal backend, encode-side state is
// single-threaded (the GL render thread); only the completion-handler fields
// in Backend are atomic.
#ifdef __APPLE__

#include "MetalCommandStream.h"
#include "MetalSwapchain.h"
#include "MetalPipeline.h"      // get_clear_pipeline / clear_depth_stencil_state
#include "../../MG_Impl/Log.h"
#include "../../MG_State/State.h" // g_state (scissor test for clears +
                                  // currentBaseVertex/currentBaseInstance for draws)

#include <cstring>
#include <unordered_map>

namespace mithril {
namespace dmt {

/* ---- Enum translations (implemented in MetalFormat.mm, same namespace) ----
 * They have no shared header, so the few this TU needs are re-declared here. */
MTLCompareFunction compare_func_from_gl(GLenum f);
MTLStencilOperation stencil_op_from_gl(GLenum op);
MTLPrimitiveType    primitive_from_gl(GLenum m);
MTLIndexType        index_type_from_int(int t); // 0=U16, 1=U32, 2=U8 (U8 invalid on Metal)
bool                format_has_stencil(VkFormat f);

namespace {

/* ---- File-scope encoder / raster-state storage ----
 *
 * The frozen EncoderState (MetalCommandStream.h) has no fields for the live
 * encoder object, cull mode, front winding, or viewport depth range, all of
 * which MUST survive encoder re-creation exactly like the shadowed fields.
 * They live here as file statics; begin_render_pass replays them together
 * with the EncoderState shadow. */
static id<MTLRenderCommandEncoder>  g_renderEncoder = nil;  // live pass encoder
static id<MTLComputeCommandEncoder> g_computeEncoder = nil; // cached per cmd buffer
static MTLCullMode  g_cullMode = MTLCullModeNone;
static MTLWinding   g_frontWinding = MTLWindingClockwise;   // Metal's own default
static double       g_vpZNear = 0.0, g_vpZFar = 1.0;
static MetalSwapchain* g_activeSwapchain = nullptr;

/* Warn-once-ish throttling (mirrors the Vulkan TU: first 8 hits only). */
void warn_limited(uint32_t& counter, const char* who, const char* what) {
    if (counter < 8) {
        ++counter;
        MITHRIL_LOG_WARN("mtl", "%s: %s", who, what);
    }
}

/* ---- Depth/stencil state cache ----
 *
 * Metal bakes depth+stencil compare/ops into an immutable
 * MTLDepthStencilState, so every set_depth_test / set_stencil_state change
 * re-fetches from this cache (key packs the full state; see the design note).
 * stencilMask is part of the cache key because it maps to the descriptor's
 * readMask — the task's enumerated key omitted it, but a stale compare mask
 * would silently break glStencilFunc semantics. */
std::unordered_map<uint64_t, id<MTLDepthStencilState>>& dss_cache() {
    static std::unordered_map<uint64_t, id<MTLDepthStencilState>> m;
    return m;
}

id<MTLDepthStencilState> get_depth_stencil_state() {
    EncoderState& e = enc();
    Backend* b = backend();
    if (!b->device) return nil;

    const uint64_t key =
        (uint64_t)(e.depthTest ? 1 : 0) |
        ((uint64_t)(e.depthWrite ? 1 : 0) << 1) |
        ((uint64_t)e.depthFunc << 2) |               // 3 bits (0..7)
        ((uint64_t)(e.stencilTest ? 1 : 0) << 5) |
        ((uint64_t)e.stencilFunc << 6) |             // 3 bits
        ((uint64_t)e.stencilSfail << 9) |            // 4 bits (ops 0..8)
        ((uint64_t)e.stencilDpfail << 13) |
        ((uint64_t)e.stencilDppass << 17) |
        ((uint64_t)e.stencilMask << 21);             // 32 bits

    auto it = dss_cache().find(key);
    if (it != dss_cache().end()) return it->second;

    @autoreleasepool {
        MTLDepthStencilDescriptor* d = [[MTLDepthStencilDescriptor alloc] init];
        // GL semantics: depth writes only happen while the depth test is
        // enabled, and a disabled depth test means "always pass, no side
        // effects" — encode that as Always + write off instead of trusting
        // the encoder's default.
        if (e.depthTest) {
            d.depthCompareFunction = e.depthFunc;
            d.depthWriteEnabled = e.depthWrite;
        } else {
            d.depthCompareFunction = MTLCompareFunctionAlways;
            d.depthWriteEnabled = NO;
        }
        if (e.stencilTest) {
            // GL has a single-sided stencil state; program both faces with
            // the same configuration so either winding hits the same rule.
            MTLStencilDescriptor* s = [[MTLStencilDescriptor alloc] init];
            s.stencilCompareFunction = e.stencilFunc;
            s.stencilFailureOperation = e.stencilSfail;
            s.depthFailureOperation = e.stencilDpfail;
            s.depthStencilPassOperation = e.stencilDppass;
            s.readMask = e.stencilMask;
            s.writeMask = 0xFFFFFFFFu;
            d.frontFaceStencil = s;
            d.backFaceStencil = s;
        }
        id<MTLDepthStencilState> st = [b->device newDepthStencilStateWithDescriptor:d];
        if (st == nil) {
            static uint32_t warned = 0;
            warn_limited(warned, "dmt", "newDepthStencilStateWithDescriptor returned nil");
            return nil;
        }
        dss_cache().emplace(key, st);
        return st;
    }
}

/* ---- Per-slot scratch index arena (fan / line-loop / U8 expansion) ----
 *
 * GL_TRIANGLE_FAN, GL_LINE_LOOP and GL_UNSIGNED_BYTE indices have no Metal
 * equivalent, so they are expanded on the CPU into a scratch MTLBuffer and
 * drawn with drawIndexedPrimitives. The GPU reads the buffer CONTENTS at
 * execution time, so two draws of the same frame must never share bytes, and
 * a frame must never overwrite bytes the previous frame still executes:
 * exactly the UBO arena's hazard, solved the same way — per-slot bump
 * allocation, rewound in ensure_command_buffer() only after the slot's
 * previous command buffer has completed. Growth mid-frame is safe: the old
 * id<MTLBuffer> is replaced (ARC drops our ref) but stays alive for the
 * already-encoded draws, since Metal retains resources referenced by
 * uncommitted command buffers. */
struct ScratchSlot {
    id<MTLBuffer> buf = nil;
    NSUInteger capacity = 0;
    NSUInteger used = 0;
};
static ScratchSlot g_scratch[MITHRIL_DMT_MAX_FRAMES_IN_FLIGHT];

constexpr NSUInteger kScratchInitialBytes = 256 * 1024;

// Bump-allocate `bytes` (256-aligned) in the current frame slot's scratch
// buffer, growing it when needed. Returns the buffer + offset; *outPtr is the
// CPU write pointer (null on failure).
bool scratch_alloc(NSUInteger bytes, id<MTLBuffer> __strong& outBuf,
                   NSUInteger& outOff,
                   void*& outPtr) {
    Backend* b = backend();
    if (!b->initialized || !b->device || bytes == 0) return false;
    bytes = (bytes + 255u) & ~255u;

    ScratchSlot& s = g_scratch[b->currentFrame];
    if (s.buf == nil || s.capacity - s.used < bytes) {
        NSUInteger newCap = kScratchInitialBytes;
        if (s.buf != nil) newCap = s.capacity * 2;
        if (newCap < bytes) newCap = bytes;
        id<MTLBuffer> nb = [b->device newBufferWithLength:newCap
                              options:MITHRIL_DMT_STORAGE(!b->unifiedMemory)];
        if (nb == nil) {
            static uint32_t warned = 0;
            warn_limited(warned, "dmt", "scratch index buffer allocation failed");
            return false;
        }
        // Old buffer stays alive for already-encoded draws (see comment above).
        s.buf = nb;
        s.capacity = newCap;
        s.used = 0;
    }
    outBuf = s.buf;
    outOff = s.used;
    outPtr = (uint8_t*)s.buf.contents + s.used;
    s.used += bytes;
    return true;
}

// Managed-storage scratch must be flushed after every CPU write so a discrete
// GPU sees the bytes. No-op on iOS (Shared storage).
void scratch_flush(id<MTLBuffer> buf, NSUInteger offset, NSUInteger len) {
#if TARGET_OS_OSX
    if (!backend()->unifiedMemory && buf != nil) {
        [buf didModifyRange:NSMakeRange(offset, len ? len : 1)];
    }
#endif
}

/* ---- Shadow-state application helpers ----
 *
 * apply_full_state replays EVERY piece of raster state onto a fresh encoder.
 * Setters below apply single pieces; both paths must stay in sync or a state
 * would silently reset on pass re-creation. */

MTLViewport make_viewport(int x, int y, int w, int h, double zn, double zf) {
    // Negative-height viewport reproducing Vulkan's rasterization on Metal
    // (MetalDevice.h convention). y is the TOP-left origin of the viewport in
    // framebuffer space; +h shifts it to the BOTTOM edge, which is where a
    // negative height starts mapping NDC y=-1 — net effect: NDC +y points
    // DOWN exactly like Vulkan, and window-space winding is NOT reversed
    // relative to Vulkan (the winding flip lives in set_front_face instead).
    MTLViewport vp;
    vp.originX = (double)x;
    vp.originY = (double)(y + h);
    vp.width = (double)w;
    vp.height = (double)-h;
    vp.znear = zn;
    vp.zfar = zf;
    return vp;
}

MTLScissorRect make_scissor(int x, int y, int w, int h, int clampW, int clampH) {
    // Metal validates the scissor against the attachment extent and faults on
    // overrun, so clamp (GL happily accepts out-of-range scissors).
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (clampW > 0 && x + w > clampW) w = clampW - x;
    if (clampH > 0 && y + h > clampH) h = clampH - y;
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    MTLScissorRect r;
    r.x = (NSUInteger)x;
    r.y = (NSUInteger)y;
    r.width = (NSUInteger)w;
    r.height = (NSUInteger)h;
    return r;
}

void apply_full_state(id<MTLRenderCommandEncoder> r) {
    EncoderState& e = enc();
    if (e.vpValid && e.vpW > 0 && e.vpH > 0) {
        [r setViewport:make_viewport(e.vpX, e.vpY, e.vpW, e.vpH, g_vpZNear, g_vpZFar)];
    } else if (e.passW > 0 && e.passH > 0) {
        // No viewport recorded yet: cover the whole attachment so a draw
        // before the first set_viewport is not clipped to a degenerate rect.
        [r setViewport:make_viewport(0, 0, e.passW, e.passH, 0.0, 1.0)];
    }
    if (e.scValid && e.scW > 0 && e.scH > 0) {
        [r setScissorRect:make_scissor(e.scX, e.scY, e.scW, e.scH, e.passW, e.passH)];
    } else if (e.passW > 0 && e.passH > 0) {
        [r setScissorRect:make_scissor(0, 0, e.passW, e.passH, e.passW, e.passH)];
    }
    [r setCullMode:g_cullMode];
    [r setFrontFacingWinding:g_frontWinding];
    id<MTLDepthStencilState> dss = get_depth_stencil_state();
    if (dss) [r setDepthStencilState:dss];
    [r setStencilReferenceValue:e.stencilRef];
    [r setBlendColorRed:e.blendColor[0] green:e.blendColor[1]
                    blue:e.blendColor[2] alpha:e.blendColor[3]];
    if (e.depthBiasOn) {
        // GL polygon offset semantics as mapped by the Vulkan reference
        // (vkCmdSetDepthBias(slope, clamp, 0)): the factor goes in as a
        // constant bias, the units as the clamp, slopeScale stays 0.
        [r setDepthBias:e.depthBiasSlope slopeScale:0.0f clamp:e.depthBiasClamp];
    }
    // Pipeline + vertex buffer bindings are re-established defensively: the
    // per-draw entry path re-binds both anyway (mirroring Drawing.cpp), but a
    // missed re-bind after a pass transition would otherwise draw garbage.
    if (e.boundPipeline && e.boundPipeline->rps) {
        [r setRenderPipelineState:e.boundPipeline->rps];
    }
    for (int i = 0; i < kMaxVertexAttribSlots; ++i) {
        if (e.vertBuf[i] != nil) {
            [r setVertexBuffer:e.vertBuf[i] offset:e.vertOff[i] atIndex:(NSUInteger)i];
        }
    }
    if (e.visibilityBuffer != nil) {
        [r setVisibilityResultMode:(e.visibilityCounting
                                        ? MTLVisibilityResultModeCounting
                                        : MTLVisibilityResultModeDisabled)
                            offset:0];
    }
}

/* ---- Draw guard (mirrors vk::draw_recording_allowed) ----
 *
 * A draw needs an active encoder AND a bound pipeline. Without either, Metal
 * validation aborts the process — unlike MoltenVK there is no dropped-draw
 * failure mode, so the guard is load-bearing. descriptorsBound is NOT checked
 * here: the entry layer (MetalBackend.mm) owns the bind-program-descriptors
 * ordering before calling into these. */
bool draw_allowed(const char* who) {
    Backend* b = backend();
    if (!b->cmd) return false; // device lost / nothing recording
    EncoderState& e = enc();
    if (!e.passActive || g_renderEncoder == nil) {
        static uint32_t warned = 0;
        warn_limited(warned, who, "no active render pass — draw dropped");
        return false;
    }
    if (!e.boundPipeline || e.boundPipeline->rps == nil) {
        static uint32_t warned = 0;
        warn_limited(warned, who, "no graphics pipeline bound — draw dropped");
        return false;
    }
    e.hasCommands = true;
    return true;
}

/* Base-instance passthrough: glDrawArraysInstancedBaseInstance and friends set
 * g_state->currentBaseInstance before falling through to the plain entry
 * points (root cause AG in the Vulkan TU — same GL frontend drives us). */
uint32_t current_base_instance() {
    return (mithril::g_state && mithril::g_state->currentBaseInstance > 0)
               ? mithril::g_state->currentBaseInstance : 0u;
}
NSInteger current_base_vertex() {
    return mithril::g_state ? (NSInteger)mithril::g_state->currentBaseVertex : 0;
}

/* ---- Primitive expansion (fan / line-loop / U8 indices) ---- */

// Byte size of one source index for the 0=U16 / 1=U32 / 2=U8 encoding.
NSUInteger index_size_of(int index_type) {
    return index_type == 1 ? 4u : (index_type == 2 ? 1u : 2u);
}

// Expand GL_TRIANGLE_FAN vertex ids [first, first+count) into triangle list
// indices [0,1,2, 0,2,3, 0,3,4, ...] (relative ids: baseVertex is passed to
// the draw call itself). Writes (count-2)*3 entries of the chosen width.
bool expand_fan_ids(uint32_t first, uint32_t count, bool use16, void* out) {
    if (count < 3) return true;
    const uint32_t tris = count - 2;
    for (uint32_t t = 0; t < tris; ++t) {
        const uint32_t v0 = first;
        const uint32_t v1 = first + t + 1;
        const uint32_t v2 = first + t + 2;
        if (use16) {
            uint16_t* o = (uint16_t*)out + (size_t)t * 3;
            o[0] = (uint16_t)v0; o[1] = (uint16_t)v1; o[2] = (uint16_t)v2;
        } else {
            uint32_t* o = (uint32_t*)out + (size_t)t * 3;
            o[0] = v0; o[1] = v1; o[2] = v2;
        }
    }
    return true;
}

// Core non-indexed draw. Everything fan/loop/instanced funnels here so the
// expansion policy lives in exactly one place.
void issue_arrays_draw(GLenum primitive, uint32_t first, uint32_t count,
                       uint32_t instanceCount, uint32_t baseInstance) {
    id<MTLRenderCommandEncoder> r = g_renderEncoder;
    if (count == 0) return;

    if (primitive == GL_TRIANGLE_FAN) {
        if (count < 3) return;
        // Vertex VALUES are ids in [first, first+count): u16 is enough while
        // the whole range fits 16 bits (design rule: vertex count < 65536).
        const bool use16 = (first + count) <= 65536u;
        const NSUInteger bytes = (NSUInteger)(count - 2) * 3 * (use16 ? 2 : 4);
        id<MTLBuffer> sb; NSUInteger off; void* ptr;
        if (!scratch_alloc(bytes, sb, off, ptr)) return;
        expand_fan_ids(first, count, use16, ptr);
        scratch_flush(sb, off, bytes);
        [r drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                       indexCount:(NSUInteger)(count - 2) * 3
                        indexType:(use16 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32)
                      indexBuffer:sb
                indexBufferOffset:off
                   instanceCount:instanceCount
                      baseVertex:0
                    baseInstance:baseInstance];
        return;
    }
    if (primitive == GL_LINE_LOOP) {
        if (count < 2) return;
        // Line strip [0, 1, ..., n-1, 0] reproduces the GL loop exactly.
        const bool use16 = count <= 65535u;
        const NSUInteger nIdx = (NSUInteger)count + 1;
        const NSUInteger bytes = nIdx * (use16 ? 2 : 4);
        id<MTLBuffer> sb; NSUInteger off; void* ptr;
        if (!scratch_alloc(bytes, sb, off, ptr)) return;
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t v = first + i;
            if (use16) ((uint16_t*)ptr)[i] = (uint16_t)v;
            else       ((uint32_t*)ptr)[i] = v;
        }
        if (use16) ((uint16_t*)ptr)[count] = (uint16_t)first;
        else       ((uint32_t*)ptr)[count] = first;
        scratch_flush(sb, off, bytes);
        [r drawIndexedPrimitives:MTLPrimitiveTypeLineStrip
                       indexCount:nIdx
                        indexType:(use16 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32)
                      indexBuffer:sb
                indexBufferOffset:off
                   instanceCount:instanceCount
                      baseVertex:0
                    baseInstance:baseInstance];
        return;
    }
    [r drawPrimitives:primitive_from_gl(primitive)
          vertexStart:(NSUInteger)first
          vertexCount:(NSUInteger)count
        instanceCount:instanceCount
         baseInstance:baseInstance];
}

// Core indexed draw. Handles the plain path plus the three expansions:
//   * U8 source (index_type==2): Metal has no UInt8 index type — widen to U16.
//   * fan: read `count` source indices, emit triangle-list fan indices.
//   * line loop: read source, append the closing index, draw a line strip.
// baseVertex/baseInstance pass straight through: the expanded buffer holds the
// ORIGINAL index values and Metal applies baseVertex at draw time.
void issue_indexed_draw(GLenum primitive, uint32_t count, int index_type,
                        MetalBuffer* index_buffer, NSUInteger index_offset,
                        uint32_t instanceCount, NSInteger baseVertex,
                        uint32_t baseInstance) {
    id<MTLRenderCommandEncoder> r = g_renderEncoder;
    if (count == 0 || !index_buffer || index_buffer->buf == nil) return;
    const bool fan = (primitive == GL_TRIANGLE_FAN);
    const bool loop = (primitive == GL_LINE_LOOP);

    if (index_type != 2 && !fan && !loop) {
        // Fast path — draw straight from the app's index buffer.
        [r drawIndexedPrimitives:primitive_from_gl(primitive)
                       indexCount:(NSUInteger)count
                        indexType:index_type_from_int(index_type)
                      indexBuffer:index_buffer->buf
                indexBufferOffset:index_offset
                   instanceCount:instanceCount
                      baseVertex:baseVertex
                    baseInstance:baseInstance];
        return;
    }

    // All expansions need CPU-readable contents. The backend creates every GL
    // buffer shared/managed, so contents is non-null in practice; a private
    // buffer here cannot be expanded — drop the draw rather than mis-render.
    if (index_buffer->contents == nullptr) {
        static uint32_t warned = 0;
        warn_limited(warned, "dmt", "expanded draw needs CPU-readable indices "
                                  "(private storage?) — draw dropped");
        return;
    }
    const uint8_t* src = (const uint8_t*)index_buffer->contents + index_offset;

    if (index_type == 2 && !fan && !loop) {
        // Widen U8 -> U16 only.
        const NSUInteger bytes = (NSUInteger)count * 2;
        id<MTLBuffer> sb; NSUInteger off; void* ptr;
        if (!scratch_alloc(bytes, sb, off, ptr)) return;
        uint16_t* dst = (uint16_t*)ptr;
        for (uint32_t i = 0; i < count; ++i) dst[i] = (uint16_t)src[i];
        scratch_flush(sb, off, bytes);
        [r drawIndexedPrimitives:primitive_from_gl(primitive)
                       indexCount:(NSUInteger)count
                        indexType:MTLIndexTypeUInt16
                      indexBuffer:sb
                indexBufferOffset:off
                   instanceCount:instanceCount
                      baseVertex:baseVertex
                    baseInstance:baseInstance];
        return;
    }

    // Gather the source indices as u32 (max of any source width).
    static thread_local std::vector<uint32_t> gather;
    gather.clear();
    gather.reserve(count);
    if (index_type == 1) {
        const uint32_t* s = (const uint32_t*)(const void*)src;
        for (uint32_t i = 0; i < count; ++i) gather.push_back(s[i]);
    } else if (index_type == 2) {
        for (uint32_t i = 0; i < count; ++i) gather.push_back((uint8_t)src[i]);
    } else {
        const uint16_t* s = (const uint16_t*)(const void*)src;
        for (uint32_t i = 0; i < count; ++i) gather.push_back(s[i]);
    }

    if (fan) {
        if (count < 3) return;
        // Values came from the app's index buffer: only a U32 source can
        // exceed 65535, so u16 is safe for U8/U16 sources.
        const bool use16 = (index_type != 1);
        const NSUInteger bytes = (NSUInteger)(count - 2) * 3 * (use16 ? 2 : 4);
        id<MTLBuffer> sb; NSUInteger off; void* ptr;
        if (!scratch_alloc(bytes, sb, off, ptr)) return;
        for (uint32_t t = 0; t + 2 < count; ++t) {
            const uint32_t v[3] = {gather[0], gather[t + 1], gather[t + 2]};
            if (use16) {
                uint16_t* o = (uint16_t*)ptr + (size_t)t * 3;
                o[0] = (uint16_t)v[0]; o[1] = (uint16_t)v[1]; o[2] = (uint16_t)v[2];
            } else {
                uint32_t* o = (uint32_t*)ptr + (size_t)t * 3;
                o[0] = v[0]; o[1] = v[1]; o[2] = v[2];
            }
        }
        scratch_flush(sb, off, bytes);
        [r drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                       indexCount:(NSUInteger)(count - 2) * 3
                        indexType:(use16 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32)
                      indexBuffer:sb
                indexBufferOffset:off
                   instanceCount:instanceCount
                      baseVertex:baseVertex
                    baseInstance:baseInstance];
        return;
    }

    // Line loop: strip of the gathered ids plus the closing first id.
    if (count < 2) return;
    const bool use16 = (index_type != 1);
    const NSUInteger nIdx = (NSUInteger)count + 1;
    const NSUInteger bytes = nIdx * (use16 ? 2 : 4);
    id<MTLBuffer> sb; NSUInteger off; void* ptr;
    if (!scratch_alloc(bytes, sb, off, ptr)) return;
    for (uint32_t i = 0; i < count; ++i) {
        if (use16) ((uint16_t*)ptr)[i] = (uint16_t)gather[i];
        else       ((uint32_t*)ptr)[i] = gather[i];
    }
    if (use16) ((uint16_t*)ptr)[count] = (uint16_t)gather[0];
    else       ((uint32_t*)ptr)[count] = gather[0];
    scratch_flush(sb, off, bytes);
    [r drawIndexedPrimitives:MTLPrimitiveTypeLineStrip
                   indexCount:nIdx
                    indexType:(use16 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32)
                  indexBuffer:sb
            indexBufferOffset:off
               instanceCount:instanceCount
                  baseVertex:baseVertex
                baseInstance:baseInstance];
}

/* Indirect-draw argument records — bit-identical to the GL/Vulkan blocks
 * (Backend.h: GL's parameter blocks pass through untranslated), and exactly
 * Metal's own indirect layouts, so the bytes can be consumed directly. */
struct MtlDrawPrimitivesArgs {
    uint32_t vertexCount;
    uint32_t instanceCount;
    uint32_t vertexStart;
    uint32_t baseInstance;
};
struct MtlDrawIndexedPrimitivesArgs {
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t indexStart;
    int32_t  baseVertex;
    uint32_t baseInstance;
};
static_assert(sizeof(MtlDrawPrimitivesArgs) == 16, "MTLDrawPrimitivesIndirectBufferArguments layout");
static_assert(sizeof(MtlDrawIndexedPrimitivesArgs) == 20, "MTLDrawIndexedPrimitivesIndirectBufferArguments layout");

/* ---- Clear machinery ----
 *
 * ABI CONTRACT with MetalPipeline.mm's built-in clear shaders (must match the
 * MSL that TU emits — flagged for review):
 *   * fragment clear color: [[buffer(0)]] float4   (setFragmentBytes)
 *   * vertex clear depth:   [[buffer(15)]] float   (setVertexBytes; index 15
 *     stays clear of the vertex-attribute slots 0..14 and the VS UBO range
 *     16..29 documented in MetalPipeline.h)
 *   * stencil clear value:  encoder stencil reference value — the clear DSS's
 *     Replace op writes it (that's what clear_depth_stencil_state provides)
 *   * geometry: vertex-id driven fullscreen triangle, no vertex buffers.
 */
struct ClearShaderVsParams { float depth; };
struct ClearShaderFsParams { float color[4]; };

void run_clear_draw(GLbitfield mask, const float color[4], float depth,
                    uint32_t stencil, int x, int y, int w, int h) {
    EncoderState& e = enc();
    if (!e.passActive || g_renderEncoder == nil) return; // gl.cpp always begins a pass first
    if (e.passW <= 0 || e.passH <= 0) return;

    // Clamp the rect to the attachment — Metal faults on an out-of-bounds
    // scissor, and vkCmdClearAttachments had the same requirement
    // (VUID-vkCmdClearAttachments-pRects-00016).
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > e.passW) w = e.passW - x;
    if (y + h > e.passH) h = e.passH - y;
    if (w <= 0 || h <= 0) return;

    VkFormat colorFormats[8] = {VK_FORMAT_UNDEFINED};
    for (int i = 0; i < e.colorCount && i < 8; ++i)
        colorFormats[i] = e.colorViews[i] ? e.colorViews[i]->vkFormat : VK_FORMAT_UNDEFINED;
    VkFormat depthFormat = e.depthView ? e.depthView->vkFormat : VK_FORMAT_UNDEFINED;

    MetalPipeline* pipe = get_clear_pipeline(colorFormats, e.colorCount, depthFormat, mask);
    if (!pipe || pipe->rps == nil) {
        static uint32_t warned = 0;
        warn_limited(warned, "dmt", "clear pipeline unavailable — clear dropped");
        return;
    }
    id<MTLDepthStencilState> dss =
        clear_depth_stencil_state((mask & GL_DEPTH_BUFFER_BIT) != 0,
                                  (mask & GL_STENCIL_BUFFER_BIT) != 0);

    id<MTLRenderCommandEncoder> r = g_renderEncoder;

    // Scope the clear's raster state, then restore the shadow state below.
    // The per-draw entry path re-sets all of this anyway (mirroring
    // Drawing.cpp), so restoring is belt-and-braces for paths that don't.
    [r setViewport:make_viewport(0, 0, e.passW, e.passH, 0.0, 1.0)];
    [r setScissorRect:make_scissor(x, y, w, h, e.passW, e.passH)];
    [r setCullMode:MTLCullModeNone]; // never cull the clear triangle
    [r setDepthBias:0.0f slopeScale:0.0f clamp:0.0f]; // never bias the cleared depth
    [r setRenderPipelineState:pipe->rps];
    if (dss) [r setDepthStencilState:dss];
    if (mask & GL_STENCIL_BUFFER_BIT) [r setStencilReferenceValue:stencil];

    if (mask & GL_COLOR_BUFFER_BIT) {
        ClearShaderFsParams fs;
        fs.color[0] = color ? color[0] : 0.0f;
        fs.color[1] = color ? color[1] : 0.0f;
        fs.color[2] = color ? color[2] : 0.0f;
        fs.color[3] = color ? color[3] : 0.0f;
        [r setFragmentBytes:&fs length:sizeof(fs) atIndex:0];
    }
    if (mask & GL_DEPTH_BUFFER_BIT) {
        ClearShaderVsParams vs;
        vs.depth = depth;
        [r setVertexBytes:&vs length:sizeof(vs) atIndex:15];
    }

    [r drawPrimitives:MTLPrimitiveTypeTriangle
          vertexStart:0
          vertexCount:3
        instanceCount:1
         baseInstance:0];

    // Restore raster state from the shadow (see apply_full_state for the
    // individual pieces).
    if (e.vpValid && e.vpW > 0 && e.vpH > 0)
        [r setViewport:make_viewport(e.vpX, e.vpY, e.vpW, e.vpH, g_vpZNear, g_vpZFar)];
    else
        [r setViewport:make_viewport(0, 0, e.passW, e.passH, 0.0, 1.0)];
    if (e.scValid && e.scW > 0 && e.scH > 0)
        [r setScissorRect:make_scissor(e.scX, e.scY, e.scW, e.scH, e.passW, e.passH)];
    else
        [r setScissorRect:make_scissor(0, 0, e.passW, e.passH, e.passW, e.passH)];
    [r setCullMode:g_cullMode];
    [r setFrontFacingWinding:g_frontWinding];
    if (e.depthBiasOn)
        [r setDepthBias:e.depthBiasSlope slopeScale:0.0f clamp:e.depthBiasClamp];
    else
        [r setDepthBias:0.0f slopeScale:0.0f clamp:0.0f];
    [r setStencilReferenceValue:e.stencilRef];
    id<MTLDepthStencilState> restoreDss = get_depth_stencil_state();
    if (restoreDss) [r setDepthStencilState:restoreDss];
    if (e.boundPipeline && e.boundPipeline->rps) [r setRenderPipelineState:e.boundPipeline->rps];

    e.hasCommands = true;
}

// Resolve the rectangle a clear applies to. GL's scissor test clips clears:
// when enabled the clear rect is the (shadow) scissor rect, otherwise the
// caller's x/y/w/h (in practice the full framebuffer). Mirrors the Vulkan
// compute_clear_rect semantics from Backend.h's clear_attachments contract.
void resolve_clear_rect(const EncoderState& e, int x, int y, int w, int h,
                        int& outX, int& outY, int& outW, int& outH) {
    if (mithril::g_state && mithril::g_state->scissorTest && e.scValid) {
        outX = e.scX; outY = e.scY; outW = e.scW; outH = e.scH;
    } else {
        outX = x; outY = y; outW = w; outH = h;
    }
}

} // namespace

/* ---- Encoder state singleton ---- */

// The shadow state. Static-local: constructed on first use, lives forever —
// safe to reference from completion handlers and other TUs via enc().
EncoderState& enc() {
    static EncoderState s;
    return s;
}

/* ---- Swapchain registration + misc accessors ---- */

// The swapchain whose drawable backs framebuffer 0 this frame. EGL installs it
// right after acquire; commit_frame() reads it only through its `present`
// argument (kept identical for the headless no-swapchain case).
MetalSwapchain* active_swapchain() { return g_activeSwapchain; }
void set_active_swapchain(MetalSwapchain* sc) { g_activeSwapchain = sc; }

bool render_pass_active() { return enc().passActive; }

// Draw guard flag owned by the entry layer (bind_program_descriptors sets it;
// reset at every commit boundary — mirrors the Vulkan descriptorsBound).
void set_descriptors_bound(bool bound) { enc().descriptorsBound = bound; }
bool descriptors_bound() { return enc().descriptorsBound; }

// Blit/compute/query TUs record commands outside the render-encoder path;
// this marks the frame non-empty so commit_frame() does not skip the submit.
void note_non_render_commands() { enc().hasCommands = true; }

/* ---- Render pass lifecycle ---- */

// Begin (or coalesce into) the frame's render pass. If a pass is already
// active this is a no-op — glClear and every draw in the frame share ONE
// encoder exactly like the Vulkan path's pass merging. The load action comes
// from the shadow loadClear flag (glClear sets CLEAR, draw passes LOAD), the
// store action from the one-shot invalidate flags (glInvalidateFramebuffer →
// DontCare, TBDR bandwidth win on Apple Silicon).
void begin_render_pass(MetalTexture** color_views, int color_count,
                       MetalTexture* depth_view, int width, int height,
                       int samples) {
    /* samples 参与签名与 Vulkan 侧对齐，但 MTLRenderPassDescriptor 不需要显式
     * 设置采样数 —— 附件纹理自身的 sampleCount（创建时来自 get_or_create_
     * texture 的 samples 参数）就是 pass 的采样数。PSO 侧的 rasterSampleCount
     * 由 MetalPipeline.mm 从同一来源（draw_fbo_sample_count）取值。 */
    (void)samples;
    Backend* b = backend();
    EncoderState& e = enc();
    if (e.passActive) return; // coalesce
    if (!b->initialized) return;
    if (!ensure_command_buffer()) return; // device lost or allocation failed

    // Only one encoder may be live per command buffer: close a cached compute
    // encoder before opening the render encoder.
    if (g_computeEncoder != nil) {
        [g_computeEncoder endEncoding];
        g_computeEncoder = nil;
    }

    const int n = color_count > 8 ? 8 : (color_count < 0 ? 0 : color_count);
    if (width <= 0 || height <= 0) return;

    @autoreleasepool {
        MTLRenderPassDescriptor* desc = [MTLRenderPassDescriptor renderPassDescriptor];
        const MTLLoadAction load = e.loadClear ? MTLLoadActionClear : MTLLoadActionLoad;
        const MTLClearColor clearClr =
            MTLClearColorMake(e.clearColor[0], e.clearColor[1], e.clearColor[2], e.clearColor[3]);

        for (int i = 0; i < n; ++i) {
            MetalTexture* t = color_views ? color_views[i] : nullptr;
            e.colorViews[i] = t; // record for clear_attachments / pipeline formats
            if (!t || t->tex == nil) continue; // unbound slot: leave attachment unused
            desc.colorAttachments[i].texture = t->tex;
            desc.colorAttachments[i].loadAction = load;
            desc.colorAttachments[i].clearColor = clearClr;
            desc.colorAttachments[i].storeAction =
                ((e.invalidateColorMask >> i) & 1u) ? MTLStoreActionDontCare
                                                    : MTLStoreActionStore;
        }
        e.colorCount = n;

        e.depthView = depth_view;
        if (depth_view && depth_view->tex != nil) {
            desc.depthAttachment.texture = depth_view->tex;
            desc.depthAttachment.loadAction = load;
            desc.depthAttachment.clearDepth = e.clearDepth;
            desc.depthAttachment.storeAction =
                e.invalidateDepth ? MTLStoreActionDontCare : MTLStoreActionStore;
            // Packed depth+stencil formats need the stencil attachment bound
            // separately (same texture) or stencil ops silently no-op.
            if (format_has_stencil(depth_view->vkFormat)) {
                desc.stencilAttachment.texture = depth_view->tex;
                desc.stencilAttachment.loadAction = load;
                desc.stencilAttachment.clearStencil = e.clearStencil;
                desc.stencilAttachment.storeAction =
                    e.invalidateStencil ? MTLStoreActionDontCare : MTLStoreActionStore;
            }
        }

        e.passW = width;
        e.passH = height;
        desc.visibilityResultBuffer = e.visibilityBuffer;

        g_renderEncoder = [b->cmd renderCommandEncoderWithDescriptor:desc];
    }
    if (g_renderEncoder == nil) {
        static uint32_t warned = 0;
        warn_limited(warned, "dmt", "renderCommandEncoderWithDescriptor returned nil — pass skipped");
        e.passActive = false;
        return;
    }

    // A render pipeline is compatible with the attachment formats of the pass
    // it was created for; unlike viewport/scissor/depth-stencil state it is
    // therefore NOT dynamic state that may be replayed onto a new encoder.
    // Drop the previous pass' PSO before replaying the true dynamic shadow.
    // Every draw goes through bind_pipeline() and establishes the pipeline
    // compiled for the current color/depth attachment signature.  This also
    // prevents a clear in a newly-bound depth-only FBO from restoring a color
    // PSO that Metal validation correctly rejects for that encoder.
    e.boundPipeline = nullptr;
    e.descriptorsBound = false;

    // A fresh encoder starts with DEFAULT dynamic state — replay the shadow.
    apply_full_state(g_renderEncoder);

    e.passActive = true;
    e.hasCommands = true;
    e.loadClear = false;         // subsequent passes in this frame LOAD
    e.invalidateColorMask = 0;   // invalidation is one-shot per GL spec
    e.invalidateDepth = false;
    e.invalidateStencil = false;
}

// End the active encoder. Keeps the shadow state (the next pass replays it).
void end_render_pass() {
    EncoderState& e = enc();
    if (!e.passActive) return;
    if (g_renderEncoder != nil) {
        [g_renderEncoder endEncoding];
        g_renderEncoder = nil;
    }
    e.passActive = false;
}

// The live render encoder of the current pass (nil when no pass is active).
// bind_program_descriptors and the clear quad draw through it.
id<MTLRenderCommandEncoder> current_encoder() { return g_renderEncoder; }

void set_visibility_query(id<MTLBuffer> buffer, bool counting) {
    EncoderState& e = enc();
    // The buffer is immutable render-pass state. Changing it requires a new
    // encoder; the next draw recreates the pass with the new descriptor.
    if (e.passActive && e.visibilityBuffer != buffer) end_render_pass();
    e.visibilityBuffer = buffer;
    e.visibilityCounting = counting && buffer != nil;
    if (e.passActive && g_renderEncoder != nil && e.visibilityBuffer != nil) {
        [g_renderEncoder setVisibilityResultMode:(e.visibilityCounting
                                                      ? MTLVisibilityResultModeCounting
                                                      : MTLVisibilityResultModeDisabled)
                                          offset:0];
    }
}

void clear_visibility_query(id<MTLBuffer> buffer) {
    EncoderState& e = enc();
    if (e.visibilityBuffer == buffer) set_visibility_query(nil, false);
}

/* ---- Dynamic-state setters ----
 *
 * Each stores into the shadow AND applies to the live encoder when a pass is
 * active. Values arrive in Vulkan convention (top-left origin) — the entry
 * layer converts the GL bottom-origin coordinates. */

void set_viewport(int x, int y, int w, int h, double znear, double zfar) {
    EncoderState& e = enc();
    e.vpX = x; e.vpY = y; e.vpW = w; e.vpH = h;
    e.vpValid = (w > 0 && h > 0);
    g_vpZNear = znear;
    g_vpZFar = zfar;
    if (e.passActive && g_renderEncoder != nil && e.vpValid) {
        [g_renderEncoder setViewport:make_viewport(x, y, w, h, znear, zfar)];
    }
}

void set_scissor(int x, int y, int w, int h) {
    EncoderState& e = enc();
    e.scX = x; e.scY = y; e.scW = w; e.scH = h;
    e.scValid = (w > 0 && h > 0);
    if (e.passActive && g_renderEncoder != nil && e.scValid) {
        // Clamped to the attachment: Metal validates the scissor extent and
        // faults on overrun.
        [g_renderEncoder setScissorRect:make_scissor(x, y, w, h, e.passW, e.passH)];
    }
}

// mode: 0=None, 1=Front, 2=Back (GL_FRONT_AND_BACK reaches us as an entry-
// layer-mapped value; Metal has no front-and-back cull, treat unknowns as Back
// which matches the Vulkan fallback of culling everything but points/lines).
void set_cull_mode(int mode) {
    g_cullMode = (mode == 1) ? MTLCullModeFront
                : (mode == 2) ? MTLCullModeBack
                : MTLCullModeNone;
    if (enc().passActive && g_renderEncoder != nil) {
        [g_renderEncoder setCullMode:g_cullMode];
    }
}

/* set_front_face — the winding mapping, full reasoning chain:
 *
 * FACTS:
 *  1. Vulkan's viewport transform is the pure affine map
 *     pixel_y = vpY + (ndc_y + 1)/2 * vpH in a TOP-LEFT-origin framebuffer,
 *     i.e. Vulkan NDC +y maps DOWN on screen.
 *  2. Metal NDC +y maps UP (Apple's Hello Triangle: NDC y=+0.5 is the top
 *     apex) — Metal's positive-height transform therefore includes a Y flip
 *     relative to Vulkan.
 *  3. A NEGATIVE-height MTLViewport {x, y+h, w, -h} cancels that flip and
 *     lands every vertex on EXACTLY Vulkan's pixel position (the MoltenVK
 *     trick, verified algebraically in make_viewport's comment).
 *
 * CONSEQUENCE FOR WINDING:
 *  4. Both APIs classify a triangle's facing from its post-transform
 *     framebuffer positions, so with identical pixel positions the only
 *     remaining question is whether the two enums name the same rotation
 *     direction.
 *  5. Metal defines winding in its native NDC orientation (y-up) while
 *     Vulkan defines it in its own (y-down). Because our viewport flip
 *     mirrors the geometry BETWEEN the two NDC conventions, a triangle that
 *     is counter-clockwise in Vulkan's y-down framebuffer space corresponds
 *     to a clockwise triangle in Metal's (pre-flip) y-up space.
 *  6. Therefore VK_FRONT_FACE_CCW (ccw==1) maps to MTLWindingClockwise and
 *     VK_FRONT_FACE_CW (ccw==0) to MTLWindingCounterClockwise — the winding
 *     mapping is INVERTED to compensate the viewport Y flip, exactly the
 *     "negative viewport height + reversed winding" pairing MoltenVK uses.
 *
 * NOTE FOR REVIEW: this is the one mapping in the file I could not verify
 * against running hardware (Linux sandbox). If culling comes out mirrored on
 * device, swapping the two cases below is the entire fix. */
void set_front_face(int ccw) {
    g_frontWinding = (ccw == 1) ? MTLWindingClockwise     // Vulkan CCW
                                : MTLWindingCounterClockwise; // Vulkan CW
    if (enc().passActive && g_renderEncoder != nil) {
        [g_renderEncoder setFrontFacingWinding:g_frontWinding];
    }
}

// compare_func is a GL enum (GL_LESS, ...); converted via MetalFormat.mm.
void set_depth_test(int enabled, int write_mask, int compare_func) {
    EncoderState& e = enc();
    e.depthTest = (enabled != 0);
    e.depthWrite = (write_mask != 0);
    e.depthFunc = compare_func_from_gl((GLenum)compare_func);
    if (e.passActive && g_renderEncoder != nil) {
        id<MTLDepthStencilState> dss = get_depth_stencil_state();
        if (dss) [g_renderEncoder setDepthStencilState:dss];
    }
}

// func/sfail/dpfail/dppass are GL enums. The ref value is encoder state
// (not part of the immutable DSS) so it is applied separately.
void set_stencil_state(int enabled, int func, int ref, int mask,
                       int sfail, int dpfail, int dppass) {
    EncoderState& e = enc();
    e.stencilTest = (enabled != 0);
    e.stencilFunc = compare_func_from_gl((GLenum)func);
    e.stencilRef = (uint32_t)ref;
    e.stencilMask = (uint32_t)mask;
    e.stencilSfail = stencil_op_from_gl((GLenum)sfail);
    e.stencilDpfail = stencil_op_from_gl((GLenum)dpfail);
    e.stencilDppass = stencil_op_from_gl((GLenum)dppass);
    if (e.passActive && g_renderEncoder != nil) {
        [g_renderEncoder setStencilReferenceValue:e.stencilRef];
        id<MTLDepthStencilState> dss = get_depth_stencil_state();
        if (dss) [g_renderEncoder setDepthStencilState:dss];
    }
}

void set_blend_color(float r, float g, float b, float a) {
    EncoderState& e = enc();
    e.blendColor[0] = r; e.blendColor[1] = g;
    e.blendColor[2] = b; e.blendColor[3] = a;
    if (e.passActive && g_renderEncoder != nil) {
        [g_renderEncoder setBlendColorRed:r green:g blue:b alpha:a];
    }
}

// Parameter meaning mirrors the Vulkan reference implementation
// (Backend.h:251): the first argument is the GL polygonOffsetFactor fed as a
// CONSTANT bias, the second the polygonOffsetUnits fed as the clamp, with a
// zero slope factor — vkCmdSetDepthBias(slope, clamp, 0.0f). Metal's argument
// order is setDepthBias:<units> slopeScale:<slope> clamp:<clamp>, so the
// first argument lands in depthBias and slopeScale stays 0.
void set_depth_bias(float slope, float clamp) {
    EncoderState& e = enc();
    e.depthBiasOn = true;
    e.depthBiasSlope = slope;
    e.depthBiasClamp = clamp;
    if (e.passActive && g_renderEncoder != nil) {
        [g_renderEncoder setDepthBias:slope slopeScale:0.0f clamp:clamp];
    }
}

// Bind a graphics pipeline: record it in the shadow, apply the PSO and —
// critically — re-apply the depth/stencil state rebuilt from the CURRENT
// shadow, so a pipeline switch cannot resurrect a stale DSS (Metal keeps
// encoder DSS state across setRenderPipelineState calls).
void bind_pipeline(MetalPipeline* pipe) {
    EncoderState& e = enc();
    e.boundPipeline = pipe;
    if (e.passActive && g_renderEncoder != nil && pipe && pipe->rps) {
        [g_renderEncoder setRenderPipelineState:pipe->rps];
        id<MTLDepthStencilState> dss = get_depth_stencil_state();
        if (dss) [g_renderEncoder setDepthStencilState:dss];
    }
}

// Record + apply one vertex-attribute-slot buffer binding. Slots are replayed
// on encoder re-creation (see apply_full_state) so the clear-quad draws that
// temporarily retarget the encoder cannot leave stale bindings behind.
void set_vertex_buffer(int slot, MetalBuffer* buf, NSUInteger offset) {
    EncoderState& e = enc();
    if (slot < 0 || slot >= kMaxVertexAttribSlots) return;
    e.vertBuf[slot] = (buf && buf->buf != nil) ? buf->buf : nil;
    e.vertOff[slot] = offset;
    if (e.passActive && g_renderEncoder != nil && e.vertBuf[slot] != nil) {
        [g_renderEncoder setVertexBuffer:e.vertBuf[slot]
                                 offset:offset
                                atIndex:(NSUInteger)slot];
    }
}

/* ---- Clear family (inside a pass) ---- */

// glClear: clear ONLY the aspects named by mask (root cause of the old
// loadOp=CLEAR-everything black screen — see Backend.h). Implemented with the
// clear pipeline; values come from the shadow clearColor/clearDepth/clearStencil.
void clear_attachments(GLbitfield mask, int x, int y, int w, int h) {
    EncoderState& e = enc();
    if (!e.passActive) return;
    if ((mask & (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) == 0)
        return;

    int rx, ry, rw, rh;
    resolve_clear_rect(e, x, y, w, h, rx, ry, rw, rh);
    if (mask & GL_DEPTH_BUFFER_BIT && !e.depthView) mask &= ~GL_DEPTH_BUFFER_BIT;
    if (mask & GL_STENCIL_BUFFER_BIT &&
        (!e.depthView || !format_has_stencil(e.depthView->vkFormat)))
        mask &= ~GL_STENCIL_BUFFER_BIT;

    run_clear_draw(mask, e.clearColor, (float)e.clearDepth, e.clearStencil,
                   rx, ry, rw, rh);
}

// glClearBuffer{fv,iv,uiv,fi}: clear one attachment with an explicit value.
//
// APPROXIMATION (documented, mirrors the Vulkan path's whole-set behaviour):
// GL_COLOR names ONE draw buffer via `drawbuffer`, but the clear pipeline
// writes every color attachment of the pass (Metal has no per-attachment
// write mask on a shared PSO short of compiling per-drawbuffer variants).
// glClearBuffer on a MRT therefore wipes all color targets with the same
// value. Depth/stencil honor their own explicit values; GL_DEPTH_STENCIL
// clears both aspects in one draw.
void clear_buffer_indexed(GLenum buffer, GLint drawbuffer, const float color[4],
                          float depth, GLuint stencil) {
    EncoderState& e = enc();
    if (!e.passActive) return;

    GLbitfield mask = 0;
    switch (buffer) {
        case GL_COLOR:
            if (drawbuffer < 0 || drawbuffer >= e.colorCount) return;
            mask = GL_COLOR_BUFFER_BIT;
            break;
        case GL_DEPTH:
            if (!e.depthView) return;
            mask = GL_DEPTH_BUFFER_BIT;
            break;
        case GL_STENCIL:
            if (!e.depthView || !format_has_stencil(e.depthView->vkFormat)) return;
            mask = GL_STENCIL_BUFFER_BIT;
            break;
        case GL_DEPTH_STENCIL:
            if (!e.depthView) return;
            mask = GL_DEPTH_BUFFER_BIT;
            if (format_has_stencil(e.depthView->vkFormat))
                mask |= GL_STENCIL_BUFFER_BIT;
            break;
        default:
            return;
    }

    int rx, ry, rw, rh;
    resolve_clear_rect(e, 0, 0, e.passW, e.passH, rx, ry, rw, rh);
    run_clear_draw(mask, color, depth, stencil, rx, ry, rw, rh);
}

/* ---- Frame lifecycle ---- */

// Ensure the current frame's command buffer exists. On the first use of a
// slot this RETIRES it first: block on the slot's previously-committed
// command buffer (waitUntilCompleted is idempotent — if its completion
// handler already ran, lastCompletedSerial is up to date and the wait returns
// immediately), then rewind the slot's UBO arena and scratch index arena
// (legal ONLY now: the wait proves no in-flight GPU work reads those bytes),
// then create the fresh buffer. This is the whole frame-pacing story: the GPU
// gets MITHRIL_DMT_MAX_FRAMES_IN_FLIGHT-1 frames of latency before the CPU
// blocks, exactly like the Vulkan slot/fence design.
bool ensure_command_buffer() {
    Backend* b = backend();
    if (!b->initialized) return false;
    if (b->deviceLost.load(std::memory_order_acquire)) return false;
    if (b->cmd != nil) return true; // fast path: already recording this frame

    const int slot = b->currentFrame;
    if (b->slotCmd[slot] != nil) {
        // Retire the slot's previous frame. This also guarantees its
        // addCompletedHandler has run (Metal invokes the handler as part of
        // completing the buffer), so the serial watermark is current.
        [b->slotCmd[slot] waitUntilCompleted];
        b->slotCmd[slot] = nil;
    }
    ubo_rewind(slot);
    {
        ScratchSlot& s = g_scratch[slot];
        s.used = 0; // capacity/buffer are reused; grow happens on demand
    }

    @autoreleasepool {
        b->cmd = [b->queue commandBuffer];
    }
    if (b->cmd == nil) {
        static uint32_t warned = 0;
        warn_limited(warned, "dmt", "queue commandBuffer returned nil — marking device lost");
        b->deviceLost.store(true, std::memory_order_release);
        return false;
    }
    // A new command buffer invalidates any cached compute encoder handle.
    g_computeEncoder = nil;
    enc().hasCommands = false; // fresh buffer, no commands yet
    return true;
}

// Commit the frame: end any open encoder, present the swapchain drawable,
// install the completion handler (serial watermark + frame pacing counter +
// deviceLost on command-buffer error), commit, and advance the frame slot.
// Returns false once the device is lost.
bool commit_frame(MetalSwapchain* present) {
    Backend* b = backend();
    EncoderState& e = enc();
    if (!b->initialized) return false;
    if (b->deviceLost.load(std::memory_order_acquire)) return false;

    if (e.passActive) end_render_pass();
    if (g_computeEncoder != nil) { // never leave a live encoder on commit
        [g_computeEncoder endEncoding];
        g_computeEncoder = nil;
    }

    const bool wantPresent =
        (present != nullptr && present->frameAcquired && present->drawable != nil);

    if (b->cmd == nil) {
        if (!wantPresent && !e.hasCommands) {
            // Nothing recorded and nothing to present (eglWaitClient-style
            // double commit): skip the empty submit entirely, mirroring the
            // Vulkan shouldSubmit guard.
            return true;
        }
        // A drawable is pending or commands exist but no buffer was created
        // yet (e.g. a frame of pure clears against an already-ended pass):
        // create one so the present can be encoded at all.
        if (!ensure_command_buffer()) return false;
    }

    // Present must be encoded BEFORE commit (it is a command on the buffer).
    if (wantPresent) {
        [b->cmd presentDrawable:present->drawable];
    }

    // Serial scheme: each committed frame buffer gets submitSerial+1, the
    // value is captured by value, and submitSerial is bumped right after the
    // commit call. Handlers may run in any order relative to the CPU, so the
    // watermark update takes the max; command buffers from one queue execute
    // in FIFO order, which makes "all serials <= watermark completed" sound.
    const uint64_t serial = b->submitSerial.load(std::memory_order_relaxed) + 1;
    Backend* bk = b;
    [b->cmd addCompletedHandler:^(id<MTLCommandBuffer> cb) {
        uint64_t cur = bk->lastCompletedSerial.load(std::memory_order_relaxed);
        while (serial > cur &&
               !bk->lastCompletedSerial.compare_exchange_weak(cur, serial,
                                                               std::memory_order_release,
                                                               std::memory_order_relaxed)) {
            // cur reloaded by compare_exchange_weak; retry.
        }
        bk->completedFrameCount.fetch_add(1, std::memory_order_release);
        if (cb.error != nil) {
            bk->deviceLost.store(true, std::memory_order_release);
        }
    }];
    [b->cmd commit];
    b->slotCmd[b->currentFrame] = b->cmd;
    b->submitSerial.store(serial, std::memory_order_release);
    b->cmd = nil;

    b->currentFrame = (b->currentFrame + 1) % MITHRIL_DMT_MAX_FRAMES_IN_FLIGHT;
    b->frameGeneration++;
    e.hasCommands = false;
    e.descriptorsBound = false; // new buffer: nothing bound yet (draw guard)
    return !b->deviceLost.load(std::memory_order_acquire);
}

// Reset the encoder to a clean "no pass active" baseline after a deviceLost
// recovery (mirrors vk::reset_encoder_state): a stale passActive would route
// the next frame's draws into a nil encoder. Keeps the swapchain
// registration and the clear-color shadows (they describe GL state, not the
// dead frame).
void reset_encoder_state() {
    EncoderState& e = enc();
    e.passActive = false;
    e.boundPipeline = nullptr;
    e.descriptorsBound = false;
    e.hasCommands = false;
    e.visibilityBuffer = nil;
    e.visibilityCounting = false;
    e.colorCount = 0;
    e.depthView = nullptr;
    e.passW = 0;
    e.passH = 0;
    for (int i = 0; i < 8; ++i) e.colorViews[i] = nullptr;
    for (int i = 0; i < kMaxVertexAttribSlots; ++i) e.vertBuf[i] = nil;
    if (g_renderEncoder != nil) {
        // Defensive: if a buffer is still recording, close the encoder
        // cleanly so the next begin_render_pass can create a new one.
        if (backend()->cmd != nil) [g_renderEncoder endEncoding];
        g_renderEncoder = nil;
    }
    if (g_computeEncoder != nil) {
        if (backend()->cmd != nil) [g_computeEncoder endEncoding];
        g_computeEncoder = nil;
    }
}

/* ---- Draw recording (GL primitive enums; fan/loop expanded internally) ---- */

void draw_arrays(GLenum primitive, int first, int count) {
    if (!draw_allowed("dmt_draw_arrays")) return;
    if (count <= 0 || first < 0) return;
    issue_arrays_draw(primitive, (uint32_t)first, (uint32_t)count, 1,
                      current_base_instance());
}

void draw_arrays_instanced(GLenum primitive, int first, int count, int primcount) {
    if (!draw_allowed("dmt_draw_arrays_instanced")) return;
    if (count <= 0 || first < 0) return;
    if (primcount < 1) return; // GL no-op; Metal requires instanceCount >= 1
    issue_arrays_draw(primitive, (uint32_t)first, (uint32_t)count,
                      (uint32_t)primcount, current_base_instance());
}

void draw_indexed(GLenum primitive, int count, int index_type,
                  MetalBuffer* index_buffer, NSUInteger index_offset) {
    if (!draw_allowed("dmt_draw_indexed")) return;
    if (count <= 0) return;
    issue_indexed_draw(primitive, (uint32_t)count, index_type, index_buffer,
                       index_offset, 1, current_base_vertex(),
                       current_base_instance());
}

void draw_indexed_instanced(GLenum primitive, int count, int index_type,
                            MetalBuffer* index_buffer, NSUInteger index_offset,
                            int primcount) {
    if (!draw_allowed("dmt_draw_indexed_instanced")) return;
    if (count <= 0) return;
    if (primcount < 1) return; // GL no-op; Metal requires instanceCount >= 1
    issue_indexed_draw(primitive, (uint32_t)count, index_type, index_buffer,
                       index_offset, (uint32_t)primcount, current_base_vertex(),
                       current_base_instance());
}

/* ---- Indirect draws (GL 4.0 ARB_draw_indirect) ----
 *
 * Metal's indirect argument records are byte-identical to GL's
 * {count, primCount, first/baseVertex, baseInstance} blocks, and the API takes
 * one record per call (no multiDrawIndirect batch), so multi-draws always
 * loop over the records. fan/loop + indirect is rare; the records are read
 * back on the CPU (the buffers are CPU-shared by construction) and pushed
 * through the expansion path record by record. */

void draw_indirect(GLenum primitive, MetalBuffer* indirect, NSUInteger offset,
                   int count, int stride) {
    if (!draw_allowed("dmt_draw_indirect")) return;
    if (!indirect || indirect->buf == nil || count <= 0) return;

    const NSUInteger effStride = (NSUInteger)(stride > 0 ? stride : 16);
    const bool expanded = (primitive == GL_TRIANGLE_FAN || primitive == GL_LINE_LOOP);

    if (!expanded && indirect->contents == nullptr) {
        // Pure GPU-side path: hand the record to Metal untouched. Only a
        // non-default stride forces the per-record loop.
        if (count == 1 && effStride == 16) {
            [g_renderEncoder drawPrimitives:primitive_from_gl(primitive)
                              indirectBuffer:indirect->buf
                        indirectBufferOffset:offset];
            return;
        }
    }
    if (indirect->contents == nullptr) {
        static uint32_t warned = 0;
        warn_limited(warned, "dmt", "draw_indirect needs CPU-readable argument buffer — draw dropped");
        return;
    }

    for (int i = 0; i < count; ++i) {
        MtlDrawPrimitivesArgs args;
        std::memcpy(&args, (const uint8_t*)indirect->contents + offset + (NSUInteger)i * effStride,
                    sizeof(args));
        // GL no-op record: vertexCount 0 or primcount 0 draws nothing.
        if (args.vertexCount == 0 || args.instanceCount == 0) continue;
        if (expanded) {
            issue_arrays_draw(primitive, args.vertexStart, args.vertexCount,
                              args.instanceCount,
                              args.baseInstance);
        } else {
            [g_renderEncoder drawPrimitives:primitive_from_gl(primitive)
                              indirectBuffer:indirect->buf
                        indirectBufferOffset:offset + (NSUInteger)i * effStride];
        }
    }
}

void draw_indexed_indirect(GLenum primitive, int index_type,
                           MetalBuffer* index_buffer, NSUInteger index_offset,
                           MetalBuffer* indirect, NSUInteger offset,
                           int count, int stride) {
    if (!draw_allowed("dmt_draw_indexed_indirect")) return;
    if (!index_buffer || index_buffer->buf == nil) return;
    if (!indirect || indirect->buf == nil || count <= 0) return;

    const NSUInteger effStride = (NSUInteger)(stride > 0 ? stride : 20);
    const bool expanded = (primitive == GL_TRIANGLE_FAN || primitive == GL_LINE_LOOP);
    const bool widenU8 = (index_type == 2); // Metal has no U8 index type

    if (!expanded && !widenU8 && indirect->contents == nullptr &&
        count == 1 && effStride == 20) {
        // Fast path: one record, plain primitive, app index buffer as-is.
        [g_renderEncoder drawIndexedPrimitives:primitive_from_gl(primitive)
                                     indexType:index_type_from_int(index_type)
                                   indexBuffer:index_buffer->buf
                             indexBufferOffset:index_offset
                                indirectBuffer:indirect->buf
                          indirectBufferOffset:offset];
        return;
    }
    if (indirect->contents == nullptr) {
        static uint32_t warned = 0;
        warn_limited(warned, "dmt", "draw_indexed_indirect needs CPU-readable argument buffer — draw dropped");
        return;
    }

    for (int i = 0; i < count; ++i) {
        MtlDrawIndexedPrimitivesArgs args;
        std::memcpy(&args, (const uint8_t*)indirect->contents + offset + (NSUInteger)i * effStride,
                    sizeof(args));
        if (args.indexCount == 0 || args.instanceCount == 0) continue;
        if (expanded || widenU8) {
            // indexStart is the first-index offset in ELEMENTS (GL/Vulkan
            // firstIndex semantics == Metal's indexStart).
            const NSUInteger byteOff = index_offset +
                (NSUInteger)args.indexStart * index_size_of(index_type);
            issue_indexed_draw(primitive, args.indexCount, index_type, index_buffer,
                               byteOff,
                               args.instanceCount,
                               (NSInteger)args.baseVertex, args.baseInstance);
        } else {
            [g_renderEncoder drawIndexedPrimitives:primitive_from_gl(primitive)
                                         indexType:index_type_from_int(index_type)
                                       indexBuffer:index_buffer->buf
                                 indexBufferOffset:index_offset
                                    indirectBuffer:indirect->buf
                              indirectBufferOffset:offset + (NSUInteger)i * effStride];
        }
    }
}

/* ---- GL 4.6 ARB_indirect_parameters (_Count variants) ----
 *
 * NOT declared in the frozen MetalCommandStream.h; defined here with external
 * linkage so MetalBackend.mm can declare + call them (flagged for the entry
 * layer author). Metal has no GPU-side draw-count primitive, so the count is
 * read from the (CPU-shared) count buffer, clamped to max_drawcount, and the
 * records are issued through the indirect loops above — same observable
 * result as vkCmdDrawIndirectCount for CPU-written counts. */

void draw_indirect_count(GLenum primitive, MetalBuffer* indirect, NSUInteger offset,
                         MetalBuffer* count_buffer, NSUInteger count_offset,
                         int max_drawcount, int stride) {
    if (!draw_allowed("dmt_draw_indirect_count")) return;
    if (!indirect || indirect->buf == nil || indirect->contents == nullptr) return;
    if (!count_buffer || count_buffer->contents == nullptr || max_drawcount <= 0) return;

    uint32_t n = *(const uint32_t*)((const uint8_t*)count_buffer->contents + count_offset);
    if (n > (uint32_t)max_drawcount) n = (uint32_t)max_drawcount;
    if (n == 0) return;
    draw_indirect(primitive, indirect, offset, (int)n, stride);
}

void draw_indexed_indirect_count(GLenum primitive, int index_type,
                                 MetalBuffer* index_buffer, NSUInteger index_offset,
                                 MetalBuffer* indirect, NSUInteger offset,
                                 MetalBuffer* count_buffer, NSUInteger count_offset,
                                 int max_drawcount, int stride) {
    if (!draw_allowed("dmt_draw_indexed_indirect_count")) return;
    if (!indirect || indirect->buf == nil || indirect->contents == nullptr) return;
    if (!count_buffer || count_buffer->contents == nullptr || max_drawcount <= 0) return;

    uint32_t n = *(const uint32_t*)((const uint8_t*)count_buffer->contents + count_offset);
    if (n > (uint32_t)max_drawcount) n = (uint32_t)max_drawcount;
    if (n == 0) return;
    draw_indexed_indirect(primitive, index_type, index_buffer, index_offset,
                          indirect, offset, (int)n, stride);
}

/* ---- Compute ---- */

// Provide (and cache) the frame's compute encoder. Metal forbids compute
// inside a render pass — same rule as Vulkan — so an active render pass is
// ended first; the next begin_render_pass replays the shadow state onto a
// fresh encoder, so mid-pass compute dispatches behave like the Vulkan
// end-pass/dispatch/re-begin sequence.
id<MTLComputeCommandEncoder> ensure_compute_encoder() {
    Backend* b = backend();
    if (!b->initialized) return nil;
    if (enc().passActive) end_render_pass();
    if (g_computeEncoder != nil) return g_computeEncoder;
    if (!ensure_command_buffer()) return nil;
    g_computeEncoder = [b->cmd computeCommandEncoder];
    if (g_computeEncoder == nil) {
        static uint32_t warned = 0;
        warn_limited(warned, "dmt", "computeCommandEncoder returned nil");
        return nil;
    }
    enc().hasCommands = true;
    return g_computeEncoder;
}

// Independent command buffer for one-shot synchronous work (readbacks,
// queries, texture clears). Not part of the frame-slot model — callers
// commit + waitUntilCompleted it themselves.
id<MTLCommandBuffer> new_oneshot_command_buffer() {
    Backend* b = backend();
    if (!b->initialized) return nil;
    @autoreleasepool {
        return [b->queue commandBuffer];
    }
}

} // namespace dmt
} // namespace mithril

#endif // __APPLE__
