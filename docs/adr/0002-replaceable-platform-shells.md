# ADR-0002: 平台 Shell 可替换，Runtime 统一

- Status: Accepted
- Date: 2026-08-16
- Related stages: POC-01, POC-04～06, R1, R3

## Context

产品目标同时包含 Web、Windows 和 Android。强求同一 UI framework 会牺牲 Web/DOM 产品能力或原生输入、Surface、IME 与设备集成。真正需要共享的是文档和画布运行时。

## Decision

- Web：React/TypeScript + WASM + WebGL。
- Windows：React/Tauri + native canvas region + versioned C ABI。
- Android：React Native Shell + Native `CanvasView` + JNI。
- ChromiumOS：复用 Web Shell，并按能力接入平台 FastInk。

Toolbar、Inspector、Dialog、Share、Account 和 Navigation 属于 Shell。Document、Operations、Ink、RichText、Scene、HitTest、Renderer 和 Persistence 属于 C++ Runtime。

Android pen 数据固定走 `MotionEvent/history → Native CanvasView → PointerSampleBatch → C++`，不得通过 RN JS。Windows/Web 也采用批量输入，避免高频跨边界逐点调用。

## Consequences

- 需要维护 WASM、C ABI、JNI 三种窄边界和共同 contract tests。
- 平台 UI 可以独立演进，但 Canvas 行为必须使用共享回放语料。
- Windows DOM 与 native canvas、Hybrid Surface Overlay 必须遵守固定 z-order 限制。
- React Native 选择不会使 JS 成为 Canvas 数据面。

## Validation

POC-01 验证 Windows/Web shared engine；POC-04 验证三平台 IME；POC-05 验证 overlay；R3 要求三平台运行同一用户流和 Bridge 契约测试。
