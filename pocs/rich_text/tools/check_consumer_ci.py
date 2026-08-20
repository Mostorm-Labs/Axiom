#!/usr/bin/env python3
"""Keep ordinary POC-04 validation source-free and isolated from POC-01."""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
WORKFLOW = ROOT / ".github/workflows/poc04.yml"
ANDROID_BUILD = ROOT / "pocs/rich_text/platform/android/app/build.gradle.kts"
ANDROID_RECORDER = ROOT / "pocs/rich_text/platform/android/record_canonical_behavior.sh"
FORBIDDEN = {
    r"bootstrap_deps\.py[^\n]*--skia": "Skia source bootstrap",
    r"sync-skia": "Skia dependency sync",
    r"\.deps/skia/out": "Skia source output",
    r"tools/skia/build\.py": "Skia SDK builder",
    r"--lock[ =]+skia-sdk\.lock\.json": "root POC-01 SDK lock",
}


def main() -> int:
    text = WORKFLOW.read_text(encoding="utf-8")
    violations = [label for pattern, label in FORBIDDEN.items() if re.search(pattern, text)]
    if violations:
        raise RuntimeError("POC-04 consumer CI contains producer behavior: " + ", ".join(violations))
    if "hashFiles('pocs/rich_text/skia-sdk.lock.json')" in text:
        raise RuntimeError("POC-04 jobs must not evaluate the SDK lock before checkout")
    if "needs.sdk-lock.outputs.available == 'true'" not in text:
        raise RuntimeError("POC-04 platform jobs must use the post-checkout SDK lock gate")
    relative_toolchain = (
        "-DCMAKE_TOOLCHAIN_FILE=.deps/emsdk/upstream/emscripten/"
        "cmake/Modules/Platform/Emscripten.cmake"
    )
    if relative_toolchain in text:
        raise RuntimeError("Web consumer must not resolve the toolchain relative to its build tree")
    absolute_toolchain = (
        '-DCMAKE_TOOLCHAIN_FILE="$GITHUB_WORKSPACE/.deps/emsdk/upstream/emscripten/'
        'cmake/Modules/Platform/Emscripten.cmake"'
    )
    if absolute_toolchain not in text:
        raise RuntimeError("Web consumer must use the checkout-absolute Emscripten toolchain path")
    web_output = "out/poc04-web/platform/web/canvas_poc04_web"
    for suffix in (".js", ".wasm"):
        if f"test -s {web_output}{suffix}" not in text:
            raise RuntimeError(f"Web consumer must verify its {suffix} target output")
    expected_wasm_dir = 'POC04_WASM_DIR="$GITHUB_WORKSPACE/out/poc04-web/platform/web"'
    if expected_wasm_dir not in text:
        raise RuntimeError("Web behavior recorder must serve the CMake target output directory")
    if 'grep -E -l "pthread|SharedArrayBuffer"' not in text:
        raise RuntimeError("Web consumer must reject pthread and SharedArrayBuffer output")
    if 'rg -l "pthread|SharedArrayBuffer"' in text:
        raise RuntimeError("Web consumer must not depend on ripgrep being installed on the runner")
    if "if: ${{ false }}" not in text:
        raise RuntimeError(
            "cross-platform acceptance must stay disabled until platform recorders exist"
        )
    if "bash pocs/rich_text/platform/android/record_canonical_behavior.sh" not in text:
        raise RuntimeError("Android emulator job must invoke the atomic recorder script")
    if ":app:assembleRelease" not in text or ":app:assembleDebug" in text:
        raise RuntimeError("Android performance gate must use optimized Release native code")
    if '-PcanvasPoc04EnforcePerformanceGate="$performance_gate"' not in text:
        raise RuntimeError("Android CI must declare whether absolute performance is representative")
    if "performance_gate=false" not in text:
        raise RuntimeError("shared Android emulator must not own the physical performance gate")
    if "Summarize non-representative emulator timing" not in text:
        raise RuntimeError("Android emulator must preserve timing as observational evidence")
    android_bootstrap_start = text.index("- name: Install pinned Android NDK and core dependencies")
    android_bootstrap_end = text.index("- name: Install Android emulator image", android_bootstrap_start)
    android_bootstrap = text[android_bootstrap_start:android_bootstrap_end]
    if "bootstrap_deps.py --core --font-only" not in android_bootstrap:
        raise RuntimeError("Android consumer must materialize the independent font fixtures")
    for asset in (
        "assets/fonts/Roboto-Regular.ttf",
        "assets/fonts/NotoSansCJK-VF-subset.otf.ttc",
    ):
        if f"grep -Fx '{asset}'" not in text:
            raise RuntimeError(f"Android consumer must verify packaged fixture {asset}")
    if "always() && matrix.emulator == true" not in text:
        raise RuntimeError("Android diagnostics artifact must upload after recorder failure")
    for target in ("web-wasm-webgl2", "windows-x64-d3d12",
                   "android-arm64-v8a-gles3", "android-x86_64-gles3"):
        if target not in text:
            raise RuntimeError(f"POC-04 consumer CI does not fetch {target}")
    android = ANDROID_BUILD.read_text(encoding="utf-8")
    expected_root = (
        "${rootProject.projectDir}/../../../../.deps/skia-sdk-poc04/"
        "${canvasPoc04SdkTarget.get()}"
    )
    if expected_root not in android:
        raise RuntimeError("Android consumer SDK path does not resolve to repository .deps")
    recorder = ANDROID_RECORDER.read_text(encoding="utf-8")
    if "outputs/apk/release/app-release.apk" not in recorder:
        raise RuntimeError("Android recorder must install the measured Release APK")
    if "am start -S -W --activity-clear-task" not in recorder:
        raise RuntimeError("Android recorder must cold-start the canonical entry point")
    for required in (
        "adb logcat -d -t 1000",
        "dumpsys activity top",
        'adb shell pidof "$package_name"',
        'test -s "$device_output_dir/$artifact_name"',
        'test -s "$device_output_dir/$failed_artifact_name"',
        'adb pull "$device_output_dir/$artifact_name"',
        'adb pull "$device_output_dir/$failed_artifact_name"',
    ):
        if required not in recorder:
            raise RuntimeError(f"Android recorder is missing required diagnostic: {required}")
    print("POC-04 workflow is an isolated, source-free RichText SDK consumer")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
