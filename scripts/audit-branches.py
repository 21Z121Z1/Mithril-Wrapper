#!/usr/bin/env python3
"""Emit a live, read-only multi-universe branch topology for repository agents.

Default mode queries live remote HEADs and uses any commit graph already present.
--fetch-graph performs one blob-filtered fetch of all remote heads so ancestry,
nearest-anchor, tree and coverage facts are current without checking out branch
working trees.
"""

from __future__ import annotations

import argparse
import collections
import datetime as dt
import json
import pathlib
import subprocess
import sys
from dataclasses import dataclass, asdict
from typing import Any, Optional

ROOT = pathlib.Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "docs/agent/manifest.json"


def git(*args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", *args], cwd=ROOT, check=check, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )


def git_output(*args: str) -> str:
    return git(*args).stdout.strip()


def object_exists(spec: str) -> bool:
    return git("cat-file", "-e", spec, check=False).returncode == 0


def load_manifest() -> dict[str, Any]:
    return json.loads(MANIFEST.read_text(encoding="utf-8"))


def live_heads(remote: str) -> dict[str, str]:
    result = git("ls-remote", "--heads", remote, check=False)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or f"git ls-remote failed for {remote}")
    heads: dict[str, str] = {}
    for line in result.stdout.splitlines():
        if not line:
            continue
        sha, ref = line.split("\t", 1)
        prefix = "refs/heads/"
        if ref.startswith(prefix):
            heads[ref[len(prefix):]] = sha
    return heads


def fetch_graph(remote: str) -> None:
    refspec = f"+refs/heads/*:refs/remotes/{remote}/*"
    result = git(
        "fetch", "--quiet", "--no-tags", "--prune", "--filter=blob:none",
        remote, refspec, check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "blob-filtered branch graph fetch failed")


def merge_base(a: str, b: str) -> Optional[str]:
    if not object_exists(f"{a}^{{commit}}") or not object_exists(f"{b}^{{commit}}"):
        return None
    result = git("merge-base", a, b, check=False)
    return result.stdout.strip() if result.returncode == 0 else None


def is_ancestor(a: str, b: str) -> bool:
    return git("merge-base", "--is-ancestor", a, b, check=False).returncode == 0


def relation(a: str, b: str) -> tuple[str, Optional[int], Optional[int], Optional[str]]:
    if a == b:
        return "same", 0, 0, a
    if not object_exists(f"{a}^{{commit}}") or not object_exists(f"{b}^{{commit}}"):
        return "graph_unavailable", None, None, None
    mb = merge_base(a, b)
    if not mb:
        return "no_common_ancestor", None, None, None
    counts = git_output("rev-list", "--left-right", "--count", f"{a}...{b}")
    left, right = (int(part) for part in counts.split())
    if mb == a:
        kind = "ahead"
    elif mb == b:
        kind = "behind"
    else:
        kind = "diverged"
    return kind, right, left, mb


def tree_sha(head: str) -> Optional[str]:
    if not object_exists(f"{head}^{{commit}}"):
        return None
    return git_output("rev-parse", f"{head}^{{tree}}")


def tree_flavor(head: str) -> str:
    if not object_exists(f"{head}^{{commit}}"):
        return "unknown"
    has_clean = object_exists(f"{head}:src")
    has_legacy = object_exists(f"{head}:Mithril-Wrapper-cpp")
    if has_clean and has_legacy:
        return "hybrid"
    if has_clean:
        return "clean"
    if has_legacy:
        return "legacy"
    return "other"


def changed_paths(base: str, head: str) -> list[str]:
    mb = merge_base(base, head)
    if not mb:
        return []
    result = git("diff", "--name-only", f"{base}...{head}", check=False)
    if result.returncode != 0:
        return []
    return [line for line in result.stdout.splitlines() if line]


def delta_kind(paths: list[str]) -> str:
    if not paths:
        return "none_or_unavailable"
    product_prefixes = (
        "src/", "include/", "Mithril-Wrapper-cpp/MG_",
        "Mithril-Wrapper-cpp/egl/", "Mithril-Wrapper-cpp/include/",
    )
    if any(path.startswith(product_prefixes) for path in paths):
        return "product_or_semantic_source"
    evidence_prefixes = (
        ".github/", "ci/", "tests/", "verify/", "docs/", "scripts/", "cmake/",
    )
    if all(path.startswith(evidence_prefixes) or path in {"CMakeLists.txt", ".gitignore", "README.md"}
           for path in paths):
        return "evidence_control_or_build_only"
    return "unclassified_tree_delta"


