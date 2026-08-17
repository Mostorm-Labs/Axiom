# POC-01 Mobile Physical-Device Evidence — 2026-08-17

> Result: **Passed for the iOS, iPadOS, and Android physical-device subset.**
> POC-01 remains `Validating` until the same-machine Windows D3D12 and Chrome
> Web physical-GPU bundle is reviewed and archived.

## Scope and evidence binding

This report records physical-device execution of the POC-01 shared C++20
Runtime at source commit
`5ab8b16bdac8f982a9d221d1f48d3867dda7b43c`. It covers the reviewed fixture,
canonical numeric corpus, independent empty-Document operation replay, GPU
readback, 100 lifecycle iterations, and the 1,000-node 60-second smoke.

The report is bound to:

| Input | Identity |
| --- | --- |
| Canvas source | `5ab8b16bdac8f982a9d221d1f48d3867dda7b43c` |
| Skia source | `b6d106297ff9ef2ff8094033695d045e87775581` |
| Skia SDK release | `skia-sdk-poc01-minimal-v1-debcbb7b9376806c` |
| Skia SDK set | `debcbb7b9376806c94ffb9af5950ebd8a6de0547833f9b57df96a20531ca7817` |
| iOS SDK ID | `eab7bc961815a59c8427efefc0cabb360c73ab7e679bf8e81c729426e397e8ea` |
| Android SDK ID | `630e4536e8eaf7ee71a81e29880a934563fbae5d87a9aaef5c2c5a19b544c80f` |
| Replay fixture SHA-256 | `110a9572d54864ae913e28d91a5e392135e22c1dbedc06a88db6d7b425d25724` |
| Checker image SHA-256 | `10ee6bb34dfe7ba4d866c1bc7cb828a045ba48e97c971e2ca8df05f66df99f59` |
| Roboto fixture SHA-256 | `466989fd178ca6ed13641893b7003e5d6ec36e42c2a816dee71f87b775ea097f` |
| Golden RGBA SHA-256 | `1b1e4a77a213515469b094ccb77b43be5c75fa7f1d2382f38583ed8aaab51041` |

[`manifest.json`](manifest.json) is the machine-readable source of truth. The
JSON files under [`results/`](results/) are normalized review copies; the
manifest records the SHA-256 of the exact raw files in the external evidence
bundle.

## Devices and results

Device serial numbers, UDIDs, ECIDs, user-assigned device names, signing
identities, and Apple Development Team identifiers are intentionally omitted.

| Platform | Physical device | OS / GPU | Backend | Lifecycle | Smoke | Max submit | Visual match | Result |
| --- | --- | --- | --- | ---: | ---: | ---: | ---: | --- |
| iOS | iPhone 15 Pro (`iPhone16,1`) | iOS 26.6 (`23G71`) / Apple A17 Pro GPU | Ganesh Metal | 100 | 60 s / 3,600 frames | 6.16062 ms | 99.982708% | Pass |
| iPadOS | iPad Air, 4th generation (`iPad13,1`) | iPadOS 26.6 (`23G71`) / Apple A14 GPU | Ganesh Metal | 100 | 60 s / 3,600 frames | 9.51679 ms | 99.982708% | Pass |
| Android | OPPO `PKD130` | Android 15 / Mali-G57 MC2, GLES 3.2 | Ganesh GLES3 | 100 | 60 s / 3,600 frames | 29.0006 ms | 99.982708% | Pass |

All three runs produced:

- Document digest `47826449b895ac4f4a57b4f386379775`.
- Pixel hash `32186d8e0c941ec118342f811a0a555e`.
- Core corpus digest `e8f3bc4f06282fc0a2348aa5059d56fa`.
- Independent replay digest `bcdb19afb9eccbf68cec4a7f442b1cd2`,
  revision `1`, operation sequence `4`.
- The same 1,920,000-byte RGBA artifact with SHA-256
  `1e1aa97f011e1d991b4f025c8df45cd96bd0b54ee0e746f0b8c8d4a58ca955a8`.

The visual gate requires at least 99.9% of pixels to keep every channel within
±2. Each device matched 479,917 of 480,000 pixels (99.982708%). The maximum
channel delta of 184 occurs in the 83 pixels outside tolerance; the reviewed
diff localizes them to the curve boundary, and the ratio-based gate passes.

## Environment and observations

The Apple universal runner was a Release build produced on macOS 26.6.1
(`25G76`) with Xcode 26.2 (`17C52`), iPhoneOS SDK 26.2, CMake 4.0.1, and
deployment target 17.0. Its executable SHA-256 was
`2f2b2dd3436176566dde09d9e47b02f20a82140f2871867bc8db515938bd6f84`.
The immutable Skia archive it consumed was produced by the toolchain recorded
in `skia-sdk.lock.json`; the consumer build did not rebuild Skia.

The Android APK was built for `arm64-v8a` with Temurin 17.0.20+8, Gradle
8.11.1, Android Gradle Plugin 8.10.1, CMake 3.30.5, NDK 27.2.12479018,
compile/target SDK 35, and minimum SDK 26. It was a debuggable POC acceptance
artifact with SHA-256
`cd23379a1531122988bcee411fbff2390eb74e6b67b31845b496586e87140a40`.
The Android crash buffer was empty; ActivityManager recorded only the
intentional pre-run force-stop. Host-side PSS samples did not show monotonic
growth during the measured run; see
[`android-memory-samples.json`](results/android-memory-samples.json).

