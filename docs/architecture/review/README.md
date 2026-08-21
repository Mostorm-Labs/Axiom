# Axiom 架构重审工作台

> 状态：Draft / Non-normative
> 建立日期：2026-08-21
> 输入：SRC-USER-ARCH-REVIEW-BRIEF-20260821
> 当前进度：步骤 0、步骤 1、步骤 2 已完成；下一步进入步骤 3“需求基线”
> 现行规范：在新的决定通过评审前，仍以
> [项目总体框架](../../PROJECT_FRAMEWORK.md)、
> [系统架构](../SYSTEM_ARCHITECTURE.md)和
> [现有 ADR](../../adr/README.md)为准。

这组文档用于重新梳理 Axiom 的架构决策链。它不是第二套架构基线，也不会因为一轮讨论
就让已有决定自动失效。我们会把仓库、POC/RF 证据、Notion 草案和历史对话都当作输入，
先说明问题和约束，再比较方案，最后才决定是否保留、澄清或替代现有结论。

## 本文用语

| 中文首选名 | 英文或代码名 | 本文含义 |
| --- | --- | --- |
| 架构重审 | Architecture Review | 对当前架构的来源、假设、方案和证据重新建立追溯关系。 |
| 再基线 | Rebaseline | 在保留历史的前提下形成新的权威基线，不等于从零推翻。 |
| 规范性文档 | Normative Document | 对实现和后续设计具有约束力的已批准文档。 |
| 来源 | Source | 仓库文档、代码、验证证据、外部草案或历史讨论。 |
| 决策主题 | Decision Topic | 需要独立说明问题、方案、结论和验证方法的架构问题。 |
| 退出条件 | Exit Criteria | 当前步骤完成并允许进入下一步所需的可检查条件。 |

更完整的术语与中英文写法见[术语表](GLOSSARY.md)。

## 为什么现在重审

当前仓库已经有 24 份 Accepted ADR、完整的 POC 证据以及正在推进的 RF/R1 工程，说明
技术探索并非空白。问题在于，许多结论直接以 ADR、Contract 或实现规格出现，方案比较和
前置假设没有被单独保存。与此同时，外部讨论已经提出新的 Axiom/Arc、Platform Host、
Native React Native Shell 和 Shared TypeScript Data Runtime 边界，其中一部分与仓库现行
基线并不一致。

所以这次工作的目标不是批量移动文件，而是回答四个问题：

1. 我们到底要解决什么产品问题，哪些是 V1 必须能力；
2. 每个长期决定当时比较过哪些方案，为什么选择现在的方向；
3. 决定、接口成熟度、实现进度和验证结果分别处于什么状态；
4. 需求、决定、契约、测试和证据能否沿同一条链追溯。

## 架构真相如何形成

架构真相不只来自 ADR。新的追溯链固定为：

```text
来源与证据
  → 需求、约束与问题
  → 方案比较 / RFC
  → ADR
  → Architecture Spec / Contract
  → Validation Plan
  → Evidence
  → Implementation
```

这个顺序不是僵硬的瀑布。Draft Contract 可以帮助验证 RFC，POC 也可以推翻一个候选方案；
但没有经过明确评审的草案不能自动升级为 Accepted 决定。

## 总体步骤

每个步骤使用独立文档记录。步骤编号只表示本轮审查顺序，不占用 ADR 编号。我们只在
真正开始某一步时创建正文，避免先生成一批空文档。

| 步骤 | 独立文档 | 要回答的问题 | 主要产出 | 退出条件 | 状态 |
| --- | --- | --- | --- | --- | --- |
| 0 | 本文、[文档工作流](DOCUMENTATION_WORKFLOW.md)、[术语表](GLOSSARY.md) | 怎样讨论、批准和保存架构决定？ | 文档类型、状态、编号、隐私和评审规则 | 工作流和术语表分别获得用户明确确认 | Completed（2026-08-21） |
| 1 | [来源目录](SOURCE_CATALOG.md) | 当前有哪些可靠输入，可以怎样引用？ | 来源 ID、覆盖主题、访问/读取范围、证据角色和引用限制 | 来源全集可复算；每项五个维度齐全；缺口和影响明确；用户逐组确认；校验通过 | Completed（2026-08-21） |
| 2 | `02_CURRENT_STATE_AUDIT.md` | 仓库、Notion 和讨论中已经有哪些决定、矛盾和缺口？ | 逐文件迁移表、ADR 对账、冲突清单 | 每个现有规范和 ADR 都有处理建议 | Completed（2026-08-21） |
| 3 | `03_REQUIREMENTS_BASELINE.md` | V1 产品到底必须做什么？ | 带稳定 ID 的功能与非功能需求、Vibe 对齐、非目标 | 每个 P0/P1 需求有平台范围和验收方法 | Next |
| 4 | `04_SYSTEM_CONTEXT_AND_BOUNDARIES.md` | Axiom、Arc、Shell、Host、Data Runtime 和外部系统分别拥有何种状态？ | 系统语境、所有权、生命周期和信任边界 | 顶层模块没有重叠的权威状态 | Not started |
| 5 | `05_DECISION_BACKLOG_AND_DAG.md` | 哪些决定必须先做，哪些可以继续实验？ | 决策清单、依赖 DAG、风险和验证方式 | 所有待决主题有 owner、前置条件和文档等级 | Not started |
| 6 | 每个主题一组独立文档 | 对单个架构主题，问题、替代方案和证据是什么？ | Problem、RFC、ADR、Spec/Contract、Validation | 逐主题通过明确评审 | Not started |
| 7 | `07_CROSS_CUTTING_REVIEW.md` | 各主题组合后是否仍满足全局不变量？ | 所有权、线程、恢复、资源、安全和错误传播审查 | 无未处理的 P0 跨主题冲突 | Not started |
| 8 | `08_VALIDATION_AND_TEST_BASELINE.md` | 如何证明设计成立、如何防止回归？ | 需求到测试/设备/阈值/证据的追踪矩阵 | 关键需求与 ADR 都有可执行验证 | Not started |
| 9 | `09_IMPLEMENTATION_ALIGNMENT.md` | 当前 POC、RF 和 R1 如何迁移到新基线？ | 实施包、依赖、迁移、回滚及 DoR/DoD | POC 接口没有被误升为稳定产品 ABI | Not started |
| 10 | `10_DOCUMENT_MIGRATION_AND_ACCEPTANCE.md` | 如何发布新基线而不留下两份真相？ | 迁移清单、归档/替代关系和发布检查 | 链接、状态、术语、追溯和门禁全部通过 | Not started |

