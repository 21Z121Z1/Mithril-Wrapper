#!/usr/bin/env python3
"""Validate the machine-readable control contract used to orient Mithril agents."""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys
from typing import Any, Iterable

ROOT = pathlib.Path(__file__).resolve().parents[1]
MANIFEST_PATH = ROOT / "docs" / "agent" / "manifest.json"


class ContractError(RuntimeError):
    pass


def fail(message: str) -> None:
    raise ContractError(message)


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def require_string(value: Any, where: str) -> str:
    require(isinstance(value, str) and bool(value.strip()), f"{where} must be a non-empty string")
    return value


def repo_path(value: Any, where: str) -> pathlib.Path:
    text = require_string(value, where)
    path = pathlib.PurePosixPath(text)
    require(not path.is_absolute(), f"{where} must be repository-relative: {text}")
    require(".." not in path.parts, f"{where} must not escape the repository: {text}")
    resolved = ROOT.joinpath(*path.parts)
    require(resolved.exists(), f"{where} references a missing repository path: {text}")
    return resolved


def unique_strings(values: Iterable[Any], where: str) -> list[str]:
    result = [require_string(value, f"{where}[]") for value in values]
    require(len(result) == len(set(result)), f"{where} contains duplicates")
    return result


def load_manifest() -> dict[str, Any]:
    require(MANIFEST_PATH.is_file(), f"missing manifest: {MANIFEST_PATH.relative_to(ROOT)}")
    try:
        parsed = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"cannot parse {MANIFEST_PATH.relative_to(ROOT)}: {exc}")
    require(isinstance(parsed, dict), "manifest root must be an object")
    return parsed


def validate_authoritative_documents(manifest: dict[str, Any]) -> None:
    repo_path(manifest.get("entrypoint"), "entrypoint")
    docs = manifest.get("authoritative_documents")
    require(isinstance(docs, dict) and docs, "authoritative_documents must be a non-empty object")
    for key, value in docs.items():
        repo_path(value, f"authoritative_documents.{key}")


def validate_product(manifest: dict[str, Any]) -> None:
    product = manifest.get("product")
    require(isinstance(product, dict), "product must be an object")
    require_string(product.get("shipping_branch"), "product.shipping_branch")
    require_string(product.get("apple_shipping_backend"), "product.apple_shipping_backend")
    require_string(product.get("reference_backend"), "product.reference_backend")
    repo_path(product.get("clean_tree_root"), "product.clean_tree_root")
    # The legacy root intentionally does not exist in the clean shipping tree.
    require_string(product.get("legacy_tree_root"), "product.legacy_tree_root")


def validate_layers(manifest: dict[str, Any]) -> set[str]:
    layers = manifest.get("layers")
    require(isinstance(layers, list), "layers must be an array")
    require(len(layers) == 8, "layers must contain the complete L0-L7 abstraction tower")
    ids: list[str] = []
    names: list[str] = []
    for index, layer in enumerate(layers):
        require(isinstance(layer, dict), f"layers[{index}] must be an object")
        ids.append(require_string(layer.get("id"), f"layers[{index}].id"))
        names.append(require_string(layer.get("name"), f"layers[{index}].name"))
        require_string(layer.get("question"), f"layers[{index}].question")
        paths = layer.get("paths")
        require(isinstance(paths, list) and paths, f"layers[{index}].paths must be non-empty")
        for path_index, path in enumerate(paths):
            repo_path(path, f"layers[{index}].paths[{path_index}]")
    require(ids == [f"L{i}" for i in range(8)], f"layer IDs/order must be L0..L7, got {ids}")
    require(len(names) == len(set(names)), "layer names must be unique")
    return set(ids)


