// Mithril-Wrapper Vulkan backend -- texture upload path (M4).
// GL textures arrive as CPU RGBA8 mip chains (UploadTexture); the engine
// creates a device-local VkImage, uploads the levels through a staging
// buffer in one command submission, builds a VkSampler from the GL sampler
// state, and keeps the resident image for descriptor binding at draw time.
// A 1x1 white fallback image (dummy tex) holds unbound texture units.

#include "internal.h"

#include <algorithm>
#include <cstring>

#include <util/log.h>

namespace mithril::vk {

namespace {

VkFilter ToVkFilter(TexFilter f) {
    return f == TexFilter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

VkSamplerAddressMode ToVkWrap(GLenum wrap) {
    switch (wrap) {
        case GL_CLAMP_TO_EDGE:   return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case GL_CLAMP_TO_BORDER: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        case GL_MIRRORED_REPEAT: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        default:                 return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

// One-shot staging copy: writable buffer -> image (all mip levels), then
// leave the image in SHADER_READ_ONLY_OPTIMAL for sampling.
void UploadImageData(VkImage image, const TexUpload& img) {
    uint32_t mips = (uint32_t)img.mip.size();
    if (mips == 0) return;

    VkDeviceSize total = 0;
    for (const auto& lv : img.mip) total += lv.size();

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    if (CreateHostBuffer(total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &staging,
                         &staging_mem) != VK_SUCCESS) {
        ML_LOG_ERROR("vk: texture staging allocation failed");
        return;
    }
    void* map = nullptr;
    if (g.fn.MapMemory(g.device, staging_mem, 0, VK_WHOLE_SIZE, 0, &map) ==
        VK_SUCCESS) {
        VkDeviceSize off = 0;
        for (const auto& lv : img.mip) {
            std::memcpy(static_cast<uint8_t*>(map) + off, lv.data(), lv.size());
            off += lv.size();
        }
        g.fn.UnmapMemory(g.device, staging_mem);
    }

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (g.fn.BeginCommandBuffer(g.cmd, &bi) != VK_SUCCESS) {
        g.fn.DestroyBuffer(g.device, staging, nullptr);
        g.fn.FreeMemory(g.device, staging_mem, nullptr);
        return;
    }

    // UNDEFINED -> TRANSFER_DST_OPTIMAL (all levels).
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, 1};
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        g.fn.CmdPipelineBarrier(g.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                                0, nullptr, 1, &barrier);
    }

    std::vector<VkBufferImageCopy> copies(mips);
    VkDeviceSize offset = 0;
    for (uint32_t l = 0; l < mips; ++l) {
        uint32_t w = std::max<uint32_t>(1, img.width >> l);
        uint32_t h = std::max<uint32_t>(1, img.height >> l);
        copies[l].bufferOffset = offset;
        copies[l].bufferRowLength = 0;
        copies[l].bufferImageHeight = 0;
        copies[l].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, l, 0, 1};
        copies[l].imageOffset = {0, 0, 0};
        copies[l].imageExtent = {w, h, 1};
        offset += (VkDeviceSize)img.mip[l].size();
    }
    g.fn.CmdCopyBufferToImage(g.cmd, staging, image,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              (uint32_t)copies.size(), copies.data());

    // TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL.
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, 1};
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        g.fn.CmdPipelineBarrier(g.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                                nullptr, 0, nullptr, 1, &barrier);
    }

    g.fn.EndCommandBuffer(g.cmd);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &g.cmd;
    if (g.fn.QueueSubmit(g.queue, 1, &si, g.fence) != VK_SUCCESS) {
        ML_LOG_ERROR("vk: texture upload submit failed");
    }
    g.fn.WaitForFences(g.device, 1, &g.fence, VK_TRUE, UINT64_MAX);
    g.fn.ResetFences(g.device, 1, &g.fence);

    g.fn.DestroyBuffer(g.device, staging, nullptr);
    g.fn.FreeMemory(g.device, staging_mem, nullptr);
}

} // namespace

void DestroyTexObj(TexObj& t) {
    if (t.sampler) g.fn.DestroySampler(g.device, t.sampler, nullptr);
    if (t.view) g.fn.DestroyImageView(g.device, t.view, nullptr);
    if (t.image) g.fn.DestroyImage(g.device, t.image, nullptr);
    if (t.mem) g.fn.FreeMemory(g.device, t.mem, nullptr);
    t = TexObj{};
}

