# Axiom 架构重审术语表

> 状态：Accepted / Review-normative
> 确认日期：2026-08-21
> 更新日期：2026-08-21
> 输入：SRC-USER-GLOSSARY-CONFIRMATION-20260821
> 用途：统一架构重审中的概念；除明确确认的 Axiom 首选称呼外，不改变现有 API、模块名或 Accepted ADR

这份表只解决“同一个词在讨论中是什么意思”。它不会用术语定义偷偷接受候选方案，也不
用一个词的存在暗示代码已经完成。当前仍以[项目总体框架](../../PROJECT_FRAMEWORK.md)、
[系统架构](../SYSTEM_ARCHITECTURE.md)、[现有 ADR](../../adr/README.md)和已批准 Contract
为规范依据；Notion 与历史讨论通过[来源 ID](SOURCE_CATALOG.md)引用。

## 1. 怎样理解本表的规范地位

| 标记 | 含义 |
| --- | --- |
| 现行规范 | 术语表达的是 Accepted ADR 或已批准 Contract 中的边界；不代表实现完成或验证通过。 |
| 现行规范，待澄清 | 核心边界已接受，但名称、物理拆分或与新候选基线的关系需要后续 RFC/ADR 澄清。 |
| 候选方案 | 来自 Notion 或历史讨论，尚未覆盖仓库现行决定。 |
| 实现术语 | 当前代码、POC 或 RF 使用的内部名称，不获得公共兼容承诺。 |
| 历史候选 | 研究中出现过，但既未接受，也没有足够证据进入当前实施基线。 |

术语表不记录 Contract 的 `Draft / Experimental / Stable`、实现进度或 Validation 状态。
例如，Arc 的模块边界可以是现行规范，而 Arc 协议仍是 Experimental，POC-06 仍在验证；
三者并不矛盾。

## 2. 描述状态时使用的词

| 中文首选名 | 英文或代码名 | 当前工作定义 | 规范地位 |
| --- | --- | --- | --- |
| 权威状态 | Authoritative State | 系统允许用来裁定语义真相的状态。Document 是文档语义权威；Scene、Preview、cache 和 GPU 状态不能反向修正文档。 | 现行规范，ADR-0003 |
| 规范结果 | Canonical Result | 按版本化规则从已确认输入产生、可确定重放的稳定语义或渲染结果。`Canonical` 不自动表示已持久化、已同步或已显示。 | 现行规范，ADR-0003/0004/0016 |
| 已提交 | Committed | Document transaction 已通过验证并原子发布，不会暴露半应用状态。它不等于本地已落盘或服务器已确认。 | 现行规范，ADR-0014/0020/0022 |
| 已持久化 | Durable / Persisted | 对应 Operation 或 checkpoint 已经满足所声明的本地 durability contract。精确 WAL/fsync 顺序仍由 Persistence RFC 决定。 | 边界现行；具体顺序待审 |
| 已同步 | Synced / Acknowledged | 外部 Sync 层已确认对应语义数据到达所声明的远端前沿。它不改变 Document digest。 | 边界现行；协议待审 |
| 已显示 | Visible / Presented | 与目标 revision、handoff token、surface generation 匹配的画面已有 presentation evidence；仅完成 draw、GPU submit 或调用 `present()` 不足以自动证明可见。 | 现行规范，ADR-0017/0024 |
| 派生状态 | Derived State | 能从权威状态及版本化输入重建的数据，例如 RuntimeScene、SpatialIndex、Preview、Tile 和 GPU resource。丢失它只能影响性能或短暂显示，不能改变 Document。 | 现行规范，ADR-0003/0007 |

## 3. 项目、模块与公共边界

