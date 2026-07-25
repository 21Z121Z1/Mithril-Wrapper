// Mithril-Wrapper - MG_Impl/Drawing.cpp
// Core drawing path: glDrawArrays / glDrawElements / instanced variants ->
// Vulkan dynamic-rendering + pipeline orchestration.
//
// Pipeline: resolve VAO + program + FBO attachments -> get-or-create
// VkGraphicsPipeline (backend_get_or_create_pipeline, SPIR-V + vertex format +
// attachment VkFormats + blend state as cache key) -> begin dynamic render
// pass (Load action) -> bind pipeline + set viewport/scissor/cull/depth/mask
// via vkCmdSet* -> bind vertex buffers + textures/samplers + uniform buffers
// -> issue draw -> end pass.
//
// This is the Vulkan/MoltenVK rewrite of the former gl/drawing.cpp. The Metal
// encoder calls (metal_encoder_*) are replaced with the Vulkan backend C API
// (backend_*) declared in MG_Backend/Backend.h. Render passes use Vulkan 1.2
// dynamic rendering (VK_KHR_dynamic_rendering) instead of Metal render
// encoders.
#include "includes.h"
#include "Framebuffer.h"

#include <algorithm>
#include <cstring>

extern "C" {

static void prepare_draw(GLenum mode) {
    // Resolve current program + its SPIR-V.
    mithril::Program* prog = mithril::state_get_program(g_state->currentProgram);
    if (!prog || !prog->linked) return;

    // Resolve current draw FBO attachments (color + depth VkImageViews + size).
    VkImageView colors[8] = {VK_NULL_HANDLE};
    VkImageView depth_view = VK_NULL_HANDLE;
    int w = 0, h = 0;
    int color_count = mithril::collect_draw_fbo_attachments(colors, &depth_view, &w, &h);

    // Compute color attachment VkFormats.
    VkFormat color_formats[8] = {VK_FORMAT_UNDEFINED};
    mithril::Framebuffer* fbo = mithril::state_get_framebuffer(g_state->currentDrawFBO);
    if (fbo) {
        for (int i = 0; i < color_count; ++i) {
            GLuint t = fbo->colors[i].texture;
            mithril::Texture* tex = mithril::state_get_texture(t);
            if (tex) color_formats[i] = backend_vk_format_for_gl((GLenum)tex->internalFormat);
        }
    } else {
        // EGL default framebuffer: the swapchain color format is BGRA8Unorm
        // (set by the swapchain creation in Swapchain.cpp). Hardcode it here
        // since there's no GL texture object to query.
        for (int i = 0; i < color_count; ++i) {
            if (colors[i] != VK_NULL_HANDLE) {
                color_formats[i] = VK_FORMAT_B8G8R8A8_UNORM;
            }
        }
    }
    // Depth format from the bound depth texture.
    // For FBO 0 (EGL default framebuffer), the depth image is a raw VkImage
    // created by the EGL layer (VK_FORMAT_D32_SFLOAT_S8_UINT), not tracked in
    // the GL texture table. For user FBOs, derive the VkFormat from the GL
    // internalFormat.
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    if (fbo && fbo->depth.texture) {
        mithril::Texture* dt = mithril::state_get_texture(fbo->depth.texture);
        if (dt) depth_format = backend_vk_format_for_gl((GLenum)dt->internalFormat);
    } else if (depth_view != VK_NULL_HANDLE) {
        // EGL default framebuffer: depth is always D32_SFLOAT_S8_UINT.
        depth_format = VK_FORMAT_D32_SFLOAT_S8_UINT;
    }

    // Build the vertex attribute descriptor array for the pipeline signature.
    mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO);
    if (!vao) vao = mithril::state_get_vao(0);
    MGVertexAttrib attribs[mithril::kMaxVertexAttribs];
    int attrib_count = 0;
    for (int i = 0; i < mithril::kMaxVertexAttribs; ++i) {
        const mithril::VertexAttrib& a = vao->attribs[i];
        if (!a.enabled) continue;
        MGVertexAttrib& m = attribs[attrib_count++];
        m.location     = i;
        m.size         = a.size;
        m.type         = a.type;
        m.normalized   = a.normalized ? 1 : 0;
        m.integer      = a.integer ? 1 : 0;
        m.stride       = a.stride;
        m.offset       = (int)(intptr_t)a.pointer;
        m.enabled      = 1;
        m.buffer_name  = a.boundBuffer;
    }

    // Get-or-create the VkGraphicsPipeline. Blend state is part of the
    // pipeline signature so that enabling/disabling GL_BLEND or changing
    // blend functions creates a distinct pipeline.
    VkPipeline pipeline = backend_get_or_create_pipeline(
        prog->id,
        prog->vertexSpirv.data(),   (int)prog->vertexSpirv.size(),
        prog->fragmentSpirv.data(), (int)prog->fragmentSpirv.size(),
        attribs, attrib_count,
        color_formats, color_count,
        depth_format,
        g_state->blend ? 1 : 0,
        g_state->blendSrcRGB,
        g_state->blendDstRGB,
        mode);
    if (pipeline == VK_NULL_HANDLE) return;

    // Begin render pass (Load action preserves previous contents).
    backend_set_load_load();
    backend_begin_render_pass(colors, color_count, depth_view, w, h, 1);

    // Bind pipeline + set dynamic state via vkCmdSet*.
    backend_bind_pipeline(pipeline);
    backend_set_viewport(g_state->viewportX, g_state->viewportY,
                         g_state->viewportW, g_state->viewportH,
                         g_state->depthNear, g_state->depthFar);
    if (g_state->scissorTest) {
        backend_set_scissor(g_state->scissorX, g_state->scissorY,
                            g_state->scissorW, g_state->scissorH);
    }
    if (g_state->cullFace) {
        int mode_cull = 0;
        if (g_state->cullMode == GL_FRONT) mode_cull = 1;
        else if (g_state->cullMode == GL_BACK) mode_cull = 2;
        backend_set_cull_mode(mode_cull);
        backend_set_front_face(g_state->frontFace == GL_CCW ? 1 : 0);
    }
    backend_set_color_write_mask(
        g_state->colorMask[0], g_state->colorMask[1],
        g_state->colorMask[2], g_state->colorMask[3]);
    backend_set_depth_test(
        g_state->depthTest ? 1 : 0,
        g_state->depthMask ? 1 : 0,
        (int)g_state->depthFunc);
    if (g_state->polygonOffsetFill) {
        backend_set_depth_bias(g_state->polygonOffsetUnits, 0.0f);
    }
    if (g_state->blend) {
        backend_set_blend_color(
            g_state->blendColor[0], g_state->blendColor[1],
            g_state->blendColor[2], g_state->blendColor[3]);
    }

    // Bind vertex buffers — one VkBuffer per enabled attribute, at index
    // == attribute location (matches the vertex input binding layout). For
    // attribute slots the VAO didn't enable, bind the shared zero buffer so
    // the unbound vertex input reads vec4(0) instead of dereferencing
    // unbound memory.
    VkBuffer zero_buf = backend_get_zero_buffer();
    bool bound_slots[16] = {false};
    for (int i = 0; i < attrib_count; ++i) {
        MGVertexAttrib& m = attribs[i];
        VkBuffer buf = backend_get_buffer(m.buffer_name);
        if (buf != VK_NULL_HANDLE) {
            backend_set_vertex_buffer(m.location, buf, (VkDeviceSize)m.offset);
            if (m.location < 16) bound_slots[m.location] = true;
        }
    }
    // Bind the zero buffer to any slot 0..15 not covered above.
    if (zero_buf != VK_NULL_HANDLE) {
        for (int loc = 0; loc < 16; ++loc) {
            if (!bound_slots[loc]) {
                backend_set_vertex_buffer(loc, zero_buf, 0);
            }
        }
    }

    // Bind textures bound to the current program (best-effort: bind the first
    // few texture units to fragment-stage texture/sampler slots).
    for (int u = 0; u < mithril::kMaxTextureUnits && u < 32; ++u) {
        GLuint tex_id = g_state->boundTextures[u];
        if (!tex_id) continue;
        VkImageView view = backend_get_texture_view(tex_id);
        VkSampler samp = backend_get_or_create_sampler(
            tex_id, GL_LINEAR, GL_LINEAR, GL_REPEAT, GL_REPEAT, GL_REPEAT, nullptr);
        if (view != VK_NULL_HANDLE && samp != VK_NULL_HANDLE) {
            backend_set_fragment_texture(u, view, samp);
        }
    }

    // Bind uniform values. MoltenVK/SPIRV-Cross assigns each GLSL uniform a
    // buffer slot starting at 30 (SPVC_COMPILER_OPTION_MSL_UNIFORM_BUFFER_BASE,
    // applied by MoltenVK's SPIRV-Cross). We create/update a small VkBuffer per
    // uniform and bind it to both vertex and fragment stages. Without this,
    // ProjMat/ModelViewMat are all zeros and every vertex collapses to the
    // origin -> black screen.
    {
        GLuint base = 30;
        GLuint idx = 0;
        for (auto& kv : prog->uniforms) {
            mithril::Uniform& u = kv.second;
            if (u.value.empty()) { idx++; continue; }
            // Create or update a Vulkan buffer for this uniform's data.
            // Use a stable name derived from the program id + uniform index.
            GLuint uname = prog->id * 10000 + idx;
            size_t sz = u.value.size() * sizeof(float);
            VkBuffer ubuf = backend_get_or_create_buffer(uname, u.value.data(), sz);
            if (ubuf != VK_NULL_HANDLE) {
                GLuint slot = base + idx;
                backend_set_vertex_buffer(slot, ubuf, 0);
                backend_set_fragment_buffer(slot, ubuf, 0);
            }
            idx++;
        }
    }
}

