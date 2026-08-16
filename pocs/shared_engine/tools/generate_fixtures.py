#!/usr/bin/env python3
"""Generate the fixed 64x64 RGBA checker PNG used by POC-01."""

from __future__ import annotations

import binascii
from pathlib import Path
import struct
import zlib


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "fixtures" / "checker.png"
WIDTH = 64
HEIGHT = 64
FIRST = (32, 95, 210, 255)
SECOND = (245, 178, 45, 255)


def chunk(name: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + name
        + payload
        + struct.pack(">I", binascii.crc32(name + payload) & 0xFFFFFFFF)
    )


def png_bytes() -> bytes:
    scanlines = bytearray()
    for y in range(HEIGHT):
        scanlines.append(0)
        for x in range(WIDTH):
            color = FIRST if (x // 8 + y // 8) % 2 == 0 else SECOND
            scanlines.extend(color)
    header = struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 6, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", header)
        + chunk(b"IDAT", zlib.compress(bytes(scanlines), level=9))
        + chunk(b"IEND", b"")
    )


def main() -> int:
    data = png_bytes()
    if OUTPUT.exists() and OUTPUT.read_bytes() == data:
        print(f"unchanged {OUTPUT}")
        return 0
    OUTPUT.write_bytes(data)
    print(f"generated {OUTPUT} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
