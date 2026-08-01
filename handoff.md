# Canvas 项目交接文档

> 文档版本：2026-08-01
>
> 状态快照：2026-08-01 21:47（Asia/Shanghai）。这是一次性快照；接手时以
> `git fetch`、`git status`、GitHub PR/Actions API 的实时结果为准，不要因为本文件中的
> SHA、测试数量或“最新运行”字样而跳过重新核对。
>
> 仓库：Mostorm-Labs/canvas（https://github.com/Mostorm-Labs/canvas）
>
> 本文用于把当前 Canvas 白板工程交给新的账号/开发者继续维护。文档中的“已验证”只表示有真实的本地或 GitHub 运行记录；没有 Windows 触控硬件、AppKit 真实窗口或多人网络环境证据的部分，均明确标为 pending。

如果本文件与源码、`git worktree` 状态、GitHub PR 或 Actions 结果冲突，实时结果优先；先更新状态快照，再开始实现。本文故意把历史绿色运行、本地未推送提交和未提交 WIP 分开描述，避免把“曾经通过”误读成“当前提交已通过”。

## 0. 先看结论

Canvas 当前不是一个已经完成的商业化多人协作产品，而是一个以 C++/Skia 原生渲染为核心的跨平台白板垂直切片：

- Windows 原生路径已经具备共享文档模型、Skia + D3D12/DirectComposition 涂写、Win32 pen/touch 输入、WebView2 网页/富文本/视频承载、命名管道 IPC、Electron 控制和 Windows portable 发布流程。当前 branch head 为 docs commit `a9912da`，最新代码 commit 仍为 `eb1d948`；runs 30697532044 与 30700281564 均全绿：200/200 CTest、Composition、打包契约、whitespace、metadata 和 portable artifact 均通过。修复包含 WebView2 启动期 host-message generation 保留，以及严格限定的已提交 `data:` fragment 快路径。
- macOS Apple Silicon 路径已有 AppKit + CAMetalLayer + Skia Ganesh 的真实首帧渲染、固定三层宿主、WKWebView 宿主和 Task 21 首文档导航/Ready 生命周期。当前远端 head `9adb455` 已推送到 PR #2；本地正确 overlay 下 configure、串行 Release build、full CTest 165/165 和 Task 21 focused 12/12 均通过。PR #2 的 Windows Build run 30698953072（Task 20 head）和 30702348950（Task 21 head）均已全绿。Pointer/IME、Electron/macOS CI 和真实 GUI 仍未接入。
- EmbeddedLoadBatch 和 EmbeddedLoadCompletionInbox 已在本地完成平台无关的异步加载基础，但两个提交尚未推送到 GitHub，也尚未接入 WhiteboardApp。
- 当前没有多人协作网络同步、CRDT/OT、账号/权限、房间/Presence、服务端，也没有 Android/iOS 实现。
- 当前没有可宣称的 i5-1235U 触控屏“肉眼跟手”或 p95 < 50 ms 测量。这个指标必须在真实 Windows 触控设备上用高速摄像机测量。

接手时最重要的顺序是：

1. 复核已记录的 PR #2 head `9adb455` Windows Build run 30702348950 及 artifact，然后进入 Task 17/18 审查。
2. 推送两个异步加载基础提交，并将其接入 WhiteboardApp 的原子文档加载事务。
3. 继续实现 macOS PointerKind/pen/touch/IME、Electron/native IPC，以及 embedded-state；随后规划 Android/iOS 平台层。
4. 最后做真实 Windows 设备、Electron GUI、触控延迟和 Release 验收；i5-1235U 触控屏 p95 <50 ms 仍必须由实机测量。

## 1. 仓库、远端和工作树事实

远端地址：

~~~text
origin = git@github.com:Mostorm-Labs/canvas.git
~~~

截至本文日期，远端主要引用为：

| 引用 | 短 SHA | 完整 SHA | 状态 |
|---|---|---|---|
| origin/main | cd445fc | `cd445fc4d24b849944958a6b108187727023d520` | 初始空壳基线 |
| origin/codex/windows-vertical-slice | a9912da | `a9912da6db3742cf3af2c4bdafca43c73b127edf` | Windows PR #1 的远端头；最新 docs push 的 run 30700281564 全绿 |
| origin/codex/macos-platform | 9adb455 | `9adb4556cf1abeb24a64f920bfe77c9359c0a4e5` | macOS PR #2 的远端头；Task 21 已推送，Windows Build run 30702348950 全绿 |

本机 worktree：

| 路径 | 分支 / HEAD | 远端情况 | 当前状态 |
|---|---|---|---|
| /Users/qing/Documents/myself/projects/canvas-task16 | codex/windows-vertical-slice / `a9912da6db3742cf3af2c4bdafca43c73b127edf` | 与 origin/codex/windows-vertical-slice 一致，当前仅 handoff 文档 WIP | Windows runtime 修复已 scoped commit 并推送；runs 30697532044、30700281564 全绿。handoff 文档仍须独立提交，不能混入其他平台改动 |
| /Users/qing/Documents/myself/projects/canvas-macos | codex/macos-platform / `9adb4556cf1abeb24a64f920bfe77c9359c0a4e5` | 与 origin/codex/macos-platform 一致，已推送 Task 21 | Task 19/20/21 代码与 evidence 已提交并推送；本地 configure/build/full CTest 165/165、Task 21 focused 12/12 |
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

