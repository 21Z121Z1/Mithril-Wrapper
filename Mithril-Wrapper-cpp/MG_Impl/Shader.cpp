// Mithril-Wrapper - MG_Impl/Shader.cpp
// GLSL (desktop Core Profile) -> Vulkan SPIR-V translation via glslang.
//
// Pipeline (mirrors MobileGL's ShaderCompiler.cpp / ProgramFactory.cpp):
//   1. Preprocess (idempotent, comment-aware):
//        a. inject MG_MITHRIL / MG_MITHRIL_VERSION macros;
//        b. upgrade GLSL versions below 460 to "#version 460 core"
//           (Vulkan GLSL minimum; the version retry MobileGL performs via
//            RetargetLegacyVersionDirectiveTo460 is subsumed by this);
//        c. normalize layout(packed/shared) -> layout(std140/std430);
//        d. rewrite gl_FragColor -> synthetic named output;
//        e. inject GL->Vulkan position fixups (Z remap always, Y flip when
//           requested) by wrapping main() — the GLSL-level equivalent of
//           MobileGL's SPIRV-Tools GlToVulkanPositionFixPass.
//   2. glslang compiles each stage under the VULKAN client dialect with
//      EXT_vulkan_glsl_relaxed (EShClientVulkan + VulkanRulesRelaxed):
//        - loose non-opaque uniforms are auto-folded into the named
//          `mithril_GlobalBlock` UBO (setGlobalUniformBlockName — the same
//          mechanism as MobileGL's MGL_GLOBAL_UBO), so NO regex wrapping is
//          needed;
//        - desktop builtins (gl_VertexID, gl_InstanceID) are accepted
//          natively, so NO identifier renaming is needed.
//   3. glLinkProgram builds ONE glslang::TProgram over VS+FS, links it, and
//      runs mapIO() with a custom resolver that pins glBindAttribLocation
//      mappings onto stage-input locations (MobileGL's TMglGlslIoResolver
//      pattern, including its dead-input handling).
//   4. The SPIR-V is post-processed by SPIRV-Reflect:
//      remap_descriptor_bindings() reassigns every descriptor binding
//      deterministically across BOTH stages keyed by (kind, name) — the
//      exact algorithm MobileGL's RemapDescriptorBindingsForVulkan uses.
//      This guarantees VS/FS can never collide on a binding and every
//      emitted resource (even an inactive sampler) carries a valid Binding
//      decoration.
//   5. MoltenVK cross-translates the Vulkan SPIR-V to MSL internally at
//      vkCreateShaderModule time; no SPIRV-Cross stage is needed here.
#include "Shader.h"
#include "Log.h"

#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Include/intermediate.h>
#include <glslang/MachineIndependent/iomapper.h>
#include <SPIRV/GlslangToSpv.h>

