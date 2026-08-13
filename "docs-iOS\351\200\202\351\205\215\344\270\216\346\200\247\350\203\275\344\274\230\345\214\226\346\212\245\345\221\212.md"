# Mithril-Wrapper — iOS / Metal 2 / MoltenVK 适配与性能优化报告

> 本轮工作围绕你的三个要求展开：**① 对照上游 MobileGL 再查缺陷、② 检查 iOS/Metal 2/MoltenVK 适配性、③ 把渲染性能优化到最好。**
>
> 验证基线：`38/38` 编译单元全部通过，GL 4.6 Core **657/657** 入口点全部导出（LWJGL 判定 `OpenGL46 = true`）。

---

## 一、结论速览

| 项目 | 状态 |
|---|---|
| GL 3.3 / 4.0 / 4.6 Core 完整度 | **100%**，657 个入口点无缺失 |
| 编译单元 | 38 个全部通过（新增 `UniformArena.cpp`） |
| P0 级致命缺陷 | **3 个已全部修复** |
| P1 级缺陷 | **4 个已修复，1 个明确标注为已知限制** |
| 性能瓶颈（每 draw 重建描述符集） | **已重构**，同时修掉一个同帧覆写的正确性 bug |
| 能否在真机渲染 MC | 静态验证全绿；**最终确认需要 macOS/iOS 真机跑一次**（沙箱无 Metal 设备） |

---

## 二、P0 级缺陷（不修就起不来 / 黑屏）

### P0-2 `vkCreateDevice` 直接失败 —— 已修复

**问题**：`dynamicRendering` 和 `extendedDynamicState` 两个 feature 结构体被写死 `VK_TRUE` 并**无条件**链进 `pNext`，但对应的扩展却是 `has_extension()` 条件启用的。两者不一致 → 扩展缺席时 `vkCreateDevice` 返回 `VK_ERROR_FEATURE_NOT_PRESENT` → 设备创建失败 → 渲染器根本起不来。

**修复**：改成动态构建的 feature 链（`featureChainHead` + `append_feature`），只有扩展确实可用时才链入；`devCI.pNext` 由写死的 `&dynRenderFeat` 改为 `featureChainHead`。

**顺带发现的连带崩溃**：`portability_subset` / `index_type_uint8` / `list_restart` 三处原本直接写 `chainTail->pNext`。一旦前两个扩展都缺席，`chainTail` 为 `nullptr` → **空指针解引用**。已统一改走 `append_feature`。

### P0-1 深度格式在 Apple GPU 上不可用 —— 已修复

**问题**：`FormatMap.cpp` 把 `GL_DEPTH_COMPONENT24` / `GL_DEPTH24_STENCIL8` 一律映射为 `VK_FORMAT_D24_UNORM_S8_UINT`。而 `MTLPixelFormatDepth24Unorm_Stencil8` 在**所有 iOS 设备和 Apple Silicon Mac 上都不可用**（仅部分 Intel Mac 独显支持）→ `vkCreateImage` 失败 → 深度附件缺失 → 黑屏。这是 iOS 上最典型的一类死法。

**修复**：新增 `resolve_supported_format()`（`Resources.cpp`），按候选链探测 `optimalTilingFeatures`。相比上游 MobileGL 的 `FindSupportedDepthStencilFormat()`（只处理 swapchain 那一个深度格式），我们做得更细——用户通过 `glTexImage2D` / `glRenderbufferStorage` 传进来的**任意**格式都要处理，因此按「是否需要 stencil」分成三条候选链，避免把纯深度请求无谓地升级成 depth-stencil（那会浪费显存并改变 `aspectMask`）。结果按 `(格式, 所需 feature)` 缓存。

### P0-3 格式能力位判断写反 —— 已修复

**问题**：`optimalTilingFeatures == 0` 被注释成「驱动没报告任何 feature（不该发生）」，于是兜底强行加上 `SAMPLED | COLOR_ATTACHMENT`。但这个值在 Vulkan 里有明确语义：**该格式完全不受支持**——正是 iOS 上 BC1/BC2/BC3 压缩纹理的正常返回值（Apple GPU 只有 ASTC/ETC/PVRTC）。把「不支持」当成「信息缺失」并强加 `COLOR_ATTACHMENT_BIT`，会让 `vkCreateImage` 直接失败，比不加更糟。

**修复**：`feats == 0` 时只保留 `TRANSFER` 位（对任何格式都合法），并在纹理创建入口先过一遍 `resolve_supported_format()`，无替代格式时回退 RGBA8 并留日志。

---

## 三、iOS / Metal 2 / MoltenVK 适配性检查

### 新发现：两个硬依赖缺席时会静默黑屏 / 崩溃 —— 已改为启动即报错

修完 P0-2 之后我继续追了一步，发现光「不崩」是不够的：

