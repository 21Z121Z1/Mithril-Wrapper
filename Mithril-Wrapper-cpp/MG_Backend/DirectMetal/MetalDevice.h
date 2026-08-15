// Mithril-Wrapper - MG_Backend/DirectMetal/MetalDevice.h
// Metal 3 direct backend (no MoltenVK): device / command queue / frame model /
// sync serials / device-lost / UBO arena / limits.
//
// HANDLE POLICY (see MG_Backend/Backend.h): every VkBuffer / VkImage /
// VkImageView / VkSampler / VkPipeline crossing the backend boundary is an
// OPAQUE COOKIE. DirectMetal hands out pointers to the wrapper structs below
// (MetalBuffer / MetalTexture / MetalSampler / MetalPipeline), which carry the
// underlying id<MTL...> object plus the metadata blits/queries need. VkFormat
// values are the backend-neutral format TAG space shared with the frontend
// (mithril::vk::gl_internal_to_vk from DirectVulkan/FormatMap.cpp — pure
// logic, compiled once and reused by both backends).
//
// RENDERING CONVENTION — "Metal as a Vulkan-conformant rasterizer":
// The frontend's SPIR-V already carries Vulkan conventions (Shader.cpp injects
// the Z remap [−1,1]→[0,1] and the default-FBO Y flip). DirectMetal therefore
// reproduces Vulkan's rasterization exactly instead of GL's:
//   * NDC +y maps DOWNWARD on screen — implemented with a NEGATIVE-height
//     MTLViewport {x, y+h, w, −h} for every draw (same trick MoltenVK uses),
//     applied uniformly to user-FBO and default-FBO passes.
//   * Texture row 0 is the TOP row, t=0 at top (Metal native) — same as
//     Vulkan, so render-to-texture / sample-from-texture double-flips cancel
//     exactly as they do under DirectVulkan.
//   * Scissor rect is top-left origin (the frontend already feeds
//     Vulkan-convention scissors).
//   * Depth range [0,1] (Metal native — matches the injected Z remap).
// This makes the whole GL frontend work unmodified on Metal.
#ifndef MITHRIL_DIRECTMETAL_DEVICE_H
#define MITHRIL_DIRECTMETAL_DEVICE_H

#ifdef __APPLE__

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <vulkan/vulkan.h>
#include <GL/gl.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

#define MITHRIL_DMT_MAX_FRAMES_IN_FLIGHT 2

// Platform compatibility: [id<MTLBuffer> didModifyRange:] and
// MTLResourceStorageModeManaged are macOS-only APIs. On iOS (unified memory)
// buffers are always Shared and need no explicit sync.
// Wrap all didModifyRange calls in MITHRIL_DMT_SYNC so they compile out on iOS.
#if TARGET_OS_OSX
    #define MITHRIL_DMT_SYNC(mb, off, len) do { \
        if ((mb) && (mb)->managed) \
            [(mb)->buf didModifyRange:NSMakeRange((NSUInteger)(off), (NSUInteger)((len) ? (len) : 1))]; \
    } while (0)
    #define MITHRIL_DMT_STORAGE(managed) \
        ((managed) ? MTLResourceStorageModeManaged : MTLResourceStorageModeShared)
    #define MITHRIL_DMT_STORAGE_MODE(managed) \
        ((managed) ? MTLStorageModeManaged : MTLStorageModeShared)
#else
    #define MITHRIL_DMT_SYNC(mb, off, len) ((void)0)
    #define MITHRIL_DMT_STORAGE(managed) MTLResourceStorageModeShared
    #define MITHRIL_DMT_STORAGE_MODE(managed) MTLStorageModeShared
#endif

