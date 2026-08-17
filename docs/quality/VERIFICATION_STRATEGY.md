# Canvas v2 验证策略

> 状态：Accepted Quality Baseline；适用范围：POC-01～06、R1～R5；阶段阈值来源：[分阶段交付计划](../planning/STAGED_DELIVERY_PLAN.md)

Canvas v2 的首要风险不是“Skia 能不能画”，而是 Document、Ink、RichText、RuntimeScene、FrameGraph 和 Cache 的边界能否在跨平台、低延迟、大场景与协作条件下保持一致。本策略规定每个结论使用什么 oracle、语料和门禁证明。

## 1. 验证不变量

1. 相同初始 Document 与 Operation sequence 产生相同 Document digest。
2. full SceneCompiler 与 incremental ChangeSet 在相同 revision 下结果等价。
3. Preview/FastInk 可以降级或丢失，Canonical Stroke 不能丢失或被预测点污染。
4. Text composition 只有 commit 才产生一次 Operation，cancel 不改变 Document。
5. RuntimeScene、GPU 和 cache state 可以丢弃并从 Document 重建。
6. Shell/Bridge 差异不改变 Document、Stroke 和 RichText 语义。
7. Collaboration Presence 的丢失不影响 Document convergence。
8. 性能结论必须绑定设备、场景、构建和采样方法。
9. RuntimeScene 的共享内容与各 View/Frame 查询结果独立；同一 Scene 上不同 Viewport 不得互相污染 visible set 或 screen damage。
10. FastInk backend 只消费共享 Preview Model；Default/FastInk sink 不得形成不同的 Stroke 算法。
11. Platform surface/context/device 丢失不得泄漏 native handle 到 Runtime Core，也不得改变 Document。
12. 所有跨模块 geometry 声明 Node Local/Page-World/View Logical/Device Pixel/Platform Screen 空间；viewport revision 和 DPR 变化不能重解释历史输入。
13. ResourceId 只经 versioned ResourceManifest 绑定不可变 ContentHash；manifest 改变影响 Document digest，资源暂时 missing 不改变语义。
14. Undo/Redo 生成新的 compensating Operations；不能倒退 DocumentSnapshot 或 operation sequence。
15. Canonical layout 与 Stroke replay 只依赖已声明的 FontResource/BrushDescriptor version，不依赖偶然系统字体或当前算法版本。
16. Canonical numeric values、serialization 与 digest 遵循 ADR-0016；视觉容差不能替代
    Document/Stroke/Scene 语义逐字节一致。
17. Runtime frame invalidation 与平台 VSync/present 分离；旧 target generation 不得 present，
    每个最新 View revision 最终可见。
18. 成功 Stroke 的 confirmed input 不得静默丢失、重复或重排；过载只能明确取消且不留下
    部分 Document，Preview/prediction/frame 可以按契约合并。
19. ChangeSet 的 InvalidationHints 可完全丢弃而不影响 Scene 正确性；只有 Operation 是持久/
    协作事实。
20. wall clock、平台 random、线程调度和容器迭代顺序不进入 semantic digest；版本化
    deterministic clock/seed/PRNG 是 POC common oracle。
21. `DocumentSnapshot@F + committed OperationContinuation F→T` 必须恢复相同 target
    revision/frontier/digest；Snapshot 不能用于普通编辑或 Undo/Redo。

## 2. 结果 Oracle

### 2.1 Document digest

规范化遍历 V1 节点、层级、排序、属性、RichText、Stroke 和 ResourceManifest bindings，排除 blob bytes、下载 URL/本地路径、decode/cache、地址、session、presence、非语义时间戳和允许不同的 transport metadata，再计算版本化摘要。节点相同但 ResourceId→ContentHash binding 改变时摘要必须改变；资源当前 missing 不改变摘要。

用途：

- 跨平台 operation replay。
- 保存/加载与 migration round-trip。
- Collaboration 多副本 convergence。
- FastInk/Canonical 最终语义比较。

摘要算法和排除字段本身版本化；不能因测试失败静默修改。

### 2.2 Scene digest

规范化 RuntimeScene records、bounds、spatial membership、hit-test order 和 render record identity，用于比较 full/incremental compile。GPU handle、cache address 和 platform surface 不进入摘要。

### 2.3 Stroke digest

