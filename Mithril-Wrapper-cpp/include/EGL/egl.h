#ifndef MITHRIL_EGL_EGL_H
#define MITHRIL_EGL_EGL_H

/*
 * Minimal EGL 1.5-compatible public header for Mithril-Wrapper.
 *
 * Mithril-Wrapper ships its own EGL implementation backed by Vulkan 1.2 via
 * MoltenVK (see ../egl/egl.mm). This header provides the EGL types, enum tokens and
 * PFNEGL*PROC function-pointer typedefs that consumers (Amethyst-iOS'
 * Natives/ctxbridges/gl_bridge.h, LWJGL's EGL probe, etc.) #include via
 * <EGL/egl.h>. Only the subset of EGL actually exercised by Amethyst's
 * egl_library struct is exposed, but it follows the Khronos layout so it
 * can also serve as a drop-in for code that does `#include <EGL/egl.h>`.
 *
 * The implementation lives in this very dylib: every `egl*` symbol is
 * exported as extern "C" and resolvable via dlsym(RTLD_DEFAULT, ...).
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Core EGL types ---- */
typedef int32_t  EGLint;
typedef uint32_t EGLBoolean;
typedef uint32_t EGLenum;
typedef void*    EGLDisplay;
typedef void*    EGLConfig;
typedef void*    EGLContext;
typedef void*    EGLSurface;
typedef void*    EGLClientBuffer;
typedef uint64_t EGLTime;
typedef int64_t  EGLint64NV;
typedef uint64_t EGLuint64KHR;

/* ---- EGL 1.5 sync / image / attrib types ---- */
typedef void*    EGLSync;
typedef void*    EGLImage;
typedef intptr_t EGLAttrib;

/*
 * Native window/display types. On iOS there is no X/Wayland/Win32 native
 * display; EGL_DEFAULT_DISPLAY (0) resolves to the singleton Metal-backed
 * display. EGLNativeWindowType is the host CALayer pointer (CAMetalLayer*)
 * passed in by SurfaceViewController.
 */
typedef void*  EGLNativeDisplayType;
typedef void*  EGLNativeWindowType;
typedef void*  EGLNativePixmapType;

typedef EGLNativeDisplayType NativeDisplayType;
typedef EGLNativePixmapType  NativePixmapType;
typedef EGLNativeWindowType  NativeWindowType;

/* ---- Sentinel handles ---- */
#define EGL_DEFAULT_DISPLAY  ((EGLNativeDisplayType)0)
#define EGL_NO_DISPLAY       ((EGLDisplay)0)
#define EGL_NO_CONTEXT       ((EGLContext)0)
#define EGL_NO_SURFACE       ((EGLSurface)0)
#define EGL_NO_TEXTURE       ((EGLint)0)
#define EGL_FALSE            ((EGLBoolean)0)
#define EGL_TRUE             ((EGLBoolean)1)
#define EGL_DONT_CARE        ((EGLint)(-1))

/* ---- Errors ---- */
#define EGL_SUCCESS                  0x3000
#define EGL_NOT_INITIALIZED          0x3001
#define EGL_BAD_ACCESS               0x3002
#define EGL_BAD_ALLOC                0x3003
#define EGL_BAD_ATTRIBUTE            0x3004
#define EGL_BAD_CONFIG               0x3005
#define EGL_BAD_CONTEXT              0x3006
#define EGL_BAD_CURRENT_SURFACE      0x3007
#define EGL_BAD_DISPLAY              0x3008
#define EGL_BAD_MATCH                0x3009
#define EGL_BAD_NATIVE_PIXMAP        0x300A
#define EGL_BAD_NATIVE_WINDOW        0x300B
#define EGL_BAD_PARAMETER            0x300C
#define EGL_BAD_SURFACE              0x300D
#define EGL_CONTEXT_LOST             0x300E
#define EGL_BAD_SYNC_KHR             0x307F

