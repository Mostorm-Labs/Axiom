# Canvas 项目交接文档

> 文档版本：2026-08-01
>
> 状态快照：2026-08-01 17:51（Asia/Shanghai）。这是一次性快照；接手时以
> `git fetch`、`git status`、GitHub PR/Actions API 的实时结果为准，不要因为本文件中的
> SHA、测试数量或“最新运行”字样而跳过重新核对。
>
> 仓库：Mostorm-Labs/canvas（https://github.com/Mostorm-Labs/canvas）
>
> 本文用于把当前 Canvas 白板工程交给新的账号/开发者继续维护。文档中的“已验证”只表示有真实的本地或 GitHub 运行记录；没有 Windows 触控硬件、AppKit 真实窗口或多人网络环境证据的部分，均明确标为 pending。

如果本文件与源码、`git worktree` 状态、GitHub PR 或 Actions 结果冲突，实时结果优先；先更新状态快照，再开始实现。本文故意把历史绿色运行、本地未推送提交和未提交 WIP 分开描述，避免把“曾经通过”误读成“当前提交已通过”。

## 0. 先看结论

Canvas 当前不是一个已经完成的商业化多人协作产品，而是一个以 C++/Skia 原生渲染为核心的跨平台白板垂直切片：

- Windows 原生路径已经具备共享文档模型、Skia + D3D12/DirectComposition 涂写、Win32 pen/touch 输入、WebView2 网页/富文本/视频承载、命名管道 IPC、Electron 控制和 Windows portable 发布流程。当前远端头 `87ebf01` 已推送，但最新 Windows CI run `30694257905` 的完整 CTest 有 4 个 WebView2 初始 `data:` 导航失败；这不是 runner/billing 阻断，WebView2 修复仍需继续。
- macOS Apple Silicon 路径已有 AppKit + CAMetalLayer + Skia Ganesh 的真实首帧渲染，并在本地 Task 19 WIP 中实现固定三层宿主（Base Metal → embedded NSView container → Overlay Metal）。Task 19 的实现尚未提交/推送，但本机最新验证为 full CTest 123/123、composition integration 2/2、重复运行 20 次通过；WKWebView、输入适配和 Electron 仍未接入。
- EmbeddedLoadBatch 和 EmbeddedLoadCompletionInbox 已在本地完成平台无关的异步加载基础，但两个提交尚未推送到 GitHub，也尚未接入 WhiteboardApp。
- 当前没有多人协作网络同步、CRDT/OT、账号/权限、房间/Presence、服务端，也没有 Android/iOS 实现。
- 当前没有可宣称的 i5-1235U 触控屏“肉眼跟手”或 p95 < 50 ms 测量。这个指标必须在真实 Windows 触控设备上用高速摄像机测量。

接手时最重要的顺序是：

1. 先定位 `87ebf01` 在 Windows WebView2 runtime 上触发的 `E_INVALIDARG (0x80070057)`，让 PR #1 的完整 CI 变绿。
2. 在保留 Task 19 WIP 的前提下，将 macOS 分支 rebase 到最新 Windows 基线，提交并复审固定三层宿主（Base / embedded / overlay）。
3. 复核并推送两个异步加载基础提交。
4. 将它们接入 WhiteboardApp 的原子文档加载事务，再实现 embedded-state、Electron 严格事件转发和 macOS WKWebView/输入。
5. 最后才做真实 Windows 设备、Electron GUI、触控延迟和 Release 验收。

## 1. 仓库、远端和工作树事实

远端地址：

~~~text
origin = git@github.com:Mostorm-Labs/canvas.git
~~~

截至本文日期，远端主要引用为：

| 引用 | 短 SHA | 完整 SHA | 状态 |
|---|---|---|---|
| origin/main | cd445fc | `cd445fc4d24b849944958a6b108187727023d520` | 初始空壳基线 |
| origin/codex/windows-vertical-slice | 87ebf01 | `87ebf01d7afeb0c5f559d9bbfa5191359b084a92` | Windows PR #1 的远端头，已推送；最新 CI 失败 |
| origin/codex/macos-platform | 672ef32 | `672ef321bb662fdd215c04e49ed555eb6e36b6b9` | macOS PR #2 的远端头；本地已有更后的未推送提交 |

本机 worktree：

| 路径 | 分支 / HEAD | 远端情况 | 当前状态 |
|---|---|---|---|
| /Users/qing/Documents/myself/projects/canvas-task16 | codex/windows-vertical-slice / `87ebf01d7afeb0c5f559d9bbfa5191359b084a92` + 当前修复 WIP | 跟踪 origin/codex/windows-vertical-slice；WIP 尚未提交 | Windows runtime 修复代理正在修改 `webview2_surface.cpp`、`tests/CMakeLists.txt`，另有一个 source contract 未跟踪；本交接文档必须独立提交，不能混入该修复 |
| /Users/qing/Documents/myself/projects/canvas-macos | codex/macos-platform / `f856aac1c4b7ad767f5c2785a706730495b7d52b` | 比远端 672ef32 多 1 个提交 | 有未提交 Task 19 RED 测试及对应三层宿主生产实现；详见下表和第 5.2 节 |
| /Users/qing/Documents/myself/projects/canvas-embedded-batch | codex/embedded-load-batch / `e6148a29f75152e82a36479f6136171687c15601` | 没有远端分支 | clean，本地-only |
| /Users/qing/Documents/myself/projects/canvas-completion-inbox | codex/embedded-completion-inbox / `025817394a4e662159bef85b749280efa530f7b6` | 没有远端分支 | clean，本地-only |
| /Users/qing/Documents/myself/projects/canvas-atomic-open | codex/atomic-document-open / `025817394a4e662159bef85b749280efa530f7b6` | 没有远端分支 | 只是从 Inbox 基线建立的空 worktree，尚无 atomic-open 实现 |

不要把 /Users/qing/Documents/myself 当作 Canvas Git 根目录；Canvas 的 Git 根目录是上表中的各个 projects/canvas-* worktree。

接手第一步建议执行：

~~~bash
cd /Users/qing/Documents/myself/projects/canvas-task16
git fetch --all --prune
git worktree list
git status --short --branch
git diff --name-only
git ls-files --others --exclude-standard
git log --graph --oneline --decorate --all --max-count=40
~~~

当前 Windows worktree 的未提交文件（修复代理可能继续变化）为：

~~~text
src/platform/windows/webview2_surface.cpp            (runtime fix WIP)
tests/CMakeLists.txt                                  (runtime fix WIP)
tests/scripts/webview2_navigation_source_contract_test.cmake (runtime fix WIP, untracked)
handoff.md                                           (本交接文档，untracked)
~~~

原先未提交的 Windows URL canonicalization、串行导航和 COM 重入改动已由
`87ebf01 fix: serialize WebView2 navigation startup` 作为一个 scoped commit 提交并推送。
该提交修改/新增 10 个文件，包括 `webview2_surface.cpp`、initial-load/message seams、
`webview2_navigation_uri.h` 及相应单元/集成测试；不要再按旧快照寻找这些已提交文件。
当前代理在此基础上尝试让不可用的 WebView2 `SourceChanged` source 观察降级为可继续导航，
并新增 source contract；这些改动仍是 WIP，不能被本交接文档覆盖、回滚或混入文档 commit。

当前 macOS Task 19 的未提交文件为：

~~~text
CMakeLists.txt                                      (tracked modification)
app/macos/main.mm                                  (tracked modification)
src/platform/macos/metal_host.h                    (tracked modification)
src/platform/macos/metal_host.mm                   (tracked modification)
src/platform/macos/metal_view.h                    (tracked modification)
src/platform/macos/metal_view.mm                   (tracked modification)
src/platform/macos/skia_frame_plan.h               (tracked modification)
tests/CMakeLists.txt                               (tracked modification)
tests/contracts/macos_skia_host_contract_test.cmake (tracked modification)
tests/unit/macos_skia_frame_plan_test.cpp           (tracked modification)
docs/tdd/task-19-macos-layer-stack-red.txt          (untracked)
src/platform/macos/composition_view.h               (untracked)
src/platform/macos/composition_view.mm              (untracked)
tests/contracts/macos_composition_host_contract_test.cmake (untracked)
tests/integration/macos_composition_layer_stack_test.mm     (untracked)
~~~

两个列表都是本地工作状态，不在远端。不要使用 git reset --hard、git checkout -- 或清理未跟踪文件来“整理”工作区。

> **跨机器交接警告：**另一个账号如果只从 GitHub clone，只能看到远端的 87ebf01 和 672ef32。它看不到 f856aac、e6148a2、0258173，也看不到 macOS Task 19 的未提交/未跟踪 WIP。离开当前机器前必须先执行第 6 节“阶段 A”的 bundle + patch + archive 备份，或者经过评审后把相应分支推到远端。

