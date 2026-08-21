# Axiom 当前状态审计

> 状态：Reviewed / Step 2 audit complete
> 规范性：Non-normative；本文记录现状、冲突和迁移建议，不改变任何 Accepted ADR
> 审计基线：`main@74c28b1`，采集日期 2026-08-21
> 输入：SRC-REPO-MAIN-20260821、SRC-NOTION-BASELINE-V03、SRC-NOTION-LEDGER-V03、
> SRC-NOTION-ADR-V03、SRC-NOTION-RFC-V03、SRC-CHAT-01～07 及步骤 1 已登记来源
> 相关文档：[文档工作流](DOCUMENTATION_WORKFLOW.md)、[术语表](GLOSSARY.md)、
> [来源目录](SOURCE_CATALOG.md)
> 评审记录：2026-08-21 五组审核全部确认；第五组确认文档迁移边界与 Decision Backlog 移交
> 最后评审：2026-08-21

本文回答一个看似简单、实际很容易混淆的问题：截至审计基线，仓库和受限历史材料究竟已经
决定了什么、实现了什么、验证了什么，又有哪些说法不能同时成立。本文不会选择新方案，
也不会把 Notion 中自称 `Accepted` 的结论直接写回仓库。需要改变现行决定的事项进入后续
Problem、RFC 和 ADR；在那之前，仓库现有规范继续有效。

## 本文用语

| 名称 | 含义 |
| --- | --- |
| 规范声称 | 文档要求系统应当怎样工作，或记录一个已经接受的决定。 |
| 实现行为 | 基线 commit 中代码实际具有的行为；POC 实现仍是实验行为。 |
| 测试机制 | 测试、fixture、workflow 或 runbook 定义了怎样验证。它不证明验证已经运行并通过。 |
| 实际证据 | 与 commit、环境、设备、阈值和产物身份绑定的运行结果。 |
| 成熟度差 | 决策、Contract、实现、验证或文档状态文字处在不同阶段。成熟度差不一定是架构冲突。 |
| 子定位符 | Child Locator，父来源内部可独立引用的条目。它不是新的顶层来源，也不增加步骤 1 的来源数。 |
| 处理建议 | `Keep / Clarify / Split / Supersede / Archive`；是步骤 2 的迁移建议，不是已执行动作。 |

## 一、范围与审计方法

本轮审计覆盖 `main@74c28b1` 的规范、代码、测试和仓库证据，并与 2026-08-21 可见的 Notion
Decision Ledger、ADR/RFC 正文和历史讨论做对账。仓库在该基线共有 646 个 tracked files；
其中 `docs/` 73、`pocs/` 454、`runtime/` 41、`arc/` 29、`tools/` 31、workflow 9。数字只帮助
检查覆盖面，不代表功能完成度。

每个 claim 至少按下面四层判断：

```text
规范声称
  ≠ 实现存在
  ≠ 测试机制存在
  ≠ 实际证据通过
```

例如，ADR-0022 的 `Accepted` 表示“采用稳定 C ABI 作为产品 Runtime 边界”这一决定已接受；
它不表示 54 个声明函数已经有产品实现，也不表示 SDK 已发布。反过来，一个 POC 有代码和
CI，也不能自动把它的 schema、ABI 或内部类型提升为产品 Contract。

### 按问题选择权威材料

这里不建立一条适用于所有问题的全局权威顺序。不同材料回答的是不同问题：

| 要回答的问题 | 首要依据 | 其他材料的作用 |
| --- | --- | --- |
| 本轮评审怎样进行 | 用户在本轮明确批准的流程、术语和来源规则 | 仓库模板用于落实，不反向扩大批准范围 |
| 系统应当怎样工作 | 仓库现行 Accepted ADR 与明确标为规范性的 Contract | 新候选必须经 RFC/ADR 才能替代 |
| 基线代码实际怎样工作 | `main@74c28b1` 的代码、build graph 和生成物 | 文档只能解释意图，不能覆盖代码事实 |
| 某个门禁是否已经通过 | 绑定 commit、环境、设备、阈值和产物身份的实际 Evidence | 测试、workflow 和 runbook 只证明机制存在 |
| 历史上讨论过什么 | Notion、历史对话和 Research Input | 只能发现 claim、方案与缺口，不能自行成为现行规范 |
| 竞品或市场有哪些能力 | 可复核的一手产品证据 | 竞品矩阵和模型归纳只是 Evidence lead / Requirement Candidate |

实现偏离规范不会让规范自动失效；它可能是实现缺口，也可能说明旧决定需要被新 ADR 替代。
同一问题的同类材料互相矛盾时登记为 `Unresolved`，不能借另一种文档状态把冲突抹平。

## 二、来源与子定位符

步骤 1 冻结的是 **53 个顶层来源记录**。步骤 2 给父页面内部条目建立子定位符，但这些定位符
不参加 53 的计数。只有出现新的、独立的 artifact 或页面时，才回到来源目录增量登记。

### Decision Ledger 定位符

受限 Ledger 可见正文已经完整读到以下原生条目：

- `SRC-NOTION-LEDGER-DL-001`～`SRC-NOTION-LEDGER-DL-021`：21 条来源自称的当前决定；
- `SRC-NOTION-LEDGER-OD-01`～`SRC-NOTION-LEDGER-OD-15`：15 个开放问题；
- `SRC-NOTION-LEDGER-CONFLICT-PRODUCT-SHELL`、
  `SRC-NOTION-LEDGER-CONFLICT-PERSISTENCE-OWNERSHIP`、
  `SRC-NOTION-LEDGER-CONFLICT-FASTINK-BOUNDARY`：3 个来源冲突段落；
