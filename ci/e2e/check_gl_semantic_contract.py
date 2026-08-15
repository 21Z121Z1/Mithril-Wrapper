#!/usr/bin/env python3
import argparse
import json
import pathlib
import re
import subprocess
import sys


def parse_advertised(getter_text: str):
    vm = re.search(r'cached\s*=\s*std::string\("([0-9]+\.[0-9]+)\.0 .*Mithril-Wrapper', getter_text)
    if not vm:
        raise ValueError("could not parse advertised GL version")
    sm = re.search(r'kShadingLangVer\s*=\s*"([0-9]+\.[0-9]+) ', getter_text)
    if not sm:
        raise ValueError("could not parse advertised GLSL version")
    em = re.search(r'static const char\* kExtensions\[\]\s*=\s*\{(.*?)\};', getter_text, re.S)
    if not em:
        raise ValueError("could not parse kExtensions")
    extensions = re.findall(r'"(GL_[A-Za-z0-9_]+)"', em.group(1))

    def integer_case(name: str):
        m = re.search(rf'case\s+{re.escape(name)}\s*:\s*\*params\s*=\s*([0-9]+)\s*;', getter_text)
        if not m:
            raise ValueError(f"could not parse {name} integer query")
        return int(m.group(1))

    major = integer_case("GL_MAJOR_VERSION")
    minor = integer_case("GL_MINOR_VERSION")
    numeric_glsl = integer_case("GL_SHADING_LANGUAGE_VERSION")
    return vm.group(1), sm.group(1), extensions, major, minor, numeric_glsl


def exported_symbols(dylib: pathlib.Path):
    proc = subprocess.run(["nm", "-gU", str(dylib)], text=True, capture_output=True)
    if proc.returncode:
        raise RuntimeError(f"nm failed: {proc.stderr.strip()}")
    out = set()
    for line in proc.stdout.splitlines():
        m = re.search(r'\b_([A-Za-z][A-Za-z0-9_]*)$', line.strip())
        if m:
            out.add(m.group(1))
    return out


def core_symbols_for_version(path: pathlib.Path, target_version: str):
    target = tuple(int(v) for v in target_version.split(".", 1))
    required = []
    malformed = []
    for lineno, raw in enumerate(path.read_text().splitlines(), 1):
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        parts = raw.split("\t")
        if len(parts) != 2:
            malformed.append((lineno, raw))
            continue
        version_tag, symbol = parts
        m = re.fullmatch(r"GL_VERSION_(\d+)_(\d+)", version_tag)
        if not m:
            malformed.append((lineno, raw))
            continue
        version = (int(m.group(1)), int(m.group(2)))
        if version <= target:
            required.append(symbol)
    return sorted(set(required)), malformed


