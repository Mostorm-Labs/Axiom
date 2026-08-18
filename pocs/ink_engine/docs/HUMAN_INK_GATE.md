# POC-02 Human Ink Gate

Overall status: **Pending — no physical-device result is claimed**

This gate supplements automated deterministic, visual, lifecycle, and queue
tests. It must be run on representative physical Windows, Web, and Android pen
devices. WARP, SwiftShader, browser automation, mouse input, Android emulators,
and synthetic replays do not satisfy it.

## Required test protocol

Use the exact commit and artifact hashes recorded in each platform report. Keep
the canvas at the report's stated DPR/zoom/pan, warm up the renderer, then run
both Vector and Dab brushes through:

1. slow diagonal and horizontal strokes;
2. fast long flicks in both directions;
3. repeated sharp turns and zigzags;
4. clockwise and counter-clockwise circles;
5. low-to-high and high-to-low pressure ramps;
6. at least 60 seconds of continuous handwriting;
7. pointer cancel/interruption and immediate recovery;
8. resize/background/surface-recreation followed by continued writing.

For every action, correlate subjective notes with the same-run trace and, when
possible, high-frame-rate video. Record discontinuity, lag, wobble, over/under
prediction, visible correction, blank handoff, double-dark handoff, dropped
input, or failure recovery as named observations rather than a generic score.

The trace must include sample timestamp, Preview-visible timestamp, display
interval/presentation identifier when available, sample-to-visible frame count,
queue age, Preview revision, prediction correction magnitude, Canonical commit,
canonical-visible acknowledgement, missed presentation, and target generation.

Quantitative gates:

- application-level sample-to-Preview-visible p95 ≤ 16.7 ms and p99 ≤ 33.3 ms;
- report p50/p95/p99/max in both milliseconds and frame count;
- pointer-up to Canonical visible handoff ≤ 2 display frames;
- no silent input drop, sample reorder, blank frame, or double-dark frame;
- queue age remains bounded during continuous writing;
- a 30-second stroke has pointer-up Canonical commit p95 ≤ 16.7 ms.

The 16.7 ms baseline does not imply one frame on every refresh rate. A 90/120+
Hz device must be judged with its measured frame intervals and the subjective
rubric as well as absolute time.

## Windows physical device report

Status: **Pending**

| Field | Result |
|---|---|
| Device/model | Pending |
| OS/build | Pending |
| App commit/artifact SHA-256 | Pending |
| GPU/vendor/device/driver | Pending |
| Pen/firmware | Pending |
| Display resolution/refresh rate | Pending |
| Backend | D3D12 hardware; WARP is not accepted here |
| Brush/fixture versions | Pending |
| p50/p95/p99/max latency | Pending |
| p50/p95/p99/max frame count | Pending |
| Queue age p95/max | Pending |
| Prediction correction p95/max | Pending |
| Handoff p95/max frames | Pending |
| Missed presentation count | Pending |
| Slow/flick/turn/circle/pressure/continuous result | Pending |
| Cancel/recovery result | Pending |
| Trace/video links and hashes | Pending |
| Reviewer/date | Pending |

## Web physical device report

Status: **Pending**

| Field | Result |
|---|---|
| Device/model | Pending |
| OS/build | Pending |
| Chrome Stable version | Pending |
| App commit/artifact SHA-256 | Pending |
| GPU/vendor/device/driver | Pending |
| Pen/firmware | Pending |
| Display resolution/refresh rate/DPR | Pending |
| Backend | WASM + WebGL2, no pthread/SharedArrayBuffer requirement |
| Brush/fixture versions | Pending |
| p50/p95/p99/max latency | Pending |
| p50/p95/p99/max frame count | Pending |
| Queue age p95/max | Pending |
| Prediction correction p95/max | Pending |
| Handoff p95/max frames | Pending |
| Missed presentation count | Pending |
| Slow/flick/turn/circle/pressure/continuous result | Pending |
| Cancel/recovery result | Pending |
| Trace/video links and hashes | Pending |
| Reviewer/date | Pending |

