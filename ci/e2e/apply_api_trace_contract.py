#!/usr/bin/env python3
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def replace_once(path: str, old: str, new: str, label: str):
    p = ROOT / path
    s = p.read_text()
    if new in s:
        return
    n = s.count(old)
    if n != 1:
        raise SystemExit(f"{label}: expected one match, found {n}")
    p.write_text(s.replace(old, new, 1))

checker = "ci/e2e/check_gl_semantic_contract.py"
replace_once(
    checker,
    '''    semantics = manifest["semantic_evidence"]\n''',
    '''    semantics = manifest["semantic_evidence"]\n    api_evidence = manifest.get("api_evidence", {})\n''',
    "api evidence declaration",
)
replace_once(
    checker,
    '''            evidence = semantics.get(key)\n            if not evidence or evidence.get("status") != "proven":\n                failures.append(\n                    f"observed but unproven semantic: {key} via {event['api']} line {event['line']}"\n                )\n''',
    '''            if event["domain"] == "api_call" and key.startswith("api."):\n                evidence = api_evidence.get(event["api"])\n                if not evidence or evidence.get("status") != "proven":\n                    failures.append(\n                        f"observed but unproven API: {event['api']} line {event['line']}"\n                    )\n            else:\n                evidence = semantics.get(key)\n                if not evidence or evidence.get("status") != "proven":\n                    failures.append(\n                        f"observed but unproven semantic: {key} via {event['api']} line {event['line']}"\n                    )\n''',
    "api trace gate",
)
replace_once(
    checker,
    '''            "oracle": semantics.get(key, {}).get("oracle"),\n            "status": semantics.get(key, {}).get("status", "unproven"),\n''',
    '''            "oracle": (\n                api_evidence.get(next(iter(value["apis"]), ""), {}).get("oracle")\n                if "api_call" in value["domains"]\n                else semantics.get(key, {}).get("oracle")\n            ),\n            "status": (\n                api_evidence.get(next(iter(value["apis"]), ""), {}).get("status", "unproven")\n                if "api_call" in value["domains"]\n                else semantics.get(key, {}).get("status", "unproven")\n            ),\n''',
    "api report evidence",
)

mp = ROOT / "ci/e2e/gl_semantic_contract.json"
m = json.loads(mp.read_text())
m.setdefault("api_evidence", {})
enforced = m.setdefault("trace_coverage", {}).setdefault("enforced_domains", [])
if "api_call" not in enforced:
    enforced.insert(0, "api_call")
mp.write_text(json.dumps(m, indent=2) + "\n")
print("observed API contract trace transformation complete")
