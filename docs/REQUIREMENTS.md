# cpp-pinyin2 需求契约（跨轮硬约束 · 单一真相源）

> **本文件的定位**：收敛**历次会话反复确认、至今仍然生效**的硬性约束，供新会话直接引用。
> 凡"某个数字/阈值/协议只允许一处权威"者，**权威不在本文件**——本文件只写指针。
>
> | 主题 | 权威文档 |
> |---|---|
> | 性能数字、测量协议、否决记录 | **`docs/PERFORMANCE.md`** |
> | 目标架构、决策台账 D1–D23、验收线 AC-1..5 / AC-9（**编号 AC-6..8 在本仓没有任何记录**；AC-10 见 `docs/NEW_DIALECT_PLAYBOOK.md` §5） | **`docs/DECISIONS.md`** §6 |
> | 测试集清单、判据缺陷、缺陷编号 | `docs/STABILITY.md` §0（门禁与用例清单） |
> | 未实现的功能需求（待排期） | 本文件 §5 与 `docs/DECISIONS.md` §8（"明确不做"） |
>
> **留档口径（v2 起）**：文中凡出现 `<scratch>/...`、带 `.tmp` 前缀的路径，或「本地留档（未入库）：...」，均指**开发期本机临时目录/文件**（板上脚本、探针、日志、dump、临时构建目录）——**不随库分发、也不在仓库里**，复现时用任意本地目录替代；随库分发的内容以 `docs/` 与 `conformance/` 为准。v2 分支把 2026-09-21/22 的 33 个分步提交压成 4 个提交，**原提交号对照表附在这 4 个提交的信息正文里**；文中标注「原 `<sha>`」的提交号指向压缩前的历史，在 v2 分支上不可解析（仅作者本地与该对照表可查）。
>
> **"过程记录"指什么**：本文件提到的**过程记录**是 2026-10-22 删掉的那批本机台账（31 个文件，原在 `tests/data/testdata/`：
> 逐轮审计、覆盖率、缺陷清单、状态页等）。`git log --diff-filter=D -- tests/data/testdata/` 仍能从删除它们的提交里取回；
> 本文件的结论、数字与判据都不依赖它们成立——需要出处时，各节自己写明了出处。
>
> 快照：压缩前提交 `d36f73a`（2026-09-22；**该提交不在本仓历史里**，见 `docs/README.md` §5）→ **第 3 期 `8995a04`（v2.3）→ 第 4 期：本文件随 v2.4 提交更新（接口扩展，见 §4.2；等价性门禁与准确率逐字未变）**。**本文件所有 `文件:行号` 均在快照下实测；行号会漂移，引用前必须自行复核。**

---

## 0. 一句话

这是一个**零外部依赖的 C++17 中文/粤语 G2P 库**。多音字消歧靠**冻结的四方向滑动窗口算法**；
所有重构必须过**等价性门禁**；所有优化必须过**P1–P6 测量协议**；**准确率不得退步**。

---

## 1. 算法不可变契约（FR-1，最高优先级）

多音字消歧的四方向滑动窗口**冻结**：任何改动必须先当作 bug 怀疑，而不是"新设计"。

| 契约 | 内容 | 权威位置（**以符号名为准**，行号只是提示：2026-09-23 逐条复核过一次） |
|---|---|---|
| **K1** 分词 | ASCII 字母连续串合并为一 token；假名 1-2 字符（后接"小假名"取 2）；其余非空白字符各成一 token；**空白字符被跳过**（实现：ASCII 空格 + C0 控制字符 + Unicode Zs，2026-09-23 与契约对齐；公开谓词 `isSpace` 仍只认 U+0020） | `docs/DECISIONS.md`；实现 `EngineImpl::splitInput()`（`src/EngineImpl.cpp:101`） |
| **K2** 汉字定位与繁简 | 仅当 token 首字符**繁简转换后**能在 `word.txt` 查到读音，才进入"待注音"序列 | `docs/DECISIONS.md`；实现 `EngineImpl::assemble()` 的 token 循环（`src/EngineImpl.cpp:409`；原 `findZhPositions()` 已并入此处） |
| **K3** 单字读音 | 取 `word.txt` 中**顺序第 0 个**为默认读音（无频率/权重）；候选去重保首次出现顺序；查不到则 `failure_kind != Ok`、`rendered == text` | `docs/DECISIONS.md`；实现 `EngineImpl::applyTone()`（`src/EngineImpl.cpp:85`）、`EngineImpl::defaultReadings()`（`:148`） |
| **K4** 多音字判定 | 依据是 **`phrases_map.txt` 是否含该字**，**不是** `word.txt` 有几个读音 | `docs/DECISIONS.md`；实现 `EngineImpl::isPolyphonic()`（`src/EngineImpl.cpp:533`）、`EngineImpl::hasPolyphonic()`（``EngineImpl::hasPolyphonic()`（`src/EngineImpl.h`；该文件行号会随注释压缩平移，故改按名字引用）`，两层短语起点位图取或） |
| **K5** 四方向滑动窗口 | 长度 4→2 递减；每轮依次试 ①②③④；任一命中即**停止整个窗口搜索**；命中后**覆盖** `offset..offset+len-1`（允许覆盖先前结论，**不得顺手修正**） | `docs/DECISIONS.md`；实现 **`EngineImpl::queryRawInto()`**（`src/EngineImpl.cpp:174`；`emitPhrase` 在 `:277`，`kAttempts[12]` 表在 `:354`） |
| **K6** 用户词典 | 用户词典优先级最高，命中即用；`clear=true` 先清空再加载 | `docs/DECISIONS.md`；实现 `EngineImpl::loadUserDict()`/`loadUserDictLayer()`（`src/EngineImpl.cpp:546`/`:550`）、`EngineImpl::rebuildPhrases()`（`:681`）、`DictUtil::loadAdditionalDict()`（`src/dict/DictUtil.cpp:149`；2026-10-09 更新：该文件已从 `src/dict/DictUtil.cpp` 移入 `src/dict/`，行号 149 仍是同一处 `if (!file.is_open())`） |
| **K7** 样式转换 | 与 pypinyin 的 tone/tone2/tone3/bopomofo/first_letter 规则对齐 | `docs/DECISIONS.md`；实现 `src/toneUtil/ManTone.cpp`、`src/toneUtil/CanTone.cpp` |
| **K8** 结果装配 | 输出项数 == 分词 token 数；无读音项保留原文并标 `failure_kind != Ok` | `docs/DECISIONS.md`；实现 `EngineImpl::assemble()`（`src/EngineImpl.cpp:409`：先按 token 逐行填 `character` 与 failed 行，再由窗口按行写回读音） |
| **K9** 候选契约 | `candidates` = **该字自身**的全部读音（词典行序、转换后去重保首次）；词组/用户词典命中行的 `rendered` 可 ≠ `candidates[0]`、甚至不在其中；`includeCandidates=false` 清空**汉字行**，失败行恒为 `{character}` | 实测钉住：`tests/cpp/test_candidates.cpp`；实现 `EngineImpl::defaultReadings()`（`src/EngineImpl.cpp:233`）、`EngineImpl::assemble()`（`:511`） |

