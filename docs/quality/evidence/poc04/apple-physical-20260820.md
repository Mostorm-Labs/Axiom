# POC-04 Apple physical-device report — 2026-08-20

Status: **Passed for the Apple UIKit bring-up and controlled semantic-result
gate.** The unified POC-04 acceptance decision is recorded by the aggregate
CI job after the macOS AppKit evidence is included.

## Scope

The same signed arm64 iOS recorder bundle was installed and launched on an
iPhone 15 Pro and an iPad Air (4th generation). The recorder uses the shared
C++20 `TextEditSession` through the UIKit `UITextInput`/`UIKeyInput` adapter.
The app has a visible editor surface, a system-keyboard instruction label, and
records callback metadata to its app data container. No personal signing
configuration is stored in the repository.

## Device and build identity

| Item | iPhone | iPad |
| --- | --- | --- |
| Hardware | iPhone 15 Pro | iPad Air (4th generation) |
| Platform report | `ios` | `ipados` |
| OS | iOS 26.6 | iPadOS 26.6 |
| Architecture | arm64 | arm64 |
| Display | 1179×2556, scale 3 | 1640×2360, scale 2 |
| Connection | CoreDevice, paired/available | CoreDevice, paired/available |

Build environment: Xcode 26.2, iPhoneOS SDK 26.2, deployment target 17.0,
`ios-arm64-metal` RichText SDK from the locked `poc04-richtext-v2` release.
The local bundle was signed with a temporary automatic-development override;
the override is not part of CMake defaults or CI.

## Adapter preflight (deterministic recorder)

Before the controlled system-keyboard run, both devices passed the deterministic
adapter preflight. This is a separate harness check for composition-aware range,
selection, caret geometry, and digest plumbing; it is not the final semantic
IME result and must not be compared with the controlled evidence below.

Both devices installed and launched the same signed bundle and produced the
same preflight recorder digest:

```text
a29042ee2b915fdba42956575b5aecc0
```

Both reports returned:

```text
protocol = UITextInput+UIKeyInput
marked_range = [5, 2]
selection = [6, 1]
caret = [80.00, 0.00, 1.00, 20.00]
```

This covers the shared C++ composition-aware range, selection, caret geometry,
and digest path on both physical device families before real keyboard input.

## System keyboard callback capture

After the deterministic recorder sequence, the app cleared its text and left
the editor focused for system keyboard interaction. The callback recorder
captured the controlled real UIKit event sequence from the active device
keyboard:

| Device | Callback count | Observed callbacks | Marked text observed | Commit observed |
| --- | ---: | --- | --- |
| iPhone 15 Pro | 7 | `setMarkedText`, `unmarkText` | yes | yes |
| iPad Air 4 | 7 | `setMarkedText`, `unmarkText` | yes | yes |

The keyboard was visible and the presented text was rendered by the native
editor view from the same C++ session queried by UIKit. Both devices recorded
`n → ni → ni hao → 你好`, followed by `unmarkText`; final text is `你好` and
the Runtime digest is `d8e61ceae722c2eb4eabefa4af30e4dc` on both devices.

The controlled artifacts were collected from the same signed v2 bundle:

- [iPhone 15 Pro controlled evidence](apple-iphone15pro-ime-20260820.json)
- [iPad Air 4 controlled evidence](apple-ipad-air4-ime-20260820.json)

Both reports include final text, selection, caret, marked range, callbacks,
device identity, v2 SDK identity, and build hashes. They pass
`python3 tools/poc04/validate_apple_ime.py`.

## Remaining gates

- Keep the real browser composition and Windows IMM evidence gates separate.
- The aggregate acceptance job owns the final POC-04 decision.

The local raw JSON artifacts are intentionally kept under ignored `out/` and
are not committed as user text or device logs.
