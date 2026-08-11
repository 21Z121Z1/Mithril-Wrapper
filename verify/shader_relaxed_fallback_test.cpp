// shader_relaxed_fallback_test.cpp — Verify that the source-level normalization
// passes (normalize_vulkan_incompatible_layouts + normalize_gl_legacy_constructs)
// make the strict EShClientOpenGL + EShMsgVulkanRules path compile shaders that
// previously required the (now-removed) third-level relaxed fallback.
//
// PURPOSE:
//   Shader.cpp's glsl_to_spirv() now uses a single EShClientOpenGL input dialect
//   for all compile attempts. The third-level relaxed fallback (a Vulkan input
//   dialect + relaxed-rules flag) was removed; its coverage is replaced by two
//   idempotent source-level preprocessor passes that run before the strict
//   compile. This test inlines faithful copies of those passes and asserts:
//     1. strict rejects layout(packed)            (precondition: normalization needed)
//     2. strict rejects gl_FragColor              (precondition: normalization needed)
//     3. normalization makes strict compile layout(packed)  (fix)
//     4. normalization makes strict compile gl_FragColor     (fix)
//     5. normalization is idempotent on std140              (no-op, still compiles)
//     6. real MC fragment shader still strict-compiles      (no regression)
//     7. real MC vertex shaders still strict-compile        (no regression)
//
// NOTE: The relaxed fallback was removed and replaced by source-level
// normalization; this test no longer references the Vulkan input dialect or the
// relaxed-rules flag. It links directly against glslang (NOT against Shader.cpp
// / the dylib), so the two normalization passes are inlined below as faithful
// minimal copies and kept in sync manually.
//
// Build (against the glslang submodule):
//   clang++ -std=c++17 -O0 -I <glslang> \
//       -I <glslang>/build-test/External/spirv-tools/include \
//       verify/shader_relaxed_fallback_test.cpp \
//       <glslang>/build-test/glslang/libglslang.a \
//       <glslang>/build-test/glslang/libMachineIndependent.a \
//       <glslang>/build-test/glslang/libGenericCodeGen.a \
//       <glslang>/build-test/glslang/OSDependent/Unix/libOSDependent.a \
//       <glslang>/build-test/SPIRV/libSPIRV.a \
//       <glslang>/build-test/glslang/libglslang-default-resource-limits.a \
//       -lpthread -o shader_relaxed_fallback_test
//
// Exit code 0 => all checks pass (normalization verified, no regression).
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>
#include <cstdio>
#include <regex>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Inlined normalization helpers — mirrors Shader.cpp's normalize_* functions.
// Kept in sync manually: the test links directly against glslang, NOT against
// Shader.cpp / the dylib, so the passes must be copied here to exercise the
// full normalize -> strict-compile path. If Shader.cpp's passes change, update
// these copies.
// ---------------------------------------------------------------------------

// Mirrors Shader.cpp's is_in_comment — true if `off` sits inside a // or /* */
// comment.
static bool is_in_comment(const std::string& s, size_t off) {
    // Line comment: any "//" between line start and off?
    size_t line_start = s.rfind('\n', off);
    line_start = (line_start == std::string::npos) ? 0 : line_start + 1;
    if (s.find("//", line_start) < off) return true;
    // Block comment: count unterminated /* before off.
    int depth = 0;
    for (size_t i = 0; i < off; ++i) {
        if (i + 1 < off && s[i] == '/' && s[i + 1] == '*') { ++depth; ++i; }
        else if (i + 1 < off && s[i] == '*' && s[i + 1] == '/') { if (depth) --depth; ++i; }
    }
    return depth > 0;
}