记录确认 world-space samples、版本化 BrushDescriptor（type/version/semantic parameters/resource identity）、resample/smooth 输出和 Canonical geometry。预测点、Preview buffer、View/Device coordinates 和平台 FastInk metadata 不进入最终摘要。

### 2.4 Text behavior oracle

一个文本语料同时包含：初始 TextDocument、FontResourceId/ContentHash/fallback chain、平台无关编辑事件、预期 selection/composition 状态、提交 operations、最终 digest 和 layout geometry。三平台必须得到相同语义结果；未声明系统字体不得参与 canonical oracle。

### 2.5 坐标与变换 oracle

坐标语料记录 Node Local→Page/World→View Logical→Device Pixel 的矩阵、ViewId、viewport revision、DPR、target generation 和舍入边界。Oracle 分别比较 world-space semantic geometry、per-view visible/hit-test、device-space placement；不能把不同空间的数值直接比较或放入同一 digest。

### 2.6 数值与确定性 oracle

Canonical corpus 记录 storage type、field order、little-endian bit pattern、algorithm/version、
中间精度/舍入边界、seed/PRNG 和预期错误。至少覆盖 `-0/+0`、subnormal、舍入中点、
NaN/Infinity、极端 finite 值、退化/不可逆矩阵、checked overflow 和不同表达但语义等价的
Operation。在 x64、arm64 和 WASM 上比较 canonical values 与 digest；非法值必须整笔/整
transaction 拒绝。任何差异都不能用视觉 tolerance 放行。

### 2.7 Snapshot 与恢复 oracle

恢复语料同时保存 Snapshot identity/schema/capability/revision/RecoveryFrontier/digest、
committed operation continuation、目标 revision/frontier/digest 和所需 ResourceManifest。
Oracle 分别比较：纯 Operations 构建状态、Snapshot round-trip、Snapshot + continuation 和
restart/crash recovery。资源 blob availability 单独记录；missing blob 不能改变合法 Snapshot
的 Document digest。Revision 与 RecoveryFrontier 必须分别比较，不得相互代替；Snapshot/
continuation 的 Document identity 和 base/target frontier 必须连续。`ViewportSnapshot` 与
`DocumentReadView` 不得出现在 persistence codec。

Checkpoint fault oracle 在 Snapshot bytes、ResourceManifest binding、continuation metadata、
durability barrier、verification 和 prefix compaction 各步骤中断；恢复必须选择最近完整
checkpoint。未证明 Snapshot 可读取/可验证前，旧 Operation prefix 不得被回收；blob GC
必须保持所有可恢复 manifest 引用可达。

### 2.8 视觉 oracle

- 使用固定 Skia commit、固定字体/图片、viewport、DPI、颜色空间和 scene fixture。
- Raster reference 保存 expected；产品 backend 输出 actual 与 diff。
- POC-01 门禁为至少 99.9% 像素每通道差值 ≤ 2。
- GPU/平台特有容差必须单独登记，不扩大通用阈值掩盖差异。
- 每次失败输出 expected、actual、diff、scene revision 和 backend metadata。

### 2.9 延迟、队列与帧调度 oracle

时间点至少包含：platform sample、InputRouter receipt、Ink processing、Preview submit、Preview visible、Canonical commit、Canonical visible、Preview removed。

- App-level Preview absolute baseline：p95 ≤ 16.7 ms，p99 ≤ 33.3 ms。
- Preview→Canonical handoff：≤ 2 帧，无空白或位置跳变 > 1 device pixel。
- 性能分布记录 p50/p95/p99/max、样本数、warm-up、异常值规则、display refresh rate、
  frame interval、sample-to-visible frame count、missed presentation、input/Preview queue
  depth 与 oldest-sample age。16.7 ms 是跨设备 absolute baseline，不代表高刷设备的一帧
  体验目标；90/120/144 Hz 还必须以 frame count 和 Human Performance Gate 判断。
- Frame trace 至少记录 invalidation(reason/revision/generation)、platform callback、target
  acquire、render submit、present、visible acknowledgement、cancel/drop。旧 generation
  不得 present，同一 View 未决 callback 有界。
- confirmed-input backlog 不得随书写时长增长；成功路径 sample 100% 完整，过载路径必须
  明确 InputOverrun/cancel 且无部分 Document。Predicted/Preview/frame 合并分别统计。
- 若具备光电设备，device FastInk 额外记录 raw input→scanout；不得与软件 timestamp 混为同一指标。

### 2.10 Human Performance Gate

