# Canvas 项目交接文档

> 文档版本：2026-08-04
>
> 快照时间：2026-08-04（Asia/Shanghai）
>
> 仓库：<https://github.com/Mostorm-Labs/canvas>
>
> 这是事实快照，不是对未来状态的保证。新账号接手后必须先执行本文件第 2 节的核对命令；若本文件与 Git、PR 或 Actions 冲突，以实时结果为准并先修正文档。

## 0. 一页结论

Canvas 当前是一个“共享 C++ 文档核心 + 各平台原生输入/窗口 + Skia 渲染 + 原生 WebView 承载网页、视频和富文本”的跨端白板垂直切片，不是已经完成的商业化多人协作产品。

当前最重要的事实：

- 截至本快照，Windows 主开发分支 `codex/windows-vertical-slice` 的远端 ref 是 docs commit `3519aca46f7167aee18ecd236c5b6e709b5fffad`，最后生产代码 commit 是 `ad3a49954aac071928f18b4fe1499af541802d6b`，PR #1 为 Draft/Open。
- Windows 已具备 Win32 pen/touch 输入、Skia/D3D12/DirectComposition、WebView2 嵌入内容、文档存储、命名管道 IPC、Electron 控制样例和 portable ZIP 工作流。
- Windows 的 IPC `open-document` 已实现隐藏候选 WebView 的原子加载事务；失败、超时或被新请求替代时保留旧 Document 和旧 surface。
- Windows docs commit `3519aca` 的已保存 CI 是 run `30793067680`；原子打开生产代码 `ad3a499` 的已保存代码 run 是 `30745845408`。两次运行的 Build、CTest、Composition、打包契约、whitespace、metadata、portable ZIP 和 artifact 上传均通过。
- 截至本快照，`637ca6ccff5e3f8b83de1d75c6b97078aec9fdc1` 是 macOS 最后经过独立 review 并由 Hosted macOS/Windows 双绿验证的生产代码 branch commit；PR #2 为 Draft/Open/MERGEABLE。
- macOS 已具备 AppKit + CAMetalLayer + Skia Ganesh、固定合成层级、WKWebView navigation/Ready，以及 AppKit left-mouse → 原生 session/controller → Document → 分层 Metal commit 的可运行垂直切片。
- Task 23D `637ca6c` 新增安全 tablet input seam：保留 pressure、device identity/capabilities、两轴 scaled tilt 与 tool intent，合并 native/associated 输入并去重，且在 mouse/tablet modality 切换和生命周期边界清理 session。
- Task 23D 的 tablet output 仍被有意丢弃，不进入 pointer delegate 或 Document；因此它不是可见 Pen/Eraser、Interact routing、真实 tablet 硬件或延迟证据。下一项是 Task 23E 可见 Pen 桥接。
- 最后针对 `637ca6c` 生产代码树验证通过的 macOS arm64 Hosted run 是 `30873272912`；配套 Windows run `30873272916` 也全绿并产生 artifact `8878654542`。
- 最后与上述生产代码对应、且被两套 Hosted run 验证为绿色的 PR #2 merge tree 是 `5e0cdc735fded5757abe3094a2a5e94e8d3bc7ae`，由 Windows `3519aca` 与 macOS `637ca6c` 合成。
- `7177a07`、`c0656a` 和 `308046...` 只保留为 Task 23C 历史证据。任何 docs-only 或生产代码提交一旦推送，PR #2 的 head SHA、merge-ref SHA 和新触发的 run 都会变化；必须用第 2.4、11、12 节命令动态核对。
- Windows 基线已由 merge commit `c7553af2c3083119950481adf3cdff9cdb2d5170` 普通合入 macOS 分支；快照时 merge-base 是 `3519aca`，不再需要执行旧文档里的“先同步 Windows base”步骤。
- 当前没有可宣称的真实 i5-1235U 触控屏 p95 `<50 ms` 证据，也没有 Windows Electron GUI E2E、macOS 真实硬件 input/IME、Android/iOS 或多人协作服务端；Task 23C 的 synthetic mouse tests 不能替代这些验收。

接手后的推荐顺序：

1. 核对两个 PR 和实时 Actions 状态，再下载与希望验证的 Windows headSha 精确对应的 artifact。
2. 先设计并验证 Task 23E 可见 Pen 桥接：保持 Task 23D 的 tablet 语义，不丢失 tool/scaled tilt，只有明确 Pen/Ink 路径可以进入 Document；Eraser/Interact 继续 fail-closed。
3. 补 macOS Electron/native IPC；不要把逐点输入放进 Electron IPC。
4. 统一 `--open`、`create-embedded` 与 IPC `open-document` 的异步 Ready/Failed 事务，并设计 `embedded-state` 事件。
5. 在真实 Windows 触控大屏做 Electron GUI、WebView2、视频、中文 IME、层级和 `<50 ms` 延迟验收。
6. 再进入 Android/iOS、协作协议/CRDT、Presence 和后端。

## 1. 产品目标与架构约束

### 1.1 目标设备与体验

- 首要目标平台：Windows 触控大屏。
- 目标 CPU：Intel Core i5-1235U。
- 体验目标：肉眼跟手，端到端触控到可见墨迹 p95 小于 50 ms。
- 后续平台：Windows + Android，再扩展到 Windows + macOS + Android/iOS。
- Launcher 使用 Electron；白板原生进程由 Electron 启停和发送低频控制命令。

`<50 ms` 是硬件验收门，不是单元测试或 API 时间戳可以证明的指标。必须用 240 fps 或更高帧率摄像机同时拍到物理触碰和屏幕像素，并报告至少 30 次笔画的 p50/p95/p99。

### 1.2 已确认的架构方向

```text
Electron Launcher
  └─ 低频控制：启动、模式、创建对象、保存、打开、退出
      └─ 命名管道 IPC
          └─ 原生白板进程
              ├─ 共享 C++ 文档/笔画/几何/序列化核心
              ├─ 平台原生 pointer、pen、touch、IME 适配
              ├─ Skia GPU 渲染层
              └─ WebView2 / WKWebView 嵌入层
                  ├─ HTTPS 网页
                  ├─ HTML5 视频
                  └─ Lexical 富文本
```