static void end_draw(void) {
    // End the render pass but DON'T commit the command buffer here.
    // The command buffer is committed once per frame in eglSwapBuffers,
    // which presents the swapchain image. Committing per-draw would flush
    // the Vulkan pipeline hundreds of times per frame, causing severe perf
    // loss and present timing issues.
    backend_end_render_pass();
}

static int index_type_to_int(GLenum type) {
    return (type == GL_UNSIGNED_INT) ? 1 : 0;
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    MITHRIL_ENSURE_INIT();
    prepare_draw(mode);
    backend_draw_arrays((int)mode, (int)first, (int)count);
    end_draw();
}

void glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei primcount) {
    MITHRIL_ENSURE_INIT();
    prepare_draw(mode);
    backend_draw_arrays_instanced((int)mode, (int)first, (int)count, (int)primcount);
    end_draw();
}

void glDrawArraysInstancedBaseInstance(GLenum mode, GLint first, GLsizei count,
                                       GLsizei primcount, GLuint baseinstance) {
    MITHRIL_ENSURE_INIT();
    (void)baseinstance; // base-instance is not exposed by the current backend wrapper
    prepare_draw(mode);
    backend_draw_arrays_instanced((int)mode, (int)first, (int)count, (int)primcount);
    end_draw();
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices) {
    MITHRIL_ENSURE_INIT();
    prepare_draw(mode);
    // If a VBO is bound for GL_ELEMENT_ARRAY_BUFFER, indices is an offset into it.
    mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO);
    GLuint ib_name = vao ? vao->elementArrayBuffer : 0;
    VkBuffer ib = backend_get_buffer(ib_name);
    if (ib != VK_NULL_HANDLE) {
        backend_draw_indexed((int)mode, (int)count, index_type_to_int(type),
                             ib, (VkDeviceSize)(intptr_t)indices);
    } else if (indices) {
        // Client-space index pointer: stage into a transient VkBuffer.
        size_t elem = (type == GL_UNSIGNED_INT) ? 4 : 2;
        GLuint transient = (GLuint)(uintptr_t)indices; // use address as throwaway name
        VkBuffer staged = backend_get_or_create_buffer(transient | 0x80000000u,
                                                       indices, (size_t)count * elem);
        if (staged != VK_NULL_HANDLE) {
            backend_draw_indexed((int)mode, (int)count, index_type_to_int(type),
                                 staged, 0);
        }
    }
    end_draw();
}

void glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type,
                              const void* indices, GLint basevertex) {
    MITHRIL_ENSURE_INIT();
    (void)basevertex; // base-vertex not exposed by the current backend wrapper
    glDrawElements(mode, count, type, indices);
}

void glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type,
                             const void* indices, GLsizei primcount) {
    MITHRIL_ENSURE_INIT();
    prepare_draw(mode);
    mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO);
    GLuint ib_name = vao ? vao->elementArrayBuffer : 0;
    VkBuffer ib = backend_get_buffer(ib_name);
    if (ib != VK_NULL_HANDLE) {
        backend_draw_indexed_instanced((int)mode, (int)count,
                                       index_type_to_int(type), ib,
                                       (VkDeviceSize)(intptr_t)indices, (int)primcount);
    } else if (indices) {
        size_t elem = (type == GL_UNSIGNED_INT) ? 4 : 2;
        GLuint transient = (GLuint)(uintptr_t)indices;
        VkBuffer staged = backend_get_or_create_buffer(transient | 0x80000000u,
                                                       indices, (size_t)count * elem);
        if (staged != VK_NULL_HANDLE) {
            backend_draw_indexed_instanced((int)mode, (int)count,
                                           index_type_to_int(type), staged, 0,
                                           (int)primcount);
        }
    }
    end_draw();
}

