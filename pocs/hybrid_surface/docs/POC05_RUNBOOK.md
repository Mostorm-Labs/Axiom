# POC-05 Hybrid Surface Runbook

## Scope

This POC validates only the future-capability boundary described in
`docs/planning/STAGED_DELIVERY_PLAN.md`. The semantic placeholder, placement
command and registry are experimental. They are not a product ABI, a file
format, a collaboration protocol or a V1 node type.

## Contract checks

1. Build `poc05-host-debug` and run all six GoogleTest cases.
2. Confirm the separate POC-03 contract test sees exactly one reserved, empty
   `ExternalSurface` pass, dependent on Ink, followed by Overlay.
3. Confirm the POC-05 core has no dependency on POC-03 C++ headers and consumes
   only stable View/Camera/Surface C ABI types plus
   `canvas_view_world_to_screen()` projection.
4. Confirm a stale frame is ignored, a target-generation change destroys old
   platform resources, and clear/unregister returns active and materialized
   counts to zero.

## Web evidence

The Playwright corpus uses an iframe and a video element without external
network content. It covers create, move, scale, clip/viewport occlusion, hide,
failure placeholder, page/generation switch, focus handoff and 100 lifecycle
iterations. The measured placement tolerance is one CSS/device pixel at DPR 1;
the two-frame update check waits for at most two animation frames.

The controlled layer is fixed above Canvas and below product UI. Arbitrary DOM
nodes are intentionally not supported between Canvas nodes.

## Remaining physical/platform gates

| Target | Status | Required evidence |
| --- | --- | --- |
| Web iframe/video | Automated validating | Playwright result and trace |
| Windows RNW/WebView2/video | Pending | Fabric component, locked WebView2, real Windows run |
| Android RN/WebView/VideoView | Pending | Fabric component, 16 KiB checks, Pixel physical run |
| Apple RN/WKWebView/AVPlayer | Pending | Fabric component and iOS/iPadOS run |

No target is marked Accepted until placement, two-frame update, lifecycle,
focus/input and failure recovery evidence exists for the actual adapter.
