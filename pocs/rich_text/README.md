# POC-04 RichText / IME

POC-04 is one unified RichText / IME proof across Web, Windows, Android,
macOS, iOS, and iPadOS. It proves that `TextDocument`, edit state, operation
replay, canonical font identity, layout, and native IME state are owned by the
shared C++20 Runtime; each platform only adapts its own input contract.

This directory is experimental. Its C ABI, NDJSON replay schema, snapshot JSON,
and layout dump are POC-only and do not promise R1 source or binary
compatibility. It deliberately does not modify the POC-01 Document/Operation
or SceneCompiler contracts; a future common-foundation PR must own any product
contract shared with POC-02 or POC-03.

## Ownership and state

- `TextDocument` owns committed paragraphs, runs, styles, attributes, UTF-16
  logical positions, revision, and the committed operation sequence.
- `TextEditSession` owns selection, caret, focus, composition preview, undo and
  redo. Composition never enters the document before commit.
- Each commit is one `TextTransaction`; cancel creates no transaction. Undo and
  redo apply replayable transactions instead of mutating text through a side
  door.
- `FontResourceResolver` accepts only declared `FontResourceId + SHA-256 +
  bytes`; no canonical path calls a system font manager.
- A committed `TextStyle` carries an ordered content-addressed fallback chain.
  The POC fixture binds Roboto to the pinned Noto Sans CJK subset so the
  fallback decision is part of snapshot, replay, and digest semantics.
  Skia's subset contains only `是`; canonical geometry uses that glyph, while
  the independent edit/IME behavior corpus continues to use `中文拼音`.
- `SkParagraphTextLayout` is the canonical layout backend. The
  `DeterministicTextLayout` host probe only exercises edit geometry while the
  RichText SDK is being produced and is never a shaping oracle.

The Android data path is:

```text
React Native shell (host/lifecycle only)
              |
      NativeCanvasView
              |
        InputConnection
              |
             JNI
              |
  C++ TextEditSession/TextDocument
```

Committed and composing text does not pass through RN JS.

## Local host-core validation

```sh
python3 tools/bootstrap_deps.py --core --font-only
cmake -S pocs/rich_text -B out/poc04-host -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build out/poc04-host --parallel
ctest --test-dir out/poc04-host --output-on-failure
out/poc04-host/canvas_poc04_cli --lifecycle=100
```

The host build does not consume Skia. It verifies the semantic model,
transactional replay, snapshot round-trip, composition state machine, resource
identity, probe geometry, lifecycle, and performance harness.

## RichText Skia SDK bootstrap

POC-01's `poc01-minimal-v1` SDK disables HarfBuzz and ICU, so it cannot satisfy
POC-04. The historical `poc04-richtext-v1` release built and packaged
SkParagraph, SkShaper, SkUnicode, bundled HarfBuzz/ICU, the platform Ganesh
backend, and fixed Latin/CJK font fixtures for these four targets:

- `windows-x64-d3d12`
- `web-wasm-webgl2`
- `android-arm64-v8a-gles3`
- `android-x86_64-gles3`

That four-target profile and its committed lock remain immutable for the
already recorded Web/Windows/Android evidence. The current producer workflow
uses the successor `poc04-richtext-v2` profile, which retains those four
targets and adds the Apple SDK supply-chain targets:

- `macos-arm64-metal`
- `ios-arm64-metal`
- `ios-simulator-arm64-metal`

The iOS device archive is shared by iPhone and iPad hardware; the simulator
archive is shared by iPhone and iPad simulator devices. These SDK producer
targets prove deterministic packaging and source-free RichText linking. They
do not, by themselves, claim AppKit `NSTextInputClient`, UIKit `UITextInput`,
or macOS/iOS/iPadOS behavior acceptance. The seven-target
`skia-sdk-poc04-richtext-v2-72f006b19ac77233` prerelease is now published from
`main` and locked by `skia-sdk.lock.json`. The lock uses the renamed
`Mostorm-Labs/Axiom` repository; the historical v1 lock remains unchanged in
Git history.

The `POC-04 RichText Skia SDK Producer` workflow now uses the seven-target
`poc04-richtext-v2` profile. After its immutable prerelease is published,
generate the replacement consumer lock with:

```sh
python3 tools/skia/update_lock.py \
  --tag <published-tag> \
  --output pocs/rich_text/skia-sdk.lock.json
```

Consumer CI never checks out or builds Skia source. Web, Windows, and Android
consume the v2 lock, and the Apple job source-free builds the AppKit/UIKit
adapter against the macOS and iOS-simulator SDK targets.

## Apple native IME adapter

