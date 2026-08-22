#!/usr/bin/env python3
"""Historical helper for reproducing a POC-04 RichText SDK source-free build.

Active RichText development consumes the R1 Full SDK through the normal
CanvasSkia package. This helper remains only so the immutable POC-04 release
and its acceptance evidence can be reproduced.
"""

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
        "cmake", "-S", str(ROOT / "pocs/rich_text"), "-B", str(build),
        "-DCMAKE_BUILD_TYPE=Release", "-DCANVAS_POC04_BUILD_TESTS=OFF",
        "-DCANVAS_POC04_ENABLE_SKPARAGRAPH=ON",
        f"-DCANVAS_POC04_SKIA_SDK_ROOT={sdk}",
        f"-DCANVAS_POC04_SKIA_EXPECTED_ID={verified['sdk_id']}",
        f"-DCANVAS_POC04_SKIA_PROFILE={profile_path}",
    ]
    platform = target["platform"]
    if platform == "web":
        command += [
            "-G", "Ninja",
            f"-DCMAKE_TOOLCHAIN_FILE={ROOT / '.deps/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake'}",
            "-DCANVAS_POC04_BUILD_WEB=ON",
        ]
        build_target = "canvas_poc04_web"
    elif platform == "windows":
        command += ["-G", "Ninja", "-DCANVAS_POC04_BUILD_WINDOWS=ON"]
        if args.cc:
            command.append(f"-DCMAKE_C_COMPILER={Path(args.cc).resolve()}")
        if args.cxx:
            command.append(f"-DCMAKE_CXX_COMPILER={Path(args.cxx).resolve()}")
        build_target = "canvas_poc04_canonical_behavior_report"
    elif platform == "android":
        if not args.ndk:
            raise RuntimeError("Android smoke requires --ndk")
        command += [
            "-G", "Ninja",
            f"-DCMAKE_TOOLCHAIN_FILE={Path(args.ndk) / 'build/cmake/android.toolchain.cmake'}",
            f"-DANDROID_ABI={target['arch']}", "-DANDROID_PLATFORM=android-26",
            "-DANDROID_STL=c++_static", "-DCANVAS_POC04_BUILD_ANDROID=ON",
        ]
        build_target = "canvas_poc04_android"
    elif platform == "macos":
        command += ["-G", "Ninja", "-DCMAKE_OSX_ARCHITECTURES=arm64"]
        build_target = "canvas_poc04_canonical_behavior_report"
    elif platform in ("ios", "ios-simulator"):
        toolchain = (
            "ios.toolchain.cmake"
            if platform == "ios"
            else "ios-simulator.toolchain.cmake"
        )
        command += [
            "-G", "Xcode",
            f"-DCMAKE_TOOLCHAIN_FILE={ROOT / 'cmake' / toolchain}",
            "-DCMAKE_OSX_ARCHITECTURES=arm64",
            "-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO",
        ]
        build_target = "canvas_poc04_canonical_behavior_report"
    else:
        raise RuntimeError(f"unsupported POC-04 platform: {platform}")
    hidden = ROOT / ".deps/skia-source-disabled"
    if hidden.exists() or not SKIA_ROOT.exists():
        raise RuntimeError("producer source checkout cannot be safely isolated")
    SKIA_ROOT.rename(hidden)
    try:
        subprocess.run(command, cwd=ROOT, check=True)
        build_command = [
            "cmake", "--build", str(build), "--target", build_target, "--parallel",
        ]
        if platform in ("ios", "ios-simulator"):
            build_command += ["--config", "Release"]
        subprocess.run(build_command, cwd=ROOT, check=True)
        if platform == "windows":
            report = build / "windows-producer-smoke.json"
            completed = subprocess.run([
                str(build / "canvas_poc04_canonical_behavior_report.exe"),
                "--platform=windows-producer-smoke",
                f"--output={report}",
            ], cwd=build, check=False)
            if report.is_file():
                print(report.read_text(encoding="utf-8"), end="")
            if completed.returncode != 0:
                raise RuntimeError(
                    "Windows canonical behavior recorder rejected the source-free SDK "
                    f"with exit code {completed.returncode}"
                )
        elif platform == "web":
            javascript = build / "platform/web/canvas_poc04_web.js"
            wasm = build / "platform/web/canvas_poc04_web.wasm"
            if not javascript.is_file() or not wasm.is_file():
                raise RuntimeError("Web source-free smoke did not produce JS and WASM")
        elif platform == "android":
            library = build / "platform/android/libcanvas_poc04_android.so"
            if not library.is_file():
                raise RuntimeError("Android source-free smoke did not produce JNI library")
        elif platform == "macos":
            report = build / "apple-producer-smoke.json"
            completed = subprocess.run([
                str(build / "canvas_poc04_canonical_behavior_report"),
                "--platform=macos-producer-smoke",
                f"--output={report}",
            ], cwd=build, check=False)
            if report.is_file():
                print(report.read_text(encoding="utf-8"), end="")
            if completed.returncode != 0:
                raise RuntimeError(
                    "macOS canonical behavior recorder rejected the source-free SDK "
                    f"with exit code {completed.returncode}"
                )
        elif platform in ("ios", "ios-simulator"):
            products = list(build.rglob("canvas_poc04_canonical_behavior_report"))
            if not any(path.is_file() for path in products):
                raise RuntimeError(
                    "Apple mobile source-free smoke did not produce the linked "
                    "canonical behavior executable"
                )
    finally:
        hidden.rename(SKIA_ROOT)
    print(f"source-free POC-04 build passed: {args.target}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
