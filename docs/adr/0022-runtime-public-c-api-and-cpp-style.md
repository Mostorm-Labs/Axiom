# ADR-0022：Runtime Public C ABI 与 Canvas C++ 风格

- Status: **Accepted**
- Date: 2026-08-19
- Related stages: R1、R2、R3；POC-01～03 仅作为 ABI 设计证据
- Clarifies: ADR-0002、ADR-0010、ADR-0012、ADR-0015、ADR-0017、ADR-0020、ADR-0021

## Context

Canvas Runtime 需要被 Web/WASM、Windows、Android/JNI、macOS/iOS/iPadOS ObjC++、Qt 或其它
未来 Shell 消费。若 Shell 直接看到 Document、RuntimeScene、Skia 或平台 Surface，
Document/Scene 分离、Platform Surface ownership 和 Native Hot Path 会逐步失效。POC-01
已有 `canvas_poc_*` C ABI，但该 ABI 明确是 Experimental，不能直接 rename 为产品接口。
同时，Runtime、SDK wrapper、平台 adapter 和测试需要一个统一的 C++ 命名/文件/include/所有权
纪律，避免不同 POC 产生不兼容的 public headers。

## Decision

### 1. 唯一公共边界是稳定 C ABI

R1 以 [Runtime Public C API Contract](../api/RUNTIME_C_API_CONTRACT.md) 和规范性签名清单
[canvas_runtime_api_v1.h](../api/canvas_runtime_api_v1.h) 作为公共契约。C++20 Runtime 的
内部类可以自由重构；C++ wrapper、JNI、ObjC++、WASM 和 Shell binding 都围绕 C ABI 适配。

公共 API 只暴露：

- Runtime/Document/View/Surface opaque generation handles；
- fixed-width scalar、UTF-8 explicit-length string、byte span、caller-provided output buffer；
- `struct_size + abi_version` 可扩展输入；
- numeric status、thread-local detailed error 和 borrowed synchronous events；
- Document open/snapshot/remote Operation port、View/camera/surface、PointerSampleBatch、
  Tool/Brush/Eraser、Command/Undo/Redo、IME/keyboard/wheel、VSync/render、Resource provider
  和 diagnostics。

公共 API 不暴露 Document/Scene/Tile/SpatialIndex/FrameGraph/Skia/GPU/platform object pointer，
不拥有 network/storage/message loop/thread，也不在 Runtime 中解析 OAuth/token/server URL。

### 2. Control Path 与 Native Hot Path 分离

Control Path 可以经由 JNI/ObjC++/RN command/Qt/QML/WASM wrapper 进入，承担 openDocument、
setTool、setBrush、executeCommand、undo/redo 和查询。Native Hot Path 固定为：

```text
Platform Pointer / VSync
    → PointerSampleBatch / FrameTiming
    → InputRouter / FrameScheduler
    → Preview / Scene / Renderer
    → Platform Surface
```

Native CanvasView、WM_POINTER、UIKit Pencil 和 Web adapter 必须批量进入 C++；逐 sample
React Native JS、QML signal、高频 JSON 或网络 callback 进入 Runtime 是架构违规。POC/R1
默认 single-owner thread，不能假设 C ABI 可并发调用；线程拓扑在后续实验 ADR 冻结。

### 3. ABI identity 和 versioning

- ABI v1 的导出函数使用 `canvas_<domain>_<verb>`，C 类型使用 `Canvas<Type>`，enum 常量
  使用 `kCanvas<Type><Value>`，handle domain 独立且 0 无效。
- 每个可扩展 struct 的前缀为 `uint32_t struct_size; uint32_t abi_version;`；只能追加尾字段。
- 可扩展 struct 不按值嵌入另一个仍有后续字段的可扩展 struct；nested config/open data 使用
  borrowed pointer。Runtime 构造的只读 event 使用固定 header + `event_size`。
- 高频数组使用 `count + stride`，不在每个 sample 重复 prefix；所有跨 ABI size 使用 `uint64_t`。
- Status enum 数值和已发布字段语义不可重排；异常不得越过 ABI；borrowed span/event 只在
  同步调用或 callback 生命周期内有效。
- Runtime API/ABI、Operation schema、Snapshot schema、Sync protocol、Renderer/Cache schema
  分别版本化，不互相替代。

### 4. Persistence、Sync、Resource 是 ports

Persistence 和 Sync 通过 Snapshot/Operation/Resource/Event port 连接，不能成为 Document
或 Renderer 的实现细节：

```text
Local Operation
    ├→ Document apply
    ├→ LocalOperationCommitted → Persistence WAL
    └→ LocalOperationCommitted → Sync offline queue
```

Runtime 只接收已验证的 snapshot/continuation、remote Operation batch 和 resource bytes；
IndexedDB/OPFS、SQLite/File、HTTP/WebSocket、ACK/retry、OAuth 和 blob GC 均在外部 adapter。
Operation durability 从 AppliedLocally 到 ServerAcknowledged 是独立诊断状态，不改变
Document digest，不等于 Document revision。

### 5. Canvas C++ style

[Canvas C++ / C ABI 代码风格](../CPP_STYLE.md) 固定 4 spaces、100 columns、K&R braces、
lower_snake_case 文件名、`.hpp/.cpp` C++ 文件、`.h` C ABI 文件、`namespace canvas`、
lowerCamelCase C++ 函数、`kValue` enum、private `_member`、完整 module include path、
public header dependency isolation、显式 ownership 和异常转换。根目录 `.clang-format` 是
规范配置。AXTP 的规则只作为来源参考，不引入其 namespace 或协议 symbols。

## Consequences

- Web/Windows/Android/Apple/Qt 可以共享同一 C ABI；Shell 变化集中在 Input/Surface/Bridge/
  Storage/Network adapter，不改变 Document/Scene/Ink/Text 语义。
- 新增 API 可以保持 ABI v1 的前缀和尾字段兼容；改变已有所有权、同步回调或字段语义必须
  新 ABI major 和迁移说明。
- 规范性 header 先落在 docs 作为 R1 输入；当前 POC 继续使用 `canvas_poc_*`，不因文档冻结
  而伪造实现或承诺 ABI 兼容。
- 由于 C ABI 不暴露 Tile/Skia/Scene，RF-01～RF-03 可以替换 RenderScene、DynamicRTree、
  TileManager 和 RasterScheduler 而不影响 Shell。

## Validation

R1 必须：

1. 将规范性 header 拆入 `include/canvas/*.h` 并在 C、C++、ObjC++、JNI/WASM wrapper 下编译。
2. 对每个 handle domain、struct prefix、short/oversized buffer、unknown enum/capability、
   stale generation handle、NaN/Infinity、callback lifetime/reentrancy 和 exception translation
   运行 contract tests。
3. 在 Web/WASM、Windows、Android、macOS/iOS/iPadOS 运行同一 symbol/struct/enum manifest
   检查；core/public ABI 变更同时运行 Tier A + Tier B。
4. 运行 `clang-format` check、C/C++ public-header self-containment、module dependency、
   `git diff --check` 和 sanitizer/lifecycle tests。
