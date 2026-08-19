#!/usr/bin/env python3
"""Reject forbidden dependencies in RF-01 contracts and the normative public C ABI."""

from __future__ import annotations

import argparse
import pathlib
import re
import sys


FORBIDDEN_CONTRACT_PATTERNS = {
    "Skia": re.compile(r"(?:#\s*include\s*[<\"](?:include/)?(?:core|effects)/Sk|\bSk(?:Canvas|Paint|Rect|Path|Image|Surface|Data)\b|\bsksg::)"),
    "Windows": re.compile(r"(?:#\s*include\s*[<\"]windows\.h|\bHWND\b|\bID3D12)"),
    "Apple platform": re.compile(r"(?:#\s*include\s*[<\"](?:UIKit|AppKit|Metal)/|\bUIView\b|\bCAMetalLayer\b)"),
    "Android/JNI": re.compile(r"(?:#\s*include\s*[<\"]jni\.h|\bJNIEnv\b|\bANativeWindow\b)"),
    "Emscripten": re.compile(r"(?:#\s*include\s*[<\"]emscripten/|\bEM_JS\b|\bEM_ASM\b)"),
    "network/storage/thread": re.compile(r"(?:#\s*include\s*[<\"](?:sqlite3\.h|pthread\.h|thread|mutex|condition_variable|boost/|sys/socket\.h))"),
    "callback/reentrancy": re.compile(r"(?:std::function|\bCallback\b)"),
    "public C ABI": re.compile(r"docs/api/canvas_runtime_api_v1\.h"),
}

FORBIDDEN_C_ABI_PATTERNS = {
    "C++ standard library": re.compile(r"#\s*include\s*[<\"](?:vector|string|memory|span|optional|array)(?:\.h)?[>\"]"),
    "C++ linkage/type": re.compile(r"\b(?:namespace|class|template|std::)\b"),
    "Skia": FORBIDDEN_CONTRACT_PATTERNS["Skia"],
    "platform": re.compile(r"(?:windows\.h|UIKit|AppKit|jni\.h|emscripten/|HWND|JNIEnv)"),
}


def source_files(directory: pathlib.Path) -> list[pathlib.Path]:
    return sorted(
        path
        for path in directory.rglob("*")
        if path.is_file() and path.suffix in {".h", ".hpp", ".c", ".cc", ".cpp"}
    )


def check_files(
    paths: list[pathlib.Path], patterns: dict[str, re.Pattern[str]], root: pathlib.Path
) -> list[str]:
    failures: list[str] = []
    for path in paths:
        text = path.read_text(encoding="utf-8")
        for label, pattern in patterns.items():
            match = pattern.search(text)
            if match is None:
                continue
            line = text.count("\n", 0, match.start()) + 1
            failures.append(f"{path.relative_to(root)}:{line}: forbidden {label} dependency")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=pathlib.Path, required=True)
    arguments = parser.parse_args()
    root = arguments.root.resolve()

    contract_roots = [
        root / "runtime/foundation/include",
        root / "runtime/scene/include",
    ]
    contract_files = [path for directory in contract_roots for path in source_files(directory)]
    failures = check_files(contract_files, FORBIDDEN_CONTRACT_PATTERNS, root)

    public_header = root / "docs/api/canvas_runtime_api_v1.h"
    failures.extend(check_files([public_header], FORBIDDEN_C_ABI_PATTERNS, root))

    scene_cmake = (root / "runtime/scene/CMakeLists.txt").read_text(encoding="utf-8")
    forbidden_targets = re.compile(r"(?:Skia|canvas_poc|platform|JNI|UIKit|D3D|Metal|GLES)")
    if forbidden_targets.search(scene_cmake):
        failures.append("runtime/scene/CMakeLists.txt: forbidden renderer/platform target dependency")

    if failures:
        print("RF-01 module boundary check failed:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1

    print(f"RF-01 module boundary check passed ({len(contract_files)} contract files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
