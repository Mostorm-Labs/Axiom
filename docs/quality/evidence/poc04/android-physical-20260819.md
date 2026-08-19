# POC-04 Android physical-device report — 2026-08-19

Status: **Passed for the Android physical-device gate**. This report records
canonical Runtime/SkParagraph performance and real Gboard `InputConnection`
delivery on one physical Android device. It does not satisfy the independent
real-browser and Windows IMM gates, does not enable cross-platform acceptance,
and does not change POC-04 from `Validating` to `Accepted`.

## Locked identity

| Item | Value |
| --- | --- |
| Runtime source commit before evidence-only fix | `d7ae1472dd39e237098d28d9794568424413b25a` |
| Skia commit | `b6d106297ff9ef2ff8094033695d045e87775581` |
| RichText SDK profile / target | `poc04-richtext-v1` / `android-arm64-v8a-gles3` |
| RichText SDK ID | `816c4d2e838381fc6d3cd300b524eb428c9bddb71948fbb73b16245d73acd30d` |
| SDK set ID | `592fc75a4b247fe9cd4fb88b0c36b1b6d71cd70b0ed5412702f8990cbe687b29` |
| NDK / API contract | `27.2.12479018` / minimum API 26 |
| Final test APK SHA-256 | `b3e6896d46324b74f61a2c612a93792aaa513a76ceb7751a52230664d45e7573` |
| Final behavior artifact SHA-256 | `d7f5694aff93cb6909091cbe1333f1b5bc2103115e4ec969c0a95c557b750073` |

The evidence-only Android change requests the soft keyboard again after the
window obtains focus and logs callback type, UTF-16 length, and selection only.
It does not log user text or change the C++ Runtime, Document, operation,
layout, digest, or performance implementation.

## Device and environment

| Item | Observation |
| --- | --- |
| Test window | 2026-08-19 13:40–13:54, Asia/Shanghai |
| Device | Google Pixel 7 (`panther`), physical USB device |
| ABI | `arm64-v8a` |
| Android | Android 17 / API 37 |
| Build fingerprint | `google/panther/panther:17/CP2A.260705.006/15641320:user/release-keys` |
| Display | 1080×2400 physical pixels, density 420, landscape during IME evidence |
| System locales | `zh-Hans-HK,en-US,ja-JP` |
| IME | Gboard `17.7.4.932364120-release-arm64-v8a` |
| Thermal | status 0 before and after measurement; no throttling reported |
| Battery | 100%, AC powered during measurement |
| Post-run process memory | 27,712 KiB total PSS; 138,348 KiB total RSS |

Performance was measured inside the optimized Release APK. Each canonical
record contains 20 warm-up plus 120 measured input/caret samples, and 5
warm-up plus 30 measured fresh-Paragraph shaping/line-breaking samples. It is
a Runtime CPU measurement, not an input-to-photon or frame-presentation
measurement.

## Canonical Runtime results

The final modified-APK record passed the complete behavior matrix, digest,
fixed-font geometry, diagnostics, performance, and lifecycle checks.

| Gate | Result | Required |
| --- | ---: | ---: |
| Input/caret p95 | 0.006063 ms | ≤16.7 ms |
| Full layout p95 | 1.922851 ms | ≤33.3 ms |
| Full layout p99 / max | 2.030396 / 2.069295 ms | recorded, no separate gate |
| Lifecycle | 100 cycles, 0 failures | 100 cycles, 0 failures |
| Digest | `acdcb3d56c867549bfca77d4d37c148a` | exact oracle |
| Layout diagnostics | empty | empty |

Three additional cold-start physical records were collected before the final
evidence-only APK rebuild:

| Run | Input/caret p95 | Full layout p95 | Full layout max | Digest / lifecycle |
| --- | ---: | ---: | ---: | --- |
| 1 | 0.005412 ms | 1.986613 ms | 2.276896 ms | exact / 100, 0 failures |
| 2 | 0.006185 ms | 1.984823 ms | 2.188395 ms | exact / 100, 0 failures |
| 3 | 0.007446 ms | 2.088705 ms | 2.121989 ms | exact / 100, 0 failures |

The final physical record is byte-equivalent to CI Android x86_64 for digest,
behavior, canonical layout, and lifecycle. CI emulator performance remains a
separate environment and was 0.008336 ms input/caret p95 and 27.990252 ms full
layout p95 in run
[`32216734527`](https://github.com/Mostorm-Labs/canvas/actions/runs/32216734527).

## Real Android IME evidence

The test used the actual Gboard window and screen taps on the physical device;
it did not call the C++ composition API directly.

1. Android `dumpsys input_method` reported `mInputShown=true` and
   `mIsInputViewShown=true`.
2. The served editor was
   `com.mostorm.canvas.poc04.NativeCanvasView`, and the served connection was
   `NativeCanvasView$onCreateInputConnection$1` with input type
   `TYPE_CLASS_TEXT | TYPE_TEXT_FLAG_MULTI_LINE | TYPE_TEXT_FLAG_NO_SUGGESTIONS`.
3. Gboard logged `onStartInput` and `onStartInputView` for
   `com.mostorm.canvas.poc04`, then activated
   `AsyncChineseProcessorBasedIme`.
4. Physical taps entered `ni hao`; Gboard displayed the Chinese candidate
   `你好`. Selecting it delivered `CanvasPoc04Ime: commitText length=2
   composing=false` to the native View.
5. English keyboard taps independently delivered five `commitText length=1`
   callbacks.

Gboard kept the pinyin pre-edit string inside its own IME UI and did not call
the application's `setComposingText` for this candidate flow. The repository's
synthetic state-machine tests still cover begin/update/cancel/commit semantics,
but this physical record claims only the callbacks actually observed. No user
text was written to logcat; only callback metadata was logged.

## Reproduction

```sh
python3 tools/bootstrap_deps.py --core --font-only
python3 tools/skia/fetch.py \
  --profile tools/skia/profiles/poc04-richtext-v1.json \
  --lock pocs/rich_text/skia-sdk.lock.json \
  --install-root .deps/skia-sdk-poc04 \
  --target android-arm64-v8a-gles3

JAVA_HOME="<complete JDK 17+>" gradle \
  -p pocs/rich_text/platform/android \
  :app:assembleRelease \
  -PcanvasPoc04Abi=arm64-v8a \
  -PcanvasPoc04CanonicalBehavior=true \
  --no-daemon

ANDROID_SERIAL=<physical-device-serial> \
  bash pocs/rich_text/platform/android/record_canonical_behavior.sh
```

The temporary `show_ime_with_hard_keyboard=1` device setting used to expose
Gboard during USB interaction was restored to its original value `0` after
collection. The test APK remains installed; it can be removed with
`adb uninstall com.mostorm.canvas.poc04` when no longer needed.

## Remaining POC-04 gates

- Record real browser composition events on the selected physical Web
  environment.
- Record real Windows IMM messages on the Windows validation machine.
- Resolve and enable the cross-platform canonical layout comparator only after
  its numeric JSON representation is canonical across Web and native writers.
- Run the aggregate review before changing POC-04 to `Accepted`.
