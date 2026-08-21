# Axiom 架构重审来源目录

> 状态：Reviewed / Source inventory complete
> 步骤状态：Completed / Source inventory closed
> 输入：当前仓库快照、用户直接要求与批准、受限 Notion、历史对话及其本地导出
> 评审记录：2026-08-21 15:11（UTC+08:00）确认第一组五条目录规则；随后确认第二组仓库与直接用户输入、第三组 Notion、第四组历史对话、第五组竞品矩阵来源及第六组来源全集/缺口规则
> 更新日期：2026-08-21
> 说明：本目录只保存脱敏来源 ID，不保存私有 Notion/ChatGPT URL、原始消息或附件路径

## 本文用语

| 名称 | 含义 |
| --- | --- |
| 来源记录 | Source Record | 对一个可定位 artifact、页面、消息或来源集合的目录登记；记录数不表示独立证据数。 |
| 来源行 | Source Row | 竞品矩阵中的原始表格行；可能是复合能力，不等于正式 Requirement。 |
| Intake ID | Intake ID | 步骤 3 为来源行分配的稳定接收编号，用于拆分、去重和需求评审；当前尚未建立。 |
| Owner Mapping | Owner Mapping | 来源建议的 Axiom/Arc/Data/Shell 责任映射，只是 Architecture Recommendation。 |
| Primary-source Gap | Primary-source Gap | 来源引用产品行为或历史决定，但缺少能直接核验该 claim 的一手材料。 |
| Reported Observation | Reported Observation | 来源声称观察到的产品行为；绑定版本、平台、日期和一手方法后才能升级为已核验事实。 |
| Model Synthesis | Model Synthesis | 模型做出的分类、归并、统计或差距总结；可用于发现问题，不能代替产品事实。 |
| Requirement Candidate | Requirement Candidate | 值得进入需求评审的候选目标、能力或优先级；尚未成为正式需求。 |
| Architecture Recommendation | Architecture Recommendation | 来源提出的模块归属或实现方向；只能进入 Options/Decision Backlog，不能覆盖 ADR。 |

## 一、目录使用规则

### 1. 五个维度必须分开

每项来源分别记录以下维度，不能再用一个 `Reviewed` 或 `Restricted` 混合表达：

| 维度 | 取值与解释 |
| --- | --- |
| 访问级别 | `Repository`：仓库可复查；`Public`：公开可访问；`Restricted`：需要登录或私有权限；`Local-only`：只存在于未跟踪的本地附件/导出。 |
| 读取范围 | `Complete`：在该行明确声明的 representation 和边界内全部可得并读完；不自动表示动态来源、原始会话或链接目标完整。`Partial`：只得到或读取一部分；`Index-only`：只读索引/链接集；`Unavailable`：当前不可得。读取范围必须注明边界。 |
| 审核状态 | `Unreviewed → Reviewing → Reviewed`。`Reviewed` 只表示身份、范围、限制、证据角色和引用规则已经审核，不表示内容正确或方案获批。 |
| 证据角色与权威性 | `Normative baseline`、`Direct user input`、`Primary evidence`、`Secondary synthesis`、`Historical context`、`Evidence lead`；权威性另标 `Normative / Non-normative / Mixed`。 |
| 保留与引用 | `Repository reference`、`Public citation`、`Sanitized summary only`、`Local fingerprint only`；必要时组合使用。 |

`Restricted` 是访问级别，可以与 `Reviewed` 同时成立。来源内容是否被采用不再使用来源级
`Incorporated` 表达；正式 Requirement、ADR、Spec 或 Contract 应逐项引用支撑它的 `SRC-*`，
防止“一条结论被吸收”被误读成“整份来源都已获批”。

### 2. 本步骤审核什么

步骤 1 只确认：来源是否存在、怎样定位、访问与读取边界、证据角色、权威范围、保留方式和
引用限制。它不裁决来源中的产品事实、P0/P1/P2、V1 范围、模块 owner 或技术方案。

仓库来源也不是统一权威等级。Accepted ADR、已批准 Contract、代码行为、测试定义、实际
验证报告和 Research Input 必须分开引用。外部页面中的 `Accepted` 只表示来源内部状态；
模型回顾不能证明用户批准，竞品能力表也不能代替一手产品证据。

### 3. 隐私与可复查性

- 私有 URL、消息原文、附件路径、身份信息和未授权材料不写入 Git；
- Restricted 来源只保存 ID、脱敏摘要、采集日期、读取范围和允许的本地指纹；
- HTML “文件读完”不等于“原始会话完整”，两者分别记录；
- 动态页面的采集标签不冒充来源正式版本；无 revision/hash 时明确写 `未固定快照`；
- 来源以后不可访问时改为 `Unavailable`，记录原因、影响和后续，不用模型记忆补造原文。

## 二、来源组覆盖表

截至 2026-08-21，当前已定位的输入及步骤 1 审核批准按 artifact/集合登记如下。不同 artifact
如果只是同一来源的 representation，则共用一个来源 ID，也不会被当成多份独立佐证。第三组
已经确认 Notion 的 inclusion/stopping rule，并按
确认当日的精确页面清单封闭；后续发现的实质材料作为目录增量处理。第六组的最终批准只写入
评审记录和退出状态，不再反向新增为本目录的输入来源，避免用“确认全集”创造新的待确认来源。

| 来源组 | 预期记录 | 已登记 | 覆盖情况 |
| --- | ---: | ---: | --- |
| Git 仓库快照 | 1 | 1 | 已覆盖 `main@74c28b1`；逐文件权威性在步骤 2 审计。 |
| 用户转交的流程材料 | 1 | 1 | 已覆盖；用户已确认它与 `SRC-CHAT-07` 不是同一来源。 |
| 用户直接任务要求与明确批准 | 7 | 7 | 架构重审 brief、工作流批准、术语批准、目录规则批准，以及第二、三、四/五组批准。 |
| Notion 根页及直接子页面 | 26 | 26 | 1 个根页和 25 个直接子页面均已登记；只读索引者保留 `Index-only`。 |
| Notion 间接或索引引用页面 | 10 | 10 | 按 2026-08-21 确认的 inclusion/stopping rule 纳入并逐项登记。 |
| 竞品矩阵引用的官方资料集合 | 1 | 1 | 21 个链接仅作 Evidence lead，尚未逐项审读。 |
| 历史对话 | 7 | 7 | 六份局部架构/产品讨论及一份同类产品需求调研对话。 |
| **合计** | **53** | **53** | ID 全部唯一；记录数不证明逻辑独立性。 |

