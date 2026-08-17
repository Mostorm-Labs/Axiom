#!/usr/bin/env python3
"""Validate repository-local Markdown links, fences, and POC-01 terminology."""

from __future__ import annotations

from pathlib import Path
import re
import sys
from urllib.parse import unquote


ROOT = Path(__file__).resolve().parents[1]
LINK = re.compile(r"(?<!!)\[[^\]]*\]\(([^)]+)\)")


def main() -> int:
    failures: list[str] = []
    markdown = sorted(
        path for path in ROOT.rglob("*.md")
        if ".deps" not in path.parts and "node_modules" not in path.parts
    )
    for path in markdown:
        text = path.read_text(encoding="utf-8")
        if text.count("```") % 2:
            failures.append(f"{path.relative_to(ROOT)}: unbalanced code fence")
        for match in LINK.finditer(text):
            raw = match.group(1).strip()
            if raw.startswith("<") and raw.endswith(">"):
                raw = raw[1:-1]
            target = raw.split("#", 1)[0]
            if not target or "://" in target or target.startswith(("mailto:", "#")):
                continue
            resolved = (path.parent / unquote(target)).resolve()
            if not resolved.exists():
                failures.append(
                    f"{path.relative_to(ROOT)}: missing local link {target}"
                )

    required = (
        "Visual Document Runtime",
        "SceneCompiler",
        "Skia Ganesh",
        "WebGL2",
        "D3D12",
        "Ganesh/Metal",
        "Native `CanvasView` + JNI",
        "47826449b895ac4f4a57b4f386379775",
    )
    combined = "\n".join(path.read_text(encoding="utf-8") for path in markdown)
    for term in required:
        if term not in combined:
            failures.append(f"missing required documentation term: {term}")
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(f"validated {len(markdown)} Markdown files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
