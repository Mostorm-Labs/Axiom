# ADR-0004: Canonical Ink 与 FastInk Preview 双路径

- Status: Accepted
- Date: 2026-08-16
- Related stages: POC-02, POC-06, R2, R3

## Context

最低延迟 wet ink 允许预测、较低分辨率和平台直接合成；最终文档笔迹必须稳定、可编辑、可持久化和可协作。把两种目标塞入同一 renderer 会让平台特例污染 Document/Scene。

## Decision

`StrokeSession` 从同一 `PointerSampleBatch` 产生两条输出：

- Preview Stroke → `FastInkBridge` → platform `FastInkBackend`。
- Canonical Stroke → Operation → Document → RuntimeScene → Skia Renderer。

二者共享 Stroke ID、transform 和 brush descriptor。`FastInkBackend` 只暴露 `begin/push/end/cancel`，Runtime 不感知 DirectComposition、SurfaceControl、DRM、HWC 或 plane。

普通应用 FastInk 是 V1 路线；Raw Input + system service + DRM overlay 是条件式设备预研，不阻塞普通应用。

## Consequences

- 需要可靠 handoff、幂等 Stroke ID 和 backend fallback。
- Preview quality 可以低于 Canonical quality，但不能改变最终 Stroke 语义。
- FastInk 失败时继续 Canonical render，不丢文档编辑。
- 平台可以独立实现 app-level 或 system-level preview。

## Validation

POC-02 验证 Stroke 模型和 Preview/Canonical 语义，POC-06 验证三平台 latency、handoff、cancel 和失败恢复。任何平台依赖进入通用 Document/Scene 即视为违反本 ADR。
