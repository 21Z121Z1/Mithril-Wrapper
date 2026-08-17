#!/usr/bin/env python3
"""Apply phase 5: resident UInt16/UInt32 index sources.

The frontend scans native-width indices for bounds/max/sentinel validation but
avoids allocating/converting a u32 vector when Metal/Vulkan can consume the
original EBO bytes without semantic loss. Unsupported or ambiguous cases keep
the existing compatibility lowering path.
"""
from pathlib import Path
import re


def exact(path, old, new, count=1):
    p=Path(path); text=p.read_text(); actual=text.count(old)
    if actual != count:
        raise SystemExit(f"{path}: expected {count}, found {actual}: {old[:120]!r}")
    p.write_text(text.replace(old,new,count))


def regex(path, pattern, replacement, count=1):
    p=Path(path); text=p.read_text()
    result,actual=re.subn(pattern,replacement,text,count=count,flags=re.MULTILINE|re.DOTALL)
    if actual != count:
        raise SystemExit(f"{path}: regex expected {count}, found {actual}: {pattern[:120]!r}")
    p.write_text(result)

# ---------------------------------------------------------------------------
# Shared draw contract: transient u32 indices remain the compatibility path;
# the resident path borrows original UInt16/UInt32 EBO storage synchronously.
# ---------------------------------------------------------------------------
exact("src/backend/types.h",
'''struct PipelineState {
''',
'''enum class IndexScalarType : uint8_t {
    Uint16 = 0,
    Uint32 = 1,
};

struct ResidentIndexSource {
    const uint8_t* source_data = nullptr;
    size_t source_size = 0;
    uint64_t source_lifetime_id = 0;
    uint64_t source_content_version = 0;
    uint64_t source_previous_content_version = 0;
    uint64_t source_update_offset = 0;
    uint64_t source_update_size = 0;
    bool source_update_is_partial = false;
    uint64_t binding_offset = 0;
    uint32_t count = 0;
    IndexScalarType scalar_type = IndexScalarType::Uint32;

    uint32_t ScalarBytes() const {
        return scalar_type == IndexScalarType::Uint16 ? 2u : 4u;
    }
    bool HasResidentSource() const {
        return source_data != nullptr && source_size != 0 &&
               source_lifetime_id != 0 && count != 0;
    }
};

struct PipelineState {
''')
exact("src/backend/types.h",
'''    std::vector<uint32_t> indices;
    // Index values matching the GL restart index are normalized by the
''',
'''    std::vector<uint32_t> indices;
    ResidentIndexSource resident_indices;
    // Index values matching the GL restart index are normalized by the
''')

# ---------------------------------------------------------------------------
# DirectMetal index diagnostics.
# ---------------------------------------------------------------------------
exact("include/mithril/directmetal_diagnostics.h",
'''int mithrilGetDirectMetalBufferStatsV1(
    MithrilDirectMetalBufferStatsV1* output, size_t output_size);
''',
'''int mithrilGetDirectMetalBufferStatsV1(
    MithrilDirectMetalBufferStatsV1* output, size_t output_size);

#define MITHRIL_DIRECT_METAL_INDEX_STATS_VERSION 1u

typedef struct MithrilDirectMetalIndexStatsV1 {
    uint32_t version;
    uint32_t struct_size;
    uint64_t resident_index_draws;
    uint64_t transient_index_draws;
    uint64_t resident_index_bytes;
    uint64_t transient_index_bytes;
} MithrilDirectMetalIndexStatsV1;

void mithrilResetDirectMetalIndexStats(void);
int mithrilGetDirectMetalIndexStatsV1(
    MithrilDirectMetalIndexStatsV1* output, size_t output_size);
''')

