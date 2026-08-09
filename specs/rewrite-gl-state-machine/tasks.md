# OpenGL 状态机重写任务分解

> 配合 [spec.md](./spec.md) 与 [checklist.md](./checklist.md) 使用。任务按 spec 第八章实施阶段编排，每个任务标注涉及文件与验收标准。**未经用户允许不提交 PR、不推送远程**；每个阶段完成后做 g++ -fsyntax-only 编译验证。

---

## 阶段 1：基础设施

### T1.1 `NameAllocator` 与 `BindingSlot` 模板
- **文件**：`Mithril-Wrapper-cpp/MG_State/State.h`（新增类型）、`State.cpp`（实现）
- **内容**：
  - `class NameAllocator`：`m_next`/`m_freeList`/`m_valid`；`generate(n,out)`/`insert(id)`/`release(id)`/`valid(id)`。
  - `template<typename T> struct BindingSlot`：`T* bound`/`uint16_t version`/`bind()`/`get()`。
  - `template<typename T> struct BindingSlotRanged`：继承 + `offset`/`size`/`hasExplicitRange`/`setRange()`/`clearRange()`。
- **验收**：编译通过；`valid()` O(1)；bind 去重不增版本。

### T1.2 `ErrorState` 错误队列
- **文件**：`State.h`、`State.cpp`
- **内容**：`std::deque<GLenum> glErrors` + `std::deque<std::string> internalErrors`；`recordGL`/`popGL`/`recordInternal`/`popInternal`/`clear`；队列上限 64。
- **验收**：`state_set_error` 累积；`state_take_error`（改名 `popGL`）返回最早 error。

### T1.3 `thread_local g_state` 与 phantom 修复
- **文件**：`State.h`、`State.cpp`、`MG_Impl/includes.h`、`egl/egl.cpp`
- **内容**：
  - `extern thread_local GLState* g_state;` + 定义。
  - `extern bool g_eglInitialized;`（egl.cpp 定义，`eglInitialize` 置 true）。
  - `MITHRIL_ENSURE_INIT` 重写：`g_state==nullptr && !g_eglInitialized` 时才 `state_init()`；否则若 `g_state==nullptr` 则 GL 入口设 `GL_INVALID_OPERATION` 后返回。
  - `eglMakeCurrent` 设当前线程 `g_state`；`EGL_NO_CONTEXT` 设 nullptr。
- **验收**：P0-1 通过；两线程 `eglMakeCurrent` 后 `g_state` 独立。

---

## 阶段 2：对象表重构

### T2.1 `BufferState` + Buffer 重写
- **文件**：`State.h`、`State.cpp`、`MG_Impl/Buffer.cpp`
- **内容**：
  - `enum class BufferTarget`（14 项 + Count）；`BufferState`：`objects` + `slots[Count]` + `indexed[4][36]` + `touchedIndexed[4]`。
  - `glBindBuffer`：按枚举分发；`GL_ELEMENT_ARRAY_BUFFER` 写当前 VAO。
  - `glBindBufferBase/Range`：indexed + 对齐校验。
  - `glDeleteBuffers`：解绑所有 slot + indexed + 所有 VAO attrib + 当前 VAO element（P2-7）。
  - `glGenBuffers` 走 `NameAllocator`。
  - `glGetBufferParameteriv` 补全；`glIsBuffer` 实现。
- **验收**：P0-3/P0-4/P2-7 通过；`glBindBufferBase` index 生效。

### T2.2 `TextureState` + Texture 重写
- **文件**：`State.h`、`State.cpp`、`MG_Impl/Texture.cpp`
- **内容**：
  - `enum class TextureTarget`（11 项）；`TextureUnit`：`slots[Count]` + `sampler`；`TextureState`：`objects` + `units[kMaxTextureUnits]` + `activeUnit` + `bindGeneration` + `defaultTextures[Count]`。
  - `Texture` 字段补全（spec 4.2）。
  - `glBindTexture`：per-target slot（P0-5）+ 懒构造。
  - `glActiveTexture`：校验（P1-13）。
  - `glTexParameter*`：补全所有 pname。
  - `glDeleteTextures`：解绑所有 unit 所有 target 槽回退默认纹理。
  - `glGetTexParameter*`：真实值（P1-4）；`glGetTexImage` 回读 shadow。
  - `glIsTexture`；target 一致性校验。
- **验收**：P0-5/P1-4/P1-13 通过；同 unit 不同 target 互不覆盖。

### T2.3 `VertexArrayState` + VAO 重写
- **文件**：`State.h`、`State.cpp`、`MG_Impl/VertexArray.cpp`
- **内容**：
  - `VertexArray`：`elementArrayBuffer` 唯一来源；`attribVersions`/`configVersion`/`markedForDeletion`。
  - `VertexArrayState`：`objects` + `defaultVAO` + `BindingSlot<VertexArray> bound`。
  - 移除全局 `currentIndexBuffer`（P0-7），所有读写走 `vaos.bound.get()->elementArrayBuffer`。
  - `glVertexAttribPointer`：删自赋值（P2-8）；写 `bound.get()->attribs[index]`。
  - `glDeleteVertexArrays`：回退 defaultVAO + erase。
  - `glIsVertexArray`；`glGetVertexAttrib*`/`glGetVertexAttribPointerv`（P1-2）。
