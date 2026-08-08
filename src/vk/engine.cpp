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

void SetClearColor(float r, float g2, float b, float a2) {
    g.clear_r = r;
    g.clear_g = g2;
    g.clear_b = b;
    g.clear_a = a2;
}

void MarkClear() {
    g.pending_clear = true;
    g.frame_dirty = true;
}

void SetViewport(float x, float y, float w, float h) {
    g.vp_x = x;
    g.vp_y = y;
    g.vp_w = w;
    g.vp_h = h;
}
} // namespace mithril::vk
