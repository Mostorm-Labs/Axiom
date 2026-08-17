# ADR-0020: DocumentSnapshot 与 Operation continuation 恢复契约

- Status: Accepted
- Date: 2026-08-17
- Related stages: POC-01, POC-02, R1, R2, R4
- Clarifies: ADR-0003, ADR-0013, ADR-0014, ADR-0019

## Context

Canvas v2 已规定 Operation 是 Document 的唯一语义写入口，但如果 Snapshot 可以任意替换
运行中的 Document，或 Snapshot 与后续 Operations 没有明确的恢复边界，Persistence、
Undo/Redo、crash recovery 和未来 Collaboration 仍会形成多条不一致的写路径。当前不需要
决定 SQLite/内存、Protobuf/FlatBuffers、服务端日志、CRDT/OT 或云端 compaction，但需要
证明 Document 天然支持从语义检查点继续确定性回放。

## Decision

- `DocumentSnapshot` 是某个已提交 transaction 边界上的完整、不可变语义检查点；
  `OperationContinuation` 是该检查点之后、到目标恢复位置为止的一系列已提交 Operations。
  恢复关系为：

  ```text
  DocumentSnapshot at frontier F
      + committed OperationContinuation F → T
      = Document state at target frontier T
  ```

- `RecoveryFrontier` 是版本化、不透明的恢复位置。单机 POC/V1 可以用连续 `uint64_t`
  sequence 实现，但公共语义不假设未来永远存在单一全局线性序号；R4 可以扩展为 causal
  frontier/version vector，而不改变本 ADR 的恢复关系。
- Document revision 是某个 Runtime 实例内的单调发布/失效标记；RecoveryFrontier 是可以
  持久化或同步的恢复位置。二者必须一起验证但不能互换。Snapshot 绑定 Document identity
  与 frontier F，continuation 明确绑定同一 Document 的 base F 与 target T。
- DocumentSnapshot 至少表达 Document identity、schema version、DocumentCapability
  requirements、完整语义 graph、RichText/Canonical Stroke、ResourceManifest bindings、
  Document revision、RecoveryFrontier、digest algorithm/version 和可验证 Document digest。
  ResourceManifest 在逻辑上属于 Snapshot/Digest；物理包可以分开存放，但必须由同一原子
  checkpoint 明确绑定。具体字段编码、容器校验、压缩和文件布局由 R2 前实验型格式 ADR
  决定。
- DocumentSnapshot 不包含 EditorSession/History UI state、Presence、Viewport、Text
  composition、Active Preview/FastInk、未提交 Pointer/Tool 状态、RuntimeScene、SpatialIndex、
  FrameState、Skia/GPU/cache、平台句柄、decode 状态、路径/URL 或资源 blob bytes。Blob 继续
  通过 ADR-0013 的 ResourceManifest/ContentHash 独立解析。
- Snapshot restore 只允许创建/恢复一个 Document、执行显式 migration 或 collaboration
  bootstrap；运行期编辑、Undo/Redo 和普通远端变化仍必须生成并应用 Operations。禁止用
  `replaceState(arbitrary_state)`、恢复旧 Snapshot 或直接修改 Scene/Document internals 绕过
  validation、transaction、ChangeSet、Persistence 或 Collaboration。
- 恢复先原子验证 Snapshot identity/schema/capability/digest/frontier，再按顺序验证并应用
  continuation。gap、duplicate、out-of-order、frontier mismatch、未知 required capability、
  损坏 operation 或 digest mismatch 必须在发布新 Document 实例前失败；不能暴露部分恢复
  状态或覆盖最近有效 checkpoint。
- Snapshot、operation continuation、ResourceManifest 和 blob availability 是独立层次。
  资源暂时 missing 不使有效 Snapshot 语义失效；RuntimeScene/GPU/cache 从恢复后的 Document
  重建。
- Snapshot 必须从同一已提交 revision/frontier 的不可变 `DocumentReadView` 导出，不能在
  transaction 中途或跨两个 revision 拼接。只有当 Snapshot、其 ResourceManifest binding、
  continuation 起点和恢复所需元数据已经持久化、校验且可读取后，才允许删除/压缩 frontier
  F 之前的 Operation prefix；blob 的保留/GC 仍按 manifest/content reachability 决定。
- 以下术语必须区分：`DocumentSnapshot` 是可持久化语义检查点；`ViewportSnapshot` 是解释
  输入坐标的瞬时变换；`DocumentReadView` 是 executor 内不可变读取视图。后二者不是保存/
  恢复格式。

## Consequences

- Operation-driven Document 可以先服务本地编辑、Undo/Redo 和 autosave，再自然扩展到
  operation log、version history 与 collaboration，而无需让网络协议进入 Core。
- POC 只需用真实 Operations 验证 replay；不要求实现正式 Snapshot codec 或持久日志。
- Undo/Redo 继续遵循 ADR-0014 产生新的 compensating Operations，不能借 Snapshot 回拨。
- Snapshot cadence、日志分段/compaction 算法、磁盘布局、增量 Snapshot、云端 bootstrap
  和最终 network encoding 仍是 Persistence/Collaboration 的后续决策；它们必须遵守上述
  “先验证 checkpoint，再回收 prefix/resource”的安全顺序。

## Validation

POC-01 的空 Document + create/move/delete replay 继续作为 operation-driven baseline；
POC-02 增加 Pointer replay → Canonical AddStroke Operation → 新空 Document replay，并比较
Stroke/Document digest。R2 验证：Operations 构建状态 A；Snapshot round-trip 得到 B；A/B
digest 一致；从同一 Snapshot 应用相同 continuation 得到 C/D，revision/frontier/digest
一致。损坏 Snapshot、gap/duplicate/out-of-order continuation、未知 capability、资源 missing、
中途故障和 migration 都不得发布部分 Document 或覆盖最近有效 checkpoint。R4 只扩展
frontier 与 bootstrap/compaction 协议，不重新开放普通编辑绕过 Operation 的路径。
