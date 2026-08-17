#!/usr/bin/env python3
"""Replace DirectMetal per-lookup string cache keys with fixed numeric keys."""
from pathlib import Path
import re


def exact(path, old, new, count=1):
    p = Path(path); text = p.read_text(); actual = text.count(old)
    if actual != count:
        raise SystemExit(f"{path}: expected {count}, found {actual}: {old[:120]!r}")
    p.write_text(text.replace(old, new, count))


def regex(path, pattern, replacement, count=1):
    p = Path(path); text = p.read_text()
    result, actual = re.subn(pattern, replacement, text, count=count,
                             flags=re.MULTILINE | re.DOTALL)
    if actual != count:
        raise SystemExit(f"{path}: regex expected {count}, found {actual}: {pattern[:120]!r}")
    p.write_text(result)


# Collision-safe fixed key: unordered_map hashes the numeric words, then full
# equality compares every canonical word. The hash is never used as identity.
key_types = r'''
template <size_t Capacity>
struct FixedNumericKey {
    std::array<uint64_t, Capacity> words{};
    uint16_t count = 0;

    bool Push(uint64_t value) {
        if (count >= Capacity) return false;
        words[count++] = value;
        return true;
    }

    bool operator==(const FixedNumericKey& other) const {
        if (count != other.count) return false;
        for (uint16_t i = 0; i < count; ++i)
            if (words[i] != other.words[i]) return false;
        return true;
    }
};

template <size_t Capacity>
struct FixedNumericKeyHash {
    size_t operator()(const FixedNumericKey<Capacity>& key) const noexcept {
        uint64_t hash = 1469598103934665603ULL;
        for (uint16_t i = 0; i < key.count; ++i) {
            uint64_t value = key.words[i];
            for (int byte = 0; byte < 8; ++byte) {
                hash ^= static_cast<uint8_t>(value);
                hash *= 1099511628211ULL;
                value >>= 8;
            }
        }
        hash ^= key.count;
        hash *= 1099511628211ULL;
        return static_cast<size_t>(hash);
    }
};

using PipelineCacheKey = FixedNumericKey<96>;
using PipelineCacheKeyHash = FixedNumericKeyHash<96>;
using SamplerCacheKey = FixedNumericKey<24>;
using SamplerCacheKeyHash = FixedNumericKeyHash<24>;
'''
exact("src/metal/engine.mm",
'''MithrilDirectMetalBufferStatsV1 EmptyBufferStats() {
    MithrilDirectMetalBufferStatsV1 stats{};
    stats.version = MITHRIL_DIRECT_METAL_BUFFER_STATS_VERSION;
    stats.struct_size = static_cast<uint32_t>(sizeof(stats));
    return stats;
}

struct Engine {
''',
'''MithrilDirectMetalBufferStatsV1 EmptyBufferStats() {
    MithrilDirectMetalBufferStatsV1 stats{};
    stats.version = MITHRIL_DIRECT_METAL_BUFFER_STATS_VERSION;
    stats.struct_size = static_cast<uint32_t>(sizeof(stats));
    return stats;
}
''' + key_types + '''
struct Engine {
''')

exact("src/metal/engine.mm",
'''    std::unordered_map<uint64_t, Program> programs;
    std::unordered_map<std::string, PipelineBundle> pipelines;
    std::unordered_map<std::string, ClearPipeline> clear_pipelines;
    std::unordered_map<uint64_t, ResidentBuffer> resident_buffers;
    std::unordered_map<uint64_t, ResidentTexture> textures;
    std::unordered_map<std::string, CachedSampler> samplers;
''',
'''    std::unordered_map<uint64_t, Program> programs;
    std::unordered_map<PipelineCacheKey, PipelineBundle, PipelineCacheKeyHash>
        pipelines;
    std::unordered_map<std::string, ClearPipeline> clear_pipelines;
    std::unordered_map<uint64_t, ResidentBuffer> resident_buffers;
    std::unordered_map<uint64_t, ResidentTexture> textures;
    std::unordered_map<SamplerCacheKey, CachedSampler, SamplerCacheKeyHash>
        samplers;
''')