/* ---- Config attributes ---- */
#define EGL_BUFFER_SIZE              0x3080
#define EGL_ALPHA_SIZE               0x3021
#define EGL_BLUE_SIZE                0x3022
#define EGL_GREEN_SIZE               0x3023
#define EGL_RED_SIZE                 0x3024
#define EGL_DEPTH_SIZE               0x3025
#define EGL_STENCIL_SIZE             0x3026
#define EGL_CONFIG_CAVEAT            0x3051
#define EGL_CONFIG_ID                0x3028
#define EGL_LEVEL                    0x3029
#define EGL_MAX_PBUFFER_HEIGHT       0x3030
#define EGL_MAX_PBUFFER_PIXELS       0x302E
#define EGL_MAX_PBUFFER_WIDTH        0x302C
#define EGL_NATIVE_RENDERABLE        0x302B
#define EGL_NATIVE_VISUAL_ID         0x3030
#define EGL_NATIVE_VISUAL_TYPE       0x3031
#define EGL_SAMPLES                  0x3031
#define EGL_SAMPLE_BUFFERS           0x3032
#define EGL_SURFACE_TYPE             0x3033
#define EGL_TRANSPARENT_TYPE         0x3034
#define EGL_TRANSPARENT_BLUE_VALUE   0x3035
#define EGL_TRANSPARENT_GREEN_VALUE  0x3036
#define EGL_TRANSPARENT_RED_VALUE    0x3037
#define EGL_NONE                     0x3038
#define EGL_BIND_TO_TEXTURE_RGB      0x3039
#define EGL_BIND_TO_TEXTURE_RGBA     0x303A
#define EGL_MIN_SWAP_INTERVAL        0x303B
#define EGL_MAX_SWAP_INTERVAL        0x303C
#define EGL_LUMINANCE_SIZE           0x303D
#define EGL_ALPHA_MASK_SIZE          0x303E
#define EGL_COLOR_BUFFER_TYPE        0x303F
#define EGL_RENDERABLE_TYPE          0x3040
#define EGL_CONFORMANT               0x3042
#define EGL_MATCH_NATIVE_PIXMAP      0x3041

#define EGL_SLOW_CONFIG              0x3050
#define EGL_NON_CONFORMANT_CONFIG    0x3051
#define EGL_TRANSPARENT_RGB          0x3052
#define EGL_RGB_BUFFER               0x308E
#define EGL_LUMINANCE_BUFFER         0x308F

/* ---- Surface types ---- */
#define EGL_PBUFFER_BIT              0x0001
#define EGL_PIXMAP_BIT               0x0002
#define EGL_WINDOW_BIT               0x0004
#define EGL_MULTISAMPLE_RESOLVE_BOX_BIT 0x0200
#define EGL_SWAP_BEHAVIOR_PRESERVED_BIT 0x0400
#define EGL_VG_COLORSPACE_LINEAR_BIT    0x0080
#define EGL_VG_ALPHA_FORMAT_PRE_BIT     0x0040

/* ---- Renderable client API bits ---- */
#define EGL_OPENGL_ES_BIT            0x0001
#define EGL_OPENVG_BIT               0x0002
#define EGL_OPENGL_ES2_BIT           0x0004
#define EGL_OPENGL_BIT               0x0008
#define EGL_OPENGL_ES3_BIT           0x0040

/* ---- Client APIs ---- */
#define EGL_OPENGL_ES_API            0x30A0
#define EGL_OPENVG_API               0x30A1
#define EGL_OPENGL_API               0x30A2

/* ---- Context attributes ---- */
#define EGL_CONTEXT_CLIENT_TYPE      0x3097
#define EGL_CONTEXT_CLIENT_VERSION   0x3098
#define EGL_CONTEXT_MAJOR_VERSION    0x3098
#define EGL_CONTEXT_MINOR_VERSION    0x30FB
#define EGL_CONTEXT_FLAGS_KHR        0x30FC
#define EGL_CONTEXT_OPENGL_PROFILE_MASK 0x30FD
#define EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT 0x00000001
#define EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT 0x00000002
#define EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR             0x0001
#define EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR 0x0002
#define EGL_CONTEXT_OPENGL_ROBUST_ACCESS_BIT_KHR     0x0004

/* ---- QueryString targets ---- */
#define EGL_VENDOR                   0x3053
#define EGL_VERSION                  0x3054
#define EGL_EXTENSIONS               0x3055
#define EGL_CLIENT_APIS              0x308D

/* ---- Platform base tokens (EGL_EXT_platform_base / EGL 1.5) ---- */
#define EGL_PLATFORM_ANGLE_ANGLE     0x3202
#define EGL_PLATFORM_ANDROID_KHR     0x3141
#define EGL_PLATFORM_DEVICE_EXT      0x313F
#define EGL_PLATFORM_WAYLAND_KHR     0x31D8
#define EGL_PLATFORM_X11_KHR         0x31D5
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#define EGL_PLATFORM_XCB_EXT          0x31DC

