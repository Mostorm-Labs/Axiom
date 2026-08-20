# POC-03 cross-platform physical validation — 2026-08-18

Status: **Passed** (the evidence collected in this report passed; POC-03 remains
`Validating`).

This report consolidates the 100K-scene correctness and physical-device
evidence for Windows Native D3D12, Chrome WebGL2, iPhone, iPad, and Android.
It records the results after the centroid-anchored pinch and pointer-count
transition fixes. It does not mark POC-03 `Accepted`: the Integrated
Performance Playground `write` path still depends on the independently
developed POC-02 Ink contract, and POC-02 is still being validated.

The machine-readable companion is
[physical-validation-20260818.json](physical-validation-20260818.json).

## Reproducibility identity

| Field | Value |
|---|---|
| Harness/source commit | `faca2d77ddb9ef9e0b36d7f9da930f7e70d322c3` |
| Branch | `codex/poc-03-100k-scene` |
| Generator | algorithm 1, seed `0x43414e5641533033`, 100,000 nodes |
| Shared Document/Scene digest | `0f769f6dfb52f818a37a48751455fb6b` |
| Skia commit | `b6d106297ff9ef2ff8094033695d045e87775581` |
| Apple Skia SDK ID | `eab7bc961815a59c8427efefc0cabb360c73ab7e679bf8e81c729426e397e8ea` |
| Android Skia SDK ID | `630e4536e8eaf7ee71a81e29880a934563fbae5d87a9aaef5c2c5a19b544c80f` |
| Apple universal runner binary SHA-256 | `0c5be17f9209cca289f634142427056110e9c5c775bac29dd1f5505f3ac3252f` |

The Windows/Web bundle contains sanitized environment metadata, complete frame
traces, the two result files, console logs, binary hashes, and the recalculated
gate manifest:

