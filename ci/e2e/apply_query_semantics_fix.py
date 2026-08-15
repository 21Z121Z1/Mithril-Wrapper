#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def replace_once(path: str, old: str, new: str, label: str) -> None:
    p = ROOT / path
    s = p.read_text()
    if new in s:
        return
    n = s.count(old)
    if n != 1:
        raise SystemExit(f"{label}: expected one match, found {n}")
    p.write_text(s.replace(old, new, 1))


def replace_all_expected(path: str, old: str, new: str, expected: int, label: str) -> None:
    p = ROOT / path
    s = p.read_text()
    if new in s and old not in s:
        return
    n = s.count(old)
    if n != expected:
        raise SystemExit(f"{label}: expected {expected} matches, found {n}")
    p.write_text(s.replace(old, new))

# Software GL_PRIMITIVES_GENERATED/TFB bookkeeping had state but no draw-side
# accounting. Count successfully accepted direct draws at the GL frontend where
# mode/count/instance cardinality are still exact and backend-independent.
replace_once(
    "Mithril-Wrapper-cpp/MG_Impl/Drawing.cpp",
    '''void glDrawArrays(GLenum mode, GLint first, GLsizei count) {\n''',
    '''static uint64_t primitive_count_for_draw(GLenum mode, GLsizei count) {\n    if (count <= 0) return 0;\n    switch (mode) {\n        case GL_POINTS:         return (uint64_t)count;\n        case GL_LINES:          return (uint64_t)count / 2u;\n        case GL_LINE_STRIP:     return count >= 2 ? (uint64_t)count - 1u : 0u;\n        case GL_LINE_LOOP:      return count >= 2 ? (uint64_t)count : 0u;\n        case GL_TRIANGLES:      return (uint64_t)count / 3u;\n        case GL_TRIANGLE_STRIP:\n        case GL_TRIANGLE_FAN:   return count >= 3 ? (uint64_t)count - 2u : 0u;\n        default:                return 0u;\n    }\n}\n\nstatic void account_software_primitive_queries(GLenum mode, GLsizei count,\n                                                GLsizei instances) {\n    if (!g_state || count <= 0 || instances <= 0) return;\n    const uint64_t prims = primitive_count_for_draw(mode, count) *\n                           (uint64_t)instances;\n    if (g_state->activeQuery[(int)mithril::QueryTarget::PrimitivesGenerated] != 0)\n        g_state->swPrimAccum += prims;\n\n    if (g_state->activeQuery[(int)mithril::QueryTarget::TfbPrimsWritten] != 0) {\n        auto* tf = mithril::state_get_transform_feedback(g_state->currentTransformFeedback);\n        if (tf && tf->active && !tf->paused)\n            g_state->swTfbWrittenAccum += prims;\n    }\n}\n\nvoid glDrawArrays(GLenum mode, GLint first, GLsizei count) {\n''',
    "primitive accounting helper",
)

