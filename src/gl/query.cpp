// OpenGL query-object names and observable target/result semantics.
//
// DirectMetal implements the two GL 3.3 occlusion targets. Timer and
// transform-feedback queries are rejected explicitly until their native
// execution paths exist; they are never answered with fabricated values.

#include "internal.h"

#include <limits>
#include <memory>

namespace {

struct QueryObject {
    GLuint name = 0;
    GLenum target = 0;
    uint64_t backend = 0;
    bool active = false;
    bool conditional_active = false;
    bool delete_pending = false;
};

std::unordered_map<GLuint, std::shared_ptr<QueryObject>> g_queries;
std::unordered_map<GLenum, std::shared_ptr<QueryObject>> g_active_queries;
std::shared_ptr<QueryObject> g_active_occlusion;
std::shared_ptr<QueryObject> g_conditional_query;
bool g_conditional_result = true;
GLuint g_next_query = 1;

bool IsOcclusionTarget(GLenum target) {
    return target == GL_SAMPLES_PASSED || target == GL_ANY_SAMPLES_PASSED;
}

bool IsKnownUnsupportedTarget(GLenum target) {
    return target == GL_PRIMITIVES_GENERATED ||
           target == GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN ||
           target == GL_TIME_ELAPSED;
}

bool ValidateTarget(GLenum target) {
    if (IsOcclusionTarget(target)) return true;
    PUSH_ERROR(IsKnownUnsupportedTarget(target) ? GL_INVALID_OPERATION
                                                : GL_INVALID_ENUM);
    return false;
}

std::shared_ptr<QueryObject> FinishedQuery(GLuint id) {
    auto found = g_queries.find(id);
    if (found == g_queries.end() || !found->second->target ||
        !found->second->backend) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return nullptr;
    }
    if (found->second->active) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return nullptr;
    }
    return found->second;
}

bool QueryValue(GLuint id, GLenum pname, uint64_t* value) {
    auto query = FinishedQuery(id);
    if (!query) return false;
    if (pname == GL_QUERY_RESULT_AVAILABLE) {
        *value = v::OcclusionQueryAvailable(query->backend) ? GL_TRUE : GL_FALSE;
        return true;
    }
    if (pname != GL_QUERY_RESULT) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return false;
    }
    if (!v::GetOcclusionQueryResult(query->backend, value)) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return false;
    }
    return true;
}

template <typename Destination>
Destination ClampQueryValue(uint64_t value) {
    constexpr uint64_t maximum = static_cast<uint64_t>(
        std::numeric_limits<Destination>::max());
    return static_cast<Destination>(std::min(value, maximum));
}

} // namespace

uint64_t CurrentOcclusionQueryHandle() {
    return g_active_occlusion ? g_active_occlusion->backend : 0;
}

bool ConditionalRenderingAllowsCommands() {
    return !g_conditional_query || g_conditional_result;
}

