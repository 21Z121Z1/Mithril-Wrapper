#!/usr/bin/env python3
"""Validate Mithril's agent navigation, proof and oracle control plane."""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys
from typing import Any, Iterable

ROOT = pathlib.Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "docs/agent/manifest.json"
PROOF_GRAPH = ROOT / "docs/agent/proof-graph.json"
ORACLES = ROOT / "docs/agent/oracles.json"


class ContractError(RuntimeError):
    pass


def require(value: bool, message: str) -> None:
    if not value:
        raise ContractError(message)


def text(value: Any, where: str) -> str:
    require(isinstance(value, str) and bool(value.strip()), f"{where} must be a non-empty string")
    return value


def unique(values: Iterable[Any], where: str) -> list[str]:
    result = [text(value, f"{where}[]") for value in values]
    require(len(result) == len(set(result)), f"{where} contains duplicates")
    return result


def load(path: pathlib.Path) -> dict[str, Any]:
    require(path.is_file(), f"missing {path.relative_to(ROOT)}")
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ContractError(f"cannot parse {path.relative_to(ROOT)}: {exc}") from exc
    require(isinstance(data, dict), f"{path.relative_to(ROOT)} root must be an object")
    return data


def repo_path(value: Any, where: str) -> pathlib.Path:
    raw = text(value, where)
    path = pathlib.PurePosixPath(raw)
    require(not path.is_absolute() and ".." not in path.parts, f"{where} must be repository-relative")
    resolved = ROOT.joinpath(*path.parts)
    require(resolved.exists(), f"{where} references missing path: {raw}")
    return resolved


def validate_manifest(manifest: dict[str, Any]) -> tuple[set[str], set[str], set[str]]:
    require(manifest.get("schema_version") == 2, "manifest schema_version must be 2")
    repo_path(manifest.get("entrypoint"), "manifest.entrypoint")
    for key, value in manifest.get("authoritative_documents", {}).items():
        repo_path(value, f"manifest.authoritative_documents.{key}")

    layers = manifest.get("layers")
    require(isinstance(layers, list) and len(layers) == 8, "manifest must define L0-L7")
    layer_ids = [text(item.get("id"), "layer.id") for item in layers]
    require(layer_ids == [f"L{i}" for i in range(8)], f"layer order must be L0-L7, got {layer_ids}")
    for item in layers:
        for path in item.get("paths", []):
            repo_path(path, f"layer {item['id']} path")

    roles = manifest.get("branch_roles")
    require(isinstance(roles, list) and roles, "branch_roles required")
    role_names = unique([item.get("name") for item in roles], "branch_roles.name")

    universes = manifest.get("history_universes")
    require(isinstance(universes, list) and len(universes) >= 2, "explicit history universes required")
    universe_ids = unique([item.get("id") for item in universes], "history_universes.id")
    anchors: list[str] = []
    for universe in universes:
        anchors += unique(universe.get("anchors", []), f"history universe {universe['id']} anchors")
        require(universe.get("promotion_mode") in {"normal_git_plus_semantic_proof", "semantic_transplant_only"}, "unknown history promotion mode")
    require(len(anchors) == len(set(anchors)), "history anchors duplicated across universes")
    require(set(role_names).issubset(anchors), "every canonical branch role must belong to a history universe")
    require("clean_shipping" in universe_ids and "legacy_experimental" in universe_ids, "clean and legacy history universes must be explicit")

    components = manifest.get("components")
    require(isinstance(components, list) and components, "components required")
    component_ids = set(unique([item.get("id") for item in components], "components.id"))
    for component in components:
        require(component.get("layer") in layer_ids, f"component {component['id']} has unknown layer")
        unique(component.get("owned_paths", []), f"component {component['id']} owned_paths")
        for path in component.get("read_now", []):
            repo_path(path, f"component {component['id']} read_now")

    boundaries = manifest.get("boundaries")
    require(isinstance(boundaries, list) and boundaries, "boundaries required")
    unique([item.get("id") for item in boundaries], "boundaries.id")
    for boundary in boundaries:
        require(not ((set(boundary.get("from", [])) | set(boundary.get("to", []))) - component_ids), f"boundary {boundary['id']} references unknown component")
        text(boundary.get("contract"), f"boundary {boundary['id']}.contract")

    profiles = manifest.get("proof_profiles")
    require(isinstance(profiles, list) and profiles, "proof_profiles required")
    proof_ids = set(unique([item.get("id") for item in profiles], "proof_profiles.id"))
    ranks: dict[str, int] = {}
    for profile in profiles:
        require(isinstance(profile.get("rank"), int) and profile["rank"] >= 0, f"proof {profile['id']} rank invalid")
        ranks[profile["id"]] = profile["rank"]
        text(profile.get("environment"), f"proof {profile['id']}.environment")
        text(profile.get("proves"), f"proof {profile['id']}.proves")
    for claim, refs in manifest.get("claim_proofs", {}).items():
        require(not (set(refs) - proof_ids), f"claim {claim} references unknown proof")
    for component in components:
        require(not (set(component.get("required_proofs", [])) - proof_ids), f"component {component['id']} references unknown proof")

    tool_ids = set(unique([item.get("id") for item in manifest.get("agent_tools", [])], "agent_tools.id"))
    require({"task_context", "branch_topology", "minecraft_reference_source", "agent_contract_validator"}.issubset(tool_ids), "required agent tools missing")
    for tool in manifest.get("agent_tools", []):
        repo_path(tool.get("script"), f"tool {tool['id']}.script")

    return component_ids, proof_ids, set(layer_ids)


