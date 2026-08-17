# ADR-0015: 平台支持分级与 Shell 选择策略

- Status: Accepted
- Date: 2026-08-17
- Related stages: POC-01, R1, R3, R5
- Clarifies: ADR-0001, ADR-0002

## Context

POC-01 在六个平台验证共享 Runtime，但首批产品 Shell 只有 Web、Windows 和 Android。
如果 portability harness、Web reuse target、Headless utility 与正式产品 release 使用同一
门禁，会让“能运行共享 Runtime”被误解为“承诺完整产品支持”。同时，Shell 可替换不应
让当前 React/Tauri 和 React Native 选择变成 Runtime 的不可替换依赖。

## Decision

V1 支持分级固定为：

| Tier | Targets | V1 责任 |
| --- | --- | --- |
| Product Tier A | Web、Windows、Android | 正式产品 Shell、完整用户流、性能/IME/Input/Surface/发布与支持门禁 |
| Portability Tier B | macOS、iOS、iPadOS | 共享 C++ Runtime、C ABI/ObjC++ harness、Ganesh/Metal bring-up 和核心 conformance；不承诺 V1 产品 Shell |
| Reuse Target | ChromiumOS | 复用 Web 产品 target；平台 FastInk 是可选 capability |
| Utility Target | Headless | test/reference/golden 和内部受控 export；V1 不承诺公共 server/batch rendering API |

- Tier A 的当前产品选择保持：Web React/TypeScript、Windows React/Tauri、Android React
  Native。长期架构不变量是窄 Bridge、Windows native canvas region、Android Native
  CanvasView/JNI 以及高频 Pointer/IME/Render 数据面不经不必要的 JS 往返。
- 更换 Tauri、React Native 或其他 Shell framework 需要产品/平台决策和对应 contract/
  regression evidence；若 Bridge、native data path、surface ownership 或 Runtime 边界不变，
  不视为推翻 Visual Document Runtime 架构。若改变这些不变量，则必须新增 Architecture ADR。
- POC-01 继续对六平台执行完整共享引擎 acceptance。产品阶段的发布阻断以 Tier A 为主；
  Tier B 维持 core compile/conformance 和定时完整验证，不能被 Tier A 特例分叉 Runtime。
- Headless 的 server-side export、thumbnail service、PDF/image batch conversion 和公共稳定
  API 属于未来产品能力，进入前另建 ADR 与安全/资源预算门禁。

## Consequences

- R5 “Release”明确指 Tier A 产品发布；Tier B 失败仍是 portability regression，但不会
  被误报为已有 Apple 产品功能故障。
- CI 可以按 stage、tier 和 changed paths 分层，同时 core/public ABI 变更仍必须编译全部
  portability targets。
- Apple 产品化不需要重写 Runtime，但需要独立 Shell/发布范围 ADR。
- 当前 Shell 技术有明确负责人和验证面，又不会泄漏为 Document/Renderer 依赖。

## Validation

POC-01 对六平台保持 digest/golden/lifecycle/smoke；R1/R3 的每次提交至少验证受影响 Tier A，
core/public ABI 变更同时编译 Tier B；定时任务运行 Tier A+Tier B full conformance；R5 只在
Tier A 完成安装、升级、真实设备、性能、稳定性和支持演练后发布。任何平台特例导致共享
Document/Operation/Scene/Stroke/Text 语义分叉均阻断所有 tier。
