#!/usr/bin/env python3
"""Enforce the POC-05 Public C ABI and isolated CI boundary."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[3]
POC = ROOT / "pocs" / "hybrid_surface"
CORE = [
    POC / "include" / "canvas" / "poc05" / "hybrid_surface.h",
    POC / "src" / "hybrid_surface.cpp",
]
WORKFLOW = ROOT / ".github" / "workflows" / "poc05.yml"


def main() -> int:
    core_text = "\n".join(path.read_text(encoding="utf-8") for path in CORE)
    forbidden_core = {
        r"canvas/poc03": "POC-03 private C++ header",
        r"poc03::": "POC-03 private C++ type",
        r"RuntimeScene|SceneBinding|SceneRecordStore": "private Scene type",
        r"SkCanvas|SkSurface|GrDirectContext": "Skia implementation type",
        r"HWND|ANativeWindow|CAMetalLayer|UIView\s*\*": "native platform handle",
    }
    violations = [
        label for pattern, label in forbidden_core.items()
        if re.search(pattern, core_text)
    ]
    for required in (
        "canvas_runtime_api_v1.h",
        "CanvasViewHandle",
        "CanvasCameraStateV1",
        "CanvasSurfaceStateV1",
        "CanvasWorldToScreenFunction",
        "viewportRevision",
        "targetGeneration",
    ):
        if required not in core_text:
            violations.append(f"missing stable boundary {required}")

    workflow = WORKFLOW.read_text(encoding="utf-8")
    for required in (
        "host-contract:",
        "web-overlay:",
        "apple-rn-fabric:",
        "poc05-host-debug",
        "poc05-host-asan",
        "node-version: 24.18.0",
        "ios-simulator-arm64-metal",
        "AxiomHybridSurfaceComponentView",
        "pocs/hybrid_surface/**",
    ):
        if required not in workflow:
            violations.append(f"workflow missing {required}")
    if violations:
        raise RuntimeError("POC-05 contract violation: " + ", ".join(violations))
    print("POC-05 core consumes only the stable Runtime View/Surface C ABI boundary")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