当前 Windows worktree 的未提交文件为：

~~~text
handoff.md                                           (本交接文档，docs WIP)
~~~

原先未提交的 Windows URL canonicalization、串行导航和 COM 重入改动已由
`87ebf01 fix: serialize WebView2 navigation startup` 作为一个 scoped commit 提交并推送；随后
`f8478b2 fix: recover WebView2 fragment navigation liveness` 和
`eb1d948 fix: preserve WebView2 startup messages and fragments` 继续收敛 runtime 行为。
该提交修改/新增 10 个文件，包括 `webview2_surface.cpp`、initial-load/message seams、
`webview2_navigation_uri.h` 及相应单元/集成测试；不要再按旧快照寻找这些已提交文件。
`eb1d948` 的最终修复通过了 Windows runtime CI：启动 promotion 不会二次清除当前 generation 的
pending host message；`data:` fragment 只有在 navigation complete、active id 非零、source
仍已提交且 canonical document key 相同的情况下才绕过 `NavigationStarting` 预期。新文档
`SourceChanged` 会撤销旧完成证据，避免 stale key 向错误页面 flush 消息。

当前 macOS Task 19/20/21 已提交并推送；macOS worktree 当前 clean。Task 21 提交为
`9adb455`（WKWebView navigation/Ready lifecycle），Task 21 evidence 为
`docs/tdd/task-21-macos-wkwebview-navigation-{red,green}.txt`。Task 20 evidence 为
`15d61f1`/`3601da2`。

~~~text
（无未提交文件）
~~~

两个列表都是本地工作状态，不在远端。不要使用 git reset --hard、git checkout -- 或清理未跟踪文件来“整理”工作区。

> **跨机器交接警告：**另一个账号从 GitHub clone 已能看到 Windows `a9912da` 和 macOS
> `9adb455` 的已提交代码/evidence；仍看不到没有远端分支的 e6148a2/0258173。离开当前机器前
> 若要继续这些本地-only 对象，仍应执行第 6 节“阶段 A”的 bundle 备份。历史失败 CI 运行和诊断结论
> 只用于回归背景，不能代替当前绿色 run 的实时核对。

三个本地-only 提交对应的证据文件也只存在于各自分支：
`f856aac1c4b7ad767f5c2785a706730495b7d52b` 的 macOS 证据在
`docs/tdd/task-16-macos-*.txt`，`e6148a2` 的证据在
`docs/tdd/task-17-embedded-load-batch-*.txt`，`0258173` 的证据在
`docs/tdd/task-18-embedded-load-completion-inbox-*.txt`。仅从 Windows 远端分支查看
`docs/tdd/` 不会看到这些本地-only 分支文件。Windows `a9912da` 和 macOS `9adb455` 已在远端，不再依赖本机 patch 才能恢复；
但它当前的失败 CI 运行和诊断结论仍应由接手者通过 GitHub Actions URL 重新核对。


## 2. GitHub PR、CI 和 Release 状态

### PR #1：Windows

