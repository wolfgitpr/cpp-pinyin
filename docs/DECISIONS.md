# 决策登记与现行裁定（单一真相源）

> **本文件是决策号与现行裁定的唯一登记处。** 它取代 2026-10-22 退役的
> `REFERENCE_IDS.md`（登记表本体）、`REFACTORING.md` §2 的 D1–D23 台账、
> `DEBT_REDUCTION_PLAN.md` §1 的 D24–D34，以及 `TEST_RECORD.md` 的判分口径与
> `STABILITY_policy_patch.md` 的政策成文规则。退役清单与去向已在 提交历史里（这些过程记录 2026-10-23 一并删除）。
>
> **规矩**：新代码引用记号前，先在本文件 §1 有一行；**新增决策从 D41 续号**，不再从 D1 起开新段。
> 记号是历史坐标，**不重编号**（重编号等于伪造历史）。

---

## 1. 登记表（机器解析）

`tools/check_doc_refs.py` 按本节的表格判定：代码里出现的每个记号必须在这里有一行；`include/` 里只允许
`live` 且权威指向随包分发文档的记号。

| 记号 | 状态 | 它指的那条裁定 | 权威（唯一的） |
|---|---|---|---|
| `R14` | live | 头文件里禁止 `using namespace`（R 系列规则之一） | `docs/STABILITY.md` 的 R 系列表（R14 行） |
| `S7` | live | `Code` 是失败的唯一值域：`Outcome::error().code` 与 `Segment::failure_kind` 用同一域 | `docs/CONTRACT.md` 第 2 节 |
| `S8` | live | 结构化读数按需开启（`Syllable` / `includeReading`），默认 OFF | `docs/CONTRACT.md` 第 3 节 |
| `S9c` | live | 版本化结构体的 padding 陷阱：尾部先由 `reserved_*` 认领，再谈 append | `docs/CONTRACT.md` 第 1 节 + `include/cpp-pinyin2/ext/c_api.h` 的 GROWTH RULE 段 |
| `C-13` | live | `isPolyphonic` 的语义是"以该字**开头**的词组"，不是"该字有多个读音" | `docs/CONTRACT.md` 第 2 节 |
| `S5` | retired | 2.0 结构化音节的生产/渲染分家（`toneUtil/ReadingCodec` 是它的产物） | 本文件 §4 |
| `S6` | retired | 每条读音的结构在**加载期**建好，查询期不再逐条 parse/反演 | 本文件 §4 |
| `S11a` | retired | 查询的"没有结果"必须是 `Code::NotReady`，不能是空结果 | 本文件 §4 |
| `S13` | retired | 最小示例要演示"拼接"，且发生在循环下的那条语句里（示例文件自身即说明） | — |
| `S17` | retired | `check_doc_claims` 的匹配按**文本顺序**而非行内任意顺序 | — |
| `C0` | retired | `U16Str` 长度用例把制表符算作 1 个 code unit 的标记 | — |
| `C-16` | retired | UTF-16 → UTF-8 的转换不得产生"本文件拒绝反解"的结果 | `src/U16Str.cpp` 的转换注释 |
| `C2` | retired | `Syllable` 的身份是 `spelling`，`initial`/`fin` 只是不可逆投影 | `docs/CONTRACT.md` 第 3 节 |
| `D4` | retired | `dialect.json` 的 `min_library` 版本握手，错版给 `Code::DictIncompatible`(5) | 本文件 §4（D4=A′） |
| `D10` | retired | tone sandhi 属于**方言**：没声明 sandhi 的方言由 C 入口直接拒绝 | 本文件 §4 |
| `D19` | retired | `cppp2_query_opts` 的 `struct_size` 必须在 `cppp2_query_opts_init` **之前**写好 | `include/cpp-pinyin2/ext/c_api.h` 的 opts 段 |
| `D31` | retired | 政策文档要能**命名**它陈述的规则，否则门禁只能靠字符串匹配 | `tools/check_root_names.py` 注释 |
| `D35` | retired | 跨语言一致性夹具用**手工挑选**的输入，不追求语料覆盖 | `conformance/cases.json` 的 provenance 段 |
| `G1` | retired | 换代兼容窗口：只承诺相邻两代 | `docs/STABILITY.md` 第 4.5 节 |
| `N3` | retired | 风格身份：所有方言共用一个数字域，各方言稀疏占用 | `docs/CONTRACT.md` 第 5 节 |
| `N4b` | retired | 取消 readiness probe：`cppp2_open` 返回的句柄总是可用 | `docs/CONTRACT.md` 第 7 节 |
| `N29` | retired | `cppp2_style_list` 把方言自己的风格表发布给壳，壳不再抄一份编号 | `docs/CONTRACT.md` 第 7 节 |
| `A2` | retired | 一个输入 span 就是一条音节（C 面 smoke 用例的标记） | — |
| `T-01` | retired | 门禁静默地什么都没测到，比没有门禁更坏 | 本文件 §4 |
| `R13` | live | `format.txt` 的版本号是方言数据的**格式版本**（R 系列规则之一） | `docs/STABILITY.md` 第 4.5 节 |
| `D1` | retired | 安装面=资源发现三层；API 收敛=不拆内部 UTF-16（两套同号，见 §4.1 / §4.2） | 本文件 §4.1 与 §4.2 |
| `D2` | retired | 安装面=不读环境变量、不猜可执行文件目录；API 收敛=UTF-16 移出公开面记 v3 候选 | 本文件 §4.1 与 §4.2 |
| `D3` | retired | 安装面=磁盘文件优先于内嵌副本；API 收敛=一个动词 + 结果上的纯函数 | 本文件 §4.1 与 §4.2 |
| `D4` | retired | 安装面=`min_library` 握手（`DictIncompatible=5`）；API 收敛=不加 pypinyin 兼容层 | 本文件 §4.1 与 §4.2 |
| `D5` | retired | 本版只补 `tone_convert`，不做包装糖 | 本文件 §4.2 |
| `D16` | retired | "判过"包含引擎判错的行；单行自带判据、缺失即 FAIL | 本文件 §4.5 |
| `D25` | retired | 状态码重编（`OK=0`…`INTERNAL=4`），`NOT_READY` 保留 | 本文件 §4.4 |
| `D26` | retired | 两份 facade 的 `Impl` 是 pimpl，不是冗余层 | 本文件 §4.4 |
| `D28` | retired | 删方言裁剪开关（与 `DialectSetGate` 互斥） | 本文件 §4.4 |
| `D30` | retired | 删零引用探针脚本前必须逐个复核引用 | 本文件 §4.4 |