- **验收**：P0-7/P1-2/P2-8 通过；default VAO 预绑定。

### T2.4 `FramebufferState` + FBO 重写
- **文件**：`State.h`、`State.cpp`、`MG_Impl/Framebuffer.cpp`
- **内容**：
  - `Framebuffer`：`drawBuffers` 全 GL_NONE；`attachmentVersions`；`markedForDeletion`。
  - `FramebufferState`：`objects` + `defaultFBO` + `draw`/`read` BindingSlot。
  - `glBindFramebuffer`：target 校验 + draw/read 分发。
  - `glFramebufferTexture2D/Layer`：附件越界校验（P1-11）。
  - `glCheckFramebufferStatus`：真实校验（P1-7）。
  - `glDrawBuffers`/`glReadBuffer`：default vs user 区分（P1-10）。
  - `glGetFramebufferAttachmentParameteriv`（P1-3）；`glIsFramebuffer`。
- **验收**：P1-7/P1-10/P1-11/P1-3 通过。

### T2.5 `ProgramState` / Shader 生命周期
- **文件**：`State.h`、`State.cpp`、`MG_Impl/Program.cpp`
- **内容**：
  - `Shader`：`markedForDeletion`/`attachCount`。
  - `Program`：`markedForDeletion`/`inUse`；**保留** `vertexSpirv/vertexSpirvYFlipped/fragmentSpirv`（红屏修复）。
  - `glDeleteShader`：marked-for-deletion + attachCount 延迟（P1-5）。
  - `glDeleteProgram`：marked-for-deletion + 非当前则立即 erase，否则延迟到 `glUseProgram(0)`。
  - `glDetachShader`/attach 时维护 attachCount。
  - `glGetUniformLocation`：移除副作用（P1-6）。
  - `glGetActiveUniform/Attrib`：真实反射；uniform block 查询；`glUniformBlockBinding`；`glIsProgram`/`glIsShader`。
- **验收**：P1-5/P1-6 通过；红屏字段保留（F1）。

---

## 阶段 3：新增状态分类

### T3.1 `SamplerState`
- **文件**：`State.h`、`State.cpp`、`MG_Impl/Sampler.cpp`（新）或并入 `Texture.cpp`
- **内容**：`Sampler` 完整字段 + `lifetimeId`（静态原子）；`SamplerState`；`TextureUnit.sampler` slot。
- 入口：`glGenSamplers/glDeleteSamplers/glIsSampler/glBindSampler/glSamplerParameter{f,i,iv,fv,Ii,Iuiv}`。
- **验收**：P2-1（sampler 部分）；`glBindSampler` 绑定到 unit。

### T3.2 `RenderbufferState`
- **文件**：`State.h`、`State.cpp`、`MG_Impl/Framebuffer.cpp`
- **内容**：`Renderbuffer` + `RenderbufferState` + `BindingSlot<Renderbuffer> bound`。
- 入口：`glGenRenderbuffers/glBindRenderbuffer/glRenderbufferStorage/glRenderbufferStorageMultisample/glFramebufferRenderbuffer/glIsRenderbuffer/glGetRenderbufferParameteriv`（P1-15）。
- **验收**：P1-15；FBO 附件可挂 renderbuffer，`glCheckFramebufferStatus` 据其校验。

### T3.3 `QueryState`
- **文件**：`State.h`、`State.cpp`、`MG_Impl/Query.cpp`（新）或并入 `Drawing.cpp`
- **内容**：`enum class QueryTarget`；`Query` + `QueryState`（`activeTimeElapsed`）。
- 入口：`glGenQueries/glDeleteQueries/glIsQuery/glBeginQuery/glEndQuery/glQueryCounter/glGetQueryiv/glGetQueryObjectuiv/glGetQueryObjectui64v`。
- **验收**：P2-1（query 部分）；`glGetQueryObject*` 返回 cachedResult 不崩溃。

### T3.4 `SyncState`
- **文件**：`State.h`、`State.cpp`、`MG_Impl/Drawing.cpp`（替换现有 sync stub）
- **内容**：`Sync` + `SyncState`（handle 单调递增，避开 0x1 sentinel）。
- 入口：`glFenceSync/glIsSync/glClientWaitSync/glWaitSync/glDeleteSync/glGetSynciv`（P1-16）。
- **验收**：P1-16；`glFenceSync` 返回真实 handle。