# ---------------------------------------------------------------------------
# Frontend: direct-resident eligibility and scan.
# Metal always treats max(IndexType) as primitive-restart sentinel, so restart
# must be disabled and that sentinel absent. UInt16 sentinel falls back to u32
# lowering because GL can otherwise use vertex 65535; UInt32 sentinel preserves
# the existing frontend error behavior.
# ---------------------------------------------------------------------------
helper=r'''enum class ResidentIndexResult {
    NotEligible = 0,
    Ready,
    Error,
};

ResidentIndexResult TryResolveResidentIndices(
    GLenum mode, GLenum type, const void* indices, GLsizei count,
    GLuint start, GLuint end, v::ResidentIndexSource* output,
    uint32_t* max_index, GLenum* err) {
    *output = {};
    *max_index = 0;
    if (count <= 0 || g_bound_element_buffer == 0)
        return ResidentIndexResult::NotEligible;
    if (mode == GL_TRIANGLE_FAN || mode == GL_LINE_LOOP)
        return ResidentIndexResult::NotEligible;
    sh::Program* program = sh::GetProgram(s::GetState().current_program);
    if (program && program->uses_flat_fragment_inputs)
        return ResidentIndexResult::NotEligible;
    const s::GLState& state = s::GetState();
    if (state.caps.Test(GL_PRIMITIVE_RESTART))
        return ResidentIndexResult::NotEligible;

    uint32_t scalar_bytes = 0;
    v::IndexScalarType scalar_type = v::IndexScalarType::Uint32;
    uint32_t sentinel = UINT32_MAX;
    if (type == GL_UNSIGNED_SHORT) {
        scalar_bytes = 2;
        scalar_type = v::IndexScalarType::Uint16;
        sentinel = 0xFFFFu;
    } else if (type == GL_UNSIGNED_INT) {
        scalar_bytes = 4;
        scalar_type = v::IndexScalarType::Uint32;
    } else {
        return ResidentIndexResult::NotEligible;
    }

    auto found = g_buffers.find(g_bound_element_buffer);
    if (found == g_buffers.end()) {
        *err = GL_INVALID_OPERATION;
        return ResidentIndexResult::Error;
    }
    const uint64_t offset = reinterpret_cast<uintptr_t>(indices);
    const uint64_t byte_count = static_cast<uint64_t>(count) * scalar_bytes;
    if (offset % scalar_bytes != 0)
        return ResidentIndexResult::NotEligible;
    if (offset > found->second.Size() ||
        byte_count > found->second.Size() - offset) {
        *err = GL_INVALID_OPERATION;
        return ResidentIndexResult::Error;
    }
    found->second.EnsureMaterialized();
    const uint8_t* bytes = found->second.data.data() + offset;
    uint32_t maximum = 0;
    bool has_vertex = false;
    for (GLsizei i = 0; i < count; ++i) {
        uint32_t value = 0;
        if (scalar_bytes == 2) {
            uint16_t narrow = 0;
            std::memcpy(&narrow, bytes + static_cast<size_t>(i) * 2, 2);
            value = narrow;
        } else {
            std::memcpy(&value, bytes + static_cast<size_t>(i) * 4, 4);
        }
        if (value == sentinel) {
            if (type == GL_UNSIGNED_INT) {
                *err = GL_INVALID_OPERATION;
                return ResidentIndexResult::Error;
            }
            return ResidentIndexResult::NotEligible;
        }
        if (start != end && (value < start || value > end)) {
            *err = GL_INVALID_OPERATION;
            return ResidentIndexResult::Error;
        }
        maximum = std::max(maximum, value);
        has_vertex = true;
    }
    if (!has_vertex) return ResidentIndexResult::NotEligible;

    const BufferData& buffer = found->second;
    output->source_data = buffer.data.data();
    output->source_size = buffer.Size();
    output->source_lifetime_id = buffer.lifetime_id;
    output->source_content_version = buffer.content_version;
    output->source_previous_content_version = buffer.previous_content_version;
    output->source_update_offset = buffer.update_offset;
    output->source_update_size = buffer.update_size;
    output->source_update_is_partial = buffer.update_is_partial;
    output->binding_offset = offset;
    output->count = static_cast<uint32_t>(count);
    output->scalar_type = scalar_type;
    *max_index = maximum;
    return ResidentIndexResult::Ready;
}

'''
exact("src/gl/draw.cpp",
'''// Expand element indices from the bound GL_ELEMENT_ARRAY_BUFFER into raw
// uint32 (payload space, base_vertex NOT applied).
''',
helper+'''// Expand element indices from the bound GL_ELEMENT_ARRAY_BUFFER into raw
// uint32 (payload space, base_vertex NOT applied).
''')