extern "C" {

void APIENTRY glGenQueries(GLsizei n, GLuint* ids) {
    if (n < 0) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    for (GLsizei i = 0; i < n; ++i) {
        while (!g_next_query || g_queries.count(g_next_query)) ++g_next_query;
        const GLuint name = g_next_query++;
        auto query = std::make_shared<QueryObject>();
        query->name = name;
        g_queries.emplace(name, std::move(query));
        ids[i] = name;
    }
}

void APIENTRY glDeleteQueries(GLsizei n, const GLuint* ids) {
    if (n < 0) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    for (GLsizei i = 0; i < n; ++i) {
        auto found = g_queries.find(ids[i]);
        if (found == g_queries.end()) continue;
        auto query = found->second;
        g_queries.erase(found);
        if (query->active || query->conditional_active) {
            // GL deletion releases the name immediately, but commands until
            // the matching EndQuery/EndConditionalRender still belong to the
            // underlying object.
            query->delete_pending = true;
        } else if (query->backend) {
            v::DestroyOcclusionQuery(query->backend);
        }
    }
}

GLboolean APIENTRY glIsQuery(GLuint id) {
    auto found = g_queries.find(id);
    return found != g_queries.end() && found->second->target
        ? GL_TRUE : GL_FALSE;
}

void APIENTRY glBeginQuery(GLenum target, GLuint id) {
    if (!ValidateTarget(target)) return;
    if (!id) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    auto found = g_queries.find(id);
    if (found == g_queries.end()) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    auto query = found->second;
    if (g_active_queries.count(target) || query->active ||
        query->conditional_active ||
        (query->target && query->target != target)) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    // Metal exposes one visibility-result mode for a draw. GL permits the two
    // occlusion target classes to overlap; reject that uncommon combination
    // explicitly until multi-counter lowering exists.
    if (g_active_occlusion) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    if (!v::EnsureInit()) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    const uint64_t backend = v::CreateOcclusionQuery(
        target == GL_ANY_SAMPLES_PASSED);
    if (!backend) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    if (query->backend) v::DestroyOcclusionQuery(query->backend);
    query->backend = backend;
    query->target = target;
    query->active = true;
    g_active_queries[target] = query;
    g_active_occlusion = std::move(query);
}

void APIENTRY glEndQuery(GLenum target) {
    if (!ValidateTarget(target)) return;
    auto found = g_active_queries.find(target);
    if (found == g_active_queries.end()) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    auto query = found->second;
    v::EndOcclusionQuery(query->backend);
    query->active = false;
    g_active_queries.erase(found);
    g_active_occlusion.reset();
    if (query->delete_pending)
        v::DestroyOcclusionQuery(query->backend);
}

void APIENTRY glGetQueryiv(GLenum target, GLenum pname, GLint* params) {
    if (!params || !ValidateTarget(target)) return;
    if (pname == GL_CURRENT_QUERY) {
        auto found = g_active_queries.find(target);
        *params = found == g_active_queries.end()
            ? 0 : static_cast<GLint>(found->second->name);
        return;
    }
    if (pname == GL_QUERY_COUNTER_BITS) {
        *params = target == GL_ANY_SAMPLES_PASSED ? 1 : 64;
        return;
    }
    PUSH_ERROR(GL_INVALID_ENUM);
}

void APIENTRY glGetQueryObjectuiv(GLuint id, GLenum pname, GLuint* params) {
    if (!params) return;
    uint64_t value = 0;
    if (QueryValue(id, pname, &value))
        *params = ClampQueryValue<GLuint>(value);
}

void APIENTRY glGetQueryObjectiv(GLuint id, GLenum pname, GLint* params) {
    if (!params) return;
    uint64_t value = 0;
    if (QueryValue(id, pname, &value))
        *params = ClampQueryValue<GLint>(value);
}

void APIENTRY glGetQueryObjectui64v(GLuint id, GLenum pname,
                                    GLuint64* params) {
    if (!params) return;
    uint64_t value = 0;
    if (QueryValue(id, pname, &value)) *params = value;
}

void APIENTRY glGetQueryObjecti64v(GLuint id, GLenum pname, GLint64* params) {
    if (!params) return;
    uint64_t value = 0;
    if (QueryValue(id, pname, &value))
        *params = ClampQueryValue<GLint64>(value);
}

void APIENTRY glQueryCounter(GLuint, GLenum target) {
    // GL_TIMESTAMP needs a real GPU timestamp/counter-sample implementation.
    PUSH_ERROR(target == GL_TIMESTAMP ? GL_INVALID_OPERATION : GL_INVALID_ENUM);
}

void APIENTRY glBeginConditionalRender(GLuint id, GLenum mode) {
    const bool wait = mode == GL_QUERY_WAIT ||
                      mode == GL_QUERY_BY_REGION_WAIT;
    if (!wait && mode != GL_QUERY_NO_WAIT &&
        mode != GL_QUERY_BY_REGION_NO_WAIT) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }
    if (g_conditional_query) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }

    auto found = g_queries.find(id);
    // GenQueries reserves a name, but it does not become a query object until
    // the first successful BeginQuery. Such an unused name is INVALID_VALUE
    // here, just like an unknown or deleted name.
    if (found == g_queries.end() || !found->second->target) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    auto query = found->second;
    if (!IsOcclusionTarget(query->target) || query->active ||
        query->conditional_active) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }

    uint64_t result = 1;
    if (wait) {
        if (!v::GetOcclusionQueryResult(query->backend, &result)) {
            PUSH_ERROR(GL_INVALID_OPERATION);
            return;
        }
    } else if (v::OcclusionQueryAvailable(query->backend)) {
        // NO_WAIT may execute unconditionally while a result is unavailable.
        // Once DirectMetal can observe a completed result, honour it exactly.
        if (!v::GetOcclusionQueryResult(query->backend, &result)) {
            PUSH_ERROR(GL_INVALID_OPERATION);
            return;
        }
    }

    query->conditional_active = true;
    g_conditional_query = std::move(query);
    g_conditional_result = result != 0;
}

void APIENTRY glEndConditionalRender(void) {
    if (!g_conditional_query) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    auto query = std::move(g_conditional_query);
    query->conditional_active = false;
    g_conditional_result = true;
    if (query->delete_pending && query->backend)
        v::DestroyOcclusionQuery(query->backend);
}

} // extern "C"
