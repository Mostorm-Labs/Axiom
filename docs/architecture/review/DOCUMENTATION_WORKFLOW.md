# Axiom 架构文档工作流

> 状态：Accepted
> 规范性：Process-normative；约束本轮架构重审的文档和评审流程，不改变产品架构
> 确认日期：2026-08-21
> 输入：SRC-USER-WORKFLOW-20260821、SRC-USER-WORKFLOW-CONFIRMATION-20260821、SRC-USER-SOURCE-CATALOG-RULES-CONFIRMATION-20260821
> 适用范围：Axiom 架构重审期间产生的 Problem、RFC、ADR、Contract、Validation 和实施文档
> 目的：让每个结论都能追溯到问题、方案、证据和明确的批准记录

## 本文用语

| 中文首选名 | 英文或代码名 | 含义 |
| --- | --- | --- |
| 问题说明 | Problem Statement | 解释为什么要做决定，以及需求、约束和非目标。 |
| 方案征求意见 | RFC | 比较方案并征求评审，可包含 Draft Contract 或实验设计。 |
| 架构决策记录 | ADR | 保存会长期影响边界、兼容性或演进方向的决定。 |
| 契约 | Contract | 精确定义跨模块、跨线程、跨语言或持久化边界。 |
| 实施规格 | Implementation Spec | 说明如何在代码、构建和迁移中落实已接受的设计。 |
| 验证计划 | Validation Plan | 给出语料、指标、设备、失败模式和退出条件。 |
| 证据 | Evidence | 实际测试结果、环境、原始产物和可复现命令。 |

## 1. 文档不是按标题决定权威性

一份名称带有 “v1.0” 或 “Contract” 的文档不一定已经被接受。每份文档开头都必须写明：

- 文档状态；
- 是否规范性；
- 输入来源；
- 关联需求、ADR、Contract 和验证；
- 负责人或评审者（如果已确定）；
- 最后评审日期。

没有这些信息的历史文档在重审期间只作为来源，不能单靠标题覆盖仓库现行基线。

## 2. 标准追溯链

```text
Source
  → Requirement / Constraint
  → Problem
  → RFC / Options
  → ADR
  → Architecture Spec / Contract
  → Validation Plan
  → Evidence
  → Implementation
```

并非每个局部问题都需要生成整套文件：

| 影响等级 | 典型情况 | 最少文档 |
| --- | --- | --- |
| L1：局部且易回滚 | 私有类结构、局部优化、无跨模块影响 | Design Note 或 Implementation Spec |
| L2：跨模块或跨平台 | 生命周期、队列、模块接口、平台行为 | RFC + Contract + Validation |
| L3：长期且难以迁移 | 公共 ABI、状态所有权、持久化语义、平台战略 | Problem + RFC + ADR + Contract/Spec + Validation |

一个 RFC 可以产生多个 ADR；一个 ADR 也可以由多个 POC 的证据共同支持。Draft Contract
可以在 RFC 阶段帮助验证，但只有决定被接受后才能进入 Experimental，完成实现和兼容门禁
后才能考虑 Stable。

## 3. 文档状态分开管理

不要再用一个 `Accepted` 同时表达“决定已接受、接口已稳定、代码已完成、测试已通过”。

### 3.1 决策主题

`Backlog → Framing → Options → Decision Required → Specifying → Validating → Closed`

### 3.2 ADR

`Proposed / Accepted / Rejected / Deprecated / Superseded`

### 3.3 RFC

`Draft / In Review / Approved / Withdrawn / Superseded`

### 3.4 Contract

`Draft / Experimental / Stable / Deprecated`

### 3.5 Validation

`Planned / Running / Passed / Failed / Blocked`

### 3.6 Source

来源审核状态只表示目录处理进度：

`Unreviewed → Reviewing → Reviewed`

`Reviewed` 表示来源身份、已得到的内容、限制、证据角色和引用规则已经审核；不表示来源内容
正确、其中方案获批或整份来源被正式吸收。`Restricted` 是访问级别，不是审核状态。
Requirement、ADR、Spec 和 Contract 逐项引用支撑其结论的 `SRC-*`；不再用来源级
`Incorporated` 推断整份来源获得认可。

来源还必须分别记录：

- 访问级别：`Repository / Public / Restricted / Local-only`；
- 读取范围：`Complete / Partial / Index-only / Unavailable`；
- 证据角色与权威性；
- 保留方式、允许的引用方式以及缺口影响。

`Partial`、`Index-only` 或 `Unavailable` 不自动阻塞后续审计，但必须说明原因、影响和后续；
不得用模型回顾补造缺失正文。

例如，“Arc 是独立边界”可以是 Accepted ADR，而 Arc ABI 仍是 Draft，POC-06 仍在
Validating。三者不矛盾，也不能互相替代。

## 4. 单个主题的工作顺序

### 4.1 来源整理

先记录可验证事实、用户明确需求、历史决定、推断、待验证假设和开放问题。外部草案中的
“Accepted”只代表该来源内部的状态，除非和仓库 ADR 对账并经本轮确认，否则仍是候选结论。

### 4.2 Problem Statement

至少包含：

1. 本文术语；
2. 背景和当前问题；
3. 带 ID 的功能与非功能需求；
4. 约束；
5. 非目标；
6. 已知风险；
7. 需要回答的问题；
8. 来源与现有决定。

问题说明不应偷偷写入唯一实现方案。

