#!/usr/bin/env python3
"""Compile a compact task-local world model for a Mithril agent."""

from __future__ import annotations

import argparse
import fnmatch
import json
import pathlib
import re
import subprocess
import sys
from typing import Any, Optional

ROOT = pathlib.Path(__file__).resolve().parents[1]
MANIFEST_PATH = ROOT / "docs/agent/manifest.json"


def git(*args: str) -> Optional[str]:
    proc = subprocess.run(
        ["git", *args], cwd=ROOT, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
    )
    return proc.stdout.strip() if proc.returncode == 0 else None


def git_is_ancestor(a: str, b: str) -> bool:
    return subprocess.run(
        ["git", "merge-base", "--is-ancestor", a, b], cwd=ROOT,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    ).returncode == 0


def load_manifest() -> dict[str, Any]:
    return json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))


def resolve_ref(name: str) -> Optional[str]:
    for candidate in (f"refs/remotes/origin/{name}", f"refs/heads/{name}", name):
        if git("rev-parse", "--verify", candidate):
            return candidate
    return None


def relation(anchor_ref: str, head_ref: str = "HEAD") -> dict[str, Any]:
    mb = git("merge-base", anchor_ref, head_ref)
    if not mb:
        return {"relation": "disconnected", "merge_base": None, "ahead": None, "behind": None}
    counts = git("rev-list", "--left-right", "--count", f"{anchor_ref}...{head_ref}")
    if not counts:
        return {"relation": "unresolved", "merge_base": mb, "ahead": None, "behind": None}
    behind_s, ahead_s = counts.split()
    behind, ahead = int(behind_s), int(ahead_s)
    if ahead == 0 and behind == 0:
        kind = "same"
    elif git_is_ancestor(anchor_ref, head_ref):
        kind = "descendant"
    elif git_is_ancestor(head_ref, anchor_ref):
        kind = "ancestor"
    else:
        kind = "diverged"
    return {"relation": kind, "merge_base": mb, "ahead": ahead, "behind": behind}


def choose_anchor(manifest: dict[str, Any]) -> tuple[Optional[str], Optional[str], dict[str, Any]]:
    relations: dict[str, Any] = {}
    candidates: list[tuple[int, int, str, str]] = []
    for universe in manifest.get("history_universes", []):
        for anchor in universe.get("anchors", []):
            ref = resolve_ref(anchor)
            if not ref:
                relations[anchor] = {"relation": "anchor_unavailable", "merge_base": None, "ahead": None, "behind": None}
                continue
            item = relation(ref)
            relations[anchor] = item
            if item["relation"] != "disconnected" and item["ahead"] is not None:
                candidates.append((item["ahead"] + item["behind"], item["behind"], universe["id"], anchor))
    if not candidates:
        return None, None, relations
    candidates.sort()
    _, _, universe_id, anchor = candidates[0]
    return universe_id, anchor, relations


def changed_files(anchor: Optional[str]) -> list[str]:
    if not anchor:
        return []
    ref = resolve_ref(anchor)
    if not ref or not git("merge-base", ref, "HEAD"):
        return []
    raw = git("diff", "--name-only", f"{ref}...HEAD") or ""
    return [line for line in raw.splitlines() if line]


def path_matches(path: str, pattern: str) -> bool:
    if fnmatch.fnmatchcase(path, pattern):
        return True
    if pattern.endswith("/**"):
        return path.startswith(pattern[:-3].rstrip("/") + "/")
    return path == pattern


def specificity(pattern: str) -> int:
    return len(pattern.replace("*", "").replace("?", ""))


def classify_paths(manifest: dict[str, Any], files: list[str]) -> tuple[list[str], dict[str, list[str]], list[str]]:
    mapping: dict[str, list[str]] = {}
    unclassified: list[str] = []
    for path in files:
        matches: list[tuple[int, str]] = []
        for component in manifest.get("components", []):
            for pattern in component.get("owned_paths", []):
                if path_matches(path, pattern):
                    matches.append((specificity(pattern), component["id"]))
        if not matches:
            unclassified.append(path)
            continue
        best = max(score for score, _ in matches)
        for _, component_id in sorted({(score, cid) for score, cid in matches if score == best}):
            mapping.setdefault(component_id, []).append(path)
    return sorted(mapping), mapping, sorted(unclassified)


def tokenize(task: str) -> set[str]:
    return {token for token in re.findall(r"[a-z0-9_.+-]+", task.lower()) if len(token) > 1}


