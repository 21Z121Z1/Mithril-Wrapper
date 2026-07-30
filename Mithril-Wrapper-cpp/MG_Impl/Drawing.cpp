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
#include "../MG_Backend/DirectVulkan/Pipeline.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <unordered_map>

extern "C" {

static void prepare_draw(GLenum mode) {
    // Resolve current program + its SPIR-V.
    mithril::Program* prog = mithril::state_get_program(g_state->currentProgram);
    if (!prog || !prog->linked) return;
    // Defensive: skip draws whose shader translation produced no SPIR-V
    // (e.g. glslang failed on an unrecognised construct). Issuing the draw
    // would pass null/0 to backend_get_or_create_pipeline, which would
    // either crash on the SPIR-V pointer or fail pipeline creation silently
    // and leave the screen black. Logging once per program id keeps the log
    // readable when the host retries the same broken shader every frame.
    if (prog->vertexSpirv.empty() || prog->fragmentSpirv.empty()) {
        static GLuint last_warned = 0;
        if (last_warned != prog->id) {
            last_warned = prog->id;
            MITHRIL_LOG_WARN("gl", "prepare_draw: program %u has empty SPIR-V "
                              "(vertex=%zu fragment=%zu words); skipping draw",
                              prog->id, prog->vertexSpirv.size(),
                              prog->fragmentSpirv.size());
        }
        return;
    }

    // Log the first few draws of each program at INFO level so developers
    // can trace which shader programs are active and how many primitives
    // they draw. The counter is per-program and independent of the
    // empty-SPIR-V / missing-VBO warning throttlers.
    {
        static std::unordered_map<GLuint, int> draw_counts;
        int& c = draw_counts[prog->id];
        if (c < 5) {
            MITHRIL_LOG_WARN("gl", "prepare_draw: program=%u mode=0x%x draw#%d "
                              "(vs_spv=%zu fs_spv=%zu)",
                              prog->id, mode, c + 1,
                              prog->vertexSpirv.size(),
                              prog->fragmentSpirv.size());
            c++;
        }
    }

    // Resolve current draw FBO attachments (color + depth VkImageViews + size).
    VkImageView colors[8] = {VK_NULL_HANDLE};
    VkImageView depth_view = VK_NULL_HANDLE;
    int w = 0, h = 0;
    int color_count = mithril::collect_draw_fbo_attachments(colors, &depth_view, &w, &h);
    // Defensive: if no color attachment is bound at all (e.g. the EGL default
    // framebuffer has no swapchain yet because the surface isn't sized), skip
    // the draw. Beginning a render pass with all-null attachments produces a
    // validation error and a no-op pass on MoltenVK, so skipping is both
    // cheaper and avoids log spam.
    if (color_count <= 0) {
        bool any_color = false;
        for (int i = 0; i < 8; ++i) if (colors[i] != VK_NULL_HANDLE) { any_color = true; break; }
        if (!any_color) return;
    }

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
        // EGL default framebuffer: read the swapchain's actual color format
        // from g_state->eglDefaultColorFormat (set by install_surface_on_state
        // after each acquire). Hardcoding VK_FORMAT_B8G8R8A8_UNORM would
        // mismatch if MoltenVK picked a different surface format (e.g. RGBA8
        // or an sRGB variant), causing a pipeline-creation failure on the
        // first draw and a black screen.
        VkFormat swapchainFmt = g_state->eglDefaultColorFormat;
        if (swapchainFmt == VK_FORMAT_UNDEFINED) {
            // Fallback for headless / surfaceless mode where no swapchain is
            // attached. BGRA8Unorm matches MoltenVK's most common default.
            swapchainFmt = VK_FORMAT_B8G8R8A8_UNORM;
        }
        for (int i = 0; i < color_count; ++i) {
            if (colors[i] != VK_NULL_HANDLE) {
                color_formats[i] = swapchainFmt;
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

    // VBO existence pre-check: verify every enabled attribute's bound VBO
    // actually resolves to a live VkBuffer BEFORE touching the pipeline cache
    // or starting a render pass. If any VBO is missing (e.g. the host deleted
    // the buffer object but left the VAO attrib enabled), skip the whole draw
    // — do NOT create a pipeline or begin a render pass. Beginning a render
    // pass triggers MoltenVK's getCAMetalDrawable()/nextDrawable on the
    // swapchain image; if the draw is then aborted without a vkCmdDraw, the
    // pass-end store action still touches the IOSurface. When the drawable's
    // IOSurface is invalid/nil (pool exhaustion, surface lost, present-then-
    // release race), IOSurfaceBindAccel dereferences a null pointer →
    // SIGSEGV at +0x10. Mirrors MobileGL VulkanRenderer::SetupDraw's pre-flight
    // pattern (VulkanRenderer.cpp:4376-4410). The `last_missing_warned`
    // static is independent from the empty-SPIR-V `last_warned` above so each
    // warning class throttles per program id on its own.
    for (int i = 0; i < attrib_count; ++i) {
        MGVertexAttrib& m = attribs[i];
        if (backend_get_buffer(m.buffer_name) == VK_NULL_HANDLE) {
            static GLuint last_missing_warned = 0;
            if (last_missing_warned != prog->id) {
                last_missing_warned = prog->id;
                MITHRIL_LOG_WARN("gl", "prepare_draw: program %u missing VBO %u "
                                  "for attrib loc %d (stride %d) — skipping draw",
                                  prog->id, m.buffer_name, m.location, m.stride);
            }
            return;
        }
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
    if (pipeline == VK_NULL_HANDLE) {
        // pipeline 创建失败会静默 return，导致 draw 永不执行 → 红屏。
        // per-program 限流输出（首4+每1000），记录关键签名字段以定位失败原因。
        static std::unordered_map<GLuint, int> program_warn_count;
        int& count = program_warn_count[prog->id];
        if (count < 4 || count % 1000 == 0) {
            MITHRIL_LOG_WARN("vk", "prepare_draw: pipeline creation failed "
                              "program=%u attrib_count=%d color_count=%d "
                              "blend=%d mode=0x%x (count=%d)",
                              prog->id, attrib_count, color_count,
                              g_state->blend ? 1 : 0, mode, count);
        }
        count++;
        return;
    }

    // End the active render pass ONLY when a texture layout transition is
    // actually needed. VK_KHR_dynamic_rendering forbids recording image-
    // memory barriers (layout transitions) inside an active pass. But ending
    // and re-beginning the pass for every draw breaks Draw Coalescing —
    // MoltenVK translates each begin/end into a separate Metal encoder, and
    // rapid end/begin cycles prevent draw content from accumulating correctly
    // (root cause of red/black screen: only the clear color survives).
    //
    // MobileGL keeps the pass active across multiple draws to the same target
    // and only ends it when attachments change or a layout transition is
    // required. We match that here: check whether any bound sampler texture
    // is NOT in SHADER_READ_ONLY_OPTIMAL, or any FBO attachment is NOT in its
    // attachment-optimal layout. If so, end the pass before the barrier;
    // otherwise leave it active so begin_render_pass's coalescing logic can
    // reuse it (no end/begin, content accumulates in one Metal encoder).
    bool need_pass_end_for_transition = false;
    for (int i = 0; i < mithril::kMaxTextureUnits && !need_pass_end_for_transition; ++i) {
        GLuint tex_id = mithril::g_state->boundTextures[i];
        if (tex_id && backend_get_texture_layout(tex_id) != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            need_pass_end_for_transition = true;
        }
    }
    if (fbo && !need_pass_end_for_transition) {
        for (int i = 0; i < color_count && i < 8 && !need_pass_end_for_transition; ++i) {
            GLuint t = fbo->colors[i].texture;
            if (t && backend_get_texture_layout(t) != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
                need_pass_end_for_transition = true;
            }
        }
        if (!need_pass_end_for_transition && fbo->depth.texture) {
            if (backend_get_texture_layout(fbo->depth.texture) != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
                need_pass_end_for_transition = true;
            }
        }
    }
    if (need_pass_end_for_transition) {
        backend_end_render_pass();
    }

    // ---- 节流诊断快照（每 60 帧一次，约 1 秒 1 次）----
    // 避免每 draw 刷屏。仅在采样帧打印 boundTextures + FBO attachments
    // 的当前布局，用于定位红屏/黑屏根因（纹理布局错误会导致 MoltenVK
    // 采样到垃圾数据 -> 只显示 clear color）。
    {
        static uint64_t frameCounter = 0;
        frameCounter++;
        if (frameCounter % 60 == 1) {
            // Summary 行
            MITHRIL_LOG_INFO("draw", "prepare_draw snapshot #%llu: fbo=%s color_count=%d need_pass_end=%d",
                              (unsigned long long)frameCounter,
                              fbo ? "yes" : "no", color_count,
                              (int)need_pass_end_for_transition);
            // Trigger 行：明确列出触发 need_pass_end 的 slot/attachment
            if (need_pass_end_for_transition) {
                for (int i = 0; i < mithril::kMaxTextureUnits; ++i) {
                    GLuint tex_id = mithril::g_state->boundTextures[i];
                    if (tex_id && backend_get_texture_layout(tex_id) != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                        MITHRIL_LOG_INFO("draw", "  trigger: tex[%d]=%u layout=%u (expect SHADER_READ_ONLY=5)",
                                          i, tex_id, (unsigned)backend_get_texture_layout(tex_id));
                    }
                }
                if (fbo) {
                    for (int i = 0; i < color_count && i < 8; ++i) {
                        GLuint t = fbo->colors[i].texture;
                        if (t && backend_get_texture_layout(t) != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
                            MITHRIL_LOG_INFO("draw", "  trigger: fbo.color[%d]=%u layout=%u (expect COLOR_ATTACHMENT=2)",
                                              i, t, (unsigned)backend_get_texture_layout(t));
                        }
                    }
                    if (fbo->depth.texture && backend_get_texture_layout(fbo->depth.texture) != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
                        MITHRIL_LOG_INFO("draw", "  trigger: fbo.depth=%u layout=%u (expect DEPTH_STENCIL=3)",
                                          fbo->depth.texture, (unsigned)backend_get_texture_layout(fbo->depth.texture));
                    }
                }
            }
            // 全 slot 摘要行：列出所有非零纹理的 (slot, name, layout)
            for (int i = 0; i < mithril::kMaxTextureUnits; ++i) {
                GLuint tex_id = mithril::g_state->boundTextures[i];
                if (tex_id) {
                    MITHRIL_LOG_INFO("draw", "  tex[%d]=%u layout=%u",
                                      i, tex_id, (unsigned)backend_get_texture_layout(tex_id));
                }
            }
            // fbo attachments 摘要
            if (fbo) {
                for (int i = 0; i < color_count && i < 8; ++i) {
                    GLuint t = fbo->colors[i].texture;
                    if (t) {
                        MITHRIL_LOG_INFO("draw", "  fbo.color[%d]=%u layout=%u",
                                          i, t, (unsigned)backend_get_texture_layout(t));
                    }
                }
                if (fbo->depth.texture) {
                    MITHRIL_LOG_INFO("draw", "  fbo.depth=%u layout=%u",
                                      fbo->depth.texture, (unsigned)backend_get_texture_layout(fbo->depth.texture));
                }
            }
        }
    }

    // ---- A1 代码级 validation: 纹理布局与 sampler 用途匹配 ----
    // 遍历当前 program 反射出的 sampled-image bindings，检查其绑定的纹理
    // layout 是否为 SHADER_READ_ONLY_OPTIMAL。MoltenVK 在 layout 不匹配时
    // 采样到垃圾数据 -> 只显示 clear color (红屏根因)。
    // 因 libMoltenVK.dylib 无 Vulkan Loader，无法用 Khronos validation layer，
    // 此检查作为代码级 validation 让违规可见。
    {
        auto& tbl = mithril::vk::program_table();
        auto pit = tbl.find(g_state->currentProgram);
        if (pit != tbl.end()) {
            for (const auto& db : pit->second.bindings) {
                if (db.type != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) continue;
                GLuint tex_id = (db.binding < (uint32_t)mithril::kMaxTextureUnits)
                                ? mithril::g_state->boundTextures[db.binding] : 0;
                if (!tex_id) continue;
                VkImageLayout layout = backend_get_texture_layout(tex_id);
                if (layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                    // FNV-1a hash 去重：前 4 次全输出，之后每 1000 次摘要
                    static std::atomic<uint64_t> lastViolHash{0};
                    static std::atomic<uint64_t> violCount{0};
                    uint64_t h = 1469598103934665603ull;
                    h ^= (uint64_t)db.binding; h *= 1099511628211ull;
                    h ^= (uint64_t)tex_id; h *= 1099511628211ull;
                    h ^= (uint64_t)layout; h *= 1099511628211ull;
                    if (h == lastViolHash.load(std::memory_order_relaxed)) {
                        uint64_t n = violCount.fetch_add(1, std::memory_order_relaxed) + 1;
                        if (n <= 4) {
                            MITHRIL_LOG_ERROR("draw", "validation: sampler binding %d tex=%u layout=%u (expect 5 SHADER_READ_ONLY_OPTIMAL) — MoltenVK will sample garbage",
                                              db.binding, tex_id, (unsigned)layout);
                        } else if (n % 1000 == 0) {
                            MITHRIL_LOG_ERROR("draw", "validation: sampler binding %d tex=%u layout=%u (expect 5 SHADER_READ_ONLY) [repeated %llu times]",
                                              db.binding, tex_id, (unsigned)layout, (unsigned long long)n);
                        }
                    } else {
                        uint64_t prev = violCount.exchange(1, std::memory_order_relaxed);
                        lastViolHash.store(h, std::memory_order_relaxed);
                        if (prev > 4) {
                            MITHRIL_LOG_ERROR("draw", "validation: previous sampler layout violation repeated %llu times total",
                                              (unsigned long long)prev);
                        }
                        MITHRIL_LOG_ERROR("draw", "validation: sampler binding %d tex=%u layout=%u (expect 5 SHADER_READ_ONLY_OPTIMAL) — MoltenVK will sample garbage",
                                          db.binding, tex_id, (unsigned)layout);
                    }
                }
                // Feedback loop 检查：纹理同时是 fbo attachment
                if (fbo && layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
                    bool isAttachment = false;
                    for (int i = 0; i < color_count && i < 8 && !isAttachment; ++i) {
                        if (fbo->colors[i].texture == tex_id) isAttachment = true;
                    }
                    if (isAttachment) {
                        MITHRIL_LOG_WARN("draw", "validation: tex %u is both sampler binding %d and fbo color attachment (feedback loop)",
                                          tex_id, db.binding);
                    }
                }
            }
        }
    }

    // ---- Texture layout transitions (before begin_render_pass) ----
    // MoltenVK samples garbage when a descriptor's imageLayout doesn't match
    // the texture's actual layout. Two transitions are needed:
    //
    // (1) Sampled textures -> SHADER_READ_ONLY_OPTIMAL: textures that were
    //     recently rendered to as FBO color attachments are in
    //     COLOR_ATTACHMENT_OPTIMAL. The descriptor (DescriptorSet.cpp) hard-
    //     codes imageLayout=SHADER_READ_ONLY_OPTIMAL but never records the
    //     barrier. Without this transition, MoltenVK reads garbage/transparent
    //     texels -> only the clear color is visible ("red screen").
    // (2) FBO attachment textures -> COLOR_ATTACHMENT_OPTIMAL /
    //     DEPTH_STENCIL_ATTACHMENT_OPTIMAL: begin_render_pass only barriers
    //     the swapchain image, not user-FBO attachment textures. Transition
    //     them here so TextureEntry::currentLayout stays accurate, enabling
    //     transition (1) to fire when the texture is later sampled.
    //
    // All transitions are recorded BEFORE begin_render_pass (outside the
    // dynamic-rendering pass) to avoid MoltenVK Metal encoding issues with
    // barriers inside a pass — consistent with the texture_upload pass-end
    // strategy. transition_image_layout is a no-op when already in the target
    // layout, so the common case (texture already SHADER_READ_ONLY) is cheap.
    // Order: samplers first, then attachments — for a feedback-loop texture
    // (both sampled and attached), the attachment layout wins, matching GL
    // feedback-loop undefined-behavior expectations.
    for (int i = 0; i < mithril::kMaxTextureUnits; ++i) {
        GLuint tex_id = mithril::g_state->boundTextures[i];
        if (tex_id) {
            backend_transition_texture_layout(tex_id,
                                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }
    if (fbo) {
        for (int i = 0; i < color_count && i < 8; ++i) {
            GLuint t = fbo->colors[i].texture;
            if (t) {
                backend_transition_texture_layout(t,
                                                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            }
        }
        if (fbo->depth.texture) {
            backend_transition_texture_layout(fbo->depth.texture,
                                              VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        }
    }

    // Begin render pass. loadOp is determined by pending clear flags
    // (set by deferred glClear). backend_set_load_load is a no-op now.
    backend_set_load_load();
    backend_begin_render_pass(colors, color_count, depth_view, w, h, 1);

    // Bind pipeline + set dynamic state via vkCmdSet*.
    backend_bind_pipeline(pipeline);
    // Bind the program's descriptor set (UBOs + sampled images) immediately
    // after the pipeline so the shader's uniform/texture bindings are live for
    // the upcoming draw. The set is built per-draw from Program.uniforms +
    // g_state->boundTextures by DescriptorSet.cpp.
    backend_bind_program_descriptors(prog->id);
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
    // unbound memory. The pre-check above guarantees every enabled attrib
    // has a live VBO here, so no missing-VBO handling is needed in this loop.
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
    // Bind the zero buffer to any slot 0..15 not covered above. These slots
    // have no enabled attribute, so the pipeline's binding for them (if any)
    // is a stride==0 dummy — binding the zero buffer there is safe.
    if (zero_buf != VK_NULL_HANDLE) {
        for (int loc = 0; loc < 16; ++loc) {
            if (!bound_slots[loc]) {
                backend_set_vertex_buffer(loc, zero_buf, 0);
            }
        }
    }

    // Uniform buffers and sampled-image bindings are now sourced + bound via
    // the descriptor set in backend_bind_program_descriptors() (called above,
    // right after backend_bind_pipeline). It reflects the program's SPIR-V,
    // maps each UBO to Program.uniforms[name].value and each sampler binding B
    // to g_state->boundTextures[B], and writes + binds a fresh VkDescriptorSet
    // for this draw. The legacy backend_set_fragment_buffer /
    // backend_set_fragment_texture stubs are no-ops (kept only for the C API
    // contract) — descriptor binding is centralised in DescriptorSet.cpp.
}

static void end_draw(void) {
    // Do NOT end the render pass here. Keeping the pass active allows
    // subsequent draws to the same target to coalesce into a single
    // dynamic-rendering pass (matching MobileGL's approach). The pass
    // is ended by: (a) begin_render_pass when attachments change,
    // (b) commit_frame/eglSwapBuffers at frame end, or (c) texture
    // upload / glBlitFramebuffer which need the pass ended.
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
