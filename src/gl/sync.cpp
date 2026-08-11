// OpenGL 3.2 sync objects. The frontend owns opaque GLsync names and all
// observable validation; native backends own execution-completion fences.

#include "internal.h"

#include <new>

struct __GLsync {
    uint64_t backend_fence = 0;
};

namespace {

std::unordered_set<GLsync> g_syncs;

bool IsLiveSync(GLsync sync) {
    return sync && g_syncs.find(sync) != g_syncs.end();
}

} // namespace

extern "C" {

GLsync APIENTRY glFenceSync(GLenum condition, GLbitfield flags) {
    if (condition != GL_SYNC_GPU_COMMANDS_COMPLETE) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return nullptr;
    }
    if (flags != 0) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return nullptr;
    }
    if (!v::EnsureInit()) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return nullptr;
    }
    const uint64_t backend_fence = v::CreateFence();
    if (!backend_fence) {
        PUSH_ERROR(GL_OUT_OF_MEMORY);
        return nullptr;
    }
    auto* sync = new (std::nothrow) __GLsync{backend_fence};
    if (!sync) {
        v::DestroyFence(backend_fence);
        PUSH_ERROR(GL_OUT_OF_MEMORY);
        return nullptr;
    }
    g_syncs.insert(sync);
    return sync;
}

void APIENTRY glDeleteSync(GLsync sync) {
    if (!sync) return;
    auto found = g_syncs.find(sync);
    if (found == g_syncs.end()) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    v::DestroyFence(sync->backend_fence);
    g_syncs.erase(found);
    delete sync;
}

GLboolean APIENTRY glIsSync(GLsync sync) {
    return IsLiveSync(sync) ? GL_TRUE : GL_FALSE;
}

GLenum APIENTRY glClientWaitSync(GLsync sync, GLbitfield flags,
                                 GLuint64 timeout) {
    if (!IsLiveSync(sync)) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return GL_WAIT_FAILED;
    }
    if (flags & ~GL_SYNC_FLUSH_COMMANDS_BIT) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return GL_WAIT_FAILED;
    }
    // DirectMetal submits a deferred batch when the fence is created, so the
    // optional flush bit never needs to introduce a second submission here.
    switch (v::ClientWaitFence(sync->backend_fence, timeout)) {
        case v::SyncWaitResult::AlreadySignaled: return GL_ALREADY_SIGNALED;
        case v::SyncWaitResult::ConditionSatisfied:
            return GL_CONDITION_SATISFIED;
        case v::SyncWaitResult::TimeoutExpired: return GL_TIMEOUT_EXPIRED;
        default:
            PUSH_ERROR(GL_INVALID_OPERATION);
            return GL_WAIT_FAILED;
    }
}

void APIENTRY glWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
    if (!IsLiveSync(sync)) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    if (flags != 0 || timeout != GL_TIMEOUT_IGNORED) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    if (!v::ServerWaitFence(sync->backend_fence))
        PUSH_ERROR(GL_INVALID_OPERATION);
}

void APIENTRY glGetSynciv(GLsync sync, GLenum pname, GLsizei count,
                          GLsizei* length, GLint* values) {
    if (!IsLiveSync(sync)) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    GLint value = 0;
    switch (pname) {
        case GL_OBJECT_TYPE: value = GL_SYNC_FENCE; break;
        case GL_SYNC_STATUS:
            value = v::FenceSignaled(sync->backend_fence)
                ? GL_SIGNALED : GL_UNSIGNALED;
            break;
        case GL_SYNC_CONDITION:
            value = GL_SYNC_GPU_COMMANDS_COMPLETE;
            break;
        case GL_SYNC_FLAGS: value = 0; break;
        default:
            PUSH_ERROR(GL_INVALID_ENUM);
            return;
    }
    if (count < 0) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    const GLsizei written = count > 0 ? 1 : 0;
    if (written && values) values[0] = value;
    if (length) *length = written;
}

} // extern "C"
