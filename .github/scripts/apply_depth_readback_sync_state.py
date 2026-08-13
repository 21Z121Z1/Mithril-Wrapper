from pathlib import Path


def one(path, old, new):
    p = Path(path)
    s = p.read_text()
    n = s.count(old)
    if n != 1:
        raise SystemExit(f"{path}: expected one match, got {n}: {old[:120]!r}")
    p.write_text(s.replace(old, new, 1))


# ---------------------------------------------------------------------------
# GLsync: do not keep a reference into unordered_map across backend calls that
# may submit, wait, recover or otherwise re-enter the runtime. Use the submit
# serial as the single source of truth: serial==0 means permanently signaled.
# ---------------------------------------------------------------------------
p = Path("Mithril-Wrapper-cpp/MG_Impl/Drawing.cpp")
s = p.read_text()
start = s.index("GLenum glClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {")
end = s.index("\nvoid glWaitSync(", start)
client_wait = r'''GLenum glClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
    MITHRIL_ENSURE_INIT();
    if (!sync) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return GL_WAIT_FAILED;
    }
    if (flags & ~GL_SYNC_FLUSH_COMMANDS_BIT) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return GL_WAIT_FAILED;
    }
    void* handle = reinterpret_cast<void*>(sync);
    auto it = g_state->syncObjects.find(handle);
    if (it == g_state->syncObjects.end()) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return GL_WAIT_FAILED;
    }

    uint64_t serial = it->second.submitSerial;
    if (serial == 0 || serial <= mithril::vk::backend_last_completed_serial()) {
        it->second.signaled = true;
        it->second.submitSerial = 0;
        return GL_ALREADY_SIGNALED;
    }

    if (flags & GL_SYNC_FLUSH_COMMANDS_BIT) {
        backend_end_render_pass();
        backend_commit();
    }

    const bool completed = mithril::vk::backend_wait_serial(serial, (uint64_t)timeout);
    if (!completed) return GL_TIMEOUT_EXPIRED;

    // Backend work above may have crossed a command-buffer/context boundary.
    // Re-find the object instead of writing through a reference retained across
    // a potentially re-entrant call. Completion is monotonic.
    it = g_state->syncObjects.find(handle);
    if (it == g_state->syncObjects.end()) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return GL_WAIT_FAILED;
    }
    it->second.signaled = true;
    it->second.submitSerial = 0;
    return GL_CONDITION_SATISFIED;
}
'''
s = s[:start] + client_wait + s[end:]
start = s.index("void glWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {")
end = s.index("\nGLboolean glIsSync(", start)
server_wait = r'''void glWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
    MITHRIL_ENSURE_INIT();
    if (!sync || flags != 0 || timeout != GL_TIMEOUT_IGNORED) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    void* handle = reinterpret_cast<void*>(sync);
    auto it = g_state->syncObjects.find(handle);
    if (it == g_state->syncObjects.end()) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    const uint64_t serial = it->second.submitSerial;
    if (serial != 0 && !mithril::vk::backend_wait_serial(serial, UINT64_MAX)) return;
    it = g_state->syncObjects.find(handle);
    if (it == g_state->syncObjects.end()) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    it->second.signaled = true;
    it->second.submitSerial = 0;
}
'''
s = s[:start] + server_wait + s[end:]
start = s.index("void glGetSynciv(GLsync sync, GLenum pname, GLsizei bufSize, GLsizei* length, GLint* values) {")
end = s.index("\n\n} // extern \"C\"", start)
get_sync = r'''void glGetSynciv(GLsync sync, GLenum pname, GLsizei bufSize, GLsizei* length, GLint* values) {
    MITHRIL_ENSURE_INIT();
    if (length) *length = 0;
    if (bufSize < 0) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    if (!sync || !values || bufSize == 0) return;
    void* handle = reinterpret_cast<void*>(sync);
    auto it = g_state->syncObjects.find(handle);
    if (it == g_state->syncObjects.end()) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    mithril::Sync& s = it->second;

    // submitSerial==0 is the canonical completed state. Refresh from the Vulkan
    // completion watermark only while a real serial is outstanding.
    if (s.submitSerial != 0 &&
        s.submitSerial <= mithril::vk::backend_last_completed_serial()) {
        s.submitSerial = 0;
        s.signaled = true;
    }

    GLint v = 0;
    switch (pname) {
        case GL_OBJECT_TYPE:    v = GL_SYNC_FENCE; break;
        case GL_SYNC_CONDITION: v = (GLint)s.condition; break;
        case GL_SYNC_FLAGS:     v = (GLint)s.flags; break;
        case GL_SYNC_STATUS:    v = (s.submitSerial == 0) ? GL_SIGNALED : GL_UNSIGNALED; break;
        default:
            mithril::state_set_error(GL_INVALID_ENUM);
            return;
    }
    values[0] = v;
    if (length) *length = 1;
}
'''
s = s[:start] + get_sync + s[end:]
p.write_text(s)