#include <spirv_reflect.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <regex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mithril {
namespace {

// Name of the synthetic UBO glslang folds loose non-opaque uniforms into.
// Mirrors MobileGL's GLOBAL_UBO_NAME ("MGL_GLOBAL_UBO") and ANGLE's
// ANGLE_DefaultUniformBlock. DescriptorSet.cpp treats blocks with this name
// as synthetic (fed from the transient uniform arena) rather than as
// application-declared blocks.
constexpr const char* kGlobalBlockName = "mithril_GlobalBlock";

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
 * Ensure the GLSL source declares a version usable by the Vulkan client.
 * Vulkan GLSL requires #version 330 minimum; we raise everything below 460
 * to "#version 460 core" so desktop GLSL 150 shaders (Minecraft's
 * blit_screen) and 420-era syntax compile under one rule. 460 subsumes the
 * retry-at-460 step MobileGL performs (RetargetLegacyVersionDirectiveTo460),
 * and 'core' is mandatory — 'compatibility' pulls in deprecated fixed-function
 * symbols that Vulkan rejects.
 *
 * Returns the resolved GLSL version number.
 */
int ensure_glsl_version(std::string& src) {
    int ver = get_glsl_version(src);
    size_t pos = src.find("#version");
    size_t line_end = (pos == std::string::npos) ? std::string::npos
                                                 : src.find('\n', pos);
    if (line_end == std::string::npos) line_end = src.length();

    if (ver == -1) {
        src.insert(0, "#version 460 core\n");
        return 460;
    }
    std::string line = src.substr(pos, line_end - pos);
    if (ver < 460) {
        src.replace(pos, line_end - pos, "#version 460 core");
    } else if (line.find("core") == std::string::npos &&
               line.find("compatibility") == std::string::npos &&
               line.find("es") == std::string::npos) {
        // Version is fine but the profile token is missing; add ' core'.
        src.replace(pos, line_end - pos, line + " core");
    }
    return std::max(ver, 460);
}

// True if offset `off` sits inside a // or /* */ comment.
static bool is_in_comment(const std::string& s, size_t off) {
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

// ---------------------------------------------------------------------------
// Vulkan-incompatible layout qualifier normalization.
//   layout(packed)  uniform { ... } -> layout(std140) uniform { ... }
//   layout(shared)  uniform { ... } -> layout(std140) uniform { ... }
//   layout(packed)  buffer  { ... } -> layout(std430) buffer  { ... }
//   layout(shared)  buffer  { ... } -> layout(std430) buffer  { ... }
// Idempotent, comment-aware, no-op fast path when no packed/shared tokens.
// ---------------------------------------------------------------------------
void normalize_vulkan_incompatible_layouts(std::string& source) {
    if (source.find("packed") == std::string::npos &&
        source.find("shared") == std::string::npos) {
        return;
    }
    static const std::regex re(
        R"(layout\s*\(([^)]*)\)\s*(uniform|buffer)\b)",
        std::regex::optimize | std::regex::multiline);

    auto process_arg = [](const std::string& arg, const std::string& replacement,
                          bool* changed) -> std::string {
        size_t b = arg.find_first_not_of(" \t");
        size_t e = arg.find_last_not_of(" \t");
        if (b == std::string::npos) return std::string();
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
        if (is_in_comment(source, match_pos)) {
            out.append(it, m[0].second);
            it = m[0].second;
            continue;
        }
        std::string args = m[1].str();
        std::string storage = m[2].str();
        std::string replacement = (storage == "uniform") ? "std140" : "std430";

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
            out.append(it, m[0].second);
        } else {
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

// ---------------------------------------------------------------------------
// GL legacy construct normalization (fragment shaders only).
//   gl_FragColor -> synthetic `layout(location = 0) out vec4
//   _mithril_FragColor;` declaration + reference rewrite.
// Idempotent, comment-aware.
// ---------------------------------------------------------------------------
void normalize_gl_legacy_constructs(std::string& source, GLenum gl_stage) {
    if (gl_stage != GL_FRAGMENT_SHADER) return;
    if (source.find("gl_FragColor") == std::string::npos) return;

    static const std::regex re(R"(\bgl_FragColor\b)", std::regex::optimize);
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

    size_t vp = source.find("#version");
    size_t insert_at = 0;
    if (vp != std::string::npos) {
        size_t nl = source.find('\n', vp);
        insert_at = (nl != std::string::npos) ? nl + 1 : source.size();
    }
    source.insert(insert_at, "layout(location = 0) out vec4 _mithril_FragColor;\n");
}

// ---------------------------------------------------------------------------
// Position fixup injection (GL -> Vulkan NDC adjustment).
//
// Deep reference: MobileGL ProgramFactory::InsertPositionFixup
// (ProgramFactory.cpp:789-857) applies these at the SPIR-V level via
// SPIRV-Tools IRBuilder; Mithril does not link SPIRV-Tools, so the same
// result is achieved via GLSL source injection before glslang compiles.
//
//   1. Z remap (ALWAYS): gl_Position.z = (gl_Position.z + gl_Position.w)*0.5
//      GL NDC Z is [-1,1]; Vulkan NDC Z is [0,1]. Must happen in-shader
//      (before clip), not via viewport min/max depth.
//   2. Y flip (only when flip_y): gl_Position.y = -gl_Position.y
//      The default framebuffer (FBO 0) renders to the on-screen drawable
//      (Vulkan/Metal Y-down) so its image must be flipped; user FBOs render
//      into textures sampled with GL Y-up coords so they must NOT be flipped.
//      This matches MobileGL GetShaderTransformFlags (PositionYFlip is only
//      set when the current draw FBO is the default framebuffer).
// ---------------------------------------------------------------------------
void inject_position_fixup(std::string& src, GLenum gl_stage, bool flip_y) {
    if (gl_stage != GL_VERTEX_SHADER) return;
    static const std::regex main_re(R"(\bvoid\s+main\s*\()");
    if (!std::regex_search(src, main_re)) return;
    src = std::regex_replace(src, main_re, "void _mithril_original_main(");
    src += "\nvoid main() {\n    _mithril_original_main();\n";
    if (flip_y) {
        src += "    gl_Position.y = -gl_Position.y;\n";
    }
    src += "    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;\n}\n";
}

// ---------------------------------------------------------------------------
// Custom IO resolver (MobileGL TMglGlslIoResolver pattern).
//
//   - Live vertex inputs that the application bound via glBindAttribLocation
//     get their layoutLocation pinned in reserverStorageSlot.
//   - Dead (inactive) vertex inputs never reserve a slot (GL semantics: only
//     active inputs consume generic attribute locations). glslang still
//     EMITS a declared-but-unused input, and SPIR-V forbids an OpVariable
//     with no Location decoration, so dead inputs are handed locations from
//     the TOP of the attribute range (15 downward) instead — both properties
//     hold: every emitted input carries a Location, and no active input is
//     displaced. (This is exactly the Adreno 830 fix MobileGL shipped:
//     TMglGlslIoResolver.cpp resolveInOutLocation.)
// ---------------------------------------------------------------------------
class MithrilIoResolver : public glslang::TDefaultGlslIoResolver {
public:
    MithrilIoResolver(const glslang::TIntermediate& intermediate,
                      EShLanguage stage,
                      const std::unordered_map<std::string, GLuint>* explicitVertexIns)
        : TDefaultGlslIoResolver(intermediate),
          m_stage(stage),
          m_explicitVertexIns(explicitVertexIns) {}

    void reserverStorageSlot(glslang::TVarEntryInfo& ent, TInfoSink& infoSink) override {
        const glslang::TType& type = ent.symbol->getType();
        if (!ent.live && m_stage == EShLangVertex && type.getQualifier().isPipeInput()) {
            return;  // dead input: never reserve a slot (GL semantics)
        }
        if (ent.live && m_stage == EShLangVertex && type.getQualifier().isPipeInput() &&
            m_explicitVertexIns) {
            auto it = m_explicitVertexIns->find(ent.symbol->getName().c_str());
            if (it != m_explicitVertexIns->end()) {
                ent.symbol->getWritableType().getQualifier().layoutLocation = it->second;
            }
        }
        TDefaultGlslIoResolver::reserverStorageSlot(ent, infoSink);
    }

    int resolveInOutLocation(EShLanguage stage, glslang::TVarEntryInfo& ent) override {
        const glslang::TType& type = ent.symbol->getType();
        if (!ent.live && stage == EShLangVertex && type.getQualifier().isPipeInput() &&
            !type.getQualifier().hasLocation() && !type.isBuiltIn()) {
            // GL/ES only guarantee GL_MAX_VERTEX_ATTRIBS >= 16; locations
            // survive into the MSL SPIRV-Cross emits, so stay inside [0,15].
            const int size = std::max(1, (int)computeTypeLocationSize(type, stage));
            if (m_nextInactiveVertexInLocation - (size - 1) >= 0) {
                m_nextInactiveVertexInLocation -= (size - 1);
                ent.symbol->getWritableType().getQualifier().layoutLocation =
                    m_nextInactiveVertexInLocation;
                --m_nextInactiveVertexInLocation;
            }
        }
        return TDefaultGlslIoResolver::resolveInOutLocation(stage, ent);
    }

private:
    EShLanguage m_stage;
    const std::unordered_map<std::string, GLuint>* m_explicitVertexIns;
    static constexpr int kInactiveVertexInLocationTop = 15;
    int m_nextInactiveVertexInLocation = kInactiveVertexInLocationTop;
};

// ---------------------------------------------------------------------------
// Cross-stage descriptor binding remapping (SPIRV-Reflect).
//
// glslang's auto binding assignment is per-TShader and unreliable across
// stages, so after link the SPIR-V can contain colliding bindings (e.g. the
// vertex and fragment mithril_GlobalBlock both at binding 0) and
// undecorated inactive samplers. remap_descriptor_bindings() fixes both by
// reassigning every descriptor binding deterministically:
//
//   - bindings are walked in (set, binding, spirv_id) order, VS first then
//     FS, and reassigned 0, 1, 2, ... across BOTH stages (the same walk
//     order MobileGL's RemapDescriptorBindingsForVulkan uses);
//   - each stage keeps its own block bindings. In particular the vertex and
//     fragment mithril_GlobalBlock are SEPARATE bindings: the two blocks
//     carry disjoint member sets with independent std140 offsets (both start
//     at 0), so folding them into one descriptor would make the fragment
//     stage read matrix bytes where a colour belongs. Keeping them separate
//     matches the semantics of the old per-stage 64-slot offset scheme,
//     without the hack: each block gets its own backing store and there is
//     no cross-stage collision possible by construction;
//   - every binding is moved to descriptor set 0, and missing Binding
//     decorations (inactive samplers) are written by SPIRV-Reflect;
//   - all variants of one program must be remapped with the same key map so
//     the Y-flipped vertex variant and the non-flipped variant agree on
//     bindings (a program's descriptor set is reflected once from the
//     non-flipped SPIR-V).
//
// Returns false only on malformed SPIR-V.
// ---------------------------------------------------------------------------
static bool remap_descriptor_bindings(
    std::vector<uint32_t>& vs,
    std::vector<uint32_t>& fs,
    uint32_t& nextBinding) {
    std::vector<std::vector<uint32_t>*> modules = {&vs, &fs};
    std::vector<SpvReflectShaderModule> reflectModules;
    reflectModules.reserve(2);

    for (auto* spv : modules) {
        if (spv->empty()) continue;
        SpvReflectShaderModule module{};
        if (spvReflectCreateShaderModule(spv->size() * sizeof(uint32_t),
                                         spv->data(), &module) != SPV_REFLECT_RESULT_SUCCESS) {
            return false;
        }
        reflectModules.push_back(module);

        uint32_t bindingCount = 0;
        if (spvReflectEnumerateDescriptorBindings(&module, &bindingCount, nullptr) !=
            SPV_REFLECT_RESULT_SUCCESS) {
            return false;
        }
        std::vector<SpvReflectDescriptorBinding*> bindings(bindingCount);
        if (bindingCount > 0 &&
            spvReflectEnumerateDescriptorBindings(&module, &bindingCount, bindings.data()) !=
            SPV_REFLECT_RESULT_SUCCESS) {
            return false;
        }
        std::sort(bindings.begin(), bindings.end(),
                  [](const SpvReflectDescriptorBinding* a,
                     const SpvReflectDescriptorBinding* b) {
                      if (a->set != b->set) return a->set < b->set;
                      if (a->binding != b->binding) return a->binding < b->binding;
                      return a->spirv_id < b->spirv_id;
                  });

        // Collect the (stable spirv_id -> assigned binding) plan first.
        // IMPORTANT: spvReflectChangeDescriptorBindingNumbers can reallocate
        // the module's descriptor_bindings array (SynchronizeDescriptorSets
        // -> ParseDescriptorSets), so a pointer held across two changes can
        // dangle. We therefore never cache descriptor pointers across a
        // change: each change re-enumerates and re-finds by spirv_id.
        std::vector<std::pair<uint32_t, uint32_t>> plan;  // (spirv_id, binding)
        plan.reserve(bindings.size());
        for (auto* binding : bindings) {
            plan.emplace_back(binding->spirv_id, nextBinding++);
        }

        for (const auto& [targetId, assigned] : plan) {
            // Re-enumerate; the array may have been reallocated by a prior
            // change (re-enumeration is cheap either way).
            uint32_t count = 0;
            if (spvReflectEnumerateDescriptorBindings(&module, &count, nullptr) !=
                SPV_REFLECT_RESULT_SUCCESS) {
                return false;
            }
            std::vector<SpvReflectDescriptorBinding*> cur(count);
            if (count > 0 &&
                spvReflectEnumerateDescriptorBindings(&module, &count, cur.data()) !=
                SPV_REFLECT_RESULT_SUCCESS) {
                return false;
            }
            SpvReflectDescriptorBinding* target = nullptr;
            for (auto* b : cur) {
                if (b->spirv_id == targetId) { target = b; break; }
            }
            if (!target) return false;  // descriptor vanished; malformed SPIR-V

            if (target->binding != assigned || target->set != 0) {
                // glslang emits every descriptor at set 0, so the common
                // case only rewrites the Binding word. Passing
                // SPV_REFLECT_SET_NUMBER_DONT_CHANGE avoids the expensive
                // SynchronizeDescriptorSets rebuild (which reallocates the
                // module's descriptor arrays) on every change.
                const uint32_t newSet =
                    (target->set != 0) ? 0u : SPV_REFLECT_SET_NUMBER_DONT_CHANGE;
                if (spvReflectChangeDescriptorBindingNumbers(&module, target, assigned, newSet) !=
                    SPV_REFLECT_RESULT_SUCCESS) {
                    return false;
                }
            }
        }
    }

    // Write the modified SPIR-V back into the caller's vectors.
    // reflectModules was filled only for non-empty modules; `modules` maps
    // 1:1 to them in the same order.
    size_t m = 0;
    for (size_t i = 0; i < modules.size(); ++i) {
        if (modules[i]->empty()) continue;
        const uint32_t* code = spvReflectGetCode(&reflectModules[m]);
        const uint32_t codeSize = spvReflectGetCodeSize(&reflectModules[m]);
        if (code && codeSize > 0) {
            modules[i]->assign(code, code + codeSize / sizeof(uint32_t));
        }
        spvReflectDestroyShaderModule(&reflectModules[m]);
        ++m;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Per-stage glslang configuration + parse (MobileGL ParseShaderSource).
// ---------------------------------------------------------------------------
struct StageConfig {
    glslang::TShader* shader;
    EShLanguage lang;
    const std::string* source;
};

bool parse_stage(const StageConfig& cfg, const TBuiltInResource& resources,
                 std::string& info) {
    glslang::TShader& sh = *cfg.shader;
    const char* src = cfg.source->c_str();
    sh.setStrings(&src, 1);
    sh.setNanMinMaxClamp(true);
    sh.setPreamble(
        "#undef VULKAN\n"
        "#define MG_MITHRIL 1\n"
        "#define MG_MITHRIL_VERSION 1000000\n");
    sh.setEnvInput(glslang::EShSourceGlsl, cfg.lang, glslang::EShClientVulkan, 450);
    sh.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
    sh.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
    // EXT_vulkan_glsl_relaxed: accept desktop builtins (gl_VertexID etc.) and
    // let glslang fold loose uniforms into the global block automatically.
    sh.setEnvInputVulkanRulesRelaxed();
    sh.setAutoMapLocations(true);
    sh.setAutoMapBindings(true);
    sh.setGlobalUniformBlockName(kGlobalBlockName);

    // EShMsgDefault exactly as MobileGL uses; the Vulkan client dialect
    // already enforces Vulkan rules, and EShMsgVulkanRules on top would
    // re-impose the strict "no loose uniforms" check the relaxed mode is
    // meant to lift.
    if (!sh.parse(&resources, 460, ECoreProfile,
                  /*forceDefaultVersionAndProfile:*/ false,
                  /*forwardCompatible:*/ true, EShMsgDefault)) {
        info = sh.getInfoLog();
        info += sh.getInfoDebugLog();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// One full cross-stage compile: parse VS+FS, link, mapIO, emit SPIR-V for
// both stages, remap descriptor bindings.
// ---------------------------------------------------------------------------
bool compile_program(const std::string& vs_src, const std::string& fs_src,
                     const std::unordered_map<std::string, GLuint>* attrib_bindings,
                     uint32_t* sharedNextBinding,
                     std::vector<uint32_t>& out_vs,
                     std::vector<uint32_t>& out_fs,
                     std::string& info) {
    glslang_init();
    const TBuiltInResource* resources = GetDefaultResources();

    glslang::TShader vs(EShLangVertex);
    glslang::TShader fs(EShLangFragment);

    const StageConfig cfgs[2] = {
        {&vs, EShLangVertex, &vs_src},
        {&fs, EShLangFragment, &fs_src},
    };
    for (const auto& c : cfgs) {
        std::string err;
        if (!parse_stage(c, *resources, err)) {
            info = err;
            return false;
        }
    }

    glslang::TProgram program;
    program.addShader(&vs);
    program.addShader(&fs);
    if (!program.link(EShMsgDefault)) {
        info = program.getInfoLog();
        info += program.getInfoDebugLog();
        return false;
    }

    // mapIO: pin glBindAttribLocation mappings onto stage inputs (and
    // perform glslang's cross-stage interface matching).
    MithrilIoResolver resolver(*program.getIntermediate(EShLangVertex),
                               EShLangVertex, attrib_bindings);
    glslang::TIoMapper* ioMapper = glslang::GetGlslIoMapper();
    if (!program.mapIO(&resolver, ioMapper)) {
        info = program.getInfoLog();
        info += program.getInfoDebugLog();
        return false;
    }

    glslang::SpvOptions spv_opts;
    spv_opts.disableOptimizer = false;
    glslang::GlslangToSpv(*program.getIntermediate(EShLangVertex), out_vs, &spv_opts);
    glslang::GlslangToSpv(*program.getIntermediate(EShLangFragment), out_fs, &spv_opts);
    if (out_vs.empty() || out_fs.empty()) {
        info = "SPIR-V generation produced no words";
        return false;
    }

    // Cross-stage descriptor binding remap. When sharedNextBinding is
    // provided (Y-flipped variant of a program whose non-flipped variant
    // was already remapped), reuse its counter so both variants agree on
    // bindings even if their resource order ever diverged.
    uint32_t localNext = 0;
    uint32_t* next = sharedNextBinding ? sharedNextBinding : &localNext;
    if (!remap_descriptor_bindings(out_vs, out_fs, *next)) {
        info = "SPIRV-Reflect binding remap failed";
        return false;
    }
    return true;
}

// FNV-1a 64-bit hash for cache keying.
uint64_t fnv1a(const std::string& s) {
    uint64_t h = 1469598103934665603ULL;
    for (char c : s) { h ^= (uint8_t)c; h *= 1099511628211ULL; }
    return h;
}

struct LinkCache {
    std::mutex mu;
    // key -> (vertexSpirv, vertexSpirvFlipped, fragmentSpirv)
    struct Entry {
        std::vector<uint32_t> vs, vsFlip, fs;
    };
    std::unordered_map<uint64_t, Entry> entries;
};
LinkCache& link_cache() { static LinkCache c; return c; }

} // namespace

bool shader_compile_stage(GLenum gl_stage, const std::string& glsl_source,
                          std::string& out_info_log) {
    glslang_init();
    EShLanguage lang = to_esh_stage(gl_stage);
    if (lang == EShLangCount) {
        out_info_log = "unsupported shader stage";
        return false;
    }

    // Same preprocessing as the link path so COMPILE_STATUS matches what a
    // later link would report.
    std::string source = glsl_source;
    ensure_glsl_version(source);
    normalize_vulkan_incompatible_layouts(source);
    normalize_gl_legacy_constructs(source, gl_stage);
    inject_position_fixup(source, gl_stage, /*flip_y=*/false);

    glslang::TShader sh(lang);
    const StageConfig cfg{&sh, lang, &source};
    return parse_stage(cfg, *GetDefaultResources(), out_info_log);
}

bool shader_link_program(const std::string& vs_source, const std::string& fs_source,
                         const std::unordered_map<std::string, GLuint>* attrib_bindings,
                         ShaderLinkOutput& out, std::string& out_info_log) {
    // Cache key: both sources + attrib bindings. The Y-flipped and
    // non-flipped variants are compiled together and cached as a unit so the
    // shared binding assignment stays consistent.
    uint64_t key = fnv1a(vs_source) ^ (fnv1a(fs_source) * 0x9E3779B97F4A7C15ULL);
    if (attrib_bindings) {
        for (const auto& kv : *attrib_bindings) {
            key ^= fnv1a(kv.first) ^ ((uint64_t)kv.second * 0x100000001B3ULL);
        }
    }
    {
        std::lock_guard<std::mutex> lk(link_cache().mu);
        auto it = link_cache().entries.find(key);
        if (it != link_cache().entries.end()) {
            out.vertexSpirv = it->second.vs;
            out.vertexSpirvFlipped = it->second.vsFlip;
            out.fragmentSpirv = it->second.fs;
            return true;
        }
    }

    // Preprocess (shared by both variants).
    std::string vs = vs_source;
    std::string fs = fs_source;
    ensure_glsl_version(vs);
    ensure_glsl_version(fs);
    normalize_vulkan_incompatible_layouts(vs);
    normalize_vulkan_incompatible_layouts(fs);
    normalize_gl_legacy_constructs(vs, GL_VERTEX_SHADER);
    normalize_gl_legacy_constructs(fs, GL_FRAGMENT_SHADER);

    // Non-flipped variant (user FBOs).
    std::string vs_plain = vs;
    inject_position_fixup(vs_plain, GL_VERTEX_SHADER, /*flip_y=*/false);
    std::vector<uint32_t> vsSpv, fsSpv;
    uint32_t nextBinding = 0;
    if (!compile_program(vs_plain, fs, attrib_bindings, &nextBinding,
                         vsSpv, fsSpv, out_info_log)) {
        return false;
    }

    // Y-flipped variant (default framebuffer). The descriptor binding
    // assignment MUST match the non-flipped variant bit-for-bit — the
    // program's descriptor set is reflected from the non-flipped SPIR-V.
    // The flip only edits gl_Position math inside main(), so the resource
    // set (declarations) is identical and an independent walk assigns the
    // same bindings. verify/shader_link_probe.cpp pins this invariant.
    std::string vs_flip = vs;
    inject_position_fixup(vs_flip, GL_VERTEX_SHADER, /*flip_y=*/true);
    std::vector<uint32_t> vsFlipSpv, fsFlipSpv;
    if (!compile_program(vs_flip, fs, attrib_bindings, nullptr,
                         vsFlipSpv, fsFlipSpv, out_info_log)) {
        return false;
    }

    out.vertexSpirv = std::move(vsSpv);
    out.vertexSpirvFlipped = std::move(vsFlipSpv);
    out.fragmentSpirv = std::move(fsSpv);

    LinkCache::Entry entry;
    entry.vs = out.vertexSpirv;
    entry.vsFlip = out.vertexSpirvFlipped;
    entry.fs = out.fragmentSpirv;
    std::lock_guard<std::mutex> lk(link_cache().mu);
    link_cache().entries[key] = std::move(entry);
    return true;
}

// ---- Fallback error shader (red-screen fix) ----
// When a shader fails to compile, the program cannot link and every draw is
// skipped — leaving only the application's glClearColor visible (Minecraft's
// red loading screen). Instead of skipping draws, we substitute a minimal
// fallback shader that renders geometry in solid gray. This proves the render
// pipeline works and avoids the "stuck on clear color" failure mode.
//
// The fallback is compiled via the same link path so the SPIR-V is always
// valid and binding-remapped. Results are cached (thread-safe).
bool get_fallback_spirv(GLenum gl_stage, bool flip_y,
                        std::vector<uint32_t>& out_spirv) {
    static std::mutex fallback_mu;
    static std::unordered_map<uint64_t, std::vector<uint32_t>> fallback_cache;
    uint64_t key = (uint64_t)gl_stage | (flip_y ? (1ULL << 32) : 0);
    {
        std::lock_guard<std::mutex> lk(fallback_mu);
        auto it = fallback_cache.find(key);
        if (it != fallback_cache.end()) {
            out_spirv = it->second;
            return true;
        }
    }

    std::string source;
    if (gl_stage == GL_VERTEX_SHADER) {
        // CRITICAL: fallback VS must NOT read any vertex attributes.
        // Reading layout(location=0) in vec4 a_position when no vertex buffer
        // is bound at binding 0 causes a GPU page fault
        // (kIOGPUCommandBufferCallbackErrorPageFault) on Metal/MoltenVK →
        // VK_ERROR_DEVICE_LOST → permanent device death.
        // Output a fixed degenerate position so all triangles collapse to a
        // single point (nothing drawn, but no crash).
        (void)flip_y;  // no position to flip
        source = "#version 330\n"
                 "void main() {\n"
                 "    gl_Position = vec4(0.0, 0.0, 0.5, 1.0);\n"
                 "}\n";
    } else if (gl_stage == GL_FRAGMENT_SHADER) {
        source = "#version 330\n"
                 "layout(location = 0) out vec4 fragColor;\n"
                 "void main() {\n"
                 "    fragColor = vec4(0.5, 0.5, 0.5, 1.0);\n"
                 "}\n";
    } else {
        return false;
    }

    // Compile the fallback as a VS+FS program so the binding remap runs and
    // the SPIR-V is fully valid; we keep only the requested stage's words.
    std::string dummy_fs =
        "#version 330\n"
        "layout(location = 0) out vec4 fragColor;\n"
        "void main() { fragColor = vec4(0.5, 0.5, 0.5, 1.0); }\n";
    std::string dummy_vs =
        "#version 330\n"
        "void main() { gl_Position = vec4(0.0, 0.0, 0.5, 1.0); }\n";

    ShaderLinkOutput linkOut;
    std::string info;
    const char* vs_use = (gl_stage == GL_VERTEX_SHADER) ? source.c_str() : dummy_vs.c_str();
    const char* fs_use = (gl_stage == GL_FRAGMENT_SHADER) ? source.c_str() : dummy_fs.c_str();
    if (!shader_link_program(vs_use, fs_use, nullptr, linkOut, info)) {
        return false;
    }

    out_spirv = (gl_stage == GL_VERTEX_SHADER) ? linkOut.vertexSpirvFlipped
                                               : linkOut.fragmentSpirv;
    std::lock_guard<std::mutex> lk(fallback_mu);
    fallback_cache[key] = out_spirv;
    return true;
}

} // namespace mithril