- 13 条 Superseded 行使用父来源、所在 section 和规范化旧方案标题定位，不编造来源原生序号。

这些页面没有不可变 revision/hash。定位符只能准确引用 2026-08-21 采集时的可见内容，不能
证明页面此前或以后版本相同。

### Notion ADR/RFC 定位符

Notion ADR 索引列出 ADR-001～013，来源内部均称为 Accepted baseline；RFC 索引列出
Persistence、Sync 和 Arc ABI 三份 Draft。它们使用页面身份去重后的 child locator。特别是：

- `SRC-NOTION-ADR-013-TS-DATA-BRIDGE` 沿用步骤 1 已登记 ID；它不是仓库 ADR-0013；
- Persistence/Sync RFC 保持 `Draft`；其中未拍板的数据库、codec、server revision、ACK、
  conflict 和 compaction 不能被 Ledger 的方向性表述提前冻结；
- Arc ABI RFC 自述“语义/Backend Contract Accepted、C ABI Design In Progress”，因此必须
  分开记录语义状态与 Compiler ABI 状态。

本轮已读取 ADR-001～013 和三份 RFC 的可见正文。为避免与仓库 ADR 混淆，统一使用以下
定位规则：

| 范围 | Child locator | 数量 / 读取边界 |
| --- | --- | --- |
| Notion ADR-001～012 | `SRC-NOTION-ADR-V03:ADR-001`～`ADR-012` | 12；可见正文已读，不含 revision、评论和外链 |
| Notion ADR-013 | `SRC-NOTION-ADR-013-TS-DATA-BRIDGE` | 1；沿用已登记页面 ID，可见正文已读 |
| Persistence RFC | `SRC-NOTION-RFC-V03:PERSISTENCE-RFC-V03` | 1；Draft 可见正文已读 |
| Sync RFC | `SRC-NOTION-RFC-V03:SYNC-RFC-V03` | 1；Draft 可见正文已读 |
| Arc ABI RFC | `SRC-NOTION-RFC-V03:ARC-ABI-RFC-V01` | 1；可见正文已读，页面内部状态分层 |

## 三、仓库现行基线

### 1. 规范、实现、测试和证据不是同一张清单

| Artifact | 声明状态 | 观察到的实际范围 | 不能据此声称 | 建议 |
| --- | --- | --- | --- | --- |
| `docs/adr/0001-*`～`0024-*` | 24/24 Accepted | 决策方向已接受；实现和证据成熟度各异 | 24 个主题均已实现、均通过产品门禁 | Keep；逐 ADR 补追溯和成熟度 |
| `docs/PROJECT_FRAMEWORK.md` | Architecture Baseline v1.4 | 目标模块、路线图与阶段门禁的综合说明 | 列出的 Runtime 模块都已存在 | Clarify；拆开目标与当前事实 |
| `docs/architecture/SYSTEM_ARCHITECTURE.md` | Accepted Baseline | 目标所有权、生命周期和数据流不变量 | 当前代码已经完整实现目标图 | Keep + claim-by-claim audit |
| Runtime C API Contract、header、manifest | Accepted | 54 functions、49 structs、163 enum constants 的产品接口设计输入 | 已有可链接产品 C ABI 或已发布 SDK | Clarify；标为未实现/未发布并在 R1 重审 |
| `runtime/foundation`、`runtime/scene` | RF-01 foundation | Foundation 值类型、Scene facade/binding、damage、uniform-grid 等 host baseline | 已有产品 Document、Operations、EditorSession 或完整 renderer | Keep；仍是实施基础候选 |
| POC-01～06 代码 | Experimental | 各 POC 的最小闭环、harness 和平台 adapter | POC ABI/schema/type 是稳定产品接口 | Keep/Archive 为实验和证据 |
| 9 个 workflow、测试和 runbook | Verification mechanism | 描述 build/test/gate 怎样运行 | 对应 gate 已通过 | Keep；必须另引运行 Evidence |
| `docs/quality/evidence`、`docs/evidence`、POC-01 reports | Mixed evidence | 报告对 commit、环境、hash 的绑定完整度不同 | 一个局部报告接受整个阶段 | 建立 canonical/supplemental/superseded lineage |

### 2. 当前产品实现边界

仓库已经有较完整的 POC 实现，却还没有 R1 产品 Runtime 的完整纵切面。`runtime/` 当前主要是
Foundation 和 RF-01 Scene 基础；产品级 `Document`、`Operations`、`RuntimeFacade`、
`EditorSession` 与公共 C ABI implementation 尚不存在。对应功能可以在 POC 中找到，但这些
类型和 `canvas_poc_*` ABI 明确是 Experimental。

各 POC 还各自维护不同的 `Document`/Operation 类型，不能把多条可行性证明相加成“统一产品
Document Model 已经存在”。POC-01 的 `Document` 仍公开 `mutable_state()`；其受控 replay 和
事务原子性证明了 Operation-driven 方案可行，却没有从类型层消除直接修改旁路。R1/R2 必须
收敛唯一写入口并增加 boundary test，也不能把 POC-01 NDJSON、POC-04 Snapshot JSON 或各自的
operation log 当成正式协议。

公共 C ABI 的现有检查只证明 header 可被 C/C++ include、ABI version 与 manifest 的函数/
struct/enum 清单没有静态漂移；仓库中没有 `canvas_runtime_create` 等产品符号实现。因此 handle、
buffer、callback、错误和 exception 边界尚未获得链接或行为验证。

