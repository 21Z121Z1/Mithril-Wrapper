#!/usr/bin/env python3
"""Apply phase 6: versioned numeric loose-uniform snapshots.

Uniform names are resolved to numeric slots once when a native program is
created. Draws then borrow stable byte views plus a version; DirectMetal packs
one immutable stage snapshot per version and uploads each snapshot once per
frame. Vulkan uses the same numeric source without draw-time name lookup.
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
# Shared contract: byte views + version replace per-draw string/value maps.
# ---------------------------------------------------------------------------
exact("src/backend/types.h",
'''// Native shader reflection for one member of the synthetic loose-uniform
// block. GL setters expose tightly packed scalar sequences, while std140/MSL
// layouts may add a stride between array elements or matrix rows/columns.
struct UniformMemberLayout {
''',
'''struct UniformValueView {
    const uint8_t* data = nullptr;
    uint32_t size = 0;
};

struct LooseUniformSource {
    const UniformValueView* values = nullptr;
    uint32_t count = 0;
    uint64_t version = 0;

    bool HasValues() const { return values != nullptr && count != 0; }
};

// Native shader reflection for one member of the synthetic loose-uniform
// block. GL setters expose tightly packed scalar sequences, while std140/MSL
// layouts may add a stride between array elements or matrix rows/columns.
struct UniformMemberLayout {
''')
exact("src/backend/types.h",
'''bool PackUniformValue(const UniformMemberLayout& layout,
                      const std::vector<uint8_t>& value,
                      uint8_t* block, size_t block_size);
''',
'''bool PackUniformValue(const UniformMemberLayout& layout,
                      const uint8_t* value_data, size_t value_size,
                      uint8_t* block, size_t block_size);
inline bool PackUniformValue(const UniformMemberLayout& layout,
                             const std::vector<uint8_t>& value,
                             uint8_t* block, size_t block_size) {
    return PackUniformValue(layout, value.data(), value.size(), block, block_size);
}
''')
exact("src/backend/types.h",
'''    // Exact bytes captured when the GL draw is issued. Integer uniforms must
    // remain integer bit patterns; converting their values through float
    // changes what SPIR-V/MSL reads from the synthetic uniform block.
    std::unordered_map<std::string, std::vector<uint8_t>> uniforms;
''',
'''    // Borrowed only for the synchronous backend Draw() call. Program-local
    // setters own the byte arrays; deferred native backends must snapshot them
    // before Draw returns.
    LooseUniformSource loose_uniforms;
''')

# PackUniformValue works directly from a non-owning byte span.
exact("src/backend/backend.cpp",
'''bool PackUniformValue(const UniformMemberLayout& layout,
                      const std::vector<uint8_t>& value,
                      uint8_t* block, size_t block_size) {
''',
'''bool PackUniformValue(const UniformMemberLayout& layout,
                      const uint8_t* value_data, size_t value_size,
                      uint8_t* block, size_t block_size) {
''')
exact("src/backend/backend.cpp", "        value.size() % kScalarBytes != 0)\n", "        value_size % kScalarBytes != 0)\n")
exact("src/backend/backend.cpp", "        if (source + kScalarBytes > value.size() ||\n", "        if (source + kScalarBytes > value_size ||\n")
exact("src/backend/backend.cpp", "        std::memcpy(block + destination, value.data() + source, kScalarBytes);\n", "        std::memcpy(block + destination, value_data + source, kScalarBytes);\n")
exact("src/backend/backend.cpp", "        if (source_base >= value.size()) break;\n", "        if (source_base >= value_size) break;\n")
exact("src/backend/backend.cpp", "                value.size() - source_base);\n", "                value_size - source_base);\n")
exact("src/backend/backend.cpp", "                        value.data() + source_base, bytes);\n", "                        value_data + source_base, bytes);\n")

# Native program creation receives frontend uniform names once, cold-path only.
for path in ("src/backend/backend.h", "src/metal/engine.h", "src/vk/engine.h"):
    exact(path,
'''uint64_t CreateProgram(const std::vector<uint32_t>& vs,
                       const std::vector<uint32_t>& fs);
''',
'''uint64_t CreateProgram(const std::vector<uint32_t>& vs,
                       const std::vector<uint32_t>& fs,
                       const std::vector<std::string>& uniform_names);
''')
exact("src/metal/MetalDeviceSession.h",
'''    uint64_t CreateProgram(const std::vector<uint32_t>& vs,
                           const std::vector<uint32_t>& fs);
''',
'''    uint64_t CreateProgram(const std::vector<uint32_t>& vs,
                           const std::vector<uint32_t>& fs,
                           const std::vector<std::string>& uniform_names);
''')
exact("src/backend/backend.cpp",
'''uint64_t CreateProgram(const std::vector<uint32_t>& vs, const std::vector<uint32_t>& fs) {
    DISPATCH_RET(CreateProgram, 0, vs, fs);
}
''',
'''uint64_t CreateProgram(const std::vector<uint32_t>& vs,
                       const std::vector<uint32_t>& fs,
                       const std::vector<std::string>& uniform_names) {
    DISPATCH_RET(CreateProgram, 0, vs, fs, uniform_names);
}
''')
exact("src/metal/MetalDeviceSession.cpp",
'''uint64_t MetalDeviceSession::CreateProgram(const std::vector<uint32_t>& vs,
                                           const std::vector<uint32_t>& fs) {
    return metal::CreateProgram(vs, fs);
}
''',
'''uint64_t MetalDeviceSession::CreateProgram(
    const std::vector<uint32_t>& vs, const std::vector<uint32_t>& fs,
    const std::vector<std::string>& uniform_names) {
    return metal::CreateProgram(vs, fs, uniform_names);
}
''')

# ---------------------------------------------------------------------------
# Shader frontend owns stable views and a monotonically increasing loose state
# version. Sampler setters update texture-unit state but do not dirty the UBO.
# ---------------------------------------------------------------------------
exact("src/shader/shader.h", "#include <GL/glcorearb.h>\n", "#include <GL/glcorearb.h>\n#include <backend/types.h>\n")
exact("src/shader/shader.h",
'''    std::vector<Uniform> uniforms;         // active uniforms (index == GL index)
''',
'''    std::vector<Uniform> uniforms;         // active uniforms (index == GL index)
    std::vector<backend::UniformValueView> loose_uniform_views;
    uint64_t loose_uniform_version = 1;
''')

# Initialize persistent views whenever linking publishes a new native-visible
# uniform table, before the program becomes linked.
exact("src/gl/shader.cpp",
'''    p->linked = true;
    for (auto& block : p->uniform_blocks) {
''',
'''    p->loose_uniform_views.resize(p->uniforms.size());
    for (size_t i = 0; i < p->uniforms.size(); ++i) {
        const auto& raw = p->uniforms[i].raw_value;
        p->loose_uniform_views[i] = {
            raw.empty() ? nullptr : raw.data(), static_cast<uint32_t>(raw.size())};
    }
    p->loose_uniform_version = 1;
    p->linked = true;
    for (auto& block : p->uniform_blocks) {
''')

# Commit helper refreshes the pointer after raw_value resize and versions only
# non-sampler loose uniforms. Uniform-block members are not writable by glUniform.
exact("src/gl/shader.cpp",
'''sh::Program* CurrentProgramForUniform() {
    GLuint id = s::GetState().current_program;
    return id ? sh::GetProgram(id) : nullptr;
}

bool ResolveUniformWrite''',
'''sh::Program* CurrentProgramForUniform() {
    GLuint id = s::GetState().current_program;
    return id ? sh::GetProgram(id) : nullptr;
}

void CommitLooseUniformWrite(sh::Uniform* uniform) {
    sh::Program* program = CurrentProgramForUniform();
    if (!program || !uniform || program->uniforms.empty()) return;
    const ptrdiff_t index = uniform - program->uniforms.data();
    if (index < 0 || static_cast<size_t>(index) >= program->uniforms.size()) return;
    if (program->loose_uniform_views.size() != program->uniforms.size())
        program->loose_uniform_views.resize(program->uniforms.size());
    const auto& raw = uniform->raw_value;
    program->loose_uniform_views[static_cast<size_t>(index)] = {
        raw.empty() ? nullptr : raw.data(), static_cast<uint32_t>(raw.size())};
    if (!IsSamplerUniformType(uniform->type)) ++program->loose_uniform_version;
}

bool ResolveUniformWrite''')

# Every setter path commits after bytes are written, including boolean early returns.
exact("src/gl/shader.cpp",
'''    if (IsBooleanUniformType(uniform->type)) {
        StoreBooleanScalars(uniform, scalar_offset, v, scalars, total_scalars);
        return;
    }
''',
'''    if (IsBooleanUniformType(uniform->type)) {
        StoreBooleanScalars(uniform, scalar_offset, v, scalars, total_scalars);
        CommitLooseUniformWrite(uniform);
        return;
    }
''', count=3)
# Three non-boolean setter functions all end with the same memcpy shape.
exact("src/gl/shader.cpp",
'''    std::memcpy(uniform->raw_value.data() + scalar_offset * sizeof(*v), v,
                scalars * sizeof(*v));
}
''',
'''    std::memcpy(uniform->raw_value.data() + scalar_offset * sizeof(*v), v,
                scalars * sizeof(*v));
    CommitLooseUniformWrite(uniform);
}
''', count=3)

# ---------------------------------------------------------------------------
# Draw frontend: names are passed once at backend program creation; draws borrow
# only numeric byte views/version. ComposeUniforms disappears entirely.
# ---------------------------------------------------------------------------
exact("src/gl/draw.cpp",
'''uint64_t CreateBackendProgram(sh::Program* prog) {
    auto it = g_backend_programs.find(prog->id);
    if (it != g_backend_programs.end()) return it->second;
    uint64_t handle = v::CreateProgram(prog->vertex_spirv, prog->fragment_spirv);
    if (handle) g_backend_programs.emplace(prog->id, handle);
    return handle;
}

std::unordered_map<std::string, std::vector<uint8_t>> ComposeUniforms(
    sh::Program* prog) {
    std::unordered_map<std::string, std::vector<uint8_t>> uniforms;
    for (const auto& u : prog->uniforms)
        if (u.location >= 0 && !u.raw_value.empty())
            uniforms[u.name] = u.raw_value;
    return uniforms;
}
''',
'''uint64_t CreateBackendProgram(sh::Program* prog) {
    auto it = g_backend_programs.find(prog->id);
    if (it != g_backend_programs.end()) return it->second;
    std::vector<std::string> uniform_names;
    uniform_names.reserve(prog->uniforms.size());
    for (const auto& uniform : prog->uniforms)
        uniform_names.push_back(uniform.name);
    uint64_t handle = v::CreateProgram(
        prog->vertex_spirv, prog->fragment_spirv, uniform_names);
    if (handle) g_backend_programs.emplace(prog->id, handle);
    return handle;
}
''')
exact("src/gl/draw.cpp",
'''    dp.uniforms = ComposeUniforms(prog);
''',
'''    dp.loose_uniforms.values = prog->loose_uniform_views.empty()
        ? nullptr : prog->loose_uniform_views.data();
    dp.loose_uniforms.count = static_cast<uint32_t>(prog->loose_uniform_views.size());
    dp.loose_uniforms.version = prog->loose_uniform_version;
''')

# ---------------------------------------------------------------------------
# DirectMetal: resolve reflected names -> numeric slots once per Program, pack
# immutable stage byte arrays once per version, retain snapshot in PendingDraw.
# ---------------------------------------------------------------------------
exact("src/metal/engine.mm",
'''struct ShaderStage {
    id<MTLLibrary> library = nil;
    id<MTLFunction> function = nil;
    std::vector<UboMember> members;
    uint32_t ubo_size = 0;
    bool uses_sampled_images = false;
};

struct Program {
''',
'''struct ShaderStage {
    id<MTLLibrary> library = nil;
    id<MTLFunction> function = nil;
    std::vector<UboMember> members;
    std::vector<uint32_t> member_value_indices;
    uint32_t ubo_size = 0;
    bool uses_sampled_images = false;
};

struct PackedUniformSnapshot {
    uint64_t version = 0;
    std::vector<uint8_t> vertex;
    std::vector<uint8_t> fragment;
};

struct Program {
''')
exact("src/metal/engine.mm",
'''    ShaderStage vertex;
    ShaderStage fragment;
};
''',
'''    ShaderStage vertex;
    ShaderStage fragment;
    std::shared_ptr<PackedUniformSnapshot> last_uniform_snapshot;
};
''')
exact("src/metal/engine.mm",
'''    id<MTLBuffer> resident_index = nil;
    std::vector<BoundUniformBuffer> uniform_buffers;
''',
'''    id<MTLBuffer> resident_index = nil;
    std::shared_ptr<PackedUniformSnapshot> uniform_snapshot;
    std::vector<BoundUniformBuffer> uniform_buffers;
''')

# Uniform diagnostics.
exact("include/mithril/directmetal_diagnostics.h",
'''int mithrilGetDirectMetalIndexStatsV1(
    MithrilDirectMetalIndexStatsV1* output, size_t output_size);
''',
'''int mithrilGetDirectMetalIndexStatsV1(
    MithrilDirectMetalIndexStatsV1* output, size_t output_size);

#define MITHRIL_DIRECT_METAL_UNIFORM_STATS_VERSION 1u

typedef struct MithrilDirectMetalUniformStatsV1 {
    uint32_t version;
    uint32_t struct_size;
    uint64_t snapshot_packs;
    uint64_t snapshot_reuses;
    uint64_t frame_uniform_uploads;
    uint64_t packed_bytes;
} MithrilDirectMetalUniformStatsV1;

void mithrilResetDirectMetalUniformStats(void);
int mithrilGetDirectMetalUniformStatsV1(
    MithrilDirectMetalUniformStatsV1* output, size_t output_size);
''')
regex("src/metal/engine.mm",
r'''(MithrilDirectMetalIndexStatsV1 EmptyIndexStats\(\) \{.*?\n\})\n\n(?=template <size_t Capacity>)''',
r'''\1

MithrilDirectMetalUniformStatsV1 EmptyUniformStats() {
    MithrilDirectMetalUniformStatsV1 stats{};
    stats.version = MITHRIL_DIRECT_METAL_UNIFORM_STATS_VERSION;
    stats.struct_size = static_cast<uint32_t>(sizeof(stats));
    return stats;
}

''')
exact("src/metal/engine.mm",
'''    MithrilDirectMetalIndexStatsV1 index_stats = EmptyIndexStats();
''',
'''    MithrilDirectMetalIndexStatsV1 index_stats = EmptyIndexStats();
    MithrilDirectMetalUniformStatsV1 uniform_stats = EmptyUniformStats();
''')

# Replace per-draw PackUniforms/string memo with versioned snapshot builders.
regex("src/metal/engine.mm",
r'''NSUInteger PackUniforms\(FrameContext& frame, NSUInteger\* cursor,.*?\n\}\n\n(?=std::vector<uint32_t> ExpandTriangleFan)''',
r'''bool ResolveUniformMemberSlots(ShaderStage* stage,
                               const std::vector<std::string>& uniform_names) {
    stage->member_value_indices.clear();
    stage->member_value_indices.reserve(stage->members.size());
    for (const auto& member : stage->members) {
        auto found = std::find(uniform_names.begin(), uniform_names.end(), member.name);
        if (found == uniform_names.end()) {
            ML_LOG_ERROR("metal: reflected loose uniform '%s' has no frontend slot",
                         member.name.c_str());
            return false;
        }
        stage->member_value_indices.push_back(
            static_cast<uint32_t>(found - uniform_names.begin()));
    }
    return true;
}

bool PackUniformStage(const ShaderStage& stage,
                      const backend::LooseUniformSource& source,
                      std::vector<uint8_t>* packed) {
    packed->clear();
    if (!stage.ubo_size) return true;
    packed->assign(AlignUp(stage.ubo_size, 256), 0);
    if (stage.member_value_indices.size() != stage.members.size()) return false;
    for (size_t i = 0; i < stage.members.size(); ++i) {
        const uint32_t slot = stage.member_value_indices[i];
        if (slot >= source.count || !source.values) return false;
        const auto& value = source.values[slot];
        if (!value.data || !value.size) continue;
        if (!backend::PackUniformValue(stage.members[i], value.data, value.size,
                                       packed->data(), stage.ubo_size)) {
            ML_LOG_ERROR("metal: invalid reflected layout for uniform '%s'",
                         stage.members[i].name.c_str());
            return false;
        }
    }
    return true;
}

std::shared_ptr<PackedUniformSnapshot> GetOrCreateUniformSnapshot(
    Program* program, const backend::LooseUniformSource& source) {
    if (!program->vertex.ubo_size && !program->fragment.ubo_size) return nullptr;
    if (program->last_uniform_snapshot &&
        program->last_uniform_snapshot->version == source.version) {
        ++GetEngine().uniform_stats.snapshot_reuses;
        return program->last_uniform_snapshot;
    }
    auto snapshot = std::make_shared<PackedUniformSnapshot>();
    snapshot->version = source.version;
    if (!PackUniformStage(program->vertex, source, &snapshot->vertex) ||
        !PackUniformStage(program->fragment, source, &snapshot->fragment))
        return nullptr;
    auto& stats = GetEngine().uniform_stats;
    ++stats.snapshot_packs;
    stats.packed_bytes += snapshot->vertex.size() + snapshot->fragment.size();
    program->last_uniform_snapshot = snapshot;
    return snapshot;
}

struct UniformFrameOffsets {
    NSUInteger vertex = NSNotFound;
    NSUInteger fragment = NSNotFound;
};

UniformFrameOffsets UploadUniformSnapshot(
    FrameContext& frame, NSUInteger* cursor,
    const std::shared_ptr<PackedUniformSnapshot>& snapshot,
    std::unordered_map<const PackedUniformSnapshot*, UniformFrameOffsets>* memo) {
    if (!snapshot) return {};
    auto existing = memo->find(snapshot.get());
    if (existing != memo->end()) return existing->second;
    UniformFrameOffsets offsets;
    if (!snapshot->vertex.empty()) {
        offsets.vertex = AllocateUpload(frame, cursor, snapshot->vertex.data(),
                                        snapshot->vertex.size());
        if (offsets.vertex == NSNotFound) return offsets;
        ++GetEngine().uniform_stats.frame_uniform_uploads;
    }
    if (!snapshot->fragment.empty()) {
        if (snapshot->fragment == snapshot->vertex && offsets.vertex != NSNotFound) {
            offsets.fragment = offsets.vertex;
        } else {
            offsets.fragment = AllocateUpload(frame, cursor, snapshot->fragment.data(),
                                              snapshot->fragment.size());
            if (offsets.fragment == NSNotFound) return offsets;
            ++GetEngine().uniform_stats.frame_uniform_uploads;
        }
    }
    memo->emplace(snapshot.get(), offsets);
    return offsets;
}

''')

# Required upload size counts each shared snapshot once, not once per draw.
regex("src/metal/engine.mm",
r'''NSUInteger UniformBytes\(const ShaderStage& stage\) \{.*?\n\}\n\nNSUInteger RequiredUploadBytes\(\) \{
    auto& engine = GetEngine\(\);
    NSUInteger cursor = 0;''',
'''NSUInteger RequiredUploadBytes() {
    auto& engine = GetEngine();
    NSUInteger cursor = 0;''')
exact("src/metal/engine.mm",
'''    for (const auto& pending : engine.draws) {
''',
'''    std::unordered_set<const PackedUniformSnapshot*> counted_uniform_snapshots;
    for (const auto& pending : engine.draws) {
''', count=1)
exact("src/metal/engine.mm",
'''        auto program = engine.programs.find(draw.program);
        if (program != engine.programs.end()) {
            add(UniformBytes(program->second.vertex));
            add(UniformBytes(program->second.fragment));
        }
''',
'''        if (pending.uniform_snapshot &&
            counted_uniform_snapshots.insert(pending.uniform_snapshot.get()).second) {
            add(pending.uniform_snapshot->vertex.size());
            if (pending.uniform_snapshot->fragment != pending.uniform_snapshot->vertex)
                add(pending.uniform_snapshot->fragment.size());
        }
''')

# EncodeDraws uses pointer-identity frame memo and immutable packed bytes.
exact("src/metal/engine.mm",
'''    std::unordered_map<std::string, NSUInteger> uniform_memo;
''',
'''    std::unordered_map<const PackedUniformSnapshot*, UniformFrameOffsets>
        uniform_memo;
''')
exact("src/metal/engine.mm",
'''        const NSUInteger vertex_ubo = PackUniforms(
            frame, &cursor, program->second.vertex, draw, &uniform_memo);
        const NSUInteger fragment_ubo = PackUniforms(
            frame, &cursor, program->second.fragment, draw, &uniform_memo);
''',
'''        const UniformFrameOffsets uniform_offsets = UploadUniformSnapshot(
            frame, &cursor, pending.uniform_snapshot, &uniform_memo);
        if (pending.uniform_snapshot &&
            ((!pending.uniform_snapshot->vertex.empty() &&
              uniform_offsets.vertex == NSNotFound) ||
             (!pending.uniform_snapshot->fragment.empty() &&
              uniform_offsets.fragment == NSNotFound)))
            return false;
''')
exact("src/metal/engine.mm",
'''        if (vertex_ubo != NSNotFound)
            [encoder setVertexBuffer:frame.upload offset:vertex_ubo
                             atIndex:kUniformBufferIndex];
        if (fragment_ubo != NSNotFound)
            [encoder setFragmentBuffer:frame.upload offset:fragment_ubo
                               atIndex:kUniformBufferIndex];
''',
'''        if (uniform_offsets.vertex != NSNotFound)
            [encoder setVertexBuffer:frame.upload offset:uniform_offsets.vertex
                             atIndex:kUniformBufferIndex];
        if (uniform_offsets.fragment != NSNotFound)
            [encoder setFragmentBuffer:frame.upload offset:uniform_offsets.fragment
                               atIndex:kUniformBufferIndex];
''')

# Native CreateProgram resolves name slots once after reflection.
exact("src/metal/engine.mm",
'''uint64_t CreateProgram(const std::vector<uint32_t>& vs,
                       const std::vector<uint32_t>& fs) {
''',
'''uint64_t CreateProgram(const std::vector<uint32_t>& vs,
                       const std::vector<uint32_t>& fs,
                       const std::vector<std::string>& uniform_names) {
''')
exact("src/metal/engine.mm",
'''    if (!TranslateStage(vs, spv::ExecutionModelVertex, &program.vertex) ||
        !TranslateStage(fs, spv::ExecutionModelFragment, &program.fragment))
        return 0;
''',
'''    if (!TranslateStage(vs, spv::ExecutionModelVertex, &program.vertex) ||
        !TranslateStage(fs, spv::ExecutionModelFragment, &program.fragment) ||
        !ResolveUniformMemberSlots(&program.vertex, uniform_names) ||
        !ResolveUniformMemberSlots(&program.fragment, uniform_names))
        return 0;
''')

# Draw snapshots loose uniforms synchronously before borrowed pointers expire.
exact("src/metal/engine.mm",
'''    PendingDraw pending;
    if (params.occlusion_query) {
''',
'''    PendingDraw pending;
    if (program->second.vertex.ubo_size || program->second.fragment.ubo_size) {
        pending.uniform_snapshot = GetOrCreateUniformSnapshot(
            &program->second, params.loose_uniforms);
        if (!pending.uniform_snapshot) return false;
    }
    if (params.occlusion_query) {
''')
exact("src/metal/engine.mm",
'''    pending.params.resident_indices.source_data = nullptr;
    pending.params.resident_indices.source_size = 0;
''',
'''    pending.params.resident_indices.source_data = nullptr;
    pending.params.resident_indices.source_size = 0;
    pending.params.loose_uniforms.values = nullptr;
    pending.params.loose_uniforms.count = 0;
''')

# Diagnostics exports.
exact("src/metal/engine.mm",
'''extern "C" int mithrilGetDirectMetalIndexStatsV1(
    MithrilDirectMetalIndexStatsV1* output, size_t output_size) {
    if (!output || output_size < sizeof(*output)) return 0;
    *output = GetEngine().index_stats;
    return 1;
}

} // namespace mithril::metal
''',
'''extern "C" int mithrilGetDirectMetalIndexStatsV1(
    MithrilDirectMetalIndexStatsV1* output, size_t output_size) {
    if (!output || output_size < sizeof(*output)) return 0;
    *output = GetEngine().index_stats;
    return 1;
}

extern "C" void mithrilResetDirectMetalUniformStats(void) {
    GetEngine().uniform_stats = EmptyUniformStats();
}

extern "C" int mithrilGetDirectMetalUniformStatsV1(
    MithrilDirectMetalUniformStatsV1* output, size_t output_size) {
    if (!output || output_size < sizeof(*output)) return 0;
    *output = GetEngine().uniform_stats;
    return 1;
}

} // namespace mithril::metal
''')

# ---------------------------------------------------------------------------
# Vulkan reference: same one-time name->slot resolution, numeric draw views.
# ---------------------------------------------------------------------------
exact("src/vk/internal.h",
'''    std::vector<UboMember> members;
''',
'''    std::vector<UboMember> members;
    std::vector<uint32_t> member_value_indices;
''')
exact("src/vk/pipeline.cpp",
'''uint64_t CreateProgram(const std::vector<uint32_t>& vs,
                       const std::vector<uint32_t>& fs) {
''',
'''uint64_t CreateProgram(const std::vector<uint32_t>& vs,
                       const std::vector<uint32_t>& fs,
                       const std::vector<std::string>& uniform_names) {
''')
exact("src/vk/pipeline.cpp",
'''            p.members.erase(dup, p.members.end());
            ML_LOG_DEBUG("vk: program %llu UBO %zu bytes (%zu members)",
''',
'''            p.members.erase(dup, p.members.end());
            p.member_value_indices.clear();
            p.member_value_indices.reserve(p.members.size());
            for (const auto& member : p.members) {
                auto found = std::find(uniform_names.begin(), uniform_names.end(),
                                       member.name);
                if (found == uniform_names.end()) {
                    ML_LOG_ERROR("vk: reflected loose uniform '%s' has no frontend slot",
                                 member.name.c_str());
                    g.fn.DestroyShaderModule(g.device, p.vs_mod, nullptr);
                    g.fn.DestroyShaderModule(g.device, p.fs_mod, nullptr);
                    return 0;
                }
                p.member_value_indices.push_back(
                    static_cast<uint32_t>(found - uniform_names.begin()));
            }
            ML_LOG_DEBUG("vk: program %llu UBO %zu bytes (%zu members)",
''')
exact("src/vk/draw.cpp",
'''    if (prog.has_ubo) {
        std::vector<uint8_t> bytes(prog.ubo_size, 0);
        for (const auto& m : prog.members) {
            auto it = params.uniforms.find(m.name);
            if (it != params.uniforms.end() &&
                !backend::PackUniformValue(
                    m, it->second, bytes.data(), bytes.size())) {
                ML_LOG_ERROR("vk: invalid reflected layout for uniform '%s'",
                             m.name.c_str());
                DestroyDrawOp(op);
                return;
            }
        }
''',
'''    if (prog.has_ubo) {
        std::vector<uint8_t> bytes(prog.ubo_size, 0);
        if (prog.member_value_indices.size() != prog.members.size()) {
            DestroyDrawOp(op);
            return;
        }
        for (size_t i = 0; i < prog.members.size(); ++i) {
            const uint32_t slot = prog.member_value_indices[i];
            if (!params.loose_uniforms.values || slot >= params.loose_uniforms.count) {
                DestroyDrawOp(op);
                return;
            }
            const auto& value = params.loose_uniforms.values[slot];
            if (value.data && value.size &&
                !backend::PackUniformValue(prog.members[i], value.data, value.size,
                                           bytes.data(), bytes.size())) {
                ML_LOG_ERROR("vk: invalid reflected layout for uniform '%s'",
                             prog.members[i].name.c_str());
                DestroyDrawOp(op);
                return;
            }
        }
''')

# Register DirectMetal performance-shape regression.
exact("cmake/MithrilSmokeTests.cmake",
'''    directmetal_resident_index_smoke
    lazy_buffer_storage_smoke)
''',
'''    directmetal_resident_index_smoke
    directmetal_uniform_snapshot_smoke
    lazy_buffer_storage_smoke)
''')

# Architectural guards.
files = "\n".join(Path(p).read_text() for p in (
    "src/backend/types.h","src/gl/draw.cpp","src/metal/engine.mm","src/vk/draw.cpp"))
for forbidden in (
    "ComposeUniforms(",
    "params.uniforms.find(",
    "draw.uniforms.find(",
    "std::unordered_map<std::string, NSUInteger> uniform_memo",
    "std::string key(reinterpret_cast<const char*>(packed.data())",
):
    if forbidden in files:
        raise SystemExit(f"old per-draw loose-uniform path remains: {forbidden}")
for required in (
    "struct LooseUniformSource",
    "loose_uniform_version",
    "GetOrCreateUniformSnapshot",
    "member_value_indices",
    "snapshot_reuses",
):
    if required not in files + Path("src/shader/shader.h").read_text() + Path("include/mithril/directmetal_diagnostics.h").read_text():
        raise SystemExit(f"missing uniform-snapshot invariant: {required}")

print("versioned numeric loose-uniform snapshots applied successfully")
