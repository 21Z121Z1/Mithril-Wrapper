#!/usr/bin/env python3
from pathlib import Path

base = Path("scripts/apply_program_prewarm.py")
script = base.read_text()
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
compile(script, str(base), "exec")
exec(compile(script, str(base), "exec"),
     {"__name__": "__main__", "__file__": str(base), "Path": Path})
