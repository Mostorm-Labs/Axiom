# POC-02 Ink Engine

Status: **Validating**

This proof validates that one experimental C++20 Ink Engine can consume batched
pen input, build deterministic Vector and Dab strokes incrementally, drive a
replaceable Preview path, and commit a Canonical `AddStroke` operation. It is
isolated under `pocs/ink_engine/`; none of its C++ types, NDJSON, digests, or
platform entry points are an R1 product ABI.

The implementation follows this data flow:

```text
platform history/coalesced samples
              |
              v
     PointerSampleBatch
              |
              v
 InputRouter + bounded queue
              |
              v
        StrokeSession
       /             \
      v               v
PreviewStrokeUpdate  Canonical Stroke
      |               |
      v               v
DefaultPreviewSink   AddStroke Operation
      |               |
      +-------+-------+
              v
          Skia frame
              |
     canonical-visible ack
```

## Scope and boundaries

POC-02 implements:

- versioned `PointerSampleBatch`, device capabilities, `ViewId`, viewport
  revision, and View Logical-to-World transforms;
- finite-only binary32 input, canonical zero, strict sample ordering, and
  deterministic domain-separated PCG32 streams;
- incremental resampling, smoothing, pressure mapping, Vector/Dab geometry,
  prediction, and prediction rollback;
- bounded input and Preview queues with compatible-batch merge, Preview
  coalescing, diagnostics, and atomic `InputOverrun` cancellation;
- `StrokeSession.begin/push/end/cancel`, Canonical commit, and a
  canonical-visible acknowledgement;
- strict experimental `AddStroke` NDJSON and empty-Document replay;
- a single-threaded WASM/WebGL2 playground, Windows D3D12/WARP adapter, Android
  Native `CanvasInkView`/JNI/GLES3 adapter, and Skia raster references.

It deliberately does not implement a product Document schema, a durable
Operation Log or Snapshot codec, Undo/Redo, collaboration, a platform FastInk
surface, a final threading topology, eraser behavior, or palm classification.
Palm, hover, and eraser-tip inputs are classified at the boundary and do not
produce an Ink operation in this POC.

Canonical strokes contain semantic confirmed samples and Vector points or Dab
records. Predicted points, Skia paths, GPU buffers, and bitmaps never enter the
Document or its digest.

## Build and test

All Skia builds consume the locked prebuilt SDK through only
`CanvasSkia::Skia`. POC-02 never checks out Skia source and never invokes GN or
Ninja for Skia.

Host deterministic core:

```bash
python3 tools/bootstrap_deps.py --core
cmake --preset poc02-host-debug
cmake --build --preset poc02-host-debug --parallel
ctest --preset poc02-host-debug --output-on-failure
```

Host sanitizer run:

```bash
cmake --preset poc02-host-asan
cmake --build --preset poc02-host-asan --parallel
ctest --preset poc02-host-asan --output-on-failure
```

macOS arm64 Metal/Ganesh validation:

```bash
python3 tools/skia/fetch.py --target macos-arm64-metal
cmake --preset poc02-macos-release
cmake --build --preset poc02-macos-release --parallel
ctest --preset poc02-macos-release --output-on-failure
```

Web WASM/WebGL2 playground:

```bash
python3 tools/bootstrap_deps.py --core --web
python3 tools/skia/fetch.py --target web-wasm-webgl2
source .deps/emsdk/emsdk_env.sh
cmake --preset poc02-web-release
cmake --build --preset poc02-web-release --parallel
cd pocs/ink_engine/playground/web
npm ci
npm run build
npm run test:e2e
```

Android builds use Gradle 8.11.1, JDK 17+, NDK `27.2.12479018`, API 26+, and
CMake 3.30.5:

```bash
cd pocs/ink_engine/platform/android
gradle :app:assembleDebug -PcanvasPocAbi=arm64-v8a --no-daemon
gradle :app:assembleDebug -PcanvasPocAbi=x86_64 --no-daemon
```

The normal activity uses the physical Surface size. Automated golden replay is
enabled explicitly with the `acceptance=true` Intent extra, which requests the
fixed 800×600 reference target; it is not a physical-device latency mode.

Windows is built by the POC-02 workflow with clang-cl 22.1.8 and the locked
D3D12 SDK. Correctness uses WARP; hardware pen/GPU evidence belongs to the Human
Ink Gate and is not inferred from WARP.

## Deterministic fixtures

| Fixture | Stroke digest | Document digest | Preview digest |
|---|---|---|---|
| `vector-pressure.ndjson` | `2ae509ac25da5ca82937044f740265e4` | `672abdc8604a169ecda4ce08e8d80b55` | `dfc99ad07efd6162efd8d2ece65e0319` |
| `dab-turn.ndjson` | `a9d4cf3e83a5f08b45621991271ec8c7` | `ae1726a94d60f3e10c2789034bbd9e36` | `efa865ba51000559f8c53d0febb539b1` |

The numeric boundary corpus digest is
`ee8615ae2159fc9f42faad6687b0cbd3` on native arm64 and must match native x64
and WASM before cross-platform acceptance passes.

Run either fixture ten times with:

```bash
out/poc02-host-debug/pocs/ink_engine/canvas_poc02_replay --repeat=10
out/poc02-host-debug/pocs/ink_engine/canvas_poc02_replay \
  --fixture=pocs/ink_engine/fixtures/dab-turn.ndjson --repeat=10
```

`vector-reference.rgba` and `dab-reference.rgba` are fixed 800x600 RGBA8888
sRGB raster references. Tests never update them. A deliberate baseline change
requires the reviewer-visible `canvas_poc02_golden --update-golden` action and
review of the resulting binary diff and digest changes.

## Evidence and acceptance status

The automated implementation report is in
[`docs/REPORT.md`](docs/REPORT.md). The pointer, queue, handoff, and POC-06
upstream contract is in
[`docs/POINTER_STROKE_CONTRACT.md`](docs/POINTER_STROKE_CONTRACT.md).

Automated correctness is necessary but does not satisfy physical writing
experience. Windows, Web, and Android device runs, application-level latency,
frame pacing, recordings, and subjective rubric are intentionally still
**Pending** in [`docs/HUMAN_INK_GATE.md`](docs/HUMAN_INK_GATE.md). POC-02 must
remain `Validating` until those reports are completed and reviewed.
