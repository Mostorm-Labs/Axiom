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

[`deps.lock.json`](../../../deps.lock.json) is the source of truth for Skia,
Emscripten/LLVM, Windows LLVM, Node, Web packages, GoogleTest, JSON, xxHash,
Android NDK, and the Roboto fixture. Dependencies are materialized only below
the ignored `.deps/` directory.

```sh
python3 tools/bootstrap_deps.py --core --font-only
python3 tools/bootstrap_deps.py --skia --sync-skia
```

`--github-api-archives`, `--raw-core-fallback`, and `--skia-archive` exist only
for development hosts whose HTTPS Git transport is blocked. Immutable commit
markers are still verified. CI uses normal Git checkouts.

Skia is built separately with official GN/Ninja:

```sh
python3 tools/build_skia.py macos
python3 tools/build_skia.py ios
python3 tools/build_skia.py ios-simulator
python3 tools/build_skia.py web
python3 tools/build_skia.py windows --cc clang-cl --cxx clang-cl
python3 tools/build_skia.py android --cpu arm64 --ndk "$ANDROID_NDK_ROOT"
python3 tools/build_skia.py android --cpu x64 --ndk "$ANDROID_NDK_ROOT"
```

## 3. Fixture and semantic acceptance

The reviewed fixture is fixed at 800×600, DPR 1, sRGB, single-sample, with a
light gray background. It contains a blue Rect, a generated 64×64 two-color
checker PNG, a cubic VectorPath, and `Canvas v2` using the locked Roboto file.
The seven-record replay also moves the Rect and creates/deletes a temporary
Rect. The fixed VectorPath uses binary (non-AA) coverage in this POC so D3D12,
WebGL2, Metal, GLES3, and CPU raster do not spend the visual tolerance budget
on backend-specific edge kernels; this is not a V1 rendering-quality policy.

The reviewed semantic digest is:

```text
47826449b895ac4f4a57b4f386379775
```

Every platform uploads a result JSON containing that exact digest. The final
acceptance job refuses missing or duplicate platform records, a single byte of
digest drift, anything other than 100 lifecycle iterations and a 60-second
smoke, an empty smoke, or a reported frame above 100 ms.

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

Only the Skia raster tool may create the baseline, and the update flag is
mandatory:

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

Build Skia/WASM with the `web-release` preset, copy assets with
`prepare_web_assets.py`, then run the Playwright Chromium/SwiftShader suite.
The generated JS/WASM is scanned for pthread, `SharedArrayBuffer`, and
COOP/COEP requirements. The browser suite recreates Runtime, Document, and
WebGL surface 100 times, then measures the 1,000-node scene for 60 seconds.
After a 60-frame warmup, the WASM heap must remain the same size for the entire
measured smoke.

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

## 7. Manual Windows benchmark bundle

The bundle must run native D3D12 and Chrome Stable Web on the same physical
Windows machine. It records DXGI vendor/device/driver, OS and browser versions,
Skia/toolchain commits, frame p50/p95/p99/max, peak memory, and hashes of every
binary, fixture, and result. WARP/SwiftShader correctness runs never satisfy
this hardware performance gate.

## 8. Exit checklist

- [ ] All six platform families clean-build from the lock file.
- [ ] Web, Windows, macOS, iOS, iPadOS, and Android upload the reviewed digest.
- [ ] Every GPU readback passes the 99.9%/±2 visual gate.
- [ ] Each platform passes 100 lifecycle iterations and a 1,000-node 60-second
      smoke without crash, sustained growth, or a frame over 100 ms.
- [ ] Web output contains no pthread/SharedArrayBuffer dependency.
- [ ] Windows and Web physical benchmark bundle is archived.
- [ ] Runtime sources contain no HWND, D3D12, Emscripten, DOM, Metal, UIKit,
      Android, EGL, or JNI types.