/* ---- Surface attributes ---- */
#define EGL_HEIGHT                   0x3056
#define EGL_WIDTH                    0x3057
#define EGL_LARGEST_PBUFFER          0x3058
#define EGL_TEXTURE_FORMAT           0x3080
#define EGL_TEXTURE_TARGET           0x3081
#define EGL_MIPMAP_TEXTURE           0x3082
#define EGL_MIPMAP_LEVEL             0x3083

/* ---- Swap behaviour ---- */
#define EGL_BUFFER_PRESERVED         0x3094
#define EGL_BUFFER_DESTROYED         0x3095
#define EGL_RENDER_BUFFER            0x3086
#define EGL_BACK_BUFFER              0x3084
#define EGL_SINGLE_BUFFER            0x3085
#define EGL_SWAP_BEHAVIOR            0x3093
#define EGL_VG_ALPHA_FORMAT          0x3088
#define EGL_VG_COLORSPACE            0x3087

/* ---- Multi-sample resolve ---- */
#define EGL_MULTISAMPLE_RESOLVE      0x3099
#define EGL_MULTISAMPLE_RESOLVE_DEFAULT 0x309A
#define EGL_MULTISAMPLE_RESOLVE_BOX  0x309B

/* ---- Current surface targets ---- */
#define EGL_DRAW                     0x3059
#define EGL_READ                     0x305A

/* ---- Core 1.5 sync tokens ---- */
#define EGL_SYNC_FENCE               0x30B9
#define EGL_SYNC_PRIOR_COMMANDS_COMPLETE 0x30F0
#define EGL_SYNC_STATUS              0x30F1
#define EGL_SIGNALED                 0x30F2
#define EGL_UNSIGNALED               0x30F3
#define EGL_TIMEOUT_EXPIRED          0x30F5
#define EGL_CONDITION_SATISFIED      0x30F6
#define EGL_SYNC_NATIVE_FENCE_ANDROID 0x3144
#define EGL_FOREVER_KHR              0xFFFFFFFFFFFFFFFFULL

/* EGL 1.5 Sync API */
#define EGL_NO_SYNC                  ((EGLSync)0)
#define EGL_SYNC_TYPE                0x30F7
#define EGL_SYNC_CONDITION           0x30F8
#define EGL_SYNC_REUSABLE_KHR        0x30FA
#define EGL_SYNC_FLUSH_COMMANDS_BIT  0x0001

/* EGL 1.5 Image API */
#define EGL_NO_IMAGE                 ((EGLImage)0)
#define EGL_GL_TEXTURE_2D            0x30B1
#define EGL_GL_TEXTURE_3D            0x30B2
#define EGL_GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x30B3
#define EGL_GL_RENDERBUFFER          0x30B9
#define EGL_IMAGE_PRESERVED          0x30D2
#define EGL_OPENVG_IMAGE             0x3096

/* ===========================================================================
 * Function-pointer typedefs. These match the Khronos EGL 1.5 signatures and
 * are what Amethyst's `egl_library` struct stores. The implementations are
 * defined in ../../egl/egl.cpp and exported as plain `egl*` C symbols, so a
 * consumer can either take their address (&eglCreateContext) or dlsym them.
 * =========================================================================== */
typedef EGLBoolean  (*PFNEGLBINDAPIPROC)              (EGLenum api);
typedef EGLBoolean  (*PFNEGLCHOOSECONFIGPROC)         (EGLDisplay dpy, const EGLint *attrib_list, EGLConfig *configs, EGLint config_size, EGLint *num_config);
typedef EGLContext  (*PFNEGLCREATECONTEXTPROC)        (EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint *attrib_list);
typedef EGLSurface  (*PFNEGLCREATEWINDOWSURFACEPROC)  (EGLDisplay dpy, EGLConfig config, EGLNativeWindowType win, const EGLint *attrib_list);
typedef EGLBoolean  (*PFNEGLDESTROYCONTEXTPROC)       (EGLDisplay dpy, EGLContext ctx);
typedef EGLBoolean  (*PFNEGLDESTROYSURFACEPROC)       (EGLDisplay dpy, EGLSurface surface);
typedef EGLBoolean  (*PFNEGLGETCONFIGATTRIBPROC)      (EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint *value);
typedef EGLBoolean  (*PFNEGLGETCONFIGSPROC)           (EGLDisplay dpy, EGLConfig *configs, EGLint config_size, EGLint *num_config);
typedef EGLContext  (*PFNEGLGETCURRENTCONTEXTPROC)    (void);
typedef EGLSurface  (*PFNEGLGETCURRENTSURFACEPROC)    (EGLenum readdraw);
typedef EGLDisplay  (*PFNEGLGETDISPLAYPROC)           (EGLNativeDisplayType display_id);
typedef EGLint      (*PFNEGLGETERRORPROC)             (void);
typedef EGLDisplay  (*PFNEGLGETPLATFORMDISPLAYPROC)   (EGLenum platform, void *native_display, const EGLint *attrib_list);
typedef EGLBoolean  (*PFNEGLINITIALIZEPROC)           (EGLDisplay dpy, EGLint *major, EGLint *minor);
typedef EGLBoolean  (*PFNEGLMAKECURRENTPROC)          (EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx);
typedef const char* (*PFNEGLQUERYSTRINGPROC)          (EGLDisplay dpy, EGLint name);
typedef EGLBoolean  (*PFNEGLRELEASETHREADPROC)        (void);
typedef EGLBoolean  (*PFNEGLSWAPBUFFERSPROC)          (EGLDisplay dpy, EGLSurface surface);
typedef EGLBoolean  (*PFNEGLSWAPINTERVALPROC)         (EGLDisplay dpy, EGLint interval);
typedef EGLBoolean  (*PFNEGLTERMINATEPROC)            (EGLDisplay dpy);

