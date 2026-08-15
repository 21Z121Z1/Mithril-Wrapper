#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MARKER = "GL semantic closure 2026-08-16"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def insert_after_once(text: str, anchor: str, addition: str, label: str) -> str:
    return replace_once(text, anchor, anchor + addition, label)


def patch_getter() -> None:
    p = ROOT / "Mithril-Wrapper-cpp/MG_Impl/Getter.cpp"
    s = p.read_text()
    if MARKER in s:
        return

    old_comment_start = "// Target desktop OpenGL 4.6 Core Profile."
    comment_pos = s.index(old_comment_start)
    shading_pos = s.index('static const char* kShadingLangVer =', comment_pos)
    new_comment = f'''// {MARKER}: advertise only the semantic surface for which this branch has\n// executable Minecraft/DirectMetal evidence.  Reporting GL 4.6 made every\n// core 4.x command an implicit promise even when parts of that state machine\n// were still compatibility stubs.  Keep the core contract at 3.3 and expose\n// newer behavior only through individually proven extensions below.\n'''
    s = s[:comment_pos] + new_comment + s[shading_pos:]
    s = replace_once(
        s,
        'static const char* kShadingLangVer = "4.60 Mithril-Wrapper (glslang -> SPIR-V)";',
        'static const char* kShadingLangVer = "3.30 Mithril-Wrapper (glslang -> SPIR-V)";',
        "GLSL version",
    )
    s = replace_once(
        s,
        'cached = std::string("4.6.0 §bMithril-Wrapper§r 1.0 (")',
        'cached = std::string("3.3.0 §bMithril-Wrapper§r 1.0 (")',
        "GL version",
    )

    ext_start = s.index("// Full Core Profile 4.6 extension advertisement.")
    ext_array = s.index("static const char* kExtensions[] = {", ext_start)
    ext_end = s.index("};", ext_array) + 2
    new_ext = '''// Extension advertisement is a capability contract, not a symbol-presence\n// compatibility list.  Every entry here must have a matching manifest record,\n// exported symbols, state semantics, and an executable oracle.  The contract\n// gate in ci/e2e/check_gl_semantic_contract.py enforces exact equality.\nstatic const char* kExtensions[] = {\n    "GL_ARB_vertex_attrib_binding",\n    "GL_ARB_clip_control",\n    "GL_ARB_multi_draw_indirect",\n    "GL_ARB_indirect_parameters",\n    "GL_MITHRIL_wrapper",\n};'''
    s = s[:ext_start] + new_ext + s[ext_end:]

    old_get_error = '''GLenum glGetError(void) {\n    MITHRIL_ENSURE_INIT();\n    // Mirror MobileGlues: always return GL_NO_ERROR to prevent Minecraft from\n    // spamming the log with GL errors that are harmless in the translation layer.\n    mithril::state_take_error();\n    return GL_NO_ERROR;\n}\n'''
    new_get_error = '''GLenum glGetError(void) {\n    MITHRIL_ENSURE_INIT();\n    // GL semantic closure: expose the error state recorded by the translation\n    // layer.  Silently draining it made every conformance assertion false-green.\n    return mithril::state_take_error();\n}\n'''
    s = replace_once(s, old_get_error, new_get_error, "glGetError")
    p.write_text(s)


def patch_program_uniforms() -> None:
    p = ROOT / "Mithril-Wrapper-cpp/MG_Impl/GL46_Compat.cpp"
    s = p.read_text()
    old = '''static void program_uniform_begin(GLuint program) {\n    if (!g_state) return;\n    g_state->currentProgram = program;\n}\n'''
    new = '''class ProgramUniformScope {\npublic:\n    explicit ProgramUniformScope(GLuint program)\n        : state_(g_state), previous_(state_ ? state_->currentProgram : 0) {\n        if (!state_) return;\n        mithril::Program* target = mithril::state_get_program(program);\n        if (!target || !target->linked) {\n            mithril::state_set_error(GL_INVALID_OPERATION);\n            return;\n        }\n        state_->currentProgram = program;\n        valid_ = true;\n    }\n\n    ~ProgramUniformScope() {\n        if (valid_ && state_) state_->currentProgram = previous_;\n    }\n\n    bool valid() const { return valid_; }\n\nprivate:\n    mithril::GLState* state_ = nullptr;\n    GLuint previous_ = 0;\n    bool valid_ = false;\n};\n\n// Keep the existing compact ProgramUniform wrappers while making the temporary\n// selector override exception/early-return safe.  The RAII scope restores the\n// program selected by glUseProgram when each wrapper returns.\n#define program_uniform_begin(program) \\\n    MITHRIL_ENSURE_INIT(); \\\n    ProgramUniformScope _programUniformScope((program)); \\\n    if (!_programUniformScope.valid()) return\n'''
    s = replace_once(s, old, new, "ProgramUniform scope")
    marker = "/* ---- Program pipeline objects (GL 4.1): minimal name tracking."
    s = replace_once(
        s,
        marker,
        "#undef program_uniform_begin\n\n" + marker,
        "ProgramUniform macro end",
    )
    p.write_text(s)


