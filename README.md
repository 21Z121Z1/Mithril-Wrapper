# Mithril-Wrapper

在 iOS 上为 Minecraft Java（LWJGL 3）提供 OpenGL 3.3 Core 实现的自研渲染库。
GL → Vulkan → Metal（MoltenVK），无 OpenGL ES 参与。

## 状态

- **M0 基建已交付**：CMake 双分支工程、EGL 44 符号 + 契约冒烟、GL 342 符号导出、CI build.yml、契约文档。
- **M1 状态引擎完成**：`src/state/`（全局 Context、错误 FIFO、capability 表）；`src/gl/state.cpp`（S1 组 48 函数真实现：glClear/glViewport/glEnable/glGetString「3.3 Core Profile」/glGetError 等）；生成脚本 `scripts/gen_gl_stubs.py` 支持实现排除名单重新生成 stub。
- **M2 完成：着色器管线 + Vulkan 后端接通**：
  - `src/shader/`：glslang GLSL→SPIR-V + SPIRV-Cross 反射（编译缓存、松散 uniform 折入合成 UBO、GLSL 150 自动升级 330；按域拆分 `glsl.cpp`/`reflect.cpp`/`registry.cpp`）；`src/gl/shader.cpp` S2 组约 60 函数真实现（shader 生命周期、link/use、glUniform* 全系、getter）；shader_smoke 通过。
  - `src/vk/`：dlsym 动态加载 Vulkan loader/ICD（libvulkan → MoltenVK）；instance 级函数经真实 instance 句柄解析（全局 GIPA 只保证全局函数）；UBO 反射 VS+FS 双阶段合并 → 动态 UBO 池；staging 顶点缓冲；renderpass + 清屏 + 帧读回。
  - `tests/draw_smoke.c` 全链通过（llvmpipe）：GL 层着色 + glDrawArrays → Vulkan 绘制 → glReadPixels 校验（白三角形、tint 驱动变色、背景色）。
- **M3 完成：顶点数据**：S3 组 76/114 真实现（顶点属性全家族：pointer/IPointer/常量 1-4 系/Divisor、buffer 映射与查询家族、10 个 draw 入口：DrawArrays(Instanced)/DrawElements(Instanced/BaseVertex/Range 双变体)/MultiDraw 全系）；引擎新增双顶点流（顶点+实例）、索引缓冲（统一 UINT32 staging）、TriangleStrip/Fan 拓扑；实例化采用 CPU 逐实例打包（divisor 行复制）；非 4 字节对齐 stride/offset 由 CPU 规整为 float32 打包；`draw_smoke` 扩展 8 个 M3 断言全部通过。
- **M4 纹理完成（S4 42/42 函数）**：`src/vk/texture.cpp` 上传路径扩展（staging→CmdCopyBufferToImage 全 mip 逐切片、image/view/sampler 覆盖 2D/1D、3D volume、2D/1D array、cubemap、wrap_r、白 dummy 兜底）；`src/gl/texture.cpp` 全量真实现（TexImage1D/2D/3D + TexSub 全系含 cubemap face/array 分层、GetTexImage PACK 回读、GenerateMipmap 逐切片滤波、TexParameter/GetTexParameter/GetTexLevelParameter 全系、S3TC DXT1/3/5 CPU 解压 + GetCompressedTexImage、CopyTexImage/CopyTexSubImage 帧读回、glTexBuffer、glPixelStoref）；`texture_smoke` 26 断言全通过（llvmpipe：红纹理采样/mip/dummy 白/GetTexImage 往返/3D 切片/数组分层/cubemap 6 面/拷贝/texBuffer/压缩）。
- **M5 进行中（stage A+B 完成）**：状态管线 `src/vk/pipeline.cpp`（深度附件 D24S8、`PipelineState` 烘焙进 pipeline 缓存 key + `Vk*CreateInfo`、显式 `CmdClearDepthStencilImage`/`CmdClearColorImage` 统一清除、动态 scissor Y 翻转、depth/blend/cull/frontFace/stencil 域（含读/写掩码、ref）/colorMask/polygon 枚举映射；stage B 补 stencil 写掩码独立字段、depth view S8 aspect、renderpass stencil loadOp=LOAD、frontFace GL→VK 取反）；GL 侧 `BuildPipelineState` 快照与状态 setter 全接入。`tests/fbo_smoke.c` 18 行 ok（17 状态断言）：depth 近者胜/LEQUAL、scissor 中心清屏+角落收色、blend off/src-alpha（mobilegl 非预乘）、cull GL_BACK 保留 + GL_FRONT/GL_CW 剔除、stencil REPLACE→EQUAL 读回 + ref 不匹配拦截、colorMask 分通道门控、polygonMode GL_LINE 留空；五冒烟回归通过，CI Linux job 已接 fbo_smoke。待办：S5 FBO/渲染缓冲 + MRT、MSAA。

## 快速构建（Linux 开发循环）

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
# 产物: output/libmithril.so
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
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu ./tests/fbo_smoke      # M5 状态管线（depth/scissor/blend/cull/stencil/colorMask）
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
src/vk      Vulkan 后端（dlsym 加载器、离屏渲染、动态 UBO 池、读回；engine/dispatch/target/pipeline/draw 按域拆分）
scripts/    gen_gl_stubs.py（stub 生成器）、exported_symbols.txt
tests/      contract_smoke.c / state_smoke.c / shader_smoke.c / draw_smoke.c / texture_smoke.c
```