> **第 3 期（v2.3）后的实现形状**（语义仍然如上，逐字节门禁已证）：
> 结果**不再经过中间 `std::vector<Syllable>`**。`assemble()`（`:407-450`）先建好足量输出行并记下
> `tokenOf[k]`＝第 k 个汉字属于哪一行，`queryRawInto()`（`:172-405`）里滑窗照旧按方向/长度顺序执行，
> 但命中后直接写 `out[tokenOf[offset+i]]`。由此：
> - 原来的 `popLast`（②方向先 `pop_back()` 再重新 append）**等价于把上一行原址重写**，参数已删除；
> - 原 `result.size()` 承担的"已决定行数"由内部 `decided` 计数维持，二者不变式逐条对应；
> - 词组读音条数与词组长度不等（畸形词库）时按"不是词组"处理，避免把行写歪；
> - **第 3 期第三项（v2.3）把 12 次尝试的闸门提到游标层**并合并两张闸门表为 `PhraseGate`（`src/dict/PhraseIndex.h:39` 的 `struct PhraseGate`；原文写的 `src/EngineImpl.h:113` 已漂）：
>   顺序与语义逐条不变（`kAttempts[12]` 就是原有的"长度 4→2 × ①②③④"展开），
>   四条方向守卫合并为**等价的一条** `start >= 0 && start + len <= n`，见 §1.1 说明。


### 1.1 K5 四个方向的守卫（**行号已漂移，以此表为准**）

**本表不记录行号**：行号每重构一次就腐一次，所以这里只给守卫与表达式，位置以符号名为准。

| 方向 | 守卫（快照取第 3 期第三项落地后的 HEAD） | 表达式 | 读取末位 | 所需上界 |
|---|---|---|---|---|
| ① | **上述统一守卫**（`kAttempts[i].rel == 0`） | `start >= 0 && start + len <= n` | `cursor+length-1` | `cursor+length <= n` ✅ |
| ② | **同上**（`rel == -1`） | `start >= 0 && start + len <= n` ⟺ 原 `cursor >= 1 && cursor + length <= n + 1` | `cursor+length-2` | `cursor+length <= n+1` ✅（**C-01 已修，等价未收紧**） |
| ③ | **同上**（`rel == 1-length`） | `start >= 0 && start + len <= n` ⟺ 原 `cursor + 1 >= length && cursor + 1 <= n` | `cursor` | `cursor < n` ✅ |
| ④ | **同上**（`rel == 2-length`） | `start >= 0 && start + len <= n` ⟺ 原 `cursor + 2 >= length && cursor + 2 <= n` | `cursor+1` | `cursor+1 < n` ✅ |

**v2.3 起的形状**：四条守卫表达式不再逐条写在分支里，而是由上述那**一条统一守卫**
`start >= 0 && start + length <= chars.size()` 承担（`start = cursor + rel`）。等价性逐方向写在代码注释中：

- ① `cursor + L <= n` ⟺ `start + L <= n`（`start = cursor`）；
- ② 原守卫 `cursor >= 1 && cursor + L <= n+1` ⟺ `start >= 0 && start + L <= n`（`start = cursor-1`）；
- ③ 原守卫 `cursor + 1 >= L && cursor + 1 <= n` ⟺ `start >= 0 && start + L <= n`（`start = cursor+1-L`，末位 = `cursor`）；
- ④ 原守卫 `cursor + 2 >= L && cursor + 2 <= n` ⟺ `start >= 0 && start + L <= n`（`start = cursor+2-L`，末位 = `cursor+1`）。

四个守卫的**语义逐字未变**（第 3 期只把 `result.size()` 换成内部的 `decided` 计数、把分支换成了等价的一条守卫）。

**② 是唯一曾被写错的分支**：它原先只有下界 `cursor >= 1`，缺上界，可使 `assign` 越过缓冲区
末尾最多两个码位。详见过程记录里的 C-01 记录。

> ⚠️ **不可**把 ② 加到 `cursor + length <= chars.size()`：那会连 `chars[n-1]` 这个**合法读取**
> 也禁止，丢掉句末 2 字词组匹配（实测退步 0.16pt）。③④ 的原始守卫**本来就是对的，不要动**。
> 等价改写（v2.3）已按上表逐方向验证，任何进一步"顺手收紧"都要重跑 full dump 门禁。

---

### 1.2 K9 候选语义（**全语料实测钉住**）

2026-09-23 对 4 个数据集 × 两方言 × `includeCandidates∈{false,true}` 做了全量扫描
（3.6M 汉字行，探针与日志为本地留档（未入库）。

| 断言 | 结果 | 定性 |
|---|---|---|
| **I1** 非失败行 `candidates == readings(text, cfg)` | **0 处不符**（约 3.6M 行） | 契约，测试逐行回归（抽样） |
| **I2** `rendered ∉ candidates` | cpp_dataset 794 行 / jyutping 77 行 | **分层词典的固有结果**：词组表/用户词典可以收录字表没有的读音（如 `汤汤:shang1`） |
| **I3** `rendered != candidates[0]` | cpp_dataset 61,557 行 / jyutping 3,369 行 | 同上（词组命中行），pypinyin 的 phrase-dict-overrides-heteronym 语义一致 |
| **I4** 失败行候选 | `includeCandidates=false` 下仍为 `{text}` | **有意保留**（改动会破坏冻结 dump 口径） |

**取消条件（只在满足时重议）**：若 dump 工具改为"按 `includeCandidates` 打印"或整体重冻，
I4 才可修（预计收益 <10%，按 D7 本就不做）。I1–I3 是**契约**，任何改动必须先当作 bug 怀疑。

---

## 2. 准确率约束（FR-2）

### 2.1 四条冻结数字（**逐字不得退步**）

权威：`docs/PERFORMANCE.md` §2.3。快照下实测（内建门禁，2026-09-22）：

