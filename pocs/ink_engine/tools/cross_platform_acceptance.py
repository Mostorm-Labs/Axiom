#!/usr/bin/env python3
"""Compare POC-02 semantic replay results without accepting missing platforms."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


EXPECTED = {
    "document_digest": "672abdc8604a169ecda4ce08e8d80b55",
    "stroke_digest": "2ae509ac25da5ca82937044f740265e4",
    "preview_digest": "dfc99ad07efd6162efd8d2ece65e0319",
    "numeric_digest": "ee8615ae2159fc9f42faad6687b0cbd3",
}

DAB_EXPECTED = {
    "dab_document_digest": "ae1726a94d60f3e10c2789034bbd9e36",
    "dab_stroke_digest": "a9d4cf3e83a5f08b45621991271ec8c7",
    "dab_preview_digest": "efa865ba51000559f8c53d0febb539b1",
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--results", type=Path, required=True)
    parser.add_argument("--required", nargs="+", default=["web", "windows", "android"])
    args = parser.parse_args()
    observed: dict[str, dict] = {}
    failures: list[str] = []
    for path in sorted(args.results.rglob("*.json")):
        try:
            value = json.loads(path.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            continue
        if not isinstance(value, dict) or "platform" not in value:
            continue
        platform = value["platform"]
        if platform in observed:
            failures.append(f"duplicate platform result: {platform}")
            continue
        observed[platform] = value
        for key, expected in EXPECTED.items():
            if value.get(key) != expected:
                failures.append(
                    f"{platform}: expected {key}={expected}, got {value.get(key)}"
                )
        for key, expected in DAB_EXPECTED.items():
            if value.get(key) != expected:
                failures.append(
                    f"{platform}: expected {key}={expected}, got {value.get(key)}"
                )
        ratio = value.get("matching_ratio")
        if not isinstance(ratio, (float, int)) or ratio < 0.999:
            failures.append(f"{platform}: visual ratio below 0.999: {ratio}")
        delta = value.get("maximum_channel_delta")
        if not isinstance(delta, int) or delta > 2:
            failures.append(f"{platform}: maximum channel delta exceeds 2: {delta}")
        dab_ratio = value.get("dab_matching_ratio")
        if not isinstance(dab_ratio, (float, int)) or dab_ratio < 0.999:
            failures.append(f"{platform}: Dab visual ratio below 0.999: {dab_ratio}")
        dab_delta = value.get("dab_maximum_channel_delta")
        if not isinstance(dab_delta, int) or dab_delta > 2:
            failures.append(
                f"{platform}: Dab maximum channel delta exceeds 2: {dab_delta}"
            )
    missing = sorted(set(args.required) - observed.keys())
    if missing:
        failures.append("missing platform results: " + ", ".join(missing))
    report = {
        "schema_version": 1,
        "expected": EXPECTED,
        "dab_expected": DAB_EXPECTED,
        "required": args.required,
        "observed": sorted(observed),
        "passed": not failures,
        "failures": failures,
    }
    args.results.mkdir(parents=True, exist_ok=True)
    output = args.results / "cross-platform-acceptance.json"
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, sort_keys=True))
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
