# EGL 导出符号清单

当前 Amethyst 的 Mithril integration 会先请求 `EGL_OPENGL_ES3_BIT` config 并绑定
`EGL_OPENGL_ES_API`，随后由 LWJGL 直接 dlsym Mithril 的 desktop OpenGL 3.3 Core
exports。Mithril 将前者作为显式 host negotiation alias，并在 EGL query/context
state 中如实保留绑定值；这不表示 DirectMetal 对外宣称独立的 OpenGL ES profile。

## 必须导出（Amethyst gl_bridge.m 直接 dlsym 的 18 个）
```
eglBindAPI  eglChooseConfig  eglCreateContext  eglCreateWindowSurface
eglDestroyContext  eglDestroySurface  eglGetConfigAttrib  eglGetCurrentContext
eglGetCurrentSurface  eglGetDisplay  eglGetError  eglGetPlatformDisplay
eglInitialize  eglMakeCurrent  eglReleaseThread  eglSwapBuffers  eglSwapInterval  eglTerminate
```

## 建议完整导出（EGL 1.5 全部，兼容 SDL3/GLFW/LWJGL 查询；desktopglues 导出为基准）
```
eglBindAPI  eglBindTexImage  eglChooseConfig  eglClientWaitSync  eglCopyBuffers
eglCreateContext  eglCreateImage  eglCreatePbufferFromClientBuffer  eglCreatePbufferSurface
eglCreatePixmapSurface  eglCreatePlatformPixmapSurface  eglCreatePlatformPixmapSurfaceEXT
eglCreatePlatformWindowSurface  eglCreatePlatformWindowSurfaceEXT  eglCreateSync
eglCreateWindowSurface  eglDestroyContext  eglDestroyImage  eglDestroySurface  eglDestroySync
eglGetConfigAttrib  eglGetConfigs  eglGetCurrentContext  eglGetCurrentDisplay  eglGetCurrentSurface
eglGetDisplay  eglGetError  eglGetPlatformDisplay  eglGetPlatformDisplayEXT  eglGetProcAddress
eglGetSyncAttrib  eglInitialize  eglMakeCurrent  eglQueryAPI  eglQueryContext
eglQueryString  eglQuerySurface  eglReleaseTexImage  eglReleaseThread  eglSurfaceAttrib
eglSwapBuffers  eglSwapInterval  eglTerminate  eglWaitClient  eglWaitGL  eglWaitNative  eglWaitSync
```
（47 个 = 44 个 EGL 1.5 标准 + 3 个 EXT 别名）

## GLX 兜底（LWJGL 符号解析回退）
```
glXGetProcAddress  glXGetProcAddressARB
```