预编译 Skia SDK 的 producer/consumer、lock、下载验证和供应链工具已经实现。普通 consumer CI
不应再源码构建 Skia；producer workflow 中显式 checkout 和 GN/Ninja 是有意例外。该成果支持
R1 构建基础，不支持“产品 Runtime 已实现”的 claim。

### 3. POC/RF 成熟度

| 工作包 | 决策/阶段状态 | 实现与测试机制 | 实际 Evidence | 审计结论 |
| --- | --- | --- | --- | --- |
| POC-01 Shared Engine | Accepted | 六端共享 Runtime、replay、digest、render、lifecycle harness | 聚合最终审计已闭合门禁；历史失败/局部报告保留 | Accepted POC evidence；ABI、replay schema、Scene 仍 Experimental |
| POC-02 Ink | Integration Ready / Validating | 自动化契约、Stroke/Preview/Canonical 路径存在 | Windows/Web/Android Human Ink Gate 仍 Pending | 可供实验集成，不是 Accepted，不是产品 Ink |
| POC-03 100K Scene | Validating | correctness/equivalence、FrameGraph/Tile L1/Integrated Ink 原型 | Windows Integrated D3D12 p95/p99 连续两次失败 | 正确性结果保留；性能门禁阻塞接受 |
| POC-04 RichText/IME | Accepted | 六端 corpus、IME、layout 和 SDK tracks | 聚合 CI run 在目标 commit 全部成功，条件已满足 | Accepted；Contract 顶部的 `Implementing / SDK publication pending` 是过期文字 |
| POC-05 Hybrid Surface | Accepted scoped risk proof | Controlled Overlay 与多 Shell harness | 跨 Web/RNW/Android RN/Apple RN/Fabric 聚合报告 | 只接受 future-capability 边界；不进入 V1 schema，不是产品 bridge |
| POC-06 / Arc | Validating | protocol、core、handoff、fallback、headless/platform conformance targets | 物理 Input/Preview latency、Human Ink 与真实 presentation 尚未闭合 | 架构/协议实验已开始；不宣称平台 FastInk 已完成 |
| RF-01 | Implementation Baseline / RF01-5 Validating | Scene participant、原子性和 host tests | SkSG SDK、真实 geometry、platform present、production perf 仍 Pending | 不能把 RF 工作计划当 POC-03 失败已解决 |
| R1 | 尚未进入接受 | Contract、计划、Scene foundation 与供应链输入存在 | 无 R1 产品纵切面验收 | 可继续工程准备；还不能标 Implemented/Accepted |

`docs/PROJECT_FRAMEWORK.md` 和 `docs/quality/VERIFICATION_STRATEGY.md` 均重复列出两个 POC-06
行（`FastInk` 与 `FastInk / Arc`）。这属于版本叠加后的语义重复，后续应合并成一份现行门禁，
但本轮不直接修改基线。

## 四、24 份 Accepted ADR 对账

所有 ADR 都有 Context、Decision、Consequences、Validation，但普遍缺少本工作流要求的
Decision owner/reviewers、Related requirements/RFC/evidence 和直接证据链接。下表中的
`Accepted` 一律只解释为 decision acceptance。

| ADR | 主题 | 实现 / Evidence 观察 | 建议 |
| --- | --- | --- | --- |
| 0001 | Visual Document Runtime | POC 覆盖若干子系统，产品模块尚未齐备 | Keep；补需求、owner 和证据范围 |
| 0002 | 可替换 Shell | Web/Android 主线和多种 POC Shell 已证；Windows Tauri 与后续 RN 候选冲突 | Clarify 与 0015/0023 的产品/实验范围 |
| 0003 | Document / RuntimeScene 分离 | POC-03 与 RF-01 有实验实现；POC-03 仍 Validating | Keep；澄清与 0021 物理类型映射 |
| 0004 | Ink 双路径 | POC-02 路径存在，旧 FastInk API 与 0024 handoff 演进并存 | Clarify 0004→0011→0024 lineage |
| 0005 | Ganesh v1 | 锁定 SDK 和多平台 POC 支持方向 | Keep；Graphite 仍为未来 backend |
| 0006 | RichText 一级模型 | POC-04 已接受；复杂并发仍后置 | Keep；补聚合证据和 V1 范围 |
| 0007 | Cache 接口前置 | POC-03 只有接口与 L1 原型 | Keep；L2/L3 和生产 L1 仍 Pending |
| 0008 | POC 单线程 | POC harness 遵循；产品线程拓扑未定 | Keep；明确只约束 POC baseline |
| 0009 | 预编译 Skia SDK | producer/consumer 工具和 lock 已实现 | Clarify 文中 POC 状态主语及发布证据 |
| 0010 | Renderer/Surface ownership | adapter 支持边界；产品 device-loss/present 证据分散 | Keep + evidence maturity |
| 0011 | Shared Preview Model | POC-02/Arc 有实现，物理 gate 未闭合 | Clarify 与 0004/0024 的 API 演进 |
| 0012 | 坐标、DPI、变换 | 跨 POC contract/fixture 存在，证据分散 | Keep；补矩阵和平台 evidence lineage |
| 0013 | Resource identity | 完整 ResourceManifest 仍属 R2 | Keep；标 Not implemented/Pending |
| 0014 | 补偿 Operation Undo | 产品 History 尚未实现 | Keep；标 plan-only，不称已验证 |
| 0015 | 平台分级与 Shell | 六端 portability 与 POC-05 有证据 | Keep；Shell 当前产品选择需单独对账 |
| 0016 | 数值确定性 | POC-01 跨端 digest 是较强证据 | Keep；补 final audit，迁移仍待 R2 |
| 0017 | 帧调度 | 有契约测试，真实 visible/present 证据有限 | Keep + Clarify 产品调度状态 |
| 0018 | 输入背压 | queue/coalescing tests 存在，Human Ink 未闭合 | Keep + Clarify partial/Validating |
| 0019 | ChangeSet / invalidation | RF-01 host baseline 存在，生产性能未证 | Keep + Clarify evidence level |
| 0020 | Snapshot + continuation | 只有 POC replay；正式 codec/log/compaction 未实现 | Keep + Clarify 语义与物理格式 |
| 0021 | Scene/spatial/tile | 多个边界决定混合；正文保留 POC-03 性能失败 | Split + Clarify，不能覆盖失败状态 |
| 0022 | Public C ABI + C++ style | Contract/header 有，产品 C ABI 无 implementation | Split + Clarify，Accepted direction ≠ shipped ABI |
| 0023 | Controlled Overlay | POC-05 scoped evidence 已接受 | Keep；不扩大到 V1/zero-copy/任意穿插 |
| 0024 | Arc boundary | protocol/core/harness 有，物理平台 gate Pending | Clarify 或拆 boundary/implementation acceptance |

