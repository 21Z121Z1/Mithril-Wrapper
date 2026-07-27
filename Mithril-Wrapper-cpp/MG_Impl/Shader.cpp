// Mithril-Wrapper - MG_Impl/Shader.cpp
// GLSL (desktop Core Profile) -> Vulkan SPIR-V translation via glslang.
//
// Pipeline:
//   1. Preprocess: inject MG_MITHRIL / MG_MITHRIL_VERSION macros so host
//      shaders can branch on the Mithril backend (mirrors MobileGlues'
//      MG_MOBILEGLUES injection). Upgrade GLSL versions below 330 (the Vulkan
//      GLSL minimum) so desktop GLSL 150 shaders like Minecraft's blit_screen
//      compile under the Vulkan client.
//   2. Inject layout(location=N) into vertex `in` declarations from
//      glBindAttribLocation mappings so the SPIR-V stage_input locations match
//      the application's vertex descriptor.
//   3. Preprocess: fold loose non-opaque uniforms into a synthetic
//      `mithril_GlobalBlock` UBO (mirroring ANGLE's ANGLE_DefaultUniformBlock).
//      Handles precision qualifiers, multi-dimensional arrays, multiple
//      declarators, named and anonymous struct uniforms, and skips
//      declarations inside comments. The #define renames let the shader body
//      reference members by their original names without a block prefix.
//      This avoids the "non-opaque uniforms outside a block" error that some
//      glslang versions emit even with EShClientOpenGL + EShMsgVulkanRules.
//   4. glslang compiles the GLSL to Vulkan SPIR-V via the GL_KHR_vulkan_glsl
//      path: EShClientOpenGL + EShMsgVulkanRules. Because step 3 already
//      wrapped loose uniforms, glslang never needs the auto-wrap path — it
//      sees only block uniforms and opaque samplers. The emitted SPIR-V stays
//      Vulkan-conformant (MoltenVK accepts it). The synthetic block name
//      "mithril_GlobalBlock" does not match any GL uniform name, so
//      DescriptorSet.cpp falls through to member-by-member packing (same
//      $Global convention), which works identically.
//      setAutoMapLocations(true) + setAutoMapBindings(true) auto-assign any
//      remaining locations/bindings.
//   5. The SPIR-V words are returned directly — MoltenVK cross-translates
//      Vulkan SPIR-V to MSL internally at vkCreateShaderModule time, so no
//      SPIRV-Cross stage is needed here.
#include "Shader.h"
#include "Log.h"

#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>

