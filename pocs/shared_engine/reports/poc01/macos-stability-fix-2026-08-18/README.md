# POC-01 macOS Stability Fix Evidence — 2026-08-18

> Result: **Passed for the macOS clean Release rerun on the fixed source.**
> The previously retained 148.524167 ms, 104.214750 ms, and 10.635462% memory-
> growth failures remain part of the audit history. POC-01 remains `Validating`
> until revised Windows/Web physical evidence and final aggregate review are
> complete.

## Root cause and scope

The macOS Metal adapter used `abandonContext()` when destroying a healthy
Ganesh context. That API represents an unusable or lost backend and
intentionally skips native-resource cleanup. The 100-iteration lifecycle could
therefore leave Metal/Ganesh allocations pending before the measured smoke.
The offscreen renderer also submitted asynchronously without the in-flight
backpressure normally supplied by a swapchain, so command buffers and their
resources could be retained across the 60-second interval.

Commit `9e9618fe9931af6695ce329c1883670c80983dcd` fixes both contracts on macOS:

- surface, renderer image/typeface caches, and scene references are dropped
  before `releaseResourcesAndAbandonContext()` cleans up the healthy backend;
- every offscreen macOS frame synchronizes GPU completion, making the existing
  per-frame timing include the work and bounding frames in flight;
- iOS/iPadOS retain their previously accepted submit and shutdown behavior via
  an explicit `TARGET_OS_OSX` boundary;
- thresholds, fixtures, Skia SDK, document model, and visual baseline are
  unchanged.

## Clean Release result

The repository was configured from scratch, the macOS Release target was
clean-built, and all 26 C++/Skia tests passed before the unchanged 100/60 gate.

| Gate | Observed result |
| --- | --- |
| Source commit | `9e9618fe9931af6695ce329c1883670c80983dcd` |
| Backend / GPU | Ganesh Metal / Apple M2 |
| Lifecycle | 100 |
| Smoke | 60 seconds, 3,600 frames |
| Maximum frame | 9.80521 ms; pass against 100 ms |
| Document digest | `47826449b895ac4f4a57b4f386379775` |
| Numeric corpus | `e8f3bc4f06282fc0a2348aa5059d56fa` |
| Independent replay | `bcdb19afb9eccbf68cec4a7f442b1cd2`, revision 1, sequence 4 |
| Visual | 479,917 / 480,000 = 99.982708% within ±2; pass |
| Memory samples | 13 process physical-footprint samples over 60,004 ms |
| Memory head/tail median | 31,932,968 / 31,932,968 bytes |
| Memory growth | 0 bytes / 0%; pass against 5% |
| Environment | thermal `fair` → `fair`, low-power mode false, maximum refresh 60 Hz |

The normalized machine-readable result is committed as
[`result.json`](result.json); visual metrics are committed as
[`visual.json`](visual.json).

## Retained evidence

- Release: [POC-01 macOS stability fix evidence 2026-08-18](https://github.com/Mostorm-Labs/canvas/releases/tag/poc01-macos-stability-2026-08-18-2854fc7155cd3856)
- Asset: `poc01-macos-stability-2026-08-18.zip`
- Asset size: 13,844 bytes; 8 file members
- Asset SHA-256: `2854fc7155cd38561ca30718ee4de076fbcfc15757530ee11cf1b396b946d4b0`
- macOS runner SHA-256: `f625dbddbe961298aebb70ae7aaf0d866750aef314752f3cde0158b4a0d06e47`

The ZIP includes raw and normalized results, raw RGBA, expected/actual/diff
images, visual metrics, and file-level hashes for the seven payload members.
The GitHub asset digest and local ZIP digest match. The Release must not be
overwritten or deleted after reference.

No device identifiers, user or host names, absolute paths, network addresses,
proxy configuration, signing information, or account data are retained.

## Audit decision

This fixed-source run closes the macOS frame and sustained-memory blocker for
the new implementation. It does not rewrite the earlier failed observations or
retroactively make their source pass. One iOS simulator run made while checking
the compile-time platform boundary reported a retained 183.159625 ms hosted-
simulator frame; the iOS/iPadOS runtime path was unchanged and the previously
accepted physical-device evidence remains authoritative.

The remaining POC-01 work is the separately running Windows/Web physical
recapture with the revised `run_bundle.ps1`, followed by immutable publication
and an aggregate audit. Until those records are reviewed, the project remains
`Validating`.
