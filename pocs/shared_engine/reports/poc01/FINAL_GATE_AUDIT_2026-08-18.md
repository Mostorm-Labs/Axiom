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
| Mobile physical devices | [report](mobile-physical-2026-08-17/README.md), [PR #7](https://github.com/Mostorm-Labs/canvas/pull/7), [evidence Release](https://github.com/Mostorm-Labs/canvas/releases/tag/poc01-mobile-physical-2026-08-17-62d1ae02ffaa15bd) | iPhone, iPadOS, and Android correctness subset passed |
| Same-machine Windows/Web | [report](../../../../docs/quality/evidence/poc01/windows-web-physical-20260817.md), [PR #8](https://github.com/Mostorm-Labs/canvas/pull/8), [evidence Release](https://github.com/Mostorm-Labs/canvas/releases/tag/poc01-windows-web-physical-20260817-6a2bac2) | Native D3D12 and Chrome WebGL2 hardware subset passed |

Both downloaded Release assets were independently checked against their
published byte count and SHA-256. The mobile archive has SHA-256
`62d1ae02ffaa15bdc0183dac9fc7657604862d7aea1f9da3267fefaf334f3fe7`;
the Windows/Web archive has SHA-256
`de3c8347fbe5af8e656ed3918fbbf2cf5d1ac4dc346017600725d12f38612225`.
All 19 Windows/Web payload members outside `artifact-hashes.json` were also
matched to their file-level hash records.

## Gate decision

| Exit condition | Status | Evidence or reason |
| --- | --- | --- |
| Six platform families clean-build from locked inputs | Passed | PR #9 attempt 2 clean-built Web, Windows, macOS, iOS/iPadOS device and simulator targets, and both Android ABIs. |
| Document digest is identical | Passed | Six-platform aggregate and all five physical devices report `47826449b895ac4f4a57b4f386379775`. |
| Numeric corpus and independent empty-Document replay agree | Passed | All reviewed results report corpus `e8f3bc4f06282fc0a2348aa5059d56fa` and replay `bcdb19afb9eccbf68cec4a7f442b1cd2`, revision 1, sequence 4. |
| GPU visual gate | Passed | Every reviewed CI and physical readback meets at least 99.9% of pixels within ±2 per channel. |
| 100 lifecycle iterations | Passed | Six CI platforms and all five physical paths report 100 iterations. |
| 1,000-node, 60-second smoke with no frame over 100 ms | Incomplete | CI and physical runs meet the frame threshold, but the Android physical result is from a Debug acceptance APK. The quality baseline forbids using Debug builds as performance conclusions. |
| No sustained memory growth | Incomplete | Web records a stable WASM heap and Android records non-monotonic PSS samples. Windows records only a peak working set; Apple runners do not record a quantitative time series. Peak-only or missing samples cannot prove absence of sustained growth. |
| Performance environment is reproducible | Incomplete | Device/OS/toolchain/backend are recorded, but the Windows/Web bundle omits thermal state, power mode, refresh/frame interval, VRR, and browser-throttling state required by the quality baseline. Equivalent metadata is also incomplete for mobile performance interpretation. |
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

## Evidence required for `Accepted`

1. Build a non-debuggable Android Release acceptance APK from the same locked
   Runtime/Skia inputs, run the existing physical-device 100/60/visual gate,
   and retain the result, artifact hash, and environment record.
2. Capture post-warm-up memory samples throughout the 60-second smoke for
   Windows, macOS, iPhone, and iPadOS (and keep the existing Web/Android
   samples), with a declared sampling interval and an explicit no-sustained-
   growth decision. Peak-only memory is insufficient.
3. Add thermal state, power mode, display refresh/frame interval, VRR status,
   and browser-throttling state to the physical benchmark manifests. If a
   platform cannot expose a field, record `unavailable` plus the observation
   method instead of silently omitting it.
4. Publish the supplemental raw evidence as content-addressed retained assets,
   update the aggregate machine-readable audit, and rerun the unchanged gate.
5. Rerun the integrated `main` Android and aggregate CI after the GitHub
   codeload incident clears; the run must execute tests rather than fail while
   downloading an action.

No CRDT, Snapshot codec, Ink, RichText, collaboration, product ABI, or product
shell work is part of this remaining POC-01 evidence task.
