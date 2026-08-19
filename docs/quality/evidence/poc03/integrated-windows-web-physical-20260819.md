# POC-03 Integrated Ink Windows/Web physical gate — 2026-08-19

Status: **Windows gate failed; POC-03 remains `Validating`**.

This report records the Integrated Performance Playground workload on the
registered physical Windows machine. It is separate from the immutable
pre-integration baseline in
[physical-validation-20260818.md](physical-validation-20260818.md). The
workload includes the real POC-02 Ink path and the POC-03 Scene/FrameGraph/
TileCache integration. It does not change POC-02's independent pressure-pen
latency or Human Ink Gate status.

## Reproducibility identity

| Field | Value |
| --- | --- |
| Source commit | [`01d2bcb4b80fac2271d1b6ee6a3482054ae46cf0`](https://github.com/Mostorm-Labs/canvas/commit/01d2bcb4b80fac2271d1b6ee6a3482054ae46cf0) |
| Branch | `codex/poc03-windows-integrated-physical` |
| Workload | 1K/10K/50K/100K, pan/zoom/write-vector/write-dab/select/drag |
| Windows | Windows 10 Pro 19045 |
| CPU/GPU | Intel Core i5-10400 / Intel UHD Graphics 630 |
| Driver | 31.0.101.2111 |
| Browser | Chrome Stable 151.0.7922.138 |
| Display | 1920×1080, approximately 59.933 Hz |
| Bundle SHA-256 | `4c96441bbf74d9924dcc47a42405e943c273b75cf6e32c2a1b33f49ccf3f4ee0` |

## Results

### Windows Native D3D12

The 100K/60-second run was repeated twice. Both runs failed the existing
Windows hard gate of p95 ≤ 16.7 ms and p99 ≤ 33.3 ms:

| Run | p50 (ms) | p95 (ms) | p99 (ms) | max (ms) | Missed |
| --- | ---: | ---: | ---: | ---: | ---: |
| Initial | 3.450 | 26.998 | 38.737 | 52.234 | 555 |
| Repeat | 3.386 | 25.693 | 37.457 | 52.942 | 550 |

The failure is reproducible. Native process peak working set was approximately
219 MB. It is not valid to close this failure by lowering the threshold or by
using host/headless timing as a substitute.

### Chrome WebGL2

The first 60-second run failed with p95/p99 `26.1/53.7 ms`. A bounded rerun
passed with p50/p95/p99/max `16.7/18.1/25.7/37.0 ms` and 41 missed
presentations. The bounded rerun passed both the repository Web gate
(p95 ≤ 20 ms, p99 ≤ 40 ms) and the observed 25/50 ms diagnostic gate, but the
initial failure remains part of the evidence and must be explained before the
lane is considered stable.

Web WASM linear memory was 128 MB without growth. Correctness, shared digest,
full/incremental equivalence, visual equivalence and maximum-candidate limits
passed on both paths.

## Architectural disposition

The evidence separates semantic correctness from production frame scheduling:

- Document/Ink/Scene digest and visual/candidate oracles pass.
- Windows Native D3D12 frame tail latency fails twice on the same device.
- The result is therefore an architecture risk for the current direct-render,
  Linear/Uniform-Grid and L1-cache baseline, not evidence that the Document
  model is incorrect.

Before a new acceptance attempt, trace candidate query, RenderScene/FrameBuilder,
Damage/Tile invalidation, raster queue age, Skia draw/flush, GPU submit, present
interval, missed presentation and categorized memory. Feed the result into
RF-01 Scene/RenderScene/Damage, RF-02 Dynamic Spatial Query and RF-03 Tiled
Raster/Scheduling in [STAGED_DELIVERY_PLAN.md](../../../planning/STAGED_DELIVERY_PLAN.md).
The ownership boundary is fixed by
[ADR-0021](../../../adr/0021-render-scene-spatial-index-tiling-boundaries.md).

POC-03 remains **Validating**. No `Accepted` state is claimed, and no Windows
or MSVC build is performed on the macOS development host.