| 数据集 | 准确率 | 分子/分母 |
|---|---|---|
| 普通话 `cpp_dataset.txt` | **90.80%** | 7282 / 79117 |
| 普通话 `op_lab.txt` | **99.95%** | 104 / 192693 |
| 粤语 `jyutping_test.txt` | **94.94%** | 4725 / 93451 |
| 粤语无调 `jyutping_untone_test.txt` | **98.11%** | 1763 / 93451 |

**金标集冻结数字（2026-09-28 新增，与上表并存，两套都不许退步）**：上表的判据有事实缺陷
，
所以另有一套**逐位置严格、双轮独立复核**的金标集，它的数字**低**是判据更严的结果，不是引擎退步：

| 数据集（`tests/data/verified/`） | 行数 | strict | algo | ENGINE_BUG | DICT_GAP | EXCLUDED |
|---|---:|---:|---:|---:|---:|---:|
| `mandarin_tone`（源自 `cpp_dataset`，分层） | 9,391 | 32.13% | 37.74% | 4,978 | 1,396 | 0 |
| `mandarin_untone`（源自 `op_lab`，分层） | 1,138 | 93.32% | 93.49% | 74 | 2 | 0 |
| `cantonese_tone`（全量） | 6,354 | 77.83% | 80.84% | 1,172 | 236 | 2 |
| `cantonese_untone`（全量，与 tone 同源） | 6,354 | 90.85% | 92.78% | 449 | 132 | 2 |

> ⛔ **上表已作废（A26）**：它不是权威，数字与今天的树不一致（实测：`mandarin_tone` 的 `ENGINE_BUG`
> 为 4,978 而权威值是 4,894，`DICT_GAP` 为 1,396 而权威值是 1,434；`mandarin_untone` 的 `ENGINE_BUG`
> 为 74 而权威值是 73；`cantonese_untone` 的 `ENGINE_BUG` 为 449 而权威值是 454、`DICT_GAP` 为 132
> 而权威值是 131）。**唯一权威**是
> `tests/cpp/test_accuracy_v2.cpp` 的 `kFrozen[]`（2026-10-20 实测：表在 **:136-142**；本行原写 `:281-286`，那是同一文件里的另一段代码 —— 行号指针漂了 145 行）。它由测试实时判定并逐字节冻结；本表只作历史留存。
>
> **口径（勿互推）**：`kFrozen` 的 `ENGINE_BUG` 是**语料状态列的计数**（审音时判定的结果，随金标修订而变），
> 而 `pass` 是**测试运行时对引擎的实时判定**。两者来自不同时刻、不同判据，**不能互相推算**：把 `pass`
> 当成 `decided - ENGINE_BUG - DICT_GAP` 之类的算式就会与权威表冲突（实测差异正来自此）。

（第三轮抽样审计 + 第四轮定向修复 + 第五轮自洽修复之后冻结；四轮共改金标 1,274 行、`cm` 的 `ENGINE_BUG` 净增 7 条；
逐条审计台账是 `tests/data/testdata/` 下的过程记录（`audit_round3.md`…`audit_round24.md`、`coverage.md` 等），
2026-10-22 随那批文件一并删除，`git log --diff-filter=D -- tests/data/testdata/` 仍能从删除它们的那个提交里取回；
结论在本节。）

判据把每次数字**逐项精确比对**（不是阈值），因此"修好一个缺陷"必须同步重新冻结，方向只许变好；
按分层读数字（失配行被刻意过采样）。**同一批位置在旧 harness 与金标裁决下的对照**（口径修正的最终结果，冻结在此）：

| 语料 | 旧 harness（位置级） | 金标裁决下同一批位置 |
|---|---:|---:|
| `cpp_dataset`（`cm`，7,282 标注位） | 90.80% | **93.61%** |
| `op_lab`（`mu`，104 标注位） | 99.95% | **99.97%** |
| `jyutping_test`（`ct`，4,870 标注位） | 94.94% | **98.52%** |
| `jyutping_untone_test`（`cu`，1,856 标注位） | 98.11% | **99.42%** |

**逐位置全量口径**（金标集自身，含每行所有位置）与分层：

| 数据集 | 位置级严格 | 失配层 | 抽样层 | 扫描层 |
|---|---:|---:|---:|---:|
| `mandarin_tone` | 96.62% | 95.92% | 99.00% | — |
| `mandarin_untone` | 99.29% | 94.06% | 99.95% | — |
| `cantonese_tone` | **98.24%** | 97.07% | 99.57% | 99.77% |
| `cantonese_untone` | **99.37%** | 98.93% | 99.86% | 99.95% |

**每次构建都会打印 `ENGINE_BUG/EXCLUDED` 明细**，
且这两个数只许减少（D16 约束 1）。

**一条命令跑完全部门禁**：`ctest --test-dir <build> -j <n> --output-on-failure` —— 实测 **15/15 通过
（2026-09-29，`-j4` 全量墙钟 5.5～6.4 s，连续 8 次全绿）**：`CppPinyinTests`（跑 `~[corpus]`，1,292 条断言 /
114 用例）/ `CorpusAccuracy`（33,262 条断言 / 1 用例，标签 `corpus`）/ `CorpusGate_*` 五条（check_corpus /
ledger / consistency / cross_set / projection，标签 `corpus`）/ `CorpusGateSelfTest` / `BenchmarkSmoke`
（`--brief --json`，约 3 s）/ `HeaderSelfCheck` / `LoadCheck` 三条 / `DumpEquivalence` + `DumpEquivalenceSelfTest`。
`LoadCheck` 已不需要 `-E "LoadCheck$"` 排除（0.3 s）。逐字节等价另由 `DumpEquivalence` 或
`tools/dump_equivalence.cpp` 负责（约 4 s）。**冻结指纹的唯一权威是 `tools/CMakeLists.txt` 的
`CPP_PINYIN2_FROZEN_DUMP_SHA256`**——**2026-10-01 重录为 `820c880f…`**（5,384,712 条 /
115,666,595 B）：`res/dict` 在 `8e992bc`（2026-09-29「Adopt Mandarin phrase corrections」）变更后，
旧值 `B871E3B7…` / 115,712,228 B 已不可复现，而该门禁自那天起一直是红的。旧值仍可用那之前的
输入复现（把工具指向 `data/dialects` 与 `tests/data/verified/source`；旧链 SHA 在今天的 `v2` 上解析不到，该配方只对当时那棵树成立）
> **2026-10-01 现状（P1 + 缺陷①②③ + P2 之后实测）**：`ctest` **16/16 全绿**（`100% tests passed`）。
> `DumpEquivalence` 随上面的重录转绿；`CorpusGate_consistency` 也转绿——`8e992bc` 新增词组键后
> 扫描范围扩大，暴露的是语料**自身**的 12 处差异（多为跨词边界：`大将军` 的 `jiang1` vs 单词
> `大将` 的 `jiang4`），已补进 `tests/data/corpus/consistency_baseline.txt` 并逐条括注，`new: 0`。
> 新增 CTest 条目 `CApiSmoke`（**用 C 编译器**编译的 C99 用例，验证 C ABI 能被纯 C 调用）。
> 断言计数：`~[corpus]` **3,134 条 / 129 用例**（P1 的 `[engine]` 7 用例 / 583 断言、P2 的 `[c_api]`
> 8 用例 / 1,259 断言）、`[corpus]` **33,274 条 / 1 用例**——上文 2026-09-29 记的 33,262 / 34,554
> 是那一轮的快照，现为 33,274 / 34,566。详见 `docs/DECISIONS.md` §2.1。

