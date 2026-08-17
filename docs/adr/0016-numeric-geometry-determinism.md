# ADR-0016: 数值与几何确定性契约

- Status: Accepted
- Date: 2026-08-17
- Related stages: POC-01～04, R1, R2, R4
- Clarifies: ADR-0008, ADR-0012

## Context

Canvas v2 要求相同 Operations、Pointer replay 和 Document revision 在不同 CPU、编译器与
WASM/native target 上产生相同 Document、Stroke 和 Scene digest。仅声明使用
`float` 或容忍视觉像素差不能保证语义确定性：负零、非有限值、FMA、平台 `libm`、中间
精度、溢出和序列化舍入都可能产生不同 bit pattern。

## Decision

- V1 Document geometry、Canonical Stroke geometry 和进入语义 digest 的几何标量使用
  IEEE-754 binary32 作为 canonical storage type。它是存储/交换契约，不要求所有中间
  计算只能使用 binary32。
- 进入 Document transaction、Canonical Stroke commit 或 canonical Scene record 前，数值
  必须是 finite；NaN、Infinity、不可逆矩阵和超出声明坐标范围的结果整笔/整事务拒绝，
  不产生部分修改。`-0` 在这些边界规范化为 `+0`。
- 输入适配器或算法可以使用 binary64 中间值，但每个持久或可回放算法版本必须固定操作
  次序、中间精度、舍入边界和输出 canonicalization。不能依赖不同平台可能给出不同末位
  的未约束 `libm` 行为；需要三角、幂或随机数学时使用版本化确定实现或保存足以重建
  canonical 输出的数据。
- 默认采用 IEEE round-to-nearest, ties-to-even。编译器 contraction/FMA、fast-math、
  flush-to-zero 等选项不得静默改变 canonical 结果；允许的构建配置进入 conformance
  matrix，任何差异必须由新算法版本或 ADR 处理。
- Canonical serialization 与 digest 使用版本化字段顺序和 little-endian IEEE bit pattern；
  不经 locale、十进制格式化或平台对象布局。比较/摘要采用 canonical 值，视觉 golden
  容差只服务 raster 差异，不能掩盖语义 digest 差异。
- 不对所有几何步骤做统一十进制定点量化。若某个算法需要网格、epsilon、fixed-point 或
  quantization，规则属于该算法版本，并必须验证精度、退化输入和跨平台 replay。
- 最大坐标、矩阵系数、路径复杂度和安全运算界限由 schema/algorithm version 明确声明；
  bounds、矩阵组合和空间索引使用 checked operations，溢出返回结构化错误。

## Consequences

- Document/Stroke/Scene digest 有明确的数值编码，不能直接 hash C++ struct memory。
- Ink、Layout、Geometry 和 SceneCompiler 的算法版本必须能说明精度、舍入和确定性数学
  依赖；性能优化不能擅自改变操作次序。
- RuntimeScene 中仅用于显示且不进入 canonical oracle 的 backend 临时值可以使用后端
  精度，但必须与 canonical semantic/scene records 分离。
- 未来若 binary64、fixed-point 或不同 canonical representation 更合适，需要带迁移、
  digest version 和跨平台 corpus 的新 ADR。

## Validation

POC common corpus 覆盖 `-0/+0`、subnormal、舍入中点、极大/极小 finite 值、退化矩阵、
溢出、NaN/Infinity 和不同表达但等价的 Operations。POC-02 逐字节比较 Vector/Dab 的
Canonical samples/geometry，POC-03 比较 full/incremental Scene digest，R2 验证
serialization/migration。必须至少在 native x64、native arm64 和 WASM 上运行；任何语义
差异都阻断对应阶段，不能改用视觉容差放行。
