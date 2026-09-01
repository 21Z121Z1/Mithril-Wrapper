#!/usr/bin/env python3
"""Compile live Mithril refs into topology facts plus stable lifecycle families."""

from __future__ import annotations

import argparse
import collections
import datetime as dt
import fnmatch
import json
import pathlib
import subprocess
import sys
from dataclasses import asdict, dataclass
from typing import Any, Optional

ROOT = pathlib.Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "docs/agent/manifest.json"
FAMILIES = ROOT / "docs/agent/branch-families.json"


def git(*args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(["git", *args], cwd=ROOT, check=check, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def out(*args: str) -> str:
    return git(*args).stdout.strip()


def exists(spec: str) -> bool:
    return git("cat-file", "-e", spec, check=False).returncode == 0


def load(path: pathlib.Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def live_heads(remote: str) -> dict[str, str]:
    proc = git("ls-remote", "--heads", remote, check=False)
    if proc.returncode:
        raise RuntimeError(proc.stderr.strip() or f"git ls-remote failed for {remote}")
    result: dict[str, str] = {}
    for line in proc.stdout.splitlines():
        if not line:
            continue
        sha, ref = line.split("\t", 1)
        prefix = "refs/heads/"
        if ref.startswith(prefix):
            result[ref[len(prefix):]] = sha
    return result


def local_remote_heads(remote: str) -> dict[str, str]:
    raw = out("for-each-ref", "--format=%(refname:short)\t%(objectname)", f"refs/remotes/{remote}")
    result: dict[str, str] = {}
    prefix = f"{remote}/"
    for line in raw.splitlines():
        if not line:
            continue
        name, sha = line.split("\t", 1)
        if name.startswith(prefix) and name != f"{remote}/HEAD":
            result[name[len(prefix):]] = sha
    return result


def fetch_graph(remote: str) -> None:
    proc = git("fetch", "--quiet", "--no-tags", "--prune", "--filter=blob:none", remote,
               f"+refs/heads/*:refs/remotes/{remote}/*", check=False)
    if proc.returncode:
        raise RuntimeError(proc.stderr.strip() or "blob-filtered branch graph fetch failed")


def merge_base(a: str, b: str) -> Optional[str]:
    if not exists(f"{a}^{{commit}}") or not exists(f"{b}^{{commit}}"):
        return None
    proc = git("merge-base", a, b, check=False)
    return proc.stdout.strip() if proc.returncode == 0 else None


def relation(a: str, b: str) -> tuple[str, Optional[int], Optional[int], Optional[str]]:
    if a == b:
        return "same", 0, 0, a
    mb = merge_base(a, b)
    if not mb:
        return "no_common_ancestor", None, None, None
    left, right = (int(part) for part in out("rev-list", "--left-right", "--count", f"{a}...{b}").split())
    kind = "ahead" if mb == a else "behind" if mb == b else "diverged"
    return kind, right, left, mb


def tree_sha(head: str) -> Optional[str]:
    return out("rev-parse", f"{head}^{{tree}}") if exists(f"{head}^{{commit}}") else None


def tree_flavor(head: str) -> str:
    if not exists(f"{head}^{{commit}}"):
        return "unknown"
    clean = exists(f"{head}:src")
    legacy = exists(f"{head}:Mithril-Wrapper-cpp")
    if clean and legacy:
        return "hybrid"
    return "clean" if clean else "legacy" if legacy else "other"


def changed_paths(base: str, head: str) -> list[str]:
    if not merge_base(base, head):
        return []
    proc = git("diff", "--name-only", f"{base}...{head}", check=False)
    return proc.stdout.splitlines() if proc.returncode == 0 else []


def delta_kind(paths: list[str]) -> str:
    if not paths:
        return "none_or_unavailable"
    product = ("src/", "include/", "Mithril-Wrapper-cpp/MG_", "Mithril-Wrapper-cpp/egl/", "Mithril-Wrapper-cpp/include/")
    if any(path.startswith(product) for path in paths):
        return "product_or_semantic_source"
    evidence = (".github/", "ci/", "tests/", "verify/", "docs/", "scripts/", "cmake/")
    if all(path.startswith(evidence) or path in {"CMakeLists.txt", ".gitignore", "README.md", "CHECKLIST.md"} for path in paths):
        return "evidence_control_or_build_only"
    return "unclassified_tree_delta"


def workflow_debt(paths: list[str]) -> list[str]:
    markers = ("experiment", "candidate", "replay", "apply", "patch", "recover", "materialize", "stage")
    return [path for path in paths if path.startswith(".github/workflows/") and any(marker in path.lower() for marker in markers)]


def family_matches(registry: dict[str, Any], branch: str) -> list[dict[str, Any]]:
    result = []
    for family in registry.get("families", []):
        if any(fnmatch.fnmatchcase(branch, selector) for selector in family.get("selectors", [])):
            result.append(family)
    return result


def anchors(manifest: dict[str, Any], live: dict[str, str]) -> list[tuple[str, str, str]]:
    result = []
    for universe in manifest.get("history_universes", []):
        for anchor in universe.get("anchors", []):
            if anchor in live:
                result.append((universe["id"], anchor, live[anchor]))
    return result


def containing_live_branches(head: str, remote: str, live: dict[str, str], local: dict[str, str]) -> list[str]:
    """One graph walk per head; stale remote-tracking refs never count as live coverage."""
    if not exists(f"{head}^{{commit}}"):
        return []
    proc = git("for-each-ref", f"--contains={head}", "--format=%(refname:short)", f"refs/remotes/{remote}", check=False)
    if proc.returncode:
        return []
    prefix = f"{remote}/"
    result = []
    for ref in proc.stdout.splitlines():
        if not ref.startswith(prefix) or ref == f"{remote}/HEAD":
            continue
        name = ref[len(prefix):]
        if name in live and local.get(name) == live[name]:
            result.append(name)
    return sorted(set(result))


@dataclass
class BranchFact:
    name: str
    head: str
    tree: Optional[str]
    tree_flavor: str
    family_ids: list[str]
    family_dispositions: list[str]
    family_unmatched: bool
    relation_to_base: str
    base_ahead: Optional[int]
    base_behind: Optional[int]
    history_universes: list[str]
    nearest_anchor: Optional[str]
    relation_to_anchor: str
    anchor_ahead: Optional[int]
    anchor_behind: Optional[int]
    delta_kind: str
    changed_file_count: int
    duplicate_head: bool
    same_tree_as: list[str]
    covered_by: list[str]
    one_shot_workflows: list[str]


def build_snapshot(remote: str, base_branch: str, refreshed: bool) -> dict[str, Any]:
    manifest, registry = load(MANIFEST), load(FAMILIES)
    live = live_heads(remote)
    if base_branch not in live:
        raise RuntimeError(f"base branch {base_branch!r} does not exist")
    base_sha = live[base_branch]
    anchor_list = anchors(manifest, live)
    if not anchor_list:
        raise RuntimeError("no declared history anchors are live")

    local = local_remote_heads(remote)
    heads_count = collections.Counter(live.values())
    trees = {name: tree_sha(sha) for name, sha in live.items()}
    tree_groups: dict[str, list[str]] = collections.defaultdict(list)
    for name, tree in trees.items():
        if tree:
            tree_groups[tree].append(name)

    facts: list[BranchFact] = []
    family_counts: collections.Counter[str] = collections.Counter()
    unmatched: list[str] = []
    for name, head in sorted(live.items()):
        matched = family_matches(registry, name)
        if not matched:
            unmatched.append(name)
        for family in matched:
            family_counts[family["id"]] += 1

        base_kind, base_ahead, base_behind, _ = relation(base_sha, head)
        candidates: list[tuple[int, int, str, str, tuple[str, Optional[int], Optional[int], Optional[str]]]] = []
        universes: set[str] = set()
        for universe_id, anchor_name, anchor_sha in anchor_list:
            rel = relation(anchor_sha, head)
            if rel[1] is not None:
                universes.add(universe_id)
                candidates.append((rel[1] + rel[2], rel[2], universe_id, anchor_name, rel))
        candidates.sort()

        nearest: Optional[str] = None
        anchor_kind = "no_declared_history_universe"
        anchor_ahead = anchor_behind = None
        paths: list[str] = []
        if candidates:
            _, _, _, nearest, rel = candidates[0]
            anchor_kind, anchor_ahead, anchor_behind, _ = rel
            paths = changed_paths(live[nearest], head)

        tree = trees.get(name)
        same_tree = sorted(other for other in tree_groups.get(tree or "", []) if other != name)
        containing = containing_live_branches(head, remote, live, local)
        covered_by = [other for other in containing if other != name and live.get(other) != head]

        facts.append(BranchFact(
            name=name, head=head, tree=tree, tree_flavor=tree_flavor(head),
            family_ids=[item["id"] for item in matched],
            family_dispositions=sorted({item["disposition"] for item in matched}),
            family_unmatched=not matched,
            relation_to_base=base_kind, base_ahead=base_ahead, base_behind=base_behind,
            history_universes=sorted(universes), nearest_anchor=nearest,
            relation_to_anchor=anchor_kind, anchor_ahead=anchor_ahead, anchor_behind=anchor_behind,
            delta_kind=delta_kind(paths), changed_file_count=len(paths),
            duplicate_head=heads_count[head] > 1, same_tree_as=same_tree,
            covered_by=covered_by, one_shot_workflows=workflow_debt(paths),
        ))

    declared = {role["name"] for role in manifest.get("branch_roles", [])}
    return {
        "schema_version": 4,
        "generated_at_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "remote": remote,
        "base_branch": base_branch,
        "base_sha": base_sha,
        "graph_refreshed": refreshed,
        "branch_count": len(facts),
        "declared_anchor_count": len(declared & set(live)),
        "non_anchor_branch_count": len(set(live) - declared),
        "family_counts": dict(sorted(family_counts.items())),
        "unmatched_branches": sorted(unmatched),
        "history_universes": manifest.get("history_universes", []),
        "duplicate_head_groups": [
            {"head": sha, "branches": sorted(name for name, value in live.items() if value == sha)}
            for sha, count in sorted(heads_count.items()) if count > 1
        ],
        "duplicate_tree_groups": [
            {"tree": tree, "branches": sorted(names)}
            for tree, names in sorted(tree_groups.items()) if len(names) > 1
        ],
        "branches": [asdict(item) for item in facts],
        "epistemic_note": "Git fields are live/generated; family fields are lifecycle metadata; neither is deletion authorization. Semantic work status lives in migration-queue.json."
    }


def print_markdown(snapshot: dict[str, Any]) -> None:
    print(f"# Branch topology ({snapshot['generated_at_utc']})\n")
    print(f"Base `{snapshot['base_branch']}` @ `{snapshot['base_sha']}`; branches={snapshot['branch_count']}; graph_refreshed={str(snapshot['graph_refreshed']).lower()}\n")
    print("| Branch | HEAD | Family | Tree | Universe | Anchor | Relation | A/B | Delta | Covered by |")
    print("| --- | --- | --- | --- | --- | --- | --- | ---: | --- | --- |")
    for item in snapshot["branches"]:
        counts = "?" if item["anchor_ahead"] is None else f"{item['anchor_ahead']}/{item['anchor_behind']}"
        covered = ", ".join(item["covered_by"][:2]) + (f" +{len(item['covered_by']) - 2}" if len(item["covered_by"]) > 2 else "")
        dup = " *" if item["duplicate_head"] else ""
        family = ",".join(item["family_ids"]) or "UNMATCHED"
        print(f"| `{item['name']}` | `{item['head'][:12]}`{dup} | {family} | {item['tree_flavor']} | {','.join(item['history_universes']) or 'unknown'} | `{item['nearest_anchor'] or '-'}` | {item['relation_to_anchor']} | {counts} | {item['delta_kind']} ({item['changed_file_count']}) | {covered or '-'} |")
    if snapshot["unmatched_branches"]:
        print("\nUnmatched lifecycle refs (control-model debt):")
        for name in snapshot["unmatched_branches"]:
            print(f"- `{name}`")
    if snapshot["duplicate_head_groups"]:
        print("\nExact duplicate HEADs:")
        for group in snapshot["duplicate_head_groups"]:
            print(f"- `{group['head']}`: " + ", ".join(f"`{name}`" for name in group["branches"]))


def self_test() -> None:
    assert delta_kind([]) == "none_or_unavailable"
    assert delta_kind(["src/gl/state.cpp"]) == "product_or_semantic_source"
    assert delta_kind(["Mithril-Wrapper-cpp/MG_Impl/Drawing.cpp"]) == "product_or_semantic_source"
    assert delta_kind([".github/workflows/experiment-a.yml", "tests/a.c"]) == "evidence_control_or_build_only"
    assert workflow_debt([".github/workflows/ios-candidate-v4.yml"])
    registry = load(FAMILIES)
    assert [item["id"] for item in family_matches(registry, "main")] == ["canonical.clean"]
    assert "legacy.directvulkan-experiments" in [item["id"] for item in family_matches(registry, "experiment/dvk-pbo-full-unpack-20260827")]
    assert "provenance.directmetal-performance" in [item["id"] for item in family_matches(registry, "perf/directmetal-async-pso-precompile-20260818")]
    print("branch audit self-test: PASS")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--remote", default="origin")
    parser.add_argument("--base", default="main")
    parser.add_argument("--fetch-graph", action="store_true")
    parser.add_argument("--markdown", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    try:
        if args.self_test:
            self_test()
            return 0
        if args.fetch_graph:
            fetch_graph(args.remote)
        snapshot = build_snapshot(args.remote, args.base, args.fetch_graph)
    except (RuntimeError, subprocess.SubprocessError, ValueError, json.JSONDecodeError, OSError) as exc:
        print(f"branch audit failed: {exc}", file=sys.stderr)
        return 1
    if args.markdown:
        print_markdown(snapshot)
    else:
        json.dump(snapshot, sys.stdout, indent=2)
        print()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