人工体验是量化门禁的补充，不是替代。POC-02 的 Canvas Ink Playground 与 POC-03 的 Integrated Performance Playground 使用固定动作 rubric：慢写、快速长划、急转、画圈、压力渐变、连续书写，以及在 1K/10K/50K/100K objects 下 pan、zoom、write、select、drag。

每次评审记录平台、设备、笔、显示刷新率、构建/commit、场景、持续时间、评审人和主观结论，并关联 input→preview trace、frame pacing、prediction correction、handoff 帧序列和内存数据。`可接受/有条件接受/阻断` 必须有结构化理由；不能只提交视频或“感觉流畅”的结论。

## 3. 验证资产

```text
tests/
├── unit/              # 精确规则和失败边界
├── property/          # Document/Scene/Tool/Text 不变量
├── replay/
│   ├── operations/
│   ├── pointer/
│   ├── coordinates/
│   └── text/
├── golden/            # scene、stroke、text 与 diff metadata
├── resources/         # manifest、content hash、missing/corrupt、font/brush blobs
├── history/           # undo grouping、compensation 与并发交错
├── convergence/       # 多副本冲突、随机 seed 和 network faults
├── bridge/            # WASM/C ABI/JNI contract tests
├── lifecycle/         # Surface、IME、ExternalSurface、FastInk
├── fuzz/              # 文件、operation、geometry、migration
├── performance/       # 固定场景与结果 schema
└── experience/        # Human gate rubric、设备记录、trace/录屏索引与签署
```

资产必须包含版本、随机 seed、能力需求、资源 hash 和预期 oracle。大文件存储方式由 R1 工程决策确定，但 clean environment 必须可确定性获得固定版本。

## 4. 测试层级

### 4.1 静态与编译期

- format/lint、警告、公开头文件自包含、ABI export 检查。
- module dependency test：Document 不能依赖 Skia/platform/network/ResourceManager/Persistence；Renderer 无 Document 写入口且不包含 native window/view/surface 类型。
- Platform surface adapter、Application API、PointerAdapter 和 TextInputAdapter 边界检查；禁止 Shell API 全部汇入 InputRouter。
- `core/input`、Geometry、Layout/HitTest、Resources/Persistence 与 Collaboration 逻辑边界检查；Serialization 不能成为旁路权威状态。
- 按 stage/tier/changed paths 选择 target matrix；core/public ABI 变更必须编译 Tier A 与 Tier B，Shell-only 变更只阻断受影响 Tier A。
- third-party lock、license 和构建 flag 检查。

### 4.2 单元测试

- geometry、transform、极端数值和坐标空间。
- canonical binary32/zero/finite/overflow/serialization、算法精度/舍入和 deterministic
  clock/seed/PRNG domain separation。
- command validation、transaction、ordering、undo grouping。
- DocumentSnapshot identity/schema/capability/frontier/digest、continuation range 和原子 restore。
- Pointer batch、confirmed queue、batch merge、Preview coalescing、backpressure/overrun、
  resample、prediction rollback 和 StrokeSession 状态。
- 坐标组合/逆变换、viewport revision、DPR/rounding、HitTest tolerance 和 ExternalSurface placement。
- ResourceId/manifest/hash、immutable blob、missing/corrupt/replace/dedup 和 FontResource fallback。
- History grouping、compensating undo/redo、no-op/rejected/conflicted 和 transaction atomicity。
- BrushDescriptor version dispatch 与 Canonical candidate 增量处理。
- Text selection、composition、logical positions 和 layout mapping。
- Scene world invalidation、SemanticChanges/InvalidationHints、per-view visible/screen damage、
  cache key、spatial query、HitTest/Selection/Snap boundary、FrameBuilder/FrameGraph logical dependencies。
- frame invalidation、PlatformFrameScheduler request merge、target generation、visible ack 和
  multi-view callback lifecycle。
- PreviewStrokeUpdate revision、confirmed/predicted replacement、buffer ownership 和 Default/FastInk sink 等价性。
- operation envelope、去重和 Presence expiry。

### 4.3 属性测试

