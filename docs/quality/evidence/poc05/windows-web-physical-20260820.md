# POC-05 Windows/Web physical validation — 2026-08-20

Status: **Scoped evidence passed; POC-05 remains `Validating`.**

This is a same-machine Windows/Web evidence bundle for the
`codex/poc-05-hybrid-surface` baseline at `4b53540` (which contains the
previously pushed `5372a0d46ce99d4b543ab5c053e56b15d708b8c3` Android runner).
The evidence is recorded on validation commit
`f0c7d46624960ff1f946f96c138cbeccb51352bf`, after the branch's
Apple overlay validation commit was integrated.
It records the Windows stable Runtime C ABI host contract and a headed Chrome
Stable iframe/video harness. It does **not** claim that Windows RNW/Fabric,
Composition/D3D Canvas, or WebView2/video was tested: those adapters are not
present in this repository.

Machine details, hashes, commands, and raw-result paths are in
[windows-web-physical-20260820.json](windows-web-physical-20260820.json).

## Reproducibility identity

| Field | Value |
| --- | --- |
| Branch | `codex/poc-05-hybrid-surface` |
| Base commit | `5372a0d46ce99d4b543ab5c053e56b15d708b8c3` |
| Runtime source commit | `5ab8b16bdac8f982a9d221d1f48d3867dda7b43c` |
| Skia commit | `b6d106297ff9ef2ff8094033695d045e87775581` |
| Windows | Windows 10 Pro `10.0.19045`, 64-bit |
| CPU | Intel Core i5-10400, 6C/12T |
| Physical GPU/driver | Intel UHD Graphics 630 / `31.0.101.2111` |
| Chrome Stable | `151.0.7922.138` |
| Node / Playwright | `24.18.0` / `1.62.1` |
| Web test time | `2026-08-20T04:41:42.4580867Z` – `2026-08-20T04:41:51.7672018Z` |

## Windows host core

MSVC x64 Debug and Release builds both passed **8/8 CTest**. The checks cover
projection through the stable View/Surface C ABI, DPR/clip, stale-frame
rejection, focus/lifecycle, generation reset, malformed snapshot rejection,
and the separate POC-03 reserved `ExternalSurface` contract. Skia was disabled
because this is the stable C ABI placement core.

This is not a D3D12 or RNW run. The Windows RNW/WebView2/video gate is therefore
**Pending**, not passed. The Release executable SHA-256 is recorded in the JSON
bundle.

## Chrome Stable Web overlay

The installed Chrome Stable headed run passed **3/3** with hardware GPU
required. WebGL2 reported the physical Intel renderer through ANGLE D3D11,
not SwiftShader. The iframe was placed at `(108, 58, 200, 200)` CSS pixels
with one-pixel tolerance, below product UI (`z=22` versus `z=30`). The local
canvas-capture video reached `readyState=4`, played, and its visible clip was
`100x100` CSS pixels.

The 120 update samples measured p50 **16.50 ms**, p95 **18.90 ms**, p99
**20.40 ms**, max **22.70 ms**; every update was observed within one animation
frame (the harness allows two). The lifecycle case ran 100 register/unregister
iterations, checked failure placeholder and recovery, external-to-canvas focus
handoff, and generation reset with zero residual surfaces and zero active video
streams.

The Web result JSON, Playwright JSON, traces, screenshot, and Release test
binary are local raw artifacts. Their byte counts and SHA-256 values are listed
in the companion JSON so they can be archived as release assets without
placing binary telemetry in Git.

## Disposition and remaining gates

The scoped Windows host and Chrome DOM harness checks pass. Windows RNW/Fabric +
WebView2/video, Android RN/Fabric, and Apple RN/Fabric remain pending. The
formal Web Node 24.18.0 CI artifact is also pending. POC-05 therefore remains
`Validating` and is not marked `Accepted`.
