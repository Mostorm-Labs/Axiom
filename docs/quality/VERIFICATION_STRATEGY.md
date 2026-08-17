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

## 2. 结果 Oracle

### 2.1 Document digest

规范化遍历 V1 节点、层级、排序、属性、RichText、Stroke 和资源引用，排除 cache、地址、session、presence、非语义时间戳和允许不同的 transport metadata，再计算版本化摘要。

用途：

- 跨平台 operation replay。
- 保存/加载与 migration round-trip。
- Collaboration 多副本 convergence。
- FastInk/Canonical 最终语义比较。

摘要算法和排除字段本身版本化；不能因测试失败静默修改。

### 2.2 Scene digest

规范化 RuntimeScene records、bounds、spatial membership、hit-test order 和 render record identity，用于比较 full/incremental compile。GPU handle、cache address 和 platform surface 不进入摘要。

### 2.3 Stroke digest

记录确认样本、规范化 brush descriptor、resample/smooth 输出和 Canonical geometry。预测点、Preview buffer 和平台 FastInk metadata 不进入最终摘要。

### 2.4 Text behavior oracle

一个文本语料同时包含：初始 TextDocument、平台无关编辑事件、预期 selection/composition 状态、提交 operations、最终 digest 和 layout geometry。三平台必须得到相同语义结果。

### 2.5 视觉 oracle

- 使用固定 Skia commit、固定字体/图片、viewport、DPI、颜色空间和 scene fixture。
- Raster reference 保存 expected；产品 backend 输出 actual 与 diff。
- POC-01 门禁为至少 99.9% 像素每通道差值 ≤ 2。
- GPU/平台特有容差必须单独登记，不扩大通用阈值掩盖差异。
- 每次失败输出 expected、actual、diff、scene revision 和 backend metadata。

### 2.6 延迟 oracle

时间点至少包含：platform sample、InputRouter receipt、Ink processing、Preview submit、Preview visible、Canonical commit、Canonical visible、Preview removed。

- App-level Preview：p95 ≤ 16.7 ms，p99 ≤ 33.3 ms。
- Preview→Canonical handoff：≤ 2 帧，无空白或位置跳变 > 1 device pixel。
- 性能分布记录 p50/p95/p99/max、样本数、warm-up 和异常值规则。
- 若具备光电设备，device FastInk 额外记录 raw input→scanout；不得与软件 timestamp 混为同一指标。

## 3. 验证资产

```text
tests/
├── unit/              # 精确规则和失败边界
├── property/          # Document/Scene/Tool/Text 不变量
├── replay/
│   ├── operations/
│   ├── pointer/
│   └── text/
├── golden/            # scene、stroke、text 与 diff metadata
├── convergence/       # 多副本冲突、随机 seed 和 network faults
├── bridge/            # WASM/C ABI/JNI contract tests
├── lifecycle/         # Surface、IME、ExternalSurface、FastInk
├── fuzz/              # 文件、operation、geometry、migration
└── performance/       # 固定场景与结果 schema
```

资产必须包含版本、随机 seed、能力需求、资源 hash 和预期 oracle。大文件存储方式由 R1 工程决策确定，但 clean environment 必须可确定性获得固定版本。

## 4. 测试层级

### 4.1 静态与编译期

- format/lint、警告、公开头文件自包含、ABI export 检查。
- module dependency test：Document 不能依赖 Skia/platform/network；Renderer 无 Document 写入口。
- Web/Windows/Android 编译器和 target matrix。
- third-party lock、license 和构建 flag 检查。

### 4.2 单元测试

- geometry、transform、极端数值和坐标空间。
- command validation、transaction、ordering、undo grouping。
- Pointer batch、resample、prediction rollback 和 StrokeSession 状态。
- Text selection、composition、logical positions 和 layout mapping。
- Scene dirty、cache key、spatial query、FrameGraph dependencies。
- operation envelope、去重和 Presence expiry。

### 4.3 属性测试

- 任意合法 Operation 后 Document 不变量保持。
- 保存/加载/migration 保持 Document digest。
- 任意 ChangeSet 序列与 full compile 的 Scene digest 等价。
- 任意 Tool/Text/Stroke cancel 不产生 Document 部分修改。
- 相同 operation set 的 Collaboration replicas 最终收敛。

失败必须保存 seed，并自动缩减到最小复现输入。

### 4.4 Replay

- Operation replay 验证跨平台确定性与旧语料兼容。
- Pointer replay 验证 batch、pressure、Vector/Dab、Preview/Canonical 和 FastInk。
- Text replay 验证三平台 IME 转换后的共享行为。
- Lifecycle replay 验证 surface、device loss、focus、background 和 backend fallback。

### 4.5 Fuzz 与不可信输入

- 快照、operation log、migration、资源索引和压缩边界。
- collaboration envelope、batch size、unknown capability 和版本协商。
- NaN/Infinity、退化 path、极大坐标和非法层级。
- 文本 runs、logical range 和 composition replacement。

Oracle 不只是“不崩溃”：还要求有限资源使用、明确错误、transaction 原子性和最近有效数据保留。

### 4.6 集成与端到端

