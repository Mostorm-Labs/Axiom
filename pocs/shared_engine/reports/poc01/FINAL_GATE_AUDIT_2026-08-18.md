# POC-01 Final Gate Audit — 2026-08-18

> Decision: **Remain `Validating`.** The shared-engine correctness proof and
> both physical evidence bundles are accepted as evidence, but the complete
> POC-01 exit gate is not yet satisfied.

## Reviewed evidence

This audit combines evidence that was intentionally submitted and retained in
separate changes:

| Evidence | Identity | Review result |
| --- | --- | --- |
| Shared Runtime and deterministic corpus | commit `5ab8b16bdac8f982a9d221d1f48d3867dda7b43c`, merged by [PR #9](https://github.com/Mostorm-Labs/canvas/pull/9) | Accepted as the common Runtime under test |
| Six-platform CI | [run `32042915468`](https://github.com/Mostorm-Labs/canvas/actions/runs/32042915468), attempt 2 | All platform jobs and aggregate acceptance passed |
| Integrated `main` regression | commit `95b92375fed00fc17fd4b806073926061e7c0fcd`, [run `32043748623`](https://github.com/Mostorm-Labs/canvas/actions/runs/32043748623) | Infrastructure-blocked: all executable jobs except Android passed; Android never started because GitHub codeload returned HTTP 429 twice, so aggregate acceptance lacked its artifact |
| Post-incident integrated regression | audit commit `77583fce67ed73d47979d147e25862f2939f0dec`, [run `32044717916`](https://github.com/Mostorm-Labs/canvas/actions/runs/32044717916), attempt 2 | Same executable code as integrated `main`; all six platforms and aggregate acceptance passed |
| Mobile physical devices | [report](mobile-physical-2026-08-17/README.md), [PR #7](https://github.com/Mostorm-Labs/canvas/pull/7), [evidence Release](https://github.com/Mostorm-Labs/canvas/releases/tag/poc01-mobile-physical-2026-08-17-62d1ae02ffaa15bd) | iPhone, iPadOS, and Android correctness subset passed |
| Same-machine Windows/Web | [report](../../../../docs/quality/evidence/poc01/windows-web-physical-20260817.md), [PR #8](https://github.com/Mostorm-Labs/canvas/pull/8), [evidence Release](https://github.com/Mostorm-Labs/canvas/releases/tag/poc01-windows-web-physical-20260817-6a2bac2) | Native D3D12 and Chrome WebGL2 hardware subset passed |
| Android non-debuggable Release | [supplemental report](android-release-physical-2026-08-18/README.md), harness [PR #11](https://github.com/Mostorm-Labs/canvas/pull/11), [evidence Release](https://github.com/Mostorm-Labs/canvas/releases/tag/poc01-android-release-physical-2026-08-18-5b7d92d422df0337) | Physical Pixel 7 Release 100/60, post-warm-up PSS, and visual subset passed |
| Apple mobile and macOS Release | [supplemental report](apple-macos-supplemental-2026-08-18/README.md), source [PR #12](https://github.com/Mostorm-Labs/canvas/pull/12), [evidence Release](https://github.com/Mostorm-Labs/canvas/releases/tag/poc01-apple-macos-supplemental-2026-08-18-55b28935b59bbef2) | iPhone and iPadOS passed; macOS failed the first-run frame gate and the only bounded-rerun memory gate |

All four downloaded Release assets were independently checked against their
published byte count and SHA-256. The mobile archive has SHA-256
`62d1ae02ffaa15bdc0183dac9fc7657604862d7aea1f9da3267fefaf334f3fe7`;
the Windows/Web archive has SHA-256
`de3c8347fbe5af8e656ed3918fbbf2cf5d1ac4dc346017600725d12f38612225`;
the Android Release supplement has SHA-256
`5b7d92d422df0337d86575df15f4158afc3ef4eb52374c6dd00f579c3622c449`;
and the Apple/macOS supplement has SHA-256
`55b28935b59bbef2325218133f9e686f6e67d2a09bc2b90f96075fa7c49c86a8`.
All 19 Windows/Web payload members outside `artifact-hashes.json` were also
matched to their file-level hash records. All eight Android payload members
listed by the ninth member, `SHA256SUMS.json`, were checked for exact byte count
and SHA-256. All 33 Apple/macOS payload members listed by the 34th member,
`SHA256SUMS.json`, were checked the same way.

## Gate decision

| Exit condition | Status | Evidence or reason |
| --- | --- | --- |
| Six platform families clean-build from locked inputs | Passed | PR #9 attempt 2 clean-built Web, Windows, macOS, iOS/iPadOS device and simulator targets, and both Android ABIs. |
| Document digest is identical | Passed | Six-platform aggregate and every completed physical path report `47826449b895ac4f4a57b4f386379775`. |
| Numeric corpus and independent empty-Document replay agree | Passed | All reviewed results report corpus `e8f3bc4f06282fc0a2348aa5059d56fa` and replay `bcdb19afb9eccbf68cec4a7f442b1cd2`, revision 1, sequence 4. |
| GPU visual gate | Passed | Every reviewed CI and physical readback meets at least 99.9% of pixels within ±2 per channel. |
| 100 lifecycle iterations | Passed | Six CI platforms and every completed physical acceptance path report 100 iterations. The retained initial iPad install failure and first macOS frame failure stopped before this gate could complete and are classified separately. |
| 1,000-node, 60-second smoke with no frame over 100 ms | **Failed** | The first clean Release macOS physical run reached 104.214750 ms against the unchanged 100 ms ceiling. Its one bounded rerun reached 49.3456 ms, but does not erase the retained failure. iPhone, iPadOS, Android, Windows/Web hardware, and reviewed CI paths otherwise meet the threshold. |
| No sustained memory growth | **Failed** | iPhone, iPadOS, Android, and Web pass their declared series checks. The only bounded macOS rerun has 13 post-warm-up physical-footprint samples over 60,004 ms and fails: tail-window median growth is 10.635462% against the unchanged 5% ceiling. Windows physical evidence still requires a revised working-set series; peak-only data is insufficient. |
| Performance environment is reproducible | Incomplete | Android and the new Apple/macOS supplement record thermal, power, refresh/frame interval, VRR availability, and throttling applicability. The retained Windows/Web bundle predates the revised collector and still lacks the equivalent reviewed fields. |
| Web has no pthread/SharedArrayBuffer/isolation dependency | Passed | The locked artifact scan passes and the Windows hardware bundle confirms the single-threaded artifact. |
| Runtime core contains no platform UI/backend types | Passed | The generic `src/` and public include boundary contain no HWND, D3D12, Emscripten, DOM, Metal, UIKit, EGL, or JNI type dependency; those types remain in platform adapters. |

The first attempt of CI run `32042915468` is retained as useful noise evidence:
Android stopped before executing tests because GitHub codeload returned HTTP
429, and the hosted iPad simulator reported one 123.23 ms frame. The failed
jobs were rerun once without code or threshold changes; attempt 2 passed
Android, both Apple simulators, and aggregate acceptance. The physical iPad
run reported a 9.51679 ms maximum. This audit does not erase the first attempt
or treat repeated reruns as a substitute for the remaining evidence above.

The first attempt of integrated `main` run `32043748623` is retained for the
same reason: Android stopped before tests when GitHub codeload returned HTTP
429 for `gradle/actions`, and hosted Windows WARP reported one 1127.37 ms
frame. The separately archived physical Windows hardware run reached 12.3209
ms maximum. The failed hosted jobs were rerun once without changing code or
the 100 ms threshold. Windows passed on attempt 2. Android again stopped before
tests on a GitHub codeload 429 for `actions/setup-java`; aggregate acceptance
therefore failed only because no Android artifact existed. This audit does not
label the integrated run green and does not perform a third blind rerun.

The subsequent audit-branch run `32044717916` contains no Runtime, harness,
fixture, threshold, or baseline changes relative to integrated `main`. Its
first attempt saw a GitHub HTTP 500 while cloning xxHash for macOS; all other
platform jobs passed. macOS and aggregate acceptance passed on the single
failed-job rerun, so the post-incident integrated regression is complete.

The Apple/macOS supplemental source is squash-merged commit
`06b61f3c7506f93d380765cdcaca59637bdf2644`. Clean Release physical runs from
that exact commit close the iPhone and iPadOS memory/environment gaps. They do
not pass macOS. The first macOS run failed at 104.214750 ms; the sole bounded
rerun passed the frame ceiling but failed memory at 10.635462% tail-window
growth. No code, fixture, threshold, or build-configuration change separated
the runs. No third rerun is permitted in this evidence cycle. The older
148.524167 ms physical macOS observation also remains failed and is not erased
or downplayed by the newer evidence.

## Evidence required for `Accepted`

1. Diagnose and resolve the macOS physical stability failure before requesting
   any fresh validation. A future run must be explicitly authorized and must
   pass both the unchanged 100 ms frame ceiling and the unchanged 5%
   quartile-median memory rule; repeated blind reruns are not acceptance.
2. On the physical Windows/Web machine, use the revised `run_bundle.ps1` to
   retain the Windows post-warm-up working-set series and the complete thermal,
   power, refresh/frame interval, VRR, and browser-throttling metadata. Preserve
   the existing Windows/Web evidence rather than overwriting it.
3. Publish any new evidence as immutable content-addressed assets and complete
   a final aggregate review. POC-01 remains `Validating` until the macOS and
   revised Windows/Web records pass review.

No CRDT, Snapshot codec, Ink, RichText, collaboration, product ABI, or product
shell work is part of this remaining POC-01 evidence task.
