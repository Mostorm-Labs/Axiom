# Canvas v2 架构决策记录

ADR 记录会长期影响 Runtime 边界、平台集成、兼容性或性能演进的决定。已接受 ADR 是实现约束；实验型 ADR 必须在对应阶段实现前关闭。

## 已接受决策

| ADR | 状态 | 决策 |
| --- | --- | --- |
| [0001](0001-visual-document-runtime.md) | Accepted | 项目定义为 C++20 Visual Document Runtime |
| [0002](0002-replaceable-platform-shells.md) | Accepted | 当前 Tier A Shell 选择；长期边界由 ADR-0015 澄清 |
| [0003](0003-semantic-document-runtime-scene.md) | Accepted | Semantic Document 与 RuntimeScene 分离 |
| [0004](0004-dual-path-ink-fastink.md) | Accepted | Canonical 与 FastInk Preview 双路径 |
| [0005](0005-skia-ganesh-v1.md) | Accepted | V1 使用 Skia Ganesh，Graphite 仅作未来 backend |
| [0006](0006-richtext-first-class.md) | Accepted | RichText/IME 是一级 Runtime 子系统 |
| [0007](0007-cache-interfaces-from-v1.md) | Accepted | Raster/Tile/TileStore 接口从 V1 存在 |
| [0008](0008-single-thread-poc-baseline.md) | Accepted | POC 先单线程，线程拓扑由数据决定 |
| [0009](0009-prebuilt-skia-sdk-supply-chain.md) | Accepted | Skia 以不可变、可验证的预编译 SDK 供普通构建消费 |
| [0010](0010-renderer-platform-surface-ownership.md) | Accepted | RendererBackend 与 Platform Surface 生命周期分离 |
| [0011](0011-shared-preview-model-fastink-sinks.md) | Accepted | Default/FastInk sink 消费共享 Preview Model |
| [0012](0012-coordinate-spaces-dpi-transforms.md) | Accepted | 坐标空间、DPI、输入逆变换和 overlay placement 使用统一契约 |
| [0013](0013-resource-identity-content-addressing.md) | Accepted | ResourceId、ContentHash、Manifest 和 blob identity 分离 |
| [0014](0014-history-undo-compensating-operations.md) | Accepted | Undo/Redo 通过新的补偿 Operations 进入唯一写路径 |
| [0015](0015-platform-support-tiers-shell-policy.md) | Accepted | Tier A 产品、Tier B 可移植性、Reuse 和 Headless 责任分级 |
| [0016](0016-numeric-geometry-determinism.md) | Accepted | canonical 数值存储、舍入、非法值和摘要编码遵循跨平台确定性契约 |
| [0017](0017-platform-frame-scheduling.md) | Accepted | Runtime invalidation 与平台 VSync/frame scheduler 分离 |
| [0018](0018-input-backpressure-coalescing.md) | Accepted | confirmed input、Preview 更新与帧请求采用分级背压/合并规则 |
| [0019](0019-semantic-changes-invalidation-hints.md) | Accepted | ChangeSet 的语义变化与非权威失效提示分离 |
| [0020](0020-document-snapshot-operation-recovery.md) | Accepted | DocumentSnapshot + committed Operation continuation 确定性恢复目标 Document |
| [0021](0021-render-scene-spatial-index-tiling-boundaries.md) | Accepted | Runtime Scene facade、SkSG 内部 Render DAG、动态空间索引、DamageTracker 与 Tile/LOD 所有权边界 |
| [0022](0022-runtime-public-c-api-and-cpp-style.md) | Accepted | 产品 Runtime 使用稳定 C ABI；固定 Control/Hot Path、ports、ABI versioning 与 Canvas C++ 风格 |
| [0023](0023-hybrid-surface-controlled-overlays.md) | Accepted | Web、Windows RNW、Android RN、Apple RN/Fabric 统一采用受控 Overlay；固定 z-order 与 native hot path |

## 必须后续建立的实验型 ADR

| 建议主题 | 阻断阶段 | 必须提供的证据 |
| --- | --- | --- |
| DocumentSnapshot、Operation Log 与 migration 具体格式 | R2 | 遵循 ADR-0020 的 round-trip、损坏输入、规模、演进与恢复测试；不重新决定恢复语义或将 Runtime revision 等同 RecoveryFrontier |
| Collaboration MVP 算法与协议 | R4 | 冲突语料、100K 随机 operations、断网/重连 |
| L2/L3 cache 格式与压缩 | 实现 L2/L3 前 | 命中收益、IO/内存、版本失效、CacheSchema/Renderer/Skia SDK/backend compatibility namespace、设备数据 |
| 产品线程拓扑与 WASM pthread | 引入 worker 前 | profiling、所有权、revision 失效和回归语料 |
| 复杂 RichText 并发语义 | V1 MVP 后 | 字符级冲突、IME、undo intention 与收敛测试 |
| 系统级 FastInk target | 设备产品化前 | 硬件/BSP、权限、光电延迟、plane/fallback 测试 |
| DocumentRoot、单 Page/多 Page schema | R2 | 产品模型、导出/坐标语义、迁移与兼容测试 |
| Headless 公共产品 API | 产品化前 | server/batch/export 用例、安全、资源预算、稳定性和兼容性 |
| Entity/Operation/Actor ID 与 stable order/z-order schema | R2；协作扩展在 R4 | domain/collision/replay、离线创建、中间插入、并发排序、迁移和收敛语料 |
| V1 color/image canonicalization | R2/R3 | sRGB/P3 产品范围、ICC/EXIF、派生 decode metadata identity、export/golden 与跨平台 codec 语料；不改变 ADR-0013 blob ContentHash |

## ADR 格式

每份 ADR 使用以下字段：

- Status、Date、Related stages，以及适用时的 `Clarifies`/`Supersedes`。
- Context：要解决的问题与硬约束。
- Decision：可执行且边界明确的决定。
- Consequences：代价、限制和后续工作。
- Validation：支持决定的证据与重新评估触发条件。

修改已接受决定时新增 ADR 并标注 `Supersedes`；不得静默改写历史。
