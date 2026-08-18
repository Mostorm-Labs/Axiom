#!/usr/bin/env python3
"""Copy reviewed POC-02 WASM and replay fixtures into ignored web assets."""

from pathlib import Path
import shutil


ROOT = Path(__file__).resolve().parents[3]
POC = ROOT / "pocs" / "ink_engine"
BUILD = ROOT / "out" / "poc02-web-release" / "pocs" / "ink_engine" / "platform" / "web"
PUBLIC = POC / "playground" / "web" / "public"


def copy(source: Path, destination: Path) -> None:
    if not source.is_file():
        raise RuntimeError(f"missing {source}; build the poc02-web-release preset first")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def main() -> int:
    copy(BUILD / "canvas_poc02_web.js", PUBLIC / "wasm" / "canvas_poc02_web.js")
    copy(BUILD / "canvas_poc02_web.wasm", PUBLIC / "wasm" / "canvas_poc02_web.wasm")
    copy(POC / "playground" / "web" / "static" / "canvas_poc02_loader.js",
         PUBLIC / "wasm" / "canvas_poc02_loader.js")
    for name in ("vector-pressure.ndjson", "dab-turn.ndjson"):
        copy(POC / "fixtures" / name, PUBLIC / "fixtures" / name)
    for name in ("vector-reference.rgba", "dab-reference.rgba"):
        copy(POC / "goldens" / name, PUBLIC / "goldens" / name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
