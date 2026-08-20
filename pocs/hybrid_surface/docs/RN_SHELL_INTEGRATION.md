# POC-05 React Native Shell Integration

## Decision under test

POC-05 will validate the intended product shape on native platforms:

```text
React Native product UI
        ↓ low-frequency props/commands
Fabric ExternalSurfaceHost + CanvasSurface
        ↓ same native owner thread
Runtime Public C ABI + POC-05 registry
        ↓
Native WebView / video overlay + native Skia Canvas surface
```

React Native is the Shell, not the Canvas or overlay placement data plane. The
Fabric component may mount/unmount an external surface and publish low-frequency
content/lifecycle intent. Camera projection, frame revision, target generation,
clip placement and native view updates must stay in Runtime/native code.

## Stable Runtime dependency

The platform component consumes only the normative v1 boundary:

- `CanvasViewHandle` identifies the attached Canvas View.
- `canvas_view_get_camera()` supplies `viewport_revision`.
- `canvas_view_world_to_screen()` owns all camera math.
- `CanvasSurfaceStateV1` supplies DPR, pixel size and `target_generation`.
- `canvas_view_on_vsync()` / `canvas_view_render_frame()` define canonical frame
  scheduling; the overlay command is applied for the matching frame snapshot.

No Scene handle is added to the C ABI. `SceneBinding`, `SceneRecordStore`,
`IRenderScene`, `ISpatialIndex`, `DamageTracker`, Skia objects and native view
handles are not visible to React Native or JavaScript.

## Fabric component contract

The experimental Shell surface is conceptually:

```tsx
<CanvasSurface viewKey={viewKey} />
<ExternalSurfaceHost
  surfaceId={surfaceId}
  kind="webView" // or "video"
  source={source}
  accessibilityLabel={label}
/>
```

`surfaceId`, `kind`, source changes, accessibility and explicit visibility are
low-frequency props. Per-frame `deviceBounds`, clip, opacity, revision and
generation are not React props and do not trigger React reconciliation. They
are applied directly by the native registry/backend.

## Platform adapters and gates

| Platform | Native objects | Required gate |
| --- | --- | --- |
| Android | Fabric ViewManager + CanvasView + WebView/VideoView | JS-stall independence, MotionEvent path remains native, 16 KiB alignment, Pixel physical run |
| iOS/iPadOS | Fabric component + UIView/CAMetalLayer + WKWebView/AVPlayerLayer | JS-stall independence, DisplayLink placement, focus/lifecycle, iPhone/iPad run |
| macOS | RN macOS component + NSView/CAMetalLayer + WKWebView/AVPlayerLayer | window/scale lifecycle and physical run |
| Windows | RNW C++ Fabric component + Composition/D3D Canvas + WebView2/video | JS-stall independence, DPI/window lifecycle, physical Windows run |
| Web | React Shell + WASM Canvas + iframe/video DOM overlay | rAF placement, focus/failure/lifecycle; React does not receive per-frame placement props |

The Apple adapter is now implemented against React Native 0.84.1/Fabric with
real `WKWebView` and video-layer objects. Its simulator build and iPad
physical launch are validated; the human iPad/iPhone gate remains in progress.
The Windows adapter now pins React Native 0.84.1 and React Native Windows
0.84.0, registers a code-generated Fabric component, renders through a real
Skia Ganesh/D3D12 swap chain, and mounts real WebView2 controllers. For the
Windows physical validation run, that Canvas uses a private POC-03 C++
`RuntimeScene` bridge configured to the Android-equivalent 100K scene. This is
deliberately outside the stable C ABI boundary and is recorded as
`runtime_c_abi_binary_conformance: false`; it must not be presented as the
final product architecture. Android and Apple product RN adapters remain
Pending where their remaining physical gates are not complete. A colored
native rectangle cannot stand in for WebView/video evidence.
## JS-stall acceptance corpus

Every native adapter must deliberately block the JS thread for 100 ms while a
native pan/zoom trace and Canvas rendering continue. During the block:

- no Canvas input interruption, black frame or presentation freeze occurs;
- placement stays within two canonical frames;
- no per-frame event is queued to JavaScript;
- releasing JS does not replay stale bounds or an old target generation;
- focus remains owned by exactly one of Canvas or the external surface.