- Input → Editor/Ink/Text → Operation → Document → Scene → frame。
- Save → restart → recovery → digest。
- FastInk Preview → Canonical visible → Preview cleanup。
- ExternalSurface placement → focus/lifecycle → fallback placeholder。
- Local Operation → network faults → remote replicas → convergence。
- Shell/Bridge → surface/IME/clipboard/file → shared Runtime behavior。

## 5. POC 门禁矩阵

| POC | 正确性门禁 | 性能/资源门禁 | 故障门禁 |
| --- | --- | --- | --- |
| POC-01 | Web/Windows/macOS/iOS/iPadOS/Android digest 一致；黄金图 99.9%/差值 2 | 各平台 1K 节点连续 60 秒；无单帧 >100 ms | 各平台 100 次 runtime/view 生命周期 |
| POC-02 | Canonical Stroke digest 一致；预测点不入文档 | Preview p95/p99 ≤16.7/33.3 ms | cancel、prediction rollback、handoff 无空白 |
| POC-03 | full/incremental scene 等价 | 100K scene；Web ≤512 MiB、Windows ≤768 MiB | cache clear、resize、device loss 恢复 |
| POC-04 | 三平台 text digest/行为一致 | 10K 字符输入/layout p95 ≤16.7/33.3 ms | 100 次 focus/composition lifecycle |
| POC-05 | placement 误差 ≤1 px | overlay 同步 ≤2 帧；100 次后内存增长 <5% | surface/focus/load failure fallback |
| POC-06 | FastInk/Canonical 最终 digest 一致 | Preview p95/p99 ≤16.7/33.3 ms；handoff ≤2 帧 | backend/device/surface failure 不丢 Stroke |

POC 报告必须同时附原始结果、环境和复现命令；只给结论截图不算通过。

## 6. 产品阶段门禁

### R1

- 三平台 clean build、Bridge contract、module dependency 和 sanitizer smoke 全部通过。
- 六个 POC 阻断语料迁入产品骨架后无回归。

### R2

- V1 节点行为、round-trip、migration、replay digest 全部通过。
- 文件/operation/migration fuzz 发布前累计 ≥24 小时无未归类 crash。
- 写入中断、磁盘满、资源缺失和损坏输入无静默数据丢失。

### R3

- 产品 target 保持 POC-02～06 的延迟、规模、视觉和生命周期门禁。
- 三平台混合编辑 2 小时无 crash，稳定期内存增长 <5%。
- device/cache/surface 丢失恢复不改变 Document digest。

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

- format/lint、依赖图、主要 target 增量构建。
- unit、快速 property、smoke operation/pointer/text replay。
- 小型 golden、Bridge contract 和 cache/device fallback smoke。

### 合并前

- Web/Windows/macOS/iOS/iPadOS/Android clean build；iOS 与 iPadOS 使用不同 simulator device 验收同一 universal runner。
- 完整 unit/property/replay/golden/Bridge/lifecycle。
- sanitizer 核心矩阵和受影响模块 benchmark。
- 文件/协议变更必须运行兼容与 migration 语料。

### 定时任务

- 长时间 fuzz、扩大 property seeds 和 100K scene matrix。
- Collaboration random convergence/network fault/soak。
- 专用基准设备运行输入延迟、FrameGraph、内存和 device recovery。
- 依赖、许可证、可复现构建和旧文件/协议矩阵。

### 发布候选

- 锁定 commit、Skia/依赖、资源和构建配置。
- 全量真实设备性能、视觉、稳定性、迁移和恢复门禁。
- 生成 SBOM、许可证、诊断、已知限制和回滚产物。

## 8. 性能测量规则

- Debug 构建只做诊断，不作为性能结论。
- 每组 benchmark 先 warm-up，再采集固定时长/次数；禁止只报告最好一次。
- CPU、GPU、内存、cache、dirty/cull 和 input timestamps 使用统一 trace correlation ID。
- 设备热状态、电源模式、刷新率和浏览器 throttling 必须记录。
- CI 噪声较大时只提示趋势；硬门禁运行在固定设备/runner。
- 修改阈值需要重复基准、原因和 ADR/阶段文档更新，不能以当前实现达不到为理由。

## 9. 失败处理

- 保存 fixture、seed、operation/input/text log、trace、expected/actual/diff 和环境。
- 不允许通过扩大通用视觉容差、删除语料或盲目重跑掩盖问题。
- flaky test 进入隔离任务时仍定时运行，并有负责人、原因和到期时间。
- Document corruption、Canonical Stroke 丢失、Scene 增量不等价或 Collaboration divergence 立即阻断阶段。
- FastInk/ExternalSurface 平台能力失败应验证 fallback；不能让可选能力拖垮 Canonical path。

## 10. 文档自身验收

架构文档变更至少执行：

- `git diff --check`。
- 全部本地 Markdown 链接存在性检查。
- Markdown/Mermaid fence 成对和 Mermaid 图中关键模块名检查。
- POC-01～06、R1～R5 均包含设计、验证、实现、交付物和退出条件。
- Accepted ADR 索引与实际文件一致。
- Visual Document Runtime、RichText、InkEngine、SceneCompiler、FrameGraph、TileCache、FastInkBridge、Native CanvasView 和 Collaboration MVP 均有定义而非只出现名称。
