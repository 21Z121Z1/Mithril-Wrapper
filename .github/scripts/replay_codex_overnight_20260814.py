#!/usr/bin/env python3
import base64
import json
from pathlib import Path
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[2]
RECOVERY = ROOT / ".github" / "recovery"
parts = sorted(RECOVERY.glob("overnight-20260814.part*"))
if len(parts) != 8:
    raise SystemExit(f"expected 8 recovery parts, found {len(parts)}")

data = "".join(p.read_text(encoding="utf-8").strip() for p in parts)
events = json.loads(zlib.decompress(base64.b64decode(data)).decode("utf-8"))
if len(events) != 124:
    raise SystemExit(f"expected 124 rollout patch events, found {len(events)}")

for index, event in enumerate(events, 1):
    rel = event["path"]
    diff = event["diff"]
    patch_text = f"--- a/{rel}\n+++ b/{rel}\n{diff}"
    proc = subprocess.run(
        ["patch", "-p1", "--batch", "--forward"],
        cwd=ROOT,
        input=patch_text,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if proc.returncode != 0:
        raise SystemExit(
            f"replay failed at event {index}/124, rollout line {event['line']}, "
            f"path {rel}\n{proc.stdout}"
        )
    print(f"[{index:03d}/124] rollout line {event['line']}: {rel}")

expected = {
    "Mithril-Wrapper-cpp/MG_Backend/Backend.h",
    "Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/CommandStream.cpp",
    "Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/CommandStream.h",
    "Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/DescriptorSet.cpp",
    "Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/Device.cpp",
    "Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/ImageOps.cpp",
    "Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/Pipeline.cpp",
    "Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/Swapchain.h",
    "Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/SwapchainCommon.cpp",
    "Mithril-Wrapper-cpp/MG_Impl/Drawing.cpp",
    "Mithril-Wrapper-cpp/MG_Impl/Framebuffer.cpp",
    "Mithril-Wrapper-cpp/MG_Impl/Getter.cpp",
    "Mithril-Wrapper-cpp/MG_Impl/Getter_gpu.mm",
    "Mithril-Wrapper-cpp/MG_Impl/Program.cpp",
    "Mithril-Wrapper-cpp/MG_Impl/Shader.cpp",
    "Mithril-Wrapper-cpp/MG_Impl/Texture.cpp",
    "Mithril-Wrapper-cpp/MG_Impl/gl.cpp",
    "Mithril-Wrapper-cpp/MG_Impl/lookup.cpp",
    "Mithril-Wrapper-cpp/egl/SurfaceMetal.mm",
    "Mithril-Wrapper-cpp/egl/egl.cpp",
}

status = subprocess.check_output(["git", "status", "--short"], cwd=ROOT, text=True)
changed = set()
for line in status.splitlines():
    if not line:
        continue
    rel = line[3:]
    if not rel.startswith(".github/"):
        changed.add(rel)

if changed != expected:
    raise SystemExit(
        "final rollout file set does not match captured dirty worktree\n"
        f"missing: {sorted(expected - changed)}\n"
        f"unexpected: {sorted(changed - expected)}\n{status}"
    )

subprocess.run(["git", "diff", "--check"], cwd=ROOT, check=True)
print("Replay complete: 124 patch events; exact 20-file dirty set recovered; diff check clean.")