| 中文首选名 | 英文或代码名 | 当前工作定义 | 规范地位 |
| --- | --- | --- | --- |
| Axiom | Axiom | 当前 Git 仓库名称，也是本轮及后续新架构文档中 Visual Document Runtime 的规范首选名。它不自动表示“唯一公共 SDK”；公共发布边界仍需独立决策。 | 仓库身份与 Runtime 规范名称已确认 |
| Canvas v2 | Canvas v2 | 现有 ADR、规范和历史证据使用的旧 Runtime 名称。在正式命名 ADR 完成替代、兼容称呼和文档迁移前，旧文件中的 `Canvas v2` 继续按原语义解释，不静默改写历史。 | 现行历史名称；待命名 ADR 迁移 |
| 视觉文档运行时 | Visual Document Runtime | 负责 Document、Operations、EditorSession、RichText、Ink、Scene 与 Rendering 等共享语义和执行边界。数据库、文件、网络、认证和 transport 的物理 adapter 已明确位于外部 ports；Persistence/Collaboration 的语义编排与候选 Data Runtime 如何分工仍需对账。 | 核心定位现行；数据层 ownership 待澄清 |
| 产品外壳 | Product Shell | 拥有导航、工具栏、账户、分享、权限和产品工作流，通过窄 Binding/Control Path 使用 Runtime；不拥有 Document、Scene 或 Ink 的语义真相，也不接管不必要的高频数据面。 | 现行规范，ADR-0001/0002/0015 |
| 平台宿主 | Platform Host | 唯一的组合根（Composition Root）角色：创建并连接 Runtime、Arc input/preview、Canonical surface adapter、frame scheduler 和 OS window/view，协调最终合成。 | 角色与 ownership 现行，ADR-0024 |
| 宿主运行时 | Host Runtime | Notion 候选基线中的物理模块或 target 名，可能把多种 Host/Binding 能力内部化。它不等同于已经接受的 Platform Host 角色。 | 候选方案，SRC-NOTION-HOST-V01 |
| 组合根 | Composition Root | 唯一负责创建组件、注入依赖并决定平台 composition ownership 的位置；Axiom Runtime 与 Arc 不得自行互相加载。 | 现行规范，ADR-0024 |
| Arc | Arc | 与 Axiom 同仓、可独立构建和抽取的 input-to-display 模块；通过版本化协议连接平台输入与 Axiom Preview Model，拥有独立 Preview Presentation Target，不拥有 Document、Canonical RenderTarget 或 Canonical 语义。 | 名称和模块边界现行；发布/Web/capability policy 待审 |
| FastInk | FastInk | 低延迟输入到显示的能力主题及 POC-06 名称，不再用作 Arc 物理模块名。系统级 Raw Input/DRM FastInk 是独立条件式预研。 | 术语分工现行，ADR-0024 |
| 共享数据运行时 | Shared Data Runtime | Notion 与历史讨论中的候选 Shell 侧数据编排层，拟拥有 DocumentSession、storage、sync、outbox、blob 和 suspend/resume；不参与 Pointer/Render 热路径。`TypeScript` 是候选实现语言，不是本术语已经接受的属性。 | 候选方案，SRC-CHAT-02 / SRC-NOTION-BASELINE-V03 |
| Runtime 公共 C ABI | Runtime Public C ABI | Axiom Runtime 唯一的跨语言兼容边界；现有规范性符号仍使用 `CanvasRuntime*`/`canvas_*`。它使用 opaque generation handle、版本化 POD、显式 buffer ownership、status/error 和受控 ports，不暴露 C++/STL/Skia/platform object。 | 现行 Accepted Contract 基线，ADR-0022；符号更名待命名 ADR |
| 产品公共 SDK 边界 | Product/Public SDK Boundary | 产品最终分发哪些 package、哪些接口获得源码或二进制兼容承诺。是否只公开 Axiom、是否把 Arc/Host internalize，不由 Runtime C ABI 或“可独立构建”自动决定。 | Open Review |
| 绑定层 | Binding | 把公共 ABI 包装为 WASM/JS、JNI、ObjC++、Rust/Tauri、React Native 或其他语言表面的薄层；不重新实现 Document/Ink/Scene 语义。 | 现行边界；具体 wrapper 待实施 |
| 平台适配器 | Platform Adapter | 实现输入、surface、frame scheduling、IME、clipboard、storage 或 network 等平台能力的边界对象。Adapter 提供能力，不取得 Document 语义 ownership。 | 现行边界 |
| 控制路径 | Control Path | 可经常规 Binding 进入的命令、配置和查询路径，例如 open document、set tool、execute command、undo/redo。它不是高频 sample 或 frame stream。 | 现行规范，ADR-0022 |
| 平台热路径 | Native Hot Path | 延迟敏感的数据路径：平台 Pointer/VSync 经 batch/timing 进入 InputRouter/FrameScheduler，再到 Preview、Scene、Renderer 和 Surface。不得逐 sample 经过 RN/React/QML state、高频 JSON、数据库或网络；Web 可以保留浏览器所需的薄 JS adapter。 | 现行规范，ADR-0022 |