### 4.3 Options / RFC

每个可行方案都要使用同一组维度比较：

- 状态和数据所有权；
- 依赖方向和公共表面；
- 平台可行性；
- 确定性和恢复；
- 性能、内存和延迟；
- 失败和降级；
- 安全与信任边界；
- 兼容、迁移和可逆性；
- 测试成本和长期维护成本。

RFC 必须写出未选择方案的价值和放弃原因，不能为预设结论制造陪衬方案。

### 4.4 ADR

ADR 只记录长期决定，不承担全部接口和实现细节。固定字段为：

```text
Title
Status
Date
Decision owner / reviewers
Related requirements
Related RFC / evidence
Supersedes / Clarifies

Context
Options considered
Decision
Consequences
Validation and revisit triggers
Related documents
```

仓库已有 ADR-0001～0024。新 ADR 从 ADR-0025 继续；替代旧决定时保留旧文件并用
`Supersedes`/`Superseded by` 建立双向关系。

### 4.5 Architecture Spec / Contract

Architecture Spec 描述系统当前应该如何工作；Contract 描述边界上什么输入合法、谁拥有
内存、线程和生命周期、错误如何传播、版本怎样演进。它们必须引用决定来源，不能反过来
偷偷创造新架构决定。

### 4.6 Validation Plan

每个重要不变量至少要有一种可执行验证。计划需列出：

- 固定语料和 oracle；
- 目标平台、设备与工具链；
- 正确性、性能、内存和生命周期指标；
- 失败注入与降级行为；
- 原始产物和环境元数据的位置；
- 可以证伪设计的条件；
- 退出条件和重新评估触发器。

### 4.7 Implementation Spec

只有进入实施准备后才说明目录、target、class、header、线程和迁移步骤。POC 代码默认是
证据，不自动成为产品代码；实验 C ABI 也不能仅通过 rename 变成稳定 ABI。

## 5. 来源、隐私和引用

原始讨论默认不进入公开 Git 历史。原因是聊天可能包含私有链接、个人信息、未授权材料或
以后会被否决的临时推断。仓库只保存：

- 脱敏的来源 ID；
- 来源类型和采集日期；
- 覆盖主题；
- 是否可复查；
- 经对账后的摘要；
- 事实/决定/推断/假设分类；
- 下游 Requirement、ADR、Spec 或 Contract 对具体 claim 的引用关系。

私有 ChatGPT 分享 URL、Notion URL 和原始对话正文不写入仓库。`Restricted` 记录访问级别；
若来源日后不可访问，读取范围标为 `Unavailable`，并记录原因、影响和后续，而不是伪造内容。

动态受限页面应记录采集日期以及是否存在 revision/hash。无法生成不可变指纹时明确写明；
无法从页面标题或 revision 证实时，不能把目录自建的 `V01/V03` 标签冒充来源正式版本。
一个来源若只有部分 claim 被吸收，由
下游规范逐项引用，不改变整份来源的审核状态。

## 6. 中文写作约定

- 正文以自然中文为主，避免为了显得正式而大量使用模板化短句；
- 专业术语首次出现写成“语义文档（Semantic Document）”；
- API、类型名、协议名和稳定 ID 保持英文；
- 同一概念只使用术语表中的一个中文首选名；
- 每份文档前面只解释本文真正使用的术语，并链接全局术语表；
- 明确区分“事实”“现行决定”“候选方案”“推断”和“待验证假设”；
- ADR 的 Decision 使用明确规范性语言，其余部分允许解释来龙去脉和真实权衡。

## 7. 评审和接受规则

- 用户说“继续”只授权进入下一步，不代表接受上一份决定；
- Problem/Requirements/Constraints 先单独确认，避免在方案比较后才改变问题；
- 对长期决定，Accepted 需要用户明确确认或 PR 审批；
- 证据失败不能通过改措辞变成通过，应保留失败结果并回到 RFC/ADR；
- 对 Accepted ADR 的语义改变必须新增 ADR；修正拼写、链接或不改变语义的澄清可以原位修改，
  但应在提交说明中写清范围；
- 步骤 1 只审核来源身份、访问、完整性、角色和引用限制；来源中的产品事实、优先级、owner
  和技术冲突进入后续步骤裁决；
- 每一步退出前都执行本地链接、Markdown fence、Mermaid、术语和差异检查。

## 8. 旧文档迁移记录

步骤 2 会为每份旧文档建立以下记录：

```text
现有路径
→ 当前文档类型与权威性
→ 关联需求 / ADR / Evidence
→ 已知冲突与缺口
→ Keep / Clarify / Split / Supersede / Archive
→ 新权威文档或继任者
```

移动旧路径时优先保留链接桩，避免历史 PR、证据报告和外部引用失效。Accepted ADR 和原始
验证证据原则上保留原路径；未完成审计前不进行批量 `git mv`。

## 9. 每份步骤文档的最小模板

```markdown
# 标题

> 状态：Draft
> 规范性：No
> 输入：SRC-...
> 相关需求 / ADR / RFC / Contract / Validation：...
> 最后评审：YYYY-MM-DD

## 本文用语

## 问题与背景

## 需求

## 约束与非目标

## 方案与权衡

## 当前结论

## 验证计划与退出条件

## 影响和后续工作

## 来源
```

不是每份文档都机械保留所有空章节；模板的目的是保证决策链完整，而不是制造篇幅。