def validate_branch_roles(manifest: dict[str, Any]) -> set[str]:
    roles = manifest.get("branch_roles")
    require(isinstance(roles, list) and roles, "branch_roles must be a non-empty array")
    names: list[str] = []
    allowed_trees = {"clean", "legacy_disconnected"}
    for index, role in enumerate(roles):
        require(isinstance(role, dict), f"branch_roles[{index}] must be an object")
        names.append(require_string(role.get("name"), f"branch_roles[{index}].name"))
        tree = require_string(role.get("tree"), f"branch_roles[{index}].tree")
        require(tree in allowed_trees, f"branch_roles[{index}].tree has unsupported value: {tree}")
        require_string(role.get("role"), f"branch_roles[{index}].role")
        require_string(role.get("merge_policy"), f"branch_roles[{index}].merge_policy")
    unique_strings(names, "branch_roles.name")
    require("main" in names, "branch_roles must define main")
    require("integration/directmetal-next" in names, "branch_roles must define DirectMetal integration ownership")
    return set(names)


def validate_history_universes(manifest: dict[str, Any], branch_names: set[str]) -> None:
    universes = manifest.get("history_universes")
    require(isinstance(universes, list) and len(universes) >= 2, "history_universes must explicitly model disconnected histories")
    ids: list[str] = []
    all_anchors: list[str] = []
    allowed_modes = {"normal_git_plus_semantic_proof", "semantic_transplant_only"}
    for index, universe in enumerate(universes):
        require(isinstance(universe, dict), f"history_universes[{index}] must be an object")
        ids.append(require_string(universe.get("id"), f"history_universes[{index}].id"))
        anchors = universe.get("anchors")
        require(isinstance(anchors, list) and anchors, f"history_universes[{index}].anchors must be non-empty")
        anchors = unique_strings(anchors, f"history_universes[{index}].anchors")
        all_anchors.extend(anchors)
        mode = require_string(universe.get("promotion_mode"), f"history_universes[{index}].promotion_mode")
        require(mode in allowed_modes, f"history_universes[{index}] has unsupported promotion mode: {mode}")
    unique_strings(ids, "history_universes.id")
    require(len(all_anchors) == len(set(all_anchors)), "history-universe anchors must not be duplicated across universes")
    require(branch_names.issubset(set(all_anchors)), "every declared canonical branch role must belong to a history universe")
    require(any(u.get("promotion_mode") == "semantic_transplant_only" for u in universes), "disconnected migration universe must fail closed to semantic transplant")


def validate_components(manifest: dict[str, Any], layer_ids: set[str]) -> set[str]:
    components = manifest.get("components")
    require(isinstance(components, list) and components, "components must be a non-empty array")
    ids: list[str] = []
    for index, component in enumerate(components):
        require(isinstance(component, dict), f"components[{index}] must be an object")
        component_id = require_string(component.get("id"), f"components[{index}].id")
        ids.append(component_id)
        require(component.get("layer") in layer_ids, f"component {component_id} references unknown layer")
        require_string(component.get("summary"), f"components[{index}].summary")
        owned = component.get("owned_paths")
        require(isinstance(owned, list) and owned, f"components[{index}].owned_paths must be non-empty")
        unique_strings(owned, f"components[{index}].owned_paths")
        read_now = component.get("read_now")
        require(isinstance(read_now, list) and read_now, f"components[{index}].read_now must be non-empty")
        for path_index, path in enumerate(read_now):
            repo_path(path, f"components[{index}].read_now[{path_index}]")
    unique_strings(ids, "components.id")
    return set(ids)


