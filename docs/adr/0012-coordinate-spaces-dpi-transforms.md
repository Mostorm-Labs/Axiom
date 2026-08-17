# ADR-0012: 坐标空间、DPI 与变换契约

- Status: Accepted
- Date: 2026-08-17
- Related stages: POC-02～05, R1～R3

## Context

Document geometry、Viewport、PointerSample、HitTest、Selection、ExternalSurface
placement 和 GPU raster 分别使用语义坐标、视图逻辑坐标和设备像素。仅在 fixture
中固定 DPI 不能约束 zoom、高 DPI、多视口、平台 overlay 或输入逆变换；若模块隐式
假设不同单位，跨平台结果会出现稳定但错误的偏移、缩放和舍入差异。

## Decision

Canvas 定义以下概念空间，所有跨模块数据必须声明所属空间：

1. **Node Local Space**：节点自身几何空间。
2. **Page/World Space**：Page 内的语义文档空间。1 world unit 是抽象文档单位，不是
   设备像素或物理长度；x 向右、y 向下。
3. **View Logical Space**：某个 EditorSession/View 的逻辑像素空间；Web 对应 CSS
   pixel，native 对应 logical pixel/DIP/point。
4. **Device Pixel Space**：RenderTarget 的物理像素空间。
5. **Platform Screen Space**：平台窗口/屏幕坐标，只允许出现在 adapter/overlay 边界。

概念变换顺序固定为：

```text
Node Local
  → node/world transform
Page/World
  → viewport transform
View Logical
  → target DPR/device transform
Device Pixel
  → platform placement transform（仅 adapter）
Platform Screen
```

对一个 local point，组合语义固定为
`p_world = node_to_world(p_local)`、
`p_view = world_to_view(p_world)`、
`p_device = view_to_device(p_view)`；逆变换严格按相反顺序执行。矩阵在 ABI/文件中的
row/column memory layout 必须显式版本化，但任何实现都不能通过隐式转置改变上述组合语义。

- Viewport transform 由 `EditorSession` 持有，包含 pan/zoom，并带 `ViewId` 与单调
  `viewport_revision`。RenderTarget 提供 dimensions、DPR、color space、orientation/
  target transform 和 generation；DPR 不进入 Document。
- Platform PointerAdapter 先把 OS client/screen coordinates 规范化为 View Logical
  coordinates，再使用明确的 viewport snapshot 逆变换为 Page/World coordinates。
  输入 batch 必须关联 ViewId 与 viewport revision；不能用之后发生变化的 Viewport
  重新解释历史样本。
- Document/Canonical Stroke 保存 Page/World 语义几何。View/Device/Screen coordinates、
  DPR、像素舍入和平台窗口位置不进入 Document digest。
- HitTest 的公共语义输入是 Page/World point 与 tolerance；平台 touch slop/pen radius
  先从 View Logical 转换为 world tolerance。返回结果包含稳定节点 ID 和必要的 local
  geometry 信息。
- ExternalSurfaceRecord 只保存 world bounds/clip/opacity/pass 与 ExternalSurfaceId。
  FrameBuilder 生成 View/Device placement，Platform Surface Registry 最后转换到平台
  screen/view 坐标。
- 几何和矩阵在过滤、clip、hit-test 与 raster 完成前保持连续值。像素取整只在明确的
  raster/overlay adapter 边界发生；矩形采用 half-open extent 语义。矩阵序列化布局和
  数值精度由 R2 schema 决定，但不得改变上述变换顺序。

## Consequences

- 多视口、minimap、Retina/高 DPI、resize 和平台 overlay 使用同一变换链。
- Pointer replay 除 samples 外还要记录 ViewId、viewport revision/transform 和 target
  DPR；单纯记录 OS coordinates 不足以确定性回放。
- Cache、dirty region 和 golden metadata 必须区分 world、view 与 device space。
- CSS transform、Android window inset、Windows scaling 和 CAMetalLayer drawable size
  只能由平台 adapter 映射，不能成为 Document 特例。

## Validation

POC-02 覆盖 zoom/pan 期间的批量笔输入和 viewport revision；POC-03 用两个不同
Viewport/DPR 验证 culling、hit-test、dirty 和 cache 隔离；POC-05 验证 world→device→
platform overlay placement 误差；POC-04 验证 caret/selection geometry。语料至少覆盖
DPR 1/1.25/1.5/2/3、负 world coordinates、非整数 zoom、resize、旋转 target（平台
支持时）和逆变换不可用错误。
