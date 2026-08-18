# POC-01 Windows/Web physical revalidation — 2026-08-18

Status: **Passed** (supplemental physical evidence). This report closes the
previously identified Windows working-set-series and Windows/Web environment
metadata evidence gaps. It does not erase the retained 2026-08-17 bundle, does
not resolve the separately retained macOS failures, and does not change POC-01
from `Validating` to `Accepted`.

Windows Native D3D12 and Chrome Stable WebGL2 were clean-built and measured on
the same physical Windows machine with the revised `run_bundle.ps1`. The first
and only collection attempt passed. Raw RGBA readbacks, expected/actual/diff
PNGs, structured benchmark output, the complete Windows working-set series,
the environment snapshot, and per-file hashes are retained together in one
content-addressed prerelease asset:

- Release: [POC-01 Windows/Web revalidation 2026-08-18](https://github.com/Mostorm-Labs/canvas/releases/tag/poc01-windows-web-revalidation-20260818-a11899ca)
- Asset: `poc01-windows-web-revalidation-20260818-a11899ca.zip`
- Asset size: 40,863 bytes
- Asset SHA-256: `a11899ca261429ab0890db6b9fd5a39cd904150caf7dac762e5d2470c4e10549`
- GitHub asset digest: `sha256:a11899ca261429ab0890db6b9fd5a39cd904150caf7dac762e5d2470c4e10549`
- Archive members: 15; every member except the intentionally self-excluded
  `artifact-hashes.json` matches its recorded byte count and SHA-256

## Locked identity

| Item | Value |
| --- | --- |
| Runtime source commit | `5ab8b16bdac8f982a9d221d1f48d3867dda7b43c` |
| Revised physical harness commit | `06b61f3c7506f93d380765cdcaca59637bdf2644` |
| Runtime core diff between those commits | 0 files below `pocs/shared_engine/src` and `pocs/shared_engine/include` |
| Skia commit | `b6d106297ff9ef2ff8094033695d045e87775581` |
| Windows Skia SDK ID | `7022f980c80e4786d8f883103552952c0aa27d8fdab9fd0c555850156ac3eec4` |
| Web Skia SDK ID | `394a46256239b3a0a332e8c514205759d22ca4a6fff27b51eb113e721e5f3c6e` |
| `skia-sdk.lock.json` SHA-256 | `847284704af4bcc0fea113c4046bd4832d52a07cfc00ead26b7f3d3b8ffd5361` |
| Fixture manifest SHA-256 | `77d4dacd86e174effa51347f65401a4a605e3222ece6de203e026461e338af26` |
| Fixture replay SHA-256 | `110a9572d54864ae913e28d91a5e392135e22c1dbedc06a88db6d7b425d25724` |
| Fixture checker SHA-256 | `10ee6bb34dfe7ba4d866c1bc7cb828a045ba48e97c971e2ca8df05f66df99f59` |
| Locked Roboto SHA-256 | `466989fd178ca6ed13641893b7003e5d6ec36e42c2a816dee71f87b775ea097f` |
| Golden SHA-256 | `1b1e4a77a213515469b094ccb77b43be5c75fa7f1d2382f38583ed8aaab51041` |
| Native executable SHA-256 | `7ee99c15067bb830ff35e8d4257c131a46249026fa220939dcb4c22ffcdeabf9` |
| Web JavaScript SHA-256 | `a3356d1d90f44bc6ec08edda99036a1b37dbe36e7810417df57e4a95c822e357` |
| Web WASM SHA-256 | `f9141961cf648debbf2555fbc2c374889330088fb2abb7f7f5cfab92832464ca` |

## Test environment

| Item | Observation |
| --- | --- |
| Test window | 2026-08-18 13:09:58–13:12:24, Asia/Shanghai |
| OS | Windows 10 Pro 10.0.19045, build 19045 |
| CPU | Intel Core i5-10400, 6 cores / 12 logical processors |
| Hardware GPU | Intel UHD Graphics 630, device `8086:9BC8` |
| GPU driver | 31.0.101.2111 |
| Chrome Stable | 151.0.7922.138 |
| Chrome WebGL renderer | ANGLE, Intel UHD Graphics 630, D3D11; not SwiftShader |
| LLVM | 22.1.8 (`ca7933e47d3a3451d81e72ac174dcb5aa28b59d1`) |
| MSVC toolset / Windows SDK | 14.51.36231 / 10.0.26100.0 |
| Emscripten / collector Node | 6.0.6 / 24.19.0 |
| Thermal | Unavailable: firmware exposes no `MSAcpi_ThermalZoneTemperature` value |
| Power mode | Observed: High performance (`8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c`) |
| Current refresh | Observed: 59 Hz through `Win32_VideoController.CurrentRefreshRate` |
| Supported refresh modes | Unavailable: display driver exposes no WMI source modes |
| Target frame interval | Explicitly unbounded submit loop; this POC measures draw/submit throughput, not presentation cadence |
| VRR | Unavailable: this POC has no public user-mode active-VRR query |
| Chrome throttling | Disabled by three command-line flags; page remained `visible` and focused |

Unavailable observations include their query method and reason in the
structured report and raw `device.json`; they are recorded as unavailable, not
silently omitted.

## Results

Both paths passed digest `47826449b895ac4f4a57b4f386379775`, the complete
core-conformance oracle, 100 lifecycle iterations, a 60-second 1,000-node
smoke, and the unchanged 100 ms maximum-frame gate.

| Path | Hardware proof | Frames | p50 | p95 | p99 | max | Peak memory |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Windows Native D3D12 | `warp=false`, Intel `8086:9BC8` | 53,393 | 1.0557 ms | 1.6624 ms | 1.8074 ms | 28.0954 ms | 128,286,720 bytes process peak working set |
| Chrome WebGL2 | Intel ANGLE/D3D11; no software renderer | 3,597 | 1.3000 ms | 1.8000 ms | 2.0000 ms | 3.9000 ms | 14,714,067 bytes renderer JS heap peak |

The generated Web JS/WASM scan passed as single-threaded and isolation-free.
The Web WASM heap remained exactly 17,170,432 bytes from post-warm-up start to
finish. Chrome reported no page errors.

## Windows working-set series

The revised native harness sampled process working set after the 60-frame GPU
warm-up, every five seconds during the 60-second smoke. The declared decision
rule requires at least 10 samples spanning at least 50 seconds and rejects a
last-quartile median more than 5% above the first-quartile median.

| Elapsed | Working set | Elapsed | Working set |
| ---: | ---: | ---: | ---: |
| 0 ms | 124,755,968 B | 35,000 ms | 97,009,664 B |
| 5,000 ms | 125,239,296 B | 40,000 ms | 97,042,432 B |
| 10,000 ms | 91,828,224 B | 45,000 ms | 97,026,048 B |
| 15,000 ms | 92,573,696 B | 50,000 ms | 97,390,592 B |
| 20,000 ms | 92,606,464 B | 55,001 ms | 98,201,600 B |
| 25,000 ms | 92,794,880 B | 60,000 ms | 98,238,464 B |
| 30,000 ms | 92,827,648 B |  |  |

| Analysis | Value |
| --- | ---: |
| Samples / span | 13 / 60,000 ms |
| First-quartile median | 108,664,832 B |
| Last-quartile median | 97,796,096 B |
| Tail growth | -10,868,736 B (-10.0021%) |
| Allowed tail growth | +5% maximum |
| Decision | **Passed — no sustained growth observed** |

This is a bounded POC leak signal, not a product memory budget.

## Visual gate

| Path | Pixels within ±2 | Matching ratio | Maximum channel delta | Result |
| --- | ---: | ---: | ---: | --- |
| Windows Native D3D12 | 479,917 / 480,000 | 99.982708% | 184 | Passed |
| Chrome WebGL2 | 479,952 / 480,000 | 99.990000% | 184 | Passed |

The gate requires at least 99.9% of pixels to have every channel within ±2.
The maximum delta is retained for completeness and occurs only among permitted
outlier pixels.

## Reproduction

The commands below reproduce the clean builds and physical collection from
harness commit `06b61f3c7506f93d380765cdcaca59637bdf2644`. Locked dependencies
and SDKs must pass repository SHA-256 and manifest checks before use.

```powershell
python tools/bootstrap_deps.py --core --windows-llvm --web --node
python tools/skia/fetch.py --target windows-x64-d3d12
python tools/skia/fetch.py --target web-wasm-webgl2

cmake --preset windows-release --fresh `
  -DCMAKE_C_COMPILER="$PWD/.deps/llvm/bin/clang-cl.exe" `
  -DCMAKE_CXX_COMPILER="$PWD/.deps/llvm/bin/clang-cl.exe"
cmake --build --preset windows-release --target clean
cmake --build --preset windows-release --parallel
ctest --preset windows-release --output-on-failure

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
  -OutputDirectory out/poc01-windows-web-revalidation-20260818-130904 `
  -DurationSeconds 60 `
  -RuntimeCommit 5ab8b16bdac8f982a9d221d1f48d3867dda7b43c
```

The revised bundle completed once without a failed attempt or rerun. The raw
archive remains external to Git. A later aggregate review may cite this report
to close the Windows-specific gaps; the separately retained macOS frame and
memory failures still keep POC-01 `Validating`.
