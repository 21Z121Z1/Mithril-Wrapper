#!/usr/bin/env python3
"""Fork-context adapter for the strict 2026-08-31 Codex rollout replay."""
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path

path = Path(__file__).with_name("replay_rollout_dvk_local_patches.py")
spec = spec_from_file_location("rollout_replay", path)
assert spec and spec.loader
mod = module_from_spec(spec)
spec.loader.exec_module(mod)

# The recovered upstream CMake hunk used the next SPIRV-Cross comment as
# trailing context.  The fork has since expanded that comment to describe the
# dual-backend build, while the two executable anchor lines are unchanged.
# Remove only that stale context line; the strict replayer still requires a
# unique exact match for the actual glslang configuration anchor.
stale = "\n \n # SPIRV-Cross: used for SPIR-V reflection (spirv_cross::Compiler +"
assert mod.PATCHES[1].count(stale) == 1
mod.PATCHES[1] = mod.PATCHES[1].replace(stale, "", 1)

raise SystemExit(mod.main())
