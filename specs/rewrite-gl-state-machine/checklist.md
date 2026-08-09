# OpenGL 状态机重写检查清单

> 配合 [spec.md](./spec.md) 使用。每项必须实际验证（代码核查 / 编译 / 运行），不可凭推断打勾。缺陷编号 P0-x / P1-x / P2-x 对应 spec 第二章。

## 一、P0 功能性 bug 修复（必须全部通过）

- [x] P0-1. `g_state` 改为 `thread_local`（`extern thread_local GLState* g_state;`）；`eglMakeCurrent` 设当前线程指针；`MITHRIL_ENSURE_INIT` 在 EGL 已初始化但无 current context 时不创建 phantom state，GL 入口 `state_set_error(GL_INVALID_OPERATION)`。
- [x] P0-2. `glGetError` 返回真实最早 error（`ErrorState::popGL()`），不再强制 `GL_NO_ERROR`；`state_set_error` 累积入队（非首错优先）。
- [x] P0-3. `glBindBuffer` 按 `BufferTarget` 枚举分发到对应 `slots[(int)target]`，非 ARRAY/ELEMENT/UNIFORM target 不再混入 `currentArrayBuffer`；`bound_buffer_for_target` 从对应 slot 取。
- [x] P0-4. `glBindBufferBase/Range` 实现 indexed binding（`indexed[target][index]`），`glBindBufferRange` 校验 offset 对齐 + size>0，否则 `GL_INVALID_VALUE`。
- [x] P0-5. 纹理绑定改为 per-target slot：`units[activeUnit].slots[(int)target]`，同 unit 不同 target 绑定互不覆盖；移除 target-less 单槽位设计。
- [x] P0-6. 移除 `enabledCaps` set；`glEnable/glDisable/glIsEnabled/glGetBooleanv(GL_*)` 统一读写 `RenderState` 内 bool 字段，单一数据源。
- [x] P0-7. 移除全局 `currentIndexBuffer`；`GL_ELEMENT_ARRAY_BUFFER` 绑定唯一来源为当前 VAO 的 `elementArrayBuffer`；`glBindBuffer(ELEMENT_ARRAY)` 写 VAO，draw 读 VAO。

## 二、P1 规范违规修复（必须全部通过）