## 4. 文档状态、编辑与操作

| 中文首选名 | 英文或代码名 | 当前工作定义 | 规范地位 |
| --- | --- | --- | --- |
| 语义文档 | Semantic Document / `Document` | 唯一可保存、迁移、恢复和协作的文档语义真相，包含稳定对象、属性、顺序、RichText/Stroke 语义和 ResourceManifest binding；不包含 EditorSession、Presence、Preview、RuntimeScene、GPU/cache、路径或平台句柄。 | 现行规范，ADR-0001/0003/0013 |
| 编辑会话 | `EditorSession` | 每个本地 View 独立持有 Viewport、Selection、Hover、Tool、Snap、Text composition、Active Stroke 与本地 History intention；可以局部恢复，但不成为 Document collaboration state。 | 现行规范；具体多 View 产品策略待审 |
| 文档会话 | `DocumentSession` | 候选 Data Runtime 中协调加载、保存、同步、blob 和前后台生命周期的对象；它不等于 Axiom `Document` 或 `EditorSession`。 | 候选方案 |
| 协作临场状态 | Presence | 在线成员、远端光标、临时选区、follow 和网络质量等可丢失、可过期状态；不进入 DocumentSnapshot、Operation history 或 Undo 语义。 | 现行边界 |
| 意图 / 命令 | Intent / Command | 依赖当前 Tool、Selection 或 composition 的用户意图，经验证和规范化后产生 Operation；它不能直接修改 Document 或 RuntimeScene。 | 现行规范 |
| 操作 | `Operation` | 对 Document 的确定性语义修改，也是编辑、Undo/Redo、Persistence continuation 和 Collaboration 的唯一语义写入口。具体网络/文件编码仍单独版本化。 | 现行规范 |
| 文档事务 | Document Transaction | 验证、应用并原子提交一个或多个相关语义修改的边界。它不要求存在独立 `Transaction` wire schema；Transaction 是否成为 Sync、Replay 或 Undo 的外部单位仍需 RFC。 | 原子边界现行；外部单位待审 |
| 操作批次 | Operation Batch | C ABI 或 transport 一次携带的版本化 Operation 容器；一个 batch 不自动等于一个 Document transaction、一个用户意图或一个协作因果单位。 | 公共 port 现行；schema 待审 |
| 变更集 | `ChangeSet` | 成功 transaction 针对明确 before/after revision 派生的 Scene 更新输入，不是第二份持久化事实。 | 现行规范，ADR-0019 |
| 语义变化 | `SemanticChanges` | ChangeSet 中由 transaction 结果确定的创建、删除、属性、层级、order 和 resource binding 变化；可用于增量正确性校验。 | 现行规范，ADR-0019 |
| 失效提示 | `InvalidationHints` | 可丢弃、扩大和重算的 bounds、layout、spatial、cache 等优化提示；不进入 Document digest、Operation Log 或 Collaboration envelope。 | 现行规范，ADR-0019 |
| 本地历史 | History / Undo Intention | EditorSession 记录的本地 authored intention 与 undo grouping。Undo/Redo 针对当前 Document 生成新的 compensating Operations，不回拨 state pointer 或改写旧 Operation。 | 现行规范，ADR-0014 |
| 文档修订号 | Document Revision | 某个 Runtime 实例内单调的发布和失效标记，用于隔离读取、Scene 和 frame；不等于 Operation sequence、RecoveryFrontier、服务器版本或 surface generation。 | 现行规范 |
| 文档只读视图 | `DocumentReadView` | Canonical executor 在一个明确 revision 上提供的不可变进程内读取视图，供 compile/snapshot 等操作使用；它不是持久化格式。 | 现行规范 |
| 视口快照 | `ViewportSnapshot` | Pointer batch 绑定的 ViewId、viewport revision 和坐标变换快照，保证历史 sample 不被较新的 camera 重新解释；它不是 DocumentSnapshot。 | 现行规范，ADR-0012 |
| 文档快照 | `DocumentSnapshot` | 已提交 transaction 边界上的完整、不可变语义检查点，逻辑上绑定 ResourceManifest、Document identity/schema/capability、revision、RecoveryFrontier 和 digest；不能用于普通编辑或 Undo。 | 现行规范，ADR-0020 |
| 恢复前沿 | `RecoveryFrontier` | 可持久化或同步的不透明、版本化恢复位置；单机可以暂用连续 sequence，但 Core 不假设未来永远是单一全局序号。 | 现行规范，ADR-0020 |
| 操作续段 | `OperationContinuation` | 从 Snapshot base frontier 到目标 frontier 的已提交 Operation 序列；gap、duplicate、乱序或 identity/frontier mismatch 必须原子拒绝。 | 现行规范，ADR-0020 |
| 持久性事实 | Durability Facts | `AppliedLocally`、`PersistedLocally`、`QueuedForSync`、`ServerAcknowledged` 等可独立报告的事实；不是强制线性、不可逆的全局状态机。 | 现行 Contract 边界，ADR-0022 |
| 文档摘要 | Document Digest | 对规范化 Document graph、语义字段和 ResourceManifest binding 计算的版本化确定性摘要；排除 session、presence、URL/path、decode、cache 和 GPU 状态。 | 现行规范；具体 schema 随 R2 冻结 |