## 2. 前缀与分段规则

`TOKEN_RX` 只认 `S/D/R/N/C/T/G/K/A/P`。**其余前缀不立册是决定而非遗漏**：`F/E/I/L/M/Q` 是分轮审计的条目
标签，只在那一轮台账内部有意义，跨轮不保证同义；引用时必须带轮次或文档名（"round-3 的 H20"）。若某个
`F/E/I/L/M/Q` 要跨文档长期引用，做法是**换一个已立册的前缀并在此加行**，不是把前缀塞进 `TOKEN_RX`。

### D 号曾有四段互不相干的台账（同号不同义）

退役前 `D<n>` 被四段台账分别使用，彼此没有语义关联；**本节的表就是这种歧义的唯一说明**。四段台账的正文都已不在仓库里，
其中 D1–D12 与 D35–D40 的现行裁决见 §2.1。

| D 号范围 | 原定义处（2026-10-22 退役） | 这一段是什么 |
|---|---|---|
| **D1–D23** | `REFACTORING.md` §2 | v2 结构重构的决策 |
| **D1–D12** | 本文件 §2.1（2026-10-23 迁入） | 插件 / C ABI / 打包的决策 |
| **D24–D34** | `DEBT_REDUCTION_PLAN.md` §1+ | 减债方案的第二段（**D25/D26/D29/D30 只在那里曾有过**） |
| **D35–D40** | 本文件 §2.1（2026-10-23 迁入） | 绑定与打包实施阶段的决策 |

