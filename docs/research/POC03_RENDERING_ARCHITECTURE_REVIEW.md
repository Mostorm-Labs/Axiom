# POC-03 渲染架构审查与后续路线

> 审查日期：2026-08-19；状态：Accepted direction，后续 RF-01～RF-03 仍需实现和验证。
> 本文不把目标接口写成当前已经存在的产品类，也不替代 POC-03 的物理证据。

## 结论先行

POC-03 当前实现适合作为基础实现：它验证了同一 Document/RuntimeScene 语义在 Host、Web、
Windows 和 Pixel 7 上的构建、digest、视觉等价、增量 Scene 和真实 Ink 写入路径。它不应被
解释为 10K/100K 生产级渲染架构已经完成。

Windows Native D3D12 集成门禁在同一设备上两次失败（p95/p99 为 26.998/38.737 ms 和
25.693/37.457 ms），而正确性、digest、视觉等价和候选数上限通过。这说明当前瓶颈至少
需要从渲染前期的对象查询、damage 传播、raster/cache 调度和显示提交链分析，不能只归因
于机器性能。POC-03 因此继续保持 `Validating`；不降低 p95/p99 门禁，也不将 Web 的一次
有界复测当作 Windows 通过证据。

正式边界见 [ADR-0021](../adr/0021-render-scene-spatial-index-tiling-boundaries.md)，总体
链路见 [系统架构](../architecture/SYSTEM_ARCHITECTURE.md)，阶段与退出条件见
[分阶段交付计划](../planning/STAGED_DELIVERY_PLAN.md)。

## 证据分层

| 证据 | 能证明什么 | 不能证明什么 |
| --- | --- | --- |
| Host Debug/ASan、full/incremental、digest、candidate oracle | Scene 数据模型、事务、增量结果和错误处理正确 | 真实 GPU、VSync、driver 和显示提交性能 |
| WebGL2/Windows/Android 跨端视觉与 digest | backend surface 与语义结果一致 | 生产 Tile/LOD、动态 R-tree 或长期资源预算 |
| Pixel 7 四档 integrated write | POC-02 Stroke/Operation 到 POC-03 Scene 的真实写路径可用 | Android 绝对帧门禁或正式 POC-02 压力笔 Human Ink Gate |
| Windows D3D12 physical gate | 当前实现的真实设备 frame budget 风险可复现 | 失败的唯一根因；需要分段 trace 和后续架构实验 |

## 当前实现与目标架构的映射

| Canvas Runtime 责任 | POC-03 当前状态 | 目标边界 |
| --- | --- | --- |
| Document 业务模型、稳定 ID、Operation | 自己实现，`Document::Apply` 是唯一写入口 | 保持由 Canvas Runtime 自己拥有，不交给 SkSG/Chromium |
| Runtime `Scene` facade | 主要由 `RuntimeScene`/compiler harness 表达 | 由 `Scene` + `SceneBinding` 对外统一，内部持有 RenderScene、SpatialIndex、DamageTracker |
| Render Scene Graph | direct Skia/最小 Render Tree 与 logical passes | 优先用 SkSG 作为内部 Render DAG；SkSG 不进入 Document/Bridge/Shell |
| Object Spatial Index | SoA + deterministic uniform grid/局部索引 | RF-02 `DynamicRTreeSpatialIndex`，必要时再评估 Hybrid |
| Damage | world-space invalidation、hints fault injection、L1 invalidate | Runtime `DamageTracker` 对外稳定；SkSG invalidation 只能是内部实现 |
| Tile/LOD | L1 `TileCache` 接口/行为原型，直接绘制为主 | 自己实现 TileGrid、TilingSet、TileManager、Priority、Prefetch、RasterTaskScheduler、Budget/Eviction |
| Raster/GPU | Skia Ganesh | 继续使用 Ganesh；Graphite/WebGPU 只作为未来 backend |
| Chromium cc | 未作为依赖 | 只借鉴 tiling、priority、raster task 和 eviction 思想，不链接源码 |

## Skia SkSG 的使用边界