These `max_frame_ms` values prove only the POC-01 `<100 ms` draw/submission
gate. The runner excludes presentation latency and the one-time RGBA readback;
the Android APK is debuggable. The values are not product performance,
input-latency, thermal-soak, or release-build claims. The Apple run did not
capture a quantitative process-memory time series, so this report makes no
Apple memory-budget claim.

## Reproduction

### iPhone and iPad

Use a local signing identity and substitute local device identifiers. Never
commit those values.

```sh
python3 tools/bootstrap_deps.py --core
python3 tools/skia/fetch.py --target ios-arm64-metal
cmake --preset ios-device-release \
  -DCANVAS_SKIA_SDK_ROOT="$PWD/.deps/skia-sdk/ios-arm64-metal"
xcodebuild \
  -project out/ios-device-release/canvas_v2_pocs.xcodeproj \
  -scheme canvas_poc01_ios_runner \
  -configuration Release \
  -sdk iphoneos \
  -destination 'id=<LOCAL_DEVICE_UDID>' \
  -allowProvisioningUpdates \
  CODE_SIGN_STYLE=Automatic \
  DEVELOPMENT_TEAM=<LOCAL_DEVELOPMENT_TEAM> \
  build
xcrun devicectl device install app \
  --device <LOCAL_CORE_DEVICE_ID> \
  out/ios-device-release/pocs/shared_engine/platform/apple/Release-iphoneos/canvas_poc01_ios_runner.app
xcrun devicectl device process launch \
  --device <LOCAL_CORE_DEVICE_ID> --terminate-existing \
  dev.mostorm.canvas.poc01
xcrun devicectl device copy from \
  --device <LOCAL_CORE_DEVICE_ID> \
  --domain-type appDataContainer \
  --domain-identifier dev.mostorm.canvas.poc01 \
  --source Documents/poc01-result.json \
  --destination out/physical/<DEVICE>/poc01-result.json
xcrun devicectl device copy from \
  --device <LOCAL_CORE_DEVICE_ID> \
  --domain-type appDataContainer \
  --domain-identifier dev.mostorm.canvas.poc01 \
  --source Documents/apple-actual.rgba \
  --destination out/physical/<DEVICE>/apple-actual.rgba
```

After the on-device runner finishes, retrieve `Documents/poc01-result.json`
and `Documents/apple-actual.rgba` from the application data container, then run
the visual command below.

### Android

Use a Java 17 runtime and the locked Android SDK/NDK versions. Preserve and
restore the device display and stay-awake settings even when a command fails.

```sh
python3 tools/bootstrap_deps.py --core
python3 tools/skia/fetch.py --target android-arm64-v8a-gles3
cd pocs/shared_engine/platform/android
JAVA_HOME=<JDK_17_HOME> \
ANDROID_HOME=<ANDROID_SDK_ROOT> \
ANDROID_SDK_ROOT=<ANDROID_SDK_ROOT> \
ANDROID_NDK_ROOT=<ANDROID_SDK_ROOT>/ndk/27.2.12479018 \
<GRADLE_8_11_1>/bin/gradle :app:assembleDebug \
  -PcanvasPocAbi=arm64-v8a --no-daemon
cd ../../../..
adb install -r pocs/shared_engine/platform/android/app/build/outputs/apk/debug/app-debug.apk
adb shell wm size 800x600
adb shell settings put global stay_on_while_plugged_in 7
adb logcat -c
adb shell am force-stop dev.mostorm.canvas.poc01
adb shell am start -W \
  -n dev.mostorm.canvas.poc01/dev.mostorm.canvas.CanvasPocActivity
# Wait for CANVAS_POC01_RESULT, then retrieve both artifacts.
adb logcat -d -s CanvasPOC01:I '*:S' | \
  sed -n 's/.*CANVAS_POC01_RESULT //p' | tail -1 \
  > out/android-result.json
adb exec-out run-as dev.mostorm.canvas.poc01 \
  cat files/android-actual.rgba > out/android-actual.rgba
# Restore the values captured before the test.
adb shell wm size reset
adb shell settings put global stay_on_while_plugged_in 0
```

### Visual gate

```sh
python3 pocs/shared_engine/tools/visual_compare.py \
  --expected pocs/shared_engine/goldens/reference.rgba \
  --actual <DEVICE_ACTUAL_RGBA> \
  --artifacts <DEVICE_VISUAL_OUTPUT> \
  --backend <REVIEWED_BACKEND_NAME> \
  --skia-commit b6d106297ff9ef2ff8094033695d045e87775581 \
  --tolerance 2 \
  --minimum-ratio 0.999
```

## Artifact retention and remaining work

The Git report intentionally excludes large RGBA files, generated PNGs,
diagnostic logs, signed Apple bundles, APKs, and device-identity records. The
privacy-filtered evidence asset is named
`poc01-mobile-physical-2026-08-17.zip`; its exact SHA-256 and contents are
recorded in [`manifest.json`](manifest.json). It is archived in the
content-addressed GitHub prerelease
[`poc01-mobile-physical-2026-08-17-62d1ae02ffaa15bd`](https://github.com/Mostorm-Labs/canvas/releases/tag/poc01-mobile-physical-2026-08-17-62d1ae02ffaa15bd),
which targets the tested source commit. The Release asset digest reported by
GitHub exactly matches the manifest SHA-256. Actions artifacts alone are not
sufficient long-term retention. GitHub currently reports the Release itself as
not server-locked; repository policy therefore forbids overwriting or deleting
this referenced tag or asset, and any correction must use a new
content-addressed tag.

This report closes only the three mobile physical-device gates. It does not
close POC-01 or replace the pending same-machine Windows D3D12 and Chrome Web
physical-GPU benchmark bundle.
