from pathlib import Path


def one(path, old, new):
    p = Path(path)
    s = p.read_text()
    n = s.count(old)
    if n != 1:
        raise SystemExit(f"{path}: expected one match, got {n}: {old[:120]!r}")
    p.write_text(s.replace(old, new, 1))


# Platform surface lifecycle: only remove CAMetalLayer children created by Mithril.
p = Path("Mithril-Wrapper-cpp/egl/SurfaceMetal.mm")
s = p.read_text()
s += r'''

// ---------------------------------------------------------------------------
// surface_destroy: release platform state owned by Mithril without touching a
// host-provided CAMetalLayer.  Repeated EGLSurface recreation (rotation,
// background/foreground, device-loss recovery) must not leave stale Metal
// children stacked under the host view's CALayer.
// ---------------------------------------------------------------------------
extern "C" void surface_destroy(void* native_window) {
    if (!native_window) return;
    CALayer* layer = (__bridge CALayer*)native_window;
    if (![layer isKindOfClass:[CAMetalLayer class]]) return;
    CAMetalLayer* mtlLayer = (CAMetalLayer*)layer;
    if ([mtlLayer.name isEqualToString:@"Mithril-Wrapper-owned-CAMetalLayer"] &&
        mtlLayer.superlayer) {
        [mtlLayer removeFromSuperlayer];
    }
}
'''
p.write_text(s)

p = Path("Mithril-Wrapper-cpp/egl/SurfaceHeadless.cpp")
s = p.read_text()
if 'surface_destroy' not in s:
    s += '\nextern "C" void surface_destroy(void* native_window) { (void)native_window; }\n'
p.write_text(s)

# EGL core: use the same Vulkan submission serials already used by GLsync.
one(
    "Mithril-Wrapper-cpp/egl/egl.cpp",
    'extern "C" bool  surface_get_size(void* native_window, int* out_w, int* out_h);\n',
    'extern "C" bool  surface_get_size(void* native_window, int* out_w, int* out_h);\n'
    'extern "C" void  surface_destroy(void* native_window);\n',
)
one(
    "Mithril-Wrapper-cpp/egl/egl.cpp",
    '''// EGL 1.5 sync object (shadow implementation: always signaled, no real GPU\n// fence). Backed by a process-local handle so eglClientWaitSync/eglWaitSync\n// can validate the handle without touching the Vulkan backend.\nstruct EglSync {\n    EGLDisplay dpy       = EGL_NO_DISPLAY;\n    EGLenum    type      = 0;\n    EGLenum    condition = 0;\n    EGLenum    status    = EGL_SIGNALED;\n};\n''',
    '''// EGL 1.5 fence sync. Each object records the DirectVulkan queue-submit\n// serial containing all client commands that preceded eglCreateSync. A zero\n// serial is the canonical "already satisfied" state.\nstruct EglSync {\n    EGLDisplay dpy       = EGL_NO_DISPLAY;\n    EGLenum    type      = 0;\n    EGLenum    condition = 0;\n    EGLenum    status    = EGL_UNSIGNALED;\n    uint64_t   submitSerial = 0;\n};\n''',
)
one(
    "Mithril-Wrapper-cpp/egl/egl.cpp",
    '''    if (s->swapchain_state) {\n        backend_destroy_swapchain(s->swapchain_state);\n        s->swapchain_state = nullptr;\n    }\n    s->native_window = nullptr;\n''',
    '''    if (s->swapchain_state) {\n        backend_destroy_swapchain(s->swapchain_state);\n        s->swapchain_state = nullptr;\n    }\n    // surface_create() may have installed a Mithril-owned CAMetalLayer child.\n    // Remove only that child; host-provided CAMetalLayer objects remain owned\n    // and managed by the application.\n    if (s->native_window) surface_destroy(s->native_window);\n    s->native_window = nullptr;\n''',
)
one(
    "Mithril-Wrapper-cpp/egl/egl.cpp",
    '''// ---- Idle sync (no-ops; Mithril flushes work synchronously per draw) ----\nEGLBoolean eglWaitClient(void)  { backend_end_render_pass(); backend_commit(); return EGL_TRUE; }\nEGLBoolean eglWaitGL(void)      { backend_end_render_pass(); backend_commit(); return EGL_TRUE; }\n''',
    '''// ---- Client completion waits -------------------------------------------\n// backend_commit() is flush semantics. EGL wait-client / wait-GL calls promise\n// completion of prior client API work before returning, so also retire the GPU\n// queue/fences through the backend's safe idle path.\nEGLBoolean eglWaitClient(void) {\n    backend_end_render_pass();\n    backend_commit();\n    mithril::vk::safe_device_wait_idle();\n    return EGL_TRUE;\n}\nEGLBoolean eglWaitGL(void) {\n    backend_end_render_pass();\n    backend_commit();\n    mithril::vk::safe_device_wait_idle();\n    return EGL_TRUE;\n}\n''',
)

