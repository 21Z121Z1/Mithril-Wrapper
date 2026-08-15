#!/usr/bin/env python3
import argparse
import json
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--ledger", required=True)
    ap.add_argument("--phase", required=True)
    ap.add_argument("--report")
    args = ap.parse_args()

    manifest = json.loads(Path(args.manifest).read_text())
    phases = manifest.get("oracle_phases", {})
    if args.phase not in phases:
        raise SystemExit(f"unknown oracle phase {args.phase!r}")

    ledger_path = Path(args.ledger)
    observed = []
    if ledger_path.is_file():
        observed = [line.strip() for line in ledger_path.read_text().splitlines() if line.strip()]
    required = list(phases[args.phase])
    missing = sorted(set(required) - set(observed))
    unexpected = sorted(set(observed) - set(required))
    duplicates = sorted({name for name in observed if observed.count(name) > 1})

    report = {
        "schema_version": "1.0",
        "phase": args.phase,
        "required": required,
        "observed": observed,
        "missing": missing,
        "unexpected": unexpected,
        "duplicates": duplicates,
        "status": "passed" if not missing and not unexpected and not duplicates else "failed",
    }
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    print(encoded, end="")
    if args.report:
        Path(args.report).write_text(encoded)
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
