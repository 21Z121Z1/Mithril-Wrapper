#!/usr/bin/env python3
"""Validate the small machine-readable contract used to orient repository agents."""

from __future__ import annotations

import json
import pathlib
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
    # The legacy root deliberately does not exist in the clean shipping tree.
    # Its name is metadata describing disconnected histories, not a local path contract.
    require_string(product.get("legacy_tree_root"), "product.legacy_tree_root")


def validate_layers(manifest: dict[str, Any]) -> None:
    layers = manifest.get("layers")
    require(isinstance(layers, list), "layers must be an array")
    require(len(layers) == 8, "layers must contain the complete L0-L7 abstraction tower")

    ids: list[str] = []
    names: list[str] = []
    for index, layer in enumerate(layers):
        require(isinstance(layer, dict), f"layers[{index}] must be an object")
        layer_id = require_string(layer.get("id"), f"layers[{index}].id")
        name = require_string(layer.get("name"), f"layers[{index}].name")
        require_string(layer.get("question"), f"layers[{index}].question")
        paths = layer.get("paths")
        require(isinstance(paths, list) and paths, f"layers[{index}].paths must be non-empty")
        for path_index, path in enumerate(paths):
            repo_path(path, f"layers[{index}].paths[{path_index}]")
        ids.append(layer_id)
        names.append(name)

    require(ids == [f"L{i}" for i in range(8)], f"layer IDs/order must be L0..L7, got {ids}")
    require(len(names) == len(set(names)), "layer names must be unique")


def validate_branch_roles(manifest: dict[str, Any]) -> None:
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


def validate_invariants(manifest: dict[str, Any]) -> None:
    invariants = manifest.get("invariants")
    require(isinstance(invariants, list) and len(invariants) >= 5, "invariants must contain the core system constraints")
    unique_strings(invariants, "invariants")


def validate_evidence_planes(manifest: dict[str, Any]) -> None:
    planes = manifest.get("evidence_planes")
    require(isinstance(planes, list) and planes, "evidence_planes must be a non-empty array")

    ids: list[str] = []
    for index, plane in enumerate(planes):
        require(isinstance(plane, dict), f"evidence_planes[{index}] must be an object")
        ids.append(require_string(plane.get("id"), f"evidence_planes[{index}].id"))
        require_string(plane.get("role"), f"evidence_planes[{index}].role")
        locator_fields = [field for field in ("workflow", "script") if field in plane]
        require(len(locator_fields) == 1, f"evidence_planes[{index}] must define exactly one workflow or script")
        repo_path(plane[locator_fields[0]], f"evidence_planes[{index}].{locator_fields[0]}")

    unique_strings(ids, "evidence_planes.id")


def validate_protocols(manifest: dict[str, Any]) -> None:
    proof = manifest.get("proof_of_branch_reuse")
    require(isinstance(proof, list) and len(proof) == 3, "proof_of_branch_reuse must contain the three-part proof")
    unique_strings(proof, "proof_of_branch_reuse")

    protocol = manifest.get("completion_protocol")
    require(isinstance(protocol, list) and len(protocol) >= 6, "completion_protocol is incomplete")
    unique_strings(protocol, "completion_protocol")

    lifetimes = manifest.get("knowledge_lifetimes")
    require(isinstance(lifetimes, dict), "knowledge_lifetimes must be an object")
    for key in ("architecture", "current", "executable", "historical"):
        require(key in lifetimes, f"knowledge_lifetimes missing {key}")
        values = lifetimes[key]
        require(isinstance(values, list) and values, f"knowledge_lifetimes.{key} must be non-empty")
        unique_strings(values, f"knowledge_lifetimes.{key}")


def validate_document_links() -> None:
    agents = (ROOT / "AGENTS.md").read_text(encoding="utf-8")
    for required in (
        "docs/system-model.md",
        "docs/agent/status.md",
        "docs/agent/manifest.json",
        "docs/evidence-model.md",
        "docs/agent/branch-ledger.md",
    ):
        require(required in agents, f"AGENTS.md must point agents to {required}")

    status = (ROOT / "docs" / "agent" / "status.md").read_text(encoding="utf-8")
    require("As of:" in status, "status.md must declare an explicit snapshot date")


def main() -> int:
    try:
        manifest = load_manifest()
        require(manifest.get("schema_version") == 1, "schema_version must be 1")
        require_string(manifest.get("as_of"), "as_of")
        require_string(manifest.get("purpose"), "purpose")
        validate_authoritative_documents(manifest)
        validate_product(manifest)
        validate_layers(manifest)
        validate_branch_roles(manifest)
        validate_invariants(manifest)
        validate_evidence_planes(manifest)
        validate_protocols(manifest)
        validate_document_links()
    except ContractError as exc:
        print(f"AGENT CONTRACT INVALID: {exc}", file=sys.stderr)
        return 1

    print("AGENT CONTRACT VALID")
    print("layers=L0-L7 branch_roles=%d evidence_planes=%d" % (
        len(manifest["branch_roles"]), len(manifest["evidence_planes"])
    ))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
