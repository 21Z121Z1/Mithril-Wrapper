// Mithril-Wrapper - MG_Backend/DirectVulkan/CommandStream.cpp
// Render-pass orchestration via VK_KHR_dynamic_rendering (vkCmdBeginRendering)
// + encoder dynamic-state setters + draw recording + per-frame submit.
#include "CommandStream.h"
#include "Device.h"
#include "../Backend.h"
#include "../../MG_Impl/Log.h"

#include <cstring>
#include <vector>

namespace mithril {
namespace vk {

namespace {

// Encoder state carried between begin_render_pass() and the draw calls.
struct EncoderState {
    bool passActive = false;
    VkPipeline boundPipeline = VK_NULL_HANDLE;

    // Pending clear values (applied to the load op of the next pass).
    float clearColor[4] = {0, 0, 0, 0};
    double clearDepth = 1.0;
    GLint clearStencil = 0;
    bool loadClear = false;   // true = CLEAR (glClear), false = LOAD (draw pass)

    // Color/depth attachment views for the active pass.
    VkImageView colorViews[8] = {};
    int colorCount = 0;
    VkImageView depthView = VK_NULL_HANDLE;
    int width = 0;
    int height = 0;
};

EncoderState& encoder() {
    static EncoderState s;
    return s;
}

GLbitfield clearMaskPending = 0;

} // namespace

bool render_pass_active() { return encoder().passActive; }

void set_clear_color(float r, float g, float b, float a) {
    auto& e = encoder();
    e.clearColor[0] = r; e.clearColor[1] = g; e.clearColor[2] = b; e.clearColor[3] = a;
}
void set_clear_depth(double d) { encoder().clearDepth = d; }
void set_clear_stencil(int s)  { encoder().clearStencil = s; }
void set_load_clear(bool clear){ encoder().loadClear = clear; }

void begin_render_pass(VkImageView* color_views, int color_count,
                       VkImageView depth_view, int width, int height, int samples) {
    (void)samples;
    Backend* b = backend();
    if (!b->initialized || !b->commandBuffer) return;
    EncoderState& e = encoder();
    if (e.passActive) return;  // coalesce draws into one pass

    // Reset + begin the command buffer (one-shot per frame).
    vkResetCommandBuffer(b->commandBuffer, 0);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(b->commandBuffer, &bi);

    // Record the per-frame attachments so draw commands can reference them.
    e.colorCount = color_count > 8 ? 8 : color_count;
    for (int i = 0; i < e.colorCount; ++i) e.colorViews[i] = color_views ? color_views[i] : VK_NULL_HANDLE;
    e.depthView = depth_view;
    e.width = width;
    e.height = height;

    // Begin dynamic rendering.
    VkRenderingAttachmentInfoKHR colorAttachs[8] = {};
    for (int i = 0; i < e.colorCount; ++i) {
        colorAttachs[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
        colorAttachs[i].imageView = e.colorViews[i];
        colorAttachs[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachs[i].loadOp = e.loadClear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAttachs[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachs[i].clearValue.color.float32[0] = e.clearColor[0];
        colorAttachs[i].clearValue.color.float32[1] = e.clearColor[1];
        colorAttachs[i].clearValue.color.float32[2] = e.clearColor[2];
        colorAttachs[i].clearValue.color.float32[3] = e.clearColor[3];
    }
    VkRenderingAttachmentInfoKHR depthAttach{};
    depthAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
    depthAttach.imageView = e.depthView;
    depthAttach.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttach.loadOp = e.loadClear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttach.clearValue.depthStencil.depth = (float)e.clearDepth;
    depthAttach.clearValue.depthStencil.stencil = (uint32_t)e.clearStencil;

    VkRenderingInfoKHR ri{};
    ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
    ri.renderArea.offset.x = 0;
    ri.renderArea.offset.y = 0;
    ri.renderArea.extent.width = (uint32_t)width;
    ri.renderArea.extent.height = (uint32_t)height;
    ri.layerCount = 1;
    ri.colorAttachmentCount = (uint32_t)e.colorCount;
    ri.pColorAttachments = e.colorCount > 0 ? colorAttachs : nullptr;
    ri.pDepthAttachment = e.depthView ? &depthAttach : nullptr;
    ri.pStencilAttachment = e.depthView ? &depthAttach : nullptr;

    // Resolve the dynamic-rendering entry point (Vulkan 1.2 + extension).
    static PFN_vkCmdBeginRenderingKHR fn = nullptr;
    if (!fn) {
        fn = (PFN_vkCmdBeginRenderingKHR)vkGetDeviceProcAddr(b->device, "vkCmdBeginRendering");
        if (!fn) fn = (PFN_vkCmdBeginRenderingKHR)vkGetDeviceProcAddr(b->device, "vkCmdBeginRenderingKHR");
    }
    if (fn) fn(b->commandBuffer, &ri);

    e.passActive = true;
    e.loadClear = false;  // subsequent passes within the frame use LOAD
}

void end_render_pass() {
    Backend* b = backend();
    EncoderState& e = encoder();
    if (!e.passActive || !b->commandBuffer) return;

    static PFN_vkCmdEndRenderingKHR fn = nullptr;
    if (!fn) {
        fn = (PFN_vkCmdEndRenderingKHR)vkGetDeviceProcAddr(b->device, "vkCmdEndRendering");
        if (!fn) fn = (PFN_vkCmdEndRenderingKHR)vkGetDeviceProcAddr(b->device, "vkCmdEndRenderingKHR");
    }
    if (fn) fn(b->commandBuffer);

    e.passActive = false;
}

void commit_frame() {
    Backend* b = backend();
    if (!b->initialized || !b->commandBuffer) return;
    EncoderState& e = encoder();
    if (e.passActive) end_render_pass();

    VkResult r = vkEndCommandBuffer(b->commandBuffer);
    if (r != VK_SUCCESS) {
        MITHRIL_LOG_WARN("vk", "vkEndCommandBuffer failed (rc=%d)", (int)r);
        return;
    }

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &b->commandBuffer;

    VkFence fence = b->frameFences[b->currentFrame];
    vkResetFences(b->device, 1, &fence);
    r = vkQueueSubmit(b->graphicsQueue, 1, &si, fence);
    if (r != VK_SUCCESS) {
        MITHRIL_LOG_WARN("vk", "vkQueueSubmit failed (rc=%d)", (int)r);
        return;
    }

    // Wait on the frame we just submitted so the command buffer is reusable.
    vkWaitForFences(b->device, 1, &fence, VK_TRUE, UINT64_MAX);
    b->currentFrame = (b->currentFrame + 1) % kMaxFramesInFlight;

    // Begin a fresh command buffer so subsequent uploads/records have somewhere
    // to go. (Render pass begins will reset + begin again.)
    vkResetCommandBuffer(b->commandBuffer, 0);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(b->commandBuffer, &bi);
}

} // namespace vk
} // namespace mithril

// ===========================================================================
// Public C API (declared in MG_Backend/Backend.h)
// ===========================================================================
extern "C" {

void backend_set_clear_color(float r, float g, float b, float a) {
    mithril::vk::set_clear_color(r, g, b, a);
}
void backend_set_clear_depth(double d) { mithril::vk::set_clear_depth(d); }
void backend_set_clear_stencil(int s)  { mithril::vk::set_clear_stencil(s); }
void backend_set_load_clear(void)      { mithril::vk::set_load_clear(true); }
void backend_set_load_load(void)       { mithril::vk::set_load_clear(false); }

void backend_begin_render_pass(VkImageView* color_views, int color_count,
                               VkImageView depth_view, int width, int height, int samples) {
    mithril::vk::begin_render_pass(color_views, color_count, depth_view, width, height, samples);
}

void backend_end_render_pass(void) { mithril::vk::end_render_pass(); }
void backend_commit(void)          { mithril::vk::commit_frame(); }

void backend_bind_pipeline(VkPipeline pipeline) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (b->commandBuffer && pipeline) {
        vkCmdBindPipeline(b->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    }
}

void backend_set_viewport(int x, int y, int w, int h, double znear, double zfar) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->commandBuffer) return;
    VkViewport vp{};
    vp.x = (float)x;
    vp.y = (float)(b->props.limits.maxViewportDimensions[1] - y - h); // GL bottom-left -> Vulkan top-left (approx)
    // Simpler: use the GL viewport directly (MoltenVK handles flip). Match Metal backend behaviour.
    vp.x = (float)x; vp.y = (float)y;
    vp.width = (float)w;
    vp.height = (float)h;
    vp.minDepth = (float)znear;
    vp.maxDepth = (float)zfar;
    vkCmdSetViewport(b->commandBuffer, 0, 1, &vp);
}

void backend_set_scissor(int x, int y, int w, int h) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->commandBuffer) return;
    VkRect2D sc{};
    sc.offset.x = x; sc.offset.y = y;
    sc.extent.width = (uint32_t)w; sc.extent.height = (uint32_t)h;
    vkCmdSetScissor(b->commandBuffer, 0, 1, &sc);
}

