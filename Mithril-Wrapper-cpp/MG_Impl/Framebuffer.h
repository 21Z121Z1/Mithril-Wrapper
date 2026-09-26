// Mithril-Wrapper - MG_Impl/Framebuffer.h
// Framebuffer attachment resolution for the Vulkan backend. Replaces the
// former gl/framebuffer.h which returned Metal texture handles; this version
// returns Vulkan VkImageView handles (the swapchain image views + texture
// views) so the drawing path can build the dynamic-rendering attachment list.
#ifndef MITHRIL_FRAMEBUFFER_H
#define MITHRIL_FRAMEBUFFER_H

#include <vulkan/vulkan.h>
#include <GL/gl.h>

namespace mithril {

// Resolve the current draw framebuffer's attachments into Vulkan image views.
// For user FBOs, each color attachment's VkImageView comes from
// backend_get_texture_view(); for the EGL default framebuffer (FBO 0), the
// swapchain color/depth views installed on g_state->eglDefaultColor/Depth are
// returned. Returns the number of valid color attachments (<=8).
//   out_color[8] : filled with color VkImageViews (VK_NULL_HANDLE allowed)
//   *out_depth   : depth VkImageView (VK_NULL_HANDLE if none)
//   *out_w/*out_h: render area (from FBO 0's EGL surface or the first color tex)
int collect_draw_fbo_attachments(VkImageView out_color[8], VkImageView* out_depth,
                                 int* out_w, int* out_h);

/* Resolve the current READ framebuffer for synchronous readback/clear visibility.
 * For a user FBO, only the glReadBuffer-selected color attachment is returned
 * at slot 0. out_color_tex_id receives the backing GL texture name (0 for the
 * default framebuffer), allowing the backend to register layout tracking
 * before materializing a deferred clear. */
int collect_read_fbo_attachments(VkImageView out_color[8], VkImageView* out_depth,
                                 int* out_w, int* out_h,
                                 GLuint* out_color_tex_id, GLuint* out_depth_tex_id);

} // namespace mithril

#endif // MITHRIL_FRAMEBUFFER_H