## Android physical device report

Status: **Pending**

| Field | Result |
|---|---|
| Device/model | Pending |
| Android/build/BSP | Pending |
| APK commit/SHA-256 | Pending |
| SoC/GPU/driver | Pending |
| Pen/firmware | Pending |
| Display resolution/refresh rate/density | Pending |
| Backend | Native CanvasInkView → JNI/C++ → Ganesh GLES3 |
| Brush/fixture versions | Pending |
| p50/p95/p99/max latency | Pending |
| p50/p95/p99/max frame count | Pending |
| Queue age p95/max | Pending |
| Prediction correction p95/max | Pending |
| Handoff p95/max frames | Pending |
| Missed presentation count | Pending |
| Slow/flick/turn/circle/pressure/continuous result | Pending |
| Cancel/recovery result | Pending |
| Proof pen samples bypass RN JS | Pending |
| Trace/video links and hashes | Pending |
| Reviewer/date | Pending |

## Supplemental iPadOS Safari exploratory evidence

Status: **Correctness fixes verified; latency gate not met; not a qualifying
Web report**

This exploratory run used an iPad Air (4th generation), iPadOS 26.6, Apple
Pencil, and Safari on 2026-08-18. It supplements the required matrix but does
not complete the Web physical-device report because that report requires
Chrome Stable and the full trace fields listed above. The Playground currently
reports application-level `PointerEvent.timeStamp` to `requestAnimationFrame`
latency; it does not export presentation timestamps, frame counts, queue age,
handoff latency, or raw trace files.

The first run at commit `894419b97d7092a0dfe96686c91c80e630552800`
reproduced a permanent-in-page input stall after repeated two-finger
interleaving followed by Apple Pencil strokes. Refreshing the page restored
input. Two independent defects were then fixed:

- `dd8d4af5780eaada792329b7225d07d3a0304c5a` bound JS input to one owner
  `pointerId` and separated the Web adapter's active stroke from the pending
  Canonical-visible stroke. Ten repetitions of the original multi-pointer
  sequence no longer stalled input.
- `cbc5fd60e323988edc2aee7e2b01968131e14e0d` added a bounded handoff queue
  for complete strokes that arrive before the previous Canonical-visible
  acknowledgement. Fifty rapid Vector strokes and fifty rapid Dab strokes
  were all visible; no redraw was needed and no silent stroke drop was
  observed.

The final measured runtime was `cbc5fd6`; its Web WASM SHA-256 was
`40dc22d3dca6b4f615510bf377dcf151b262b6603d7ad520ca322b5c72d36e4a`
and its frontend bundle SHA-256 was
`40b57fc8bda3ae04b56b03d7e9cfc9dea0303186f4ccb4137fc7cf20d91001bd`.

| Brush/run | Correctness | p50 | p95 | p99 | Disposition |
|---|---|---:|---:|---:|---|
| Vector, 50 rapid strokes | 50/50 visible; no stall or silent drop | 16 ms | 51 ms | 81 ms | Fails p95 and p99 latency gates |
| Dab, 50 rapid strokes | 50/50 visible; no stall or silent drop | 8 ms | 38 ms | 48 ms | Fails p95 and p99 latency gates |

The operator reported Dab as subjectively more responsive than Vector. Before
the handoff queue fix, Vector also silently lost some rapid strokes during the
Canonical-visible window; that correctness failure invalidated the earlier
latency sample and is not treated as an accepted measurement. The remaining
tail-latency failure must be investigated with exportable per-sample and
presentation evidence before another qualifying device run.

## Sign-off

POC-02 may move from `Validating` to `Accepted` only when all three reports are
complete, the quantitative gates pass, subjective observations are linked to
evidence, and every failure has a classified disposition. CI success alone must
not change this status.