## 5. Scene、渲染与缓存

| 中文首选名 | 英文或代码名 | 当前工作定义 | 规范地位 |
| --- | --- | --- | --- |
| 运行时场景 | `RuntimeScene` | 从 Document revision 派生、跨 View 共享、可重建的场景概念聚合，覆盖 layout、world bounds、render/hit records、稳定 resource refs、对象空间查询和 world-space invalidation；不包含 per-view visible/LOD/screen damage、Selection、Presence 或 Preview。RF-01 已把它拆成 compiled data、Scene facade 及多个 participant，概念与具体类型的映射将在步骤 2 澄清。 | 核心概念现行；物理映射待澄清 |
| 已编译场景快照 / 增量 | `CompiledSceneSnapshot` / `CompiledSceneDelta` | RF-01 中 SceneCompiler 产生的内部、可重建 scene 输入；与可持久化 `DocumentSnapshot` 完全不同。 | 实现术语，RF-01 |
| 场景编译器 | `SceneCompiler` | 从明确 revision 的 DocumentReadView/ChangeSet 产生版本化 compiled scene 输入；full 与 incremental 在相同 revision 上必须等价，错误 hints 只能触发扩大失效或 full rebuild。 | 现行规范；RF-01 已细化 |
| 场景绑定 | `SceneBinding` | 把 compiled snapshot/delta 通过 prepare→commit 原子应用到 Scene facade 的内部桥；增量失败时从同一 DocumentReadView 回退 full rebuild。 | 现行内部边界，ADR-0021/RF-01 |
| 场景门面 | Runtime `Scene` Facade / `canvas::Scene` | Runtime 内部 owning coordinator，组合 scene records、RenderScene、SpatialIndex 和 DamageTracker，对上提供 query、hit、draw 与 damage；不是公共 C ABI，也不是 SkSG `Scene`。 | 现行内部边界，ADR-0021 |
| 渲染场景 | `RenderScene` / `IRenderScene` | Scene facade 背后的私有渲染 participant，负责 revisioned render state、精确 hit 和不可变 draw list；不拥有 Document、全局空间查询、Tile 或公共 API。它不必天然是 DAG。 | ownership/interface 现行；具体实现可替换 |
| SkSG | Skia Scene Graph | 可作为私有 RenderScene adapter 的候选 Skia DAG；不得成为 Document model、公共 Scene API 或无限画布动态索引。当前 Direct baseline 已存在，真实 SkSG adapter 仍在验证。 | 候选内部实现，非公共契约 |
| 空间索引 | `SpatialIndex` / `ISpatialIndex` | 对 world-space 范围做候选查询，服务 viewport、Selection、Lasso、Eraser、HitTest 和 raster source；不负责精确命中、damage、对象 ownership 或最终 z-order。动态接口和 Runtime ownership 已接受，生产算法仍开放。 | 边界现行；RF-02 算法待验证 |
| 几何命中测试 | Geometry Hit Test | 在 SpatialIndex 候选上执行精确 geometry、clip 和 transform 判定；Editor SelectionPolicy 再决定选谁，SnapEngine 使用查询能力但不属于 Scene。 | 现行边界 |
| 损伤跟踪器 | `DamageTracker` / `DamageSet` | 保存带 revision 的 world-space content damage journal；正确性下界来自 before/after 与语义变化。它不是 InvalidationHints、SkSG dirty、单 View screen damage，也不是“读取一次就全局清空”的状态。 | 现行边界；RF-01 验证中 |
| 视图查询 / 帧状态 | `ViewQuery` / `FrameState` | 由 RuntimeScene、EditorSession/View 和 target 参数产生的单 View、单 target、单帧短暂状态，包含 visible records、clip、LOD、DPR/color 和 screen-space damage。 | 现行概念 |
| 帧构建器 | `FrameBuilder` | 合并共享 Scene、per-view state、Editor/Presence overlay、Active Preview 与 ExternalSurface placement，生成不可变 frame plan。 | 现行边界 |
| 帧图 | `FrameGraph` | 表达单帧 logical pass 依赖和 transient resource lifetime；不是 Document/Scene/Semantic Graph，也不要求每个 logical pass 对应一个物理 GPU pass。 | 现行边界；产品化在 R3 |
| 合成器 | `Compositor` | 根据 frame plan/FrameGraph 分配 pass 资源、应用 cache 并组织最终画布合成；不拥有 Document 语义或 OS window lifecycle。 | 现行边界 |
| 渲染后端 | `RendererBackend` | 把不可变 frame plan 通过当前 Ganesh backend 绘制到调用方提供的有效 RenderTarget；不拥有 native window/view/surface lifecycle。 | 现行规范，ADR-0005/0010 |
| 渲染目标 | `RenderTarget` | PlatformSurfaceAdapter 每帧提供、带 dimensions、DPR、color space、backend capability 和 generation 的受控目标；过期 generation 不可继续使用。 | 现行规范，ADR-0010/0012 |
| 平台表面适配器 | `PlatformSurfaceAdapter` | 拥有 HTML Canvas/WebGL context、HWND/swapchain、ANativeWindow/EGLSurface、CAMetalLayer/drawable 或 headless surface 的 acquire/resize/present/recover 生命周期。 | 现行规范，ADR-0010 |
| 外部表面 | `ExternalSurface` / `ExternalSurfaceRegistry` | 非 V1 的 semantic placement placeholder 与 Shell 侧 registry，用于受控 WebView/Video Overlay；Document/RuntimeScene 只持稳定 ID 与 placement 语义，不持 native handle。 | 扩展边界现行，ADR-0023 |
| 光栅缓存 | `RasterCache` | 对对象或 subtree 的可丢弃 raster 结果进行缓存，不拥有语义对象。 | 接口边界现行；产品实现待 R3 |
| 瓦片缓存 | `TileCache` | 缓存某个 world rect 在 content/raster revision、render params、scale、color space 和 backend capability 下的可丢弃 tile 结果；不按 Document ownership 分区，也不等于版本历史。 | 边界现行；L1 原型、产品化待 R3 |
| 持久瓦片存储 | `TileStore` | 可选 L3 persistent tile cache，必须带完整 compatibility namespace；不是 DocumentSnapshot 或权威持久化。 | 接口现行；格式和实现待独立 ADR |
| 瓦片调度 | `TileManager` / Tiling / LOD | TileGrid、优先级、预取、raster task、memory budget 和 eviction 的产品调度边界；算法可以参考 Chromium `cc`，但不直接依赖其文档/Layer ownership。 | 边界现行；RF-03 实现与参数待验证 |

