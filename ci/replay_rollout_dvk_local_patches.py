#!/usr/bin/env python3
"""
Replay the DirectVulkan fixes recovered from the 2026-08-31 Codex rollout.

This intentionally implements a strict subset of Codex's apply_patch format:
only *** Update File and @@ hunks are accepted. Each old hunk must match
exactly once in the current checkout; ambiguity or drift aborts the replay.
"""
from __future__ import annotations
from pathlib import Path

PATCHES = [
r'''
*** Begin Patch
*** Update File: Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/DescriptorSet.cpp
@@
-        // FIX (mid-frame flush stale-set cache - mirror invalidate_descriptor_memo):
-        // a safe_device_wait_idle() flush submits the current buffer, re-begins the
-        // SAME slot, and (via drain_disposal_queues_except / the pre-vkCreateImage GC
-        // and delete_program_resources paths) can FREE resources that previously-
-        // allocated sets in this slot's cache still reference. Reusing such a set
-        // (setCursor already rewound to 0 above) relies on the rewrite to refresh
-        // it — which is skipped on a bail path (incomplete-set guard, DescriptorSet
-        // ~1299). Drop the whole per-slot set cache so the next bind allocates a
-        // BRAND-NEW set rewritten from current, still-valid resources.
-        pr.allocatedSets[slot].clear();
         /* The cached sets this slot owns are about to be handed out again and
          * REWRITTEN by this frame's draws, so every memo entry naming one is
@@
-            // Fix (mirrors reset_all_descriptor_pools): drop the whole per-slot
-            // set cache so the next bind allocates a BRAND-NEW set that is fully
-            // written from the current, still-valid resources. A set that may
-            // reference a destroyed view is never reused or re-bound.
+            // Keep the allocated-set cache and cursor intact. Resource
+            // invalidation can happen mid-frame; dropping ownership here would
+            // leak every set from the pool until it exhausts. The memo is still
+            // cleared, so the next bind advances the cursor and fully rewrites
+            // a distinct set. At the next fence-gated generation boundary the
+            // cursor rewinds and those sets become reusable.
             for (int j = 0; j < kDescriptorMemoSize; ++j) {
                 pr.descMemo[i][j] = DescriptorMemoEntry{};
             }
             pr.descMemoNext[i] = 0;
-            pr.allocatedSets[i].clear();
-            pr.setCursor[i] = 0;
*** End Patch
''',
r'''
*** Begin Patch
*** Update File: CMakeLists.txt
@@
 set(GLSLANG_TESTS              OFF CACHE BOOL "" FORCE)
 add_subdirectory(Mithril-Wrapper-cpp/3rdparty/glslang)
+
+# Keep the bundled glslang implementation private to libmithril.  The iOS
+# launcher also loads LWJGL's libshaderc.dylib, which carries a different
+# glslang ABI.  Exporting both implementations lets dyld interpose a vtable or
+# member function from shaderc into the statically linked compiler and causes
+# a native SIGSEGV during the first Minecraft shader compile (for example in
+# TParseContext::lValueErrorCheck).  GL/EGL entry points remain exported by the
+# main target; only the compiler implementation is hidden.
+option(MITHRIL_HIDE_GLSLANG_SYMBOLS
+    "Hide bundled glslang symbols to avoid ABI interposition with host shaderc"
+    ON)
+if(MITHRIL_HIDE_GLSLANG_SYMBOLS)
+    foreach(_mithril_glslang_target IN ITEMS
+            glslang SPIRV glslang-default-resource-limits)
+        if(TARGET ${_mithril_glslang_target})
+            target_compile_options(${_mithril_glslang_target} PRIVATE
+                -fvisibility=hidden)
+            target_compile_definitions(${_mithril_glslang_target} PRIVATE
+                GLSLANG_EXPORT=)
+        endif()
+    endforeach()
+    # Shader.cpp includes the glslang headers.  Use the same empty export
+    # macro and hidden visibility for its compiler-facing instantiations so no
+    # glslang C++ type leaks back into libmithril's dynamic export table.
+    set_source_files_properties(
+        Mithril-Wrapper-cpp/MG_Impl/Shader.cpp
+        PROPERTIES
+            COMPILE_OPTIONS "-fvisibility=hidden"
+            COMPILE_DEFINITIONS "GLSLANG_EXPORT=")
+endif()
 
 # SPIRV-Cross: used for SPIR-V reflection (spirv_cross::Compiler +
*** End Patch
''',
r'''
*** Begin Patch
*** Update File: Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/CommandStream.h
@@
 void set_active_swapchain(Swapchain* sc);
+
+// Return the tracked layout of the currently-acquired swapchain color image.
+// Image-level operations (glBlitFramebuffer / glReadPixels) run outside the
+// render-pass encoder and therefore cannot inspect EncoderState directly.
+// VK_IMAGE_LAYOUT_UNDEFINED means that no swapchain is active or that the
+// image has not been acquired yet.
+VkImageLayout active_swapchain_color_layout();
*** Update File: Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/CommandStream.cpp
@@
 void set_active_swapchain(Swapchain* sc) {
     encoder().activeSwapchain = sc;
 }
+
+VkImageLayout active_swapchain_color_layout() {
+    Swapchain* sc = encoder().activeSwapchain;
+    if (!sc || sc->currentImage < 0 ||
+        sc->currentImage >= (int)sc->images.size()) {
+        return VK_IMAGE_LAYOUT_UNDEFINED;
+    }
+    return sc->currentColorLayout;
+}
*** End Patch
''',
r'''
*** Begin Patch
*** Update File: Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/ImageOps.cpp
@@
 #include "Device.h"
 #include "Resources.h"
+#include "CommandStream.h"
@@
-void end_one_shot(OneShotCtx& c) {
+bool end_one_shot(OneShotCtx& c) {
     Backend* b = backend();
     if (!c.ok) {
         if (c.fence) { vkDestroyFence(b->device, c.fence, nullptr); c.fence = VK_NULL_HANDLE; }
         if (c.cmd)   { vkFreeCommandBuffers(b->device, b->commandPool, 1, &c.cmd); c.cmd = VK_NULL_HANDLE; }
-        return;
+        return false;
     }
-    vkEndCommandBuffer(c.cmd);
+    VkResult endRc = vkEndCommandBuffer(c.cmd);
+    if (endRc != VK_SUCCESS) {
+        MITHRIL_LOG_ERROR("vk", "one-shot vkEndCommandBuffer failed (rc=%d)",
+                          (int)endRc);
+        vkDestroyFence(b->device, c.fence, nullptr);
+        vkFreeCommandBuffers(b->device, b->commandPool, 1, &c.cmd);
+        c.cmd = VK_NULL_HANDLE;
+        c.fence = VK_NULL_HANDLE;
+        c.ok = false;
+        return false;
+    }
     VkSubmitInfo si{};
@@
-    vkQueueSubmit(b->graphicsQueue, 1, &si, c.fence);
-    vkWaitForFences(b->device, 1, &c.fence, VK_TRUE, UINT64_MAX);
+    VkResult submitRc = vkQueueSubmit(b->graphicsQueue, 1, &si, c.fence);
+    if (submitRc != VK_SUCCESS) {
+        MITHRIL_LOG_ERROR("vk", "one-shot vkQueueSubmit failed (rc=%d)",
+                          (int)submitRc);
+        if (submitRc == VK_ERROR_DEVICE_LOST) b->deviceLost = true;
+        // A failed submit does not consume the command buffer or signal the
+        // fence, so both objects are safe to release here. Never wait on an
+        // unsignaled fence after a failed submit.
+        vkDestroyFence(b->device, c.fence, nullptr);
+        vkFreeCommandBuffers(b->device, b->commandPool, 1, &c.cmd);
+        c.cmd = VK_NULL_HANDLE;
+        c.fence = VK_NULL_HANDLE;
+        c.ok = false;
+        return false;
+    }
+
+    // A broken image-layout transition can leave MoltenVK's completion fence
+    // unsignaled forever. An unbounded wait here used to pin Minecraft's render
+    // thread inside glBlitFramebuffer indefinitely. Keep the wait bounded so
+    // the backend can fail closed and expose the actual submit/fence error.
+    constexpr uint64_t kOneShotFenceTimeoutNs = 5ull * 1000ull * 1000ull * 1000ull;
+    VkResult waitRc = vkWaitForFences(b->device, 1, &c.fence, VK_TRUE,
+                                      kOneShotFenceTimeoutNs);
+    if (waitRc != VK_SUCCESS) {
+        MITHRIL_LOG_ERROR("vk", "one-shot fence wait failed (rc=%d)",
+                          (int)waitRc);
+        if (waitRc == VK_ERROR_DEVICE_LOST || waitRc == VK_TIMEOUT) {
+            b->deviceLost = true;
+        }
+        // On timeout the command buffer may still be in flight. Do not destroy
+        // or free it: doing so would be a Vulkan use-after-free. The device is
+        // marked lost so subsequent recording paths stop instead of reusing a
+        // potentially poisoned queue. The handles intentionally remain owned
+        // by the device until shutdown.
+        c.ok = false;
+        return false;
+    }
     vkDestroyFence(b->device, c.fence, nullptr);
@@
     c.cmd = VK_NULL_HANDLE;
     c.fence = VK_NULL_HANDLE;
+    c.ok = false;
+    return true;
 }
+
+struct BlitLayouts {
+    VkImageLayout initial = VK_IMAGE_LAYOUT_UNDEFINED;
+    VkImageLayout final = VK_IMAGE_LAYOUT_UNDEFINED;
+};
+
+// Resolve the actual layout of a raw image handle after the caller has flushed
+// any active render pass. User-FBO images are tracked by TextureEntry; the
+// currently acquired EGL image is tracked by the swapchain encoder. The old
+// implementation guessed COLOR_ATTACHMENT_OPTIMAL for every image, which is
+// wrong after end_render_pass() (user textures are read-only) and after
+// commit_frame() (the swapchain image is PRESENT_SRC_KHR).
+BlitLayouts resolve_blit_layouts(VkImage image) {
+    BlitLayouts out{};
+    if (image == VK_NULL_HANDLE) return out;
+
+    if (g_state && image == g_state->eglDefaultColorImage) {
+        out.initial = active_swapchain_color_layout();
+        // A freshly acquired image may still be UNDEFINED. Preserve its
+        // contents-discard semantics for the source, but leave a valid
+        // attachment layout for the next frame after a destination blit.
+        out.final = (out.initial == VK_IMAGE_LAYOUT_UNDEFINED)
+                  ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : out.initial;
+        return out;
+    }
+
+    auto& tbl = texture_table();
+    for (const auto& kv : tbl) {
+        const TextureEntry& tex = kv.second;
+        if (tex.image != image) continue;
+        out.initial = tex.currentLayout;
+        // A texture created but not uploaded has no meaningful old contents;
+        // keep the transition legal and make the blit result sampleable.
+        out.final = (out.initial == VK_IMAGE_LAYOUT_UNDEFINED)
+                  ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : out.initial;
+        return out;
+    }
+
+    // Unknown raw images are only expected from legacy callers. Keep a legal
+    // fallback while preferring a fail-closed read-only final state over the
+    // previous unconditional COLOR_ATTACHMENT_OPTIMAL guess.
+    out.initial = VK_IMAGE_LAYOUT_UNDEFINED;
+    out.final = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
+    return out;
+}
+
+void fill_blit_layout_access(VkImageLayout oldLayout, VkImageLayout newLayout,
+                             VkImageMemoryBarrier& barrier,
+                             VkPipelineStageFlags& srcStage,
+                             VkPipelineStageFlags& dstStage,
+                             VkAccessFlags srcAccess,
+                             VkAccessFlags dstAccess) {
+    switch (oldLayout) {
+        case VK_IMAGE_LAYOUT_UNDEFINED:
+            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
+            barrier.srcAccessMask = 0;
+            break;
+        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
+            srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
+            barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
+            break;
+        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
+            srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
+            barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
+            break;
+        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
+            srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
+                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
+            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
+            break;
+        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
+            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
+            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
+            break;
+        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
+            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
+            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
+            break;
+        default:
+            srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
+            barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT |
+                                    VK_ACCESS_MEMORY_WRITE_BIT;
+            break;
+    }
+
+    switch (newLayout) {
+        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
+        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
+            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
+            barrier.dstAccessMask = dstAccess;
+            break;
+        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
+            dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
+            barrier.dstAccessMask = 0;
+            break;
+        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
+            dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
+            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
+                                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
+            break;
+        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
+            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
+                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
+            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
+            break;
+        case VK_IMAGE_LAYOUT_UNDEFINED:
+            dstStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
+            barrier.dstAccessMask = 0;
+            break;
+        default:
+            dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
+            barrier.dstAccessMask = dstAccess;
+            break;
+    }
+    (void)srcAccess;
+}
*** End Patch
''',
r'''
*** Begin Patch
*** Update File: Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/ImageOps.cpp
@@
-    // Transition source to TRANSFER_SRC_OPTIMAL.
+    // Transition source to TRANSFER_SRC_OPTIMAL using the actual old layout.
     VkImageMemoryBarrier sb{};
     sb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
-    sb.srcAccessMask = (src_initial == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
-                        ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
-                        : VK_ACCESS_SHADER_READ_BIT;
-    sb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
     sb.oldLayout = src_initial;
@@
-    VkPipelineStageFlags srcStage = (src_initial == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
-                        ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
-                        : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
+    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
+    VkPipelineStageFlags srcDstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
+    fill_blit_layout_access(src_initial, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
+                            sb, srcStage, srcDstStage,
+                            VK_ACCESS_MEMORY_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);
     vkCmdPipelineBarrier(c.cmd, srcStage,
-                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
+                         srcDstStage, 0,
                          0, nullptr, 0, nullptr, 1, &sb);
@@
-    db.srcAccessMask = (dst_initial == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
-                        ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
-                        : VK_ACCESS_SHADER_READ_BIT;
-    db.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
     db.oldLayout = dst_initial;
@@
-    VkPipelineStageFlags dstStage = (dst_initial == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
-                        ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
-                        : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
+    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
+    VkPipelineStageFlags dstDstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
+    fill_blit_layout_access(dst_initial, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
+                            db, dstStage, dstDstStage,
+                            VK_ACCESS_MEMORY_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
     vkCmdPipelineBarrier(c.cmd, dstStage,
-                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
+                         dstDstStage, 0,
                          0, nullptr, 0, nullptr, 1, &db);
@@
-    sb.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
-    sb.dstAccessMask = (src_final == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
-                        ? (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)
-                        : VK_ACCESS_SHADER_READ_BIT;
     sb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
     sb.newLayout = src_final;
-    VkPipelineStageFlags srcFinalStage = (src_final == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
-                        ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
-                        : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
+    VkPipelineStageFlags srcFinalStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
+    VkPipelineStageFlags srcFinalDstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
+    fill_blit_layout_access(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, src_final,
+                            sb, srcFinalStage, srcFinalDstStage,
+                            VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT);
     vkCmdPipelineBarrier(c.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
-                         srcFinalStage, 0,
+                         srcFinalDstStage, 0,
                          0, nullptr, 0, nullptr, 1, &sb);
 
-    db.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
-    db.dstAccessMask = (dst_final == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
-                        ? (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)
-                        : VK_ACCESS_SHADER_READ_BIT;
     db.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
     db.newLayout = dst_final;
-    VkPipelineStageFlags dstFinalStage = (dst_final == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
-                        ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
-                        : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
+    VkPipelineStageFlags dstFinalStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
+    VkPipelineStageFlags dstFinalDstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
+    fill_blit_layout_access(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, dst_final,
+                            db, dstFinalStage, dstFinalDstStage,
+                            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
     vkCmdPipelineBarrier(c.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
-                         dstFinalStage, 0,
+                         dstFinalDstStage, 0,
                          0, nullptr, 0, nullptr, 1, &db);
*** End Patch
''',
r'''
*** Begin Patch
*** Update File: Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/ImageOps.cpp
@@
 void dvk_blit_images(VkImage src_image, VkFormat src_format,
@@
                          GLbitfield mask, GLenum filter,
                          int is_dst_default_fbo, int dst_height) {
-    // Both images are assumed to be in a sampling or attachment layout before
-    // the blit. The swapchain color image is in COLOR_ATTACHMENT_OPTIMAL
-    // (it was just rendered into, or will be rendered into next frame); user
-    // FBO textures are in SHADER_READ_ONLY_OPTIMAL (after an upload or a
-    // previous blit). We transition them back to those same layouts after the
-    // blit so subsequent rendering / sampling continues to work.
-    //
-    // Heuristic: if the format is a color format (not depth/stencil), assume
-    // COLOR_ATTACHMENT_OPTIMAL for the initial/final layout. This matches the
-    // common glBlitFramebuffer case (blitting between render targets that are
-    // actively being rendered into). For depth/stencil formats we would use
-    // DEPTH_STENCIL_ATTACHMENT_OPTIMAL, but depth blits are not yet supported.
-    VkImageLayout src_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
-    VkImageLayout dst_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
-    mithril::vk::blit_images_impl(src_image, src_format, src_layout, src_layout,
-                                  dst_image, dst_format, dst_layout, dst_layout,
+    mithril::vk::BlitLayouts src_layouts = mithril::vk::resolve_blit_layouts(src_image);
+    mithril::vk::BlitLayouts dst_layouts = mithril::vk::resolve_blit_layouts(dst_image);
+    static int blitLogCount = 0;
+    if (blitLogCount < 8) {
+        MITHRIL_LOG_INFO("vk", "blit_images src=%p layout=%d->%d dst=%p layout=%d->%d default=%d",
+                         (void*)src_image, (int)src_layouts.initial,
+                         (int)src_layouts.final, (void*)dst_image,
+                         (int)dst_layouts.initial, (int)dst_layouts.final,
+                         is_dst_default_fbo);
+        blitLogCount++;
+    }
+    mithril::vk::blit_images_impl(src_image, src_format,
+                                  src_layouts.initial, src_layouts.final,
+                                  dst_image, dst_format,
+                                  dst_layouts.initial, dst_layouts.final,
                                   srcX0, srcY0, srcX1, srcY1,
*** End Patch
''',
r'''
*** Begin Patch
*** Update File: Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/ImageOps.cpp
@@
-    if (vkBeginCommandBuffer(c.cmd, &bi) != VK_SUCCESS) return false;
+    if (vkBeginCommandBuffer(c.cmd, &bi) != VK_SUCCESS) {
+        vkDestroyFence(b->device, c.fence, nullptr);
+        vkFreeCommandBuffers(b->device, b->commandPool, 1, &c.cmd);
+        c.cmd = VK_NULL_HANDLE;
+        c.fence = VK_NULL_HANDLE;
+        return false;
+    }
*** End Patch
''',
]