**引用纪律**：① 引用时写"本文件 §2.1 的 D5"或"本文件 §4.2 的 D4"，**不要只写 `D2`**；
② 新增一律 D41 续号；③ 引用 D 号**必须带系列名与一句话含义**（同一个 `D4` 在安装面/API 收敛/测试集三处不同义，见 §4）。）

### 2.1 D1–D12、D35–D40：插件、C ABI 与打包的现行裁决

原台账在 `docs/DECISIONS.md` §2.1（已删除）里以"推荐项 vs 备选"的形式记录决策过程。这里只留
**裁决本身与它在树里的落点**——过程不必随仓库走。

| ID | 现行裁决 | 落点 |
|---|---|---|
| **D1** | 插件接口 = 最小三层：`Dialect` 纯数据描述（id / dictSubDir / ToneConverter 按值 / 样式能力 / 可选后处理钩子） + 注册表 + 查询走实例 | `include/cpp-pinyin2/core/`、`src/` |
| **D2** | 实例级并发**零锁**：构造与加载完成后才共享，只读查询可并发；加载期不得被其他线程观察 | `docs/CONTRACT.md`、`docs/STABILITY.md` |
| **D3** | 新语种资料归词典仓（`pinyin-makedict`），本仓只留接口与边界的一篇说明 | `docs/Custom Chinese Dialect.md` |
| **D4** | C ABI = 平铺 `extern "C"`：opaque handle + UTF-8 + 返回码，**C++ 异常一律挡在边界内** | `include/cpp-pinyin2/ext/c_api.h`、`src/c_api_*.cpp` |
| **D5** | Python 壳 = C ABI + `ctypes` ⇒ `py3-none-<platform>` wheel（不需要 cibuildwheel） | `python/` |
| **D6** | 实施分层 P1 插件接口 → P2 C ABI → P3 壳与打包；三者均已落地 | 本仓 |
| **D7** | 绑定与库**同仓**，语言目录在仓根：`python/`、`csharp/` | 仓根 |
| **D8** | 版本号单一源 `PROJECT_VERSION`，生成各包版本，两个生态同号 | `CMakeLists.txt`、`version.props`、`_version.py` |
| **D9** | 跨语言质量门禁 = 共享夹具：C++ 侧生成 `conformance/cases.json`，Python 与 C# 读同一份、断言同口径 | `conformance/` |
| **D10** | C ABI 加固：`-fvisibility=hidden` + version script + 导出符号快照门禁 + `c_api.h` 的书面承诺 | `tests/abi/`、`include/cpp-pinyin2/ext/c_api.h` |
| **D11** | 平台范围：四平台脚本齐备；Windows 与 Linux（开发板）端到端实测，macOS 只出脚本、未实测 | `docs/RELEASE_CHECKLIST.md` |
| **D12** | 包身份以代令牌区分：Python 分发名 `cpp-pinyin2`、导入名 `cpp_pinyin2`；C# 包 id `CppPinyin` | `python/setup.py`、`csharp/CppPinyin/CppPinyin.csproj` |
| **D35** | 夹具口径：`tests/cpp/test_c_api.cpp` 的手写断言作**独立 oracle**；夹具输入人工挑选、期望值由引擎生成 | `tests/cpp/test_c_api.cpp`、`conformance/README.md` |
| **D36** | C# 目标框架 `netstandard2.0;net7.0;net10.0`（2.0 保住 .NET Framework 与 32 位消费方，UTF-8 手工兜底） | `csharp/CppPinyin/CppPinyin.csproj` |
| **D37** | `net7.0` 已 EOL：SDK 10 对它的目标报 `NETSDK1138`（告警，不影响构建），仍保留该 TFM | 同上 |
| **D38** | `ci.yml` 用 `workflow_call`，不自带触发（避免一次推送把同一套作业跑两遍） | `.github/workflows/ci.yml` |
| **D39** | 调度层 `test_runner.yml` 的聚合 job 必须覆盖 C++ 套件，否则"CI 绿"不覆盖库本体 | `.github/workflows/test_runner.yml` |
| **D40** | 原生库随 nupkg 走 `runtimes/<rid>/native/`；RID 与源目录由 `CppPinyinRid` / `CppPinyinNativeDir` 注入 | `csharp/CppPinyin/CppPinyin.csproj` |


