# Mithril-Wrapper

在 Apple 平台上为 Minecraft Java（LWJGL 3）提供 OpenGL Core 语义实现的自研渲染库。

当前有两条显式 backend 路径：

- `DirectMetal`：GL frontend/state → shared SPIR-V → SPIRV-Cross MSL → Metal；不调用 Vulkan/MoltenVK。Apple 构建默认选择。
- `Vulkan`：现有 reference/fallback backend；在 Apple 上可通过 MoltenVK，在 Linux 上可通过 Vulkan loader/lavapipe。用 `MITHRIL_BACKEND=vulkan` 显式选择。

可用 `MITHRIL_BACKEND=metal|vulkan` 覆盖默认选择。未知名称或非 Apple 构建请求 `metal` 会明确失败，不会静默切换 backend。

## 状态

- **DirectMetal 原生执行路径**：frontend 已改为 backend-neutral draw/resource contract；Apple 路径直接创建 `MTLDevice`、command queue、offscreen RGBA8 + depth/stencil target，执行 GLSL→SPIR-V→MSL→`MTLLibrary`/`MTLRenderPipelineState`，支持 clear（包括按 `GL_DRAW_BUFFERi` 定向的 `glClearBuffer*`）、triangle/line vertex/index/instance draw、自定义 primitive restart、`GL_SAMPLES_PASSED`/`GL_ANY_SAMPLES_PASSED`、loose uniform、真实 GL uniform block（block reflection/查询、`glUniformBlockBinding`、`glBindBufferBase/Range`，每 stage 最多 12 个）、2D texture/mipmap、单采样 `GL_DEPTH_COMPONENT32F` texture（FBO depth write 后可由 shader 原生读取）、独立 GL sampler object、typed `samplerBuffer`/`isamplerBuffer`/`usamplerBuffer`、texture/renderbuffer FBO、MRT、4× MSAA resolve、RGBA8 `GL_TEXTURE_2D_MULTISAMPLE` render target + `sampler2DMS` 读取、同尺寸 nearest blit、基础 depth/blend/raster/stencil state、GL 3.2 fence sync 和可靠 readback。EGL window surface 接受真实 `CAMetalLayer`，默认 framebuffer 在同一 Metal queue 复制到 drawable 并由 `presentDrawable` 非阻塞提交。program/pipeline 有缓存与销毁边界，pipeline cache 有上限；`glBindAttribLocation`/`glBindFragDataLocation` 在 link 时改写共享 SPIR-V interface location，显式 GLSL layout 优先，Metal/Vulkan 不各自重写 shader namespace；viewport/scissor、texture-unit target/sampler 选择等 observable state 均在调用点快照，不会在延迟编码时折叠；全幅 clear 走 loadAction，局部/写掩码 clear 使用有上限缓存的 Metal clear pipeline。常见交错 VBO（float/half、normalized 8/16-bit、`glVertexAttribIPointer` 的 signed/unsigned 8/16/32-bit）与 GL uniform buffer 都以不可复用 lifetime ID + content version 驻留并跨 draw 复用；backend-neutral vertex ABI 保留 scalar type/normalized，DirectMetal 直接生成匹配的 `MTLVertexFormat`，只有 Metal 无法原生表达的转换才走 typed transient 重排。frontend 在应用 `baseVertex` 前识别任意 GL restart index；triangle/line strip 归一化为 Metal 的原生 32-bit 哨兵，list/fan 在共享 draw 层分段，`GL_LINE_LOOP` 在同层闭合为 line list，因此 backend 不各自复制 GL 语义。每次 query begin 使用独立 backend generation，Metal 提交时为本批次复用一个 visibility buffer；跨 clear/flush 的 query 聚合异步 segment，查询 result 才按 GL 要求等待 CPU。buffer texture 从 authoritative GL buffer 按 content version 派生并使用对齐的 native `MTLBuffer` texture view，普通纹理只拥有 image storage，`MTLSamplerState` 由独立、上限 128 项的 LRU cache 复用，动态索引/实例数据和严格按字节去重后的 loose-uniform block 使用三帧复用 upload arena。每个 native command buffer 只附加轻量 completion 状态；`glFenceSync` 提交此前 deferred batch 但不 CPU wait，有限/无限 `glClientWaitSync` 等待该 completion，删除 GLsync 名称也不会提前释放 in-flight 状态。`glFlush`/`eglSwapBuffers` 不 wait，`glFinish`/readback/query result 才建立 CPU 完成点。
- **DirectMetal 当前诚实边界**：window surface 必须由调用方提供 `CAMetalLayer`；swap 时会在编码 pending frame 前同步 drawable 尺寸，尺寸变化会重建默认 target。depth texture 目前仅支持 level 0、单采样 2D `DEPTH_COMPONENT32F`、NULL 初始数据，并支持匹配的 `sampler2DShadow` compare mode/function；CPU upload/readback、mipmap、D16/D24、depth-stencil 及 array/cube shadow sampler 会明确拒绝或不宣称支持。多重采样纹理目前仅支持 2D RGBA8 color（尚无 multisample array/integer/depth texture）；Vulkan context 对这些尚未迁移的纹理存储入口明确返回 unsupported，而不是单调用切换 backend。buffer texture 当前支持 `RGBA8/R8I/R8UI/R32I/R32UI/R32F`，其余合法 GL texel format 会明确返回 unsupported error。sync objects 当前限于单 context/单 Metal command queue（尚未宣称跨 context 共享）。timer/transform-feedback query、两个 occlusion target 同时重叠、dual-source fragment output/blending 尚未接通，会明确返回错误而不是假值。compute、缩放/过滤 framebuffer blit、任意/整数 border color、非零 sampler LOD bias，以及 3D texture 的 mip 链采样尚未接通；mip filter 遇到不完整链会明确拒绝 draw，其余相关路径也会返回错误并记录 unsupported，不宣称这些能力。需要这些功能时应显式使用 Vulkan backend。
- **Vulkan reference 边界**：scissored clear 已保持顺序并按矩形执行；部分 color-channel 或 stencil-bit clear 暂不近似；真实 GL uniform block 与 texture-buffer descriptor seam 尚未接入。上述路径均明确返回 unsupported/error，不会误用 loose-uniform allocation 或 2D image descriptor 近似执行。
- **M0 基建已交付**：CMake 双分支工程、EGL 44 符号 + 契约冒烟、GL 342 符号导出、CI build.yml、契约文档。
- **M1 状态引擎完成**：`src/state/`（全局 Context、错误 FIFO、capability 表）；`src/gl/state.cpp`（S1 组 48 函数真实现：glClear/glViewport/glEnable/glGetString「3.3 Core Profile」/glGetError 等）；生成脚本 `scripts/gen_gl_stubs.py` 支持实现排除名单重新生成 stub。
- **M2 完成：着色器管线 + Vulkan 后端接通**：
  - `src/shader/`：glslang GLSL→SPIR-V + SPIRV-Cross 反射（编译缓存、松散 uniform 折入合成 UBO、GLSL 150 自动升级 330；按域拆分 `glsl.cpp`/`reflect.cpp`/`registry.cpp`）；`src/gl/shader.cpp` S2 组约 60 函数真实现（shader 生命周期、link/use、glUniform* 全系、getter）；shader_smoke 通过。
  - `src/vk/`：dlsym 动态加载 Vulkan loader/ICD（libvulkan → MoltenVK）；instance 级函数经真实 instance 句柄解析（全局 GIPA 只保证全局函数）；UBO 反射 VS+FS 双阶段合并 → 动态 UBO 池；staging 顶点缓冲；renderpass + 清屏 + 帧读回。
  - `tests/draw_smoke.c` 全链通过（llvmpipe）：GL 层着色 + glDrawArrays → Vulkan 绘制 → glReadPixels 校验（白三角形、tint 驱动变色、背景色）。