- 任意合法 Operation 后 Document 不变量保持。
- 保存/加载/migration 保持 Document digest。
- 任意资源 availability 变化不改变 graph+manifest digest；合法 manifest binding Operation 必须改变 digest。
- 任意 Undo/Redo 仍通过正常 Operation replay；旧 sequence/history 不被改写。
- 任意合法 Snapshot@F + continuation F→T 恢复 target T；切分 checkpoint 位置不改变目标 digest。
- 任意 ChangeSet 序列与 full compile 的 Scene digest 等价。
- 删除、扩大或损坏任意 InvalidationHints 仍保持 full/incremental Scene 等价，只允许性能/诊断变化。
- 任意 Tool/Text/Stroke cancel 不产生 Document 部分修改。
- 任意 queue overrun、过期 frame callback 或 View destroy 不产生部分 Stroke、错误 present 或共享 View 状态污染。
- 相同 operation set 的 Collaboration replicas 最终收敛。

失败必须保存 seed，并自动缩减到最小复现输入。

### 4.4 Replay

- Operation replay 验证跨平台确定性与旧语料兼容。
- Snapshot recovery replay 验证 round-trip、restart、frontier continuity 和 continuation 原子性。
- Pointer replay 验证 batch、pressure、Vector/Dab、Preview/Canonical 和 FastInk。
- 同一 deterministic seed/clock replay 验证 brush random streams、100K generator 和 algorithm version；失败保存 seed/domain/version。
- Coordinate replay 验证同 world path 在不同 Viewport/DPR 下的 Canonical digest，以及 view/device placement 的独立 oracle。
- Text replay 验证三平台 IME 转换后的共享行为。
- Lifecycle replay 验证 surface、device loss、focus、background 和 backend fallback。
- Scheduler replay 验证 burst invalidation、VSync callback、resize、target generation、visible ack 与多 View teardown。

### 4.5 Fuzz 与不可信输入

- 快照、operation log、migration、资源索引和压缩边界。
- Snapshot digest/schema/capability/frontier mismatch、continuation gap/duplicate/out-of-order、
  截断与超限；失败不能暴露部分 Document。
- checkpoint/manifest/continuation 写入、durability/verification 和 prefix/blob compaction
  之间的 crash cut；不能过早回收最近可恢复状态。
- collaboration envelope、batch size、unknown capability 和版本协商。
- NaN/Infinity、退化 path、极大坐标和非法层级。
- negative zero、subnormal、舍入边界、checked arithmetic overflow、损坏 InvalidationHints、
  input queue/batch size 与 frame revision/generation。
- 文本 runs、logical range 和 composition replacement。

Oracle 不只是“不崩溃”：还要求有限资源使用、明确错误、transaction 原子性和最近有效数据保留。

### 4.6 集成与端到端

- Input → Editor/Ink/Text → Operation → Document → Scene → frame。
- History intention → compensating Operation → Document/Persistence/Collaboration → replay。
- ResourceManifest Operation → verified blob/missing placeholder → Scene invalidation → digest。
- Save → restart → recovery → digest。
- Empty Document → Operations → A；A → DocumentSnapshot → B；Snapshot@F + continuation →
  C/D，比较 identity/revision/frontier/digest 并验证 RuntimeScene 从恢复后 Document 重建。
- FastInk Preview → Canonical visible → Preview cleanup。
- Confirmed input burst → bounded queue/batch merge → Preview coalescing → frame invalidation/
  VSync → visible ack；过载取消路径无部分 Document。
- RuntimeScene + 两个 Viewport → 两个独立 FrameState/FrameGraph，无跨 view 污染。
- Document transaction → SemanticChanges + optional InvalidationHints → incremental/full Scene
  equivalence；persist/collaboration 只包含 Operation。
- PlatformSurfaceAdapter acquire/resize/present/context loss → 新 generation RenderTarget → Canonical redraw。
- ExternalSurface placement → focus/lifecycle → fallback placeholder。
- Local Operation → network faults → remote replicas → convergence。
- Shell/Bridge → surface/IME/clipboard/file → shared Runtime behavior。

## 5. POC 门禁矩阵

