# ADR-0009: 使用不可变的预编译 Skia SDK

- Status: Accepted
- Date: 2026-08-17
- Related stages: POC-01, R1, R3

## Context

POC-01 的六端 CI 即使命中编译缓存，仍会重复同步 Skia 源码并执行 GN/Ninja。
这既消耗 runner 时间，也让 Canvas 的普通构建直接依赖 Skia 的内部输出目录和传递库名称。
Actions artifact 适合作为同一 workflow 内的短期传递介质，但其保留期不能承担长期、可回滚的依赖锁定。

## Decision

建立 `poc01-minimal-v1` 生产 profile，为 Windows、Web、macOS、iOS、iOS
Simulator 和两个 Android ABI 生成七个自包含、确定性的 Release ZIP。每个 ZIP
包含 headers、实际静态库、固定字体、许可证、规范化 GN 参数、manifest 和只公开
`CanvasSkia::Skia` 的 CMake package。

SDK 集合由 `sdk_id`/`set_id` 和 SHA-256 标识，发布到本仓库的不可变 GitHub
prerelease。PR 只读构建与验证全部 target；只有从 `main` 人工触发的独立 publish
job 拥有写权限和 provenance attestation 权限。已存在 tag 只能逐字节验证通过，不能覆盖。

Canvas consumer 只接受提交到仓库的 SDK lock。普通构建下载并严格验证 Release 或
同布局镜像，不允许隐式回退到源码构建，也不感知 Skia source tree、GN、Ninja 或
传递 archive 路径。

## Consequences

- Skia 升级、profile 或 toolchain 变化先产生新 SDK ID，再显式更新 consumer lock。
- 普通 Canvas CI 不再承担 Skia source checkout、sync、GN 和 Ninja 成本。
- Release 资产成为供应链的一部分；被任何 lock 引用的 prerelease 必须永久保留。
- 第一版仅发布 Apple arm64、官方 Release 静态库，不包含 Debug、符号包或新增 Skia 功能。
- POC-01 合并和 SDK 预编译都不替代物理 Windows GPU 与移动真机报告；本 ADR
  采纳时状态继续为 `Validating`。后续只有聚合审计可在独立证据齐全后改为
  `Accepted`。

## R1 Full 扩展

R1 产品化新增 `canvas-skia-sdk-profile-v2` / `r1-full-v1`，不修改或取代历史 POC
profile/lock。它用同一个锁定 Skia commit 为 8 个 target 生成 Release、Debug、ASan
三种 variant：24 个 SDK ZIP，加 16 个 Debug/ASan symbols ZIP。macOS 同时覆盖 arm64
与 x64；iOS device arm64 同时服务 iPhone/iPad；Android arm64 是产品 ABI，x86_64 是
emulator/CI ABI。

24 个 `target × variant` 组合是独立 Producer job 和独立 artifact。单个 ASan 失败不会
阻止同 target 的 Release/Debug 上传，同一次 run 可只重跑失败 job。跨 run 只复用经过
当前 profile、recipe、toolchain identity、SDK ID、GitHub digest 和文件级 hash 全量验证的
历史 artifact，并仍重新执行 source-free consumer smoke；验证失败或身份变化时必须重建。

Full SDK 固定 Ganesh、PDF、SVG、Skottie、PathOps、RichText、PNG/JPEG/WebP 及其
FreeType/HarfBuzz/ICU/Expat/zlib/Wuffs closure。DNG/PIEX、Graphite、Dawn、Vulkan 和
非 Runtime 工具全部关闭。PathOps 由 API probe 证明而不是发明 GN 参数。包内
`archive_closure` 必须从 GN 实际 dependency graph/output 导出，消费者只能链接
`CanvasSkia::Skia` 或受支持的 Paragraph/Skottie/Svg/PathOps/Media imported target，
不得手工组合 archive。

Release 是唯一默认 variant。Debug 和 ASan 必须显式选择；ASan consumer 同时插桩并
遵循 manifest 的平台验证等级，不能把 link-only/instrumented-link 表述为 runtime
smoke。R1 Full matrix lock 只在不可变 Release 发布后生成，不预造哈希。普通 Canvas
CI 继续只下载 SDK，不允许源码回退、GN 或 Ninja。

R1 Full 的 PR 入口按变更影响范围分类：profile、Skia lock、构建/打包 recipe 和
Producer workflow 运行完整 24-job 矩阵；平台专属 producer 变更只运行对应平台的
三个 variant；fetch、consumer、lock 和验证工具只运行 source-free consumer validation；
文档与无关变更不启动 Producer。正式发布由独立的 `workflow_dispatch` workflow 从
`main` 触发，始终要求完整矩阵。跨 run 复用仍必须逐项校验身份、manifest、digest 和
文件 hash，复用失败才回到 Skia source build。

## Validation

Producer 必须验证 schema、canonical hash、target/toolchain identity、包内容、许可证、
路径安全、损坏输入和连续两次打包的字节一致性。打包后隐藏 Skia source checkout，
仅从解压 SDK clean-build 当前 Canvas target。

Consumer 切换后，六端原有 digest、黄金图、100 次生命周期、60 秒 smoke、sanitizer、
模拟器和跨平台接受门禁全部保留；普通 POC workflow 还必须通过静态检查证明没有源码
bootstrap、`skia/out` cache 或 producer builder 引用。

## 已验收 POC workflow 退役

POC-01 与 POC-04 已完成统一验收，POC-05 已完成非 V1 风险验证。对应自动 workflow、
POC-01 minimal Producer 与 POC-04 RichText Producer 从日常 CI 中删除；这不删除或重写
历史 profile、lock、Release、复现工具和证据。POC-02/03/06 在剩余门禁关闭前继续拥有
按路径触发的实验验证，但其 Skia Consumer 统一下载锁定的 R1 Full `release` variant。

R1 以后不再为单个 POC 创建新的 Skia profile 或 Producer。历史 POC 复现必须显式使用
对应旧 lock；普通 PR/push 只运行产品/RF 门禁、尚未完成的实验门禁，以及按变更影响范围
选择的 R1 Full Producer 或 source-free Consumer validation。
