# Canvas v2

Canvas v2 is a cross-platform **Visual Document Runtime** built with C++20 and
Skia Ganesh. It provides one semantic document, editor, ink, text, scene,
rendering, persistence, and collaboration foundation for replaceable Web,
Windows, and Android product shells.

This repository is currently in architecture validation. The delivery order is
six focused POCs followed by five productization stages; no production code
should bypass their documented exit gates.

## Fixed architecture baseline

- Web: React/TypeScript + WASM + WebGL.
- Windows: React/Tauri + native canvas region + C ABI.
- Android: React Native shell + native `CanvasView` + JNI. Pen input and canvas
  rendering never pass through React Native JS.
- Runtime: C++20 modules for Document, Operations, EditorSession, RichText,
  InkEngine, SceneCompiler, RuntimeScene, FrameGraph, Compositor, TileCache,
  Resources, and Persistence.
- Renderer: Skia Ganesh for v1; Graphite/WebGPU is a future backend.
- Ink: canonical document rendering and low-latency preview are separate paths
  connected through `FastInkBridge`.

## Documents

- [Project framework](docs/PROJECT_FRAMEWORK.md)
- [System architecture](docs/architecture/SYSTEM_ARCHITECTURE.md)
- [Staged delivery plan](docs/planning/STAGED_DELIVERY_PLAN.md)
- [Verification strategy](docs/quality/VERIFICATION_STRATEGY.md)
- [Vibe architecture findings](docs/research/VIBE_ARCHITECTURE_FINDINGS.md)
- [Architecture decisions](docs/adr/README.md)

## Current sequence

`POC-01 Shared Engine` → `POC-02 Ink Engine` → `POC-03 100K Scene` →
`POC-04 RichText / IME` → `POC-05 Hybrid Surface` → `POC-06 FastInk`

After all POC gates pass, the project proceeds through Runtime Foundation, V1
Runtime, Production Rendering and Shells, Collaboration MVP, then Hardening and
Release.