def infer_task_components(task: str) -> list[str]:
    tokens = tokenize(task)
    hints = {
        "host.egl": {"egl", "surface", "context", "amethyst", "glfw"},
        "gl.semantics": {"gl", "opengl", "state", "fbo", "framebuffer", "pixel", "unpack", "pack", "query", "sync", "texture", "buffer", "draw"},
        "shader.contract": {"shader", "spirv", "glsl", "reflection", "ubo", "uniform", "sampler", "location"},
        "semantic.lowering": {"lowering", "drawparams", "backend-neutral", "semantic", "snapshot", "intent", "lifetime", "generation"},
        "backend.directmetal": {"metal", "directmetal", "mtl", "pso"},
        "backend.directvulkan": {"vulkan", "directvulkan", "moltenvk", "descriptor", "vk"},
        "platform.presentation": {"presentation", "present", "drawable", "cametallayer", "orientation", "yflip", "bgra", "iphoneos"},
        "validation.contract": {"test", "oracle", "smoke", "readback", "validation"},
        "evaluation.control": {"ci", "workflow", "agent", "branch", "evidence", "github", "audit"},
        "legacy.migration": {"legacy", "replay", "patch", "rollout", "transplant", "mithril-wrapper-cpp"},
    }
    scored: list[tuple[int, str]] = []
    for component_id, words in hints.items():
        score = 0
        for token in tokens:
            if token in words:
                score += 5
            elif any(token in word or word in token for word in words):
                score += 2
        if score:
            scored.append((score, component_id))
    scored.sort(key=lambda x: (-x[0], x[1]))
    return [component_id for _, component_id in scored[:2]]


def infer_claim(task: str, direct: list[str], requested: str) -> str:
    if requested != "auto":
        return requested
    tokens = tokenize(task)
    if direct and set(direct) <= {"evaluation.control"}:
        return "control"
    if tokens & {"performance", "perf", "fps", "latency", "throughput", "p95", "optimize", "optimization"}:
        return "performance"
    if tokens & {"presentation", "present", "drawable", "cametallayer", "orientation", "device", "iphone", "ipad"}:
        return "presentation"
    if tokens & {"minecraft", "e2e", "integration", "runtime", "startup", "gui"}:
        return "integration"
    return "semantic"


def boundaries_for(manifest: dict[str, Any], direct: list[str]) -> list[dict[str, Any]]:
    active = set(direct)
    result = []
    for boundary in manifest.get("boundaries", []):
        sources = set(boundary.get("from", []))
        targets = set(boundary.get("to", []))
        if active & sources:
            result.append(boundary)
        elif boundary.get("id") == "legacy_transplant" and active & targets:
            result.append(boundary)
    return result


def proof_plan(manifest: dict[str, Any], direct: list[str], claim: str) -> list[dict[str, Any]]:
    profile_map = {item["id"]: item for item in manifest.get("proof_profiles", [])}
    component_map = {item["id"]: item for item in manifest.get("components", [])}
    selected = set(manifest.get("claim_proofs", {}).get(claim, []))
    for component_id in direct:
        selected.update(component_map.get(component_id, {}).get("required_proofs", []))
    return [profile_map[item] for item in sorted(selected, key=lambda item: (profile_map.get(item, {}).get("rank", 999), item)) if item in profile_map]


def build_capsule(manifest: dict[str, Any], task: str, requested_claim: str) -> dict[str, Any]:
    branch = git("branch", "--show-current") or "detached/unknown"
    head = git("rev-parse", "HEAD") or "unknown"
    tree = git("rev-parse", "HEAD^{tree}") or "unknown"
    dirty = bool(git("status", "--porcelain=v1") or "")

    universe, anchor, anchor_relations = choose_anchor(manifest)
    files = changed_files(anchor)
    changed_components, component_files, unclassified = classify_paths(manifest, files)
    task_components = infer_task_components(task)
    direct = changed_components if changed_components else task_components
    ownership_basis = "diff" if changed_components else ("task_inference" if task_components else "unresolved")
    claim = infer_claim(task, direct, requested_claim)
    boundaries = boundaries_for(manifest, direct)
    proofs = proof_plan(manifest, direct, claim)
    component_map = {item["id"]: item for item in manifest.get("components", [])}

    read_now = ["AGENTS.md", "docs/system-model.md"]
    for component_id in direct:
        read_now.extend(component_map.get(component_id, {}).get("read_now", []))
    read_now = list(dict.fromkeys(read_now))

    warnings: list[str] = []
    if not universe:
        warnings.append("HEAD cannot currently be related to a declared history universe; refresh the branch graph before merge/rebase reasoning")
    elif universe == "legacy_experimental":
        warnings.append("legacy/experimental history universe: clean-tree promotion requires semantic/evidence transplant, not wholesale merge")
    if ownership_basis == "task_inference":
        warnings.append("ownership route comes from task text because no comparable changed-path delta established ownership")
    if len(changed_components) > 2:
        warnings.append(f"diff directly owns {len(changed_components)} components; split unless the boundary itself is the task")
    if unclassified:
        warnings.append(f"{len(unclassified)} changed paths are outside the current ownership model; investigate the model gap instead of guessing")
    if dirty:
        warnings.append("working tree is dirty")
    if any(path.startswith("Mithril-Wrapper-cpp/") or path.startswith("ci/") for path in files):
        warnings.append("diff contains legacy/replay roots; isolate reusable semantics/oracles from historical architecture")
    if any(path.startswith(".github/workflows/") and any(marker in path.lower() for marker in ("experiment", "candidate", "replay", "patch", "apply", "recover")) for path in files):
        warnings.append("diff contains task-local experiment/candidate workflow machinery; do not promote it as durable CI by inertia")

    return {
        "schema_version": 1,
        "task": task or None,
        "claim": claim,
        "git": {
            "branch": branch,
            "head": head,
            "tree": tree,
            "dirty": dirty,
            "history_universe": universe,
            "nearest_anchor": anchor,
            "anchor_relations": anchor_relations,
            "changed_file_count": len(files),
            "changed_files": files[:120]
        },
        "routing": {
            "ownership_basis": ownership_basis,
            "direct_components": direct,
            "changed_components": changed_components,
            "task_components": task_components,
            "component_files": component_files,
            "unclassified_files": unclassified,
            "boundaries": boundaries
        },
        "read_now": read_now,
        "proof_plan": proofs,
        "warnings": warnings
    }