# Canonical sampler key. Dormant state that cannot affect the Metal descriptor
# is zeroed so irrelevant GL state does not fragment the native sampler cache.
sampler_builder = r'''uint32_t FloatBits(float value) {
    uint32_t output = 0;
    static_assert(sizeof(output) == sizeof(value));
    std::memcpy(&output, &value, sizeof(output));
    return output;
}

SamplerCacheKey BuildSamplerCacheKey(const backend::TexSamplerInfo& info,
                                     NSUInteger levels) {
    SamplerCacheKey key;
    key.Push(static_cast<uint8_t>(info.mag));
    key.Push(static_cast<uint8_t>(info.min));
    key.Push(static_cast<uint8_t>(info.mip));
    key.Push(info.wrap_s);
    key.Push(info.wrap_t);
    key.Push(info.wrap_r);
    key.Push(FloatBits(info.lod_bias));
    key.Push(info.compare_mode);

    if (info.mip != backend::TexMipFilter::None) {
        key.Push(FloatBits(info.min_lod));
        key.Push(FloatBits(info.max_lod));
        key.Push(levels);
    } else {
        key.Push(0); key.Push(0); key.Push(0);
    }

    key.Push(info.compare_mode != GL_NONE ? info.compare_func : 0);
    const bool uses_border = info.wrap_s == GL_CLAMP_TO_BORDER ||
                             info.wrap_t == GL_CLAMP_TO_BORDER ||
                             info.wrap_r == GL_CLAMP_TO_BORDER;
    for (float component : info.border_color)
        key.Push(uses_border ? FloatBits(component) : 0);
    return key;
}
'''
regex("src/metal/engine.mm",
      r'std::string SamplerCacheKey\(const backend::TexSamplerInfo& info,\s*NSUInteger levels\) \{.*?\n\}\n\n(?=bool ResolveMetalBorderColor)',
      sampler_builder)
exact("src/metal/engine.mm",
'''    const std::string key = SamplerCacheKey(info, levels);
    auto cached = engine.samplers.find(key);
''',
'''    const SamplerCacheKey key = BuildSamplerCacheKey(info, levels);
    auto cached = engine.samplers.find(key);
''')

# Pipeline key mirrors precisely the state that contributes to the native
# pipeline/depth-stencil descriptors. Vertex attributes are packed into one
# word each; target draw-buffer identity is canonicalized to the enabled mask.
pipeline_builder = r'''uint64_t PackVertexAttributeKey(const backend::VertexAttr& attr) {
    return static_cast<uint64_t>(attr.location & 0x3fu) |
           (static_cast<uint64_t>(attr.components & 0x7u) << 6) |
           (static_cast<uint64_t>(static_cast<uint8_t>(attr.scalar_type) & 0xfu) << 9) |
           (static_cast<uint64_t>(attr.normalized ? 1u : 0u) << 13) |
           (static_cast<uint64_t>(attr.offset) << 16);
}

bool AppendVertexStreamKey(PipelineCacheKey* key,
                           const backend::VertexStream& stream) {
    if (!key->Push(stream.stride) || !key->Push(stream.attrs.size())) return false;
    for (const auto& attr : stream.attrs)
        if (!key->Push(PackVertexAttributeKey(attr))) return false;
    return true;
}

bool AppendPipelineStateKey(PipelineCacheKey* key,
                            const backend::PipelineState& state) {
    const uint64_t values[] = {
        state.depth_test, state.depth_func, state.depth_write,
        state.stencil_test, state.stencil_front_func, state.stencil_back_func,
        state.stencil_front_read_mask, state.stencil_back_read_mask,
        state.stencil_front_write_mask, state.stencil_back_write_mask,
        state.stencil_front_op_fail, state.stencil_front_op_zfail,
        state.stencil_front_op_zpass, state.stencil_back_op_fail,
        state.stencil_back_op_zfail, state.stencil_back_op_zpass,
        state.blend_enable, state.blend_src_rgb, state.blend_dst_rgb,
        state.blend_src_alpha, state.blend_dst_alpha,
        state.blend_eq_rgb, state.blend_eq_alpha,
        state.color_wmask_r, state.color_wmask_g,
        state.color_wmask_b, state.color_wmask_a,
    };
    for (uint64_t value : values)
        if (!key->Push(value)) return false;
    return true;
}

bool BuildPipelineCacheKey(const backend::DrawParams& params,
                           const ResolvedTarget& target,
                           const backend::FboSpec* fbo_spec,
                           PipelineCacheKey* key) {
    *key = {};
    if (!key->Push(params.program) ||
        !key->Push(static_cast<uint8_t>(params.topology)) ||
        !AppendVertexStreamKey(key, params.vertex_stream) ||
        !AppendVertexStreamKey(key, params.instance_stream) ||
        !AppendPipelineStateKey(key, params.pipeline) ||
        !key->Push(target.colors.size()))
        return false;

    uint64_t present_mask = 0;
    uint64_t enabled_mask = 0;
    for (NSUInteger i = 0; i < target.colors.size(); ++i) {
        if (target.colors[i]) present_mask |= 1ULL << i;
        bool enabled = true;
        if (fbo_spec && !fbo_spec->draw_bufs.empty()) {
            enabled = false;
            for (GLenum draw_buffer : fbo_spec->draw_bufs)
                if (draw_buffer == GL_COLOR_ATTACHMENT0 + i) enabled = true;
        }
        if (enabled) enabled_mask |= 1ULL << i;
    }
    if (!key->Push(present_mask) || !key->Push(enabled_mask) ||
        !key->Push(target.depth_stencil != nil) ||
        !key->Push(target.depth_stencil
            ? static_cast<uint64_t>(target.depth_stencil.pixelFormat)
            : static_cast<uint64_t>(MTLPixelFormatInvalid)) ||
        !key->Push(target.has_stencil) || !key->Push(target.samples))
        return false;
    return true;
}
'''
regex("src/metal/engine.mm",
      r'void AppendPipelineState\(std::ostringstream& key,\s*const backend::PipelineState& state\) \{.*?\n\}\n\nstd::string PipelineKey\(const backend::DrawParams& params\) \{.*?\n\}\n\n(?=MTLStencilDescriptor\*)',
      pipeline_builder)