- **`VK_KHR_dynamic_rendering`**：整个 `CommandStream.cpp` 只有 `vkCmdBeginRendering` 一条录制路径，没有传统 `VkRenderPass` 退路。而我们请求的是 **Vulkan 1.2**，该特性要到 1.3 才进核心。缺席时 `vkGetDeviceProcAddr` 返回 `nullptr`，代码里的 `if (fn)` 会**安静跳过**——pass 标记为 active、draw 照常录制，但没有附件绑定，什么都画不出来。没有报错、没有验证层警告，只有一块黑屏。

- **`VK_EXT_extended_dynamic_state`**：`backend_set_cull_mode` / `set_front_face` / `set_depth_test` 是**直接调用** `vkCmdSetCullMode` 那一族，不是取指针后判空。扩展没启用时这些符号是空指针，直接调用 = **段错误**。而且 `Pipeline.cpp:633` 已经把 `cullMode` 设成 `NONE` 并声明为动态状态，管线里没有静态配置可回退。

**处理**：两者都改为在 `backend_init` 阶段明确失败并打印可操作的日志（提示升级 MoltenVK 到 1.1.0+，对应 iOS 14+ / macOS 11+）。**我认为让它在启动时带着原因失败，远好过让用户对着一块黑屏猜。**

### 其余 Apple 平台约束核查

| 约束 | 核查结果 |
|---|---|
| Metal 无 3 分量像素/顶点格式 | 已展开为 4 分量（此前已修） |
| Metal 无 64 位顶点格式 | 见下方 P1 已知限制 |
| Argument Buffer Tier 1 仅 31 个 texture slot | `kMaxTextureUnits = 32`，上报值已夹紧 |
| `maxDescriptorSetUniformBuffersDynamic` 偏小（常见 8） | 已实现降级路径，见性能章节 |
| swapchain 深度格式 | 用 `D32_SFLOAT_S8_UINT`，Apple 支持，安全 |
| 设备显存紧张 | arena 采用增长式分块，非一次性预分配 |

---

## 四、P1 级缺陷

### GL 上限硬编码 —— 已修复

原先 `GL_MAX_TEXTURE_SIZE` 写死 `16384`，而 A9/A10 这类 iOS GPU 上限只有 `8192`。**谎报上限的危害是实打实的**：Sodium 和 Iris 会照着这个数字去分配阴影贴图和图集，`vkCreateImage` 直接失败 → 纹理丢失甚至崩溃。

`GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS` 写死 `80`，却和内部 `kMaxTextureUnits = 32` 自相矛盾——上层按 80 个单位绑定，后端只有 32 个槽，多出来的被静默丢弃。

**修复**：新增 `backend_device_limit()`（20 个 `MITHRIL_LIMIT_*` 查询项），从真实 `VkPhysicalDeviceLimits` 取值，并同时夹到「设备上限」和「内部数组容量」两者的较小值。已接入的项目包括纹理尺寸、纹理单元、viewport、MSAA 采样数、UBO 对齐与大小、SSBO、compute 工作组等。

三个细节值得一提：
- `uint32_t → GLint` 统一夹到 `INT32_MAX`，避免驱动上报 `0xFFFFFFFF` 变成 `-1`（Sodium 会算出负的数组大小）。
- `GL_MAX_SAMPLES` 取 **color 与 depth 采样数的交集**，只看 color 会在 depth 不支持 4x 时上报过高。
- `GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT` 必须报真实值，报小了会触发 dynamic offset 未对齐的 VUID 校验失败。

### 顶点格式缺少 `bufferFeatures` 校验 —— 已修复

在管线创建路径加了运行时兜底：检查顶点属性格式的 `bufferFeatures` 是否含 `VERTEX_BUFFER_BIT`，不满足就点名报告 location 和 GL type。**这类故障（整条管线创建失败 → 该 program 的所有 draw 静默消失）极难从现象反推原因**，让它在日志里可见是值得的。结果按格式缓存，不影响热路径。

### `GL_DOUBLE` 顶点属性 —— 标注为已知限制（刻意不「修」）

Metal **完全没有** 64 位顶点格式，MSL 也不支持 `double`。

我一开始把映射改成了 R32，然后意识到**那是错的**：缓冲区里每个分量占 8 字节，按 4 字节去取只会读到 double 的低半边，得到彻底的垃圾几何。要正确支持必须在 CPU 侧把整条顶点流重打包（上游 MobileGL 的 `RepackVertexStream` 就是干这个的），而本项目目前没有这套基础设施。

所以我**回退了那个改动**，保留 R64 映射并加上明确的警告日志和 TODO。理由：MC / Sodium / Iris 的顶点格式只用 float / byte / short / packed-2101010，从不使用 `GL_DOUBLE`，这条路径在目标工作负载下不会被触发；而让它在管线创建时明确失败并留下日志，好过静默渲染出垃圾几何。

---

## 五、性能优化

### 原有瓶颈