EXPECTED_FILES = {
    "CMakeLists.txt",
    "Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/DescriptorSet.cpp",
    "Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/CommandStream.h",
    "Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/CommandStream.cpp",
    "Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/ImageOps.cpp",
}

def apply_update(path: Path, section: list[str]) -> None:
    text = path.read_text(encoding="utf-8")
    i = 0
    while i < len(section):
        if not section[i].startswith("@@"):
            if section[i].strip():
                raise RuntimeError(f"{path}: unexpected patch line: {section[i]!r}")
            i += 1
            continue
        i += 1
        hunk: list[str] = []
        while i < len(section) and not section[i].startswith("@@"):
            hunk.append(section[i])
            i += 1
        old_lines: list[str] = []
        new_lines: list[str] = []
        for line in hunk:
            if not line:
                continue
            prefix = line[0]
            body = line[1:]
            if prefix == " ":
                old_lines.append(body)
                new_lines.append(body)
            elif prefix == "-":
                old_lines.append(body)
            elif prefix == "+":
                new_lines.append(body)
            else:
                raise RuntimeError(f"{path}: unsupported hunk line: {line!r}")
        if not old_lines:
            raise RuntimeError(f"{path}: insertion hunk without exact anchor is forbidden")
        old = "\n".join(old_lines)
        new = "\n".join(new_lines)
        count = text.count(old)
        if count != 1:
            raise RuntimeError(
                f"{path}: expected exactly one old-hunk match, got {count}\n"
                f"--- old hunk ---\n{old}\n--- end ---"
            )
        text = text.replace(old, new, 1)
    path.write_text(text, encoding="utf-8")

