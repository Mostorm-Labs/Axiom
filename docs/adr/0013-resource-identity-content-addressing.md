# ADR-0013: 资源身份、内容版本与 Manifest 分离

- Status: Accepted
- Date: 2026-08-17
- Related stages: POC-04, R1～R4

## Context

节点需要稳定引用图片、字体和其他 blobs，但路径、URL、平台 asset handle 和内容 hash
承担不同语义。若一个 ResourceId 背后的内容可以静默变化，Document digest、协作、
离线恢复、cache invalidation 和跨平台文本布局都无法确定；若直接用内容 hash 作为节点
身份，替换资源又会迫使所有引用节点改变。

## Decision

- `ResourceId` 是 Document 内稳定、不可复用的语义身份，不是文件路径、URL、平台
  handle 或 content hash。节点只保存 ResourceId。
- `ResourceManifest` 是 Document semantic state 的版本化组成部分。每个 entry 至少包含
  ResourceId、kind、`ResourceRevision`、versioned `ContentHash`、内容长度和布局/解码
  所需的规范化元数据。
- V1 ContentHash 使用带算法前缀的 SHA-256（`sha256:<lowercase hex>`）。blob 是不可变
  内容寻址对象；替换图片/字体通过 Operation 更新同一 ResourceId 的 revision/hash，
  不能在原 hash 下覆写字节。
- Document digest 覆盖规范化 Document graph 和 ResourceManifest entries（包括 revision、
  content hash 与语义元数据），但不重复 hash blob bytes，不包含下载 URL、本地路径、
  decode/GPU/cache 状态。资源暂时 missing 不改变 digest；manifest binding 改变必须改变
  digest。
- Persistence 原子保存 Document snapshot、ResourceManifest 和所需 blobs/locator metadata。
  Collaboration 同步 manifest-changing Operations 和可验证的 blob availability，不把本地
  文件路径当协作事实。
- ResourceResolver 只读取 manifest snapshot，通过 ContentHash 获取并校验 blob。解析、
  下载、decode 或 upload 失败产生 placeholder/diagnostic，不反向修改 Document。
- 可变外部 URL 若没有固定 expected ContentHash，不具备 V1 可确定资源语义；未来 linked
  resource capability 必须另建 ADR，并明确 digest、刷新和安全策略。
- 字体使用 `FontResourceId` 与 ContentHash；规范化 fallback chain 由 RichText schema
  保存/引用，系统字体的偶然可用性不能改变 canonical layout。

## Consequences

- 同一资源可被多个节点稳定引用，内容替换、去重、离线和 cache invalidation 各自有
  明确身份。
- ResourceManifest 虽由独立模块管理和持久化，但属于可保存/协作的语义状态；这不让
  Document node 依赖 ResourceManager。
- 需要 blob GC、manifest transaction、missing/corrupt resource、hash mismatch 和资源
  替换语料。
- Package/blob availability 可以与 Document operations 分阶段传输，但打开文档必须能
  区分“语义引用存在”和“内容当前不可用”。

## Validation

R2 前验证 resource replace、dedup、missing、corrupt、offline、save/reopen、migration 和
collaboration replay。相同 graph+manifest 必须产生相同 Document digest；相同 ResourceId
绑定不同 ContentHash 必须产生不同 digest；不同路径/下载来源取得相同 hash 必须得到相同
解析结果。POC-04 额外验证 FontResourceId/fallback 在三平台产生相同 canonical layout。
