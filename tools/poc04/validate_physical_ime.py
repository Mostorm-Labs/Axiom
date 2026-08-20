#!/usr/bin/env python3
"""Validate controlled POC-04 Windows or Chrome physical IME evidence."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def validate(path: Path) -> None:
    report = json.loads(path.read_text(encoding="utf-8"))
    required = {
        "schema_version", "platform", "protocol", "controlled_flow",
        "controlled_flow_passed", "final_text", "selection", "caret",
        "digest", "observed_composition_start", "observed_composition_update",
    }
    missing = sorted(required - report.keys())
    if missing:
        raise RuntimeError(f"{path}: missing fields: {', '.join(missing)}")
    if report["schema_version"] != 1:
        raise RuntimeError(f"{path}: unsupported schema version")
    if report["platform"] not in {"windows", "web"}:
        raise RuntimeError(f"{path}: unexpected platform")
    if report["controlled_flow"] != "ni hao -> 你好":
        raise RuntimeError(f"{path}: controlled flow identity mismatch")
    if report["controlled_flow_passed"] is not True or report["final_text"] != "你好":
        raise RuntimeError(f"{path}: controlled Chinese commit did not pass")
    if not report["observed_composition_start"] or not report["observed_composition_update"]:
        raise RuntimeError(f"{path}: native composition callbacks were not observed")
    if report["platform"] == "windows":
        if report.get("observed_composition_commit") is not True:
            raise RuntimeError(f"{path}: Win32 IMM composition commit was not observed")
        lifecycle = report.get("lifecycle")
        if lifecycle != {"cycles": 100, "failures": 0}:
            raise RuntimeError(f"{path}: Windows lifecycle gate did not pass")
    else:
        if report.get("observed_composition_end") is not True:
            raise RuntimeError(f"{path}: browser compositionend was not observed")
        browser = report.get("browser")
        if (not isinstance(browser, str) or "Chrome/" not in browser
                or "HeadlessChrome/" in browser or "Edg/" in browser):
            raise RuntimeError(f"{path}: report is not from installed Chrome Stable")
    if not isinstance(report["digest"], str) or len(report["digest"]) != 32:
        raise RuntimeError(f"{path}: invalid Runtime digest")
    if len(report["selection"]) != 2 or len(report["caret"]) != 4:
        raise RuntimeError(f"{path}: invalid selection/caret geometry")
    print(f"PASS {report['platform']}: {path}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("reports", nargs="+", type=Path)
    args = parser.parse_args()
    for report in args.reports:
        validate(report)


if __name__ == "__main__":
    main()
