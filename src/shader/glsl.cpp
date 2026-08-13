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

// Minecraft 26.2's projection include writes two disjoint vector swizzles:
//
//     projection.xy = ...;
//     projection.zw = ...;
//
// The iOS arm64 glslang build can lose the swizzle l-value node during error
// recovery and then reject otherwise valid stages with "l-value required".
// These two writes are equivalent to one complete vec4 assignment, which is
// also friendlier to the shared SPIR-V front end. Keep this narrowly scoped
// to the exact helper text; arbitrary user shader swizzle assignments must
// retain normal GLSL semantics.
void rewrite_minecraft_projection_swizzles(std::string& src) {
    static const std::string xy =
        "projection.xy = vec2(projection.x + projection.w, projection.y + projection.w);";
    static const std::string zw = "projection.zw = position.zw;";
    const size_t first = src.find(xy);
    const size_t second = src.find(zw, first == std::string::npos ? 0 : first + xy.size());
    if (first == std::string::npos || second == std::string::npos) return;
    const size_t end = second + zw.size();
    src.replace(first, end - first,
                "projection = vec4((position.x + position.w) * 0.5, "
                "(position.y + position.w) * 0.5, position.z, position.w);");
}

// Two constructs in the 26.2 include shaders are valid desktop GLSL but
// expose an iOS-arm64 glslang AST bug: a two-component swizzle used as a
// function argument, and an implicit ivec2-to-vec2 conversion in division.
// Lower only the exact Mojang helper statements; do not rewrite arbitrary
// application shaders.
void rewrite_minecraft_ios_vector_compatibility(std::string& src) {
    auto replace_all = [&](const std::string& from, const std::string& to) {
        size_t offset = 0;
        while ((offset = src.find(from, offset)) != std::string::npos) {
            src.replace(offset, from.size(), to);
            offset += to.size();
        }
    };

    static const std::string fog = "float distXZ = length(pos.xz);";
    static const std::string fog_lowered =
        "float distXZ = length(vec2(pos.x, pos.z));";
    size_t pos = 0;
    while ((pos = src.find(fog, pos)) != std::string::npos) {
        src.replace(pos, fog.size(), fog_lowered);
        pos += fog_lowered.size();
    }

    static const std::string lightmap =
        "return texture(lightMap, clamp((uv / 256.0) + 0.5 / 16.0, "
        "vec2(0.5 / 16.0), vec2(15.5 / 16.0)));";
    static const std::string lightmap_lowered =
        "vec2 uvf = vec2(uv);\n"
        "    return texture(lightMap, clamp((uvf / 256.0) + 0.5 / 16.0, "
        "vec2(0.5 / 16.0), vec2(15.5 / 16.0)));";
    pos = 0;
    while ((pos = src.find(lightmap, pos)) != std::string::npos) {
        src.replace(pos, lightmap.size(), lightmap_lowered);
        pos += lightmap_lowered.size();
    }

    // Fog and lighting includes are shared by most 26.2 programs.  Lower
    // their three-component reads as well; otherwise even a shader that does
    // not use the corresponding feature still carries the problematic AST.
    replace_all(
        "return vec4(mix(inColor.rgb, fogColor.rgb, fogValue * fogColor.a), inColor.a);",
        "return vec4(mix(vec3(inColor.x, inColor.y, inColor.z), "
        "vec3(fogColor.x, fogColor.y, fogColor.z), fogValue * fogColor.a), inColor.a);");
    replace_all(
        "return vec4(color.rgb * lightAccum, color.a);",
        "return vec4(vec3(color.x, color.y, color.z) * lightAccum, color.a);");

    // Entity/item overlay path: preserve the l-value semantics while avoiding
    // a compound rgb swizzle assignment.
    replace_all(
        "color.rgb = mix(overlayColor.rgb, color.rgb, overlayColor.a);",
        "color = vec4(mix(vec3(overlayColor.x, overlayColor.y, overlayColor.z), "
        "vec3(color.x, color.y, color.z), overlayColor.a), color.a);");
    replace_all(
        "fragColor = vec4(color.rgb * fade, color.a);",
        "fragColor = vec4(vec3(color.x, color.y, color.z) * fade, color.a);");
    replace_all(
        "fragColor = vec4(ColorModulator.rgb * vertexColor.rgb, ColorModulator.a);",
        "fragColor = vec4(vec3(ColorModulator.x, ColorModulator.y, ColorModulator.z) * "
        "vec3(vertexColor.x, vertexColor.y, vertexColor.z), ColorModulator.a);");
    replace_all(
        "vec4 texColor = texture(Sampler0, texCoord0).rrrr;",
        "vec4 texColor = vec4(texture(Sampler0, texCoord0).r);");

    // Lines renderer: expand the two vector swizzles used for NDC setup.
    replace_all(
        "vec3 ndc1 = linePosStart.xyz / linePosStart.w;",
        "vec3 ndc1 = vec3(linePosStart.x, linePosStart.y, linePosStart.z) / linePosStart.w;");
    replace_all(
        "vec3 ndc2 = linePosEnd.xyz / linePosEnd.w;",
        "vec3 ndc2 = vec3(linePosEnd.x, linePosEnd.y, linePosEnd.z) / linePosEnd.w;");
    replace_all(
        "vec2 lineScreenDirection = normalize((ndc2.xy - ndc1.xy) * ScreenSize);",
        "vec2 lineScreenDirection = normalize((vec2(ndc2.x, ndc2.y) - "
        "vec2(ndc1.x, ndc1.y)) * ScreenSize);");

    // The texture-matrix path is repeated in entity, glint, and world-border
    // vertex shaders.  Materialize the matrix result before selecting x/y.
    replace_all(
        "texCoord0 = (TextureMat * vec4(UV0, 0.0, 1.0)).xy;",
        "vec4 texCoord0Transformed = TextureMat * vec4(UV0, 0.0, 1.0);\n"
        "    texCoord0 = vec2(texCoord0Transformed.x, texCoord0Transformed.y);");

    // End-portal samples use rgb swizzles on textureProj results.  Keep one
    // sample per expression and lower the component selection explicitly.
    replace_all(
        "vec3 color = textureProj(Sampler0, texProj0).rgb * COLORS[0];",
        "vec4 portalBase = textureProj(Sampler0, texProj0);\n"
        "    vec3 color = vec3(portalBase.x, portalBase.y, portalBase.z) * COLORS[0];");
    replace_all(
        "color += textureProj(Sampler1, texProj0 * end_portal_layer(float(i + 1))).rgb * COLORS[i];",
        "vec4 portalLayer = textureProj(Sampler1, texProj0 * end_portal_layer(float(i + 1)));\n"
        "        color += vec3(portalLayer.x, portalLayer.y, portalLayer.z) * COLORS[i];");
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

// Vulkan GLSL needs explicit sampler bindings. One sampler-array declaration
// still owns one SPIR-V descriptor binding, but classic Metal consumes one
// texture/sampler slot per fixed array element. Reserve that complete span so
// a later sampler never aliases an earlier array's Metal resource slots.
//
// This OpenGL 3.3 slice accepts the normal fixed literal array form. More
// elaborate constant-expression sizes are rejected explicitly instead of
// relying on glslang auto-binding and silently producing overlapping slots.
bool assign_sampler_bindings(std::string& source, std::string& error) {
    static const std::regex re(
        R"((layout\s*\([^)]*\))?\s*uniform\s+(?:(?:highp|mediump|lowp)\s+)?((?:[iu]?sampler)(?:2DMSArray|2DArray|CubeArray|2DRect|1DArray|2DMS|2D|3D|Cube|1D|Buffer)(?:Shadow)?)\s+(\w+)\s*(\[\s*([^\]]+)\s*\])?\s*;)");
    static const std::regex literal(R"(^\s*([0-9]+)\s*$)");
    struct Edit { size_t pos; size_t len; std::string text; };
    std::vector<Edit> edits;
    auto cur = source.cbegin(), end = source.cend();
    std::smatch match;
    uint32_t next_binding = 1;
    while (std::regex_search(cur, end, match, re)) {
        const size_t off = match.position(0) + (cur - source.cbegin());
        cur = match.suffix().first;
        if (is_in_comment(source, off)) continue;

        uint32_t span = 1;
        if (match[4].matched) {
            std::smatch size_match;
            const std::string expression = match[5].str();
            if (!std::regex_match(expression, size_match, literal)) {
                error = "sampler array size must be a positive integer literal: " +
                        match[3].str();
                return false;
            }
            const unsigned long long parsed = std::stoull(size_match[1].str());
            if (parsed == 0 || parsed > UINT32_MAX) {
                error = "sampler array size is outside Mithril's fixed resource range: " +
                        match[3].str();
                return false;
            }
            span = static_cast<uint32_t>(parsed);
        }
        if (span > UINT32_MAX - next_binding + 1) {
            error = "sampler binding range overflow";
            return false;
        }
        const uint32_t binding = next_binding;
        next_binding += span;
        if (match[1].matched) {
            edits.push_back({off, match[1].str().size(),
                             "layout(binding=" + std::to_string(binding) + ")"});
        } else {
            edits.push_back({off, 0, "\nlayout(binding=" +
                                         std::to_string(binding) + ") "});
        }
    }
    // Apply from the tail so earlier offsets stay valid.
    for (auto it = edits.rbegin(); it != edits.rend(); ++it) {
        if (it->len == 0) source.insert(it->pos, it->text);
        else source.replace(it->pos, it->len, it->text);
    }
    return true;
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

void LogSourceChunks(const char* label, uint64_t digest,
                     const std::string& source) {
    static unsigned sequence = 0;
    const unsigned current = ++sequence;
    constexpr size_t kChunk = 260;
    for (size_t offset = 0; offset < source.size(); offset += kChunk) {
        std::string escaped = source.substr(offset, kChunk);
        for (size_t i = 0; i < escaped.size(); ++i) {
            switch (escaped[i]) {
                case '\\': escaped.replace(i, 1, "\\\\"); ++i; break;
                case '\n': escaped.replace(i, 1, "\\n"); ++i; break;
                case '\r': escaped.replace(i, 1, "\\r"); ++i; break;
                case '\t': escaped.replace(i, 1, "\\t"); ++i; break;
                default: break;
            }
        }
        ML_LOG_INFO("TRACE GLSL %s seq=%u off=%zu hash=%016llx %s",
                    label, current, offset,
                    (unsigned long long)digest, escaped.c_str());
    }
}

struct Cache {
    std::mutex mu;
    std::unordered_map<uint64_t, std::vector<uint32_t>> entries;
};
Cache& cache() { static Cache c; return c; }

// glslang's parser/IO mapper has process-global state in this vendored build.
// Minecraft 26.2 can precompile several pipelines from different executor
// threads, so serialize the complete glslang transaction even though Mithril's
// shader cache itself is concurrent.
std::mutex& glslang_compile_mutex() {
    static std::mutex m;
    return m;
}

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
    // Keep the guard across the whole stage transaction.  In addition to the
    // parser/linker calls, this protects InitializeProcess and the cache-miss
    // window: several Minecraft executor threads can otherwise enter the
    // vendored glslang state between the initial lookup and parse().
    std::lock_guard<std::mutex> glslang_lock(glslang_compile_mutex());
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
    rewrite_minecraft_projection_swizzles(source);
    rewrite_minecraft_ios_vector_compatibility(source);

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
    if (!assign_sampler_bindings(source, info) ||
        !assign_sampler_bindings(unwrapped, info))
        return false;
    // layout(binding=...) requires GLSL 420+; bump only when injected.
    if (source.find("binding=") != std::string::npos)
        bump_glsl_version(source, 450);
    if (unwrapped.find("binding=") != std::string::npos)
        bump_glsl_version(unwrapped, 450);

    int version = get_glsl_version(source);
    if (version < 330) version = 330;

    auto compile_once = [&](const std::string& src2, std::string& err) -> bool {
        const uint64_t source_digest = fnv1a(src2);
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
            ML_LOG_WARN("TRACE GLSL parse failure stage=0x%x bytes=%zu hash=%016llx first=%.*s last=%.*s",
                        (unsigned)stage, src2.size(),
                        (unsigned long long)source_digest, 160, src2.c_str(),
                        160, src2.size() > 160 ? src2.c_str() + src2.size() - 160 : src2.c_str());
            if (src2.size() >= 2800 && src2.size() <= 3300)
                LogSourceChunks("parse-source", source_digest, src2);
            return false;
        }
        // glslang can keep parsing after a semantic error (notably the
        // malformed l-value/swizzle trees seen in Minecraft 26.2).  Do not
        // hand such an intermediate tree to the SPIR-V backend: it is not a
        // valid input and some backend paths assume every access chain has a
        // materialized result.
        std::string parse_info = shader.getInfoLog();
        parse_info += shader.getInfoDebugLog();
        if (!parse_info.empty()) {
            ML_LOG_WARN("TRACE GLSL parse diagnostics stage=0x%x: %s",
                        (unsigned)stage, parse_info.c_str());
            if (parse_info.find("ERROR:") != std::string::npos) {
                ML_LOG_WARN("TRACE GLSL rejected source stage=0x%x:\n%s",
                            (unsigned)stage, src2.c_str());
                err = parse_info;
                return false;
            }
        }
        glslang::TProgram program;
        program.addShader(&shader);
        if (!program.link(messages)) {
            err = program.getInfoLog();
            err += program.getInfoDebugLog();
            ML_LOG_WARN("TRACE GLSL link failure stage=0x%x bytes=%zu hash=%016llx",
                        (unsigned)stage, src2.size(),
                        (unsigned long long)source_digest);
            return false;
        }
        std::string link_info = program.getInfoLog();
        link_info += program.getInfoDebugLog();
        if (!link_info.empty()) {
            ML_LOG_WARN("TRACE GLSL link diagnostics stage=0x%x: %s",
                        (unsigned)stage, link_info.c_str());
            if (link_info.find("ERROR:") != std::string::npos) {
                ML_LOG_WARN("TRACE GLSL rejected linked source stage=0x%x:\n%s",
                            (unsigned)stage, src2.c_str());
                err = link_info;
                return false;
            }
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
        spv::SpvBuildLogger spv_logger;
        glslang::GlslangToSpv(*inter, out, &spv_logger, &spv_opts);
        const std::string spv_info = spv_logger.getAllMessages();
        if (!spv_info.empty()) {
            ML_LOG_WARN("TRACE GLSL SPIR-V diagnostics stage=0x%x: %s",
                        (unsigned)stage, spv_info.c_str());
        }
        if (out.empty()) {
            err = "SPIR-V generation produced no words";
            if (!spv_info.empty()) {
                err += ": ";
                err += spv_info;
            }
            ML_LOG_WARN("TRACE GLSL rejected SPIR-V source stage=0x%x:\n%s",
                        (unsigned)stage, src2.c_str());
            return false;
        }
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
