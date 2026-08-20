# POC-05 Hybrid Surface

POC-05 is an experimental architecture risk proof for controlled external
overlays. It does not add an `ExternalSurface` node to the V1 Document schema
or extend the stable Runtime C ABI.

The host core consumes the normative Runtime C ABI's `CanvasViewHandle`,
`CanvasCameraStateV1`, `CanvasSurfaceStateV1`, and
`canvas_view_world_to_screen()` contract. It owns experimental placeholders,
placement revisions, target generations, lifecycle, focus and failure state.
Native handles never enter the registry, Scene, FrameGraph, or Canvas draw
list; they are owned by `PlatformOverlayBackend`. The only supported z-order
is:

```text
product UI
controlled external overlay (WebView / Video)
canonical Canvas
```

The POC-03 reserved-pass dependency is checked in a separate test only. The
POC-05 library does not link or include POC-03 C++ scene types. This keeps the
future React Native/Fabric adapters on the public View/Surface boundary.

The Web harness uses a real iframe and a real HTML video element in the fixed
overlay band. An experimental native Apple runner now validates the shared
runtime with `WKWebView` and a deterministic video layer on iOS/iPadOS; it is
not the eventual React Native/Fabric shell. Windows WebView2 and the Android
WebView/VideoView product adapters remain pending.
The intended native Shell boundary is specified in
[RN_SHELL_INTEGRATION.md](docs/RN_SHELL_INTEGRATION.md).

Build the host contract tests with:

```sh
python3 tools/bootstrap_deps.py --core
cmake --preset poc05-host-debug
cmake --build --preset poc05-host-debug --parallel
ctest --preset poc05-host-debug --output-on-failure
```

Run the browser harness with Node 24.18.0 and the locked Playwright version:

```sh
cd pocs/hybrid_surface/platform/web
npm ci
npx playwright install chromium
npm test
```
