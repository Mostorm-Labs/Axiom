#!/usr/bin/env python3
"""Keep the isolated POC-03 workflow a source-free Skia SDK consumer."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[3]
WORKFLOW = ROOT / ".github/workflows/poc03.yml"
FORBIDDEN = {
    r"bootstrap_deps\.py[^\n]*--skia": "Skia source bootstrap",
    r"sync-skia": "Skia dependency sync",
    r"\.deps/skia/out": "Skia source output cache",
    r"actions/cache": "worktree-crossing hidden cache",
    r"tools/build_skia\.py": "legacy Skia builder",
    r"tools/skia/build\.py": "SDK producer builder",
    r"pocs/shared_engine": "POC-01 implementation coupling",
}


def main() -> int:
    text = WORKFLOW.read_text(encoding="utf-8")
    violations = [
        label for pattern, label in FORBIDDEN.items() if re.search(pattern, text)
    ]
    if violations:
        raise RuntimeError("POC-03 CI violation: " + ", ".join(violations))
    for required in (
        "host-core:",
        "web:",
        "windows:",
        "android:",
        "cross-platform-acceptance:",
        "web-wasm-webgl2",
        "windows-x64-d3d12",
        "android-arm64-v8a-gles3",
        "android-x86_64-gles3",
        "pocs/large_scene/**",
        "pocs/ink_engine/**",
        "canvas_poc03_integrated_benchmark",
    ):
        if required not in text:
            raise RuntimeError(f"POC-03 workflow is missing {required}")
    print("POC-03 workflow is isolated and consumes source-free Skia SDKs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