def patch_trace_state() -> None:
    hp = ROOT / "Mithril-Wrapper-cpp/MG_State/State.h"
    hs = hp.read_text()
    declaration_anchor = '''// ---- Error helpers ----\nvoid   state_set_error(GLenum err);\nGLenum state_take_error();\n'''
    declaration_new = declaration_anchor + '''\n// Optional production semantic trace. Enabled only when\n// MITHRIL_GL_SEMANTIC_TRACE points at an output TSV file.\nvoid semantic_trace_eventf(const char* domain, const char* semantic,\n                           const char* api, const char* fmt, ...);\n'''
    hs = replace_once(hs, declaration_anchor, declaration_new, "trace declaration")
    hs = hs.replace(
        "// (glVertexAttribDivisor is spec'd as shorthand for setting the divisor of the\n// binding the attribute currently points at). Pipeline.cpp reads",
        "// (glVertexAttribDivisor is spec'd as VertexAttribBinding(index,index) followed\n// by VertexBindingDivisor(index,divisor)). Pipeline.cpp reads",
        1,
    )
    hp.write_text(hs)

    cp = ROOT / "Mithril-Wrapper-cpp/MG_State/State.cpp"
    cs = cp.read_text()
    cs = insert_after_once(
        cs,
        '#include "State.h"\n',
        '\n#include <cstdarg>\n#include <cstdio>\n#include <cstdlib>\n#include <mutex>\n',
        "trace includes",
    )
    anchor = '''// ---- EGL initialized flag ----\nbool g_eglInitialized = false;\n'''
    impl = anchor + '''\n// Optional semantic trace used by the production Minecraft E2E.  It is fully\n// dormant unless MITHRIL_GL_SEMANTIC_TRACE is set, so normal runtime hot paths\n// pay only a getenv-free function call at explicitly instrumented semantic\n// boundaries.  The file is line buffered so a crash still leaves useful evidence.\nvoid semantic_trace_eventf(const char* domain, const char* semantic,\n                           const char* api, const char* fmt, ...) {\n    static std::mutex traceMutex;\n    static bool initialized = false;\n    static FILE* traceFile = nullptr;\n\n    std::lock_guard<std::mutex> lock(traceMutex);\n    if (!initialized) {\n        initialized = true;\n        const char* path = std::getenv("MITHRIL_GL_SEMANTIC_TRACE");\n        if (path && *path) {\n            traceFile = std::fopen(path, "a");\n            if (traceFile) std::setvbuf(traceFile, nullptr, _IOLBF, 0);\n        }\n    }\n    if (!traceFile) return;\n\n    char details[1024] = {};\n    if (fmt && *fmt) {\n        va_list args;\n        va_start(args, fmt);\n        std::vsnprintf(details, sizeof(details), fmt, args);\n        va_end(args);\n    }\n    std::fprintf(traceFile, "%s\\t%s\\t%s\\t%s\\n",\n                 domain ? domain : "", semantic ? semantic : "",\n                 api ? api : "", details);\n}\n'''
    cs = replace_once(cs, anchor, impl, "trace implementation")
    cp.write_text(cs)


