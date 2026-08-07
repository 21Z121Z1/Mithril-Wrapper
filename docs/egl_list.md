# EGL 导出符号清单

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