def workflow_debt(paths: list[str]) -> list[str]:
    markers = ("experiment", "candidate", "replay", "apply", "patch", "recover", "materialize", "stage")
    return [
        path for path in paths
        if path.startswith(".github/workflows/") and any(marker in path.lower() for marker in markers)
    ]


def history_anchors(manifest: dict[str, Any], live: dict[str, str]) -> list[tuple[str, str, str]]:
    result: list[tuple[str, str, str]] = []
    for universe in manifest.get("history_universes", []):
        for anchor in universe.get("anchors", []):
            sha = live.get(anchor)
            if sha:
                result.append((universe["id"], anchor, sha))
    return result


@dataclass
class BranchFact:
    name: str
    head: str
    tree: Optional[str]
    tree_flavor: str
    relation_to_base: str
    base_ahead: Optional[int]
    base_behind: Optional[int]
    base_merge_base: Optional[str]
    history_universes: list[str]
    nearest_anchor: Optional[str]
    relation_to_anchor: str
    anchor_ahead: Optional[int]
    anchor_behind: Optional[int]
    anchor_merge_base: Optional[str]
    delta_kind: str
    changed_file_count: int
    duplicate_head: bool
    same_tree_as: list[str]
    covered_by: list[str]
    one_shot_workflows: list[str]


def build_snapshot(remote: str, base_branch: str, refreshed: bool) -> dict[str, Any]:
    manifest = load_manifest()
    live = live_heads(remote)
    if base_branch not in live:
        raise RuntimeError(f"base branch {base_branch!r} does not exist on remote {remote!r}")
    base_sha = live[base_branch]
    anchors = history_anchors(manifest, live)
    if not anchors:
        raise RuntimeError("no declared history-universe anchors are live")

    duplicate_heads = collections.Counter(live.values())
    trees = {name: tree_sha(sha) for name, sha in live.items()}
    tree_groups: dict[str, list[str]] = collections.defaultdict(list)
    for name, tree in trees.items():
        if tree:
            tree_groups[tree].append(name)

    facts: list[BranchFact] = []
    for name, head in sorted(live.items()):
        base_kind, base_ahead, base_behind, base_mb = relation(base_sha, head)

        universe_ids: list[str] = []
        anchor_candidates: list[tuple[int, int, str, str, tuple[str, Optional[int], Optional[int], Optional[str]]]] = []
        for universe_id, anchor_name, anchor_sha in anchors:
            rel = relation(anchor_sha, head)
            if rel[0] in {"same", "ahead", "behind", "diverged"}:
                universe_ids.append(universe_id)
                assert rel[1] is not None and rel[2] is not None
                anchor_candidates.append((rel[1] + rel[2], rel[2], universe_id, anchor_name, rel))

        anchor_candidates.sort()
        nearest_anchor: Optional[str] = None
        anchor_kind = "no_declared_history_universe"
        anchor_ahead = anchor_behind = None
        anchor_mb = None
        paths: list[str] = []
        if anchor_candidates:
            _, _, _, nearest_anchor, rel = anchor_candidates[0]
            anchor_kind, anchor_ahead, anchor_behind, anchor_mb = rel
            paths = changed_paths(live[nearest_anchor], head)

        same_tree = []
        tree = trees.get(name)
        if tree:
            same_tree = sorted(other for other in tree_groups[tree] if other != name)

        covered_by = []
        if object_exists(f"{head}^{{commit}}"):
            for other, other_head in live.items():
                if other == name or not object_exists(f"{other_head}^{{commit}}"):
                    continue
                if is_ancestor(head, other_head):
                    covered_by.append(other)
        covered_by.sort()

        facts.append(BranchFact(
            name=name,
            head=head,
            tree=tree,
            tree_flavor=tree_flavor(head),
            relation_to_base=base_kind,
            base_ahead=base_ahead,
            base_behind=base_behind,
            base_merge_base=base_mb,
            history_universes=sorted(set(universe_ids)),
            nearest_anchor=nearest_anchor,
            relation_to_anchor=anchor_kind,
            anchor_ahead=anchor_ahead,
            anchor_behind=anchor_behind,
            anchor_merge_base=anchor_mb,
            delta_kind=delta_kind(paths),
            changed_file_count=len(paths),
            duplicate_head=duplicate_heads[head] > 1,
            same_tree_as=same_tree,
            covered_by=covered_by,
            one_shot_workflows=workflow_debt(paths),
        ))

    duplicate_head_groups = [
        {"head": sha, "branches": sorted(name for name, value in live.items() if value == sha)}
        for sha, count in sorted(duplicate_heads.items()) if count > 1
    ]
    duplicate_tree_groups = [
        {"tree": tree, "branches": sorted(names)}
        for tree, names in sorted(tree_groups.items()) if len(names) > 1
    ]

    declared_names = {role["name"] for role in manifest.get("branch_roles", [])}
    task_branches = [name for name in live if name not in declared_names]

    return {
        "schema_version": 2,
        "generated_at_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "remote": remote,
        "base_branch": base_branch,
        "base_sha": base_sha,
        "graph_refreshed": refreshed,
        "branch_count": len(facts),
        "declared_anchor_count": len(declared_names & set(live)),
        "non_anchor_branch_count": len(task_branches),
        "history_universes": manifest.get("history_universes", []),
        "duplicate_head_groups": duplicate_head_groups,
        "duplicate_tree_groups": duplicate_tree_groups,
        "branches": [asdict(fact) for fact in facts],
        "epistemic_note": "Generated Git facts; covered/same-tree/evidence-only classifications are not deletion authorization."
    }


