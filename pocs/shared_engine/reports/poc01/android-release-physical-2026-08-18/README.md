# POC-01 Android Release Physical Evidence — 2026-08-18

> Result: **Passed for the Android non-debuggable Release physical-device
> subset.** POC-01 remains `Validating`; this report does not close the
> Windows/macOS/iPhone/iPad memory-series or revised environment-metadata
> evidence still listed by the final audit.

## Scope and identity

This report records one physical Pixel 7 run of the arm64-v8a POC-01
acceptance APK. The tested collector/Runtime commit is
`4f1ab8d1833de06cf42508e8f398393a4c6677b8`; it is the first commit contained
unchanged in the squash-merged [PR #11](https://github.com/Mostorm-Labs/canvas/pull/11).
The post-merge `main` commit is
`a87a8269229309dec453c34708326665500c2aa8` and the complete six-platform CI
run [`32092302992`](https://github.com/Mostorm-Labs/canvas/actions/runs/32092302992)
passed. The physical result is bound to the tested commit, not relabelled as a
post-merge execution.

| Input | Identity |
| --- | --- |
| Canvas source under test | `4f1ab8d1833de06cf42508e8f398393a4c6677b8` |
| Merged collector source | `a87a8269229309dec453c34708326665500c2aa8` |
| Skia source | `b6d106297ff9ef2ff8094033695d045e87775581` |
| Skia SDK set | `debcbb7b9376806c94ffb9af5950ebd8a6de0547833f9b57df96a20531ca7817` |
| Android SDK ID | `630e4536e8eaf7ee71a81e29880a934563fbae5d87a9aaef5c2c5a19b544c80f` |
| Replay fixture SHA-256 | `110a9572d54864ae913e28d91a5e392135e22c1dbedc06a88db6d7b425d25724` |
| Checker PNG SHA-256 | `10ee6bb34dfe7ba4d866c1bc7cb828a045ba48e97c971e2ca8df05f66df99f59` |
| Roboto fixture SHA-256 | `466989fd178ca6ed13641893b7003e5d6ec36e42c2a816dee71f87b775ea097f` |
| Reference RGBA SHA-256 | `1b1e4a77a213515469b094ccb77b43be5c75fa7f1d2382f38583ed8aaab51041` |
| Installed APK SHA-256 | `86ce182cdb34bdde50ea0830689b0b8a69e4193ec88a9172fcf9d0aee624193a` |

[`manifest.json`](manifest.json) is the machine-readable source of truth.
The normalized result and visual metrics are committed under [`results/`](results/);
the raw RGBA, logcat, rendered diff PNGs, and file-level checksums are retained
in the external content-addressed evidence asset.

## Device and gate result

| Field | Observed value |
| --- | --- |
| Device | Google Pixel 7 (`panther`), physical USB connection |
| OS / ABI | Android 17, API 37, arm64-v8a |
| Backend | Ganesh GLES3; `ro.hardware.egl=mali` |
| Build | Release, installed package `debuggable=false` |
| Lifecycle | 100 iterations |
| Smoke | 60 seconds, 5,410 frames |
| Maximum draw/submit | 17.2159 ms; passes the unchanged 100 ms ceiling |
| Document digest | `47826449b895ac4f4a57b4f386379775` |
| Numeric corpus | `e8f3bc4f06282fc0a2348aa5059d56fa` |
| Independent replay | `bcdb19afb9eccbf68cec4a7f442b1cd2`, revision 1, sequence 4 |
| Visual | 479,917 / 480,000 pixels within ±2 = 99.982708%; pass |
| RGBA | 1,920,000 bytes; SHA-256 `1e1aa97f011e1d991b4f025c8df45cd96bd0b54ee0e746f0b8c8d4a58ca955a8` |

The APK is signed with a local development key only to permit installation.
The certificate identity is not an acceptance input and is not retained.
`apkanalyzer manifest debuggable` reported `false`, and the physical collector
independently rejected the installed package if `dumpsys package` exposed the
`DEBUGGABLE` flag.

## Memory and performance environment

Host-side `dumpsys meminfo` sampled process total PSS after the native warm-up
marker. Thirteen samples span 60,059 ms at a nominal 5-second interval. The
declared leak screen compares medians of the first and last quartile windows:

| Metric | Value |
| --- | ---: |
| Head median | 75,675,648 bytes |
| Tail median | 73,640,960 bytes |
| Tail change | −2,034,688 bytes / −2.688696% |
| Failure threshold | tail growth greater than 5% |
| Decision | No sustained growth observed; pass |

This is a bounded POC leak signal, not a product memory budget or long thermal
soak. The final sample is retained even though Android reclaimed a substantial
amount of PSS near smoke completion; the quartile-window decision does not
depend on that single value.

Before and after the measured interval, Android reported thermal status `0`,
battery status code `2`, and wired power. Low-power mode and reliable active
VRR state were unavailable and are explicitly labelled as such. The active
display mode ID was `2`; the public shell output did not yield a reliable
current refresh value, while settings reported peak `Infinity` and minimum
`0.0`. The test uses an unbounded native draw/submit loop, so its target frame
interval is intentionally `null`; browser throttling is not applicable.

## Reproduction

Use the locked NDK/Skia SDK, a complete JDK 17, Gradle 8.11.1, and one connected
physical arm64 Android device:

```sh
python3 tools/bootstrap_deps.py --core
python3 tools/skia/fetch.py --target android-arm64-v8a-gles3
cd pocs/shared_engine/platform/android
JAVA_HOME=<JDK_17_HOME> \
ANDROID_HOME=<ANDROID_SDK_ROOT> \
ANDROID_SDK_ROOT=<ANDROID_SDK_ROOT> \
ANDROID_NDK_ROOT=<ANDROID_SDK_ROOT>/ndk/27.2.12479018 \
<GRADLE_8_11_1>/bin/gradle :app:assembleRelease \
  -PcanvasPocAbi=arm64-v8a --no-daemon
cd ../../../..
python3 pocs/shared_engine/tools/run_android_physical.py \
  --apk pocs/shared_engine/platform/android/app/build/outputs/apk/release/app-release.apk \
  --output out/physical/android-release
```

The collector preserves and restores display-size and stay-awake settings,
refuses an emulator or debuggable APK, verifies digest/100/60/frame/memory
gates, and runs the visual comparator. It intentionally omits ADB/device
serial, build fingerprint, accounts, network identifiers, and signing data.

## Retention and remaining work

The Git report excludes the APK, raw RGBA, generated PNGs, and logcat. The
privacy-filtered raw ZIP is published as the content-addressed prerelease
recorded in `manifest.json`; its tag and asset must not be overwritten or
deleted after reference. Any correction uses a new tag and preserves this
result.

This evidence closes the final audit's `android-release-physical` blocker. It
does not close the separate Windows/macOS/iPhone/iPad post-warm-up memory
series or the revised cross-platform performance-environment manifests, so
POC-01 remains `Validating`.
