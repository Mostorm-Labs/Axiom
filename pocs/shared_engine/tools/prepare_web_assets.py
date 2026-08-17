#!/usr/bin/env python3
"""Copy reviewed POC assets and CMake-produced WASM into Vite public/."""

from pathlib import Path
import os
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
    sdk_root = Path(os.environ.get(
        "CANVAS_SKIA_SDK_ROOT",
        REPO / ".deps/skia-sdk/web-wasm-webgl2",
    ))
    copy(sdk_root / "resources/fonts/Roboto-Regular.ttf",
         WEB / "fixtures" / "Roboto-Regular.ttf")
    copy(REPO / "out" / "web-release" / "pocs" / "shared_engine" / "platform" / "web" / "canvas_poc01_web.js",
         WEB / "wasm" / "canvas_poc01_web.js")
    copy(REPO / "out" / "web-release" / "pocs" / "shared_engine" / "platform" / "web" / "canvas_poc01_web.wasm",
         WEB / "wasm" / "canvas_poc01_web.wasm")
    copy(ROOT / "platform" / "web" / "static" / "canvas_poc_loader.js",
         WEB / "wasm" / "canvas_poc_loader.js")
    copy(ROOT / "goldens" / "reference.rgba", WEB / "goldens" / "reference.rgba")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