当前没有足够依据建议立即 Supersede 某份仓库 ADR。最先需要补的关系是：

- ADR-0004/0011 中的旧 `begin/push/end/cancel` 与 ADR-0024 的
  `sealInput → canonicalCommitted → canonicalVisible → retire`；
- ADR-0003 的概念性 `RuntimeScene` 与 ADR-0021/RF-01 的 Scene facade、compiled data 和
  RenderScene participant；
- ADR-0002/0015 的产品 Shell 与 ADR-0023 为风险证明使用的 RNW/Apple RN/Fabric Shell；
- ADR-0020 的恢复语义与仍未决定的 Snapshot codec、Operation Log、compaction 和 Sync。

## 五、Notion Ledger 与仓库对账

Ledger 的 21 条 claim 不能作为四个整包一起接受。下表保留来源内部状态，并逐条说明它与仓库
现行决定的关系；`Aligned` 也只表示方向相容，不会把 Ledger 升级为仓库规范。

| Ledger | 脱敏 claim | 仓库对账 | 审计分类 |
| --- | --- | --- | --- |
| DL-001 | Axiom 是 C++ Visual Document Runtime，不是 Product Data Platform | Visual Runtime 定位与 ADR-0001 相容；Persistence 是否仍由 Runtime 拥有另有冲突 | Partially aligned |
| DL-002 | Semantic Document 是 truth，Operation/Transaction 是写入口 | 与 ADR-0003/0014/0020 方向相容；统一 Transaction 模型尚未实现 | Aligned direction |
| DL-003 | Document 与可重建 RuntimeScene 分离 | 与 ADR-0003、ADR-0021 相容；物理类型仍待映射 | Aligned |
| DL-004 | Skia 只是 Renderer backend | 与 ADR-0001/0005/0023 相容 | Aligned |
| DL-005 | Pointer/Render hot path 不经过 Storage/Sync/React state | 与 ADR-0018/0022/0023 的 hot-path 边界相容 | Aligned boundary |
| DL-006 | Preview/Canonical 双路径，prediction 不进 Document | 与 ADR-0004/0011/0024 相容 | Aligned；协议 lineage 待澄清 |
| DL-007 | FastInk 正式命名为 Arc | 仓库 ADR-0024 已采用 Arc，旧名仍广泛存在 | Aligned naming direction；迁移未完成 |
| DL-008 | Arc 负责 native input/transient presentation，Axiom 负责语义/renderer | 与 ADR-0024 部分相容；Input required、Preview optional、Web 无 Arc 是新增能力分级 | Partially aligned / candidate refinement |
| DL-009 | Arc/Axiom 不共享 presentable backbuffer，Host 负责 composition | 与 ADR-0010/0024 相容；`Platform Host` 角色已在步骤 0 接受 | Aligned boundary |
| DL-010 | Storage/Sync 移出 Axiom，归 Shared Data Runtime | 与 ADR-0001 和 SYSTEM_ARCHITECTURE 的 Runtime 模块归属冲突 | Unresolved ownership conflict |
| DL-011 | Shared Data Runtime 采用 TypeScript-first | 步骤 0 已明确只使用中性名称，语言和 Bridge 留后续 RFC | Open candidate，不是现行决定 |
| DL-012 | Local Ready 优先 Cloud Ready，Durable 与 Synced 分离 | 仓库没有同等精度的 Accepted 产品决定 | New requirement/architecture candidate |
| DL-013 | 移动端默认 foreground sync，采用 suspend/resume/catch-up | 仓库没有对应 Accepted 生命周期决定 | New product/runtime candidate |
| DL-014 | Snapshot + OpLog continuation + BlobRef | Snapshot continuation 与 ADR-0020 部分相容；OpLog/codec/compaction 尚未冻结 | Partially aligned；physical model open |
| DL-015 | 大 Blob 不进入 Transaction，只携带 BlobRef | 与 ADR-0013 的资源身份方向相容，但 Transaction schema 未定 | Candidate constraint |
| DL-016 | Tile 是 derived cache，不拥有 Document object | 与 ADR-0007/0021 相容 | Aligned boundary |
| DL-017 | SpatialIndex 属于 Runtime，但算法不冻结为 R-tree | 与 ADR-0021 相容 | Aligned boundary |
| DL-018 | Document Layer、Runtime RenderGroup、Frame Overlay 分层 | 与 ADR-0021/0023 部分相容；`RenderGroup` 物理模型未冻结 | Partially aligned |
| DL-019 | 复杂编辑对象使用 Hybrid/ExternalSurface 边界 | 与 ADR-0023 的未来能力边界相容；不改变 ExternalSurface 不进入 V1 | Aligned only for scoped future boundary |
| DL-020 | Web React，所有 Native 产品 Shell 使用 RN，Windows Overlay 例外 | 与 ADR-0002/0015 的 Windows Tauri、Apple Tier B 直接冲突 | Unresolved normative conflict |
| DL-021 | 独立 DataBridge 使用 opaque handle、batch transaction/snapshot bytes、control/data plane | 与 Runtime C ABI 有相邻但不同的层级；步骤 0 已将 Bridge 留给 RFC | New interface candidate，不是现行 Contract |