| POC | 正确性门禁 | 性能/资源门禁 | 故障门禁 |
| --- | --- | --- | --- |
| POC-01 | Web/Windows/macOS/iOS/iPadOS/Android digest 一致；独立空 Document operation replay 一致；黄金图 99.9%/差值 2 | 各平台 1K 节点连续 60 秒；无单帧 >100 ms | 各平台 100 次 runtime/view 生命周期；不实现正式 Snapshot codec |
| POC-02 | Canonical Stroke/Brush/seed/world-coordinate digest 一致；Pointer→AddStroke 与空 Document replay 一致；numeric corpus 通过；Preview Model 跨 sink 一致 | Preview absolute p95/p99 ≤16.7/33.3 ms，并报告 refresh/frame-count/queue-age；长笔迹 end p95 ≤16.7 ms；三平台 Human Ink Gate | cancel、InputOverrun、transform revision、prediction rollback、过期 target/handoff 无空白或部分 Stroke |
| POC-03 | full/incremental 在正确/空/损坏 hints 下等价；多 viewport/DPR FrameState 隔离；logical pass 优化等价 | 100K scene；Web ≤512 MiB、Windows ≤768 MiB；Android 真机集成报告；callback/queue 有界 | cache clear、resize、旧 generation、device loss 恢复 |
| POC-04 | 三平台 text digest/行为/font resource/fallback 一致 | 10K 字符输入/layout p95 ≤16.7/33.3 ms | missing/corrupt font；100 次 focus/composition lifecycle |
| POC-05 | 非 V1 risk proof；ExternalSurfaceId/registry placement 误差 ≤1 px | overlay 同步 ≤2 帧；100 次后内存增长 <5% | surface/focus/load failure fallback；不进入 V1 schema |
| POC-06 | FastInk/Canonical 最终 digest 一致；Default/FastInk sink 消费同一 Preview revision | Preview absolute p95/p99 ≤16.7/33.3 ms，并报告 refresh/frame-count/queue-age；handoff ≤2 帧 | backend/device/surface/旧 generation failure 不丢 Stroke |

POC 报告必须同时附原始结果、环境和复现命令；只给结论截图不算通过。

## 6. 产品阶段门禁

### R1

- Product Tier A clean build、Bridge contract、module dependency 和 sanitizer smoke 全部通过；core/public ABI 变更同时编译 Portability Tier B。
- POC-01～04 阻断语料迁入产品骨架后无回归；POC-06 Accepted 后必须迁入，POC-05 不作为 V1/R1 门禁。
- numeric/clock/random、input backpressure、frame scheduling、ChangeSet/hints 和分层 capability contract tests 通过。
- DocumentSnapshot/RecoveryFrontier 概念 contract 通过，且无 Snapshot mutation/Undo 旁路。

### R2

- V1 节点行为、round-trip、migration、replay digest 全部通过。
- Snapshot@F + continuation F→T 的 round-trip/restart/故障恢复全部得到相同 target frontier/digest。
- ResourceManifest/blob、FontResource、BrushDescriptor 和 compensating Undo/Redo 语料全部通过。
- ID/stable order、multi-view lifecycle 与 V1 color/Image EXIF/ICC ADR corpus 全部通过。
- 文件/operation/migration fuzz 发布前累计 ≥24 小时无未归类 crash。
- 写入中断、磁盘满、资源缺失和损坏输入无静默数据丢失。

### R3

- Product Tier A 保持 POC-02/03/04/06 的延迟、规模、视觉和生命周期门禁；POC-05 Hybrid Surface 不进入 V1 产品 target。
- Product Tier A 混合编辑 2 小时无 crash，稳定期内存增长 <5%。
- Global Resource Budget 覆盖 decoded resources、Canvas/Skia cache、FrameGraph transient 和
  surface memory；memory pressure 无未归因峰值、无无界增长或 OOM。
- device/cache/surface 丢失恢复不改变 Document digest。
- Product Tier A Human Ink/Integrated Performance Gate 使用正式产品 target 完成签署，主观问题均有关联 trace 和处置结论。

### R4

- 3/5 replicas 随机交错累计 100K operations，最终 digest 100% 一致。
- 5 客户端 2 小时 soak 无 divergence、已确认操作丢失或队列无界增长。
- duplicate、out-of-order、partition、reconnect、server restart 和 snapshot switch 全覆盖。

### R5

- 24 小时 fuzz、8 小时混合编辑 soak 和协作 soak 通过。
- 相比 R3 accepted baseline 的关键性能回归 ≤5%。
- 安装、升级、迁移、恢复、服务故障、诊断导出和回滚演练通过。

## 7. CI 分层

### 每次提交

- format/lint、依赖图、主要 target 增量构建；按 stage、support tier 与 changed paths 选择矩阵。
- unit、快速 property、smoke operation/pointer/text replay。
- 小型 golden、Bridge contract 和 cache/device fallback smoke。
- core/public ABI 或 shared semantic 变更必须编译 Product Tier A + Portability Tier B；单一 Shell UI 变更只阻断受影响 Tier A，但不得跳过共享 contract tests。

