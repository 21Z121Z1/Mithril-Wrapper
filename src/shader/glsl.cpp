// Mithril-Wrapper shader module -- GLSL -> SPIR-V compilation (M2-S2).
// glslang compiles desktop GLSL (Core Profile) to Vulkan SPIR-V:
//   1. GLSL versions below 330 are upgraded (the Vulkan GLSL minimum).
//   2. Desktop builtins Vulkan GLSL renames (gl_VertexID etc.).
//   3. Loose non-opaque uniforms fold into a synthetic mithril_GlobalBlock
//      UBO (ANGLE-style).
//   4. User uniform blocks receive a collision-free internal binding.
//   5. EShClientOpenGL + EShMsgVulkanRules + mapIO, then GlslangToSpv.
// Results are cached by (stage, source hash).

#include <shader/shader.h>

#include <GL/glcorearb.h>

#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>

#include <util/log.h>

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

namespace mithril::shader {

namespace {

struct GlslangInit {
    GlslangInit() { glslang::InitializeProcess(); }
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

int get_glsl_version(const std::string& src) {
    static const std::regex re(R"(#version\s+(\d{3}))");
    std::smatch m;
    if (std::regex_search(src, m, re)) return std::stoi(m[1].str());
    return -1;
}

// Rewrite `src` to declare GLSL version `v` (floor): layout(binding=...)
// needs GLSL 420+.
void bump_glsl_version(std::string& src, int v) {
    if (get_glsl_version(src) >= v) return;
    std::string line = "#version " + std::to_string(v) + " core";
    int cur = get_glsl_version(src);
    if (cur == -1) {
        src.insert(0, line + "\n");
        return;
    }
    size_t pos = src.find("#version");
    size_t line_end = src.find('\n', pos);
    if (line_end == std::string::npos) line_end = src.length();
    src.replace(pos, line_end - pos, line);
}

// Vulkan GLSL requires #version 330 minimum; upgrade anything below.
int ensure_glsl_version(std::string& src) {
    int ver = get_glsl_version(src);
    if (ver == -1) {
        src.insert(0, "#version 330 core\n");
        return 330;
    }
    if (ver < 330) {
        size_t pos = src.find("#version");
        size_t line_end = src.find('\n', pos);
        if (line_end == std::string::npos) line_end = src.length();
        src.replace(pos, line_end - pos, "#version 330 core");
        return 330;
    }
    size_t pos = src.find("#version");
    size_t line_end = src.find('\n', pos);
    if (line_end == std::string::npos) line_end = src.length();
    std::string line = src.substr(pos, line_end - pos);
    if (line.find("core") == std::string::npos &&
        line.find("compatibility") == std::string::npos &&
        line.find("es") == std::string::npos) {
        src.replace(pos, line_end - pos, line + " core");
    }
    return ver;
}

// Desktop-GLSL builtins that Vulkan GLSL declares under different names.
void rewrite_desktop_builtins(std::string& src) {
    static const std::regex re(R"(\bgl_VertexID\b|\bgl_InstanceID\b)",
                               std::regex::optimize);
    std::string out;
    out.reserve(src.size());
    std::string::const_iterator it = src.cbegin();
    std::smatch m;
    while (std::regex_search(it, src.cend(), m, re)) {
        out.append(it, m[0].first);
        out.append(m.str() == "gl_VertexID" ? "gl_VertexIndex" : "gl_InstanceIndex");
        it = m[0].second;
    }
    out.append(it, src.cend());
    src.swap(out);
}

bool is_opaque_glsl_type(const std::string& name) {
    if (name.find("sampler") != std::string::npos) return true;
    if (name.find("image") != std::string::npos) return true;
    if (name == "atomic_uint") return true;
    if (name.find("subpass") != std::string::npos) return true;
    return false;
}

size_t find_matching_brace(const std::string& s, size_t open_idx) {
    int depth = 0;
    for (size_t i = open_idx; i < s.size(); ++i) {
        if (s[i] == '{') ++depth;
        else if (s[i] == '}') { --depth; if (depth == 0) return i; }
    }
    return std::string::npos;
}

bool is_in_comment(const std::string& s, size_t off) {
    size_t line_start = s.rfind('\n', off);
    line_start = (line_start == std::string::npos) ? 0 : line_start + 1;
    if (s.find("//", line_start) < off) return true;
    int depth = 0;
    for (size_t i = 0; i < off; ++i) {
        if (i + 1 < off && s[i] == '/' && s[i + 1] == '*') { ++depth; ++i; }
        else if (i + 1 < off && s[i] == '*' && s[i + 1] == '/') { if (depth) --depth; ++i; }
    }
    return depth > 0;
}

// Vulkan GLSL needs explicit sampler bindings; assign each sampler a fixed
// binding in declaration order (1-based), leaving binding 0 for the folded
// mithril_GlobalBlock UBO. The engine mirrors these bindings 1:1 when it
// builds the descriptor set layout.
void assign_sampler_bindings(std::string& source) {
    static const std::regex re(
        R"((layout\s*\([^)]*\))?\s*uniform\s+((?:[iu]?sampler)(?:2DMSArray|2DArray|CubeArray|2DRect|1DArray|2DMS|2D|3D|Cube|1D|Buffer)(?:Shadow)?)\s+(\w+)\s*;)");
    struct Edit { size_t pos; size_t len; std::string text; };
    std::vector<Edit> edits;
    {
        auto cur = source.cbegin(), end = source.cend();
        std::smatch m;
        int slot = 0;
        while (std::regex_search(cur, end, m, re)) {
            size_t off = m.position(0) + (cur - source.cbegin());
            if (!is_in_comment(source, off)) {
                ++slot;
                if (m[1].matched) {
                    edits.push_back({off, m[1].str().size(),
                                     "layout(binding=" + std::to_string(slot) + ")"});
                } else {
                    edits.push_back({off, 0, "\nlayout(binding=" +
                                                 std::to_string(slot) + ") "});
                }
            }
            cur = m.suffix().first;
        }
    }
    // Apply from the tail so earlier offsets stay valid.
    for (auto it = edits.rbegin(); it != edits.rend(); ++it) {
        if (it->len == 0) {
            source.insert(it->pos + it->len, it->text);
        } else {
            source.replace(it->pos, it->len, it->text);
        }
    }
}

const std::regex& uniform_block_re() {
    static const std::regex re(
        R"(((layout\s*\(([^)]*)\)\s*)?uniform\s+(\w+)\s*\{))",
        std::regex::optimize);
    return re;
}

bool set_internal_uniform_block_bindings(std::string& source,
                                         std::string& error) {
    struct Edit { size_t pos; size_t len; std::string text; };
    std::vector<Edit> edits;
    uint32_t user_block = 0;
    auto cur = source.cbegin(), end = source.cend();
    std::smatch match;
    while (std::regex_search(cur, end, match, uniform_block_re())) {
        const size_t off = match.position(0) + (cur - source.cbegin());
        cur = match.suffix().first;
        if (is_in_comment(source, off)) continue;

        const std::string name = match[4].str();
        uint32_t binding = kLooseUniformBinding;
        if (name != "mithril_GlobalBlock") {
            if (user_block >= kMaxUserUniformBlocksPerStage) {
                error = "shader uses more than " +
                        std::to_string(kMaxUserUniformBlocksPerStage) +
                        " user uniform blocks in one stage";
                return false;
            }
            binding = kUserUniformBindingBase + user_block++;
        }

        std::string inner = match[3].matched ? match[3].str() : "";
        static const std::regex literal_binding(
            R"(\bbinding\s*=\s*([0-9]+))", std::regex::optimize);
        static const std::regex any_binding(
            R"(\bbinding\s*=)", std::regex::optimize);
        if (std::regex_search(inner, any_binding) &&
            !std::regex_search(inner, literal_binding)) {
            error = "uniform block layout(binding=) must use an integer literal";
            return false;
        }
        if (std::regex_search(inner, literal_binding)) {
            inner = std::regex_replace(
                inner, literal_binding,
                "binding=" + std::to_string(binding),
                std::regex_constants::format_first_only);
        } else {
            if (!inner.empty()) inner += ", ";
            inner += "binding=" + std::to_string(binding);
        }
        const std::string layout = "layout(" + inner + ") ";
        if (match[2].matched)
            edits.push_back({off, match[2].str().size(), layout});
        else
            edits.push_back({off, 0, layout});
    }
    for (auto it = edits.rbegin(); it != edits.rend(); ++it) {
        if (it->len) source.replace(it->pos, it->len, it->text);
        else source.insert(it->pos, it->text);
    }
    return true;
}

void split_declarators(const std::string& list, std::vector<std::string>& out) {
    std::string cur;
    int depth = 0;
    for (char c : list) {
        if (c == '[') ++depth;
        else if (c == ']') { if (depth) --depth; }
        if (c == ',' && depth == 0) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
}

bool parse_declarator(const std::string& d, std::string& name, std::string& arr) {
    static const std::regex nm_re(R"(^\s*(\w+)\s*((?:\[[^\]]*\])*)\s*$)");
    std::smatch m;
    if (!std::regex_match(d, m, nm_re)) return false;
    name = m[1].str();
    arr = m[2].str();
    return true;
}

// Fold loose non-opaque uniforms into a synthetic UBO (ANGLE-style), mirroring
// the retired renderer's proven preprocessor pass.
void wrap_loose_uniforms(std::string& source) {
    struct Member { std::string decl; std::string name; };
    struct Erase { size_t pos; size_t len; };
    std::vector<Member> members;
    std::vector<Erase> erases;

    static const std::regex simple_re(
        R"(^[ \t]*((?:layout\s*\([^)]*\)\s*)?uniform\s+(?!struct\b)(?:(?:highp|mediump|lowp)\s+)?(\w+)\s+([^;]+?)\s*;))",
        std::regex::multiline | std::regex::optimize);
    {
        auto cur = source.cbegin(), end = source.cend();
        std::smatch m;
        while (std::regex_search(cur, end, m, simple_re)) {
            size_t off = m.position(0) + (cur - source.cbegin());
            size_t full = m[1].str().size();
            std::string type = m[2].str();
            std::string decllist = m[3].str();
            cur = m.suffix().first;
            if (is_in_comment(source, off)) continue;
            if (is_opaque_glsl_type(type)) continue;
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

    static const std::regex struct_re(
        R"(^[ \t]*(uniform\s+struct\s+(\w+)?\s*\{))",
        std::regex::multiline | std::regex::optimize);
    {
        auto cur = source.cbegin(), end = source.cend();
        std::smatch m;
        while (std::regex_search(cur, end, m, struct_re)) {
            size_t off = m.position(0) + (cur - source.cbegin());
            size_t brace = off + m[1].str().size() - 1;
            bool named = m[2].matched;
            std::string struct_name = named ? m[2].str() : "";
            size_t close = find_matching_brace(source, brace);
            cur = m.suffix().first;
            if (close == std::string::npos) continue;
            if (is_in_comment(source, off)) continue;
            std::string struct_def = source.substr(brace, close - brace + 1);
            size_t after = close + 1;
            size_t semi = source.find(';', after);
            if (semi == std::string::npos) continue;
            std::string decllist = source.substr(after, semi - after);
            std::vector<std::string> names;
            split_declarators(decllist, names);
            for (const auto& n : names) {
                std::string var, arr;
                if (!parse_declarator(n, var, arr)) continue;
                if (named) {
                    erases.push_back({off, 7});
                    erases.push_back({after, semi - after + 1});
                    members.push_back({struct_name + " " + var + arr, var});
                } else {
                    erases.push_back({off, semi - off + 1});
                    members.push_back({struct_def + " " + var + arr, var});
                }
            }
        }
    }

    if (members.empty()) return;

    size_t version_end = 0;
    auto vp = source.find("#version");
    if (vp != std::string::npos) {
        auto nl = source.find('\n', vp);
        version_end = (nl != std::string::npos) ? nl + 1 : source.size();
    }

    std::string injection = "\nuniform mithril_GlobalBlock {\n";
    for (const auto& u : members) injection += "    " + u.decl + ";\n";
    injection += "} _m;\n\n";
    for (const auto& u : members) injection += "#define " + u.name + " _m." + u.name + "\n";
    injection += "\n";

    std::sort(erases.begin(), erases.end(),
              [](const Erase& a, const Erase& b) { return a.pos > b.pos; });
    for (const auto& e : erases) source.erase(e.pos, e.len);
    source.insert(version_end, injection);
}

uint64_t fnv1a(const std::string& s) {
    uint64_t h = 1469598103934665603ULL;
    for (char c : s) { h ^= (uint8_t)c; h *= 1099511628211ULL; }
    return h;
}

struct Cache {
    std::mutex mu;
    std::unordered_map<uint64_t, std::vector<uint32_t>> entries;
};
Cache& cache() { static Cache c; return c; }

} // namespace

std::vector<UniformBlockDeclaration> DiscoverUniformBlocks(
    const std::string& source) {
    std::vector<UniformBlockDeclaration> declarations;
    auto cur = source.cbegin(), end = source.cend();
    std::smatch match;
    static const std::regex binding_re(
        R"(\bbinding\s*=\s*([0-9]+))", std::regex::optimize);
    while (std::regex_search(cur, end, match, uniform_block_re())) {
        const size_t off = match.position(0) + (cur - source.cbegin());
        cur = match.suffix().first;
        if (is_in_comment(source, off)) continue;
        UniformBlockDeclaration declaration;
        declaration.name = match[4].str();
        const size_t brace = off + match[0].str().size() - 1;
        const size_t close = find_matching_brace(source, brace);
        if (close != std::string::npos) {
            const size_t semicolon = source.find(';', close + 1);
            if (semicolon != std::string::npos) {
                const std::string instance = source.substr(
                    close + 1, semicolon - close - 1);
                static const std::regex instance_re(
                    R"(^\s*[A-Za-z_]\w*)", std::regex::optimize);
                static const std::regex array_re(
                    R"(^\s*[A-Za-z_]\w*\s*\[)", std::regex::optimize);
                declaration.has_instance =
                    std::regex_search(instance, instance_re);
                declaration.is_array = std::regex_search(instance, array_re);
            }
        }
        std::smatch binding;
        const std::string inner = match[3].matched ? match[3].str() : "";
        if (std::regex_search(inner, binding, binding_re)) {
            const unsigned long value = std::stoul(binding[1].str());
            if (value <= UINT32_MAX) {
                declaration.binding = static_cast<GLuint>(value);
                declaration.has_explicit_binding = true;
            }
        }
        declarations.push_back(std::move(declaration));
    }
    return declarations;
}

bool CompileStage(GLenum stage, const std::string& src,
                  std::vector<uint32_t>& spirv, std::string& info) {
    glslang_init();
    EShLanguage esh_stage = to_esh_stage(stage);
    if (esh_stage == EShLangCount) { info = "unsupported shader stage"; return false; }

    uint64_t key = fnv1a(src) ^ (uint64_t)stage * 0x9E3779B97F4A7C15ULL;
    {
        std::lock_guard<std::mutex> lk(cache().mu);
        auto it = cache().entries.find(key);
        if (it != cache().entries.end()) { spirv = it->second; return true; }
    }

    for (const auto& declaration : DiscoverUniformBlocks(src)) {
        if (declaration.name == "mithril_GlobalBlock") {
            info = "uniform block name mithril_GlobalBlock is reserved by Mithril";
            return false;
        }
        if (declaration.is_array) {
            info = "uniform block arrays are not supported by the current Mithril execution model";
            return false;
        }
    }

    std::string source = src;
    ensure_glsl_version(source);
    rewrite_desktop_builtins(source);

    // wrap_loose_uniforms uses regex that can throw on pathological input;
    // fall back to unwrapped source (glslang auto-wrap path) rather than crash.
    std::string unwrapped = source;
    bool wrapped = false;
    try {
        wrap_loose_uniforms(source);
        wrapped = true;
    } catch (const std::exception& e) {
        ML_LOG_WARN("wrap_loose_uniforms threw (%s); using unwrapped source", e.what());
        source = unwrapped;
    }
    if (!set_internal_uniform_block_bindings(source, info) ||
        !set_internal_uniform_block_bindings(unwrapped, info))
        return false;
    assign_sampler_bindings(source);
    assign_sampler_bindings(unwrapped);
    // layout(binding=...) requires GLSL 420+; bump only when injected.
    if (source.find("binding=") != std::string::npos)
        bump_glsl_version(source, 450);
    if (unwrapped.find("binding=") != std::string::npos)
        bump_glsl_version(unwrapped, 450);

    int version = get_glsl_version(source);
    if (version < 330) version = 330;

    auto compile_once = [&](const std::string& src2, std::string& err) -> bool {
        glslang::TShader shader(esh_stage);
        const char* s = src2.c_str();
        shader.setStrings(&s, 1);
        shader.setEnvInput(glslang::EShSourceGlsl, esh_stage, glslang::EShClientOpenGL, version);
        shader.setEnvClient(glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450);
        shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);
        shader.setAutoMapLocations(true);
        shader.setAutoMapBindings(true);
        shader.setPreamble("#define MG_MITHRIL 1\n#define MG_MITHRIL_VERSION 1000000\n");
        const EShMessages messages = static_cast<EShMessages>(
            EShMsgDefault | EShMsgSpvRules | EShMsgVulkanRules);
        if (!shader.parse(GetDefaultResources(), version, true, messages)) {
            err = shader.getInfoLog();
            err += shader.getInfoDebugLog();
            return false;
        }
        glslang::TProgram program;
        program.addShader(&shader);
        if (!program.link(messages)) {
            err = program.getInfoLog();
            err += program.getInfoDebugLog();
            return false;
        }
        if (!program.mapIO()) {
            err = "glslang mapIO failed: ";
            err += program.getInfoLog();
            err += program.getInfoDebugLog();
            return false;
        }
        glslang::TIntermediate* inter = program.getIntermediate(esh_stage);
        if (!inter) { err = "no intermediate after link"; return false; }
        glslang::SpvOptions spv_opts;
        spv_opts.disableOptimizer = false;
        std::vector<uint32_t> out;
        glslang::GlslangToSpv(*inter, out, &spv_opts);
        if (out.empty()) { err = "SPIR-V generation produced no words"; return false; }
        spirv = std::move(out);
        return true;
    };

    std::string err1;
    if (!compile_once(source, err1)) {
        if (wrapped) {
            std::string err2;
            if (compile_once(unwrapped, err2)) {
                ML_LOG_WARN("retry without wrap succeeded for stage 0x%x", (unsigned)stage);
            } else {
                info = err1;
                return false;
            }
        } else {
            info = err1;
            return false;
        }
    }

    {
        std::lock_guard<std::mutex> lk(cache().mu);
        cache().entries[key] = spirv;
    }
    return true;
}

} // namespace mithril::shader
