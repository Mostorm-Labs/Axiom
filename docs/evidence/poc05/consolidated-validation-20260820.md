# POC-05 Hybrid Surface consolidated validation — 2026-08-20

Status: **Accepted as an experimental future-capability architecture risk proof.**

This report consolidates the POC-05 evidence for the controlled external
surface boundary. It supersedes the status wording in the earlier scoped
reports, but keeps those reports and their raw artifacts as the audit trail.
POC-05 acceptance means that the controlled Overlay model, lifecycle, focus,
failure and platform-shell boundaries are viable. It does not add
`ExternalSurface`, `Video` or `Embed` to the V1 Document schema, and it does
not promote POC-only C++ scene bridges to the product Runtime ABI.

## Cross-platform result

| Shell / target | Native canvas path | External surfaces | Evidence | Result |
| --- | --- | --- | --- | --- |
| Web / WASM + WebGL2 | Accepted POC-01 WASM Canvas baseline + React-controlled DOM overlay harness | iframe + HTML video | [WASM/WebGL2 evidence](../../quality/evidence/poc01/windows-web-revalidation-20260818.md) and [Web overlay harness](../../../pocs/hybrid_surface/platform/web/tests/hybrid_surface.spec.mjs) | Passed |
| Windows RNW | RNW 0.84.0 / RN 0.84.1 New Architecture/Fabric; Skia Ganesh/D3D12 | WebView2 + video | [Windows RNW physical report](../../quality/evidence/poc05/windows-rnw-scene-physical-20260820.md) | Passed |
| Android RN | RN 0.84.1; native CanvasView/JNI; Skia Ganesh/GLES3 | `android.webkit.WebView` + `TextureView`/`MediaPlayer` | [Pixel 7 physical report](android-rn-physical-validation-20260820.md) and [Android runner](../../../pocs/hybrid_surface/platform/react_native/android) | Passed |
| iOS / iPadOS RN | RN 0.84.1/Fabric; Skia Ganesh/Metal | `WKWebView` + native video layer | [Apple RN/Fabric report](apple-rn-fabric-validation-20260820.md) | Passed |

The Web conclusion composes two independently scoped results: accepted POC-01
provides the real single-threaded `canvas_poc01_web.wasm` + Ganesh/WebGL2
Canvas path, while the POC-05 harness proves the DOM Overlay and z-order
behavior with local iframe/video content, placement updates, focus handoff,
failure recovery and 100 lifecycle iterations. The POC-05 harness does not
rebuild another WASM binary; R1/R3 must integrate these already-validated
boundaries in the product Web Shell.

## Acceptance evidence

- Overlay placement is derived from one world-to-view contract and is bounded
  by the platform surface target generation.
- Web, Windows RNW, Android RN and Apple RN/Fabric all materialize a real
  external Web surface and a real video surface; no colored rectangle is used
  as a substitute for either.
- Pan/zoom, clipping, hide/show, page switching, focus handoff, failure
  placeholder/recovery and recreate/background lifecycle paths passed in the
  platform-specific evidence.
- The RN implementations keep high-frequency placement and Canvas rendering
  outside RN JS. Apple records the JS-stall probe; Android instruments the same
  path, while its unobserved manual label is retained as an R1/R3 evidence gap.
- Windows RNW recorded a 100,000-node scene, Skia Ganesh/D3D12 readback,
  18 placement applications, zero stale frames and zero backend failures.
- Android recorded the 16 KiB-aligned arm64 library; the Pixel 7 manual gate
  passed for pan/zoom, WebView IME/focus, lifecycle and overlay recovery, with
  minor video jitter retained as a POC limitation.
- iPad Air 4 and iPhone 15 Pro passed the RN/Fabric physical gate.

## Deliberate boundaries

This evidence validates RNW and RN/Fabric as viable Shell implementations; it
does not silently replace the accepted Product Tier A Shell decision in
ADR-0015. Windows React/Tauri remains the current product choice, while RNW is
now an evidence-backed alternative subject to a separate product/platform
decision. Likewise, Apple RN/Fabric validation proves portability and the
native data-plane boundary, not an Apple V1 release commitment.

The Windows and Android physical runners currently link the experimental
POC-03 C++ `RuntimeScene` implementation to exercise the 100K scene. The
Apple runner compiles the same experimental scene sources into its local
Fabric pod. These are validation bridges and are recorded as
`runtime_c_abi_binary_conformance: false`; R1/R3 must replace them with the
stable Runtime C ABI/SDK consumer path.

POC-05 also does not validate arbitrary DOM/native nodes inserted between
Canvas draw passes, texture import/zero-copy, complex masks/effects, or
product AVPlayer/WebView2 policy. The accepted result is specifically the
controlled overlay band:

```text
Product UI
    ↓
Controlled external overlay (WebView / Video)
    ↓
Canonical Canvas
```

## Disposition

POC-05 is **Accepted** for the architecture risk proof. Future product work
must preserve the stable C ABI boundary, the fixed z-order, generation-bound
placement and native hot-path ownership. A later product ADR is required if
the product needs arbitrary in-canvas DOM/native insertion, zero-copy texture
composition or an external-surface node in the V1 schema.