p = Path("Mithril-Wrapper-cpp/egl/egl.cpp")
s = p.read_text()
start = s.index("// ---- EGL 1.5 Sync (shadow implementation) ----")
end = s.index("\n// ---- EGL 1.5 Image", start)
real_sync = r'''// ---- EGL 1.5 Sync: backed by DirectVulkan submit serials -----------------
EGLSync eglCreateSync(EGLDisplay dpy, EGLenum type, const EGLAttrib* attrib_list) {
    clear_error();
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_NO_SYNC; }
    if (type != EGL_SYNC_FENCE) { set_error(EGL_BAD_ATTRIBUTE); return EGL_NO_SYNC; }
    if (!t_currentCtx) { set_error(EGL_BAD_MATCH); return EGL_NO_SYNC; }
    if (attrib_list && attrib_list[0] != EGL_NONE) {
        set_error(EGL_BAD_ATTRIBUTE);
        return EGL_NO_SYNC;
    }

    // Insert the fence after all prior client commands. Eagerly flushing here
    // is legal and gives the software EGL fence an exact Vulkan submit serial;
    // the wait operations below still honour their timeout semantics.
    backend_end_render_pass();
    backend_commit();

    EglSync sync{};
    sync.dpy = dpy;
    sync.type = EGL_SYNC_FENCE;
    sync.condition = EGL_SYNC_PRIOR_COMMANDS_COMPLETE;
    sync.submitSerial = mithril::vk::backend_current_submit_serial();
    if (sync.submitSerial == 0 ||
        sync.submitSerial <= mithril::vk::backend_last_completed_serial()) {
        sync.status = EGL_SIGNALED;
        sync.submitSerial = 0;
    } else {
        sync.status = EGL_UNSIGNALED;
    }

    EGLSync handle = reinterpret_cast<EGLSync>(g_nextSyncHandle++);
    g_syncs[handle] = sync;
    return handle;
}

EGLBoolean eglDestroySync(EGLDisplay dpy, EGLSync sync) {
    clear_error();
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_FALSE; }
    auto it = g_syncs.find(sync);
    if (it == g_syncs.end() || it->second.dpy != dpy) {
        set_error(EGL_BAD_SYNC_KHR);
        return EGL_FALSE;
    }
    g_syncs.erase(it);
    return EGL_TRUE;
}

EGLint eglClientWaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags, EGLTime timeout) {
    clear_error();
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_FALSE; }
    if (flags & ~EGL_SYNC_FLUSH_COMMANDS_BIT) {
        set_error(EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }
    auto it = g_syncs.find(sync);
    if (it == g_syncs.end() || it->second.dpy != dpy) {
        set_error(EGL_BAD_SYNC_KHR);
        return EGL_FALSE;
    }
    EglSync& s = it->second;
    if (s.status == EGL_SIGNALED || s.submitSerial == 0 ||
        s.submitSerial <= mithril::vk::backend_last_completed_serial()) {
        s.status = EGL_SIGNALED;
        s.submitSerial = 0;
        return EGL_CONDITION_SATISFIED;
    }

    if (flags & EGL_SYNC_FLUSH_COMMANDS_BIT) {
        backend_end_render_pass();
        backend_commit();
    }
    if (mithril::vk::backend_wait_serial(s.submitSerial, (uint64_t)timeout)) {
        s.status = EGL_SIGNALED;
        s.submitSerial = 0;
        return EGL_CONDITION_SATISFIED;
    }
    return EGL_TIMEOUT_EXPIRED;
}

EGLBoolean eglWaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags) {
    clear_error();
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_FALSE; }
    if (flags != 0) { set_error(EGL_BAD_PARAMETER); return EGL_FALSE; }
    auto it = g_syncs.find(sync);
    if (it == g_syncs.end() || it->second.dpy != dpy) {
        set_error(EGL_BAD_SYNC_KHR);
        return EGL_FALSE;
    }
    EglSync& s = it->second;
    // Mithril uses one Vulkan graphics queue. A host wait is conservative but
    // preserves EGL server-wait ordering across context/thread hand-offs until
    // a native semaphore-backed cross-context path is introduced.
    if (s.status == EGL_SIGNALED || s.submitSerial == 0 ||
        mithril::vk::backend_wait_serial(s.submitSerial, UINT64_MAX)) {
        s.status = EGL_SIGNALED;
        s.submitSerial = 0;
        return EGL_TRUE;
    }
    return EGL_FALSE;
}

EGLBoolean eglGetSyncAttrib(EGLDisplay dpy, EGLSync sync, EGLint attribute, EGLAttrib* value) {
    clear_error();
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_FALSE; }
    auto it = g_syncs.find(sync);
    if (it == g_syncs.end() || it->second.dpy != dpy) {
        set_error(EGL_BAD_SYNC_KHR);
        return EGL_FALSE;
    }
    if (!value) { set_error(EGL_BAD_PARAMETER); return EGL_FALSE; }
    EglSync& s = it->second;
    if (s.status != EGL_SIGNALED &&
        (s.submitSerial == 0 ||
         s.submitSerial <= mithril::vk::backend_last_completed_serial())) {
        s.status = EGL_SIGNALED;
        s.submitSerial = 0;
    }
    switch (attribute) {
        case EGL_SYNC_TYPE:      *value = s.type;      break;
        case EGL_SYNC_STATUS:    *value = s.status;    break;
        case EGL_SYNC_CONDITION: *value = s.condition; break;
        default:                 set_error(EGL_BAD_ATTRIBUTE); return EGL_FALSE;
    }
    return EGL_TRUE;
}
'''
p.write_text(s[:start] + real_sync + s[end:])

