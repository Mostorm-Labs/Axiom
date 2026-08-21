# ADR-0023: Hybrid Surface 使用受控 Overlay

- Status: Accepted
- Date: 2026-08-20
- Related stages: POC-05, R1, R3

## Context

Canvas 需要在 Web、Windows、Android、iOS 和 iPadOS Shell 中承载 Web
内容与视频，但这些内容拥有各自的输入、合成和生命周期。把 DOM/native
surface 当作普通 Scene node 或允许它们任意穿插 Canvas draw pass，会把
平台句柄、焦点和 z-order 语义泄漏进 Runtime。

## Decision

- `ExternalSurface` 只作为实验性 semantic placeholder 和 placement
  contract 存在，不进入 V1 Document schema 或稳定 Runtime C ABI。
- Runtime 只发布稳定的 surface ID、world bounds、clip、opacity、page、
  frame revision 和 target generation；native handles 只存在于平台
  `ExternalSurfaceRegistry`/adapter。
- 所有平台使用固定 z-order：Product UI → controlled external overlay
  (WebView/Video) → Canonical Canvas。禁止任意 DOM/native 节点插入 Canvas
  节点之间。
- Web 使用 React + WASM-facing placement contract 与 DOM overlay；Windows
  使用 RNW/Fabric + WebView2；Android 使用 RN + Native CanvasView/JNI +
  WebView/VideoView；Apple 使用 RN/Fabric + Metal Canvas + WKWebView/native
  video。
- Overlay 的移动、缩放、裁剪、隐藏、切页、焦点、失败恢复、重建和前后台
  生命周期由 native/platform frame scheduler 驱动，不经过逐帧 RN/React JS。
- Windows、Android 和 Apple 的 POC-03 C++ scene bridge 仅用于实验验证；
  产品化时必须替换为稳定 Runtime C ABI/SDK 消费路径。
- POC-05 对 RNW 和 Apple RN/Fabric 的验证是 Shell 可行性证据，不改变
  ADR-0015 已接受的 Windows React/Tauri Product Tier A 选择，也不构成
  Apple V1 产品承诺；更换产品 Shell 需另行产品/平台决策。

## Consequences

该边界保留 WebView、视频、IME 和平台合成能力，避免把系统 surface
伪装成 Skia texture；代价是不能提供任意节点间混合、复杂 mask/effect 或
首版 zero-copy texture import。产品若需要这些能力，必须另建 ADR 并提供
跨端合成、焦点和性能证据。

## Validation

POC-05 的 [consolidated validation report](../evidence/poc05/consolidated-validation-20260820.md)
记录 Web、Windows RNW、Android RN、iOS/iPadOS RN/Fabric 的真实 Web/video
surface、placement、focus/failure/lifecycle 和 JS-stall 证据。该报告同时
记录生产 WASM artifact、稳定 C ABI bridge 和复杂 texture composition 不在
本 POC 的范围内。
