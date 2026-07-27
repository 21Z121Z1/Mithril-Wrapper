// Mithril-Wrapper - MG_Backend/DirectVulkan/CommandStream.h
// Render-pass orchestration (dynamic rendering) + encoder dynamic-state setters
// + draw command recording + per-frame submit/present. Implements the
// backend_begin_render_pass / backend_end_render_pass / backend_commit /
// backend_set_* / backend_draw_* family declared in MG_Backend/Backend.h.
#ifndef MITHRIL_DIRECTVULKAN_COMMANDSTREAM_H
#define MITHRIL_DIRECTVULKAN_COMMANDSTREAM_H

#include <vulkan/vulkan.h>

namespace mithril {
namespace vk {

struct Swapchain;

// True when a dynamic-rendering pass is currently active (between
// begin_render_pass() and end_render_pass()).
bool render_pass_active();

// Pending clear values applied to the load op of the next render pass.
void set_clear_color(float r, float g, float b, float a);
void set_clear_depth(double d);
void set_clear_stencil(int s);
void set_load_clear(bool clear);   // true = CLEAR (glClear), false = LOAD

/*
 * Register the swapchain whose currently-acquired image is the render target
 * for framebuffer 0. Called by EGL (install_surface_on_state) after each
 * vkAcquireNextImageKHR. begin_render_pass() uses this to record the
 * PRESENT_SRC/UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL layout barrier on the
 * swapchain color image (and the one-shot UNDEFINED ->
 * DEPTH_STENCIL_ATTACHMENT_OPTIMAL barrier on the depth image); commit_frame()
 * uses it to record the reverse COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR
 * barrier before vkEndCommandBuffer, and to signal the swapchain's
 * pendingRenderFinished semaphore on vkQueueSubmit so vkQueuePresentKHR can
 * wait on it. Pass nullptr to detach (headless / no surface).
 */
void set_active_swapchain(Swapchain* sc);

// Begin a dynamic-rendering pass against the given attachments.
void begin_render_pass(VkImageView* color_views, int color_count,
                       VkImageView depth_view, int width, int height, int samples);

// End the active dynamic-rendering pass.
void end_render_pass();

// Submit the recorded command buffer to the graphics queue and wait on the
// per-frame fence. Called by backend_commit() and backend_present_and_acquire().
void commit_frame();

} // namespace vk
} // namespace mithril

#endif // MITHRIL_DIRECTVULKAN_COMMANDSTREAM_H