# DrawCommon gains an optional resident-index description. Existing call sites
# use defaults and therefore retain their semantics.
exact("src/gl/draw.cpp",
'''void DrawCommon(GLenum mode, const std::vector<uint32_t>& idx, GLint first,
                GLsizei count, GLint base_vertex, GLsizei instance_count) {
''',
'''void DrawCommon(GLenum mode, const std::vector<uint32_t>& idx, GLint first,
                GLsizei count, GLint base_vertex, GLsizei instance_count,
                const v::ResidentIndexSource* resident_indices = nullptr,
                uint32_t resident_max_index = 0) {
''')
exact("src/gl/draw.cpp",
'''    GLint row_base = idx.empty() ? first : base_vertex;
    GLsizei v_count = 0;
    if (idx.empty()) {
        v_count = count;
    } else {
''',
'''    const bool indexed = !idx.empty() ||
        (resident_indices && resident_indices->HasResidentSource());
    GLint row_base = indexed ? base_vertex : first;
    GLsizei v_count = 0;
    if (resident_indices && resident_indices->HasResidentSource()) {
        v_count = static_cast<GLsizei>(resident_max_index + 1u);
    } else if (idx.empty()) {
        v_count = count;
    } else {
''')
exact("src/gl/draw.cpp",
'''    dp.indices = idx;  // raw u32 indices into the payload rows
    dp.primitive_restart = std::find(idx.begin(), idx.end(), UINT32_MAX) !=
                           idx.end();
''',
'''    dp.indices = idx;  // compatibility path: raw u32 indices into payload rows
    if (resident_indices) dp.resident_indices = *resident_indices;
    dp.primitive_restart = std::find(idx.begin(), idx.end(), UINT32_MAX) !=
                           idx.end();
''')

# Replace the common non-restart DrawElements tail with a direct-source attempt.
exact("src/gl/draw.cpp",
'''    if (!restart_enabled || !contains_restart) {
        DrawCommon(mode, idx, 0, count, base_vertex, instance_count);
        return;
    }
''',
'''    if (!restart_enabled || !contains_restart) {
        v::ResidentIndexSource resident;
        uint32_t resident_max = 0;
        GLenum resident_error = GL_NO_ERROR;
        const ResidentIndexResult resident_result = TryResolveResidentIndices(
            mode, type, indices, count, start, end,
            &resident, &resident_max, &resident_error);
        if (resident_result == ResidentIndexResult::Error) {
            PUSH_ERROR(resident_error);
            return;
        }
        if (resident_result == ResidentIndexResult::Ready) {
            DrawCommon(mode, {}, 0, count, base_vertex, instance_count,
                       &resident, resident_max);
            return;
        }
        DrawCommon(mode, idx, 0, count, base_vertex, instance_count);
        return;
    }
''')

# The replacement above currently calls LoadIndices before it can try direct.
# Rewrite DrawElementsImpl to try the resident source first, then fall back to
# the existing vector path. This removes both conversion and allocation on the
# eligible path rather than merely discarding the vector afterwards.
regex("src/gl/draw.cpp",
r'''void DrawElementsImpl\(GLenum mode, GLsizei count, GLenum type,
                      const void\* indices, GLint base_vertex,
                      GLsizei instance_count, GLuint start, GLuint end\) \{
    if \(count < 0\) \{ PUSH_ERROR\(GL_INVALID_VALUE\); return; \}
    GLenum err = GL_NO_ERROR;
    auto idx = LoadIndices\(type, indices, count, start, end, &err\);
    if \(err\) \{ PUSH_ERROR\(err\); return; \}
    const s::GLState& state = s::GetState\(\);
    const bool restart_enabled = state.caps.Test\(GL_PRIMITIVE_RESTART\);
    const bool contains_restart =
        std::find\(idx.begin\(\), idx.end\(\), UINT32_MAX\) != idx.end\(\);

    if \(!restart_enabled \|\| !contains_restart\) \{
        v::ResidentIndexSource resident;
        uint32_t resident_max = 0;
        GLenum resident_error = GL_NO_ERROR;
        const ResidentIndexResult resident_result = TryResolveResidentIndices\(
            mode, type, indices, count, start, end,
            &resident, &resident_max, &resident_error\);
        if \(resident_result == ResidentIndexResult::Error\) \{
            PUSH_ERROR\(resident_error\);
            return;
        \}
        if \(resident_result == ResidentIndexResult::Ready\) \{
            DrawCommon\(mode, \{\}, 0, count, base_vertex, instance_count,
                       &resident, resident_max\);
            return;
        \}
        DrawCommon\(mode, idx, 0, count, base_vertex, instance_count\);
        return;
    \}
''',
'''void DrawElementsImpl(GLenum mode, GLsizei count, GLenum type,
                      const void* indices, GLint base_vertex,
                      GLsizei instance_count, GLuint start, GLuint end) {
    if (count < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }

    v::ResidentIndexSource resident;
    uint32_t resident_max = 0;
    GLenum resident_error = GL_NO_ERROR;
    const ResidentIndexResult resident_result = TryResolveResidentIndices(
        mode, type, indices, count, start, end,
        &resident, &resident_max, &resident_error);
    if (resident_result == ResidentIndexResult::Error) {
        PUSH_ERROR(resident_error);
        return;
    }
    if (resident_result == ResidentIndexResult::Ready) {
        DrawCommon(mode, {}, 0, count, base_vertex, instance_count,
                   &resident, resident_max);
        return;
    }

    GLenum err = GL_NO_ERROR;
    auto idx = LoadIndices(type, indices, count, start, end, &err);
    if (err) { PUSH_ERROR(err); return; }
    const s::GLState& state = s::GetState();
    const bool restart_enabled = state.caps.Test(GL_PRIMITIVE_RESTART);
    const bool contains_restart =
        std::find(idx.begin(), idx.end(), UINT32_MAX) != idx.end();

    if (!restart_enabled || !contains_restart) {
        DrawCommon(mode, idx, 0, count, base_vertex, instance_count);
        return;
    }
''')