# ---------------------------------------------------------------------------
# glReadPixels depth path. The old backend was color-only and also chose the
# current DRAW framebuffer. GL readback comes from the READ framebuffer.
# Implement a focused depth-aspect transfer path before the existing color path.
# ---------------------------------------------------------------------------
p = Path("Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/ImageOps.cpp")
s = p.read_text()
needle = '''int read_pixels(int x, int y, int w, int h, GLenum format, GLenum type, void* out_pixels) {\n    Backend* b = backend();\n    if (!b->initialized || !out_pixels || w <= 0 || h <= 0) return 0;\n\n'''
if s.count(needle) != 1:
    raise SystemExit("ImageOps read_pixels entry mismatch")
depth_branch = r'''int read_pixels(int x, int y, int w, int h, GLenum format, GLenum type, void* out_pixels) {
    Backend* b = backend();
    if (!b->initialized || !out_pixels || w <= 0 || h <= 0) return 0;

    const GLuint readFboName = g_state ? g_state->currentReadFBO : 0;

    // Depth readback needs a depth-aspect copy; treating it as RGBA color leaves
    // the destination untouched (the old behavior) and also violates Vulkan's
    // aspect contract for depth-only images.
    if (format == GL_DEPTH_COMPONENT) {
        VkImage srcImage = VK_NULL_HANDLE;
        VkFormat srcFmt = VK_FORMAT_UNDEFINED;
        VkImageLayout srcLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        GLuint srcTexId = 0;

        if (readFboName == 0) {
            srcImage = g_state->eglDefaultDepthImage;
            srcFmt = g_state->eglDefaultDepthFormat;
            // Swapchain depth remains attachment-optimal between passes.
            srcLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        } else {
            mithril::Framebuffer* fbo = mithril::state_get_framebuffer(readFboName);
            if (!fbo || fbo->depth.texture == 0) return 0;
            srcTexId = fbo->depth.texture;
            auto& tbl = texture_table();
            auto it = tbl.find(srcTexId);
            if (it == tbl.end() || it->second.image == VK_NULL_HANDLE) return 0;
            srcImage = it->second.image;
            srcFmt = it->second.format;
            srcLayout = it->second.currentLayout;
        }
        if (srcImage == VK_NULL_HANDLE || srcFmt == VK_FORMAT_UNDEFINED) return 0;

        // End/submit any active draw pass before an out-of-band transfer. Queue
        // order then makes the one-shot copy observe all prior depth writes.
        backend_end_render_pass();
        backend_commit();

        // The just-ended user-FBO pass transitions its depth texture to the
        // backend's sampled/read-only layout; refresh after the flush.
        if (readFboName != 0 && srcTexId != 0) {
            auto& tbl = texture_table();
            auto it = tbl.find(srcTexId);
            if (it != tbl.end()) srcLayout = it->second.currentLayout;
        }
        if (srcLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
            srcLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }

        int srcBpp = 0;
        switch (srcFmt) {
            case VK_FORMAT_D16_UNORM:             srcBpp = 2; break;
            case VK_FORMAT_D32_SFLOAT:            srcBpp = 4; break;
            case VK_FORMAT_D24_UNORM_S8_UINT:     srcBpp = 4; break;
            case VK_FORMAT_D32_SFLOAT_S8_UINT:    srcBpp = 4; break; // depth aspect only
            default: return 0;
        }
        const VkDeviceSize stagingSize = (VkDeviceSize)w * (VkDeviceSize)h * (VkDeviceSize)srcBpp;
        BufferEntry staging{};
        if (!create_buffer(staging, stagingSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, nullptr)) return 0;

        OneShotCtx c;
        if (!begin_one_shot(c)) {
            destroy_buffer_entry(staging);
            return 0;
        }

        VkImageMemoryBarrier bar{};
        bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        bar.oldLayout = srcLayout;
        bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.image = srcImage;
        bar.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        bar.subresourceRange.baseMipLevel = 0;
        bar.subresourceRange.levelCount = 1;
        bar.subresourceRange.baseArrayLayer = 0;
        bar.subresourceRange.layerCount = 1;

        VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        if (srcLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
            srcLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) {
            bar.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        } else {
            bar.srcAccessMask = VK_ACCESS_SHADER_READ_BIT |
                                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        }
        bar.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(c.cmd, srcStage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &bar);

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {x, y, 0};
        region.imageExtent = {(uint32_t)w, (uint32_t)h, 1};
        vkCmdCopyImageToBuffer(c.cmd, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               staging.buffer, 1, &region);

        bar.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        bar.dstAccessMask = (srcLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
                             srcLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
                                ? (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)
                                : (VK_ACCESS_SHADER_READ_BIT |
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
        bar.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        bar.newLayout = srcLayout;
        VkPipelineStageFlags dstStage = (srcLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
                                         srcLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
                                            ? (VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                               VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT)
                                            : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        vkCmdPipelineBarrier(c.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, dstStage, 0,
                             0, nullptr, 0, nullptr, 1, &bar);
        end_one_shot(c);

        void* mapped = nullptr;
        if (vkMapMemory(b->device, staging.memory, 0, stagingSize, 0, &mapped) != VK_SUCCESS || !mapped) {
            destroy_buffer_entry(staging);
            return 0;
        }

        const size_t count = (size_t)w * (size_t)h;
        if (type == GL_FLOAT) {
            float* dst = static_cast<float*>(out_pixels);
            if (srcFmt == VK_FORMAT_D32_SFLOAT || srcFmt == VK_FORMAT_D32_SFLOAT_S8_UINT) {
                std::memcpy(dst, mapped, count * sizeof(float));
            } else if (srcFmt == VK_FORMAT_D16_UNORM) {
                const uint16_t* src = static_cast<const uint16_t*>(mapped);
                for (size_t i = 0; i < count; ++i) dst[i] = (float)src[i] / 65535.0f;
            } else { // D24_UNORM_S8_UINT depth aspect is packed into 32 bits.
                const uint32_t* src = static_cast<const uint32_t*>(mapped);
                for (size_t i = 0; i < count; ++i) dst[i] = (float)(src[i] & 0x00FFFFFFu) / 16777215.0f;
            }
        } else {
            // Keep the implementation explicit rather than silently returning
            // mis-typed bytes. Minecraft's diagnostic/readback path and our
            // regression use GL_FLOAT for depth.
            vkUnmapMemory(b->device, staging.memory);
            destroy_buffer_entry(staging);
            return 0;
        }

        vkUnmapMemory(b->device, staging.memory);
        destroy_buffer_entry(staging);
        return 1;
    }

'''
s = s.replace(needle, depth_branch, 1)
# Color glReadPixels must source the READ framebuffer, not the DRAW framebuffer.
s = s.replace("if (g_state->currentDrawFBO == 0) {", "if (readFboName == 0) {", 1)
s = s.replace("mithril::state_get_framebuffer(g_state->currentDrawFBO)", "mithril::state_get_framebuffer(readFboName)", 1)
s = s.replace("if (g_state->currentDrawFBO == 0) {", "if (readFboName == 0) {", 1)
s = s.replace("mithril::state_get_framebuffer(g_state->currentDrawFBO)", "mithril::state_get_framebuffer(readFboName)", 1)
p.write_text(s)
