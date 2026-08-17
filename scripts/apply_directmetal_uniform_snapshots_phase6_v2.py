#!/usr/bin/env python3
from pathlib import Path

base = Path("scripts/apply_directmetal_uniform_snapshots_phase6.py")
script = base.read_text()
old = '''exact("src/metal/MetalDeviceSession.h",
''' + "'''" + '''    uint64_t CreateProgram(const std::vector<uint32_t>& vs,
                           const std::vector<uint32_t>& fs);
''' + "'''" + ''',
''' + "'''" + '''    uint64_t CreateProgram(const std::vector<uint32_t>& vs,
                           const std::vector<uint32_t>& fs,
                           const std::vector<std::string>& uniform_names);
''' + "'''" + ''')'''
new = '''exact("src/metal/MetalDeviceSession.h",
''' + "'''" + '''    uint64_t CreateProgram(const std::vector<uint32_t>& vertex,
                           const std::vector<uint32_t>& fragment);
''' + "'''" + ''',
''' + "'''" + '''    uint64_t CreateProgram(const std::vector<uint32_t>& vertex,
                           const std::vector<uint32_t>& fragment,
                           const std::vector<std::string>& uniform_names);
''' + "'''" + ''')'''
if script.count(old) != 1:
    raise SystemExit("MetalDeviceSession.h patch anchor drifted")
script = script.replace(old, new, 1)
exec(compile(script, str(base), "exec"), {"__name__":"__main__", "__file__":str(base)})
