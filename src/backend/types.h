// Backend-neutral render/resource descriptions produced by the GL frontend.
//
// These types describe resolved GL semantics. They deliberately contain no
// Vulkan or Metal handles so every native backend consumes the same snapshot.

#pragma once

#include <GL/glcorearb.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mithril::backend {

constexpr uint32_t kMaxTextureUnits = 16;
constexpr size_t kMaxVertexAttributes = 16;

// Fixed-capacity metadata storage for API-bounded hot state. push_back reports
// overflow instead of reallocating; valid callers derive Capacity from the GL
// or shader contract, so overflow is a violated renderer invariant rather than
// a representable state that should spill to the heap.
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
    Triangles = 0,
    TriangleStrip = 1,
    TriangleFan = 2,
    Lines = 3,
    LineStrip = 4,
};

// Result of a client-side wait on a backend execution fence.  The GL layer
// maps this small semantic set to GL_ALREADY_SIGNALED / GL_CONDITION_SATISFIED
// / GL_TIMEOUT_EXPIRED / GL_WAIT_FAILED without exposing native handles.
enum class SyncWaitResult {
    AlreadySignaled = 0,
    ConditionSatisfied,
    TimeoutExpired,
    Failed,
};

enum class VertexScalarType {
    Float32 = 0,
    Float16,
    Sint8,
    Uint8,
    Sint16,
    Uint16,
    Sint32,
    Uint32,
};

inline uint32_t VertexScalarBytes(VertexScalarType type) {
    switch (type) {
        case VertexScalarType::Sint8:
        case VertexScalarType::Uint8: return 1;
        case VertexScalarType::Float16:
        case VertexScalarType::Sint16:
        case VertexScalarType::Uint16: return 2;
        default: return 4;
    }
}

struct VertexAttr {
    uint32_t location = 0;
    uint32_t components = 0;
    uint32_t offset = 0;
    VertexScalarType scalar_type = VertexScalarType::Float32;
    bool normalized = false;
};

struct VertexStream {
    // Transient compatibility path: frontend-resolved typed records. This is
    // byte storage because one interleaved record may contain float, sint and
    // uint inputs with distinct shader ABIs.
    std::vector<uint8_t> data;
    uint32_t stride = 0;
    FixedList<VertexAttr, kMaxVertexAttributes> attrs;

    // Resident-source fast path. `source_data` is borrowed only for the
    // synchronous backend Draw() call. Backends retain/copy it before Draw
    // returns and key reuse on the never-reused lifetime id plus content
    // version. This keeps the contract extensible beyond per-draw repacking.
    const uint8_t* source_data = nullptr;
    size_t source_size = 0;
    uint64_t source_lifetime_id = 0;
    uint64_t source_content_version = 0;
    uint64_t source_previous_content_version = 0;
    uint64_t source_update_offset = 0;
    uint64_t source_update_size = 0;
    bool source_update_is_partial = false;
    uint64_t binding_offset = 0;
    uint32_t record_count = 0;

    bool HasStorage() const { return !data.empty() || source_data != nullptr; }
    bool HasResidentSource() const {
        return source_data != nullptr && source_size != 0 &&
               source_lifetime_id != 0;
    }
};

