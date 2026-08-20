# POC-05 Hybrid Surface Runbook

## Scope

This POC validates only the future-capability boundary described in
`docs/planning/STAGED_DELIVERY_PLAN.md`. The semantic placeholder, placement
command and registry are experimental. They are not a product ABI, a file
format, a collaboration protocol or a V1 node type.

## Contract checks

1. Build `poc05-host-debug` and run all six GoogleTest cases.
2. Confirm the separate POC-03 contract test sees exactly one reserved, empty
   `ExternalSurface` pass, dependent on Ink, followed by Overlay.
3. Confirm the POC-05 core has no dependency on POC-03 C++ headers and consumes
   only stable View/Camera/Surface C ABI types plus
   `canvas_view_world_to_screen()` projection.
4. Confirm a stale frame is ignored, a target-generation change destroys old
   platform resources, and clear/unregister returns active and materialized
   counts to zero.

## Web evidence

The Playwright corpus uses an iframe and a video element without external
network content. It covers create, move, scale, clip/viewport occlusion, hide,
failure placeholder, page/generation switch, focus handoff and 100 lifecycle
iterations. The measured placement tolerance is one CSS/device pixel at DPR 1;
the two-frame update check waits for at most two animation frames.

The controlled layer is fixed above Canvas and below product UI. Arbitrary DOM
nodes are intentionally not supported between Canvas nodes.

## Remaining physical/platform gates

| Target | Status | Required evidence |
| --- | --- | --- |
| Web iframe/video | **Accepted (POC risk proof)** | Playwright placement, two-frame update, focus/failure and 100 lifecycle corpus |
| Windows RNW/WebView2/video | **Accepted (POC risk proof)** | RNW 0.84 Fabric Shell, Skia/D3D12 CanvasSurface, WebView2/video and physical report |
| Android RN/WebView/VideoView | **Accepted (POC risk proof)** | RN 0.84.1 Fabric shell, GLES3 CanvasView/JNI, 16 KiB check and Pixel physical run |
| Apple native UIKit/WKWebView/video | Validated experimentally | Native universal runner and iOS/iPadOS run; see [Apple evidence](../../../docs/evidence/poc05/apple-native-physical-validation-20260820.md) |
| Apple RN/Fabric/WKWebView/native video | **Accepted (POC risk proof)** | RN 0.84.1 Fabric simulator, iPad and iPhone physical runs |

POC-05 is accepted as a future-capability risk proof after the platform
evidence is consolidated. This does not make the experimental registry a
product ABI or add external surfaces to V1.

For the Apple RN/Fabric implementation, use the dedicated
[RN/Fabric evidence report](../../../docs/evidence/poc05/apple-rn-fabric-validation-20260820.md).
Do not substitute the UIKit runner report for this gate.

## Windows native peer and RNW product Shell

`platform/windows` keeps the RNW/Fabric boundary deliberately small: Fabric
mount/update/unmount operations become low-frequency placeholder changes, while
`RuntimeViewFrame` snapshots are published directly to the registry on the
native owner thread. `windows_webview2_backend` owns only WebView2 COM
controllers and applies DPI-scaled bounds, visibility, focus, failure state and
generation destruction. The standalone runner is the physical validation
vehicle. The RNW product Shell lives in `platform/react_native/windows` and
registers the code-generated `AxiomHybridSurface` Fabric component. Its native
view creates a Skia/D3D12 child HWND, mounts WebView2 and video through
`WindowsRnwFabricExternalSurfaceHost`, and applies Fabric visibility, failure,
page and lifecycle-generation props without sending per-frame placement to JS.

The Canvas peer wraps both D3D12 swap-chain back buffers as Skia Ganesh
surfaces. The Windows RNW physical-validation Shell additionally links the
POC-03 core and renders its Android-equivalent 100K `RuntimeScene` through a
private bridge. The bridge is explicitly temporary and does not satisfy the
stable Runtime C ABI product boundary; record
`runtime_c_abi_binary_conformance: false`. The Shell writes
`poc05-windows-rnw-scene.json` beside the executable after both overlays mount
and 10 native frames complete, including the scene
node count, GPU, DPI, Skia RGBA readback, render count and overlay registry
diagnostics. POC-05 is accepted for the controlled-overlay architecture risk
proof; the private bridge remains non-conforming product integration work.

The SDK is pinned to Microsoft.Web.WebView2 `1.0.2592.51`; the package and
native-header SHA-256 values are recorded in
[`WEBVIEW2_SDK.lock.json`](../platform/windows/WEBVIEW2_SDK.lock.json). The
runner records the installed Runtime version and writes
`poc05-windows-rnw-webview2.json`.

Use `tools/run_windows_rnw_shell.ps1` to codegen-check, autolink-check, build
and launch the self-contained Release x64 Shell. Treat the private POC-03
bridge only as scoped Windows physical evidence, not stable Runtime C ABI
conformance.