void UploadTexture(uint64_t gl_id, const TexUpload& img,
                   const TexSamplerInfo& sampler) {
    if (!g.initialized || img.mip.empty()) return;

    // Replace any previous resident image for this id.
    auto it = g.textures.find(gl_id);
    if (it != g.textures.end())
        DestroyTexObj(it->second);
    else
        it = g.textures.emplace(gl_id, TexObj{}).first;

    uint32_t mips = (uint32_t)img.mip.size();

    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent = {std::max<uint32_t>(1, img.width),
                 std::max<uint32_t>(1, img.height), 1};
    ii.mipLevels = mips;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (g.fn.CreateImage(g.device, &ii, nullptr, &it->second.image) !=
        VK_SUCCESS) {
        ML_LOG_ERROR("vk: texture image creation failed");
        g.textures.erase(it);
        return;
    }

    VkMemoryRequirements req;
    g.fn.GetImageMemoryRequirements(g.device, it->second.image, &req);
    uint32_t type = 0;
    if (FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                       &type) != VK_SUCCESS) {
        ML_LOG_ERROR("vk: no device-local memory for texture");
        DestroyTexObj(it->second);
        g.textures.erase(it);
        return;
    }
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = type;
    if (g.fn.AllocateMemory(g.device, &ai, nullptr, &it->second.mem) !=
        VK_SUCCESS) {
        ML_LOG_ERROR("vk: texture memory allocation failed");
        DestroyTexObj(it->second);
        g.textures.erase(it);
        return;
    }
    if (g.fn.BindImageMemory(g.device, it->second.image, it->second.mem, 0) !=
        VK_SUCCESS) {
        ML_LOG_ERROR("vk: texture memory bind failed");
        DestroyTexObj(it->second);
        g.textures.erase(it);
        return;
    }

    // Upload pixel data (leaves the image in SHADER_READ_ONLY_OPTIMAL).
    UploadImageData(it->second.image, img);

    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = it->second.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, 1};
    if (g.fn.CreateImageView(g.device, &vi, nullptr, &it->second.view) !=
        VK_SUCCESS) {
        ML_LOG_ERROR("vk: texture image view creation failed");
        DestroyTexObj(it->second);
        g.textures.erase(it);
        return;
    }

    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = ToVkFilter(sampler.mag);
    si.minFilter = ToVkFilter(sampler.min);
    si.mipmapMode = sampler.mip ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                                : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = ToVkWrap(sampler.wrap_s);
    si.addressModeV = ToVkWrap(sampler.wrap_t);
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.mipLodBias = 0.0f;
    si.anisotropyEnable = VK_FALSE;
    si.maxAnisotropy = 1.0f;
    si.compareEnable = VK_FALSE;
    si.compareOp = VK_COMPARE_OP_ALWAYS;
    si.minLod = 0.0f;
    si.maxLod = static_cast<float>(mips - 1);
    si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    if (g.fn.CreateSampler(g.device, &si, nullptr, &it->second.sampler) !=
        VK_SUCCESS) {
        ML_LOG_WARN("vk: sampler creation failed for texture %llu",
                    (unsigned long long)gl_id);
    }
    it->second.levels = mips;
}

void CreateDummyTexture() {
    if (g.textures.find(0) != g.textures.end()) return;
    TexUpload img;
    img.width = img.height = 1;
    img.mip.push_back(
        {255, 255, 255, 255});   // 1x1 opaque white
    TexSamplerInfo info;
    info.mag = TexFilter::Linear;
    info.min = TexFilter::Linear;
    info.wrap_s = GL_REPEAT;
    info.wrap_t = GL_REPEAT;
    UploadTexture(0, img, info);
}

void DestroyResidentTexture(uint64_t gl_id) {
    auto it = g.textures.find(gl_id);
    if (it != g.textures.end()) {
        DestroyTexObj(it->second);
        g.textures.erase(it);
    }
}

TexObj* GetTexObj(uint64_t gl_id) {
    if (gl_id != 0) {
        auto it = g.textures.find(gl_id);
        if (it != g.textures.end()) return &it->second;
    }
    auto it = g.textures.find(0);
    return it == g.textures.end() ? nullptr : &it->second;
}

} // namespace mithril::vk