- PR：Build the Windows whiteboard vertical slice（https://github.com/Mostorm-Labs/canvas/pull/1）
- 分支：codex/windows-vertical-slice → main
- 状态：Draft / Open；最新 check 已通过，PR 可继续进行 macOS rebase 和后续功能开发。
- 远端 branch head：`a9912da6db3742cf3af2c4bdafca43c73b127edf`（最新代码 commit `eb1d948d5efc09e21ca8687f3522e9d7f8656e15`）
- 最新绿色运行：Windows Build run [30697532044](https://github.com/Mostorm-Labs/canvas/actions/runs/30697532044)
- build job：[91362837977](https://github.com/Mostorm-Labs/canvas/actions/runs/30697532044/job/91362837977)

最新运行的真实结果（head SHA 为 `eb1d948d5efc09e21ca8687f3522e9d7f8656e15`）：

- Configure：通过
- Build：通过
- Web assets、WebView2 SDK、vcpkg restore：通过
- CTest：200/200 通过
- Composition integration tests：通过
- Release packaging contract：通过
- Whitespace check：通过
- Package metadata：通过
- Windows portable bundle：通过
- Upload Windows portable artifact：通过
- artifact：`canvas-windows-x64-pr-1-93e7d86d7e23`，未过期，约 3,167,064 bytes

该 run 确实分配到了 `windows-2022` runner，不是 Actions billing/spending blocker。PR 事件中的
`release` job 是 skipped，这是预期行为：只有推送 `v*` tag 才会创建/更新 GitHub Release。
当前 Windows CI 已完全绿色；接手者仍应以实时 API 重新核对，而不是只看 PR 顶部图标：

~~~bash
gh run view 30697532044 --repo Mostorm-Labs/canvas --json jobs,headSha,conclusion,url
gh api repos/Mostorm-Labs/canvas/actions/runs/30697532044/artifacts
~~~

随后 docs-only head `a9912da6db3742cf3af2c4bdafca43c73b127edf` 的验证也已完成：run
[30700281564](https://github.com/Mostorm-Labs/canvas/actions/runs/30700281564)，job
[91369983853](https://github.com/Mostorm-Labs/canvas/actions/runs/30700281564/job/91369983853)。
Configure、Build、200/200 CTest、Composition、packaging contract、whitespace、metadata、
portable package 和 artifact upload 全部通过；artifact 为
`canvas-windows-x64-pr-1-7b92537f0a2f`，约 3,167,064 bytes，artifact id `8818623066`，未过期。
该 run 的 PR `release` job skipped 仍是预期行为。

历史失败 run `30696437691`（`f8478b2`）只保留作诊断背景：当时 200 项中 198 通过，失败为
`SerialNavigationKeepsOnlyTheLatestRequestAndDoesNotStarveAfterFragment` 与
`HostsContentBelowInkAndGatesSyntheticClicksByMode`；最终已由 `eb1d948` 修复并在本 run
验证。更早的 `E_INVALIDARG` 失败不应再被当作当前状态。

最近一个完整通过的 Windows 运行是 run 30160695952：
https://github.com/Mostorm-Labs/canvas/actions/runs/30160695952

当时为较早的 `80ba591c9c04d969e5b10c753804a73934671016`，记录为生产构建、完整 CTest 167/167、Composition 测试 10/10 和后续打包检查通过。它现在仅作为历史回归参考；当前 head 的权威证据是 run 30697532044。

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
- 状态：Draft / Open；base 已更新到 Windows `eb1d948`，GitHub 无 macOS runner，但会运行 Windows Build
- 远端头：`9adb4556cf1abeb24a64f920bfe77c9359c0a4e5`
- rebase/push：已完成，使用带旧远端 SHA `672ef32` 的显式 `--force-with-lease`
- rebase 后提交：`fc16496`（Skia Metal）、`ff03ed2`/`d65afe9`（Task 19 evidence/stack）、
  `d5d51e8`（Task 20 WKWebView host）、`15d61f1`/`3601da2`（Task 20 evidence）、
  `9adb455`（Task 21 WKWebView navigation/Ready lifecycle）
- 本地 macOS：正确 overlay 下 configure、串行 Release build、focused 12/12、full CTest 165/165，worktree clean
- PR Windows checks：Task 20 head 的 run [30698953072](https://github.com/Mostorm-Labs/canvas/actions/runs/30698953072) 和 Task 21 head 的 run [30702348950](https://github.com/Mostorm-Labs/canvas/actions/runs/30702348950) 均已全绿；Task 21 build job 为 [91375451176](https://github.com/Mostorm-Labs/canvas/actions/runs/30702348950/job/91375451176)

PR #2 Task 20 head 的 Windows 远端验证已完成。run [30698953072](https://github.com/Mostorm-Labs/canvas/actions/runs/30698953072)
（head `3601da2478289044517c3726534a4ecf3e097a98`）结果：Configure、Build、200/200 CTest、
Composition、release packaging contract、whitespace、metadata、portable package 和 artifact
upload 全部通过。首次 vcpkg cache miss 花费约 27 分钟并成功保存 cache，不是 billing blocker。
build job 为 [91366454552](https://github.com/Mostorm-Labs/canvas/actions/runs/30698953072/job/91366454552)。
PR artifact 为 `canvas-windows-x64-pr-2-f3edd04012ef`，约 3,167,064 bytes，未过期，下载 API URL：
`https://api.github.com/repos/Mostorm-Labs/canvas/actions/artifacts/8818476342/zip`。
`release` job skipped 是 PR 事件的预期行为；推送 `v*` tag 才会创建 Release。

Task 21 的 macOS 本机证据在 `9adb455`：WKWebView action/response 双重 fail-closed policy、
HTTPS/package-root file/受限 data URL、latest-wins generation、同步 reentry identity、
late completion URI、weak delegate 和 close 状态均已实现。overlay 配置下 focused CTest
12/12、fresh full CTest 165/165 通过；真实 macOS GitHub runner、HTTPS redirect 专项运行时
测试、Electron/Pointer/IME、真实 GUI 和 i5-1235U <50 ms 仍 pending。Task 21 证据文件为：
`docs/tdd/task-21-macos-wkwebview-navigation-{red,green}.txt`（仅 macOS worktree 可见）。

Task 21 head 的 Windows Build run 30702348950 已完成并全绿；接手者仍应查询该 run 和 artifact 的实时状态：

~~~bash
gh run list --repo Mostorm-Labs/canvas --branch codex/macos-platform \
  --workflow 319742650 --limit 5 \
  --json databaseId,status,conclusion,headSha,url
~~~

Task 21 head 的实际 Windows 结果（head SHA `9adb4556cf1abeb24a64f920bfe77c9359c0a4e5`）为：
run [30702348950](https://github.com/Mostorm-Labs/canvas/actions/runs/30702348950)，build job
[91375451176](https://github.com/Mostorm-Labs/canvas/actions/runs/30702348950/job/91375451176)；
Configure、Build、CTest 200/200、Composition integration、release packaging contract、
whitespace、package metadata、Windows portable package 和 artifact upload 全部通过。PR 事件的
release job [91375736804](https://github.com/Mostorm-Labs/canvas/actions/runs/30702348950/job/91375736804)
为 skipped，符合 workflow 设计。artifact 为 `canvas-windows-x64-pr-2-0459b8f1a9e6`，artifact id
`8819253543`，大小 3,167,064 bytes，未过期；下载 API URL：
`https://api.github.com/repos/Mostorm-Labs/canvas/actions/artifacts/8819253543/zip`。

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
基础，`d65afe9` 在其上提交了 Task 19 三层宿主：

- `CanvasCompositionView` 固定创建三个 back-to-front sibling：opaque Base `CanvasMetalView` → embedded `NSView` container → transparent Overlay `CanvasMetalView`，并保持三者 frame 同步。
- Base surface 白色清屏，只绘制 `LayerClass::Base`；Overlay surface 透明清屏，只绘制 `LayerClass::Annotation` 和 `LayerClass::Chrome`，不在原生层绘制 `Embedded`。
- 两个 CAMetalLayer 共享同一个 `MTLDevice`、`MTLCommandQueue`、Ganesh `GrDirectContext` 和 `SkiaRenderer`，同时各自保留 attachment generation、CAMetalLayer 和 `FrameInvalidation`。
- `MetalHost` 强制 AppKit 主线程，CAMetalDrawable texture 被包装为 Skia backend render target；Retina backing scale、事件驱动首帧、无 drawable 重试、resize、detach/reattach 均有本机测试覆盖。
- 默认 overlay 拥有 hit-test；显式开启 `embeddedInteractionEnabled` 后，当前实现把命中交给 embedded container。该策略符合 Task 19 RED，但还不是最终 PointerKind/实际 child 命中路由。
- macOS demo 已从单一 `CanvasMetalView` 切换到 `CanvasCompositionView`。
- 当前目标是 Apple Silicon arm64；Task 20 的真实 WKWebView child 已在 Task 21 增加首文档 navigation/Ready、双重 policy、latest-wins 和 close/reentry 生命周期。Task 19/20/21 已推送到 PR #2 远端头 `9adb455`；pen/touch/mouse/IME 输入适配、Electron/native IPC 和 macOS Release workflow 仍未完成。Windows worktree 不包含这些 macOS-only 后继提交。

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
| 12 | WebView2 composition surface、策略、输入转发 | `eb1d948` 已由 Windows run 30697532044 的 runtime integration 覆盖；真实 GUI/IME 仍 pending；`task-12-green.txt` |
| 13 | Lexical rich text、HTML video、WebView 资产和消息边界 | web 定向测试已 GREEN；真实 Windows WebView2/视频/IME 仍 pending；`task-13-green.txt` |
| 14 | Document JSON/MessagePack codec、atomic DocumentStore | 核心定向验证 GREEN；Win32 文件替换仍需 Windows 运行时证据；`task-14-green.txt` |
| 15 | Electron/native 控制、named pipe、Windows vertical slice | `eb1d948` 在 run 30697532044 完整绿色；硬件/GUI/IME/<50 ms 仍 pending；见第 2、5 节和 `task-15-green.txt` |
| 16 | Windows portable artifact/tag Release；WebView2 initial-load 早期契约 | Release `v0.1.0-alpha.1` 为旧发布；PR #1 artifact 由 run 30697532044/30700281564、PR #2 artifact 由 run 30698953072/30702348950 生成；`task-16-release-green.txt` |
| 17 | EmbeddedLoadBatch 固定容量异步加载状态机 | 本地-only GREEN，提交 e6148a2，尚未推送/接入 WhiteboardApp；见下文 |
| 18 | EmbeddedLoadCompletionInbox 固定 ring、合并 wake、generation cancel | 本地-only GREEN，提交 0258173，尚未推送/接入 WhiteboardApp；见下文 |
| 19 | macOS Base / embedded / Overlay 三层宿主 | rebase 后提交 `d65afe9`，已随 PR #2 推送；Task 20 基线 full CTest 157/157；见第 5.2 节 |
| 20 | macOS WKWebView 生命周期/几何宿主 | `d5d51e8` 已实现并独立 review PASS；main-thread attach/detach、bounds、visibility、interaction、flipped top-left 对齐；Task 20 基线 full CTest 157/157 |
| 21 | macOS WKWebView 首文档导航/Ready | `9adb455` 已推送并独立 reviewer PASS；HTTPS/package-root file/受限 data、action/response policy、latest-wins、reentry、late URI、close/reentry；focused 12/12、full CTest 165/165；真实 macOS CI/GUI/HTTPS redirect 专项仍 pending |

任务台账中的“完成”只描述代码/测试切片，不承诺多人协作、签名发布、跨设备
输入或 50 ms 体验指标。接手者应打开对应 evidence 文件，确认命令、架构和基线 SHA。

### Windows（远端）

当前权威绿色 CI 是 `eb1d948` / run 30697532044：MSVC configure/build、200/200 CTest、Windows Composition 集成测试、WebView2 runtime integration、Web 资产构建、portable package contract、whitespace、metadata 与 artifact upload 全部通过。硬件触控、GUI、IME、视频播放观感和 <50 ms 仍不在 CI 覆盖范围内。

历史证据文件：

- docs/tdd/task-15-green.txt：Windows vertical slice、D3D12/Skia、WebView2、IPC 的自动化结果和未完成项。
- docs/tdd/task-16-release-green.txt：portable artifact/tag Release 流程。
- docs/evidence/*.pending.md：明确记录尚未有真实 Windows touch/GUI/IME/video/latency 证据的项目。

### macOS

在本机 /Users/qing/Documents/myself/projects/canvas-macos，Task 19/20/21 的已推送提交使用相同
macOS preset。标准命令为：

~~~bash
PATH=/opt/homebrew/bin:$PATH VCPKG_ROOT=/Users/qing/Documents/myself/projects/vcpkg VCPKG_OVERLAY_PORTS=/Users/qing/Documents/myself/projects/vcpkg-overlays cmake --preset macos-arm64
PATH=/opt/homebrew/bin:$PATH VCPKG_ROOT=/Users/qing/Documents/myself/projects/vcpkg VCPKG_OVERLAY_PORTS=/Users/qing/Documents/myself/projects/vcpkg-overlays cmake --build --preset macos-arm64-release --parallel 1
PATH=/opt/homebrew/bin:$PATH VCPKG_ROOT=/Users/qing/Documents/myself/projects/vcpkg VCPKG_OVERLAY_PORTS=/Users/qing/Documents/myself/projects/vcpkg-overlays ctest --preset macos-arm64-release --output-on-failure
~~~

历史 `f856aac` 基线结果是 119/119，Task 19 历史 checkpoint 是 123/123。Task 20 的
`3601da2` 基线结果为 157/157；当前 Task 21 `9adb455` 在正确 overlay 下的权威本地结果是：

~~~text
configure: 通过
Release build（串行，--parallel 1）: 通过
focused navigation tests: 12/12
full CTest: 165/165
canvas_macos_skia_frame_plan_test: 5/5
canvas_macos_composition_layer_stack_test: 2/2
canvas_macos_appkit_frame_scheduling_test: 1/1
macOS source/contracts: 全部通过
canvas_macos_wkwebview_surface_test: 7/7
macOS navigation seam: 4/4
macOS source/runtime contracts: 6/6
x86_64 Objective-C++ strict syntax: 通过
clang static analyzer: 无诊断
git diff --check: 通过
~~~

Task 20/21 独立 reviewer 最终 PASS，无 P0/P1；review 中发现的 y-up/top-left 坐标 P1 已通过
flipped embedded container 修复，Task 21 又覆盖了 latest-wins、同步 reentry、late URI、
close/reentry 和 package-root file。这里的 165/165 是本地 arm64 验证，不是 GitHub macOS CI：
PR #2 没有 macOS runner。HTTPS redirect 专项 runtime、真实 GUI/硬件、Electron/IME 和 <50 ms
仍 pending。历史截图
证据仍在 `docs/tdd/task-16-macos-appkit-scheduling-green.txt`，Task 21 证据在
`docs/tdd/task-21-macos-wkwebview-navigation-{red,green}.txt`；截图文件本身可能已清理，
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

接入前的独立只读审查（2026-08-01）还记录了几个事务层 P1 风险：现有
`EmbeddedSurfaceManager::sync()` 会直接销毁/替换 live surface，不能拿来充当 candidate
document 的原子 staging；必须有独立 candidate surface 集合和 commit/discard。平台回调入口
必须严格 gate 当前 active generation/token，旧 generation 或重复 completion 不能把 256-slot
Inbox 填满并饿死当前 batch；Inbox Full 必须转成明确 rollback/failure。Batch/Inbox 当前是
单线程契约，平台非 UI 回调必须 marshal 到 owner 线程或增加同步/TSAN contract。shutdown
顺序应是 invalidate generation、停止 callback/timer、释放 pending wake，再 clear Inbox 和
销毁窗口。上述风险尚未接入实现，不能把 Task 17/18 描述成 WhiteboardApp 原子加载已完成。

## 5. 当前未完成项和已知风险

### 5.1 Windows WebView2 runtime 修复已完成

Windows 修复序列 `87ebf01` → `f8478b2` → `eb1d948` 已全部 scoped commit 并推送。
最终独立 reviewer PASS、无剩余 P0/P1/P2；portable seam、navigation/initial-load source
contracts 和 `git diff --check` 通过，随后 run 30697532044 在 Windows runtime 上完成
200/200 CTest 及全部后续 workflow 步骤。以下串行模型和历史诊断仍保留，供后续修改
WebView2 时理解为何不能回到 URI-ledger 或多 outstanding Navigate 模型。

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

`eb1d948` 已包含 getter/`put_Cancel` 同步重入、same-document/no-start 消息冲刷、startup
message generation 保留和状态传播实现。后续修改不能回退这些不变量；独立审查至少要继续逐项确认：

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

历史排查 `E_INVALIDARG` 时重点检查过以下调用/事件路径；若未来同类回归出现，应继续保留
这些可观测证据：

- `onNavigationStarting()` 中 bounded URI view、navigation policy 与 canonicalization 的每个 HRESULT；
- `SourceChanged` 中 `get_Source()`、WebView identity/epoch 校验及 `canonicalDocumentUri()`；
- `onNavigationCompleted()` getter 的失败归因；
- WebView2 实际返回的 `data:` source 是否为空、超长、带 fragment 或使用不同 spelling；
- `copyOpaqueDocumentUri()` / `canonicalDocumentUri()` 是否错误拒绝有效 data URI；
- 初始 `data:` 导航是否应完全绕开 URLMon/COM canonicalization；
- 日志/测试断言应标明失败的具体 operation，不能把任意 HRESULT 都压缩成 surface Failed。

历史失败日志见 run 30694257905 和 30696437691；当前权威结果见第 2 节 run 30697532044。
Windows 已可标为自动化 GREEN，但真实 i5-1235U 触控延迟、GUI、IME、网页/视频观感和
unsigned portable 包安装体验仍必须保持 pending。

### 5.2 macOS Task 19/20 已提交、rebase 并推送

Task 19 的 RED 证据仍在 `docs/tdd/task-19-macos-layer-stack-red.txt`，GREEN evidence 已由
`ff03ed2`（rebase 后 SHA）提交，生产实现由 `d65afe9` 提交。实现提供：

- `CanvasCompositionView` 固定 Base Metal → embedded container → Overlay Metal sibling 顺序；
- Base opaque 白底且仅绘 Base，Overlay transparent 且仅绘 Annotation + Chrome；
- 两个 surface 共享 device/queue/Ganesh context/renderer，同时独立 present/invalidation；
- resize、Retina scale、attach/detach/reattach、无 drawable retry、无 busy loop；
- 默认 overlay hit-test，显式 embedded interaction 后切到中间 container；
- macOS demo 实际使用 composition host。

Task 19 历史验证为 full CTest 123/123、frame-plan 5/5、composition integration 2/2、
AppKit scheduling 1/1、所有 macOS contracts 通过、composition integration 重复 10 次通过、
x86_64 Objective-C++ strict syntax 通过、clang static analyzer 无诊断、`git diff --check`
通过。独立 reviewer 为 Conditional PASS，无 P0/P1 blocker。

只读复核仍应保留以下 P2/后续边界，不能把 Task 19 描述成完整 macOS 白板：

- `embeddedInteractionEnabled` 当前把整个区域交给空的 embedded container；接入真实 WKWebView/InputRouter 时，应按实际 child 命中和 PointerKind 路由，避免空白区域吞掉 pen/viewport 输入。
- Task 20 已有真实 WKWebView child 的最小 host；Task 21 已增加首文档 navigation/Ready，但没有 focus/IME、视频/网页内容同步、pointer adapter 或 Electron IPC；三层宿主仍只是这些能力的容器。
- runtime test 验证层顺序、opacity、共享资源和 frame commit，但还没有逐像素 readback 证明透明 overlay 不污染中间内容；source contract 也是结构性检查。
- `MetalRenderResources` 的每个 `shared_ptr` 拷贝都可能成为最后 owner，最终释放必须发生在 AppKit 主线程；当前 header 已说明此约束，未来 surface factory 也必须遵守。
- Task 19 GREEN evidence 已在 `ff03ed2`，Task 20 GREEN evidence 在 `15d61f1`/`3601da2`，Task 21 GREEN evidence 在 `9adb455`；历史 evidence 不能替代新基线验证。
- README 已按 Task 19/20/21 更新，明确 navigation/Ready 与内容同步、输入、Electron 之间的边界。
- 两个 surface 当前在 AppKit 主线程串行使用共享 `SkiaRenderer`；若未来把渲染移到异步线程，必须增加同步或拆分 renderer/context，不能直接复用当前无锁对象。
- PR #2 现在以 Windows SHA `eb1d948` 为 base、远端头为 `9adb455`；没有 GitHub macOS CI，本地 165/165 不能替代未来真实 macOS CI/硬件证据。

Task 20 已增加最小 WKWebView child：主线程生命周期、attach/detach、bounds、visibility、
interaction gate 和 flipped top-left 对齐。Task 21 在其上增加首文档 navigation/Ready：
HTTPS、canonical package-root file、bounded opt-in data URL，action/response 双 policy，
latest-wins、同步 reentry identity、late completion URI、weak delegate 和 close 状态。独立
reviewer PASS 后以 `9adb455` 单独提交并推送 PR #2；overlay 下 fresh full CTest 165/165，
focused navigation 12/12。真实 macOS GitHub CI、HTTPS redirect 专项 runtime、Electron/IME
和硬件 GUI 仍 pending。后续不得把 EmbeddedLoadBatch/Inbox 混入该已收口提交。

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

- macOS：Task 21 已实现并推送首文档 navigation/Ready 与 fail-closed policy；Pointer/pen/touch/IME、Electron 控制、视频/富文本内容同步和 HTTPS redirect 专项 runtime 仍未实现或验证。
- Android/iOS：没有 platform layer、输入适配、Metal/Vulkan/Skia host 或发布流程。
- 多人在线协作：没有网络协议、服务端、CRDT/OT、冲突解决、Presence、权限、离线合并。
- 性能：没有 50 ms 端到端测量；需要在 i5-1235U 触控大屏上测物理接触到可见墨迹的 p50/p95/p99，而不是只看 API timestamp。
- macOS workflow/Release：当前只有 Windows GitHub Actions；macOS 只能本机验证。
- Release：当前 v0.1.0-alpha.1 是旧提交的 unsigned Windows 包，不能代表最新代码。

## 6. 推荐接手实施顺序

### 阶段 A：保全现状

1. 把全部本地已提交引用打成 Git bundle。这样 macOS 本地 commits、e6148a2、0258173 即使没有远端分支也能恢复：

   ~~~bash
   git -C /Users/qing/Documents/myself/projects/canvas-task16 bundle create /tmp/canvas-local-refs.bundle --all
   git -C /Users/qing/Documents/myself/projects/canvas-task16 bundle verify /tmp/canvas-local-refs.bundle
   git -C /Users/qing/Documents/myself/projects/canvas-task16 bundle list-heads /tmp/canvas-local-refs.bundle
   ~~~

2. 当前 Windows/macOS 源码均已推送且 worktree clean，不需要源代码 patch；仍可用下面命令
   验证 macOS 没有 tracked WIP（输出文件应为空）：

   ~~~bash
   git -C /Users/qing/Documents/myself/projects/canvas-macos diff HEAD --binary > /tmp/canvas-macos-tracked-wip.patch
   ~~~

3. 当前 macOS 没有 untracked WIP，不需要 tgz；本 `handoff.md` 在独立文档提交推送前仍应另存一份：

   ~~~bash
   cp /Users/qing/Documents/myself/projects/canvas-task16/handoff.md /tmp/canvas-handoff.md
   ~~~

4. 复制 bundle 和 handoff 副本到新账号可访问的安全位置，并运行 bundle verify；最好同时保存 SHA-256。备份文件不应提交到 Canvas 仓库，也不要放在会被自动同步到公开网盘的目录。
5. 不要把 macOS Task 20、e6148a2/0258173 或 handoff 文档混在一个提交里；每项使用自己的明确白名单。
6. 先在本地执行 git diff --check，再按文件白名单 stage；不要 git add -A。

### 阶段 B：Windows runtime 修复与 CI（已完成）

1. `87ebf01` 建立“最多一个 issued + 一个 latest deferred”的串行导航模型，删除旧 URI
   ledger，并加入 URL canonicalization、COM getter/reentry 与 generation/epoch 防护。
2. `f8478b2` 修复 fragment navigation liveness；run 30696437691 将失败缩小到 2/200。
3. `eb1d948` 保留 startup promotion 的正确 host-message generation，并把 opaque `data:`
   fragment 快路径收紧到 completed + active id + committed/current source + same key。
4. 实现代理完成后由独立 reviewer 发现并推动修复 stale source-current P1；同一 reviewer
   最终 PASS，无 P0/P1/P2。
5. run 30697532044 的 Configure、Build、200/200 CTest、Composition、packaging contract、
   whitespace、metadata、portable package 和 artifact upload 全部通过。
6. 后续只有在修改 Windows/WebView2 路径后才需要重新执行下面的完整命令和 workflow；
   不能因当前绿色而跳过新提交的验证。
7. 硬件/GUI/IME/video/i5-1235U <50 ms pending 仍不能改成 GREEN。

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

### 阶段 C：收敛 macOS Task 19/20/21 并更新 PR #2（已完成）

1. Task 19 已由 `ff03ed2`/`d65afe9` 完成 evidence 与实现提交；Task 20 已由 `d5d51e8` 完成。
2. 独立 reviewer 检查了 ARC/PImpl ownership、AppKit 主线程最后释放、weak parent、
   attach/detach/reattach/close、points 几何、visibility/hit-test gate 和层级；坐标 P1 修复后
   同一 reviewer 最终 PASS。
3. Task 20 rebase 后本机结果为 configure/build、focused 3/3、full CTest 157/157、contracts
   5/5、diff-check 全部通过；Task 21 `9adb455` 又以 overlay 重跑 configure、串行 build、
   focused 12/12、full CTest 165/165。
4. 已按白名单提交 evidence，rebase 到 `eb1d948`，并使用显式 `--force-with-lease` 更新 PR #2。
   5. run 30698953072 已绿色，Task 20 阶段完成；Task 21 `9adb455` 已推送，本地 overlay
   configure/build、focused 12/12、full CTest 165/165 均通过。run 30702348950 也已完成并全绿；
   下一阶段是审查 EmbeddedLoadBatch/CompletionInbox 并实现 WhiteboardApp 原子加载事务。

   复现命令（未来修改 macOS 后仍需执行）：

   ~~~bash
   PATH=/opt/homebrew/bin:$PATH VCPKG_ROOT=/Users/qing/Documents/myself/projects/vcpkg VCPKG_OVERLAY_PORTS=/Users/qing/Documents/myself/projects/vcpkg-overlays cmake --preset macos-arm64
   PATH=/opt/homebrew/bin:$PATH VCPKG_ROOT=/Users/qing/Documents/myself/projects/vcpkg VCPKG_OVERLAY_PORTS=/Users/qing/Documents/myself/projects/vcpkg-overlays cmake --build --preset macos-arm64-release --parallel
   VCPKG_ROOT=/Users/qing/Documents/myself/projects/vcpkg ctest --preset macos-arm64-release --output-on-failure
   ~~~


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
3. 在 Task 20 最小 WKWebView host 之上实现 navigation delegate/Ready、Web/RichText/Video
   lifecycle 与 surface factory，再按 child hit + PointerKind 接交互；
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

macOS 命令必须在 `canvas-macos` worktree 执行；Windows `eb1d948` 的
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
src/platform/macos/wkwebview_surface.*   Task 20 最小 WKWebView lifecycle/geometry host
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
- [x] 确认 `eb1d948` 的 Windows run 30697532044 已绿色：200/200 CTest、Composition、package、whitespace、metadata、artifact 全部通过。
- [x] macOS Task 19/20 已提交、rebase、推送，Task 19 RED/GREEN 与 Task 20 GREEN evidence 均在远端；Windows URL/COM 修复已在 `eb1d948` 远端。
- [ ] Windows URL canonicalization、COM reentrancy、SourceChanged stale identity 有独立测试和复审记录。
- [x] Windows full CI 的 Build、CTest、Composition、package、whitespace、artifact 已全绿并写入本文件 evidence。
- [x] macOS 三层宿主/WK surface 在 Task 20 基线 full CTest 157/157，Task 21 `9adb455` 已独立 review、提交并推送；fresh full CTest 165/165、focused 12/12 已记录。
- [x] PR #2 Task 20 head `3601da2` 的 Windows Build run 30698953072 已全绿，artifact `canvas-windows-x64-pr-2-f3edd04012ef` 已记录。
- [x] PR #2 Task 21 head `9adb455` 的 Windows Build run 30702348950 已完成并全绿；build job `91375451176`，release job `91375736804`（PR 事件 skipped），artifact `canvas-windows-x64-pr-2-0459b8f1a9e6`，artifact id `8819253543`，约 3,167,064 bytes，未过期。
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

- canvas-task16 位于 a9912da，Windows 源代码 clean（交接文档单独提交）；
- canvas-macos 位于 9adb455，与远端一致且 clean，Task 19/20/21 代码与 evidence 已可从 GitHub 获取；
- embedded-batch 位于 e6148a2 且 clean；
- completion-inbox 和 atomic-open 都位于 0258173 且 clean；
- 不应出现来源不明的新改动。如果实际状态不同，先更新本文件中的状态快照或查清改动来源，再开始写代码。

随后只对当前优先任务做定向检查。Windows branch 当前 docs head 是 `a9912da`，macOS
branch 当前 Task 21 head 是 `9adb455`；先读最新绿色 run 30702348950 证据，重点核对 PR #2
Windows run：

~~~bash
cd /Users/qing/Documents/myself/projects/canvas-task16
git show --stat --oneline eb1d948
gh run view 30697532044 --repo Mostorm-Labs/canvas --json jobs,headSha,conclusion,url
gh api repos/Mostorm-Labs/canvas/actions/runs/30697532044/artifacts
sed -n '1,260p' src/platform/windows/webview2_navigation_uri.h
sed -n '1,320p' tests/unit/webview2_navigation_uri_test.cpp

git -C /Users/qing/Documents/myself/projects/canvas-macos status --short
gh run list --repo Mostorm-Labs/canvas --branch codex/macos-platform \
  --workflow 319742650 --limit 5 \
  --json databaseId,status,conclusion,headSha,url
~~~

在理解 macOS WIP 和 Windows runtime failure 前不要运行格式化全仓库、自动修复、rebase、
stash pop 或任何清理命令。构建目录和依赖恢复可以重建，源码工作树中的未提交内容不可以。

### 11.2 如果新账号在另一台机器

先在旧机器按第 6 节阶段 A 生成并复制以下文件；Windows `a9912da` 与 macOS `9adb455`
已在 GitHub，仍未推送的只有本地-only e6148a2/0258173 对象：

~~~text
canvas-local-refs.bundle
（当前 macOS clean，因此这两个 patch/archive 文件通常不需要生成）
canvas-handoff.md (如果 handoff commit 尚未推送)
~~~

在新机器上先验证而不是直接覆盖源码：

~~~bash
git bundle verify /secure-transfer/canvas-local-refs.bundle

git clone --branch codex/windows-vertical-slice https://github.com/Mostorm-Labs/canvas.git canvas-windows
cd canvas-windows
git fetch /secure-transfer/canvas-local-refs.bundle 'refs/heads/*:refs/remotes/handoff/*'
git log --oneline --decorate --all --max-count=30
~~~

确认能在 refs/remotes/handoff/ 下看到 e6148a2、0258173 后，再分别创建 worktree。macOS 可直接从
GitHub 的 `codex/macos-platform` checkout；示例路径可按新机器调整：

~~~bash
git worktree add -b codex/macos-platform-local ../canvas-macos refs/remotes/handoff/codex/macos-platform
git worktree add -b codex/embedded-load-batch-local ../canvas-embedded-batch refs/remotes/handoff/codex/embedded-load-batch
git worktree add -b codex/embedded-completion-inbox-local ../canvas-completion-inbox refs/remotes/handoff/codex/embedded-completion-inbox
git worktree add -b codex/atomic-document-open-local ../canvas-atomic-open refs/remotes/handoff/codex/atomic-document-open
~~~

分支名带 -local 是为了避免和 clone 后已经存在的远端跟踪分支发生歧义。对于未来仍存在的 patch/tgz
备份，先执行 `git apply --check`，并在临时 staging 目录比较 untracked 文件；不要直接在仓库根目录解压。

恢复本地-only 对象后至少验证：

~~~bash
git fsck --full
git show --stat --oneline 9adb455
git show --stat --oneline e6148a2
git show --stat --oneline 0258173
~~~

确认 fsck 不报告缺对象或损坏，并且 macOS `9adb455` 与两个本地-only SHA 都可读取；若另有
patch/tgz 备份，再确认它们可恢复后才允许旧账号删除本机 worktree。

### 11.3 建议复制给新 Codex 账号的首条任务说明

~~~text
接手 Mostorm-Labs/canvas。工作区在
/Users/qing/Documents/myself/projects/canvas-task16，先完整阅读 handoff.md，
再只读检查所有 canvas-* worktree、PR #1、PR #2 和最新 CI。保留所有已有
tracked/untracked WIP，禁止 reset --hard、checkout --、clean、git add -A。

当前优先审查/推送 EmbeddedLoadBatch + CompletionInbox 并规划 WhiteboardApp 原子文档加载事务。
PR #2 Task 21 的 Windows Build run 30702348950 已全绿。macOS Task 21 已完成 navigation/Ready，
但不包含 Pointer/IME、Electron。硬件、GUI、视频和 <50 ms 触控证据仍保持
pending；不要把 e6148a2/0258173 或 atomic-open 改动混入已绿色分支。
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