// Mirrors Shader.cpp's normalize_vulkan_incompatible_layouts — rewrites
// layout(packed)/layout(shared) UBO -> layout(std140), SSBO -> layout(std430).
// Idempotent (std140/std430/other qualifiers untouched) and comment-aware.
static void normalize_vulkan_incompatible_layouts(std::string& source) {
    // Fast path: skip if source has no packed/shared qualifiers at all.
    if (source.find("packed") == std::string::npos &&
        source.find("shared") == std::string::npos) {
        return;
    }

    // Match `layout(<args>) <storage>` where <storage> is uniform (UBO) or
    // buffer (SSBO). [^)]* is safe: GLSL layout() never contains nested parens.
    static const std::regex re(
        R"(layout\s*\(([^)]*)\)\s*(uniform|buffer)\b)",
        std::regex::optimize | std::regex::multiline);

    // Helper: trim a single arg; if it equals "packed"/"shared", return the
    // appropriate std replacement; otherwise return trimmed arg unchanged.
    auto process_arg = [](const std::string& arg, const std::string& replacement,
                          bool* changed) -> std::string {
        size_t b = arg.find_first_not_of(" \t");
        size_t e = arg.find_last_not_of(" \t");
        if (b == std::string::npos) return std::string();  // whitespace-only
        std::string trimmed = arg.substr(b, e - b + 1);
        if (trimmed == "packed" || trimmed == "shared") {
            *changed = true;
            return replacement;
        }
        return trimmed;
    };

    std::string out;
    out.reserve(source.size());
    std::string::const_iterator it = source.cbegin();
    std::smatch m;
    while (std::regex_search(it, source.cend(), m, re)) {
        size_t match_pos = m.position(0) + (it - source.cbegin());
        // Skip occurrences inside // or /* */ comments.
        if (is_in_comment(source, match_pos)) {
            out.append(it, m[0].second);
            it = m[0].second;
            continue;
        }

        std::string args = m[1].str();
        std::string storage = m[2].str();
        std::string replacement = (storage == "uniform") ? "std140" : "std430";

        // Split args by comma; replace packed/shared with the appropriate std
        // version. Track whether any replacement happened so non-matching
        // layout() qualifiers are left untouched (idempotent).
        std::string new_args;
        bool changed = false;
        std::string cur;
        auto flush = [&]() {
            std::string processed = process_arg(cur, replacement, &changed);
            if (!processed.empty()) {
                if (!new_args.empty()) new_args += ", ";
                new_args += processed;
            }
            cur.clear();
        };
        for (char c : args) {
            if (c == ',') flush();
            else cur += c;
        }
        flush();

        if (!changed) {
            // No packed/shared in this layout() — leave it untouched.
            out.append(it, m[0].second);
        } else {
            // Reconstruct: `layout(<new_args>) <storage>`.
            out.append(it, m[0].first);
            out += "layout(";
            out += new_args;
            out += ") ";
            out += storage;
        }
        it = m[0].second;
    }
    out.append(it, source.cend());
    source.swap(out);
}