def parse_trace(path: pathlib.Path):
    events = []
    if not path.exists():
        return events
    for lineno, raw in enumerate(path.read_text(errors="replace").splitlines(), 1):
        if not raw.strip():
            continue
        parts = raw.split("\t", 3)
        if len(parts) != 4:
            events.append({"line": lineno, "malformed": raw})
            continue
        domain, semantic, api, details = parts
        events.append({
            "line": lineno,
            "domain": domain,
            "semantic": semantic,
            "api": api,
            "details": details,
        })
    return events


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--getter", required=True)
    ap.add_argument("--dylib")
    ap.add_argument("--trace")
    ap.add_argument("--require-trace", action="store_true")
    ap.add_argument("--report")
    args = ap.parse_args()

    manifest_path = pathlib.Path(args.manifest).resolve()
    getter_path = pathlib.Path(args.getter)
    manifest = json.loads(manifest_path.read_text())
    getter_text = getter_path.read_text()
    gl_version, glsl_version, advertised, major, minor, numeric_glsl = parse_advertised(getter_text)

    failures = []
    notes = []
    core = manifest["core"]
    if gl_version != core["version"]:
        failures.append(f"GL_VERSION source={gl_version} contract={core['version']}")
    if glsl_version != core["glsl_version"]:
        failures.append(f"GLSL source={glsl_version} contract={core['glsl_version']}")

    try:
        contract_major, contract_minor = [int(x) for x in core["version"].split(".", 1)]
        contract_numeric_glsl = int(core["glsl_version"].replace(".", ""))
    except Exception as exc:
        failures.append(f"invalid core version contract: {exc}")
        contract_major = contract_minor = contract_numeric_glsl = -1
    if (major, minor) != (contract_major, contract_minor):
        failures.append(
            f"GL_MAJOR/MINOR integer query={major}.{minor} contract={core['version']}"
        )
    if numeric_glsl != contract_numeric_glsl:
        failures.append(
            f"GL_SHADING_LANGUAGE_VERSION integer query={numeric_glsl} contract={contract_numeric_glsl}"
        )

    semantics = manifest["semantic_evidence"]
    if core.get("status") != "minecraft-e2e-proven":
        failures.append("core version status must be minecraft-e2e-proven")
    if not core.get("state_semantics"):
        failures.append("core version has no state semantic evidence")
    if not core.get("oracles"):
        failures.append("core version has no executable oracle evidence")
    for semantic in core.get("state_semantics", []):
        ev = semantics.get(semantic)
        if not ev or ev.get("status") != "proven":
            failures.append(f"core {core['version']}: semantic {semantic} is unproven")

    repo_root = manifest_path.parents[2]
    core_symbols_path = repo_root / core.get("symbols_file", "")
    core_required = []
    core_symbol_malformed = []
    if not core.get("symbols_file"):
        failures.append("core version has no symbols_file")
    elif not core_symbols_path.is_file():
        failures.append(f"core symbols file missing: {core_symbols_path}")
    else:
        core_required, core_symbol_malformed = core_symbols_for_version(
            core_symbols_path, core["version"]
        )
        if core_symbol_malformed:
            failures.append(
                f"core symbols file has {len(core_symbol_malformed)} malformed line(s)"
            )
        if not core_required:
            failures.append(f"no cumulative core symbols resolved for GL {core['version']}")

    contract_ext = manifest["advertised_extensions"]
    if len(advertised) != len(set(advertised)):
        failures.append("kExtensions contains duplicate entries")
    missing_contract = sorted(set(advertised) - set(contract_ext))
    over_contract = sorted(set(contract_ext) - set(advertised))
    if missing_contract:
        failures.append("advertised without contract: " + ", ".join(missing_contract))
    if over_contract:
        failures.append("contract says advertised but source omits: " + ", ".join(over_contract))

    for ext_name, entry in contract_ext.items():
        if entry.get("status") != "proven":
            failures.append(f"{ext_name}: status is not proven")
        if not entry.get("symbols"):
            failures.append(f"{ext_name}: no symbol evidence")
        if not entry.get("state_semantics"):
            failures.append(f"{ext_name}: no state semantic evidence")
        if not entry.get("gpu_oracles"):
            failures.append(f"{ext_name}: no executable oracle evidence")
        for semantic in entry.get("state_semantics", []):
            ev = semantics.get(semantic)
            if not ev or ev.get("status") != "proven":
                failures.append(f"{ext_name}: semantic {semantic} is unproven")

    exports = None
    missing_core_exports = []
    if args.dylib:
        dylib = pathlib.Path(args.dylib)
        if not dylib.is_file():
            failures.append(f"dylib missing: {dylib}")
        else:
            exports = exported_symbols(dylib)
            missing_core_exports = sorted(set(core_required) - exports)
            if missing_core_exports:
                failures.append(
                    f"GL {core['version']} core export contract missing "
                    f"{len(missing_core_exports)} symbol(s): "
                    + ", ".join(missing_core_exports[:24])
                    + (" ..." if len(missing_core_exports) > 24 else "")
                )
            for ext_name, entry in contract_ext.items():
                for symbol in entry.get("symbols", []):
                    if symbol not in exports:
                        failures.append(f"{ext_name}: missing exported symbol {symbol}")

    trace_events = []
    observed = {}
    malformed = []
    if args.trace:
        trace_events = parse_trace(pathlib.Path(args.trace))
        for event in trace_events:
            if "malformed" in event:
                malformed.append(event)
                continue
            key = event["semantic"]
            observed.setdefault(key, {"count": 0, "apis": set(), "domains": set()})
            observed[key]["count"] += 1
            observed[key]["apis"].add(event["api"])
            observed[key]["domains"].add(event["domain"])
            evidence = semantics.get(key)
            if not evidence or evidence.get("status") != "proven":
                failures.append(
                    f"observed but unproven semantic: {key} via {event['api']} line {event['line']}"
                )
        if malformed:
            failures.append(f"semantic trace contains {len(malformed)} malformed line(s)")
    if args.require_trace and not trace_events:
        failures.append("semantic trace required but no events were recorded")

    enforced = set(manifest.get("trace_coverage", {}).get("enforced_domains", []))
    if args.require_trace and enforced:
        seen_domains = {
            event.get("domain") for event in trace_events if "malformed" not in event
        }
        missing_domains = sorted(enforced - seen_domains)
        if missing_domains:
            failures.append("required trace domain not observed: " + ", ".join(missing_domains))

    report_observed = {
        key: {
            "count": value["count"],
            "apis": sorted(value["apis"]),
            "domains": sorted(value["domains"]),
            "oracle": semantics.get(key, {}).get("oracle"),
            "status": semantics.get(key, {}).get("status", "unproven"),
        }
        for key, value in sorted(observed.items())
    }
    report = {
        "schema_version": manifest.get("schema_version", "1.0"),
        "profile": manifest.get("profile"),
        "advertised_gl_version": gl_version,
        "advertised_glsl_version": glsl_version,
        "integer_gl_version": f"{major}.{minor}",
        "integer_glsl_version": numeric_glsl,
        "core_symbol_contract": {
            "required_count": len(core_required),
            "missing_count": len(missing_core_exports),
            "symbols_file": core.get("symbols_file"),
        },
        "advertised_extensions": advertised,
        "export_check_performed": exports is not None,
        "trace_event_count": len(trace_events),
        "observed_semantics": report_observed,
        "next_domains": manifest.get("trace_coverage", {}).get("next_domains", []),
        "failures": failures,
        "notes": notes,
        "status": "passed" if not failures else "failed",
    }
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.report:
        pathlib.Path(args.report).write_text(encoded)
    print(encoded, end="")
    if failures:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