/* Extra helpers commonly expected by EGL 1.5 consumers. */
typedef EGLSurface  (*PFNEGLCREATEPBUFFERSURFACEPROC) (EGLDisplay dpy, EGLConfig config, const EGLint *attrib_list);
typedef EGLBoolean  (*PFNEGLQUERYCONTEXTPROC)         (EGLDisplay dpy, EGLContext ctx, EGLint attribute, EGLint *value);
typedef EGLBoolean  (*PFNEGLQUERYSURFACEPROC)         (EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint *value);
typedef EGLBoolean  (*PFNEGLWAITCLIENTPROC)           (void);
typedef EGLBoolean  (*PFNEGLWAITGLPROC)               (void);
typedef EGLBoolean  (*PFNEGLWAITNATIVEPROC)           (EGLint engine);
typedef EGLDisplay  (*PFNEGLGETCURRENTDISPLAYPROC)    (void);

/* EGL 1.5 Sync / Image / Platform Surface / helper typedefs. */
typedef void*       (*PFNEGLCREATESYNCPROC)            (EGLDisplay dpy, EGLenum type, const EGLAttrib *attrib_list);
typedef EGLBoolean  (*PFNEGLDESTROYSYNCPROC)           (EGLDisplay dpy, EGLSync sync);
typedef EGLint      (*PFNEGLCLIENTWAITSYNCPROC)        (EGLDisplay dpy, EGLSync sync, EGLint flags, EGLTime timeout);
typedef EGLBoolean  (*PFNEGLWAITSYNCPROC)              (EGLDisplay dpy, EGLSync sync, EGLint flags);
typedef EGLBoolean  (*PFNEGLGETSYNCATTRIBPROC)         (EGLDisplay dpy, EGLSync sync, EGLint attribute, EGLAttrib *value);
typedef void*       (*PFNEGLCREATEIMAGEPROC)           (EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer, const EGLAttrib *attrib_list);
typedef EGLBoolean  (*PFNEGLDESTROYIMAGEPROC)          (EGLDisplay dpy, EGLImage image);
typedef EGLSurface  (*PFNEGLCREATEPLATFORMWINDOWSURFACEPROC) (EGLDisplay dpy, EGLConfig config, void *native_window, const EGLAttrib *attrib_list);
typedef EGLSurface  (*PFNEGLCREATEPLATFORMPIXMAPSURFACEPROC) (EGLDisplay dpy, EGLConfig config, void *native_pixmap, const EGLAttrib *attrib_list);
typedef EGLSurface  (*PFNEGLCREATEPIXMAPSURFACEPROC)   (EGLDisplay dpy, EGLConfig config, EGLNativePixmapType pixmap, const EGLint *attrib_list);
typedef EGLSurface  (*PFNEGLCREATEPBUFFERFROMCLIENTBUFFERPROC) (EGLDisplay dpy, EGLenum buftype, EGLClientBuffer buffer, EGLConfig config, const EGLint *attrib_list);
typedef void        (*(*PFNEGLGETPROCADDRESSPROC)      (const char *procname))(void);
typedef EGLBoolean  (*PFNEGLSURFACEATTRIBPROC)         (EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint value);
typedef EGLBoolean  (*PFNEGLBINDTEXIMAGEPROC)          (EGLDisplay dpy, EGLSurface surface, EGLint buffer);
typedef EGLBoolean  (*PFNEGLRELEASETEXIMAGEPROC)       (EGLDisplay dpy, EGLSurface surface, EGLint buffer);
typedef EGLBoolean  (*PFNEGLCOPYBUFFERSPROC)           (EGLDisplay dpy, EGLSurface surface, EGLNativePixmapType target);
typedef EGLenum     (*PFNEGLQUERYAPIPROC)              (void);

