// Mithril-Wrapper Vulkan backend -- frame submission path.
// Stages the GL payloads into vertex/instance/index buffers, records
// the frame (clear + ordered draws) in one VkCommandBuffer, and reads
// the finished frame back for glReadPixels.

#include "internal.h"

#include <algorithm>
#include <cstring>

namespace mithril::vk {

// Draw path
// ---------------------------------------------------------------------------

namespace {

// MRT: whether colour attachment `i` of `f` is among the active draw buffers.
static bool OpDrawBufEnabled(const FboObj& f, size_t i) {
    for (auto b : f.draw_bufs)
        if (b == (GLenum)(GL_COLOR_ATTACHMENT0 + i)) return true;
    return false;
}

// Create a host-visible staging buffer of `size` bytes and copy `data` in.
bool StageBytes(const void* data, VkDeviceSize size, VkBufferUsageFlags usage,
                VkBuffer* buf, VkDeviceMemory* mem) {
    if (CreateHostBuffer(size, usage, buf, mem) != VK_SUCCESS) {
        ML_LOG_ERROR("vk: draw staging allocation failed");
        return false;
    }
    void* map = nullptr;
    if (g.fn.MapMemory(g.device, *mem, 0, VK_WHOLE_SIZE, 0, &map) ==
        VK_SUCCESS) {
        std::memcpy(map, data, (size_t)size);
        g.fn.UnmapMemory(g.device, *mem);
    }
    return true;
}

// Stage either a frontend-resolved stream or its raw resident-source snapshot.
// Vulkan remains the reference backend and deliberately keeps its existing
// per-draw staging ownership; the shared contract no longer requires repacking.
bool StageStream(const VertexStream& stream, VkBuffer* buf,
                 VkDeviceMemory* mem) {
    if (!stream.HasStorage() || stream.stride == 0) return true;
    const void* bytes = stream.HasResidentSource()
        ? static_cast<const void*>(stream.source_data)
        : static_cast<const void*>(stream.data.data());
    const size_t size = stream.HasResidentSource()
        ? stream.source_size : stream.data.size() * sizeof(float);
    return StageBytes(bytes, size,
                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, buf, mem);
}

} // namespace

void Draw(const DrawParams& params) {
    if (!g.initialized) return;
    auto prog_it = g_programs.find(params.program);
    if (prog_it == g_programs.end()) return;
    const Program& prog = prog_it->second;
    if (!params.vertex_stream.HasStorage()) return;
    // Ensure the bound draw framebuffer's device resources exist (rebuilds
    // them lazily); the pass identity flows into the pipeline cache key.
    FboObj fbo;
    bool rp_ok = ResolveDrawFbo(&fbo);
    if (!rp_ok) return;
    (void)fbo;

    DrawOp op;
    op.program = params.program;
    op.topology = (uint32_t)params.topology;
    op.v_stride = params.vertex_stream.stride;
    op.v_attrs = params.vertex_stream.attrs;
    op.i_stride = params.instance_stream.stride;
    op.i_attrs = params.instance_stream.attrs;
    op.instance_count = std::max<uint32_t>(params.instance_count, 1);
    const size_t vertex_bytes = params.vertex_stream.HasResidentSource()
        ? params.vertex_stream.source_size
        : params.vertex_stream.data.size() * sizeof(float);
    op.vertex_count = params.vertex_stream.record_count
        ? params.vertex_stream.record_count
        : (uint32_t)(vertex_bytes / op.v_stride);
    op.vertex_offset = params.vertex_stream.binding_offset;
    op.instance_offset = params.instance_stream.binding_offset;
    op.index_count = (uint32_t)params.indices.size();
    op.pipe = params.pipeline;
    if (g.bound_draw_fbo) {
        // resolve the FBO material in case Draw() ran before flush
        ResolveDrawFbo(&fbo);
        op.has_render_pass = true;
        op.render_pass = fbo.pass;
        op.rp_sig = fbo.sig;
        op.color_count = (uint32_t)fbo.colors.size();
        op.samples = fbo.samples;
        op.draw_mask = 0;
        for (size_t i = 0; i < fbo.draw_bufs.size() && i < 32; ++i)
            if (fbo.draw_bufs[i] != GL_NONE)
                op.draw_mask |= 1u << (fbo.draw_bufs[i] - GL_COLOR_ATTACHMENT0);
    }

    std::string base_key =
        BuildPipelineKey(params.program, op.topology, op.v_attrs, op.v_stride,
                         op.i_attrs, op.i_stride) +
        StateSignature(params.pipeline);
    op.pipeline_key = base_key + "|RP" + (op.rp_sig.empty() ? "default" : op.rp_sig);

    if (!StageStream(params.vertex_stream, &op.vertex_buffer,
                     &op.vertex_mem))
        return;
    if (!op.i_attrs.empty() &&
        !StageStream(params.instance_stream, &op.instance_buffer,
                     &op.instance_mem)) {
        g.fn.DestroyBuffer(g.device, op.vertex_buffer, nullptr);
        g.fn.FreeMemory(g.device, op.vertex_mem, nullptr);
        return;
    }
    if (op.index_count &&
        !StageBytes(params.indices.data(), op.index_count * sizeof(uint32_t),
                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &op.index_buffer,
                    &op.index_mem)) {
        g.fn.DestroyBuffer(g.device, op.vertex_buffer, nullptr);
        g.fn.FreeMemory(g.device, op.vertex_mem, nullptr);
        if (op.instance_buffer) {
            g.fn.DestroyBuffer(g.device, op.instance_buffer, nullptr);
            g.fn.FreeMemory(g.device, op.instance_mem, nullptr);
        }
        return;
    }

    // Compose the UBO from the reflected members + current uniform values.
    VkDeviceSize range = prog.has_ubo ? prog.ubo_size : 16;
    if (g.ubo_next + range > kUboPoolSize) {
        ML_LOG_WARN("vk: dynamic UBO exhausted; flushing and resetting");
        SubmitFlush();
    }
    op.ubo_offset = AlignUp(g.ubo_next, 16);
    op.ubo_range = range;
    g.ubo_next = op.ubo_offset + range;
    if (prog.has_ubo) {
        std::vector<uint8_t> bytes((size_t)prog.ubo_size, 0);
        for (const auto& m : prog.members) {
            auto it = params.uniforms.find(m.name);
            if (it == params.uniforms.end()) continue;
            size_t n = std::min<size_t>(m.size, it->second.size() * sizeof(float));
            std::memcpy(bytes.data() + m.offset, it->second.data(), n);
        }
        std::memcpy(g.ubo_map + op.ubo_offset, bytes.data(), bytes.size());
    } else {
        std::memset(g.ubo_map + op.ubo_offset, 0, 16);
    }

    // Descriptor for this draw.
    VkDescriptorSetAllocateInfo dsa{};
    dsa.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsa.descriptorPool = g.desc_pool;
    dsa.descriptorSetCount = 1;
    dsa.pSetLayouts = &g.set_layout;
    if (g.fn.AllocateDescriptorSets(g.device, &dsa, &op.desc_set) !=
        VK_SUCCESS) {
        ML_LOG_ERROR("vk: AllocateDescriptorSets failed");
        g.fn.DestroyBuffer(g.device, op.vertex_buffer, nullptr);
        g.fn.FreeMemory(g.device, op.vertex_mem, nullptr);
        if (op.instance_buffer) {
            g.fn.DestroyBuffer(g.device, op.instance_buffer, nullptr);
            g.fn.FreeMemory(g.device, op.instance_mem, nullptr);
        }
        if (op.index_buffer) {
            g.fn.DestroyBuffer(g.device, op.index_buffer, nullptr);
            g.fn.FreeMemory(g.device, op.index_mem, nullptr);
        }
        return;
    }
    VkDescriptorBufferInfo dbi{};
    dbi.buffer = g.ubo;
    dbi.range = op.ubo_range;
    std::vector<VkWriteDescriptorSet> writes;
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = op.desc_set;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    w.pBufferInfo = &dbi;
    writes.push_back(w);

    // Combined image samplers: one per (binding, gl texture id) handed over
    // by the GL layer; unbound units resolve to the 1x1 white dummy.
    std::vector<VkDescriptorImageInfo> tis;
    for (const auto& sb : params.sampler_binds) {
        TexObj* tex = GetTexObj(sb.second);
        if (!tex) continue;
        VkDescriptorImageInfo di{};
        di.sampler = tex->sampler;
        di.imageView = tex->view;
        di.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        tis.push_back(di);
        VkWriteDescriptorSet ws{};
        ws.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws.dstSet = op.desc_set;
        ws.dstBinding = sb.first;
        ws.descriptorCount = 1;
        ws.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ws.pImageInfo = &tis.back();
        writes.push_back(ws);
    }
    g.fn.UpdateDescriptorSets(g.device, (uint32_t)writes.size(), writes.data(),
                              0, nullptr);

    g.frame_draws.push_back(std::move(op));
    g.frame_dirty = true;
}

void SubmitFlush() {
    if (!g.initialized || !g.frame_dirty) return;

    // Target resolution: default framebuffer vs bound FBO.
    uint32_t pw = g.width, ph = g.height;
    VkRenderPass rp = g.renderpass;
    VkFramebuffer fb_handle = g.target_fb;
    VkImage color_img = g.target_image;
    VkImageLayout* color_layout = &g.target_layout;
    VkImage depth_img = g.depth_image;
    VkImageLayout* depth_layout = &g.depth_layout;
    bool has_depth = true;
    FboObj* fbo = nullptr;

    if (g.bound_draw_fbo) {
        FboObj resolved;
        if (!ResolveDrawFbo(&resolved)) return;
        auto it = g.framebuffers.find(g.bound_draw_fbo);
        if (it == g.framebuffers.end()) return;
        fbo = &it->second;
        rp = fbo->pass;
        fb_handle = fbo->fb;
        pw = fbo->width;
        ph = fbo->height;
        color_img =
            fbo->color_msaa.empty() ? VK_NULL_HANDLE
                                    : FboColorImage(*fbo, 0);
        color_layout = &fbo->color_layout;
        if (fbo->has_depth) {
            depth_img = FboDepthImage(*fbo);
            depth_layout = &fbo->depth_layout;
        } else {
            depth_img = VK_NULL_HANDLE;
            has_depth = false;
        }
    }

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    g.fn.ResetCommandBuffer(g.cmd, 0);
    g.fn.BeginCommandBuffer(g.cmd, &bi);

    // Optional explicit clear of the current target. MRT: one clear per
    // colour attachment (only those selected by the draw buffers).
    VkImageSubresourceRange color_range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (g.pending_clear) {
        if (g.clear_mask & GL_COLOR_BUFFER_BIT) {
            VkClearColorValue c{};
            c.float32[0] = g.clear_r;
            c.float32[1] = g.clear_g;
            c.float32[2] = g.clear_b;
            c.float32[3] = g.clear_a;
            auto clear_img = [&](VkImage img, VkImageLayout lay) {
                if (img == VK_NULL_HANDLE) return;
                TransitionLayout(g.cmd, img, lay,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                g.fn.CmdClearColorImage(g.cmd, img,
                                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                        &c, 1, &color_range);
            };
            if (!fbo) {
                clear_img(color_img, *color_layout);
            } else {
                for (size_t i = 0; i < fbo->colors.size(); ++i) {
                    if (!(OpDrawBufEnabled(*fbo, i))) continue;
                    clear_img(FboColorImage(*fbo, (int)i),
                              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                    if (fbo->color_msaa[i])
                        clear_img(FboResolveImage(*fbo, (int)i),
                                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                }
            }
        }
    }
    if (g.pending_clear && has_depth &&
        (g.clear_mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT))) {
        VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (g.clear_mask & GL_STENCIL_BUFFER_BIT)
            aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
        if (*depth_layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            TransitionLayoutAspect(g.cmd, depth_img, {aspect, 0, 1, 0, 1},
                                   *depth_layout,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            *depth_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        }
        VkClearDepthStencilValue c{};
        c.depth = (float)g.clear_depth;
        c.stencil = (uint32_t)g.clear_stencil;
        VkImageSubresourceRange depth_range{aspect, 0, 1, 0, 1};
        g.fn.CmdClearDepthStencilImage(g.cmd, depth_img,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                       &c, 1, &depth_range);
    }
    if (*color_layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        TransitionLayout(g.cmd, color_img, *color_layout,
                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        *color_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    // Bring the FBO colour resolve targets back to color-attachment optimal
    // (the resolve blit in the render pass expects that layout).
    for (size_t i = 0; i < (fbo ? fbo->resolve_view.size() : 0); ++i)
        if (fbo->color_msaa[i] &&
            fbo->resolve_view[i] &&
            FboResolveImage(*fbo, (int)i) != VK_NULL_HANDLE) {
            auto rim = FboResolveImage(*fbo, (int)i);
            TransitionLayout(g.cmd, rim, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        }
    if (has_depth &&
        *depth_layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        TransitionLayoutAspect(g.cmd, depth_img,
                               {VK_IMAGE_ASPECT_DEPTH_BIT |
                                    VK_IMAGE_ASPECT_STENCIL_BIT,
                                0, 1, 0, 1},
                               *depth_layout,
                               VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        *depth_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    if (!g.frame_draws.empty()) {
        VkRenderPassBeginInfo rbi{};
        rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rbi.renderPass = rp;
        rbi.framebuffer = fb_handle;
        rbi.renderArea = {0, 0, pw, ph};
        g.fn.CmdBeginRenderPass(g.cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport vp{};
        vp.x = g.vp_x;
        vp.y = g.vp_y;
        vp.width = std::min<float>(g.vp_w, pw);
        vp.height = std::min<float>(g.vp_h, ph);
        vp.minDepth = 0.f;
        vp.maxDepth = 1.f;
        g.fn.CmdSetViewport(g.cmd, 0, 1, &vp);

        // Per-draw scissor: GL_SCISSOR_TEST gates a per-draw rectangle
        // (dynamic state), otherwise the full target.
        for (const auto& op : g.frame_draws) {
            VkPipeline pipe =
                GetOrCreatePipeline(g_programs.at(op.program), op);
            if (pipe == VK_NULL_HANDLE) continue;
            g.fn.CmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);

            // Dynamic scissor for this draw (GL_SCISSOR_TEST).
            VkRect2D sc;
            if (op.pipe.scissor_test && g.sc_w > 0 && g.sc_h > 0) {
                // GL scissor has a bottom-left origin; Vulkan is top-left, so
                // flip Y and clamp the rectangle to the target.
                int32_t sx = std::clamp<int32_t>((int32_t)g.sc_x, 0,
                                                 (int32_t)pw);
                int32_t sy = std::clamp<int32_t>((int32_t)ph - ((int32_t)g.sc_y + (int32_t)g.sc_h), 0,
                                                 (int32_t)ph);
                uint32_t sw = std::min<uint32_t>((uint32_t)g.sc_w,
                                                 pw - (uint32_t)sx);
                uint32_t sh = std::min<uint32_t>((uint32_t)g.sc_h,
                                                 ph - (uint32_t)sy);
                sc.offset = {sx, sy};
                sc.extent = {sw, sh};
            } else {
                sc.offset = {0, 0};
                sc.extent = {pw, ph};
            }
            g.fn.CmdSetScissor(g.cmd, 0, 1, &sc);

            const VkBuffer binds[2] = {op.vertex_buffer, op.instance_buffer};
            const VkDeviceSize offsets[2] = {op.vertex_offset,
                                             op.instance_offset};
            uint32_t nb = op.instance_buffer ? 2 : 1;
            g.fn.CmdBindVertexBuffers(g.cmd, 0, nb, binds, offsets);

            if (op.index_count) {
                g.fn.CmdBindIndexBuffer(g.cmd, op.index_buffer, 0,
                                        VK_INDEX_TYPE_UINT32);
            }
            uint32_t dyn = (uint32_t)op.ubo_offset;
            g.fn.CmdBindDescriptorSets(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       g.pipeline_layout, 0, 1, &op.desc_set,
                                       1, &dyn);
            if (op.index_count) {
                g.fn.CmdDrawIndexed(g.cmd, op.index_count, op.instance_count,
                                    0, 0, 0);
            } else {
                g.fn.CmdDraw(g.cmd, op.vertex_count, op.instance_count, 0, 0);
            }
        }
        g.fn.CmdEndRenderPass(g.cmd);
    }

    // Copy the finished read-buffer colour image to the readback buffer.
    VkImage read_img = g.target_image;
    VkImageLayout read_layout = g.target_layout;
    uint32_t rw = g.width, rh = g.height;
    FboObj* read_fbo = nullptr;
    bool have_read = false;
    int ridx = 0;
    if (g.bound_read_fbo) {
        auto rit = g.framebuffers.find(g.bound_read_fbo);
        if (rit != g.framebuffers.end()) {
            read_fbo = &rit->second;
            ridx = (int)(read_fbo->read_buf - GL_COLOR_ATTACHMENT0);
            if (ridx < 0 || ridx >= (int)read_fbo->colors.size()) ridx = 0;
            rw = rit->second.width;
            rh = rit->second.height;
            have_read = true;
        }
    } else if (fbo) {
        // No separate read binding: read back the draw target.
        read_fbo = fbo;
        ridx = (int)(read_fbo->read_buf - GL_COLOR_ATTACHMENT0);
        if (ridx < 0 || ridx >= (int)read_fbo->colors.size()) ridx = 0;
        rw = fbo->width;
        rh = fbo->height;
        have_read = true;
    }
    if (have_read) {
        if (read_fbo->color_msaa[ridx]) {
            read_img = FboResolveImage(*read_fbo, ridx);
            read_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        } else {
            read_img = FboColorImage(*read_fbo, ridx);
            read_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
    }

    if (read_img != VK_NULL_HANDLE &&
        read_layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        TransitionLayout(g.cmd, read_img, read_layout,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        read_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }

    // (Re)size the readback buffer to the read target.
    if (g.read_w != rw || g.read_h != rh) {
        if (g.readback) {
            g.fn.UnmapMemory(g.device, g.readback_mem);
            g.fn.DestroyBuffer(g.device, g.readback, nullptr);
            g.fn.FreeMemory(g.device, g.readback_mem, nullptr);
            g.readback = VK_NULL_HANDLE;
            g.readback_mem = VK_NULL_HANDLE;
            g.readback_map = nullptr;
        }
        if (CreateHostBuffer((VkDeviceSize)rw * rh * 4,
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT, &g.readback,
                             &g.readback_mem) == VK_SUCCESS &&
            g.fn.MapMemory(g.device, g.readback_mem, 0, VK_WHOLE_SIZE, 0,
                           reinterpret_cast<void**>(&g.readback_map)) == VK_SUCCESS) {
            g.read_w = rw;
            g.read_h = rh;
        }
    }

    if (read_img != VK_NULL_HANDLE && g.readback_map && rw && rh) {
        VkBufferImageCopy bic{};
        bic.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        bic.imageExtent = {rw, rh, 1};
        g.fn.CmdCopyImageToBuffer(g.cmd, read_img,
                                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g.readback,
                                  1, &bic);
    }

    // Restore the read-image layout. MSAA resolve images are only ever
    // transfer-src (read back), so nothing to do; FBO textures may also be
    // sampled next, so leave them shader-read-optimal.
    if (read_img != VK_NULL_HANDLE) {
        if (read_fbo && !read_fbo->color_msaa[ridx]) {
            TransitionLayout(g.cmd, read_img, read_layout,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            read_fbo->color_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        } else if (!read_fbo && read_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
            TransitionLayout(g.cmd, read_img, read_layout,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            g.target_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
    }

    g.fn.EndCommandBuffer(g.cmd);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &g.cmd;
    if (g.fn.QueueSubmit(g.queue, 1, &si, g.fence) != VK_SUCCESS) {
        ML_LOG_ERROR("vk: QueueSubmit failed");
        return;
    }
    g.fn.WaitForFences(g.device, 1, &g.fence, VK_TRUE, UINT64_MAX);
    g.fn.ResetFences(g.device, 1, &g.fence);

    for (auto& op : g.frame_draws) {
        g.fn.DestroyBuffer(g.device, op.vertex_buffer, nullptr);
        g.fn.FreeMemory(g.device, op.vertex_mem, nullptr);
        if (op.instance_buffer) {
            g.fn.DestroyBuffer(g.device, op.instance_buffer, nullptr);
            g.fn.FreeMemory(g.device, op.instance_mem, nullptr);
        }
        if (op.index_buffer) {
            g.fn.DestroyBuffer(g.device, op.index_buffer, nullptr);
            g.fn.FreeMemory(g.device, op.index_mem, nullptr);
        }
    }
    g.fn.ResetDescriptorPool(g.device, g.desc_pool, 0);
    g.ubo_next = 0;
    g.pending_clear = false;
    g.frame_draws.clear();
    g.frame_dirty = false;
    ML_LOG_DEBUG("vk: frame submitted to %s target (%ux%u)",
                 fbo ? "FBO" : "default", pw, ph);
}

void RefreshReadback() {
    // GL_READ_BUFFER / the read target changed after a rendered frame:
    // arrange for the next SubmitFlush to re-record the readback copy.
    if (!g.initialized) return;
    g.frame_dirty = true;
}

void ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, void* out) {
    if (!g.initialized || !g.readback_map) return;
    x = std::max<GLint>(0, x);
    y = std::max<GLint>(0, y);
    width = std::min<GLsizei>(width, (GLsizei)g.read_w - x);
    height = std::min<GLsizei>(height, (GLsizei)g.read_h - y);
    if (width <= 0 || height <= 0) return;
    // Rows are copied upside-down to match GL's bottom-left framebuffer
    // origin (the Vulkan framebuffer has +Y down).
    for (GLsizei row = 0; row < height; ++row) {
        std::memcpy(reinterpret_cast<uint8_t*>(out) + (size_t)row * width * 4,
                    g.readback_map +
                        ((size_t)(g.read_h - 1 - (y + row)) * g.read_w + x) * 4,
                    (size_t)width * 4);
    }
}
} // namespace mithril::vk