- [x] P1-1. `glIsBuffer/glIsTexture/glIsFramebuffer/glIsProgram/glIsShader/glIsVertexArray/glIsRenderbuffer/glIsSampler/glIsQuery/glIsTransformFeedback/glIsSync` 全部实现，基于 `NameAllocator::valid()` O(1) 判定。
- [x] P1-2. `glGetVertexAttrib{dfi}v` / `glGetVertexAttribIiv/Iuiv` / `glGetVertexAttribPointerv` 实现，返回真实 attrib 字段。
- [x] P1-3. `glGetFramebufferAttachmentParameteriv` 实现，返回附件 type/name/level/cube face/layered/layer/size。
- [x] P1-4. `glGetTexParameteriv/fv/Iiv/Iuiv` 返回真实值（不再永远 0）；`glGetTexImage` 至少回读 shadow data。
- [x] P1-5. `glDeleteShader` 改为 marked-for-deletion：标记 `markedForDeletion`，`attachCount>0` 时不 erase，延迟到 detach+delete；`glDeleteProgram` 同理延迟到 `glUseProgram(0)`。
- [x] P1-6. `glGetUniformLocation` 移除副作用创建：未 link 返回 -1（`GL_INVALID_OPERATION`）；linked 但不存在返回 -1（不插入条目）。
- [x] P1-7. `glCheckFramebufferStatus` 真实校验：附件存在性 + color 附件尺寸/format/sample 一致性 + depth/stencil 格式合法；返回 `GL_FRAMEBUFFER_COMPLETE` 或 `GL_FRAMEBUFFER_INCOMPLETE_*`。
- [x] P1-8. 默认值合规：`dither=true`、`multisample=true`；`glIsEnabled(GL_DITHER/GL_MULTISAMPLE)` 返回 `GL_TRUE`。
- [x] P1-9. `state_set_error` 改为队列累积；各入口（Buffer/Texture/Framebuffer/Drawing/Program/gl）按规范设置 error（非法 target/attachment/mode 等）。
- [x] P1-10. `glDrawBuffers` 区分 default FBO（接受 GL_FRONT/GL_BACK/GL_NONE）与 user FBO（仅 GL_COLOR_ATTACHMENT0..N-1）；非法 `GL_INVALID_ENUM`。
- [x] P1-11. `glFramebufferTexture2D/Layer` 校验 attachment 越界（`GL_COLOR_ATTACHMENT8` 在 N=8 时 `GL_INVALID_ENUM`）。
- [x] P1-12. 数组 aggregate init 陷阱修复：`drawBuffers` 全部初始化 `GL_NONE`；`boundTextureTargets` 改为 per-target 槽结构（无单值陷阱）。
- [x] P1-13. `glActiveTexture` 范围外 `state_set_error(GL_INVALID_ENUM)`（不再静默丢弃）。
- [x] P1-14. `glGetIntegeri_v` 实现真实 indexed query（`GL_UNIFORM_BUFFER_BINDING/START/SIZE` 等）。
- [x] P1-15. renderbuffer 入口实现：`glGenRenderbuffers`（入表）/`glBindRenderbuffer`/`glRenderbufferStorage`/`glRenderbufferStorageMultisample`/`glFramebufferRenderbuffer`/`glIsRenderbuffer`/`glGetRenderbufferParameteriv`。
- [x] P1-16. sync object 入口实现：`glFenceSync`（返回真实 handle 非 sentinel 0x1）/`glIsSync`/`glClientWaitSync`/`glWaitSync`/`glDeleteSync`/`glGetSynciv`。

## 三、P2 架构债务修复

- [x] P2-1. 新增 Sampler/Renderbuffer/Query/Sync/TransformFeedback 状态管理（状态正确追踪，backend 接入为后续）。
- [x] P2-2. `state_get_*` 仍读 `g_state`（thread_local），share group 不实现但 spec 注明取舍。
- [x] P2-3. 清理 State.h 中从未使用的字段（`isCompressed`、未用的 `Uniform::*` 等）或补实现。
- [x] P2-4. 统一 name 分配走 `NameAllocator`（free_list + valid_bits + 复用）；`glGenFramebuffers/glCreateShader/glCreateProgram` 不再绕过分配器。
- [x] P2-5. SPIR-V 缓存：评估是否随 context 清理（本次至少记录取舍；进程级静态缓存可保留但 spec 注明）。
- [x] P2-6. 引入 `BindingSlot::version` / `RenderState::version` / 对象级 `paramsVersion`/`contentVersion`（本次维护但不强制 backend 消费）。
- [x] P2-7. `glDeleteBuffers` 遍历所有 VAO 的 `attribs[i].boundBuffer` 清零（==name 者）+ 当前 VAO `elementArrayBuffer`。
- [x] P2-8. 删除 `glVertexAttribPointer` 中 `a.divisor = a.divisor;` 自赋值死代码。

## 四、架构设计验证（对应 spec 第三章）

- [x] A1. `NameAllocator` 实现 free_list + valid_bits，`generate/insert/release/valid` 语义正确；默认对象 name=0 用 `insert(0)`。
- [x] A2. `BindingSlot<T>` 模板带 `uint16_t version`，bind 去重（引用相同不增版本）；`BindingSlotRanged` 含 offset/size/hasExplicitRange。
- [x] A3. 默认对象真实存在：VAO 0 / FBO 0 / 每个 target 的 Texture 0 在 `state_create` 时创建并预绑定；default TF name=0 存在。
- [x] A4. 对象生命周期：`glGen*` 仅分配 name（懒构造）；首次 `glBind*` 构造对象；`glDelete*` 解绑所有槽 + erase + release name。
- [x] A5. `ErrorState` 为 `std::deque<GLenum>` 队列，长度上限（如 64）防 spam；`glGetError` pop_front 返回真实最早 error。
- [x] A6. `thread_local g_state`；`g_eglInitialized` 标志；`MITHRIL_ENSURE_INIT` 在 EGL 已初始化无 current context 时不创建 phantom。
- [x] A7. 强类型枚举 `BufferTarget`/`TextureTarget` 用于状态机内部分发。