## 三、已登记来源

| 来源 ID | 类型、版本与采集点 | 覆盖主题 | 访问级别 | 读取范围 | 审核状态 | 证据角色 / 权威性 | 保留、引用、缺口与后续 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| SRC-REPO-MAIN-20260821 | Git snapshot，`main@74c28b1`，2026-08-21 | 架构/ADR/API/计划、实现、测试、证据和研究材料 | Repository | Partial：完成 tree inventory 与关键基线定位；逐文件审计留步骤 2 | Reviewed | Mixed：Normative baseline + implementation behavior + primary evidence + research | Repository reference；引用必须带 commit+path，并按文件状态解释权威性。 |
| SRC-USER-WORKFLOW-20260821 | User-provided AI-assisted pasted text，2026-08-21 | 历史架构流程重排建议 | Restricted | Complete：仅指收到的粘贴 artifact | Reviewed | Secondary synthesis / Non-normative | Sanitized summary only；用户已确认它与 `SRC-CHAT-07` 不是同一来源。 |
| SRC-USER-ARCH-REVIEW-BRIEF-20260821 | Direct user request，2026-08-21 | 分步骤审查、每步独立成文、中文为主、术语前置、自然写作并最终入库 | Restricted | Complete：对应直接任务要求 | Reviewed | Direct user input / Normative for this review scope | Sanitized summary only；只约束本轮工作目标，不产生产品架构决定。 |
| SRC-USER-WORKFLOW-CONFIRMATION-20260821 | Explicit user approval，2026-08-21 | 明确批准步骤 0 文档工作流，并要求术语表另行审核 | Restricted | Complete：对应批准消息 | Reviewed | Direct user input / Process-normative | Sanitized summary only；批准范围仅是 `DOCUMENTATION_WORKFLOW.md`，没有提前批准术语策略。 |
| SRC-USER-GLOSSARY-CONFIRMATION-20260821 | Explicit user approval，2026-08-21 | 逐项确认 7 条术语策略 | Restricted | Complete：对应逐项确认消息 | Reviewed | Direct user input / Review-normative | Sanitized summary only；只接受词义和评审边界，不升级标为候选的技术方案。 |
| SRC-USER-SOURCE-CATALOG-RULES-CONFIRMATION-20260821 | Explicit user approval，2026-08-21 15:11（UTC+08:00） | 确认步骤 1 的 5 条目录规则并进入第二组审核 | Restricted | Complete：对应批准消息 | Reviewed | Direct user input / Process-normative | Sanitized summary only；不表示步骤 1 整体通过，也不批准目录中的技术结论。 |
| SRC-USER-SOURCE-GROUP2-CONFIRMATION-20260821 | Explicit user approval and correction，2026-08-21 | 确认第二组第 1～3 条；明确 `SRC-CHAT-07` 是同类产品需求调研，而非架构重置流程 | Restricted | Complete：对应批准与更正消息 | Reviewed | Direct user input / Process-normative for source identity | Sanitized summary only；只批准第二组来源分类。`SRC-CHAT-07` 可进入需求发现、Roadmap 和架构问题定界，但不能单独或直接支撑架构决定。 |
| SRC-USER-SOURCE-GROUP3-CONFIRMATION-20260821 | Explicit user approval and clarification，2026-08-21 | 确认第三组 Notion 来源规则、10 个间接页面纳入范围及三份竞品需求材料的 lineage | Restricted | Complete：对应批准与澄清消息 | Reviewed | Direct user input / Process-normative for source scope and lineage | Sanitized summary only；确认 `SRC-CHAT-07`、产品功能基线和竞品矩阵之间不存在直接生成或继承关系，不表示其重合 claim 可重复计权。 |
| SRC-USER-SOURCE-GROUP4-5-CONFIRMATION-20260821 | Explicit user approval，2026-08-21 | 确认第四组 6 条历史对话规则，并确认第五组竞品矩阵的两条核心边界 | Restricted | Complete：对应批准消息 | Reviewed | Direct user input / Process-normative for source roles | Sanitized summary only；确认 178 个 source rows 只是需求输入而非正式需求，21 个官方链接只是 Evidence leads；不表示矩阵中的产品 claim 已核验。 |
| SRC-NOTION-BASELINE-V03 | Restricted Notion；来源自称标题 `Axiom 整体架构基线 v0.3`；captured 2026-08-21 | 总体基线、模块所有权、Shell、Scene/Render、Arc、Data、Persistence/Sync 和 open decisions | Restricted | Complete：本轮可见根页正文已读；不含链接子页正文或修订历史，未固定不可变快照 | Reviewed | Historical context + architecture recommendations / Non-normative | Sanitized summary only；已核实为 25 个直接子页面的导航父页，导航层级不证明内容派生关系。 |
| SRC-NOTION-LEDGER-V03 | Restricted Notion；来源自称标题 `Axiom Decision Ledger v0.3`；captured 2026-08-21 | DL-001～021、Superseded、Source Conflict 和 Open Decisions | Restricted | Complete：本轮可见页面正文已读；不含链接目标或修订历史 | Reviewed | Historical context / Non-normative | Sanitized summary only；具体 DL/OD/Superseded/Source Conflict claim 进入步骤 2 时建立 child ID。 |
| SRC-NOTION-ADR-V03 | Restricted Notion；来源自称标题 `Axiom Architecture Decision Records v0.3`；captured 2026-08-21 | Notion 内部 ADR 索引 | Restricted | Index-only：步骤 1 只读取索引；步骤 2 后续读取 ADR-001～013 的可见正文，边界见步骤 2 child locator | Reviewed | Evidence lead + historical context / Non-normative | Sanitized summary only；正文审计不改变本索引来源的权威性，也不增加顶层来源数。 |
| SRC-NOTION-RFC-V03 | Restricted Notion；来源自称标题 `Axiom RFCs v0.3`；captured 2026-08-21 | Persistence、Sync、Arc ABI 等 RFC 索引 | Restricted | Index-only：步骤 1 只读取索引；步骤 2 后续读取三份 RFC 的可见正文，边界见步骤 2 child locator | Reviewed | Evidence lead + historical context / Non-normative | Sanitized summary only；正文审计不把 Draft RFC 升级为现行规范，也不增加顶层来源数。 |
| SRC-NOTION-FUNCTIONS-V01 | Restricted Notion；来源自称标题 `Axiom & Arc 产品功能基线 v0.1 — Vibe 对齐清单`；captured 2026-08-21 | Vibe 对齐能力及 V1/V2/Architecture-ready 候选范围 | Restricted | Complete：本轮可见正文已读；不含链接目标或修订历史 | Reviewed | Requirement candidates + model synthesis / Non-normative | Sanitized summary only；优先级和范围仍需步骤 3 确认；与 `SRC-CHAT-07`、`SRC-NOTION-COMPETITIVE-20260821` 无直接生成或继承关系。 |
| SRC-NOTION-HOST-V01 | Restricted Notion；来源自称标题 `Platform Host Runtime Contract v0.1 — Axiom / Arc Orchestration & Shell Facade`；captured 2026-08-21 | Axiom SDK、Host/Arc packaging、Native input/preview 和 Web bundle 候选边界 | Restricted | Complete：本轮可见正文已读；不含链接目标或修订历史 | Reviewed | Architecture recommendations / Non-normative | Sanitized summary only；标题中的 Contract 不使其自动覆盖仓库 ADR。 |
| SRC-NOTION-DECISION-LOG-V10 | Restricted Notion；来源自称标题 `Axiom Architecture Decision Log v1.0`；discovered 2026-08-21 | 历史决定汇总页面 | Restricted | Index-only：只从根页确认标题与链接，正文未审 | Reviewed | Evidence lead + historical context / Non-normative | Sanitized summary only；进入步骤 2 时读取正文并与 Ledger/ADR 去重。 |
| SRC-NOTION-SYSTEM-DIAGRAM-V10 | Restricted Notion；来源自称标题 `Axiom Final System Architecture Diagram v1.0`；discovered 2026-08-21 | 候选最终系统图 | Restricted | Index-only：只确认标题与链接 | Reviewed | Evidence lead + architecture recommendation / Non-normative | Sanitized summary only；图中结论须回溯 ADR，不能因 `Final` 得到权威性。 |
| SRC-NOTION-DOCUMENT-INDEX-V10 | Restricted Notion；来源自称标题 `Axiom Architecture Directory & Document Index v1.0`；discovered 2026-08-21 | Notion 文档目录与索引 | Restricted | Index-only：只确认标题与链接 | Reviewed | Evidence lead / Non-normative | Sanitized summary only；用于补齐 lineage，不证明被索引内容已审。 |
| SRC-NOTION-DESIGN-JOURNEY-V10 | Restricted Notion；来源自称标题 `Axiom Architecture Overview & Design Journey v1.0`；discovered 2026-08-21 | 架构概览与设计历程 | Restricted | Index-only：只确认标题与链接 | Reviewed | Historical context + model synthesis / Non-normative | Sanitized summary only；历史回顾不能替代用户批准记录。 |
| SRC-NOTION-POC01-DESIGN-V01 | Restricted Notion；来源自称标题 `Axiom POC-01 Design — Minimal Vertical Slice Specification v0.1`；discovered 2026-08-21 | POC-01 候选设计 | Restricted | Index-only：只确认标题与链接 | Reviewed | Historical context + implementation recommendation / Non-normative | Sanitized summary only；仓库实际 POC-01/证据优先。 |
| SRC-NOTION-ROADMAP-V01 | Restricted Notion；来源自称标题 `Axiom Implementation Roadmap & POC Decomposition v0.1`；discovered 2026-08-21 | 实施路线与 POC 拆分建议 | Restricted | Index-only：只确认标题与链接 | Reviewed | Requirement candidates + architecture recommendations / Non-normative | Sanitized summary only；只作 roadmap 输入，不改变仓库阶段状态。 |
| SRC-NOTION-BINDING-V01 | Restricted Notion；来源自称标题 `Axiom Binding Architecture v0.1 — React Native / Web / Native Integration`；discovered 2026-08-21 | Binding 与多端集成建议 | Restricted | Index-only：只确认标题与链接 | Reviewed | Architecture recommendation / Non-normative | Sanitized summary only；与 ADR-0022/0023 对账后使用。 |
| SRC-NOTION-PUBLIC-ABI-TYPES-V01 | Restricted Notion；来源自称标题 `Axiom Public ABI Type System & Lifetime Model v0.1`；discovered 2026-08-21 | ABI 类型与生命周期建议 | Restricted | Index-only：只确认标题与链接 | Reviewed | Architecture recommendation / Non-normative | Sanitized summary only；不覆盖 Runtime C API Contract。 |
| SRC-NOTION-PUBLIC-ABI-BOUNDARY-V01 | Restricted Notion；来源自称标题 `Axiom Public C++ / C ABI Boundary v0.1`；discovered 2026-08-21 | 公共 C++/C ABI 边界建议 | Restricted | Index-only：只确认标题与链接 | Reviewed | Architecture recommendation / Non-normative | Sanitized summary only；公共 SDK 与 Runtime C ABI 分开评审。 |
| SRC-NOTION-CPP-MODULES-V01 | Restricted Notion；来源自称标题 `Axiom C++ Module Boundary & Repository Architecture v0.1`；discovered 2026-08-21 | C++ 模块与仓库结构建议 | Restricted | Index-only：只确认标题与链接 | Reviewed | Architecture recommendation / Non-normative | Sanitized summary only；与当前代码只做步骤 2 对账。 |
| SRC-NOTION-BACKEND-ABSTRACTION-V01 | Restricted Notion；来源自称标题 `Axiom Platform Backend & Rendering Backend Abstraction v0.1`；discovered 2026-08-21 | 平台/渲染 backend 抽象 | Restricted | Index-only：只确认标题与链接 | Reviewed | Architecture recommendation / Non-normative | Sanitized summary only；具体 backend 仍由 ADR/验证裁决。 |
| SRC-NOTION-FRAME-PIPELINE-V01 | Restricted Notion；来源自称标题 `Axiom Runtime Scheduler & Frame Pipeline v0.1`；discovered 2026-08-21 | Runtime scheduler 与帧管线建议 | Restricted | Index-only：只确认标题与链接 | Reviewed | Architecture recommendation / Non-normative | Sanitized summary only；进入 Runtime 主题时读取。 |
| SRC-NOTION-THREADING-V01 | Restricted Notion；来源自称标题 `Axiom Threading & Runtime Scheduling Model v0.1`；discovered 2026-08-21 | 线程和调度模型建议 | Restricted | Index-only：只确认标题与链接 | Reviewed | Architecture recommendation / Non-normative | Sanitized summary only；线程拓扑仍须 RFC/benchmark。 |
| SRC-NOTION-MEMORY-V01 | Restricted Notion；来源自称标题 `Axiom Runtime Memory & Residency Model v0.1`；discovered 2026-08-21 | 内存与 residency 建议 | Restricted | Index-only：只确认标题与链接 | Reviewed | Architecture recommendation / Non-normative | Sanitized summary only；资源预算仍是证据缺口。 |
| SRC-NOTION-SPATIAL-QUERY-V01 | Restricted Notion；来源自称标题 `Axiom SpatialIndex & Query Engine v0.1`；discovered 2026-08-21 | SpatialIndex 与查询建议 | Restricted | Index-only：只确认标题与链接 | Reviewed | Architecture recommendation / Non-normative | Sanitized summary only；算法不因标题冻结。 |
| SRC-NOTION-TILE-STRATEGY-V01 | Restricted Notion；来源自称标题 `Axiom Tile & Spatial Rendering Strategy v0.1`；discovered 2026-08-21 | Tile 与空间渲染建议 | Restricted | Index-only：只确认标题与链接 | Reviewed | Architecture recommendation / Non-normative | Sanitized summary only；与 RF-01～03/ADR-0021 对账。 |
| SRC-NOTION-RENDER-GRAPH-V01 | Restricted Notion；来源自称标题 `Axiom Render Compiler & Render Graph Model v0.1`；discovered 2026-08-21 | Render Compiler/Graph 建议 | Restricted | Index-only：只确认标题与链接 | Reviewed | Architecture recommendation / Non-normative | Sanitized summary only；不覆盖现行 Scene/FrameGraph 边界。 |
| SRC-NOTION-SCENE-RUNTIME-V01 | Restricted Notion；来源自称标题 `Axiom Scene Runtime & Derived State Model v0.1`；discovered 2026-08-21 | Scene 与派生状态建议 | Restricted | Index-only：只确认标题与链接 | Reviewed | Architecture recommendation / Non-normative | Sanitized summary only；与 RF-01 物理映射留步骤 2。 |
| SRC-NOTION-DOCUMENT-OPERATION-V01 | Restricted Notion；来源自称标题 `Axiom Document & Operation Model v0.1`；discovered 2026-08-21 | Document/Operation 建议 | Restricted | Index-only：只确认标题与链接 | Reviewed | Architecture recommendation + historical context / Non-normative | Sanitized summary only；与 ADR-0003/0014/0020/0022 对账。 |
| SRC-NOTION-COMPETITIVE-20260821 | Restricted Notion，AI-assisted matrix，正式标题含日期 2026-08-21 | Vibe + 6 款参照产品、8 域、实际 178 个 source rows、候选优先级和 owner mapping | Restricted | Complete：可见表格已读完；不含 21 个外链正文或修订历史，未固定不可变快照 | Reviewed | Reported observations + model synthesis + requirement candidates + architecture recommendations / Non-normative | Sanitized summary only；产品事实存在 Primary-source gap；与 `SRC-CHAT-07`、`SRC-NOTION-FUNCTIONS-V01` 无直接生成或继承关系。 |
| SRC-NOTION-PUBLIC-RUNTIME-FACADE-V01 | Restricted Notion；正式标题 `Axiom Public Runtime Facade v0.1`；来源自称 v0.1；discovered 2026-08-21 | Public Runtime facade 候选调用面与边界 | Restricted | Index-only：只从文档索引确认标题与页面身份；正文和修订历史未审 | Reviewed | Architecture recommendation / Non-normative | Sanitized summary only；与 Runtime C API Contract、ADR-0022 和产品 SDK 边界对账；标题中的 Public 不产生兼容承诺。 |
| SRC-NOTION-CANVAS-COMMAND-STATE-V01 | Restricted Notion；正式标题 `Axiom Canvas Command & State Contract v0.1`；来源自称 v0.1；discovered 2026-08-21 | Command 与可查询状态的候选契约 | Restricted | Index-only：只从文档索引确认标题与页面身份；正文和修订历史未审 | Reviewed | Architecture recommendation / Non-normative | Sanitized summary only；与 Command/Operation/EditorSession 和现行 C API 对账；Contract 标题不表示获批。 |
| SRC-NOTION-TOOL-BRUSH-PUBLIC-SCHEMA-V01 | Restricted Notion；正式标题 `Axiom Tool & Brush Public Schema v0.1`；来源自称 v0.1；discovered 2026-08-21 | Tool/Brush 的候选公共 schema | Restricted | Index-only：只从文档索引确认标题与页面身份；正文和修订历史未审 | Reviewed | Architecture recommendation / Non-normative | Sanitized summary only；与现行 Tool/Brush/Ink Contract 对账；标题不冻结格式或 SDK。 |
| SRC-NOTION-OBJECT-RUNTIME-REPRESENTATION-V01 | Restricted Notion；正式标题 `Axiom Object Model & Runtime Representation v0.1`；来源自称 v0.1；discovered 2026-08-21 | Semantic Object 与 Runtime 表示的候选映射 | Restricted | Index-only：只从文档索引确认标题与页面身份；正文和修订历史未审 | Reviewed | Architecture recommendation / Non-normative | Sanitized summary only；与 Document/RuntimeScene 分离和 V1 node scope 对账，不从标题接受具体映射。 |
| SRC-NOTION-RENDERER-COMPILATION-PIPELINE-V01 | Restricted Notion；正式标题 `Axiom Renderer Architecture & Render Compilation Pipeline v0.1`；来源自称 v0.1；discovered 2026-08-21 | Renderer 与 render compilation pipeline 候选设计 | Restricted | Index-only：只从文档索引确认标题与页面身份；正文和修订历史未审 | Reviewed | Architecture recommendation / Non-normative | Sanitized summary only；与 SceneCompiler、FrameBuilder、FrameGraph 和 RendererBackend 现行边界对账。 |
| SRC-NOTION-END-TO-END-RUNTIME-FLOW-V01 | Restricted Notion；正式标题 `Axiom End-to-End Runtime Flow v0.1`；来源自称 v0.1；discovered 2026-08-21 | Runtime 端到端控制流、数据流与生命周期候选视图 | Restricted | Index-only：只从文档索引确认标题与页面身份；正文和修订历史未审 | Reviewed | Architecture recommendation + model synthesis / Non-normative | Sanitized summary only；只作步骤 2 对账线索，不以流程图覆盖 ADR、Contract 或验证证据。 |
| SRC-NOTION-ADR-013-TS-DATA-BRIDGE | Restricted Notion；正式标题 `ADR-013 — Axiom ↔ TS Data Bridge`；discovered 2026-08-21 | Axiom 与 TS Data Bridge 的历史候选决定 | Restricted | Index-only：限定 Notion 搜索确认标题与页面身份；正文、来源状态和修订历史未审 | Reviewed | Evidence lead + historical context / Non-normative | Sanitized summary only；不等于仓库 ADR-0013，也不因 ADR 标题获得 Accepted 身份；步骤 2 按 page identity 与 ADR 索引 child 去重。 |
| SRC-NOTION-ARC-BUFFER-MEMORY-ABI-V01 | Restricted Notion；正式标题 `Arc Buffer & Memory Ownership ABI v0.1`；来源自称 v0.1；discovered 2026-08-21 | Arc buffer、memory ownership 与候选 ABI | Restricted | Index-only：限定 Notion 搜索确认标题与页面身份；正文和修订历史未审 | Reviewed | Architecture recommendation / Non-normative | Sanitized summary only；ABI 标题不产生公共稳定承诺；与 Arc protocol、buffer lifetime 和 C ABI 对账。 |
| SRC-NOTION-ARC-RESOURCE-REGISTRY-ABI-V01 | Restricted Notion；正式标题 `ArcResource Registry ABI v0.1`；来源自称 v0.1；discovered 2026-08-21 | ArcResource registry 与候选 ABI | Restricted | Index-only：限定 Notion 搜索确认标题与页面身份；正文和修订历史未审 | Reviewed | Architecture recommendation / Non-normative | Sanitized summary only；标题不冻结 registry ownership、handle 或 ABI；正文进入 Arc 主题时再读。 |
| SRC-NOTION-REQUIREMENTS-R2-PAGE-DOCUMENT-INK-BRUSH | Restricted Notion；正式标题 `需求深化 R2 — Page / Document Topology & Ink / Brush`；discovered 2026-08-21 | Page/Document topology 与 Ink/Brush 需求深化候选 | Restricted | Index-only：限定 Notion 搜索确认标题与页面身份；正文和修订历史未审 | Reviewed | Requirement candidates + architecture recommendations / Non-normative | Sanitized summary only；具体能力在步骤 3 建 Intake ID 并拆分、去重、验收，不直接升级为 Requirement 或 ADR。 |
| SRC-PUBLIC-COMPETITIVE-EVIDENCE-LEADS-20260821 | 竞品矩阵内的 21 个官方资料入口，discovered 2026-08-21 | Vibe、myViewBoard、EZWrite、Miro、FigJam、Lucidspark、Freeform 的产品资料线索 | Public | Index-only：只确认链接集合存在，尚未逐项读取或核验 | Reviewed | Evidence lead / Non-normative | 只表示入口集合的身份和角色已审核；绑定产品版本、平台、套餐、日期和具体 claim 后才能作为证据。 |
| SRC-CHAT-01 | Restricted ChatGPT share + local HTML representation，fingerprint 见下表 | Arc 命名、Axiom/Arc/Host、Input、Preview/Canonical、surface 与延迟 | Restricted | Partial：HTML artifact 已读完，但原始会话开头缺失 | Reviewed | User questions/constraints + historical context + model synthesis / Non-normative | Sanitized summary + local fingerprint only；模型回顾不能证明用户批准 Arc 边界或接口。 |
| SRC-CHAT-02 | Restricted ChatGPT share + local HTML representation，fingerprint 见下表 | Local-first、durability、Data Runtime、Snapshot、Operation/Transaction 与 BlobRef | Restricted | Partial：HTML artifact 已读完，但原始会话开头缺失 | Reviewed | Historical context + model synthesis / Non-normative | Sanitized summary + local fingerprint only；缺失回合中的用户意图不得补造，也不能证明 TS/C++ 或同步边界已批准。 |
| SRC-CHAT-03 | Restricted ChatGPT share + local HTML representation，fingerprint 见下表 | Scene、SpatialIndex、Damage、Tile/LOD、冷启动、Hydration 与 Working Set | Restricted | Partial：HTML artifact 已读完，但原始讨论保留不全 | Reviewed | Historical context + model synthesis / Non-normative | Sanitized summary + local fingerprint only；类名和算法建议不是现行实现或架构决定。 |
| SRC-CHAT-04 | Restricted ChatGPT share + local HTML representation，fingerprint 见下表 | 旧架构的多轮问题审查 | Restricted | Partial：HTML artifact 已读完，但缺少第一轮原始回合 | Reviewed | Historical context + model synthesis / Non-normative | Sanitized summary + local fingerprint only；模型回顾中的状态标签不能证明用户逐项批准。 |
| SRC-CHAT-05 | Restricted ChatGPT share + local HTML representation，fingerprint 见下表 | 多端、Persistence/Sync、Scene/Tile、Ink/Erase、Arc 和 Host | Restricted | Partial：HTML artifact 已读完，但多数早期内容仅由模型回顾保留 | Reviewed | User-reported observations + model synthesis + requirement candidates / Non-normative | Sanitized summary + local fingerprint only；Vibe 粗细笔与橡皮擦行为未经核验，缺产品版本、平台和观察日期。 |
| SRC-CHAT-06 | Restricted ChatGPT share + local HTML representation，fingerprint 见下表 | Vibe 产品价值、能力、AI、Semantic Graph、Overlay 和候选 POC | Restricted | Partial：HTML artifact 已读完，但最初 Vibe 输入缺失 | Reviewed | Reported observations + model synthesis + requirement candidates / Non-normative | Sanitized summary + local fingerprint only；存在 Primary-source gap，不能直接支撑架构或 POC 优先级。 |
| SRC-CHAT-07 | Restricted ChatGPT share，captured 2026-08-21 | 市面同类型产品调研、能力归纳、需求候选，以及 Roadmap/架构问题的输入线索 | Restricted | Complete：仅指采集时可见正文已读；不证明原始会话完整，未固定不可变快照 | Reviewed | Competitive requirements research：reported observations + model synthesis + requirement candidates / Non-normative | Sanitized summary only；可用于需求发现、Roadmap 和问题定界，不能绕过需求确认与方案评审直接支撑 ADR；与两份 Notion 需求材料无直接生成或继承关系，重合 claim 仍须去重。 |

