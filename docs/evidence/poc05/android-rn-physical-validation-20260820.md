# POC-05 Android React Native physical validation — 2026-08-20

Status: **Passed for the POC-05 controlled-overlay architecture risk proof.**

The Android runner uses React Native 0.84.1 with Fabric enabled. Its native
`AxiomCanvasSurfaceView` renders the 100,000-node experimental scene through
JNI and Skia Ganesh/GLES3; `AxiomHybridSurfaceView` owns a real
`android.webkit.WebView` and a real `TextureView`/`MediaPlayer` video surface.
Per-frame Canvas rendering, gestures and overlay placement stay in the native
data plane rather than React JS.

## Device and binary checks

| Item | Result |
| --- | --- |
| Device | Pixel 7, arm64-v8a |
| Shell | React Native 0.84.1 / Fabric |
| Canvas | Native `SurfaceView` + JNI + Skia Ganesh/GLES3 |
| Scene | 100,000 experimental POC-03 nodes |
| External surfaces | Android WebView + TextureView/MediaPlayer |
| Native library | `libcanvas_poc05_android.so`, 16 KiB LOAD alignment passed |

## Physical manual gate

The physical run confirmed native single-finger pan outside the WebView,
two-finger pan/zoom around the gesture center, WebView focus/IME, Hide/Show,
Fail/Recover, page switching, recreate and background/foreground recovery.
The WebView intentionally owns single-pointer scrolling and text interaction
inside its bounds; two-pointer Canvas manipulation is forwarded to the native
Canvas owner.

After the gesture/lifecycle corrections, WebView-area pinch responsiveness and
background/foreground behavior were acceptable for this POC. Minor video
placement jitter during active transform remains a known POC limitation and
is not a release-quality guarantee.

The runner contains a 100 ms JS-stall probe and records native placement
frames, but the manual run did not visibly expose the stall label. No archived
Android result JSON is claimed for that observation. The POC-05 architecture
acceptance relies on the demonstrated native ownership boundary and the wider
cross-platform corpus; an archived JS-stall trace remains R1/R3 hardening.

## Scope

The Android JNI runner directly links experimental POC-03 C++ scene types and
therefore records `runtime_c_abi_binary_conformance: false`. It proves the RN
Shell/native Canvas/overlay structure, not the final product Runtime SDK or
binary ABI integration.
