# OpenGL 3.3 Core Profile 渲染器完整性 Spec

## Why
参考 MobileGL 架构，在 Vulkan 后端的 GL 渲染器上补全 OpenGL 3.3 Core Profile 的关键缺失功能，以解决黑屏问题。当前缺失 SPIR-V uniform 反射、实例化渲染支持、像素解包状态、动态管线状态、sampler 对象优先级、glClearBuffer 系列和 sampler 缓存失效，导致 shader 数据错误、纹理腐败和渲染异常。

## What Changes
- 在 glLinkProgram 中实现 SPIR-V uniform 反射，建立 UBO backing store
- VertexAttribDivisor 端到端支持（Backend.h + Drawing.cpp + Pipeline.cpp）
- 完整 GL_UNPACK_* 参数支持（MGUnpackParams 结构体）
- 消除 Pipeline.cpp 硬编码状态（polygonMode/lineWidth/depthClamp/stencil/depthBias 动态化）
- DescriptorSet 优先使用 sampler 对象参数
- 实现 glClearBufferfi/fv/iv/uiv
- glTexParameter* 失效 sampler 缓存

## Impact
- Affected code: Program.cpp, DescriptorSet.cpp, Backend.h, Pipeline.cpp, Drawing.cpp, Texture.cpp, Resources.cpp, gl.cpp, State.h

## ADDED Requirements
### Requirement: SPIR-V Uniform Reflection
The system SHALL reflect SPIR-V uniforms in glLinkProgram via reflect_stage + merge_bindings, populate Program::uniforms/uniformByLocation/uniformBlocks, and maintain a UBO backing store per binding. store_uniform* SHALL write raw bytes at reflected offsets. DescriptorSet SHALL pack UBO payload from the backing store.

### Requirement: VertexAttribDivisor
The system SHALL support per-instance vertex attributes via divisor field end-to-end.

### Requirement: Pixel Unpack State
The system SHALL support full GL_UNPACK_* parameters (alignment/rowLength/skipPixels/skipRows/imageHeight/skipImages) in texture upload.

### Requirement: Dynamic Pipeline State
The system SHALL read polygonMode/lineWidth/depthClamp/stencilTest/depthBias from g_state instead of hardcoding.

### Requirement: Sampler Object Priority
The system SHALL check g_state->samplerBindings first and use bound sampler object params over texture params.

### Requirement: glClearBuffer Series
The system SHALL implement glClearBufferfv/iv/uiv/fi with save/restore of clear state.

### Requirement: Sampler Cache Invalidation
The system SHALL invalidate cached VkSampler when glTexParameter* changes min/mag/wrap/border params.