---

## 3. 现行裁定（live 正文）

**这一段只写"这条裁定住在哪"，不复述它的正文。** 复述就是第二份权威，而 `CONTRACT.md` 与 `STABILITY.md`
是随包分发的那一份；两份一起改才不漂，只改一处就漂——本仓为此删过一整套台账。

| 记号 | 一句话 | 正文在哪 |
|---|---|---|
| `S7` | 失败只有一个值域（`Outcome::error().code` 与 `Segment::failure_kind` 同域） | `docs/CONTRACT.md` 第 2 节 |
| `S8` | 结构化读数默认关，`includeReading` 显式开启 | `docs/CONTRACT.md` 第 3 节 |
| `S9c` | 版本化结构体 append 前，尾部 padding 必须已由 `reserved_*` 认领 | `docs/CONTRACT.md` 第 1 节 + `include/cpp-pinyin2/ext/c_api.h` 的 GROWTH RULE 段 |
| `C-13` | `isPolyphonic` 问的是"以该字开头有词组吗"，不是"该字有多个读音" | `docs/CONTRACT.md` 第 2 节 |
| `N3` | 风格共用一个数字域，方言稀疏占用；未声明的风格原样返回 | `docs/CONTRACT.md` 第 5 节 |
| `N4b` | 句柄开出来就可用，没有 readiness probe | `docs/CONTRACT.md` 第 7 节 |
| `N29` | 风格表由方言发布，壳不再抄一份编号 | `docs/CONTRACT.md` 第 7 节 |
| `R13` | `format.txt` 的版本号是方言数据的格式版本 | `docs/STABILITY.md` 第 4.5 节 |

### D41｜两种编码各有各的地盘，一个翻译单元只说一种（2026-10-22）

**裁定**：UTF-16 是**内部**的方言（词典按码元索引，一个汉字一个下标）；UTF-8 是**每一个边界**的方言——
C ABI、启动时装载的词典文件、以及调用者读到的 `Result` / `Syllable` 字段。一个翻译单元只应说其中**一种**；
必须同时说两种时，转换属于那个单元的**边缘**。库自己的短名 `toUtf8()` / `toU16()` 只在 `src/Utf8Conv.h`
（不安装、不导出、不进任何冻结面），公共头保留长名。

**为什么不是"去掉 UTF-16"**：换掉内部表示不是换个编码，是换**索引方式**——`std::array<char16_t, 65536>`
与 4×16 位短语键打包直接依赖"一个汉字一个码元"。换成 UTF-8 后每个查表点都要先解码，核心反而更长更难读；
D1 已经量过这笔账（收益上界 1.0422 ≈ 4.2%，且那是"转换免费"的理想上界）。

**为什么公共面保持 UTF-8**：把 `Result`/`Syllable` 改成 `std::u16string` 确实能一次消掉 17 处被类型逼出来的
转换，代价是**每个** C++ 使用者打印前都要自己转码，并且破 76 条冻结签名——用库内整洁换使用者日常麻烦，方向反了。