## 四、第二组审核结论：仓库与直接用户输入

本节只审核来源角色，不批准仓库里的具体技术结论。

### 1. 仓库快照的四种角色

`SRC-REPO-MAIN-20260821` 保持一个 commit 级父来源，引用时必须进一步定位到 path：

| 证据角色 | 当前定位范围 | 使用限制 |
| --- | --- | --- |
| 现行规范候选 | `docs/adr/0001-*`～`0024-*`、`PROJECT_FRAMEWORK.md`、`SYSTEM_ARCHITECTURE.md`、Delivery/Quality baseline、Runtime C API Contract/header、C++ style；RF-01 只在其声明的内部 implementation baseline 范围内 | 文件自述状态不等于实现或验证完成；具体权威冲突在步骤 2 对账，Accepted ADR 正文优先于汇总状态。 |
| 实现行为 | `runtime/`、`arc/`、`pocs/`、CMake、CI、`tools/` 和依赖锁 | 只能证明该 commit 存在的行为；POC、private bridge 和实验 ABI 不能升级为产品规范。 |
| 验证证据 | 带 commit、环境、结果和产物身份的结构化/物理报告 | Test、fixture、golden、runbook 和 workflow 是验证机制，不是已经运行的 Evidence。 |
| 研究输入 | `docs/research/` 以及未被 ADR/Contract 吸收的方案说明 | 作为问题与 Options 输入；缺少原始 artifact 时保留 Primary-source gap。 |