# ---------------------------------------------------------------------------
# DirectMetal: retain the EBO generation with the same lifetime/update machinery
# as resident VBO/UBO resources, and bind it directly at the original byte offset.
# ---------------------------------------------------------------------------
exact("src/metal/engine.mm",
'''struct PendingDraw {
    backend::DrawParams params;
    // Strong references make GL deletion safe for already-recorded work.
    id<MTLBuffer> resident_vertex = nil;
    id<MTLBuffer> resident_instance = nil;
''',
'''struct PendingDraw {
    backend::DrawParams params;
    // Strong references make GL deletion safe for already-recorded work.
    id<MTLBuffer> resident_vertex = nil;
    id<MTLBuffer> resident_instance = nil;
    id<MTLBuffer> resident_index = nil;
''')
regex("src/metal/engine.mm",
r'''(MithrilDirectMetalBufferStatsV1 EmptyBufferStats\(\) \{.*?\n\})\n\n(?=template <size_t Capacity>)''',
r'''\1

MithrilDirectMetalIndexStatsV1 EmptyIndexStats() {
    MithrilDirectMetalIndexStatsV1 stats{};
    stats.version = MITHRIL_DIRECT_METAL_INDEX_STATS_VERSION;
    stats.struct_size = static_cast<uint32_t>(sizeof(stats));
    return stats;
}

''')
exact("src/metal/engine.mm",
'''    MithrilDirectMetalBindingStatsV1 binding_stats = EmptyBindingStats();
    MithrilDirectMetalBufferStatsV1 buffer_stats = EmptyBufferStats();
''',
'''    MithrilDirectMetalBindingStatsV1 binding_stats = EmptyBindingStats();
    MithrilDirectMetalBufferStatsV1 buffer_stats = EmptyBufferStats();
    MithrilDirectMetalIndexStatsV1 index_stats = EmptyIndexStats();
''')
exact("src/metal/engine.mm",
'''    if (params.instance_stream.HasResidentSource()) {
        pending.resident_instance = RetainResidentBuffer(params.instance_stream);
        if (!pending.resident_instance) return false;
    }
    for (size_t i = 0; i < params.uniform_buffers.size(); ++i) {
''',
'''    if (params.instance_stream.HasResidentSource()) {
        pending.resident_instance = RetainResidentBuffer(params.instance_stream);
        if (!pending.resident_instance) return false;
    }
    if (params.resident_indices.HasResidentSource()) {
        const auto& source = params.resident_indices;
        const uint64_t bytes = static_cast<uint64_t>(source.count) * source.ScalarBytes();
        if (source.binding_offset > source.source_size ||
            bytes > source.source_size - source.binding_offset)
            return false;
        pending.resident_index = RetainResidentBytes(
            source.source_data, source.source_size,
            source.source_lifetime_id, source.source_content_version,
            source.source_previous_content_version,
            static_cast<size_t>(source.source_update_offset),
            static_cast<size_t>(source.source_update_size),
            source.source_update_is_partial);
        if (!pending.resident_index) return false;
    }
    for (size_t i = 0; i < params.uniform_buffers.size(); ++i) {
''')
exact("src/metal/engine.mm",
'''    pending.params.instance_stream.source_data = nullptr;
    pending.params.instance_stream.source_size = 0;
    for (auto& binding : pending.params.uniform_buffers) {
''',
'''    pending.params.instance_stream.source_data = nullptr;
    pending.params.instance_stream.source_size = 0;
    pending.params.resident_indices.source_data = nullptr;
    pending.params.resident_indices.source_size = 0;
    for (auto& binding : pending.params.uniform_buffers) {
''')