> **2026-09-29 去重（实测理由）**：`CppPinyinTests` 的 CTest 条目加了 `~[corpus]` 过滤器。该二进制共
> 34,554 条断言，其中 **33,262 条（96.3%）来自 `[corpus]` 那一个用例**，而它本来就被
> `CorpusAccuracy` 条目单独跑了一遍——**每次 `ctest` 都跑两遍语料**。过滤器写在**条目**上、不写进用例，
> 所以用例本身没动；代价是 `ctest -R CppPinyinTests` 单跑不再覆盖语料，那部分由 `-R CorpusAccuracy`
> 或 `-L corpus` 负责（这条已写在 `tests/CMakeLists.txt` 的注释里）。

**`enable_testing()` 在根 `CMakeLists.txt` 的 `if (CPP_PINYIN2_BUILD_TESTS)` 里**，所以 `ctest` 在顶层构建目录
与 `<build>/tests` 下都能跑（早先在顶层会报 `No tests were found`，六个已注册门禁实际不可达）。CI 的三个
job 因此统一用 `ctest --test-dir build -j <n>` 而不是 `./CppPinyinTests`，这样 CI 与本地跑的是同一组门禁，
Python 侧的五条与 `DumpEquivalence` 不再只在本机被跑到。

> **前置条件（第三十轮踩到，实测）**：`tests/` 与 `tools/` 由 `-DCPP_PINYIN2_BUILD_TESTS=ON` 控制，而它**默认 OFF**（`CMakeLists.txt:17`，第 97–99 行 `if (CPP_PINYIN2_BUILD_TESTS) add_subdirectory(tests) add_subdirectory(tools)`）。用默认参数配置**只会构建库**：`bin/` 里没有测试/工具可执行文件，`ctest` 报 `No tests were found`（响亮的失败，不是静默通过，所以只是易踩、不算危险）。含共享库与 Release 的完整一条：
> ```
> cmake -S . -B cmake-build-release -G Ninja -DCMAKE_BUILD_TYPE=Release \
>       -DCPP_PINYIN2_BUILD_TESTS=ON -DCMAKE_CXX_COMPILER=<c++>
> cmake --build cmake-build-release
> ```
> 实测：如此重建后 6/6 门禁全过（`CppPinyinTests` 4.52 s / `BenchmarkSmoke` 0.96 s / `HeaderSelfCheck` 1.60 s / `LoadCheck` 0.20 s / `LoadCheckSelfTest` 0.01 s / `LoadCheckNegativeCase` 0.05 s）。

**加载回归门禁**：CTest `LoadCheck`（min-of-5 对 500 ms，实测 20×+ 余量）+ `LoadCheckSelfTest` + `LoadCheckNegativeCase`（阈值设 0 ms 必须失败，已实测 exit 1）。判据只看 min、阈值取大冗余，所以它抓灾难性回归而不引入 flaky（加载段落 5 个样本实测 21.1–29.4 ms，max/min = 1.39）。

**基准冒烟门禁**：CTest `BenchmarkSmoke` 运行 `Benchmark`（实测 1.01 s，exit 0），只验证"能跑完"、不断言时间（加载噪声本机 ±2%、板上 1.3–2.6%）。

**公共头自包含门禁**：`HeaderSelfCheck`（同名 CTest 条目，用 `ctest --test-dir <build>/tests -R HeaderSelfCheck` 运行）逐个编译 `include/cpp-pinyin2/*.h`，失败即构建失败；负例自证：缺 `<string>` 的头会被拦住。

**全量测试套件**：**34,850 断言 / 119 用例**，必须 `exit 0` 全绿（= 原有的 1,308 / 118，加上金标仓
`[corpus]` 的 33,542 断言 / 1 用例；2026-09-23 第三轮起；第二轮 1,266/114，v2.4 为 1,190 断言 / 107 用例，
v2.3 为 624 断言 / 91 用例——v2.4 新增 `test_candidates.cpp` / `test_style_convert.cpp` / `test_tone_sandhi.cpp`
三个文件共 16 个用例，语料抽样断言按行聚合以免断言总数失去意义；其后 `isHanzi` 边界用例 +10 断言）。

### 2.2 等价性门禁（P1）

任何改动必须满足其一：
- **强等价**：全语料 79,117 句输出**逐字节相同**；或
- **弱等价**（允许极少数差异时）：四条准确率数字**逐字未变** + 逐句差异**逐条给出并定性**。

历史先例：C-01 修复达到 79,116/79,117 逐字节相同，唯一差异经词表取证定性为"两种合法读音"。

### 2.3 判分口径（**易错，已实测澄清**）

- `tests/data/verified/source/cpp_dataset.txt` 格式为 **`句子▁音节下标▁该位置期望读音`**（如 `…▁17▁le5`）。
  `▁` = U+2581。**判分只比较该下标出的一个音节**，不是整句、也不是"句末标记"。
  
- 期望值支持 `a/b` 多可接受答案（旧 harness 入口 `tests/cpp/test_batch_accuracy.cpp` **已随旧语料于 2026-09-29 删除** ⇒ 本条留作历史口径）。
- **粤语旧宽松判据已退役**（"整行任意位置出现过即算对" ✗）：其宿主测试随旧语料一同删除，现行金标集对粤语**同样逐位置严格**比较。
  同一批位置的旧→新对照见本节上方的准确率表：`jyutping_test` 94.94% → **98.52%**、`jyutping_untone_test` 98.11% → **99.42%**（原表另有一处的读数已更正为 **99.42%**；历史缺陷记录见过程记录。）