步骤 2 再逐文件建立“规范声称 / 实现存在 / 验证通过”三列，不允许由任一层推导另外两层。

### 2. 直接用户输入的边界

- 架构重审 brief 是本轮任务约束，不是产品架构 ADR；
- 对工作流的明确批准只接受流程，不提前批准术语；
- 七项术语逐项确认只接受词义和评审边界；
- 2026-08-21 15:11 对五条目录规则的确认只约束步骤 1 的来源模型；
- 用户转交的 AI-assisted 流程文字仍是 Secondary synthesis；用户已确认它与 `SRC-CHAT-07`
  不是同一来源。用户“要求据此重排流程”与“明确批准最终工作流”也仍是不同 claim，必须分别追溯。

### 3. `SRC-CHAT-07` 的正确流向

`SRC-CHAT-07` 可以成为架构重审的上游输入，但中间不能省略需求确认和方案评审：

```text
同类产品调研中的观察和归纳
    ↓
需求候选 / Roadmap 参考 / 架构问题线索
    ↓
步骤 3 的需求确认，或步骤 6 的 Problem / Options 评审
    ↓
经一手证据、Axiom 约束和验证方案共同支撑的架构决定
```

因此，“是架构决策的一项输入”与“能直接证明某项架构选择”是两件事。前者成立，后者不成立。
调研中某个竞品具有某项能力，也不能单独推出 Axiom 必须实现该能力，更不能推出它必须由某个
模块或技术方案实现。

