#!/usr/bin/env python3
"""Apply the POC-01 RGBA visual gate and emit expected/actual/diff PNGs."""

from __future__ import annotations

import argparse
import binascii
import json
from pathlib import Path
import struct
import zlib


WIDTH = 800
HEIGHT = 600
CHANNELS = 4


def png_chunk(name: bytes, payload: bytes) -> bytes:
    return struct.pack(">I", len(payload)) + name + payload + struct.pack(
        ">I", binascii.crc32(name + payload) & 0xFFFFFFFF
    )


def encode_png(rgba: bytes) -> bytes:
    rows = bytearray()
    stride = WIDTH * CHANNELS
    for y in range(HEIGHT):
        rows.append(0)
        rows.extend(rgba[y * stride : (y + 1) * stride])
    header = struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 6, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", header)
        + png_chunk(b"IDAT", zlib.compress(bytes(rows), 9))
        + png_chunk(b"IEND", b"")
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--expected", type=Path, required=True)
    parser.add_argument("--actual", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--backend", required=True)
    parser.add_argument("--skia-commit", required=True)
    parser.add_argument("--tolerance", type=int, default=2)
    parser.add_argument("--minimum-ratio", type=float, default=0.999)
    args = parser.parse_args()

    expected = args.expected.read_bytes()
    actual = args.actual.read_bytes()
    required = WIDTH * HEIGHT * CHANNELS
    if len(expected) != required or len(actual) != required:
        raise RuntimeError(f"expected two {required}-byte RGBA files")
    matching = 0
    max_delta = 0
    diff = bytearray(required)
    for pixel in range(WIDTH * HEIGHT):
        pixel_matches = True
        for channel in range(CHANNELS):
            offset = pixel * CHANNELS + channel
            delta = abs(expected[offset] - actual[offset])
            max_delta = max(max_delta, delta)
            pixel_matches = pixel_matches and delta <= args.tolerance
        if pixel_matches:
            matching += 1
            diff[pixel * 4 : pixel * 4 + 4] = bytes((0, 0, 0, 255))
        else:
            peak = max(
                abs(expected[pixel * 4 + channel] - actual[pixel * 4 + channel])
                for channel in range(4)
            )
            diff[pixel * 4 : pixel * 4 + 4] = bytes((255, 0, 255, max(64, peak)))
    ratio = matching / (WIDTH * HEIGHT)
    args.artifacts.mkdir(parents=True, exist_ok=True)
    (args.artifacts / "expected.png").write_bytes(encode_png(expected))
    (args.artifacts / "actual.png").write_bytes(encode_png(actual))
    (args.artifacts / "diff.png").write_bytes(encode_png(bytes(diff)))
    report = {
        "backend": args.backend,
        "skia_commit": args.skia_commit,
        "width": WIDTH,
        "height": HEIGHT,
        "tolerance": args.tolerance,
        "matching_pixels": matching,
        "total_pixels": WIDTH * HEIGHT,
        "matching_ratio": ratio,
        "maximum_channel_delta": max_delta,
        "passed": ratio >= args.minimum_ratio,
    }
    (args.artifacts / "metrics.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(report, sort_keys=True))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