# Upload budget excludes a directly resident index stream.
exact("src/metal/engine.mm",
'''        if (draw.topology == backend::Topology::TriangleFan) {
            const NSUInteger source_count = draw.indices.empty()
''',
'''        if (pending.resident_index) {
            // Native UInt16/UInt32 EBO is bound directly; no frame-arena copy.
        } else if (draw.topology == backend::Topology::TriangleFan) {
            const NSUInteger source_count = draw.indices.empty()
''')

# Encode direct resident index before the compatibility vector staging branch.
exact("src/metal/engine.mm",
'''        std::vector<uint32_t> fan_indices;
        const std::vector<uint32_t>* indices = &draw.indices;
        if (draw.topology == backend::Topology::TriangleFan) {
            fan_indices = ExpandTriangleFan(draw);
            indices = &fan_indices;
        }
        NSUInteger index_offset = NSNotFound;
        if (!indices->empty()) {
            index_offset = AllocateUpload(frame, &cursor, indices->data(),
                                           indices->size() * sizeof(uint32_t));
            if (index_offset == NSNotFound) return false;
        }
''',
'''        std::vector<uint32_t> fan_indices;
        const std::vector<uint32_t>* indices = &draw.indices;
        id<MTLBuffer> index_buffer = pending.resident_index;
        NSUInteger index_offset = NSNotFound;
        NSUInteger index_count = 0;
        MTLIndexType index_type = MTLIndexTypeUInt32;
        if (index_buffer) {
            index_offset = static_cast<NSUInteger>(draw.resident_indices.binding_offset);
            index_count = draw.resident_indices.count;
            index_type = draw.resident_indices.scalar_type == backend::IndexScalarType::Uint16
                ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
            ++engine.index_stats.resident_index_draws;
            engine.index_stats.resident_index_bytes +=
                static_cast<uint64_t>(index_count) * draw.resident_indices.ScalarBytes();
        } else {
            if (draw.topology == backend::Topology::TriangleFan) {
                fan_indices = ExpandTriangleFan(draw);
                indices = &fan_indices;
            }
            if (!indices->empty()) {
                index_offset = AllocateUpload(frame, &cursor, indices->data(),
                                               indices->size() * sizeof(uint32_t));
                if (index_offset == NSNotFound) return false;
                index_buffer = frame.upload;
                index_count = indices->size();
                ++engine.index_stats.transient_index_draws;
                engine.index_stats.transient_index_bytes +=
                    static_cast<uint64_t>(index_count) * sizeof(uint32_t);
            }
        }
''')
exact("src/metal/engine.mm",
'''        if (index_offset != NSNotFound) {
            [encoder drawIndexedPrimitives:primitive
                                indexCount:indices->size()
                                 indexType:MTLIndexTypeUInt32
                               indexBuffer:frame.upload
                         indexBufferOffset:index_offset
                             instanceCount:instance_count];
''',
'''        if (index_buffer && index_offset != NSNotFound) {
            [encoder drawIndexedPrimitives:primitive
                                indexCount:index_count
                                 indexType:index_type
                               indexBuffer:index_buffer
                         indexBufferOffset:index_offset
                             instanceCount:instance_count];
''')