- Release: [POC-03 Windows/Web physical evidence](https://github.com/Mostorm-Labs/canvas/releases/tag/poc03-windows-web-physical-20260818-c4be424b)
- Asset: `poc03-windows-web-physical-20260818-c4be424b.zip`
- Size: 284,967 bytes
- SHA-256: `c4be424b5da3be54aada1c367c58f3a2a912480635b981ea43a2516659fb2d91`

Mobile result summaries below were copied from the retained local raw reports.
The checked-in evidence deliberately excludes signing identities, Team IDs,
provisioning profiles, device IDs, serial numbers, accounts, and device names.

## Windows and Web hardware gate

Both paths ran on the same Windows 10 physical machine with an Intel Core
i5-10400 and Intel UHD Graphics 630 (`warp=false`). Chrome Stable 151 used
hardware ANGLE D3D11, not SwiftShader. Each path rendered the fixed 100K scene
for 60 seconds.

| Path | Frames | Primary p50 / p95 / p99 / max | Presentation/callback p50 / p95 / p99 / max | Missed | Max candidates | Memory |
|---|---:|---|---|---:|---:|---|
| Windows Ganesh D3D12 | 3,596 | 2.342 / 4.867 / 6.200 / 17.032 ms render/submit | 16.684 / 16.704 / 16.721 / 33.379 ms DXGI presentation | 1 | 2,565 | 149,508,096 bytes process peak; 40,882,372 bytes Document/Scene/cache |
| Chrome Ganesh WebGL2 | 3,597 | 16.7 / 16.8 / 17.1 / 28.7 ms rAF callback | 2.5 / 4.8 / 6.2 / 7.4 ms render plus `gl.finish` | 3 | 2,565 | 51,380,224 bytes WASM linear memory; heap 42,795,008 bytes before and after |

Windows passed the repository p95 <= 16.7 ms and p99 <= 33.3 ms limits. Web
passed p95 <= 20 ms and p99 <= 40 ms. Both produced the shared digest, matched
full and incremental compilation, passed the same-revision visual oracle, and
stayed below the 5,000-candidate limit. The Web artifact remained
single-threaded and had no page errors.

## Mobile physical-device evidence

Mobile results exercise the same 100K Document/Scene semantics and verify the
native Metal/GLES render and input paths. They are portability and experience
evidence; they do not replace the Windows/Web hard performance gate.

| Device/path | Display | Frame p50 / p95 / p99 / max | Missed | Max candidates | Process peak | Correctness |
|---|---|---|---:|---:|---:|---|
| iPhone 15 Pro (`iPhone16,1`), iOS 26.6 (`23G71`), A17 Pro Metal | 2556x1179, DPR 3, measured 59.988 Hz | 2.404 / 4.077 / 4.431 / 8.007 ms | 0 | 1,537 | 185.579 MiB | full/incremental and visual equivalence passed |
| iPad Air 4 (`iPad13,1`), iPadOS 26.6 (`23G71`), A14 Metal | 2360x1640, DPR 2, measured 60.005 Hz | 4.767 / 12.848 / 13.795 / 34.630 ms | 2 | 3,074 | 134.392 MiB | full/incremental and visual equivalence passed |
| Pixel 7 (`panther`), Android 17, Google GS201 GLES3 | 2400x1080, DPR 2.625, measured 90 Hz | 9.443 / 11.462 / 12.453 / 182.417 ms | 55 | 1,538 | 239.547 MiB | full/incremental and visual equivalence passed |

The iPhone run measured approximately 60 Hz; this report makes no 120 Hz
claim. The shared Apple runner historically emits `platform: "ipados"` for
both device families. The iPhone row is classified from `iPhone16,1`; the
report does not pretend that the emitted field has already been corrected.

An additional post-fix iPad warm run recorded p95 18.515 ms, p99 33.231 ms,
max 35.827 ms, and 27 missed presentations. It is retained as a scheduling or
temperature sensitivity observation rather than discarded. Pixel 7 recorded a
182.417 ms cold-start maximum and 55 missed presentations at 90 Hz; steady
p95/p99 were 11.462/12.453 ms, but the spike and frame-budget misses remain
open optimization evidence. Neither observation changes semantic or visual
correctness.

The final Pixel 7 APK SHA-256 was
`a39b2246b72ca3828ad67d32a88bef38f8915a00b933de30a08bac6778d8617a`.
All three `libcanvas_poc3_android.so` `PT_LOAD` segments were aligned to
`0x4000`, and the APK passed `zipalign -P 16`.

## Manual gesture acceptance

The user exercised iPhone 15 Pro, iPad Air 4, and Pixel 7 after the final
gesture fix. All three passed the same rubric:

- single-finger fast pan and continuous two-finger zoom showed no freeze,
  black frame, flicker, or input discontinuity;
- the world point below the moving two-finger centroid remained anchored;
- simultaneous two-finger release caused no viewport jump;
- releasing either finger first caused no viewport jump; and
- the remaining finger continued panning without a transition jump.

The iPhone exercise explicitly covered about ten seconds of pan and pinch. The
iPad and Pixel 7 final feedback confirmed the same complete rubric.

## Cross-platform equivalence and disposition

The repository aggregation tool compared four result inputs: host core, Pixel
7, iPad Air 4, and iPhone 15 Pro. It passed with the shared digest,
full/incremental equivalence, visual equivalence, and at most 5,000 candidates;
the aggregate JSON SHA-256 was
`886f274c2cdf0a0e8fcd3990322dd40d1223f25539e6d74590da3b4ad9ad1dd4`.
The independently archived Windows and Web paths produced the same digest and
equivalence results. Consequently, six execution paths agree, while the
machine-generated aggregate itself truthfully contains four inputs.

This evidence closes the Windows/Web, Android, iPhone, iPad, gesture, and
cross-platform result-collection items. POC-03 remains **Validating** until the
Integrated Performance Playground can consume POC-02's real Ink output for its
`write` path. No substitute stroke model is introduced by POC-03.
