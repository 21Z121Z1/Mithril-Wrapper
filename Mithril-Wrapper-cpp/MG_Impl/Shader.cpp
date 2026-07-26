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
//   3. Preprocess: wrap loose non-opaque uniforms (e.g. `uniform mat4 MVM;`)
//      into a synthetic `mithril_GlobalBlock` UBO that is injected right after
//      #version, paired with #define renames so the shader body can still
//      reference members by their original names without an explicit block
//      prefix. This avoids the "non-opaque uniforms outside a block" error
//      that some glslang versions emit even with EShClientOpenGL +
//      EShMsgVulkanRules.
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

/*
 * Preprocess GLSL source to wrap loose (non-block) non-opaque uniform
 * declarations into a synthetic UBO.  Vulkan-conformant SPIR-V requires all
 * uniforms to live inside blocks; glslang's EShClientOpenGL +
 * EShMsgVulkanRules combination is supposed to auto-wrap but some versions
 * reject the shader with:
 *
 *   ERROR: 'non-opaque uniforms outside a block' : not allowed when using
 *   GLSL for Vulkan
 *
 * To avoid this, we rewrite:
 *
 *   #version 150
 *   uniform mat4 ModelViewMat;
 *   uniform sampler2D Sampler0;     // opaque — stays as-is
 *   uniform mat4 ProjMat;
 *
 *   out = ProjMat * ModelViewMat * v;
 *
 * into:
 *
 *   #version 150
 *   uniform mithril_GlobalBlock {   // synthetic UBO (named block, no instance)
 *       mat4 ModelViewMat;
 *       mat4 ProjMat;
 *   };
 *   #define ModelViewMat mithril_GlobalBlock.ModelViewMat
 *   #define ProjMat      mithril_GlobalBlock.ProjMat
 *
 *   uniform sampler2D Sampler0;     // opaque, unchanged
 *
 *   out = ProjMat * ModelViewMat * v;
 *
 * The block + #define are inserted immediately after the #version directive
 * so the pre-processor renames all occurrences before the compiler runs.
 * The original loose-uniform lines are erased from wherever they appeared.
 *
 * The block name "mithril_GlobalBlock" is arbitrary — it does not match any
 * GL uniform name, so the descriptor-upload code in DescriptorSet.cpp falls
 * through to member-by-member packing via SPIRV-Cross reflection, which is
 * identical to the $Global convention that glslang's auto-wrap produces.
 * The block is emitted without an explicit binding so setAutoMapBindings(true)
 * (set in glsl_to_spirv) assigns one; the binding number is irrelevant since
 * the UBO is only ever consumed by member-name lookup in bind_program_descriptors.
 *
 * Shaders that already have all uniforms in blocks or use only opaque-sampler
 * uniforms are unaffected (a no-op scan).
 */
static void wrap_loose_uniforms(std::string& source) {
    // Match: [layout(...)] uniform <type> <name>[<array>] ;
    // Group 1 = full declaration, group 2 = optional layout qualifier,
    // group 3 = type name, group 4 = variable name, group 5 = optional array.
    static const std::regex decl_re(
        R"(^[ \t]*((layout\s*\([^)]*\)\s*)?uniform\s+(\w+)\s+(\w+)(\s*\[[^\]]*\])?\s*;))",
        std::regex::multiline | std::regex::optimize);

    // Phase 1: scan and collect all non-opaque uniform declarations.
    struct UniformDecl {
        size_t pos;
        size_t len;
        std::string type;
        std::string var;  // base variable name (no array suffix)
        std::string arr;  // array suffix ("" or "[N]")
    };
    std::vector<UniformDecl> non_opaque;

    {
        auto cur = source.cbegin();
        auto end = source.cend();
        std::smatch m;
        while (std::regex_search(cur, end, m, decl_re)) {
            size_t match_off = m.position(0) + (cur - source.cbegin());
            size_t match_len = m[1].str().size();
            const std::string& layout = m[2].str();
            const std::string& type   = m[3].str();
            const std::string& var    = m[4].str();
            const std::string& arr    = m[5].str();

            if (layout.empty() && !is_opaque_glsl_type(type)) {
                non_opaque.push_back({match_off, match_len, type, var, arr});
            }

            cur = m.suffix().first;
        }
    }

    if (non_opaque.empty())
        return;

    // Phase 2: determine the insertion point — right after the #version
    // directive (which ensure_glsl_version guarantees is present).
    size_t version_end = 0;
    {
        auto vp = source.find("#version");
        if (vp != std::string::npos) {
            auto nl = source.find('\n', vp);
            version_end = (nl != std::string::npos) ? nl + 1 : source.size();
        }
    }

    // Phase 3: build the synthetic block and #define redirects.
    std::string injection;
    injection += "\nuniform mithril_GlobalBlock {\n";
    for (const auto& u : non_opaque) {
        injection += "    " + u.type + " " + u.var + u.arr + ";\n";
    }
    injection += "};\n\n";
    for (const auto& u : non_opaque) {
        injection += "#define " + u.var + " mithril_GlobalBlock." + u.var + "\n";
    }
    injection += "\n";

    // Phase 4: erase the original loose-uniform declarations (reverse order
    // so earlier positions are unaffected by later erasures).
    for (auto it = non_opaque.rbegin(); it != non_opaque.rend(); ++it) {
        source.erase(it->pos, it->len);
    }

    // Phase 5: inject the block + defines at the insertion point (stable
    // because all erasures are after version_end in the source).
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
