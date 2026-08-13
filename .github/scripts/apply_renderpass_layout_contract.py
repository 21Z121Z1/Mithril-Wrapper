from pathlib import Path

p = Path('Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/CommandStream.cpp')
s = p.read_text()
old = '''VkImageLayout choose_initial_layout(VkFormat format, LoadStoreAction action) {
    if (action == LoadStoreAction::Clear || action == LoadStoreAction::DontCare) {
        return VK_IMAGE_LAYOUT_UNDEFINED;
    }
    return sampled_layout_for_format(format);
}
'''
new = '''VkImageLayout choose_initial_layout(VkFormat format, LoadStoreAction action) {
    if (action == LoadStoreAction::Clear || action == LoadStoreAction::DontCare) {
        return VK_IMAGE_LAYOUT_UNDEFINED;
    }
    // begin_render_pass() explicitly transitions every live attachment to its
    // attachment-optimal layout before vkCmdBeginRenderPass. Vulkan requires a
    // non-UNDEFINED VkAttachmentDescription::initialLayout to equal the image's
    // actual layout at render-pass begin (VUID-*-initialLayout-00900/03100).
    // Declaring SHADER_READ_ONLY here after that explicit barrier was a layout
    // contract violation, particularly hazardous for depth-only passes.
    return attachment_layout_for_format(format);
}
'''
if s.count(old) != 1:
    raise SystemExit(f'choose_initial_layout matches={s.count(old)}')
s = s.replace(old, new, 1)
old = '''    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
'''
new = '''    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    // Cover both attachment classes. A color-only dependency leaves a
    // depth-only subpass without the intended external execution/memory edge.
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                       VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep.dstStageMask = dep.srcStageMask;
    dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
'''
if s.count(old) != 1:
    raise SystemExit(f'subpass dependency matches={s.count(old)}')
p.write_text(s.replace(old, new, 1))