- **金标集口径（`tests/data/verified/`，`tests/cpp/test_accuracy_v2.cpp`）**：每行 4 个 TAB 字段
  （句子 / 逐位置期望 / 状态 / 备注），**逐位置严格比较**，`a/b` 按备选展开；
  位置数不一致的行**不许比较到 `min()` 就停**，必须 `REQUIRE(status == "EXCLUDED")`；
  **缺失即 FAIL**（不是 WARN+return）：引擎对某个汉字没有读音、把原字符回显回来时，
  该位置**判错**，不会因为"回显等于期望字符"而白送分；对标点/拉丁/数字这类引擎本来就原样透传的
  字符，才按相等比较。每条数字**逐项精确冻结**（`kFrozen[]`），改动只能朝"修好了"的方向，且必须同步重新冻结。

---

## 3. 性能约束（FR-3）

### 3.1 测量协议 P1–P6（**不得绕过**）

权威原文：`docs/PERFORMANCE.md:14-46`。

| 阶段 | 要求 |
|---|---|
| **P1** | 等价性门禁：两侧准确率**逐字相同** |
| **P2** | 逐样本交错测量（消解漂移） |
| **P3** | **min 判优**（不是中位数） |
| **P4** | **≥7 轮** |
| **P5** | 收益 **≥1.10** 才接受 |
| **P6** | **独占机器**：A/B 期间不得并行跑构建/门禁/其他压测（实测反例见 `docs/PERFORMANCE.md` §1） |

**工具自证**：任何测量工具改动后必须做**零效应对照**（A=B 同一二进制）。

**⚠️ 噪声地板（2026-09-22 复验，权威见 `docs/PERFORMANCE.md` §2.0）**：
实测**噪声幅度 21–31%**；**同一二进制**跑 A=B 也能测出 **min 比值 1.0113** 的假差异。
历史上流传的"min 噪声地板 1.46%"**已失效，勿再引用**。
⇒ 单次读数完全不可用；P3 的 min 判优 + P4 的 ≥7 轮是**必需**的，不是形式。
**判优时必须同时给出当次零效应对照的读数**，否则无法判断收益是否超过本底。

### 3.2 禁止的测量方式

权威：`docs/PERFORMANCE.md:80-88`。

- ❌ 引用测试内打印的吞吐（`fmtRate` 仅 2 位有效数字）
- ❌ 单次读数、中位数判优
- ❌ 只报一侧（内置门禁与真实数据**必须双侧报数**，只说一侧会掩盖另一侧退步）
- ❌ 用 `-O2` 测量。库口径是 **`-O3 -DNDEBUG -std=gnu++17`**；本代码 `-O2` 与 `-O3` 差约 9 倍

### 3.3 优化准入与丢弃规则

- **收益 < 10% 一律丢弃**，并在代码/文档留「**已试过、无效、勿重试**」注释（含读数）。
  权威：`docs/DECISIONS.md`（D7）。
- **先量基线收益再动手**：确认代码真在热路径被调用，并实测前后耗时。禁止凭代码气味推断
  （历史教训：`toneToTone3` 删 `reserve` 凭气味推断，实测只值 0.1%）。
- **单点增行 ≤ 15 行**；最小改动优先，不堆层次。
- **与编译器常见优化/标准库不得相抵触**。

### 3.4 已否决向量（**勿重试**）

权威：`docs/PERFORMANCE.md` §4.3–§4.10。速查：

| 编号 | 向量 | 实测结果 | 重试门槛 |
|---|---|---|---|
| §4.3 | SIMD 向量化 | 净收益 1.3% < 噪声 | 仅当改为批量调用后才有 >3x 空间 |
| §4.4 | `toneToTone3` 删 `reserve` | 1.02x、整遍 0.1% | 无需重试（前提已被证伪） |
| §4.5 | `convert` 改 `const&` | 0.9827（更慢） | — |
| §4.7 | 按窗口起点筛剪 lookback | **丢 17,771 次真命中** | 方向已关闭 |
| §4.8.1 | 快路径逐段拆解 | 整支只占约 13% | — |
| §4.8.2 | 词组表换容器 | 三种替代慢 2–4 倍 | — |
| §4.9 | 删三个结构性死格 | 省 24.8% 查找**只值 3.0%** | 单次尝试实际成本约 1.8 ns，非 23.8 ns |
| §4.16.4 | ~~词组容器扁平化 / 开放寻址~~ | **已撤销**：第 3 期专用索引 本机 1.2083 / 板上 1.1193 | 见过程记录、§4.17.5 |
| §4.16.4 | ~~C/F 融合单遍装配~~ | **已撤销**：第 3 期 本机 1.2208 / 板上 1.2529 | 见过程记录、§4.17.5 |
| §4.16.4 | ~~H 后续「掩码判定提到窗口外」~~ | **部分撤销**：v2.3 把 12 次尝试的**闸门**提到游标层（板上 1.0887 单步）+ 合并闸门表（合计 **1.1063**） | 见过程记录–§4.18.5；"守卫去零"仍不做，失效条件见 §4.18.7 |
| §4.2 | gprof / 自写 IP 采样器 | 本环境不可用 | — |
| §4.19 | `-flto`（跨 TU 内联） | 板上 9 轮 × 4 passes **1.0028**（med 1.0097）＝**0.3%** | **方向已关闭**（P5 丢弃，且无收益不该改进默认构建）。**本机不可测**：CLion MinGW GCC 15.2 报 "LTO support has not been enabled"，勿在本机重试 |
| §4.19.1 | `toneToTone3` + `u16strToUtf8str` 融合成单遍转换 | 静态上限：两者自耗合计 **38.2%**（板上 callgrind），融合后上限 ≈10–15% | **不做**（超 D7 的「单点 ≤15 行」，需 30+ 行新路径且与 9 个风格共用面分叉）。**失效条件**：出现"必须再挤 10%"的硬需求时先做只测不改的融合探针 |

**已落地的净收益（按时间顺序）**：`原 846d944`（装载期合并词组两层，min A/B 1.1404）→ 第 2 期（§4.13/§4.14/A/B/E1+H，
板上链式 ≈1.915×）→ **第 3 期（直接索引 + 单趟装配 + 窗口闸门批量化/槽合并，板上 1.4024 × 1.1063 = 1.5516）**。
**相对 v1 累计 ≈2.95×**（3066.3 → 1040.5 ms；乘积口径 2.972、保守端 2.877；**v2.4 板上复核 min 1039.4 ms，维持该结论**），判优口径与每步门禁见
过程记录与 §5。

---

## 4. 行为与接口约束（FR-4）

