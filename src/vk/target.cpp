// Mithril-Wrapper Vulkan backend -- render target & resource helpers.
// Host-memory helpers (AlignUp/FindMemoryType/CreateHostBuffer),
// CreateTargetImage/TransitionLayout, and the render pass + offscreen
// target construction feeding EnsureInit (dispatch.cpp) and the
// draw path (draw.cpp) through internal.h.

#include "internal.h"

namespace mithril::vk {

VkDeviceSize AlignUp(VkDeviceSize v, VkDeviceSize a) {
    return (v + a - 1) / a * a;
}

VkResult FindMemoryType(uint32_t bits, VkMemoryPropertyFlags want,
                        uint32_t* out) {
    VkPhysicalDeviceMemoryProperties mem;
    g.fn.GetPhysicalDeviceMemoryProperties(g.physical, &mem);
    for (uint32_t i = 0; i < mem.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) &&
            (mem.memoryTypes[i].propertyFlags & want) == want) {
            *out = i;
            return VK_SUCCESS;
        }
    }
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

VkResult CreateHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                          VkBuffer* buf, VkDeviceMemory* mem) {
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (g.fn.CreateBuffer(g.device, &bi, nullptr, buf) != VK_SUCCESS)
        return VK_ERROR_INITIALIZATION_FAILED;

    VkMemoryRequirements req;
    g.fn.GetBufferMemoryRequirements(g.device, *buf, &req);
    uint32_t type = 0;
    if (FindMemoryType(req.memoryTypeBits,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       &type) != VK_SUCCESS) {
        g.fn.DestroyBuffer(g.device, *buf, nullptr);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = type;
    if (g.fn.AllocateMemory(g.device, &ai, nullptr, mem) != VK_SUCCESS) {
        g.fn.DestroyBuffer(g.device, *buf, nullptr);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    if (g.fn.BindBufferMemory(g.device, *buf, *mem, 0) != VK_SUCCESS) {
        g.fn.FreeMemory(g.device, *mem, nullptr);
        g.fn.DestroyBuffer(g.device, *buf, nullptr);
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return VK_SUCCESS;
}

VkResult CreateTargetImage(VkImage* img, VkDeviceMemory* mem) {
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = g.format;
    ii.extent = {g.width, g.height, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
               VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (g.fn.CreateImage(g.device, &ii, nullptr, img) != VK_SUCCESS)
        return VK_ERROR_INITIALIZATION_FAILED;

    VkMemoryRequirements req;
    g.fn.GetImageMemoryRequirements(g.device, *img, &req);
    uint32_t type = 0;
    if (FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                       &type) != VK_SUCCESS) {
        g.fn.DestroyImage(g.device, *img, nullptr);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = type;
    if (g.fn.AllocateMemory(g.device, &ai, nullptr, mem) != VK_SUCCESS) {
        g.fn.DestroyImage(g.device, *img, nullptr);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    if (g.fn.BindImageMemory(g.device, *img, *mem, 0) != VK_SUCCESS) {
        g.fn.FreeMemory(g.device, *mem, nullptr);
        g.fn.DestroyImage(g.device, *img, nullptr);
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return VK_SUCCESS;
}

void TransitionLayout(VkCommandBuffer cb, VkImage image,
                      VkImageLayout old_layout, VkImageLayout new_layout) {
    TransitionLayoutAspect(cb, image,
                           {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}, old_layout,
                           new_layout);
}

void TransitionLayoutAspect(VkCommandBuffer cb, VkImage image,
                            VkImageSubresourceRange range,
                            VkImageLayout old_layout,
                            VkImageLayout new_layout) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = range;
    // Coarse but correct for the milestone: everything waits on everything.
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    g.fn.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr,
                            0, nullptr, 1, &barrier);
}

// ---- render target construction --------------------------------------------
// ---------------------------------------------------------------------------

bool CreateRenderPass() {
    VkAttachmentDescription att[2]{};
    att[0].format = g.format;
    att[0].samples = VK_SAMPLE_COUNT_1_BIT;
    att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    att[1].format = g.depth_format;
    att[1].samples = VK_SAMPLE_COUNT_1_BIT;
    // Depth is cleared explicitly via CmdClearDepthStencilImage (matching the
    // color attachment's explicit clear), so the render pass must LOAD it.
    att[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    att[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference col{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference dep{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &col;
    sub.pDepthStencilAttachment = &dep;

    VkRenderPassCreateInfo ri{};
    ri.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ri.attachmentCount = 2;
    ri.pAttachments = att;
    ri.subpassCount = 1;
    ri.pSubpasses = &sub;

    return g.fn.CreateRenderPass(g.device, &ri, nullptr, &g.renderpass) ==
           VK_SUCCESS;
}

bool CreateDepthTarget() {
    if (g.depth_image) return true;
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = g.depth_format;
    ii.extent = {g.width, g.height, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (g.fn.CreateImage(g.device, &ii, nullptr, &g.depth_image) != VK_SUCCESS) {
        ML_LOG_ERROR("vk: depth image creation failed");
        return false;
    }
    VkMemoryRequirements req;
    g.fn.GetImageMemoryRequirements(g.device, g.depth_image, &req);
    uint32_t type = 0;
    if (FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                       &type) != VK_SUCCESS) {
        ML_LOG_ERROR("vk: no device-local memory for depth");
        return false;
    }
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = type;
    if (g.fn.AllocateMemory(g.device, &ai, nullptr, &g.depth_mem) !=
        VK_SUCCESS) {
        ML_LOG_ERROR("vk: depth memory allocation failed");
        return false;
    }
    if (g.fn.BindImageMemory(g.device, g.depth_image, g.depth_mem, 0) !=
        VK_SUCCESS) {
        ML_LOG_ERROR("vk: depth memory bind failed");
        return false;
    }
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = g.depth_image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = g.depth_format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT |
                               VK_IMAGE_ASPECT_STENCIL_BIT,
                           0, 1, 0, 1};
    if (g.fn.CreateImageView(g.device, &vi, nullptr, &g.depth_view) !=
        VK_SUCCESS) {
        ML_LOG_ERROR("vk: depth image view creation failed");
        return false;
    }
    g.depth_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    return true;
}

bool CreateTarget() {
    VkImage img = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    if (CreateTargetImage(&img, &mem) != VK_SUCCESS) {
        ML_LOG_ERROR("vk: target image creation failed");
        return false;
    }
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = img;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = g.format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (g.fn.CreateImageView(g.device, &vi, nullptr, &g.target_view) !=
        VK_SUCCESS) {
        ML_LOG_ERROR("vk: target view creation failed");
        g.fn.DestroyImage(g.device, img, nullptr);
        g.fn.FreeMemory(g.device, mem, nullptr);
        return false;
    }
    if (!CreateDepthTarget()) {
        ML_LOG_ERROR("vk: depth target creation failed");
        g.fn.DestroyImageView(g.device, g.target_view, nullptr);
        g.fn.DestroyImage(g.device, img, nullptr);
        g.fn.FreeMemory(g.device, mem, nullptr);
        return false;
    }
    VkImageView atts[2] = {g.target_view, g.depth_view};
    VkFramebufferCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fi.renderPass = g.renderpass;
    fi.attachmentCount = 2;
    fi.pAttachments = atts;
    fi.width = g.width;
    fi.height = g.height;
    fi.layers = 1;
    if (g.fn.CreateFramebuffer(g.device, &fi, nullptr, &g.target_fb) !=
        VK_SUCCESS) {
        ML_LOG_ERROR("vk: target framebuffer creation failed");
        g.fn.DestroyImageView(g.device, g.depth_view, nullptr);
        g.fn.DestroyImage(g.device, g.depth_image, nullptr);
        g.fn.FreeMemory(g.device, g.depth_mem, nullptr);
        g.depth_view = VK_NULL_HANDLE;
        g.depth_image = VK_NULL_HANDLE;
        g.depth_mem = VK_NULL_HANDLE;
        g.fn.DestroyImageView(g.device, g.target_view, nullptr);
        g.fn.DestroyImage(g.device, img, nullptr);
        g.fn.FreeMemory(g.device, mem, nullptr);
        return false;
    }
    g.target_image = img;
    g.target_mem = mem;
    g.target_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    return true;
}

// ---------------------------------------------------------------------------
// Pipelines
// ---------------------------------------------------------------------------

} // namespace mithril::vk
