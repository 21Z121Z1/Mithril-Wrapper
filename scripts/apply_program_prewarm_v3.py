#!/usr/bin/env python3
from pathlib import Path

base = Path("scripts/apply_program_prewarm.py")
script = base.read_text()

# Current shader.cpp gets shader types through internal.h and has a separate
# standard-header block. Anchor the diagnostic include only to util/log.h.
start = script.index("# shader.cpp owns the frontend prewarm policy and diagnostics.")
end = script.index("\ninsert_before(\n    \"src/gl/shader.cpp\"", start)
old_include = "#include <util/log.h>\n"
new_include = "#include <util/log.h>\n#include <mithril/program_diagnostics.h>\n"
replacement = (
    "# shader.cpp owns the frontend prewarm policy and diagnostics.\n"
    "replace_once(\n"
    "    \"src/gl/shader.cpp\",\n"
    f"    {old_include!r},\n"
    f"    {new_include!r})\n"
)
script = script[:start] + replacement + script[end:]

# Phase 7 changed the lazy program map insertion from operator[] to emplace.
# Keep the deletion anchor byte-for-byte aligned with current draw.cpp.
old_map_insert = "if (handle) g_backend_programs[prog->id] = handle;"
new_map_insert = "if (handle) g_backend_programs.emplace(prog->id, handle);"
if script.count(old_map_insert) != 1:
    raise SystemExit(
        f"draw helper patch anchor drifted: {script.count(old_map_insert)}")
script = script.replace(old_map_insert, new_map_insert, 1)

compile(script, str(base), "exec")
exec(compile(script, str(base), "exec"),
     {"__name__": "__main__", "__file__": str(base), "Path": Path})
