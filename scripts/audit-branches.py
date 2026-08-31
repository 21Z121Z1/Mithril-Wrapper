#!/usr/bin/env python3
"""Emit a live, read-only branch topology snapshot for repository agents.

The default mode never mutates refs and uses whatever remote-tracking graph is
already available. --fetch-graph performs one blob-filtered fetch of all remote
heads so ancestry/ahead/behind and tree-flavor facts are current without
checking out or downloading branch working trees.
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
from typing import Optional

ROOT = pathlib.Path(__file__).resolve().parents[1]


def git(*args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", *args],
        cwd=ROOT,
        check=check,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def git_output(*args: str) -> str:
    return git(*args).stdout.strip()


def object_exists(spec: str) -> bool:
    return git("cat-file", "-e", spec, check=False).returncode == 0


def normalize_branch(remote: str, short_ref: str) -> Optional[str]:
    prefix = f"{remote}/"
    if not short_ref.startswith(prefix):
        return None
    branch = short_ref[len(prefix):]
    if branch == "HEAD":
        return None
    return branch


@dataclass
class BranchFact:
    name: str
    head: str
    relation_to_base: str
    ahead: Optional[int]
    behind: Optional[int]
    merge_base: Optional[str]
    tree_flavor: str
    duplicate_head: bool


def relation(base_sha: str, head_sha: str) -> tuple[str, Optional[int], Optional[int], Optional[str]]:
    if base_sha == head_sha:
        return "same", 0, 0, base_sha
    if not object_exists(f"{base_sha}^{{commit}}") or not object_exists(f"{head_sha}^{{commit}}"):
        return "graph_unavailable", None, None, None

    merge = git("merge-base", base_sha, head_sha, check=False)
    if merge.returncode != 0:
        return "no_common_ancestor", None, None, None
    merge_base = merge.stdout.strip()

    counts = git_output("rev-list", "--left-right", "--count", f"{base_sha}...{head_sha}")
    left, right = (int(part) for part in counts.split())
    behind = left
    ahead = right

    if merge_base == base_sha:
        kind = "ahead"
    elif merge_base == head_sha:
        kind = "behind"
    else:
        kind = "diverged"
    return kind, ahead, behind, merge_base


def tree_flavor(head_sha: str) -> str:
    if not object_exists(f"{head_sha}^{{commit}}"):
        return "unknown"
    has_clean = object_exists(f"{head_sha}:src")
    has_legacy = object_exists(f"{head_sha}:Mithril-Wrapper-cpp")
    if has_clean and has_legacy:
        return "hybrid"
    if has_clean:
        return "clean"
    if has_legacy:
        return "legacy"
    return "other"


def read_remote_refs(remote: str) -> list[tuple[str, str]]:
    output = git_output(
        "for-each-ref",
        "--format=%(refname:short)\t%(objectname)",
        f"refs/remotes/{remote}",
    )
    refs: list[tuple[str, str]] = []
    for line in output.splitlines():
        if not line:
            continue
        short_ref, sha = line.split("\t", 1)
        branch = normalize_branch(remote, short_ref)
        if branch is not None:
            refs.append((branch, sha))
    return sorted(refs)


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


def build_snapshot(remote: str, base_branch: str, refreshed: bool) -> dict:
    live = live_heads(remote)
    if base_branch not in live:
        raise RuntimeError(f"base branch {base_branch!r} does not exist on remote {remote!r}")
    base_sha = live[base_branch]

    tracked = dict(read_remote_refs(remote))
    duplicates = collections.Counter(live.values())
    facts: list[BranchFact] = []

    for name, head in sorted(live.items()):
        # Use the exact live SHA. If the local object graph is stale/unavailable,
        # relation() says so rather than silently comparing another commit.
        local_sha = tracked.get(name)
        graph_head = head if local_sha == head and object_exists(f"{head}^{{commit}}") else head
        kind, ahead, behind, merge_base = relation(base_sha, graph_head)
        facts.append(BranchFact(
            name=name,
            head=head,
            relation_to_base=kind,
            ahead=ahead,
            behind=behind,
            merge_base=merge_base,
            tree_flavor=tree_flavor(graph_head),
            duplicate_head=duplicates[head] > 1,
        ))

    duplicate_groups = [
        {"head": sha, "branches": sorted(name for name, value in live.items() if value == sha)}
        for sha, count in sorted(duplicates.items())
        if count > 1
    ]

    return {
        "schema_version": 1,
        "generated_at_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "remote": remote,
        "base_branch": base_branch,
        "base_sha": base_sha,
        "graph_refreshed": refreshed,
        "branch_count": len(facts),
        "duplicate_head_groups": duplicate_groups,
        "branches": [asdict(fact) for fact in facts],
    }


def print_markdown(snapshot: dict) -> None:
    print(f"# Branch topology ({snapshot['generated_at_utc']})")
    print()
    print(f"Base: `{snapshot['base_branch']}` @ `{snapshot['base_sha']}`")
    print(f"Branches: {snapshot['branch_count']}; graph_refreshed={str(snapshot['graph_refreshed']).lower()}")
    print()
    print("| Branch | HEAD | Tree | Relation | Ahead | Behind |")
    print("| --- | --- | --- | --- | ---: | ---: |")
    for item in snapshot["branches"]:
        ahead = "?" if item["ahead"] is None else str(item["ahead"])
        behind = "?" if item["behind"] is None else str(item["behind"])
        duplicate = " *" if item["duplicate_head"] else ""
        print(
            f"| `{item['name']}` | `{item['head'][:12]}`{duplicate} | "
            f"{item['tree_flavor']} | {item['relation_to_base']} | {ahead} | {behind} |"
        )
    if snapshot["duplicate_head_groups"]:
        print()
        print("`*` = exact duplicate branch HEAD. Duplicate groups:")
        for group in snapshot["duplicate_head_groups"]:
            names = ", ".join(f"`{name}`" for name in group["branches"])
            print(f"- `{group['head']}`: {names}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--remote", default="origin")
    parser.add_argument("--base", default="main")
    parser.add_argument(
        "--fetch-graph",
        action="store_true",
        help="fetch all remote branch commit graphs with --filter=blob:none before auditing",
    )
    parser.add_argument("--markdown", action="store_true", help="emit a compact Markdown table instead of JSON")
    args = parser.parse_args()

    try:
        if args.fetch_graph:
            fetch_graph(args.remote)
        snapshot = build_snapshot(args.remote, args.base, args.fetch_graph)
    except (RuntimeError, subprocess.SubprocessError, ValueError) as exc:
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
