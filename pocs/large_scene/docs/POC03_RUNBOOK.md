# POC-03 validation runbook

## Reproducibility identity

Every report must record the Git commit, generator algorithm version, seed,
node count, compiler/build type, platform/architecture, Skia SDK ID/backend,
viewport/resolution/DPR, device/driver/browser, refresh rate, test duration,
and sampling method. The current canonical generator invocation is:

```text
algorithm_version = 1
seed              = 0x43414e5641533033
nodes             = 100000
columns           = 1000
cell_size         = 32 binary32 world units
```

Cross-platform acceptance compares `document_digest` and `scene_digest`
byte-for-byte. Pixel digests are backend diagnostics; the visual gate compares
incremental output with the same-revision full-compile oracle because different
GPU raster kernels are not expected to have identical hashes.

## Automated lanes

### Host core

Run the Debug and sanitizer presets, followed by the fixed benchmark. Archive
its JSON. The benchmark's timing source is explicitly
`headless-steady-clock-not-vsync`; it cannot satisfy the platform frame gates.

POC-03's direct Skia/Linear-or-uniform-grid path is an experimental baseline.
It does not prove that a production 10K/100K canvas can scale linearly with
the current implementation. Follow-on rendering work is tracked as RF-01
Scene/RenderScene/Damage boundaries, RF-02 dynamic spatial query, and RF-03
TileGrid/LOD/raster scheduling. The target interfaces and ownership are in
[ADR-0021](../../../docs/adr/0021-render-scene-spatial-index-tiling-boundaries.md).

### Web/WebGL2

Fetch `web-wasm-webgl2`, build `poc03-web-release`, run the browser probe under
Playwright Chromium/SwiftShader, and scan the JavaScript/WASM outputs for
pthread, `SharedArrayBuffer`, COOP, or COEP. The report must show linear memory
at or below 512 MiB and at most 5,000 candidates.

### Windows/D3D12 WARP

Fetch `windows-x64-d3d12` and build with the repository-pinned clang-cl. Run
the core tests/benchmark and `canvas_poc03_windows_probe`. WARP proves build,
render/readback, and correctness only; it does not satisfy physical performance.

### Integrated Ink matrix

Host, Web, Windows, and Android run the same four scales: 1,000/200,
10,000/2,000, 50,000/10,000, and 100,000/20,000 base nodes/canonical Strokes.
Each scale uses real four-batch Vector/Dab history, then `pan`, `zoom`,
`write-vector`, `write-dab`, `select`, and `drag`. Results must contain matching
`ink_document_digest` and `document_digest`, `maximum_candidates <= 5000`, zero
full fallbacks, and `full_incremental_equivalent = true`. Select/Drag updates
ordinary non-Stroke nodes only; it never invents a MoveStroke operation.

The Web shell is a React/Vite wrapper around the combined WASM module;
`getCoalescedEvents()` goes directly into C++ PointerSampleBatch arrays. The
Windows interactive probe accepts mouse input for select/drag while retaining
the same D3D12 Scene/Ink Pass. These shells are experimental evidence surfaces,
not product ABI commitments.

## Physical performance bundle

On the registered Windows benchmark device, run native D3D12 and Chrome Stable
for 60 seconds at 100K nodes using the same pan/zoom trace. Store:

- adapter vendor/device/driver and browser version;
- refresh rate and actual presentation intervals;
- frame p50/p95/p99/max and missed presentations;
- Runtime/scene/cache peak bytes, with source assets reported separately;
- digest and generated-binary hashes;
- trace timestamps for request, callback, render submit, present, and visible.

Windows must meet p95 ≤ 16.7 ms and p99 ≤ 33.3 ms. Web must meet p95 ≤
20 ms and p99 ≤ 40 ms. The high-refresh experience must also be judged in
frame intervals; these absolute limits are not a claim that 16.7 ms is one
frame on every display.