namespace mithril {
namespace dmt {

/* ---- Opaque handle wrappers (cross the C API as VkBuffer/VkImage/...) ---- */

struct MetalBuffer {
    id<MTLBuffer> buf = nil;     // nil before first (re)creation
    NSUInteger    capacity = 0;  // actual MTLBuffer length (256-aligned)
    void*         contents = nullptr; // contents pointer (shared) — null when private
    bool          managed = false;    // MTLStorageModeManaged (discrete GPU)
    bool          persistent = false; // glBufferStorage MAP_PERSISTENT
    void*         persistentHost = nullptr; // host alias the app writes through
};

struct MetalTexture {
    id<MTLTexture> tex = nil;
    VkFormat       vkFormat = VK_FORMAT_UNDEFINED; // tag space
    int            width = 0, height = 0, depth = 0;
    int            levels = 1;
    GLenum         glTarget = GL_TEXTURE_2D;
    int            samples = 1;
};

struct MetalSampler {
    id<MTLSamplerState> smp = nil;
};

struct MetalPipeline {
    id<MTLRenderPipelineState> rps = nil;
    id<MTLComputePipelineState> cps = nil;  // compute pipelines (graphics: nil)
    uint32_t wgSize[3] = {1, 1, 1};         // compute threadgroup size (reflection)
    bool   isDefaultFBO = false;   // selects Y-flip compensation at bind time
    bool   hasDepth = false;       // pass-attachment info for state application
};

inline MetalBuffer*  as_buffer(VkBuffer h)  { return (MetalBuffer*)(uintptr_t)h; }
inline MetalTexture* as_tex(VkImageView h)  { return (MetalTexture*)(uintptr_t)h; }
inline MetalTexture* as_teximg(VkImage h)   { return (MetalTexture*)(uintptr_t)h; }
inline MetalSampler* as_sampler(VkSampler h){ return (MetalSampler*)(uintptr_t)h; }
inline MetalPipeline* as_pipeline(VkPipeline h){ return (MetalPipeline*)(uintptr_t)h; }
inline VkBuffer   to_vkbuf(MetalBuffer* p)  { return (VkBuffer)(uintptr_t)p; }
inline VkImageView to_vkview(MetalTexture* p){ return (VkImageView)(uintptr_t)p; }
inline VkImage    to_vkimg(MetalTexture* p)  { return (VkImage)(uintptr_t)p; }
inline VkSampler  to_vksmp(MetalSampler* p)  { return (VkSampler)(uintptr_t)p; }
inline VkPipeline to_vkpipe(MetalPipeline* p){ return (VkPipeline)(uintptr_t)p; }

/* ---- Per-frame UBO arena (mirrors DirectVulkan UniformArena design) ----
 *
 * Why: two draws in one frame must never read the same arena bytes (the
 * glUniform()→glDraw() pattern would alias). Each upload bump-allocates a
 * fresh 256-aligned slice from the CURRENT frame slot's block chain; the slot
 * is rewound only after its last command buffer has completed.
 */
struct UboBlock {
    id<MTLBuffer> buf;
    NSUInteger    capacity = 0;
    NSUInteger    used = 0;
};
struct UboArenaSlot {
    std::vector<UboBlock> blocks;   // chain: 256 KB, 512 KB, ... capped 4 MB
};

struct UboSliceDmt {
    id<MTLBuffer> buf = nil;
    NSUInteger    offset = 0;
    NSUInteger    size = 0;
    bool          valid() const { return buf != nil; }
};

/* ---- Backend singleton ---- */

struct MetalSwapchain; // MetalSwapchain.h

struct Backend {
    bool initialized = false;
    id<MTLDevice>   device = nil;
    id<MTLCommandQueue> queue = nil;
    bool unifiedMemory = true;   // Apple Silicon — shared storage everywhere
    bool supportsFamilyApple7 = false; // 16K textures

    // Current-frame command buffer (created lazily, committed at present).
    id<MTLCommandBuffer> cmd = nil;
    int  currentFrame = 0;                 // arena slot index
    uint64_t frameGeneration = 0;          // bumped at each commit boundary
    id<MTLCommandBuffer> slotCmd[MITHRIL_DMT_MAX_FRAMES_IN_FLIGHT]; // last committed cmd per slot

    // Sync serials (glFenceSync / glClientWaitSync backing).
    std::atomic<uint64_t> submitSerial{0};       // total commits issued
    std::atomic<uint64_t> lastCompletedSerial{0};// watermark completed on GPU
    std::atomic<int>      completedFrameCount{0};// completions since startup
    std::atomic<int>      polledFrameBaseline{0};

    // Device-lost: any command-buffer error or allocator failure sets this.
    std::atomic<bool> deviceLost{false};

    // Per-frame UBO arenas.
    UboArenaSlot uboArena[MITHRIL_DMT_MAX_FRAMES_IN_FLIGHT];

    // Frame pacing: number of frames retired by the last
    // poll_completed_frames() call (consumed by EGL).
    int drainPollBaseline() {
        int now = completedFrameCount.load(std::memory_order_acquire);
        int base = polledFrameBaseline.load(std::memory_order_acquire);
        polledFrameBaseline.store(now, std::memory_order_release);
        return now - base;
    }
};

Backend* backend();

/* ---- Lifecycle (implemented in MetalDevice.mm) ---- */
bool init_device();
void shutdown_device();

/* ---- UBO arena ---- */
bool ubo_allocate(int slot, NSUInteger size, UboSliceDmt& out);
bool ubo_upload(int slot, const void* data, NSUInteger size, UboSliceDmt& out);
void ubo_rewind(int slot);            // legal only after slot's cmd completed
void ubo_shutdown_all();
constexpr NSUInteger ubo_alignment() { return 256; } // MTLBuffer offset alignment

/* ---- Command buffer lifecycle (MetalCommandStream.mm) ---- */
// Ensure the current frame's command buffer exists (lazily created). Also
// retires the slot: waits for the PREVIOUS command buffer of this slot (from
// kMax frames ago) and rewinds its arena.
bool ensure_command_buffer();
// Commit the current command buffer with serial tracking. If `present` is
// non-nil the drawable is presented first. Returns false on deviceLost.
bool commit_frame(MetalSwapchain* present);

/* ---- Device limits (BackendTypes.h MITHRIL_LIMIT_* selectors) ---- */
int device_limit(int which, int fallback);

// True when the device supports the MSL version we compile with.
uint32_t msl_version_for_device();

} // namespace dmt
} // namespace mithril

#endif // __APPLE__
#endif // MITHRIL_DIRECTMETAL_DEVICE_H
