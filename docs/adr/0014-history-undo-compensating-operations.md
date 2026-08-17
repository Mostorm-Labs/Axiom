# ADR-0014: Undo/Redo 通过补偿 Operation 修改 Document

- Status: Accepted
- Date: 2026-08-17
- Related stages: R1, R2, R4

## Context

EditorSession 拥有本地 History/Undo intention，而 Operations 是 Document 的唯一持久、
可回放和可同步写入口。通过移动本地状态指针或恢复旧 Document snapshot 实现 Undo，
会绕过远端并发变化、operation log、Persistence 和 Collaboration，最终在 R4 被推翻。

## Decision

- `History` 属于 EditorSession，记录本地 authored intentions、undo grouping、关联 Operation
  IDs 和生成补偿所需的受控 preimage；它不是 Document 或 Presence 状态。
- Undo command 选择一个仍可撤销的本地 intention，针对**当前** Document revision 生成一组
  新的 compensating Operations，经正常 validation 和原子 Document transaction 应用。
  它不是指针回退，也不删除或改写既有 Operation。
- Redo 同样产生新的 Operation transaction；它可以补偿上一次 undo，也可以按当前状态
  重新表达原 intention，但不得复用旧 sequence/operation ID。
- 补偿 transaction 带稳定 intent/group reference 以及 `undo_of`/`redo_of` 诊断关系，作为
  普通 Operations 持久化和同步。具体 collaboration envelope/causal ordering 仍由 R4 ADR
  决定。
- 如果 concurrent edits、删除、权限或 schema 使 intention 不再完整适用，Undo 必须得到
  明确的 applied/no-op/rejected/conflicted 结果。生成与验证是原子的，不能留下部分补偿。
- Operation/schema 必须保留或可确定性重建所需 preimage；保留期限、历史压缩与权限策略
  由 Persistence/Collaboration ADR 决定，不能通过直接保存任意内存对象解决。
- 远端客户端把补偿视为正常 Operation。Undo 只选择本地 intention，不隐式撤销其他用户
  的 Operation。

## Consequences

- 单机与协作使用同一写路径，operation replay、crash recovery 和 convergence 不因 Undo
  产生特例。
- 并非所有命令都有机械逆 Operation；文本、删除恢复、资源替换和层级移动需要明确的
  intention/preimage 与并发语料。
- History 可以按 session 生命周期恢复或丢弃，但已经提交的补偿是 Document 事实。
- R2 可以在未知最终 CRDT/OT 算法时建立正确 ownership；R4 只需决定补偿 Operations 的
  并发合并语义。

## Validation

R2 覆盖 create/edit/delete/move/style/text/resource 的 undo/redo、grouping、已提交补偿的
重启恢复、以及“session history 被恢复或明确不可用”的产品行为和故障原子性。R4 覆盖本地 edit→远端冲突→undo、undo 与远端 delete 交错、duplicate/
out-of-order compensation 和多副本收敛。任何 Undo 直接替换 Document snapshot、倒退
operation sequence 或绕过唯一写入口都违反本 ADR。
