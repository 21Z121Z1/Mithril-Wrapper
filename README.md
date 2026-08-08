# Mithril-Wrapper

在 iOS 上为 Minecraft Java（LWJGL 3）提供 OpenGL 3.3 Core 实现的自研渲染库。
GL → Vulkan → Metal（MoltenVK），无 OpenGL ES 参与。

## 状态

- **M0 基建已交付**：CMake 双分支工程、EGL 44 符号 + 契约冒烟、GL 342 符号导出、CI build.yml、契约文档。
- **M1 状态引擎完成**：`src/state/`（全局 Context、错误 FIFO、capability 表）；`src/gl/gl_impl.cpp`（S1 组 48 函数真实现：glClear/glViewport/glEnable/glGetString「3.3 Core Profile」/glGetError 等）；生成脚本 `scripts/gen_gl_stubs.py` 支持实现排除名单重新生成 stub。
- **M2 完成：着色器管线 + Vulkan 后端接通**：
  - `src/shader/`：glslang GLSL→SPIR-V + SPIRV-Cross 反射（编译缓存、松散 uniform 折入合成 UBO、GLSL 150 自动升级 330）；`gl_impl.cpp` S2 组约 60 函数真实现（shader 生命周期、link/use、glUniform* 全系、getter）；shader_smoke 通过。
  - `src/vk/`：dlsym 动态加载 Vulkan loader/ICD（libvulkan → MoltenVK）；instance 级函数经真实 instance 句柄解析（全局 GIPA 只保证全局函数）；UBO 反射 VS+FS 双阶段合并 → 动态 UBO 池；staging 顶点缓冲；renderpass + 清屏 + 帧读回。
  - `tests/draw_smoke.c` 全链通过（llvmpipe）：GL 层着色 + glDrawArrays → Vulkan 绘制 → glReadPixels 校验（白三角形、tint 驱动变色、背景色）。

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
src/gl      分发电层（gl_exports.cpp 生成 + gl_impl.cpp 真实现）
src/shader  glslang GLSL→SPIR-V + SPIRV-Cross 反射（M2 完成）
src/state   GL 状态引擎（Context 结构、错误队列、capability 表）
src/vk      Vulkan 后端（dlsym 加载器、离屏渲染、动态 UBO 池、读回）
scripts/    gen_gl_stubs.py（stub 生成器）、exported_symbols.txt
tests/      contract_smoke.c / state_smoke.c / shader_smoke.c / draw_smoke.c
```