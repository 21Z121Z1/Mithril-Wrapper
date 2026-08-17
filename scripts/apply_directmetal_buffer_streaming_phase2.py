#!/usr/bin/env python3
"""Apply the DirectMetal resident-buffer generation streaming phase.

This is a temporary fail-closed patcher used by the macOS verifier.  Every
rewrite is anchored to the current integration/directmetal-next source shape;
source drift aborts instead of producing a best-effort patch.
"""
from pathlib import Path
import re


def exact(path: str, old: str, new: str, count: int = 1) -> None:
    p = Path(path)
    text = p.read_text()
    actual = text.count(old)
    if actual != count:
        raise SystemExit(
            f"{path}: expected {count} exact match(es), found {actual}: {old[:100]!r}"
        )
    p.write_text(text.replace(old, new, count))


def regex(path: str, pattern: str, replacement: str, count: int = 1) -> None:
    p = Path(path)
    text = p.read_text()
    result, actual = re.subn(
        pattern, replacement, text, count=count, flags=re.MULTILINE | re.DOTALL
    )
    if actual != count:
        raise SystemExit(
            f"{path}: expected {count} regex match(es), found {actual}: {pattern[:100]!r}"
        )
    p.write_text(result)


def in_function(path: str, start: str, end: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    begin = text.index(start)
    finish = text.index(end, begin)
    segment = text[begin:finish]
    actual = segment.count(old)
    if actual != 1:
        raise SystemExit(
            f"{path}:{start}: expected 1 match, found {actual}: {old!r}"
        )
    segment = segment.replace(old, new, 1)
    p.write_text(text[:begin] + segment + text[finish:])


# ---------------------------------------------------------------------------
# Frontend: retain an exact description of the most recent buffer mutation.
# If several mutations happen before DirectMetal observes the buffer, the
# previous-version check below deliberately fails and the backend falls back
# to a full copy rather than preserving from the wrong native generation.
# ---------------------------------------------------------------------------
exact(
    "src/gl/internal.h",
    "    uint64_t content_version = 0;\n",
    "    uint64_t content_version = 0;\n"
    "    uint64_t previous_content_version = 0;\n"
    "    size_t update_offset = 0;\n"
    "    size_t update_size = 0;\n"
    "    bool update_is_partial = false;\n"
    "    void RecordUpdate(size_t offset, size_t size, bool partial) {\n"
    "        previous_content_version = content_version;\n"
    "        ++content_version;\n"
    "        update_offset = offset;\n"
    "        update_size = size;\n"
    "        update_is_partial = partial;\n"
    "    }\n",
)

in_function(
    "src/gl/vertex.cpp",
    "void CommitPixelPackDestination(",
    'extern "C" {',
    "    ++destination->buffer->content_version;\n",
    "    destination->buffer->RecordUpdate(\n"
    "        0, destination->buffer->data.size(), false);\n",
)
in_function(
    "src/gl/vertex.cpp",
    "void APIENTRY glBufferData(",
    "void APIENTRY glBufferSubData(",
    "    ++it->second.content_version;\n",
    "    it->second.RecordUpdate(0, it->second.data.size(), false);\n",
)
in_function(
    "src/gl/vertex.cpp",
    "void APIENTRY glBufferSubData(",
    "// ---- buffer queries / mapping",
    "    ++it->second.content_version;\n",
    "    const size_t write_offset = static_cast<size_t>(offset);\n"
    "    const size_t write_size = static_cast<size_t>(size);\n"
    "    const bool full_write = write_offset == 0 &&\n"
    "                            write_size == it->second.data.size();\n"
    "    it->second.RecordUpdate(write_offset, write_size, !full_write);\n",
)
in_function(
    "src/gl/vertex.cpp",
    "void APIENTRY glCopyBufferSubData(",
    "void APIENTRY glGetBufferParameteriv(",
    "    ++dst->content_version;\n",
    "    const bool full_write = writeoffset == 0 &&\n"
    "        static_cast<size_t>(size) == dst->data.size();\n"
    "    dst->RecordUpdate(static_cast<size_t>(writeoffset),\n"
    "                      static_cast<size_t>(size), !full_write);\n",
)
in_function(
    "src/gl/vertex.cpp",
    "void* APIENTRY glMapBuffer(",
    "void* APIENTRY glMapBufferRange(",
    "        ++b->content_version;\n",
    "        b->RecordUpdate(0, b->data.size(), false);\n",
)
in_function(
    "src/gl/vertex.cpp",
    "void* APIENTRY glMapBufferRange(",
    "GLboolean APIENTRY glUnmapBuffer(",
    "        ++b->content_version;\n",
    "        const size_t map_offset = static_cast<size_t>(offset);\n"
    "        const size_t map_length = static_cast<size_t>(length);\n"
    "        const bool full_write = map_offset == 0 &&\n"
    "                                map_length == b->data.size();\n"
    "        b->RecordUpdate(map_offset, map_length, !full_write);\n",
)
in_function(
    "src/gl/vertex.cpp",
    "void APIENTRY glFlushMappedBufferRange(",
    "void APIENTRY glEnableVertexAttribArray(",
    "    ++b->content_version;\n",
    # This API can be called for explicitly flushed maps whose writes are not
    # fully tracked by the current frontend.  Stay conservative and invalidate
    # the whole native snapshot instead of claiming a range we cannot prove.
    "    b->RecordUpdate(0, b->data.size(), false);\n",
)

# ---------------------------------------------------------------------------
# Backend-neutral resident source snapshots forward current/previous version
# and the latest mutation interval to the native backend.
# ---------------------------------------------------------------------------
exact(
    "src/backend/types.h",
    "    uint64_t source_content_version = 0;\n",
    "    uint64_t source_content_version = 0;\n"
    "    uint64_t source_previous_content_version = 0;\n"
    "    uint64_t source_update_offset = 0;\n"
    "    uint64_t source_update_size = 0;\n"
    "    bool source_update_is_partial = false;\n",
    count=2,
)

exact(
    "src/gl/draw.cpp",
    "            vstream.source_content_version = bit->second.content_version;\n",
    "            vstream.source_content_version = bit->second.content_version;\n"
    "            vstream.source_previous_content_version =\n"
    "                bit->second.previous_content_version;\n"
    "            vstream.source_update_offset = bit->second.update_offset;\n"
    "            vstream.source_update_size = bit->second.update_size;\n"
    "            vstream.source_update_is_partial = bit->second.update_is_partial;\n",
)
exact(
    "src/gl/draw.cpp",
    "            binding.source_content_version = buffer->second.content_version;\n",
    "            binding.source_content_version = buffer->second.content_version;\n"
    "            binding.source_previous_content_version =\n"
    "                buffer->second.previous_content_version;\n"
    "            binding.source_update_offset = buffer->second.update_offset;\n"
    "            binding.source_update_size = buffer->second.update_size;\n"
    "            binding.source_update_is_partial = buffer->second.update_is_partial;\n",
)

# ---------------------------------------------------------------------------
# Diagnostics: make the performance shape testable rather than inferred.
# ---------------------------------------------------------------------------
exact(
    "include/mithril/directmetal_diagnostics.h",
    "int mithrilGetDirectMetalBindingStatsV1(\n"
    "    MithrilDirectMetalBindingStatsV1* output, size_t output_size);\n",
    "int mithrilGetDirectMetalBindingStatsV1(\n"
    "    MithrilDirectMetalBindingStatsV1* output, size_t output_size);\n\n"
    "#define MITHRIL_DIRECT_METAL_BUFFER_STATS_VERSION 1u\n\n"
    "typedef struct MithrilDirectMetalBufferStatsV1 {\n"
    "    uint32_t version;\n"
    "    uint32_t struct_size;\n"
    "    uint64_t resident_allocations;\n"
    "    uint64_t resident_reuses;\n"
    "    uint64_t full_cpu_upload_bytes;\n"
    "    uint64_t partial_cpu_upload_bytes;\n"
    "    uint64_t preserve_blit_bytes;\n"
    "} MithrilDirectMetalBufferStatsV1;\n\n"
    "void mithrilResetDirectMetalBufferStats(void);\n"
    "int mithrilGetDirectMetalBufferStatsV1(\n"
    "    MithrilDirectMetalBufferStatsV1* output, size_t output_size);\n",
)

# ---------------------------------------------------------------------------
# DirectMetal: immutable resident generations.  A compatible partial update
# writes only the changed range on the CPU, and an ordered blit copies the
# untouched prefix/suffix from the previous generation.  Old generations enter
# the reuse pool only after the command buffer that last used them completes.
# ---------------------------------------------------------------------------
exact(
    "src/metal/engine.mm",
    "struct ResidentBuffer {\n"
    "    id<MTLBuffer> buffer = nil;\n"
    "    uint64_t content_version = 0;\n"
    "    size_t size = 0;\n"
    "};\n",
    "struct ResidentBuffer {\n"
    "    id<MTLBuffer> buffer = nil;\n"
    "    uint64_t content_version = 0;\n"
    "    size_t size = 0;\n"
    "};\n\n"
    "struct PendingResidentCopy {\n"
    "    id<MTLBuffer> source = nil;\n"
    "    id<MTLBuffer> destination = nil;\n"
    "    NSUInteger prefix_size = 0;\n"
    "    NSUInteger suffix_offset = 0;\n"
    "    NSUInteger suffix_size = 0;\n"
    "};\n",
)
exact(
    "src/metal/engine.mm",
    "struct CommandCompletion {\n"
    "    std::mutex mutex;\n"
    "    std::condition_variable condition;\n"
    "    bool completed = false;\n"
    "    bool success = false;\n"
    "};\n",
    "struct CommandCompletion {\n"
    "    std::mutex mutex;\n"
    "    std::condition_variable condition;\n"
    "    bool completed = false;\n"
    "    bool success = false;\n"
    "};\n\n"
    "struct RetiredResidentBuffer {\n"
    "    id<MTLBuffer> buffer = nil;\n"
    "    std::shared_ptr<CommandCompletion> completion;\n"
    "};\n",
)
regex(
    "src/metal/engine.mm",
    r"(MithrilDirectMetalBindingStatsV1 EmptyBindingStats\(\) \{.*?\n\})\n\nstruct Engine",
    r"\1\n\n"
    r"MithrilDirectMetalBufferStatsV1 EmptyBufferStats() {\n"
    r"    MithrilDirectMetalBufferStatsV1 stats{};\n"
    r"    stats.version = MITHRIL_DIRECT_METAL_BUFFER_STATS_VERSION;\n"
    r"    stats.struct_size = static_cast<uint32_t>(sizeof(stats));\n"
    r"    return stats;\n"
    r"}\n\nstruct Engine",
)
exact(
    "src/metal/engine.mm",
    "    MithrilDirectMetalBindingStatsV1 binding_stats = EmptyBindingStats();\n"
    "    std::vector<PendingDraw> draws;\n",
    "    MithrilDirectMetalBindingStatsV1 binding_stats = EmptyBindingStats();\n"
    "    MithrilDirectMetalBufferStatsV1 buffer_stats = EmptyBufferStats();\n"
    "    std::vector<PendingResidentCopy> pending_resident_copies;\n"
    "    std::vector<id<MTLBuffer>> resident_retire_on_submit;\n"
    "    std::vector<RetiredResidentBuffer> retired_resident_buffers;\n"
    "    std::vector<id<MTLBuffer>> resident_buffer_pool;\n"
    "    std::vector<PendingDraw> draws;\n",
)

native_retain = r'''bool CompletionReady(const std::shared_ptr<CommandCompletion>& completion) {
    if (!completion) return true;
    std::lock_guard<std::mutex> lock(completion->mutex);
    return completion->completed;
}

constexpr size_t kMaxResidentBufferPoolEntries = 32;

void ReclaimResidentBuffers() {
    auto& engine = GetEngine();
    for (auto it = engine.retired_resident_buffers.begin();
         it != engine.retired_resident_buffers.end();) {
        if (!CompletionReady(it->completion)) {
            ++it;
            continue;
        }
        if (it->buffer &&
            engine.resident_buffer_pool.size() < kMaxResidentBufferPoolEntries)
            engine.resident_buffer_pool.push_back(it->buffer);
        it = engine.retired_resident_buffers.erase(it);
    }
}

id<MTLBuffer> AcquireResidentBuffer(size_t size) {
    auto& engine = GetEngine();
    ReclaimResidentBuffers();
    for (size_t i = 0; i < engine.resident_buffer_pool.size(); ++i) {
        id<MTLBuffer> candidate = engine.resident_buffer_pool[i];
        if (candidate && [candidate length] == size) {
            engine.resident_buffer_pool[i] = engine.resident_buffer_pool.back();
            engine.resident_buffer_pool.pop_back();
            ++engine.buffer_stats.resident_reuses;
            return candidate;
        }
    }
    id<MTLBuffer> buffer = [engine.device
        newBufferWithLength:size options:MTLResourceStorageModeShared];
    if (buffer) ++engine.buffer_stats.resident_allocations;
    return buffer;
}

id<MTLBuffer> RetainResidentBytes(const uint8_t* source_data,
                                  size_t source_size,
                                  uint64_t lifetime_id,
                                  uint64_t content_version,
                                  uint64_t previous_content_version,
                                  size_t update_offset,
                                  size_t update_size,
                                  bool update_is_partial) {
    if (!source_data || !source_size || !lifetime_id) return nil;
    auto& engine = GetEngine();
    ResidentBuffer& resident = engine.resident_buffers[lifetime_id];
    if (resident.buffer && resident.content_version == content_version &&
        resident.size == source_size)
        return resident.buffer;

    const bool valid_update_range = update_offset <= source_size &&
        update_size <= source_size - update_offset;
    const size_t update_end = valid_update_range ? update_offset + update_size : 0;
    const bool can_preserve_with_blit =
        resident.buffer && resident.size == source_size &&
        resident.content_version == previous_content_version &&
        update_is_partial && update_size != 0 && valid_update_range &&
        source_size % 4 == 0 && update_offset % 4 == 0 &&
        update_size % 4 == 0 && update_end % 4 == 0;

    id<MTLBuffer> replacement = AcquireResidentBuffer(source_size);
    if (!replacement) return nil;

    if (can_preserve_with_blit) {
        std::memcpy(static_cast<uint8_t*>(replacement.contents) + update_offset,
                    source_data + update_offset, update_size);
        engine.buffer_stats.partial_cpu_upload_bytes += update_size;
        engine.buffer_stats.preserve_blit_bytes += source_size - update_size;
        engine.pending_resident_copies.push_back({
            resident.buffer, replacement,
            static_cast<NSUInteger>(update_offset),
            static_cast<NSUInteger>(update_end),
            static_cast<NSUInteger>(source_size - update_end)});
    } else {
        std::memcpy(replacement.contents, source_data, source_size);
        engine.buffer_stats.full_cpu_upload_bytes += source_size;
    }

    if (resident.buffer)
        engine.resident_retire_on_submit.push_back(resident.buffer);
    resident.buffer = replacement;
    resident.buffer.label = @"Mithril resident GL buffer";
    resident.content_version = content_version;
    resident.size = source_size;
    return resident.buffer;
}

id<MTLBuffer> RetainResidentBuffer(const backend::VertexStream& stream) {
    if (!stream.HasResidentSource()) return nil;
    return RetainResidentBytes(
        stream.source_data, stream.source_size,
        stream.source_lifetime_id, stream.source_content_version,
        stream.source_previous_content_version,
        static_cast<size_t>(stream.source_update_offset),
        static_cast<size_t>(stream.source_update_size),
        stream.source_update_is_partial);
}

'''
regex(
    "src/metal/engine.mm",
    r"id<MTLBuffer> RetainResidentBytes\(const uint8_t\* source_data,.*?\n\}\n\n"
    r"id<MTLBuffer> RetainResidentBuffer\(const backend::VertexStream& stream\) \{.*?\n\}\n\n"
    r"(?=MTLSamplerMinMagFilter)",
    native_retain,
)
regex(
    "src/metal/engine.mm",
    r"id<MTLBuffer> resident = RetainResidentBytes\(\s*"
    r"binding\.source_data, binding\.source_size,\s*"
    r"binding\.source_lifetime_id, binding\.source_content_version\);",
    "id<MTLBuffer> resident = RetainResidentBytes(\n"
    "            binding.source_data, binding.source_size,\n"
    "            binding.source_lifetime_id, binding.source_content_version,\n"
    "            binding.source_previous_content_version,\n"
    "            static_cast<size_t>(binding.source_update_offset),\n"
    "            static_cast<size_t>(binding.source_update_size),\n"
    "            binding.source_update_is_partial);",
)

# Preservation blits are encoded before the render encoder in the same command
# buffer. Metal command-buffer ordering therefore makes the replacement fully
# populated before any draw that references it.
exact(
    "src/metal/engine.mm",
    '    command.label = @"Mithril DirectMetal frame";\n\n'
    "    std::vector<std::shared_ptr<OcclusionQueryState>> query_states;\n",
    '    command.label = @"Mithril DirectMetal frame";\n\n'
    "    if (!engine.pending_resident_copies.empty()) {\n"
    "        id<MTLBlitCommandEncoder> resident_blit = [command blitCommandEncoder];\n"
    "        if (!resident_blit) return false;\n"
    '        resident_blit.label = @"Mithril resident buffer preservation";\n'
    "        for (const auto& copy : engine.pending_resident_copies) {\n"
    "            if (copy.prefix_size)\n"
    "                [resident_blit copyFromBuffer:copy.source sourceOffset:0\n"
    "                    toBuffer:copy.destination destinationOffset:0 size:copy.prefix_size];\n"
    "            if (copy.suffix_size)\n"
    "                [resident_blit copyFromBuffer:copy.source\n"
    "                    sourceOffset:copy.suffix_offset toBuffer:copy.destination\n"
    "                    destinationOffset:copy.suffix_offset size:copy.suffix_size];\n"
    "        }\n"
    "        [resident_blit endEncoding];\n"
    "    }\n\n"
    "    std::vector<std::shared_ptr<OcclusionQueryState>> query_states;\n",
)
exact(
    "src/metal/engine.mm",
    "    auto completion = CommitCommandBuffer(command);\n"
    "    if (!completion) return false;\n"
    "    for (const auto& query : query_states) {\n",
    "    auto completion = CommitCommandBuffer(command);\n"
    "    if (!completion) return false;\n"
    "    for (id<MTLBuffer> buffer : engine.resident_retire_on_submit)\n"
    "        engine.retired_resident_buffers.push_back({buffer, completion});\n"
    "    engine.resident_retire_on_submit.clear();\n"
    "    engine.pending_resident_copies.clear();\n"
    "    for (const auto& query : query_states) {\n",
)

exact(
    "src/metal/engine.mm",
    "    *output = GetEngine().binding_stats;\n"
    "    return 1;\n"
    "}\n\n"
    "} // namespace mithril::metal\n",
    "    *output = GetEngine().binding_stats;\n"
    "    return 1;\n"
    "}\n\n"
    'extern "C" void mithrilResetDirectMetalBufferStats(void) {\n'
    "    GetEngine().buffer_stats = EmptyBufferStats();\n"
    "}\n\n"
    'extern "C" int mithrilGetDirectMetalBufferStatsV1(\n'
    "    MithrilDirectMetalBufferStatsV1* output, size_t output_size) {\n"
    "    if (!output || output_size < sizeof(*output)) return 0;\n"
    "    *output = GetEngine().buffer_stats;\n"
    "    return 1;\n"
    "}\n\n"
    "} // namespace mithril::metal\n",
)

# DirectMetal-only deterministic regression.
exact(
    "cmake/MithrilSmokeTests.cmake",
    "    layered_fbo_smoke\n"
    "    directmetal_fbo_smoke)\n",
    "    layered_fbo_smoke\n"
    "    directmetal_fbo_smoke\n"
    "    directmetal_buffer_streaming_smoke)\n",
)

# Architecture guards: the old full-copy allocation path must be gone and the
# partial path must have both a CPU dirty-range copy and GPU preservation copy.
metal = Path("src/metal/engine.mm").read_text()
if "newBufferWithBytes:source_data" in metal:
    raise SystemExit("old full-copy resident allocation path remains")
for required in (
    "partial_cpu_upload_bytes += update_size",
    "copyFromBuffer:copy.source",
    "resident_buffer_pool",
    "resident.content_version == previous_content_version",
):
    if required not in metal:
        raise SystemExit(f"missing DirectMetal streaming invariant: {required}")

print("DirectMetal resident-buffer streaming phase applied successfully")
