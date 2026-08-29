#!/usr/bin/env python3
from __future__ import annotations
from pathlib import Path
import base64, json, zlib

PAYLOAD = Path(__file__).with_name("dvk_replay_payload.b64")
PATCHES = json.loads(zlib.decompress(base64.b64decode(PAYLOAD.read_text().strip())).decode("utf-8"))
ROOT = Path.cwd()

def normalize_path(raw: str) -> Path:
    for marker in ("Mithril-Wrapper-cpp/", "tests/", ".github/", "ci/"):
        pos = raw.find(marker)
        if pos >= 0:
            return ROOT / raw[pos:]
    if raw.endswith("CMakeLists.txt"):
        return ROOT / "CMakeLists.txt"
    raise RuntimeError(f"cannot normalize patch path: {raw}")

def find_subseq(lines: list[str], needle: list[str]) -> int:
    hits = [i for i in range(len(lines)-len(needle)+1) if lines[i:i+len(needle)] == needle]
    if len(hits) != 1:
        raise RuntimeError(f"hunk match count {len(hits)} != 1: {needle[:8]!r}")
    return hits[0]

def apply_hunk(path: Path, hunk: list[str]) -> None:
    old, new = [], []
    for line in hunk:
        if line.startswith("\\ No newline"):
            continue
        if not line:
            raise RuntimeError(f"malformed empty hunk line in {path}")
        tag, body = line[0], line[1:]
        if tag == " ":
            old.append(body); new.append(body)
        elif tag == "-":
            old.append(body)
        elif tag == "+":
            new.append(body)
        else:
            raise RuntimeError(f"malformed hunk line in {path}: {line!r}")
    text = path.read_text(encoding="utf-8")
    had_nl = text.endswith("\n")
    lines = text.splitlines()
    at = find_subseq(lines, old)
    lines[at:at+len(old)] = new
    path.write_text("\n".join(lines) + ("\n" if had_nl else ""), encoding="utf-8")

def apply_patch(patch: str) -> None:
    lines = patch.splitlines()
    if lines[0] != "*** Begin Patch" or lines[-1] != "*** End Patch":
        raise RuntimeError("bad patch envelope")
    current = None
    hunk = []
    def flush():
        nonlocal hunk
        if hunk:
            if current is None:
                raise RuntimeError("hunk without file")
            apply_hunk(current, hunk)
            hunk = []
    for line in lines[1:-1]:
        if line.startswith("*** Update File: "):
            flush()
            current = normalize_path(line[len("*** Update File: "):])
            if not current.is_file():
                raise RuntimeError(f"missing file: {current}")
        elif line.startswith("@@"):
            flush()
        elif line.startswith("*** "):
            raise RuntimeError(f"unsupported directive: {line}")
        else:
            hunk.append(line)
    flush()

for idx, patch in enumerate(PATCHES, 1):
    print(f"applying validated Codex patch {idx}/{len(PATCHES)}")
    apply_patch(patch)
print("validated Codex replay complete")