- **M3 完成：顶点数据**：S3 组 76/114 真实现（顶点属性全家族：pointer/IPointer/常量 1-4 系/Divisor、buffer 映射与查询家族、10 个 draw 入口：DrawArrays(Instanced)/DrawElements(Instanced/BaseVertex/Range 双变体)/MultiDraw 全系）；引擎新增双顶点流（顶点+实例）、索引缓冲（统一 UINT32 staging）、TriangleStrip/Fan 拓扑；实例化采用 CPU 逐实例打包（divisor 行复制）；非 4 字节对齐 stride/offset 由 CPU 规整为 float32 打包；`draw_smoke` 扩展 8 个 M3 断言全部通过。
- **M4 纹理完成（S4 42/42 函数）**：`src/vk/texture.cpp` 上传路径扩展（staging→CmdCopyBufferToImage 全 mip 逐切片、image/view 覆盖 2D/1D、3D volume、2D/1D array、cubemap、白 dummy 兜底）；sampler 不再错误地归 texture image 所有，而是在 draw 时解析 texture parameter 或 unit 上绑定的 GL sampler object，并在 Metal/Vulkan backend 使用有上限的 native sampler cache。`src/gl/texture.cpp` 全量真实现（TexImage1D/2D/3D + TexSub 全系含 cubemap face/array 分层、GetTexImage PACK 回读、GenerateMipmap 逐切片滤波、TexParameter/GetTexParameter/GetTexLevelParameter 全系、S3TC DXT1/3/5 CPU 解压 + GetCompressedTexImage、CopyTexImage/CopyTexSubImage 帧读回、glTexBuffer、glPixelStoref）；`texture_smoke` 26 断言全通过（llvmpipe：红纹理采样/mip/dummy 白/GetTexImage 往返/3D 切片/数组分层/cubemap 6 面/拷贝/texBuffer/压缩）。
- **M5 完成（S5 FBO 全量 24 + MRT + MSAA）**：
  - stage A+B 状态管线：`src/vk/pipeline.cpp` 深度附件 D24S8、`PipelineState` 烘焙进 pipeline 缓存 key、显式清除、动态 scissor Y 翻转、depth/blend/cull/frontFace/stencil 域、colorMask/polygon；GL 侧 `BuildPipelineState` 快照接入。
  - stage C S5 FBO/渲染缓冲：`src/vk/fbo.cpp`（renderbuffer 表 CreateRbImage/Rb view、FBO 表 SetFramebuffer + 懒重建 Vk framebuffer/renderpass，`ResolveDrawFbo` 脏检测 + 纹理重传跟随、`BlitFramebuffer`）；`draw.cpp` SubmitFlush 按 target（默认或 FBO）清屏-渲染-回读，readback buffer 按目标尺寸重建，read/draw 分离绑定；`src/gl/fbo.cpp` 对象表 + 24 函数真实现。
  - **MRT**：`src/gl/fbo.cpp` color[8] 多附件槽 + `glDrawBuffers`/`glDrawBuffer`/`glReadBuffer` 真实现；Vk 层 renderpass N 附件 + 附件 N 视图、pipeline `attachmentCount=附件数` 每附件独立 blend（draw_mask 未选型号清写掩码）、显式 clear 逐附件（仅 draw buffer 选中）、读回按 read_buf 挑附件。
  - **MSAA**：renderbuffer `samples>1` → `rasterizationSamples` + resolve 附件（单采样转储）、clear 写颜色与 resolve、读回 resolve 图；`ToVkSampleCount` 映射 1/2/4/8/16/32/64。
  - `tests/fbo_smoke.c` 29 行 ok（28 断言）：17 状态断言 + S5 FBO 纹理/RBO/blit + MRT 双附件读回与单 drawBuffer 门控 + MSAA 4x resolve 回读；八冒烟（contract/state/shader/draw/texture/fbo/3d/render3d）回归全过。

