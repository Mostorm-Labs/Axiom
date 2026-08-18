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
        values = {json.dumps(value[field], sort_keys=True) for value in records}
        if len(values) != 1:
            raise RuntimeError(f"cross-platform {field} mismatch")
    for value in records:
        behavior = value["behavior"]
        lifecycle = value["lifecycle"]
        performance = value["performance"]
        if set(behavior) != REQUIRED_BEHAVIOR or not all(behavior.values()):
            raise RuntimeError(f"{value['platform']}: behavior matrix failed")
        layout = value["layout"]
        if set(layout) != {"height", "lines", "clusters", "selection"} or not all(
            layout[field] for field in ("lines", "clusters", "selection")
        ):
            raise RuntimeError(f"{value['platform']}: canonical layout is incomplete")
        if lifecycle.get("cycles") != 100 or lifecycle.get("failures") != 0:
            raise RuntimeError(f"{value['platform']}: lifecycle gate failed")
        if performance.get("input_caret_p95_ms", 1e9) > 16.7:
            raise RuntimeError(f"{value['platform']}: input/caret p95 gate failed")
        if performance.get("full_layout_p95_ms", 1e9) > 33.3:
            raise RuntimeError(f"{value['platform']}: layout p95 gate failed")
    print(json.dumps({"accepted": True, "platforms": sorted(platforms)}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