// Mirrors Shader.cpp's normalize_gl_legacy_constructs — fragment-only; injects
// `layout(location = 0) out vec4 _mithril_FragColor;` right after the #version
// line and rewrites all word-boundary gl_FragColor references (outside comments)
// to _mithril_FragColor. Idempotent (no-op if gl_FragColor is absent or only in
// comments; a shader that already declares its own output is untouched).
//
// Signature adapted: takes EShLanguage stage instead of GLenum (this test does
// not include GL headers); the gate `stage == EShLangFragment` is equivalent to
// Shader.cpp's `gl_stage == GL_FRAGMENT_SHADER`.
static void normalize_gl_legacy_constructs(std::string& source, EShLanguage stage) {
    // Only fragment shaders use gl_FragColor; vertex shaders are unaffected.
    if (stage != EShLangFragment) return;

    // Fast path: if gl_FragColor doesn't appear at all, no work to do.
    if (source.find("gl_FragColor") == std::string::npos) return;

    static const std::regex re(R"(\bgl_FragColor\b)", std::regex::optimize);

    // First pass: rewrite all word-boundary gl_FragColor references outside
    // comments to _mithril_FragColor. Track whether any rewrite happened so we
    // can skip the declaration injection if gl_FragColor only appears in
    // comments (idempotent no-op).
    std::string out;
    out.reserve(source.size());
    std::string::const_iterator it = source.cbegin();
    std::smatch m;
    bool rewritten = false;
    while (std::regex_search(it, source.cend(), m, re)) {
        size_t match_pos = m.position(0) + (it - source.cbegin());
        out.append(it, m[0].first);
        if (is_in_comment(source, match_pos)) {
            out.append(m[0].str());
        } else {
            out.append("_mithril_FragColor");
            rewritten = true;
        }
        it = m[0].second;
    }
    out.append(it, source.cend());
    if (!rewritten) return;
    source.swap(out);

    // Inject synthetic declaration right after the #version line so the
    // rewritten references resolve to a Vulkan-legal named output.
    size_t vp = source.find("#version");
    size_t insert_at = 0;
    if (vp != std::string::npos) {
        size_t nl = source.find('\n', vp);
        insert_at = (nl != std::string::npos) ? nl + 1 : source.size();
    }
    source.insert(insert_at, "layout(location = 0) out vec4 _mithril_FragColor;\n");
}

// Strict compile path — mirrors Shader.cpp's glsl_to_spirv strict config:
// EShClientOpenGL input + client, SPIR-V 1.5 target, Vulkan rules messages,
// auto-mapped locations and bindings. Returns true on successful parse+link+
// SPIR-V emission.
static bool strict_compile(EShLanguage stage, int glsl_version,
                           const std::string& src, std::string& err) {
    glslang::TShader sh(stage);
    const char* s = src.c_str();
    sh.setStrings(&s, 1);
    sh.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientOpenGL, glsl_version);
    sh.setEnvClient(glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450);
    sh.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);
    sh.setAutoMapLocations(true);
    sh.setAutoMapBindings(true);
    auto msgs = (EShMessages)(EShMsgDefault | EShMsgSpvRules | EShMsgVulkanRules);
    if (!sh.parse(GetDefaultResources(), glsl_version, true, msgs)) {
        err = sh.getInfoLog();
        return false;
    }
    glslang::TProgram prog;
    prog.addShader(&sh);
    if (!prog.link(msgs)) { err = prog.getInfoLog(); return false; }
    std::vector<unsigned int> spirv;
    glslang::GlslangToSpv(*prog.getIntermediate(stage), spirv);
    return !spirv.empty();
}