def apply_patch(patch: str, changed: set[str]) -> None:
    lines = patch.splitlines()
    if not lines or lines[0] != "*** Begin Patch" or lines[-1] != "*** End Patch":
        raise RuntimeError("malformed apply_patch envelope")
    i = 1
    while i < len(lines) - 1:
        marker = lines[i]
        if marker.startswith("*** Update File: "):
            rel = marker[len("*** Update File: "):]
            if rel.startswith("/") or ".." in Path(rel).parts:
                raise RuntimeError(f"unsafe path in rollout patch: {rel}")
            i += 1
            start = i
            while i < len(lines) - 1 and not lines[i].startswith("*** Update File: "):
                i += 1
            section = lines[start:i]
            path = Path(rel)
            if not path.is_file():
                raise RuntimeError(f"target file missing: {rel}")
            apply_update(path, section)
            changed.add(rel)
        else:
            raise RuntimeError(f"unsupported patch directive: {marker!r}")

def semantic_gates() -> None:
    desc = Path("Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/DescriptorSet.cpp").read_text()
    cmake = Path("CMakeLists.txt").read_text()
    cmd_h = Path("Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/CommandStream.h").read_text()
    cmd_cpp = Path("Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/CommandStream.cpp").read_text()
    img = Path("Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/ImageOps.cpp").read_text()

    assert "MITHRIL_HIDE_GLSLANG_SYMBOLS" in cmake
    assert "active_swapchain_color_layout();" in cmd_h
    assert "VkImageLayout active_swapchain_color_layout()" in cmd_cpp
    assert "constexpr uint64_t kOneShotFenceTimeoutNs = 5ull" in img
    assert "BlitLayouts resolve_blit_layouts(VkImage image)" in img
    assert "fill_blit_layout_access(" in img
    assert "src_layouts.initial, src_layouts.final" in img
    assert "dst_layouts.initial, dst_layouts.final" in img
    assert "pr.allocatedSets[slot].clear();" not in desc
    invalidate = desc[desc.index("void invalidate_descriptor_memo()"):]
    invalidate = invalidate[:invalidate.index("void reset_all_descriptor_pools()")]
    assert "pr.allocatedSets[i].clear();" not in invalidate
    assert "pr.setCursor[i] = 0;" not in invalidate

def main() -> int:
    changed: set[str] = set()
    for patch in PATCHES:
        apply_patch(patch.strip("\n"), changed)
    if changed != EXPECTED_FILES:
        raise RuntimeError(f"unexpected changed-file set: {sorted(changed)}")
    semantic_gates()
    print("ROLLOUT REPLAY APPLIED:", *sorted(changed), sep="\n- ")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
