# ADR-0008: POC 使用单线程确定性基线

- Status: Accepted
- Date: 2026-08-16
- Related stages: POC-01～06, R1, R3

## Context

Document、Scene、Skia、WASM、IME、Ink 和缓存同时引入多线程会把功能错误、竞态、COOP/COEP、worker 生命周期和性能问题混在一起。现代产品最终可能需要 Render/Scene/IO/Cache workers，但线程数量不是目标。

## Decision

POC-01～06 默认在单一有序执行器中运行 Document write、SceneCompiler 和 render，建立跨平台 digest、golden、input replay 和性能 reference。

接口仍携带 revision、不可变 snapshot、任务取消和 GPU context ownership。引入任何 worker 前必须满足：profiling 证明瓶颈、所有权/失效规则有测试、新线程拓扑 ADR 被接受。

POC Web 不启用 SharedArrayBuffer/pthread。是否启用 WASM pthread 在产品基准后决定。

## Consequences

- POC 更容易调试并提供可靠 oracle。
- 首期性能可能低于最终目标，但能区分算法问题与并发问题。
- R1 不得机械复制单线程内部结构为永久 ABI。
- 后续多线程必须继续通过单线程语料的结果等价性验证。

## Validation

POC-03 若单线程已经达到 100K 场景门禁，则没有理由提前并发；若未达到，frame trace 必须定位瓶颈。任何 worker 引入都要通过 deterministic replay、race/sanitizer、revision invalidation 和 cancellation tests。