三个本地-only 提交对应的证据文件也只存在于各自分支：
`f856aac1c4b7ad767f5c2785a706730495b7d52b` 的 macOS 证据在
`docs/tdd/task-16-macos-*.txt`，`e6148a2` 的证据在
`docs/tdd/task-17-embedded-load-batch-*.txt`，`0258173` 的证据在
`docs/tdd/task-18-embedded-load-completion-inbox-*.txt`。仅从 Windows 远端分支查看
`docs/tdd/` 不会看到这些文件。Windows `87ebf01` 已在远端，不再依赖本机 patch 才能恢复；
但它当前的失败 CI 运行和诊断结论仍应由接手者通过 GitHub Actions URL 重新核对。


## 2. GitHub PR、CI 和 Release 状态

### PR #1：Windows

- PR：Build the Windows whiteboard vertical slice（https://github.com/Mostorm-Labs/canvas/pull/1）
- 分支：codex/windows-vertical-slice → main
- 状态：Draft / Open / `MERGEABLE` 但 check 不稳定（截至快照）
- 远端头：`87ebf01d7afeb0c5f559d9bbfa5191359b084a92`
- 最新运行（截至快照，2026-08-01 17:42–17:45 Asia/Shanghai）：Windows Build run 30694257905（https://github.com/Mostorm-Labs/canvas/actions/runs/30694257905）
- 失败 build job：https://github.com/Mostorm-Labs/canvas/actions/runs/30694257905/job/91354264780

最新运行的结果（head SHA 为 `87ebf01d7afeb0c5f559d9bbfa5191359b084a92`）：

- Configure：通过
- Build：通过
- CTest：196 个测试，192 通过、4 失败（98%）
- 失败测试：`InitialDataNavigationCompletesReadyExactlyOnce`、`InitialLoadTracksTheSupersedingNavigationOnly`、`SerialNavigationKeepsOnlyTheLatestRequestAndDoesNotStarveAfterFragment`、`HostsContentBelowInkAndGatesSyntheticClicksByMode`
- 失败表现：四个测试都在初始 `data:` 导航阶段进入 `State::Failed` / `InitialLoadState::Failed`，底层 HRESULT 为 `E_INVALIDARG (0x80070057)`；第一个测试 completion 结果为 `-2147024809` 且非预期 `Ready`，第二个 completion 次数为 0，后两个 surface state 为 Failed。
- runner 正常分配并完成 Configure、Build；这不是 Actions billing/spending blocker。需要定位 WebView2 实际 data URI 与 canonicalization/导航调用的交互，不应只重跑旧提交。
- Composition integration、release packaging、artifact 和 whitespace 步骤因为完整 CTest 失败而没有继续执行

这次运行确实分配到了 `windows-2022` runner；失败不是 Actions runner/billing 阻断，而是
CTest 的产品/集成测试失败。接手后不要据此跳过重新运行：修复提交必须用新的 `headSha`
重新验证。可以用下面的命令核对每个 job/step，而不是只看 PR 顶部图标：

~~~bash
gh run view 30694257905 --repo Mostorm-Labs/canvas --json jobs,headSha,conclusion,url
gh run view 30694257905 --repo Mostorm-Labs/canvas --job 91354264780 --log-failed
~~~

最近一个完整通过的 Windows 运行是 run 30160695952：
https://github.com/Mostorm-Labs/canvas/actions/runs/30160695952

当时为较早的 `80ba591c9c04d969e5b10c753804a73934671016`，记录为生产构建、完整 CTest 167/167、Composition 测试 10/10 和后续打包检查通过。它不能替代当前 `87ebf01` 的验证，只能作为回归参考。

该历史绿色运行的 build job 为
https://github.com/Mostorm-Labs/canvas/actions/runs/30160695952/job/89685423910，PR artifact 为
`canvas-windows-x64-pr-1-6107722f995c`：
https://github.com/Mostorm-Labs/canvas/actions/runs/30160695952/artifacts/8620179578。
截至本文快照，该 artifact 的 GitHub digest 为
`sha256:32d30e3db82af86449091f84cc9f7d81a2777c4a371f073df747808e52a9867d`，大小
3,155,463 bytes，预计于 2026-08-24 14:00 UTC 过期；过期后应重新触发当前提交的
workflow，不能把旧 ZIP 私下改名当成新版本。

### PR #2：macOS

- PR：Add the macOS native platform foundation（https://github.com/Mostorm-Labs/canvas/pull/2）
- 分支：codex/macos-platform → codex/windows-vertical-slice
- 状态：Draft / Open；远端显示 DIRTY/CONFLICTING，无 GitHub CI check
- 远端头：`672ef321bb662fdd215c04e49ed555eb6e36b6b9`
- GitHub 当前记录的 PR base SHA 仍是 80ba591；这也是它相对最新 Windows 头冲突的直接信号
- 本地已完成但尚未推送的提交：`f856aac1c4b7ad767f5c2785a706730495b7d52b` feat: render macOS whiteboard with Skia Metal
- 本地 Task 19 RED 也尚未提交

PR #2 必须等 Windows 新基线确定后处理。正确顺序是 fetch Windows 最新头，在 macOS worktree 保存/提交 RED 和实现提交，再 rebase 到 Windows 最新分支，解决 CMake/tests 冲突，重跑全量 macOS 测试，然后使用 push --force-with-lease 更新 PR #2。

### Windows Build / Release workflow

唯一 workflow 是 .github/workflows/windows-build.yml，名称为 Windows Build。触发条件：

- Pull request：构建、测试并上传 30 天保留的 portable artifact；
- workflow_dispatch：同上；
- 推送 v* tag：构建同一份经过测试的包，然后创建/更新 GitHub Release。

当前已有 Release：

- v0.1.0-alpha.1（https://github.com/Mostorm-Labs/canvas/releases/tag/v0.1.0-alpha.1）
- tag 指向 `ee06c803a96737bf6e45e674d6a61c8f611a5fcb`，是较早的 Windows 文档/发布基线，不包含后来的 WebView2 initial-load tracker、EmbeddedLoadBatch 或 Inbox。
- Windows ZIP： https://github.com/Mostorm-Labs/canvas/releases/download/v0.1.0-alpha.1/canvas-windows-x64-v0.1.0-alpha.1.zip
- SHA-256 文件： https://github.com/Mostorm-Labs/canvas/releases/download/v0.1.0-alpha.1/canvas-windows-x64-v0.1.0-alpha.1.zip.sha256
- Windows ZIP 内容 SHA-256：`d0cffd8114273c86ca6c987835cdb74b067099020010d59a0af53104572aeb86`
- 这是 unsigned native portable build，不包含 Electron launcher；运行时需要 Microsoft Edge WebView2 Runtime，并且 ZIP 解压后的相邻 web/ 目录不能删除。

PR artifact 名称按以下规则生成：

~~~text
canvas-windows-x64-pr-<PR_NUMBER>-<SHORT_SHA>.zip
canvas-windows-x64-pr-<PR_NUMBER>-<SHORT_SHA>.zip.sha256
~~~

这里的 `<SHORT_SHA>` 来自 workflow 的 `github.sha`。在 `pull_request` 事件中它通常是
GitHub 生成的 PR merge commit，而不一定等于分支 HEAD；下载后必须同时记录 run URL、
`headSha` 和 ZIP 内的版本名，不能只靠文件名判断源码版本。

只有完整 CTest 和 packaging contract 通过，artifact 步骤才会执行。不要把旧 Release 当成当前失败 PR 的产物。

交接后用下面的只读命令刷新本节；不要只看 PR 页面顶部的单个 check 图标：

~~~bash
gh pr view 1 --repo Mostorm-Labs/canvas \
  --json state,isDraft,headRefOid,statusCheckRollup,url
gh pr view 2 --repo Mostorm-Labs/canvas \
  --json state,isDraft,headRefOid,baseRefName,statusCheckRollup,url
gh run list --repo Mostorm-Labs/canvas --workflow "Windows Build" --limit 10
gh release view v0.1.0-alpha.1 --repo Mostorm-Labs/canvas
~~~

## 3. 已实现功能和代码地图

### 3.1 平台无关 C++ 核心

核心库在 canvas_core 中，主要入口：

| 路径 | 作用 |
|---|---|
| include/canvas/core/geometry.h | Vec2、Rect、变换和边界几何 |
| include/canvas/document/node.h / document.h | 版本化 Document、Base/Embedded/Annotation/Chrome 层、Stroke/Embedded/Unknown payload、父子附着 |
| src/document/document.cpp | 节点增删、边界校验、原子 bulk replacement、revision/cache identity |
| src/storage/document_codec.cpp | MessagePack/JSON 版本化编码解码；坏输入不得部分修改目标 Document |
| src/stroke/stroke_builder.cpp | 增量笔画、预测尾、真实采样替换、dirty bounds |
| src/input/input_router.cpp | Draw / Select / Interact 路由；pen 优先于 embedded，touch 可按配置绘制 |
| src/render/skia_renderer.cpp | Skia 光栅和 GPU layer 绘制；路径/块缓存、增量追加和 dirty rendering |
| src/embed/embedded_surface_manager.cpp | 可见 Video 与 active Web/RichText surface 的有限生命周期管理 |
| src/platform/frame_invalidation.h | 事件驱动帧合并、begin/complete/abandon/fail 状态；只在本地 macOS 分支 f856aac 及其后继中存在，不在 Windows 87ebf01 中 |