值得特别注意的不是标题状态，而是内容内部也有成熟度层次：Notion ADR-010 的正文状态是
`Accepted Direction`；Arc ABI RFC 的 Semantic Contract 与 C ABI Design 状态不同；Persistence
和 Sync RFC 仍明确保留 codec、store、server ordering、ACK、conflict、compaction 等开放项。

### Notion 来源内部不一致

以下差异不能靠页面标题自动消解：

| ID | 来源内两种说法 | 当前处理 |
| --- | --- | --- |
| NINT-STATUS-ADR010 | ADR 索引称 ADR-001～013 都是 Accepted baseline；ADR-010 正文写 `Accepted Direction` | 保留较细粒度正文状态，不据此升级为仓库 Accepted |
| NINT-BRIDGE-FREEZE | ADR-013 称 handle、opaque bytes、batch、no-echo、async delivery 等已冻结；Sync RFC 的 Bridge 段仍称 handle/encoding/ownership 未冻结 | 标记 stale/lineage conflict；没有 revision 证据时不推断先后 |
| NINT-ARC-RFC | RFC 索引和页面标题称 Arc ABI RFC 为 Draft；正文称 Semantic/Backend Contract Accepted、C ABI Design In Progress | 拆成语义、backend、compiler ABI 三个状态，不使用单一 `Draft/Accepted` 覆盖 |

这些都是受限来源内部的审计结果，不改变仓库现行规范。若后续方案确实采用其中内容，需要先把
不一致的问题放入对应 RFC，并由新的仓库 ADR/Contract 明确状态。

### Open Decisions 映射

Ledger OD-01～15 可以移交到步骤 5 的 Decision Backlog，初步归组如下：

| 主题 | Open Decisions |
| --- | --- |
| Scene / Performance | OD-01 SpatialIndex、OD-02 Stroke chunk、OD-03 Tile/cache |
| Runtime execution | OD-04 线程/queue/backpressure |
| Arc | OD-05 ABI、OD-06 GPU sharing/sync |
| Data/Persistence/Sync | OD-07 名称、OD-08 Transaction codec、OD-09 schema/AXTP、OD-10 Snapshot/store/compaction、OD-11 catch-up/server authority、OD-12 conflict/RichText |
| Object/Surface/Platform | OD-13 complex object/semantic graph、OD-14 ExternalSurface lifecycle、OD-15 platform implementations |

这些条目只是 backlog 输入。OD-13 中的 Semantic Graph 和 Context API 不因进入 Ledger 就进入
现行 V1；步骤 0 已明确它们当前不属于现行 V1 范围。

## 六、主题对账矩阵

| 主题 | 仓库现行说法 | 外部候选说法 | 实现 / Evidence | 当前判断 |
| --- | --- | --- | --- | --- |
| 项目身份 | Canvas v2 Visual Document Runtime | Axiom 是 Runtime 规范/产品名 | 代码路径仍混用 Canvas | 名称未统一；技术定位大体相容 |
| Platform Host | 可替换 Shell + platform adapters | Host 是 composition root；Host Runtime 可能成物理模块 | 各 POC 自有 host/adapters | 角色已在术语表接受，物理模块未决定 |
| Shared Data Runtime | Runtime/ports/Persistence/Collaboration 表述混合 | 独立 TS-first Data Runtime 拥有 Session/Storage/Sync | 产品实现不存在 | 所有权冲突，且语言/Bridge 尚不可定案 |
| Public interface | 产品稳定 C ABI | Runtime API、DataBridge、JSI/WASM/C ABI、单一 SDK 等多层候选 | 只有 contract/header 和 POC ABI | 必须拆 Runtime C ABI 与产品发布 SDK |
| Document/Operation | 唯一写路径、补偿 Undo、Snapshot continuation | 同向，并增加 Transaction/DataBridge | POC replay/局部 schema；产品 model 未实现 | 语义方向可保留；schema/codec/ownership 未定 |
| RuntimeScene | 概念聚合 + RF-01 participants | Scene facade、derived runtime state、render DAG | RF-01 host foundation | 概念和物理类型映射待澄清 |
| Rendering/Tile | Ganesh、FrameGraph、Runtime-owned spatial/tile | RenderCompiler/Graph、RenderGroup、Tile derived cache | POC-03 失败，RF-01 Validating | 边界与算法/性能成熟度必须分开 |
| Ink/Arc | InkEngine + Preview/Canonical；Arc 全平台模块 | Native input required、preview optional；Web 无 Arc | POC-02/06 均未最终物理验收 | 能力分级和产品 ABI待决 |
| Shell policy | Web React，Windows Tauri，Android RN；Apple Tier B | Web React，Native 全 RN，Windows overlay 特例 | RNW/Apple 只在 POC-05 risk proof | 直接冲突，不能由 POC 可行性替换产品决定 |
| Persistence/Sync | Snapshot 语义已定，物理格式/协作后置；旧图仍列模块 | Runtime 只产语义，TS Data Runtime 持久化/同步 | 无产品实现 | ownership 需重审；具体协议继续开放 |

