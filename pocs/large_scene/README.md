# POC-03 — 100K Scene

Status: **Validating** (the Integrated Windows D3D12 hard gate is reproducibly failing). This directory is a disposable experiment, not a stable
product API or file format.

POC-03 proves that one semantic `Document` can compile into a shared SoA
`RuntimeScene`, accept bounded incremental updates, service isolated Views,
and feed a logical `FrameGraph` plus L1 `TileCache` interface at 100K nodes.
It is a correctness/cross-platform baseline, not proof of a production dynamic
R-tree, layered Tile/LOD renderer, raster scheduler, or universal 100K frame
budget. Shape,
Image, VectorPath, simple read-only Text, and Stroke are deliberately compact
render records; RichText layout/editing remains POC-04, and ExternalSurface is
only a reserved empty pass for POC-05.

## Implemented proof boundaries

- A versioned deterministic generator (`algorithm version 1`, explicit seed)
  creates 100K mixed records without wall-clock or container-order inputs.
- `Document::Apply(Operation)` is the sole semantic mutation path. A successful
  transaction emits authoritative `SemanticChange` records and optional,
  disposable `InvalidationHints`.
- `SceneCompiler` supports full compile and incremental create/update/delete/
  reorder. Invalid semantic/revision input safely falls back to full compile;
  missing, enlarged, stale, or corrupt hints cannot change Scene correctness.
- `RuntimeScene` stores record fields in structure-of-arrays form and indexes
  stable IDs to slots. Its deterministic uniform grid is incrementally updated;
  a normal property update touches one record instead of scanning 100K. This
  grid is an experimental correctness implementation, not the production
  `DynamicRTreeSpatialIndex` selected by ADR-0021.
- `ViewState` owns viewport, zoom, DPR, target generation, visible set, scale
  bucket, and screen-space damage. The shared `RuntimeScene` owns none of that
  state, so a minimap does not copy or mutate the Document.
- Geometry `HitTest` is independent from the small `SelectionPolicy` and
  `SnapNearestX` harnesses; `SceneCompiler` never observes a current tool.
- `FrameBuilder` always emits Background, Content, Ink, reserved
  ExternalSurface, Overlay, Selection, and HUD logical passes. A backend may
  merge/elide physical passes while `FrameGraph::VisualDigest()` and the
  compositor draw list remain unchanged.
- The L1 cache prototype has a strict View/content/device/backend/scale/color/
  tile key, byte budget, LRU eviction, world invalidation, clear, and device
  generation reset. It is a behavior/interface proof, not yet a production GPU
  texture cache, TileGrid/TilingSet, LOD policy, prefetcher, or raster task
  scheduler.
- `DeterministicFrameScheduler` proves one pending callback per View, revision
  coalescing, target-generation rejection, latest-revision presentation, and
  View teardown isolation. Platform VSync integrations remain platform code.
- A Skia Ganesh compositor probe consumes only the verified prebuilt
  `CanvasSkia::Skia` SDK target. Ordinary POC-03 CI never checks out Skia source
  and never runs GN or builds Skia with Ninja.
- Experimental Integrated Ink keeps POC-02 authoritative: historical and live
  Vector/Dab strokes enter through `PointerSampleBatch → InputRouter →
  StrokeSession → AddStrokeOperation`; POC-03 stores only stable Stroke IDs,
  bounds, and revisions. Preview uses POC-02's renderer, and Canonical visible
  acknowledgement follows successful Scene presentation.

## Build and run

Each worktree owns its ignored `.deps/` and `out/poc03-*` directories:

```bash
python3 tools/bootstrap_deps.py --core
cmake --preset poc03-host-debug
cmake --build --preset poc03-host-debug --parallel
ctest --preset poc03-host-debug --output-on-failure
cmake --preset poc03-host-release
cmake --build --preset poc03-host-release --parallel
out/poc03-host-release/pocs/large_scene/canvas_poc03_benchmark \
  --nodes=100000 --frames=600 --updates=1000 \
  --output=out/poc03-host-release/poc03-result.json
```

The benchmark exits nonzero unless full/incremental results agree, a property
update touches at most one record, and the measured representative viewport has
at most 5,000 spatial candidates. Its steady-clock headless timing is a local
algorithm diagnostic, not physical display evidence.

## Production rendering boundary after POC-03

The accepted target architecture keeps a Canvas-owned `Scene` facade over
`RenderScene`, `ISpatialIndex`, and `DamageTracker`. Skia SkSG may implement the
private Render DAG, but it is never the Document model or public Runtime API.
Spatial query is two-stage: a Canvas-owned dynamic index returns candidates,
then Geometry/SkSG performs precise hit testing. TileGrid, signed `TileKey`,
TilingSet/LOD, TileManager, TilePriority, `IRasterSource`, raster scheduling,
memory budget, and eviction are follow-on RF-01 through RF-03 work; Chromium cc
is a source-level design reference only. See
[ADR-0021](../../docs/adr/0021-render-scene-spatial-index-tiling-boundaries.md)
and the
[rendering architecture review](../../docs/research/POC03_RENDERING_ARCHITECTURE_REVIEW.md).

To verify the prebuilt Skia consumer on macOS without any Skia source build:

