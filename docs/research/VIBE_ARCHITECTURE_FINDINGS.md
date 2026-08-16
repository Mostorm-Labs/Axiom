# Vibe 架构研究结论

> 状态：Research Input；用途：解释 Canvas v2 架构决策的证据来源，不作为第三方实现规格；证据范围：早期 Android `libcanvas.so`、FastDraw `libvibe.so`、现代 Web `canvas.wasm`、Android APK native libraries 与产品功能观察

本文汇总前期二进制分析中与 Canvas v2 直接相关的结论，并严格区分“二进制中可直接观察的证据”“由证据组合得到的高置信推断”和“本项目采用的设计”。原始二进制尚未进入本仓库，因此正式实现前应把 hash、符号/字符串提取脚本和最小复现结果作为独立研究资产归档。

## 1. 证据等级

- **明确证据**：动态依赖、导出/导入符号、JNI signature、字符串、源码路径泄漏或系统调用可直接观察。
- **高置信推断**：多个独立证据共同支持，但缺少完整源码、Java 调用端或运行追踪。
- **架构结论**：Canvas v2 根据证据和自身产品目标作出的决定，不声称 Vibe 内部实现完全相同。

## 2. 早期 Android `libcanvas.so`

### 明确证据

- ARM64 Android native library，链接 EGL/GLES 与 Android 基础库。
- 包含 SkCanvas、SkPath、SkSurface、SkParagraph、SkShaper 及 Ganesh/OpenGL 类符号；存在 Skia M89 时代标识。
- JNI 生命周期包含 surface created/changed/draw frame、touch event 和 GPU task 回调。
- 能观察到 Document/Page、Protobuf event、Selection、Viewport、Text、Path、Lasso、Magnet、ActiveDrawPath 等语义。
- 存在 `PathRendererProxy` 线程相关符号和 active draw path 状态。
- JNI 查找/调用 `startFastDraw`、`disableFastDraw`、SmartTouch 和 touch history 接口。
- 平台路径指向 Android render service，并包含 Rockchip SoC 检测。
- 未观察到该库直接依赖 DRM、HWC、SurfaceFlinger 私有库或 framebuffer ioctl。

### 高置信推断

- Java/Android 层拥有 EGL context 与 surface，C++ 用 Skia 包装当前 framebuffer。
- 普通画布路径是 `C++ Canvas Engine → Skia Ganesh → GLES → Android Surface`。
- 输入进入 ActiveDrawPath 后分为 Canonical Renderer 和 FastDraw Preview 两条路径。
- Canvas Core 只通过 platform service 请求 FastDraw，最终直接送显实现位于其他组件。

## 3. FastDraw `libvibe.so`

### 明确证据

- 导出 FastDrawService、FastDrawCanvas、FrameBuffer、FrameBufferDRM 和 TouchInput。
- JNI 包含 service start/stop、draw frame、pointer properties、transform、clip region 和 board bounds。
- TouchInput 扫描 `/dev/hidraw0`～`/dev/hidraw7`，读取 raw HID report，并包含特定硬件 ID/Rockchip fallback。
- RK3288 路径存在 framebuffer、ION 和自定义 `VIBE_FBIOSET_*` ioctl。
- RK3399 路径打开 DRM card，创建/map dumb buffer、`drmModeAddFB2`、set plane 和 atomic commit。
- FastDrawCanvas 获得像素 buffer/rowBytes，并出现 SkCanvas、clip、stroke、eraser 和 drawn-points 管理。
- 存在独立 Touch thread 与 Worker thread。
- DRM 路径包含 plane reservation/position 更新和 4K display 使用较低分辨率 preview buffer 的迹象。

### 高置信推断

- FastDraw 同时缩短输入链与显示链：Raw HID 绕过普通 Android event dispatch，mapped scanout buffer/hardware plane 绕过普通 Surface 合成。
- Preview 主要使用低开销 Skia raster 写入映射 buffer；价值来自删除调度/合成中间层，不是单纯增加 GPU。
- `libcanvas.so → Java platform service → libvibe.so` 形成解耦链，两份 native library 不需要直接链接。
- Preview quality 可以低于 Canonical quality；正式画布稍后以完整质量接管。

## 4. 现代 Web `canvas.wasm`

### 明确证据

- 存在 C++ Canvas Runtime、Skia/Ganesh/WebGL 和 WASM 平台适配迹象。
- 能观察到 StreamPath、PixelDraw、hybrid/pixel engine、VectorBrush、BrushDrawable 和 PersistentRecorder 等名称。
- 能观察到 RONodeTree、FrameGraph、Compositor、GPUBackend 以及 L1/L2/L3 tile cache 相关名称。
- 平台层包含 render/event/io/persistent-storage 等 Web/WASM service。
- 构建具备 pthread/shared-memory 能力迹象。

