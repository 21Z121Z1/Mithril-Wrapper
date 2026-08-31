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
# trailing context. The fork has since expanded that comment to describe the
# dual-backend build, while the two executable anchor lines are unchanged.
stale = "\n \n # SPIRV-Cross: used for SPIR-V reflection (spirv_cross::Compiler +"
assert mod.PATCHES[1].count(stale) == 1
mod.PATCHES[1] = mod.PATCHES[1].replace(stale, "", 1)

def fork_semantic_gates() -> None:
    desc = Path("Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/DescriptorSet.cpp").read_text()
    cmake = Path("CMakeLists.txt").read_text()
    cmd_h = Path("Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/CommandStream.h").read_text()
    cmd_cpp = Path("Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/CommandStream.cpp").read_text()
    img = Path("Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/ImageOps.cpp").read_text()

    assert "MITHRIL_HIDE_GLSLANG_SYMBOLS" in cmake
    assert "active_swapchain_color_layout();" in cmd_h
    assert "VkImageLayout active_swapchain_color_layout()" in cmd_cpp
    assert "constexpr uint64_t kOneShotFenceTimeoutNs = 5ull" in img
    assert "BlitLayouts resolve_blit_layouts(VkImage image)" in img
    assert "fill_blit_layout_access(" in img
    assert "src_layouts.initial, src_layouts.final" in img
    assert "dst_layouts.initial, dst_layouts.final" in img

    # The ownership fix is deliberately local. Clearing the CPU ownership list
    # is correct after vkResetDescriptorPool; it is wrong at a mere generation
    # boundary or resource-memo invalidation because those paths do not free the
    # VkDescriptorSets back to the pool.
    bind = desc[desc.index("void bind_program_descriptors("):]
    bind = bind[:bind.index("void invalidate_descriptor_memo()")]
    boundary = bind[bind.index("if (pr.lastFrameGen[slot] != b->frameGeneration"):]
    assert "pr.allocatedSets[slot].clear();" not in boundary

    invalidate = desc[desc.index("void invalidate_descriptor_memo()"):]
    invalidate = invalidate[:invalidate.index("void reset_all_descriptor_pools()")]
    assert "pr.allocatedSets[i].clear();" not in invalidate
    assert "pr.setCursor[i] = 0;" not in invalidate

mod.semantic_gates = fork_semantic_gates
raise SystemExit(mod.main())