**短名的边界（这条是量出来的，不是感觉）**：`toU16` 只接受 `const std::string &`，**不提供 `string_view` 重载**。
21 个反向调用点里 17 个传的是已存在的 `std::string`（包短名不藏任何东西），4 个传 `string_view` 并显式写
`std::string(view)`——那 4 个真的在分配，短名会让它跟另外 17 个长得一样，把分配藏起来。**短名只在它背后免费时才有价值。**

**现状（2026-10-22 实测，数字为门禁可核）**：`src/` 里 **39 个转换点**（`toUtf8(` 18 + `toU16(` 21）全部用短名，
长名只出现在 `src/U16Str.cpp` 的定义处。**混用两种类型的文件仍是 8 个**——短名不改变类型，只改变读法；把混用
文件压下去要动公共类型，那就是上面否掉的那条路。这 8 个文件是：`EngineImpl.cpp`、`EngineImpl.h`、`U16Str.cpp`、
`c_api_query.cpp`、`dict/DictUtil.cpp`、`dict/DictUtil.h`、`dict/PhraseLayers.cpp`、`dict/PhraseLayers.h`
（实测口径：同一文件里 `std::string` 与 `u16string` 各出现至少一次）。**出现第 9 个时，先问它是不是把转换
从边缘挪进了内部。**

---

### D42｜manylinux 政策线是 **wheel 用标签许下的承诺**，不是所有 Linux wheel 的义务（2026-10-22）

**裁定**：`python/tools/check_wheel.py` 的 glibc 上限（`MAX_GLIBC = (2, 28)`）**只对平台标签里声称
`manylinux*` 的 wheel 生效**。不声称的（例如托管 runner 上直接打出来的 `linux_x86_64`）把实测到的最高 glibc
版本记为 note，不判红。

**为什么**：CI 首跑时这条规则把宿主机构建的 wheel 判红（`references glibc 2.38, above the manylinux_2_28
policy line 2.28`）——那个 wheel 的标签就是 `linux_x86_64`，它**没有承诺** 2.28，宿主机的 glibc 是多少它就
该是多少。真正需要 2.28 承诺的是容器里构建的那颗（`docs/RELEASE_CHECKLIST.md` §6 里那条 manylinux 腿），而它
的标签会写 `manylinux_2_28_*`，规则在它身上照旧生效。

**控制项**：`--self-test` 两侧都有——声称 manylinux 时必须红，同一份字节不声称时必须不红。


### D43｜许可证文本有两种合法布局，校验器两者都收（2026-10-22）

**裁定**：wheel 里的 `LICENSE` 与 `THIRD_PARTY_NOTICES.md` 可以落在 `dist-info/` 下，也可以落在 PEP 639 的
`dist-info/licenses/` 下；两种都算合规，**都没有才判红**。

**为什么**：setuptools 77 起按 PEP 639 把 `license_files` 写到 `licenses/` 子目录，而校验器只认旧位置，
于是三个 Python job 拒收了一颗完全正确的 wheel。**这是包装标准的版本差，不是本项目的事实**——把其中一种
写死，等于让校验器随 setuptools 版本变红。

**控制项**：`--self-test` 同时有"PEP 639 布局必须绿"与"缺第三方声明必须红"两条；后一条此前是**假控制**
（用了 6 段文件名，`check()` 在解析处就返回了），本轮一并修成真控制。

### D44｜生成文件必须由 configure 或 job 显式产出，不能让下游项目替它猜（2026-10-23）

**裁定**：仓库里每个**生成**的文件（`csharp/CppPinyin/version.props`、`BuildInfo.g.cs`、`python/cpp_pinyin2/_version.py` 等）都要有一个**明确的产出点**——CMake configure 期，或该 job 里一条写清名字的命令。消费它的项目只允许**报错并给出可执行的修复命令**，不得自己生成、不得静默降级。

