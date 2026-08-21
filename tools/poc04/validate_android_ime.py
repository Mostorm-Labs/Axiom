#!/usr/bin/env python3
"""Validate complementary Android Gboard and representative-device evidence."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path


SHA256 = re.compile(r"[0-9a-f]{64}")
COMMIT = re.compile(r"[0-9a-f]{40}")
DIGEST = re.compile(r"[0-9a-f]{32}")


def validate(path: Path) -> None:
    report = json.loads(path.read_text(encoding="utf-8"))
    required = {
        "schema_version", "platform", "evidence_model", "native_path",
        "source_reports", "physical_input", "canonical_runtime",
    }
    missing = sorted(required - report.keys())
    if missing:
        raise RuntimeError(f"{path}: missing fields: {', '.join(missing)}")
    if report["schema_version"] != 1 or report["platform"] != "android":
        raise RuntimeError(f"{path}: invalid Android evidence identity")
    if report["evidence_model"] != "complementary-physical-records":
        raise RuntimeError(f"{path}: physical input and canonical Runtime must remain distinct")
    if report["native_path"] != "NativeCanvasView/InputConnection/Gboard":
        raise RuntimeError(f"{path}: unexpected Android native input path")
    if len(report["source_reports"]) != 2:
        raise RuntimeError(f"{path}: both physical source reports are required")
    root = Path(__file__).resolve().parents[2]
    for source in report["source_reports"]:
        source_path = root / source.get("path", "")
        if not source_path.is_file() or not SHA256.fullmatch(source.get("sha256", "")):
            raise RuntimeError(f"{path}: invalid physical source report binding")
        actual = hashlib.sha256(source_path.read_bytes()).hexdigest()
        if actual != source["sha256"]:
            raise RuntimeError(f"{path}: physical source report checksum drift: {source_path}")

    physical = report["physical_input"]
    if not COMMIT.fullmatch(physical.get("source_commit", "")):
        raise RuntimeError(f"{path}: physical-input source commit is invalid")
    if physical.get("device") != "Google Pixel 7 (panther)" or physical.get("abi") != "arm64-v8a":
        raise RuntimeError(f"{path}: input evidence is not from the representative physical device")
    if physical.get("controlled_flow") != "physical taps: ni hao -> candidate 你好":
        raise RuntimeError(f"{path}: controlled Gboard flow is missing")
    for field in (
        "system_input_shown", "observed_start_input", "observed_start_input_view",
        "observed_chinese_processor",
    ):
        if physical.get(field) is not True:
            raise RuntimeError(f"{path}: missing physical Gboard observation: {field}")
    if physical.get("observed_candidate") != "你好":
        raise RuntimeError(f"{path}: Chinese candidate was not observed")
    if physical.get("observed_commit_text_utf16_length") != 2:
        raise RuntimeError(f"{path}: InputConnection commitText(length=2) was not observed")
    if physical.get("user_text_logged") is not False:
        raise RuntimeError(f"{path}: evidence must confirm user text was not logged")

    runtime = report["canonical_runtime"]
    if not COMMIT.fullmatch(runtime.get("source_commit", "")):
        raise RuntimeError(f"{path}: canonical Runtime source commit is invalid")
    if runtime.get("skia_profile") != "poc04-richtext-v2":
        raise RuntimeError(f"{path}: representative performance is not bound to the v2 SDK")
    if runtime.get("skia_target") != "android-arm64-v8a-gles3":
        raise RuntimeError(f"{path}: canonical Runtime target is invalid")
    if not SHA256.fullmatch(runtime.get("sdk_id", "")):
        raise RuntimeError(f"{path}: SDK identity is invalid")
    if not DIGEST.fullmatch(runtime.get("digest", "")):
        raise RuntimeError(f"{path}: Runtime digest is invalid")
    if runtime.get("lifecycle") != {"cycles": 100, "failures": 0}:
        raise RuntimeError(f"{path}: physical lifecycle gate failed")
    if runtime.get("layout_diagnostics") != []:
        raise RuntimeError(f"{path}: physical layout diagnostics are not empty")
    if runtime.get("input_caret_p95_ms", float("inf")) > 16.7:
        raise RuntimeError(f"{path}: physical input/caret p95 gate failed")
    if runtime.get("full_layout_p95_ms", float("inf")) > 33.3:
        raise RuntimeError(f"{path}: physical full-layout p95 gate failed")
    print(f"PASS android physical input + performance: {path}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("reports", nargs="+", type=Path)
    for report in parser.parse_args().reports:
        validate(report)


if __name__ == "__main__":
    main()
