# OpenGL 状态机重写规范

> 深度参考 [MobileGL](https://github.com/MobileGL-Dev/MobileGL) 的状态机架构设计，但**不照搬其代码**，仅借鉴设计思想。目标是重写 `/workspace/Mithril-Wrapper-cpp/MG_State/` 的 OpenGL 状态机，使其**正确、健壮、符合 OpenGL 3.3 Core Profile 规范、无已知 bug**。
>
> 配套文件：[checklist.md](./checklist.md) · [tasks.md](./tasks.md)

---

## 一、目标与范围

### 1.1 目标
1. **正确性**：消除当前状态机中所有已识别的功能性 bug（P0）与规范违规（P1）。
2. **健壮性**：对象生命周期、名称分配、上下文隔离、错误处理全部符合 GL 3.3 Core 规范语义，避免崩溃与状态泄漏。
3. **完整性**：补全 Core Profile 必需但当前缺失的状态分类（sampler / query / sync / transform feedback / renderbuffer / indexed buffer binding points / 完整查询入口）。
4. **可演进**：预留 dirty 版本号与对象 content-version 接口，便于后续 backend 优化（本次不强制 backend 消费）。

### 1.2 范围（in-scope）
- `MG_State/State.h` 与 `MG_State/State.cpp` 完全重写。
- `MG_Impl/*.cpp` 中**因状态机使用错误或字段缺失导致 bug** 的入口点修复（target 绑定、查询入口、错误处理、对象生命周期、默认值合规）。
- `egl/egl.cpp` 中 `eglMakeCurrent` 的 `g_state` 切换改为 thread_local。
- `MG_Impl/includes.h` 中 `MITHRIL_ENSURE_INIT` 的 phantom-state 修复。

### 1.3 范围（out-of-scope，本次不做）
- Vulkan/Metal backend（`MG_Backend/`）的 pipeline/资源/dirty 优化重写——仅保留状态机侧的版本号接口供未来使用。
- Share group（多 EGLContext 共享对象表）——MobileGL 本身也未实现，本次维持 per-context 独立对象表，仅在 spec 第 4.7 节说明取舍。
- GL 4.x 特性（compute / image load-store / subroutine / multi-draw indirect）——超出 GL 3.3 Core 目标。
- 已在 `specs/fix-red-black-screen/` 完成的红屏/黑屏修复（Y 翻转 / Z 重映射 / cullMode）不在本次重做，但须保证不被破坏。

### 1.4 设计原则（借鉴 MobileGL，非照搬）
1. **分层聚合**：`GLContext` 聚合多个职责单一的子状态组件，每个组件自管 name 分配 + 对象表 + 绑定槽。
2. **统一 `BindingSlot<T>`**：所有"当前绑定"用带 `Uint16` 版本号的模板包装；bind 时引用相同则不增版本，避免 spurious dirty。
3. **`NameAllocator`**：free_list + valid_bits，O(1) 有效性检查 + name 复用，符合 GL"name 可复用"语义。
4. **`SharedPtr` + lazy create + deferred delete**：`Gen*` 只分配 name；首次 `Bind` 才构造对象；`Delete` 只解绑并从表移除，对象在被其它对象（VAO 引用的 buffer、FBO 附件等）持有的 `SharedPtr` 释放前不死。
5. **默认对象真实存在**：VAO 0 / FBO 0 / 每个 target 的 Texture 0 在构造时创建并预绑定，永生不可删；纹理删除时槽回退到默认纹理。
6. **thread_local `g_state`**：每个线程独立 current context 指针，消除多线程互踩与 no-context phantom state。
7. **错误队列**：GL error 与内部 non-GL error 分离的 `std::deque`，符合规范累积语义；`glGetError` 返回真实最早错误。
8. **强类型枚举**：GL enum 在状态机内部转为 `enum class`（如 `BufferTarget` / `TextureTarget` / `Capability`），避免 magic number 与误用。
9. **不破坏现有访问语义**：保留 `g_state->` 全局指针访问语法与现有 `state_get_*` 查找函数签名，最小化 `MG_Impl` 改动面；新增能力通过新字段/新函数暴露。

---

## 二、现状缺陷总结（重写的动机）

### 2.1 P0 功能性 bug
| 编号 | 缺陷 | 后果 | 位置 |
|------|------|------|------|
| P0-1 | `g_state` 为进程级全局指针，EGL 在 `eglMakeCurrent` 中 swap | 多线程渲染互踩；no-context 时 `MITHRIL_ENSURE_INIT` 创建 phantom state | `State.cpp:7`、`egl.cpp:722`、`State.cpp:9-14` |
| P0-2 | `glGetError` 强制返回 `GL_NO_ERROR`，丢弃真实 error | error 路径完全失效，依赖 `glGetError` 的应用无法做错误恢复 | `Getter.cpp:107-113` |
| P0-3 | `glBindBuffer` 的非 ARRAY/ELEMENT/UNIFORM target 全部写入 `currentArrayBuffer` | `glCopyBufferSubData` / UBO / SSBO / PBO 行为错误，target 互相覆盖 | `Buffer.cpp:72-95` |
| P0-4 | `glBindBufferBase/Range` 完全忽略 index/offset/size | UBO 绑定点数组缺失，`glBindBufferRange` 等价于 `glBindBuffer` | `Buffer.cpp:210-221` |
| P0-5 | 纹理绑定 target-less 单槽位：`boundTextures[unit]` 不区分 target | 同一 unit 上 `glBindTexture(GL_TEXTURE_2D,A)` 后 `glBindTexture(GL_TEXTURE_CUBE_MAP,B)` 丢失 2D 绑定 | `Texture.cpp:38-52`、`State.h:187-188` |
| P0-6 | `glIsEnabled` 读 `enabledCaps` set，`glGetBooleanv(GL_DEPTH_TEST)` 读 bool 字段，两源不同步 | 同一 cap 查询结果不一致 | `gl.cpp:105-108`、`Getter.cpp:120` |
| P0-7 | `ELEMENT_ARRAY_BUFFER` 绑定同时存在 VAO 内 `elementArrayBuffer` 与全局 `currentIndexBuffer` 两份镜像 | 需手工同步，是 bug 温床 | `State.h:181,48`、`Buffer.cpp:82-85` |

### 2.2 P1 规范违规
| 编号 | 缺陷 | 位置 |
|------|------|------|
| P1-1 | `glIsBuffer/glIsTexture/glIsFramebuffer/glIsProgram/glIsShader/glIsVertexArray/glIsRenderbuffer/glIsSampler` 完全缺失 | `MG_Impl/` 全局无实现 |
| P1-2 | `glGetVertexAttrib*` / `glGetVertexAttribPointerv` 完全未实现 | `Getter.cpp` |
| P1-3 | `glGetFramebufferAttachmentParameteriv` 完全未实现 | — |
| P1-4 | `glGetTexParameteriv/fv` 永远返回 0；`glGetTexImage` no-op | `Stubs.cpp:213-215` |
| P1-5 | `glDeleteShader` 立即从表 erase 而非 marked-for-deletion（违反 GL 规范：attached shader 应延迟到 detach+delete） | `Program.cpp:28-31` |
| P1-6 | `glGetUniformLocation` 副作用地往 program 插入新 uniform（未 link 或不存在 uniform 应返回 -1） | `Program.cpp:308-323` |
| P1-7 | `glCheckFramebufferStatus` 永远返回 `GL_FRAMEBUFFER_COMPLETE`，不校验 | `Framebuffer.cpp:152-155` |
| P1-8 | 默认 `multisample=false`、`enabledCaps` 不含 `GL_DITHER`/`GL_MULTISAMPLE`（规范要求默认 enabled） | `State.h:176,256-257` |
| P1-9 | error 为单槽位非队列，且大量入口未 `state_set_error` | `State.cpp:82-85`、`Framebuffer.cpp`/`Drawing.cpp`/`gl.cpp` 无 error 设置 |
| P1-10 | `glDrawBuffers` 把 `GL_FRONT/GL_BACK` 映射到 `GL_COLOR_ATTACHMENT0`（仅对 default FBO 合法，对 user FBO 应拒绝） | `Framebuffer.cpp:137-139` |
| P1-11 | `glFramebufferTexture2D` 不校验 attachment 越界（`GL_COLOR_ATTACHMENT8` 在 `kMaxColorAttachments=8` 时 UB） | `Framebuffer.cpp:58-118` |
| P1-12 | `boundTextureTargets` / `drawBuffers` 数组 `{GL_TEXTURE_2D}` / `{GL_NONE}` aggregate init 只填 [0]，其余为 0 | `State.h:160,188` |
| P1-13 | `glActiveTexture` 范围外静默丢弃，不 `state_set_error(GL_INVALID_ENUM)` | `gl.cpp:302-307` |
| P1-14 | `glGetIntegeri_v` 永远返回 0（indexed query stub） | `Getter.cpp:273-277` |
| P1-15 | renderbuffer 全部 no-op，`glGenRenderbuffers` 不入表 | `Framebuffer.cpp:237-246` |
| P1-16 | sync object 全部 stub，`glFenceSync` 返回 sentinel `(GLsync)0x1` | `Drawing.cpp:415-443` |

### 2.3 P2 架构债务
| 编号 | 缺陷 |
|------|------|
| P2-1 | 完全缺失 sampler object / query object / transform feedback / program pipeline 状态管理 |
| P2-2 | `state_get_*` 硬编码 `g_state`，无法支持 share group 或显式 context 访问 |
| P2-3 | 大量 State.h 字段定义但从未使用（`isCompressed`、`Uniform::blockIndex/offset/...`、`multisample/sampleCoverage/...` 等） |
| P2-4 | name 分配单调递增不复用（`state_gen_names`），各 `glGen*` 实现不一致（有的绕过 `state_gen_names`） |
| P2-5 | SPIR-V 缓存为进程级静态，不随 context 销毁清理 |
| P2-6 | 缺少对象 content-version / bind-generation，backend 无法跳过重复状态下发 |
| P2-7 | `glDeleteBuffers` 不清理 VAO 中 `attribs[i].boundBuffer` 引用，已删 buffer 仍可被 draw 引用 |
| P2-8 | `glVertexAttribPointer` 中 `a.divisor = a.divisor;` 自赋值死代码 |

---

## 三、架构设计

### 3.1 顶层结构
状态机以 `GLContext` 为顶层类（`namespace mithril::gl`），聚合若干子状态组件。为最小化对 `MG_Impl` 现有代码的冲击，**`GLContext` 仍以 `GLState` 结构体的形式对外暴露**（保留 `g_state->` 访问语法），但其内部字段重构为子组件指针/内嵌结构，并提供完整的访问 API。

```
namespace mithril::gl {

// name 分配器（free_list + valid_bits）
class NameAllocator { ... };

// 带 Uint16 版本号的绑定槽
template <typename T> struct BindingSlot { ... };
template <typename T> struct BindingSlotRanged : BindingSlot<T> { Range range; bool hasExplicitRange; };

// 子状态组件（内嵌于 GLState）
struct BufferState { ... };
struct TextureState { ... };
struct SamplerState { ... };
struct VertexArrayState { ... };
struct FramebufferState { ... };
struct ProgramState { ... };
struct RenderState { ... };       // capabilities + blend/depth/stencil/rasterizer/pixelstore
struct ErrorState { ... };

// 顶层
struct GLState {
    NameAllocator bufferNames, textureNames, samplerNames, vaoNames, fboNames,
                  programNames, shaderNames, renderbufferNames, queryNames, tfNames;
    BufferState       buffers;
    TextureState      textures;
    SamplerState      samplers;
    VertexArrayState  vaos;
    FramebufferState  framebuffers;
    ProgramState      programs;
    RenderState       render;
    ErrorState        errors;
    // EGL default framebuffer 安装槽（保留现有字段语义）
    VkImageView eglDefaultColor = VK_NULL_HANDLE;
    ...
};

} // namespace
```

> **取舍说明**：MobileGL 用 class + private 成员 + 转发方法。本实现选择 struct + public 子组件，原因是 Mithril 的 `MG_Impl` 大量直接访问 `g_state->xxx` 字段；用 struct 公开字段可保持现有代码编译通过，仅增量修改出错路径。子组件内部仍封装不变量。

### 3.2 `NameAllocator`
```cpp
class NameAllocator {
    GLuint m_next = 1;                 // 0 保留给默认对象
    std::vector<GLuint> m_freeList;    // 已删除可复用
    std::vector<bool>   m_valid;       // name -> 是否有效（下标直访）
public:
    void generate(GLsizei n, GLuint* out);   // 优先消费 freeList，否则自增
    bool insert(GLuint id);                   // 显式插入（用于默认对象 name=0）
    void release(GLuint id);                  // 置 m_valid[id]=false，push freeList
    bool valid(GLuint id) const;              // O(1)
};
```
- 每个对象类型一个 `NameAllocator` 实例，互不复用。
- `glGen*` 统一走对应 allocator 的 `generate`，消除当前"有的绕过 state_gen_names"的不一致。
- `glDelete*` 走 `release`，name 可被后续 `glGen*` 复用（符合 GL 规范）。
- `glIs*` 用 `valid()` 实现 O(1) 判定。

### 3.3 `BindingSlot<T>`
```cpp
template <typename T>
struct BindingSlot {
    T* bound = nullptr;            // 指向对象表中的对象（非拥有）
    uint16_t version = 0;          // 每次 bind 改变自增
    void bind(T* obj) {
        if (bound == obj) return;  // 去重，不增版本
        bound = obj;
        ++version;
    }
    T* get() const { return bound; }
};
template <typename T>
struct BindingSlotRanged : BindingSlot<T> {
    GLintptr offset = 0;
    GLsizeiptr size = 0;
    bool hasExplicitRange = false;  // glBindBufferRange=true, glBindBufferBase=false
};
```
- 对象表用 `std::unordered_map<GLuint, T>`，`BindingSlot` 存裸指针（对象表 erase 前先解绑所有槽，保证指针有效）。
- 版本号供未来 backend 比较，本次重写不强制消费。
- `BindingSlotRanged` 用于 UBO/SSBO/TF/AtomicCounter 的 indexed binding points。

### 3.4 默认对象
| 对象 | name | 创建时机 | 行为 |
|------|------|---------|------|
| VAO 0 | 0 | `GLState` 构造 | 真实存在，预绑定；`glBindVertexArray(0)` 绑定它 |
| FBO 0 | 0 | `GLState` 构造 | 真实存在；drawBuffers[0]=GL_COLOR_ATTACHMENT0；EGL 在 `eglMakeCurrent` 安装 swapchain view |
| Texture 0 | 0 | `GLState` 构造，**每个 target 一个**（2D/3D/Cube/2DArray/...） | 每个纹理单元的每个 target 槽预绑定到对应默认纹理；`state_get_texture(0)` 仍返回 nullptr（符合"name 0 不是 GenTextures 产生"），但绑定槽指向默认纹理对象 |
| 默认 Buffer | — | 不需要（buffer 0 无意义，`state_get_buffer(0)` 返回 nullptr，bind 0 = unbind） |

> 默认纹理不进 `textures` 表（`glIsTexture(0)=false`），单独存于 `TextureState::defaultTextures[TextureTarget]` 数组。

### 3.5 对象生命周期
- **创建**：`glGen*` → `NameAllocator::generate`（仅分配 name，不创建对象）。首次 `glBind*` 时若 name 有效但对象不存在，懒构造并插入对象表。
- **删除**：`glDelete*` → 遍历所有相关绑定槽解绑（回退到默认对象或 0）→ 从对象表 erase → `NameAllocator::release`。
  - **buffer**：解绑所有 buffer target 槽 + 所有 indexed binding points + 所有 VAO 的 `attribs[i].boundBuffer`（==name 者置 0）+ 当前 VAO 的 `elementArrayBuffer`。
  - **texture**：解绑所有纹理单元所有 target 槽 → 回退到默认纹理。
  - **shader**：**marked-for-deletion**——仅标记 `markedForDeletion=true`，不立即从表 erase；当所有 attached program detach 它（或 program 被 delete）后才真正 erase。符合 GL 规范。
  - **program**：marked-for-deletion；若非 current program 则立即 erase，否则延迟到 `glUseProgram(0)` 后下次删除检查。
  - **FBO/VAO/sampler**：立即解绑 + erase。
- **`SharedPtr` 延迟释放**：对象被其它对象引用时（如 FBO 附件引用 texture、VAO 引用 buffer），引用方存对象指针；delete 时引用方在自身析构或重新 attach 时处理。**本次为简化，FBO 附件/VAO attrib 存 GLuint name 而非指针**（与现状一致），delete 时主动清零引用方 name（见上 buffer/texture 删除逻辑），避免悬垂。这与 MobileGL 的 SharedPtr 方案语义等价但更轻量。

### 3.6 线程模型：`thread_local g_state`
```cpp
// State.h
namespace mithril {
extern thread_local GLState* g_state;
}
// State.cpp
thread_local GLState* mithril::g_state = nullptr;
```
- `eglMakeCurrent` 设置**当前线程**的 `g_state = ctx->state`。
- `eglMakeCurrent(EGL_NO_CONTEXT)` 设当前线程 `g_state = nullptr`。
- `MITHRIL_ENSURE_INIT()` 修改：仅当 `g_state == nullptr` **且** EGL 未初始化时才创建全局 phantom state（兼容无 EGL 的单元测试场景）；若 EGL 已初始化但当前线程无 current context，则**不创建 phantom state**，GL 入口直接 `state_set_error(GL_INVALID_OPERATION)` 并返回（符合规范"无 current context 时 GL 调用产生 error"）。
- 引入 `bool g_eglInitialized`（EGL 首次 `eglInitialize` 时置 true），供 `MITHRIL_ENSURE_INIT` 判定。

### 3.7 Share group 取舍
本次**不实现** share group（多 context 共享对象表）。理由：
1. MobileGL 本身也未实现，单 global `pGLContext`。
2. Mithril 当前每 `EGLContext` 独立 `GLState*`，已提供 per-context 隔离，满足单 context 渲染场景。
3. Share group 涉及共享 buffer/texture/program/shader/sampler 表 + 私有 VAO/FBO/state，复杂度高且现有目标 App（单 context）不需要。
4. 预留接口：`GLState` 顶部注释说明"share group 未实现，所有对象表 per-context 私有"，后续如需可在 `GLState` 构造时接受 `std::shared_ptr<SharedTables>`。

### 3.8 `ErrorState`：错误队列
```cpp
struct ErrorState {
    std::deque<GLenum> glErrors;          // GL error 队列（FIFO）
    std::deque<std::string> internalErrors; // 内部 non-GL error（调试用）
    void recordGL(GLenum err);            // push_back
    GLenum popGL();                       // pop_front，空返回 GL_NO_ERROR
    void recordInternal(std::string msg);
    bool hasInternal() const;
    std::string popInternal();
    void clear();
};
```
- `state_set_error` → `errors.recordGL(err)`，**累积**而非首错优先。
- `glGetError` → `errors.popGL()`，返回真实最早错误。
- 队列长度上限（如 64），溢出时丢弃最新（防恶意 spam，但保留早期 error 供诊断）。
- 内部 error 通道供 backend 异常记录，不污染 `glGetError`。

### 3.9 Dirty 版本号（预留，本次不强制 backend 消费）
- `BindingSlot::version`：每次 bind 自增。
- `RenderState::version`：任一 rasterizer/blend/depth/stencil/pixelstore 字段变化时自增。
- 对象级 `uint16_t paramsVersion`（texture/sampler）/ `uint64_t contentVersion`（texture/buffer 像素/数据变化）。
- 本次仅在状态机侧维护这些版本号，**不改动 backend 读取逻辑**；backend 优化列为后续独立任务。

---

## 四、数据结构详细设计

### 4.1 BufferState
```cpp
enum class BufferTarget : int {
    Array, ElementArray, Uniform, CopyRead, CopyWrite, PixelPack, PixelUnpack,
    TransformFeedback, AtomicCounter, ShaderStorage, DrawIndirect, DispatchIndirect,
    Query, Parameter, Texture,  // GL 3.3 Core 全部 target
    Count
};
constexpr int kBufferTargetCount = (int)BufferTarget::Count;
constexpr int kMaxIndexedBindingPoints = 36; // >= GL_MAX_UNIFORM_BUFFER_BINDINGS

struct BufferState {
    std::unordered_map<GLuint, Buffer> objects;
    BindingSlot<Buffer> slots[kBufferTargetCount];                 // 非索引绑定
    BindingSlotRanged<Buffer> indexed[4][kMaxIndexedBindingPoints]; // [Uniform/TF/AtomicCounter/SSBO][index]
    int touchedIndexed[4] = {0,0,0,0};                              // 高水位标记
    Buffer* get(GLuint id);        // id==0 返回 nullptr
    // 注意：ElementArray 绑定不存这里，存当前 VAO（见 4.4）
};
```
- `glBindBuffer(target, buf)`：按 `BufferTarget` 枚举分发到对应 `slots[(int)target]`，**不再全部混入 Array**。
  - `GL_ELEMENT_ARRAY_BUFFER` 特殊：写入**当前 VAO** 的 `elementArrayBuffer`（不进 BufferState.slots）。
- `glBindBufferBase(target, index, buf)`：`indexed[(int)target][index].bind(buf); .hasExplicitRange=false;` + 更新 `touchedIndexed`。
- `glBindBufferRange(target, index, buf, offset, size)`：同上 + `SetRange({offset,size}, true)`，校验 offset 对齐（`GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT`）、size>0，否则 `GL_INVALID_VALUE`。
- `bound_buffer_for_target(target)`：从对应 slot 取（不再从 Array 取）。

### 4.2 TextureState
```cpp
enum class TextureTarget : int {
    _1D, _2D, _3D, CubeMap, Rectangle, _2DMultisample, Buffer,
    _1DArray, _2DArray, CubeMapArray, _2DMultisampleArray,
    Count
};
constexpr int kTextureTargetCount = (int)TextureTarget::Count;

struct TextureUnit {
    BindingSlot<Texture> slots[kTextureTargetCount];  // 每个 target 独立槽
    BindingSlot<Sampler> sampler;                     // glBindSampler 绑定
};

struct TextureState {
    std::unordered_map<GLuint, Texture> objects;
    TextureUnit units[kMaxTextureUnits];
    int activeUnit = 0;                                // GL_TEXTURE0 相对
    uint64_t bindGeneration = 0;                       // 任一纹理/sampler bind 自增
    int maxTouchedUnit = 0;                            // 高水位
    Texture defaultTextures[kTextureTargetCount];      // name=0，每 target 一个，不进 objects
    Texture* get(GLuint id);                           // id==0 返回 nullptr
};
```
- `glBindTexture(target, tex)`：`units[activeUnit].slots[(int)target].bind(...)`，**不再覆盖整个 unit**。若 tex 有效但未创建则懒构造。
- `glActiveTexture(unit)`：校验 `unit < kMaxTextureUnits`，否则 `GL_INVALID_ENUM`；设 `activeUnit`。
- `glBindSampler(unit, sampler)`：`units[unit].sampler.bind(...)`。
- 删除 texture：遍历所有 unit 所有 target 槽，==name 者回退到 `defaultTextures[target]`。
- **Texture 字段补全**（在现有基础上新增）：
  - `GLint baseLevel=0, maxLevel=1000`
  - `GLfloat minLod=-1000, maxLod=1000, lodBias=0`
  - `GLfloat maxAnisotropy=1.0`
  - `GLenum compareMode=GL_NONE, compareFunc=GL_LEQUAL`
  - `GLenum swizzleR=GL_RED, swizzleG=GL_GREEN, swizzleB=GL_BLUE, swizzleA=GL_ALPHA`
  - `GLint borderColorI[4]={0}, borderColorUI[4]={0}`（integer texture）
  - `bool immutable=false; GLint immutableLevels=0`
  - `GLsizei samples=0; bool fixedSampleLocations=false`（multisample）
  - `uint16_t paramsVersion=0; uint64_t contentVersion=0`
- `glTexParameter*` 完整支持上述所有 pname（含 `GL_TEXTURE_LOD_BIAS`/`GL_TEXTURE_MAX_ANISOTROPY_EXT`/`GL_TEXTURE_COMPARE_MODE`/`GL_TEXTURE_SWIZZLE_*`/`GL_TEXTURE_BASE_LEVEL`/`GL_TEXTURE_MAX_LEVEL`/`GL_TEXTURE_MIN/MAX_LOD`）。

### 4.3 SamplerState
```cpp
struct Sampler {
    GLuint id=0;
    uint64_t lifetimeId=0;          // 全局单调，不随 name 复用重置
    uint16_t version=0;
    // sampler 参数（与 Texture 内嵌 sampler 字段同构）
    GLint minFilter=GL_NEAREST_MIPMAP_LINEAR, magFilter=GL_LINEAR;
    GLint wrapS=GL_REPEAT, wrapT=GL_REPEAT, wrapR=GL_REPEAT;
    GLfloat minLod=-1000, maxLod=1000, lodBias=0, maxAnisotropy=1.0;
    GLenum compareMode=GL_NONE, compareFunc=GL_LEQUAL;
    GLfloat borderColor[4]={0,0,0,0};
    GLint borderColorI[4]={0}, borderColorUI[4]={0};
    bool markedForDeletion=false;
};
struct SamplerState {
    std::unordered_map<GLuint, Sampler> objects;
    NameAllocator* names;            // 指向 GLState::samplerNames
    Sampler* get(GLuint id);
};
```
- `glGenSamplers/glDeleteSamplers/glIsSampler/glBindSampler/glSamplerParameter*` 全部实现。
- draw 时 backend 优先用 `units[unit].sampler`，为空则用 texture 内嵌 sampler 参数（本次 backend 不改，仅状态机正确追踪）。
- `lifetimeId` 由静态原子计数器分配，供 backend cache 区分同名复用。

### 4.4 VertexArrayState
```cpp
struct VertexArray {
    GLuint id=0;
    VertexAttrib attribs[kMaxVertexAttribs];
    GLuint elementArrayBuffer=0;     // GL_ELEMENT_ARRAY_BUFFER 归属 VAO（唯一来源）
    uint16_t attribVersions[kMaxVertexAttribs]={0}; // per-attr 格式/缓冲版本
    uint32_t configVersion=0;        // 整体配置版本
    bool markedForDeletion=false;
};
struct VertexArrayState {
    std::unordered_map<GLuint, VertexArray> objects;
    VertexArray defaultVAO;          // name=0
    BindingSlot<VertexArray> bound;  // 当前绑定（默认指向 defaultVAO）
    VertexArray* get(GLuint id);     // id==0 返回 &defaultVAO
};
```
- **移除全局 `currentIndexBuffer`**（P0-7）。所有 ELEMENT_ARRAY 绑定读写都走 `bound.get()->elementArrayBuffer`。
- `glBindVertexArray(0)` → 绑定 `defaultVAO`。
- `glVertexAttribPointer`：写入 `bound.get()->attribs[index]`，**修复 `a.divisor=a.divisor` 自赋值**（P2-8，删除该行，不动 divisor）。
- `glDeleteVertexArrays`：解绑当前 VAO（回退 defaultVAO）+ erase。
- `glDeleteBuffers` 时遍历所有 VAO 的 `attribs[i].boundBuffer` 清零（P2-7）。

### 4.5 FramebufferState
```cpp
struct Framebuffer {
    GLuint id=0;
    FBOAttachment colors[kMaxColorAttachments];
    FBOAttachment depth, stencil;
    GLenum drawBuffers[kMaxColorAttachments]; // 全部初始化 GL_NONE
    GLenum readBuffer=GL_NONE;
    uint16_t attachmentVersions[3]={0,0,0};   // color/depth/stencil
    bool markedForDeletion=false;
};
struct FramebufferState {
    std::unordered_map<GLuint, Framebuffer> objects;
    Framebuffer defaultFBO;        // name=0
    BindingSlot<Framebuffer> draw; // 默认指向 defaultFBO
    BindingSlot<Framebuffer> read; // 默认指向 defaultFBO
    Framebuffer* getDraw();
    Framebuffer* getRead();
};
```
- `glBindFramebuffer(target, fbo)`：按 `GL_DRAW_FRAMEBUFFER`/`GL_READ_FRAMEBUFFER`/`GL_FRAMEBUFFER` 分发；未知 target → `GL_INVALID_ENUM`。
- **附件校验**（P1-11）：`glFramebufferTexture2D/Layer` 校验 attachment ∈ `GL_COLOR_ATTACHMENT0..N-1` / `GL_DEPTH` / `GL_STENCIL` / `GL_DEPTH_STENCIL`，越界 `GL_INVALID_ENUM`。
- **`glCheckFramebufferStatus`**（P1-7）真正校验：
  - 至少一个 color/depth/stencil 附件（draw buffer 配置）
  - 所有 color 附件尺寸、format、sample count 一致
  - depth/stencil 附件格式合法
  - 返回 `GL_FRAMEBUFFER_COMPLETE` / `GL_FRAMEBUFFER_INCOMPLETE_*` 之一
- `glDrawBuffers`（P1-10）：user FBO 只接受 `GL_COLOR_ATTACHMENT0..N-1`；default FBO 接受 `GL_FRONT`/`GL_BACK`/`GL_NONE`；非法 → `GL_INVALID_ENUM`。
- `glReadBuffer`：校验 mode 合法。
- `glGetFramebufferAttachmentParameteriv` 实现：返回 `GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE`/`_NAME`/`_TEXTURE_LEVEL`/`_TEXTURE_CUBE_MAP_FACE`/`_LAYERED`/`_LAYER`/`_RED_SIZE`/... 。

### 4.6 ProgramState / Shader 生命周期
```cpp
struct Shader {
    GLuint id=0; GLenum type; std::string source; bool compiled=false;
    std::string infoLog; std::vector<uint32_t> spirv;
    bool markedForDeletion=false;          // ★ 新增
    int attachCount=0;                     // 被 program attach 的次数
};
struct Program {
    // 现有字段保留（含 vertexSpirv/vertexSpirvYFlipped/fragmentSpirv——红屏修复不破坏）
    bool markedForDeletion=false;          // ★ 新增
    bool inUse=false;                      // == (currentProgram==id)
};
```
- `glDeleteShader`（P1-5）：标记 `markedForDeletion=true`；若 `attachCount==0` 则立即 erase，否则延迟到所有 program detach。
- `glDetachShader` / `glDeleteProgram` 时递减 attachCount，归零且 markedForDeletion 则 erase shader。
- `glDeleteProgram`：标记 `markedForDeletion`；若 `currentProgram!=id` 立即 erase + 释放 backend 资源，否则延迟到 `glUseProgram(0)`。
- `glGetUniformLocation`（P1-6）：**移除副作用创建**。未 link 返回 -1（`GL_INVALID_OPERATION`）；linked 但 uniform 不存在返回 -1（不插入新条目）。

### 4.7 RenderState（capabilities + blend/depth/stencil/rasterizer/pixelstore）
**统一 capabilities 表示**（P0-6）：移除 `enabledCaps` set，所有 cap 用 `RenderState` 内独立 bool 字段（含 per-draw-buffer blend 数组）。
```cpp
struct BlendState { bool enabled=false; GLenum srcRGB, dstRGB, srcA, dstA, eqRGB, eqA; };
struct RenderState {
    // capabilities（独立 bool，不再用 set）
    bool depthTest=false, cullFace=false, stencilTest=false, scissorTest=false;
    bool dither=true, multisample=true;       // ★ 默认 true（P1-8）
    bool sampleAlphaToCoverage=false, sampleCoverage=false, sampleMask=false;
    bool programPointSize=false, primitiveRestart=false, primitiveRestartFixedIndex=false;
    bool polygonOffsetFill=false, framebufferSRGB=false;
    bool depthClamp=false, rasterizerDiscard=false, textureCubeMapSeamless=false;
    bool clipDistance[8]={};                   // GL_CLIP_DISTANCE0..7
    BlendState blends[kMaxColorAttachments];   // per-draw-buffer blend
    // depth
    bool depthMask=true; GLenum depthFunc=GL_LESS; double depthNear=0, depthFar=1;
    // stencil（front + back）
    ...
    // blend color, color mask[kMaxColorAttachments][4]
    ...
    // rasterizer: cullMode, frontFace, polygonMode, polygonOffset, lineWidth, pointSize
    ...
    // pixel store（pack + unpack 各 8 字段）
    PixelStore pack, unpack;
    // clear values
    ...
    uint16_t version=0;                        // 任一字段变化自增
};
```
- `glEnable/glDisable/glIsEnabled` 统一读写上述 bool 字段（经 `setCapability(cap,bool)` / `isCapabilityEnabled(cap)` 集中分发），消除 set 与 bool 不一致。
- `glEnablei/glDisablei/glIsEnabledi`：支持 `GL_BLEND` per draw buffer（写 `blends[index].enabled`）；其它 indexed cap 暂不支持（符合 3.3，仅 blend 是 indexed）。
- `PixelStore` 完整 8 字段（含 `SwapBytes`/`LSBFirst`，P2 补全 pack）。
- 默认值全部对齐 GL 3.3 Core 规范（P1-8）：`dither=true`、`multisample=true`。

### 4.8 RenderbufferState
```cpp
struct Renderbuffer {
    GLuint id=0; GLenum internalFormat=GL_RGBA8; GLsizei width=0, height=0;
    GLsizei samples=0; bool markedForDeletion=false;
};
struct RenderbufferState {
    std::unordered_map<GLuint, Renderbuffer> objects;
    Renderbuffer* get(GLuint id);
};
```
- 实现 `glGenRenderbuffers`（入表）、`glBindRenderbuffer`（记录当前绑定）、`glRenderbufferStorage`/`glRenderbufferStorageMultisample`（写入字段）、`glFramebufferRenderbuffer`（FBO 附件 renderbuffer 字段）、`glIsRenderbuffer`、`glGetRenderbufferParameteriv`。
- backend 接 renderbuffer 的实际存储留作后续（本次仅状态正确追踪，避免 stub 崩溃）；FBO 附件若为 renderbuffer，`glCheckFramebufferStatus` 据其尺寸/格式校验。

### 4.9 QueryState
```cpp
enum class QueryTarget { SamplesPassed, PrimitivesGenerated, TimeElapsed, Timestamp, Count };
struct Query {
    GLuint id=0; QueryTarget target; bool active=false, ended=false;
    bool resultCached=false; uint64_t cachedResult=0;
    bool markedForDeletion=false;
};
struct QueryState {
    std::unordered_map<GLuint, Query> objects;
    GLuint activeTimeElapsed=0;  // 同时仅一个 TIME_ELAPSED active
};
```
- 实现 `glGenQueries/glDeleteQueries/glIsQuery/glBeginQuery/glEndQuery/glQueryCounter/glGetQueryiv/glGetQueryObjectuiv/glGetQueryObjectui64v`。
- backend 接真实 query 留作后续；本次状态正确追踪 + `glGetQueryObject*` 返回 cachedResult（0 或 backend 填充值），不崩溃。

### 4.10 SyncState
```cpp
struct Sync {
    void* handle;                    // 作为 GLsync 返回（opaque 指针）
    GLenum condition=GL_SYNC_GPU_COMMANDS_COMPLETE;
    GLbitfield flags=0;
    bool signaled=false;
    bool markedForDeletion=false;
};
struct SyncState {
    std::unordered_map<void*, Sync> objects;  // key = handle
    void* nextHandle = (void*)0x10;           // 单调递增，避开 sentinel
};
```
- 实现 `glFenceSync`（创建 Sync，返回 `reinterpret_cast<GLsync>(handle)`，不再返回 sentinel 0x1）、`glIsSync`、`glClientWaitSync`、`glWaitSync`、`glDeleteSync`（marked-for-deletion + 立即移除，sync 无 attachment 引用）、`glGetSynciv`（返回 `GL_OBJECT_TYPE`/`GL_SYNC_CONDITION`/`GL_SYNC_FLAGS`/`GL_SYNC_STATUS`）。
- backend 接真实 fence 留作后续；本次 `glClientWaitSync` 返回 `GL_ALREADY_SIGNALED` 或 backend 填值的 `GL_CONDITION_SATISFIED`，状态正确。

### 4.11 TransformFeedbackState
```cpp
struct TransformFeedback {
    GLuint id=0; bool active=false; bool paused=false;
    GLenum primitiveMode=GL_POINTS;
    bool markedForDeletion=false;
};
struct TransformFeedbackState {
    std::unordered_map<GLuint, TransformFeedback> objects;
    BindingSlot<TransformFeedback> bound;  // 当前绑定（default=0）
};
```
- 实现 `glGenTransformFeedbacks/glDeleteTransformFeedbacks/glIsTransformFeedback/glBindTransformFeedback`。
- `glBeginTransformFeedback/glEndTransformFeedback/glPauseTransformFeedback/glResumeTransformFeedback` 写入当前 TF 对象状态 + 校验（无 active TF 时 begin → `GL_INVALID_OPERATION`）。
- `glTransformFeedbackVaryings/glGetTransformFeedbackVarying`：记录 varyings 到 program（本次仅存字符串列表，backend 接 SPIR-V 变换留作后续）。
- TF buffer 绑定复用 BufferState 的 `TransformFeedback` indexed binding points。
- default TF (name=0) 真实存在并预绑定。

### 4.12 ProgramPipeline（GL 4.1 ARB_separate_shader_objects，3.3 可用扩展）
**本次不实现** program pipeline（超出 GL 3.3 Core 必需，且目标 App 未使用）。`glGenProgramPipelines` 等入口维持 stub 但返回 `GL_INVALID_OPERATION` error（而非静默），避免误用。

---

## 五、GL 入口点修复清单

> 修复原则：仅修状态机使用错误与字段缺失，**不改动** `MG_Backend/` 的 Vulkan 逻辑（红屏修复涉及的双 SPIR-V/cullMode 调整保留）。

### 5.1 Buffer (`MG_Impl/Buffer.cpp`)
- `glBindBuffer`：按 `BufferTarget` 枚举分发（P0-3）；`GL_ELEMENT_ARRAY_BUFFER` 写当前 VAO。
- `glBindBufferBase/Range`：实现 indexed binding（P0-4）+ 对齐校验。
- `glDeleteBuffers`：解绑所有 slot + indexed + VAO attrib 引用（P2-7）。
- `glGetBufferParameteriv`：补全 `GL_BUFFER_MAPPED`/`GL_BUFFER_MAP_OFFSET`/`GL_BUFFER_MAP_LENGTH`/`GL_BUFFER_ACCESS_FLAGS`/`GL_BUFFER_IMMUTABLE_STORAGE`（若支持）/`GL_BUFFER_USAGE`。
- 新增 `glIsBuffer`。

### 5.2 Texture (`MG_Impl/Texture.cpp`)
- `glBindTexture`：per-target slot（P0-5）。
- `glActiveTexture`：范围校验 + `GL_INVALID_ENUM`（P1-13）。
- `glTexParameter*`：补全所有 pname（LOD/anisotropy/compare/swizzle/base-max level）。
- `glDeleteTextures`：解绑所有 unit 所有 target 槽 + 清理 `boundTextureTargets`。
- `glGetTexParameteriv/fv/Iiv/Iuiv`：返回真实值（P1-4）。
- `glGetTexImage`：实现 CPU 回读（从 shadow data 或 backend readback，本次至少返回 shadow data 已有部分）。
- 新增 `glIsTexture`。
- `glTexImage2D/3D`/`glTexStorage*`/`glTexSubImage*`：校验 target 与 `t->target` 一致（不一致 `GL_INVALID_OPERATION`）。

### 5.3 Sampler（新文件 `MG_Impl/Sampler.cpp` 或并入 Texture.cpp）
- 全套 `glGenSamplers/glDeleteSamplers/glIsSampler/glBindSampler/glSamplerParameter{f,i,iv,fv,Ii,Iuiv}`。

### 5.4 VertexArray (`MG_Impl/VertexArray.cpp`)
- 移除全局 `currentIndexBuffer` 读写，改读 `g_state->vaos.bound.get()->elementArrayBuffer`（P0-7）。
- `glVertexAttribPointer`：删除 `a.divisor=a.divisor` 自赋值（P2-8）。
- `glDeleteVertexArrays`：回退 defaultVAO + erase。
- 新增 `glIsVertexArray`。
- 新增 `glGetVertexAttrib{dfi}v` / `glGetVertexAttribIiv/Iuiv` / `glGetVertexAttribPointerv`（P1-2）。

### 5.5 Framebuffer (`MG_Impl/Framebuffer.cpp`)
- `glBindFramebuffer`：target 校验 + draw/read 分发。
- `glFramebufferTexture2D/Layer`：attachment 越界校验（P1-11）。
- `glCheckFramebufferStatus`：真实校验（P1-7）。
- `glDrawBuffers/glReadBuffer`：default vs user FBO 区分（P1-10）。
- 新增 `glGetFramebufferAttachmentParameteriv`（P1-3）。
- 新增 `glIsFramebuffer`。
- renderbuffer 入口实现（4.8）。

### 5.6 Program/Shader (`MG_Impl/Program.cpp`)
- `glDeleteShader/Program`：marked-for-deletion（P1-5）。
- `glGetUniformLocation`：移除副作用（P1-6）。
- `glGetActiveUniform/Attrib`：返回真实 type/size（基于 SPIR-V 反射，若反射不可用则返回合理默认，不再永远 GL_FLOAT/size=1）。
- `glGetUniformBlockIndex`/`glGetActiveUniformBlockiv`/`glGetActiveUniformsiv`/`glGetUniformIndices`：基于 SPIR-V 反射实现（本次至少返回结构化数据，不再 stub 返回 0）。
- `glUniformBlockBinding`：实现（写入 program 的 block binding 表）。
- 新增 `glIsProgram`/`glIsShader`。

### 5.7 Drawing (`MG_Impl/Drawing.cpp`)
- `glDrawArrays/glDrawElements`：mode/count 合法性校验 + `state_set_error`（P1-9）。
- sync object 入口实现（4.10）。
- `glFlush/glFinish`：维持现有 backend flush，补 `state_set_error` 路径。

### 5.8 gl.cpp / Getter.cpp / Stubs.cpp
- `glEnable/glDisable/glIsEnabled`：走 `RenderState` 集中分发（P0-6）。
- `glGetError`：返回真实 error（P0-2）。
- `glGetIntegerv/glGetBooleanv/glGetFloatv/glGetDoublev/glGetInteger64v`：补全所有缺失 pname（P1-2/3/4、§2.3 列表）。
- `glGetIntegeri_v`：实现 indexed query（P1-14）。
- `glGetString`：维持现有；`glGetStringi`：补 `GL_SHADING_LANGUAGE_VERSION` 子版本。
- pixel store 入口（`glPixelStoref/i`）：写入完整 PixelStore（含 SwapBytes/LSBFirst/pack）。

### 5.9 EGL (`egl/egl.cpp`)
- `g_state` 改 `thread_local`（P0-1）。
- `eglMakeCurrent`：设当前线程 `g_state`。
- `MITHRIL_ENSURE_INIT`（`includes.h`）：EGL 已初始化但无 current context 时不创建 phantom state，GL 入口 `state_set_error(GL_INVALID_OPERATION)`。
- `eglInitialize`：置 `g_eglInitialized=true`。

### 5.10 init.cpp / State.cpp
- `state_init`：保持幂等；`state_create` 预创建默认 VAO/FBO/per-target 默认纹理/default TF。
- `state_destroy`：清理所有对象表 + allocator。

---

## 六、默认值合规清单（对齐 GL 3.3 Core）

| 字段 | 新默认 | 旧默认 | 规范 |
|------|--------|--------|------|
| `dither` | true | true（但 enabledCaps 空） | enabled |
| `multisample` | true | false | enabled |
| `clipDistance[0..7]` | false | 缺失 | disabled |
| `textureCubeMapSeamless` | false | 缺失 | disabled |
| `framebufferSRGB` | false | false | disabled |
| `primitiveRestart` | false | false | disabled |
| Texture `baseLevel` | 0 | 缺失 | 0 |
| Texture `maxLevel` | 1000 | 缺失 | 1000 |
| Texture `minLod`/`maxLod` | -1000/1000 | 缺失 | -1000/1000 |
| Texture `compareMode` | GL_NONE | 缺失 | GL_NONE |
| Texture `swizzleRGBA` | RED/GREEN/BLUE/ALPHA | 缺失 | identity |
| Sampler 同 Texture 默认 | — | 缺失 | 同 |
| `boundTextureTargets` 数组 | 每 unit 每 target 独立槽（无单值陷阱） | aggregate init 陷阱 | 修复 |

---

## 七、状态查询补全清单

新增/修复的 `glGet*` 与 `glIs*`：
- `glIsBuffer/glIsTexture/glIsFramebuffer/glIsProgram/glIsShader/glIsVertexArray/glIsRenderbuffer/glIsSampler/glIsQuery/glIsTransformFeedback/glIsSync`
- `glGetVertexAttrib{dfi}v` / `glGetVertexAttribIiv/Iuiv` / `glGetVertexAttribPointerv`
- `glGetFramebufferAttachmentParameteriv`
- `glGetTexParameter{iv,fv,Iiv,Iuiv}`（返回真实值）
- `glGetTexImage`（至少 shadow data 回读）
- `glGetRenderbufferParameteriv`
- `glGetQueryiv` / `glGetQueryObjectuiv` / `glGetQueryObjectui64v`
- `glGetSynciv`
- `glGetIntegeri_v`（indexed buffer binding query）
- `glGetIntegerv` 补全：`GL_TEXTURE_BINDING_CUBE_MAP/3D/2D_ARRAY/1D`、`GL_SAMPLER_BINDING`、各 buffer target binding、`GL_UNIFORM_BUFFER_BINDING/START/SIZE`、`GL_DEPTH_RANGE`、`GL_STENCIL_CLEAR_VALUE`、`GL_STENCIL_BACK_*`、`GL_MAX_UNIFORM_BUFFER_BINDINGS`、`GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT`、`GL_MAX_TEXTURE_LOD_BIAS`、`GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT`、`GL_SAMPLE_BUFFERS`、`GL_SAMPLES`、`GL_PRIMITIVE_RESTART_INDEX` 等。
- `glGetActiveUniformBlockiv` / `glGetActiveUniformsiv` / `glGetUniformIndices`
- `glGetTransformFeedbackVarying`

---

## 八、实施阶段（详见 tasks.md）

1. **阶段 1 基础设施**：`NameAllocator`、`BindingSlot<T>`、`ErrorState`、`thread_local g_state`、`MITHRIL_ENSURE_INIT` 修复。
2. **阶段 2 对象表重构**：Buffer/Texture/VAO/FBO/Program/Shader 子状态组件 + 默认对象 + 生命周期（lazy create / deferred delete / marked-for-deletion）。
3. **阶段 3 新增状态分类**：Sampler/Renderbuffer/Query/Sync/TransformFeedback。
4. **阶段 4 RenderState 统一**：capabilities 集中分发 + per-draw-buffer blend + 完整 pixel store + 默认值合规。
5. **阶段 5 GL 入口修复**：按 §5 逐文件修复 target 绑定/校验/生命周期/查询入口。
6. **阶段 6 编译验证**：全工程 g++ -fsyntax-only + 完整 cmake build（Apple 目标环境）。
7. **阶段 7 回归验证**：红屏/黑屏修复不回归 + checklist 逐项核查。

---

## 九、验证标准

1. **编译**：`MG_State/*.cpp` + `MG_Impl/*.cpp` + `egl/egl.cpp` 通过 g++ -fsyntax-only（-std=c++20 -Wall -Wextra）无新增 warning/error。
2. **无 P0/P1 缺陷**：checklist 中所有 P0/P1 项打勾。
3. **红屏/黑屏不回归**：`specs/fix-red-black-screen/` 的修复（双 SPIR-V / cullMode / Z 重映射）仍生效。
4. **默认值合规**：`glIsEnabled(GL_DITHER/GL_MULTISAMPLE)` 返回 `GL_TRUE`；`glGetIntegerv(GL_MULTISAMPLE)` 一致。
5. **error 路径**：`glGetError` 在注入错误后返回真实 error；多次注入依次返回。
6. **对象生命周期**：`glDeleteShader` 后仍 attached 的 shader 不被立即回收；`glDeleteTextures` 后所有 unit 槽回退默认纹理。
7. **target 隔离**：同 unit 不同 target 绑定互不覆盖；不同 buffer target 绑定互不覆盖。
8. **thread 隔离**：两线程各自 `eglMakeCurrent` 后 `g_state` 独立。
9. **运行**（Apple 目标环境）：目标 App 主界面 + 进游戏正常，无新增崩溃。

---

## 十、风险与回退

### 10.1 风险
1. **改动面大**：State.h 重写波及所有 `MG_Impl`。缓解：保留 `g_state->` 语法与 `state_get_*` 签名，增量修改；分阶段提交，每阶段编译验证。
2. **红屏修复回归**：双 SPIR-V / cullMode 调整依赖 `currentDrawFBO` 与 `Program::vertexSpirvYFlipped`，重写须保留这些字段与语义。缓解：阶段 2 显式保留并在 checklist 设回归项。
3. **EGL thread_local**：现有 `install_surface_on_state` 假设 `g_state` 已指向当前 context；thread_local 后语义不变（eglMakeCurrent 先设 g_state 再 install）。缓解：核查 egl.cpp 顺序。
4. **性能**：移除 enabledCaps set 改 bool 字段无性能损失；BindingSlot 版本号本次不消费，无开销。
5. **backend 未接的新对象**（sampler/query/sync/TF/renderbuffer）：状态正确但 backend 不消费，可能 App 查询到"已创建"但无实际 GPU 效果。缓解：spec 明确标注"backend 接入为后续任务"，本次保证不崩溃 + 状态可查。

### 10.2 回退
- 单文件回退：`git checkout -- <file>`。
- 整体回退：`git revert` 重写提交，恢复旧 State.h/State.cpp。
- 关键保留点：红屏修复提交独立，重写提交在其之后，回退重写不影响红屏修复。

---

## 十一、约束遵守
- 全程在 `main` 分支工作。
- 推送令牌 `ghp_***` 仅用于推送（如用户要求时），本次未使用。
- 未经用户允许不提交 PR、不推送远程。
- 所有回答使用中文。
- 不照搬 MobileGL 代码，仅参考设计。