int main() {
    glslang::InitializeProcess();
    int failures = 0, checks = 0;
#define CHECK(cond, fmt, ...) do { ++checks; if (cond) printf("ok  : " fmt "\n", ##__VA_ARGS__); \
    else { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; } } while (0)

    const int GLSL_VER = 420;
    std::string err;

    // ============================================================
    // Case 1: Precondition — strict rejects layout(packed).
    // Proves normalization is needed: strict EShClientOpenGL +
    // EShMsgVulkanRules must fail on a fragment shader with a
    // layout(packed) UBO.
    // ============================================================
    const std::string packed_ubo =
        "#version 420 core\n"
        "layout(packed) uniform Matrices {\n    mat4 MVP;\n} _m;\n"
        "in vec2 texCoord0;\n"
        "out vec4 fragColor;\n"
        "void main() { fragColor = vec4(texCoord0 * _m.MVP[3].xy, 0.0, 1.0); }\n";
    {
        bool strict_packed = strict_compile(EShLangFragment, GLSL_VER, packed_ubo, err);
        printf("Case 1 strict layout(packed)            : %s%s\n",
               strict_packed ? "OK" : "FAIL",
               strict_packed ? "" : (" - " + err).c_str());
        CHECK(!strict_packed,
              "Case 1: strict must reject layout(packed) (precondition: normalization needed)");
    }

    // ============================================================
    // Case 2: Precondition — strict rejects gl_FragColor.
    // Proves normalization is needed: strict EShClientOpenGL +
    // EShMsgVulkanRules must fail on a fragment shader using the
    // GL-legacy gl_FragColor output.
    // ============================================================
    const std::string fragcolor_src =
        "#version 420 core\n"
        "in vec2 texCoord0;\n"
        "void main() { gl_FragColor = vec4(texCoord0, 0.0, 1.0); }\n";
    {
        bool strict_fc = strict_compile(EShLangFragment, GLSL_VER, fragcolor_src, err);
        printf("Case 2 strict gl_FragColor              : %s%s\n",
               strict_fc ? "OK" : "FAIL",
               strict_fc ? "" : (" - " + err).c_str());
        CHECK(!strict_fc,
              "Case 2: strict must reject gl_FragColor (precondition: normalization needed)");
    }

    // ============================================================
    // Case 3: Fix — normalization makes strict compile layout(packed).
    // Apply normalize_vulkan_incompatible_layouts to the Case-1 source,
    // then strict-compile. Must succeed and emit non-empty SPIR-V. Verify
    // the normalized source contains layout(std140) and not layout(packed).
    // ============================================================
    {
        std::string normalized = packed_ubo;
        normalize_vulkan_incompatible_layouts(normalized);
        bool has_std140 = normalized.find("layout(std140)") != std::string::npos;
        bool has_packed = normalized.find("layout(packed)") != std::string::npos;
        bool ok = strict_compile(EShLangFragment, GLSL_VER, normalized, err);
        printf("Case 3 normalize layout(packed)->std140 : %s%s\n",
               ok ? "OK" : "FAIL", ok ? "" : (" - " + err).c_str());
        CHECK(has_std140 && !has_packed,
              "Case 3: normalized source must contain layout(std140) and not layout(packed)");
        CHECK(ok, "Case 3: strict must compile normalized layout(packed) source and emit SPIR-V");
    }

    // ============================================================
    // Case 4: Fix — normalization makes strict compile gl_FragColor.
    // Apply normalize_gl_legacy_constructs (fragment stage) to the Case-2
    // source, then strict-compile. Must succeed. Verify the normalized
    // source contains _mithril_FragColor and the synthetic declaration
    // `layout(location = 0) out vec4 _mithril_FragColor;`.
    // ============================================================
    {
        std::string normalized = fragcolor_src;
        normalize_gl_legacy_constructs(normalized, EShLangFragment);
        bool has_sym = normalized.find("_mithril_FragColor") != std::string::npos;
        bool has_decl = normalized.find(
            "layout(location = 0) out vec4 _mithril_FragColor;") != std::string::npos;
        bool ok = strict_compile(EShLangFragment, GLSL_VER, normalized, err);
        printf("Case 4 normalize gl_FragColor->_mithril : %s%s\n",
               ok ? "OK" : "FAIL", ok ? "" : (" - " + err).c_str());
        CHECK(has_sym && has_decl,
              "Case 4: normalized source must contain _mithril_FragColor and its synthetic declaration");
        CHECK(ok, "Case 4: strict must compile normalized gl_FragColor source and emit SPIR-V");
    }

    // ============================================================
    // Case 5: Idempotency — std140 untouched.
    // Apply normalize_vulkan_incompatible_layouts to a shader already using
    // layout(std140). The source must be unchanged (before == after), and
    // strict-compile must succeed.
    // ============================================================
    const std::string already_std140 =
        "#version 420 core\n"
        "layout(std140) uniform Matrices {\n    mat4 MVP;\n} _m;\n"
        "in vec2 texCoord0;\n"
        "out vec4 fragColor;\n"
        "void main() { fragColor = vec4(texCoord0 * _m.MVP[3].xy, 0.0, 1.0); }\n";
    {
        std::string normalized = already_std140;
        normalize_vulkan_incompatible_layouts(normalized);
        bool unchanged = (normalized == already_std140);
        bool ok = strict_compile(EShLangFragment, GLSL_VER, normalized, err);
        printf("Case 5 idempotent std140                : %s%s\n",
               ok ? "OK" : "FAIL", ok ? "" : (" - " + err).c_str());
        CHECK(unchanged, "Case 5: layout(std140) source must be unchanged by normalization");
        CHECK(ok, "Case 5: strict must compile idempotent std140 source");
    }

    // ============================================================
    // Case 6: No regression — real Minecraft fragment shader.
    // rendertype_text.frag with mithril_GlobalBlock + Sampler0 must still
    // strict-compile successfully AFTER normalization. (Source kept from
    // the old test.)
    // ============================================================
    const std::string mc_text_fs =
        "#version 420 core\n"
        "uniform mithril_GlobalBlock {\n"
        "    vec4 ColorModulator;\n    float FogStart;\n    float FogEnd;\n    vec4 FogColor;\n"
        "} _m;\n"
        "#define ColorModulator _m.ColorModulator\n"
        "#define FogStart _m.FogStart\n"
        "#define FogEnd _m.FogEnd\n"
        "#define FogColor _m.FogColor\n"
        "in vec2 texCoord0;\nin vec2 texCoord2;\n"
        "uniform sampler2D Sampler0;\n"
        "out vec4 fragColor;\n"
        "void main(){ vec4 c=texture(Sampler0,texCoord0)*ColorModulator; "
        "c=mix(c,FogColor,clamp((FogEnd-gl_FragCoord.z)/(FogEnd-FogStart),0.0,1.0)); fragColor=c; }\n";
    {
        std::string normalized = mc_text_fs;
        normalize_vulkan_incompatible_layouts(normalized);
        normalize_gl_legacy_constructs(normalized, EShLangFragment);
        bool ok = strict_compile(EShLangFragment, GLSL_VER, normalized, err);
        printf("Case 6 MC text.frag strict-compile      : %s%s\n",
               ok ? "OK" : "FAIL", ok ? "" : (" - " + err).c_str());
        CHECK(ok, "Case 6: no regression: real MC fragment shader strict-compiles after normalization");
    }

    // ============================================================
    // Case 7: No regression — real Minecraft vertex shaders.
    // mc_text_vs (plain) and mc_lines_vs (uses gl_VertexID) must still
    // strict-compile successfully AFTER normalization.
    //
    // PRE-WRAPPED SOURCES: Both VS sources below are written in the
    // post-wrap_loose_uniforms() form that Shader.cpp's real pipeline produces
    // BEFORE normalization/compile — i.e. their loose non-opaque uniforms are
    // already folded into a `uniform mithril_GlobalBlock { ... } _m;` UBO
    // block injected right after #version, with `#define <name> _m.<name>`
    // renames so the shader body still references the original identifiers.
    // Opaque uniforms (e.g. mc_text_vs's `sampler2D Sampler2`) stay standalone
    // — they cannot sit in a UBO. This mirrors exactly what mc_text_fs (Case 6)
    // already does. The reason for pre-wrapping here: under strict
    // EShClientOpenGL + EShMsgVulkanRules, glslang rejects loose non-opaque
    // `uniform` declarations (only block uniforms / opaque samplers are
    // allowed); Shader.cpp's wrap_loose_uniforms() fixes that upstream, but
    // this test does NOT inline wrap_loose_uniforms (only the two normalize_*
    // passes), so feeding the raw loose-uniform sources would fail
    // non-deterministically depending on glslang's auto-wrap behavior. Pre-
    // wrapping makes the strict-compile deterministic and faithful to the real
    // pipeline's input to glslang.
    //
    // NOTE on gl_VertexID: Shader.cpp's rewrite_desktop_builtins() renames
    // gl_VertexID -> gl_VertexIndex (and gl_InstanceID -> gl_InstanceIndex)
    // upstream before normalization/compile, because gl_VertexID is not a
    // Vulkan GLSL builtin and glslang (EShClientOpenGL + EShMsgVulkanRules)
    // rejects it with "'gl_VertexID' : undeclared identifier". That rewrite is
    // NOT part of this test's scope (we only inline the two normalization
    // passes). The mc_lines_vs source below therefore intentionally KEEPS
    // `gl_VertexID` in the body; the pre-rewrite step inside the test (see
    // below, just before strict_compile) converts it to gl_VertexIndex,
    // mirroring what Shader.cpp does upstream. Do NOT remove that pre-rewrite
    // step.
    // ============================================================
    const std::string mc_text_vs =
        "#version 420 core\n"
        "uniform mithril_GlobalBlock {\n"
        "    mat4 ModelViewMat;\n    mat4 ProjMat;\n"
        "} _m;\n"
        "#define ModelViewMat _m.ModelViewMat\n"
        "#define ProjMat _m.ProjMat\n"
        "in vec3 Position;\nin vec2 UV0;\nin vec2 UV2;\n"
        "uniform sampler2D Sampler2;\n"
        "out vec2 texCoord0;\nout vec2 texCoord2;\n"
        "void main(){ gl_Position=ProjMat*ModelViewMat*vec4(Position,1.0); texCoord0=UV0; texCoord2=UV2; }\n";
    const std::string mc_lines_vs =
        "#version 420 core\n"
        "uniform mithril_GlobalBlock {\n"
        "    mat4 ModelViewMat;\n    mat4 ProjMat;\n    vec4 ColorModulator;\n"
        "} _m;\n"
        "#define ModelViewMat _m.ModelViewMat\n"
        "#define ProjMat _m.ProjMat\n"
        "#define ColorModulator _m.ColorModulator\n"
        "in vec3 Position;\nin vec4 Color;\nin vec3 Normal;\n"
        "out vec4 vertexColor;\n"
        "void main(){ int id=gl_VertexID; vec4 p=ProjMat*ModelViewMat*vec4(Position,1.0); "
        "gl_Position=p; vertexColor=Color*ColorModulator; }\n";
    {
        // Pre-rename gl_VertexID -> gl_VertexIndex (mirrors rewrite_desktop_builtins).
        std::string lines_normalized = mc_lines_vs;
        {
            static const std::regex vre(R"(\bgl_VertexID\b)", std::regex::optimize);
            lines_normalized = std::regex_replace(lines_normalized, vre, "gl_VertexIndex");
        }
        normalize_vulkan_incompatible_layouts(lines_normalized);
        normalize_gl_legacy_constructs(lines_normalized, EShLangVertex);

        std::string text_normalized = mc_text_vs;
        normalize_vulkan_incompatible_layouts(text_normalized);
        normalize_gl_legacy_constructs(text_normalized, EShLangVertex);

        bool vs_ok = strict_compile(EShLangVertex, GLSL_VER, text_normalized, err);
        printf("Case 7 MC text.vert strict-compile      : %s%s\n",
               vs_ok ? "OK" : "FAIL", vs_ok ? "" : (" - " + err).c_str());
        bool lines_ok = strict_compile(EShLangVertex, GLSL_VER, lines_normalized, err);
        printf("Case 7 MC lines.vert strict-compile     : %s%s\n",
               lines_ok ? "OK" : "FAIL", lines_ok ? "" : (" - " + err).c_str());
        CHECK(vs_ok,
              "Case 7: no regression: pre-wrapped MC text vertex shader strict-compiles after normalization");
        CHECK(lines_ok,
              "Case 7: no regression: pre-wrapped MC lines vertex shader (gl_VertexID renamed) strict-compiles after normalization");
    }

    printf("\nchecks=%d failures=%d\n", checks, failures);
    glslang::FinalizeProcess();
    return failures ? 1 : 0;
}