## 6. Input、Ink 与 Arc

| 中文首选名 | 英文或代码名 | 当前工作定义 | 规范地位 |
| --- | --- | --- | --- |
| 指针样本批次 | `PointerSampleBatch` | 批量携带位置、压力、倾角、时间戳、设备、provenance 与绑定的 coordinate/viewport revision；历史点不逐 sample 跨语言边界。 | 现行规范 |
| 笔迹会话 | `StrokeSession` | 对同一 confirmed/predicted 输入执行统一 resample、smooth、pressure mapping、prediction/rollback，并增量产生 Preview Model 与 Canonical candidate。 | 现行规范，ADR-0004/0011 |
| 规范笔迹 | Canonical Stroke | 只由 confirmed input 和版本化 brush/resource 语义产生、可编辑、可重放并通过原子 Operation 进入 Document 的笔迹。 | 现行规范 |
| 预览模型 | Preview Model / `PreviewStrokeUpdate` | StrokeSession 输出的版本化派生表示，支持 confirmed/predicted、replace/truncate、revision 与 resource/transform identity；平台 sink 不重新解释 raw samples 或另做一套笔刷语义。 | 现行规范，ADR-0011 |
| 活动预览 | Active / Transient Preview | Preview Model 的短生命周期显示状态，可预测、替换、取消或降级，不进入 Document、digest、History 或 Persistence；presentation 失败不能取消 confirmed input 或 Canonical commit。 | 语义边界现行；物理延迟仍在验证 |
| 规范渲染 | Canonical Rendering | 从已提交 Document/Canonical Stroke 与匹配 scene revision 产生稳定最终画面的规范渲染路径。Scene/cache/GPU 仍是可重建派生状态，render 完成也不自动等于已显示。 | 现行规范 |
| 可见确认 | Canonical Visible Acknowledgement | 与 handoff token、stroke/scene revision 和 target generation 匹配的 presentation evidence；Arc 只在收到匹配确认后退役对应 Preview。 | 现行 handoff 语义，ADR-0017/0024 |
| Arc 输入源 | Arc Input Source | 平台侧批量采集高频输入并报告 source capabilities/loss 的组件；source loss 与 preview presentation failure 是不同 failure domain。 | Arc 边界现行；平台策略待审 |
| Arc 预览呈现目标 | Arc Preview Presentation Target | Arc backend 独立 acquire/present 的 transient target；不得与 Axiom Canonical target 并发拥有同一个 presentable backbuffer。 | 现行规范，ADR-0024 |