必须保持的边界：

- pointer/stroke 热路径留在原生进程，不能逐点经过 Electron、JSON 或命名管道。
- Electron 只负责低频控制和进程生命周期。
- Document 是可持久化事实源；WebView 是由 Document 恢复的宿主，不是文档事实源。
- WebView 内容层和 Skia 墨迹层可以固定层级。当前实现是嵌入内容在下、原生墨迹/标注在上，并支持移动、缩放和在其上批注。
- 网页、视频和富文本可以嵌入，但跨设备同步必须同步结构化状态或资源引用，不能同步视频帧或原始 pointer 样本。

## 2. 仓库、PR 和工作树

### 2.1 远端引用

| 引用 | 快照 SHA | 用途 |
|---|---|---|
| `origin/main` | `cd445fc4d24b849944958a6b108187727023d520` | 初始基线；尚未合并两个 Draft PR |
| `origin/codex/windows-vertical-slice` | `3519aca46f7167aee18ecd236c5b6e709b5fffad` | Windows PR #1；`ad3a499` 生产代码后的 handoff docs head |
| `origin/codex/macos-platform` | `637ca6ccff5e3f8b83de1d75c6b97078aec9fdc1` | Task 23D；快照时最后独立 review 且 Hosted 双绿的生产代码 branch commit。handoff docs push 后以实时 ref 为准 |

### 2.2 PR