文档 schema 当前为 1。DocumentStore 的 Windows 保存路径使用临时文件刷新后原子替换；加载上限为 512 MiB。

### 3.2 Windows 原生渲染、输入和嵌入

Windows 生产目标由 canvas_windows_platform、app/windows 和 canvas_windows.exe 组成：

- src/platform/windows/dcomp_host.*：固定 DirectComposition visual 树和 back-to-front slot。
- src/platform/windows/skia_d3d12_context.*、skia_swap_chain_layer.*：Skia 148 D3D12 backend、swap chain、透明 annotation/chrome surface、dirty render。
- src/platform/windows/win_pointer_adapter.*、embedded_mouse_session.h：WM_POINTER pen/touch history、pressure/timestamp、capture 和 embedded mouse forwarding。
- src/platform/windows/webview2_surface.*：STA WebView2 composition controller、security/virtual host mapping、导航、message queue、input forwarding、close lifetime。
- src/platform/windows/webview2_navigation_uri.h：`87ebf01` 中新增的 Windows URLMon canonicalization seam；`data:` document identity 使用 COM-free 路径。
- src/platform/windows/webview2_media_source.h、webview2_video_restore.h：只允许经过批准的本地媒体路径；视频统一加载 web/video.html，不直接把本地媒体 URL 当导航 URL。
- app/windows/whiteboard_app.*：窗口、D3D/Composition layer 初始化、pointer hot path、IPC command dispatch、document save/open、embedded surface 创建。
- src/platform/windows/named_pipe_server.*：带 token 的 named-pipe 会话、连接 generation、旧连接响应隔离、有限队列和 shutdown。

当前固定层顺序是：

~~~text
Base canvas (opaque)
EmbeddedContent (WebView2)
Annotation (transparent ink)
InteractionChrome (transparent handles/selection)
~~~

命令行诊断入口：

~~~text
--self-test-layers
--self-test-embedded --video <approved-test-video.mp4>
--self-test-document --save <path>
--open <path>
--ipc-pipe <pipe-name> --session-token <token>
~~~

### 3.3 Web 资产和 Electron host

- web/ 是 Vite/TypeScript 资产，包含 richtext.html（Lexical）和 video.html。
- web/src/host-bridge.ts、richtext.ts、video.ts 有自己的 Node/Vitest 测试和消息边界。
- tools/electron-host/src/main.ts 负责生成一次性 token、启动 native 子进程、连接 named pipe、发送认证 hello、重连、队列 backpressure、优雅 shutdown。
- tools/electron-host/src/preload.ts 只暴露窄能力面，不把 token 或 ipcRenderer 暴露给 renderer。
- IPC 是版本 1、换行分隔 JSON；高频 pointer sample 必须留在 native hot path，不能改成每点走 Electron IPC。

当前控制通道的硬边界是：每行 JSON 最多 1 MiB、`requestId` 最多 256 个 UTF-8
字节、native outbound JSON 最深 32 层/最多 16384 个节点；Electron outbound 队列最多
256 条且总计 1 MiB，并为 shutdown 保留 512 字节。Electron 最多连接 20 次，退避从
50 ms 到 500 ms；这些数字是当前实现契约，不是网络协作协议的容量设计。

当前允许的 launcher command 包括 open-document、save-document、set-tool、set-mode、create-embedded、set-embedded-bounds、delete-node、interaction 切换和 shutdown。native event 类型已经在协议枚举中预留 ready、response、document-state、selection-changed、embedded-state、diagnostics、fatal-error，但并不代表每种事件都已经有完整 runtime 语义。

WebView 与 launcher 的安全边界也不能在后续迭代中绕开：`canvas.local` 只映射已打包
`web/`，本地视频只允许经批准的单文件 `media.canvas.local` 映射，普通远端内容只允许
HTTPS；Electron renderer 使用 `contextIsolation: true`、`nodeIntegration: false`、
`sandbox: true`，session token 只在主进程和 native 子进程中存在。

### 3.4 macOS 当前实现

macOS 代码位于 src/platform/macos/ 和 app/macos/。`f856aac` 提供单表面的 Skia/Metal
基础，当前未提交 Task 19 WIP 在其上补齐三层宿主：

- `CanvasCompositionView` 固定创建三个 back-to-front sibling：opaque Base `CanvasMetalView` → embedded `NSView` container → transparent Overlay `CanvasMetalView`，并保持三者 frame 同步。
- Base surface 白色清屏，只绘制 `LayerClass::Base`；Overlay surface 透明清屏，只绘制 `LayerClass::Annotation` 和 `LayerClass::Chrome`，不在原生层绘制 `Embedded`。
- 两个 CAMetalLayer 共享同一个 `MTLDevice`、`MTLCommandQueue`、Ganesh `GrDirectContext` 和 `SkiaRenderer`，同时各自保留 attachment generation、CAMetalLayer 和 `FrameInvalidation`。
- `MetalHost` 强制 AppKit 主线程，CAMetalDrawable texture 被包装为 Skia backend render target；Retina backing scale、事件驱动首帧、无 drawable 重试、resize、detach/reattach 均有本机测试覆盖。
- 默认 overlay 拥有 hit-test；显式开启 `embeddedInteractionEnabled` 后，当前实现把命中交给 embedded container。该策略符合 Task 19 RED，但还不是最终 PointerKind/实际 child 命中路由。
- macOS demo 已从单一 `CanvasMetalView` 切换到 `CanvasCompositionView`。
- 当前目标是 Apple Silicon arm64；没有真实 WKWebView child、pen/touch/mouse/IME 输入适配、Electron/native IPC 或 macOS Release workflow。Task 19 的生产文件仍是本地未提交 WIP，不在 PR #2 远端头 672ef32，也不在 Windows worktree 的 87ebf01 文件树中。

### 3.5 运行时数据流、线程和模块边界

Windows 的两个主要数据流必须保持分离：

~~~text
高频输入热路径
WM_POINTER / composition input
  -> WinPointerAdapter / EmbeddedMouseSession
  -> InputRouter
  -> StrokeBuilder / Document mutation
  -> SkiaRenderer + swap-chain invalidation/present

低频控制路径
Electron main process
  -> authenticated JSONL named pipe
  -> pipe worker bounded queue
  -> Win32 UI-thread drain
  -> WhiteboardApp command transaction
  -> Document / EmbeddedSurface / DComp mutation
  -> bounded native event back to the original connection generation
~~~

所有 Document、D3D12、DirectComposition 和 WebView2 对象都由 Windows UI/STA 路径
驱动；named-pipe worker 只能排队字节和命令，不能直接进入这些对象。WebView2 callback
可能在 STA 调用栈内同步重入，因此“同一线程”不代表“不会重入”。Electron 是独立进程，
renderer 只能经过 preload 的窄 API 到 main process；它既不拥有 native Document，也拿不到
session token。macOS 的 AppKit/MetalHost 则强制主线程，后续 WKWebView host 也必须保持
AppKit ownership。

| 边界 | 拥有的状态 | 不允许跨界的行为 |
|---|---|---|
| `canvas_core` | Document、geometry、stroke/input/render 算法、surface policy | 引入 Win32/AppKit/Electron 类型；把平台句柄写入持久文档 |
| Windows platform | COM、DComp/D3D12/Skia GPU、WebView2、Win32 pointer、DocumentStore | 从 pipe worker 或任意线程直接调用 UI/STA 对象 |
| `WhiteboardApp` | native window lifecycle、命令事务、可见 surface/document ownership | 把异步 controller/navigation admission 当成已 Ready；失败时部分替换 live state |
| IPC protocol/server | 认证、direction/schema/budget、连接 generation、bounded queue | 传逐点 pointer/stroke/video frame；让旧连接 response 泄漏到新连接 |
| Electron host/preload | 子进程、token、重连、低频 command/event 转发 | renderer 获得 token、raw `ipcRenderer`、Node 权限或无界队列 |
| Web assets | Lexical/video adapter 和结构化 host bridge | 直接读取本地任意路径；绕过 `canvas.local` / `media.canvas.local` policy |
| macOS platform | AppKit/CAMetalLayer/Metal/Ganesh host | 在非主线程操作 UI；让单 opaque surface 永久遮住未来 WKWebView |

持久化路径是 `Document` → versioned codec → Windows `DocumentStore` 的临时文件 + flush
+ atomic replace；embedded WebView、COM pointer、进程内媒体映射和 Electron token 都是
runtime-only，不能进入 `.canvas` schema。网络协作未来应位于 Document operation/同步层，
不能以“把 pointer event 广播给所有端”代替可重放、可合并的数据模型。

## 4. 已验证的测试和证据

### 4.1 任务台账（按当前 TDD 证据，不等同于产品完成度）