## 7. 资源、持久化与协作

| 中文首选名 | 英文或代码名 | 当前工作定义 | 规范地位 |
| --- | --- | --- | --- |
| 资源标识 | `ResourceId` | Document 内稳定、不可复用的语义身份；不是 URL、路径、platform handle、ContentHash 或可解引用对象指针。 | 现行规范，ADR-0013 |
| 资源修订 | `ResourceRevision` | 某个 ResourceId binding 的单调语义版本，用于区分资源替换；不等于 blob hash 或 decode generation。 | 现行规范，ADR-0013 |
| 内容哈希 | `ContentHash` | 带算法前缀的不可变内容身份；V1 使用 `sha256:<lowercase hex>`。相同 bytes 可跨路径/来源去重。 | 现行规范，ADR-0013 |
| 不可变内容块 | Blob | 由 ContentHash 寻址的原始 bytes。Blob 路径、下载 URL、可用性、decode 与 GPU upload 状态都不是 Document 语义。 | 现行规范 |
| 资源清单 | `ResourceManifest` | Document semantic state 与 digest 的组成部分，把 ResourceId 绑定到 ResourceRevision、ContentHash、kind、长度和必要规范元数据。 | 现行规范，ADR-0013 |
| 资源解析器 | `ResourceResolver` | 按 manifest/hash 只读获取并校验 blob；missing/corrupt 产生确定 placeholder/diagnostic，不修改 Document binding。 | 现行边界 |
| 持久化端口 | Persistence Port | Runtime 与外部 storage/data layer 交换 Snapshot、Operation continuation、Resource 和 durability event 的窄边界；Runtime 不拥有 DB、文件系统或 WAL 实现。 | 现行规范，ADR-0020/0022 |
| 同步端口 | Sync Port | Runtime 与外部 sync/collaboration transport 交换 Operation batch、frontier、durability/ACK 和 Presence channel 的窄边界；Runtime 不拥有 HTTP/WebSocket/Auth/retry。 | 现行边界；算法与协议待审 |
| 资源提供端口 | Resource Provider Port | 外部系统按 ResourceId/ContentHash 提供受校验 bytes 的接口；网络、磁盘位置与 blob GC 留在外部。 | 现行 Contract 边界 |
| 编码器 / 模式 | Codec / Schema | Operation、Snapshot、Sync、ABI、Renderer/Cache 各自独立版本化的编码契约；选择 Protobuf、FlatBuffers 或其他格式不会改变上层 ownership。 | 版本分离现行；具体格式待 ADR |
| 操作日志 | Operation Log | 已提交 Operation 序列的持久化组织概念。具体分段、索引、WAL、compaction、网络编码和协作算法尚未决定；不能与进程内 History 混用。 | 概念边界现行；格式待审 |
| 同步 | Sync | 负责传输、排队、重试、ACK 和 catch-up，不自行定义 Document 语义或修改 Scene/Tile。 | 边界现行；协议待审 |
| 协作 | Collaboration | 多 Actor 的 Operation envelope、merge/convergence、离线重连及独立 Presence 语义；不等同于 WebSocket 或 Sync transport。 | 产品范围现行；算法待 R4 ADR |

