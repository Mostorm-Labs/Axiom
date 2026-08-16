# ADR-0003: Semantic Document 与 RuntimeScene 分离

- Status: Accepted
- Date: 2026-08-16
- Related stages: POC-01, POC-03, R2, R3, R4

## Context

Comment、search metadata、permissions、collaboration 和版本信息并不都参与渲染；布局、空间索引、dirty region 和 GPU cache 又不应成为文档事实。直接把 Document node 当 render node 会耦合持久化、编辑和 Skia 生命周期。

## Decision

Document 保存语义事实，经 `SceneCompiler` 编译为可重建的 `RuntimeScene`。RuntimeScene 再经过 visibility/spatial query、Render Tree、FrameGraph 和 Compositor 进入 Skia。

SceneCompiler 同时支持 full compile 和基于 ChangeSet 的 incremental apply。相同 Document revision 的两种路径必须在 scene digest、bounds、hit-test 和视觉输出上等价。

Renderer/RuntimeScene 没有 Document 写入口；cache/GPU state 可以无条件丢弃并重建。

## Consequences

- 增加编译层和 revision/失效管理，但避免领域模型被 renderer 污染。
- 支持 minimap/多视口、headless export、搜索和不可渲染领域对象。
- 增量错误可以回退 full compile，而不修正文档。
- 需要专门的 full/incremental 差分语料。

## Validation

POC-03 用 100K scene 验证等价性、空间查询、dirty 与多视口。如果增量收益不足，可以更换内部表示，但不能合并 Document 与 RuntimeScene 所有权。
