#!/usr/bin/env python3
"""Record the target toolchain identity without embedding installation paths."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess

from sdk import DEFAULT_PROFILE, load_profile, target_definition, validate_toolchain


def output(*command: str) -> str:
    return subprocess.check_output(command, text=True, stderr=subprocess.STDOUT).strip()


def require_contains(actual: str, expected: str, label: str) -> None:
    if expected not in actual:
        raise RuntimeError(f"{label} mismatch: expected {expected!r} in {actual!r}")


def android_ndk_version(ndk: Path) -> str:
    properties = (ndk / "source.properties").read_text(encoding="utf-8")
    match = re.search(r"^Pkg\.Revision\s*=\s*(.+)$", properties, re.MULTILINE)
    if not match:
        raise RuntimeError("Android NDK source.properties has no Pkg.Revision")
    return match.group(1).strip()


def identify(target_name: str, profile: dict, ndk: str | None) -> dict:
    target = target_definition(profile, target_name)
    expected = target["toolchain"]
    platform = target["platform"]
    if platform == "web":
        version = output("emcc", "--version")
        require_contains(version, expected["emscripten"], "Emscripten")
        identity = {
            "emscripten": expected["emscripten"],
            "llvm": expected["llvm"],
            "pthread": False,
        }
    elif platform == "windows":
        version = output("clang-cl", "--version")
        require_contains(version, expected["llvm"], "Windows LLVM")
        msvc = os.environ.get("VCToolsVersion", "").rstrip("\\/")
        windows_sdk = os.environ.get("WindowsSDKVersion", "").rstrip("\\/")
        if not msvc or not windows_sdk:
            raise RuntimeError("MSVC developer environment did not expose toolset/SDK versions")
        identity = {
            "llvm": expected["llvm"],
            "msvc_toolset": msvc,
            "windows_sdk": windows_sdk,
        }
    elif platform in ("macos", "ios", "ios-simulator"):
        sdk = expected["sdk"]
        xcode = "; ".join(output("xcodebuild", "-version").splitlines())
        identity = {
            "xcode": xcode,
            "sdk": sdk,
            "sdk_version": output("xcrun", "--sdk", sdk, "--show-sdk-version"),
        }
        if "deployment_target" in expected:
            identity["deployment_target"] = expected["deployment_target"]
    elif platform == "android":
        ndk_path = Path(ndk or os.environ.get("ANDROID_NDK_ROOT", ""))
        if not ndk_path.is_dir():
            raise RuntimeError("Android toolchain identity requires --ndk or ANDROID_NDK_ROOT")
        identity = {
            "android_ndk": android_ndk_version(ndk_path),
            "api_level": expected["api_level"],
        }
    else:
        raise RuntimeError(f"unsupported target platform: {platform}")
    validate_toolchain(profile, target_name, identity)
    return identity


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--target", required=True)
    parser.add_argument("--profile", type=Path, default=DEFAULT_PROFILE)
    parser.add_argument("--ndk", default=os.environ.get("ANDROID_NDK_ROOT"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    profile = load_profile(args.profile.resolve())
    identity = identify(args.target, profile, args.ndk)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(args.output.suffix + ".tmp")
    temporary.write_text(
        json.dumps(identity, indent=2, sort_keys=True) + "\n", encoding="utf-8",
    )
    temporary.replace(args.output)
    print(json.dumps(identity, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