## 8. 平台和能力分级

| 中文首选名 | 英文或代码名 | 当前工作定义 | 规范地位 |
| --- | --- | --- | --- |
| 产品一级平台 | Product Tier A | Web、Windows、Android；承担 V1 正式产品 Shell、完整用户流、性能、IME/Input/Surface、发布和支持门禁。 | 现行规范，ADR-0015 |
| 可移植性二级平台 | Portability Tier B | macOS、iOS、iPadOS；持续验证共享 Runtime、C ABI/ObjC++、Ganesh/Metal 和核心 conformance，不自动承诺 V1 产品 Shell。 | 现行规范，ADR-0015 |
| 复用目标 | Reuse Target | ChromiumOS 复用 Web 产品 target；平台特有能力可以分级，但不能分叉共享语义。 | 现行规范，ADR-0015 |
| 工具目标 | Utility Target | Headless test/reference/golden 和受控内部 export；当前不承诺公共 server/batch API。 | 现行规范，ADR-0015 |
| 能力命名空间 | Capability Namespaces | `DocumentCapability`、`RendererCapability`、`PlatformCapability`、`ProductCapability` 分别表达 schema、渲染、平台和产品能力；各自声明 required/optional、version、fallback/reject 和 diagnostic。 | 边界现行；编码待 R1 |

## 9. 只作为候选使用的研究术语

| 中文首选名 | 英文或代码名 | 当前工作定义 | 规范地位 |
| --- | --- | --- | --- |
| 工作集 | Working Set | 为当前 camera、编辑和预取需求保持在内存或可立即执行状态的数据集合。它不等于 visible set、SpatialIndex、TileCache 或完整 Document；是否允许卸载 semantic object payload 尚未决定。 | 历史候选，SRC-CHAT-03 |
| 水合 | Hydration | 从持久对象/资源表示加载足够 payload，使对象可参与 Scene compile/render 的候选过程；它不等于图片/字体 decode。由 C++ Runtime 还是 Data Runtime 编排尚未决定。 | 历史候选，SRC-CHAT-03 |
| 语义图 | Semantic Graph | 表达对象之间业务关系、引用、依赖或 AI provenance 的领域图；与空间索引、RenderScene DAG 和视觉 Connector 正交。V1 是否实现及 schema 均未决定。 | 产品候选，SRC-CHAT-06 |
| 上下文查询 | `ContextQuery` | AI 或产品功能按 selection、viewport、frame、邻域或语义关系读取受控上下文的候选 Contract；AI 写入仍必须回到 Command/Operation 路径。 | 产品候选，SRC-CHAT-06 |