### 历史对话 claim 去重边界

六份局部历史对话按主题只建立 claim locator，不把每个助手段落变成新来源。当前先登记以下
claim 组，后续若引用到具体 Requirement/RFC 再细化 locator：

| Claim locator | 父来源 | 脱敏主题 | 与其他材料的关系 | 使用限制 |
| --- | --- | --- | --- | --- |
| CLM-CHAT-01-ARC-BOUNDARY | SRC-CHAT-01 | Axiom/Arc/Host、Input/Preview、surface 与 latency | 与 Ledger DL-007～009、ADR-0024、Arc RFC 高度重合 | 只能证明讨论过方案，不能证明批准或物理延迟 |
| CLM-CHAT-02-LOCAL-DATA | SRC-CHAT-02 | Local-first、durability、Data Runtime、Snapshot/Transaction/BlobRef | 与 DL-010～015、Notion ADR-009～013、Persistence/Sync RFC 重合 | TS/C++、store、sync 细节仍未批准 |
| CLM-CHAT-03-SPATIAL-RUNTIME | SRC-CHAT-03 | Scene、SpatialIndex、Damage、Tile/LOD、Hydration/Working Set | 与 ADR-0021、RF-01 和 Notion rendering pages 重合 | Working Set/Hydration 不进入现行 V1；算法仍开放 |
| CLM-CHAT-04-ARCH-REVIEW | SRC-CHAT-04 | 对旧基线的多轮问题审查 | 是问题发现和历史背景，不是独立技术证据 | 模型状态标签不能证明用户逐项批准 |
| CLM-CHAT-05-CROSS-PLATFORM | SRC-CHAT-05 | 多端、Persistence/Sync、Ink/Erase、Arc 与 Host | 与多组 ADR/Ledger 重合；含未经核验的产品观察 | 产品行为需一手证据，不能直接定 owner/priority |
| CLM-CHAT-06-VIBE-DIRECTION | SRC-CHAT-06 | Vibe 价值、AI、Semantic Graph、Overlay 与候选 POC | 与需求输入/竞品矩阵部分重合 | 主要流向步骤 3；Semantic Graph 不进入现行 V1 |

`SRC-CHAT-07` 只作为同类产品需求调研入口进入步骤 3，不在步骤 2 把 178 个 source rows 转成
178 条架构 claim。它与 Notion 产品功能基线、竞品矩阵没有直接生成或继承关系；内容重合时
仍按一个产品能力 claim 对待，不能把三个来源简单累加成三份独立证据。

## 七、冲突与缺口登记表

这里同时登记规范冲突、待决边界、实现/证据缺口和来源血缘问题，不能把它们都解释成“两个
架构决定互相冲突”。`Blocked downstream` 表示相关下游事项不能获得接受，不表示整个架构重审
停止。步骤 0 已澄清 Axiom、Arc 和 Platform Host 的概念用法，因此名称项只剩公开迁移问题。

| ID | Topic / 两侧来源 | 类别 / 状态 | 缺失证据 | 下一步 / Blocked downstream |
| --- | --- | --- | --- | --- |
| C-IDENTITY | 仓库 Canvas v2、现行 Axiom 名称、FastInk/Arc 历史名 | Naming migration / Partially clarified | 公开产品命名、兼容别名和迁移约束 | 建身份 Problem/RFC 后由新 ADR 决定；阻塞公开 rename、SDK branding，不阻塞继续使用步骤 0 术语 |
| C-SHELL | ADR-0002/0015 vs Ledger DL-020/Notion ADR-002 | Normative conflict / Unresolved | 产品平台范围、发布承诺、RNW 维护/包体/overlay benchmark | 建 Shell Problem/RFC 并跑产品级 spike；阻塞产品 Shell 和发布矩阵接受 |
| C-DATA-OWNERSHIP | ADR-0001/SYSTEM_ARCHITECTURE vs DL-010～015/Notion ADR-009～013 | Ownership conflict / Unresolved | lifecycle/offline/crash、安全边界、TS/native bridge benchmark | 建 Data boundary Problem/RFC；阻塞 R1 module DAG、Persistence/Sync Contract |
| C-PUBLIC-BOUNDARY | ADR-0022 Runtime C ABI vs DataBridge/JSI/WASM/SDK 候选 | Interface layering decision / Open | consumer 清单、兼容窗口、binding/发布/性能需求 | 分开建立 Runtime ABI 与 Product SDK RFC；阻塞 Stable ABI/SDK 发布 |
| C-SCENE-MAPPING | ADR-0003 vs ADR-0021/RF-01/Notion Scene pages | Concept-to-physical mapping / Clarification required | RF-01 migration、SkSG、真实 geometry 与 benchmark evidence | 建 Scene Contract 与类型映射；阻塞生产 rendering module DAG |
| C-ARC-EVOLUTION | ADR-0004/0011 vs ADR-0024/Notion ADR-006/007/Arc RFC | Protocol lineage + validation gap / Open | 真实 backend、visible ack、latency/Human Ink 和 fault corpus | 建 compatibility map、完成 Arc RFC/POC-06；阻塞产品 Arc ABI 与 R3 FastInk |
| C-SNAPSHOT-PERSISTENCE | ADR-0020 vs TS Data Runtime Persistence RFC | Semantic/physical ownership decision / Open | recovery/crash/migration/large-document corpus | 完成 Persistence Problem/RFC 和实验；阻塞 stable codec/store/compaction |
| C-OPERATION-WRITE-PATH | ADR-0014/0020 的唯一写路径 vs 多套 POC model 与 POC-01 `mutable_state()` | Implementation gap / Open | 统一 Document API、旁路 lint/test、Undo/replay corpus | 建 R1/R2 model contract 并移除/隔离旁路；阻塞统一产品 Document |
| C-EVIDENCE-MATURITY | 24 Accepted ADR vs POC/RF 状态 | Traceability gap / Open | 每个 claim 的 test commit、run、artifact 与阈值 | 建 decision→implementation→evidence 矩阵；阻塞阶段验收，不阻塞决定保留 |
| C-POC03-PERF | POC-03 correctness vs Windows performance failure | Actual failed gate / Failed and open | 分段 profile、新 renderer 结果和同设备 revalidation | 继续 RF-01～03 后按原门禁重测；阻塞 POC-03/R3 rendering acceptance |
| C-EVIDENCE-LINEAGE | POC-05 双 evidence 目录、POC-01 历史 subset、POC-04 stale contract | Provenance/editorial gap / Open | canonical report、tested commit/asset 和 supersession map | 建 Evidence index 并修正文档状态；阻塞可审计引用和阶段汇总 |
| C-NOTION-INTERNAL | Notion ADR/RFC 索引与正文中的成熟度、Bridge freeze 状态 | Source-internal conflict / Unresolved | 不可变 revision、明确 supersedes 或用户重新确认 | 在对应 RFC 中重新定界；阻塞把 Notion 状态直接迁入仓库 |
| C-LINEAGE | Notion 导航/标题/历史总结 | Source lineage / Unknown | 不可变 revision、明确 derived relation 或独立一手证据 | 保持 Unknown 并按 claim 去重；阻塞把重复材料计为独立佐证 |

