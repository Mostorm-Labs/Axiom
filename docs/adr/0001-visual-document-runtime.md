# ADR-0001: Canvas v2 是 Visual Document Runtime

- Status: Accepted
- Date: 2026-08-16
- Related stages: POC-01～06, R1～R5

## Context

目标能力包含结构化文档、RichText、多类 Ink、搜索、Comment、外部 surface、多视口、协作和持久化。把项目定义为“Skia Renderer”或“白板绘图库”会让 Document、Editor、Text、Ink 与 Cache 继续以 renderer 特例增长。

## Decision

Canvas v2 定义为 C++20 Visual Document Runtime。Runtime 正式拥有 Document、Operations、EditorSession、RichText、InkEngine、SceneCompiler、RuntimeScene、Layout、Geometry、HitTest、FrameGraph、Compositor、TileCache、Resources 和 Persistence。

Skia 是可封装的 GFX backend。Product Shell 负责业务 UI、账户、分享、导航和平台服务，不拥有核心文档语义。

## Consequences

- 需要比单一 renderer 更严格的模块和状态边界。
- V1 功能面必须受控，但类型和 extension boundary 需支撑长期能力。
- Runtime 可服务 Web、Windows、Android、ChromiumOS 和 headless target。
- 功能不能通过在 Shell 中复制 Document 模型快速实现。

## Validation

六个 POC 分别验证共享 Runtime、Ink、100K Scene、RichText、Hybrid Surface 和 FastInk。若多个目标平台无法共享 Document/Operations/Scene 语义，必须新增 ADR 重新评估边界。
