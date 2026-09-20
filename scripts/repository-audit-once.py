#!/usr/bin/env python3
"""Generate one-time live branch and fork topology evidence for repository consolidation."""
from __future__ import annotations

import collections
import datetime as dt
import json
import pathlib
import subprocess
from typing import Any

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / "repository-audit"
ANCHORS = (
    "origin/main",
    "origin/integration/directmetal-next",
    "origin/integration/directvulkan-reference",
    "origin/integration/legacy-capability-port",
)
FORK_REMOTES = ("eternity", "uniaball", "dev", "herbrine")


def run(*args: str, check: bool = True) -> str:
    proc = subprocess.run(
        args,
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if check and proc.returncode:
        raise RuntimeError(f"{' '.join(args)} failed: {proc.stderr.strip()}")
    return proc.stdout.strip()


def exists(spec: str) -> bool:
    return subprocess.run(
        ("git", "cat-file", "-e", spec),
        cwd=ROOT,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    ).returncode == 0


def refs(prefix: str) -> dict[str, str]:
    raw = run("git", "for-each-ref", "--format=%(refname:short)\t%(objectname)", prefix)
    result: dict[str, str] = {}
    for line in raw.splitlines():
        if not line:
            continue
        name, sha = line.split("\t", 1)
        if name.endswith("/HEAD"):
            continue
        result[name] = sha
    return result


def merge_relation(anchor: str, head: str) -> dict[str, Any]:
    mb = run("git", "merge-base", anchor, head, check=False)
    if not mb:
        return {"kind": "disconnected", "merge_base": None, "ahead": None, "behind": None}
    counts = run("git", "rev-list", "--left-right", "--count", f"{anchor}...{head}")
    behind_s, ahead_s = counts.split()
    behind, ahead = int(behind_s), int(ahead_s)
    if ahead == 0 and behind == 0:
        kind = "same"
    elif run("git", "merge-base", "--is-ancestor", anchor, head, check=False) == "":
        # `git merge-base --is-ancestor` has no stdout. Check status separately below.
        proc = subprocess.run(("git", "merge-base", "--is-ancestor", anchor, head), cwd=ROOT)
        if proc.returncode == 0:
            kind = "descendant"
        else:
            proc = subprocess.run(("git", "merge-base", "--is-ancestor", head, anchor), cwd=ROOT)
            kind = "ancestor" if proc.returncode == 0 else "diverged"
    else:
        kind = "diverged"
    return {"kind": kind, "merge_base": mb, "ahead": ahead, "behind": behind}


def nearest_anchor(head: str) -> tuple[str | None, dict[str, Any], dict[str, Any]]:
    all_rel: dict[str, Any] = {}
    candidates: list[tuple[int, int, str]] = []
    for anchor in ANCHORS:
        if not exists(f"{anchor}^{{commit}}"):
            continue
        rel = merge_relation(anchor, head)
        all_rel[anchor] = rel
        if rel["ahead"] is not None:
            candidates.append((rel["ahead"] + rel["behind"], rel["behind"], anchor))
    if not candidates:
        return None, {"kind": "disconnected", "merge_base": None, "ahead": None, "behind": None}, all_rel
    candidates.sort()
    anchor = candidates[0][2]
    return anchor, all_rel[anchor], all_rel


def paths(anchor: str | None, head: str) -> list[str]:
    if not anchor:
        return []
    return [p for p in run("git", "diff", "--name-only", f"{anchor}...{head}").splitlines() if p]


def list_at(head: str, prefix: str) -> list[str]:
    raw = run("git", "ls-tree", "-r", "--name-only", head, "--", prefix, check=False)
    return [line for line in raw.splitlines() if line]


def commits(anchor: str | None, head: str, limit: int = 12) -> list[str]:
    spec = f"{anchor}..{head}" if anchor else head
    raw = run("git", "log", "--no-merges", f"--max-count={limit}", "--format=%h %s", spec, check=False)
    return [line for line in raw.splitlines() if line]


def branch_fact(name: str, head: str, all_heads: dict[str, str]) -> dict[str, Any]:
    tree = run("git", "rev-parse", f"{head}^{{tree}}")
    anchor, rel, relations = nearest_anchor(head)
    changed = paths(anchor, head)
    product_prefixes = ("src/", "include/", "Mithril-Wrapper-cpp/")
    test_prefixes = ("tests/", "cmake/")
    workflow_prefix = ".github/workflows/"
    product = [p for p in changed if p.startswith(product_prefixes) or p == "CMakeLists.txt"]
    tests = [p for p in changed if p.startswith(test_prefixes)]
    workflows = [p for p in list_at(head, workflow_prefix) if p.endswith((".yml", ".yaml"))]
    subject = run("git", "show", "-s", "--format=%s", head)
    committed = run("git", "show", "-s", "--format=%cI", head)
    covered_by = []
    for other, other_head in all_heads.items():
        if other == name or other_head == head:
            continue
        proc = subprocess.run(("git", "merge-base", "--is-ancestor", head, other_head), cwd=ROOT,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if proc.returncode == 0:
            covered_by.append(other)
    roots = set(p.split("/", 1)[0] for p in list_at(head, ""))
    flavor = "hybrid" if {"src", "Mithril-Wrapper-cpp"} <= roots else "clean" if "src" in roots else "legacy" if "Mithril-Wrapper-cpp" in roots else "other"
    return {
        "name": name.removeprefix("origin/"),
        "head": head,
        "tree": tree,
        "subject": subject,
        "committed_at": committed,
        "flavor": flavor,
        "nearest_anchor": anchor.removeprefix("origin/") if anchor else None,
        "relation": rel,
        "anchor_relations": {k.removeprefix("origin/"): v for k, v in relations.items()},
        "changed_paths": changed,
        "product_paths": product,
        "test_paths": tests,
        "workflow_paths_at_head": workflows,
        "covered_by": sorted(x.removeprefix("origin/") for x in covered_by),
        "unique_commit_subjects": commits(anchor, head),
    }


def remote_default(remote: str) -> str | None:
    sym = run("git", "symbolic-ref", "-q", f"refs/remotes/{remote}/HEAD", check=False)
    if sym:
        return sym.removeprefix("refs/remotes/")
    for candidate in (f"{remote}/main", f"{remote}/master"):
        if exists(f"{candidate}^{{commit}}"):
            return candidate
    items = refs(f"refs/remotes/{remote}")
    return sorted(items)[0] if items else None


def fork_fact(remote: str) -> dict[str, Any]:
    default = remote_default(remote)
    if not default:
        return {"remote": remote, "default": None}
    head = run("git", "rev-parse", default)
    anchor, rel, relations = nearest_anchor(head)
    changed = paths(anchor, head)
    return {
        "remote": remote,
        "default": default.split("/", 1)[1],
        "head": head,
        "tree": run("git", "rev-parse", f"{head}^{{tree}}"),
        "subject": run("git", "show", "-s", "--format=%s", head),
        "committed_at": run("git", "show", "-s", "--format=%cI", head),
        "nearest_anchor": anchor.removeprefix("origin/") if anchor else None,
        "relation": rel,
        "anchor_relations": {k.removeprefix("origin/"): v for k, v in relations.items()},
        "changed_paths": changed,
        "unique_commit_subjects": commits(anchor, head, 20),
    }


def main() -> None:
    OUT.mkdir(exist_ok=True)
    origin_heads = refs("refs/remotes/origin")
    facts = [branch_fact(name, head, origin_heads) for name, head in sorted(origin_heads.items())]
    head_groups: dict[str, list[str]] = collections.defaultdict(list)
    tree_groups: dict[str, list[str]] = collections.defaultdict(list)
    for fact in facts:
        head_groups[fact["head"]].append(fact["name"])
        tree_groups[fact["tree"]].append(fact["name"])
    snapshot = {
        "generated_at": dt.datetime.now(dt.timezone.utc).isoformat(),
        "branch_count": len(facts),
        "duplicate_heads": {k: v for k, v in head_groups.items() if len(v) > 1},
        "duplicate_trees": {k: v for k, v in tree_groups.items() if len(v) > 1},
        "branches": facts,
    }
    (OUT / "branches.json").write_text(json.dumps(snapshot, indent=2) + "\n", encoding="utf-8")

    lines = [
        "# Live branch topology", "",
        f"Generated: {snapshot['generated_at']}", "",
        "| Branch | Head | Flavor | Anchor | Relation | A/B | Product | Tests | Workflows | Covered by |",
        "| --- | --- | --- | --- | --- | ---: | ---: | ---: | ---: | --- |",
    ]
    for item in facts:
        rel = item["relation"]
        counts = "-" if rel["ahead"] is None else f"{rel['ahead']}/{rel['behind']}"
        lines.append(
            f"| `{item['name']}` | `{item['head'][:12]}` | {item['flavor']} | "
            f"`{item['nearest_anchor'] or '-'}` | {rel['kind']} | {counts} | "
            f"{len(item['product_paths'])} | {len(item['test_paths'])} | "
            f"{len(item['workflow_paths_at_head'])} | {', '.join(item['covered_by'][:3]) or '-'} |"
        )
    lines += ["", "## Duplicate heads"]
    for sha, names in snapshot["duplicate_heads"].items():
        lines.append(f"- `{sha}`: " + ", ".join(f"`{n}`" for n in names))
    lines += ["", "## Duplicate trees"]
    for sha, names in snapshot["duplicate_trees"].items():
        lines.append(f"- `{sha}`: " + ", ".join(f"`{n}`" for n in names))
    (OUT / "branches.md").write_text("\n".join(lines) + "\n", encoding="utf-8")

    forks = [fork_fact(remote) for remote in FORK_REMOTES]
    (OUT / "forks.json").write_text(json.dumps({"forks": forks}, indent=2) + "\n", encoding="utf-8")
    fork_lines = ["# Fork defaults", "", "| Remote | Default | Head | Anchor | Relation | A/B | Changed paths |", "| --- | --- | --- | --- | --- | ---: | ---: |"]
    for item in forks:
        if not item.get("default"):
            fork_lines.append(f"| {item['remote']} | unavailable | - | - | - | - | - |")
            continue
        rel = item["relation"]
        counts = "-" if rel["ahead"] is None else f"{rel['ahead']}/{rel['behind']}"
        fork_lines.append(f"| {item['remote']} | `{item['default']}` | `{item['head'][:12]}` | `{item['nearest_anchor'] or '-'}` | {rel['kind']} | {counts} | {len(item['changed_paths'])} |")
    (OUT / "forks.md").write_text("\n".join(fork_lines) + "\n", encoding="utf-8")
    print((OUT / "branches.md").read_text(encoding="utf-8"))
    print((OUT / "forks.md").read_text(encoding="utf-8"))


if __name__ == "__main__":
    main()
