# Mithril-Wrapper

在 iOS 上为 Minecraft Java（LWJGL 3）提供 OpenGL 3.3 Core 实现的自研渲染库。
GL → Vulkan → Metal（MoltenVK），无 OpenGL ES 参与。

## 状态

- **M0 基建已交付**：CMake 双分支工程、EGL 44 符号 + 契约冒烟、GL 342 符号导出、CI build.yml、契约文档。
- **M1 状态引擎完成**：`src/state/`（全局 Context、错误 FIFO、capability 表）；`src/gl/gl_impl.cpp`（S1 组 48 函数真实现：glClear/glViewport/glEnable/glGetString「3.3 Core Profile」/glGetError 等）；生成脚本 `scripts/gen_gl_stubs.py` 支持实现排除名单重新生成 stub。

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
src/state   GL 状态引擎（Context 结构、错误队列、capability 表）
src/vk      Vulkan 后端（规划；iOS 经 MoltenVK→Metal）
src/shader  glslang 集成（规划）
scripts/    gen_gl_stubs.py（stub 生成器）、exported_symbols.txt
tests/      contract_smoke.c / state_smoke.c
```