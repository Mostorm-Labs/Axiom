#!/usr/bin/env python3
"""Reject Skia source-build knowledge in ordinary POC-02 consumer CI."""

from __future__ import annotations

import re
import sys
from pathlib import Path


PATTERNS = {
    "Skia source builder": r"build_skia\.py",
    "Skia source bootstrap": r"bootstrap_deps\.py[^\n]*(?:--skia|--sync-skia)",
    "Skia source output": r"(?:\.deps/)?skia/out",
    "Skia source checkout": (
        r"(?:\.deps|third_party)/skia(?:\.git)?(?:[/ ]|$)|"
        r"\bgit\s+clone[^\n]*\bskia(?:\.git)?(?:[/ ]|$)"
    ),
    "GN generation": r"\bgn\s+gen\b",
    "gclient sync": r"\bgclient\s+sync\b",
}

REQUIRED_FETCH_TARGETS = {
    "web-wasm-webgl2",
    "windows-x64-d3d12",
    "android-arm64-v8a-gles3",
    "android-x86_64-gles3",
}


def main() -> int:
    path = Path(sys.argv[1]) if len(sys.argv) == 2 else Path(
        ".github/workflows/poc02.yml"
    )
    text = path.read_text(encoding="utf-8")
    failures: list[str] = []
    for label, pattern in PATTERNS.items():
        if re.search(pattern, text, flags=re.IGNORECASE):
            failures.append(f"{label} is forbidden in {path}")
    for target in sorted(REQUIRED_FETCH_TARGETS):
        command = f"tools/skia/fetch.py --target {target}"
        if command not in text:
            failures.append(f"missing locked SDK fetch: {target}")
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print("POC-02 CI consumes only locked prebuilt Skia SDK targets")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
