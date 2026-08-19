#!/usr/bin/env python3
"""Copy the built POC-03 WASM module into ignored Vite public assets."""

from pathlib import Path
import shutil


ROOT = Path(__file__).resolve().parents[3]
BUILD = ROOT / "out" / "poc03-web-release" / "pocs" / "large_scene" / "platform" / "skia"
WEB = ROOT / "pocs" / "large_scene" / "platform" / "web"
PUBLIC = WEB / "public" / "wasm"


def main() -> int:
    PUBLIC.mkdir(parents=True, exist_ok=True)
    for name in ("canvas_poc03_web_probe.js", "canvas_poc03_web_probe.wasm"):
        source = BUILD / name
        if not source.is_file():
            raise RuntimeError(
                f"missing {source}; build the poc03-web-release preset first"
            )
        shutil.copy2(source, PUBLIC / name)
    shutil.copy2(WEB / "static" / "canvas_poc03_loader.js",
                 PUBLIC / "canvas_poc03_loader.js")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