void backend_set_vertex_buffer(int slot, VkBuffer buffer, VkDeviceSize offset) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->commandBuffer || !buffer) return;
    VkDeviceSize offsets[1] = { offset };
    vkCmdBindVertexBuffers(b->commandBuffer, (uint32_t)slot, 1, &buffer, offsets);
}

void backend_set_fragment_buffer(int slot, VkBuffer buffer, VkDeviceSize offset) {
    (void)slot; (void)buffer; (void)offset;
    // Fragment UBO binding requires a descriptor set; deferred (bring-up).
}

void backend_set_vertex_texture(int slot, VkImageView view, VkSampler sampler) {
    (void)slot; (void)view; (void)sampler;
    // Texture binding requires descriptor sets; deferred (bring-up).
}

void backend_set_fragment_texture(int slot, VkImageView view, VkSampler sampler) {
    (void)slot; (void)view; (void)sampler;
    // Texture binding requires descriptor sets; deferred (bring-up).
}

void backend_set_blend_color(float r, float g, float b, float a) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->commandBuffer) return;
    float bc[4] = { r, g, b, a };
    vkCmdSetBlendConstants(b->commandBuffer, bc);
}

void backend_set_depth_bias(float slope, float clamp) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->commandBuffer) return;
    vkCmdSetDepthBias(b->commandBuffer, slope, clamp, 0.0f);
}