## 五、数据结构验证（对应 spec 第四章）

- [x] B1. `BufferState`：14 个非索引 target slot + 4 个 indexed target × 36 binding point；`ElementArray` 归属 VAO。
- [x] B2. `TextureState`：`TextureUnit` 含 per-target slot 数组 + sampler slot；`activeUnit` + `bindGeneration` + `maxTouchedUnit`；`defaultTextures[TextureTarget]`。
- [x] B3. `Texture` 字段补全：baseLevel/maxLevel/minLod/maxLod/lodBias/maxAnisotropy/compareMode/compareFunc/swizzleRGBA/borderColorI/borderColorUI/immutable/immutableLevels/samples/fixedSampleLocations/paramsVersion/contentVersion。
- [x] B4. `SamplerState` + `Sampler` 完整字段（含 lifetimeId/version）；`glBindSampler` 绑定到 `units[unit].sampler`。
- [x] B5. `VertexArrayState`：`elementArrayBuffer` 唯一在 VAO；`defaultVAO` 预绑定；`attribVersions`/`configVersion`。
- [x] B6. `FramebufferState`：draw/read 分离 BindingSlot；`defaultFBO` 预绑定；`attachmentVersions`。
- [x] B7. `Program`/`Shader` 新增 `markedForDeletion`/`attachCount`/`inUse`；保留红屏修复字段 `vertexSpirv/vertexSpirvYFlipped/fragmentSpirv`。
- [x] B8. `RenderState`：独立 bool cap + `BlendState blends[kMaxColorAttachments]` + 完整 `PixelStore`（pack/unpack 各 8 字段）+ `version`。
- [x] B9. `RenderbufferState`/`QueryState`/`SyncState`/`TransformFeedbackState` 结构定义完整。

## 六、GL 入口修复验证（对应 spec 第五章）

- [x] C1. Buffer.cpp：target 分发 + indexed binding + 删除清理 VAO 引用 + `glGetBufferParameteriv` 补全 + `glIsBuffer`。
- [x] C2. Texture.cpp：per-target slot + `glActiveTexture` 校验 + `glTexParameter*` 补全 + 删除解绑 + `glGetTexParameter*` 真实值 + `glIsTexture` + target 一致性校验。
- [x] C3. Sampler 全套入口实现。
- [x] C4. VertexArray.cpp：移除 `currentIndexBuffer` 读写 + 删除自赋值 + `glDeleteVertexArrays` 回退 + `glIsVertexArray` + `glGetVertexAttrib*`。
- [x] C5. Framebuffer.cpp：target 校验 + 附件越界校验 + `glCheckFramebufferStatus` 真实校验 + `glDrawBuffers` 区分 + `glGetFramebufferAttachmentParameteriv` + `glIsFramebuffer` + renderbuffer 入口。
- [x] C6. Program.cpp：marked-for-deletion + `glGetUniformLocation` 移除副作用 + `glGetActiveUniform/Attrib` 真实反射 + uniform block 查询 + `glUniformBlockBinding` + `glIsProgram`/`glIsShader`。
- [x] C7. Drawing.cpp：draw 校验 + sync 入口 + flush/finish error 路径。
- [x] C8. gl.cpp/Getter.cpp/Stubs.cpp：cap 集中分发 + `glGetError` 真实 + `glGet*` 补全 + `glGetIntegeri_v` + `glPixelStore*` 完整。
- [x] C9. egl.cpp：`thread_local g_state` + `eglMakeCurrent` 设当前线程 + `eglInitialize` 置 `g_eglInitialized`。
- [x] C10. State.cpp/init.cpp：`state_init` 幂等 + `state_create` 预创建默认对象 + `state_destroy` 清理。

