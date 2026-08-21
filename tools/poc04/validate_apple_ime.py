#!/usr/bin/env python3
"""Validate controlled iOS/iPadOS POC-04 IME evidence."""
from __future__ import annotations
import argparse, json, re
from pathlib import Path

def normalized_letters(value: str) -> str:
    return "".join(re.findall(r"[A-Za-z]", value)).lower()

def validate(path: Path) -> None:
    report = json.loads(path.read_text(encoding="utf-8"))
    required = {"schema_version", "platform", "protocol", "controlled_flow",
                "controlled_flow_passed", "final_text", "selection", "caret",
                "digest", "observed_marked_text", "observed_controlled_pinyin",
                "observed_commit", "events", "device", "build"}
    missing = sorted(required - report.keys())
    if missing: raise RuntimeError(f"{path}: missing fields: {', '.join(missing)}")
    if report["schema_version"] != 1 or report["platform"] not in {"ios", "ipados"}:
        raise RuntimeError(f"{path}: invalid Apple evidence identity")
    if report["protocol"] != "UITextInput+UIKeyInput" or report["controlled_flow"] != "ni hao -> 你好":
        raise RuntimeError(f"{path}: invalid protocol or controlled flow")
    if report["controlled_flow_passed"] is not True or report["final_text"] != "你好":
        raise RuntimeError(f"{path}: controlled Chinese commit did not pass")
    if report["presented_text"] != "你好" or report["marked_range"] != []:
        raise RuntimeError(f"{path}: final composition state is not committed")
    if not all(report[key] is True for key in ("observed_marked_text", "observed_controlled_pinyin", "observed_commit")):
        raise RuntimeError(f"{path}: required UIKit callbacks were not observed")
    events = report["events"]
    if not any(event.get("event") == "setMarkedText" for event in events):
        raise RuntimeError(f"{path}: setMarkedText is missing")
    if not any(event.get("event") == "unmarkText" for event in events):
        raise RuntimeError(f"{path}: unmarkText is missing")
    if not any(normalized_letters(event.get("presented_text", "")) == "nihao" for event in events):
        raise RuntimeError(f"{path}: exact ni hao marked-text sequence is missing")
    if len(report["selection"]) != 2 or len(report["caret"]) != 4 or not re.fullmatch(r"[0-9a-f]{32}", report["digest"]):
        raise RuntimeError(f"{path}: invalid selection/caret/digest")
    if report["build"].get("skia_profile") != "poc04-richtext-v2":
        raise RuntimeError(f"{path}: evidence is not bound to the v2 SDK")
    print(f"PASS {report['platform']}: {path}")

def main() -> None:
    parser = argparse.ArgumentParser(); parser.add_argument("reports", nargs="+", type=Path)
    for report in parser.parse_args().reports: validate(report)

if __name__ == "__main__": main()