def validate_proofs(manifest: dict[str, Any]) -> set[str]:
    profiles = manifest.get("proof_profiles")
    require(isinstance(profiles, list) and profiles, "proof_profiles must be a non-empty array")
    ids: list[str] = []
    ranks: list[int] = []
    for index, profile in enumerate(profiles):
        require(isinstance(profile, dict), f"proof_profiles[{index}] must be an object")
        ids.append(require_string(profile.get("id"), f"proof_profiles[{index}].id"))
        rank = profile.get("rank")
        require(isinstance(rank, int) and rank >= 0, f"proof_profiles[{index}].rank must be non-negative")
        ranks.append(rank)
        require_string(profile.get("environment"), f"proof_profiles[{index}].environment")
        require_string(profile.get("cost"), f"proof_profiles[{index}].cost")
        require_string(profile.get("command"), f"proof_profiles[{index}].command")
        require_string(profile.get("proves"), f"proof_profiles[{index}].proves")
    unique_strings(ids, "proof_profiles.id")
    proof_ids = set(ids)
    claims = manifest.get("claim_proofs")
    require(isinstance(claims, dict) and claims, "claim_proofs must be a non-empty object")
    for claim, refs in claims.items():
        require(isinstance(refs, list) and refs, f"claim_proofs.{claim} must be non-empty")
        unknown = set(refs) - proof_ids
        require(not unknown, f"claim_proofs.{claim} references unknown proofs: {sorted(unknown)}")
    return proof_ids


def validate_components_proofs(manifest: dict[str, Any], proof_ids: set[str]) -> None:
    for component in manifest.get("components", []):
        refs = component.get("required_proofs")
        require(isinstance(refs, list) and refs, f"component {component['id']} required_proofs must be non-empty")
        unknown = set(refs) - proof_ids
        require(not unknown, f"component {component['id']} references unknown proofs: {sorted(unknown)}")


def validate_boundaries(manifest: dict[str, Any], component_ids: set[str]) -> None:
    boundaries = manifest.get("boundaries")
    require(isinstance(boundaries, list) and boundaries, "boundaries must be a non-empty array")
    ids: list[str] = []
    for index, boundary in enumerate(boundaries):
        require(isinstance(boundary, dict), f"boundaries[{index}] must be an object")
        boundary_id = require_string(boundary.get("id"), f"boundaries[{index}].id")
        ids.append(boundary_id)
        sources = boundary.get("from")
        targets = boundary.get("to")
        require(isinstance(sources, list) and sources, f"boundary {boundary_id}.from must be non-empty")
        require(isinstance(targets, list) and targets, f"boundary {boundary_id}.to must be non-empty")
        unknown = (set(sources) | set(targets)) - component_ids
        require(not unknown, f"boundary {boundary_id} references unknown components: {sorted(unknown)}")
        require_string(boundary.get("contract"), f"boundary {boundary_id}.contract")
    unique_strings(ids, "boundaries.id")


def validate_invariants(manifest: dict[str, Any]) -> None:
    invariants = manifest.get("invariants")
    require(isinstance(invariants, list) and len(invariants) >= 8, "invariants must contain the core system constraints")
    unique_strings(invariants, "invariants")


def validate_evidence_planes(manifest: dict[str, Any]) -> None:
    planes = manifest.get("evidence_planes")
    require(isinstance(planes, list) and planes, "evidence_planes must be a non-empty array")
    ids: list[str] = []
    for index, plane in enumerate(planes):
        require(isinstance(plane, dict), f"evidence_planes[{index}] must be an object")
        ids.append(require_string(plane.get("id"), f"evidence_planes[{index}].id"))
        require_string(plane.get("role"), f"evidence_planes[{index}].role")
        repo_path(plane.get("workflow"), f"evidence_planes[{index}].workflow")
        require_string(plane.get("mutation"), f"evidence_planes[{index}].mutation")
    unique_strings(ids, "evidence_planes.id")


def validate_agent_tools(manifest: dict[str, Any]) -> None:
    tools = manifest.get("agent_tools")
    require(isinstance(tools, list) and tools, "agent_tools must be a non-empty array")
    ids: list[str] = []
    for index, tool in enumerate(tools):
        require(isinstance(tool, dict), f"agent_tools[{index}] must be an object")
        ids.append(require_string(tool.get("id"), f"agent_tools[{index}].id"))
        repo_path(tool.get("script"), f"agent_tools[{index}].script")
        require_string(tool.get("role"), f"agent_tools[{index}].role")
    unique_strings(ids, "agent_tools.id")
    for required in ("task_context", "branch_topology", "minecraft_reference_source", "agent_contract_validator"):
        require(required in ids, f"agent_tools must expose {required}")


