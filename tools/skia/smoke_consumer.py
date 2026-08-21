#!/usr/bin/env python3
"""Clean-build the current Canvas target using only an extracted SDK."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess

from sdk import DEFAULT_PROFILE, ROOT, SKIA_ROOT, load_profile, target_definition
from verify import verify_archive


def built_probe(build: Path, platform: str) -> Path:
    if platform == "ios-simulator":
        bundles = sorted(
            path for path in build.rglob("canvas_skia_sdk_probe.app")
            if path.is_dir()
        )
        if len(bundles) != 1:
            raise RuntimeError(
                f"expected one runtime probe app bundle, found {len(bundles)}"
            )
        info = bundles[0] / "Info.plist"
        metadata = plistlib.loads(info.read_bytes())
        executable = bundles[0] / metadata["CFBundleExecutable"]
        if not executable.is_file():
            raise RuntimeError(f"runtime probe bundle executable is missing: {executable}")
        return executable
    suffix = ".exe" if platform == "windows" else ""
    candidates = sorted(
        path for path in build.rglob(f"canvas_skia_sdk_probe{suffix}")
        if path.is_file()
    )
    if len(candidates) != 1:
        raise RuntimeError(
            f"expected one runtime probe executable, found {len(candidates)}"
        )
    return candidates[0]


def cmake_apple_arch(profile_arch: str) -> str:
    """Translate profile/GN architecture names to Apple's CMake spellings."""
    return {"x64": "x86_64", "arm64": "arm64"}.get(profile_arch, profile_arch)


def canvas_variant_cmake_args(variant: str) -> list[str]:
    """Return explicit Canvas consumer instrumentation for an SDK variant."""
    if variant == "asan":
        injector = ROOT / "tools/skia/cmake/asan_consumer.cmake"
        return [
            "-DCANVAS_SKIA_SDK_ASAN_CONSUMER=ON",
            f"-DCMAKE_PROJECT_INCLUDE={injector}",
        ]
    return []


def windows_variant_cmake_args(variant: str) -> list[str]:
    """Match the CMake consumer to Skia's static-CRT STL contract on Windows."""
    args = ["-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"]
    if variant in ("debug", "asan"):
        args += [
            "-DCMAKE_C_FLAGS=/D_ITERATOR_DEBUG_LEVEL=0 /D_HAS_ITERATOR_DEBUGGING=0",
            "-DCMAKE_CXX_FLAGS=/D_ITERATOR_DEBUG_LEVEL=0 /D_HAS_ITERATOR_DEBUGGING=0",
        ]
    return args


