#!/usr/bin/env python3
"""Validate real AppKit NSTextInputClient POC-04 evidence."""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


def validate(path: Path) -> None:
    report = json.loads(path.read_text(encoding="utf-8"))
    required = {
        "schema_version", "platform", "protocol", "evidence", "controlled_flow",
        "controlled_flow_passed", "final_text", "selection", "caret", "marked_range",
        "digest", "event_count", "observed_marked_text", "observed_commit", "events",
        "build",
    }
    missing = sorted(required - report.keys())
    if missing:
        raise RuntimeError(f"{path}: missing fields: {', '.join(missing)}")
    if report["schema_version"] != 1 or report["platform"] != "macos":
        raise RuntimeError(f"{path}: invalid macOS evidence identity")
    if report["protocol"] != "NSTextInputClient" or report["evidence"] != "controlled-system-input":
        raise RuntimeError(f"{path}: evidence is not a real AppKit input capture")
    if report["controlled_flow"] != "ni hao -> 你好" or report["controlled_flow_passed"] is not True:
        raise RuntimeError(f"{path}: controlled semantic flow failed")
    if report["final_text"] != "你好" or report["marked_range"] != []:
        raise RuntimeError(f"{path}: final composition is not committed")
    if report["observed_marked_text"] is not True or report["observed_commit"] is not True:
        raise RuntimeError(f"{path}: AppKit composition callbacks are missing")
    events = report["events"]
    if not any(item.get("event") == "setMarkedText" for item in events):
        raise RuntimeError(f"{path}: setMarkedText is missing")
    if not any(item.get("event") in {"insertText", "unmarkText"} for item in events):
        raise RuntimeError(f"{path}: commit callback is missing")
    if len(report["selection"]) != 2 or len(report["caret"]) != 4:
        raise RuntimeError(f"{path}: invalid selection/caret geometry")
    if not isinstance(report["digest"], str) or len(report["digest"]) != 32:
        raise RuntimeError(f"{path}: invalid Runtime digest")
    build = report["build"]
    if not re.fullmatch(r"[0-9a-f]{40}", build.get("source_commit", "")):
        raise RuntimeError(f"{path}: invalid source commit binding")
    if build.get("skia_profile") != "poc04-richtext-v2":
        raise RuntimeError(f"{path}: evidence is not bound to the v2 SDK")
    if build.get("app_bundle") != "dev.mostorm.canvas.poc04.macos-ime":
        raise RuntimeError(f"{path}: unexpected AppKit recorder bundle")
    print(f"PASS macos: {path}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("reports", nargs="+", type=Path)
    for report in parser.parse_args().reports:
        validate(report)


if __name__ == "__main__":
    main()
