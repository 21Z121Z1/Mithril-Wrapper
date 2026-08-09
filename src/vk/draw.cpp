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

// Stage a float32 stream into buf/mem (no-op for an empty stream).
bool StageStream(const VertexStream& stream, VkBuffer* buf,
                 VkDeviceMemory* mem) {
    if (stream.data.empty() || stream.stride == 0) return true;
    return StageBytes(stream.data.data(), stream.data.size() * sizeof(float),
                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, buf, mem);
}

} // namespace

void Draw(const DrawParams& params) {
    if (!g.initialized) return;
    auto prog_it = g_programs.find(params.program);
    if (prog_it == g_programs.end()) return;
    const Program& prog = prog_it->second;
    if (params.vertex_stream.data.empty()) return;

    DrawOp op;
    op.program = params.program;
    op.topology = (uint32_t)params.topology;
    op.v_stride = params.vertex_stream.stride;
    op.v_attrs = params.vertex_stream.attrs;
    op.i_stride = params.instance_stream.stride;
    op.i_attrs = params.instance_stream.attrs;
    op.instance_count = std::max<uint32_t>(params.instance_count, 1);
    op.vertex_count =
        (uint32_t)(params.vertex_stream.data.size() * sizeof(float) /
                   op.v_stride);
    op.index_count = (uint32_t)params.indices.size();
    op.pipe = params.pipeline;
    op.pipeline_key =
        BuildPipelineKey(params.program, op.topology, op.v_attrs, op.v_stride,
                         op.i_attrs, op.i_stride) +
        StateSignature(params.pipeline);

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

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    g.fn.ResetCommandBuffer(g.cmd, 0);
    g.fn.BeginCommandBuffer(g.cmd, &bi);

    // Optional explicit clear.
    VkImageSubresourceRange color_range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (g.pending_clear) {
        if (g.clear_mask & GL_COLOR_BUFFER_BIT) {
            if (g.target_layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
                TransitionLayout(g.cmd, g.target_image, g.target_layout,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                g.target_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            }
            VkClearColorValue c{};
            c.float32[0] = g.clear_r;
            c.float32[1] = g.clear_g;
            c.float32[2] = g.clear_b;
            c.float32[3] = g.clear_a;
            g.fn.CmdClearColorImage(g.cmd, g.target_image,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &c, 1,
                                    &color_range);
        }
        if (g.clear_mask &
            (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) {
            VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
            if (g.clear_mask & GL_STENCIL_BUFFER_BIT)
                aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
            if (g.depth_layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
                TransitionLayoutAspect(
                    g.cmd, g.depth_image, {aspect, 0, 1, 0, 1}, g.depth_layout,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                g.depth_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            }
            VkClearDepthStencilValue c{};
            c.depth = (float)g.clear_depth;
            c.stencil = (uint32_t)g.clear_stencil;
            VkImageSubresourceRange depth_range{aspect, 0, 1, 0, 1};
            g.fn.CmdClearDepthStencilImage(g.cmd, g.depth_image,
                                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                           &c, 1, &depth_range);
        }
    }
    if (g.target_layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        TransitionLayout(g.cmd, g.target_image, g.target_layout,
                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        g.target_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    if (g.depth_layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        TransitionLayoutAspect(g.cmd, g.depth_image,
                               {VK_IMAGE_ASPECT_DEPTH_BIT |
                                    VK_IMAGE_ASPECT_STENCIL_BIT,
                                0, 1, 0, 1},
                               g.depth_layout,
                               VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        g.depth_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    if (!g.frame_draws.empty()) {
        VkRenderPassBeginInfo rbi{};
        rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rbi.renderPass = g.renderpass;
        rbi.framebuffer = g.target_fb;
        rbi.renderArea = {0, 0, g.width, g.height};
        g.fn.CmdBeginRenderPass(g.cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport vp{};
        vp.x = g.vp_x;
        vp.y = g.vp_y;
        vp.width = std::min<float>(g.vp_w, g.width);
        vp.height = std::min<float>(g.vp_h, g.height);
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
                                                 (int32_t)g.width);
                int32_t sy = std::clamp<int32_t>(
                    (int32_t)g.height - ((int32_t)g.sc_y + (int32_t)g.sc_h),
                    0, (int32_t)g.height);
                uint32_t sw = std::min<uint32_t>((uint32_t)g.sc_w,
                                                 g.width - (uint32_t)sx);
                uint32_t sh = std::min<uint32_t>((uint32_t)g.sc_h,
                                                 g.height - (uint32_t)sy);
                sc.offset = {sx, sy};
                sc.extent = {sw, sh};
            } else {
                sc.offset = {0, 0};
                sc.extent = {g.width, g.height};
            }
            g.fn.CmdSetScissor(g.cmd, 0, 1, &sc);

            const VkBuffer binds[2] = {op.vertex_buffer, op.instance_buffer};
            const VkDeviceSize zeros[2] = {0, 0};
            uint32_t nb = op.instance_buffer ? 2 : 1;
            g.fn.CmdBindVertexBuffers(g.cmd, 0, nb, binds, zeros);

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

    // Copy the finished frame to the readback buffer (final layout).
    VkBufferImageCopy bic{};
    bic.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    bic.imageExtent = {g.width, g.height, 1};
    g.fn.CmdCopyImageToBuffer(g.cmd, g.target_image,
                              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g.readback,
                              1, &bic);
    TransitionLayout(g.cmd, g.target_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    g.target_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

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
}

void ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, void* out) {
    if (!g.initialized || !g.readback_map) return;
    x = std::max<GLint>(0, x);
    y = std::max<GLint>(0, y);
    width = std::min<GLsizei>(width, (GLsizei)g.width - x);
    height = std::min<GLsizei>(height, (GLsizei)g.height - y);
    if (width <= 0 || height <= 0) return;
    // Rows are copied upside-down to match GL's bottom-left framebuffer
    // origin (the Vulkan framebuffer has +Y down).
    for (GLsizei row = 0; row < height; ++row) {
        std::memcpy(reinterpret_cast<uint8_t*>(out) + (size_t)row * width * 4,
                    g.readback_map +
                        ((size_t)(g.height - 1 - (y + row)) * g.width + x) * 4,
                    (size_t)width * 4);
    }
}
} // namespace mithril::vk
