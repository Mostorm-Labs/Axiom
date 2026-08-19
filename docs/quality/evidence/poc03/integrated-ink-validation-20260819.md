# POC-03 Integrated Ink validation — 2026-08-19

Status: **Host matrix passed; POC-03 remains `Validating`**.

This report records the first integrated validation of the POC-02 Ink contract
inside the POC-03 Scene/FrameGraph/TileCache path. It does not close the
physical integrated gates: Windows D3D12, Chrome Stable WebGL2, and Pixel 7
must still run the same four-scale playground on their real targets. The
immutable pre-integration physical report remains
[physical-validation-20260818.md](physical-validation-20260818.md).

## Reproducibility identity

| Field | Value |
|---|---|
| Source commit | `4295fb73f31964d462b4540abede918786a08fd9` (`POC-03: integrate POC-02 Ink validation`) |
| POC-02 contract commit | `35a02f8` |
| Branch | `codex/poc-03-100k-scene` |
| Generator | algorithm 1, seed `0x43414e5641533033` |
| Scales | 1K/200, 10K/2K, 50K/10K, 100K/20K base nodes/historical Strokes |
| Input fixture | 16 samples per Stroke, four batches of four samples, fixed pressure/time sequence |
| Host backend | C++20 host core; no Skia source or platform SDK consumed |
| Platform Skia identity | Skia `b6d106297ff9ef2ff8094033695d045e87775581`; platform SDK IDs are captured when the pending physical integrated runs are produced |

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

- Windows physical D3D12 integrated playground at 1K/10K/50K/100K.
- Chrome Stable WebGL2 integrated playground at 1K/10K/50K/100K.
- Pixel 7 Android integrated playground at 1K/10K/50K/100K.
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
