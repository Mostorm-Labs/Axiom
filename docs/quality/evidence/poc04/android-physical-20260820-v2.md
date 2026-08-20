# POC-04 Android physical-device report — 2026-08-20 (v2)

Status: **Passed for the Android v2 canonical Runtime and physical-device
performance gate.** Real Gboard `InputConnection` callback delivery remains
recorded by the earlier physical-input report; this run does not pretend that
the canonical recorder synthesized or repeated system-keyboard input. It does
not enable cross-platform acceptance or change POC-04 from `Validating` to
`Accepted`.

## Locked identity

| Item | Value |
| --- | --- |
| Runtime source commit | `a57eb8d53c1232651514c3db58b98fc9967e7106` |
| Local source changes | none |
| Skia commit | `b6d106297ff9ef2ff8094033695d045e87775581` |
| RichText SDK profile / target | `poc04-richtext-v2` / `android-arm64-v8a-gles3` |
| RichText SDK ID | `fee8d9c93e4e4c7122d64374ed53e7ffd1b65deca9b80717ee752451386d4be0` |
| NDK / API contract | `27.2.12479018` / minimum API 26 |
| APK SHA-256 | `56ac8e34c7db11860bf5830f15e110048701ef602e0b47f186144e3b49df6483` |
| Behavior artifact SHA-256 | `bcfa2228c8054edc065beef72934185ccf08fa180297c43be50676f533029ad2` |

## Device and results

Google Pixel 7 (`panther`), physical USB device, arm64-v8a, Android 17/API 37,
release-keys fingerprint `google/panther/panther:17/CP2A.260705.006/15641320`.
The optimized Release canonical recorder ran on the device. The independent
real Gboard callback evidence remains in
[the 2026-08-19 report](android-physical-20260819.md).

| Gate | Result | Required |
| --- | ---: | ---: |
| Input/caret p95 | 0.005655 ms | ≤16.7 ms |
| Full layout p95 | 1.935832 ms | ≤33.3 ms |
| Full layout p99 / max | 1.937622 / 2.080323 ms | recorded |
| Lifecycle | 100 cycles, 0 failures | 100 cycles, 0 failures |
| Digest | `acdcb3d56c867549bfca77d4d37c148a` | exact Android oracle |
| Layout diagnostics | empty | empty |

The hosted Android x86_64 emulator remains a correctness recorder. It still
checks behavior, digest, layout, lifecycle, and performance sample schema and
records timing, but shared-runner absolute p95 is observational. The
`16.7/33.3 ms` product thresholds remain enforced on representative physical
devices and are not lowered.
