# ADR-0006: RichText 与 IME 是一级子系统

- Status: Accepted
- Date: 2026-08-16
- Related stages: POC-04, R2, R3, R4

## Context

未来能力包含 RichText、Sticky、Table、搜索、拼写检查和协作。把 Text 建模为 `string + fontSize` 的 Shape 会在 selection、composition、runs、layout 和 undo 出现后被整体推翻。

## Decision

V1 从一开始建立：

- `TextDocument`：paragraphs、runs、styles、attributes。
- `TextEditSession`：selection、caret、composition、undo grouping。
- `TextInputAdapter`：Web/Windows/Android 平台 IME 边界。
- `TextLayout`：共享语义到 SkParagraph 的布局层。

Platform Shell 不复制 TextDocument。Android IME 通过 Native CanvasView/JNI 进入 Runtime，不经 RN JS 数据面。

## Consequences

- V1 文本工作量增加，但避免未来迁移简单 Text node。
- 必须处理 logical positions、composition cancel/commit 和固定字体测试。
- Collaboration MVP 只同步已提交文本原子操作；复杂字符级并发另建 ADR。
- TextLayout 属于派生状态，可重建，不进入 Document 快照。

## Validation

POC-04 用三平台中英文/中文拼音、selection、caret、clipboard、undo 和 10K 字符性能语料验证。任何平台 widget 成为权威文本模型都违反本 ADR。
