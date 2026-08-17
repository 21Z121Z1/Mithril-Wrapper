#!/usr/bin/env python3
from pathlib import Path

base = Path("scripts/apply_directmetal_uniform_snapshots_phase6.py")
script = base.read_text()

def swap(old: str, new: str, label: str) -> None:
    global script
    n = script.count(old)
    if n != 1:
        raise SystemExit(f"{label}: expected one patch block, found {n}")
    script = script.replace(old, new, 1)

swap(r"""exact("src/metal/MetalDeviceSession.h",
'''    uint64_t CreateProgram(const std::vector<uint32_t>& vs,
                           const std::vector<uint32_t>& fs);
''',
'''    uint64_t CreateProgram(const std::vector<uint32_t>& vs,
                           const std::vector<uint32_t>& fs,
                           const std::vector<std::string>& uniform_names);
''')""",
r"""exact("src/metal/MetalDeviceSession.h",
'''    uint64_t CreateProgram(const std::vector<uint32_t>& vertex,
                           const std::vector<uint32_t>& fragment);
''',
'''    uint64_t CreateProgram(const std::vector<uint32_t>& vertex,
                           const std::vector<uint32_t>& fragment,
                           const std::vector<std::string>& uniform_names);
''')""", "MetalDeviceSession")

swap(r"""exact("src/metal/engine.mm",
'''    for (const auto& pending : engine.draws) {
''',
'''    std::unordered_set<const PackedUniformSnapshot*> counted_uniform_snapshots;
    for (const auto& pending : engine.draws) {
''', count=1)""",
r"""p = Path("src/metal/engine.mm")
text = p.read_text()
function_begin = text.index("NSUInteger RequiredUploadBytes() {")
function_end = text.index("\nNSUInteger AllocateUpload(", function_begin)
body = text[function_begin:function_end]
needle = "    for (const auto& pending : engine.draws) {\n"
if body.count(needle) != 1:
    raise SystemExit(
        f"RequiredUploadBytes: expected one draw loop, found {body.count(needle)}")
body = body.replace(
    needle,
    "    std::unordered_set<const PackedUniformSnapshot*> counted_uniform_snapshots;\n" + needle,
    1)
p.write_text(text[:function_begin] + body + text[function_end:])""",
"RequiredUploadBytes")

swap(r"""exact("src/metal/engine.mm",
'''    if (!TranslateStage(vs, spv::ExecutionModelVertex, &program.vertex) ||
        !TranslateStage(fs, spv::ExecutionModelFragment, &program.fragment))
        return 0;
''',
'''    if (!TranslateStage(vs, spv::ExecutionModelVertex, &program.vertex) ||
        !TranslateStage(fs, spv::ExecutionModelFragment, &program.fragment) ||
        !ResolveUniformMemberSlots(&program.vertex, uniform_names) ||
        !ResolveUniformMemberSlots(&program.fragment, uniform_names))
        return 0;
''')""",
r"""exact("src/metal/engine.mm",
'''        if (!TranslateStage(vs, spv::ExecutionModelVertex, &program.vertex) ||
            !TranslateStage(fs, spv::ExecutionModelFragment, &program.fragment))
            return 0;
''',
'''        if (!TranslateStage(vs, spv::ExecutionModelVertex, &program.vertex) ||
            !TranslateStage(fs, spv::ExecutionModelFragment, &program.fragment) ||
            !ResolveUniformMemberSlots(&program.vertex, uniform_names) ||
            !ResolveUniformMemberSlots(&program.fragment, uniform_names))
            return 0;
''')""", "Metal TranslateStage")

swap(r"""exact("src/vk/draw.cpp",
'''    if (prog.has_ubo) {
        std::vector<uint8_t> bytes(prog.ubo_size, 0);
        for (const auto& m : prog.members) {
            auto it = params.uniforms.find(m.name);
            if (it != params.uniforms.end() &&
                !backend::PackUniformValue(
                    m, it->second, bytes.data(), bytes.size())) {
                ML_LOG_ERROR("vk: invalid reflected layout for uniform '%s'",
                             m.name.c_str());
                DestroyDrawOp(op);
                return;
            }
        }
''',""",
r"""exact("src/vk/draw.cpp",
'''    if (prog.has_ubo) {
        std::vector<uint8_t> bytes((size_t)prog.ubo_size, 0);
        for (const auto& m : prog.members) {
            auto it = params.uniforms.find(m.name);
            if (it == params.uniforms.end()) continue;
            if (!backend::PackUniformValue(
                    m, it->second, bytes.data(), bytes.size())) {
                ML_LOG_ERROR("vk: invalid reflected layout for uniform '%s'",
                             m.name.c_str());
                return;
            }
        }
''',""", "Vulkan loose UBO body")

compile(script, str(base), "exec")
exec(compile(script, str(base), "exec"),
     {"__name__":"__main__", "__file__":str(base), "Path":Path})
