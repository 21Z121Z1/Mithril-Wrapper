#!/usr/bin/env python3
from pathlib import Path

base = Path("scripts/apply_directmetal_uniform_snapshots_phase6.py")
script = base.read_text()

def replace_script_block(old: str, new: str, label: str) -> None:
    global script
    actual = script.count(old)
    if actual != 1:
        raise SystemExit(f"{label}: expected one patch block, found {actual}")
    script = script.replace(old, new, 1)

# 1. MetalDeviceSession uses vertex/fragment parameter names.
replace_script_block(
r'''exact("src/metal/MetalDeviceSession.h",
''' + "'''" + r'''    uint64_t CreateProgram(const std::vector<uint32_t>& vs,
                           const std::vector<uint32_t>& fs);
''' + "'''" + r''',
''' + "'''" + r'''    uint64_t CreateProgram(const std::vector<uint32_t>& vs,
                           const std::vector<uint32_t>& fs,
                           const std::vector<std::string>& uniform_names);
''' + "'''" + r''')''',
r'''exact("src/metal/MetalDeviceSession.h",
''' + "'''" + r'''    uint64_t CreateProgram(const std::vector<uint32_t>& vertex,
                           const std::vector<uint32_t>& fragment);
''' + "'''" + r''',
''' + "'''" + r'''    uint64_t CreateProgram(const std::vector<uint32_t>& vertex,
                           const std::vector<uint32_t>& fragment,
                           const std::vector<std::string>& uniform_names);
''' + "'''" + r''')''',
"MetalDeviceSession CreateProgram")

# 2. engine.mm has three draw loops; insert snapshot sizing only in
# RequiredUploadBytes(). This source code is injected into the base patcher.
replace_script_block(
r'''exact("src/metal/engine.mm",
''' + "'''" + r'''    for (const auto& pending : engine.draws) {
''' + "'''" + r''',
''' + "'''" + r'''    std::unordered_set<const PackedUniformSnapshot*> counted_uniform_snapshots;
    for (const auto& pending : engine.draws) {
''' + "'''" + r''', count=1)''',
r'''p = Path("src/metal/engine.mm")
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
    "    std::unordered_set<const PackedUniformSnapshot*> "
    "counted_uniform_snapshots;\n" + needle,
    1)
p.write_text(text[:function_begin] + body + text[function_end:])''',
"RequiredUploadBytes draw loop")

# 3. TranslateStage is nested inside @autoreleasepool, so preserve the actual
# 8/12-space indentation when adding cold-path name->slot resolution.
replace_script_block(
r'''exact("src/metal/engine.mm",
''' + "'''" + r'''    if (!TranslateStage(vs, spv::ExecutionModelVertex, &program.vertex) ||
        !TranslateStage(fs, spv::ExecutionModelFragment, &program.fragment))
        return 0;
''' + "'''" + r''',
''' + "'''" + r'''    if (!TranslateStage(vs, spv::ExecutionModelVertex, &program.vertex) ||
        !TranslateStage(fs, spv::ExecutionModelFragment, &program.fragment) ||
        !ResolveUniformMemberSlots(&program.vertex, uniform_names) ||
        !ResolveUniformMemberSlots(&program.fragment, uniform_names))
        return 0;
''' + "'''" + r''')''',
r'''exact("src/metal/engine.mm",
''' + "'''" + r'''        if (!TranslateStage(vs, spv::ExecutionModelVertex, &program.vertex) ||
            !TranslateStage(fs, spv::ExecutionModelFragment, &program.fragment))
            return 0;
''' + "'''" + r''',
''' + "'''" + r'''        if (!TranslateStage(vs, spv::ExecutionModelVertex, &program.vertex) ||
            !TranslateStage(fs, spv::ExecutionModelFragment, &program.fragment) ||
            !ResolveUniformMemberSlots(&program.vertex, uniform_names) ||
            !ResolveUniformMemberSlots(&program.fragment, uniform_names))
            return 0;
''' + "'''" + r''')''',
"Metal TranslateStage")

compile(script, str(base), "exec")
exec(compile(script, str(base), "exec"),
     {"__name__":"__main__", "__file__":str(base), "Path":Path})
