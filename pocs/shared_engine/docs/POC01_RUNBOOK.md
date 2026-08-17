# POC-01 Shared Engine Runbook

> Status: Experimental implementation. None of the ABI, replay, scene, or
> platform adapter types in this POC carry compatibility promises into R1.

## 1. Proof boundary

POC-01 proves that the same single-threaded C++20 `Document`, `Operations`,
`SceneCompiler`, canonical encoder, and C bridge can run on six platform
families without semantic forks.

| Validation target | Shell/harness | Ganesh backend | Runtime boundary |
| --- | --- | --- | --- |
| Web | React/TypeScript demo | WebGL2 | WASM exports |
| Windows | Win32 visible/offscreen demo | D3D12, WARP in CI | C ABI |
| macOS | Native command-line harness | Metal | C ABI + ObjC++ adapter |
| iOS | Universal native runner on iPhone simulator/device | Metal | C ABI + ObjC++ adapter |
| iPadOS | Same universal runner on iPad simulator/device | Metal | C ABI + ObjC++ adapter |
| Android | Native `SurfaceView`/`CanvasView` | OpenGL ES 3 | JNI; no JS data path |

The first product shells are still Web, Windows, and Android. POC-01 does not
choose a macOS/iOS/iPadOS product UI framework. It also excludes Ink,
RichText editing, persistence, collaboration, Android RN integration, Tauri,
pthread, and a production ABI.

## 2. Locked environment

[`deps.lock.json`](../../../deps.lock.json) locks source/tool dependencies, and
[`skia-sdk.lock.json`](../../../skia-sdk.lock.json) locks the seven immutable
prebuilt Skia SDK assets. Dependencies are materialized only below the ignored
`.deps/` directory. Ordinary Canvas builds never check out Skia source or run
GN/Ninja for Skia.

```sh
python3 tools/bootstrap_deps.py --core
python3 tools/skia/fetch.py --target macos-arm64-metal
```

Use the matching target ID for Web, Windows, iOS/iPadOS device or simulator,
and either Android ABI. `CANVAS_SKIA_SDK_BASE_URL` may select a mirror with the
same `<tag>/<asset>` layout; all lock and manifest checks remain mandatory.
CMake consumes only `CanvasSkia::Skia` from `CANVAS_SKIA_SDK_ROOT`, fails with
the exact fetch command when absent, and has no automatic source fallback.

Skia source and GN/Ninja are restricted to the Producer workflow documented in
[`SKIA_SDK_SUPPLY_CHAIN.md`](../../../docs/architecture/SKIA_SDK_SUPPLY_CHAIN.md).
Producer maintenance uses the seven profile targets; these commands are not a
Canvas build prerequisite. A manual Producer run from `main` is the only
publication path, and `update_lock.py` is the only supported lock update path:

```sh
python3 tools/skia/update_lock.py --tag <immutable-prerelease-tag>
```

The current lock points to
`skia-sdk-poc01-minimal-v1-debcbb7b9376806c`. This Consumer cutover does not
change POC-01 from `Validating` or waive either physical-device report.

## 3. Fixture and semantic acceptance

The reviewed fixture is fixed at 800×600, DPR 1, sRGB, single-sample, with a
light gray background. It contains a blue Rect, a generated 64×64 two-color
checker PNG, a cubic VectorPath, and `Canvas v2` using the locked Roboto file.
The seven-record replay also moves the Rect and creates/deletes a temporary
Rect. The fixed VectorPath and pinned-Roboto Text use binary (non-AA) coverage
with disabled font hinting in this POC so D3D12, WebGL2, Metal, GLES3, and CPU
raster do not spend the visual tolerance budget on backend-specific edge
kernels; this is a fixture determinism rule, not the V1 RichText rendering
policy.

The reviewed semantic digest is:

```text
47826449b895ac4f4a57b4f386379775
```