## 快速构建（Linux 开发循环）

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
# 产物: output/libmithril.so
```

Apple/DirectMetal 本机构建：

```sh
cmake -S . -B build-macos -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-macos
clang -o /tmp/mithril-draw-smoke tests/draw_smoke.c
MITHRIL_BACKEND=metal MITHRIL_EXPECT_RENDERER=DirectMetal /tmp/mithril-draw-smoke
clang -std=c11 -o /tmp/mithril-ubo-smoke tests/ubo_smoke.c
MITHRIL_BACKEND=metal MTL_DEBUG_LAYER=1 /tmp/mithril-ubo-smoke
clang -std=c11 -o /tmp/mithril-sampler-smoke tests/sampler_smoke.c
MITHRIL_BACKEND=metal MTL_DEBUG_LAYER=1 /tmp/mithril-sampler-smoke
clang -std=c11 -o /tmp/mithril-buffer-texture-smoke tests/buffer_texture_smoke.c
MITHRIL_BACKEND=metal MTL_DEBUG_LAYER=1 /tmp/mithril-buffer-texture-smoke
clang -std=c11 -o /tmp/mithril-sync-smoke tests/sync_smoke.c
MITHRIL_BACKEND=metal MTL_DEBUG_LAYER=1 /tmp/mithril-sync-smoke
clang -std=c11 -o /tmp/mithril-typed-vertex-smoke tests/typed_vertex_smoke.c
MITHRIL_BACKEND=metal MTL_DEBUG_LAYER=1 /tmp/mithril-typed-vertex-smoke
clang -std=c11 -o /tmp/mithril-query-smoke tests/query_smoke.c
MITHRIL_BACKEND=metal MTL_DEBUG_LAYER=1 /tmp/mithril-query-smoke
```

## 冒烟测试

```sh
gcc -o tests/contract_smoke tests/contract_smoke.c -ldl
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu ./tests/contract_smoke   # EGL 契约
gcc -o tests/state_smoke tests/state_smoke.c -ldl
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu ./tests/state_smoke      # GL 状态机
gcc -o tests/shader_smoke tests/shader_smoke.c -ldl
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu ./tests/shader_smoke     # 着色器管线
gcc -o tests/draw_smoke tests/draw_smoke.c -ldl
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu ./tests/draw_smoke       # GL→Vulkan 全链三角（需 lavapipe/loader）
gcc -o tests/texture_smoke tests/texture_smoke.c -ldl
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu ./tests/texture_smoke   # M4 纹理全链（需 lavapipe/loader）
gcc -o tests/fbo_smoke tests/fbo_smoke.c -ldl
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu ./tests/fbo_smoke      # M5 S5 FBO/渲染缓冲 + MRT + MSAA + 状态管线（depth/scissor/blend/cull/stencil/colorMask）
gcc -o tests/3d_smoke tests/3d_smoke.c -ldl -lm
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu ./tests/3d_smoke       # 3D 深度排序 + 透视投影（mat4 uniform 全链）
gcc -o tests/render3d_smoke tests/render3d_smoke.c -ldl -lm
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu ./tests/render3d_smoke # 俯视场景：地板网格 + 立方体 + 像素断言，导出 tests/render3d.ppm
python3 scripts/ppm_render.py tests/render3d.ppm tests/render3d.png # PPM→PNG
```

> 本开发容器 ldd 找不到 libstdc++/libm/libgcc_s，运行 .so 相关程序需
> 显式 `LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu`。

## 契约文档
- `CHECKLIST.md` — 桥接契约（Amethyst GL bridge 硬性要求）+ 里程碑规划 + 架构
- `docs/gl33_core_list.md` — GL 3.3 core 342 函数分组
- `docs/egl_list.md` — EGL 导出符号清单

## 目录结构
```
src/egl     EGL 层（44 符号，display/config/context/surface 生命周期）
src/gl      分发电层（exports.cpp 生成 + 按域拆分的真实现 state/shader/vertex/draw）
src/shader  glslang GLSL→SPIR-V + SPIRV-Cross 反射（M2 完成）
src/state   GL 状态引擎（Context 结构、错误队列、capability 表）
src/backend backend-neutral draw/resource contract 与显式 backend 选择
src/metal   DirectMetal（SPIR-V→MSL、program/pipeline cache、frame arena、command encoding/readback）
src/vk      Vulkan reference/fallback（dlsym 加载器、离屏渲染、动态 UBO 池、读回）
scripts/    gen_gl_stubs.py（stub 生成器）、exported_symbols.txt
tests/      contract_smoke.c / state_smoke.c / shader_smoke.c / draw_smoke.c / ubo_smoke.c / sampler_smoke.c / buffer_texture_smoke.c / sync_smoke.c / typed_vertex_smoke.c / query_smoke.c / texture_smoke.c / fbo_smoke.c / 3d_smoke.c / render3d_smoke.c
```