### 高置信推断

- 现代实现是早期 C++ Canvas Engine 的持续演进，而不是 Web/JavaScript 重写。
- Stroke 模型已经从单一 vector path 扩展到 vector、pixel/dab 和 hybrid 表示。
- Scene/FrameGraph/Compositor/多级缓存已成为大规模、多效果画布的核心架构。
- 平台 Host 可由 Android 切换到 Web/WASM，而 Document/Editor/Renderer 能力继续共享。

## 5. Android Shell 与现代产品形态

### 明确证据

- APK 库组合包含 React Native、JSI、Yoga、Hermes/JSC executor、fbjni、folly 等典型组件。
- 同一 APK 同时包含图片、PDF/Chromium 和 WebRTC 相关 native libraries。
- 现代 Canvas 功能观察包含选择、套索、多种笔刷、RichText、Section、Table、Comment、对象变换、页面、分享/导出和 viewport controls。

### 高置信推断

- 早期 Android 产品使用 React Native 作为 Product Shell，`libcanvas.so` 作为 Native Canvas Engine，`libvibe.so` 作为系统 FastDraw。
- React Native 没有承载高频 Canvas 数据面；Native View/JNI 是合理边界。
- 现代产品更接近 Infinite Canvas Workspace / Visual Document Editor，而不是只画笔迹的白板。
- 持续可复用的资产是 C++ Canvas Runtime 与平台 abstraction，不是某一个 UI framework。

## 6. 对 Canvas v2 的直接结论

### 6.1 Runtime，而不是 Renderer

项目定义为 Visual Document Runtime。Skia 只是 GFX backend；Document、EditorSession、InkEngine、RichText、SceneCompiler、FrameGraph、Cache 和 Persistence 都是一等模块。

### 6.2 Shell 可替换

- Web 使用 React/TypeScript + WASM。
- Windows 使用 React/Tauri + native canvas region。
- Android 使用 React Native + Native CanvasView/JNI。
- 高频 pen path、RichText model 和 Document operations 不进入 JS 数据面。

### 6.3 Semantic Document 与 RuntimeScene 分离

Table、Comment、Search metadata、permissions 和 collaboration state 不必直接参与 render。Document 先经 SceneCompiler 转成 RuntimeScene，后者再进入 FrameGraph/Compositor。

### 6.4 Ink 双路径

Pointer 输入同时驱动：

- Preview Stroke → FastInkBridge → Platform FastInk。
- Canonical Stroke → Operation → Document → RuntimeScene → Skia。

两条路径共享 Stroke ID 和笔刷语义，但拥有不同的质量、延迟和生命周期。

### 6.5 RichText 前置

RichText/IME 是架构风险，不是后期 UI 功能。TextDocument、TextEditSession、TextInputAdapter 和 SkParagraph 边界必须在 POC-04 验证。

### 6.6 FrameGraph 与 Cache 接口前置

不需要第一天完成 L1/L2/L3，但必须先固定 pass、dirty、tile 和 persistent store 接口，避免大场景或外部 surface 到来时重写 Renderer。

### 6.7 先单线程建立 Oracle

现代 WASM 可以使用 pthread，但 POC 应先用单线程得到确定性 digest、golden 和性能参考，再以数据决定 Scene/Render/IO/Cache worker。

## 7. 不应直接复制的内容

- 不复制旧 Skia 版本、旧 Android GLSurfaceView 生命周期或具体 JNI signature。
- 不把 RK3288 ION/自定义 framebuffer ioctl 作为现代设备默认方案。
- 不假定所有 Android/Windows 设备都有系统 FastInk 权限。
- 不因早期产品使用 React Native 而追求所有平台共用一套 UI。
- 不把二进制名称或推断出的内部类当作 Canvas v2 的兼容 API。

## 8. 后续证据工作

若原始资产进入研究环境，应补充：

1. 文件 hash、架构、动态依赖和可复现的 strings/symbol extraction。
2. JNI/导出 API 表和调用关系图。
3. FastDraw 输入、buffer、plane 与 handoff 的运行追踪。
4. `canvas.wasm` module/string/source-map 证据索引。
5. “明确证据/推断”逐条复核；推翻时更新研究文档和关联 ADR。

研究结论为架构提供方向，不替代 POC-01～06 的本项目实测。