## 10. 这些名称不能互换

| 容易混用的名称 | 必须保持的区别 |
| --- | --- |
| `DocumentSnapshot` / `DocumentReadView` / `ViewportSnapshot` / `CompiledSceneSnapshot` | 分别是可持久化语义检查点、进程内只读文档视图、输入坐标快照和可重建 scene 输入。 |
| Document Revision / Operation Sequence / RecoveryFrontier / Target Generation | 分别服务进程内发布、操作顺序、持久/同步恢复和 surface 生命周期。 |
| Intent/Command / Operation / Document Transaction / Operation Batch / ChangeSet | 分别是用户意图、确定语义修改、原子提交边界、传输容器和提交后的派生变化。 |
| RuntimeScene / Scene Facade / RenderScene / SkSG Scene | 分别是共享派生场景概念、Runtime 内部协调门面、私有渲染 participant 和一个候选实现。 |
| SpatialIndex / Geometry HitTest / SelectionPolicy / SnapEngine | 分别是候选查询、精确命中、产品选择规则和编辑吸附策略。 |
| Platform Surface / RenderTarget / ExternalSurface / Arc Preview Target | 分别是 OS surface 生命周期、单帧受控渲染目标、受控外部 Overlay 和独立临时预览目标。 |
| Canonical State / Canonical Stroke / Canonical Rendering / Canonical Visible | 分别是语义权威、规范笔迹对象、稳定渲染路径和已获得匹配显示证据。 |
| Preview Model / Active Preview / Arc Presentation | 分别是版本化派生数据、短生命周期显示状态和一种平台呈现实现。 |
| Persistence / Sync / Collaboration / History | 分别是本地耐久、传输追赶、多用户语义和本地 Undo intention。 |
| Runtime Public C ABI / Product Public SDK Boundary | 前者是已接受的 Runtime 跨语言 Contract；后者决定最终发布和兼容承诺，仍待评审。 |

## 11. 本轮需要用户确认的术语策略

确认本表只表示同意用这些定义继续架构重审，不会把“候选方案”升级为 Accepted。需要明确
确认的策略是：

1. `Axiom` 表示仓库身份，也作为本轮及后续新架构文档的 Runtime 规范名称；现有
   `Canvas v2` 文档的正式更名、兼容称呼和迁移关系另做 ADR。
2. `Platform Host` 表示已经接受的 composition-root 角色；`Host Runtime` 只表示候选物理模块。
3. 使用中性的 `Shared Data Runtime`；TypeScript、ownership、bridge 和 codec 都留给后续 RFC。
4. `Runtime Public C ABI` 与“最终公开哪些 SDK”分开讨论。
5. `RuntimeScene` 暂作架构聚合概念；它与 RF-01 `Scene`/compiled types 的物理映射进入步骤 2
   澄清，不在术语表中静默选择。
6. Arc 的现行模块边界继续有效；public/internal packaging、Native input/preview 分级、Web bundle
   policy 和低延迟门禁分别审查。
7. Working Set、Hydration、Semantic Graph 和 ContextQuery 只作为候选词使用，不进入现行 V1
   架构或产品范围。

以上七条术语策略已于 2026-08-21 获得用户逐项确认。确认只接受词义和评审边界；标为
“候选方案”“历史候选”或“待审”的具体技术方案没有因此升级为 Accepted。

如果后续主题需要新术语，优先在对应 Problem/RFC 的“本文用语”中定义；只有跨多个主题并
通过评审后才加入本表，避免全局术语表变成未经验证的类名清单。