/* ===========================================================================
 * Public EGL entry points (exported by this dylib). Consumers may either
 * call them directly or obtain their addresses via dlsym/dlsym(RTLD_DEFAULT).
 * =========================================================================== */
EGLint      eglGetError(void);
EGLDisplay  eglGetDisplay(EGLNativeDisplayType display_id);
EGLDisplay  eglGetPlatformDisplay(EGLenum platform, void *native_display, const EGLint *attrib_list);
EGLBoolean  eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor);
EGLBoolean  eglTerminate(EGLDisplay dpy);
const char* eglQueryString(EGLDisplay dpy, EGLint name);
EGLBoolean  eglBindAPI(EGLenum api);
EGLBoolean  eglReleaseThread(void);

EGLBoolean  eglGetConfigs(EGLDisplay dpy, EGLConfig *configs, EGLint config_size, EGLint *num_config);
EGLBoolean  eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list, EGLConfig *configs, EGLint config_size, EGLint *num_config);
EGLBoolean  eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint *value);

EGLSurface  eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config, EGLNativeWindowType win, const EGLint *attrib_list);
EGLSurface  eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config, const EGLint *attrib_list);
EGLBoolean  eglDestroySurface(EGLDisplay dpy, EGLSurface surface);
EGLBoolean  eglQuerySurface(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint *value);

EGLContext  eglCreateContext(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint *attrib_list);
EGLBoolean  eglDestroyContext(EGLDisplay dpy, EGLContext ctx);
EGLBoolean  eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx);
EGLContext  eglGetCurrentContext(void);
EGLSurface  eglGetCurrentSurface(EGLenum readdraw);
EGLDisplay  eglGetCurrentDisplay(void);
EGLBoolean  eglQueryContext(EGLDisplay dpy, EGLContext ctx, EGLint attribute, EGLint *value);

EGLBoolean  eglSwapBuffers(EGLDisplay dpy, EGLSurface surface);
EGLBoolean  eglSwapInterval(EGLDisplay dpy, EGLint interval);
EGLBoolean  eglWaitClient(void);
EGLBoolean  eglWaitGL(void);
EGLBoolean  eglWaitNative(EGLint engine);

/* EGL 1.5 Sync API */
EGLSync     eglCreateSync(EGLDisplay dpy, EGLenum type, const EGLAttrib *attrib_list);
EGLBoolean  eglDestroySync(EGLDisplay dpy, EGLSync sync);
EGLint      eglClientWaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags, EGLTime timeout);
EGLBoolean  eglWaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags);
EGLBoolean  eglGetSyncAttrib(EGLDisplay dpy, EGLSync sync, EGLint attribute, EGLAttrib *value);

/* EGL 1.5 Image API */
EGLImage    eglCreateImage(EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer, const EGLAttrib *attrib_list);
EGLBoolean  eglDestroyImage(EGLDisplay dpy, EGLImage image);

/* EGL 1.5 Platform Surface API */
EGLSurface  eglCreatePlatformWindowSurface(EGLDisplay dpy, EGLConfig config, void *native_window, const EGLAttrib *attrib_list);
EGLSurface  eglCreatePlatformPixmapSurface(EGLDisplay dpy, EGLConfig config, void *native_pixmap, const EGLAttrib *attrib_list);
EGLSurface  eglCreatePixmapSurface(EGLDisplay dpy, EGLConfig config, EGLNativePixmapType pixmap, const EGLint *attrib_list);
EGLSurface  eglCreatePbufferFromClientBuffer(EGLDisplay dpy, EGLenum buftype, EGLClientBuffer buffer, EGLConfig config, const EGLint *attrib_list);

/* EGL 1.5 / extension helpers already implemented but previously undeclared. */
void        (*eglGetProcAddress(const char *procname))(void);
EGLBoolean  eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint value);
EGLBoolean  eglBindTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer);
EGLBoolean  eglReleaseTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer);
EGLBoolean  eglCopyBuffers(EGLDisplay dpy, EGLSurface surface, EGLNativePixmapType target);
EGLenum     eglQueryAPI(void); /* Non-standard extension, retained for Amethyst-iOS compatibility */

#ifdef __cplusplus
}
#endif

#endif /* MITHRIL_EGL_EGL_H */
