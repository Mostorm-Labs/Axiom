# POC-04 Apple physical-device report — 2026-08-20

Status: **Passed for the Apple UIKit bring-up and callback-chain gate;** the
real Chinese Pinyin marked-text gate remains pending. This report does not
change POC-04 from `Validating` to `Accepted`.

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

## Shared runtime recorder result

Both devices installed and launched the same bundle and produced the same
semantic recorder digest:

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
and digest path on both physical device families.

## System keyboard callback capture

After the deterministic recorder sequence, the app cleared its text and left
the editor focused for system keyboard interaction. The callback recorder
captured real UIKit events from the active device keyboard:

| Device | Callback count | Observed callbacks | Marked text observed |
| --- | ---: | --- | --- |
| iPhone 15 Pro | 23 | `insertText`, `setSelectedTextRange`, `deleteBackward` | no |
| iPad Air 4 | 76 | `insertText`, `setSelectedTextRange`, `unmarkText` | no |

The keyboard was visible and text was rendered in the editor label. The
captured event streams prove that UIKit calls reach the adapter and update the
same C++ session; they do **not** prove that the Chinese Pinyin pre-edit string
was delivered as `setMarkedText` on these runs. The captured text is therefore
not used as a Chinese-language semantic oracle.

## Remaining gates

- Manually select Chinese Pinyin on each physical device and enter a short
  phrase (for example `ni hao` → `你好`), then recover
  `poc04-ime-system-input.json` with `observed_marked_text` and the final
  commit event.
- Keep the real browser composition and Windows IMM evidence gates separate.
- Run aggregate review only after all required platform evidence is present;
  do not enable cross-platform acceptance or mark POC-04 `Accepted` here.

The local raw JSON artifacts are intentionally kept under ignored `out/` and
are not committed as user text or device logs.
