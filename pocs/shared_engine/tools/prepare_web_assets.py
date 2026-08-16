#!/usr/bin/env python3
"""Copy reviewed POC assets and CMake-produced WASM into Vite public/."""

from pathlib import Path
import shutil


ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "platform" / "web" / "public"
REPO = ROOT.parents[1]


def copy(source: Path, destination: Path) -> None:
    if not source.exists():
        raise RuntimeError(f"missing required Web asset: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def main() -> int:
    copy(ROOT / "fixtures" / "checker.png", WEB / "fixtures" / "checker.png")
    copy(ROOT / "fixtures" / "scene.ndjson", WEB / "fixtures" / "scene.ndjson")
    copy(REPO / ".deps" / "skia" / "resources" / "fonts" / "Roboto-Regular.ttf",
         WEB / "fixtures" / "Roboto-Regular.ttf")
    copy(REPO / "out" / "web-release" / "pocs" / "shared_engine" / "platform" / "web" / "canvas_poc01_web.js",
         WEB / "wasm" / "canvas_poc01_web.js")
    copy(REPO / "out" / "web-release" / "pocs" / "shared_engine" / "platform" / "web" / "canvas_poc01_web.wasm",
         WEB / "wasm" / "canvas_poc01_web.wasm")
    copy(ROOT / "goldens" / "reference.rgba", WEB / "goldens" / "reference.rgba")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
