#!/usr/bin/env python3
"""Compile the smallest useful world model for one Mithril task."""

from __future__ import annotations

import argparse
import fnmatch
import json
import pathlib
import re
import subprocess
import sys
from typing import Any, Iterable, Optional

ROOT = pathlib.Path(__file__).resolve().parents[1]
MANIFEST_PATH = ROOT / "docs/agent/manifest.json"
PROOF_GRAPH_PATH = ROOT / "docs/agent/proof-graph.json"
ORACLES_PATH = ROOT / "docs/agent/oracles.json"


def run_git(*args: str) -> Optional[str]:
    proc = subprocess.run(
        ["git", *args], cwd=ROOT, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
    )
    return proc.stdout.strip() if proc.returncode == 0 else None


def git_ok(*args: str) -> bool:
    return subprocess.run(
        ["git", *args], cwd=ROOT,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    ).returncode == 0


def load_json(path: pathlib.Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def resolve_ref(name: str) -> Optional[str]:
    for candidate in (f"refs/remotes/origin/{name}", f"refs/heads/{name}", name):
        if run_git("rev-parse", "--verify", candidate):
            return candidate
    return None


def relation(anchor_ref: str, head_ref: str = "HEAD") -> dict[str, Any]:
    mb = run_git("merge-base", anchor_ref, head_ref)
    if not mb:
        return {"relation": "disconnected", "merge_base": None, "ahead": None, "behind": None}
    counts = run_git("rev-list", "--left-right", "--count", f"{anchor_ref}...{head_ref}")
    if not counts:
        return {"relation": "unresolved", "merge_base": mb, "ahead": None, "behind": None}
    behind_s, ahead_s = counts.split()
    behind, ahead = int(behind_s), int(ahead_s)
    if ahead == 0 and behind == 0:
        kind = "same"
    elif git_ok("merge-base", "--is-ancestor", anchor_ref, head_ref):
        kind = "descendant"
    elif git_ok("merge-base", "--is-ancestor", head_ref, anchor_ref):
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
            if item["ahead"] is not None:
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
    if not ref or not run_git("merge-base", ref, "HEAD"):
        return []
    raw = run_git("diff", "--name-only", f"{ref}...HEAD") or ""
    return [line for line in raw.splitlines() if line]


def path_matches(path: str, pattern: str) -> bool:
    if fnmatch.fnmatchcase(path, pattern):
        return True
    if pattern.endswith("/**"):
        return path.startswith(pattern[:-3].rstrip("/") + "/")
    return path == pattern


def specificity(pattern: str) -> int:
    return len(pattern.replace("*", "").replace("?", ""))


def classify_paths(manifest: dict[str, Any], files: Iterable[str]) -> tuple[list[str], dict[str, list[str]], list[str]]:
    mapping: dict[str, list[str]] = {}
    gaps: list[str] = []
    for path in files:
        matches: list[tuple[int, str]] = []
        for component in manifest.get("components", []):
            for pattern in component.get("owned_paths", []):
                if path_matches(path, pattern):
                    matches.append((specificity(pattern), component["id"]))
        if not matches:
            gaps.append(path)
            continue
        best = max(score for score, _ in matches)
        for component_id in sorted({cid for score, cid in matches if score == best}):
            mapping.setdefault(component_id, []).append(path)
    return sorted(mapping), mapping, sorted(gaps)


def tokens(text: str) -> set[str]:
    return {item for item in re.findall(r"[a-z0-9_.+-]+", text.lower()) if len(item) > 1}


TASK_HINTS = {
    "host.egl": {"egl", "surface", "context", "amethyst", "glfw", "lwjgl"},
    "gl.semantics": {"gl", "opengl", "state", "fbo", "framebuffer", "pixel", "unpack", "pack", "query", "sync", "texture", "buffer", "draw"},
    "shader.contract": {"shader", "spirv", "glsl", "reflection", "ubo", "uniform", "sampler", "varying", "location"},
    "semantic.lowering": {"lowering", "drawparams", "backend-neutral", "semantic", "snapshot", "intent", "lifetime", "generation"},
    "backend.directmetal": {"metal", "directmetal", "mtl", "pso"},
    "backend.directvulkan": {"vulkan", "directvulkan", "moltenvk", "descriptor", "vk"},
    "platform.presentation": {"presentation", "present", "drawable", "cametallayer", "orientation", "yflip", "bgra", "iphoneos"},
    "validation.contract": {"test", "oracle", "smoke", "readback", "validation"},
    "evaluation.control": {"ci", "workflow", "agent", "branch", "evidence", "github", "audit"},
    "legacy.migration": {"legacy", "replay", "patch", "rollout", "transplant", "mithril-wrapper-cpp"},
}


def infer_task_components(task: str) -> list[str]:
    observed = tokens(task)
    scored: list[tuple[int, str]] = []
    for component_id, words in TASK_HINTS.items():
        score = sum(5 if token in words else 2 if any(token in word or word in token for word in words) else 0 for token in observed)
        if score:
            scored.append((score, component_id))
    scored.sort(key=lambda item: (-item[0], item[1]))
    return [component_id for _, component_id in scored[:3]]


def infer_claim(task: str, direct: list[str], requested: str) -> str:
    if requested != "auto":
        return requested
    observed = tokens(task)
    if direct and set(direct) <= {"evaluation.control"}:
        return "control"
    if observed & {"performance", "perf", "fps", "latency", "throughput", "p95", "optimize", "optimization"}:
        return "performance"
    if observed & {"presentation", "present", "drawable", "cametallayer", "orientation", "device", "iphone", "ipad"}:
        return "presentation"
    if observed & {"minecraft", "e2e", "integration", "runtime", "startup", "gui"}:
        return "integration"
    return "semantic"


def boundaries_for(manifest: dict[str, Any], direct: list[str]) -> list[dict[str, Any]]:
    active = set(direct)
    result = []
    for boundary in manifest.get("boundaries", []):
        if active & set(boundary.get("from", [])):
            result.append(boundary)
        elif boundary.get("id") == "legacy_transplant" and active & set(boundary.get("to", [])):
            result.append(boundary)
    return result


def proof_closure(manifest: dict[str, Any], graph: dict[str, Any], direct: list[str], claim: str) -> list[dict[str, Any]]:
    profiles = {item["id"]: item for item in manifest.get("proof_profiles", [])}
    selected = set(manifest.get("claim_proofs", {}).get(claim, []))
    component_map = {item["id"]: item for item in manifest.get("components", [])}
    obligations = graph.get("component_obligations", {})
    for component_id in direct:
        selected.update(component_map.get(component_id, {}).get("required_proofs", []))
        selected.update(obligations.get(component_id, []))

    requires = graph.get("requires", {})
    visiting: set[str] = set()
    done: set[str] = set()

    def visit(proof_id: str) -> None:
        if proof_id in done:
            return
        if proof_id in visiting:
            raise RuntimeError(f"proof dependency cycle at {proof_id}")
        visiting.add(proof_id)
        for dep in requires.get(proof_id, []):
            visit(dep)
        visiting.remove(proof_id)
        done.add(proof_id)

    for proof_id in list(selected):
        visit(proof_id)
    unknown = done - set(profiles)
    if unknown:
        raise RuntimeError(f"proof graph references unknown profiles: {sorted(unknown)}")
    return [profiles[item] for item in sorted(done, key=lambda item: (profiles[item]["rank"], item))]


def route_oracles(index: dict[str, Any], task: str, direct: list[str], limit: int = 4) -> list[dict[str, Any]]:
    observed = tokens(task)
    active = set(direct)
    scored: list[tuple[int, str, dict[str, Any]]] = []
    for oracle in index.get("oracles", []):
        component_overlap = len(active & set(oracle.get("components", [])))
        keyword_overlap = len(observed & set(oracle.get("keywords", [])))
        score = component_overlap * 20 + keyword_overlap * 5
        if score:
            scored.append((score, oracle["id"], oracle))
    scored.sort(key=lambda item: (-item[0], item[1]))
    return [oracle for _, _, oracle in scored[:limit]]


def build_capsule(task: str, requested_claim: str) -> dict[str, Any]:
    manifest = load_json(MANIFEST_PATH)
    graph = load_json(PROOF_GRAPH_PATH)
    oracle_index = load_json(ORACLES_PATH)

    branch = run_git("branch", "--show-current") or "detached/unknown"
    head = run_git("rev-parse", "HEAD") or "unknown"
    tree = run_git("rev-parse", "HEAD^{tree}") or "unknown"
    dirty = bool(run_git("status", "--porcelain=v1") or "")

    universe, anchor, anchor_relations = choose_anchor(manifest)
    files = changed_files(anchor)
    changed_components, component_files, gaps = classify_paths(manifest, files)
    task_components = infer_task_components(task)
    direct = changed_components if changed_components else task_components
    basis = "diff" if changed_components else "task_inference" if task_components else "unresolved"
    claim = infer_claim(task, direct, requested_claim)
    boundaries = boundaries_for(manifest, direct)
    proofs = proof_closure(manifest, graph, direct, claim)
    focused_oracles = route_oracles(oracle_index, task, direct)

    component_map = {item["id"]: item for item in manifest.get("components", [])}
    read_now = ["AGENTS.md", "docs/system-model.md"]
    for component_id in direct:
        read_now.extend(component_map.get(component_id, {}).get("read_now", []))
    read_now.extend(oracle["path"] for oracle in focused_oracles[:2])
    read_now = list(dict.fromkeys(read_now))

    warnings: list[str] = []
    if not universe:
        warnings.append("HEAD is outside declared history universes; refresh live graph before merge/rebase reasoning")
    elif universe == "legacy_experimental":
        warnings.append("legacy history: clean promotion is semantic/oracle transplant, never wholesale merge")
    if basis == "task_inference":
        warnings.append("ownership is inferred from task text because no comparable diff established it")
    if len(changed_components) > 3:
        warnings.append(f"diff directly owns {len(changed_components)} components; split unless the boundary itself is the task")
    if gaps:
        warnings.append(f"{len(gaps)} changed paths are outside the ownership model; treat as model gaps")
    if dirty:
        warnings.append("working tree is dirty")
    if any(path.startswith(("Mithril-Wrapper-cpp/", "ci/")) for path in files):
        warnings.append("legacy/replay roots changed; isolate reusable semantics/oracles from historical architecture")
    if any(path.startswith(".github/workflows/") and any(marker in path.lower() for marker in ("experiment", "candidate", "replay", "patch", "apply", "recover")) for path in files):
        warnings.append("task-local workflow machinery is present; do not promote it as durable CI by inertia")

    return {
        "schema_version": 2,
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
            "changed_files": files[:120],
        },
        "routing": {
            "ownership_basis": basis,
            "direct_components": direct,
            "changed_components": changed_components,
            "task_components": task_components,
            "component_files": component_files,
            "unclassified_files": gaps,
            "boundaries": boundaries,
        },
        "focused_oracles": focused_oracles,
        "read_now": read_now,
        "proof_plan": proofs,
        "warnings": warnings,
    }


