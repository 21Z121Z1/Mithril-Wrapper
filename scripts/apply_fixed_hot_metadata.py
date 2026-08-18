#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    n = text.count(old)
    if n != 1:
        raise SystemExit(f"{path}: expected one anchor, found {n}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1))

# A reusable fixed-capacity sequence for renderer metadata with API-defined
# limits. It never allocates and preserves vector-like iteration/indexing.
replace_once(
    "src/backend/types.h",
    '''constexpr uint32_t kMaxTextureUnits = 16;

enum class Topology {
''',
    '''constexpr uint32_t kMaxTextureUnits = 16;
constexpr size_t kMaxVertexAttributes = 16;

template <typename T, size_t Capacity>
class FixedList {
public:
    bool push_back(const T& value) {
        if (size_ >= Capacity) return false;
        values_[size_++] = value;
        return true;
    }
    void clear() { size_ = 0; }
    void reserve(size_t requested) const { (void)requested; }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    T* data() { return values_.data(); }
    const T* data() const { return values_.data(); }
    T& front() { return values_[0]; }
    const T& front() const { return values_[0]; }
    T& operator[](size_t index) { return values_[index]; }
    const T& operator[](size_t index) const { return values_[index]; }
    auto begin() { return values_.begin(); }
    auto end() { return values_.begin() + static_cast<ptrdiff_t>(size_); }
    auto begin() const { return values_.begin(); }
    auto end() const { return values_.begin() + static_cast<ptrdiff_t>(size_); }
private:
    std::array<T, Capacity> values_{};
    size_t size_ = 0;
};

enum class Topology {
''')

replace_once(
    "src/backend/types.h",
    '''    std::vector<uint8_t> data;
    uint32_t stride = 0;
    std::vector<VertexAttr> attrs;
''',
    '''    std::vector<uint8_t> data;
    uint32_t stride = 0;
    FixedList<VertexAttr, kMaxVertexAttributes> attrs;
''')

# Shared frontend state uses only API/linker-bounded arrays.
replace_once(
    "src/gl/draw.cpp",
    '''    uint64_t backend_program = 0;
    std::vector<GLuint> vertex_slots;
    std::vector<GLuint> instance_slots;
    std::vector<sh::VertexInput> constant_inputs;
    v::LooseUniformSource loose_uniforms;
    std::vector<v::UniformBufferBinding> uniform_buffers;
    std::vector<v::SampledTextureBinding> sampled_textures;
''',
    '''    uint64_t backend_program = 0;
    v::FixedList<GLuint, kMaxAttribs> vertex_slots;
    v::FixedList<GLuint, kMaxAttribs> instance_slots;
    v::FixedList<sh::VertexInput, kMaxAttribs> constant_inputs;
    v::LooseUniformSource loose_uniforms;
    v::FixedList<v::UniformBufferBinding,
                 sh::kMaxUserUniformBlocksPerStage * 2> uniform_buffers;
    v::FixedList<v::SampledTextureBinding,
                 v::kMaxTextureUnits * 2> sampled_textures;
''')

# Vertex attributes are also capped at 16. This removes the last tiny vector
# allocation from the representable resident-VBO path.
replace_once(
    "src/gl/draw.cpp",
    '''        std::vector<v::VertexAttr> native_attrs;
''',
    '''        v::FixedList<v::VertexAttr, v::kMaxVertexAttributes> native_attrs;
''')

# Vulkan's reference backend intentionally keeps its existing deferred DrawOp
# vectors. Copy the fixed frontend list into those vectors explicitly.
replace_once(
    "src/vk/draw.cpp",
    '''    op.v_stride = params.vertex_stream.stride;
    op.v_attrs = params.vertex_stream.attrs;
    op.i_stride = params.instance_stream.stride;
    op.i_attrs = params.instance_stream.attrs;
''',
    '''    op.v_stride = params.vertex_stream.stride;
    op.v_attrs.assign(params.vertex_stream.attrs.begin(),
                      params.vertex_stream.attrs.end());
    op.i_stride = params.instance_stream.stride;
    op.i_attrs.assign(params.instance_stream.attrs.begin(),
                      params.instance_stream.attrs.end());
''')

print("fixed-capacity hot metadata applied")