**为什么**：C# 门禁在两个 job 上连红两轮，原因不是代码，而是"文件没人生产"。linux 腿先缺 `version.props`，补上后又缺 `BuildInfo.g.cs`（`CppPinyin.csproj(145,5)` 报的就是它）。两次都是**同一条规则的另一半**：谁生产、谁消费，必须在 CI 里对齐。

**控制项**：`tests/CMakeLists.txt` 在 configure 期调 `emit_build_info.py --version-props-only`（版本不需要库）；linux 腿在 stage 之后调完整的 `emit_build_info.py --native/--commit`。`CppPinyin.csproj` 的 `RequireGeneratedBuildInfo` 目标保留——它的报错文案就是修复命令。

### D45｜平台相关的排除必须带证据与未解问题，且排除面尽量小（2026-10-23）

**裁定**：某个用例在某个平台上跑不了时，允许排除，但必须同时写清三件事：**观测到的证据**（日期、次数、试过什么）、**未解问题是什么**、**为什么排除面这么小**（同类自检仍在该平台运行）。排除不是"通过"，文档里也不许把它记成通过。

**为什么**：windows 腿上 `InstallConsumerSmoke` 运行外置消费端可执行文件时**不返回**——三次运行、空 stdin、测试自带的 300 秒预算都试过，同用例在 Linux/macOS 上约 1 秒通过。它的两个自检（证明门禁能红的那两条）仍在该腿运行，所以排除的是**一个用例**，不是一条规则。

**反面参考**：msys2 腿的 `ConformanceProvenanceGateSelfTest` 也是按这条裁定的方式排除的——它建临时 git 仓库断言"不可达提交必须被拒"，在 MSYS2 git 上 6 例里 4 例失败，而原生 `windows` 腿 6 例全绿；该腿同时跑了**两个**构建目录，排除必须同时落在两处，否则只是排除了半个 job。


## 4. 退役记号里仍然重要的裁定（retired，但代码里还在引用）

**这一段按"系列"分组，因为 `D<n>` 在不同系列里不同义**——代码里同时引用着**三个不同的 `D4`**（安装面 =
`min_library`、API 收敛 = 不加 pypinyin 兼容层、测试集 = 候选来源随字形）。所以引用纪律再加一条：
**引用 D 号必须带系列名与一句话含义**，例如 `D4（安装面，min_library 握手）`。

### 4.1 安装 / 嵌入面系列（原 `installed-consumer-surface-2026-10-18.md`）

| 记号 | 裁定与理由 |
|---|---|
| **D1** | 资源发现三层：显式参数 > `CPP_PINYIN2_DICT_DIR`（**只由包装层读**）> 包内副本 > 报错并列出试过的位置。库本体保持"无隐式路径"。 |
| **D2** | 包装层的默认解析**只读环境变量与包内副本**，绝不猜"相对可执行文件"位置——静态链接下猜错比报错更难排查；候选目录从**库自身模块位置**推导，不用编译期安装路径常量（`--prefix` 或整目录搬家后那条常量就是错的）。 |
| **D3** | 一张表只有**一个打开点**，磁盘文件与内嵌副本共用同一个面：**磁盘存在时永远优先**，内嵌副本不得遮蔽真实文件（否则过期的资源目录会被静默忽略）。 |
| **D4 = A′** | `dialect.json` 只加 **`min_library`** 一个维度，不重复表格式版本（`format.txt` 那件事由 R13 管）；错版报**追加码** `Code::DictIncompatible = 5`。**不加**第二个版本字段：两处写同一个事实就会漂移。`min_library` 受两条约束——不得超过包版本、只在数据**真需要**新库时才抬。 |

### 4.2 API 收敛系列（原 `api-and-simplification-plan-2026-10-21.md`）

