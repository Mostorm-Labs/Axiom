#!/usr/bin/env python3
"""Compare POC-04 platform behavior artifacts without normalizing differences."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


REQUIRED = {"platform", "digest", "behavior", "layout", "lifecycle", "performance"}
REQUIRED_BEHAVIOR = {
    "english", "simplified_chinese", "pinyin_composition", "newline",
    "mixed_runs", "selection", "caret", "clipboard", "undo", "redo",
}
REQUIRED_PERFORMANCE = {
    "input_caret_samples", "input_caret_warmup_samples",
    "input_caret_p50_ms", "input_caret_p95_ms", "input_caret_p99_ms",
    "input_caret_max_ms", "full_layout_samples",
    "full_layout_warmup_samples", "full_layout_p50_ms",
    "full_layout_p95_ms", "full_layout_p99_ms", "full_layout_max_ms",
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("artifacts", nargs="+", type=Path)
    args = parser.parse_args()
    if len(args.artifacts) < 3:
        raise RuntimeError("Web, Windows, and Android artifacts are required")
    records = []
    for path in args.artifacts:
        value = json.loads(path.read_text(encoding="utf-8"))
        if set(value) != REQUIRED:
            raise RuntimeError(f"{path}: unexpected behavior artifact schema")
        records.append(value)
    platforms = {value["platform"] for value in records}
    if platforms != {"web", "windows", "android"}:
        raise RuntimeError(f"platform matrix is incomplete: {sorted(platforms)}")
    for field in ("digest", "behavior", "layout"):
        # Compare the parsed JSON model, not serializer spelling. JSON numbers
        # 0 and 0.0 represent the same exact value, and JS deliberately emits
        # the former where the native writers emit the latter. No geometric
        # tolerance or rounding is applied here.
        expected = records[0][field]
        if any(value[field] != expected for value in records[1:]):
            raise RuntimeError(f"cross-platform {field} mismatch")
    for value in records:
        behavior = value["behavior"]
        lifecycle = value["lifecycle"]
        performance = value["performance"]
        if set(behavior) != REQUIRED_BEHAVIOR or not all(behavior.values()):
            raise RuntimeError(f"{value['platform']}: behavior matrix failed")
        layout = value["layout"]
        if set(layout) != {
            "height", "lines", "clusters", "selection", "diagnostics",
        } or not all(
            layout[field] for field in ("lines", "clusters", "selection")
        ):
            raise RuntimeError(f"{value['platform']}: canonical layout is incomplete")
        if layout["diagnostics"]:
            raise RuntimeError(f"{value['platform']}: canonical layout has diagnostics")
        if lifecycle.get("cycles") != 100 or lifecycle.get("failures") != 0:
            raise RuntimeError(f"{value['platform']}: lifecycle gate failed")
        if set(performance) != REQUIRED_PERFORMANCE:
            raise RuntimeError(
                f"{value['platform']}: performance schema is incomplete")
        for prefix, samples, warmup in (
            ("input_caret", performance["input_caret_samples"],
             performance["input_caret_warmup_samples"]),
            ("full_layout", performance["full_layout_samples"],
             performance["full_layout_warmup_samples"]),
        ):
            if not isinstance(samples, int) or samples <= 0:
                raise RuntimeError(f"{value['platform']}: {prefix} sample count invalid")
            if not isinstance(warmup, int) or warmup < 0:
                raise RuntimeError(f"{value['platform']}: {prefix} warmup count invalid")
            metrics = [
                performance[f"{prefix}_p50_ms"],
                performance[f"{prefix}_p95_ms"],
                performance[f"{prefix}_p99_ms"],
                performance[f"{prefix}_max_ms"],
            ]
            if any(not isinstance(metric, (int, float)) or metric < 0
                   for metric in metrics):
                raise RuntimeError(f"{value['platform']}: {prefix} metrics invalid")
            if metrics != sorted(metrics):
                raise RuntimeError(f"{value['platform']}: {prefix} metrics unordered")
        # The hosted Android x86_64 emulator owns correctness, schema and
        # lifecycle—not representative latency. Android's unchanged product
        # thresholds are enforced against the Pixel 7 physical record by
        # validate_android_ime.py in the aggregate acceptance job.
        if value["platform"] != "android":
            if performance.get("input_caret_p95_ms", 1e9) > 16.7:
                raise RuntimeError(f"{value['platform']}: input/caret p95 gate failed")
            if performance.get("full_layout_p95_ms", 1e9) > 33.3:
                raise RuntimeError(f"{value['platform']}: layout p95 gate failed")
    print(json.dumps({"accepted": True, "platforms": sorted(platforms)}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
