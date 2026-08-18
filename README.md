# Canvas v2

Canvas v2 is a cross-platform **Visual Document Runtime** built with C++20 and
Skia Ganesh. It provides one semantic document, editor, ink, text, scene,
rendering, persistence, and collaboration foundation for replaceable product
shells. The shared C++20 Runtime is validated on Web, Windows, macOS, iOS,
iPadOS, and Android; the initial product shells remain Web, Windows, and
Android.

POC-01 is accepted. POC-02 is Integration Ready / Validating, so POC-03 scene
work, POC-06 FastInk experiments, and R1 foundation work may consume its
experimental contracts while its final latency and physical-device gates
remain Pending. This branch implements POC-03 and remains `Validating` until
its physical Windows/Web/Android and integrated-ink gates have recorded
evidence. POC-04 can advance independently before R1 acceptance. The delivery
order is six focused POCs followed by five productization stages; no production
code should bypass their documented exit gates.

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
- [POC-03 100K Scene implementation](pocs/large_scene/README.md)
- [Prebuilt Skia SDK supply chain](docs/architecture/SKIA_SDK_SUPPLY_CHAIN.md)

## Current sequence

The accepted `POC-01 Shared Engine` has unlocked parallel Ink, Scene, and
RichText work. POC-02 is Integration Ready / Validating: POC-06 may consume its
Preview Model, and POC-03's integrated ink experience gate may consume its Ink
outputs without treating POC-02 as Accepted. POC-05 depends on the POC-03
reserved external-surface pass.

R1 foundation acceptance is blocked by POC-01 through POC-04. POC-05 is a
future-capability risk proof and does not enter V1 product scope. POC-06 may run
alongside R1 but blocks FastInk productization in R3. Product stages then proceed
through the local V1 Runtime, Tier A rendering and shells, Collaboration MVP,
and release hardening.
