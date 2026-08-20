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
POC-05 host core still does not link or include POC-03 C++ scene types. For the
Windows RNW physical validation lane only, the product Shell has an explicitly
labelled private C++ `RuntimeScene` bridge so its Canvas renders the same 100K
scene as Android. This bridge is temporary validation evidence, not the final
Runtime C ABI product architecture; its conformance flag remains false.

The Web harness uses a real iframe and a real HTML video element in the fixed
overlay band. Apple now also has a React Native 0.84.1 Fabric component that
hosts the same native owner (`CAMetalLayer` + Skia Ganesh + `WKWebView` + a
video layer); the UIKit runner remains a separate lower-level oracle. The
Apple RN/Fabric simulator build and iPad physical launch are validated, while
the manual iPad/iPhone interaction gate is still pending. Windows has both a
standalone native peer in `platform/windows` and a real RNW 0.84 New
Architecture/Fabric product Shell in `platform/react_native/windows`. Its
Fabric component owns a visible Skia Ganesh/D3D12 child surface and
WebView2/video overlays through `WindowsRnwFabricExternalSurfaceHost`. The
Android WebView/VideoView product adapter remains pending.
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

Build and run the Windows native peer with MSVC, the pinned WebView2 SDK and
the verified Windows Skia SDK:

```powershell
cmake -S . -B build-poc05-win -G Ninja `
  -DCANVAS_BUILD_POC01=OFF -DCANVAS_BUILD_POC03=ON `
  -DCANVAS_BUILD_POC05=ON -DCANVAS_POC05_BUILD_TESTS=OFF `
  -DCANVAS_POC05_ENABLE_SKIA=ON `
  -DCANVAS_POC03_BUILD_TESTS=OFF `
  -DCANVAS_POC05_WEBVIEW2_SDK_ROOT="$env:USERPROFILE/.nuget/packages/microsoft.web.webview2/1.0.2592.51/build/native" `
  -DCANVAS_POC05_SKIA_SDK_ROOT=".deps/skia-sdk/windows-x64-d3d12"
cmake --build build-poc05-win --target canvas_poc05_windows_runner
build-poc05-win/pocs/hybrid_surface/platform/windows/canvas_poc05_windows_runner.exe
```

For a repeatable build/run/check from a Visual Studio developer shell, use
`powershell -ExecutionPolicy Bypass -File
pocs/hybrid_surface/tools/run_windows_native_validation.ps1`.

The helper downloads and verifies the locked Skia SDK when it is absent. The
runner writes `poc05-windows-rnw-webview2.json` with Runtime/WebView2 versions,
DPI, memory, frame interval percentiles, registry lifecycle counts, the
`skia-ganesh-d3d12` renderer identity and a 100 ms JavaScript-stall probe. The
visible grid/title is a Skia diagnostic scene proving the swap-chain surface;
it is not yet a product Runtime document. The result is evidence for the
native peer and must not be represented as product Runtime document/Scene
conformance.

Build and launch the self-contained x64 RNW product Shell with:

```powershell
powershell -ExecutionPolicy Bypass -File `
  pocs/hybrid_surface/tools/run_windows_rnw_shell.ps1
```

The Shell pins React Native 0.84.1, React Native Windows 0.84.0 and Fabric,
bundles Hermes for Release, and deploys Windows App SDK app-local. The machine
does not need a preinstalled `Microsoft.WindowsAppRuntime` framework package.
The Windows RNW validation Shell uses the private POC-03 scene bridge described
above and writes `poc05-windows-rnw-scene.json` beside the executable after
both overlays mount and 10 native frames complete (the
actual render count is recorded in the report). Do not
promote that result to stable Runtime C ABI conformance or mark POC-05
`Accepted` until the C ABI scene bridge and all remaining platform gates exist.