下表把仓库中已有的任务编号映射到可追溯证据。`历史 GREEN` 表示对应
checkpoint 曾经通过；它不表示当前 dirty worktree 或最新 PR head 仍然通过。

| 任务 | 交付边界 | 当前状态 / 证据 |
|---|---|---|
| 01 | 工具链、版本 smoke test | 已建立；`docs/tdd/task-01-bootstrap-red.txt` 记录 RED 起点，后续版本测试随基础套件回归 |
| 02 | 几何、Transform2D、pointer value types | 历史 GREEN；`task-02-geometry-green.txt` |
| 03 | 版本化 Document/node 模型和校验 | 历史 GREEN；`task-03-document-green.txt` |
| 04 | 增量 StrokeBuilder、预测尾替换/失效 | 历史 GREEN；`task-04-stroke-green.txt` |
| 05 | Draw/Select/Interact 输入路由 | 历史 GREEN；`task-05-router-green.txt` |
| 06 | Skia 光栅/dirty renderer | 实现已在核心库；本地历史记录明确 Skia 环境受限，权威验证在 Windows CI；`task-06-skia-green.txt` |
| 07 | 嵌入内容变换与边界 | 历史 GREEN（平台无关定向验证）；`task-07-transform-green.txt` |
| 08 | EmbeddedSurfaceManager 生命周期 | 历史 GREEN（管理器定向验证）；`task-08-surface-green.txt` |
| 09 | DirectComposition 固定四层树 | 实现完成，历史 Windows 验证记录为 pending 后由后续 CI 覆盖；`task-09-dcomp-green.txt` |
| 10 | Win32 pen/touch history 适配 | 实现完成；当时本机无 Windows SDK，需以 Windows CI 为权威；`task-10-pointer-green.txt` |
| 11 | D3D12 + Skia Ganesh 双 swap-chain 呈现 | 历史实现/CI 已验证；`task-11-green.txt` |
| 12 | WebView2 composition surface、策略、输入转发 | 历史静态/Windows CI 边界；当前初始导航回归修复仍在 WIP；`task-12-green.txt` |
| 13 | Lexical rich text、HTML video、WebView 资产和消息边界 | web 定向测试已 GREEN；真实 Windows WebView2/视频/IME 仍 pending；`task-13-green.txt` |
| 14 | Document JSON/MessagePack codec、atomic DocumentStore | 核心定向验证 GREEN；Win32 文件替换仍需 Windows 运行时证据；`task-14-green.txt` |
| 15 | Electron/native 控制、named pipe、Windows vertical slice | 历史 CI 有 GREEN checkpoint；当前 87ebf01 的 PR #1 为 196 中 4 个 WebView2 integration 失败，尚未收敛；见第 2、5 节和 `task-15-green.txt` |
| 16 | Windows portable artifact/tag Release；WebView2 initial-load 早期契约 | Release 已在 `v0.1.0-alpha.1` GREEN；87ebf01 的串行 superseding-navigation 修复已提交/推送但 runtime CI 失败；`task-16-release-green.txt` |
| 17 | EmbeddedLoadBatch 固定容量异步加载状态机 | 本地-only GREEN，提交 e6148a2，尚未推送/接入 WhiteboardApp；见下文 |
| 18 | EmbeddedLoadCompletionInbox 固定 ring、合并 wake、generation cancel | 本地-only GREEN，提交 0258173，尚未推送/接入 WhiteboardApp；见下文 |
| 19 | macOS Base / embedded / Overlay 三层宿主 | 本地生产实现及测试已完成但未提交/推送；full CTest 123/123、composition integration 2/2、重复 20 次通过，reviewer Conditional PASS；见第 5.2 节 |

任务台账中的“完成”只描述代码/测试切片，不承诺多人协作、签名发布、跨设备
输入或 50 ms 体验指标。接手者应打开对应 evidence 文件，确认命令、架构和基线 SHA。

### Windows（远端）

历史绿色 CI 已验证：MSVC configure/build、完整 CTest、Windows Composition 集成测试、WebView2 集成测试、Web 资产构建、portable package contract、whitespace。最新 `87ebf01` 不能宣称绿色，必须先修复上述 4 个失败测试并重新跑完整 workflow；旧绿色 run 不能替代当前 head 的证据。

历史证据文件：