## 第 6 步的主题顺序

具体主题按依赖关系逐个推进，不一次性生成全部结论：

1. 项目身份、产品范围与命名：Axiom、Canvas 和 Arc 分别是什么；
2. 公共 SDK 边界、Product Shell、Platform Host 与 Binding；
3. Semantic Document、Object、Operation、Command、History 与 Snapshot；
4. Runtime 生命周期、调度、线程、内存、诊断和资源预算；
5. SceneCompiler、Scene、SpatialIndex、Rendering、Tile、Cache 与 Resources；
6. Input、Ink、Arc、Preview/Canonical 与 handoff；
7. RichText、IME 与 TextEditSession；
8. Surface、Web、React Native、WASM、JNI、ObjC++ 和平台集成；
9. Persistence、Recovery、Sync 与 Collaboration；
10. 安全、兼容迁移、可观测性、供应链和发布。

主题顺序可以因证据依赖调整，但任何调整都应在状态表中记录理由。

## 每一步怎样与用户协作

每一步都分为四个小回合：

1. **取证**：读取该主题所需的数据源，列出事实、用户需求、历史决定、推断和开放问题；
2. **问题定界**：先请用户确认 Problem、Requirements、Constraints 和 Non-goals；
3. **方案评审**：比较可行方案和代价，必要时设计 POC/Benchmark；
4. **形成结论**：用户明确确认后，才更新 ADR、规范、Contract 和验证计划。

“继续下一份”只表示进入下一项工作，不自动表示上一份 ADR 已 Accepted。决定被接受时应有
明确回复或 PR 审批记录。

## 当前已识别的首批对账主题

这些只是需要审查的问题，不是本工作台已经作出的新决定：

- 项目与模块正式名称仍在 `Canvas v2`、`Axiom` 和 `Arc` 之间混用；
- 仓库当前 Windows 产品 Shell 是 Tauri，外部候选基线将 Native Shell 收敛为 React Native；
- 仓库图中 Persistence/Collaboration 与外部 Shared TypeScript Data Runtime 的所有权不同；
- 仓库稳定 C ABI 与外部“只发布一个 Axiom SDK、Host/Arc 内部化”的边界需要重新对账；
- Arc 的 input、preview、协议和 surface ownership 中，哪些是模块不变量、哪些仍是 POC 假设；
- POC-03/RF-01 的 Scene、SpatialIndex、Tile 和 SkSG 结论需要区分边界决定与具体实现；
- 当前 24 份 ADR 全部 Accepted，但接口、实现和验证成熟度并不都相同；
- 安全、信任边界、兼容迁移、可观测性和资源预算尚未形成完整决策链。

## 旧文档处理原则

重审完成前不批量移动或重写旧文档。每份旧文档在步骤 2 中只会被标记为：

- **保留（Keep）**：内容和决策链足够完整；
- **澄清（Clarify）**：结论仍成立，但需补范围或关系；
- **拆分（Split）**：把混合的 ADR、RFC、Contract、实施和验证内容分开；
- **替代（Supersede）**：新 ADR 明确替代旧决定，旧文件保留历史；
- **归档（Archive）**：不再规范实现，但保留来源和继任链接。

现有 ADR-0001～0024 不重编号。新决定从 ADR-0025 继续；改变 Accepted 决定时必须新增
`Supersedes` ADR，不能静默改写历史。POC-01 已 Accepted，不会重新创建同名 POC；如果新
决定影响其假设，只重验受影响的门禁。

## 工作台文件

- [文档工作流](DOCUMENTATION_WORKFLOW.md)
- [术语表](GLOSSARY.md)
- [来源目录](SOURCE_CATALOG.md)
- [步骤 2：当前状态审计](02_CURRENT_STATE_AUDIT.md)

步骤 2 已完成五组审核，现状、冲突和 Decision Backlog 输入见[当前状态审计](02_CURRENT_STATE_AUDIT.md)。
下一步进入步骤 3：把产品功能基线、竞品矩阵、`SRC-CHAT-07`、用户明确目标和现行 V1 范围
接收为候选项，逐项拆分、去重并形成可审核的 Requirement baseline。需求文档不会直接替换
现行 ADR，也不会把 178 个竞品 source rows 原样变成 178 个正式需求。