## 八、证据缺口与局部阻塞

本节使用 `Audit-B0/B1/B2` 表示审计处理级别；它不等于步骤 3 的 Requirement priority、竞品
矩阵的 source-priority，也不等于步骤 7 的 issue-severity。

### Audit-B0：在相关下游事项接受前必须处理

- 产品公共 C ABI 只有 Contract/header/manifest，没有 R1 implementation 与发布兼容证据；
- 多个 POC 的 Document/Operation 模型尚未收敛，POC-01 仍有直接 mutable escape hatch；
- POC-03 的 Windows Integrated D3D12 性能门禁仍失败；
- Arc 平台 target 多数证明 protocol/capability conformance，而非真实 HWND/JNI/Metal/DOM
  input-to-present 和物理延迟；
- Product Shell 与 Data Runtime ownership 直接影响依赖方向，不能仅凭 Notion 状态裁决。

### Audit-B1：不阻塞盘点，但会阻塞特定下游接受

- POC-05 两个 evidence 目录和历史报告缺统一 canonical lineage；
- Snapshot/Operation recovery 已有语义 ADR，却没有正式 codec/log/compaction/migration 实现；
- 目标架构列出的 RichText、Ink、Persistence、Collaboration 等模块与当前产品 `runtime/` 代码
  覆盖面不同；
- 受限 Notion 没有 revision/hash，历史对话缺早期回合，不能用来证明过去批准。

### Audit-B2：文档新鲜度和编辑性问题

- POC-04 Contract 仍保留验收前状态文字，实际阻塞 CI 已成功；
- POC-06 在框架和质量表中重复两行；
- 根 README 仍写着 `This branch implements POC-03`，但基线已包含 POC-04/05/06 与 RF-01；
  这类分支时期或阶段时期措辞应在明确 canonical status 后统一整理。

## 九、旧文档处理建议

本节的建议是迁移 disposition，不是当前执行的文件操作。步骤 2 完成前不批量 `git mv`、不
删除历史报告、不原位改写 Accepted Decision。任何会改变语义的修改都必须在后续 RFC/ADR 中
建立 `Supersedes`/`Superseded by` 双向关系。

| 文档组 | 建议 | 原因和继任动作 |
| --- | --- | --- |
| ADR-0001～0020、0023 | Keep 为主，局部 Clarify | 决策方向仍是现行规范；补需求、owner、evidence 和成熟度，不静默改 Decision |
| ADR-0021 | Split + Clarify | Scene facade、SkSG、SpatialIndex、Damage、Tile/LOD 决策和实验成熟度混合 |
| ADR-0022 | Split + Clarify | Public C ABI 与 C++ style 是不同层级；Contract 尚未实现/发布 |
| ADR-0024 | Clarify，必要时 Split | 模块边界可 Accepted，Compiler ABI 与物理平台验收仍 Pending |
| PROJECT_FRAMEWORK / SYSTEM_ARCHITECTURE | Keep + Clarify | 保留现行基线，但逐项区分目标、当前事实和候选模块 owner |
| Runtime C API Contract/header | Keep as accepted design input | R1 前重新审查可实现性和 SDK 层级；不可标 shipped/stable product |
| POC-01/04/05 聚合 Evidence | Keep canonical | 建立明确的测试 commit、asset 和 supersedes 关系 |
| POC-01/05 历史局部报告 | Archive/Supplemental | 保留历史失败和 scoped 结论，不重复计权 |
| POC-02/03/06 与 RF-01 | Keep Validating | 失败和 Pending 是有效证据，不用计划文档改写状态 |
| Research / Notion / chats | Keep as input only | 不能成为第二套规范基线；后续按 claim 引用 |

### 迁移动作的边界

- `Keep`：保留原路径和历史内容，只允许补充不改变语义的来源、状态和证据链接；
- `Clarify`：优先增加成熟度、范围、owner、`Related evidence` 或历史关系，不把澄清文字写进
  原 Decision；
- `Split`：新建 RFC/Contract/Implementation/Validation 文档，旧 ADR 保留为决定入口，并添加
  继任链接；