- docs/tdd/task-15-green.txt：Windows vertical slice、D3D12/Skia、WebView2、IPC 的自动化结果和未完成项。
- docs/tdd/task-16-release-green.txt：portable artifact/tag Release 流程。
- docs/evidence/*.pending.md：明确记录尚未有真实 Windows touch/GUI/IME/video/latency 证据的项目。

### macOS

在本机 /Users/qing/Documents/myself/projects/canvas-macos，`f856aac` clean 基线及当前
Task 19 WIP 使用相同 macOS preset。标准命令为：

~~~bash
PATH=/opt/homebrew/bin:$PATH VCPKG_ROOT=/Users/qing/Documents/myself/projects/vcpkg VCPKG_OVERLAY_PORTS=/Users/qing/Documents/myself/projects/vcpkg-overlays cmake --preset macos-arm64
PATH=/opt/homebrew/bin:$PATH VCPKG_ROOT=/Users/qing/Documents/myself/projects/vcpkg VCPKG_OVERLAY_PORTS=/Users/qing/Documents/myself/projects/vcpkg-overlays cmake --build --preset macos-arm64-release --parallel
VCPKG_ROOT=/Users/qing/Documents/myself/projects/vcpkg ctest --preset macos-arm64-release
~~~

`f856aac` clean 基线结果是 119/119。当前 dirty Task 19 三层宿主 WIP 的最新结果是：

~~~text
full CTest: 123/123
canvas_macos_skia_frame_plan_test: 5/5
canvas_macos_composition_layer_stack_test: 2/2
canvas_macos_appkit_frame_scheduling_test: 1/1
macOS source/contracts: 全部通过
composition integration 连续重复 20 次: 全部通过
x86_64 Objective-C++ strict syntax: 通过
clang static analyzer: 无诊断
git diff --check: 通过
~~~

独立 reviewer 给出 Conditional PASS，无 P0/P1 blocker。这里的 123/123 是当前本机 WIP
验证，不是远端 PR #2 CI：PR #2 没有 GitHub macOS check，且 WIP 尚无提交 SHA。历史截图
证据仍在 `docs/tdd/task-16-macos-appkit-scheduling-green.txt`；截图文件本身可能已清理，
证据中记录的 SHA-256 `84431004a55ad42dfcd7eccb7a25ad9670b771cb34aa34fc768dd21002bdcb5d`
只是历史首帧证据，不是可下载的 macOS 安装包。

### EmbeddedLoadBatch（本地-only）

提交：e6148a2 feat: stage embedded document load batches。

- 固定最多 256 个 load；generation/token 非零且 token 不重复。
- completion 必须精确匹配 generation + token。
- 状态：Pending → Ready / Failed / Cancelled / Timeout，终态不可再改变。
- remaining() 的语义已写在 header 和 Task 17 evidence 中。
- 定向测试 15/15；portable/core 全量 138/138；严格 warning、format、diff-check 通过。
- GREEN 证据文件：`docs/tdd/task-17-embedded-load-batch-green.txt`，只存在于 `codex/embedded-load-batch` 及其后继分支。

### EmbeddedLoadCompletionInbox（本地-only）

提交：0258173 feat: queue embedded load completions，基于 e6148a2。

- std::array<Event, 256> 固定 ring FIFO，无动态存储。
- token/generation/outcome/failure code 严格校验。
- enqueue 返回是否需要发送一次 UI wake；consumeNotification、requestNotificationIfNeeded、notificationPostFailed 处理合并唤醒和 PostMessage 失败恢复。
- generation cancel 稳定压缩；overflow 饱和计数；clear 保留 wake/diagnostics。
- 定向测试 13/13；portable/core 全量 151/151；严格 warning、clang-format、diff-check 通过。
- 独立审查曾覆盖随机状态模型、reentrant enqueue/drain、wraparound、clear/cancel、overflow、PostMessage 失败恢复并 PASS；新账号仍应在接入前重新 review，因为审查报告没有单独落库。
- GREEN 证据文件：`docs/tdd/task-18-embedded-load-completion-inbox-green.txt`，只存在于 `codex/embedded-completion-inbox` 及 `codex/atomic-document-open`。

Task 17/18 的 138/138 和 151/151 都是 macOS portable/core 验证，不是 MSVC、
WebView2 或 WhiteboardApp 集成结果。把两个提交引入 Windows 分支后，必须重新跑 Windows
完整 CTest；不能用这些本地测试数字代替 Windows CI。

## 5. 当前未完成项和已知风险

### 5.1 必须先完成的 Windows WebView2 runtime 修复

原 WIP 已由 `87ebf01 fix: serialize WebView2 navigation startup` 提交并推送。代码级
独立 reviewer 在 CI 前给出 PASS、无剩余 P0/P1；portable seam 严格语法检查和
`git diff --check` 也通过。但是 GitHub Windows runtime 证据已经证明该提交仍有真实缺陷：
run 30694257905 的 Build 通过，完整 CTest 196 个中有 4 个失败，均在初始 `data:`
导航时得到 `E_INVALIDARG (0x80070057)`。

被否决的旧方向是“允许多个 outstanding `Navigate()`，再用事件 URI 反向查找 request
generation”。这个模型无法可靠区分 duplicate URI，也无法证明
`NavigationStarting` 顺序；旧 completion 可能终结新请求，旧事件还可能在新页面 Ready
后重写 active state。因此最终方向不再把 URI 当事件归属标识。

当前串行导航模型为：

~~~text
at most one issued native Navigate
  phase = Preparing -> Calling -> AwaitingStart

at most one deferred latest request
  every newer request replaces the deferred request

non-redirect NavigationStarting consumes the issued host admission
redirect NavigationStarting stays attached to the current navigation
only after the issued start is consumed may the driver issue deferred latest
~~~

对应实现中的主要状态名是 `IssuedNavigation`、`NativeNavigationPhase`、
`deferredNavigation`、`navigationDriveActive`、`navigationDriveRequested`、
`navigationStartDispatchBlocked`、`navigationMutationEpoch`、
`activeNavigationRevision` 和 `closeInProgress`。`InitialLoadTracker` 同时被收紧为最多一个
pending native generation。旧 URI→generation ledger/数组已从
`webview2_surface.cpp` 移除，`NavigationStarting` 不再靠 URI spelling 猜归属。

URI 现在只用于 navigation policy 和“可证明的 same-document”判定。原实现对 fragment
前 URI 做原始字符串比较，无法识别 scheme/host 大小写、默认端口、空路径、dot segment
或等价 percent encoding；而 same-document 导航可能不发 `NavigationStarting`，会让串行
driver 永久等待。`87ebf01` 的 `webview2_navigation_uri.h` 使用 Windows URLMon
`CreateUri` / `IUri::GetAbsoluteUri` 产生去 fragment 的 canonical document key，并有
Windows-only URI tests。pre-ready `data:` 初始页不能因为这项修改而开始依赖 URLMon/COM
初始化；canonicalization 只应在真实 WebView ready 后用于允许的 URI。

同步重入防护的目标是：每次 `get_Source`、event args getter、`Navigate`、
`put_Cancel`、`PostWebMessageAsJson` 或 cleanup 进入 WebView2 后，都重新验证 captured
WebView identity、surface state、request generation/lifetime epoch 和 active revision。
pending host message 使用 `takeValues()` 先 detach，再逐条调用 WebView2，避免回调清空或
追加原 vector 造成迭代器失效。close 必须在任何 COM cleanup 前设置 `closeInProgress`。

`87ebf01` 已包含 getter/`put_Cancel` 同步重入、same-document/no-start 消息冲刷和状态
传播的实现。下一修复不能回退这些不变量；独立审查至少要继续逐项确认：

- duplicate URI、A/A 和 A/B/C/D 连续请求都只有 issued + latest deferred，不靠 URI 匹配 generation；
- 每个可能同步重入的 COM getter/call 后都检查全局 mutation epoch 和 captured WebView identity；
- `SourceChanged` 即使 active navigation ID 为 0，也不能绕过全局 epoch；
- `NavigationCompleted` 校验 sender/WebView identity，旧 completion 不能终结新请求；
- redirect 不消费新的 host admission；same-document/no-start 不阻塞后续请求或消息；
- `Navigate` 同步失败只取消当前 issued request，不清除回调期间创建的新 deferred request；
- `put_Cancel`/event getter 同步 close 或 supersede 后，不再写旧 active state；
- pending message batch 已 detach；close guard 在 controller/WebView cleanup 之前生效；
- URLMon BSTR 有单一 RAII ownership，输入长度有上限，空 canonical result 被拒绝；
- `urlmon`/`oleaut32` 只链接 Windows platform 和 Windows URI test 的正确 target。

当前最高优先诊断不是串行模型是否存在，而是 `E_INVALIDARG` 到底由哪一个真实 WebView2
调用/事件路径返回。重点检查并增加可观测证据：

- `onNavigationStarting()` 中 bounded URI view、navigation policy 与 canonicalization 的每个 HRESULT；
- `SourceChanged` 中 `get_Source()`、WebView identity/epoch 校验及 `canonicalDocumentUri()`；
- `onNavigationCompleted()` getter 的失败归因；
- WebView2 实际返回的 `data:` source 是否为空、超长、带 fragment 或使用不同 spelling；
- `copyOpaqueDocumentUri()` / `canonicalDocumentUri()` 是否错误拒绝有效 data URI；
- 初始 `data:` 导航是否应完全绕开 URLMon/COM canonicalization；
- 日志/测试断言应标明失败的具体 operation，不能把任意 HRESULT 都压缩成 surface Failed。

四个失败测试及完整日志见第 2 节。修复必须由新的实现代理完成、独立 reviewer 复审，
创建不含 `handoff.md` 的 scoped commit 后推送。只有 Configure、Build、full CTest、
Composition integration、release packaging contract、whitespace 和 artifact 全部通过，
Windows 才能标为当前 GREEN。

### 5.2 macOS Task 19 已实现但尚未提交

Task 19 的 RED 证据仍在
`docs/tdd/task-19-macos-layer-stack-red.txt`，对应生产实现现在已经存在于本地 dirty
worktree。完整 modified/untracked 白名单见第 1 节。实现提供：

- `CanvasCompositionView` 固定 Base Metal → embedded container → Overlay Metal sibling 顺序；
- Base opaque 白底且仅绘 Base，Overlay transparent 且仅绘 Annotation + Chrome；
- 两个 surface 共享 device/queue/Ganesh context/renderer，同时独立 present/invalidation；
- resize、Retina scale、attach/detach/reattach、无 drawable retry、无 busy loop；
- 默认 overlay hit-test，显式 embedded interaction 后切到中间 container；
- macOS demo 实际使用 composition host。

最新本地验证为 full CTest 123/123、frame-plan 5/5、composition integration 2/2、
AppKit scheduling 1/1、所有 macOS contracts 通过、composition integration 重复 20 次通过、
x86_64 Objective-C++ strict syntax 通过、clang static analyzer 无诊断、`git diff --check`
通过。独立 reviewer 为 Conditional PASS，无 P0/P1 blocker。

只读复核仍应保留以下 P2/后续边界，不能把 Task 19 描述成完整 macOS 白板：

- `embeddedInteractionEnabled` 当前把整个区域交给空的 embedded container；接入真实 WKWebView/InputRouter 时，应按实际 child 命中和 PointerKind 路由，避免空白区域吞掉 pen/viewport 输入。
- 当前没有真实 WKWebView child、focus/IME、视频/网页生命周期、pointer adapter 或 Electron IPC；三层宿主只是这些能力的容器。
- runtime test 验证层顺序、opacity、共享资源和 frame commit，但还没有逐像素 readback 证明透明 overlay 不污染中间内容；source contract 也是结构性检查。
- `MetalRenderResources` 的每个 `shared_ptr` 拷贝都可能成为最后 owner，最终释放必须发生在 AppKit 主线程；当前 header 已说明此约束，未来 surface factory 也必须遵守。
- 当前只有 RED evidence 文件，没有单独的 Task 19 GREEN evidence；提交前应补一份记录 123/123、定向测试、review 结论和未运行项目的 GREEN 文档。
- README/既有 Task 16 macOS GREEN 记录仍按旧的单 Metal surface 描述；Task 19 提交时必须逐项更新，避免新账号误以为旧证据覆盖三层宿主。
- 两个 surface 当前在 AppKit 主线程串行使用共享 `SkiaRenderer`；若未来把渲染移到异步线程，必须增加同步或拆分 renderer/context，不能直接复用当前无锁对象。
- PR #2 仍以旧 Windows SHA `80ba591` 为 base、远端头仍是 672ef32，且没有 GitHub macOS CI；本地通过不能替代 rebase 后的重跑和远端证据。

接下来应先保全 WIP，再等待/跟随 Windows 基线收敛，rebase 后重跑所有 macOS 验证。
Task 19 commit 必须使用第 1 节的明确白名单，不得把 EmbeddedLoadBatch/Inbox 混入。

### 5.3 异步嵌入加载尚未接入 WhiteboardApp

WebView2 的 controller/navigation admission 是异步的。当前 WhiteboardApp 仍可能在同步 HRESULT/render admission 后就把命令当成成功；尚未完整实现：

- candidate Document + 所有隐藏 WebView staging；
- 256 surface 上限和唯一 token/generation 分配；
- 所有 embedded Ready 后才 render/show/swap；
- 任一 Failed、timeout、cancel、render/show 失败时保留旧 Document/旧可见 surface；
- WM_APP bounded drain、Inbox wake 合并、PostMessage 失败 fallback；
- synchronous COM callback 的 mutation-depth/reentrancy guard；
- embedded-state Ready/Failed 事件和 node/request/connection 关联。

EmbeddedLoadBatch 和 CompletionInbox 本身已经准备好，但没有 WhiteboardApp 接入提交。create-embedded 和 open-document 的异步失败策略不能通过“同步 response accepted”来假装已完成。现有 docs/evidence/electron-native-control.pending.md 明确要求把这部分作为后续增强。

### 5.4 Electron/协议未完成项

- tools/electron-host 当前能启动/认证/重连/发送命令，但 native event payload 的严格 validator/forwarder 还不完整，尤其是 embedded-state、selection-changed、diagnostics 的端到端语义。
- state-、fatal- 等 native 生成的 requestId 前缀可能使原始 256-byte requestId 超限，需要统一做长度预算和截断/拒绝策略。
- 需要真实 Electron GUI E2E：ready gate、Add Web/Video/Rich Text、Save/Open、断线重连、旧 connection response 隔离、优雅退出。
- session token、named pipe 名称和本地凭证绝不能写入 handoff、日志或公开 issue。

Electron 当前 `isNativeEvent` 只校验 envelope 和 payload 为普通对象，尚未按 event type
校验字段，也未把 response/document-state 等事件转发给 renderer；现阶段 UI 主要只消费
`ready` 和本地错误。后续实现必须保留 1 MiB framing、严格 UTF-8、sender identity、
ready gate 和 backpressure，不应为了接事件而直接暴露 `ipcRenderer`。

### 5.5 跨平台和产品项

- macOS：WKWebView 双层承载、输入/IME、Electron 控制、视频和网页层级尚未实现。
- Android/iOS：没有 platform layer、输入适配、Metal/Vulkan/Skia host 或发布流程。
- 多人在线协作：没有网络协议、服务端、CRDT/OT、冲突解决、Presence、权限、离线合并。
- 性能：没有 50 ms 端到端测量；需要在 i5-1235U 触控大屏上测物理接触到可见墨迹的 p50/p95/p99，而不是只看 API timestamp。
- macOS workflow/Release：当前只有 Windows GitHub Actions；macOS 只能本机验证。
- Release：当前 v0.1.0-alpha.1 是旧提交的 unsigned Windows 包，不能代表最新代码。

## 6. 推荐接手实施顺序

### 阶段 A：保全现状

1. 把全部本地已提交引用打成 Git bundle。这样 f856aac、e6148a2、0258173 即使没有远端分支也能恢复：

   ~~~bash
   git -C /Users/qing/Documents/myself/projects/canvas-task16 bundle create /tmp/canvas-local-refs.bundle --all
   git -C /Users/qing/Documents/myself/projects/canvas-task16 bundle verify /tmp/canvas-local-refs.bundle
   git -C /Users/qing/Documents/myself/projects/canvas-task16 bundle list-heads /tmp/canvas-local-refs.bundle
   ~~~

2. 保存 macOS tracked WIP。使用 `git diff HEAD --binary` 而不是只用 `git diff`，这样即使
   某个代理已经暂存了文件，备份仍包含 staged + unstaged 内容；`--binary` 可避免未来
   二进制改动被截断。Windows `87ebf01` 已推送，当前没有源代码 patch 需要备份：

   ~~~bash
   git -C /Users/qing/Documents/myself/projects/canvas-macos diff HEAD --binary > /tmp/canvas-macos-tracked-wip.patch
   ~~~

3. 普通 git diff 不包含 untracked 文件；必须单独归档当前 macOS 白名单中的 untracked
   文件。本 `handoff.md` 在独立文档提交推送前也应另存一份：

   ~~~bash
   cp /Users/qing/Documents/myself/projects/canvas-task16/handoff.md /tmp/canvas-handoff.md
   tar -czf /tmp/canvas-macos-untracked-wip.tgz -C /Users/qing/Documents/myself/projects/canvas-macos docs/tdd/task-19-macos-layer-stack-red.txt src/platform/macos/composition_view.h src/platform/macos/composition_view.mm tests/contracts/macos_composition_host_contract_test.cmake tests/integration/macos_composition_layer_stack_test.mm
   ~~~

4. 复制 bundle、macOS patch、macOS tgz 和 handoff 副本到新账号可访问的安全位置，并在副本上运行 bundle verify / tar -tzf 检查；最好同时保存每个备份文件的 SHA-256。备份文件不应提交到 Canvas 仓库，也不要放在会被自动同步到公开网盘的目录。
5. 不要把新的 Windows runtime 修复、macOS Task 19、e6148a2/0258173 或 handoff 文档混在一个提交里；每项使用自己的明确白名单。
6. 先在本地执行 git diff --check，再按文件白名单 stage；不要 git add -A。

### 阶段 B：Windows `E_INVALIDARG` 修复和 CI

1. 先读取 run 30694257905 的失败日志，确认当前失败都是初始 `data:` 导航的
   `E_INVALIDARG`；再确认旧 URI ledger 仍已删除：
   `rg -n 'nativeNavigationGenerationForDocument|oldestNativeNavigationGeneration|NativeNavigationAdmission' src/platform/windows/webview2_surface.cpp`
   应无结果；然后逐段阅读 `IssuedNavigation`/deferred driver，而不是只看测试名。
2. 在 `onNavigationStarting`、`SourceChanged`、`onNavigationCompleted` 和调用
   `canonicalDocumentUri`/`Navigate` 的路径上定位具体失败 operation。必要时先增加 scoped
   diagnostics/更精确断言；保留“最多一个 issued + 一个 latest deferred”的不变量，不能
   为了让单测过而恢复 URI→generation 猜测。
3. 在能使用 Windows SDK/WebView2 的环境中运行 URI unit、initial-load seam、WebView2
   integration；至少确认 `WebView2NavigationUri.*`、`WebView2InitialLoadTracker.*`、
   `WebView2MessageLog.*` 和 `canvas_webview2_surface_test.*` 被发现并执行。在 macOS
   端只能做 portable seam 的严格语法/警告和 diff-check，不能声称 WebView2 runtime 已验证。
4. 让独立 reviewer 检查 URL canonicalization、getter/`put_Cancel`/`SourceChanged`/
   `Navigate` 重入、generation/epoch、old WebView identity、detached message batch、
   BSTR/COM ownership；把 reviewer 的结论和未运行项目写入 evidence 或 PR 评论。
5. 修复 reviewer findings 后由同一 reviewer 复审；只有明确 PASS 才能按 Windows 文件白名单
   stage/commit，不能把 `handoff.md` 或异步组件混入。
6. 推送 `codex/windows-vertical-slice`，监控 PR #1 直到以下步骤全部通过：Configure →
   Build → full CTest → Composition integration → release packaging contract → whitespace
   → artifact。若 workflow 因 billing/spending limit 无法分配 runner，记录真实 run URL、
   不修改代码；若 runner 已启动，则继续诊断代码/CI，不把旧绿色 run 当成当前证据。
7. 只有完整绿色后，才把真实 run URL、head SHA、artifact URL/digest 写入 Task 15/16
   evidence；硬件/GUI/IME/video/50 ms pending 仍不能改成 GREEN。

Windows 本地命令（Visual Studio Developer PowerShell）：

~~~powershell
./scripts/Restore-WebView2.ps1
cmake --preset windows-x64
cmake --build --preset windows-x64-release --parallel
ctest --preset windows-x64-release --output-on-failure
ctest --preset windows-x64-release -R "canvas_(windows_composition|webview2_surface)_test" --output-on-failure
~~~

若只想先定位平台无关 seam，可先执行：

~~~powershell
ctest --preset windows-x64-release -R "(WebView2InitialLoadTracker|WebView2NavigationUri)" --output-on-failure --no-tests=error
~~~

这条定向命令不能替代完整 workflow。GitHub Actions 的顺序是 web `npm test/build`
→ vcpkg/WebView2 restore → Configure → Build → full CTest → Composition integration
→ release packaging contract → whitespace → package/artifact；任一步失败，后续步骤会被
跳过，故 artifact 不存在并不一定是上传权限问题。

### 阶段 C：收敛并提交 macOS 固定三层

1. 先按阶段 A 保存 `/Users/qing/Documents/myself/projects/canvas-macos` 当前 Task 19
   production WIP；不要在 `canvas-task16` Windows worktree 中运行 macOS preset。
2. 复核已经实现的 `composition_view.*`、可参数化 MetalHost/frame plan 和 tests；补写
   Task 19 GREEN evidence，保留 RED evidence 原样，不要伪造或改写 RED 结果。
3. 让独立 reviewer 检查 AppKit sibling 顺序、hit testing、透明度、共享 Metal 资源、
   所有 `shared_ptr` 的主线程最终释放、生命周期和 reattach；修复后由同一 reviewer 复审。
4. 在本机重新运行（当前快照已通过 123/123，但 rebase/任何修改后都要再跑）：

   ~~~bash
   PATH=/opt/homebrew/bin:$PATH VCPKG_ROOT=/Users/qing/Documents/myself/projects/vcpkg VCPKG_OVERLAY_PORTS=/Users/qing/Documents/myself/projects/vcpkg-overlays cmake --preset macos-arm64
   PATH=/opt/homebrew/bin:$PATH VCPKG_ROOT=/Users/qing/Documents/myself/projects/vcpkg VCPKG_OVERLAY_PORTS=/Users/qing/Documents/myself/projects/vcpkg-overlays cmake --build --preset macos-arm64-release --parallel
   VCPKG_ROOT=/Users/qing/Documents/myself/projects/vcpkg ctest --preset macos-arm64-release --output-on-failure
   ~~~

5. reviewer PASS 后按 Task 19 白名单创建本地 scoped commit；Windows 最新绿色 head 确定后，
   将本地 `codex/macos-platform` rebase 到它，解决 CMake/tests 冲突并重跑全量验证。
6. 通过后使用带期望旧 SHA 的 `push --force-with-lease` 更新 PR #2；不要把本地依赖
   overlay 写进项目配置，除非另有评审。PR #2 当前 base SHA 是 80ba591，rebase 后必须
   重新查看 mergeable 状态，不能只看到分支 push 成功就认为 PR 可合并。

### 阶段 D：提交两个异步基础组件

在 Windows 基线稳定后，分别审查/推送或 cherry-pick：

~~~text
e6148a29f75152e82a36479f6136171687c15601  feat: stage embedded document load batches
025817394a4e662159bef85b749280efa530f7b6  feat: queue embedded load completions
~~~

推荐先把它们合并到一个后续异步事务分支，保留两个逻辑清晰的提交；重新跑 portable/core 全量测试。`0258173` 的父提交就是 `e6148a2`，因此只 cherry-pick `0258173` 不会自动带入父提交内容，最安全的顺序仍是先 `e6148a2` 再 `0258173`。两个提交目前没有远端分支，任何新账号必须先用 `git cat-file -e <SHA>^{commit}` 和 `git show --stat <SHA>` 确认对象存在，再操作。

### 阶段 E：WhiteboardApp 原子加载事务

建议另开 codex/atomic-document-open，不能直接在 Windows CI 修复分支上开发。事务契约：

1. decode candidate Document；为所有 embedded node 创建 hidden WebView2。
2. 每个 surface 绑定唯一非零 token 和 document generation，completion 只进入 Inbox，不在 COM callback 内直接 swap。
3. UI 消息每次最多 drain 固定数量，队列未空则 rearm；PostMessage 失败必须释放 wake 标志并采用安全的 deferred/fatal fallback。
4. 全部 Ready 后 render candidate、显示 candidate，最后一次性交换 Document 和 surface ownership。
5. 任一创建/导航失败、Ready Failed、timeout、取消、render/show 失败，都销毁 candidate 并保留旧可见状态；rollback 失败才进入现有 fatal-close policy。
6. 新 open 取消旧 generation；旧 callback、旧 timer、旧 connection 不能影响新事务。
7. open-document 的同步 response 只表示 staging admission；真正 commit 后才发 document-state，并绑定原始 connectionId。下一任务再加 embedded-state Ready/Failed。
8. 覆盖 0、1、乱序、多失败、256/257、重复 completion、stale generation/token、timeout、同步 callback reentrancy、window destroy、origin isolation 的测试。

### 阶段 F：协议、Electron 和 macOS WebKit

按以下顺序推进，避免一次混入太多平台边界：

1. native embedded-state Ready/Failed 的 schema、nodeId/token/generation/requestId/connectionId 关联和失败策略；
2. Electron 严格 payload validator、事件 forwarder、requestId 长度预算、断线重连 E2E；
3. macOS WKWebView surface factory，先实现 Web/RichText/Video 的 layer/frame/visibility，再接交互；
4. macOS pointer/touch/pen/IME 输入和 overlay hit-test；
5. Android/iOS 平台层；
6. 最后接多人协作服务和同步协议。

## 7. 构建、依赖和常见陷阱

### Windows

需要 Visual Studio 2022 Desktop C++、Windows SDK、Node.js 22.12+、vcpkg、Microsoft Edge WebView2 Runtime。VCPKG_ROOT 必须指向 vcpkg 根目录。CMake 会构建锁定的 npm web 资产，并把 web/ 复制到 exe 旁边；只复制 exe 是不能运行的。

标准命令：

~~~powershell
./scripts/Restore-WebView2.ps1
cmake --preset windows-x64
cmake --build --preset windows-x64-release --parallel
ctest --preset windows-x64-release --output-on-failure
~~~

### macOS

本机已知依赖位置：

~~~text
VCPKG_ROOT=/Users/qing/Documents/myself/projects/vcpkg
VCPKG_OVERLAY_PORTS=/Users/qing/Documents/myself/projects/vcpkg-overlays
GN mirror=/Users/qing/Documents/myself/projects/gn-mirror
~~~

overlay 是本机依赖恢复 workaround，不应在没有评审的情况下提交到 Canvas。当前 vcpkg Skia 使用 macOS Metal/PNG feature；Windows 使用 Direct3D/PNG feature。

macOS 命令必须在 `canvas-macos` worktree 执行；Windows 87ebf01 的
`CMakePresets.json` 只有 Windows preset。该 Mac 的成功构建使用 arm64 AppleClang、
Ninja 和本地 vcpkg overlay；换机器时不要假设 `/Users/qing/...` 路径存在，也不要把
overlay 目录复制进仓库后悄悄提交。若依赖恢复被网络/CIPD 阻塞，应记录实际镜像和 hash，
不要用未锁定的系统 Skia 或 x86_64 GTest 结果替代 arm64 构建证据。

### Web/Electron

~~~bash
cd web
npm ci
npm test
npm run build

cd ../tools/electron-host
npm ci
npm run build
node --check dist/main.js
node --check dist/preload.js
~~~

不要更新 lockfile 来“解决”网络或版本问题；先确认 Node 版本和 registry。不要把生成视频、session token 或运行日志提交到仓库。

Electron host 的启动前提是 Windows 环境变量 `CANVAS_EXE` 指向已构建的
`canvas_windows.exe`；`npm start` 本身不会编译 C++。native 子进程必须与相邻 `web/`
目录一起运行，且 WebView2 Runtime 已安装。当前 package lock 固定 Electron 43.1.1、
TypeScript 7.0.2、Vite 8.1.5、Vitest 4.1.10 和 Lexical 0.48.0；不要用全局 Electron
或 `npm install` 改写锁文件来代替 `npm ci`。

### Release

发布由 tag workflow 自动完成，流程是：构建 → 全量 CTest → Composition tests → packaging contract → 上传 artifact → gh release create/edit → 上传 ZIP 和 .sha256。新 tag 前必须确认目标 commit 的 Windows CI 全绿，且明确版本号是否 prerelease（带连字符的 tag 会标为 prerelease）。

下载后验证示例（PowerShell）：

~~~powershell
Get-FileHash .\canvas-windows-x64-v0.1.0-alpha.1.zip -Algorithm SHA256
Get-Content .\canvas-windows-x64-v0.1.0-alpha.1.zip.sha256
~~~

哈希必须与 checksum 文件相同；解压后应同时存在 `canvas_windows.exe`、README 和 `web\`
目录。该发布物是 unsigned native portable ZIP，不是安装程序，也没有 Electron launcher；
正式分发前仍需代码签名、安装/升级策略、WebView2 Runtime 依赖说明和恶意软件扫描流程。

## 8. 关键文件索引

~~~text
CMakeLists.txt                           平台 target、依赖、Windows/macOS 条件
CMakePresets.json                        windows-x64 / macos-arm64 preset
.github/workflows/windows-build.yml      Windows CI、artifact、tag release
app/windows/whiteboard_app.*             Windows UI/IPC/事务入口
app/macos/main.mm                        macOS demo app
src/platform/windows/                    DComp、D3D12、WebView2、pointer、IPC backend
src/platform/macos/                      MetalHost、CanvasMetalView、后续 composition host
src/app/embedded_load_tracker.*          Windows 早期异步 load tracker
src/app/embedded_load_batch.*            Task 17 平台无关批量加载状态机
src/app/embedded_load_completion_inbox.* Task 18 固定容量 UI completion FIFO
include/canvas/ipc/                      协议模型和方向/预算校验
tools/electron-host/                     Electron 主进程和 preload
web/                                     Lexical、video、host bridge 资产
docs/tdd/                                每个切片的 RED/GREEN 证据
docs/evidence/*.pending.md               尚未完成的 Windows/GUI/硬件验收清单
~~~

## 9. 交接时的工程规则

- 每个功能遵循“实现代理 → 独立审查 → 修复 → 原审查者复审”；审查未 PASS 不提交。
- 代码和 CMake 改动按任务 scoped；不要顺手格式化或重写不相关文件。
- 先写 RED，再写 GREEN 证据；不能用生成截图、模拟硬件结果或静态推断替代真实 GUI/触控证据。
- 所有 WebView2/COM callback 都要考虑 STA synchronous reentrancy、owner lifetime、WebView identity 和 generation。
- 高频输入保持 native；Electron IPC 只做低频控制和状态，不承载逐点 pointer/stroke 数据。
- 不记录 session token、named pipe token、用户文件路径中的敏感信息。
- 保留现有用户改动；避免 destructive git 命令；stage 时使用明确文件白名单。
- 合并/推送前检查：git diff --check、warning-clean build、目标测试、完整 CTest、必要的 CI artifact 和证据文档。

## 10. 最终交接验收清单

新账号接手后，至少应能回答并实际验证以下问题：

- [ ] 能从远端 clone/fetch 并定位 PR #1、PR #2 和本地-only 的 e6148a2/0258173。
- [ ] 明白 `87ebf01` 当前 CI 不是绿色，4 个 WebView2 initial `data:` navigation tests 因 `E_INVALIDARG (0x80070057)` 失败。
- [ ] 保存了 macOS Task 19 的 tracked/untracked production WIP 和 RED evidence，没有误删；Windows URL/COM 修复已在 `87ebf01` 远端。
- [ ] Windows URL canonicalization、COM reentrancy、SourceChanged stale identity 有独立测试和复审记录。
- [ ] Windows full CI 的 Build、CTest、Composition、package、whitespace、artifact 全绿后才更新 evidence。
- [ ] macOS 三层宿主测试通过，且 PR #2 已 rebase 到最新 Windows 基线。
- [ ] EmbeddedLoadBatch/CompletionInbox 已重新审查并推送，之后才接入 WhiteboardApp。
- [ ] embedded-state、Electron GUI E2E、WKWebView、触控/IME/视频和 i5-1235U <50 ms 证据均有真实记录，或仍明确 pending。
- [ ] 多人协作网络层、Android/iOS 和正式签名发布被单独排期，而不是误认为当前 vertical slice 已包含。

本文件不包含任何凭证、session token 或未公开的用户数据。交接账号应先阅读本文件，再查看各 worktree 的 git status，最后按照第 6 节的顺序推进。

## 11. 给新账号的实际开工手册

### 11.1 如果新账号仍使用当前这台 Mac

不要重新 clone，也不要创建同名分支。五个 worktree 和其中的未提交文件已经存在；重复 clone 很容易让新账号误在一个“干净但缺少 WIP”的目录里继续开发。

新账号第一轮只执行只读检查：

~~~bash
cd /Users/qing/Documents/myself/projects/canvas-task16
sed -n '1,240p' handoff.md
sed -n '241,$p' handoff.md
git worktree list --porcelain
git status --short --branch
git diff --stat

git -C /Users/qing/Documents/myself/projects/canvas-macos status --short --branch
git -C /Users/qing/Documents/myself/projects/canvas-embedded-batch status --short --branch
git -C /Users/qing/Documents/myself/projects/canvas-completion-inbox status --short --branch
git -C /Users/qing/Documents/myself/projects/canvas-atomic-open status --short --branch
~~~

预期结果：

- canvas-task16 位于 87ebf01，Windows 源代码 clean（交接文档单独提交）；
- canvas-macos 位于 f856aac，显示比远端 ahead 1，并能看到 Task 19 RED 与三层宿主 production WIP 文件；
- embedded-batch 位于 e6148a2 且 clean；
- completion-inbox 和 atomic-open 都位于 0258173 且 clean；
- 不应出现来源不明的新改动。如果实际状态不同，先更新本文件中的状态快照或查清改动来源，再开始写代码。

随后只对当前优先任务做定向检查。Windows 代码已经在 `87ebf01`，先读提交和失败日志；
macOS 生产 WIP 则只在其 worktree 做 diff：

~~~bash
cd /Users/qing/Documents/myself/projects/canvas-task16
git show --stat --oneline 87ebf01
gh run view 30694257905 --repo Mostorm-Labs/canvas --job 91354264780 --log-failed
sed -n '1,260p' src/platform/windows/webview2_navigation_uri.h
sed -n '1,320p' tests/unit/webview2_navigation_uri_test.cpp

git -C /Users/qing/Documents/myself/projects/canvas-macos diff --stat
git -C /Users/qing/Documents/myself/projects/canvas-macos status --short
~~~

在理解 macOS WIP 和 Windows runtime failure 前不要运行格式化全仓库、自动修复、rebase、
stash pop 或任何清理命令。构建目录和依赖恢复可以重建，源码工作树中的未提交内容不可以。

### 11.2 如果新账号在另一台机器

先在旧机器按第 6 节阶段 A 生成并复制以下文件；仅有 GitHub 仓库 URL 不足以恢复当前
macOS 未提交状态（Windows `87ebf01` 已在 GitHub）：

~~~text
canvas-local-refs.bundle
canvas-macos-tracked-wip.patch
canvas-macos-untracked-wip.tgz
canvas-handoff.md (如果 handoff commit 尚未推送)
~~~

在新机器上先验证而不是直接覆盖源码：

~~~bash
git bundle verify /secure-transfer/canvas-local-refs.bundle
tar -tzf /secure-transfer/canvas-macos-untracked-wip.tgz

git clone --branch codex/windows-vertical-slice https://github.com/Mostorm-Labs/canvas.git canvas-windows
cd canvas-windows
git fetch /secure-transfer/canvas-local-refs.bundle 'refs/heads/*:refs/remotes/handoff/*'
git log --oneline --decorate --all --max-count=30
~~~

确认能在 refs/remotes/handoff/ 下看到 f856aac、e6148a2、0258173 后，再分别创建 worktree。示例路径可按新机器调整：

~~~bash
git worktree add -b codex/macos-platform-local ../canvas-macos refs/remotes/handoff/codex/macos-platform
git worktree add -b codex/embedded-load-batch-local ../canvas-embedded-batch refs/remotes/handoff/codex/embedded-load-batch
git worktree add -b codex/embedded-completion-inbox-local ../canvas-completion-inbox refs/remotes/handoff/codex/embedded-completion-inbox
git worktree add -b codex/atomic-document-open-local ../canvas-atomic-open refs/remotes/handoff/codex/atomic-document-open
~~~

分支名带 -local 是为了避免和 clone 后已经存在的远端跟踪分支发生歧义。恢复 Windows/macOS tracked patch 前分别执行 git apply --check；untracked tgz 先解压到临时 staging 目录，逐文件比较后再复制到目标 worktree。不要直接在仓库根目录执行 tar -xzf：归档内容与未来远端文件重名时可能发生覆盖。

恢复完成后至少验证：

~~~bash
git -C ../canvas-macos apply --check /secure-transfer/canvas-macos-tracked-wip.patch
git fsck --full
git show --stat --oneline f856aac
git show --stat --oneline e6148a2
git show --stat --oneline 0258173
~~~

确认 fsck 不报告缺对象或损坏，并且三个本地-only SHA 都可读取。确认 macOS patch/tgz
可恢复后，才允许旧账号删除本机 worktree。

### 11.3 建议复制给新 Codex 账号的首条任务说明

~~~text
接手 Mostorm-Labs/canvas。工作区在
/Users/qing/Documents/myself/projects/canvas-task16，先完整阅读 handoff.md，
再只读检查所有 canvas-* worktree、PR #1、PR #2 和最新 CI。保留所有已有
tracked/untracked WIP，禁止 reset --hard、checkout --、clean、git add -A。

当前优先完成 handoff.md 第 6 节阶段 B：定位 `87ebf01` 的 Windows WebView2 初始
`data:` 导航 `E_INVALIDARG`，并按“实现代理 -> 独立审查 -> 修复 -> 原审查者复审”
流程只提交 scoped 修复。推送后监控 Windows workflow，必须 Build、完整 CTest、
Composition integration、packaging、whitespace、artifact 全部绿色。随后将 macOS Task 19
WIP 补 GREEN evidence、提交并 rebase 到新的 Windows 基线。硬件、GUI、IME、视频和
<50 ms 触控证据仍保持 pending；不要提前混合 EmbeddedLoadBatch/Inbox 或 atomic-open 改动。
~~~

如果路径已经变化，应先替换提示词中的绝对路径；不得因为提示词中的 SHA 是快照就强行把更新后的远端退回旧 SHA。

### 11.4 每次任务结束时必须留下的交接信息

后续每个代理/开发者停止工作前，都应在对应 Task evidence、PR 评论或本文件状态快照中留下：

- 实际分支、完整 HEAD SHA、是否已 push；
- 精确的 modified/untracked 文件列表，以及哪些属于用户原有改动；
- 已运行的命令、测试数量、结果和真实 CI run URL；
- reviewer findings、修复提交和复审结论；
- 未运行项目及原因，尤其是 Windows 触控硬件、Electron GUI、WebView2 Runtime、AppKit/WKWebView 真实窗口；
- 可下载 artifact/Release URL、SHA-256，以及产物是否签名、是否包含 Electron launcher；
- 下一步的单一最高优先任务和明确停止条件。

“编译通过”“应该可以”或“历史 CI 绿色”都不能替代上述证据。对当前 Canvas，完成定义是目标提交上的目标测试和 CI 真实通过，同时所有无法在当前环境执行的硬件/GUI 验收仍被明确列为 pending。
