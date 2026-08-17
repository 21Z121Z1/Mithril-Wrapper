#!/usr/bin/env python3
from pathlib import Path

base = Path("scripts/apply_directmetal_uniform_snapshots_phase6.py")
script = base.read_text()

# Carry the already-verified session-header anchor correction from v2.
old_session = '''exact("src/metal/MetalDeviceSession.h",
''' + "'''" + '''    uint64_t CreateProgram(const std::vector<uint32_t>& vs,
                           const std::vector<uint32_t>& fs);
''' + "'''" + ''',
''' + "'''" + '''    uint64_t CreateProgram(const std::vector<uint32_t>& vs,
                           const std::vector<uint32_t>& fs,
                           const std::vector<std::string>& uniform_names);
''' + "'''" + ''')'''
new_session = '''exact("src/metal/MetalDeviceSession.h",
''' + "'''" + '''    uint64_t CreateProgram(const std::vector<uint32_t>& vertex,
                           const std::vector<uint32_t>& fragment);
''' + "'''" + ''',
''' + "'''" + '''    uint64_t CreateProgram(const std::vector<uint32_t>& vertex,
                           const std::vector<uint32_t>& fragment,
                           const std::vector<std::string>& uniform_names);
''' + "'''" + ''')'''
if script.count(old_session) != 1:
    raise SystemExit("MetalDeviceSession.h patch anchor drifted")
script = script.replace(old_session, new_session, 1)

# The source contains three loops over engine.draws. Only RequiredUploadBytes()
# must receive the unique-snapshot sizing set. Replace the patcher's ambiguous
# global exact() with a function-scoped source transformation.
old_block = '''exact("src/metal/engine.mm",
''' + "'''" + '''    for (const auto& pending : engine.draws) {
''' + "'''" + ''',
''' + "'''" + '''    std::unordered_set<const PackedUniformSnapshot*> counted_uniform_snapshots;
    for (const auto& pending : engine.draws) {
''' + "'''" + ''', count=1)'''
new_block = '''p = Path("src/metal/engine.mm")
text = p.read_text()
function_begin = text.index("NSUInteger RequiredUploadBytes() {")
function_end = text.index("\nNSUInteger AllocateUpload(", function_begin)
body = text[function_begin:function_end]
needle = "    for (const auto& pending : engine.draws) {\\n"
if body.count(needle) != 1:
    raise SystemExit(
        f"RequiredUploadBytes: expected one draw loop, found {body.count(needle)}")
body = body.replace(
    needle,
    "    std::unordered_set<const PackedUniformSnapshot*> "
    "counted_uniform_snapshots;\\n" + needle,
    1)
p.write_text(text[:function_begin] + body + text[function_end:])'''
if script.count(old_block) != 1:
    raise SystemExit("ambiguous RequiredUploadBytes patch block drifted")
script = script.replace(old_block, new_block, 1)

exec(compile(script, str(base), "exec"),
     {"__name__":"__main__", "__file__":str(base), "Path":Path})
