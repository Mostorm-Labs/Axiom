#!/usr/bin/env python3
"""Assert the POC-03 WASM consumer retains the single-thread POC contract."""

from pathlib import Path
import sys


FORBIDDEN = (b"SharedArrayBuffer", b"pthread", b"PThread", b"COOP", b"COEP")


def main() -> int:
    failures: list[str] = []
    for value in sys.argv[1:]:
        path = Path(value)
        data = path.read_bytes()
        for needle in FORBIDDEN:
            if needle in data:
                failures.append(f"{path}: contains {needle.decode()}")
    if not sys.argv[1:]:
        failures.append("usage: check_web_artifact.py FILE [FILE ...]")
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print("POC-03 Web artifact is single-threaded and isolation-free")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
