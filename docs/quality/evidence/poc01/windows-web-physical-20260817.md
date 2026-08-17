# POC-01 Windows/Web physical-machine report — 2026-08-17

Status: **Passed** (physical evidence only; this report does not change POC-01
from `Validating` to `Accepted`).

Windows Native D3D12 and Chrome WebGL2 were clean-built and measured on the
same physical Windows machine. The raw RGBA images, build and benchmark logs,
JUnit output, expected/actual/diff PNGs, device snapshot, and per-file hashes
are published together as one Release asset:

- Release: [POC-01 Windows/Web physical report 2026-08-17](https://github.com/Mostorm-Labs/canvas/releases/tag/poc01-windows-web-physical-20260817-6a2bac2)
- Asset: `poc01-windows-web-physical-20260817-6a2bac2.zip`
- Asset size: 49,195 bytes
- Asset SHA-256: `de3c8347fbe5af8e656ed3918fbbf2cf5d1ac4dc346017600725d12f38612225`

## Locked identity

| Item | Value |
| --- | --- |
| Runtime source commit | `5ab8b16bdac8f982a9d221d1f48d3867dda7b43c` |
| Physical benchmark harness commit | `6a2bac2b6b8c43747c1c1b113495615e4c368866` |
| Runtime core diff between those commits | 0 files below `pocs/shared_engine/src` |
| Skia commit | `b6d106297ff9ef2ff8094033695d045e87775581` |
| `skia-sdk.lock.json` SHA-256 | `847284704af4bcc0fea113c4046bd4832d52a07cfc00ead26b7f3d3b8ffd5361` |
| Fixture manifest SHA-256 | `77d4dacd86e174effa51347f65401a4a605e3222ece6de203e026461e338af26` |
| Fixture replay SHA-256 | `110a9572d54864ae913e28d91a5e392135e22c1dbedc06a88db6d7b425d25724` |
| Fixture checker SHA-256 | `10ee6bb34dfe7ba4d866c1bc7cb828a045ba48e97c971e2ca8df05f66df99f59` |
| Locked Roboto SHA-256 | `466989fd178ca6ed13641893b7003e5d6ec36e42c2a816dee71f87b775ea097f` |
| Golden SHA-256 | `1b1e4a77a213515469b094ccb77b43be5c75fa7f1d2382f38583ed8aaab51041` |

## Test environment

| Item | Value |
| --- | --- |
| Test window | 2026-08-17 23:00–23:05, Asia/Shanghai |
| OS | Windows 10 Pro 10.0.19045, build 19045 |
| CPU | Intel Core i5-10400, 6 cores / 12 logical processors |
| GPU | Intel UHD Graphics 630, device `0x9BC8` |
| GPU driver | 31.0.101.2111 |
| Chrome Stable | 151.0.7922.138 |
| Chrome WebGL renderer | ANGLE, Intel UHD Graphics 630, D3D11 |
| LLVM | 22.1.8 (`ca7933e47d3a3451d81e72ac174dcb5aa28b59d1`) |
| MSVC toolset / Windows SDK | 14.51.36231 / 10.0.26100.0 |
| Emscripten / Node | 6.0.6 / 24.18.0 |

## Results

Both paths passed the reviewed digest
`47826449b895ac4f4a57b4f386379775`, the complete core-conformance oracle,
100 lifecycle iterations, a 60-second 1,000-node smoke, and the 100 ms
maximum-frame gate.

| Path | Hardware proof | Frames | p50 | p95 | p99 | max | Peak memory |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Windows Native D3D12 | `warp=false`, Intel vendor/device `8086:9BC8` | 47,606 | 1.1297 ms | 1.8789 ms | 2.1919 ms | 12.3209 ms | 128,311,296 bytes process peak working set |
| Chrome WebGL2 | Intel ANGLE/D3D11; no SwiftShader/software renderer | 3,597 | 1.4000 ms | 2.1000 ms | 2.3000 ms | 3.8000 ms | 14,909,534 bytes renderer JS heap peak |

The Web WASM heap remained 17,170,432 bytes from post-warm-up start to finish.
The generated JS/WASM scan found no pthread, `SharedArrayBuffer`, or isolation
dependency.

## Visual gate

| Path | Pixels within ±2 | Matching ratio | Maximum channel delta | Result |
| --- | ---: | ---: | ---: | --- |
| Windows Native D3D12 | 479,917 / 480,000 | 99.982708% | 184 | Passed |
| Chrome WebGL2 | 479,952 / 480,000 | 99.990000% | 184 | Passed |

The gate requires at least 99.9% of pixels to have every channel within ±2.
The maximum delta is reported for completeness and occurs only among the
allowed outlier pixels.

## Reproduction

The commands below were run from harness commit
`6a2bac2b6b8c43747c1c1b113495615e4c368866`. Locked dependencies and SDKs
must pass the repository SHA-256 and manifest checks before use.

```powershell
python tools/bootstrap_deps.py --core --windows-llvm
python tools/skia/fetch.py --target windows-x64-d3d12
cmake --preset windows-release --fresh `
  -DCMAKE_C_COMPILER="$PWD/.deps/llvm/bin/clang-cl.exe" `
  -DCMAKE_CXX_COMPILER="$PWD/.deps/llvm/bin/clang-cl.exe"
cmake --build --preset windows-release --target clean
cmake --build --preset windows-release --parallel
ctest --preset windows-release --output-on-failure

python tools/bootstrap_deps.py --web --node
python tools/skia/fetch.py --target web-wasm-webgl2
cmake --preset web-release --fresh
cmake --build --preset web-release --target clean
cmake --build --preset web-release --parallel
python pocs/shared_engine/tools/check_web_artifact.py `
  out/web-release/pocs/shared_engine/platform/web/canvas_poc01_web.js `
  out/web-release/pocs/shared_engine/platform/web/canvas_poc01_web.wasm
npm ci --prefix pocs/shared_engine/platform/web
npm run build --prefix pocs/shared_engine/platform/web

# Start the Web demo on http://127.0.0.1:4173, then:
$native = Resolve-Path out/windows-release/pocs/shared_engine/platform/windows/canvas_poc01_windows.exe
pocs/shared_engine/benchmarks/windows/run_bundle.ps1 `
  -NativeExe $native `
  -WebUrl http://127.0.0.1:4173 `
  -OutputDirectory out/results/poc01-windows-web-physical-20260817-6a2bac2 `
  -DurationSeconds 60 `
  -RuntimeCommit 5ab8b16bdac8f982a9d221d1f48d3867dda7b43c
```

The benchmark and both visual gates completed successfully. The first
in-process packaging attempt could not hash the externally-open transcript on
Windows; after the process exited, the unchanged result directory was hashed
and compressed offline. The Release ZIP and its `artifact-hashes.json` are the
final archived evidence.

Final POC-01 acceptance remains gated on the separately submitted mobile
physical report, all six CI platform records, and a later aggregate acceptance
report that cites both evidence packages and their SHA-256 values.