# Direct and instanced draws: account only after prepare_draw accepted the draw.
replacements = [
    (
        '''    if (!prepare_draw(mode)) return;\n    backend_draw_arrays((int)mode, (int)first, (int)count);\n''',
        '''    if (!prepare_draw(mode)) return;\n    account_software_primitive_queries(mode, count, 1);\n    backend_draw_arrays((int)mode, (int)first, (int)count);\n''',
        "draw arrays accounting",
    ),
    (
        '''    if (!prepare_draw(mode)) return;  // root cause AI — see glDrawArrays\n    backend_draw_arrays_instanced((int)mode, (int)first, (int)count, (int)primcount);\n''',
        '''    if (!prepare_draw(mode)) return;  // root cause AI — see glDrawArrays\n    account_software_primitive_queries(mode, count, primcount);\n    backend_draw_arrays_instanced((int)mode, (int)first, (int)count, (int)primcount);\n''',
        "draw arrays instanced accounting",
    ),
    (
        '''    if (!prepare_draw(mode)) { g_state->currentBaseInstance = 0; return; }\n    backend_draw_arrays_instanced((int)mode, (int)first, (int)count, (int)primcount);\n''',
        '''    if (!prepare_draw(mode)) { g_state->currentBaseInstance = 0; return; }\n    account_software_primitive_queries(mode, count, primcount);\n    backend_draw_arrays_instanced((int)mode, (int)first, (int)count, (int)primcount);\n''',
        "draw arrays base instance accounting",
    ),
    (
        '''    if (!prepare_draw(mode)) return;  // root cause AI — see glDrawArrays\n    // If a VBO is bound for GL_ELEMENT_ARRAY_BUFFER, indices is an offset into it.\n''',
        '''    if (!prepare_draw(mode)) return;  // root cause AI — see glDrawArrays\n    account_software_primitive_queries(mode, count, 1);\n    // If a VBO is bound for GL_ELEMENT_ARRAY_BUFFER, indices is an offset into it.\n''',
        "draw elements accounting",
    ),
    (
        '''    if (!prepare_draw(mode)) return;  // root cause AI — see glDrawArrays\n    mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO);\n''',
        '''    if (!prepare_draw(mode)) return;  // root cause AI — see glDrawArrays\n    account_software_primitive_queries(mode, count, primcount);\n    mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO);\n''',
        "draw elements instanced accounting",
    ),
]
for old, new, label in replacements:
    replace_once("Mithril-Wrapper-cpp/MG_Impl/Drawing.cpp", old, new, label)

# MultiDraw direct loops share one prepare_draw; account each emitted subdraw.
replace_once(
    "Mithril-Wrapper-cpp/MG_Impl/Drawing.cpp",
    '''        backend_draw_arrays((int)mode, (int)first[i], (int)count[i]);\n''',
    '''        account_software_primitive_queries(mode, count[i], 1);\n        backend_draw_arrays((int)mode, (int)first[i], (int)count[i]);\n''',
    "multidraw arrays accounting",
)
replace_all_expected(
    "Mithril-Wrapper-cpp/MG_Impl/Drawing.cpp",
    '''            if (need_draw)\n                backend_draw_indexed((int)mode, (int)count[i], idx_type, ib,\n''',
    '''            if (need_draw) {\n                account_software_primitive_queries(mode, count[i], 1);\n                backend_draw_indexed((int)mode, (int)count[i], idx_type, ib,\n''',
    1,
    "multidraw elements accounting open",
)
replace_once(
    "Mithril-Wrapper-cpp/MG_Impl/Drawing.cpp",
    '''                                     (VkDeviceSize)(intptr_t)indices[i]);\n        }\n    } else {\n        // 客户端指针路径：逐 sub-draw staging 进 transient buffer\n''',
    '''                                     (VkDeviceSize)(intptr_t)indices[i]);\n            }\n        }\n    } else {\n        // 客户端指针路径：逐 sub-draw staging 进 transient buffer\n''',
    "multidraw elements accounting close",
)
replace_once(
    "Mithril-Wrapper-cpp/MG_Impl/Drawing.cpp",
    '''                if (staged != VK_NULL_HANDLE)\n                    backend_draw_indexed((int)mode, (int)count[i], idx_type, staged, 0);\n''',
    '''                if (staged != VK_NULL_HANDLE) {\n                    account_software_primitive_queries(mode, count[i], 1);\n                    backend_draw_indexed((int)mode, (int)count[i], idx_type, staged, 0);\n                }\n''',
    "multidraw elements staged accounting",
)

