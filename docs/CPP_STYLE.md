# Canvas C++ / C ABI 代码风格规范

> Status: **Accepted**；适用范围：Canvas Runtime、公开 C ABI、SDK wrapper、测试和工具。
> 风格来源：Mostorm Labs 的 AXTP C++ 规范。该外部规范用于本次决策依据，但仓库文档不
> 依赖开发者本机路径；本页完整复制了 Canvas 所需规则，也不把 `axtp` namespace、AXTP
> 前缀或协议命名引入 Canvas。

## 1. 总体规则

- C++20；4 spaces；100 columns；K&R braces；禁止 tabs。
- `clang-format` 负责空白、缩进、括号、换行和 include 分组；review/lint 负责 identifier
  naming、文件命名、依赖和 ownership。
- 根目录 [`.clang-format`](../.clang-format) 是规范性格式配置；R1 增加 format/check-format
  脚本，扫描 `include`、`src`、`tests` 和 `tools`，排除 generated/third-party/build。
- public C++ header 不泄漏平台、Skia、网络、数据库、线程库或 C++ ABI；public C header
  只使用固定宽度 C 类型、指针、长度、enum、opaque handle 和 caller-provided buffers。
- ownership 必须显式。优先 RAII、`std::unique_ptr` 单一 owning、`std::shared_ptr` 只用于
  明确共享生命周期；borrowed pointer/reference 必须在命名或注释中说明有效期。
- Runtime 默认 ManualPoll/single-owner；core 不创建 thread、不做 socket I/O、不拥有平台
  surface、不跨 public ABI 抛异常。

## 2. Namespace、类型和函数命名

Canvas C++ symbol 位于 `namespace canvas` 或更具体子 namespace；不要用全局 `Canvas` 前缀
替代 namespace。内部 pipeline 类型不重复模块前缀，例如：

```cpp
namespace canvas {

class Runtime;
class SceneCompiler;
class DamageTracker;
class ISpatialIndex;

}  // namespace canvas
```

- C++ class/struct/type：UpperCamelCase，interface 以 `I` 开头，例如 `IRasterSource`。
- C++ 函数和方法：lowerCamelCase，例如 `applyRemoteOperations()`、`requestFrame()`。
- C++ enum class value：`kValue`，例如 `FrameDecision::kUrgent`。
- 常量：`kConstantName`；宏只用于 ABI/export/compile feature，使用 `CANVAS_*`。
- Public C ABI：类型 `Canvas<Type>`，函数 `canvas_<domain>_<verb>`，enum value
  `kCanvas<Type><Value>`；不能把 AXTP 的 `Axtp` 前缀或 generated protocol enum 规则复制进来。
- Private/protected non-static member：leading underscore + lowerCamelCase，例如
  `_sceneCompiler`、`_targetGeneration`。不使用 `member_`、`fMember`、`gGlobal`、`__member`。
- 局部变量和参数也使用 lowerCamelCase，与 AXTP C++ 习惯保持一致，例如
  `documentRevision`、`targetGeneration`；C ABI 的 struct field 和函数参数按 C 约定使用
  lower_snake_case。不要在同一 C++ 模块中混用两种局部命名。

## 3. 文件和目录命名

新 C++ 文件使用 lower_snake_case：

```text
include/canvas/runtime/runtime.hpp
include/canvas/scene/scene.hpp
src/runtime/runtime.cpp
src/scene/scene_compiler.cpp
tests/runtime/runtime_test.cpp
```

新 C ABI 头使用 lower_snake_case `.h`，因为它必须被 C、ObjC、JNI bridge 和 WASM 编译器
直接包含：

```text
include/canvas/runtime.h
include/canvas/types.h
include/canvas/input.h
include/canvas/document.h
```

目录也使用 lower_snake_case；不要创建 `CanvasRuntime.h`、`AxtpCore.hpp` 或 `RenderScene.hpp`。
现有 POC/第三方/generated 文件可以保留原名，迁移时由独立 change 完成，不在无关改动中批量
重命名。

## 4. Include 和依赖

使用完整 module include path，并按：对应 header、标准库、Canvas public header、Canvas internal
header、third-party 分组。public header 禁止 include `windows.h`、`UIKit`、`jni.h`、
`emscripten`、Skia private header、socket、SQLite、Boost、Qt 或 pthread。

```cpp
#include "canvas/scene/scene.hpp"

#include <memory>
#include <vector>

#include "canvas/geometry/rect.hpp"
#include "canvas/scene/spatial_index.hpp"
```

Platform adapter 可以包含平台库；这些依赖不能通过 Runtime public header 泄漏。Skia 只允许
在 renderer implementation 或明确的 internal adapter header 出现。

## 5. ABI、ownership 和 callback style

- 所有 extensible C struct 首字段为 `struct_size`、`abi_version`；新增字段只追加到尾部。
- extensible struct 不按值嵌入另一个仍有后续字段的 extensible struct；使用 borrowed pointer，
  或把嵌套值放在外层尾部并为兼容性单独审查。Runtime 构造的只读 event 使用固定 header +
  `event_size`，不冒充调用方输入 struct。
- C API 长度使用 `uint64_t`，不使用 `size_t` 作为跨 32/64 位 ABI 的 wire size；文本统一
  UTF-8 + explicit length，不假设 NUL。
- C API 不返回由 Runtime 分配、要求 Shell 释放的字符串；输出由调用方 buffer 提供，采用
  query-size-then-fill 两步法。
- 输入 span 和 callback payload 都是 borrowed；callback 返回后失效，必须复制才能异步保存。
- callback target/user_data 非 owning；Runtime destroy 不释放调用者对象。
- 导出函数捕获所有 C++ exception 并转换为 `CanvasStatus`；callback 禁止重入同一 Runtime。
- 句柄是 generation handle，0 无效；每个句柄 domain 独立，销毁后访问必须返回错误。

## 6. Control Path 与 Native Hot Path

Control Path 可以经过 C++ wrapper、JNI/ObjC++、React Native command、Qt/QML 或 WASM host：

```text
setTool / setBrush / undo / openDocument / executeCommand / getSelection
```

Native Hot Path 必须保持 native→C++→GPU：

```text
WM_POINTER / MotionEvent / Pencil / PointerEvent
  → PointerSampleBatch
  → InputRouter / StrokeSession
  → Preview / FrameScheduler / RenderTarget
```

Hot Path 禁止逐 sample 经过 RN JS、React state、QML signal 或高频 JSON；Web 可由 native/WASM
adapter 在模块内批量传递 coalesced events。当前 POC/R1 不承诺 C ABI 的多线程安全；调用方必须
遵守单一 owner thread 和明确的 frame callback 时序。

## 7. Review checklist

- public header 是否仍只依赖固定 C 类型/Canvas public types？
- 是否把 Document/Operation 的持久语义与 RuntimeScene/Tile/GPU/cache 状态混在一起？
- 是否有完整 struct prefix、无易碎的 extensible-by-value 嵌套、enum 数值、长度检查、
  generation handle 和错误传播？
- callback 是否说明 borrowed/owning、线程、重入和生命周期？
- 是否把平台 handle、Skia object、网络 URL、token、路径或 STL 暴露出 ABI？
- 是否有 Control Path/HOT Path 误用、逐 sample bridge 或无界队列？
- 是否有跨端 contract test、digest/replay、lifecycle、sanitizer 和 `git diff --check` 证据？
