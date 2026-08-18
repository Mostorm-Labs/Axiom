# POC-02 Automated Validation Report

Status: **Integration Ready / Validating**

Scope: deterministic core, operation replay, raster/GPU rendering, platform
builds, and automated shell acceptance

Physical Human Ink Gate: **Pending**

An iPadOS Safari + Apple Pencil exploratory run found and verified fixes for a
multi-pointer/visible-ack stall and rapid-stroke handoff drops. Its final
Vector and Dab p95/p99 application-level latency remained above the gate. The
device details, artifact hashes, measurements, and limitations are recorded in
[`HUMAN_INK_GATE.md`](HUMAN_INK_GATE.md); the required Windows, Chrome Web, and
Android reports remain Pending.

## Integration decision

POC-02 is **Integration Ready** for downstream experimental work. The shared
Pointer/Stroke/Preview contracts, deterministic Vector/Dab semantics,
operation-driven commit, pointer ownership, bounded Canonical handoff, and
cross-platform automated oracles are stable enough for POC-03 integrated scene
testing, POC-06 FastInk sinks, and R1 foundation work to consume.

This is not final acceptance. The POC remains **Validating** because its current
Web timing is not a presentation timestamp, Vector and Dab p95/p99 did not meet
the exploratory iPad latency gate, and the qualifying Windows, Chrome Web, and
Android physical-device reports are Pending. Historical-stroke scale behavior
also moves into the POC-03 integrated ink gate, where SpatialIndex, dirty
regions, and Raster/Tile cache can be measured together instead of forcing
POC-02 to solve a circular dependency.

## Implemented evidence

| Area | Evidence | Current result |
|---|---|---|
| C++20 core | Foundation, input, StrokeSession, Preview, Document, scheduler | Implemented |
| Operation-driven Document | Pointer fixture → `AddStroke` → empty Document replay | Passing locally |
| Deterministic Vector/Dab | Ten repeated runs and fixed 128-bit digests | Passing locally |
| Incremental work | 7,201 confirmed input samples; `end()` adds no geometry work | Passing locally |
| Sustained input | 60 s × 240 Hz = 14,400 confirmed samples | Passing locally |
| Backpressure | compatible merge, capacity/bytes/age, atomic overrun cancellation | Passing locally |
| Preview | confirmed append, replaceable prediction, revision coalescing | Passing locally |
| Handoff semantics | committed → canonical-visible acknowledgement | Passing locally |
| Device taxonomy | palm/hover/eraser-tip do not create Ink Operations | Passing locally |
| View transforms | DPR/zoom/pan variants yield equal World-space digest | Passing locally |
| Frame scheduling | bounded callback and stale-generation rejection | Passing locally |
| macOS arm64 | locked prebuilt Skia Metal/Ganesh clean build and tests | Passing locally |
| Web | single-thread WASM/WebGL2, Playwright replay/golden/interactive flow | Passing locally |
| Windows | clang-cl/D3D12 WARP build, visual, digest, 100 lifecycle loops | Passing CI |
| Android | arm64/x86_64 build | Passing locally |
| Android arm64 emulator | Native CanvasView replay, Vector/Dab golden, semantic/numeric digest | Passing locally |
| Android x86_64 emulator | Native CanvasView replay/golden acceptance | Passing CI |
| Physical writing | Windows/Web/Android representative pen devices | Pending Human Ink Gate |

“Passing locally” records development-host evidence, not an acceptance status.
The POC-02 workflow independently rebuilds and compares required Web, Windows,
and Android artifacts before its cross-platform result can pass.