## 五、第三组审核结论：Notion 来源

本节只审核页面身份、采集范围、关系和后续引用方法，不接受页面中的架构结论。以下规则与
10 个间接页面的纳入边界已经获得用户确认。

### 1. 导航关系与内容派生关系

`Axiom 整体架构基线 v0.3` 当前可确认是 25 个直接子页面的导航根页。这个事实只能建立
`navigation_parent`，不能据此声称子页面由根页派生、彼此独立，或者版本随根页同步。

页面之间只有在正文、修订记录或用户确认明确说明时，才登记 `derived_from`、`supersedes`、
`representation_of` 等内容关系。当前没有证据的关系统一记为 `Unknown`，不靠标题和主题相似
度推断。

### 2. 来源自称版本与采集点

- 页面标题明确包含 `v0.3`、`v0.1` 或 `v1.0` 时，只按标题记录其“来源自称版本”；它不能证明
  页面已经批准、形成不可变 revision，或同版本页面属于同一次受控发布；
- 竞品矩阵标题使用日期，因此只登记正式标题中的日期，不增造 `V01`；
- `captured/discovered 2026-08-21` 只是本轮采集点，不是 Notion revision；
- 页面缺少 revision、导出 hash 或不可变快照时，目录持续明确这一限制。

### 3. `Complete` 的准确边界