def render_markdown(data: dict[str, Any]) -> str:
    git_data, routing = data["git"], data["routing"]
    lines = [
        "# Mithril task context", "",
        f"- branch: `{git_data['branch']}`",
        f"- HEAD/tree: `{git_data['head']}` / `{git_data['tree']}`",
        f"- history universe / nearest anchor: `{git_data['history_universe'] or 'unknown'}` / `{git_data['nearest_anchor'] or 'none'}`",
        f"- ownership basis: `{routing['ownership_basis']}`",
        f"- claim: `{data['claim']}`",
    ]
    if data.get("task"):
        lines.append(f"- task: {data['task']}")
    if data["warnings"]:
        lines += ["", "## Warnings"] + [f"- {item}" for item in data["warnings"]]
    lines += ["", "## Owning components"]
    lines += [f"- `{item}`" for item in routing["direct_components"]] or ["- unresolved"]
    if routing["boundaries"]:
        lines += ["", "## Boundary risk"] + [f"- `{item['id']}` — {item['contract']}" for item in routing["boundaries"]]
    lines += ["", "## Focused oracles"]
    lines += [f"- `{item['id']}` -> `{item['path']}` ({', '.join(item['backends'])})" for item in data["focused_oracles"]] or ["- no indexed oracle; search the owning test slice before implementation"]
    lines += ["", "## Read now"] + [f"- `{path}`" for path in data["read_now"]]
    lines += ["", "## Minimum proof order"]
    lines += [f"{item['rank']}. `{item['id']}` [{item['environment']}] — {item['proves']}" for item in data["proof_plan"]] or ["- unresolved; absence of a planned gate is not acceptance"]
    if routing["unclassified_files"]:
        lines += ["", "## Ownership model gaps"] + [f"- `{path}`" for path in routing["unclassified_files"][:40]]
    return "\n".join(lines) + "\n"


