# ADR-0017: Runtime invalidation 与平台帧调度分离

- Status: Accepted
- Date: 2026-08-17
- Related stages: POC-02, POC-03, POC-06, R1, R3
- Clarifies: ADR-0008, ADR-0010, ADR-0011

## Context

RendererBackend、RenderTarget 和 PlatformSurfaceAdapter 已定义“由谁画、画到哪里”，但
没有定义脏状态如何对接 `requestAnimationFrame`、Choreographer、DisplayLink、DXGI 等
平台 VSync/呈现机制。若 Runtime 自己拥有平台 event loop，或 Shell 每收到一个输入就直接
调用 render，会产生重复帧、过期 target、跨线程竞态和不可比较的延迟数据。

## Decision

- Runtime 只通过 Runtime 定义、host 实现的 `FrameInvalidationSink` 发布带 `ViewId`、
  reason、minimum document/view/preview revision 和 target generation 的 frame
  invalidation；这是内部语义，不承诺成为产品公共 ABI。
- `PlatformFrameScheduler` 位于平台集成边界，拥有 VSync/frame callback 的注册、取消、
  节流和生命周期。Web 对接 rAF，Android 对接 Choreographer，Apple 对接 DisplayLink，
  Windows 对接选定的 DXGI/window scheduler；Headless 使用显式 deterministic pump。
- 同一 View 同一时刻最多保留一个未决平台 frame callback。Scheduler 可以合并 reasons
  和较旧 revisions，但不能把较新的 invalidation 标记为已呈现。
- Frame callback 只触发一次受 generation 约束的 acquire → build/render → present。resize、
  background、surface/device loss 或 generation 改变时，旧 target/frame 被丢弃并按最新
  invalidation 重约；失败不得改变 Document。
- Canonical executor 决定可读取的 `DocumentReadView`/revision；PlatformFrameScheduler 不写
  Document、不运行 Tool 逻辑，也不把平台时间写进语义状态。跨 executor/presentation
  thread 使用显式 revision、generation、cancel 和 completion/visible acknowledgement。
- Preview sink 可以使用比 Canonical Renderer 更直接的平台 presentation path，但仍消费
  ADR-0011 的共享 Preview Model。Canonical handoff 以“对应 revision 已可见”而不是
  “render 调用已返回”为准。

## Consequences

- Runtime、Shell 和 Renderer 之间增加一条窄的 invalidation/frame callback 契约。
- 多次输入或 Scene 变化可以合并为一帧，同时保留 revision 与 visible-time 诊断。
- 平台可以采用不同 VSync API，但 request、frame build、present 和 visible 的 trace 语义
  保持一致。
- 具体 Windows scheduler、triple buffering 和产品 render-thread 拓扑仍由测量和后续 ADR
  决定，本 ADR 不提前固定线程数量。

## Validation

POC-02/03 注入 burst invalidations、resize、后台/前台、device loss、过期 callback 和多 View
语料，验证每个最新 revision 最终可见、旧 generation 不 present、Document digest 不变且
frame callback 数量有界。POC-06 验证 Preview/Canonical visible acknowledgement 与 handoff。
性能报告同时记录 request、callback、render submit、present 和实际 visible 时间。