def render_markdown(data: dict[str, Any]) -> str:
    git_data = data["git"]
    routing = data["routing"]
    lines = [
        "# Mithril task context",
        "",
        f"- branch: `{git_data['branch']}`",
        f"- HEAD: `{git_data['head']}`",
        f"- tree: `{git_data['tree']}`",
        f"- history universe: `{git_data['history_universe'] or 'unknown'}`",
        f"- nearest anchor: `{git_data['nearest_anchor'] or 'none'}`",
        f"- ownership basis: `{routing['ownership_basis']}`",
        f"- claim: `{data['claim']}`",
    ]
    if data.get("task"):
        lines.append(f"- task: {data['task']}")
    if data["warnings"]:
        lines.extend(["", "## Warnings"])
        lines.extend(f"- {warning}" for warning in data["warnings"])
    lines.extend(["", "## Read now"])
    lines.extend(f"- `{path}`" for path in data["read_now"])
    lines.extend(["", "## Owning components"])
    lines.extend(f"- `{component}`" for component in routing["direct_components"]) if routing["direct_components"] else lines.append("- unresolved")
    if routing["boundaries"]:
        lines.extend(["", "## Boundary risk"])
        lines.extend(f"- `{item['id']}` — {item['contract']}" for item in routing["boundaries"])
    lines.extend(["", "## Minimum proof order"])
    if data["proof_plan"]:
        for item in data["proof_plan"]:
            lines.append(f"{item['rank']}. `{item['id']}` [{item['environment']}] — {item['proves']}")
    else:
        lines.append("- unresolved; absence of a planned gate is not acceptance")
    if routing["unclassified_files"]:
        lines.extend(["", "## Ownership model gaps"])
        lines.extend(f"- `{path}`" for path in routing["unclassified_files"][:40])
    return "\n".join(lines) + "\n"


def self_test(manifest: dict[str, Any]) -> None:
    components, mapping, gaps = classify_paths(manifest, [
        "src/gl/fbo.cpp",
        "src/backend/types.h",
        "src/metal/engine.mm",
        "Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/Resources.cpp",
        "tests/amethyst_egl_smoke.mm"
    ])
    expected = {"gl.semantics", "semantic.lowering", "backend.directmetal", "legacy.migration", "platform.presentation"}
    assert expected.issubset(set(components)), (components, mapping)
    assert not gaps, gaps
    assert infer_claim("optimize GPU p95", ["backend.directmetal"], "auto") == "performance"
    assert infer_claim("fix framebuffer semantics", ["gl.semantics"], "auto") == "semantic"
    assert "legacy.migration" in infer_task_components("replay rollout patches and transplant the semantic fix")
    print("agent context self-test: PASS")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--task", default="")
    parser.add_argument("--claim", choices=["auto", "control", "semantic", "integration", "presentation", "performance"], default="auto")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    manifest = load_manifest()
    if args.self_test:
        self_test(manifest)
        return 0
    data = build_capsule(manifest, args.task, args.claim)
    if args.json:
        json.dump(data, sys.stdout, indent=2, sort_keys=False)
        print()
    else:
        sys.stdout.write(render_markdown(data))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
