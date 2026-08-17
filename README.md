# Canvas v2

Canvas v2 is a cross-platform **Visual Document Runtime** built with C++20 and
Skia Ganesh. It provides one semantic document, editor, ink, text, scene,
rendering, persistence, and collaboration foundation for replaceable product
shells. The shared C++20 Runtime is validated on Web, Windows, macOS, iOS,
iPadOS, and Android; the initial product shells remain Web, Windows, and
Android.

This repository is currently implementing and validating POC-01. The delivery order is
six focused POCs followed by five productization stages; no production code
should bypass their documented exit gates.

## Fixed architecture baseline

- Web: React/TypeScript + WASM + WebGL.
- Windows: React/Tauri + native canvas region + C ABI.
- Android: React Native shell + native `CanvasView` + JNI. Pen input and canvas
  rendering never pass through React Native JS.
- Apple runtime validation: native macOS/iOS/iPadOS harnesses + C ABI + Ganesh
  Metal. A product-shell choice for Apple platforms is intentionally deferred.
- Runtime: C++20 modules for RuntimeFacade, InputRouter, Document, Operations,
  EditorSession, RichText, InkEngine, SceneCompiler, shared RuntimeScene,
  per-view FrameState, FrameBuilder, FrameGraph, Compositor, RendererBackend,
  TileCache, Resources, and Persistence.
- Renderer: Skia Ganesh for v1; Graphite/WebGPU is a future backend.
- Surfaces: platform adapters own native window/surface/context lifecycles and
  provide generation-bound RenderTargets; RendererBackend does not own them.
- Ink: canonical document rendering and low-latency preview are separate paths
  connected through one shared Preview Model and `FastInkBridge`.

## Documents

- [Project framework](docs/PROJECT_FRAMEWORK.md)
- [System architecture](docs/architecture/SYSTEM_ARCHITECTURE.md)
- [Staged delivery plan](docs/planning/STAGED_DELIVERY_PLAN.md)
- [Verification strategy](docs/quality/VERIFICATION_STRATEGY.md)
- [Vibe architecture findings](docs/research/VIBE_ARCHITECTURE_FINDINGS.md)
- [Architecture decisions](docs/adr/README.md)
- [POC-01 implementation](pocs/shared_engine/README.md)
- [Prebuilt Skia SDK supply chain](docs/architecture/SKIA_SDK_SUPPLY_CHAIN.md)

## Current sequence

`POC-01 Shared Engine` → `POC-02 Ink Engine` → `POC-03 100K Scene` →
`POC-04 RichText / IME` → `POC-05 Hybrid Surface` → `POC-06 FastInk`

After all POC gates pass, the project proceeds through Runtime Foundation, V1
Runtime, Production Rendering and Shells, Collaboration MVP, then Hardening and
Release.