SkSG 的 Node/DAG observation、bounds/revalidation、Group、Transform、Draw、CustomRenderNode
和精确几何 hit test 很适合作为 `SkSGRenderScene` 的内部实现。`sksg::Scene` 自身只提供
渲染型 root facade，不拥有我们的 Document、Operation、协作、资源 manifest 或无限世界
对象查询，因此不能直接 `using Scene = sksg::Scene`。

推荐的依赖方向是：

```text
Document / SceneDelta
        ↓
    SceneBinding
        ↓
Runtime Scene facade
   ┌────┼───────────────┐
   ↓    ↓               ↓
RenderScene  ISpatialIndex  DamageTracker
   ↓
 SkSG DAG → Skia Ganesh
```

HitTest 固定为 SpatialIndex 候选查询→Geometry/SkSG 精确命中两阶段。这样即使后续替换
SkSG 或改变 Tile renderer，上层 selection、eraser 和 semantic query 也不需要重写。

## 动态空间索引与无限世界

Skia `SkRTree` 的 bulk-load 语义适合一次性记录，不适合 Canvas 的 drag、insert、remove、
update 生命周期。Runtime 的 `ISpatialIndex` 必须支持动态操作，并要求：

- bounds、候选顺序和 z-order 结果可确定重放；
- 负坐标、极大有限坐标、空/退化 bounds 和 checked overflow 有明确结果；
- 单对象更新的访问量可观测，不允许把全量扫描藏在接口后；
- brute-force oracle 与空间索引结果逐字节等价。

Chromium cc 的 tile 空间假设不能直接当作无限 Canvas 坐标模型。Canvas 的 `TileKey` 使用
有符号 `int64` x/y 与显式 level；world→tile 取整和越界策略在 RF-03 之前冻结。

## Tile、LOD 与调度路线

Tile 不保存对象，只缓存一个 world rect 在指定 content/raster revision 的 raster 结果。
生产路线按以下顺序推进：

1. RF-01：Scene facade、SkSG 私有 Render DAG、DamageTracker 和两阶段 HitTest。
2. RF-02：动态 R-tree、viewport culling、Selection/Eraser query、负坐标和退化 bounds。
3. RF-03：TileGrid、TilingSet/LOD、TilePriority、IRasterSource、TileManager、可取消的
   RasterTaskScheduler、Prefetch、MemoryBudget 和 Eviction。

Tile priority 至少包括 Visible、NearViewport、Prefetch、Background。调度必须记录 queue
age、cancel/drop、raster duration、tile content/raster version、memory bytes 和 eviction
reason；预取受 viewport movement 和 soft/hard budget 约束。清除缓存、resize、device loss
或 Skia context 重建后，Scene/Document digest 必须不变且可重新栅格化。

## Windows 失败后的分析动作

在修复或调整门禁前，下一次 Windows/Web/Android 真实验证必须分段记录：

1. candidate query、precise hit、Scene/RenderTree build、Damage/Tile invalidation；
2. raster task 排队、Skia draw/flush、GPU submit、present interval 和 missed presentation；
3. Runtime/Scene/Tile/Skia GPU/decoded resource/transient/surface 各自内存；
4. warm-up、shader/pipeline、窗口 resize、pan/zoom/write action cycle 和 thermal 状态。

如果动态索引降低 query 与 build 的长尾但 present 仍超时，应进入 RF-03 的 Tile/LOD/scheduler
实验；如果 raster 已短而 presentation 超时，应单独分析 D3D12 swapchain/driver/vsync，不应
把业务 Scene 继续复杂化。每个修复都必须保持 full/incremental digest、视觉等价和 Android
16 KiB 对齐门禁，不得用基准设备特例绕过共享 Runtime。

## 暂不冻结的内容

本审查不冻结具体 R-tree 分裂参数、tile size、LOD 数量、prefetch skew、线程拓扑、Skia
GPU cache budget、L2/L3 持久格式或 Graphite/WebGPU。它们分别要以 RF-02/RF-03 的 trace、
内存、正确性和跨平台证据进入后续实验 ADR。
