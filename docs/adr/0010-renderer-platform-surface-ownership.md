# ADR-0010: RendererBackend 与 Platform Surface 所有权分离

- Status: Accepted
- Date: 2026-08-17
- Related stages: POC-01, POC-03, R1, R3

## Context

Canvas Renderer 属于共享 C++ Runtime，但 HTML Canvas/WebGL context、HWND/DXGI
swapchain、Android Surface/ANativeWindow/EGLSurface、CAMetalLayer/Metal drawable
和 Headless surface 具有平台专属的创建、resize、present、前后台与丢失恢复生命周期。
若 RendererBackend 直接拥有这些对象，平台类型会进入 Runtime Core，并让 surface 生命周期、
Document/View 生命周期和 GPU context ownership 相互耦合。

## Decision

- `PlatformSurfaceAdapter` 位于平台集成边界，拥有 native window/view、surface/context、
  swapchain/drawable 的 acquire、resize、present、recover 和销毁。
- Adapter 为每帧提供带 dimensions、DPR、color space、backend capability 和 generation 的
  `RenderTarget`。具体 native 类型只存在于平台实现；公共 Runtime 契约只使用不透明、
  受 generation 约束的 target 表示。
- `RendererBackend` 属于 Runtime 渲染能力，消费不可变 frame plan 与有效 RenderTarget，
  使用 Ganesh 绘制，但不缓存、present 或销毁 native window/view/surface handle。
- resize、context/device loss 或 drawable replacement 使旧 target generation 失效；
  Runtime 必须从 RuntimeScene/frame state 重建输出，不改变 Document。

## Consequences

- Web、Windows、Android、Apple 与 Headless 可以独立实现 surface 生命周期，同时共享
  Scene、FrameGraph、Compositor 和 RendererBackend 语义。
- Platform adapter 与 Renderer 需要明确 acquire/render/present 时序、错误、generation、
  thread/context ownership 和 recovery contract。
- POC-01 的平台 adapter 是实验性证据，不因通过验证而成为产品 ABI；R1 重新冻结接口。
- RendererBackend 的未来 Ganesh→Graphite 迁移不要求 Shell 改写 Document 或 Editor。

## Validation

POC-01 验证各平台 surface bring-up、readback、100 次生命周期和 context/device failure；
POC-03 验证 frame plan/target generation 与 cache invalidation；R1 module dependency test
禁止 Runtime 公共头包含 native window/view/surface 类型；R3 验证 resize、前后台、device
loss、surface replacement 和 Canonical redraw。