| 记号 | 裁定与理由 |
|---|---|
| **D1** | **不拆内部 UTF-16**（承重墙；UTF-8 化的理论上界实测 1.0422 ≈ 4.2%，可读性论据同向支持不动）。 |
| **D2** | 把 UTF-16 移出公开面记为 **v3 候选**（破 15 头 / 39 名 / 76 签名，属换代事项），v2 不动。 |
| **D3** | 两壳收敛为"**一个动词 + 结果上的纯函数**"，但 `readings()` 例外保留——实测它问的是另一个问题（`你好` 只给 `['nǐ']`，query 展平给 `['nǐ','hǎo','hào','hāo']`）。 |
| **D4** | **不加** pypinyin 兼容层：四个本地消费者没有一个用本仓 Python 包，加了等于并存第二套 API。 |
| **D5** | 本版只补 `tone_convert`（进 C ABI + 两个壳），因为下游在库外手搓风格互转；`slug`/`lazy_pinyin` 之类"包装糖"不做。 |

### 4.3 C 面 / ABI 系列

| 记号 | 裁定与理由 |
|---|---|
| **D10** | tone sandhi 属于**方言**：没声明 sandhi 的方言，C 入口直接拒绝，不套用另一方言的规则（粤拼也是"字母+数字"，数据层看不出差别，所以这条必须由方言声明决定）。 |
| **D19** | `cppp2_query_opts.struct_size` 必须在 `cppp2_query_opts_init` **之前**写好：init 只填它认得的字段，结构体的"我多大"由调用者先声明。 |

### 4.4 减债系列（原 `DEBT_REDUCTION_PLAN.md`）

| 记号 | 裁定与理由 |
|---|---|
| **D25** | 状态码重编：`OK=0 / INVALID_ARG=1 / NOT_READY=2 / LOAD=3 / INTERNAL=4`。`NOT_READY` 保留，因为 `open` 的未知方言分支是它的生产者。 |
| **D26** | 两份 facade 的 `Impl` 是 **pimpl**（把 Engine 挡在公共头外），**不是**冗余层：代码不动；去重必须引入模板或基类，代价大于收益。 |
| **D28** | **删掉方言裁剪开关**（`CPP_PINYIN_DIALECT_*`）：它与 `DialectSetGate`（注册表冻结为 `{mandarin, cantonese}`）互斥，且没有任何测试或 CI 设置过它；唯一一个 OFF 组合连自己的测试都构建不出来。 |
| **D30** | 一次性探针脚本：**删零引用者**，但删前必须**逐个复核**引用——"25 个零引用"的审计结论曾被抽样推翻。 |

### 4.5 测试集治理（原 `REFACTORING.md` / `TEST_RECORD.md`）

| 记号 | 裁定与理由 |
|---|---|
| **D16** | **"判过"的定义包含引擎判错的行**：一行只要被复核过（哪怕最后记成 `ENGINE_BUG`），就是"已判"；把它排除出去会让分母随缺陷数缩小，准确率反而虚高。配套纪律：单行自带判据、六态标记、缺失即 FAIL、先修数据再切判据、四步可回滚。 |
| **T-0x / K9.x** | **已退役且无后继**：`T-01`（门禁静默空过）、`T-03/T-04/T-05`（旧测试集的标注质量缺陷）、`K9.3`（候选来源随字形）这些标签只在已删除的测试台账里有定义。写注释时**不要再引用号码**，直接写那句话本身。 |


---

## 5. 核心算法冻结区 K1–K8

`src/` 的查询核心是**已验证基线**：重构必须逐条保持等价，任何违反**先当 bug** 而不是"新设计"。

| 记号 | 规则 |
|---|---|
| K1–K2 | 词典装载与短语索引的既有语义（含"窗口命中后覆盖该区间既有结论"，这是 v1 语义，不得"顺手修正"） |
| K3 | 单字读音取 `word.txt` 顺序第 0 个为默认（**无频率/权重**）；候选去重保首次出现顺序 |
| K4 | 多音字判定依据是 `phrases_map.txt` 是否含该字，**不是** `word.txt` 有几个读音 |
| K5 | 窗口命中优先于既有结论；覆盖率与顺序是行为的一部分 |
| K6–K8 | 分段/编码/渲染的等价性由 `DumpEquivalence` 的冻结摘要守住 |

