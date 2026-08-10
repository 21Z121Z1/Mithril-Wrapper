#!/usr/bin/env python3
from pathlib import Path

p = Path("src/metal/engine.mm")
s = p.read_text()

def rep(old, new, label):
    global s
    n = s.count(old)
    if n != 1:
        raise SystemExit(f"{label}: expected 1 match, got {n}")
    s = s.replace(old, new)

rep('''struct ResolvedTarget {
    std::vector<id<MTLTexture>> colors;
    std::vector<id<MTLTexture>> resolve_colors;
    id<MTLTexture> depth_stencil = nil;
    bool has_stencil = false;
    NSUInteger width = 0;
    NSUInteger height = 0;
    NSUInteger samples = 1;
};
''', '''struct AttachmentSelection {
    NSUInteger level = 0;
    NSUInteger slice = 0;
    NSUInteger depth_plane = 0;
    bool uses_depth_plane = false;
};

struct ResolvedTarget {
    std::vector<id<MTLTexture>> colors;
    std::vector<id<MTLTexture>> resolve_colors;
    std::vector<AttachmentSelection> color_selections;
    id<MTLTexture> depth_stencil = nil;
    AttachmentSelection depth_selection;
    bool has_stencil = false;
    NSUInteger width = 0;
    NSUInteger height = 0;
    NSUInteger samples = 1;
};
''', "ResolvedTarget")

rep('''bool ResolveAttachment(const backend::FboAttach& attachment,
                       id<MTLTexture>* texture, bool* is_depth = nullptr,
                       bool* has_stencil = nullptr) {
    auto& engine = GetEngine();
    if (attachment.is_texture) {
        auto found = engine.textures.find(attachment.tex_id);
        if (found == engine.textures.end() || attachment.level != 0 ||
            attachment.layer != 0)
            return false;
        *texture = found->second.texture;
        if (is_depth)
            *is_depth = found->second.format == backend::TexelFormat::Depth32Float;
        if (has_stencil) *has_stencil = false;
        return *texture != nil;
    }
    if (attachment.rbo_id) {
        auto found = engine.renderbuffers.find(attachment.rbo_id);
        if (found == engine.renderbuffers.end()) return false;
        *texture = found->second.texture;
        if (is_depth) *is_depth = found->second.depth_stencil;
        if (has_stencil) *has_stencil = found->second.has_stencil;
        return *texture != nil;
    }
    return false;
}
''', '''bool ResolveAttachment(const backend::FboAttach& attachment,
                       id<MTLTexture>* texture, AttachmentSelection* selection,
                       bool* is_depth = nullptr,
                       bool* has_stencil = nullptr) {
    if (!texture || !selection) return false;
    *texture = nil;
    *selection = {};
    auto& engine = GetEngine();
    if (attachment.is_texture) {
        auto found = engine.textures.find(attachment.tex_id);
        if (found == engine.textures.end()) return false;
        const ResidentTexture& resident = found->second;
        if (!resident.texture || resident.is_buffer ||
            attachment.level >= resident.levels)
            return false;

        selection->level = attachment.level;
        if (resident.is_multisample) {
            if (attachment.level != 0 || attachment.layer != 0) return false;
        } else if (resident.is_3d) {
            const NSUInteger depth_at_level = std::max<NSUInteger>(
                1, static_cast<NSUInteger>(resident.depth) >> attachment.level);
            if (attachment.layer >= depth_at_level) return false;
            selection->depth_plane = attachment.layer;
            selection->uses_depth_plane = true;
        } else if (resident.is_cube) {
            if (attachment.layer >= 6) return false;
            selection->slice = attachment.layer;
        } else if (resident.depth > 1) {
            if (attachment.layer >= resident.depth) return false;
            selection->slice = attachment.layer;
        } else if (attachment.layer != 0) {
            return false;
        }

        *texture = resident.texture;
        if (is_depth)
            *is_depth = resident.format == backend::TexelFormat::Depth32Float;
        if (has_stencil) *has_stencil = false;
        return true;
    }
    if (attachment.rbo_id) {
        auto found = engine.renderbuffers.find(attachment.rbo_id);
        if (found == engine.renderbuffers.end()) return false;
        *texture = found->second.texture;
        if (is_depth) *is_depth = found->second.depth_stencil;
        if (has_stencil) *has_stencil = found->second.has_stencil;
        return *texture != nil;
    }
    return false;
}
''', "ResolveAttachment")