# Extend the platform smoke to verify owned-child teardown and host-layer safety.
p = Path("tests/surface_metal_smoke.mm")
s = p.read_text()
s = s.replace(
    "using surface_get_size_fn = bool (*)(void*, int*, int*);\n",
    "using surface_get_size_fn = bool (*)(void*, int*, int*);\n"
    "using surface_destroy_fn = void (*)(void*);\n",
    1,
)
s = s.replace(
    '    auto surface_get_size = (surface_get_size_fn)dlsym(h, "surface_get_size");\n'
    '    CHECK(surface_create && surface_get_size, "surface Metal entry points exported");\n'
    '    if (!surface_create || !surface_get_size) return 1;\n',
    '    auto surface_get_size = (surface_get_size_fn)dlsym(h, "surface_get_size");\n'
    '    auto surface_destroy = (surface_destroy_fn)dlsym(h, "surface_destroy");\n'
    '    CHECK(surface_create && surface_get_size && surface_destroy, "surface Metal entry points exported");\n'
    '    if (!surface_create || !surface_get_size || !surface_destroy) return 1;\n',
    1,
)
needle = '''        CHECK(surface_get_size((__bridge void*)metal, &qw, &qh) && qw == 200 && qh == 100,
              "surface_get_size reports pixel drawable size (%dx%d)", qw, qh);

        CAMetalLayer* existing = [CAMetalLayer layer];
'''
repl = '''        CHECK(surface_get_size((__bridge void*)metal, &qw, &qh) && qw == 200 && qh == 100,
              "surface_get_size reports pixel drawable size (%dx%d)", qw, qh);
        surface_destroy((__bridge void*)metal);
        CHECK(metal.superlayer == nil, "owned fallback CAMetalLayer detaches on surface_destroy");

        CAMetalLayer* existing = [CAMetalLayer layer];
'''
if s.count(needle) != 1:
    raise SystemExit("surface smoke owned teardown marker mismatch")
