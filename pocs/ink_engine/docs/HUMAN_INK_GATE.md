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

## Sign-off

POC-02 may move from `Validating` to `Accepted` only when all three reports are
complete, the quantitative gates pass, subjective observations are linked to
evidence, and every failure has a classified disposition. CI success alone must
not change this status.