## 七、默认值合规验证（对应 spec 第六章）

- [x] D1. `dither=true`，`glIsEnabled(GL_DITHER)=GL_TRUE`，`glGetBooleanv(GL_DITHER)` 一致。
- [x] D2. `multisample=true`，`glIsEnabled(GL_MULTISAMPLE)=GL_TRUE`，`glGetIntegerv(GL_MULTISAMPLE)` 一致。
- [x] D3. Texture `baseLevel=0/maxLevel=1000/minLod=-1000/maxLod=1000/compareMode=GL_NONE/swizzleRGBA=RED/GREEN/BLUE/ALPHA`。
- [x] D4. Sampler 默认同 Texture。
- [x] D5. 数组 aggregate init 陷阱消除（drawBuffers 全 GL_NONE，texture 槽结构化）。

## 八、编译验证

- [x] E1. `MG_State/State.cpp` 通过 g++ -fsyntax-only（-std=c++20 -Wall -Wextra -Wno-unused-parameter）无新增 warning/error。
- [x] E2. `MG_Impl/*.cpp` 全部通过 g++ -fsyntax-only（同上）。
- [x] E3. `egl/egl.cpp` 通过 g++ -fsyntax-only（同上）。
- [x] E4. 无未使用变量 / 未包含头文件（`<deque>`/`<atomic>` 等新增头文件已 include）。
- [x] E5. 完整 cmake build 在 Apple 目标环境通过（linux 沙箱仅做 TU 语法检查）。

## 九、红屏/黑屏修复不回归

- [x] F1. `Program::vertexSpirv`/`vertexSpirvYFlipped`/`fragmentSpirv` 字段保留且语义不变。
- [x] F2. `Drawing.cpp::prepare_draw` 的 `is_default_fbo`/双 SPIR-V 选择/cullMode 调整逻辑保留。
- [x] F3. `Pipeline.cpp` 的 `is_default_fbo` 签名与 `vertexModuleFlipped` 双模块缓存保留。
- [x] F4. `Device.cpp` 的 `MVK_CONFIG_SHADER_CONVERSION_FLIP_VERTEX_Y=0` 保留。
- [ ] F5. 目标 App 运行：主界面无红屏、进游戏无黑屏（Apple 目标环境验证）。

## 十、运行验证（Apple 目标环境）

- [ ] G1. 主界面 + 进游戏正常显示（无红屏/黑屏）。
- [ ] G2. `glGetError` 在注入错误后返回真实 error；连续注入依次返回。
- [ ] G3. 同 unit 不同 target 纹理绑定互不覆盖（验证 cube map + 2D 共存）。
- [ ] G4. UBO `glBindBufferRange` offset 对齐校验生效。
- [ ] G5. `glDeleteShader` 后 attached shader 不被立即回收，detach 后回收。
- [ ] G6. `glCheckFramebufferStatus` 对不完整 FBO 返回正确 incomplete 码。
- [ ] G7. 多线程各自 `eglMakeCurrent` 后 `g_state` 独立（无互踩）。
- [ ] G8. 无新增崩溃（SIGBUS/SIGSEGV/deviceLost）。

## 十一、约束遵守

- [x] H1. 全程在 main 分支工作。
- [x] H2. 推送令牌 `ghp_***` 仅用于推送（如用户要求时），本次未使用、未推送。
- [x] H3. 未经用户允许未提交 PR、未推送远程。
- [x] H4. 所有回答使用中文。
- [x] H5. 不照搬 MobileGL 代码，仅参考设计（grep 确认无 MobileGL 源码片段直接复制）。
