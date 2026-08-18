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

- Product Tier A: Web, Windows, and Android receive complete product, device,
  performance, release, and support gates.
- Web reference/product shell: React/TypeScript + WASM + WebGL.
- Windows reference/product shell: React/Tauri + native canvas region + C ABI.
- Android reference/product shell: React Native + native `CanvasView` + JNI. Pen input and canvas
  rendering never pass through React Native JS.
- Portability Tier B: native macOS/iOS/iPadOS harnesses + C ABI + Ganesh Metal
  continuously validate the shared Runtime; Apple product shells are deferred.
- ChromiumOS reuses the Web target. Headless is a V1 test/reference utility,
  not yet a supported public server or batch-rendering product API.
- Runtime: C++20 modules for RuntimeFacade, InputRouter, Document, Operations,
  EditorSession, RichText, InkEngine, Geometry, Layout, HitTest, SceneCompiler,
  shared RuntimeScene, per-view FrameState, FrameBuilder, FrameGraph,
  Compositor, RendererBackend, FrameInvalidationSink, TileCache,
  ResourceBudgetCoordinator, Resources, Persistence, and Collaboration.
- Renderer: Skia Ganesh for v1; Graphite/WebGPU is a future backend.
- Surfaces: platform adapters own native window/surface/context lifecycles and
  provide generation-bound RenderTargets; RendererBackend does not own them.
- Ink: canonical document rendering and low-latency preview are separate paths
  connected through one shared Preview Model and `FastInkBridge`.
- Determinism: canonical numeric storage/encoding, deterministic clock/random,
  semantic ChangeSets, and cross-platform replay are explicit contracts.
- Recovery: an immutable DocumentSnapshot plus committed operation continuation
  deterministically restores a target frontier; snapshots never bypass the
  normal Operation path for editing or undo/redo.
- Scheduling: the Runtime emits revision-bound frame invalidations; platform
  schedulers own VSync/present, while bounded input queues preserve confirmed
  samples independently of render cadence.

## Documents

- [Project framework](docs/PROJECT_FRAMEWORK.md)
- [System architecture](docs/architecture/SYSTEM_ARCHITECTURE.md)
- [Staged delivery plan](docs/planning/STAGED_DELIVERY_PLAN.md)
- [Verification strategy](docs/quality/VERIFICATION_STRATEGY.md)
- [Vibe architecture findings](docs/research/VIBE_ARCHITECTURE_FINDINGS.md)
- [Architecture decisions](docs/adr/README.md)
- [POC-01 implementation](pocs/shared_engine/README.md)
- [POC-02 Ink Engine implementation](pocs/ink_engine/README.md)
- [Prebuilt Skia SDK supply chain](docs/architecture/SKIA_SDK_SUPPLY_CHAIN.md)

## Current sequence

`POC-01 Shared Engine` unlocks parallel Ink, Scene, and RichText work. POC-06
depends on the POC-02 Preview Model; POC-05 depends on the POC-03 reserved
external-surface pass. POC-03's integrated ink experience gate also consumes
POC-02 outputs.

R1 foundation acceptance is blocked by POC-01 through POC-04. POC-05 is a
future-capability risk proof and does not enter V1 product scope. POC-06 may run
alongside R1 but blocks FastInk productization in R3. Product stages then proceed
through the local V1 Runtime, Tier A rendering and shells, Collaboration MVP,
and release hardening.
