#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    n = text.count(old)
    if n != 1:
        raise SystemExit(f"{path}: expected one anchor, found {n}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1))

# Backend contract: metadata arrays are borrowed for the synchronous Draw()
# call. Native backends must retain anything needed after Draw returns.
replace_once(
    "src/backend/types.h",
    '''struct LooseUniformSource {
    const UniformValueView* values = nullptr;
    uint32_t count = 0;
    uint64_t version = 0;

    bool HasValues() const { return values != nullptr && count != 0; }
};
''',
    '''struct LooseUniformSource {
    const UniformValueView* values = nullptr;
    uint32_t count = 0;
    uint64_t version = 0;

    bool HasValues() const { return values != nullptr && count != 0; }
};

// Non-owning array used by the hot draw contract. The GL frontend owns the
// backing storage for the duration of the synchronous backend Draw() call.
// Deferred backends must resolve/retain native state before Draw returns.
template <typename T>
struct ArrayView {
    const T* data = nullptr;
    size_t count = 0;

    bool empty() const { return count == 0; }
    size_t size() const { return count; }
    const T* begin() const { return data; }
    const T* end() const { return data ? data + count : nullptr; }
    const T& operator[](size_t index) const { return data[index]; }
};
''')

replace_once(
    "src/backend/types.h",
    '''    LooseUniformSource loose_uniforms;
    std::vector<UniformBufferBinding> uniform_buffers;
    std::vector<SampledTextureBinding> sampled_textures;
    PipelineState pipeline;
''',
    '''    LooseUniformSource loose_uniforms;
    ArrayView<UniformBufferBinding> uniform_buffers;
    ArrayView<SampledTextureBinding> sampled_textures;
    PipelineState pipeline;
''')

# The Phase-7 SharedDrawState owns these vectors once per ordinary draw or
# MultiDraw batch. DrawParams only borrows them for the backend call.
replace_once(
    "src/gl/draw.cpp",
    '''    dp.loose_uniforms = shared->loose_uniforms;
    dp.uniform_buffers = shared->uniform_buffers;
    dp.sampled_textures = shared->sampled_textures;
    dp.pipeline = shared->pipeline;
''',
    '''    dp.loose_uniforms = shared->loose_uniforms;
    dp.uniform_buffers = {
        shared->uniform_buffers.data(), shared->uniform_buffers.size()};
    dp.sampled_textures = {
        shared->sampled_textures.data(), shared->sampled_textures.size()};
    dp.pipeline = shared->pipeline;
''')

# DirectMetal already has a fixed-capacity InlineList for render-target state.
# Reuse it for native bindings whose maxima are fixed by the shared shader and
# texture-unit contracts: <=12 user UBOs per stage and <=16 samplers per stage.
replace_once(
    "src/metal/engine.mm",
    '''constexpr size_t kMaxResolvedColorAttachments = 8;

struct ResolvedTarget {
''',
    '''constexpr size_t kMaxResolvedColorAttachments = 8;
constexpr size_t kMaxPendingUniformBufferBindings =
    shader::kMaxUserUniformBlocksPerStage * 2;
constexpr size_t kMaxPendingTextureBindings = backend::kMaxTextureUnits * 2;

struct ResolvedTarget {
''')

replace_once(
    "src/metal/engine.mm",
    '''    std::shared_ptr<PackedUniformSnapshot> uniform_snapshot;
    std::vector<BoundUniformBuffer> uniform_buffers;
    std::vector<BoundTexture> textures;
    std::shared_ptr<OcclusionQueryState> occlusion;
''',
    '''    std::shared_ptr<PackedUniformSnapshot> uniform_snapshot;
    InlineList<BoundUniformBuffer, kMaxPendingUniformBufferBindings>
        uniform_buffers;
    InlineList<BoundTexture, kMaxPendingTextureBindings> textures;
    std::shared_ptr<OcclusionQueryState> occlusion;
''')

# Capacity checks are defensive even though the frontend/shader contracts make
# these maxima unreachable in valid GL state.
replace_once(
    "src/metal/engine.mm",
    '''        if (!resident) return false;
        pending.uniform_buffers.push_back({
            static_cast<NSUInteger>(binding.internal_binding),
            static_cast<NSUInteger>(binding.offset), resident,
            binding.vertex_stage, binding.fragment_stage});
''',
    '''        if (!resident) return false;
        if (pending.uniform_buffers.size() >= kMaxPendingUniformBufferBindings) {
            ML_LOG_ERROR("metal: resolved uniform-buffer bindings exceed fixed limit");
            return false;
        }
        pending.uniform_buffers.push_back({
            static_cast<NSUInteger>(binding.internal_binding),
            static_cast<NSUInteger>(binding.offset), resident,
            binding.vertex_stage, binding.fragment_stage});
''')

replace_once(
    "src/metal/engine.mm",
    '''        pending.textures.push_back({
            slot, texture->second.texture, sampler,
            texture->second.backing_buffer,
            bind.vertex_stage, bind.fragment_stage});
''',
    '''        if (pending.textures.size() >= kMaxPendingTextureBindings) {
            ML_LOG_ERROR("metal: resolved sampled-image bindings exceed fixed limit");
            return false;
        }
        pending.textures.push_back({
            slot, texture->second.texture, sampler,
            texture->second.backing_buffer,
            bind.vertex_stage, bind.fragment_stage});
''')

# Frontend metadata views become invalid once Draw returns. Native objects were
# retained above, so deferred PendingDraw must carry no borrowed frontend views.
replace_once(
    "src/metal/engine.mm",
    '''    pending.params.loose_uniforms.values = nullptr;
    pending.params.loose_uniforms.count = 0;
    for (auto& binding : pending.params.uniform_buffers) {
        binding.source_data = nullptr;
        binding.source_size = 0;
    }
''',
    '''    pending.params.loose_uniforms = {};
    pending.params.uniform_buffers = {};
    pending.params.sampled_textures = {};
''')

print("borrowed draw metadata views applied")
