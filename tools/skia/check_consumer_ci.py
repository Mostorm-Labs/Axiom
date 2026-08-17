#!/usr/bin/env python3
"""Ensure ordinary POC-01 CI never builds or checks out Skia source."""

from __future__ import annotations

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github/workflows/poc01.yml"
FORBIDDEN = {
    r"bootstrap_deps\.py[^\n]*--skia": "Skia source bootstrap",
    r"sync-skia": "Skia dependency sync",
    r"\.deps/skia/out": "Skia source output cache",
    r"tools/build_skia\.py": "legacy Skia builder",
    r"tools/skia/build\.py": "Skia SDK producer builder",
}


def main() -> int:
    text = WORKFLOW.read_text(encoding="utf-8")
    violations = [label for pattern, label in FORBIDDEN.items() if re.search(pattern, text)]
    if violations:
        raise RuntimeError(
            "ordinary POC-01 CI contains forbidden producer behavior: "
            + ", ".join(violations)
        )
    required_targets = {
        "windows-x64-d3d12", "web-wasm-webgl2", "macos-arm64-metal",
        "ios-arm64-metal", "ios-simulator-arm64-metal",
        "android-arm64-v8a-gles3", "android-x86_64-gles3",
    }
    missing = sorted(target for target in required_targets if target not in text)
    if missing:
        raise RuntimeError(f"ordinary POC-01 CI does not fetch SDK targets: {missing}")
    print("POC-01 workflow is a source-free Skia SDK consumer")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
