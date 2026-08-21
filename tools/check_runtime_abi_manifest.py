#!/usr/bin/env python3
"""Verify that RF-01 did not change the normative Runtime C ABI baseline."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import sys


FUNCTION_PATTERN = re.compile(
    r"CANVAS_RUNTIME_API\s+[\s\S]*?\b(canvas_[a-z0-9_]+)\s*\("
)
STRUCT_PATTERN = re.compile(r"typedef struct\s+(Canvas\w+)")
ENUM_PATTERN = re.compile(r"^\s+(kCanvas\w+)\s*=\s*([^,\n}]+)", re.MULTILINE)
ABI_VERSION_PATTERN = re.compile(r"#define\s+CANVAS_RUNTIME_ABI_VERSION\s+(\d+)u")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=pathlib.Path, required=True)
    arguments = parser.parse_args()
    root = arguments.root.resolve()
    header = root / "docs/api/canvas_runtime_api_v1.h"
    manifest_path = root / "docs/api/canvas_runtime_api_v1.manifest.json"

    header_bytes = header.read_bytes()
    # Git may materialize the same tracked header with CRLF on Windows. The
    # ABI manifest describes the normalized source contract, not checkout
    # policy, so hash canonical LF bytes on every host.
    normalized_header_bytes = header_bytes.replace(b"\r\n", b"\n")
    header_text = normalized_header_bytes.decode("utf-8")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

    abi_match = ABI_VERSION_PATTERN.search(header_text)
    if abi_match is None:
        print("Runtime ABI version macro is missing", file=sys.stderr)
        return 1

    actual = {
        "schema_version": 1,
        "abi_version": int(abi_match.group(1)),
        "header_sha256": hashlib.sha256(normalized_header_bytes).hexdigest(),
        "exported_function_count": len(FUNCTION_PATTERN.findall(header_text)),
        "struct_declaration_count": len(STRUCT_PATTERN.findall(header_text)),
        "enum_constant_count": len(ENUM_PATTERN.findall(header_text)),
        "exported_functions": FUNCTION_PATTERN.findall(header_text),
    }
    if actual != manifest:
        print("Runtime C ABI manifest mismatch", file=sys.stderr)
        for key, value in actual.items():
            if manifest.get(key) != value:
                print(f"- {key}: expected {manifest.get(key)!r}, actual {value!r}", file=sys.stderr)
        print(
            "Update the ABI contract through an accepted ABI change; RF-01 must not rewrite the baseline.",
            file=sys.stderr,
        )
        return 1

    print(
        "Runtime C ABI manifest passed "
        f"({actual['exported_function_count']} functions, "
        f"{actual['struct_declaration_count']} structs, "
        f"{actual['enum_constant_count']} enum constants)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