| 编号 | 约束 | 依据 |
|---|---|---|
| FR-4.1 | **跨平台**：x86-64 基线 SSE2；ARM 需回退。若用 SIMD 必须**运行时探测 + 纯标量回退** | `docs/DECISIONS.md`（D14） |
| FR-4.2 | **不引入外部依赖**（新库/新工具），除非用户明确同意 | 工作区协议 |
| FR-4.3 | **允许破坏接口**，但新接口必须**长期稳定且包含全部功能**；允许破坏 ABI，但保证源码级迁移路径 | `docs/DECISIONS.md`（D8） |
| FR-4.4 | **抛弃全部技术债/架构债**——不留过渡层 | `docs/DECISIONS.md`（D17） |
| FR-4.5 | 词典路径**只作为构造参数**，类型层面钉死，不依赖全局可变状态 | `docs/DECISIONS.md`（D12） |
| FR-4.6 | `isHanzi` 范围：`[0x3400,0x4DBF]` + `[0x4E00,0x9FFF]` + `[0xF900,0xFAFF]` | `docs/DECISIONS.md`（D6 原文写作 `[0x3400,0x9FFF]`）。**2026-09-23 落地**：`src/G2pglobal.cpp` 已改为三段式，把 U+4DC0–U+4DFF（易经六十四卦符号，非表意文字）排除在外；旧实现是 `[0x4E00,0x9FA5]`（Unicode 4.0 之前的基本区末尾）。**引擎从不依赖该谓词**：`EngineImpl::splitInput` 原分支 `isHanzi(c) \|\| isDigit(c) \|\| !isSpace(c)` 中后两项对"非空格"恒真，`isHanzi` 从未决定过分支（该死条件已删），所以放宽范围**不改变任何转换输出**——full dump 逐字节相同（`B871E3B7…`）。 |

### 4.1 已知未修的架构债（**活的事实，勿当成已修复**）

> 2026-09-23 复核：本节原先的三条**有两条是错的**，已按代码实测更正（`file:line` 均可复核）。

- ~~**全局词典路径无同步**~~ **本条撤销**：仓库里**不存在全局词典路径**——`setDictionaryPath` /
  `getDictionaryPath` / `dictionaryPath` 实测 **0 处**，`src/` 中 `mutex`/`lock`/`atomic` 也曾是 **0 处**
  （**2026-10-01 更新**：`src/Engine.cpp` 的方言注册表新增 1 把 `std::shared_mutex`，只在 `Engine`
  构造时取锁，**查询热路径仍是 0 锁**。，
  词典根只作为构造函数参数进入 `EngineImpl::m_dictDir`。**FR-4.5 / D12 已经达成**，没有"待加锁的全局量"。
  ⚠️ 已删除的旧 `docs/BUG_REPORT.md` 声称"已添加 `std::mutex`"——那是错的；现存文档说它是"未修的债"——
  **同样是错的**。
- **还活着的线程安全项是实例级的**：`loadUserDict` / `loadTraditionalDict` / `setDisambiguationCallback`
  会改读者正在读的表，与并发 `query()` 重叠是数据竞争。
  〔**2026-10-10 改写（原为指向已删除门面的陈旧锚点）**：契约现在是 `include/cpp-pinyin2/core/Engine.h`
  的类文档注释 **Thread safety** 段（"MUTATE tables that readers touch … that is a data race today
  (measured: concurrent loading corrupts the heap)"），门面类 `Mandarin` / `Cantonese` 与其两个头已删除
  （见 `docs/API_zh.md` §2）；"既未加锁也未测试"里的**未测试**这半句也已不成立 —— 只读并发有
  `ThreadSafety` / `InstanceIsolation` 两条 CTest，写读重叠仍是设计上不支持、无门禁〕
- ~~**词典加载失败静默**~~ **已修**：`EngineImpl::init()` 逐个文件收集失败名并给出
  `"failed to load: <文件名> (under <目录>)"`；**2026-10-10 改写**（原写"通过公开的
  `Mandarin::lastError()` / `Cantonese::lastError()` 暴露"，而这两个门面类与该头均已删除）：现在从
  `Engine::open(...)` 返回的 `Outcome` 取（`error().detail`），或从描述构造的两件式面取
  （`Engine.h` 的 `usable()` / `reason()`）；`user_dict.txt` 属可选文件，缺失不再让引擎不可用。
  旧 TD-006 描述的"哪个文件无从得知"已不成立；`DictUtil.cpp` 里那个从未被使用的 `path` 形参（旧的
  "缺口记号"）也已删除。
- `m_toneConverter` 已是 `private`——这一条**确实已修**，旧 ISS-013 可关闭。
- **三项架构债：运行复现 → 已修（2026-09-23，第二轮）**。三项都不是读码推断，而是先做出可复现的
  失败证据再改（探针 `.tmp/probe/arch_probe.cpp`，用例 `tests/cpp/test_user_dict.cpp`）：
  1. ~~`loadUserDict(path, /*clear=*/false)` 丢掉全部内置词组~~ **已修**：`rebuildPhrases()` 删掉
     `reloadBuiltin` 形参，无条件从 `phrases_dict.txt` 重载内置层（该文件本就是权威副本，不需要额外快照容器，
     不违反 D7）。**实测**：修复前 `loadUserDict(other, false)` 后「银行」由 `yin2 hang2` 变 `yin2 xing2`；
     修复后仍 `yin2 hang2`，且用户层叠加（`一旦` → `yi2 dan4`）生效。`clear` 的语义已写进
     `docs/API_zh.md` / `docs/API_en.md` 的对照表。
  2. ~~`Mandarin::isReady()` 对 moved-from 对象解空指针~~ **已修**：`Mandarin.cpp`/`Cantonese.cpp` 均改为
     `impl_ ? impl_->engine.isReady() : false`，与同文件 `lastError()` 的既有守卫统一。**实测**：修复前
     `isReady()` 在 moved-from 对象上触发 `0xC0000005` 访问违例（探针 `exit=-1073741819`），修复后返回
     `false` 且进程存活。契约写进两个公共头与 `docs/Custom Chinese Dialect.md`。
  3. ~~**粤语用户词典被普通话去调器改写**（审计 A-01，此前任何文档都没记）~~ **已修**：根部是
     `EngineImpl.cpp` 硬写 `tone3ToTone`、`DictUtil.h` 把它当默认实参、`DictUtil.h` 还把普通话声调层拉进
     词典层。改法是把"外部词典读音 → 引擎内部形式"抽成 `ToneConverter::m_normalize`（**函数指针而非虚函数**：
     引擎按值持有转换器，值拷贝会切片到基类，虚派发会静默退回基类实现），由方言在自己的构造函数里注册：
     `ManTone` → `tone3ToTone`，`CanTone` 不注册即恒等；`loadAdditionalDict` 的该形参改成必填；
     `EngineImpl` 构造时注入转换器，`setToneConverter()` 删除（构造后再换会让已装载词典与转换器不一致）。
     **实测**：修复前 `粵語:jyut6 jyu5` → TONE3 `jyu6t6`/`jyu`、`廣州:gwong2 zau1` → `gwóng`/`zāu`、
     NORMAL 砍掉真字母成 `jyu6t`/`gwón`；修复后 `jyut6 jyu5`/`gwong2 zau1` 原样保留、NORMAL 得 `jyut jyu`/`gwong zau`。
     零测试覆盖的根因是 `data/dialects/cantonese/user_dict.txt` 为 0 B —— 本轮补上 8 条用例填住这个盲区。
  **本轮等价性**：dump 逐字节相同（`B871E3B7…`，115,712,228 B）、Catch2 **1,266 断言 / 114 用例** exit 0、
  四条准确率数字未动、装载时间 A/B 1.0068（7 轮交错，min）——三项修复对冻结基线中性。

