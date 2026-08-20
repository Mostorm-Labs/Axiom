# POC-04 unified acceptance — 2026-08-20

Status: **Accepted**, subject to the blocking `POC-04 RichText and IME /
cross-platform-acceptance` CI job remaining green on the acceptance commit.

## Canonical Runtime track

Web/Chromium, Windows D3D12 and Android run the same behavior corpus and must
produce byte-equivalent digest, behavior and canonical SkParagraph layout.
The aggregate job also enforces 100 lifecycle cycles, the complete performance
schema, and the 16.7/33.3 ms product thresholds. The hosted Android emulator's
absolute timing is observational; the Pixel 7 report owns the representative
device performance gate without changing the thresholds.

## Native IME track

| Platform | Native path | Controlled result |
| --- | --- | --- |
| Chrome Stable | composition/beforeinput | `ni hao → 你好` |
| Windows | Win32 IMM | `ni hao → 你好` |
| Android | Native CanvasView/InputConnection/Gboard | candidate `你好`, commitText length 2 |
| macOS | keyDown/interpretKeyEvents/NSTextInputClient | `n → ni → 你 → h → ha → hao → 你好` |
| iPhone 15 Pro | UIKit UITextInput/UIKeyInput | `ni hao → 你好` |
| iPad Air 4 | UIKit UITextInput/UIKeyInput | `ni hao → 你好` |

The macOS audit found and fixed a missing `keyDown:` → `interpretKeyEvents:`
route. The committed evidence therefore comes from real AppKit input callbacks,
not direct adapter method calls. Platform callback shapes may differ, but each
final report contains committed text, selection, caret and Runtime digest.

## Evidence index

- [Windows and Chrome revalidation](windows-web-physical-20260820-r2.md)
- [Android Gboard physical input](android-physical-20260819.md)
- [Android v2 Runtime/performance](android-physical-20260820-v2.md)
- [Android machine-readable complementary evidence](android-ime-20260820.json)
- [macOS controlled AppKit input](macos-ime-20260820.json)
- [iPhone/iPad controlled UIKit input](apple-physical-20260820.md)

This acceptance proves RichText rendering/editing and platform IME feasibility.
It does not claim production-editor completeness: advanced selection handles,
context menus, accessibility, bidirectional text, typography breadth and final
interaction polish remain R2/R3 product work.