- `Archive`：只在新 canonical 报告或继任文档完成后建立链接桩，历史证据仍可复查；
- `Supersede`：本轮不执行。只有用户明确确认新 ADR 后，才能保留旧文件并建立替代关系。

Evidence 的 canonical/supplemental/superseded 标记也不等于删除：历史失败、旧设备报告和
局部实验仍保留，引用时必须带 scope、commit、环境和产物身份。

## 十、移交 Decision Backlog

步骤 5 至少应建立以下决策主题；依赖顺序在该步骤另画 DAG：

1. Axiom/Canvas/Arc 的正式身份与迁移命名；
2. Product Shell、Platform Host、Binding 和 Public SDK 分层；
3. Runtime C ABI 与产品最终公开 SDK 的关系；
4. Document/Transaction/Snapshot 与 Shared Data Runtime 的所有权；
5. Shared Data Runtime 是否 TS-first，以及 DataBridge contract；
6. RuntimeScene 概念到 RF-01 物理类型的映射；
7. Scene/Spatial/Tile/Cache 的边界 ADR 与算法 RFC 拆分；
8. Arc Input/Preview 能力分级、handoff 和 Compiler ABI；
9. Product 线程拓扑、queue/backpressure 和 frame scheduler；
10. Persistence、recovery、sync、conflict 与 schema/codec 的分层决定；
11. 安全、信任边界、兼容迁移、可观测性和平台资源预算。

### Backlog 的状态和依赖规则

这些主题只是从当前审计移交的候选项，初始状态为 `Backlog`，不是已接受决定。步骤 5 将为每项
补齐 owner、问题说明、前置证据、方案文档等级、验证计划和退出条件。当前已能确定的依赖只有：

```text
身份/范围
  → Shell、Host、Binding 与 SDK 分层
  → Document/Data ownership 与 Public boundary
  → Scene、Arc、Persistence/Sync 的主题 RFC
  → 跨主题线程、资源、安全与发布审查
```

这张关系图不是最终拓扑；如果实验显示依赖方向错误，步骤 5 必须记录调整原因。没有 owner、
前置条件和可证伪验证的条目不能进入 `Decision Required`，更不能直接生成 Accepted ADR。

## 十一、待审核组与退出条件

为避免一次确认掩盖具体分歧，步骤 2 建议按以下五组与用户审核：

| 组别 | 审核范围 | 状态 |
| --- | --- | --- |
| 第一组 | 审计方法、按问题选择权威材料、53 个冻结来源与 child locator 规则 | Confirmed（2026-08-21） |
| 第二组 | 仓库现状、POC/RF 成熟度和 24 份 ADR 处理建议 | Confirmed（2026-08-21） |
| 第三组 | Notion Ledger/ADR/RFC 与仓库的 claim 映射 | Confirmed（2026-08-21） |
| 第四组 | 冲突、证据缺口、处理优先级和局部阻塞 | Confirmed（2026-08-21） |
| 第五组 | 文档迁移建议与 Decision Backlog 移交 | Confirmed（2026-08-21） |

第一组确认是一条步骤 2 评审事件，只接受本文的审计方法和来源定位规则，不批准第二组之后的
技术判断，也不反向增加步骤 1 已冻结的 53 个架构输入来源。

第二组确认接受仓库实现边界、POC/RF 证据成熟度和 `Keep / Clarify / Split` 处理建议；它不
直接修改现有 ADR，也不把尚未通过的 POC/RF 或未实现的 Public C ABI 升级为完成状态。

第三组确认接受 Notion claim 与仓库之间 `Aligned / Partially aligned / Candidate / Unresolved`
的对账分类，以及 Notion 来源内部状态不一致的记录；它不批准 Notion 中的技术方案，也不改变
仓库现行 ADR、V1 范围或 Contract。

第四组确认接受冲突类别、条目状态、`Audit-B0/B1/B2` 处理级别及局部阻塞范围；它不表示任何
冲突已经解决，也不授权在本审计文档中选择 Shell、Data Runtime、Arc 或公共 SDK 的最终方案。

第五组确认旧文档暂不批量移动或删除，接受 `Keep / Clarify / Split / Archive` 的处理边界，
并确认本轮不执行 `Supersede`。上述 11 个主题只作为 `Backlog` 输入移交步骤 5，不构成新 ADR
或技术方案批准。后续步骤 5 仍需重新建立每项的 owner、前置条件和验证计划，不能把本次移交
当成决定接受。

步骤 2 完成前必须满足：

- [x] 步骤 1 最终校验完成，来源快照冻结；
- [x] 仓库规范、实现、测试机制和实际 Evidence 已分层盘点；
- [x] 24 份 Accepted ADR 已逐项给出初步处理建议；
- [x] Ledger DL、Superseded、Conflict、OD 与 Notion ADR/RFC 已建立 locator 和初步映射；
- [x] 历史对话中影响现行决定的 claim 已完成主题级定位和去重；具体下游引用按需细化；
- [x] 仓库现有规范和 24 份 ADR 的初步处理建议已经用户审核；具体替代决定仍须后续 RFC/ADR；
- [x] 所有冲突与缺口都有类别/状态、missing evidence、next action 和 blocked downstream；
- [x] `tools/check_docs.py`、`git diff --check`、Markdown/Mermaid 与私有 locator 泄漏检查通过；
- [x] 用户明确确认步骤 2 完成（2026-08-21）。

完成步骤 2 只表示当前状态、证据成熟度、冲突和缺口已经完成审计，不表示任何 `Unresolved`
冲突已经被解决，也不表示任何新 ADR、Contract 或产品方案已经接受。