def patch_vertex_trace() -> None:
    p = ROOT / "Mithril-Wrapper-cpp/MG_Impl/VertexArray.cpp"
    s = p.read_text()
    entries = [
        (
            'void glVertexAttribPointer(GLuint index, GLint size, GLenum type,\n                           GLboolean normalized, GLsizei stride, const void* pointer) {\n    MITHRIL_ENSURE_INIT();',
            'void glVertexAttribPointer(GLuint index, GLint size, GLenum type,\n                           GLboolean normalized, GLsizei stride, const void* pointer) {\n    MITHRIL_ENSURE_INIT();\n    mithril::semantic_trace_eventf("vao_vertex_fetch", "vertex.legacy.pointer", "glVertexAttribPointer",\n        "attrib=%u;size=%d;type=0x%x;normalized=%u;stride=%d;offset=%llu", index, size, type,\n        (unsigned)normalized, stride, (unsigned long long)(uintptr_t)pointer);',
            "trace glVertexAttribPointer",
        ),
        (
            'void glVertexAttribIPointer(GLuint index, GLint size, GLenum type,\n                            GLsizei stride, const void* pointer) {\n    MITHRIL_ENSURE_INIT();',
            'void glVertexAttribIPointer(GLuint index, GLint size, GLenum type,\n                            GLsizei stride, const void* pointer) {\n    MITHRIL_ENSURE_INIT();\n    mithril::semantic_trace_eventf("vao_vertex_fetch", "vertex.legacy.ipointer", "glVertexAttribIPointer",\n        "attrib=%u;size=%d;type=0x%x;stride=%d;offset=%llu", index, size, type, stride,\n        (unsigned long long)(uintptr_t)pointer);',
            "trace glVertexAttribIPointer",
        ),
        (
            'void glVertexAttribDivisor(GLuint index, GLuint divisor) {\n    MITHRIL_ENSURE_INIT();',
            'void glVertexAttribDivisor(GLuint index, GLuint divisor) {\n    MITHRIL_ENSURE_INIT();\n    mithril::semantic_trace_eventf("vao_vertex_fetch", "vertex.legacy.divisor", "glVertexAttribDivisor",\n        "attrib=%u;divisor=%u", index, divisor);',
            "trace glVertexAttribDivisor",
        ),
        (
            'void glBindVertexBuffer(GLuint bindingindex, GLuint buffer, GLintptr offset, GLsizei stride) {\n    MITHRIL_ENSURE_INIT();',
            'void glBindVertexBuffer(GLuint bindingindex, GLuint buffer, GLintptr offset, GLsizei stride) {\n    MITHRIL_ENSURE_INIT();\n    mithril::semantic_trace_eventf("vao_vertex_fetch", "vertex.bind_buffer", "glBindVertexBuffer",\n        "binding=%u;buffer=%u;offset=%lld;stride=%d", bindingindex, buffer, (long long)offset, stride);',
            "trace glBindVertexBuffer",
        ),
        (
            'void glVertexAttribBinding(GLuint attribindex, GLuint bindingindex) {\n    MITHRIL_ENSURE_INIT();',
            'void glVertexAttribBinding(GLuint attribindex, GLuint bindingindex) {\n    MITHRIL_ENSURE_INIT();\n    mithril::semantic_trace_eventf("vao_vertex_fetch", "vertex.attrib.binding", "glVertexAttribBinding",\n        "attrib=%u;binding=%u", attribindex, bindingindex);',
            "trace glVertexAttribBinding",
        ),
        (
            'void glVertexAttribFormat(GLuint attribindex, GLint size, GLenum type,\n                          GLboolean normalized, GLuint relativeoffset) {\n    MITHRIL_ENSURE_INIT();',
            'void glVertexAttribFormat(GLuint attribindex, GLint size, GLenum type,\n                          GLboolean normalized, GLuint relativeoffset) {\n    MITHRIL_ENSURE_INIT();\n    mithril::semantic_trace_eventf("vao_vertex_fetch", "vertex.attrib.format", "glVertexAttribFormat",\n        "attrib=%u;size=%d;type=0x%x;normalized=%u;relative=%u", attribindex, size, type,\n        (unsigned)normalized, relativeoffset);',
            "trace glVertexAttribFormat",
        ),
        (
            'void glVertexAttribIFormat(GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset) {\n    MITHRIL_ENSURE_INIT();',
            'void glVertexAttribIFormat(GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset) {\n    MITHRIL_ENSURE_INIT();\n    mithril::semantic_trace_eventf("vao_vertex_fetch", "vertex.attrib.iformat", "glVertexAttribIFormat",\n        "attrib=%u;size=%d;type=0x%x;relative=%u", attribindex, size, type, relativeoffset);',
            "trace glVertexAttribIFormat",
        ),
        (
            'void glVertexBindingDivisor(GLuint bindingindex, GLuint divisor) {\n    MITHRIL_ENSURE_INIT();',
            'void glVertexBindingDivisor(GLuint bindingindex, GLuint divisor) {\n    MITHRIL_ENSURE_INIT();\n    mithril::semantic_trace_eventf("vao_vertex_fetch", "vertex.binding.divisor", "glVertexBindingDivisor",\n        "binding=%u;divisor=%u", bindingindex, divisor);',
            "trace glVertexBindingDivisor",
        ),
    ]
    for old, new, label in entries:
        s = replace_once(s, old, new, label)
    p.write_text(s)