### 合并前

- POC-01 与 shared Runtime/ABI 变更：Web/Windows/macOS/iOS/iPadOS/Android clean build；iOS 与 iPadOS 使用不同 simulator device 验收同一 universal runner。
- 产品阶段非 core 变更：Tier A 全量受影响 target；Tier B 可由 build/conformance impact rule 选择，但不能长期跳过定时完整矩阵。
- 完整 unit/property/replay/golden/Bridge/lifecycle。
- sanitizer 核心矩阵和受影响模块 benchmark。
- 文件/协议变更必须运行兼容与 migration 语料。

### 定时任务

- 长时间 fuzz、扩大 property seeds 和 100K scene matrix。
- Product Tier A + Portability Tier B 完整 build/conformance，防止 Tier A 平台特例污染共享 Runtime。
- Collaboration random convergence/network fault/soak。
- 专用基准设备运行输入延迟、FrameGraph、内存和 device recovery。
- 代表性真实笔/移动设备运行 Human Ink 与 Integrated Performance Gate，并归档结构化体验报告。
- 依赖、许可证、可复现构建和旧文件/协议矩阵。

### 发布候选

- 锁定 commit、Skia/依赖、资源和构建配置。
- Product Tier A 运行全量真实设备性能、视觉、稳定性、迁移、恢复、安装/升级和支持门禁。
- Portability Tier B 运行 core conformance、Metal render/readback 与生命周期；结果不宣称 Apple V1 产品发布。
- Headless 只验收 test/reference/golden 与内部受控 export；不发布公共 server/batch API。
- 生成 SBOM、许可证、诊断、已知限制和回滚产物。

## 8. 性能测量规则

- Debug 构建只做诊断，不作为性能结论。
- 每组 benchmark 先 warm-up，再采集固定时长/次数；禁止只报告最好一次。
- CPU、GPU、内存、cache、dirty/cull 和 input timestamps 使用统一 trace correlation ID。
- 设备热状态、电源模式、刷新率、frame interval、VRR 状态和浏览器 throttling 必须记录。
- 延迟同时使用 milliseconds 与 display-frame/sample-to-visible 指标；不得把 60 Hz 的
  16.7/33.3 ms 直接解释为所有高刷设备的一/两帧体验。
- 内存报告分别列出 decoded image/font、Canvas Raster/Tile、Skia GPU cache、FrameGraph
  transient、surface/overlay 和 unknown/unattributed；只报告单模块预算不算通过。
- CI 噪声较大时只提示趋势；硬门禁运行在固定设备/runner。
- 修改阈值需要重复基准、原因和 ADR/阶段文档更新，不能以当前实现达不到为理由。
- 人工体验问题不因量化指标通过而自动关闭；同样，主观“流畅”也不能豁免 digest、延迟、帧时间或内存门禁。

## 9. 失败处理

- 保存 fixture、seed、operation/input/text log、trace、expected/actual/diff 和环境。
- 不允许通过扩大通用视觉容差、删除语料或盲目重跑掩盖问题。
- flaky test 进入隔离任务时仍定时运行，并有负责人、原因和到期时间。
- Document corruption、Canonical Stroke 丢失、Scene 增量不等价或 Collaboration divergence 立即阻断相关阶段。
- FastInk 平台能力失败应验证 fallback，不能拖垮 Canonical path；ExternalSurface failure 只阻断 POC-05/future capability，不阻断 V1/R1～R5。

## 10. 文档自身验收

架构文档变更至少执行：

- `git diff --check`。
- 全部本地 Markdown 链接存在性检查。
- Markdown/Mermaid fence 成对和 Mermaid 图中关键模块名检查。
- POC-01～06、R1～R5 均包含设计、验证、实现、交付物和退出条件。
- Accepted ADR 索引与实际文件一致。
- Visual Document Runtime、Coordinate Spaces、Numeric Determinism、DocumentSnapshot/
  RecoveryFrontier、ResourceManifest、
  compensating Undo/Redo、platform support tiers、PlatformFrameScheduler、Input backpressure、
  SemanticChanges/InvalidationHints、RichText、InkEngine、SceneCompiler、FrameGraph、TileCache、
  FastInkBridge、Native CanvasView 和 Collaboration MVP 均有定义而非只出现名称。
