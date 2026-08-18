#!/usr/bin/env python3
"""Validate a post-warm-up POC-01 memory time series."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any


MINIMUM_SAMPLES = 10
MINIMUM_SPAN_MS = 50_000
# POC-01 is a 60-second leak screen, not a product memory budget. A series
# fails when the stable tail is more than five percent above the stable head.
# Quartile-window medians reduce sensitivity to individual allocator samples.
MAX_TAIL_GROWTH_RATIO = 0.05


def _median(values: list[int]) -> float:
    ordered = sorted(values)
    middle = len(ordered) // 2
    if len(ordered) % 2:
        return float(ordered[middle])
    return (ordered[middle - 1] + ordered[middle]) / 2.0


def analyze(samples: list[dict[str, Any]]) -> dict[str, Any]:
    normalized: list[tuple[int, int]] = []
    for sample in samples:
        elapsed = sample.get("elapsed_ms")
        value = sample.get("bytes")
        if not isinstance(elapsed, int) or elapsed < 0:
            raise ValueError("each memory sample needs a non-negative integer elapsed_ms")
        if not isinstance(value, int) or value <= 0:
            raise ValueError("each memory sample needs a positive integer bytes value")
        normalized.append((elapsed, value))
    if normalized != sorted(normalized):
        raise ValueError("memory samples must be ordered by elapsed_ms")
    if len({elapsed for elapsed, _ in normalized}) != len(normalized):
        raise ValueError("memory sample elapsed_ms values must be unique")
    if len(normalized) < MINIMUM_SAMPLES:
        raise ValueError(f"at least {MINIMUM_SAMPLES} memory samples are required")
    span_ms = normalized[-1][0] - normalized[0][0]
    if span_ms < MINIMUM_SPAN_MS:
        raise ValueError(f"memory series must span at least {MINIMUM_SPAN_MS} ms")

    window = max(3, math.ceil(len(normalized) / 4))
    head = _median([value for _, value in normalized[:window]])
    tail = _median([value for _, value in normalized[-window:]])
    growth_bytes = tail - head
    growth_ratio = growth_bytes / head
    sustained_growth = growth_ratio > MAX_TAIL_GROWTH_RATIO
    return {
        "schema_version": 1,
        "sample_count": len(normalized),
        "span_ms": span_ms,
        "window_samples": window,
        "head_median_bytes": round(head),
        "tail_median_bytes": round(tail),
        "tail_growth_bytes": round(growth_bytes),
        "tail_growth_ratio": growth_ratio,
        "maximum_bytes": max(value for _, value in normalized),
        "minimum_bytes": min(value for _, value in normalized),
        "decision_rule": {
            "minimum_samples": MINIMUM_SAMPLES,
            "minimum_span_ms": MINIMUM_SPAN_MS,
            "maximum_tail_growth_ratio": MAX_TAIL_GROWTH_RATIO,
            "method": "compare medians of the first and last quartile windows",
        },
        "sustained_growth_observed": sustained_growth,
        "passed": not sustained_growth,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    source = json.loads(args.input.read_text(encoding="utf-8"))
    samples = source["memory_samples"] if isinstance(source, dict) else source
    report = analyze(samples)
    serialized = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(serialized, encoding="utf-8")
    print(serialized, end="")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