#include <cstdint>
#include <mutex>
#include <regex>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace mithril {
namespace {

struct GlslangInit {
    GlslangInit()  { glslang::InitializeProcess(); }
    ~GlslangInit() { /* process-lifetime; no finalize needed */ }
};
GlslangInit& glslang_init() {
    static GlslangInit g;
    return g;
}

EShLanguage to_esh_stage(GLenum gl) {
    switch (gl) {
        case GL_VERTEX_SHADER:          return EShLangVertex;
        case GL_FRAGMENT_SHADER:        return EShLangFragment;
        case GL_GEOMETRY_SHADER:        return EShLangGeometry;
        case GL_TESS_CONTROL_SHADER:    return EShLangTessControl;
        case GL_TESS_EVALUATION_SHADER: return EShLangTessEvaluation;
        case GL_COMPUTE_SHADER:         return EShLangCompute;
        default:                        return EShLangCount;
    }
}

// Extract the GLSL #version number. Returns -1 if not found.
int get_glsl_version(const std::string& src) {
    static std::regex version_pattern(R"(#version\s+(\d{3}))");
    std::smatch match;
    if (std::regex_search(src, match, version_pattern)) {
        return std::stoi(match[1].str());
    }
    return -1;
}

/*
 * Ensure the GLSL source has a version usable by the Vulkan client. Vulkan
 * GLSL requires #version 330 minimum (GL_KHR_vulkan_glsl). Minecraft's
 * blit_screen uses #version 150, so we upgrade anything below 330 to 330
 * (core profile). If no #version line is present, prepend #version 330.
 *
 * Returns the resolved GLSL version number.
 */
int ensure_glsl_version(std::string& src) {
    int ver = get_glsl_version(src);
    if (ver == -1) {
        ver = 330;
        src.insert(0, "#version 330 core\n");
        return ver;
    }
    if (ver < 330) {
        // Replace the existing #version line with #version 330 core. The
        // 'core' profile is required for Vulkan GLSL; 'compatibility' would
        // pull in deprecated fixed-function symbols that Vulkan rejects.
        size_t pos = src.find("#version");
        size_t line_end = src.find('\n', pos);
        if (line_end == std::string::npos) line_end = src.length();
        // Preserve any trailing profile token that was on the line by
        // replacing the whole line with the upgraded version + core.
        src.replace(pos, line_end - pos, "#version 330 core");
        ver = 330;
    } else {
        // Ensure a profile token is present; Vulkan GLSL requires core.
        size_t pos = src.find("#version");
        size_t line_end = src.find('\n', pos);
        if (line_end == std::string::npos) line_end = src.length();
        std::string line = src.substr(pos, line_end - pos);
        if (line.find("core") == std::string::npos &&
            line.find("compatibility") == std::string::npos &&
            line.find("es") == std::string::npos) {
            // No profile token; append ' core'.
            src.replace(pos, line_end - pos, line + " core");
        }
    }
    return ver;
}

/*
 * Inject `layout(location=N)` qualifiers into GLSL `in` declarations based on
 * the application's glBindAttribLocation() mappings. Minecraft 1.21 shaders
 * use bare `in vec3 Position;` declarations and rely on glBindAttribLocation
 * to assign locations at runtime. Even with setAutoMapLocations(true), we pin
 * the locations explicitly so the SPIR-V stage_input locations match the
 * application's vertex descriptor.
 *
 * Only vertex shaders are affected. The rewrite is conservative: it matches
 * declarations of the form `in <type> <name>;` and skips lines that already
 * have a layout() qualifier.
 */
void apply_attrib_bindings(std::string& src, GLenum gl_stage,
                           const std::unordered_map<std::string, GLuint>* bindings) {
    if (!bindings || bindings->empty()) return;
    if (gl_stage != GL_VERTEX_SHADER) return;

    static std::regex in_decl_re(
        R"(^\s*(?:layout\s*\([^)]*\)\s*)?(in|attribute)\s+(\w+)\s+(\w+)\s*(\[[^\]]*\])?\s*;)",
        std::regex::optimize | std::regex::multiline);

    std::string out;
    out.reserve(src.size() + bindings->size() * 24);
    std::string::const_iterator search_start(src.cbegin());
    std::smatch m;
    size_t last_pos = 0;

    while (std::regex_search(search_start, src.cend(), m, in_decl_re)) {
        size_t match_pos = m.position(0) + (search_start - src.cbegin());
        out.append(src, last_pos, match_pos - last_pos);

        const std::string& keyword = m[1].str();   // "in" or "attribute"
        const std::string& vartype = m[2].str();
        const std::string& varname = m[3].str();
        const std::string& array_suffix = m[4].matched ? m[4].str() : std::string();
        (void)keyword;

        auto it = bindings->find(varname);
        if (it != bindings->end()) {
            out += "layout(location=";
            out += std::to_string(it->second);
            out += ") in ";
            out += vartype;
            out += ' ';
            out += varname;
            if (!array_suffix.empty()) out += array_suffix;
            out += ';';
        } else {
            out += m[0].str();
        }

        last_pos = match_pos + m[0].length();
        search_start = m.suffix().first;
    }
    out.append(src, last_pos, std::string::npos);
    src.swap(out);
}

// ---------------------------------------------------------------------------
// Opaque GLSL type detection: types that MUST remain as standalone uniform
// declarations (samplers, images, atomic counters, subpass inputs) because
// they cannot be placed inside a uniform block.
// ---------------------------------------------------------------------------
static bool is_opaque_glsl_type(const std::string& name) {
    // All sampler types contain "sampler" in the name (sampler2D, isampler2D,
    // usampler2D, samplerCube, sampler2DArray, sampler2DShadow, etc.).
    if (name.find("sampler") != std::string::npos) return true;
    // All image types contain "image" (image2D, iimage2D, uimage2D, etc.).
    if (name.find("image")   != std::string::npos) return true;
    // Atomic counters and subpass inputs.
    if (name == "atomic_uint") return true;
    if (name.find("subpass") != std::string::npos) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Loose-uniform collection: fold every non-block non-opaque uniform into a
// synthetic UBO, mirroring how ANGLE's CollectVariables folds default uniforms
// into ANGLE_DefaultUniformBlock. The GL_KHR_vulkan_glsl path *should* do
// this automatically via EShClientOpenGL + EShMsgVulkanRules wrapping into
// $Global, but some glslang versions still reject the shader with
// "non-opaque uniforms outside a block". We stay deterministic by doing the
// wrapping ourselves as a preprocessor pass before glslang sees the source.
//
// Forms folded (this is the superset ANGLE collects, minus interface blocks
// and SSBOs which stay as-is):
//   uniform mat4 M;                    single declaration
//   uniform mat4 A, B;                 multiple declarators on one line
//   uniform highp float f;             precision qualifier
//   uniform mat4 arr[8];               (multi-dimensional) arrays
//   uniform MyStruct s;                named struct-typed uniform
//   uniform struct { mat4 a; } u;      anonymous inline struct uniform
//
// All collected declarations are removed from their original locations and
// re-emitted inside a single `mithril_GlobalBlock` UBO injected right after
// #version, with `#define <name> mithril_GlobalBlock.<name>` so the body keeps
// using the original identifier. Opaque uniforms (samplers, images,
// atomic_uint, subpass inputs) remain standalone — they cannot sit in a block.
// Shaders with no loose non-opaque uniforms are left untouched (no-op scan).
// Declarations inside // or /* */ comments are ignored.
// ---------------------------------------------------------------------------

// Find the '}' that closes the '{' at open_idx (brace-depth scan).
static size_t find_matching_brace(const std::string& s, size_t open_idx) {
    int depth = 0;
    for (size_t i = open_idx; i < s.size(); ++i) {
        if (s[i] == '{')       ++depth;
        else if (s[i] == '}') { --depth; if (depth == 0) return i; }
    }
    return std::string::npos;
}

// True if offset `off` sits inside a // or /* */ comment.
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

// Split a declarator list ("a, b[2], c") into individual trimmed declarators,
// tracking [] depth so commas inside array sizes are ignored.
static void split_declarators(const std::string& list,
                              std::vector<std::string>& out) {
    std::string cur;
    int depth = 0;
    for (char c : list) {
        if (c == '[')      ++depth;
        else if (c == ']') { if (depth) --depth; }
        if (c == ',' && depth == 0) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
}

// Extract (name, arraySuffix) from a single declarator "arr[2][3]".
static bool parse_declarator(const std::string& d, std::string& name,
                             std::string& arr) {
    static const std::regex nm_re(R"(^\s*(\w+)\s*((?:\[[^\]]*\])*)\s*$)");
    std::smatch m;
    if (!std::regex_match(d, m, nm_re)) return false;
    name = m[1].str();
    arr  = m[2].str();
    return true;
}
static void wrap_loose_uniforms(std::string& source) {
    struct Member { std::string decl; std::string name; };
    struct Erase  { size_t pos; size_t len; };
    std::vector<Member> members;
    std::vector<Erase>  erases;

    // --- Pass A: simple / named-struct / precision / array / multi-declarator.
    //     uniform [layout(...)] [prec] <type> <decllist> ;
    static const std::regex simple_re(
        R"(^[ \t]*((?:layout\s*\([^)]*\)\s*)?uniform\s+(?!struct\b)(?:(?:highp|mediump|lowp)\s+)?(\w+)\s+([^;]+?)\s*;))",
        std::regex::multiline | std::regex::optimize);

    {
        auto cur = source.cbegin(), end = source.cend();
        std::smatch m;
        while (std::regex_search(cur, end, m, simple_re)) {
            size_t off  = m.position(0) + (cur - source.cbegin());
            size_t full = m[1].str().size();
            std::string type      = m[2].str();
            std::string decllist  = m[3].str();
            cur = m.suffix().first;

            if (is_in_comment(source, off)) continue;
            if (is_opaque_glsl_type(type))  continue;

            std::vector<std::string> names;
            split_declarators(decllist, names);
            for (const auto& n : names) {
                std::string var, arr;
                if (parse_declarator(n, var, arr)) {
                    members.push_back({type + " " + var + arr, var});
                    erases.push_back({off, full});
                }
            }
        }
    }

    // --- Pass B: anonymous / inline / named struct uniforms.
    //     uniform struct [Name] { ... } <decllist> ;
    static const std::regex struct_re(
        R"(^[ \t]*(uniform\s+struct\s+(\w+)?\s*\{))",
        std::regex::multiline | std::regex::optimize);

    {
        auto cur = source.cbegin(), end = source.cend();
        std::smatch m;
        while (std::regex_search(cur, end, m, struct_re)) {
            size_t off     = m.position(0) + (cur - source.cbegin());
            size_t brace   = off + m[1].str().size() - 1;
            bool  named    = m[2].matched;
            std::string struct_name = named ? m[2].str() : "";
            size_t close   = find_matching_brace(source, brace);
            cur = m.suffix().first;
            if (close == std::string::npos) continue;
            if (is_in_comment(source, off)) continue;

            std::string struct_def = source.substr(brace, close - brace + 1);
            size_t after = close + 1;
            size_t semi  = source.find(';', after);
            if (semi == std::string::npos) continue;
            std::string decllist = source.substr(after, semi - after);

            std::vector<std::string> names;
            split_declarators(decllist, names);
            for (const auto& n : names) {
                std::string var, arr;
                if (!parse_declarator(n, var, arr)) continue;
                if (named) {
                    // Keep "struct Name { ... };" globally; drop "uniform " and
                    // the trailing declarator so only the type definition survives.
                    erases.push_back({off, 7});                  // "uniform "
                    erases.push_back({after, semi - after + 1}); // " u;"
                    members.push_back({struct_name + " " + var + arr, var});
                } else {
                    erases.push_back({off, semi - off + 1});
                    members.push_back({struct_def + " " + var + arr, var});
                }
            }
        }
    }

    if (members.empty()) return;

    // Insertion point: right after #version (ensure_glsl_version guarantees it).
    size_t version_end = 0;
    {
        auto vp = source.find("#version");
        if (vp != std::string::npos) {
            auto nl = source.find('\n', vp);
            version_end = (nl != std::string::npos) ? nl + 1 : source.size();
        }
    }

    std::string injection;
    injection += "\nuniform mithril_GlobalBlock {\n";
    for (const auto& u : members)
        injection += "    " + u.decl + ";\n";
    injection += "} _m;\n\n";
    for (const auto& u : members)
        injection += "#define " + u.name + " _m." + u.name + "\n";
    injection += "\n";

    // Erase originals (descending position so earlier offsets stay valid).
    std::sort(erases.begin(), erases.end(),
              [](const Erase& a, const Erase& b) { return a.pos > b.pos; });
    for (const auto& e : erases)
        source.erase(e.pos, e.len);

    source.insert(version_end, injection);
}

// FNV-1a 64-bit hash for cache keying.
uint64_t fnv1a(const std::string& s) {
    uint64_t h = 1469598103934665603ULL;
    for (char c : s) { h ^= (uint8_t)c; h *= 1099511628211ULL; }
    return h;
}

struct Cache {
    std::mutex mu;
    std::unordered_map<uint64_t, std::vector<uint32_t>> entries; // key -> SPIR-V
};
Cache& cache() { static Cache c; return c; }

bool glsl_to_spirv(GLenum gl_stage, const std::string& src,
                   std::vector<uint32_t>& spirv, std::string& info,
                   const std::unordered_map<std::string, GLuint>* attrib_bindings) {
    glslang_init();
    EShLanguage stage = to_esh_stage(gl_stage);
    if (stage == EShLangCount) { info = "unsupported shader stage"; return false; }

    // Preprocess: upgrade GLSL version (Vulkan requires 330+), inject
    // attribute location bindings, and wrap loose non-opaque uniforms into
    // a synthetic UBO so glslang produces Vulkan-conformant SPIR-V.
    std::string source = src;
    int glsl_version = ensure_glsl_version(source);
    apply_attrib_bindings(source, gl_stage, attrib_bindings);
    wrap_loose_uniforms(source);

    glslang::TShader shader(stage);
    const char* s = source.c_str();
    shader.setStrings(&s, 1);

    // GL_KHR_vulkan_glsl path: parse as OpenGL GLSL but emit Vulkan SPIR-V.
    // EShClientOpenGL (NOT EShClientVulkan) is required — the Vulkan client
    // forbids non-block uniforms outright. However, the loose uniforms have
    // already been wrapped into a synthetic block by wrap_loose_uniforms()
    // above (step 3 of the pipeline comment), so glslang never encounters
    // them unadorned. The EShMsgVulkanRules flag + EShClientOpenGL pair is
    // kept as a belt-and-suspenders safety net: if any loose non-opaque
    // uniform slips through (e.g. a type the regex did not recognise), the
    // auto-wrap code path will still protect it. The OpenGL client also keeps
    // desktop GLSL builtin semantics (e.g. gl_VertexID 1-based, gl_InstanceID
    // 1-based).
    // Target OpenGL 4.50 feature level (a superset of Minecraft's GLSL 150-330)
    // and emit SPIR-V 1.5 (paired with Vulkan 1.2).
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientOpenGL, glsl_version);
    shader.setEnvClient(glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);

    // Auto-assign locations/bindings for any `in`/`out`/uniform declarations
    // that lack explicit layout() qualifiers. This lets desktop GLSL 330
    // shaders written without Vulkan qualifiers compile.
    shader.setAutoMapLocations(true);
    shader.setAutoMapBindings(true);

    // Inject the Mithril backend identification macros so host shaders can
    // branch on the backend (mirrors MobileGlues' MG_MOBILEGLUES injection).
    // MG_MITHRIL_VERSION encodes major/minor/patch as MMMNNPPP decimal.
    shader.setPreamble(
        "#define MG_MITHRIL 1\n"
        "#define MG_MITHRIL_VERSION 1000000\n"
    );

    // EShMsgVulkanRules IS set as a belt-and-suspenders safety net. If any
    // loose non-opaque uniform somehow bypasses wrap_loose_uniforms() (e.g.
    // an unrecognised type), the glslang auto-wrap path will still catch it
    // and wrap it into the synthetic `$Global` UBO. Without EShMsgVulkanRules,
    // glslang would not enforce Vulkan rules and the emitted SPIR-V might
    // contain non-block uniforms that MoltenVK rejects at module-creation
    // time. The client stays EShClientOpenGL so the emitted SPIR-V remains
    // Vulkan-conformant for MoltenVK. DescriptorSet.cpp uploads the `$Global`
    // UBO's members by name via SPIRV-Cross reflection.
    const EShMessages messages = static_cast<EShMessages>(
        EShMsgDefault | EShMsgSpvRules | EShMsgVulkanRules);

    if (!shader.parse(GetDefaultResources(), glsl_version, true, messages)) {
        info = shader.getInfoLog();
        info += shader.getInfoDebugLog();
        return false;
    }

    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages)) {
        info = program.getInfoLog();
        info += program.getInfoDebugLog();
        return false;
    }

