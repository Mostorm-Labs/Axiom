#!/usr/bin/env python3
"""Clean-build the current Canvas target using only an extracted SDK."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess

from sdk import DEFAULT_PROFILE, ROOT, SKIA_ROOT, load_profile, target_definition
from verify import verify_archive


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--target", required=True)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--profile", type=Path, default=DEFAULT_PROFILE)
    parser.add_argument("--cc")
    parser.add_argument("--cxx")
    parser.add_argument("--ndk", default=os.environ.get("ANDROID_NDK_ROOT"))
    args = parser.parse_args()

    profile = load_profile(args.profile.resolve())
    target = target_definition(profile, args.target)
    work = ROOT / "out/skia-sdk-smoke" / args.target
    sdk_root = work / "sdk"
    build = work / "build"
    probe_build = work / "cmake-package-build"
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    verify_archive(args.archive.resolve(), args.profile.resolve(), args.target, sdk_root)

    configure = [
        "cmake", "-S", str(ROOT), "-B", str(build),
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCANVAS_POC01_BUILD_TESTS=OFF",
        "-DCANVAS_POC01_ENABLE_SKIA=ON",
        f"-DCANVAS_POC01_SKIA_ROOT={sdk_root}",
        f"-DCANVAS_POC01_SKIA_LIBRARY={sdk_root / 'lib' / target['libraries'][0]}",
    ]
    probe_configure = [
        "cmake", "-S", str(ROOT / "tools/skia/cmake_probe"), "-B", str(probe_build),
        "-DCMAKE_BUILD_TYPE=Release",
        f"-DCanvasSkia_DIR={sdk_root / 'lib/cmake/CanvasSkia'}",
    ]
    platform = target["platform"]
    if platform == "web":
        configure += [
            "-G", "Ninja",
            f"-DCMAKE_TOOLCHAIN_FILE={ROOT / '.deps/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake'}",
            "-DCANVAS_POC01_WEB=ON",
        ]
        probe_configure += [
            "-G", "Ninja",
            f"-DCMAKE_TOOLCHAIN_FILE={ROOT / '.deps/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake'}",
        ]
        build_target = "canvas_poc01_web"
    elif platform == "windows":
        configure += ["-G", "Ninja", "-DCANVAS_POC01_WINDOWS=ON"]
        probe_configure += ["-G", "Ninja"]
        if args.cc:
            configure.append(f"-DCMAKE_C_COMPILER={Path(args.cc).resolve()}")
            probe_configure.append(f"-DCMAKE_C_COMPILER={Path(args.cc).resolve()}")
        if args.cxx:
            configure.append(f"-DCMAKE_CXX_COMPILER={Path(args.cxx).resolve()}")
            probe_configure.append(f"-DCMAKE_CXX_COMPILER={Path(args.cxx).resolve()}")
        build_target = "canvas_poc01_windows"
    elif platform == "macos":
        configure += ["-G", "Ninja", "-DCMAKE_OSX_ARCHITECTURES=arm64"]
        probe_configure += ["-G", "Ninja", "-DCMAKE_OSX_ARCHITECTURES=arm64"]
        build_target = "canvas_poc01_macos_runner"
    elif platform in ("ios", "ios-simulator"):
        toolchain = "ios.toolchain.cmake" if platform == "ios" else "ios-simulator.toolchain.cmake"
        configure += [
            "-G", "Xcode", f"-DCMAKE_TOOLCHAIN_FILE={ROOT / 'cmake' / toolchain}",
            "-DCMAKE_OSX_ARCHITECTURES=arm64",
        ]
        probe_configure += [
            "-G", "Xcode", f"-DCMAKE_TOOLCHAIN_FILE={ROOT / 'cmake' / toolchain}",
            "-DCMAKE_OSX_ARCHITECTURES=arm64",
        ]
        build_target = "canvas_poc01_ios_runner"
    elif platform == "android":
        if not args.ndk:
            raise RuntimeError("Android source-free smoke requires --ndk")
        configure += [
            "-G", "Ninja",
            f"-DCMAKE_TOOLCHAIN_FILE={Path(args.ndk) / 'build/cmake/android.toolchain.cmake'}",
            f"-DANDROID_ABI={target['arch']}",
            "-DANDROID_PLATFORM=android-26", "-DANDROID_STL=c++_static",
            "-DCANVAS_POC01_ANDROID=ON",
        ]
        probe_configure += [
            "-G", "Ninja",
            f"-DCMAKE_TOOLCHAIN_FILE={Path(args.ndk) / 'build/cmake/android.toolchain.cmake'}",
            f"-DANDROID_ABI={target['arch']}",
            "-DANDROID_PLATFORM=android-26", "-DANDROID_STL=c++_static",
        ]
        build_target = "canvas_poc01_android"
    else:
        raise RuntimeError(f"unsupported target platform: {platform}")

    hidden_source = ROOT / ".deps/skia-source-disabled"
    if hidden_source.exists():
        raise RuntimeError(f"refusing to overwrite {hidden_source}")
    if not SKIA_ROOT.exists():
        raise RuntimeError("source-free smoke expects the producer checkout before isolation")
    SKIA_ROOT.rename(hidden_source)
    try:
        subprocess.run(configure, cwd=ROOT, check=True)
        command = ["cmake", "--build", str(build), "--target", build_target, "--parallel"]
        if platform in ("ios", "ios-simulator"):
            command += ["--config", "Release"]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run(probe_configure, cwd=ROOT, check=True)
        probe_command = [
            "cmake", "--build", str(probe_build), "--target",
            "canvas_skia_sdk_probe", "--parallel",
        ]
        if platform in ("ios", "ios-simulator"):
            probe_command += ["--config", "Release"]
        subprocess.run(probe_command, cwd=ROOT, check=True)
    finally:
        hidden_source.rename(SKIA_ROOT)
    print(f"source-free Canvas build passed: {args.target}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
