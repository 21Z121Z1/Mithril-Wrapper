// Mithril-Wrapper - MG_Backend/DirectVulkan/UniformArena.cpp
// Implementation of the per-frame-in-flight transient uniform arena.
// See UniformArena.h for the rationale (GPU/CPU aliasing hazard) and the
// growth/lifetime rules.
#include "UniformArena.h"

#include "Device.h"
#include "Resources.h"   // find_memory_type / try_allocate_memory_with_gc
#include "../../MG_Impl/Log.h"

#include <cstring>
#include <vector>

namespace mithril {
namespace vk {

namespace {

/* Block sizing.
 *
 * 256 KB holds ~1000 typical Minecraft global blocks (a terrain shader's
 * mithril_GlobalBlock is 150-250 bytes once std140-padded), so the common
 * frame never allocates a second block. Growth doubles up to 4 MB so a
 * pathological frame (Iris deferred passes with many programs) reaches
 * capacity in a handful of vkAllocateMemory calls rather than hundreds —
 * MoltenVK's maxMemoryAllocationCount is the scarce resource here, not bytes.
 *
 * kMaxBlocksPerSlot bounds the worst case at 256K+512K+1M+2M+4M*12 ≈ 51 MB per
 * slot. Beyond that an upload fails rather than growing without bound; the
 * caller skips the binding for that draw. Chosen to be reachable only by a
 * runaway, not by any real workload.
 */
constexpr VkDeviceSize kInitialBlockSize = 256u * 1024u;
constexpr VkDeviceSize kMaxBlockSize     = 4u * 1024u * 1024u;
constexpr size_t       kMaxBlocksPerSlot = 16;

struct ArenaBlock {
    VkBuffer       buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize   size   = 0;
    VkDeviceSize   cursor = 0;
    void*          mapped = nullptr;
};

struct ArenaSlot {
    std::vector<ArenaBlock> blocks;
    size_t activeBlock = 0;
};

struct ArenaState {
    ArenaSlot slots[kMaxFramesInFlight];
    VkDeviceSize alignment = 0;      // 0 = not yet resolved
    uint32_t     maxDynamicUbos = 0; // 0 = not yet resolved
    bool         oomLogged = false;
};

ArenaState& arena() {
    static ArenaState s;
    return s;
}

// Round `v` up to a multiple of `a` (a must be a power of two, which every
// Vulkan alignment limit is).
inline VkDeviceSize align_up(VkDeviceSize v, VkDeviceSize a) {
    return (v + a - 1) & ~(a - 1);
}

// Create one host-visible, coherent, persistently mapped block of `size`.
bool create_block(ArenaBlock& out, VkDeviceSize size) {
    Backend* b = backend();
    if (!b->device) return false;

    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(b->device, &bci, nullptr, &out.buffer) != VK_SUCCESS) {
        out.buffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(b->device, out.buffer, &req);

    // HOST_VISIBLE|HOST_COHERENT: the arena is written by the CPU every frame
    // and read by the GPU without an explicit flush. On Apple silicon this is
    // shared memory, so there is no staging copy to pay for.
    const uint32_t mt = find_memory_type(
        req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt == UINT32_MAX) {
        vkDestroyBuffer(b->device, out.buffer, nullptr);
        out.buffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = mt;
    if (try_allocate_memory_with_gc(b->device, &ai, nullptr, &out.memory) != VK_SUCCESS) {
        vkDestroyBuffer(b->device, out.buffer, nullptr);
        out.buffer = VK_NULL_HANDLE;
        out.memory = VK_NULL_HANDLE;
        return false;
    }
    b->currentAllocationCount++;

    if (vkBindBufferMemory(b->device, out.buffer, out.memory, 0) != VK_SUCCESS ||
        vkMapMemory(b->device, out.memory, 0, VK_WHOLE_SIZE, 0, &out.mapped) != VK_SUCCESS) {
        vkFreeMemory(b->device, out.memory, nullptr);
        vkDestroyBuffer(b->device, out.buffer, nullptr);
        if (b->currentAllocationCount) b->currentAllocationCount--;
        out.buffer = VK_NULL_HANDLE;
        out.memory = VK_NULL_HANDLE;
        out.mapped = nullptr;
        return false;
    }

    out.size = size;
    out.cursor = 0;
    return true;
}

void destroy_block(ArenaBlock& blk) {
    Backend* b = backend();
    if (!b->device) return;
    if (blk.memory) {
        vkUnmapMemory(b->device, blk.memory);
        vkFreeMemory(b->device, blk.memory, nullptr);
        if (b->currentAllocationCount) b->currentAllocationCount--;
    }
    if (blk.buffer) vkDestroyBuffer(b->device, blk.buffer, nullptr);
    blk = ArenaBlock{};
}

} // namespace

VkDeviceSize ubo_arena_alignment() {
    ArenaState& a = arena();
    if (a.alignment != 0) return a.alignment;
    Backend* b = backend();
    VkDeviceSize align = b->props.limits.minUniformBufferOffsetAlignment;
    if (align == 0) align = 16;
    // std140 wants 16-byte granularity anyway, and a 0/1 alignment from a
    // half-initialised props struct would make offsets useless.
    if (align < 16) align = 16;
    a.alignment = align;
    return align;
}

uint32_t ubo_arena_max_dynamic_ubos() {
    ArenaState& a = arena();
    if (a.maxDynamicUbos != 0) return a.maxDynamicUbos;
    Backend* b = backend();
    const VkPhysicalDeviceLimits& l = b->props.limits;
    uint32_t cap = l.maxDescriptorSetUniformBuffersDynamic;
    // A dynamic UBO also consumes a per-stage uniform-buffer slot; the tighter
    // of the two is the real ceiling.
    if (l.maxPerStageDescriptorUniformBuffers &&
        l.maxPerStageDescriptorUniformBuffers < cap) {
        cap = l.maxPerStageDescriptorUniformBuffers;
    }
    // props may not be populated yet (arena used before init_device filled it).
    // 8 is the Vulkan spec minimum for maxDescriptorSetUniformBuffersDynamic
    // and also what MoltenVK reports on the Apple GPUs we target, so it is the
    // safe assumption rather than an optimistic one.
    if (cap == 0) cap = 8;
    a.maxDynamicUbos = cap;
    return cap;
}

bool ubo_arena_allocate(int slot, VkDeviceSize size, UboSlice& out) {
    out = UboSlice{};
    if (slot < 0 || slot >= kMaxFramesInFlight || size == 0) return false;
    Backend* b = backend();
    if (!b->device) return false;

    ArenaState& a = arena();
    ArenaSlot& s = a.slots[slot];
    const VkDeviceSize align = ubo_arena_alignment();

    // Walk forward from the active block. Blocks before it are already full
    // for this frame; blocks after it are recycled leftovers from previous
    // frames and are reused in order.
    for (;;) {
        if (s.activeBlock < s.blocks.size()) {
            ArenaBlock& blk = s.blocks[s.activeBlock];
            const VkDeviceSize offset = align_up(blk.cursor, align);
            if (offset + size <= blk.size) {
                blk.cursor = offset + size;
                out.buffer = blk.buffer;
                out.offset = offset;
                out.size = size;
                out.mapped = static_cast<uint8_t*>(blk.mapped) + offset;
                return true;
            }
            // Doesn't fit — retire this block for the rest of the frame and
            // move on. The bytes already handed out from it stay valid.
            s.activeBlock++;
            continue;
        }

        // Need a brand new block.
        if (s.blocks.size() >= kMaxBlocksPerSlot) {
            if (!a.oomLogged) {
                a.oomLogged = true;
                MITHRIL_LOG_WARN("vk", "ubo_arena: slot %d hit the %zu-block cap; "
                                 "uniform upload of %llu bytes dropped",
                                 slot, kMaxBlocksPerSlot, (unsigned long long)size);
            }
            return false;
        }
        VkDeviceSize blockSize = kInitialBlockSize;
        if (!s.blocks.empty()) {
            blockSize = s.blocks.back().size * 2;
            if (blockSize > kMaxBlockSize) blockSize = kMaxBlockSize;
        }
        // A single oversized uniform block must still fit.
        if (blockSize < align_up(size, align)) blockSize = align_up(size, align);

        ArenaBlock blk{};
        if (!create_block(blk, blockSize)) {
            if (!a.oomLogged) {
                a.oomLogged = true;
                MITHRIL_LOG_WARN("vk", "ubo_arena: failed to create a %llu-byte block "
                                 "for slot %d; uniform uploads will be dropped",
                                 (unsigned long long)blockSize, slot);
            }
            return false;
        }
        s.blocks.push_back(blk);
        // Loop around; the new back() is now s.blocks[s.activeBlock].
    }
}

bool ubo_arena_upload(int slot, const void* data, VkDeviceSize size, UboSlice& out) {
    if (!ubo_arena_allocate(slot, size, out)) return false;
    if (data && out.mapped) std::memcpy(out.mapped, data, static_cast<size_t>(size));
    return true;
}

void ubo_arena_rewind(int slot) {
    if (slot < 0 || slot >= kMaxFramesInFlight) return;
    ArenaSlot& s = arena().slots[slot];
    s.activeBlock = 0;
    for (auto& blk : s.blocks) blk.cursor = 0;
}

void ubo_arena_shutdown() {
    ArenaState& a = arena();
    for (int i = 0; i < kMaxFramesInFlight; ++i) {
        for (auto& blk : a.slots[i].blocks) destroy_block(blk);
        a.slots[i].blocks.clear();
        a.slots[i].activeBlock = 0;
    }
    a.alignment = 0;
    a.maxDynamicUbos = 0;
    a.oomLogged = false;
}

} // namespace vk
} // namespace mithril