rep('''        target->colors.push_back(engine.color);
        target->resolve_colors.push_back(nil);
        target->depth_stencil = engine.depth_stencil;
''', '''        target->colors.push_back(engine.color);
        target->resolve_colors.push_back(nil);
        target->color_selections.push_back({});
        target->depth_stencil = engine.depth_stencil;
''', "default target selection")

rep('''    for (const auto& color : found->second.spec.color) {
        id<MTLTexture> texture = nil;
        ResolveAttachment(color, &texture);
        if (!merge_sample_count(texture)) return false;
        target->colors.push_back(texture);
        id<MTLTexture> resolve = nil;
        if (color.is_texture) {
            if (engine.textures.find(color.tex_id) == engine.textures.end())
                return false;
        } else if (color.rbo_id) {
            auto renderbuffer = engine.renderbuffers.find(color.rbo_id);
            if (renderbuffer != engine.renderbuffers.end()) {
                resolve = renderbuffer->second.resolve;
            }
        }
        target->resolve_colors.push_back(resolve);
    }
    if (found->second.spec.has_depth) {
        bool depth = false;
        bool has_stencil = false;
        id<MTLTexture> depth_texture = nil;
        if (!ResolveAttachment(found->second.spec.depth,
                               &depth_texture, &depth, &has_stencil) || !depth)
            return false;
        if (!merge_sample_count(depth_texture)) return false;
        target->depth_stencil = depth_texture;
        target->has_stencil = has_stencil;
    }
    return !target->colors.empty() && target->colors.front() != nil;
''', '''    bool has_attachment = false;
    for (const auto& color : found->second.spec.color) {
        id<MTLTexture> texture = nil;
        AttachmentSelection selection;
        if (!color.is_texture && !color.rbo_id) {
            target->colors.push_back(nil);
            target->resolve_colors.push_back(nil);
            target->color_selections.push_back({});
            continue;
        }
        if (!ResolveAttachment(color, &texture, &selection)) return false;
        if (!merge_sample_count(texture)) return false;
        has_attachment = true;
        target->colors.push_back(texture);
        target->color_selections.push_back(selection);
        id<MTLTexture> resolve = nil;
        if (color.rbo_id) {
            auto renderbuffer = engine.renderbuffers.find(color.rbo_id);
            if (renderbuffer != engine.renderbuffers.end())
                resolve = renderbuffer->second.resolve;
        }
        target->resolve_colors.push_back(resolve);
    }
    if (found->second.spec.has_depth) {
        bool depth = false;
        bool has_stencil = false;
        id<MTLTexture> depth_texture = nil;
        AttachmentSelection depth_selection;
        if (!ResolveAttachment(found->second.spec.depth, &depth_texture,
                               &depth_selection, &depth, &has_stencil) || !depth)
            return false;
        if (!merge_sample_count(depth_texture)) return false;
        target->depth_stencil = depth_texture;
        target->depth_selection = depth_selection;
        target->has_stencil = has_stencil;
        has_attachment = true;
    }
    return has_attachment;
''', "ResolveTarget")

rep('''    id<MTLTexture> read_color = nil;
    if (copy_for_readback) {
''', '''    id<MTLTexture> read_color = nil;
    AttachmentSelection read_selection;
    if (copy_for_readback) {
''', "read selection declaration")

rep('''        read_color = read_target.resolve_colors[read_index]
            ? read_target.resolve_colors[read_index]
            : read_target.colors[read_index];
        if (!read_color)
            return false;
''', '''        if (read_target.resolve_colors[read_index]) {
            read_color = read_target.resolve_colors[read_index];
            read_selection = {};
        } else {
            read_color = read_target.colors[read_index];
            read_selection = read_target.color_selections[read_index];
        }
        if (!read_color) return false;
''', "read selection")