old_head = '''PipelineBundle* GetOrCreatePipeline(const backend::DrawParams& params) {
    auto& engine = GetEngine();
    ResolvedTarget target;
    if (!ResolveTarget(engine.bound_draw_fbo, &target)) return nullptr;
    std::ostringstream target_key;
    target_key << PipelineKey(params) << "|rt:" << target.colors.size() << ':'
               << (target.depth_stencil != nil)
               << ':' << (target.depth_stencil
                    ? target.depth_stencil.pixelFormat : MTLPixelFormatInvalid)
               << ':' << target.has_stencil << ':' << target.samples;
    if (engine.bound_draw_fbo) {
        auto fbo = engine.framebuffers.find(engine.bound_draw_fbo);
        if (fbo != engine.framebuffers.end())
            for (GLenum draw_buffer : fbo->second.spec.draw_bufs)
                target_key << ':' << draw_buffer;
    }
    const std::string key = target_key.str();
    auto cached = engine.pipelines.find(key);
'''
new_head = '''PipelineBundle* GetOrCreatePipeline(const backend::DrawParams& params) {
    auto& engine = GetEngine();
    ResolvedTarget target;
    if (!ResolveTarget(engine.bound_draw_fbo, &target)) return nullptr;
    const backend::FboSpec* fbo_spec = nullptr;
    if (engine.bound_draw_fbo) {
        auto fbo = engine.framebuffers.find(engine.bound_draw_fbo);
        if (fbo != engine.framebuffers.end()) fbo_spec = &fbo->second.spec;
    }
    PipelineCacheKey key;
    if (!BuildPipelineCacheKey(params, target, fbo_spec, &key)) {
        ML_LOG_ERROR("metal: pipeline key exceeds fixed hot-path capacity");
        return nullptr;
    }
    auto cached = engine.pipelines.find(key);
'''
exact("src/metal/engine.mm", old_head, new_head)
# fbo_spec is now resolved before the lookup; remove the duplicate miss-only block.
exact("src/metal/engine.mm",
'''    const backend::FboSpec* fbo_spec = nullptr;
    if (engine.bound_draw_fbo) {
        auto fbo = engine.framebuffers.find(engine.bound_draw_fbo);
        if (fbo != engine.framebuffers.end()) fbo_spec = &fbo->second.spec;
    }
    for (NSUInteger i = 0; i < target.colors.size(); ++i) {
''',
'''    for (NSUInteger i = 0; i < target.colors.size(); ++i) {
''')

# Source-shape assertions: other cold/change-time ostringstream uses may remain,
# but sampler/pipeline hit paths must not construct strings or string-key maps.
engine = Path("src/metal/engine.mm").read_text()
for forbidden in (
    "std::string SamplerCacheKey(",
    "std::string PipelineKey(",
    "std::unordered_map<std::string, PipelineBundle>",
    "std::unordered_map<std::string, CachedSampler>",
    "std::ostringstream target_key;",
):
    if forbidden in engine:
        raise SystemExit(f"hot string-cache identity remains: {forbidden}")
for required in (
    "FixedNumericKey<96>",
    "BuildSamplerCacheKey",
    "BuildPipelineCacheKey",
    "resident_buffer_pool",  # ensure phase-2 residency survives the refactor
):
    if required not in engine:
        raise SystemExit(f"missing numeric-cache invariant: {required}")

print("DirectMetal numeric pipeline/sampler cache keys applied successfully")
