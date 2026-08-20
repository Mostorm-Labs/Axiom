# POC-03 Integrated Ink validation — 2026-08-19

Status: **Host matrix and Pixel 7 Android integrated lane passed; POC-03 remains `Validating`**.

This report records the first integrated validation of the POC-02 Ink contract
inside the POC-03 Scene/FrameGraph/TileCache path. Pixel 7 subsequently closed
its four-scale physical lane. Windows D3D12 and Chrome Stable WebGL2 evidence
was later collected separately: Windows failed its hard frame gate twice and
the Web lane had an initial failure followed by a bounded passing rerun. See
[the dedicated Windows/Web report](integrated-windows-web-physical-20260819.md).
The immutable pre-integration physical report remains
[physical-validation-20260818.md](physical-validation-20260818.md).

## Reproducibility identity

| Field | Value |
|---|---|
| Source commit | `de6ce74705cd445654c86d84e06ec68830080f9d` (`test: measure Android memory after warmup allocations`) |
| POC-02 contract commit | `35a02f8` |
| Branch | `codex/poc-03-100k-scene` |
| Generator | algorithm 1, seed `0x43414e5641533033` |
| Scales | 1K/200, 10K/2K, 50K/10K, 100K/20K base nodes/historical Strokes |
| Input fixture | 16 samples per Stroke, four batches of four samples, fixed pressure/time sequence |
| Host backend | C++20 host core; no Skia source or platform SDK consumed |
| Platform Skia identity | Skia `b6d106297ff9ef2ff8094033695d045e87775581`; Android SDK ID `630e4536e8eaf7ee71a81e29880a934563fbae5d87a9aaef5c2c5a19b544c80f` |

All historical and live Strokes use the real
`PointerSampleBatch → InputRouter → StrokeSession → AddStrokeOperation` path.
POC-03 stores only the stable Stroke ID, resource key, bounds, color, and
revision. Select/Drag updates an ordinary non-Stroke node.

## Host integrated matrix

| Base nodes | Historical Strokes | Ink digest | Document/Scene digest | Max candidates | Records touched | Full fallbacks | Cache invalidations | Handoff frames | Runtime MiB | Result |
|---:|---:|---|---|---:|---:|---:|---:|---:|---:|---|
| 1,000 | 200 | `78cfbfef60ea606b77b8bb9753a22733` | `992b9776596d824d8ce2ea4e63ff278c` | 130 | 1 | 0 | 2 | 1 | 0.533 | pass |
| 10,000 | 2,000 | `3c67a77040df4b290e139b319f8ec69f` | `07b91f337ce46a2496badcf43498df61` | 770 | 1 | 0 | 2 | 1 | 4.933 | pass |
| 50,000 | 10,000 | `1816f3b03a083098ef4e96ef8340d52f` | `3d6365692eb24487fb70819805d8d964` | 3,524 | 1 | 0 | 2 | 1 | 24.453 | pass |
| 100,000 | 20,000 | `0bc9cc0db2e708bac474b23f8a52db71` | `002e438d90f6ae775e60eb41bda93735` | 4,805 | 1 | 0 | 2 | 1 | 48.895 | pass |

Every row executed `pan`, `zoom`, `write-vector`, `write-dab`, `select`, and
`drag`. The incremental Scene digest matched a same-revision full compile;
the preview-to-canonical handoff had no blank or double-dark frame in the
automated controller trace. Queue and pending-callback bounds remained within
the configured limits.

## Pixel 7 Android integrated lane

The physical Android lane was run on a Pixel 7 (Android 17, GS201, arm64-v8a)
at 2400x1080 landscape, DPR 2.625 and 90 Hz. The APK was built from commit
`de6ce74705cd445654c86d84e06ec68830080f9d`; its SHA-256 is
`fe4f6fc8ef6bcb0f03cb83a933c45104efb363d352553ada5ddceee5c0e49fd3`.
The APK zip alignment check passed and the native library `PT_LOAD` alignment
was `0x4000` (16 KiB) for every segment.

| Base nodes | Historical Strokes | P50 ms | P95 ms | P99 ms | Max ms | Missed presentations | Max candidates | Peak VmHWM MiB | Steady RSS growth | Correctness |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 1,000 | 200 | 11.042 | 13.892 | 14.647 | 123.883 | 271 | 96 | 224.445 | 4.916% | pass |
| 10,000 | 2,000 | 10.965 | 13.582 | 15.637 | 117.962 | 243 | 578 | 232.016 | 4.751% | pass |
| 50,000 | 10,000 | 10.033 | 12.976 | 21.978 | 71.256 | 127 | 2,018 | 268.988 | 3.948% | pass |
| 100,000 | 20,000 | 9.531 | 13.725 | 24.951 | 44.473 | 101 | 2,498 | 314.398 | 3.988% | pass |

