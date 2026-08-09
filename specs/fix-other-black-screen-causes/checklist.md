# Checklist

## 一、根因 E：MVK_CONFIG_VK_SEMAPHORE_SUPPORT_STYLE

- [x] E1. `MG_Backend/DirectVulkan/Device.cpp` 的 setenv 块已增加 `setenv("MVK_CONFIG_VK_SEMAPHORE_SUPPORT_STYLE", "1", 1);`（Device.cpp:453）。
- [x] E2. 该 setenv 调用位于 MoltenVK 实例创建之前（vkCreateInstance 之前，setenv 块在 438-453 行，Instance 创建在 455 行后）。
- [x] E3. 注释说明强制 Metal 信号量同步，防止 Rosetta2/NVIDIA/旧版 MoltenVK 退化为 no-op（Device.cpp:443-452）。

## 二、根因 F：部分纹理上传保留既有内容

- [x] F1. `stage_and_copy_image`（Resources.cpp）签名已增加 `bool is_full_upload` 参数（Resources.cpp:228-231）。
- [x] F2. `stage_and_copy_image` 中初始 barrier 的 `oldLayout` 为 `is_full_upload ? VK_IMAGE_LAYOUT_UNDEFINED : tex.currentLayout`（Resources.cpp:371）。
- [x] F3. `backend_texture_upload`（Backend.h 声明）已增加 `int is_full_upload` 参数（Backend.h:196-199）。
- [x] F4. `backend_texture_upload`（Resources.cpp 定义）同步增加参数并透传到 `stage_and_copy_image`（Resources.cpp:732-741，`is_full_upload != 0` 转 bool）。
- [x] F5. `glTexImage2D`/`glTexImage3D`（Texture.cpp）调用 `backend_texture_upload` 传 `is_full_upload=1`（Texture.cpp:152, 176）。
- [x] F6. `glTexSubImage2D`/`glTexSubImage3D`（Texture.cpp）调用 `backend_texture_upload` 传 `is_full_upload=0`（Texture.cpp:244, 257）。
- [x] F7. `glCopyTexSubImage2D` 是 stub 未实现（Texture.cpp:604-609），不调用 backend_texture_upload，无需修改。
- [x] F8. grep 确认全工程无其他 `backend_texture_upload` 调用点遗漏新参数（仅 4 处调用 + 声明 + 定义，已全部更新）。

## 三、根因 G：RGB swapchain clear alpha 强制 1.0

- [x] G1. `CommandStream.cpp` 已增加 `format_has_alpha(VkFormat)` helper（CommandStream.cpp:25-39）。
- [x] G2. clear 路径（`begin_render_pass` 设置 clearValue 和 `clear_attachments` 设置 cv）在 swapchain 格式无 alpha 时强制 `clearValue.color.float32[3] = 1.0f`（CommandStream.cpp:475-482, 575-580）。
- [x] G3. RGBA 格式（当前默认 BGRA8）行为不变（format_has_alpha 对 BGRA8 返回 true，alpha 原样使用）。

## 四、编译验证

- [x] V1. 修改后的 .cpp 文件通过 g++ -fsyntax-only（-std=c++20 -Wall -Wextra）：Texture.cpp、CommandStream.cpp 完全干净；Resources.cpp 仅 pre-existing unused 警告；Device.cpp 错误为 pre-existing PORTABILITY_SUBSET 宏缺失（stash 验证与本次修改无关）。（注：linux 沙箱无 MoltenVK/ObjC++，仅做跨平台 C++ TU 语法检查；完整链接构建需在 Apple 目标环境进行）

## 五、不改动项确认（审计已实现，不在本 spec 范围）

- [x] N1. FBO-only 帧 PRESENT_SRC_KHR barrier 已实现（CommandStream.cpp:685-698, 791-798）。
- [x] N2. imageAvailableSemaphore consumed 标志已实现（Swapchain.h:99, CommandStream.cpp:846）。
- [x] N3. 零面积窗口 present 挂起已实现（egl.cpp:929-932）。
- [x] N4. frame fence 创建即 signaled 已实现（Device.cpp:727-732）。
- [x] N5. acquire/present SUBOPTIMAL 当成功码已实现（SwapchainCommon.cpp:344-346, 492）。
- [x] N6. default FBO finalLayout=PRESENT_SRC_KHR 已实现（CommandStream.cpp:788-798，含 BOTTOM_OF_PIPE 修正）。
- [x] N7. glClear 用 vkCmdClearAttachments 已实现（gl.cpp:41-60）。
- [x] N8. 内容有效性→DONT_CARE（color 路径）已实现（CommandStream.cpp:380-448）。
