#!/usr/bin/env python3
"""Build the pinned Skia revision for a POC-01 backend using GN/Ninja."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import platform
import shlex
import subprocess


ROOT = Path(__file__).resolve().parents[1]
SKIA = ROOT / ".deps" / "skia"
LOCK = json.loads((ROOT / "deps.lock.json").read_text(encoding="utf-8"))


COMMON = {
    "is_official_build": "true",
    "is_component_build": "false",
    "skia_enable_ganesh": "true",
    "skia_enable_graphite": "false",
    "skia_enable_pdf": "false",
    "skia_enable_svg": "false",
    "skia_enable_fontmgr_custom_embedded": "true",
    "skia_use_dawn": "false",
    "skia_use_dng_sdk": "false",
    "skia_use_expat": "false",
    "skia_use_fontconfig": "false",
    "skia_use_harfbuzz": "false",
    "skia_use_icu": "false",
    "skia_use_libjpeg_turbo_decode": "false",
    "skia_use_libjpeg_turbo_encode": "false",
    "skia_use_libwebp_decode": "false",
    "skia_use_libwebp_encode": "false",
    "skia_use_piex": "false",
    "skia_use_partition_alloc": "false",
    "skia_use_wuffs": "false",
    "skia_use_system_freetype2": "false",
    "skia_use_system_libpng": "false",
    "skia_use_system_zlib": "false",
    "skia_use_libpng_decode": "true",
    "skia_use_libpng_encode": "true",
    "skia_use_freetype": "true",
}


def quoted(value: str) -> str:
    return json.dumps(value)


def target_args(target: str, args: argparse.Namespace) -> tuple[str, dict[str, str]]:
    values = dict(COMMON)
    if target == "windows":
        values.update(
            target_os=quoted("win"),
            target_cpu=quoted("x64"),
            cc=quoted(args.cc or "clang-cl"),
            cxx=quoted(args.cxx or "clang-cl"),
            skia_use_direct3d="true",
            skia_use_gl="false",
            skia_use_angle="false",
            skia_use_vulkan="false",
        )
    elif target == "web":
        values.update(
            target_os=quoted("wasm"),
            target_cpu=quoted("wasm"),
            cc=quoted(args.cc or "emcc"),
            cxx=quoted(args.cxx or "em++"),
            skia_use_webgl="true",
            skia_use_gl="true",
            skia_use_vulkan="false",
        )
    elif target in ("macos", "ios", "ios-simulator"):
        target_os = "mac" if target == "macos" else "ios"
        cpu = args.cpu or ("arm64" if platform.machine() == "arm64" else "x64")
        values.update(
            target_os=quoted(target_os),
            target_cpu=quoted(cpu),
            skia_use_metal="true",
            skia_use_gl="false",
            skia_use_vulkan="false",
        )
        if target != "macos":
            values["ios_min_target"] = quoted("17.0")
        if target == "ios-simulator":
            values["ios_use_simulator"] = "true"
    elif target == "android":
        ndk = args.ndk or os.environ.get("ANDROID_NDK_ROOT")
        if not ndk:
            raise RuntimeError("Android build requires --ndk or ANDROID_NDK_ROOT")
        android_cpu = args.cpu or "arm64"
        values.update(
            target_os=quoted("android"),
            target_cpu=quoted(android_cpu),
            ndk=quoted(str(Path(ndk).resolve())),
            ndk_api=str(LOCK["dependencies"]["android_ndk"]["api_level"]),
            skia_use_gl="true",
            skia_use_vulkan="false",
        )
        return f"poc01-android-{android_cpu}", values
    else:
        raise RuntimeError(f"unknown target: {target}")
    return f"poc01-{target}", values


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "target",
        choices=["windows", "web", "macos", "ios", "ios-simulator", "android"],
    )
    parser.add_argument("--cc")
    parser.add_argument("--cxx")
    parser.add_argument("--gn", help="Explicit GN binary for constrained development hosts")
    parser.add_argument("--cpu", choices=["x64", "arm64"])
    parser.add_argument("--ndk")
    parser.add_argument("--print-args", action="store_true")
    args = parser.parse_args()

    gn = Path(args.gn).resolve() if args.gn else SKIA / "bin" / "gn"
    if not gn.exists():
        raise RuntimeError(
            "missing GN binary; sync Skia dependencies or pass --gn"
        )
    expected = LOCK["dependencies"]["skia"]["commit"]
    marker = SKIA / ".canvas-poc-revision"
    if marker.exists():
        revision = marker.read_text(encoding="utf-8").strip()
    else:
        revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=SKIA, text=True
        ).strip()
    if revision != expected:
        raise RuntimeError(f"Skia revision mismatch: expected {expected}, got {revision}")

    output_name, values = target_args(args.target, args)
    gn_args = " ".join(f"{key}={value}" for key, value in sorted(values.items()))
    if args.print_args:
        print(gn_args)
        return 0
    output_dir = SKIA / "out" / output_name
    command = [str(gn), "gen", str(output_dir), f"--args={gn_args}"]
    print("+", " ".join(shlex.quote(part) for part in command), flush=True)
    subprocess.run(command, cwd=SKIA, check=True)
    subprocess.run(["ninja", "-C", str(output_dir), "skia"], cwd=SKIA, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