The first authoritative run for commit `40232e68b5f4f2b4e40ebe1cabbfc74ddd3a16c1`
is [GitHub Actions run 32098127687](https://github.com/Mostorm-Labs/canvas/actions/runs/32098127687).
Its host-arm64, Web, Windows, Android, and cross-platform acceptance jobs all
passed. The aggregator observed all three required GPU platforms with no
semantic, Preview, numeric, or visual failure. This automated result does not
change the separate Human Ink Gate from `Pending`.

## Fixed semantic and visual oracles

| Fixture | Stroke | Document | Preview |
|---|---|---|---|
| Vector | `2ae509ac25da5ca82937044f740265e4` | `672abdc8604a169ecda4ce08e8d80b55` | `dfc99ad07efd6162efd8d2ece65e0319` |
| Dab | `a9d4cf3e83a5f08b45621991271ec8c7` | `ae1726a94d60f3e10c2789034bbd9e36` | `efa865ba51000559f8c53d0febb539b1` |

The visual oracle is an explicit Skia raster baseline at 800×600, DPR 1,
RGBA8888/sRGB. WebGL2, D3D12 WARP, and GLES3 readback must have at least 99.9%
pixels within per-channel delta 2 and maximum channel delta no greater than 2.
Coverage is deliberately binary so backend anti-aliasing kernels do not mask or
manufacture semantic differences.

The reviewed numeric corpus digest is
`ee8615ae2159fc9f42faad6687b0cbd3`. Web/WASM, Windows x64, Android x86_64,
and Android arm64 must reproduce it; this gate cannot use a pixel tolerance.

## Automated test inventory

The host suite covers:

- strict AddStroke version/schema/sequence parsing, duplicate IDs, invalid
  floats, and all-or-nothing application;
- Vector prediction rollback and Dab random-domain determinism;
- brush spacing, resource identity, algorithm version rejection, and digest
  sensitivity;
- finite transforms, canonical signed zero, missing device capabilities, and
  sequence gaps;
- long-stroke incremental work and 14,400-sample preservation;
- input queue merge/overrun, Preview queue coalescing/overrun, cancel, palm,
  hover, and eraser-tip behavior;
- canonical-visible handoff and bounded/stale-generation frame scheduling;
- Vector/Dab ten-run replay and World-coordinate invariance.

Web Playwright adds GPU replay/golden checks and an interactive coalesced
pointer stroke that reaches Canonical visible acknowledgement. Static artifact
inspection rejects `pthread`, `SharedArrayBuffer`, COOP, and COEP dependencies.

Windows CI is the authoritative compiler/adapter validation for Windows. It
uses the pinned Windows toolchain and does not cause the macOS development host
to locate or inspect an MSVC runtime. Android CI builds both required ABIs and
runs the x86_64 Native CanvasView on an API 35 emulator.

## CI architecture

`.github/workflows/poc02.yml` is independent from the POC-01 workflow and has
host-arm64, Web, Windows, Android, and cross-platform acceptance jobs. Every GPU
job downloads a target named in `skia-sdk.lock.json`; ordinary POC-02 CI does
not contain Skia source checkout, GN invocation, Skia Ninja invocation, or a
`.deps/skia/out` cache.

The aggregator requires all Web, Windows, and Android results and rejects
missing/duplicate platforms, semantic drift, visual drift, or malformed
Preview evidence. WARP, SwiftShader, and the Android emulator establish
correctness only.

## Remaining final-acceptance work

POC-02 is not accepted until:

1. the independent CI jobs compile and pass on their native runners;
2. the cross-platform semantic/visual aggregator passes with no missing target;
3. real Windows, Web, and Android pen devices complete
   [`HUMAN_INK_GATE.md`](HUMAN_INK_GATE.md);
4. application sample-to-visible p95/p99, frame-count and queue-age reporting,
   long-stroke pointer-up time, and Canonical handoff meet the roadmap gates;
5. any subjective defect is tied to a trace/video and has an explicit
   disposition.

No placeholder, emulator run, synthetic replay, or green CI run may be recorded
as physical-device evidence.

Merging this POC records the `Integration Ready / Validating` decision only. It
does not change any Pending Human Ink Gate row, and it does not promise source
or binary compatibility for the experimental POC interfaces.