### 4.2 第 4 期（v2.4）接口扩展：`INITIALS` / `FINALS` / `FINALS_TONE3` + 变调后处理

**授权**：用户 2026-09-23 明确指示"参考 python-pinyin 的功能，择优设计接口和方案、自动实施"，
**FR-5.5 的"须先确认"在此范围内由该指令豁免**（其余改动仍需确认）。

| 新增 | 形状 | 对齐/约束 |
|---|---|---|
| `MandarinStyle::INITIALS=5` / `FINALS=6` / `FINALS_TONE3=7` | 本库自有编号（**与 pypinyin 的 `Style` 数值不同**）；填满 `ToneConverter::m_converts[9]` 剩余空槽 | K7：与 pypinyin 0.55.0 `to_initials`/`to_finals`/`to_finals_tone3` **逐例对照 113/115 逐字节相同** |
| `ManTone::toneToInitials/toneToFinals/toneToFinals3` | `(const std::u16string&, bool v_to_u=false, bool neutral_tone_with_five=false)` | 同上；`FINALS` 默认 `v` 记法，`vToU=true` 时输出 `ü` |
| `Result::operator[]/at` 非 const 版本 | 可写 `Syllable&` | 供变调等调用方后处理改写 |
| `cpp_pinyin::v2::applyMandarinToneSandhi(Result&, const ToneSandhiOptions& = {})` | 返回改写行数；**可选后处理，`query()` 内不调用** | 只改 `rendered`（并把新声调同步进 `reading.tone`）；行数/`text`/`candidates`/`failure_kind` 不变 |

**已记录的差异（不要当成 bug）**：

1. `ê` 系列叹词拼写（`ê̄` / `ế`）：pypinyin 视作带调的 `ê`，本库不做特判 → `FINALS` 返回空串。
   已核对 `data/dialects` 全部词表**不含任何 ê 族或组合附加符号字符**，故不影响库内数据与 dump。
2. `vToU` 只影响**写作 `ü` 本身**的读音；带调 `ǖǘǚǜ` 一律归 `v`（`女:nǚ,nü` → `nv3` + 候选
   `{nv3,nü}`）。既有契约，测试 `test_golden_pinyin.cpp` "Mandarin TONE3 v_to_u" 已钉住，
   **本轮不改**（改它属于行为变更，收益 0，风险高）。
3. pypinyin 的 `tone_sandhi` 与规则本体有两处实现差异，本库**按规则本体实现**：
   三声串"仅当结束于该分段时才生效"（`老鼠`→pypinyin `lao3 shu3`，本库 `lao2 shu3`）；
   缺"一 + 数字/一 → 一声"例外。见 `include/cpp-pinyin2/core/ToneSandhi.h` 顶部与过程记录。

**未采纳**：slug/lazy 包装（`join()`/`toVector()` 已覆盖）、
`errors='replace'`、西里尔/威妥玛/盲文/GR 等风格、插件注册表、`pinyin(heteronym=True)` 投影、
`convert_finals` 的公开暴露。

---

## 5. 仓库与流程纪律（FR-5）

| 编号 | 约束 |
|---|---|
| FR-5.1 | **禁止 push**，完全本地完成。fetch/pull/clone 需事先确认。禁止 `--force` 等破坏性选项 |
| FR-5.2 | **临时产物一律不入 git**（`本地留档（未入库）：` 等）；提交前核对 `git diff --cached --name-only` |
| FR-5.3 | **精确暂存**，禁止 `git add -A` / `git add .`；不提交用户刻意未跟踪的文件 |
| FR-5.4 | 每阶段**独立提交、可回滚** |
| FR-5.5 | 会改行为或改接口的方案，**须先出方案并经用户选项式确认** |
| FR-5.6 | 断言必须**回溯到代码/实测**；**推断与事实分开标注**；禁止凭记忆转抄 |
| FR-5.7 | 用户手改过的文件是**既定事实**，不覆盖、不回退 |
| FR-5.8 | 回复用中文；用户给编号意见时**逐条回应**；结论先行、证据（`file:line`）随后 |

### 5.1 待用户确认的未决项

| 项 | 状态 |
|---|---|
| `C-15` 繁简读音（`toSimp` 的**全部**调用点：`wordLookup(toSimp(...))`、`hansList.emplace_back(toSimp(...))`、以及两个按首字取读音/取字形的入口 —— 本行原写 `src/EngineImpl.cpp:142-160`，实测该区间内**一处 `toSimp` 也没有**，是指错了地方 ✗，故改记符号锚点） | **留 v2**，不擅自改。关键反例：`cpp_dataset` 25 行含「徵」，20 行是「魏徵」应读 `zhēng`，现有改写**反而正确** |
| `M3` / `C3` 数据修正 | 已延后 |
| 儿化 `r5` 4 行 | 标 `UNREVIEWED` |
| 417 个 `u:`/`ü`/`v` 记法统一 | 原则同意，**未落盘** |
| `user_dict.txt` 8 条「都」-名 `dou1` 应为 `du1` | 属数据修复，未动 |
| 失败行候选（K9 / I4）：`includeCandidates=false` 时失败行仍带 `{text}` | **有意保留**；改动会改变冻结 dump 口径且收益 <10%（D7） |
| `vToU` 对带调 `ǖǘǚǜ` 无效（§4.2 差异 2） | 既有契约，**留待用户裁决**；改它属行为变更 |