| PR | 分支 | 状态 | 说明 |
|---|---|---|---|
| [#1](https://github.com/Mostorm-Labs/canvas/pull/1) | `codex/windows-vertical-slice` → `main` | Draft/Open，mergeable | Windows 垂直切片和共享核心 |
| [#2](https://github.com/Mostorm-Labs/canvas/pull/2) | `codex/macos-platform` → `codex/windows-vertical-slice` | Draft/Open，mergeable | macOS 平台层；PR CI 测试 merge ref |

### 2.3 快照时的主要本机 worktree

| 路径 | 分支 / 快照 HEAD | 用途 |
|---|---|---|
| `/Users/qing/Documents/myself/projects/canvas-task16` | `codex/windows-vertical-slice` / `3519aca` | Windows 主线；包含上一版 handoff 快照 |
| `/Users/qing/Documents/myself/projects/canvas-macos` | `codex/macos-platform` / 生产基线 `637ca6c`；本 handoff docs commit SHA 用 `git rev-parse HEAD` 获取 | Task 23D 已提交并双绿；另有 Task 23E bridge/CMake/tests 未提交 WIP，仍属 in progress，不得随文档暂存或误报完成 |
| `/Users/qing/Documents/myself/projects/canvas-atomic-open-v2` | `codex/atomic-document-open-v2` / `844c27b` 起的原始实现链 | 历史实施 worktree；内容已经以 scoped commits 合入 Windows 分支，不能再当作待合并来源 |

其他旧 worktree 和 local-only 分支只保留历史研究价值。不要从旧 handoff 快照继续 cherry-pick `EmbeddedLoadBatch`/Inbox；它们已经在 Windows 分支中。

### 2.4 接手后第一组命令

```bash
cd /Users/qing/Documents/myself/projects/canvas-task16
git fetch --all --prune
git status --short --branch
git worktree list
git log --graph --oneline --decorate --all --max-count=60
git rev-parse HEAD origin/codex/windows-vertical-slice

gh pr view 1 --repo Mostorm-Labs/canvas \
  --json state,isDraft,headRefOid,baseRefOid,mergeable,mergeStateStatus,url
gh pr view 2 --repo Mostorm-Labs/canvas \
  --json state,isDraft,headRefOid,baseRefOid,mergeable,mergeStateStatus,url
PR2_MERGE_SHA="$(gh api repos/Mostorm-Labs/canvas/git/ref/pull/2/merge --jq '.object.sha')"
test -n "$PR2_MERGE_SHA"
gh api "repos/Mostorm-Labs/canvas/commits/${PR2_MERGE_SHA}" \
  --jq '{sha: .sha, parents: [.parents[].sha]}'
gh run list --repo Mostorm-Labs/canvas --limit 20
```

`PR2_MERGE_SHA` 必须现场获取。docs-only 或生产代码 push 都会重建 `refs/pull/2/merge`；不要把第 5.2 节保存的 `5e0cdc...` 当成永久 ref。

不要用 `git reset --hard`、`git checkout --` 或 `git clean` 清理不认识的工作树内容。先用 `git status`、`git diff` 和 `git ls-files --others --exclude-standard` 判断归属。

## 3. Windows 当前实现

### 3.1 已实现能力

- 共享 C++17 Document、节点、geometry、embedded transform、stroke builder 和 input router。
- MessagePack 版本化文档序列化；Windows 保存使用临时文件 flush 后原子替换；载入拒绝超过 512 MiB 的文件。
- Win32 pointer/pen/touch 输入适配、capture 和输入路由。
- Skia + D3D12 + swap chain + DirectComposition 渲染。
- WebView2 surface，用于 HTTPS、打包富文本和打包视频页面。
- WebView2 导航安全策略、virtual host、串行导航、initial Ready/Failed 跟踪、关闭和 late-callback 防护。
- 固定合成层级：嵌入内容位于原生墨迹/标注层之下；对象可移动和缩放，画笔可在其上批注。
- 命名管道服务端、认证 session、bounded IPC envelope、断开/重连 generation 隔离。
- Electron host 样例，负责进程启停和低频命令。
- Windows portable ZIP、30 天 Actions artifact 和 `v*` tag Release 工作流。

### 3.2 IPC 原子 `open-document`

Windows HEAD 中相关 scoped commits：

| Commit | 内容 |
|---|---|
| `195ce29` | 原子打开嵌入文档的主体实现 |
| `058d063` | 强化 admission、上限、同步回调和 timer 行为 |
| `38255df` | staging 失败时保留旧输入状态，commit 时才切断旧路由 |
| `f973c47` | 为派生 native event requestId 加 256-byte 预算 |
| `ad3a499` | diagnostics 恢复原请求 ID，保持准确关联 |

已实现语义：

- `EmbeddedLoadBatch` 汇总多个候选 surface 的 Ready/Failed。
- `EmbeddedLoadCompletionInbox` 是有界完成队列，由 `WM_APP` 消息在 UI 线程 drain。
- 打开新文档前先预检嵌入节点数量；最多接受 256 个，257 个会在创建 WebView 前拒绝。
- 候选 WebView2 surface 隐藏创建和导航；只有全部 Ready 才提交 Document、显示候选并替换旧 surface。
- 任一候选失败、30 秒超时、事务取消或被更新的打开请求 supersede 时，候选全部清理，旧 Document 和旧 surface 保持可用；应用 shutdown 则使 callback 失效并按正常生命周期销毁全部资源，不产生 late commit。
- generation/token 隔离旧回调；callback state 使用 weak/invalidation，避免 close/failure/commit 后访问悬空对象。
- 处理 `Navigate()` 或 `PostMessage()` 同步回调重入；当前调用栈退出后再统一清理失败事务。
- timer ID 绑定 generation，旧队列里的 timer 不能杀掉新请求。
- staging admission 成功后 response 立即且只发送一次；真正 commit 只发 `document-state`。异步失败或 supersede 通过 diagnostics 报告，不重复 response。
- 打开 pending 期间拒绝会并发修改 Document 的 IPC 命令和新的 pointer/mouse edit；旧输入/capture 只在成功 commit 时统一取消，失败时不破坏旧文档状态。
- 原始 inbound requestId 由 decode 保证非空且 UTF-8 不超过 256 bytes；`state-`/`fatal-` 派生 ID 超预算时安全回退，diagnostics 始终保留原 ID 以便关联。

平台无关 coordinator 主要作为测试 seam；`WhiteboardApp` 的实际集成直接使用 Batch、Inbox 和平台 surface lifecycle。不要假定 coordinator 是唯一生产入口。

### 3.3 原子打开仍未覆盖的入口

- 启动参数 `--open` 仍走同步 surface restore，不等待每个 surface Ready。
- `create-embedded` 仍是同步 admission 路径，没有使用同一事务抽象。
- 尚未定义稳定的 `embedded-state` Ready/Failed 事件协议。
- 没有真实 Windows GUI/WebView2 runtime 证据证明多 surface 的异步失败、超时、supersede 和回滚体验。
- 建议补一个 WhiteboardApp/IPC 端到端测试：256-byte `open-document` requestId 的 diagnostics 必须保留原 ID，`state`/`fatal` 事件仍可编码。

## 4. macOS 当前实现

### 4.1 已实现能力

- `canvas_macos.app` AppKit 应用入口。
- `CAMetalLayer` + Metal command queue + Skia Ganesh 的原生渲染宿主。
- AppKit invalidation/scheduling 和 resize 生命周期。
- 固定的原生合成层栈。
- `WKWebViewSurface` 宿主、HTTPS/package-root file/受限 data URL 策略。
- latest-wins navigation generation、同步 reentry identity、late completion URI、weak delegate 和 close 防护。
- 首文档 navigation/Ready 生命周期与 AppKit/Metal/WKWebView 自动化测试。
- AppKit left-mouse 输入：top-left logical-point 坐标、单 stroke session ID、Cancel、read-only/editable Document ownership、Base/Annotation 路由和分层 Metal invalidation/commit。
- 活动 preview 在 mode/document 切换、window/app resign、window close、detach 和 ARC teardown 时回滚；controller 用 Document instance/cache identity/revision 防止擦除被外部替换的同 ID 节点。
- 安全 tablet seam：同步提取 AppKit tablet point/proximity，保留 pressure、device identity/capabilities、scaled tilt、tangential pressure、rotation、button mask 和 Pen/Cursor/Eraser/Unknown tool intent；固定容量 session 对 native/associated 点去重，并在 proximity、生命周期及 mouse/tablet modality 切换时 exactly-once cleanup。
- `macos-14` arm64 Hosted workflow，锁定 vcpkg `builtin-baseline`，把非 GUI 测试和 GUI/Metal/WKWebView/MouseInput 测试分成必需 gate，并逐套件 fail-closed discovery。

### 4.2 尚未实现能力

- 真实 AppKit event-dispatch 与真实 mouse/trackpad 硬件证据；现有 MouseInput GUI 测试通过直接调用 responder 方法构造 synthetic `NSEvent`。
- tablet sample 到白板/Document 的可见 Pen 桥接；Task 23D 输出仍被 fail-closed 丢弃。
- 可见 Eraser 语义、Interact routing、touch、coalesced/predicted samples 和完整 capture 语义。
- 中文 IME 与富文本真实输入验证。
- macOS Electron/native IPC 与进程生命周期。
- macOS 文档打开的完整原子 candidate-surface 事务。
- macOS portable/DMG、签名、公证和 Release artifact。
- 真实 macOS GUI/硬件输入体验与延迟证据。

### 4.3 Task 23A/B/C/D：macOS 输入提交链

本节的“内部独立审查”“reviewer”和“复审 PASS”指本次实施过程中由不同子代理完成、并由本 handoff 持久化记录的内部审查，不是 GitHub PR Review，也没有独立的 GitHub review URL。Git commit 与测试可直接复核；若接手方需要更强的审计链，应重新独立审查并把报告作为仓库 evidence 提交。

| Task / Commit | 内容 | 审查与证据 |
|---|---|---|
| Task 23A `aeb53ef` | `RawMacMouseEvent`、逻辑点坐标归一化、`MacosMouseSession`、稳定 pointer ID 与 exactly-once Cancel seam | 平台无关 pointer/session tests；不声称真实硬件输入 |
| Task 23B `c0ca19b` | `MacosWhiteboardInput`：InputRouter → preview stroke → Document，Base/embedded Annotation 路由 | 纯 C++ controller tests |
| Task 23B fix `47f752a` | 用 cache identity 保护 preview ownership，旧 owner 不擦除同 ID replacement | 本 handoff 记录的内部 review finding 修复 |
| Task 23B fix `c4fcd69` | 绑定 Document instance/revision/non-append revision，外部赋值或 mutation 时 fail-closed | 本 handoff 记录的内部 reviewer 最终 PASS |
| Task 23C `5228d02` | AppKit overlay mouseDown/Dragged/Up → session/controller → Document；editable setter；按 Base/Overlay 分层 invalidate；GUI tests 与 CI regex | 初审发现 4 个 P1，未直接判定完成 |
| Task 23C fix `7177a07` | `fullRedraw` 优先双层、active-dealloc 直接回滚且不依赖 weak delegate、目标层真实 Metal commit、四套 GUI suite 分别 discovery | 本 handoff 记录的原内部 reviewer 复审 PASS，无剩余 P0/P1 |

Task 23C 的关键自动化语义：

- Base 笔画只请求并提交 Base Metal frame；嵌入对象上的 Annotation 只请求并提交 Overlay frame。
- `fullRedraw=true` 即使同时携带 `layer` 也会让 Base/Overlay 都提交，避免一层保留 stale frame。
- composition 析构先清 weak delegate，再直接 take/consume 唯一 Cancel；controller 若仍 active 会直接 rollback，析构期不安排新的 Metal frame。
- read-only setter 不接受 mouse mutation；editable Document replacement 先取消旧 preview。
- App/window resign、window close、detach、Interact mode、重复/孤立/右键事件均有自动化边界。

Task 23C 的本地复审结果：pointer/session/controller 37/37，`MacosMouseInput.*` 11/11，重复 3 轮稳定通过，workflow contract 4/4，non-GUI 207/207，GUI/Metal 21/21；Hosted 历史证据见第 5.2 节末尾。

Task 23D commit `637ca6c`（`feat: add macOS tablet input seam`）已完成独立 review。初审发现 associated tablet point 可能遗留旧 ordinary-mouse preview 的跨 modality P1；修复为先 dispatch Mouse Cancel 再消费 tablet sample，并在 ordinary Mouse Down 前 reset tablet epoch。同一 reviewer 复审 PASS，无剩余 P0/P1/P2。该审查与 RED/GREEN 命令已持久化在 `docs/tdd/task-23d-macos-tablet-seam-green.txt`，不是虚构的 GitHub PR Review。

Task 23D 已验证语义：

- 规范化并保留 pressure、device/pointing/system/unique identity、capability mask、两轴 scaled tilt、tangential pressure、rotation、button mask 和显式 tool intent；Pen 为 future `Ink`，Eraser 为 `EraserPending`，Cursor/Unknown 为 `Unsupported`。
- associated mouse Down 建立 contact；native tabletPoint 只能更新同设备既有 contact，不能制造第二个 Down；完全相同的 native/associated measurement 去重。
- orphan、device mismatch、unproximate、非法 source/phase 和容量溢出 fail-closed；duplicate Down、proximity leave、reset 和生命周期边界提供确定性取消/清理。
- associated tablet 输入会先取消 ordinary-mouse preview；ordinary Mouse Down 会先清空 tablet contact/profile，避免跨 modality stale state。

本地验证：pure tablet 10/10；tablet + 两个 source contracts 12/12；non-GUI 218/218；GUI/Metal 21/21；既有 mouse 11/11 并 repeat-until-fail 3 轮；workflow contract 4/4。Hosted 双绿证据见第 5.2 节。

Task 23D 的有意边界：`MacosTabletSample` 尚未转换为现有 `PointerSample`，因为后者无法无损表达 tool identity 与 scaled tilt；所有 tablet session output 都在 `CanvasPointerMetalView` 中丢弃，不调用 pointer delegate、不修改 Document、不产生可见 Pen/Eraser。它也不证明 Interact routing、真实硬件、pressure 精度或端到端延迟。

### 4.4 macOS CI 提交链

| Commit | 内容 |
|---|---|
| `f1320ab` | 新增 macOS arm64 build workflow |
| `8c82ec2` | 获取 manifest 锁定的 vcpkg baseline |
| `ac2dfce` | 要求 vcpkg checkout HEAD 精确等于 baseline |
| `e0cd6fe` | 记录 Hosted CI 已绿色的证据 |

前两次失败是 workflow/vcpkg checkout 问题，已被修复：

- run `30703942343`：预装 vcpkg checkout 缺少 `builtin-baseline` 对象。
- run `30704136648`：只 fetch 对象但仍在较新 HEAD，port database 与 baseline 不一致。
- run `30704424435`：使用 detached exact-baseline checkout 后首次全绿。

这些历史失败在快照时不是外部 blocker；Task 23D 最后独立 review 且 Hosted 双绿的生产代码 run 见第 5.2 节。

## 5. CI 和可下载产物

### 5.1 Windows PR #1 已保存运行证据

快照时 Windows 分支 ref `3519aca46f7167aee18ecd236c5b6e709b5fffad` 是 handoff docs commit；对应的全绿 run 为：

- Run：<https://github.com/Mostorm-Labs/canvas/actions/runs/30793067680>
- Build job：<https://github.com/Mostorm-Labs/canvas/actions/runs/30793067680/job/91620535022>
- Release job：`91621233144`，PR 事件下 skipped，符合设计。
- Artifact：`canvas-windows-x64-pr-1-9721e47f4b03`，ID `8847852615`，3,181,054 bytes，digest `sha256:72b83241ec76b8583b470e73f8898ca8dfc8de63143d2f8175221bc7765e3e60`，2026-09-02 07:20:27 UTC 过期。

下面的 `30745845408` 是生产代码 `ad3a499` 本身最后保存并验证的代码 run，保留用于原子打开实现追溯：

- Run：<https://github.com/Mostorm-Labs/canvas/actions/runs/30745845408>
- Head：`ad3a49954aac071928f18b4fe1499af541802d6b`
- Build job：<https://github.com/Mostorm-Labs/canvas/actions/runs/30745845408/job/91491193761>
- Release job：`91491513175`，PR 事件下 skipped，符合设计。
- 通过项目：依赖、Web assets、Configure、Build、CTest、Composition integration、release packaging contract、whitespace、metadata、portable package、artifact upload。

Artifact：

- 名称：`canvas-windows-x64-pr-1-648e5711446a`
- ID：`8832876580`
- 大小：3,181,057 bytes
- Digest：`sha256:e53d4287910b161c1068483ee0de25bae8414095d9c99d6a8edecf9c3cfe7902`
- 过期：2026-09-01 11:32:50 UTC；快照时 `expired=false`
- API：<https://api.github.com/repos/Mostorm-Labs/canvas/actions/artifacts/8832876580/zip>

下载：

```bash
DOWNLOAD_DIR="$HOME/Downloads/canvas-windows-ad3a499"
mkdir -p "$DOWNLOAD_DIR"
gh run download 30745845408 \
  --repo Mostorm-Labs/canvas \
  --name canvas-windows-x64-pr-1-648e5711446a \
  --dir "$DOWNLOAD_DIR"
```

解压完整 ZIP 后运行 `canvas_windows.exe`。相邻 `web/` 目录不能删除；当前包未签名，需要 Microsoft Edge WebView2 Runtime，也不包含 Electron launcher。

### 5.2 最后独立复审且 Hosted 绿色的 macOS 生产代码树

本节固定记录 Task 23D `637ca6c` 生产代码 branch commit 的最后已验证证据，不声称它们在后续 docs-only 或生产代码 push 后仍是 PR 的最新 ref 或 run。

macOS Hosted run：

- Run：<https://github.com/Mostorm-Labs/canvas/actions/runs/30873272912>
- Job：<https://github.com/Mostorm-Labs/canvas/actions/runs/30873272912/job/91879415474>
- Head branch：`637ca6ccff5e3f8b83de1d75c6b97078aec9fdc1`
- 实际 checkout：PR merge ref `5e0cdc735fded5757abe3094a2a5e94e8d3bc7ae`
- 通过：arm64 guard、Node/web tests、workflow contract 4/4、exact-baseline vcpkg、Configure、Build、non-GUI 218/218、逐套件 GUI discovery、GUI/Metal 21/21、whitespace。
- Required GUI suite discovery：AppKit scheduling 1、Composition 2、MouseInput 11、WKWebView 7。
- 该 workflow 不生成 macOS app/DMG Release artifact；绿色 run 不能当作可见 Pen/Eraser、Interact routing、真实 mouse/tablet/touch/IME 或延迟证据。

同一 merge ref 的 Windows run：

- Run：<https://github.com/Mostorm-Labs/canvas/actions/runs/30873272916>
- Build job：<https://github.com/Mostorm-Labs/canvas/actions/runs/30873272916/job/91879415215>
- Release job：`91879950084`，PR 事件下 skipped，符合设计。
- Build、完整 CTest、Composition/WebView2 integration、打包契约、whitespace、metadata、portable ZIP 和 upload 均通过。
- Artifact：`canvas-windows-x64-pr-2-5e0cdc735fde`
- Artifact ID：`8878654542`
- 大小：3,181,063 bytes
- Digest：`sha256:32d7d70ebc9b479817cd87955884a69768aac02456b9c5e498ca043400afc90d`
- 过期：2026-09-03 03:00:09 UTC；快照时 `expired=false`
- API：<https://api.github.com/repos/Mostorm-Labs/canvas/actions/artifacts/8878654542/zip>

下载最后验证的 PR #2 merge tree Windows portable 包：

```bash
DOWNLOAD_DIR="$HOME/Downloads/canvas-windows-pr2-637ca6c"
mkdir -p "$DOWNLOAD_DIR"
gh run download 30873272916 \
  --repo Mostorm-Labs/canvas \
  --name canvas-windows-x64-pr-2-5e0cdc735fde \
  --dir "$DOWNLOAD_DIR"
```

最后验证的 PR #2 merge-ref 证据：

- merge commit：`5e0cdc735fded5757abe3094a2a5e94e8d3bc7ae`
- first parent：Windows `3519aca46f7167aee18ecd236c5b6e709b5fffad`
- second parent：macOS `637ca6ccff5e3f8b83de1d75c6b97078aec9fdc1`
- macOS 分支自己的 merge commit `c7553af` 已把 Windows `3519aca` 纳入；该生产代码树的 merge-base 是 `3519aca`。

Task 23C 的 `7177a07`、merge ref `c0656a...`、macOS run `30804620591`、Windows run `30804620593` 和 artifact `8852305181` 均为历史绿色证据，不再代表最后绿色生产 head。更早的 `30764881845` / `30764881840` 与 artifact `8838947539` 只保留作 Task 22 基线。核对 CI 时必须现场查询 `refs/pull/2/merge`、run `headSha` 和 checkout 日志。

### 5.3 GitHub Release

快照时唯一 Release 是 [v0.1.0-alpha.1](https://github.com/Mostorm-Labs/canvas/releases/tag/v0.1.0-alpha.1)：

- 这是较早的 Windows prerelease，不包含 `ad3a499` 的原子文档打开增量。
- ZIP：<https://github.com/Mostorm-Labs/canvas/releases/download/v0.1.0-alpha.1/canvas-windows-x64-v0.1.0-alpha.1.zip>
- ZIP SHA-256：`d0cffd8114273c86ca6c987835cdb74b067099020010d59a0af53104572aeb86`
- checksum 文件与 ZIP 一同发布。

不要把这个旧 Release 当作 `3519aca`、`7177a07` 或后继提交的构建。需要某次 PR 构建时应下载与其 run/headSha 对应的 artifact；需要新的稳定下载时，在明确版本号、更新 release notes 并完成运行时验收后创建新 `v*` tag。带连字符的 tag（例如 `v0.2.0-alpha.1`）会生成 prerelease。

## 6. 本地构建与测试

### 6.1 Windows

要求：Visual Studio 2022 Desktop development with C++、Node.js 22.12+、PowerShell、vcpkg，并设置 `VCPKG_ROOT`。

```powershell
./scripts/Restore-WebView2.ps1
cmake --preset windows-x64
cmake --build --preset windows-x64-release --parallel
ctest --preset windows-x64-release
ctest --preset windows-x64-release `
  -R "canvas_(windows_composition|webview2_surface)_test"
node --test tests/contracts/windows_release_workflow.test.mjs
./tests/contracts/Test-WindowsPortablePackage.ps1
git diff --check
```

本地可执行文件：

```text
out/build/windows-x64/app/windows/Release/canvas_windows.exe
```

文档 roundtrip：

```powershell
./out/build/windows-x64/app/windows/Release/canvas_windows.exe `
  --self-test-document --save "$env:TEMP/canvas-roundtrip.canvas"
./out/build/windows-x64/app/windows/Release/canvas_windows.exe `
  --open "$env:TEMP/canvas-roundtrip.canvas"
```

嵌入内容诊断：

```powershell
./scripts/New-TestVideo.ps1
./out/build/windows-x64/app/windows/Release/canvas_windows.exe `
  --self-test-embedded `
  --video "$PWD/tests/fixtures/test-pattern-1080p30.mp4"
```

### 6.2 macOS arm64

要求：Apple Silicon、Xcode/Command Line Tools、Ninja、Node.js 22、vcpkg。`VCPKG_ROOT` 必须指向 `vcpkg.json` 中 `builtin-baseline` 对应的干净 checkout；Hosted workflow 是该逻辑的权威参考。

```bash
set -euo pipefail
cd /Users/qing/Documents/myself/projects/canvas-macos

npm --prefix web ci
npm --prefix web test
npm --prefix web run build
node --test tests/contracts/macos_build_workflow.test.mjs

cmake --preset macos-arm64
cmake --build --preset macos-arm64-release --parallel

GUI_TEST_REGEX='^(MacosAppKitFrameScheduling|MacosCompositionLayerStack|MacosMouseInput|MacosWKWebViewSurface)\.'
GUI_TEST_SUITES=(
  MacosAppKitFrameScheduling
  MacosCompositionLayerStack
  MacosMouseInput
  MacosWKWebViewSurface
)

ctest --preset macos-arm64-release \
  --output-on-failure \
  -E "$GUI_TEST_REGEX"

ctest --preset macos-arm64-release -N -R "$GUI_TEST_REGEX"

for suite in "${GUI_TEST_SUITES[@]}"; do
  count="$(ctest --preset macos-arm64-release -N -R "^${suite}\." \
    | awk '/Total Tests:/ { print $3 }')"
  if [[ -z "$count" ]]; then
    echo "Missing CTest discovery count for ${suite}" >&2
    exit 1
  fi
  if ! [[ "$count" =~ ^[0-9]+$ ]]; then
    echo "Invalid CTest discovery count for ${suite}: ${count}" >&2
    exit 1
  fi
  if (( count == 0 )); then
    echo "CTest discovered zero tests for ${suite}" >&2
    exit 1
  fi
done

MTL_DEBUG_LAYER=1 ctest --preset macos-arm64-release \
  --output-on-failure \
  -R "$GUI_TEST_REGEX"

git diff --check
```

Bundle 通常位于：

```text
out/build/macos-arm64/app/macos/canvas_macos.app
```

Task 23C 的定向复核命令：

```bash
ctest --preset macos-arm64-release \
  -R '^((MacosPointerAdapterTest|MacosMouseSessionTest|MacosWhiteboardInputTest)\.|canvas_macos_pointer_seam_source_contract$)' \
  --output-on-failure

MTL_DEBUG_LAYER=1 ctest --preset macos-arm64-release \
  -R '^MacosMouseInput\.' --output-on-failure
```

Hosted GUI 测试绿色只说明 GitHub `macos-14` runner 能执行 synthetic AppKit/Metal 用例；它不是用户真实 event-dispatch、硬件 mouse/pen/touch、窗口体验、视频体验或硬件延迟证据。

### 6.3 Electron host

Electron 样例位于 `tools/electron-host`。Windows 构建后可设置 `CANVAS_EXE` 启动：

```powershell
Push-Location tools/electron-host
npm ci
npm run build
$env:CANVAS_EXE = (Resolve-Path `
  "..\..\out\build\windows-x64\app\windows\Release\canvas_windows.exe").Path
npm start
Pop-Location
```

现有自动化覆盖协议和部分生命周期，但没有完整 GUI E2E 证据。测试时要验证 Electron 只启动一个 native child、认证 `ready` 前按钮禁用、断线重连使用新 generation、退出优先走 graceful shutdown，以及连续画 30 秒不会产生与 pointer 数量成比例的 pipe 写入。

## 7. 任务台账

下表按能力归并，历史 TDD 细节可在 `docs/tdd/` 和 Git history 中查看。

| 阶段 | 状态 | 结果 |
|---|---|---|
| Task 1–15：Windows 垂直切片 | 已实现并自动化验证 | 核心、Skia/D3D12/DComp、输入、WebView2、存储、IPC、Electron host、打包 |
| Task 16：macOS Skia/Metal 基础 | 已实现 | AppKit/CAMetalLayer/Skia 首帧与调度 |
| Task 17：EmbeddedLoadBatch | 已合入 Windows | 不再是 local-only 待办 |
| Task 18：CompletionInbox | 已合入 Windows | bounded UI completion drain |
| Task 19：macOS 合成层 | 已实现 | 固定 AppKit/Metal/WebView 层栈 |
| Task 20：macOS WKWebView surface | 已实现 | 宿主、策略和 surface lifecycle |
| Task 21：macOS navigation/Ready | 已实现 | latest-wins、reentry、close/late callback 防护 |
| Task 22：macOS Hosted CI | 已实现并绿色 | 首个稳定基线 `e0cd6fe`；Task 23D run `30873272912` 双绿；无 macOS release artifact |
| Windows 原子文档打开增量 | 已实现并 Hosted CI 绿色 | `195ce29`–`ad3a499`，run `30745845408`；真实 GUI runtime pending |
| Task 23A：macOS pointer/session seam | 已实现、本 handoff 记录内部审查 PASS、Hosted CI 绿色 | `aeb53ef`；mouse 逻辑点归一化、session ID、Cancel；无硬件证据 |
| Task 23B：macOS whiteboard input | 已实现、本 handoff 记录内部审查 PASS、Hosted CI 绿色 | `c0ca19b` + fixes `47f752a`/`c4fcd69`；Document preview ownership fail-closed |
| Task 23C：AppKit mouse → Metal | 已实现、本 handoff 记录内部修复复审 PASS、Hosted CI 绿色 | `5228d02` + `7177a07`；synthetic MouseInput 11/11，最后验证 run `30804620591` |
| Task 23D：macOS tablet safety seam | 已实现、独立 review PASS、Hosted 双绿 | `637ca6c`；pure 10/10、tablet+contracts 12/12、non-GUI 218/218、GUI 21/21；输出仍不进 Document |
| Task 23E：可见 Pen bridge | in progress，只有未提交 WIP | bridge/CMake/tests 尚无完成 commit、复审或 Hosted run；Eraser/Interact 必须继续 fail-closed |
| macOS pen/touch/IME/Electron | 未实现或未接入 | 下一主要平台任务；mouse synthetic slice 不等于这些能力 |
| Android/iOS | 未开始 | 需在共享核心/API 稳定后规划 |
| 多人协作服务 | 未开始 | 无房间、Presence、CRDT/OT、账号权限或后端 |

## 8. 下一阶段详细实施顺序

### 8.1 Task 23E：可见 Pen 桥接

Task 23D 已完成；Task 23E 已有未提交 bridge/CMake/tests WIP，但没有完成 commit、独立复审或 Hosted run。接手后先只读核对并保留 WIP：

```bash
cd /Users/qing/Documents/myself/projects/canvas-macos
git fetch origin --prune
git status --short --branch
git log --oneline --decorate -20
git rev-parse HEAD origin/codex/macos-platform \
  origin/codex/windows-vertical-slice
gh pr checks 2 --repo Mostorm-Labs/canvas
gh run list --repo Mostorm-Labs/canvas \
  --branch codex/macos-platform --limit 10
```

Task 23E 必须无损承接 Task 23D 的 tool identity、scaled tilt、pressure、device/capabilities 与 Cancel；只有明确的 Pen/Ink + Draw 路径可进入白板 controller/Document 并触发可见分层提交。Eraser、Cursor/Unknown、Interact 与无法表达的 sample 必须 fail-closed；逐点热路径不得经过 Electron/IPC。完成门是 RED/GREEN evidence、focused/full tests、独立 review/复审和 macOS/Windows Hosted 双绿；在此之前保持 in progress。

### 8.2 macOS mouse/pen/touch/IME

建议拆成可独立审查的小任务：

1. 为现有 synthetic left-mouse slice 增加至少一个经过真实 AppKit responder/event dispatch 的 smoke test；保留当前 direct method tests 作为确定性 seam。
2. 在真实 mouse/trackpad 上验证坐标、拖出窗口、失焦、关闭、detach、read-only/editable replacement 和分层可见提交；记录硬件与 OS。
3. 增加 coalesced/predicted samples，并证明逐点路径仍只 mutate Document + invalidate，不调用 `setCanvasDocument` 或 Electron IPC。
4. Task 23E 可见 Pen 之后再实现 Eraser、touch 与 capture；Task 23D 的 constructed tablet records 不能充当 pen 硬件证据。
5. 富文本区域的 first responder、键盘和中文 IME 路由；Draw 与 Interact mode 必须 fail-closed，embedded blank-area hit routing 需要收窄到真实 child。
6. 每项继续走实现代理 → 独立 reviewer → 修复 → 原 reviewer 复审，并等待 Hosted macOS/Windows 两套 PR checks。

性能原则：事件采样、stroke building 和 invalidate 都在 native 进程；Electron 只接收模式/对象/文档等低频命令。

### 8.3 统一异步嵌入内容事务

将 Windows IPC `open-document` 已验证的事务规则抽成跨平台可复用 contract，并补：

- `--open` 与 `create-embedded` 进入相同 Ready/Failed/timeout/supersede 模型。
- `embedded-state` 事件包含 node ID、generation、state、稳定错误码和原始 request correlation。
- Windows WebView2 与 macOS WKWebView 使用同一 admission/terminal 语义。
- 失败回滚不修改旧 Document；关闭和 supersede 不能产生 late commit。
- Electron UI 明确区分“命令已接纳”和“嵌入内容已 Ready”。

### 8.4 Windows 真实设备验收

在 i5-1235U 触控大屏记录：

- Windows build、GPU driver、显示刷新率、触控采样率、WebView2 Runtime、Node/Electron 版本和测试 commit。
- mouse、pen、touch、capture、取消、窗口失焦和多指边界。
- Rich text 中文 IME；HTTPS 页面；1080p30 视频 play/pause/seek。
- WebView 始终位于墨迹下，移动/缩放只影响目标 surface，批注跟随正确坐标。
- 原子打开 Ready、单个 surface 失败、30 秒超时、257 节点拒绝、latest-wins、关闭时 late callback。
- Electron 断线重连和 graceful shutdown。
- 240 fps+ 摄像机的触控到可见墨迹 p50/p95/p99；验收门为 p95 `<50 ms`。

未保存原始视频、逐次测量表和环境信息，不得把“肉眼看起来流畅”写成性能验收通过。

### 8.5 后续跨端与协作

在 Windows/macOS 平台 API 稳定后再做：

- Android：NDK C++ core + Skia/Vulkan 或平台受支持 GPU backend + 原生 MotionEvent/InputMethod + Android WebView。
- iOS：C++ core + Skia/Metal + UIKit/Pencil input + WKWebView。
- 协作数据模型：稳定 object/stroke ID、操作序列、幂等、撤销语义、快照和迁移。
- 同步算法：根据对象/笔画冲突模型选择 CRDT/OT；不要在没有一致性规范时直接同步整个 Document blob。
- Presence、房间、账号、权限、离线队列、重连、服务端持久化和可观测性。
- 视频只同步 URL/资源 ID、时间点和控制状态；富文本同步结构化编辑操作；网页通常只同步 URL、transform 和交互状态。

## 9. 明确未完成、不得误报

以下项目截至快照仍为 pending：

- Windows i5-1235U 真实触控大屏 p95 `<50 ms`。
- Windows Electron/native GUI E2E。
- Windows 原子文档打开的真实多 WebView2 runtime 故障注入和体验验收。
- Windows 中文 IME、1080p30 视频、层级、移动/缩放的完整实机证据。
- macOS Task 23C 只有 synthetic left-mouse 自动化；Task 23D 只有 constructed tablet records 与 source-level AppKit contract。真实 event dispatch/hardware mouse/tablet、可见 Pen/Eraser、Interact routing、touch、coalesced/predicted、完整 capture 和 IME 仍 pending。
- Task 23D 只完成安全 tablet seam；tablet output 不进 Document，不得误报为可见 Pen/Eraser 或 Interact routing。
- Task 23E 只有未提交 bridge/CMake/tests WIP；没有完成 commit、独立复审或 Hosted CI 证据。
- macOS Electron/native IPC、发布包、签名、公证和 Release artifact。
- macOS 真实设备 GUI/输入/延迟证据。
- Android/iOS 应用和平台层。
- 多人协作服务端、房间、Presence、CRDT/OT、账号和权限。
- 视频/富文本的跨设备实时同步。
- 正式代码签名、安装器、自动更新、崩溃上报和生产发布运维。

## 10. 子代理实施与审查规则

项目后续继续采用已经确认的流程：

1. 主代理把一个有明确边界的任务交给实现子代理。
2. 实现代理先写失败测试/contract，再实现最小功能，提交 scoped commit。
3. 不同的独立 reviewer 检查正确性、生命周期、并发/重入、平台 guard、测试覆盖和未授权范围。
4. 有 P0/P1 时返回原实现代理修复，再由原 reviewer 复审。
5. 本地可运行测试全绿后推送；Windows/macOS 平台代码必须等待对应 Hosted CI。
6. CI 失败先区分代码、workflow、runner/额度外部 blocker；只针对真实原因修改。
7. 每项功能、修复和 docs evidence 分开 commit，stage 使用明确文件白名单。

审查时尤其关注：

- 同步 COM/Objective-C callback 重入和对象 lifetime。
- stale generation/timer/callback 是否能作用于新事务。
- bounds、UTF-8 byte budget、队列容量和资源上限是否 fail-closed。
- pending/failed 路径是否修改旧 Document 或泄漏 surface。
- Windows-only、Apple-only 代码是否被 CMake guard 隔离。
- 测试是否真正发现了目标用例；禁止零测试或 skipped 伪装绿色。

## 11. CI 监控与故障处理

```bash
gh run list --repo Mostorm-Labs/canvas \
  --branch codex/windows-vertical-slice --limit 10
gh run list --repo Mostorm-Labs/canvas \
  --branch codex/macos-platform --limit 10

RUN_ID="$(gh run list --repo Mostorm-Labs/canvas \
  --branch codex/macos-platform --limit 1 \
  --json databaseId --jq '.[0].databaseId')"
test -n "$RUN_ID"

gh run view "$RUN_ID" --repo Mostorm-Labs/canvas \
  --json status,conclusion,headSha,url,jobs
gh run watch "$RUN_ID" --repo Mostorm-Labs/canvas --exit-status
gh api "repos/Mostorm-Labs/canvas/actions/runs/${RUN_ID}/artifacts"
```

若 GitHub 拒绝分配 runner 并明确显示 Actions billing/spending limit：

- 不改代码来“修复”外部额度问题。
- 最新一次 billing-blocked attempt 不足两小时时不要重试。
- 超过两小时最多重试一次并记录 run URL；仍被拒绝就明确标记 external blocker。
- runner 已启动后才根据 Build、CTest、Composition、GUI/Metal、packaging 或 whitespace 的实际失败进行修复。

## 12. 交接验收清单

新账号接手时逐项执行并更新结果：

- [ ] `git fetch --all --prune` 后动态核对三个远端引用；记录实测 SHA，不假设仍等于第 2.1 节快照值。
- [ ] Windows worktree clean，HEAD 与远端相符。
- [ ] 用 `git rev-parse HEAD` 区分 `637ca6c` 生产基线与后继 docs commit；保留并逐文件归属 Task 23E 未提交 bridge/CMake/tests WIP。
- [ ] 用 `gh pr view` 确认 PR #1/#2 的实时 Open/base/head 状态；快照时 PR #2 head 为 `637ca6c`，docs-only 或 Task 23E push 后不得沿用该假设。
- [ ] Windows 已保存的 docs run `30793067680` 和生产代码 run `30745845408` 均可查询；artifact 过期后针对希望验证的实测 head 重新运行。
- [ ] 将 `637ca6c` / `30873272912`、`30873272916` / artifact `8878654542` 作为快照时最后 Hosted 双绿生产树证据；`7177a07` / `308046...` 只作 Task 23C 历史证据。
- [ ] 现场读取 `refs/pull/2/merge` 和 parents；只有 SHA 仍为 `5e0cdc73...` 时才复用第 5.2 节 parents，否则记录新的 merge ref/head 并检查对应 runs。
- [ ] 确认 Task 23D commit `637ca6c`、green evidence、独立 review PASS 和双绿 runs 可追溯；同时确认其 tablet output 仍不进入 Document。
- [ ] 从 Git history 和测试复核 Task 23A/B/C commit 链及 Task 23C 四个 P1 修复；内部 reviewer PASS 目前只由本 handoff 持久化，不虚构 GitHub PR Review。
- [ ] 新增代码继续执行实现子代理 → 独立 reviewer → 修复 → 复审。
- [ ] 不把 Hosted CI/synthetic MouseInput 当作 Windows 触控硬件、Electron GUI、真实 macOS input 或 `<50 ms` 证据。
- [ ] 不把旧 Release `v0.1.0-alpha.1` 当作 `3519aca`、`7177a07`、`637ca6c` 或任何后继提交的构建。
- [ ] 所有“完成”声明都附 commit、测试命令、run/job URL 或真实硬件记录。

完成以上核对后，优先按第 8.1 节继续 Task 23E；没有 commit、复审与 Hosted 双绿前不得写成完成。
