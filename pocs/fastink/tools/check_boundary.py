#!/usr/bin/env python3
"""Keep the extractable Arc SDK independent from Axiom/POC implementation."""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
ARC = ROOT / "arc"
FORBIDDEN = (
    "canvas_poc02",
    "pocs/ink_engine",
    "SceneCompiler",
    "DirectComposition",
    "ANativeWindow",
    "CAMetalLayer",
)

failures = []
for path in sorted(ARC.rglob("*")):
    if not path.is_file() or path.suffix not in {".h", ".hpp", ".cpp", ".mm", ".inc"}:
        continue
    text = path.read_text(encoding="utf-8")
    for token in FORBIDDEN:
        if token in text:
            failures.append(f"{path.relative_to(ROOT)} contains forbidden token {token!r}")

if failures:
    print("\n".join(failures), file=sys.stderr)
    raise SystemExit(1)
print("Arc dependency boundary OK")
