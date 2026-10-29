# docs 索引 —— 先读哪一份，以及"哪个事实归谁管"

本目录的文档分成三类。**外部使用者只需前两类**；第三类是维护者改动本仓时的台账，不是手册。

## 1. 上手（给第一次用本库的人）

| 文档 | 什么时候读 |
|---|---|
| [`QUICKSTART.md`](./QUICKSTART.md) | **第一条**：三种壳（C++ / Python / C#）各自最短的可用路径、预期输出、常见坑 |
| [`API_en.md`](./API_en.md) / [`API_zh.md`](./API_zh.md) | 查某个函数/字段的签名、默认值、失败语义 |
| [`Custom Chinese Dialect.md`](./Custom%20Chinese%20Dialect.md) | 要加一个本仓没有的方言 |
| [`PERFORMANCE.md`](./PERFORMANCE.md) | 性能数字（**本文件是性能数字的唯一真相源**）、测量协议、准入判据 |

## 2. 参考（用起来之后会回来查的承诺）

| 文档 | 它承诺了什么 |
|---|---|
| [`CONTRACT.md`](./CONTRACT.md) | 契约与不变量：对齐、失败码、线程安全边界、**C ABI 的增长规则**（哪些结构体可以长大、怎么长大） |
| [`STABILITY.md`](./STABILITY.md) | **跨代兼容政策 §4.5（R1–R16 的唯一权威）**；§0 是**注册用例数的单一权威**；另有稳定性评估（sanitizer / 线程 / 多实例 / ABI） |
| [`UPGRADING_A_GENERATION.md`](./UPGRADING_A_GENERATION.md) | 换代/迁移清单：发布前与落地时两张表；末节记 2026-10-18 的默认风格迁移 |

## 3. 内部台账（维护者用；读者不必从这里开始）

读者面向的文档只在 §1/§2 各列一次——**一份文档在索引里出现两次，就会有两种描述**，这一节因此只收维护者改本仓时才翻的东西。

| 文档 | 记什么 |
|---|---|
| [`DECISIONS.md`](./DECISIONS.md) | **决策号的唯一登记处 + 现行裁定**：S/D/R/N/C/T/G/K/A/P 各系列的含义、D 号分段说明、核心算法冻结区 K1–K8、验收线 AC、性能与测量纪律、明确不做清单；打包矩阵 / 版本链 / 插件与打包的决策台账在 §2.1（D1–D12、D35–D40）。2026-10-22 起取代 `REFERENCE_IDS.md`、`REFACTORING.md` §2、`DEBT_REDUCTION_PLAN.md` §1。发布**动作**清单不在本文件，见 [`RELEASE_CHECKLIST.md`](./RELEASE_CHECKLIST.md) |
| [`RELEASE_RUNBOOK.md`](./RELEASE_RUNBOOK.md) | **发版当天照着敲的三渠道命令**（PyPI / NuGet / pkg-config），每条写判据与最后一次实测的读数（含 `twine check`，2026-10-27 首次 PASSED） |
| [`RELEASE_CHECKLIST.md`](./RELEASE_CHECKLIST.md) | **发布一版的唯一落点**：版本投影逐处核对、矩阵盖章、发布当天只做的一件事 |
| [`REQUIREMENTS.md`](./REQUIREMENTS.md) | 需求契约索引（算法冻结区、准确率门禁、跨平台），含三个冻结面的单一登记处 |
| [`NEW_DIALECT_PLAYBOOK.md`](./NEW_DIALECT_PLAYBOOK.md) | 新语种落地的可执行流程（本仓只做中粤） |

## 4. 单一真相源地图（同一个事实只允许一处权威）

