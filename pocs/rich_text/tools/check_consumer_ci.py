#!/usr/bin/env python3
"""Keep ordinary POC-04 validation source-free and isolated from POC-01."""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
WORKFLOW = ROOT / ".github/workflows/poc04.yml"
ANDROID_BUILD = ROOT / "pocs/rich_text/platform/android/app/build.gradle.kts"
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
    if "if: ${{ false }}" not in text:
        raise RuntimeError(
            "cross-platform acceptance must stay disabled until platform recorders exist"
        )
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
    print("POC-04 workflow is an isolated, source-free RichText SDK consumer")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
