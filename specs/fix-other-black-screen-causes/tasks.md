# Tasks

## 阶段 0：基线确认

- [x] T0.1 确认当前在 main 分支，工作区干净（已修复根因 A-D 已提交）。

## 阶段 1：根因 E —— 显式设置 MVK_CONFIG_VK_SEMAPHORE_SUPPORT_STYLE（最高优先级）

- [x] T1.1 `MG_Backend/DirectVulkan/Device.cpp`：在 `setenv` 块（442 行后）增加 `setenv("MVK_CONFIG_VK_SEMAPHORE_SUPPORT_STYLE", "1", 1);`，注释说明强制 Metal 信号量实现真实 GPU 同步，防止 Rosetta2/NVIDIA/旧版 MoltenVK 退化为 no-op 导致黑屏。
- [x] T1.2 grep 确认无其他 MoltenVK 配置项遗漏。

## 阶段 2：根因 F —— 部分纹理上传保留既有内容

- [x] T2.1 `MG_Backend/DirectVulkan/Resources.cpp`：`stage_and_copy_image` 签名增加 `bool is_full_upload` 参数。
- [x] T2.2 `stage_and_copy_image` 中初始 barrier 的 `oldLayout` 改为 `is_full_upload ? VK_IMAGE_LAYOUT_UNDEFINED : tex.currentLayout`。
- [x] T2.3 `MG_Backend/Backend.h`：`backend_texture_upload` 声明增加 `int is_full_upload` 参数。
- [x] T2.4 `Resources.cpp` 中 `backend_texture_upload` 定义同步增加参数，透传到 `stage_and_copy_image`。
- [x] T2.5 `MG_Impl/Texture.cpp`：`glTexImage2D`/`glTexImage3D` 调用 `backend_texture_upload` 传 `is_full_upload=1`。
- [x] T2.6 `MG_Impl/Texture.cpp`：`glTexSubImage2D`/`glTexSubImage3D` 调用 `backend_texture_upload` 传 `is_full_upload=0`。
- [x] T2.7 grep 确认无其他 `backend_texture_upload` 调用点遗漏新参数（仅 4 处调用 + 声明 + 定义，已全部更新）。

## 阶段 3：根因 G —— RGB swapchain clear alpha 强制 1.0

- [x] T3.1 `MG_Backend/DirectVulkan/CommandStream.cpp`：增加 helper 函数 `format_has_alpha(VkFormat)`，检查格式是否有 alpha 通道。
- [x] T3.2 在 clear 路径（`clear_attachments` 和 `begin_render_pass` 设置 clearValue 时），若 swapchain 格式无 alpha，强制 `clearValue.color.float32[3] = 1.0f`。

## 阶段 4：编译验证

- [x] T4.1 完整构建工程，确认无新增 warning/error（Texture.cpp、CommandStream.cpp 完全干净；Resources.cpp 仅 pre-existing unused 警告；Device.cpp 错误为 pre-existing PORTABILITY_SUBSET 宏缺失，与本次修改无关）。

## 阶段 5：提交

- [ ] T5.1 用 git-commit skill 提交本次修改（conventional commit）。

# Task Dependencies

- T2.1-T2.4 是根因 F 的后端改动，T2.5-T2.6 依赖 T2.4（声明变更）。
- T1.1、T2.*、T3.* 之间无依赖，可并行。
- T4.1 依赖 T1.1、T2.*、T3.* 全部完成。
- T5.1 依赖 T4.1。