exact("src/metal/engine.mm",
'''extern "C" int mithrilGetDirectMetalBufferStatsV1(
    MithrilDirectMetalBufferStatsV1* output, size_t output_size) {
    if (!output || output_size < sizeof(*output)) return 0;
    *output = GetEngine().buffer_stats;
    return 1;
}

} // namespace mithril::metal
''',
'''extern "C" int mithrilGetDirectMetalBufferStatsV1(
    MithrilDirectMetalBufferStatsV1* output, size_t output_size) {
    if (!output || output_size < sizeof(*output)) return 0;
    *output = GetEngine().buffer_stats;
    return 1;
}

extern "C" void mithrilResetDirectMetalIndexStats(void) {
    GetEngine().index_stats = EmptyIndexStats();
}

extern "C" int mithrilGetDirectMetalIndexStatsV1(
    MithrilDirectMetalIndexStatsV1* output, size_t output_size) {
    if (!output || output_size < sizeof(*output)) return 0;
    *output = GetEngine().index_stats;
    return 1;
}

} // namespace mithril::metal
''')

# ---------------------------------------------------------------------------
# Vulkan reference consumes the same raw resident source but keeps its existing
# per-draw host staging ownership. This preserves the backend-neutral contract.
# ---------------------------------------------------------------------------
exact("src/vk/internal.h",
'''    uint32_t index_count = 0;
    bool primitive_restart = false;
''',
'''    uint32_t index_count = 0;
    VkIndexType index_type = VK_INDEX_TYPE_UINT32;
    bool primitive_restart = false;
''')
exact("src/vk/draw.cpp",
'''    op.index_count = (uint32_t)params.indices.size();
    op.primitive_restart = params.primitive_restart;
''',
'''    op.index_count = params.resident_indices.HasResidentSource()
        ? params.resident_indices.count
        : static_cast<uint32_t>(params.indices.size());
    op.index_type = params.resident_indices.HasResidentSource() &&
                    params.resident_indices.scalar_type == IndexScalarType::Uint16
        ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    op.primitive_restart = params.primitive_restart;
''')
exact("src/vk/draw.cpp",
'''    if (op.index_count &&
        !StageBytes(params.indices.data(), op.index_count * sizeof(uint32_t),
                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &op.index_buffer,
                    &op.index_mem)) {
''',
'''    const void* index_bytes = params.resident_indices.HasResidentSource()
        ? static_cast<const void*>(params.resident_indices.source_data +
                                  params.resident_indices.binding_offset)
        : static_cast<const void*>(params.indices.data());
    const size_t index_byte_count = params.resident_indices.HasResidentSource()
        ? static_cast<size_t>(params.resident_indices.count) *
              params.resident_indices.ScalarBytes()
        : static_cast<size_t>(op.index_count) * sizeof(uint32_t);
    if (op.index_count &&
        !StageBytes(index_bytes, index_byte_count,
                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &op.index_buffer,
                    &op.index_mem)) {
''')
exact("src/vk/draw.cpp",
'''                g.fn.CmdBindIndexBuffer(g.cmd, op.index_buffer, 0,
                                        VK_INDEX_TYPE_UINT32);
''',
'''                g.fn.CmdBindIndexBuffer(g.cmd, op.index_buffer, 0,
                                        op.index_type);
''')

# Register DirectMetal-only performance-shape regression. Shared draw_smoke and
# Vulkan suite validate backend-neutral indexed semantics separately.
exact("cmake/MithrilSmokeTests.cmake",
'''    directmetal_buffer_streaming_smoke
    lazy_buffer_storage_smoke)
''',
'''    directmetal_buffer_streaming_smoke
    directmetal_resident_index_smoke
    lazy_buffer_storage_smoke)
''')

# Architectural guards.
shared=Path("src/backend/types.h").read_text()
gl=Path("src/gl/draw.cpp").read_text()
metal=Path("src/metal/engine.mm").read_text()
vk=Path("src/vk/draw.cpp").read_text()
for required in (
    "struct ResidentIndexSource",
    "TryResolveResidentIndices",
    "pending.resident_index",
    "MTLIndexTypeUInt16",
    "params.resident_indices.HasResidentSource()",
):
    if required not in shared+gl+metal+vk:
        raise SystemExit(f"missing resident-index invariant: {required}")
if "DrawCommon(mode, idx, 0, count, base_vertex, instance_count);\n        return;\n    }\n\n    // Restart handling" not in gl:
    raise SystemExit("compatibility restart path shape changed unexpectedly")

print("resident UInt16/UInt32 index path applied successfully")
