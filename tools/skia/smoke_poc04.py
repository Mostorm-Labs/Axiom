#!/usr/bin/env python3
"""Prove a POC-04 RichText SDK can build Canvas without the Skia checkout."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess

from sdk import ROOT, SKIA_ROOT, load_profile, target_definition
from verify import verify_archive


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--target", required=True)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--cc")
    parser.add_argument("--cxx")
    parser.add_argument("--ndk", default=os.environ.get("ANDROID_NDK_ROOT"))
    args = parser.parse_args()
    profile_path = args.profile.resolve()
    target = target_definition(load_profile(profile_path), args.target)
    work = ROOT / "out/skia-sdk-poc04-smoke" / args.target
    sdk = work / "sdk"
    build = work / "build"
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    verified = verify_archive(args.archive.resolve(), profile_path, args.target, sdk)
    command = [
        "cmake", "-S", str(ROOT / "pocs/rich_text"), "-B", str(build), "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release", "-DCANVAS_POC04_BUILD_TESTS=OFF",
        "-DCANVAS_POC04_ENABLE_SKPARAGRAPH=ON",
        f"-DCANVAS_POC04_SKIA_SDK_ROOT={sdk}",
        f"-DCANVAS_POC04_SKIA_EXPECTED_ID={verified['sdk_id']}",
    ]
    platform = target["platform"]
    if platform == "web":
        command += [
            f"-DCMAKE_TOOLCHAIN_FILE={ROOT / '.deps/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake'}",
            "-DCANVAS_POC04_BUILD_WEB=ON",
        ]
        build_target = "canvas_poc04_web"
    elif platform == "windows":
        command += ["-DCANVAS_POC04_BUILD_WINDOWS=ON"]
        if args.cc:
            command.append(f"-DCMAKE_C_COMPILER={Path(args.cc).resolve()}")
        if args.cxx:
            command.append(f"-DCMAKE_CXX_COMPILER={Path(args.cxx).resolve()}")
        build_target = "canvas_poc04_canonical_behavior_report"
    elif platform == "android":
        if not args.ndk:
            raise RuntimeError("Android smoke requires --ndk")
        command += [
            f"-DCMAKE_TOOLCHAIN_FILE={Path(args.ndk) / 'build/cmake/android.toolchain.cmake'}",
            f"-DANDROID_ABI={target['arch']}", "-DANDROID_PLATFORM=android-26",
            "-DANDROID_STL=c++_static", "-DCANVAS_POC04_BUILD_ANDROID=ON",
        ]
        build_target = "canvas_poc04_android"
    else:
        raise RuntimeError(f"unsupported POC-04 platform: {platform}")
    hidden = ROOT / ".deps/skia-source-disabled"
    if hidden.exists() or not SKIA_ROOT.exists():
        raise RuntimeError("producer source checkout cannot be safely isolated")
    SKIA_ROOT.rename(hidden)
    try:
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run(
            ["cmake", "--build", str(build), "--target", build_target, "--parallel"],
            cwd=ROOT, check=True,
        )
        if platform == "windows":
            subprocess.run([
                str(build / "canvas_poc04_canonical_behavior_report.exe"),
                "--platform=windows-producer-smoke",
                f"--output={build / 'windows-producer-smoke.json'}",
            ], cwd=build, check=True)
        elif platform == "web":
            javascript = build / "platform/web/canvas_poc04_web.js"
            wasm = build / "platform/web/canvas_poc04_web.wasm"
            if not javascript.is_file() or not wasm.is_file():
                raise RuntimeError("Web source-free smoke did not produce JS and WASM")
        elif platform == "android":
            library = build / "platform/android/libcanvas_poc04_android.so"
            if not library.is_file():
                raise RuntimeError("Android source-free smoke did not produce JNI library")
    finally:
        hidden.rename(SKIA_ROOT)
    print(f"source-free POC-04 build passed: {args.target}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