def self_test() -> None:
    manifest = load_json(MANIFEST_PATH)
    graph = load_json(PROOF_GRAPH_PATH)
    index = load_json(ORACLES_PATH)
    components, _, gaps = classify_paths(manifest, ["src/gl/fbo.cpp", "src/backend/types.h", "src/metal/engine.mm", "Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/Resources.cpp"])
    assert {"gl.semantics", "semantic.lowering", "backend.directmetal", "legacy.migration"}.issubset(components)
    assert not gaps
    proofs = [item["id"] for item in proof_closure(manifest, graph, ["gl.semantics"], "semantic")]
    assert proofs.index("focused_semantic") < proofs.index("directmetal_ctest")
    assert "directvulkan_ctest" in proofs
    assert route_oracles(index, "fix incomplete framebuffer fbo semantics", ["gl.semantics"])[0]["id"] == "framebuffer_semantics"
    print("agent context self-test: PASS")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--task", default="")
    parser.add_argument("--claim", choices=["auto", "control", "semantic", "integration", "presentation", "performance"], default="auto")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    try:
        if args.self_test:
            self_test()
            return 0
        data = build_capsule(args.task, args.claim)
    except (OSError, json.JSONDecodeError, RuntimeError, AssertionError) as exc:
        print(f"agent context failed: {exc}", file=sys.stderr)
        return 1
    if args.json:
        json.dump(data, sys.stdout, indent=2)
        print()
    else:
        sys.stdout.write(render_markdown(data))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
