# ADR-0021：Render Scene、空间索引与分层 Tile 边界

- Status: **Accepted**
- Date: 2026-08-19
- Related stages: POC-03、POC-04、POC-05、R3
- Clarifies: [ADR-0003](0003-semantic-document-runtime-scene.md)、[ADR-0005](0005-skia-ganesh-v1.md)、[ADR-0007](0007-cache-interfaces-from-v1.md)

## Context

POC-03 已经证明 Document→RuntimeScene、局部更新、统一网格查询、FrameGraph 和 L1
TileCache 可以在跨平台目标上保持正确性，但它不是生产级大场景渲染方案。最新 Windows
D3D12 Integrated Performance Playground 证据在同一物理设备上两次未达到 p95 ≤ 16.7 ms、
p99 ≤ 33.3 ms（p95/p99 分别为 26.998/38.737 ms 和 25.693/37.457 ms）。正确性、digest、
视觉等价性和候选上限均通过，因此不能把失败归因于语义错误，也不能简单以“机器性能”
结案。当前实现仍有大范围候选查询、直接绘制和仅 L1 原型等实验性特征；生产路径需要
明确 Scene facade、Render DAG、动态对象空间索引、damage 传播、分层 Tile/LOD 和 raster
调度的所有权。

Skia SkSG 适合承担渲染 DAG 的节点观察、bounds、invalidation、组合和精确几何命中等
内部机制，但 `sksg::Scene` 不是 Document、协作或无限画布 Query API。Skia `SkRTree`
适合一次性 bulk-load，不满足 Canvas 拖动、新增和删除对象的动态索引需求。Chromium cc
的 tiling、priority、raster task 和 eviction 设计有参考价值，但它的坐标假设、依赖树和
资源策略不能直接成为 Canvas Runtime 依赖，尤其不能覆盖无限世界的负坐标。

## Decision

### 1. Runtime Scene facade 与 Render Scene 分离

Runtime 对外定义自己的 `Scene` 门面；Shell、EditorSession 和 Document 不接触 SkSG 类型。
门面最小语义为：

```cpp
class Scene {
public:
    void apply(const SceneDelta&);
    QueryResult query(const SceneQuery&) const;
    HitTestResult hitTest(const PointD& worldPoint) const;
    DamageSet revalidate();
    void render(SkCanvas*, const RenderContext&);

private:
    std::unique_ptr<RenderScene> renderScene_;
    std::unique_ptr<ISpatialIndex> spatialIndex_;
    DamageTracker damageTracker_;
};
```

`SceneBinding` 把 Document/SceneDelta 投影到这个门面。首个 `RenderScene` 实现可以在内部
使用 SkSG Render DAG（Group、Transform、Draw、CustomRenderNode 等），但 SkSG 只是可替换
实现细节，不是公共 Runtime API，也不成为 Document Model。复杂业务节点、稳定 ID、事务、
语义关系和持久化始终归 Canvas Runtime。

### 2. 空间索引由 Runtime 拥有，并分阶段升级

对象索引使用自己的动态接口：

```cpp
class ISpatialIndex {
public:
    virtual void insert(ObjectId, const RectD&) = 0;
    virtual void remove(ObjectId) = 0;
    virtual void update(ObjectId, const RectD& oldBounds,
                        const RectD& newBounds) = 0;
    virtual void query(const RectD&, QueryResult&) const = 0;
};
```

- POC-03 只承诺 `LinearSpatialIndex`/确定性 uniform-grid 实验实现，用于证明 Scene
  正确性、增量等价性和跨端效果；它不是生产性能结论。
- RF-02（可与 POC-04 RichText/IME 并行）实现 `DynamicRTreeSpatialIndex`，覆盖新增、删除、移动、viewport culling、
  Selection/Eraser/HitTest 查询，并用 brute-force oracle 验证结果等价。
- 后续如出现超大对象、Stroke 专用查询或分区需求，再以实验 ADR 评估 `HybridSpatialIndex`。

Skia `SkRTree` 不直接实现这个生产接口，因为其 bulk-load 语义不能表达逐对象生命周期。
HitTest 固定为两阶段：SpatialIndex 先返回可能命中的候选，SkSG/Geometry 再执行精确命中
和 z-order 选择；任何阶段都不得把全量遍历伪装成生产查询。

