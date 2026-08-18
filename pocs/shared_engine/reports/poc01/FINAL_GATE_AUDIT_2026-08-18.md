# POC-01 Final Gate Audit — 2026-08-18

> Decision: **`Accepted`.** Every declared POC-01 exit condition has reviewed,
> reproducible evidence. Historical failed and infrastructure-blocked attempts
> remain retained; acceptance applies to the corrected current source and does
> not stabilize the POC ABI, replay schema, or scene representation.

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
| macOS fixed-source Release | [fix report](macos-stability-fix-2026-08-18/README.md), [evidence Release](https://github.com/Mostorm-Labs/canvas/releases/tag/poc01-macos-stability-2026-08-18-2854fc7155cd3856) | Healthy Ganesh cleanup and bounded offscreen submission implemented; clean Release 100/60, memory, and visual gates passed on commit `9e9618fe9931af6695ce329c1883670c80983dcd` |
| Fixed-source full CI | [PR #15](https://github.com/Mostorm-Labs/canvas/pull/15), [run `32103105209`](https://github.com/Mostorm-Labs/canvas/actions/runs/32103105209) | Host core, Web, Windows, macOS, Apple mobile, Android, and byte-for-byte cross-platform acceptance all passed |
| Revised same-machine Windows/Web | [report](../../../../docs/quality/evidence/poc01/windows-web-revalidation-20260818.md), [PR #16](https://github.com/Mostorm-Labs/canvas/pull/16), [evidence Release](https://github.com/Mostorm-Labs/canvas/releases/tag/poc01-windows-web-revalidation-20260818-a11899ca) | First and only revised collection passed hardware, 100/60, memory, environment, visual, and conformance gates |

All six downloaded Release assets were independently checked against their
published byte count and SHA-256. The mobile archive has SHA-256
`62d1ae02ffaa15bdc0183dac9fc7657604862d7aea1f9da3267fefaf334f3fe7`;
the Windows/Web archive has SHA-256
`de3c8347fbe5af8e656ed3918fbbf2cf5d1ac4dc346017600725d12f38612225`;
the Android Release supplement has SHA-256
`5b7d92d422df0337d86575df15f4158afc3ef4eb52374c6dd00f579c3622c449`;
and the Apple/macOS supplement has SHA-256
`55b28935b59bbef2325218133f9e686f6e67d2a09bc2b90f96075fa7c49c86a8`;
the fixed-source macOS asset has SHA-256
`2854fc7155cd38561ca30718ee4de076fbcfc15757530ee11cf1b396b946d4b0`;
and the revised Windows/Web asset has SHA-256
`a11899ca261429ab0890db6b9fd5a39cd904150caf7dac762e5d2470c4e10549`.
All 19 Windows/Web payload members outside `artifact-hashes.json` were also
matched to their file-level hash records. All eight Android payload members
listed by the ninth member, `SHA256SUMS.json`, were checked for exact byte count
and SHA-256. All 33 Apple/macOS payload members listed by the 34th member,
`SHA256SUMS.json`, were checked the same way. All seven fixed-source macOS
payload members listed by its eighth member were also matched by byte count and
SHA-256. All 14 payload members in the revised Windows/Web archive outside
`artifact-hashes.json` were independently matched to their file-level byte
counts and SHA-256 records.

## Gate decision

| Exit condition | Status | Evidence or reason |
| --- | --- | --- |
| Six platform families clean-build from locked inputs | Passed | PR #9 attempt 2 clean-built Web, Windows, macOS, iOS/iPadOS device and simulator targets, and both Android ABIs. |
| Document digest is identical | Passed | Six-platform aggregate and every completed physical path report `47826449b895ac4f4a57b4f386379775`. |
| Numeric corpus and independent empty-Document replay agree | Passed | All reviewed results report corpus `e8f3bc4f06282fc0a2348aa5059d56fa` and replay `bcdb19afb9eccbf68cec4a7f442b1cd2`, revision 1, sequence 4. |
| GPU visual gate | Passed | Every reviewed CI and physical readback meets at least 99.9% of pixels within ±2 per channel. |
| 100 lifecycle iterations | Passed | Six CI platforms and every completed physical acceptance path report 100 iterations. The retained initial iPad install failure and first macOS frame failure stopped before this gate could complete and are classified separately. |
| 1,000-node, 60-second smoke with no frame over 100 ms | Passed on current source; historical failures retained | The fixed macOS source reports 3,600 frames and 9.80521 ms maximum. Earlier 148.524167 ms and 104.214750 ms macOS failures remain retained observations against their older source. Other reviewed paths meet the unchanged threshold. |
| No sustained memory growth | Passed; historical failures retained | iPhone, iPadOS, Android, Web, and fixed-source macOS pass their declared checks. Revised Windows evidence has 13 post-warm-up working-set samples over 60,000 ms; the first/last quartile medians are 108,664,832/97,796,096 bytes, or -10.0021%, against the unchanged +5% limit. The earlier macOS +10.635462% result remains a failure against its older source. |
| Performance environment is reproducible | Passed with explicit unavailability | Android, Apple/macOS, and revised Windows/Web reports record thermal, power, refresh/frame interval, VRR, and throttling state. Windows firmware/driver fields that public queries could not supply are explicitly `unavailable` with observation methods and reasons rather than omitted. |
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

The failure was resolved on macOS by correcting two backend lifecycle
contracts. A healthy Ganesh/Metal context now releases surfaces and renderer
caches before `releaseResourcesAndAbandonContext()`; the lost-context-only
`abandonContext()` path no longer substitutes for cleanup. The offscreen
macOS renderer also synchronizes GPU completion per frame, bounding frames in
flight as a swapchain normally would. Clean Release source commit
`9e9618fe9931af6695ce329c1883670c80983dcd` then passed 100 lifecycle
iterations, 60 seconds/3,600 frames, a 9.80521 ms maximum, the 5% memory rule
at 0% growth, and the visual gate. iOS/iPadOS behavior is compile-time excluded
from this fix. This new result closes the macOS blocker without rewriting the
older failures.

One iOS simulator boundary-regression run on the unchanged mobile path reported
183.159625 ms while the macOS fix was being checked. It remains a retained
failure and was not rerun away. The macOS-only code path is compile-time
excluded from iOS/iPadOS; accepted physical iPhone/iPadOS evidence and the
subsequent complete Apple Mobile CI pass remain the relevant current-source
evidence.

The revised Windows/Web collection used Runtime commit
`5ab8b16bdac8f982a9d221d1f48d3867dda7b43c` and harness commit
`06b61f3c7506f93d380765cdcaca59637bdf2644`. From that harness commit through
the accepted fixed-source head, the Windows/Web Runtime, platform adapters,
collector, fixtures, locks, and thresholds did not change; the executable
change was scoped to the macOS Metal adapter and shared renderer cache cleanup.
The first and only revised collection passed with a 28.0954 ms Windows maximum,
a 3.9000 ms Web maximum, stable Web WASM heap, complete environment metadata,
and the working-set result above.

## Acceptance scope

POC-01 accepts the architectural claim that one single-threaded C++20 Runtime
can produce the declared deterministic semantic and visual result across the
six platform families. All POC interfaces remain Experimental. R1 must rebuild
the product boundary from the evidence rather than inherit source or binary
compatibility. CRDT, a production Snapshot codec, Ink, RichText editing,
collaboration, product ABI, and product shells remain outside POC-01 scope and
must satisfy their own staged gates.