rep('''            auto* color = pass.colorAttachments[i];
            color.texture = draw_target.colors[i];
            if (draw_target.resolve_colors[i]) {
''', '''            auto* color = pass.colorAttachments[i];
            color.texture = draw_target.colors[i];
            const AttachmentSelection& selection =
                draw_target.color_selections[i];
            color.level = selection.level;
            if (selection.uses_depth_plane)
                color.depthPlane = selection.depth_plane;
            else
                color.slice = selection.slice;
            if (draw_target.resolve_colors[i]) {
''', "color render-pass selection")

rep('''        if (draw_target.depth_stencil) {
            pass.depthAttachment.texture = draw_target.depth_stencil;
            pass.depthAttachment.storeAction = MTLStoreActionStore;
''', '''        if (draw_target.depth_stencil) {
            pass.depthAttachment.texture = draw_target.depth_stencil;
            pass.depthAttachment.level = draw_target.depth_selection.level;
            if (draw_target.depth_selection.uses_depth_plane)
                pass.depthAttachment.depthPlane = draw_target.depth_selection.depth_plane;
            else
                pass.depthAttachment.slice = draw_target.depth_selection.slice;
            pass.depthAttachment.storeAction = MTLStoreActionStore;
''', "depth render-pass selection")

rep('''            if (draw_target.has_stencil) {
                pass.stencilAttachment.texture = draw_target.depth_stencil;
                pass.stencilAttachment.storeAction = MTLStoreActionStore;
''', '''            if (draw_target.has_stencil) {
                pass.stencilAttachment.texture = draw_target.depth_stencil;
                pass.stencilAttachment.level = draw_target.depth_selection.level;
                if (draw_target.depth_selection.uses_depth_plane)
                    pass.stencilAttachment.depthPlane = draw_target.depth_selection.depth_plane;
                else
                    pass.stencilAttachment.slice = draw_target.depth_selection.slice;
                pass.stencilAttachment.storeAction = MTLStoreActionStore;
''', "stencil render-pass selection")

rep('''        [blit copyFromTexture:read_color
                 sourceSlice:0
                 sourceLevel:0
                sourceOrigin:MTLOriginMake(0, 0, 0)
''', '''        [blit copyFromTexture:read_color
                 sourceSlice:read_selection.uses_depth_plane ? 0 : read_selection.slice
                 sourceLevel:read_selection.level
                sourceOrigin:MTLOriginMake(0, 0,
                    read_selection.uses_depth_plane ? read_selection.depth_plane : 0)
''', "readback subresource")

rep('''    [blit copyFromTexture:source.colors[source_index]
              sourceSlice:0 sourceLevel:0
             sourceOrigin:MTLOriginMake(source_x, source_y, 0)
               sourceSize:MTLSizeMake(source_width, source_height, 1)
                toTexture:destination.colors[destination_index]
         destinationSlice:0 destinationLevel:0
        destinationOrigin:MTLOriginMake(destination_x, destination_y, 0)];
''', '''    const AttachmentSelection& source_selection =
        source.color_selections[source_index];
    const AttachmentSelection& destination_selection =
        destination.color_selections[destination_index];
    [blit copyFromTexture:source.colors[source_index]
              sourceSlice:source_selection.uses_depth_plane ? 0 : source_selection.slice
              sourceLevel:source_selection.level
             sourceOrigin:MTLOriginMake(source_x, source_y,
                 source_selection.uses_depth_plane ? source_selection.depth_plane : 0)
               sourceSize:MTLSizeMake(source_width, source_height, 1)
                toTexture:destination.colors[destination_index]
         destinationSlice:destination_selection.uses_depth_plane
             ? 0 : destination_selection.slice
         destinationLevel:destination_selection.level
        destinationOrigin:MTLOriginMake(destination_x, destination_y,
            destination_selection.uses_depth_plane
                ? destination_selection.depth_plane : 0)];
''', "framebuffer blit subresources")

p.write_text(s)
