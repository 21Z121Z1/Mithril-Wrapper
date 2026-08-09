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

void SetClearColor(float r, float g2, float b, float a2) {
    g.clear_r = r;
    g.clear_g = g2;
    g.clear_b = b;
    g.clear_a = a2;
}

void SetClearMask(GLbitfield mask) {
    g.pending_clear = true;
    g.frame_dirty = true;
    g.clear_mask = mask;
}

void SetClearDepth(double depth) { g.clear_depth = depth; }
void SetClearStencil(GLint value) { g.clear_stencil = value; }

void SetViewport(float x, float y, float w, float h) {
    g.vp_x = x;
    g.vp_y = y;
    g.vp_w = w;
    g.vp_h = h;
}

void SetScissor(float x, float y, float w, float h) {
    g.sc_x = x;
    g.sc_y = y;
    g.sc_w = w;
    g.sc_h = h;
}
} // namespace mithril::vk
