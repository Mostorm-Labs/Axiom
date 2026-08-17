# ADR-0005: V1 使用 Skia Ganesh

- Status: Accepted
- Date: 2026-08-16
- Related stages: POC-01～03, R1, R3

## Context

Web、Windows 和 Android 都需要成熟的矢量、图片、文本和 GPU 支持。为了追求新后端而同时承担 Graphite/WebGPU、WASM 和多平台 bring-up 风险，会掩盖 Document/Ink/Text/Scene 的真正架构问题。

## Decision

V1 RendererBackend 使用 Skia Ganesh：Web 使用 WebGL，native target 使用经 POC 验证的 Ganesh GPU backend，headless/golden 使用受控 raster surface。

FrameGraph、Compositor 和 RendererBackend 不暴露 Ganesh 私有类型给 Document、Editor、Ink 或 Text。Graphite/WebGPU 仅作为未来 backend，不能成为 V1 依赖。

## Consequences

- 可以使用成熟 Skia/SkParagraph 能力和统一黄金图工具。
- 需要锁定 Skia commit、build flags、shader/cache 兼容信息和许可证。
- 未来迁移 Graphite 要实现 RendererBackend，不重写上层 Runtime。
- Web 首期不承担 WebGPU 和 SharedArrayBuffer 的联合复杂度。

## Validation

POC-01 验证 Web、Windows、macOS、iOS、iPadOS、Android bring-up，POC-03 验证 100K scene、FrameGraph 和 cache。若目标平台 Ganesh 无法达到门禁，应先分析 backend/场景瓶颈，再由新 ADR 评估替代。