void glDrawElementsInstancedBaseVertex(GLenum mode, GLsizei count, GLenum type,
                                       const void* indices, GLsizei primcount,
                                       GLint basevertex) {
    MITHRIL_ENSURE_INIT();
    (void)basevertex;
    glDrawElementsInstanced(mode, count, type, indices, primcount);
}

void glDrawElementsInstancedBaseInstance(GLenum mode, GLsizei count, GLenum type,
                                         const void* indices, GLsizei primcount,
                                         GLuint baseinstance) {
    MITHRIL_ENSURE_INIT();
    (void)baseinstance;
    glDrawElementsInstanced(mode, count, type, indices, primcount);
}

void glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count,
                         GLenum type, const void* indices) {
    MITHRIL_ENSURE_INIT();
    (void)start; (void)end;
    glDrawElements(mode, count, type, indices);
}

void glDrawElementsBaseVertexBaseInstance(GLenum mode, GLsizei count, GLenum type,
                                          const void* indices, GLint basevertex,
                                          GLuint baseinstance) {
    MITHRIL_ENSURE_INIT();
    (void)basevertex; (void)baseinstance;
    glDrawElements(mode, count, type, indices);
}

void glMultiDrawArrays(GLenum mode, const GLint* first, const GLsizei* count, GLsizei drawcount) {
    MITHRIL_ENSURE_INIT();
    if (!first || !count || drawcount <= 0) return;
    for (GLsizei i = 0; i < drawcount; ++i) {
        if (count[i] > 0) glDrawArrays(mode, first[i], count[i]);
    }
}

void glMultiDrawElements(GLenum mode, const GLsizei* count, GLenum type,
                         const void* const* indices, GLsizei drawcount) {
    MITHRIL_ENSURE_INIT();
    if (!count || !indices || drawcount <= 0) return;
    for (GLsizei i = 0; i < drawcount; ++i) {
        if (count[i] > 0) glDrawElements(mode, count[i], type, indices[i]);
    }
}

/* ---- Sync objects ---- */
GLsync glFenceSync(GLenum condition, GLbitfield flags) {
    MITHRIL_ENSURE_INIT();
    (void)condition; (void)flags;
    // Return a non-null sentinel pointer. Real implementation would create a
    // VkFence/VkSemaphore; sufficient for the sync-id pattern used by most GL
    // apps (glClientWaitSync returning ALREADY_SIGNALED immediately).
    return (GLsync)0x1;
}

void glDeleteSync(GLsync sync) {
    MITHRIL_ENSURE_INIT();
    (void)sync;
}

GLenum glClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
    MITHRIL_ENSURE_INIT();
    (void)sync; (void)flags; (void)timeout;
    return GL_ALREADY_SIGNALED;
}

void glWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
    MITHRIL_ENSURE_INIT();
    (void)sync; (void)flags; (void)timeout;
}

GLboolean glIsSync(GLsync sync) {
    MITHRIL_ENSURE_INIT();
    return sync ? GL_TRUE : GL_FALSE;
}

} // extern "C"
