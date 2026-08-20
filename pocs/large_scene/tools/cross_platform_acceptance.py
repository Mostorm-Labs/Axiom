#!/usr/bin/env python3
"""Compare POC-03 deterministic results and enforce platform result schemas."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys


def load(path: Path, expected_nodes: int = 100000) -> dict:
    raw = path.read_text(encoding="utf-8").strip()
    if not raw:
        raise ValueError(f"{path}: empty result")
    value = json.loads(raw)
    required = {
        "document_digest",
        "scene_digest",
        "ink_document_digest",
        "historical_strokes",
        "nodes",
        "maximum_candidates",
        "full_incremental_equivalent",
        "process_peak_mib",
    }
    missing = required.difference(value)
    if missing:
        raise ValueError(f"{path}: missing {sorted(missing)}")
    if value["document_digest"] != value["scene_digest"]:
        raise ValueError(f"{path}: document/scene digest mismatch")
    if value["nodes"] != expected_nodes:
        raise ValueError(f"{path}: expected {expected_nodes} nodes")
    if value["historical_strokes"] != expected_nodes // 5:
        raise ValueError(f"{path}: expected {expected_nodes // 5} canonical Strokes")
    if value["maximum_candidates"] > 5000:
        raise ValueError(f"{path}: viewport candidate gate exceeded")
    if not value["full_incremental_equivalent"]:
        raise ValueError(f"{path}: full/incremental equivalence failed")
    return value


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("results", nargs="+", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--nodes", type=int, default=100000)
    args = parser.parse_args()
    try:
        values = [load(path, args.nodes) for path in args.results]
        digests = {value["document_digest"] for value in values}
        if len(digests) != 1:
            raise ValueError(f"cross-platform digest mismatch: {sorted(digests)}")
        ink_digests = {value["ink_document_digest"] for value in values}
        if len(ink_digests) != 1:
            raise ValueError(
                f"cross-platform Ink digest mismatch: {sorted(ink_digests)}"
            )
        summary = {
            "schema_version": 1,
            "accepted": True,
            "document_digest": values[0]["document_digest"],
            "ink_document_digest": values[0]["ink_document_digest"],
            "platform_result_count": len(values),
        }
        text = json.dumps(summary, indent=2, sort_keys=True) + "\n"
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(text, encoding="utf-8")
        print(text, end="")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