s = s.replace(needle, repl, 1)
needle = '''        CHECK(ew == 123 && eh == 77, "existing nonzero drawableSize is preserved (%dx%d)", ew, eh);
'''
repl = needle + '''        surface_destroy((__bridge void*)existing);
        CHECK((__bridge CAMetalLayer*)same == existing,
              "surface_destroy never replaces or invalidates host CAMetalLayer");
'''
if s.count(needle) != 1:
    raise SystemExit("surface smoke host teardown marker mismatch")
p.write_text(s.replace(needle, repl, 1))

# Dedicated EGL fence-sync smoke: create a surfaceless/pbuffer context, record
# a real user-FBO clear, create an EGL fence, wait it, and verify monotonic state.
Path("tests/egl_sync_smoke.c").write_text(r'''#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <EGL/egl.h>
#include <GL/glcorearb.h>

static int failures = 0;
#define CHECK(c, fmt, ...) do { if (c) printf("ok : " fmt "\n", ##__VA_ARGS__); \
    else { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; } } while (0)
#define RESOLVE(var, sym) do { var = dlsym(h, sym); CHECK(var != NULL, "%s resolved", sym); } while (0)

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    void* h = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }

    EGLDisplay (*getDisplay)(EGLNativeDisplayType) = NULL;
    EGLBoolean (*initialize)(EGLDisplay,EGLint*,EGLint*) = NULL;
    EGLBoolean (*bindAPI)(EGLenum) = NULL;
    EGLBoolean (*getConfigs)(EGLDisplay,EGLConfig*,EGLint,EGLint*) = NULL;
    EGLContext (*createContext)(EGLDisplay,EGLConfig,EGLContext,const EGLint*) = NULL;
    EGLSurface (*createPbuffer)(EGLDisplay,EGLConfig,const EGLint*) = NULL;
    EGLBoolean (*makeCurrent)(EGLDisplay,EGLSurface,EGLSurface,EGLContext) = NULL;
    EGLSync (*createSync)(EGLDisplay,EGLenum,const EGLAttrib*) = NULL;
    EGLint (*clientWaitSync)(EGLDisplay,EGLSync,EGLint,EGLTime) = NULL;
    EGLBoolean (*getSyncAttrib)(EGLDisplay,EGLSync,EGLint,EGLAttrib*) = NULL;
    EGLBoolean (*destroySync)(EGLDisplay,EGLSync) = NULL;
    EGLBoolean (*waitClient)(void) = NULL;
    EGLBoolean (*destroySurface)(EGLDisplay,EGLSurface) = NULL;
    EGLBoolean (*destroyContext)(EGLDisplay,EGLContext) = NULL;

    void (*genTextures)(GLsizei,GLuint*) = NULL;
    void (*bindTexture)(GLenum,GLuint) = NULL;
    void (*texImage2D)(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const void*) = NULL;
    void (*genFramebuffers)(GLsizei,GLuint*) = NULL;
    void (*bindFramebuffer)(GLenum,GLuint) = NULL;
    void (*framebufferTexture2D)(GLenum,GLenum,GLenum,GLuint,GLint) = NULL;
    GLenum (*checkFramebufferStatus)(GLenum) = NULL;
    void (*clearColor)(GLfloat,GLfloat,GLfloat,GLfloat) = NULL;
    void (*clear)(GLbitfield) = NULL;

    RESOLVE(getDisplay, "eglGetDisplay"); RESOLVE(initialize, "eglInitialize");
    RESOLVE(bindAPI, "eglBindAPI"); RESOLVE(getConfigs, "eglGetConfigs");
    RESOLVE(createContext, "eglCreateContext"); RESOLVE(createPbuffer, "eglCreatePbufferSurface");
    RESOLVE(makeCurrent, "eglMakeCurrent"); RESOLVE(createSync, "eglCreateSync");
    RESOLVE(clientWaitSync, "eglClientWaitSync"); RESOLVE(getSyncAttrib, "eglGetSyncAttrib");
    RESOLVE(destroySync, "eglDestroySync"); RESOLVE(waitClient, "eglWaitClient");
    RESOLVE(destroySurface, "eglDestroySurface"); RESOLVE(destroyContext, "eglDestroyContext");
    RESOLVE(genTextures, "glGenTextures"); RESOLVE(bindTexture, "glBindTexture");
    RESOLVE(texImage2D, "glTexImage2D"); RESOLVE(genFramebuffers, "glGenFramebuffers");
    RESOLVE(bindFramebuffer, "glBindFramebuffer"); RESOLVE(framebufferTexture2D, "glFramebufferTexture2D");
    RESOLVE(checkFramebufferStatus, "glCheckFramebufferStatus"); RESOLVE(clearColor, "glClearColor");
    RESOLVE(clear, "glClear");
    if (failures) return 1;

    EGLDisplay dpy = getDisplay(EGL_DEFAULT_DISPLAY);
    EGLint maj=0,min=0;
    CHECK(dpy != EGL_NO_DISPLAY && initialize(dpy,&maj,&min), "EGL %d.%d initializes", maj, min);
    CHECK(bindAPI(EGL_OPENGL_API), "EGL OpenGL client API bound");
    EGLConfig cfg = NULL; EGLint ncfg = 0;
    CHECK(getConfigs(dpy,&cfg,1,&ncfg) && ncfg > 0 && cfg, "EGL config available");
    const EGLint ctxAttrs[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3, EGL_NONE };
    EGLContext ctx = createContext(dpy,cfg,EGL_NO_CONTEXT,ctxAttrs);
    CHECK(ctx != EGL_NO_CONTEXT, "EGL context created");
    const EGLint pbAttrs[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
    EGLSurface surf = createPbuffer(dpy,cfg,pbAttrs);
    CHECK(surf != EGL_NO_SURFACE, "EGL pbuffer placeholder created");
    CHECK(makeCurrent(dpy,surf,surf,ctx), "EGL context made current");

    GLuint tex=0,fbo=0;
    genTextures(1,&tex); bindTexture(GL_TEXTURE_2D,tex);
    texImage2D(GL_TEXTURE_2D,0,GL_RGBA8,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    genFramebuffers(1,&fbo); bindFramebuffer(GL_FRAMEBUFFER,fbo);
    framebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
    CHECK(checkFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE, "user FBO complete");
    clearColor(0.25f,0.5f,0.75f,1.0f); clear(GL_COLOR_BUFFER_BIT);

    EGLSync sync = createSync(dpy,EGL_SYNC_FENCE,NULL);
    CHECK(sync != EGL_NO_SYNC, "eglCreateSync inserted a fence after GPU clear");
    EGLAttrib status = EGL_UNSIGNALED;
    CHECK(getSyncAttrib(dpy,sync,EGL_SYNC_STATUS,&status), "eglGetSyncAttrib status query succeeds");
    EGLint wr = clientWaitSync(dpy,sync,EGL_SYNC_FLUSH_COMMANDS_BIT,1000000000ULL);
    CHECK(wr == EGL_CONDITION_SATISFIED, "eglClientWaitSync observes completion (0x%x)", wr);
    status = EGL_UNSIGNALED;
    CHECK(getSyncAttrib(dpy,sync,EGL_SYNC_STATUS,&status) && status == EGL_SIGNALED,
          "EGL fence status is monotonic SIGNALED after wait (0x%lx)", (unsigned long)status);
    CHECK(destroySync(dpy,sync), "EGL sync destroyed");

    // A second command followed by eglWaitClient must be fully complete when it returns.
    clearColor(0.1f,0.2f,0.3f,1.0f); clear(GL_COLOR_BUFFER_BIT);
    CHECK(waitClient(), "eglWaitClient completes prior client API work");
    EGLSync afterWait = createSync(dpy,EGL_SYNC_FENCE,NULL);
    status = EGL_UNSIGNALED;
    CHECK(afterWait != EGL_NO_SYNC && getSyncAttrib(dpy,afterWait,EGL_SYNC_STATUS,&status) &&
          status == EGL_SIGNALED,
          "fence inserted after eglWaitClient is already satisfied (0x%lx)", (unsigned long)status);
    if (afterWait != EGL_NO_SYNC) destroySync(dpy,afterWait);

    makeCurrent(dpy,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    destroySurface(dpy,surf); destroyContext(dpy,ctx);
    dlclose(h);
    printf("EGL SYNC SMOKE: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
''')