enum class IndexScalarType : uint8_t {
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
    bool scissor_test = false;
    bool depth_test = false;
    GLenum depth_func = GL_LESS;
    GLboolean depth_write = GL_TRUE;
    bool stencil_test = false;
    GLenum stencil_front_func = GL_ALWAYS;
    GLenum stencil_back_func = GL_ALWAYS;
    GLint stencil_front_ref = 0;
    GLint stencil_back_ref = 0;
    GLuint stencil_front_read_mask = 0xFFFFFFFFu;
    GLuint stencil_back_read_mask = 0xFFFFFFFFu;
    GLuint stencil_front_write_mask = 0xFFFFFFFFu;
    GLuint stencil_back_write_mask = 0xFFFFFFFFu;
    GLenum stencil_front_op_fail = GL_KEEP;
    GLenum stencil_front_op_zfail = GL_KEEP;
    GLenum stencil_front_op_zpass = GL_KEEP;
    GLenum stencil_back_op_fail = GL_KEEP;
    GLenum stencil_back_op_zfail = GL_KEEP;
    GLenum stencil_back_op_zpass = GL_KEEP;
    bool blend_enable = false;
    GLenum blend_src_rgb = GL_ONE, blend_dst_rgb = GL_ZERO;
    GLenum blend_src_alpha = GL_ONE, blend_dst_alpha = GL_ZERO;
    GLenum blend_eq_rgb = GL_FUNC_ADD, blend_eq_alpha = GL_FUNC_ADD;
    float blend_color[4] = {0, 0, 0, 0};
    bool cull_test = false;
    GLenum cull_face = GL_BACK;
    GLenum front_face = GL_CCW;
    GLenum polygon_mode = GL_FILL;
    float poly_offset_factor = 0.f, poly_offset_units = 0.f;
    GLboolean color_wmask_r = GL_TRUE, color_wmask_g = GL_TRUE;
    GLboolean color_wmask_b = GL_TRUE, color_wmask_a = GL_TRUE;
};

// GL dynamic raster state is observable at each draw.  It must travel with
// the draw description because native backends may defer command encoding.
struct DynamicState {
    std::array<float, 4> viewport{0.f, 0.f, 0.f, 0.f};
    std::array<float, 4> scissor{0.f, 0.f, 0.f, 0.f};
};

// One resolved GL uniform-block binding. The source pointer is borrowed only
// for the synchronous backend Draw() call; native backends retain versioned
// storage before returning. Internal binding numbers come from shared shader
// lowering and are deliberately distinct from GL indexed binding points.
struct UniformBufferBinding {
    uint32_t internal_binding = 0;
    bool vertex_stage = false;
    bool fragment_stage = false;
    const uint8_t* source_data = nullptr;
    size_t source_size = 0;
    uint64_t source_lifetime_id = 0;
    uint64_t source_content_version = 0;
    uint64_t source_previous_content_version = 0;
    uint64_t source_update_offset = 0;
    uint64_t source_update_size = 0;
    bool source_update_is_partial = false;
    uint64_t offset = 0;
    uint64_t size = 0;
};

struct UniformValueView {
    const uint8_t* data = nullptr;
    uint32_t size = 0;
};

