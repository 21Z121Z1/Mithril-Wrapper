// Mithril-Wrapper Vulkan backend -- public API shell.
// Owns the engine globals (Engine g / g_programs / g_pipelines) and the
// small state accessors; the heavy lifting lives in dispatch.cpp /
// target.cpp / pipeline.cpp / draw.cpp.

#include "internal.h"

namespace mithril::vk {

// Engine globals (declared extern in internal.h).
Engine g;
std::unordered_map<uint64_t, Program> g_programs;
std::unordered_map<std::string, VkPipeline> g_pipelines;

bool IsInitialized() { return g.initialized; }

// Vulkan's current compatibility path stages resolved bytes per draw and has
// no cross-draw resident buffer cache to retire yet.
void DestroyBuffer(uint64_t) {}

bool SetTargetSize(uint32_t w, uint32_t h) {
    if (!g.initialized) return false;
    if (w == g.width && h == g.height) return true;
    g.fn.DestroyFramebuffer(g.device, g.target_fb, nullptr);
    g.fn.DestroyImageView(g.device, g.target_view, nullptr);
    g.fn.DestroyImage(g.device, g.target_image, nullptr);
    g.fn.FreeMemory(g.device, g.target_mem, nullptr);
    g.width = w;
    g.height = h;
    return CreateTarget();
}

uint32_t TargetWidth() { return g.width; }
uint32_t TargetHeight() { return g.height; }

bool Clear(const ClearParams& params) {
    // A clear is ordered with previous draws.  Submit the old batch first;
    // the reference backend's queue behavior is otherwise unchanged.
    if (g.frame_dirty) SubmitFlush();
    const bool partial_color =
        params.color_write[0] != params.color_write[1] ||
        params.color_write[0] != params.color_write[2] ||
        params.color_write[0] != params.color_write[3];
    const bool partial_stencil =
        (params.mask & GL_STENCIL_BUFFER_BIT) &&
        params.stencil_write_mask != 0 &&
        params.stencil_write_mask != 0xFFFFFFFFu;
    if (partial_color || partial_stencil) {
        ML_LOG_ERROR("vk: partial channel/stencil clear is unsupported");
        return false;
    }
    g.clear = params;
    g.pending_clear = true;
    g.frame_dirty = true;
    return true;
}

} // namespace mithril::vk