### T3.5 `TransformFeedbackState`
- **文件**：`State.h`、`State.cpp`、`MG_Impl/TransformFeedback.cpp`（新）或并入 `Drawing.cpp`
- **内容**：`TransformFeedback` + `TransformFeedbackState`（default TF name=0 预绑定）。
- 入口：`glGenTransformFeedbacks/glDeleteTransformFeedbacks/glIsTransformFeedback/glBindTransformFeedback/glBegin/End/Pause/ResumeTransformFeedback/glTransformFeedbackVaryings/glGetTransformFeedbackVarying`。
- TF buffer 绑定复用 BufferState 的 TransformFeedback indexed points。
- **验收**：P2-1（TF 部分）；default TF 预绑定。

---

## 阶段 4：RenderState 统一

### T4.1 capabilities 集中分发
- **文件**：`State.h`、`State.cpp`、`MG_Impl/gl.cpp`
- **内容**：
  - `RenderState` 内独立 bool cap 字段（spec 4.7）+ `BlendState blends[kMaxColorAttachments]`。
  - 移除 `enabledCaps` set（P0-6）。
  - `setCapability(cap,bool)`/`isCapabilityEnabled(cap)` 集中 switch 分发。
  - `glEnable/glDisable/glIsEnabled` 走集中分发；`glEnablei/glDisablei/glIsEnabledi` 支持 `GL_BLEND` per draw buffer。
  - 默认 `dither=true`/`multisample=true`（P1-8）。
- **验收**：P0-6/P1-8 通过；`glIsEnabled` 与 `glGetBooleanv` 一致。

### T4.2 完整 PixelStore + 其余 RenderState
- **文件**：`State.h`、`State.cpp`、`MG_Impl/gl.cpp`
- **内容**：
  - `PixelStore` 结构 8 字段（含 SwapBytes/LSBFirst）；`pack` + `unpack`。
  - `glPixelStoref/i` 写入完整字段。
  - depth/stencil/blend color/color mask/rasterizer/polygonOffset/clear values 字段对齐 spec 4.7。
  - `RenderState::version` 在任一字段变化时自增。
- **验收**：pixel store 完整；版本号维护。

---

## 阶段 5：GL 入口修复

### T5.1 `glGetError` + 错误设置补全
- **文件**：`MG_Impl/Getter.cpp`、各 `MG_Impl/*.cpp`
- **内容**：
  - `glGetError` 返回 `errors.popGL()`（P0-2）。
  - 各入口按规范设置 error：Buffer/Texture/Framebuffer/Drawing/Program/gl.cpp 的非法 target/attachment/mode/enum 路径（P1-9）。
- **验收**：P0-2/P1-9；注入错误后 `glGetError` 返回真实值。

### T5.2 `glGet*` 查询补全
- **文件**：`MG_Impl/Getter.cpp`、`Stubs.cpp`、`Program.cpp`、`Buffer.cpp`、`Texture.cpp`、`Framebuffer.cpp`
- **内容**：按 spec 第七章补全所有 `glGet*` 与 `glIs*`（P1-1/2/3/4/14 + 缺失 pname）。
- **验收**：checklist 第七章全部打勾。

### T5.3 EGL/init 配套
- **文件**：`egl/egl.cpp`、`MG_Impl/init.cpp`、`State.cpp`
- **内容**：`g_eglInitialized` 标志；`state_create` 预创建所有默认对象；`state_destroy` 清理全部表 + allocator。
- **验收**：phantom state 修复；默认对象预绑定。

---

## 阶段 6：编译验证

### T6.1 TU 语法检查（linux 沙箱）
- **命令**：g++ -std=c++20 -fsyntax-only -Wall -Wextra 对 `MG_State/State.cpp` + 全部 `MG_Impl/*.cpp` + `egl/egl.cpp`。
- **验收**：E1/E2/E3/E4 通过（无新增 warning/error）。

### T6.2 完整 cmake build（Apple 目标环境）
- **验收**：E5 通过；链接 libmithril.dylib 成功。

---

## 阶段 7：回归与运行验证

### T7.1 红屏/黑屏不回归核查
- **内容**：核查 `Program::vertexSpirvYFlipped`/`Drawing::prepare_draw` 双 SPIR-V 选择/`Pipeline::is_default_fbo`/`Device::FLIP_VERTEX_Y=0` 保留（F1-F4）。
- **验收**：F1-F4 打勾。

### T7.2 checklist 逐项核查
- **内容**：对照 `checklist.md` 全部项打勾（代码核查 + 编译 + 运行）。
- **验收**：P0/P1/P2 全部通过。

### T7.3 目标 App 运行验证（Apple 目标环境）
- **内容**：主界面 + 进游戏；`glGetError` 注入测试；多 target 纹理绑定；多线程 `eglMakeCurrent`。
- **验收**：G1-G8 打勾。

---

## 约束
- 全程 main 分支。
- `ghp_***` 仅用于推送（用户要求时），本次未使用。
- 未经允许不提交 PR、不推送远程。
- 中文回答。
- 不照搬 MobileGL 代码，仅参考设计。