```bash
python3 tools/skia/fetch.py --target macos-arm64-metal
cmake -S . -B out/poc03-macos-skia -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCANVAS_POC01_BUILD=OFF -DCANVAS_POC03_BUILD=ON \
  -DCANVAS_POC03_BUILD_TESTS=OFF -DCANVAS_POC03_ENABLE_SKIA=ON
cmake --build out/poc03-macos-skia --parallel
out/poc03-macos-skia/pocs/large_scene/platform/skia/canvas_poc03_skia_raster_probe
```

Windows and Web use the `poc03-windows-release` and `poc03-web-release`
presets. They fetch their own verified D3D12/WebGL2 SDK package; a missing or
mismatched manifest is a hard configure error with no source fallback.

The Android validation app is a native `SurfaceView`/JNI/GLES3 data path. It
automatically runs the fixed 600-frame trace at a selected scale and accepts
pan/pinch, live Vector/Dab write, and ordinary-node Select/Drag through a
Choreographer-coalesced native frame request. Select a scale at launch with
`--ei poc03_nodes 1000|10000|50000|100000`:

```bash
python3 tools/skia/fetch.py --target android-arm64-v8a-gles3
cd pocs/large_scene/platform/android
gradle :app:assembleDebug -PcanvasPoc03Abi=arm64-v8a
mkdir -p ../../../../out/poc03-android-apks
cp app/build/outputs/apk/debug/app-debug.apk \
  ../../../../out/poc03-android-apks/poc03-arm64-v8a-debug.apk
python3 ../../tools/run_android_device.py \
  --apk ../../../../out/poc03-android-apks/poc03-arm64-v8a-debug.apk \
  --nodes 100000 \
  --output ../../reports/android-$(date +%Y%m%d)
```

This app records Android Scene/render/input evidence and consumes the shared
experimental Ink contract. Its write action is an integration gate, not the
POC-02 formal pressure-pen Human Ink Gate.
The native target uses 16 KiB-aligned ELF load segments, and CI also requires
each uncompressed APK JNI library to pass `zipalign -P 16`.

The iPadOS/iOS runner uses a native UIKit `CAMetalLayer` and the locked
`ios-arm64-metal` SDK. Signing identities, Team IDs, profiles, and device IDs
are supplied only on the local command line and must not be committed:

```bash
python3 tools/skia/fetch.py --target ios-arm64-metal
cmake --preset poc03-ios-device-release \
  -DCANVAS_SKIA_SDK_ROOT="$PWD/.deps/skia-sdk/ios-arm64-metal"
xcodebuild \
  -project out/poc03-ios-device-release/canvas_v2_pocs.xcodeproj \
  -scheme canvas_poc03_ipados_runner -configuration Release \
  -sdk iphoneos -destination 'id=<LOCAL_DEVICE_UDID>' \
  -allowProvisioningUpdates CODE_SIGN_STYLE=Automatic \
  DEVELOPMENT_TEAM=<LOCAL_DEVELOPMENT_TEAM> CODE_SIGNING_ALLOWED=YES build
```

Install and launch the resulting app with `devicectl`, then copy
`Documents/poc03-result.json` from its app data container. The report separates
the complete CADisplayLink callback time, render/submit time, and actual display
callback intervals. Preserve both the first launch and a no-reinstall warm
launch; shader/pipeline compilation cost must not be silently discarded.

## Acceptance evidence and honest limits

Automated tests cover canonical float handling, deterministic generation,
atomic operations, full/incremental equivalence, hint fault injection, the
100K no-scan update gate, spatial-index versus brute-force queries, hit-test
order, two-View isolation, logical-pass optimization, cache recovery, and
scheduler generation/coalescing behavior.

Pre-integration physical Windows D3D12 and Chrome Stable Web traces, Pixel 7,
iPhone, iPad,
manual centroid/release gestures, and cross-platform equivalence are recorded
in the [2026-08-18 physical validation report](../../docs/quality/evidence/poc03/physical-validation-20260818.md).
That immutable baseline report passed its then-current Windows/Web path. Mobile performance evidence
retains the observed Pixel 7 cold-start spike and an iPad warm-run scheduling
anomaly; it is portability and experience evidence rather than a replacement
for the same-machine Windows/Web gate.

The newer Integrated Ink Windows/Web physical evidence is a different workload
and supersedes the baseline for POC-03 disposition. On Windows Native D3D12,
two 60-second runs failed the p95 ≤ 16.7 ms and p99 ≤ 33.3 ms gates:
26.998/38.737 ms and 25.693/37.457 ms. The bounded Chrome rerun passed, while
its first run did not. Correctness, digest, visual equivalence, candidate count,
and memory remained within their gates. The source evidence is commit
[`01d2bcb4b80fac2271d1b6ee6a3482054ae46cf0`](https://github.com/Mostorm-Labs/canvas/commit/01d2bcb4b80fac2271d1b6ee6a3482054ae46cf0)
on `codex/poc03-windows-integrated-physical`; its bundle SHA-256 is
`4c96441bbf74d9924dcc47a42405e943c273b75cf6e32c2a1b33f49ccf3f4ee0`.

POC-03 remains `Validating` until fresh Windows, Chrome Stable Web, and Pixel 7
Integrated Performance Playground evidence passes at 1K/10K/50K/100K. POC-02
remains `Integration Ready / Validating`; its formal pressure-pen latency and
Human Ink Gate are independent. POC-03 does not create an Android or Apple
product shell. The Windows failure is an input to RF-01 through RF-03; it does
not justify lowering the gate or claiming that the POC's direct-render path is
the final production architecture.

See [the runbook](docs/POC03_RUNBOOK.md) for evidence commands and schemas.