The 100K/60 second run rendered 5,400 frames in 62,263.318 ms: p50 9.544 ms,
p95 13.928 ms, p99 25.552 ms, max 70.430 ms, 832 missed presentations,
and peak VmHWM 314.070 MiB (warmup end RSS 289.500 MiB, end RSS 301.133 MiB,
4.018% steady growth). Android does not add an absolute frame-time gate in this
POC; these values are retained as device evidence. All four scales and the
long run passed the ≤5% post-warmup memory gate, full/incremental equivalence,
visual equivalence, and candidates ≤5,000.

The manual 100K action pass used the native Android SurfaceView and exercised
PAN, VECTOR write, DAB write, SELECT and SELECT drag. The captured log contains
84 `INK_PREVIEW`, 2 `INK_CANONICAL_VISIBLE`, 1 `SELECTED`, 40
`SELECT_DRAGGING` and 1 `SELECT_END` events. The process remained alive; no crash, SIGSEGV, abort or
ANR was observed. The post-action screenshot and sanitized log are retained as
local artifacts (not committed):

| Artifact | SHA-256 |
|---|---|
| `pixel7-final-after-actions.png` | `de09cf72c18cf98397424d4645f81aa8b0000cfe0cea2751a59d9fe8e0a70ad0` |
| `pixel7-final-after-actions-logcat.txt` | `24992cc2faefb21f8260497527081d2a60836f4ad993f5d1292612ff1e2892e4` |

## Gate disposition

Passed locally:

- POC-03 host Debug: 26/26 tests.
- POC-03 host ASan/UBSan: 26/26 tests.
- POC-02 host contract: 25/25 tests.
- Four-scale integrated replay, including Vector/Dab history and live writes.
- `maximum_candidates <= 5,000`, one Scene record touched per Stroke, zero
  full fallbacks, full/incremental equivalence, and one-frame handoff.
- `git diff --check`, Markdown validation, CI-boundary validation, Python
  syntax, YAML parsing, and Web TypeScript type-check.

Still pending and intentionally not claimed here:

- Windows physical D3D12 integrated playground at 1K/10K/50K/100K. The first
  evidence bundle has now been collected and failed the Windows p95/p99 gate
  twice; see [the dedicated Windows/Web report](integrated-windows-web-physical-20260819.md).
- Chrome Stable WebGL2 integrated playground at 1K/10K/50K/100K. Its bounded
  rerun passed, but the initial run failed and the lane remains under review.
- The POC-02 formal pressure-pen latency and Human Ink Gate.

The Windows shell now accepts native `WM_POINTER` select/drag messages and
uses pointer capture. The Web and Android drag paths invalidate TileCache from
the compiler's authoritative dirty bounds rather than assuming an optional
hint exists. These are correctness hardening changes, not physical evidence.

## Raw artifacts

The JSON files below are ignored local outputs and are not committed. Their
hashes make a later device run auditable without checking device data into the
repository.

| Scale | Local artifact | SHA-256 |
|---:|---|---|
| 1K | `out/poc03-integrated-host/result-1000.json` | `59a870340290fdbbf93fa5448771bf9471877804341f0545ebe5c07c21f674a7` |
| 10K | `out/poc03-integrated-host/result-10000.json` | `7f7d10567378f1a5cdb54a0260c04eb98a7cc44bc4346cfc5f38009697805466` |
| 50K | `out/poc03-integrated-host/result-50000.json` | `ac573d38a1ad75d009f0e819c1012b5112be77d71d90ceca82ac30fdfef0317f` |
| 100K | `out/poc03-integrated-host/result-100000.json` | `b6d53441f03390af232b7ef5e0a02f6835dc002d2cfcaa27113cd6d574397515` |

## Reproduction

```bash
cmake --preset poc03-host-debug
cmake --build --preset poc03-host-debug --parallel 4
ctest --preset poc03-host-debug --output-on-failure

cmake --preset poc03-host-asan
cmake --build --preset poc03-host-asan --parallel 4
ctest --preset poc03-host-asan --output-on-failure

cmake --preset poc03-host-release
cmake --build --preset poc03-host-release --parallel 4
mkdir -p out/poc03-integrated-host
for nodes in 1000 10000 50000 100000; do
  out/poc03-host-release/pocs/large_scene/canvas_poc03_integrated_benchmark \
    --nodes="$nodes" --output="out/poc03-integrated-host/result-$nodes.json"
done
```

POC-03 remains `Validating` until the three pending integrated physical lanes
produce sanitized evidence bundles and the existing POC-02 status remains
`Integration Ready / Validating`.