---

### 5.3 稳定版契约：四套门禁，与"必过面 / 可选项"（2026-10-04 立）

"稳定版"在本仓**不是口号，而是下面这四套（加文档事实共五面）可机器核验的门禁**。任何改动只要它们仍全绿，
就没有破坏稳定性；反之，改动如果动了其中任一面，就必须**在同一次改动里**更新它的权威值并说明理由。

| 契约面 | 权威在哪（单一真相源） | 判据 |
|---|---|---|
| ① 接口冻结 | `tools/CMakeLists.txt` 与各公共头 | 15 公开头 / 23 C ABI / 18 插件面声明（本行由 `tools/check_doc_claims.py` **机器校验**） |
| ② 全量行为等价 | `tools/CMakeLists.txt` 的 `CPP_PINYIN2_FROZEN_DUMP_SHA256` / `_ENTRIES` / `_BYTES` | 全量 dump 字节流 SHA256 与条数/字节（清单第 1 步据此判 PASS；**权威值只在该文件**） |
| ③ 出厂数据一致 | 词典仓 `tools/check_drift.ps1`（**字节权威**） | 本仓 `data/dialects` **逐字节**等于词典仓生成物 ⇒ `0 drift` |
| ④ 配置矩阵 | `docs/NEW_DIALECT_PLAYBOOK.md`（计数与**指纹算法**） | 四套配置的条目计数 + 集合指纹；只允许按已记录方式变化并同步文档 |
| ⑤ 文档事实 | `tools/check_doc_claims.py` / `tools/audit_doc_anchors.py` | 0 不一致；锚点分类不漂 |

**必过面**：①–⑤ 的默认运行。**怎么跑**：

```
run_local_gate.sh --checklist <builddir> 1     # 本机夹具；期望 CHECKLIST: PASS（内含 ①③④⑤ 与 L3 判据）
cd <pinyin-makedict>; powershell -File tools/check_drift.ps1 -CppPinyinRoot <cpp-pinyin2> -Python <python>   # 期望 0 drift
```

**明示的可选项**（已裁定；它们的缺席**不是**缺陷，别当成待修项）：

- 词典仓 `.cpdict` 二进制工作流（`binary/`）：**不进 CTest**（2026-10-04 裁定）。它当前是干净的
  （`build_binary.py --all` exit 0、`tools/check_binary.py --all` = `OK: binary matches the text tables exactly`）。
- sanitizer：**门禁化**的那条腿当年**只能靠 CI**（需 push 才能接线）⇒ 曾归"出仓后第一件事"。
  **2026-10-27 更新：那条腿已经接上并跑过** —— `.github/workflows/ci.yml` 的 `linux-sanitizers` job
  （`CPP_PINYIN2_SANITIZE=ON`、`-fno-sanitize-recover=all`、`ASAN_OPTIONS=detect_leaks=1`）随 `test_runner.yml`
  在 2026-10-22 起跑、2026-10-23 全绿（运行记录见 `docs/STABILITY.md` §0.1），所以"出仓后第一件事"这一项已结。
  **2026-10-06 更正（原句只说"只能靠 CI"、且未限定是哪条腿，结论过强）**：本机另有**一条已实测可跑的 clang/MSVC 目标窄路**
  （**examples OFF** 配置下全量 **32** 条：`-LE slow` 27/28、`-L slow` 4/4、ASan/UBSan 报告 0 条），但那只是**手工复现**；
  配方与全部边界见 `docs/STABILITY.md` §1.1（本节只写指针，不复写命令）；条目计数与指纹的权威清单在 `docs/NEW_DIALECT_PLAYBOOK.md`。
- 板上复验（firefly.local）：只在改动**构建 / 门禁 / 数据 / 历史形状**时是必须的；纯文档或纯工具改动
  用本机清单即可（口径与配方见 `docs/STABILITY.md`；**板上夹具不入库**（2026-10-23 决定：`board_baseline.sh` / `checklist.sh` / `run_local_gate.sh` 随本机保留，见 `.gitignore`））。

**跨仓口径分层（别混用）**：本仓 `data/dialects` 与词典仓生成物的"逐字节"判定**只在** `check_drift.ps1`；
归因（内容 vs 顺序）在词典仓 `tools/compare_dict_dirs.py`。三者的职责边界见 `tools/check_drift.ps1` 的文件头（字节权威）与 `tools/compare_dict_dirs.py` 的模块注释（原总表所在的 `docs/REPO_LAYOUT.md` 已于 2026-10-23 删除）。

## 6. 决策台账索引（D1–D23）

**权威在 `docs/DECISIONS.md`**，本文件**不复制结论**（避免副本漂移）。仅给主题索引，便于定位：

| D | 主题 | 一句话 |
|---|---|---|
| D1–D2 | 公共 API 形态 / 异常返回状态 | — |
| D3 → D15 | 二进制词典（D3 已被 D15 取代） | — |
| D4 | 非汉字符输出策略 | — |
| D5 | 用户词典覆盖强度 | — |
| D6 | `isHanzi` 范围 | — |
| **D7** | **性能投入档位** | 只做编译器替不掉的部分；<10% 丢弃 |
| D8 | ABI 破坏 | 允许，保源码迁移 |
| D9 | 准确率/性能如何进 CI | 准确率设真实下限；吞吐不设硬门限 |
| **D10** | **文档收敛** | 合并旧文档（**本文件的直接授权来源**） |
| D11–D13 | 方言挂载 / 全局路径 / 词典格式解耦 | — |
| D14 | 跨平台矩阵 | — |
| D15 | 二进制词典范围 | — |
| D16 | 测试集治理与迁移 | **已落地**：金标集 `tests/data/verified/` + `[corpus]` 判据（严格逐位置、缺失即 FAIL、数字精确冻结）；裁决结果即本节上方的准确率表（逐条审计台账已随过程记录删除）；错误簇与缺读音清单的结论已并入`tests/data/verified/` 的状态列与备注列 |
| D17 | 重构力度与验收 | 10 条可测验收线 |
| D18–D21 | 记法修正 / 疑似错标 / 覆盖缺口 | — |
| D22 | 真算法问题 | 暂不处理 |
| D23 | 繁简的方言差异 | 分词阶段不再改写字符 |

---

## 7. 本文件的维护规则

1. 本文件**只写仍然生效的约束**；已经失效的历史条目一律不收录。
2. 涉及数字/阈值/协议细节时**只写指针**，不复制权威内容。
3. 每次修改必须同步更新顶部登记的快照 HEAD；`file:line` 引用前必须复核。