struct LooseUniformSource {
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

// Native shader reflection for one member of the synthetic loose-uniform
// block. GL setters expose tightly packed scalar sequences, while std140/MSL
// layouts may add a stride between array elements or matrix rows/columns.
struct UniformMemberLayout {
    std::string name;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t vector_components = 1;
    uint32_t matrix_columns = 1;
    uint32_t array_elements = 1;
    uint32_t array_stride = 0;
    uint32_t matrix_stride = 0;
    bool row_major = false;
};

// Copy one GL uniform snapshot into its reflected block layout. Values use
// 32-bit GL scalar representations and matrices are normalized column-major.
bool PackUniformValue(const UniformMemberLayout& layout,
                      const uint8_t* value_data, size_t value_size,
                      uint8_t* block, size_t block_size);
inline bool PackUniformValue(const UniformMemberLayout& layout,
                             const std::vector<uint8_t>& value,
                             uint8_t* block, size_t block_size) {
    return PackUniformValue(layout, value.data(), value.size(), block, block_size);
}

enum class TexFilter { Nearest = 0, Linear = 1 };
enum class TexMipFilter { None = 0, Nearest = 1, Linear = 2 };

// Pixel representation is part of a texture-buffer binding's ABI. Ordinary
// image uploads currently normalize to RGBA8Unorm; texel buffers keep their
// declared scalar/integer type so samplerBuffer/isamplerBuffer/usamplerBuffer
// cannot silently reinterpret the same bytes differently per backend.
enum class TexelFormat {
    RGBA8Unorm = 0,
    Depth32Float,
    R8Sint,
    R8Uint,
    R32Sint,
    R32Uint,
    R32Float,
};

// Fully resolved sampling state. It is captured with each draw because a GL
// sampler object is bound to a texture unit, not owned by the texture, and the
// same image may be sampled through different state in one deferred batch.
struct TexSamplerInfo {
    TexFilter mag = TexFilter::Linear;
    TexFilter min = TexFilter::Linear;
    TexMipFilter mip = TexMipFilter::None;
    GLenum wrap_s = GL_REPEAT, wrap_t = GL_REPEAT, wrap_r = GL_REPEAT;
    float min_lod = -1000.0f;
    float max_lod = 1000.0f;
    float lod_bias = 0.0f;
    std::array<float, 4> border_color{0.f, 0.f, 0.f, 0.f};
    GLenum compare_mode = GL_NONE;
    GLenum compare_func = GL_LEQUAL;
};

struct SampledTextureBinding {
    uint32_t binding = 0;
    uint64_t texture = 0;
    TexSamplerInfo sampler;
    bool vertex_stage = false;
    bool fragment_stage = false;
};

struct ClearParams {
    GLbitfield mask = 0;
    // -1 broadcasts glClear to every active draw buffer. Non-negative values
    // identify GL_DRAW_BUFFERi for glClearBuffer*(GL_COLOR, i, ...).
    int32_t color_drawbuffer = -1;
    std::array<float, 4> color{0.f, 0.f, 0.f, 0.f};
    double depth = 1.0;
    uint32_t stencil = 0;
    bool scissor_test = false;
    std::array<int32_t, 4> scissor{0, 0, 0, 0};
    std::array<GLboolean, 4> color_write{
        GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    GLboolean depth_write = GL_TRUE;
    uint32_t stencil_write_mask = 0xFFFFFFFFu;
};

struct DrawParams {
    uint64_t program = 0;
    VertexStream vertex_stream;
    VertexStream instance_stream;
    std::vector<uint32_t> indices;
    ResidentIndexSource resident_indices;
    // Index values matching the GL restart index are normalized by the
    // frontend to UINT32_MAX. Metal consumes that sentinel natively; Vulkan
    // uses this flag to enable its matching input-assembly behavior.
    bool primitive_restart = false;
    // Backend query generation active when this draw was issued. Zero means
    // visibility testing is disabled for this draw.
    uint64_t occlusion_query = 0;
    uint32_t instance_count = 1;
    Topology topology = Topology::Triangles;
    // Borrowed only for the synchronous backend Draw() call. Program-local
    // setters own the byte arrays; deferred native backends must snapshot them
    // before Draw returns.
    LooseUniformSource loose_uniforms;
    ArrayView<UniformBufferBinding> uniform_buffers;
    ArrayView<SampledTextureBinding> sampled_textures;
    PipelineState pipeline;
    DynamicState dynamic;
};

struct TexUpload {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;
    // Multisample images have one implicit level and no CPU pixel payload.
    // Ordinary uploads keep samples == 1 and provide mip storage below.
    uint32_t samples = 1;
    bool is_multisample = false;
    bool is_3d = false;
    bool is_cube = false;
    bool is_buffer = false;
    TexelFormat format = TexelFormat::RGBA8Unorm;
    std::vector<std::vector<uint8_t>> mip;
    uint64_t content_version = 0;
};

struct FboAttach {
    bool is_texture = false;
    uint64_t tex_id = 0;
    uint32_t level = 0;
    uint32_t layer = 0;
    uint64_t rbo_id = 0;
};

struct FboSpec {
    std::vector<FboAttach> colors;
    FboAttach depth_stencil;
    bool has_depth_stencil = false;
    GLenum depth_stencil_format = GL_DEPTH24_STENCIL8;
    std::vector<GLenum> draw_bufs;
    GLenum read_buf = GL_COLOR_ATTACHMENT0;
};

} // namespace mithril::backend