def validate_proof_graph(graph: dict[str, Any], proof_ids: set[str], component_ids: set[str], manifest: dict[str, Any]) -> None:
    require(graph.get("schema_version") == 1, "proof graph schema_version must be 1")
    requires = graph.get("requires")
    require(isinstance(requires, dict), "proof graph requires map missing")
    require(set(requires) == proof_ids, "proof graph must define prerequisites for every proof profile exactly once")
    for proof_id, deps in requires.items():
        require(isinstance(deps, list), f"proof {proof_id} prerequisites must be an array")
        require(not (set(deps) - proof_ids), f"proof {proof_id} has unknown prerequisite")

    ranks = {item["id"]: item["rank"] for item in manifest["proof_profiles"]}
    visiting: set[str] = set()
    done: set[str] = set()
    def visit(node: str) -> None:
        if node in done:
            return
        require(node not in visiting, f"proof dependency cycle at {node}")
        visiting.add(node)
        for dep in requires[node]:
            require(ranks[dep] <= ranks[node], f"proof prerequisite {dep} ranks after {node}")
            visit(dep)
        visiting.remove(node)
        done.add(node)
    for proof_id in proof_ids:
        visit(proof_id)

    obligations = graph.get("component_obligations")
    require(isinstance(obligations, dict), "component_obligations missing")
    require(not (set(obligations) - component_ids), "proof graph has unknown component obligation")
    for component_id, refs in obligations.items():
        require(not (set(refs) - proof_ids), f"component obligation {component_id} references unknown proof")
    for shared in ("host.egl", "gl.semantics", "shader.contract", "semantic.lowering"):
        required = set(obligations.get(shared, []))
        require({"directmetal_ctest", "directvulkan_ctest"}.issubset(required), f"shared component {shared} must preserve both backend regression obligations")


def validate_oracles(index: dict[str, Any], component_ids: set[str], proof_ids: set[str]) -> None:
    require(index.get("schema_version") == 1, "oracle index schema_version must be 1")
    oracles = index.get("oracles")
    require(isinstance(oracles, list) and oracles, "oracle index must be non-empty")
    unique([item.get("id") for item in oracles], "oracle.id")
    allowed_backends = {"directmetal", "vulkan"}
    covered_components: set[str] = set()
    for oracle in oracles:
        repo_path(oracle.get("path"), f"oracle {oracle['id']}.path")
        components = set(oracle.get("components", []))
        require(components and not (components - component_ids), f"oracle {oracle['id']} has unknown components")
        covered_components |= components
        keywords = oracle.get("keywords")
        require(isinstance(keywords, list) and keywords, f"oracle {oracle['id']} needs keywords")
        require(not (set(oracle.get("backends", [])) - allowed_backends), f"oracle {oracle['id']} has unknown backend")
        require(oracle.get("proof") in proof_ids, f"oracle {oracle['id']} has unknown proof")
    require({"host.egl", "gl.semantics", "shader.contract", "semantic.lowering"}.issubset(covered_components), "core semantic components need indexed oracles")


def validate_documents() -> None:
    for path in (
        "docs/README.md",
        "docs/contracts/amethyst-host-contract.md",
        "docs/history/implementation-checklist-2026-08.md",
        "docs/agent/proof-graph.json",
        "docs/agent/oracles.json",
    ):
        repo_path(path, "document contract")
    readme = (ROOT / "README.md").read_text(encoding="utf-8")
    require("scripts/agent-context.py" in readme and "docs/system-model.md" in readme, "README must be a system/agent entrypoint")
    checklist = (ROOT / "CHECKLIST.md").read_text(encoding="utf-8")
    require("docs/history/implementation-checklist-2026-08.md" in checklist, "CHECKLIST must point to frozen historical copy")
    host = (ROOT / "docs/contracts/amethyst-host-contract.md").read_text(encoding="utf-8")
    require("eglGetProcAddress" in host and "semantic" in host.lower(), "host contract missing current ABI/ownership boundary")


def validate_workflow() -> None:
    workflow = (ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")
    require("github.event.pull_request.head.sha" in workflow, "candidate identity must be explicit")
    require("Verify exact candidate identity" in workflow, "candidate checkout must be asserted")
    require("integration/directmetal-next" in workflow and "main" in workflow, "normal gate must cover both clean canonical refs")


def run_self_tests() -> None:
    for script in ("scripts/agent-context.py", "scripts/audit-branches.py"):
        subprocess.run([sys.executable, script, "--self-test"], cwd=ROOT, check=True)


def main() -> int:
    try:
        manifest = load(MANIFEST)
        graph = load(PROOF_GRAPH)
        oracles = load(ORACLES)
        component_ids, proof_ids, _ = validate_manifest(manifest)
        validate_proof_graph(graph, proof_ids, component_ids, manifest)
        validate_oracles(oracles, component_ids, proof_ids)
        validate_documents()
        validate_workflow()
        run_self_tests()
    except (ContractError, subprocess.CalledProcessError, OSError, json.JSONDecodeError) as exc:
        print(f"AGENT CONTRACT INVALID: {exc}", file=sys.stderr)
        return 1
    print("AGENT CONTRACT VALID")
    print(f"components={len(component_ids)} proofs={len(proof_ids)} proof_dag=acyclic oracle_index=valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