Every platform uploads a result JSON containing that exact digest. The final
acceptance job refuses missing or duplicate platform records, a single byte of
digest drift, anything other than 100 lifecycle iterations and a 60-second
smoke, an empty smoke, or a reported frame above 100 ms.
The same result contains the versioned `core_conformance` oracle. It covers
canonical signed zero, subnormal and rounding boundaries, maximum finite input,
non-finite/overflow rejection with transaction atomicity, and equivalent replay
into two independent empty Documents. The final job compares the complete
object, including result/error digest, revision, and operation sequence, across
native x64, native arm64, and WASM; visual tolerance never applies to this
semantic oracle.
GPU-backed smoke runs render 60 unmeasured warmup frames before the 60-second
window so shader and pipeline creation are not misreported as steady-state
frame time. RuntimeScene is rebuilt only when the Document revision changes.
The measured loop covers draw plus GPU command submission but excludes both a
CPU wait and the RGBA artifact readback; the reviewed readback runs once before
the smoke and synchronizes the backend. Every measured submission remains
subject to the 100 ms ceiling.
Web uses `requestAnimationFrame`, and Apple Metal uses an equivalent 60 Hz
submission cadence after the warmup drain. This prevents an asynchronous
software/simulator queue from being saturated while keeping scheduling delay
outside the measured Runtime draw/submit interval.

## 4. Host-core development

```sh
python3 tools/bootstrap_deps.py --core --font-only
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
out/host-debug/pocs/shared_engine/canvas_poc01_cli --lifecycle=100 --smoke=60
```

The host CLI uses a dependency-free software probe only for core lifecycle and
smoke diagnostics. It is not a visual golden renderer and does not create a
macOS product target.

## 5. Golden ownership and visual gate

Only the Skia raster tool built against the locked SDK may create the baseline,
and the update flag is mandatory:

```sh
out/macos-release/pocs/shared_engine/platform/canvas_poc01_golden \
  --update-golden \
  --output=pocs/shared_engine/goldens/reference.rgba
```

Tests never update the baseline. Platform readbacks are evaluated with:

```sh
python3 pocs/shared_engine/tools/visual_compare.py \
  --expected pocs/shared_engine/goldens/reference.rgba \
  --actual out/results/platform-actual.rgba \
  --artifacts out/results/visual \
  --backend ganesh-metal \
  --skia-commit b6d106297ff9ef2ff8094033695d045e87775581
```

At least 99.9% of pixels must have every channel within ±2. A failure produces
`expected.png`, `actual.png`, `diff.png`, and `metrics.json` with backend and
Skia revision.

## 6. Platform commands

### Web

Fetch `web-wasm-webgl2`, build WASM with the `web-release` preset, copy assets with
`prepare_web_assets.py`, then run the Playwright Chromium/SwiftShader suite.
The generated JS/WASM is scanned for pthread, `SharedArrayBuffer`, and
COOP/COEP requirements. The browser suite recreates Runtime, Document, and
WebGL surface 100 times, then measures the 1,000-node scene for 60 seconds.
The warmup and measured smoke are paced by `requestAnimationFrame` so the test
models a browser frame loop instead of saturating an unbounded SwiftShader
submission queue. After 60 warmup frames and an untimed synchronization, the
WASM heap must remain the same size for the entire measured smoke; each Runtime
draw/submit call is still independently subject to the 100 ms ceiling.

### Windows

```powershell
canvas_poc01_windows.exe --offscreen --lifecycle=100 --smoke=60 `
  --output=windows-actual.rgba
