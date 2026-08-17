# ADR-0018: 输入背压与可合并更新契约

- Status: Accepted
- Date: 2026-08-17
- Related stages: POC-02, POC-03, POC-06, R1, R3
- Clarifies: ADR-0008, ADR-0011

## Context

真实设备可能以 120/240 Hz 产生 Pointer sample，并在一次平台事件中附带大量历史点，而
显示通常只有 60/90/120 Hz。若每个 sample 对应一次 render，或跨边界队列无界增长，书写
会持续落后；若任意丢点，又会破坏 Canonical Stroke、回放与协作语义。

## Decision

- InputRouter 按 pointer stream 顺序处理 confirmed samples。允许把相邻 batch 合并为更大
  batch，但不得静默删除、重复或重排 confirmed sample；batch 的 ViewId、viewport
  revision/transform 和时间域必须兼容才能合并。
- predicted samples 是可替换的派生尾部，可以丢弃并从最新 confirmed prefix 重算；它们
  不进入 Document 或 Canonical digest。
- `PreviewStrokeUpdate` 仅在 sink 已确认支持 replace/truncate 且保留的较新 revision 能完整
  取代旧 revision 时才可合并。begin、end、cancel、Canonical commit 和 visible/handoff
  acknowledgement 不可丢弃。
- Frame invalidations 按 ADR-0017 合并，不与 input sample queue 一一对应。输入处理、Preview
  更新和 Canonical candidate 构建必须增量进行，不能把积压工作推迟到 pointer up。
- 所有跨线程/跨 ABI 队列都有声明容量、字节上限、最大 batch、revision 和 telemetry。
  达到 confirmed-input 上限时不得继续无界分配或假装成功：能施加背压的调用方收到明确
  backpressure；不能阻塞的 adapter 返回 `InputOverrun`，原子取消受影响 StrokeSession、
  清理 Preview 并记录结构化诊断，不提交部分 Stroke。
- Scheduler 必须为 input/Ink processing 保留进展，不能因连续 render 或资源任务造成饥饿。
  具体队列容量和调度时间片由 POC trace 决定，不进入持久 schema。

## Consequences

- “不丢 confirmed sample”适用于成功完成的 Stroke；系统资源不足时以明确取消代替静默
  损坏或无限延迟。
- Preview 和 frame 可以跳过中间派生 revision，但 Canonical prefix、生命周期事件和最终
  visible acknowledgement 保持完整。
- 需要 queue depth、oldest-sample age、coalesced count、prediction replacement、overrun 和
  cancel reason 的统一诊断。

## Validation

POC-02 回放 60 秒 240 Hz 输入、32+ historical samples burst、慢 renderer、暂停/恢复和容量
边界；成功路径 confirmed samples 100% 完整且 queue age 有界，故障路径只允许明确
`InputOverrun` 和无部分 Document 修改。POC-06 对 Default/FastInk sink 注入慢消费者、乱序
ack 和取消，最终 Canonical digest 必须一致。R3 在真实设备记录 queue depth、sample age、
frame cadence 和 memory，不接受随书写时长持续增长。
