# ADR-0011: FastInk backend 消费共享 Preview Model

- Status: Accepted
- Date: 2026-08-17
- Related stages: POC-02, POC-06, R2, R3
- Clarifies: ADR-0004, ADR-0008

## Context

ADR-0004 已确定 Canonical 与 Preview 双路径，但旧接口示例让
`FastInkBackend.push` 接收 raw `PointerSample`。与此同时 resample、smooth、pressure
mapping、prediction 和 rollback 属于共享 StrokeSession。若平台 backend 分别解释原始
样本，Web、Windows、Android 和设备级 FastInk 会形成不同的 wet-ink 算法，handoff、
回放和故障 fallback 无法保持同一语义。

## Decision

- `StrokeSession` 统一处理输入并输出版本化 `PreviewStrokeUpdate`。更新至少表达 Stroke
  ID、update revision、brush descriptor、transform/坐标空间、confirmed representation、
  predicted tail 和 replace/truncate 语义。
- `DefaultPreviewSink` 与 `FastInkBridge/FastInkBackend` 消费同一个 Preview Model。
  POC-02 用普通 Skia overlay 验证模型、prediction rollback 和 Canonical handoff；POC-06
  保持上游模型不变，验证平台低延迟 presentation、surface 生命周期和 fallback。
- 平台 sink 可以选择低分辨率、buffering 或 presentation 策略，但不得从 raw samples
  重新实现另一套平滑、预测、压力映射或笔刷语义。
- `begin/push/end/cancel` 保持按 Stroke ID 幂等。具体 segment/dab layout、buffer
  ownership、ABI 与平台线程交接由 POC-02/06 的延迟和回放证据冻结。
- Canonical deterministic executor 不要求 OS input/compositor/GPU driver 或平台
  presentation thread 与其同线程；跨边界必须使用明确的 queue、revision、generation、
  ack/fence、cancel 和销毁契约。

## Consequences

- Default 与 FastInk 路径共享 Stroke 解释，FastInk 失败可切回普通 Preview/Canonical，
  不改变最终 Document。
- Preview representation 成为明确的实验接口，需要 revision replacement、预测尾部回滚、
  buffer lifetime 和丢帧/乱序测试。
- 平台仍可进行不改变共享 Preview 语义的 presentation 优化；设备级 direct plane 不进入
  Document、RuntimeScene 或 Canonical Renderer。
- 本 ADR 澄清 ADR-0004，不改变 Canonical Stroke 只由确认样本提交的决定。

## Validation

POC-02 要求同一 pointer replay 经 DefaultPreviewSink 得到确定的 Preview revision 序列，
并通过 prediction rollback、cancel 和 handoff。POC-06 要求 Default/FastInk sink 消费同一
序列，平台 failure 后最终 Stroke/Document digest 一致，且 backend/device/surface/thread
故障不丢 Canonical Stroke。若某平台必须重新解释 raw samples 才能达到门禁，必须以新
ADR 和跨平台差异证据重新评估此边界。
