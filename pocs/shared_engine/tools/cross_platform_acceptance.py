#!/usr/bin/env python3
"""Require one matching POC-01 semantic result from every platform family."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


EXPECTED_DIGEST = "47826449b895ac4f4a57b4f386379775"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--results", type=Path, required=True)
    parser.add_argument(
        "--required",
        nargs="+",
        default=["web", "windows", "macos", "ios", "ipados", "android"],
    )
    args = parser.parse_args()
    records: dict[str, dict] = {}
    failures: list[str] = []
    for path in sorted(args.results.rglob("*.json")):
        try:
            value = json.loads(path.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            continue
        if not isinstance(value, dict) or "platform" not in value or "digest" not in value:
            continue
        platform = value["platform"]
        if platform in records:
            failures.append(f"duplicate platform result: {platform}")
        records[platform] = value
        if value["digest"] != EXPECTED_DIGEST:
            failures.append(
                f"{platform}: expected {EXPECTED_DIGEST}, got {value['digest']}"
            )
        if value.get("lifecycle") != 100:
            failures.append(
                f"{platform}: expected 100 lifecycle iterations, "
                f"got {value.get('lifecycle')}"
            )
        if value.get("smoke_seconds") != 60:
            failures.append(
                f"{platform}: expected a 60 second smoke, "
                f"got {value.get('smoke_seconds')}"
            )
        if not isinstance(value.get("smoke_frames"), int) or value["smoke_frames"] <= 0:
            failures.append(f"{platform}: smoke frame count must be positive")
        max_frame_ms = value.get("max_frame_ms")
        if not isinstance(max_frame_ms, (int, float)) or max_frame_ms > 100:
            failures.append(
                f"{platform}: maximum frame time must be at most 100 ms, "
                f"got {max_frame_ms}"
            )
    missing = sorted(set(args.required) - records.keys())
    if missing:
        failures.append("missing platform results: " + ", ".join(missing))
    report = {
        "expected_digest": EXPECTED_DIGEST,
        "required": args.required,
        "observed": sorted(records),
        "passed": not failures,
        "failures": failures,
    }
    output = args.results / "cross-platform-acceptance.json"
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, sort_keys=True))
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