### 3. DamageTracker 是 Runtime API，SkSG invalidation 只能是内部实现

`DamageTracker` 对外产出 world-space `DamageSet`，并负责把旧/新 bounds、语义 ChangeSet、
viewport 变化和资源替换归一化为可重算的失效记录。POC-03 可以在其内部封装 SkSG
`InvalidationController`；后续 Tile 阶段必须能在不改变公共 API 的情况下替换或扩展为
对象 damage→TileKey→raster invalidation。SkSG 的 dirty region 不得泄漏为 Document、
Operation 或跨 View 的共享状态。

### 4. Tile/LOD/Raster 调度由 Runtime 自己实现，算法参考 Chromium cc

POC-03 只保留接口和 L1 行为原型。POC-05/R3 承担以下产品级组件：

```cpp
struct TileKey { int64_t x; int64_t y; uint8_t level; };

class TileManager {
public:
    void updateViewport(const ViewportState&);
    void invalidate(const DamageSet&);
    void prepareTiles(const FrameContext&);
    void onMemoryPressure(MemoryPressure);
    TileSet visibleTiles() const;
};

class IRasterSource {
public:
    virtual void raster(SkCanvas&, const RectD& worldRect,
                        const RasterContext&) = 0;
};
```

Tile 只缓存某一 world rect 在某一 content/raster revision 的结果，不拥有 Canvas Object。
`TileGrid` 负责 world→tile 映射，`TilingSet` 管理多个 raster level/LOD，`TilePriority` 至少
区分 Visible、NearViewport、Prefetch、Background，`RasterTaskScheduler`、MemoryBudget 和
Eviction 协调有限资源。Chromium `PictureLayerTiling`、`TileManager`、`TilePriority` 和
`RasterSource` 只作为源码级设计参考，不作为依赖引入。

Canvas world space 必须支持负坐标和极大有限坐标；TileKey 使用有符号坐标并定义溢出/取整
规则，不能直接继承只支持正空间的 tiling 假设。Prefetch 也必须绑定 viewport velocity、
memory budget 和可取消 task，不能把预取变成无界缓存。

## Consequences

- POC-03 的性能失败会被记录为生产渲染路线未完成的架构风险，而不是降低既有门禁或修改
  正确性结果；POC-03 继续保持 `Validating`。
- 上层 Document/Operations/EditorSession 不会被 SkSG 或 Tile 生命周期反向污染；RuntimeScene
  仍然可以从 Document 全量重建。
- POC-04/05 需要新的 benchmark、memory attribution、negative-world 和 full/incremental
  equivalence 语料；在这些证据出现前，不能声称 10K/100K 生产级线性性能。
- Tile cache、Skia GPU cache 和 decoded resource 仍由 `ResourceBudgetCoordinator` 统一观测，
  具体 L2/L3 格式和 eviction 参数继续由后续实验 ADR 决定。
- 直接 Skia 绘制仍是 POC-03 的有效 baseline；这不会阻止未来用 SkSG、Tile 或其它
  `RendererBackend` 实现替换内部路径。

## Validation

本 ADR 的关闭/修订需要同时提供：

1. Scene facade 与 SkSG 私有实现的 module/dependency 检查，证明 SkSG 类型没有进入
   Document、Bridge 或产品 Shell。
2. POC-04 动态索引与 brute-force 在增删改、负坐标、退化 bounds、HitTest 和 Selection
   语料上逐字节等价，并证明局部更新不扫描无关对象。
3. POC-05/R3 的 TileGrid、LOD、priority、prefetch、raster scheduling、memory pressure
   和 eviction trace；Tile 清空或 device loss 后从 Document/Scene 可重建且 digest 不变。
4. Windows、Web、Android 真实设备在固定 1K/10K/50K/100K Playground 上的 frame/input/
   memory evidence。绝对门禁仍由 [POC-03 计划](../planning/STAGED_DELIVERY_PLAN.md)规定，
   未通过不得将 POC-03 标记为 `Accepted`。
