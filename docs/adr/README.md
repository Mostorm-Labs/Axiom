# Canvas v2 架构决策记录

ADR 记录会长期影响 Runtime 边界、平台集成、兼容性或性能演进的决定。已接受 ADR 是实现约束；实验型 ADR 必须在对应阶段实现前关闭。

## 已接受决策

| ADR | 状态 | 决策 |
| --- | --- | --- |
| [0001](0001-visual-document-runtime.md) | Accepted | 项目定义为 C++20 Visual Document Runtime |
| [0002](0002-replaceable-platform-shells.md) | Accepted | React Web、React/Tauri Windows、React Native Android Shell |
| [0003](0003-semantic-document-runtime-scene.md) | Accepted | Semantic Document 与 RuntimeScene 分离 |
| [0004](0004-dual-path-ink-fastink.md) | Accepted | Canonical 与 FastInk Preview 双路径 |
| [0005](0005-skia-ganesh-v1.md) | Accepted | V1 使用 Skia Ganesh，Graphite 仅作未来 backend |
| [0006](0006-richtext-first-class.md) | Accepted | RichText/IME 是一级 Runtime 子系统 |
| [0007](0007-cache-interfaces-from-v1.md) | Accepted | Raster/Tile/TileStore 接口从 V1 存在 |
| [0008](0008-single-thread-poc-baseline.md) | Accepted | POC 先单线程，线程拓扑由数据决定 |
| [0009](0009-prebuilt-skia-sdk-supply-chain.md) | Accepted | Skia 以不可变、可验证的预编译 SDK 供普通构建消费 |

## 必须后续建立的实验型 ADR

| 建议主题 | 阻断阶段 | 必须提供的证据 |
| --- | --- | --- |
| 文档快照、操作日志与 migration 格式 | R2 | round-trip、损坏输入、规模、演进与恢复测试 |
| Collaboration MVP 算法与协议 | R4 | 冲突语料、100K 随机 operations、断网/重连 |
| L2/L3 cache 格式与压缩 | 实现 L2/L3 前 | 命中收益、IO/内存、版本失效、设备数据 |
| 产品线程拓扑与 WASM pthread | 引入 worker 前 | profiling、所有权、revision 失效和回归语料 |
| 复杂 RichText 并发语义 | V1 MVP 后 | 字符级冲突、IME、undo intention 与收敛测试 |
| 系统级 FastInk target | 设备产品化前 | 硬件/BSP、权限、光电延迟、plane/fallback 测试 |

## ADR 格式

每份 ADR 使用以下字段：

- Status、Date、Related stages。
- Context：要解决的问题与硬约束。
- Decision：可执行且边界明确的决定。
- Consequences：代价、限制和后续工作。
- Validation：支持决定的证据与重新评估触发条件。

修改已接受决定时新增 ADR 并标注 `Supersedes`；不得静默改写历史。
