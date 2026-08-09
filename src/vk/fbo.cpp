// Mithril-Wrapper Vulkan backend -- S5 framebuffer object / renderbuffer
// support. Renders into a GL framebuffer's attachments instead of the
// default offscreen target: a color texture (or renderbuffer) plus an
// optional depth renderbuffer. The Vk framebuffer + render pass are rebuilt
// lazily before each draw flush (a texture re-upload replaces its image, so
// attachment views must track the live TexObj), and the draw/read paths in
// draw.cpp select these resources through the FboObj.

#include "internal.h"

#include <algorithm>
#include <cstring>

#include <util/log.h>

namespace mithril::vk {

namespace {

// GL internal formats actually reachable through S5 (kept intentionally
// small; unlisted formats fall back to R8G8B8A8 and log once).
VkFormat MapRbFormat(GLenum internalformat) {
    switch (internalformat) {
        case GL_RGBA8: case GL_RGBA: return VK_FORMAT_R8G8B8A8_UNORM;
        case GL_RGB8:  case GL_RGB:  return VK_FORMAT_R8G8B8A8_UNORM;
        case GL_RGB565:              return VK_FORMAT_R5G6B5_UNORM_PACK16;
        case GL_RGBA4:               return VK_FORMAT_R4G4B4A4_UNORM_PACK16;
        case GL_DEPTH_COMPONENT16:   return VK_FORMAT_D16_UNORM;
        case GL_DEPTH_COMPONENT24:
        case GL_DEPTH24_STENCIL8:    return VK_FORMAT_D24_UNORM_S8_UINT;
        case GL_DEPTH_COMPONENT32F:  return VK_FORMAT_D32_SFLOAT;
        default: {
            static GLenum warned = 0;
            if (warned != internalformat) {
                warned = internalformat;
                ML_LOG_WARN("vk: renderbuffer internalformat 0x%04x mapped "
                            "to RGBA8", internalformat);
            }
            return VK_FORMAT_R8G8B8A8_UNORM;
        }
    }
}

bool IsDepthFormat(VkFormat f) {
    return f == VK_FORMAT_D16_UNORM || f == VK_FORMAT_D24_UNORM_S8_UINT ||
           f == VK_FORMAT_D32_SFLOAT;
}

// Build a render pass matching the given signature. The default framebuffer
// reuses the engine renderpass (color+depth); FBO passes are cached in
// g.fbo_passes keyed by the signature string.
static VkRenderPass BuildFboPass(const std::string& sig, bool has_depth) {
    if (sig == "RGBA8:D24S8") return g.renderpass;
    auto it = g.fbo_passes.find(sig);
    if (it != g.fbo_passes.end()) return it->second;

    VkAttachmentDescription att[2]{};
    att[0].format = VK_FORMAT_R8G8B8A8_UNORM;
    att[0].samples = VK_SAMPLE_COUNT_1_BIT;
    att[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;   // explicit clear
    att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentReference col{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference dep{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &col;

    VkRenderPassCreateInfo ri{};
    ri.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ri.subpassCount = 1;
    ri.pSubpasses = &sub;

    if (has_depth) {
        att[1].format = VK_FORMAT_D24_UNORM_S8_UINT;
        att[1].samples = VK_SAMPLE_COUNT_1_BIT;
        att[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        att[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        sub.pDepthStencilAttachment = &dep;
        ri.attachmentCount = 2;
    } else {
        ri.attachmentCount = 1;
    }
    ri.pAttachments = att;

    VkRenderPass pass = VK_NULL_HANDLE;
    if (g.fn.CreateRenderPass(g.device, &ri, nullptr, &pass) != VK_SUCCESS) {
        ML_LOG_ERROR("vk: FBO render pass creation failed (%s)", sig.c_str());
        return VK_NULL_HANDLE;
    }
    g.fbo_passes.emplace(sig, pass);
    return pass;
}

void DestroyRbo(RbObj& r) {
    if (r.view) g.fn.DestroyImageView(g.device, r.view, nullptr);
    if (r.image) g.fn.DestroyImage(g.device, r.image, nullptr);
    if (r.mem) g.fn.FreeMemory(g.device, r.mem, nullptr);
    r = RbObj{};
}

// Image for a renderbuffer: an optimal, device-local 2D image with the
// format's natural usage (color or depth/stencil) plus a transfer-src bit so
// FBO readbacks work.
bool CreateRbImage(RbObj& r, VkFormat fmt, uint32_t w, uint32_t h,
                   uint32_t samples) {
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = fmt;
    ii.extent = {std::max(1u, w), std::max(1u, h), 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = samples > 1 ? VK_SAMPLE_COUNT_2_BIT : VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
               (IsDepthFormat(fmt) ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                                   : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (g.fn.CreateImage(g.device, &ii, nullptr, &r.image) != VK_SUCCESS)
        return false;

    VkMemoryRequirements req;
    g.fn.GetImageMemoryRequirements(g.device, r.image, &req);
    uint32_t type = 0;
    if (FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                       &type) != VK_SUCCESS) {
        g.fn.DestroyImage(g.device, r.image, nullptr);
        r.image = VK_NULL_HANDLE;
        return false;
    }
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = type;
    if (g.fn.AllocateMemory(g.device, &ai, nullptr, &r.mem) != VK_SUCCESS ||
        g.fn.BindImageMemory(g.device, r.image, r.mem, 0) != VK_SUCCESS) {
        DestroyRbo(r);
        return false;
    }

    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = r.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = fmt;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    uint32_t layer_count = 1;
    if (IsDepthFormat(fmt)) {
        aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (fmt == VK_FORMAT_D24_UNORM_S8_UINT)
            aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
        layer_count = 1;
    }
    vi.subresourceRange = {aspect, 0, 1, 0, layer_count};
    if (g.fn.CreateImageView(g.device, &vi, nullptr, &r.view) != VK_SUCCESS) {
        DestroyRbo(r);
        return false;
    }
    return true;
}

// Image view for one mip/layer slice of a resident texture (referenced image,
// no extra memory).
bool CreateTexSliceView(VkImage image, VkFormat fmt, bool is_depth,
                        uint32_t level, uint32_t layer, VkImageView* out) {
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = fmt;
    vi.subresourceRange = {is_depth ? VK_IMAGE_ASPECT_DEPTH_BIT
                                    : VK_IMAGE_ASPECT_COLOR_BIT,
                           level, 1, layer, 1};
    return g.fn.CreateImageView(g.device, &vi, nullptr, out) == VK_SUCCESS;
}

} // namespace

// Public: render pass for an FBO signature ("RGBA8:D24S8" -> the default
// pass). Cached in g.fbo_passes.
VkRenderPass GetOrCreateFboPass(const std::string& sig, bool has_depth) {
    if (sig == "RGBA8:D24S8") return g.renderpass;
    auto it = g.fbo_passes.find(sig);
    if (it != g.fbo_passes.end()) return it->second;
    return BuildFboPass(sig, has_depth);
}

// ---------------------------------------------------------------------------
// Public S5 API
// ---------------------------------------------------------------------------

void CreateRenderbuffer(uint64_t rbo_id, GLenum internalformat,
                        uint32_t width, uint32_t height, uint32_t samples) {
    if (!g.initialized) return;
    RbObj r;
    r.format = MapRbFormat(internalformat);
    r.samples = std::max(1u, samples);
    if (!CreateRbImage(r, r.format, width, height, r.samples)) {
        ML_LOG_ERROR("vk: renderbuffer %llu creation failed",
                     (unsigned long long)rbo_id);
        return;
    }
    g.renderbuffers[rbo_id] = std::move(r);
}

void DestroyRenderbuffer(uint64_t rbo_id) {
    auto it = g.renderbuffers.find(rbo_id);
    if (it != g.renderbuffers.end()) {
        DestroyRbo(it->second);
        g.renderbuffers.erase(it);
    }
}

void SetFramebuffer(uint64_t fbo_id, const FboSpec& spec) {
    if (!g.initialized) return;
    FboObj& f = g.framebuffers[fbo_id];
    f.color = spec.color;
    f.has_depth = spec.has_depth;
    f.depth = spec.depth;
    f.width = spec.width;
    f.height = spec.height;
    f.dirty = true;
}

void DestroyFramebuffer(uint64_t fbo_id) {
    auto it = g.framebuffers.find(fbo_id);
    if (it == g.framebuffers.end()) return;
    FboObj& f = it->second;
    if (f.fb) g.fn.DestroyFramebuffer(g.device, f.fb, nullptr);
    if (f.color_view) g.fn.DestroyImageView(g.device, f.color_view, nullptr);
    if (f.depth_view) g.fn.DestroyImageView(g.device, f.depth_view, nullptr);
    g.framebuffers.erase(it);
}

void BindDrawFramebuffer(uint64_t fbo_id) {
    if (g.bound_draw_fbo == fbo_id) return;
    g.bound_draw_fbo = fbo_id;
    // A framebuffer switch changes what the *next* readback should capture;
    // force the next flush to re-record (so the readback tracks the new
    // target even when no draw is queued).
    g.frame_dirty = true;
}
void BindReadFramebuffer(uint64_t fbo_id) {
    if (g.bound_read_fbo == fbo_id) return;
    g.bound_read_fbo = fbo_id;
    g.frame_dirty = true;
}

uint32_t DrawTargetWidth() { return g.bound_draw_fbo ? g.framebuffers[g.bound_draw_fbo].width
                                                     : g.width; }
uint32_t DrawTargetHeight() { return g.bound_draw_fbo ? g.framebuffers[g.bound_draw_fbo].height
                                                      : g.height; }

VkImage FboColorImage(const FboObj& f) {
    if (f.color.is_texture) {
        auto it = g.textures.find(f.color.tex_id);
        return it == g.textures.end() ? VK_NULL_HANDLE : it->second.image;
    }
    auto it = g.renderbuffers.find(f.color.rbo_id);
    return it == g.renderbuffers.end() ? VK_NULL_HANDLE : it->second.image;
}

VkImage FboDepthImage(const FboObj& f) {
    if (!f.has_depth) return VK_NULL_HANDLE;
    if (f.depth.is_texture) {
        auto it = g.textures.find(f.depth.tex_id);
        return it == g.textures.end() ? VK_NULL_HANDLE : it->second.image;
    }
    auto it = g.renderbuffers.find(f.depth.rbo_id);
    return it == g.renderbuffers.end() ? VK_NULL_HANDLE : it->second.image;
}

// Resolve the currently bound draw framebuffer's device resources. Rebuilds
// the Vk framebuffer + render pass when dirty (attachment view, size, or the
// resident colour texture changed). Returns false when the FBO can't render.
bool ResolveDrawFbo(FboObj* out) {
    if (!g.bound_draw_fbo) return true;
    auto it = g.framebuffers.find(g.bound_draw_fbo);
    if (it == g.framebuffers.end()) return false;
    FboObj& f = it->second;

    // Colour image: from a texture (level/layer slice of the resident image)
    // or a renderbuffer.
    VkImage color_img = VK_NULL_HANDLE;
    if (f.color.is_texture) {
        auto tit = g.textures.find(f.color.tex_id);
        if (tit == g.textures.end()) return false;
        color_img = tit->second.image;
    } else {
        auto rit = g.renderbuffers.find(f.color.rbo_id);
        if (rit == g.renderbuffers.end()) return false;
        color_img = rit->second.image;
    }

    // Cache stamp: (texture resident gen) -- recreated on texture upload by
    // replacing the TexObj, so compare the image handle.
    bool tex_changed = f.last_tex_gen != (uint64_t)color_img;
    if (!f.dirty && !tex_changed && f.fb) { *out = f; return true; }
    if (f.fb) g.fn.DestroyFramebuffer(g.device, f.fb, nullptr);
    if (f.color_view) g.fn.DestroyImageView(g.device, f.color_view, nullptr);
    if (f.depth_view) g.fn.DestroyImageView(g.device, f.depth_view, nullptr);
    f.color_view = f.depth_view = VK_NULL_HANDLE;

    VkImage depth_img = VK_NULL_HANDLE;
    if (f.has_depth) {
        if (f.depth.is_texture) {
            auto dit = g.textures.find(f.depth.tex_id);
            if (dit == g.textures.end()) return false;
            depth_img = dit->second.image;
        } else {
            auto dit = g.renderbuffers.find(f.depth.rbo_id);
            if (dit == g.renderbuffers.end()) return false;
            depth_img = dit->second.image;
        }
    }

    if (!CreateTexSliceView(color_img, VK_FORMAT_R8G8B8A8_UNORM, false,
                            f.color.level, f.color.layer, &f.color_view)) {
        ML_LOG_ERROR("vk: FBO %llu colour view creation failed",
                     (unsigned long long)g.bound_draw_fbo);
        return false;
    }
    if (f.has_depth &&
        !CreateTexSliceView(depth_img, VK_FORMAT_D24_UNORM_S8_UINT, true,
                            f.depth.level, f.depth.layer, &f.depth_view)) {
        ML_LOG_ERROR("vk: FBO %llu depth view creation failed",
                     (unsigned long long)g.bound_draw_fbo);
        return false;
    }

    f.sig = f.has_depth ? "RGBA8:D24S8" : "RGBA8:";
    f.pass = GetOrCreateFboPass(f.sig, f.has_depth);
    if (f.pass == VK_NULL_HANDLE) return false;

    VkImageView atts[2] = {f.color_view, f.depth_view};
    VkFramebufferCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fi.renderPass = f.pass;
    fi.attachmentCount = f.has_depth ? 2 : 1;
    fi.pAttachments = atts;
    fi.width = std::max(1u, f.width);
    fi.height = std::max(1u, f.height);
    fi.layers = 1;
    if (g.fn.CreateFramebuffer(g.device, &fi, nullptr, &f.fb) != VK_SUCCESS) {
        ML_LOG_ERROR("vk: FBO %llu framebuffer creation failed",
                     (unsigned long long)g.bound_draw_fbo);
        return false;
    }
    f.last_tex_gen = (uint64_t)color_img;
    f.dirty = false;
    *out = f;
    return true;
}

void BlitFramebuffer(uint64_t src_fbo, uint64_t dst_fbo,
                     GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                     GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                     GLbitfield mask, GLenum filter) {
    if (!g.initialized || !(mask & GL_COLOR_BUFFER_BIT)) return;

    // Resolve source/dest colour images.
    auto img_for = [](uint64_t fbo_id, VkImage* img, VkFormat* fmt,
                      uint32_t* w, uint32_t* h) {
        if (!fbo_id) {
            *img = g.target_image;
            *fmt = g.format;
            *w = g.width;
            *h = g.height;
            return true;
        }
        auto it = g.framebuffers.find(fbo_id);
        if (it == g.framebuffers.end()) return false;
        const FboObj& f = it->second;
        if (f.color.is_texture) {
            auto tit = g.textures.find(f.color.tex_id);
            if (tit == g.textures.end()) return false;
            *img = tit->second.image;
        } else {
            auto rit = g.renderbuffers.find(f.color.rbo_id);
            if (rit == g.renderbuffers.end()) return false;
            *img = rit->second.image;
        }
        *fmt = VK_FORMAT_R8G8B8A8_UNORM;
        *w = f.width;
        *h = f.height;
        return true;
    };

    VkImage src, dst;
    VkFormat sf, df;
    uint32_t sw = 0, sh = 0, dw = 0, dh = 0;
    if (!img_for(src_fbo, &src, &sf, &sw, &sh) ||
        !img_for(dst_fbo, &dst, &df, &dw, &dh)) {
        ML_LOG_WARN("vk: blit framebuffer attachment missing");
        return;
    }

    // GL blit rects are bottom-left; Vulkan images are top-left (the render
    // pass + readback keep the GL flip). Map y to Vulkan rows.
    GLint s_top = sh - std::min(srcY0, srcY1);
    GLint s_bot = sh - std::max(srcY0, srcY1);
    GLint d_top = dh - std::min(dstY0, dstY1);
    GLint d_bot = dh - std::max(dstY0, dstY1);
    // Sizes (>=0 regardless of axis order).
    GLint s_w = std::abs(srcX1 - srcX0);
    GLint s_h = std::abs(srcY1 - srcY0);
    GLint d_w = std::abs(dstX1 - dstX0);
    GLint d_h = std::abs(dstY1 - dstY0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    g.fn.ResetCommandBuffer(g.cmd, 0);
    g.fn.BeginCommandBuffer(g.cmd, &bi);

    TransitionLayout(g.cmd, src, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    TransitionLayout(g.cmd, dst, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Same-size copies use vkCmdCopyImage; scaling uses vkCmdBlitImage.
    if (s_w == d_w && s_h == d_h) {
        VkImageCopy region{};
        region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.srcOffset = {std::min(srcX0, srcX1), s_bot, 0};
        region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.dstOffset = {std::min(dstX0, dstX1), d_bot, 0};
        region.extent = {(uint32_t)s_w, (uint32_t)s_h, 1};
        g.fn.CmdCopyImage(g.cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    } else {
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit.srcOffsets[0] = {std::min(srcX0, srcX1), s_bot, 0};
        blit.srcOffsets[1] = {std::max(srcX0, srcX1), s_top, 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit.dstOffsets[0] = {std::min(dstX0, dstX1), d_bot, 0};
        blit.dstOffsets[1] = {std::max(dstX0, dstX1), d_top, 1};
        VkFilter f = filter == GL_NEAREST ? VK_FILTER_NEAREST
                                          : VK_FILTER_LINEAR;
        g.fn.CmdBlitImage(g.cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                          f);
    }

    TransitionLayout(g.cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    TransitionLayout(g.cmd, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    g.fn.EndCommandBuffer(g.cmd);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &g.cmd;
    if (g.fn.QueueSubmit(g.queue, 1, &si, g.fence) != VK_SUCCESS) {
        ML_LOG_ERROR("vk: blit submit failed");
        return;
    }
    g.fn.WaitForFences(g.device, 1, &g.fence, VK_TRUE, UINT64_MAX);
    g.fn.ResetFences(g.device, 1, &g.fence);
}

} // namespace mithril::vk