def run_asan_runtime_smoke(build: Path, platform: str, sdk_root: Path) -> None:
    probe = built_probe(build, platform)
    if platform == "windows":
        runtime = sdk_root / "runtime/windows/clang_rt.asan_dynamic-x86_64.dll"
        if not runtime.is_file():
            raise RuntimeError(f"Windows ASan runtime is missing from SDK: {runtime}")
        environment = dict(os.environ)
        environment["PATH"] = f"{runtime.parent}{os.pathsep}{environment.get('PATH', '')}"
        subprocess.run([str(probe)], check=True, env=environment)
        return
    if platform == "macos":
        subprocess.run([str(probe)], check=True)
        return
    if platform != "ios-simulator":
        return
    devices = json.loads(subprocess.check_output(
        ["xcrun", "simctl", "list", "devices", "available", "--json"],
        text=True,
    ))["devices"]
    candidates = [
        device for runtime in sorted(devices, reverse=True)
        for device in devices[runtime]
        if device.get("isAvailable") and device.get("name", "").startswith("iPhone")
    ]
    if not candidates:
        raise RuntimeError("no available iPhone simulator for ASan runtime smoke")
    device = candidates[0]
    booted_here = device.get("state") != "Booted"
    if booted_here:
        subprocess.run(["xcrun", "simctl", "boot", device["udid"]], check=True)
        subprocess.run(
            ["xcrun", "simctl", "bootstatus", device["udid"], "-b"], check=True,
        )
    bundle = probe.parent
    metadata = plistlib.loads((bundle / "Info.plist").read_bytes())
    bundle_id = metadata.get("CFBundleIdentifier")
    if not bundle_id:
        raise RuntimeError(f"runtime probe bundle identifier is missing: {bundle}")
    try:
        subprocess.run(
            ["xcrun", "simctl", "install", device["udid"], str(bundle)],
            check=True,
        )
        subprocess.run(
            [
                "xcrun", "simctl", "launch", "--console",
                "--terminate-running-process", device["udid"], bundle_id,
            ],
            check=True,
        )
    finally:
        subprocess.run(
            ["xcrun", "simctl", "uninstall", device["udid"], bundle_id],
            check=False,
        )
        if booted_here:
            subprocess.run(
                ["xcrun", "simctl", "shutdown", device["udid"]], check=False,
            )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--target", required=True)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--profile", type=Path, default=DEFAULT_PROFILE)
    parser.add_argument("--variant", choices=("release", "debug", "asan"), default="release")
    parser.add_argument("--cc")
    parser.add_argument("--cxx")
    parser.add_argument("--ndk", default=os.environ.get("ANDROID_NDK_ROOT"))
    args = parser.parse_args()

    profile = load_profile(args.profile.resolve())
    target = target_definition(profile, args.target)
    full = profile.get("schema_version") == 2
    work = ROOT / "out/skia-sdk-smoke" / args.target / args.variant
    sdk_root = work / "sdk"
    build = work / "build"
    probe_build = work / "cmake-package-build"
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    verified = verify_archive(
        args.archive.resolve(), args.profile.resolve(), args.target, sdk_root,
        expected_variant=args.variant,
    )

    configuration = "Debug" if args.variant in ("debug", "asan") else "Release"
    configure = [
        "cmake", "-S", str(ROOT), "-B", str(build),
        f"-DCMAKE_BUILD_TYPE={configuration}",
        "-DCANVAS_POC01_BUILD_TESTS=OFF",
        "-DCANVAS_POC01_ENABLE_SKIA=ON",
        f"-DCANVAS_SKIA_SDK_ROOT={sdk_root}",
        f"-DCANVAS_SKIA_SDK_EXPECTED_ID={verified['sdk_id']}",
        f"-DCANVAS_SKIA_SDK_VARIANT={args.variant}",
    ]
    configure += canvas_variant_cmake_args(args.variant)
    probe_configure = [
        "cmake", "-S", str(ROOT / "tools/skia/cmake_probe"), "-B", str(probe_build),
        f"-DCMAKE_BUILD_TYPE={configuration}",
        f"-DCanvasSkia_DIR={sdk_root / 'lib/cmake/CanvasSkia'}",
    ]
    if args.variant == "asan":
        probe_configure += [
            "-DCMAKE_C_FLAGS=-fsanitize=address -fno-omit-frame-pointer",
            "-DCMAKE_CXX_FLAGS=-fsanitize=address -fno-omit-frame-pointer",
            "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address",
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
        windows_args = windows_variant_cmake_args(args.variant)
        configure += windows_args
        probe_configure += windows_args
        if args.cc:
            configure.append(f"-DCMAKE_C_COMPILER={Path(args.cc).resolve()}")
            probe_configure.append(f"-DCMAKE_C_COMPILER={Path(args.cc).resolve()}")
        if args.cxx:
            configure.append(f"-DCMAKE_CXX_COMPILER={Path(args.cxx).resolve()}")
            probe_configure.append(f"-DCMAKE_CXX_COMPILER={Path(args.cxx).resolve()}")
        build_target = "canvas_poc01_windows"
    elif platform == "macos":
        apple_arch = cmake_apple_arch(target["arch"])
        configure += ["-G", "Ninja", f"-DCMAKE_OSX_ARCHITECTURES={apple_arch}"]
        probe_configure += ["-G", "Ninja", f"-DCMAKE_OSX_ARCHITECTURES={apple_arch}"]
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
    source_was_hidden = False
    if SKIA_ROOT.exists():
        SKIA_ROOT.rename(hidden_source)
        source_was_hidden = True
    try:
        subprocess.run(configure, cwd=ROOT, check=True)
        command = ["cmake", "--build", str(build), "--target", build_target, "--parallel"]
        if platform in ("ios", "ios-simulator"):
            command += ["--config", configuration]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run(probe_configure, cwd=ROOT, check=True)
        probe_command = [
            "cmake", "--build", str(probe_build), "--target",
            "canvas_skia_capability_probes" if full else "canvas_skia_sdk_probe",
            "--parallel",
        ]
        if platform in ("ios", "ios-simulator"):
            probe_command += ["--config", configuration]
        subprocess.run(probe_command, cwd=ROOT, check=True)
        if args.variant == "asan" and platform in (
            "windows", "macos", "ios-simulator",
        ):
            run_asan_runtime_smoke(probe_build, platform, sdk_root)
    finally:
        if source_was_hidden:
            hidden_source.rename(SKIA_ROOT)
    print(f"source-free Canvas build passed: {args.target}/{args.variant}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