| 事实 | 权威 | 其它地方只能写指针 |
|---|---|---|
| 版本号 | `CMakeLists.txt` 的 `project(... VERSION ...)` | **投影共七处、逐处由谁守 —— 只有 [`RELEASE_CHECKLIST.md`](./RELEASE_CHECKLIST.md) §1 那张表**（①`cppp2_version()` ②`_version.py` ③`version.props` ④wheel METADATA ⑤`nuspec` ⑥git tag ⑦CHANGELOG 标题）。本行**不再自己列一份更短的清单**：2026-10-20 实测这里列的 5 类与那张表的 7 处对不上，而"哪份清单算数"必须只有一个答案（2026-10-27：本行原写"投影共六处"，与紧接着列出的 ①–⑦ 自相矛盾，改为七处） |
| 注册用例数 / `slow` 标签数 | `STABILITY.md` §0（四套配置的指纹见 `NEW_DIALECT_PLAYBOOK.md`） | README、板上记录（都写"见 STABILITY §0"） |
| 性能数字 | `PERFORMANCE.md` | README 与 CHANGELOG 只写指针 |
| 发布动作清单 | `RELEASE_CHECKLIST.md`（发布一版的唯一落点） | 本机执行入口只在清单里 |
| 兼容承诺（跨代） | `STABILITY.md` §4.5 R1–R16 | `CONTRACT.md` §1 是同一政策的用户向摘要 |
| C ABI 的形状/增长规则 | 头文件 `include/cpp-pinyin2/ext/c_api.h` + 六份冻结清单（`tests/abi/`：C 符号、C ABI 签名基线、插件面、公共头、公共名字、公共成员签名） | `CONTRACT.md`、`API_*.md` 描述它们 |
| 冻结面的计数 | `REQUIREMENTS.md` §5.3 ① 行（机器真值源是 `tests/abi/` 的**六份**清单文件；其中被门禁逐字校验的是**三个计数**：公开头 / C ABI 符号 / 插件面声明） | **登记处只此一处；复述份数不设限，但每一份都必须被门禁校验** —— 2026-10-20 更正：本行原写「别处只写指针」，与实测的 14 处复述相矛盾（那些复述由 `check_doc_claims.py` 逐轮比对，写死指针反而会丢掉这层保护）。**2026-10-27 再更正**：份数原写"五份"，漏了 2026-10-20 之后加入的 `expected_c_api_signatures.v2.txt`（C ABI 签名基线），实为六份 |
| 词典数据与许可 | 根 `THIRD_PARTY_NOTICES.md`（许可矩阵；产地是词典仓 `pinyin-makedict`） | `data/dialects/*/License.txt`（随包分发的那一份）。**根 `README.md` 有一句摘要 + 指针**——2026-10-20 更正：本行原写「本仓不复述许可结论」，与根 README 的第 614-617 行相矛盾；正确口径是「逐项结论只在 `THIRD_PARTY_NOTICES.md`，README 只允许留一句摘要并指过去」 |
| 决策号的含义（`(S7)`、`(D4)`、`(R14)`…） | [`DECISIONS.md`](./DECISIONS.md) §1（唯一登记处；`tools/check_doc_refs.py` 按它判定，公共头只许引用 `live` 的） | 代码与文档里引用决策号时不再各写一份解释 |

## 5. 不在本目录里的东西

- 方案草稿与演练脚本曾放在 `.tmp/`（已 gitignore，属 scratch，随时可删）——文档里引用它时只作**历史指针**。
- 退役的台账（`l1-interface-stability.md` 等）及其去向记录已删除，需要的结论已并入本目录的活文档；
  历史文档里指向它们的 `path:NN` 属"退役路径"，不是断链。
- **本仓历史里查不到的提交号不是笔误。** `v2` 树于 **2026-10-22 以单个导入提交 `8e9cd00`** 落到本仓的 2024 年历史之上
  （该提交之前 80 个提交全是 2024 年的；`git log --since=2026-09-01 --until=2026-10-21` 为空，中间那段开发历史不在本仓）。
  因此文档里所有**2026-09 至 2026-10 中旬**的提交号——`d36f73a`、`1b76be2`、`8995a04`、`0d883e7`、`de7d3d4`、`8e992bc`、`e03a772` 等——
  命名的是**原开发历史**，在本 clone 里 `git cat-file -e` 一律失败。它们是"当时那棵树"的标识，不是可点击的链接；
  引用它们的句子保留，是因为它们记录的实测结论仍然有效。
