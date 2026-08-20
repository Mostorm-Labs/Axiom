# POC-05 Apple native overlay physical validation — 2026-08-20

Status: **Validated for the experimental native Apple adapter**.

This report records the iPad Air 4 (Piccaso) and iPhone 15 Pro (Mosaic)
validation of the native universal runner. It does not claim React Native /
Fabric conformance; the runner intentionally uses UIKit, `CAMetalLayer`,
`WKWebView` and `AVSampleBufferDisplayLayer` to validate the shared C++20
runtime and controlled overlay lifecycle first.

## Build and devices

| Item | Value |
| --- | --- |
| Branch | `codex/poc-05-hybrid-surface` |
| Bundle | `dev.mostorm.axiom.poc05.apple` |
| Rendering | Skia Ganesh / Metal |
| iPad | iPad Air 4, `Piccaso`, arm64 device |
| iPhone | iPhone 15 Pro, `Mosaic`, arm64 device |
| Deployment target | iOS/iPadOS 17.0 |

The release app was built with the `poc05-ios-device-release` CMake preset and
installed with `xcrun devicectl` on both physical devices.

## Automated evidence

Both devices produced the same correctness fields:

```json
{
  "scene_equivalent": true,
  "placement_frames": 120,
  "max_placement_error_px": 0,
  "active_surfaces": 2,
  "materialized_surfaces": 2,
  "runtime_c_abi_binary_conformance": false,
  "react_native_fabric_conformance": false
}
```

The two device result files also recorded 100,000 scene nodes and successful
WebView/video materialization. The process footprint increased during the
scene and overlay run, but no crash or rendering failure occurred.

## Manual gate

- Single-finger Canvas pan works outside the WebView; single-finger interaction
  inside the WebView remains owned by the WebView for scrolling, focus and IME.
- Two-finger pan/zoom follows the gesture center after the coordinate-space
  correction; no release-time jump was observed.
- WebView text focus and IME input passed.
- Page 1 ↔ Page 2, Fail Web → Recover, background/foreground and `Recreate`
  lifecycle checks passed.
- `Recreate` now destroys and recreates both native surfaces and re-registers
  their semantic placeholders instead of only incrementing a generation.

## Deliberate scope limits

- This is native Apple evidence, not the eventual React Native/Fabric shell.
- The video test surface uses `AVSampleBufferDisplayLayer` as a deterministic
  placeholder rather than a product `AVPlayerLayer` integration.
- Minor video placement jitter during active WebView pinch/zoom is retained as
  a known POC limitation and is not treated as a release-quality guarantee.
- Windows RNW/WebView2 and macOS native-shell gates remain separate pending
  work.