## 6. 验收线

| 编号 | 判据 |
|---|---|
| **AC-1** | 最小示例 `examples/minimal_usage.cpp` 的"构造 + 查询 + 遍历"必须在 **6 条有效行**内，由测试数行数，不由人眼看 |
| **AC-2** | **口径已重述（2026-10-04 用户裁定）**：原"公共头 ≤300 行 / ≤3 include"**降为史实、不再作判据**；现行判据 = **三个冻结面（15 公开头 / 23 C ABI / 18 插件面声明）的任何增减必须与清单文件同提交** |
| **AC-3** | 分层是**机器门禁**而非散文：`tools/check_layers.py` 是规则执行者，散文不得复制 LAYERS 表。现行读数 **35 文件 0 违例**（2026-10-22 加入 `src/Utf8Conv.h` 后 +1；这条数字由门禁每次构建比对，改文件数不改本行就会红） |
| **AC-4** | 词典层可脱离 tone/engine **独立编译与测试**（`tests/CMakeLists.txt` 用原文引用本条） |
| **AC-5** | 只约束**新模块**（无文件 >300 行，`Engine` ≤200 行）；既有文件不在此限 ⇒ `EngineImpl` / `ManTone` **不拆** |
| **AC-9** | 速度验收：查询/加载吞吐不低于基线 min，**±5% 内视为持平**，超出即回滚该次改动（不做"以后再优化回来"） |

## 7. 性能与测量的纪律

| 记号 | 规则 |
|---|---|
| **D7** | 性能档位：单项收益 **<10% 一律丢弃**；需要改数据结构/加缓存/加索引的**一律不做**；≥20% 吞吐即达标停手 |
| **O2** | 收益门槛同 D7 的量化版：须用 **A/B 交错实测**该点独占耗时，不接受"调用次数多"式推断 |
| **O3** | 复杂度预算：每项优化**增行 ≤15 行**且不新增类型/层次，超预算即降级为"只写注释记录、不做" |
| **O4** | 保守优先：拿不到**干净读数**的优化不做 |
| **测量协议** | 每用例 **7 轮 × ≥50 采样**，报 min/p50/p90，**主判据用 min**，A/B 交错执行以摊掉漂移 |

**为什么这么严**（实测）：一次"1.1072 的胜利"来自放宽了尝试次数的判据，真实值 1.0491；并发构建与门禁同跑曾伪造出
1.1225 的加速，独占重测只有 1.0045。

## 8. 明确不做（含理由）

| 不做 | 理由 |
|---|---|
| 动态库插件体系（`dlopen` / 清单 JSON / 接口等级寻址） | 只有两个进程内方言；静态注册 + 只读工厂表已覆盖"方言可挂载" |
| `slug` / `lazy_pinyin` 等包装糖 | 下游要的形态各不相同，放进库等于替使用者选一种 |
| pypinyin 兼容层 | 四个本地消费者没有一个用本仓 Python 包；加了等于并存第二套 API |
| `errors='replace'` 之类的容错开关 | 把"读不出来"变成"读成别的"，比报错更难排查 |
| 第三方转换器注册表、词频排序 | 前者要 ABI 承诺一个我们不控制的扩展点；后者要一份我们没有的权威词频 |
| 重编号历史台账的 D/S/R 号 | 重编号等于伪造历史；只建登记表 + 门禁 |
| vcpkg 端口填**假** `SHA512` | 假值会让 vcpkg 在**任何**下载上失败；留 `0` 只在缺 tarball 时才报错 |
| 把发布动作自动化 | 按既定决策**永不自动化**，由人按 `docs/RELEASE_RUNBOOK.md` 执行 |
