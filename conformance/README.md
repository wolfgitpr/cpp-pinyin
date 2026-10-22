# The conformance fixture

`cases.json` is emitted from a dictionary tree by `CApiSmoke --emit-conformance` and read verbatim by the C++,
Python and C# suites, so the three shells are checked against one set of inputs rather than three. Two properties
are gated: `generated_by_commit` must name a commit this repository contains, and the file's sha256 must match
`cases.sha256`.

| Item | Value | Detail |
|---|---|---|
| **P3 夹具条数**（防悄悄缩小） | **49（按 op 分布见右列）** | `cases.json` 的 `cases_count` = **49**（query 28 / readings 6 / to_simplified 5 / is_polyphonic 4 / style_list 2 / render_syllable 2 / apply_tone_sandhi 2）。**本行是条数的唯一登记处**：Python 与 C# 的套件都读这一行的状态列，与夹具自报的条数比对，所以夹具不能被悄悄缩小。生成器 `CApiSmoke --emit-conformance`；校验 `conformance/cases.sha256` |
