#!/usr/bin/env python3
import base64
import hashlib
import json
from pathlib import Path
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[2]
RECOVERY = ROOT / ".github" / "recovery"
parts = sorted(RECOVERY.glob("overnight-20260814.part*"))
if len(parts) != 8:
    raise SystemExit(f"expected 8 recovery parts, found {len(parts)}")
expected_hashes = [
    "1217b35902eb8f34f32b318b2c120cbf3548aad53e9504a1b1323cf920573e7f",
    "c91e711a99be98bc8681cfd599c05bb0afc9accf0294b8f2070788362af4510f",
    "6581a29096202f2030bf884edc7fea3d0ac274ff94cb6fa1bfd074048029cd87",
    "fbac9285ea8876e01a5babcd20569bad514ea6c7e891c7a3c7fea457c20a9077",
    "1049f0c48273dad16b0b4cfd4c774cba4510a9898c2c8e9e59d81bf57911dcf2",
    "a438d6002c36ba0be7edecdb1e76638e1fe0bf5bcf9c6276787ec05037838590",
    "809cd68f110c6a65b4471ef50a442ecd8c59f61c1a7127a9333d9f457c063c31",
    "94f463f8ecd51f7745c192253972a0b6ddf888e3a0186565e3aa2eae9ee80f24",
]
chunks = []
for index, (part, expected_hash) in enumerate(zip(parts, expected_hashes)):
    chunk = part.read_text(encoding="utf-8").strip()
    actual = hashlib.sha256(chunk.encode()).hexdigest()
    print(f"part{index:02}: len={len(chunk)} sha256={actual}")
    if actual != expected_hash:
        raise SystemExit(f"recovery part {index:02} checksum mismatch: expected {expected_hash}, got {actual}")
    chunks.append(chunk)

data = "".join(chunks)
events = json.loads(zlib.decompress(base64.b64decode(data)).decode("utf-8"))
if len(events) != 124:
    raise SystemExit(f"expected 124 rollout patch events, found {len(events)}")

for index, event in enumerate(events, 1):
    rel = event["path"]
    patch_text = f"--- a/{rel}\n+++ b/{rel}\n{event['diff']}"
    proc = subprocess.run(
        ["patch", "-p1", "--batch", "--forward"], cwd=ROOT,
        input=patch_text, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    )
    if proc.returncode != 0:
        raise SystemExit(
            f"replay failed at event {index}/124, rollout line {event['line']}, path {rel}\n{proc.stdout}"
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
changed = {line[3:] for line in status.splitlines() if line and not line[3:].startswith(".github/")}
if changed != expected:
    raise SystemExit(
        "final rollout file set mismatch\n"
        f"missing: {sorted(expected - changed)}\n"
        f"unexpected: {sorted(changed - expected)}\n{status}"
    )
subprocess.run(["git", "diff", "--check"], cwd=ROOT, check=True)
print("Replay complete: 124 patch events; exact 20-file dirty set recovered; diff check clean.")