# Query/sync smoke was written when glGetError intentionally hid errors. Now
# assert the actual GL error contract at each invalid call and drain it there,
# so later sync assertions cannot be polluted by intentional earlier errors.
replace_once(
    "tests/query_sync_smoke.c",
    '''    /* 注：本项目 glGetError 依 MobileGlues 惯例恒返回 GL_NO_ERROR（防 MC\n     * 日志刷屏，Getter.cpp:197），错误只入队不外报。故此处断言可观测契约：\n     * 重复 begin 不崩溃、活动查询槽仍指向第一个查询、endQuery 只结束一次。*/\n    beginQuery(GL_SAMPLES_PASSED, qOcc);\n    beginQuery(GL_SAMPLES_PASSED, qAny);  /* 同 target 重复 begin：被拒 */\n    GLint nested = 0;\n    getQueryiv(GL_SAMPLES_PASSED, GL_CURRENT_QUERY, &nested);\n    CHECK(nested == (GLint)qOcc,\n          "nested glBeginQuery(same target) rejected, current query stays %d", nested);\n    endQuery(GL_SAMPLES_PASSED);\n    endQuery(GL_SAMPLES_PASSED);  /* 第二次 end 无活跃查询：安全 no-op */\n    getError();\n''',
    '''    beginQuery(GL_SAMPLES_PASSED, qOcc);\n    beginQuery(GL_SAMPLES_PASSED, qAny);  /* 同 target 重复 begin：INVALID_OPERATION */\n    CHECK(getError() == GL_INVALID_OPERATION,\n          "nested glBeginQuery reports GL_INVALID_OPERATION");\n    GLint nested = 0;\n    getQueryiv(GL_SAMPLES_PASSED, GL_CURRENT_QUERY, &nested);\n    CHECK(nested == (GLint)qOcc,\n          "nested glBeginQuery(same target) rejected, current query stays %d", nested);\n    endQuery(GL_SAMPLES_PASSED);\n    endQuery(GL_SAMPLES_PASSED);  /* 第二次 end 无活跃查询 */\n    CHECK(getError() == GL_INVALID_OPERATION,\n          "glEndQuery without active query reports GL_INVALID_OPERATION");\n    CHECK(getError() == GL_NO_ERROR, "query error queue drained after exact assertions");\n''',
    "query error assertions",
)
replace_once(
    "tests/query_sync_smoke.c",
    '''    GLsync bad = fenceSync(0x1234 /* bad condition */, 0);\n    CHECK(bad == NULL, "glFenceSync(bad condition) → NULL (error queued, not reported)");\n    deleteSync(f2);\n    CHECK(isSync(f2) == GL_FALSE, "glIsSync false after glDeleteSync");\n    CHECK(clientWaitSync(f2, 0, 0) == GL_WAIT_FAILED,\n          "glClientWaitSync(deleted) → GL_WAIT_FAILED");\n    CHECK(clientWaitSync(NULL, 0, 0) == GL_WAIT_FAILED,\n          "glClientWaitSync(NULL) → GL_WAIT_FAILED");\n''',
    '''    GLsync bad = fenceSync(0x1234 /* bad condition */, 0);\n    CHECK(bad == NULL, "glFenceSync(bad condition) → NULL");\n    CHECK(getError() == GL_INVALID_ENUM,\n          "glFenceSync(bad condition) reports GL_INVALID_ENUM");\n    deleteSync(f2);\n    CHECK(isSync(f2) == GL_FALSE, "glIsSync false after glDeleteSync");\n    CHECK(clientWaitSync(f2, 0, 0) == GL_WAIT_FAILED,\n          "glClientWaitSync(deleted) → GL_WAIT_FAILED");\n    CHECK(getError() == GL_INVALID_VALUE,\n          "glClientWaitSync(deleted) reports GL_INVALID_VALUE");\n    CHECK(clientWaitSync(NULL, 0, 0) == GL_WAIT_FAILED,\n          "glClientWaitSync(NULL) → GL_WAIT_FAILED");\n    CHECK(getError() == GL_INVALID_VALUE,\n          "glClientWaitSync(NULL) reports GL_INVALID_VALUE");\n''',
    "sync error assertions",
)

print("query semantics transformation complete")
