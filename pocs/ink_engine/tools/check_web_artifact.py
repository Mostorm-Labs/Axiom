#!/usr/bin/env python3
"""Prove POC-02 Web stays single-threaded and needs no cross-origin isolation."""

from pathlib import Path
import sys


FORBIDDEN = (b"SharedArrayBuffer", b"pthread", b"PThread", b"COOP", b"COEP")


def main() -> int:
    if len(sys.argv) < 2:
        raise SystemExit("usage: check_web_artifact.py FILE [FILE ...]")
    failures: list[str] = []
    for value in sys.argv[1:]:
        path = Path(value)
        data = path.read_bytes()
        for needle in FORBIDDEN:
            if needle in data:
                failures.append(f"{path}: contains {needle.decode()}")
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print("POC-02 Web artifact is single-threaded and isolation-free")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