def patch_draw_trace() -> None:
    p = ROOT / "Mithril-Wrapper-cpp/MG_Impl/Drawing.cpp"
    s = p.read_text()
    anchor = '''    // Get-or-create the VkGraphicsPipeline. Blend state + colorWriteMask are\n'''
    addition = '''    mithril::semantic_trace_eventf(\n        "draw", "draw.configuration", "prepare_draw",\n        "mode=0x%x;program=%u;vao=%u;fbo=%u;attribs=%d",\n        mode, g_state->currentProgram, g_state->currentVAO,\n        g_state->currentDrawFBO, attrib_count);\n\n'''
    s = replace_once(s, anchor, addition + anchor, "draw trace")
    p.write_text(s)


def patch_e2e_workflow() -> None:
    p = ROOT / ".github/workflows/minecraft-client-e2e.yml"
    s = p.read_text()
    s = replace_once(
        s,
        "      - 'ci/minecraft-on-mithril-e2e-*'\n",
        "      - 'ci/minecraft-on-mithril-e2e-*'\n      - 'fix/gl-semantic-closure-*'\n",
        "E2E branch trigger",
    )
    s = replace_once(
        s,
        '          gradle -p .github/ci/minecraft-e2e --build-cache --stacktrace \\\n',
        '          export MITHRIL_GL_SEMANTIC_TRACE="$E2E_ROOT/gl-semantic-trace.tsv"\n          rm -f "$MITHRIL_GL_SEMANTIC_TRACE"\n\n          gradle -p .github/ci/minecraft-e2e --build-cache --stacktrace \\\n',
        "Minecraft trace env",
    )
    gate_anchor = "      - name: Verify exact active Mithril image and bridge activity\n"
    gate_step = '''      - name: Enforce advertised and observed GL semantic contract\n        shell: bash\n        env:\n          MITHRIL_DYLIB: ${{ steps.mithril.outputs.dylib }}\n        run: |\n          set -euo pipefail\n          export DYLD_LIBRARY_PATH="$(brew --prefix)/lib"\n          python3 ci/e2e/check_gl_semantic_contract.py \\\n            --manifest ci/e2e/gl_semantic_contract.json \\\n            --getter Mithril-Wrapper-cpp/MG_Impl/Getter.cpp \\\n            --dylib "$MITHRIL_DYLIB" \\\n            --trace "$E2E_ROOT/gl-semantic-trace.tsv" \\\n            --require-trace \\\n            --report "$E2E_ROOT/gl-semantic-contract-report.json"\n\n'''
    s = replace_once(s, gate_anchor, gate_step + gate_anchor, "semantic contract gate")
    # Add compact evidence paths wherever wrapper identity is already uploaded.
    s = replace_once(
        s,
        "            build/minecraft-e2e-artifacts/wrapper-identity.json\n",
        "            build/minecraft-e2e-artifacts/wrapper-identity.json\n            build/minecraft-e2e-artifacts/gl-semantic-trace.tsv\n            build/minecraft-e2e-artifacts/gl-semantic-contract-report.json\n",
        "semantic artifacts",
    )
    p.write_text(s)


def main() -> None:
    patch_getter()
    patch_program_uniforms()
    patch_trace_state()
    patch_vertex_trace()
    patch_draw_trace()
    patch_e2e_workflow()
    print("GL semantic closure source transformation complete")


if __name__ == "__main__":
    main()