`bind_program_descriptors` 是 Sodium 工作负载下最热的函数（每帧几千次 draw，每次都调）。原实现每次都：
1. 对每个 uniform 做 `std::string` 哈希查表
2. 整个 UBO 按 std140 重新打包
3. 整块上传 GPU
4. `vkAllocateDescriptorSets` + `vkUpdateDescriptorSets`

即使这一帧 uniform 一个字节都没变，也照做不误。

**更要命的是一个正确性 bug**：每个 program 的 UBO 是**单个** `VkBuffer`（key = `program*1000000+binding`），每次原地覆写。同一帧内「draw → 改 uniform → 再 draw」时，第二次写入会冲掉第一次的数据——而第一次的 draw 命令此时还没执行完，它读到的是被污染的数据。

### 重构方案（对齐上游 MobileGL 的 arena + dynamic offset + 内容哈希设计）

新增 `UniformArena.{h,cpp}`，并重写 `bind_program_descriptors`：

- **UBO arena**：每个 in-flight slot 一条块链，256 KB 起、翻倍至 4 MB/块、最多 16 块。用**链式而非 realloc**，保证本帧已发出的 slice 不会被搬走。对齐取 `max(minUniformBufferOffsetAlignment, 16)`。每次 draw 拿新字节，**同帧覆写 bug 自然消失**。
- **rewind 时机**：只在 `ensure_command_buffer_recording()` 里、该 slot 的 fence 等待**之后**执行。我专门核对过这一点——fence wait 证明该 slot 上提交过的所有命令缓冲都已执行完毕，此时回收才安全；在 `commit_frame` 等任何其他位置 rewind 都会回收 GPU 尚未读完的内存。
- **内容哈希跳过上传**：FNV-1a 哈希相同且 slice 仍在同一 `(slot, frameGeneration)` → 跳过 bump 分配与 memcpy。
- **描述符集复用**：签名相同 → 跳过 `vkAllocateDescriptorSets` + `vkUpdateDescriptorSets`；set/layout/dynamic offsets 全同 → 连 `vkCmdBindDescriptorSets` 都跳过（Sodium 的地形批次正好是这种连续同构 draw，MoltenVK 会把重复绑定翻译成真实的 Metal argument buffer 重绑工作）。
- **消除字符串哈希**：uniform 名到 std140 偏移的映射在 link 时固化成紧凑数组，运行时不再碰字符串。

### MoltenVK 降级路径

`maxDescriptorSetUniformBuffersDynamic` 在多款 Apple GPU 上是 8（也是 Vulkan 规范下限）。超限会让 `vkCreateDescriptorSetLayout` 直接失败 → 该 program 没有 layout → 所有 draw 静默消失。

因此：程序想要的 dynamic UBO 数超过设备允许时，**全部**退回普通 `UNIFORM_BUFFER`——arena 照用（同帧覆写 bug 依然是修好的），只是 slice 偏移改走 `VkDescriptorBufferInfo::offset`。代价仅是描述符集复用率下降。

应用自声明的 block（`glBindBufferBase`）和 UBO 数组一律不做 dynamic：前者已直接指向应用自己的 buffer，无别名风险且常用 `VK_WHOLE_SIZE`（与非零 dynamic offset 组合会违反 `offset + range <= size`）；后者需要按元素序提供多个 offset，会让 `dynamicOffsetCount` 与 layout 不一致——那是硬性违规，不是画错像素而已。

### 我复核过的不变量

重构涉及 `pDynamicOffsets` 这类「错了就是硬崩」的约束，我逐条验证了：

- ✅ **dynamic offset 数量与顺序**：`pr.bindings` 按 `(set, binding)` 升序排序，`dynOffsets` 在同一次遍历中 push，顺序天然一致；所有中途退出路径都是整体 `return` 而非跳过单个 binding（跳过会让计数与 layout 不符）。
- ✅ **arena rewind 在 fence 之后**（见上）。
- ✅ **arena 耗尽的处理**：约 51 MB/帧的 uniform 属于失控而非正常负载，此时整个 bind 放弃并限流告警，而不是退回共享 buffer（那会把刚修好的覆写 bug 请回来）。

---

## 六、还需要你做的事

**沙箱里没有 Metal 设备，所有验证都是静态的。** 真机跑一次能确认的事情，静态分析确认不了。建议在 macOS 或 iOS 真机上：

1. 开 Vulkan validation layer 跑一遍，重点看 `vkCmdBindDescriptorSets` 的 dynamic offset 相关 VUID。
2. 确认启动日志里出现深度格式回退提示（`D24_UNORM_S8_UINT → D32_SFLOAT_S8_UINT`），这说明 P0-1 的修复路径真的被走到了。
3. 用 Xcode GPU Frame Capture 看描述符绑定次数是否随缓存命中显著下降。
4. 拿一张 A9/A10 老设备验证 `GL_MAX_TEXTURE_SIZE` 现在上报 8192 而非 16384。
