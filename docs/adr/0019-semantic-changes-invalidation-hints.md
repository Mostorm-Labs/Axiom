# ADR-0019: 语义 ChangeSet 与失效提示分离

- Status: Accepted
- Date: 2026-08-17
- Related stages: POC-03, R1, R2, R3
- Clarifies: ADR-0003

## Context

Operation 应用结果既需要表达“哪些语义字段发生变化”，也可以携带 dirty bounds、layout
或 cache invalidation 等优化信息。若二者没有身份边界，派生提示可能进入 operation log、
协作协议或成为 SceneCompiler 正确性的隐藏前提。

## Decision

- `Operation` 是可验证、持久化和协作同步的语义事实；`ChangeSet` 是成功 Document
  transaction 针对明确 before/after revision 产生的 Runtime 内部派生结果。
- `ChangeSet` 逻辑上包含：
  - `SemanticChanges`：创建/删除实体、字段、层级/order、ResourceManifest binding 和明确
    的 layout-affecting semantic facts；
  - `InvalidationHints`：旧/新 bounds、dirty region、layout/cache/spatial hints 和优先级。
- `SemanticChanges` 必须由 transaction 结果确定，可用于增量编译校验；
  `InvalidationHints` 非权威、可丢弃、可扩大、可重新计算，不能进入 Document digest、
  operation log 或 collaboration envelope。
- SceneCompiler 收到缺失、冲突或无法证明安全的 hints 时必须扩大失效或回退 full compile；
  不能产生错误 Scene。增量结果仍以同 revision full compile 为 oracle。
- hints 的具体布局可以随实现演进，不构成公开文件/协议兼容承诺。

## Consequences

- Operation codec 不会被 Renderer/Cache 优化细节污染。
- ChangeSet 可继续作为增量 SceneCompiler 的高效输入，但 correctness 不依赖不可重建状态。
- 测试必须能分别故障注入 SemanticChanges 与 InvalidationHints，并观察 fallback/diagnostic。

## Validation

POC-03 对每组 transaction 使用正确 hints、空 hints、扩大 hints、损坏/过期 hints 和 full
compile 比较 Scene digest、bounds、hit-test order 与视觉输出。R2 验证 DocumentSnapshot/operation
log 不包含 invalidation-only 字段；R3 验证丢弃 hints 只影响性能，不影响正确性。
