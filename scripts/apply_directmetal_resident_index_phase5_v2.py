#!/usr/bin/env python3
"""Run phase 5 with DrawElementsImpl replaced as one semantic unit."""
from pathlib import Path

base = Path("scripts/apply_directmetal_resident_index_phase5.py")
script = base.read_text()

start_marker = "# Replace the common non-restart DrawElements tail with a direct-source attempt.\n"
end_marker = "# ---------------------------------------------------------------------------\n# DirectMetal: retain the EBO generation"
start = script.index(start_marker)
end = script.index(end_marker, start)

replacement = r'''# Replace DrawElementsImpl atomically so the direct path is attempted before
# LoadIndices allocates/converts the compatibility vector.
exact("src/gl/draw.cpp",
''' + "'''" + r'''void DrawElementsImpl(GLenum mode, GLsizei count, GLenum type,
                      const void* indices, GLint base_vertex,
                      GLsizei instance_count, GLuint start, GLuint end) {
    if (count < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    GLenum err = GL_NO_ERROR;
    std::vector<uint32_t> idx = LoadIndices(type, indices, count, start, end, &err);
    if (err) { PUSH_ERROR(err); return; }
    if (idx.empty()) return;
    const bool has_restart = std::find(idx.begin(), idx.end(), UINT32_MAX) !=
                             idx.end();
    const bool native_restart = mode == GL_TRIANGLE_STRIP ||
                                mode == GL_LINE_STRIP;
    if (has_restart && !native_restart) {
        // Metal has no native triangle-fan primitive, and Vulkan list restart
        // is not universally available. Split these modes at the shared
        // semantic layer; each segment keeps the original baseVertex and
        // instance parameters, and no restart vertex is ever fetched.
        size_t begin = 0;
        for (size_t i = 0; i <= idx.size(); ++i) {
            if (i != idx.size() && idx[i] != UINT32_MAX) continue;
            if (i > begin) {
                std::vector<uint32_t> segment(idx.begin() + begin,
                                              idx.begin() + i);
                SubmitIndexSegment(mode, segment, base_vertex,
                                   instance_count);
            }
            begin = i + 1;
        }
        return;
    }
    SubmitIndexSegment(mode, idx, base_vertex, instance_count);
}
''' + "'''" + r''',
''' + "'''" + r'''void DrawElementsImpl(GLenum mode, GLsizei count, GLenum type,
                      const void* indices, GLint base_vertex,
                      GLsizei instance_count, GLuint start, GLuint end) {
    if (count < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }

    v::ResidentIndexSource resident;
    uint32_t resident_max = 0;
    GLenum resident_error = GL_NO_ERROR;
    const ResidentIndexResult resident_result = TryResolveResidentIndices(
        mode, type, indices, count, start, end,
        &resident, &resident_max, &resident_error);
    if (resident_result == ResidentIndexResult::Error) {
        PUSH_ERROR(resident_error);
        return;
    }
    if (resident_result == ResidentIndexResult::Ready) {
        DrawCommon(mode, {}, 0, count, base_vertex, instance_count,
                   &resident, resident_max);
        return;
    }

    GLenum err = GL_NO_ERROR;
    std::vector<uint32_t> idx = LoadIndices(type, indices, count, start, end, &err);
    if (err) { PUSH_ERROR(err); return; }
    if (idx.empty()) return;
    const bool has_restart = std::find(idx.begin(), idx.end(), UINT32_MAX) !=
                             idx.end();
    const bool native_restart = mode == GL_TRIANGLE_STRIP ||
                                mode == GL_LINE_STRIP;
    if (has_restart && !native_restart) {
        size_t begin = 0;
        for (size_t i = 0; i <= idx.size(); ++i) {
            if (i != idx.size() && idx[i] != UINT32_MAX) continue;
            if (i > begin) {
                std::vector<uint32_t> segment(idx.begin() + begin,
                                              idx.begin() + i);
                SubmitIndexSegment(mode, segment, base_vertex,
                                   instance_count);
            }
            begin = i + 1;
        }
        return;
    }
    SubmitIndexSegment(mode, idx, base_vertex, instance_count);
}
''' + "'''" + r''')

'''

script = script[:start] + replacement + script[end:]

# Remove the obsolete text-shape guard tied to an older DrawElementsImpl.
old_guard = '''if "DrawCommon(mode, idx, 0, count, base_vertex, instance_count);\\n        return;\\n    }\\n\\n    // Restart handling" not in gl:\n    raise SystemExit("compatibility restart path shape changed unexpectedly")\n'''
if old_guard not in script:
    raise SystemExit("obsolete DrawElements guard not found in base patcher")
script = script.replace(old_guard, '''if "LoadIndices(type, indices, count, start, end, &err)" not in gl:\n    raise SystemExit("compatibility index lowering disappeared unexpectedly")\n''', 1)

exec(compile(script, str(base), "exec"), {"__name__": "__main__", "__file__": str(base)})
