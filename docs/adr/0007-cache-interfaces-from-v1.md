# ADR-0007: 缓存接口从 V1 存在，能力分级实现

- Status: Accepted
- Date: 2026-08-16
- Related stages: POC-03, R1, R3

## Context

100K scene、多视口、复杂 Ink、图片和外部内容需要明确的 raster/tile 失效边界。把缓存完全推迟会让 Renderer 与 Scene 数据结构按“每帧全画”固化；第一天实现完整多级缓存又会过度设计。

## Decision

从 POC-03 起定义三层接口：

- `RasterCache`：对象/subtree 的可丢弃 raster。
- `TileCache`：L1 GPU 与可选 L2 RAM tile。
- `TileStore`：可选 L3 SSD/eMMC persistent tile。

POC-03 只实现 L1 原型，R3 产品化 L1。L2/L3 只有 profiling 证明收益且独立 ADR 接受后实现。cache key 必须包含内容 revision、render parameters、scale bucket、color space 和 backend capability。

## Consequences

- FrameGraph/Compositor 从一开始具备正确 cache/dirty 边界。
- 未知或不兼容 key 一律 miss，不允许陈旧画面。
- L2/L3 不成为 V1 发布的隐含范围。
- 需要 cache clear/device loss 与 full render 等价测试。

## Validation

POC-03 验证 100K scene 下 L1 收益、预算和失效。若 L1 无可测收益，可以保留接口并降低实现复杂度；若实现 L2/L3，必须先提交命中、IO、内存和版本数据。