def print_markdown(snapshot: dict[str, Any]) -> None:
    print(f"# Branch topology ({snapshot['generated_at_utc']})")
    print()
    print(f"Base: `{snapshot['base_branch']}` @ `{snapshot['base_sha']}`")
    print(
        f"Branches: {snapshot['branch_count']}; declared anchors={snapshot['declared_anchor_count']}; "
        f"non-anchor refs={snapshot['non_anchor_branch_count']}; graph_refreshed={str(snapshot['graph_refreshed']).lower()}"
    )
    print()
    print("| Branch | HEAD | Tree | Universe | Nearest anchor | Relation | Ahead/Behind | Delta | Covered by |")
    print("| --- | --- | --- | --- | --- | --- | ---: | --- | --- |")
    for item in snapshot["branches"]:
        ahead_behind = "?" if item["anchor_ahead"] is None else f"{item['anchor_ahead']}/{item['anchor_behind']}"
        covered = ", ".join(item["covered_by"][:2])
        if len(item["covered_by"]) > 2:
            covered += f" +{len(item['covered_by']) - 2}"
        duplicate = " *" if item["duplicate_head"] else ""
        print(
            f"| `{item['name']}` | `{item['head'][:12]}`{duplicate} | {item['tree_flavor']} | "
            f"{','.join(item['history_universes']) or 'unknown'} | `{item['nearest_anchor'] or '-'}` | "
            f"{item['relation_to_anchor']} | {ahead_behind} | "
            f"{item['delta_kind']} ({item['changed_file_count']}) | {covered or '-'} |"
        )
    if snapshot["duplicate_head_groups"]:
        print("\nExact duplicate HEAD groups:")
        for group in snapshot["duplicate_head_groups"]:
            print(f"- `{group['head']}`: " + ", ".join(f"`{name}`" for name in group["branches"]))
    extra_tree = [g for g in snapshot["duplicate_tree_groups"] if len(g["branches"]) > 1]
    if extra_tree:
        print("\nSame-tree groups (may have different commit history):")
        for group in extra_tree:
            print(f"- `{group['tree']}`: " + ", ".join(f"`{name}`" for name in group["branches"]))


def self_test() -> None:
    assert delta_kind([]) == "none_or_unavailable"
    assert delta_kind(["src/gl/state.cpp"]) == "product_or_semantic_source"
    assert delta_kind(["Mithril-Wrapper-cpp/MG_Impl/Drawing.cpp"]) == "product_or_semantic_source"
    assert delta_kind([".github/workflows/experiment-a.yml", "tests/a.c"]) == "evidence_control_or_build_only"
    assert workflow_debt([".github/workflows/ios-candidate-v4.yml"])
    assert not workflow_debt([".github/workflows/build.yml"])
    print("branch audit self-test: PASS")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--remote", default="origin")
    parser.add_argument("--base", default="main", help="compatibility/reporting base; universe routing uses all manifest anchors")
    parser.add_argument("--fetch-graph", action="store_true", help="fetch all remote branch commit graphs with --filter=blob:none")
    parser.add_argument("--markdown", action="store_true", help="emit compact Markdown instead of JSON")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        self_test()
        return 0
    try:
        if args.fetch_graph:
            fetch_graph(args.remote)
        snapshot = build_snapshot(args.remote, args.base, args.fetch_graph)
    except (RuntimeError, subprocess.SubprocessError, ValueError, json.JSONDecodeError) as exc:
        print(f"branch audit failed: {exc}", file=sys.stderr)
        return 1

    if args.markdown:
        print_markdown(snapshot)
    else:
        json.dump(snapshot, sys.stdout, indent=2, sort_keys=False)
        print()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