`platform/apple/apple_ime_adapter.mm` is an Objective-C++ bridge over the same
`TextEditSession`, not a second text model. On macOS it implements the required
`NSTextInputClient` methods, including replacement ranges, marked text,
selection queries, attributed substrings, screen-coordinate composition
geometry, and point hit-testing. On iOS and iPadOS it implements `UITextInput`
plus `UIKeyInput`, custom UTF-16 `UITextPosition`/`UITextRange`/
`UITextSelectionRect`, selection and composition callbacks, caret/selection
geometry, and hit-testing. The simulator recorder runs on both an iPhone and
an iPad; both reuse `ios-simulator-arm64-metal` while producing separate
behavior artifacts. Real hardware keyboard/pinyin evidence is part of this
same POC's native-IME track, not a separate Apple POC.

The Windows RichText SDK also packages the pinned `icudtl.dat` next to the
static archives. CMake copies it beside the demo executable because Skia's
bundled Windows ICU loader resolves that runtime data file from the executable
or library directory.

## POC exit evidence

The canonical Runtime recorder now emits measured behavior, digest,
SkParagraph geometry, 100-cycle lifecycle, and 10K-character latency artifacts
from Web/Chromium, Windows, and Android/emulator consumers. It does not claim
to synthesize native pinyin IME keystrokes: real browser IME, Win32 IMM, and
Android InputConnection event evidence remains a separate manual/device gate.

The v2 physical Android gate was executed on a Pixel 7 and is recorded in
[`docs/quality/evidence/poc04/android-physical-20260820-v2.md`](../../docs/quality/evidence/poc04/android-physical-20260820-v2.md).
The hosted x86_64 emulator remains a correctness recorder; representative
performance is gated on the physical device without lowering the 16.7/33.3 ms
thresholds.

The Apple physical bring-up gate was executed on an iPhone 15 Pro and an iPad
Air 4 and is recorded in
[`docs/quality/evidence/poc04/apple-physical-20260820.md`](../../docs/quality/evidence/poc04/apple-physical-20260820.md).
Both devices displayed the editor, showed the system keyboard, and delivered
real UIKit callbacks into the shared C++ session. The latest signed v2 bundle
captured the controlled `setMarkedText` sequence `n → ni → ni hao → 你好`,
`unmarkText`, final text `你好`, and identical digest on both devices. The
Apple controlled semantic-result gate is now closed. The macOS AppKit recorder
also routes real key events through `interpretKeyEvents:` and records the
native `setMarkedText`/`insertText` sequence; its controlled report is
[`docs/quality/evidence/poc04/macos-ime-20260820.json`](../../docs/quality/evidence/poc04/macos-ime-20260820.json).

The final unified evidence must cover English, simplified Chinese,
pinyin composition, newline, mixed runs, selection, caret, clipboard,
undo/redo, cancel/commit atomicity, digest, fixed-font line/cluster/selection
geometry, missing/corrupt/fallback resources, 10K-character latency, and 100
focus/unfocus/view-destroy cycles. `tools/behavior_conformance.py` rejects an
incomplete canonical platform set or any semantic/layout mismatch.

## Unified acceptance tracks

POC-04 has one status and one exit decision. Its evidence is collected through
two complementary tracks:

1. **Canonical Runtime track** — Web, Windows, and Android compare the same
   digest, behavior matrix, fixed-font layout, performance, and lifecycle
   artifact.
2. **Native IME track** — Web, Windows, Android, macOS, iOS, and iPadOS prove
   real platform callback delivery, composition/commit/cancel mapping, final
   text state, selection/caret updates, and view lifecycle behavior. Platforms
   are not required to emit identical callback names; they must produce the
   same Runtime semantics.

Neither track independently changes the status. The unified acceptance job
requires both tracks, the controlled `ni hao → 你好` semantic-result evidence,
and the v2 SDK identity. Once that aggregate passes, the single POC-04 status
is `Accepted`.

## Windows and Chrome Stable physical revalidation

Run the physical validation from a clean checkout of
`codex/poc04-windows-web-revalidation` on the Windows machine. The preparation
script consumes only the locked `poc04-richtext-v2` SDKs, builds and tests the
Windows canonical target, builds the Web/WASM recorder, and prints the exact
commands for both interactive gates:

```powershell
powershell -ExecutionPolicy Bypass -File tools/poc04/prepare_windows_web_validation.ps1
```

For Windows, select Microsoft Pinyin in the Win32 recorder, enter `ni hao`,
commit `你好`, and close the window. For Web, serve the printed recorder
directory and open `physical_recorder.html` in installed Chrome Stable; repeat
the same controlled input and download its JSON report. Validate both reports
before archiving them:

```powershell
python tools/poc04/validate_physical_ime.py `
  out/poc04-windows-web-revalidation/windows/windows-ime.json `
  $HOME/Downloads/poc04-chrome-ime.json
```

The reports deliberately keep the final controlled text because the semantic
result is the gate under test. Other IME updates are represented by callback
metadata; no unrelated user text should be entered in either recorder.
