#!/usr/bin/env python3
"""Classify R1 Skia changes into producer scope or consumer-only validation.

This deliberately keeps the expensive producer trigger conservative: anything
that can change a packaged archive or its build identity selects the complete
matrix.  Consumer/download/lock changes never select a Skia source build.
"""

from __future__ import annotations

import argparse
import json
from pathlib import PurePosixPath
import sys


ALL_TARGETS = [
    "windows-x64-d3d12",
    "web-wasm-webgl2",
    "macos-arm64-metal",
    "macos-x64-metal",
    "ios-arm64-metal",
    "ios-simulator-arm64-metal",
    "android-arm64-v8a-gles3",
    "android-x86_64-gles3",
]
PLATFORM_TARGETS = {
    "windows": ["windows-x64-d3d12"],
    "web": ["web-wasm-webgl2"],
    "apple": [
        "macos-arm64-metal", "macos-x64-metal", "ios-arm64-metal",
        "ios-simulator-arm64-metal",
    ],
    "android": ["android-arm64-v8a-gles3", "android-x86_64-gles3"],
}
TARGET_MATRIX = {
    "windows-x64-d3d12": {
        "os": "windows-2025", "family": "windows",
        "toolchain": "windows-2025-llvm-22.1.8",
    },
    "web-wasm-webgl2": {
        "os": "ubuntu-24.04", "family": "web",
        "toolchain": "ubuntu-24.04-emscripten-6.0.6",
    },
    "macos-arm64-metal": {
        "os": "macos-15", "family": "apple", "toolchain": "macos-15-xcode",
    },
    "macos-x64-metal": {
        "os": "macos-15", "family": "apple", "toolchain": "macos-15-xcode",
    },
    "ios-arm64-metal": {
        "os": "macos-15", "family": "apple", "toolchain": "macos-15-xcode",
    },
    "ios-simulator-arm64-metal": {
        "os": "macos-15", "family": "apple", "toolchain": "macos-15-xcode",
    },
    "android-arm64-v8a-gles3": {
        "os": "ubuntu-24.04", "family": "android",
        "toolchain": "ubuntu-24.04-ndk-27.2.12479018",
    },
    "android-x86_64-gles3": {
        "os": "ubuntu-24.04", "family": "android",
        "toolchain": "ubuntu-24.04-ndk-27.2.12479018",
    },
}


def classify(paths: list[str]) -> dict[str, object]:
    normalized = [PurePosixPath(path).as_posix() for path in paths]
    full_exact = {
        "deps.lock.json",
        "tools/skia/profiles/r1-full-v1.json",
        "tools/skia/sdk.py",
        "tools/skia/build.py",
        "tools/skia/package.py",
        "tools/skia/identify_toolchain.py",
        "tools/skia/aggregate.py",
        "tools/skia/classify_r1_changes.py",
        "tools/skia/publish_release.py",
        ".github/workflows/skia-sdk-r1-full-producer.yml",
    }
    if any(path in full_exact for path in normalized):
        return {"mode": "full", "targets": ALL_TARGETS, "reason": "SDK identity or recipe"}

    platform_targets: set[str] = set()
    for path in normalized:
        for family, targets in PLATFORM_TARGETS.items():
            if path.startswith(f"tools/skia/platform/{family}/"):
                platform_targets.update(targets)
    if platform_targets:
        return {
            "mode": "platform",
            "targets": [target for target in ALL_TARGETS if target in platform_targets],
            "reason": "platform-specific producer source",
        }

    consumer_prefixes = (
        "tools/skia/fetch.py",
        "tools/skia/consumer.py",
        "tools/skia/update_lock.py",
        "tools/skia/reuse_artifact.py",
        "tools/skia/verify.py",
        "tools/skia/check_consumer_ci.py",
        "tools/skia/cmake/",
        "tools/skia/cmake_probe/",
        "tools/skia/tests/",
        "r1-full-skia-sdk.lock.json",
        ".github/workflows/r1-full-consumer-validation.yml",
        ".github/workflows/r1-full-producer-contract.yml",
    )
    if any(path == prefix or path.startswith(prefix) for path in normalized for prefix in consumer_prefixes):
        return {"mode": "consumer", "targets": [], "reason": "consumer or schema validation"}

    return {"mode": "none", "targets": [], "reason": "documentation or unrelated change"}


def build_matrix(targets: list[str]) -> dict[str, list[dict[str, str]]]:
    unknown = sorted(set(targets) - set(TARGET_MATRIX))
    if unknown:
        raise ValueError(f"unknown R1 Skia target(s): {', '.join(unknown)}")
    include = []
    for target in ALL_TARGETS:
        if target not in targets:
            continue
        for variant in ("release", "debug", "asan"):
            include.append({
                "target": target, "variant": variant, **TARGET_MATRIX[target],
            })
    if not include:
        raise ValueError("R1 Skia producer matrix cannot be empty")
    return {"include": include}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", nargs="*")
    parser.add_argument("--matrix", help="emit a target × variant matrix for this JSON array")
    args = parser.parse_args()
    if args.matrix is not None:
        targets = json.loads(args.matrix)
        if not isinstance(targets, list) or any(not isinstance(item, str) for item in targets):
            parser.error("--matrix must be a JSON string array")
        print(json.dumps(build_matrix(targets), separators=(",", ":")))
        return 0
    paths = args.paths
    # The PR workflow pipes `git diff --name-only` into this tool. Keep the
    # positional form useful for local checks, but make the pipeline form
    # equivalent instead of silently classifying an empty change set.
    if not paths and not sys.stdin.isatty():
        paths = [line.strip() for line in sys.stdin if line.strip()]
    print(json.dumps(classify(paths), separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