```

Offscreen CI selects D3D12 WARP. `--hardware` selects the first high-performance
DXGI adapter and records vendor, device, and driver identifiers. Omitting
`--offscreen` opens the visible Win32 harness.

### macOS / iOS / iPadOS

macOS runs `canvas_poc01_macos_runner --lifecycle=100 --smoke=60`. The iOS
universal bundle is clean-built for arm64 devices, then installed once on an
iPhone simulator and once on an iPad simulator. Each simulator executes the
same 100/60 gate and writes `poc01-result.json` and `apple-actual.rgba` to its
application Documents container. Physical-device reports are required before
acceptance; simulator results are correctness evidence only.

### Android

The Gradle demo builds the C++ target with the locked NDK. `CanvasPocView`
loads the three assets and runs the 100/60 gate on a worker thread through JNI
and an EGL ES3 surface; the high-frequency rendering data path never enters
JavaScript. CI clean-builds arm64 and executes the x86_64 emulator/SwiftShader
artifact. A physical-device report remains a separate acceptance gate.

The reviewed iPhone, iPad, and Android physical-device report is recorded in
[`mobile-physical-2026-08-17`](../reports/poc01/mobile-physical-2026-08-17/README.md).
It binds the structured results to the tested source commit, immutable Skia SDK
IDs, fixtures, golden, environment, reproduction commands, and an external
privacy-filtered evidence bundle. The structured report is committed; the raw
bundle is published as the content-addressed retained prerelease
[`poc01-mobile-physical-2026-08-17-62d1ae02ffaa15bd`](https://github.com/Mostorm-Labs/canvas/releases/tag/poc01-mobile-physical-2026-08-17-62d1ae02ffaa15bd),
and its manifest records the release, asset, target commit, and GitHub digest.
It closes no non-mobile gate and does not change POC-01 from `Validating`.

## 7. Manual Windows benchmark bundle

The bundle must run native D3D12 and Chrome Stable Web on the same physical
Windows machine. It records DXGI vendor/device/driver, OS and browser versions,
Skia/toolchain commits, frame p50/p95/p99/max, peak memory, and hashes of every
binary, fixture, and result. WARP/SwiftShader correctness runs never satisfy
this hardware performance gate.

Run the local Web demo and then collect both hardware paths into one bundle:

```powershell
$native = Resolve-Path out/windows-release/pocs/shared_engine/platform/windows/canvas_poc01_windows.exe
pocs/shared_engine/benchmarks/windows/run_bundle.ps1 `
  -NativeExe $native `
  -WebUrl http://127.0.0.1:4173 `
  -OutputDirectory out/results/windows-web-physical `
  -DurationSeconds 60 `
  -RuntimeCommit 5ab8b16bdac8f982a9d221d1f48d3867dda7b43c
```

The command refuses WARP, SwiftShader/software WebGL, digest drift, missing
percentiles or peak-memory fields, an incomplete 100/60 gate, and any frame
over 100 ms. Commit only the redacted report and structured summary; publish
the generated ZIP containing raw RGBA/log/diff evidence as a Release asset.
Both native D3D12 and Chrome WebGL2 readbacks and visual-diff directories must
be present in that single ZIP.

The reviewed result is archived in the
[Windows/Web physical report](../../../docs/quality/evidence/poc01/windows-web-physical-20260817.md)
and its content-addressed
[evidence Release](https://github.com/Mostorm-Labs/canvas/releases/tag/poc01-windows-web-physical-20260817-6a2bac2).

The combined decision for the shared Runtime, six-platform CI, mobile devices,
and Windows/Web machine is recorded in the
[POC-01 final gate audit](../reports/poc01/FINAL_GATE_AUDIT_2026-08-18.md).
The audit accepts the correctness evidence but keeps POC-01 `Validating` until
the listed Release-build, memory-series, and environment-metadata evidence is
captured. Historical subset reports retain the status they had when recorded.

## 8. Exit checklist

- [x] All six platform families clean-build from the lock file.
- [x] Web, Windows, macOS, iOS, iPadOS, and Android upload the reviewed digest.
- [x] Every GPU readback passes the 99.9%/±2 visual gate.
- [ ] Each platform passes 100 lifecycle iterations and a 1,000-node 60-second
      smoke in an eligible build without crash, sustained growth, or a frame
      over 100 ms. Android Release and missing memory series remain open.
- [x] Web output contains no pthread/SharedArrayBuffer dependency.
- [x] iPhone, iPadOS, and Android physical-device report is reviewed and
      committed with raw artifact hashes and reproduction commands.
- [x] The privacy-filtered mobile raw evidence bundle is published as the
      content-addressed Release asset bound by the report manifest and covered
      by the repository's no-overwrite/no-delete retention policy.
- [x] Windows and Web physical benchmark bundle is archived.
- [x] Runtime sources contain no HWND, D3D12, Emscripten, DOM, Metal, UIKit,
      Android, EGL, or JNI types.
- [ ] Physical performance manifests record thermal, power, refresh/frame
      interval, VRR, and throttling state (or explicit unavailability).
