# 修复任务：Mithril-Wrapper 纯红 + GPU Page Fault（MoltenVK 1.2.9 / A11）

## 项目
- iOS OpenGL→Vulkan/MoltenVK 包装器，跑 Minecraft 1.17+，测试用的1.21.1（用的opengl 4.6不知道完不完整）
- 设备：iPhone X / A11 / Metal 2。MoltenVK 版本：1.2.9。
- 分支：feat/ios-metal2-moltenvk-compat-and-perf
- 远程：https://github.com/EternityQwQ/Mithril-Wrapper.git
- 推送：git push "https://x-access-token:${GITHUB_TOKEN}@github.com/EternityQwQ/Mithril-Wrapper.git" feat/ios-metal2-moltenvk-compat-and-perf（${GITHUB_TOKEN} 为环境变量注入的 Personal Access Token，勿明文写死）

## 症状
- 画面**纯红**：MC 加载界面(MOJANG 红底)该显示的白字"MOJANG/STUDIOS"和白色进度条**都没画出来**，只剩纯红背景。
- 加载→主菜单过渡时**闪一正确帧**，然后 GPU 地址访问错误
  (kIOGPUCommandBufferCallbackErrorPageFault / VK_ERROR_OUT_OF_DEVICE_MEMORY: code 3)
  → device lost → 崩溃退出。日志里 wrapper 全程无警告。

## 已确认(重要，别再走弯路)
- wrapper 日志全程干净 → 非 OOM/GC 路径(那些会打 log)。
- bcb1325 新增的 descriptors_bound 守卫**零警告** → descriptor 绑定了、draw 没被丢。
- 所有资源生命周期(延迟销毁/disposal queue/UBO arena/纹理重规格 memo 失效)经核查均 fence 安全。
- 纯色 clear 能出来，但**所有带纹理/几何的绘制全失败** → 方向锁定**纹理采样链路**。

## 修复方向(从这入手)
纹理采样链：纹理上传(Resources.cpp backend_get_or_create_texture / upload_compressed_tex /
stage_and_copy_image)→ sampler 创建(Resources.cpp backend_get_or_create_sampler, 注意 keyed by name 只缓存首个参数)→
descriptor 写入 imageView+sampler(DescriptorSet.cpp bind_program_descriptors)。
重点排查：某纹理 view 是否可能为 NULL 或 stale 仍被采样；MoltenVK 1.2.9 在 A11 上采样
特定纹理/格式是否有 GPU fault；mip 链上传不完整导致采样读越界。

## 要求
- 修完：能编译(本机无 vulkan.h，可仅做静态/括号校验，真实编译走 iOS CI build.yml)。
- 提交并推送上述 push 命令。
- 用中文回答用户。