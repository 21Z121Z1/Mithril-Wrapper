#!/usr/bin/env python3
from pathlib import Path

patcher = Path(__file__).with_name("apply_terrain_semantics_fix.py")
source = patcher.read_text()
old = "out, n = re.subn(pattern, repl, s, count=1, flags=re.S)"
new = "out, n = re.subn(pattern, lambda _m: repl, s, count=1, flags=re.S)"
if source.count(old) != 1:
    raise SystemExit("unexpected terrain patcher regex_once implementation")
source = source.replace(old, new, 1)
exec(compile(source, str(patcher), "exec"), {"__name__": "__main__", "__file__": str(patcher)})