    glslang::TIntermediate* inter = program.getIntermediate(stage);
    if (!inter) { info = "no intermediate after link"; return false; }

    glslang::SpvOptions spv_opts;
    spv_opts.disableOptimizer = false;
    glslang::GlslangToSpv(*inter, spirv, &spv_opts);
    if (spirv.empty()) { info = "SPIR-V generation produced no words"; return false; }
    return true;
}

} // namespace

bool shader_translate(GLenum gl_stage, const std::string& glsl_source,
                      std::vector<uint32_t>& out_spirv, std::string& out_info_log,
                      const std::unordered_map<std::string, GLuint>* attrib_bindings) {
    const char* stage_name =
        gl_stage == GL_VERTEX_SHADER ? "vertex" :
        gl_stage == GL_FRAGMENT_SHADER ? "fragment" : "other";

    // Cache key includes the bindings so that re-linking with different
    // attribute bindings (e.g. a different VertexFormat) produces fresh SPIR-V.
    uint64_t key = fnv1a(glsl_source) ^ (uint64_t)gl_stage * 0x9E3779B97F4A7C15ULL;
    if (attrib_bindings) {
        for (const auto& kv : *attrib_bindings) {
            key ^= fnv1a(kv.first) ^ ((uint64_t)kv.second * 0x100000001B3ULL);
        }
    }
    {
        std::lock_guard<std::mutex> lk(cache().mu);
        auto it = cache().entries.find(key);
        if (it != cache().entries.end()) {
            out_spirv = it->second;
            MITHRIL_LOG_DEBUG("shader", "Cache hit for %s shader (hash %016llx)",
                              stage_name, (unsigned long long)key);
            return true;
        }
    }

    MITHRIL_LOG_INFO("shader", "Translating %s shader (%zu bytes GLSL)",
                     stage_name, glsl_source.size());

    std::vector<uint32_t> spirv;
    if (!glsl_to_spirv(gl_stage, glsl_source, spirv, out_info_log, attrib_bindings)) {
        MITHRIL_LOG_ERROR("shader", "GLSL->SPIR-V failed for %s shader: %s",
                          stage_name, out_info_log.c_str());
        return false;
    }

    MITHRIL_LOG_INFO("shader", "Translated %s shader: %zu SPIR-V words",
                     stage_name, spirv.size());

    out_spirv = spirv;
    std::lock_guard<std::mutex> lk(cache().mu);
    cache().entries[key] = std::move(spirv);
    return true;
}

} // namespace mithril