The Windows physical probe separates render/submit duration (the `frame_*`
gate fields) from DXGI presentation intervals. Run it from an x64 Visual
Studio developer environment after the `poc03-windows-release` build:

```powershell
out/poc03-windows-release/pocs/large_scene/platform/skia/canvas_poc03_windows_probe.exe `
  --hardware --seconds=60 `
  --output=out/poc03-windows-physical/windows-hardware-result.json `
  --trace-output=out/poc03-windows-physical/windows-frame-trace.ndjson
```

The Web physical probe must use installed Chrome Stable, not Playwright's
bundled Chromium or SwiftShader. It records rAF intervals separately from
render/GPU completion (`gl.finish`) duration:

```powershell
node pocs/large_scene/platform/web/hardware_benchmark.mjs `
  --build out/poc03-web-release/pocs/large_scene/platform/skia `
  --chrome "C:/Program Files/Google/Chrome/Application/chrome.exe" `
  --seconds 60 `
  --output out/poc03-windows-physical/web-hardware-result.json `
  --trace out/poc03-windows-physical/web-frame-trace.ndjson
```

When a physical gate fails, preserve the failed result and do not lower the
threshold. The integrated Windows D3D12 evidence on the registered machine
failed twice (p95/p99 `26.998/38.737 ms` and `25.693/37.457 ms`), while a
bounded Web rerun passed after an initial failure. Before changing the
renderer, add trace segments for candidate query, precise hit, Scene/RenderTree
build, Damage/Tile invalidation, raster queue age, Skia draw/flush, GPU submit,
present interval, and each memory category. This distinguishes an index/damage
cost from Tile/raster scheduling or D3D12 present/driver cost.

For Android, archive one representative physical-device report covering the
1K/10K/50K/100K pan, zoom, select, and drag paths. `write` is only accepted
after the POC-02 playground is integrated. Record frame/input/memory traces and
classify every interaction freeze, input discontinuity, or unbounded growth.
Use the checked-in `platform/android` app without changing the seed or 600-frame
trace. Copy `poc03-android-result.json` with `run-as`, then record device model,
SoC/GPU/driver, OS build, screen resolution, refresh modes, APK hash, Git commit,
thermal state, and whether the manual pan/pinch rubric exposed a freeze. The
emulator lane proves APK/GLES/readback correctness only and cannot replace this
physical-device evidence. Before installation, verify both the ELF `PT_LOAD`
alignment and the uncompressed APK entry alignment are 16 KiB; the Android CI
lane performs both checks and a device compatibility warning is a hard failure.

For iPadOS, build the checked-in `canvas_poc03_ipados_runner` with the locked
`ios-arm64-metal` SDK and a local signing identity. Run the fixed 600-frame
trace once immediately after installation and once more without reinstalling.
Archive both reports, the signed app binary hash, Skia SDK ID, device model,
iPadOS build, physical resolution/DPR, maximum refresh rate, and wired/wireless
transport. Interpret `frame_*` as the whole CADisplayLink callback,
`render_submit_*` as Metal draw/submit diagnostics, and `display_interval_*`
as the scheduling evidence. Record first-launch shader/pipeline compilation
spikes rather than folding them into the warm result. Finally, exercise single-
finger pan and two-finger pinch for at least ten seconds each. Pinch must
preserve the world point below the moving two-finger centroid. Verify both
simultaneous release and one-finger-first release, then continue dragging with
the remaining finger; none may cause a viewport jump. Also report any freeze,
black frame, flicker, or input discontinuity. Gesture events crossing a pointer-
count boundary must update recognizer state without committing a transform
derived from a one-pointer pinch centroid.

## Failure artifacts

An equivalence failure must preserve the seed, operations, ChangeSets, injected
hint variant, incremental/full Scene digests, candidate/hit-test results, and
incremental/full RGBA outputs plus their diff. Do not update or bless a visual
baseline automatically. Reduce the operation list and node count while keeping
the failure before opening a correctness issue.