def validate_protocols(manifest: dict[str, Any]) -> None:
    proof = manifest.get("proof_of_branch_reuse")
    require(isinstance(proof, list) and len(proof) == 3, "proof_of_branch_reuse must contain the three-part proof")
    unique_strings(proof, "proof_of_branch_reuse")
    protocol = manifest.get("completion_protocol")
    require(isinstance(protocol, list) and len(protocol) >= 8, "completion_protocol is incomplete")
    unique_strings(protocol, "completion_protocol")
    lifetimes = manifest.get("knowledge_lifetimes")
    require(isinstance(lifetimes, dict), "knowledge_lifetimes must be an object")
    for key in ("architecture", "current", "executable", "historical"):
        values = lifetimes.get(key)
        require(isinstance(values, list) and values, f"knowledge_lifetimes.{key} must be non-empty")
        unique_strings(values, f"knowledge_lifetimes.{key}")


def validate_document_links() -> None:
    agents = (ROOT / "AGENTS.md").read_text(encoding="utf-8")
    for required in (
        "scripts/agent-context.py",
        "scripts/audit-branches.py",
        "docs/system-model.md",
        "docs/evidence-model.md",
        "docs/agent/manifest.json",
    ):
        require(required in agents, f"AGENTS.md must point agents to {required}")
    status = (ROOT / "docs/agent/status.md").read_text(encoding="utf-8")
    require("As of:" in status, "status.md must declare an explicit snapshot date")
    branch_policy = (ROOT / "docs/branches.md").read_text(encoding="utf-8")
    require("semantic" in branch_policy.lower() and "no common" in branch_policy.lower(), "branch policy must preserve semantic-port/no-common-ancestor rules")
    evidence = (ROOT / "docs/evidence-model.md").read_text(encoding="utf-8")
    require("tree" in evidence.lower() and "candidate" in evidence.lower(), "evidence model must bind claims to exact tree/candidate identity")


def validate_workflow_subject_contract() -> None:
    workflow = (ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")
    require("github.event.pull_request.head.sha" in workflow, "agent-contract PR proof must explicitly name candidate HEAD")
    require("Verify exact candidate identity" in workflow, "agent-contract workflow must assert the checked-out candidate identity")


def run_self_tests() -> None:
    subprocess.run([sys.executable, "scripts/agent-context.py", "--self-test"], cwd=ROOT, check=True)
    subprocess.run([sys.executable, "scripts/audit-branches.py", "--self-test"], cwd=ROOT, check=True)


def main() -> int:
    try:
        manifest = load_manifest()
        require(manifest.get("schema_version") == 2, "schema_version must be 2")
        require_string(manifest.get("as_of"), "as_of")
        require_string(manifest.get("purpose"), "purpose")
        validate_authoritative_documents(manifest)
        validate_product(manifest)
        layer_ids = validate_layers(manifest)
        branch_names = validate_branch_roles(manifest)
        validate_history_universes(manifest, branch_names)
        component_ids = validate_components(manifest, layer_ids)
        proof_ids = validate_proofs(manifest)
        validate_components_proofs(manifest, proof_ids)
        validate_boundaries(manifest, component_ids)
        validate_invariants(manifest)
        validate_evidence_planes(manifest)
        validate_agent_tools(manifest)
        validate_protocols(manifest)
        validate_document_links()
        validate_workflow_subject_contract()
        run_self_tests()
    except (ContractError, subprocess.CalledProcessError) as exc:
        print(f"AGENT CONTRACT INVALID: {exc}", file=sys.stderr)
        return 1

    print("AGENT CONTRACT VALID")
    print(
        "layers=L0-L7 branch_roles=%d universes=%d components=%d boundaries=%d proofs=%d tools=%d" % (
            len(manifest["branch_roles"]), len(manifest["history_universes"]),
            len(manifest["components"]), len(manifest["boundaries"]),
            len(manifest["proof_profiles"]), len(manifest["agent_tools"])
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