动态 Notion 页面中的 `Complete` 只表示采集时可见正文或已经加载的表格已读完，不包含链接
子页正文、页面修订历史、评论、附件内容和外部链接正文。因此当前建议：

| 页面范围 | 读取范围与审核状态 |
| --- | --- |
| 根页、Decision Ledger、产品功能基线、Platform Host Contract、竞品矩阵 | 按各行写明边界的 `Complete / Reviewed`；其中 Ledger 已读 DL-001～021、Superseded、Source Conflict 和 Open Decisions。 |
| ADR 与 RFC 汇总页 | `Index-only / Reviewed`；没有据此声称各链接正文已读。 |
| 只从根页确认标题和链接的其他直接子页面 | `Index-only / Reviewed`；正文仍未读。 |

这里的 `Reviewed` 只表示来源元数据与引用规则已经审核，仍不表示页面内容正确或其中决定有效。

### 4. 目录之外发现的引用页面

文档索引和限定 Notion 搜索当前精确发现了以下 10 个不在根页 25 个直接子页面集合内的页面：

- `Axiom Public Runtime Facade v0.1`；
- `Axiom Canvas Command & State Contract v0.1`；
- `Axiom Tool & Brush Public Schema v0.1`；
- `Axiom Object Model & Runtime Representation v0.1`；
- `Axiom Renderer Architecture & Render Compilation Pipeline v0.1`；
- `Axiom End-to-End Runtime Flow v0.1`；
- `ADR-013 — Axiom ↔ TS Data Bridge`；
- `Arc Buffer & Memory Ownership ABI v0.1`；
- `ArcResource Registry ABI v0.1`；
- `需求深化 R2 — Page / Document Topology & Ink / Brush`。