void backend_set_cull_mode(int mode) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->commandBuffer) return;
    VkCullModeFlags cull = VK_CULL_MODE_NONE;
    if (mode == 1) cull = VK_CULL_MODE_FRONT_BIT;
    else if (mode == 2) cull = VK_CULL_MODE_BACK_BIT;
    vkCmdSetCullMode(b->commandBuffer, cull);
}

void backend_set_front_face(int ccw) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->commandBuffer) return;
    vkCmdSetFrontFace(b->commandBuffer, ccw ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE);
}

void backend_set_depth_test(int enabled, int write_mask, int compare_func) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->commandBuffer) return;
    vkCmdSetDepthTestEnable(b->commandBuffer, enabled ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthWriteEnable(b->commandBuffer, write_mask ? VK_TRUE : VK_FALSE);
    VkCompareOp op = VK_COMPARE_OP_LESS;
    switch (compare_func) {
        case 0x200: op = VK_COMPARE_OP_NEVER; break;    // GL_NEVER
        case 0x201: op = VK_COMPARE_OP_LESS; break;     // GL_LESS
        case 0x202: op = VK_COMPARE_OP_EQUAL; break;    // GL_EQUAL
        case 0x203: op = VK_COMPARE_OP_LESS_OR_EQUAL; break;
        case 0x204: op = VK_COMPARE_OP_GREATER; break;
        case 0x205: op = VK_COMPARE_OP_NOT_EQUAL; break;
        case 0x206: op = VK_COMPARE_OP_GREATER_OR_EQUAL; break;
        case 0x207: op = VK_COMPARE_OP_ALWAYS; break;
        default: op = VK_COMPARE_OP_LESS; break;
    }
    vkCmdSetDepthCompareOp(b->commandBuffer, op);
}

void backend_set_color_write_mask(int r, int g, int b, int a) {
    (void)r; (void)g; (void)b; (void)a;
    // VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT is extension-only; colour
    // write is part of the pipeline's blend attachment for now.
}

void backend_set_stencil_state(int enabled, int func, int ref, int mask,
                               int sfail, int dpfail, int dppass) {
    (void)enabled; (void)func; (void)ref; (void)mask;
    (void)sfail; (void)dpfail; (void)dppass;
    // Stencil dynamic state deferred (bring-up).
}

void backend_draw_arrays(int primitive, int first, int count) {
    (void)primitive;
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->commandBuffer) return;
    vkCmdDraw(b->commandBuffer, (uint32_t)count, 1, (uint32_t)first, 0);
}

void backend_draw_indexed(int primitive, int count, int index_type,
                          VkBuffer index_buffer, VkDeviceSize index_offset) {
    (void)primitive;
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->commandBuffer || !index_buffer) return;
    VkIndexType t = (index_type == 1) ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
    vkCmdBindIndexBuffer(b->commandBuffer, index_buffer, index_offset, t);
    vkCmdDrawIndexed(b->commandBuffer, (uint32_t)count, 1, 0, 0, 0);
}

void backend_draw_arrays_instanced(int primitive, int first, int count, int primcount) {
    (void)primitive;
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->commandBuffer) return;
    vkCmdDraw(b->commandBuffer, (uint32_t)count, (uint32_t)primcount, (uint32_t)first, 0);
}

void backend_draw_indexed_instanced(int primitive, int count, int index_type,
                                    VkBuffer index_buffer, VkDeviceSize index_offset,
                                    int primcount) {
    (void)primitive;
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->commandBuffer || !index_buffer) return;
    VkIndexType t = (index_type == 1) ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
    vkCmdBindIndexBuffer(b->commandBuffer, index_buffer, index_offset, t);
    vkCmdDrawIndexed(b->commandBuffer, (uint32_t)count, (uint32_t)primcount, 0, 0, 0);
}

} // extern "C"
