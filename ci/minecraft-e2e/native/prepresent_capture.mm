#include <GL/gl.h>

#include <dlfcn.h>
#include <cerrno>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
std::string env_string(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

void append_event(const std::string& root, const char* event, int frame, const char* detail) {
    if (root.empty()) return;
    FILE* f = std::fopen((root + "/native-events.jsonl").c_str(), "a");
    if (!f) return;
    std::fprintf(f,
        "{\"schema_version\":\"1.0\",\"producer\":\"prepresent-capture\","
        "\"event\":\"%s\",\"frame_id\":%d,\"message\":\"%s\"}\n",
        event, frame, detail ? detail : "");
    std::fclose(f);
}

template <typename T>
T sym(void* handle, const char* name) {
    return reinterpret_cast<T>(handle ? dlsym(handle, name) : nullptr);
}

bool write_atomic(const std::string& path, const void* data, size_t size) {
    const std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(data, 1, size, f) == size;
    if (std::fclose(f) != 0) ok = false;
    if (!ok) {
        std::remove(tmp.c_str());
        return false;
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}
} // namespace

extern "C" void mithril_e2e_capture_before_present(int width, int height, void* mithril_handle) {
    const std::string root = env_string("MITHRIL_E2E_ROOT");

    // One-shot heartbeat. This seam returns silently on several paths (empty
    // root, degenerate size, null handle, missing request file), and when that
    // happens it leaves NO evidence at all -- which is exactly how a missing
    // capture turned into an opaque RUNTIME_MINECRAFT_CAPTURE_TIMEOUT with no
    // way to tell whether the seam never ran, was misconfigured, or merely
    // never saw a request. Publish its own view of its configuration once.
    static std::atomic<bool> announced{false};
    if (!announced.exchange(true)) {
        char detail[512];
        std::snprintf(detail, sizeof(detail),
                      "root=%s width=%d height=%d handle=%s",
                      root.empty() ? "<empty>" : root.c_str(),
                      width, height, mithril_handle ? "set" : "NULL");
        append_event(root, "capture_seam_ready", 0, detail);
    }

    if (root.empty() || width <= 1 || height <= 1 || !mithril_handle) return;

    const std::string request = root + "/render/prepresent-request.txt";
    FILE* request_file = std::fopen(request.c_str(), "r");
    if (!request_file) {
        // Rate-limited: the seam runs on EVERY swap (one run presented 9720
        // frames), so log the first miss and then only occasionally.
        static std::atomic<int> misses{0};
        if (misses.fetch_add(1) % 500 == 0) {
            append_event(root, "capture_no_request", 0, request.c_str());
        }
        return;
    }

    int frame = 0;
    if (std::fscanf(request_file, "%d", &frame) != 1 || frame <= 0) {
        std::fclose(request_file);
        std::remove(request.c_str());
        append_event(root, "prepresent_capture_bad_request", frame, "invalid capture request");
        return;
    }
    std::fclose(request_file);
    // Claim the request before touching GL so one request maps to exactly one swap.
    std::remove(request.c_str());

    using GetIntegerv = void (*)(GLenum, GLint*);
    using BindFramebuffer = void (*)(GLenum, GLuint);
    using BindBuffer = void (*)(GLenum, GLuint);
    using PixelStorei = void (*)(GLenum, GLint);
    using ReadPixels = void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
    using GetError = GLenum (*)(void);

    GetIntegerv getIntegerv = sym<GetIntegerv>(mithril_handle, "glGetIntegerv");
    BindFramebuffer bindFramebuffer = sym<BindFramebuffer>(mithril_handle, "glBindFramebuffer");
    BindBuffer bindBuffer = sym<BindBuffer>(mithril_handle, "glBindBuffer");
    PixelStorei pixelStorei = sym<PixelStorei>(mithril_handle, "glPixelStorei");
    ReadPixels readPixels = sym<ReadPixels>(mithril_handle, "glReadPixels");
    GetError getError = sym<GetError>(mithril_handle, "glGetError");
    if (!getIntegerv || !bindFramebuffer || !bindBuffer || !pixelStorei || !readPixels || !getError) {
        append_event(root, "prepresent_capture_failed", frame, "required Mithril GL symbol missing");
        return;
    }

    GLint read_fbo = 0, pack_pbo = 0;
    GLint pack_alignment = 4, pack_row_length = 0, pack_skip_rows = 0, pack_skip_pixels = 0;
    GLint pack_image_height = 0, pack_skip_images = 0;
    getIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_fbo);
    getIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack_pbo);
    getIntegerv(GL_PACK_ALIGNMENT, &pack_alignment);
    getIntegerv(GL_PACK_ROW_LENGTH, &pack_row_length);
    getIntegerv(GL_PACK_SKIP_ROWS, &pack_skip_rows);
    getIntegerv(GL_PACK_SKIP_PIXELS, &pack_skip_pixels);
    getIntegerv(GL_PACK_IMAGE_HEIGHT, &pack_image_height);
    getIntegerv(GL_PACK_SKIP_IMAGES, &pack_skip_images);

    bindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    bindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    pixelStorei(GL_PACK_ALIGNMENT, 1);
    pixelStorei(GL_PACK_ROW_LENGTH, 0);
    pixelStorei(GL_PACK_SKIP_ROWS, 0);
    pixelStorei(GL_PACK_SKIP_PIXELS, 0);
    pixelStorei(GL_PACK_IMAGE_HEIGHT, 0);
    pixelStorei(GL_PACK_SKIP_IMAGES, 0);

    std::vector<unsigned char> rgba(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);

    // Drain any STALE GL error before the readback.
    //
    // GL error flags are sticky: once set, they stay until glGetError() clears
    // them, and later errors do not replace them. Minecraft had been running
    // for minutes (the bridge counted 9720 presents) and any earlier call that
    // reported GL_INVALID_ENUM left 0x0500 pending -- nothing drained it,
    // because until Stage 1 the wrapper's glGetError() always returned
    // GL_NO_ERROR, so the game never cleared anything.
    //
    // The capture therefore read that OLD error right after its own glReadPixels
    // and concluded "glReadPixels error 0x0500", even though the readback
    // itself had never reported a failure. Draining first means the error we
    // inspect afterwards can only come from this call.
    int drained = 0;
    for (int i = 0; i < 64; ++i) {
        if (getError() == GL_NO_ERROR) break;
        ++drained;
    }
    if (drained > 0) {
        char detail[96];
        std::snprintf(detail, sizeof(detail), "drained %d stale GL error(s)", drained);
        append_event(root, "capture_drained_stale_errors", frame, detail);
    }

    // PRE-READ: capture font atlas 1532 before the prepresent glReadPixels.
    {
        using ReadTex2 = int (*)(unsigned int, int, void*);
        ReadTex2 rt2 = sym<ReadTex2>(mithril_handle, "backend_read_texture_pixels");
        if (rt2) {
            std::vector<unsigned char> fa(256u*256*4u);
            if (rt2(1532,0,fa.data())) {
                FILE* qf = std::fopen("/tmp/fontatlas_pre.rgba","wb");
                if(qf){std::fwrite(fa.data(),1,fa.size(),qf);std::fclose(qf);}
            }
        }
    }
    // glReadPixels is synchronous by GL contract. In Mithril this path ends the
    // active render pass, submits the DirectMetal command buffer, blits the
    // current default-color texture into CPU-visible storage, and waits for the
    // copy. Crucially this runs before eglSwapBuffers presents/acquires the next
    // drawable, so it observes the frame that Minecraft just rendered.
    readPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    GLenum error = getError();

    // DIAG: read several pipeline-stage FBOs at their own sizes to locate content.
    // fbo3/fbo21 = 1708x960; fbo9 (shared color with fbo9-13) = 2048x2048 block atlas;
    // fbo18 = 1024x1024 GUI.
    struct Diag { GLuint fbo; int w,h; };
    const Diag diagList[] = { {4,512,256}, {3,1708,960}, {9,2048,2048}, {18,1024,1024}, {21,1708,960} };
    for (const Diag& dg : diagList) {
        std::vector<unsigned char> ur(static_cast<size_t>(dg.w)*dg.h*4u);
        bindFramebuffer(GL_READ_FRAMEBUFFER, dg.fbo);
        readPixels(0,0,dg.w,dg.h,GL_RGBA,GL_UNSIGNED_BYTE,ur.data());
        GLenum ue = getError();
        char un[32]; std::snprintf(un,sizeof(un),"userfbo-%u",dg.fbo);
        const std::string upath = root + "/render/" + un + ".rgba";
        write_atomic(upath, ur.data(), ur.size());
        unsigned long nz=0; for(size_t i=0;i<ur.size();++i) if(ur[i])++nz;
        char ud[160]; std::snprintf(ud,sizeof(ud),"fbo=%u %dx%d glError=0x%x nonzeroBytes=%lu size=%zu",dg.fbo,dg.w,dg.h,ue,nz,ur.size());
        append_event(root,"diag_user_fbo",frame,ud);
        // Depth readback for the block-atlas stage (fbo9): distinguishes vertex clip
        // (depth stays at clear) from fragment-color failure.
        if (dg.fbo == 9) {
            std::vector<float> dep(static_cast<size_t>(dg.w)*dg.h);
            readPixels(0,0,dg.w,dg.h,0x1902 /*GL_DEPTH_COMPONENT*/,0x1406 /*GL_FLOAT*/,dep.data());
            GLenum de = getError();
            float mn=1, mx=0, sum=0; int varied=0;
            for (size_t i=0;i<dep.size();++i){ float z=dep[i]; if(z<mn)mn=z; if(z>mx)mx=z; sum+=z; if(i && dep[i]!=dep[i-1]) ++varied; }
            const std::string dpath = root + "/render/userfbo-9.depth";
            write_atomic(dpath, dep.data(), dep.size()*sizeof(float));
            char dd[200]; std::snprintf(dd,sizeof(dd),"depth fbo9 glError=0x%x min=%.4f max=%.4f mean=%.4f varied=%zu",de,mn,mx,sum/dep.size(),(size_t)varied);
            append_event(root,"diag_depth",frame,dd);
        }
    }
    bindFramebuffer(GL_READ_FRAMEBUFFER, 0);

    // Post-frame (GPU work for the captured frame has completed by now): dump
    // the TRUE vertex-buffer content recorded by the MITHRIL_TXT_DUMP path.
    using ReadBufferHost = int (*)(unsigned int, unsigned long long,
                                  unsigned long long, void*);
    ReadBufferHost readBufferHost = sym<ReadBufferHost>(mithril_handle, "backend_read_buffer_host");
    if (readBufferHost) {
        FILE* cf2 = std::fopen("/tmp/mithril_txt_captures.txt", "r");
        if (cf2) {
            unsigned int nm; unsigned long long off; int st, fb, pr; int idx = 0;
            while (std::fscanf(cf2, "%u %llu %d %d %d", &nm, &off, &st, &fb, &pr) == 5 && idx < 24) {
                unsigned char vb[24 * 12] = {0};
                int got = readBufferHost(nm, off, sizeof(vb), vb);
                char outp[80];
                std::snprintf(outp, sizeof(outp), "/tmp/txtbuf_%02d_fbo%d_prog%d_got%d.rgba",
                              idx, fb, pr, got);
                FILE* of2 = std::fopen(outp, "wb");
                if (of2) { std::fwrite(vb, 1, sizeof(vb), of2); std::fclose(of2); }
                ++idx;
            }
            std::fclose(cf2);
        }
        // Hardcoded dump of the vertex staging/render ring buffers.
        for (unsigned int bn : {33u, 34u, 35u}) {
            const size_t cap = (bn == 35u) ? 4096u : 262144u;
            std::vector<unsigned char> vb(cap, 0);
            int got = readBufferHost(bn, 0, cap, vb.data());
            char outp[64];
            std::snprintf(outp, sizeof(outp), "/tmp/vring_%u_got%d.bin", bn, got);
            FILE* rf2 = std::fopen(outp, "wb");
            if (rf2) { std::fwrite(vb.data(), 1, cap, rf2); std::fclose(rf2); }
        }
        // Reliable direct GPU readback of the real font atlas (name 1532).
        using ReadTex = int (*)(unsigned int, int, void*);
        ReadTex readTex = sym<ReadTex>(mithril_handle, "backend_read_texture_pixels");
        if (readTex) {
            std::vector<unsigned char> fa(256u * 256u * 4u);
            if (readTex(1532, 0, fa.data())) {
                FILE* ff2 = std::fopen("/tmp/fontatlas_direct.rgba", "wb");
                if (ff2) { std::fwrite(fa.data(), 1, fa.size(), ff2); std::fclose(ff2); }
            }
        }
        using DumpRing = void (*)(const char*);
        DumpRing dumpRing = sym<DumpRing>(mithril_handle, "mithril_dump_draw_ring");
        if (dumpRing) dumpRing("/tmp/drawring.txt");
        // Dump all candidate textures bound on fbo3 near capture.
        using TexSize = void (*)(unsigned int, int*, int*, int*);
        TexSize texSize = sym<TexSize>(mithril_handle, "backend_texture_size");
        if (readTex && texSize) {
            for (unsigned int cn : {1523u,1525u,14u,1026u,13u,649u,1532u}) {
                int cw=0,ch=0,cf=0; texSize(cn,&cw,&ch,&cf);
                if(cw<=0||ch<=0) continue;
                std::vector<unsigned char> cb((size_t)cw*ch*4u);
                if (readTex(cn,0,cb.data())) {
                    char cp[96]; std::snprintf(cp,sizeof(cp),"/tmp/texcand_%u_%dx%d.rgba",cn,cw,ch);
                    FILE* cf2=std::fopen(cp,"wb");
                    if(cf2){std::fwrite(cb.data(),1,cb.size(),cf2);std::fclose(cf2);}
                }
            }
        }
    }

    bindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(read_fbo));
    bindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(pack_pbo));
    pixelStorei(GL_PACK_ALIGNMENT, pack_alignment);
    pixelStorei(GL_PACK_ROW_LENGTH, pack_row_length);
    pixelStorei(GL_PACK_SKIP_ROWS, pack_skip_rows);
    pixelStorei(GL_PACK_SKIP_PIXELS, pack_skip_pixels);
    pixelStorei(GL_PACK_IMAGE_HEIGHT, pack_image_height);
    pixelStorei(GL_PACK_SKIP_IMAGES, pack_skip_images);

    if (error != GL_NO_ERROR) {
        char detail[96];
        std::snprintf(detail, sizeof(detail), "glReadPixels error 0x%04x", error);
        append_event(root, "prepresent_capture_failed", frame, detail);
        return;
    }

    // Do not put the absolute path in a fixed-size C buffer: GitHub hosted
    // workspaces are long enough that a 96-byte stem silently truncates
    // "prepresent-frame-0001" into "prepres", making the producer and Java
    // consumer disagree even though readback itself succeeded.
    char frame_name[40];
    std::snprintf(frame_name, sizeof(frame_name), "prepresent-frame-%04d", frame);
    const std::string stem = root + "/render/" + frame_name;
    const std::string raw_path = stem + ".rgba";
    const std::string meta_path = stem + ".meta";
    if (!write_atomic(raw_path, rgba.data(), rgba.size())) {
        append_event(root, "prepresent_capture_failed", frame, "could not persist RGBA readback");
        return;
    }

    char meta[128];
    int n = std::snprintf(meta, sizeof(meta), "%d %d %zu\n", width, height, rgba.size());
    if (n <= 0 || !write_atomic(meta_path, meta, static_cast<size_t>(n))) {
        append_event(root, "prepresent_capture_failed", frame, "could not persist capture metadata");
        return;
    }
    append_event(root, "prepresent_capture_completed", frame, "captured default framebuffer before eglSwapBuffers");
}