本组已经确认以下 inclusion/stopping rule：纳入截至 2026-08-21 满足任一条件、可定位且与本轮
主题相关的 Notion 页面——根页、Decision Ledger、ADR/RFC 汇总页或文档索引明确引用，或者由
限定的 Axiom/Arc 架构搜索发现；排除重复 alias、外部站点链接和偶然关键词命中。以上 10 页已
按 Notion page identity 去重并逐项登记为 `Index-only / Reviewed`。本轮目录以确认当日的精确
清单封闭，日后新发现页面作为目录增量处理，不反向伪造本次全集。

### 5. 页面与下游 claim 的编号

- 步骤 2 对可独立定位的 `DL-*`、`OD-*`、Superseded、Source Conflict 及 Notion ADR/RFC 条目
  建立 child source ID；不能独立定位的 claim 使用父 `SRC-*` 加来源原生条目 ID，不强造 source；
- child ID 建立前按 Notion page identity 与间接发现页面去重，避免同一页面得到两个 `SRC-*`；
- 步骤 3 对产品功能基线和竞品矩阵的具体能力行建立 `Intake ID`，用于拆分、去重和需求审核，
  不把每个表格行伪装成一份独立 source；若某行实际链接到独立页面，该页面仍可另有 source ID；
- 用户已确认 `SRC-CHAT-07`、产品功能基线和竞品矩阵之间没有直接生成或继承关系；这三项仍
  可能包含重合 claim，须在步骤 2/3 去重，不能仅凭来源数量累加证据权重。

## 六、第四组审核结论：历史对话

本组审核 `SRC-CHAT-01～07` 的 representation、完整性、证据角色和引用限制，不审核其中技术
方案是否正确，也不从模型回顾推导用户批准。

### 1. 分享页与 HTML 的关系

`SRC-CHAT-01～06` 的 ChatGPT 分享页与对应本地 HTML 是同一逻辑来源的两种 representation，
不是两份独立证据。本地 HTML 只提供本次读取字节的 fingerprint；它既不使动态分享页成为
不可变来源，也不证明原始会话完整。

### 2. 读取完整性

- `SRC-CHAT-01～06` 均保持 `Partial`：对应 HTML artifact 已读完，但六份都从助手消息开始，
  早期用户输入或原始回合有缺失；
- `SRC-CHAT-07` 的 `Complete` 只表示采集时分享页可见正文已读，不证明原始私有会话完整，
  也没有 revision/hash；
- “artifact 已读完”和“原始对话完整”必须继续分开记录。

### 3. claim 的角色必须分开

历史对话中的用户原始问题或约束、用户报告的产品观察、模型分析/回顾，以及模型提出的技术
建议应按具体 claim 分类。模型写出的“已确认”“最终方案”等状态不能证明用户批准；当前六份
HTML 中也没有可独立定位的架构批准记录。因此 `SRC-CHAT-01～07` 整体保持 Non-normative，
需要时再由步骤 2 对可定位 claim 建立精确引用。

尤其是 `SRC-CHAT-05` 对 Vibe 粗细笔及橡皮擦行为的描述，只能作为未经核验的 User-reported
Observation；产品版本、平台和观察日期仍是 Primary-source gap。

### 4. 缺失回合的影响

缺失回合不阻塞步骤 2 盘点“曾经讨论过什么”，但会阻止相关 claim 升级为产品事实、正式需求、
用户决定或 Accepted ADR。升级前至少需要用户重新确认、可定位的仓库批准记录或适当的一手证据；
不得用模型总结补造缺失的用户意图。

### 5. 仓库保留边界

仓库只保留 `SRC-*`、脱敏主题/claim 摘要、读取边界和 `SRC-CHAT-01～06` 的本地文件 fingerprint。
私有分享 URL、HTML 原件、消息 ID、回合原文和附件路径均不提交。

### 6. 与 Notion 需求材料的关系

用户已经确认 `SRC-CHAT-07`、Notion 产品功能基线和竞品矩阵之间没有直接生成或继承关系。
这项确认建立 `no_direct_lineage`，但不表示三者内容天然独立：相同能力或观察仍须按 claim 去重，
不能因为三份材料重复提及就自动提高证据权重。

## 七、本轮来源引入顺序

下面只表示材料在本轮中的引入顺序，不证明 Notion 页面与历史对话的创建时间、派生关系或
证据独立性：

```text
六份局部架构/产品历史对话及其 HTML representation
    ↓
同类型产品需求调研对话 + 用户转交的架构重置流程材料（两者不是同一来源）
    ↓
仓库内步骤 0 工作流与术语确认
    ↓
竞品需求矩阵
    ↓
步骤 1 来源目录审核
```

历史对话编号只用于稳定引用，不代表 `SRC-CHAT-01～06` 之间的精确顺序；下载或采集日期也
不能替代对话发生日期。

## 八、第五组审核结论：竞品矩阵来源

用户已经确认两个核心边界：178 个 source rows 只是待接收、拆分和去重的需求输入，不是
178 个正式或彼此独立的需求；21 个官方链接只是 Evidence leads，不证明矩阵单元格或产品行为。

矩阵覆盖 Vibe 与 6 款参照产品。实际表格行数为：

| 需求域 | Source rows |
| --- | ---: |
| Canvas / Navigation | 16 |
| Ink / Stylus / Low-latency | 23 |
| Objects / Editing | 30 |
| Structured Work | 15 |
| Collaboration / Meeting | 32 |
| AI | 21 |
| Persistence / Sync / History / Assets | 19 |
| Platform / External Surface / Import-Export | 22 |
| **合计** | **178** |

来源自称 177 项，Collaboration 标题自称 31 项；实际分别是 178 行和 32 行。来源建议的
`source-priority` 为 P0 53、P1 74、P2 51，合计 178。这里不修正来源，也不把 178 行当成
178 个正式或独立需求，也不为每行建立一个 `SRC-*`。步骤 3 将按需建立 `Intake ID`，并另建
`baseline-priority`；步骤 7 的问题严重度使用
`issue-severity`，三者不得混用。

产品单元格是 Reported Observation，八域分类和统计是 Model Synthesis，P0/P1/P2 是
Requirement Candidate，owner 是 Architecture Recommendation。`○` 只表示当前未知，不表示
产品绝对不支持。21 个官方入口目前只是 Evidence lead，不构成逐单元格验证。

## 九、本地导出的版本指纹

| 来源 ID | SHA-256 |
| --- | --- |
| SRC-CHAT-01 | `eb29a6de8955505def27354b1c3a91ae44cb570f7568f926a4b1a59c859f87d0` |
| SRC-CHAT-02 | `98b2852163b712caff48a0c167ccc740cbc4b0253c7d874f336cf7daf2d6eda1` |
| SRC-CHAT-03 | `2cee3a883e980b32c9d18dd827e6c429c0872262fbdcabfc666b1311e1ac78dd` |
| SRC-CHAT-04 | `de470e25d93334a274ef50ef60f366bc94211591de633c4227351204e1deafab` |
| SRC-CHAT-05 | `3d51224679bdc2cd64f13174844cee26593910302fc9196c46d6c14a30bb5709` |
| SRC-CHAT-06 | `f299ffb5a979e368fea2b169e9a1fdfcc48924f3ec14232e597e9fa659c47531` |

这些 hash 只定位本轮读取的本地 HTML bytes，不证明内容正确。原件和提取文本都不提交。
Notion 与 `SRC-CHAT-07` 没有不可变 fingerprint；目录已经明确记录采集日和这一限制。

## 十、已知来源缺口

### 1. 访问或采集缺口

- `SRC-CHAT-01～06` 缺失的原始回合；
- `SRC-CHAT-07` 与动态 Notion 页面缺少不可变导出/revision/hash；
- 步骤 1 关闭时 Notion ADR/RFC 只读索引；步骤 2 已逐项读取 13 份 ADR 与 3 份 RFC 的可见正文，
  但仍缺不可变 revision/hash、评论、修订历史和外链正文；
- 用户已确认 `SRC-CHAT-07`、产品功能基线和竞品矩阵无直接生成或继承关系；三者仍可能有
  内容重合，必须按 claim 去重，不能仅因不同来源重复提及就增加证据权重。

这些项目只要保持 `Partial/Index-only` 并说明影响，就不阻塞步骤 2 的现状审计；它们会阻止
缺失 claim 被直接当成用户决定或 Accepted ADR 的依据。

### 2. 内容证据缺口

- Vibe 与竞品行为的逐项一手证据、产品版本、平台、套餐和观察日期；
- Axiom 正式产品优先级与各平台可验收目标；
- 安全、隐私与信任边界；兼容迁移、发布和供应链约束；各平台资源预算；
- `VIBE_ARCHITECTURE_FINDINGS.md` 所依据但未入库的原始二进制 hash、提取脚本和最小复现资产。

这些缺口不阻塞来源目录和步骤 2，但会阻塞对应 Requirement、ADR 或 Contract 最终接受。

### 3. 第六组建议的阻塞分级

第六组建议按“当前工作能否继续”而不是按来源看起来是否完整来判断阻塞：

| 缺口类型 | 允许继续 | 阻塞事项 |
| --- | --- | --- |
| 历史对话缺回合、动态来源无 revision/hash、Notion 正文尚为 `Index-only` | 步骤 2 盘点现有 claim、冲突和待决项 | 用缺失内容证明用户批准、产品事实或 Accepted 决定；某页进入实质对账时必须先读取正文。 |
| 竞品产品行为未逐项核验，缺版本、平台、套餐和日期 | 步骤 3 接收、拆分、去重 Requirement Candidate；Axiom 也可基于明确的自身产品决定提出需求 | 接受“竞品具备某能力”或“必须实现竞品对齐”等外部事实，以及仅凭竞品观察确定 Axiom 的 P0/P1 或验收标准。 |
| Axiom 正式产品优先级、平台范围和可量化验收目标不足 | 需求发现和 Problem framing | 对应正式 Requirement baseline 的接受。 |
| 安全/隐私/信任边界、兼容迁移、供应链和资源预算不足 | 建立 Decision Backlog、比较方案、设计验证 | 依赖这些约束的 ADR、Stable Contract 和发布门禁最终接受。 |
| 研究报告缺原始二进制身份、提取脚本或复现资产 | 把报告作为 Research Input 或历史线索 | 把其中二进制观察升级为可复现 Primary Evidence，或以此单独接受架构决定。 |

这些都是局部阻塞，不应把整个步骤 1 或步骤 2 标成 Blocked。进入后续步骤时，每个 Requirement、
ADR、Contract 和 Validation 必须按自己引用的 claim 检查相应缺口；只有缺口实际支撑该结论时
才阻塞其接受。

测试定义、fixture、workflow 和 runbook 只证明验证机制存在，不证明门禁已经通过。Validation
接受还必须包含实际运行结果、环境或设备、目标 commit/artifact、阈值，以及原始产物身份。
Axiom 自身的明确产品决定与独立实验可以形成新的证据链，不要求先补齐所有竞品资料。

### 4. 来源快照与后续增量

本轮冻结的是截至 2026-08-21 已登记的 53 条目录记录，不是对未来资料的永久封闭。以后出现
新的仓库 commit、Notion 页面、用户直接需求或一手产品证据时，以新 `SRC-*` 增量登记，并注明
采集点和对现有 Requirement/Decision 的影响。第六组最终批准只关闭本次目录审核，不作为第
54 条输入来源；批准日期和范围写入评审记录即可。

## 十一、步骤 1 退出检查表

- [x] 截至 2026-08-21 的来源范围已经封闭；53 个唯一 ID 各归入一个来源组，分组计数可复算；
- [x] 每项分别记录访问级别、读取范围、审核状态、证据角色/权威性和引用限制；
- [x] `Partial / Index-only / Unavailable` 项记录原因、影响和后续处理；
- [x] 技术冲突只作为步骤 2 输入，不在来源目录裁决；
- [x] 用户确认第二组：仓库与直接用户输入；
- [x] 用户确认第三组：Notion 来源及父子/版本关系；
- [x] 用户确认第四组：六份局部历史对话与一份同类产品需求调研对话；
- [x] 用户确认第五组：竞品矩阵的来源角色和限制；
- [x] 用户确认第六组：来源全集、缺口及其阻塞级别；
- [x] `tools/check_docs.py`、`git diff --check` 和私有 locator 泄漏扫描通过。

步骤 1 完成只表示 Source inventory 已审核，不表示来源中的 claims 已核验或技术结论已接受。
第六组确认不新增来源 ID；本轮来源快照仍为 53 条，后续新增材料按增量登记。
