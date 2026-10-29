# 长期稳定性评估（sanitizer / 线程 / 多实例 / ABI）

> **这是什么**：给"下一步要动这四个面的人"看的事实清单 —— 现在能不能验、靠什么装置验、改什么会破。
> **这不是什么**：不是方案、不是待办列表、不是路线图。本文不提议任何改动。
>
> 本文只写**当前**的判断与装置；起草过程的记录已不在仓库里，正文从 §0 起。下文提到**旧台账**时指的就是那批
> 过程记录：它们住在 `tests/data/testdata/`（31 个文件，2026-10-22 删除），`git log --diff-filter=D --
> tests/data/testdata/` 仍能从删除它们的那个提交里取回。
>
> **标记约定**：`[事实]` = 成文时亲自读到的原文/代码；`[推断]` = 由事实推出、成文时未实测；
> `[未证实]` = 只有他人（文档/注释/CI 文件自述）记录，成文时无法验证。**未证实项一律不当既定事实用。**

---

## 0. 总表：四对象 × 三问

| 对象 | ① 现在能不能验？ | ② 靠什么验？ | ③ 最脆弱点（改什么会破） |
|---|---|---|---|
| **sanitizer**（ASan/UBSan） | **本机能**（clang/MSVC 目标一条窄路，见 §1.1：**examples OFF** 的全量 36 条〔**2026-10-06 快照**；当前注册用例数见本文件 §0 末节〕，`-LE slow` **27/28**、`-L slow` **4/4**、ASan/UBSan 报告 **0** 条）；**板上不能**；**CI 腿仍未观察运行 `[未证实]`** | `option(CPP_PINYIN2_SANITIZE)`（`CMakeLists.txt::option(CPP_PINYIN2_SANITIZE`）+ 能力自检（`CMakeLists.txt::check_cxx_source_compiles`）+ 聚合目标 `cpp-pinyin_sanitizers`（`CMakeLists.txt::add_library(cpp-pinyin2_sanitizers`）；执行者只有 `ci.yml` 的 `linux-sanitizers` job（`.github/workflows/ci.yml::linux-sanitizers`） | ① 新增 C++ 目标忘了链 `cpp-pinyin_dev_flags`（`CMakeLists.txt::cpp-pinyin2_dev_flags`）⇒ 静默失去插桩；② 触发链断了没人知道（`.github/workflows/ci.yml` 的触发段）；③ 把 `-fno-sanitize-recover=all` 去掉 ⇒ UBSan 只打印不判红 |
| **线程**（同一/不同 `Engine` 并发） | **能**（本机共享构建），**但只覆盖"只读并发"这一半**；写侧重叠**按契约就是数据竞争，无法验也不打算验** | CTest **`ThreadSafety`**（`tests/CMakeLists.txt::add_test(NAME ThreadSafety`，标签 `thread`，实测选中 5 个 case）+ 主套件 `CppPinyinTests`（`tests/CMakeLists.txt::add_test(NAME CppPinyinTests`）里的同批 case，**按符号引用**（本行原先写的行号 2026-10-17 实测已漂）：`tests/cpp/test_bug_regression.cpp` 的 `TEST_CASE("concurrent read-only calls on a shared engine agree with single-threaded results", "[api][thread_safety]")`、`tests/cpp/test_comprehensive.cpp` 的 `TEST_CASE_METHOD(Fixture, "Thread safety smoke test", "[thread][concurrent]")`、`tests/cpp/test_c_api.cpp` 的 `TEST_CASE("C ABI: a shared handle survives concurrent readers", …)`、`tests/cpp/test_engine_dialect.cpp` 的 `TEST_CASE("no shared state between instances, across threads", "[engine][thread]")`。**2026-10-04 前**这些 case 只能靠 Catch2 标签单跑（**无条目、无标签**）——该缺口当日已闭合 | ① 只读路径里开始写共享状态（缓存/惰性索引）；② 契约文本与实现漂移（契约在三处：`Engine.h` 的类注释 **THREADS** 段、`Dialect.h` 的对应说明、`c_api.h` 的线程注记；`Mandarin.h` 已随门面类删除，不再是一处）；③ 无 TSan，**竞争本身不可判** |
| **多实例**（异方言/异词典/异配置） | **能**，且有正/负对照 | 同上 `CppPinyinTests` 里的 `tests/cpp/test_engine_dialect.cpp` 的 `TEST_CASE("no shared state between instances, across threads", "[engine][thread]")`（线程×多实例）、`TEST_CASE("instances do not share loaded user dictionaries", "[engine][isolation]")`（用户词典不串味）——**按符号引用**（本行原先写的行号 `:412`/`:482` 2026-10-17 实测落在别的用例里）；注册表对 `HostOwnedRegistry` / `HostOwnedRegistryBare`（`tests/CMakeLists.txt::HostOwnedRegistry`）；方言集合门禁 `DialectSetGate`（`tests/CMakeLists.txt::add_test(NAME DialectSetGate`）；字典根是构造参数（`include/cpp-pinyin2/core/Engine.h::dictDir`） | ① 给任何东西加"全局默认词典路径/全局配置"；② 注册表变成**进程级**可变状态（2026-10-05 起不再有单例，见 §3.1）；③ 让"调用方没注册过的 id"也能 resolve —— 由 `HostOwnedRegistryBare` 把守 |
| **ABI 兼容** | **部分**：三个冻结面的**源级/符号级**能验；**二进制兼容没有任何可比较对象**（本线未发布） | CTest `ExportGate`（`tests/CMakeLists.txt::add_test(NAME ExportGate`，**仅共享构建**）、`BindingGates`（`tests/CMakeLists.txt::add_test(NAME BindingGates`，需 Python3）、`HeaderSelfCheck`（`tests/CMakeLists.txt::add_test(NAME HeaderSelfCheck`）、`CApiSmoke`（`tests/CMakeLists.txt::add_test(NAME CApiSmoke`）；运行时守卫 `struct_size` + `cppp2_version()`；权威契约 `docs/DECISIONS.md` §2.1:926-953` | ① 共享构建里增删 `cppp_*` 或让内部符号泄漏；② **静态构建**下删 C ABI 定义（只剩链期把守）；③ 改三个冻结清单之一而不同提交；④ 改 `-fvisibility=hidden` / `__declspec` 方案 |

**一句话总结**：四个面里只有"线程"和"多实例"在本机是**真能验**的（且都藏在同一个 CTest 条目里）；
ABI 是"部分能验"（差二进制兼容这一维，原因是没有消费者而不是没有装置）；**sanitizer 的装置齐全，且 2026-10-06
第一次在本机真跑出了观测**（§1.1），但它**还不是门禁**：板与 CI 两侧不覆盖、慢档/Debug 档与泄漏检测各有未拆的
边界 ⇒ 别把这一条读成"sanitizer 已全量覆盖"。

### 0.1 CI 与门禁的接线现状（四栏；A29，2026-10-27 补第四栏）

**`test_runner.yml` 这条链已经全绿**（2026-10-23，`v2` 提交 `f5c44bd`，运行 `39877724768`）：**19 个 job 全部成功** —— linux、linux-sanitizers、macos、windows、mingw、mingw32、C# 三条腿（ubuntu / macos / windows）、Python 三条腿（ubuntu / macos / windows）、`python_versions` 的 3.9–3.13 五档、以及 manylinux aarch64 的 wheel 容器。此前那一批失败是怎么一类一类拆掉的，记在
`docs/RELEASE_CHECKLIST.md` §6。

**仍然未跑的是 `release_prep.yml`**（只在 `workflow_dispatch` 下存在，标签、镜像拉取、runner 的 Docker 都没验过）——读下面的接线行时，请按这个分工理解：

**Windows 的 `InstallConsumerSmoke`：排除项已撤，修复仍无 runner 证据（2026-10-27 更新）。** 历史观测：该用例在
Windows runner 上执行外置消费端可执行文件时**不返回**（三次运行、空 stdin、测试自带的 300 秒预算都试过），
同一用例在 Linux/macOS 上约 1 秒通过。**根因已于 2026-10-23 定位并修**（runner 上跑的是 Debug 消费者、
链接 Release DLL，CRT 分裂；修法是候选"首个匹配胜出"＋失败路径三段诊断）。

**2026-10-27 用户裁决撤除排除**：`ci.yml` 里 Windows 腿的 `-E '^InstallConsumerSmoke$'` 与那个单独跑它的
`continue-on-error` 探针**一起删除**（`c3a728c`），该用例回到 Windows 腿的正常 CTest 集合里，每测预算
1200 → 600 秒。也就是说：**修复是否真的生效，下一次 Windows CI 运行就是第一份证据**——在此之前，
"曾在 runner 上挂死"是历史，"现在还会不会挂死"是未知。判词与撤除前后的证据链记在 `docs/DECISIONS.md` 的 D45。

| 接法 | 是谁 | 证据 |
|---|---|---|
| **在 CTest 内**（⇒ CI 的 ctest 步骤会跑到） | `check_layers.py` | `tests/CMakeLists.txt::add_test(NAME LayerIncludes` |
| | `check_bindings.py` 本体与自检 | `tests/CMakeLists.txt::add_test(NAME BindingGates` |
| | `check_doc_claims.py --self-test`（**只是自检**） | `tests/CMakeLists.txt::add_test(NAME DocClaimsSelfTest` |
| **独立 CI workflow**（2026-10-23 起已有运行记录，随上面那次全绿一起） | pytest | `.github/workflows/test_python.yml::pytest` |
| | `dotnet test` net7.0 + net10.0 | `.github/workflows/test_csharp.yml::dotnet` |
| | wheel 对源树／对绑定契约 | `.github/workflows/test_python.yml::wheel` |
| | 打包与版本链门禁 | `.github/workflows/test_csharp.yml::nupkg` |
| | manylinux aarch64 那条 | `.github/workflows/test_python.yml::manylinux` |
| **CI 的 `linux` 腿，经 `run_all_gates --with-build`**（2026-10-22 起） | `check_gate_inventory.py`、`check_doc_claims.py --ctest-inventory`（**真文档那一趟**；它顺带跑 `check_wheel --self-test` 取数） | `.github/workflows/ci.yml::Full gate suite`：先 configure `.tmp/mk-default`（examples OFF）→ `ctest --show-only=json-v1` 出 inventory → `python3 tools/run_all_gates.py --with-build`；清单在 `tools/run_all_gates.py::GATES` |
| **只在本机**（不入库，且**不是门禁**） | 板上复验夹具与人为读数 | 夹具随本机保留（2026-10-23 起），口径见 `docs/PERFORMANCE.md`；没有任何 workflow 会执行它 |

两个值得单独点出的结论：

1. **文档数字门禁 2026-10-22 起在 CI 里跑到了（2026-10-27 更正）。** 此前这里写的是"跑不到"，依据是
   "CI 经 CTest 只跑到 `--self-test`"＋"`.github/` 下对该脚本的引用是 0 处（实测）"。后半句只在
   "文件里没写出脚本名"这一层成立——`ci.yml` 里对 `check_doc_claims` 的 4 处提及**都是注释**；而在
   "有没有跑到"这一层是错的：同一个 job 的 "Full gate suite" 步骤跑 `run_all_gates.py --with-build`，
   它的 `GATES` 表里就有这一条，inventory 由该步骤自己先生成。
2. **这一类失效有过前科**：`tests/CMakeLists.txt:487` 自述，插件面清单原属 `check_bindings.py`，
   而「本仓没有任何东西调用它」。检查项存在、却没人调用，是这里反复出现的形状；
   `check_bindings.py` 现在自己校验这一点（文档串编号 + 每个 `check_*` 都有调用点）。

### 0.2 跨仓（词典仓）的门禁接线现状

出厂词典由**另一个仓库**（`pinyin-makedict`）生成，所以它的一致性由那边的工具守。成文时只读实测，
读数如下，同样只记接线、不记运行史：

| 事项 | 实测 |
|---|---|
| 本仓对出厂 `.cpdict` **内容**的门禁 | **没有**。`cpdict` 在本仓只出现在 **5 个文件**里，全是 `docs/`（`tests/`、`tools/` 各 0 处）—— 2026-10-07 复核：写这句时是 4 个，现已 5 个；口径未变，仍**只在 docs/**。正确性由 ctest 让引擎真读那份 `.cpdict` 间接兜住，**同代性**没有任何断言 |
| 词典仓 `tools/check_drift.ps1` | 字节权威：跑生成器后与 `<cpp-pinyin>/data/dialects` 逐字节比对。方言/表**不是写死的**，由两棵树的目录 + `dialect.json` 的 `required_tables` 派生；两侧都没有方言目录时 `exit 4` **拒绝报绿** |
| 词典仓 `tools/check_binary.py` | 有意做成**第二份实现**：从文本表按引擎规则重算，再与二进制实测内容比对（6 项/方言），与写入器不共享代码 —— 一个错误假设必须被犯两次才会漏掉 |
| 两者的**运行方式** | **人手跑**：词典仓 `README.md:24`／`:29` 与 `TOOLING.md:21` 给的是命令；该仓**未见 `.github/workflows/`** ⇒ 未自动化（与 §0.1 第一栏同类风险） |

成文时的读数（只读、`-SkipGenerate`）：`RESULT: generated output matches the repo exactly (0 drift)`，退出码 0，
2 方言 × 4 张文本表全部 `IDENTICAL (byte-exact)`、行数逐一相同。

两个历史注记（都出自词典仓自己的文档，已复核）：`TOOLING.md:246` 记着这份门禁曾经**写死 2 方言 × 4 表**，
于是多出第三方言时仍打 `0 drift` —— **忽略一整个方言还报全绿**，已改为从目录派生；`TOOLING.md:198-200` 记着
拼路径曾指向已不存在的 `res/dict`（必然 `exit 2`），已改为 `data/dialects`。


---

## 0.3 门禁自检的用例数（2026-10-10 实测，本节是单一权威）

数字由 `tools/check_doc_claims.py` 的 `measure_selftests()` **实跑各工具的 `--self-test` 并读它们
自己的输出**得到（不是从源码数的）。下面每行都写成 `tools/check_doc_claims.py` 能认领的措辞，
写错会被它抓住；本机夹具的 README 只写指针，不复述数字。

* `check_bindings`（`tools/check_bindings.py`）：有 `--self-test`，自检 **77 条**。
* `check_layers`（`tools/check_layers.py`）：有 `--self-test`，自检 **21 条**。
* `check_wheel`（`python/tools/check_wheel.py`）：有 `--self-test`，自检 **19 条**。
* `check_links`（`tools/check_links.py`）：有 `--self-test`，自检 **9 条**。
* `check_public_surface`（`tools/check_public_surface.py`）：有 `--self-test`，自检 **16 条**。
* `merge_nupkgs`（`csharp/tools/merge_nupkgs.py`）：有 `--self-test`，自检 **10 条**。
* `check_doc_claims`（`tools/check_doc_claims.py`）：有 `--self-test`，自检 **5 条**。
* `audit_doc_anchors`：有 `--self-test`，自检 **10 条**。
* `check_gate_inventory`（`tools/check_gate_inventory.py`）：有 `--self-test`，自检 **10 条**。
* `check_source_list`（`tools/check_source_list.py`）：有 `--self-test`，自检 **5 条**。
* `check_doc_refs`（`tools/check_doc_refs.py`）：有 `--self-test`，自检 **10 条**。
* `check_csharp_shell`（`tools/check_csharp_shell.py`）：有 `--self-test`，自检 **5 条**。
* `check_conformance_provenance`（`tools/check_conformance_provenance.py`）：有 `--self-test`，自检 **6 条**。
* `check_doc_paths`（`tools/check_doc_paths.py`）：有 `--self-test`，自检 **5 条**。
* 其余工具按文件名登记在这里，便于查找：`audit_doc_anchors.py`（锚点复核）、`check_comment_language.py`
  （注释语言）、`check_format.py`（随包 `format.txt` 与引擎常量）、`check_links.py`（相对链接）、
  `check_public_surface.py`（头/符号/声明三冻结面）、`check_python_shell.py`（Python 壳）。
* 规范档（`examples OFF`）的**注册用例数：49**，`slow` 标签 4 条 —— 这两项的单一权威是
  `docs/NEW_DIALECT_PLAYBOOK.md` 的配置指纹表（下面的条数说明也只写指针）。

**本节列出的每个门禁都有自检，而且各门禁的条数全部由 `tools/check_doc_claims.py` 每次实跑比对**（2026-10-20 起）：写的数字与实跑读数不等即报 `MISMATCH … self-test`。**本节刻意不写"共几个门禁"** —— 那个数字属于 §5 的测量表；写在这里只会是又一份要手动同步的副本（2026-10-20 修：这句原先写"13 个门禁"，而当时实数是 **19**，恰好是一份没人校验的副本）。

这句总述以前数错过两次（先写「9 个门禁工具」，后来只加条目、没改这句，又写成 12），所以现在不再写一个要与列表同步维护的全局工具数 —— **以本节这份列表为准**。

2026-10-20 又补了三个洞：`check_source_list` / `check_doc_refs` / `check_doc_claims` 三个工具的条数此前根本不在测量表里（被判「not measured」后直接放行），其中 `check_doc_refs` 的「6 条」实际是 **7** 条、工具与文档同时错；另外那两行**把工具名写了两遍**，把「自检」推出了条数断言的匹配窗口，等于双重失守。修法三件事一起做：接进测量表、两行改成与其余行同一形状、断言允许 `**` 加粗。**验证方式是毒化**：改成 8 ⇒ `MISMATCH docs/STABILITY.md:96`，改回 ⇒ PASS。
⚠️ **`TEMP`/`TMP` 与环境的关系（2026-10-27 复测：原文那句已经不成立）**：

本节这张表的数字是在**默认 `TEMP`** 下量的；历史上（2026-10-20）曾记录"把 `TEMP` 指向本仓 `.tmp/` 会让部分工具的自检夹具
落进它自己的跳过列表、扫描到 0 条而拒绝运行"。**这条在当前的树上不复现**：2026-10-27 实测把 `TEMP`/`TMP` 指到 `.tmp` 根、
在 staged 库是新鲜的前提下跑 `tools/run_all_gates.py --with-build`，**22 条门禁全绿**，唯一 skip 的 `check_generation_compat`
在默认 `TEMP` 下也 skip（树上只有一代，规则无对象）。当年那个失败已被这两个工具自己在代码里处理掉，所以"必须移除 `TEMP`"
不再是前置条件。

**真正有前置条件的是 `ctest`，方向相反，别把两者混用**：

| 你要跑什么 | `TEMP`/`TMP` | 设错的症状 |
|---|---|---|
| `ctest`（C++ 测试） | **必须**指到仓内可写目录 | 默认 `%TEMP%` ⇒ `CppPinyinTests` / `ThreadSafety` / `InstanceIsolation` 三条红（判据与复现见 §5 开头那条） |
| `tools/run_all_gates.py` | 无所谓（2026-10-27 两种设置都 22/22） | —— |

本节列表之外的 `tools/run_all_gates.py` **不接受** `--self-test`：它的价值是"一条命令看全部判词"，
不是给 `ctest` 增加覆盖面。它不进这份列表的理由与列表本身相同：一条没有东西能量的"自检 N 条"，
就是一句没人能证伪的话。

`check_gate_inventory` 的自检特别之处：它**独立重述一遍规则**（`audit_inventory()`）并断言与真实清单
一致 —— 因为两次试着把 `main()` 的判定体抽出来共用都改坏了门禁（`SyntaxError: 'return' outside function`），
所以选择"规则写两遍 + 一致性用例"，代价是多一份文本，收益是这个门禁终于可被测试。

## 1. sanitizer（ASan/UBSan）

### 1.1 ① 现在能不能验？—— 本机能（一条窄路，2026-10-06 实测）；板上不能

**装置（代码事实，行号成文时重读）**：

- `[事实]` option 存在且默认 OFF：`option(CPP_PINYIN2_SANITIZE "Build the library, tools and tests with
  ASan and UBSan (GCC/Clang)" OFF)` —— `CMakeLists.txt:31`；其存在理由写在 `:27-30`（"开发机是 MinGW，
  没有 libasan"）。
- `[事实]` 配置期自检并**响亮拒绝**：`check_cxx_source_compiles(... CPP_PINYIN_HAVE_SANITIZERS)`
  （`CMakeLists.txt:206`），失败即 `message(FATAL_ERROR ...)`（`:209-213`）。自检刻意同时设
  `CMAKE_REQUIRED_FLAGS`（`:204`）与 `CMAKE_REQUIRED_LINK_OPTIONS`（`:205`），理由写在 `:193-199`
  （`check_cxx_compiler_flag` 在 ubuntu:24.04 + GCC 13.3 上曾给出**假阴性**）。
- `[事实]` flag 集：`-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g`
  （`CMakeLists.txt:214-215`）+ 链接 `-fsanitize=address,undefined`（`:216`）。
- `[事实]` 插桩覆盖范围：`cpp-pinyin_dev_flags = cpp-pinyin_warnings + cpp-pinyin_sanitizers`
  （`CMakeLists.txt:231-232`），库 `:144`、两个工具 `tools/CMakeLists.txt:66-67`、
  tests 三个目标 `tests/CMakeLists.txt:485-488`（三条 `target_link_libraries(... cpp-pinyin2_dev_flags)`）、examples `examples/CMakeLists.txt:10,32`。
  **C 目标**（`CApiSmoke`）刻意只链 sanitizer 集、不链 C++ 警告集：`tests/CMakeLists.txt:249`。

**本机可用路径（2026-10-06 实测；这是全仓**唯一**的 ASan 复现配方，别处只写指针）**：

- `[事实]` **三个条件同时具备才有这条路**：**clang++ 22.1.4（MSVC 目标）** +
  `-DCMAKE_BUILD_TYPE=RelWithDebInfo` + 仓库自带的 `-DCPP_PINYIN2_SANITIZE=ON`，另外给三个绕开 MSVC STL
  注释宏的编译宏 `-D_DISABLE_VECTOR_ANNOTATION -D_DISABLE_STRING_ANNOTATION -D_DISABLE_OPTIONAL_ANNOTATION`
  （本机 VS 树里**没有** `stl_asan.lib`，不加这三个宏就在链接期死在
  `lld-link: error: could not open 'stl_asan.lib'`，见 `.tmp/p1s28-asan/build.log:31`）。
  配置期自检随即打印 `Performing Test CPP_PINYIN_HAVE_SANITIZERS - Success`、`configure exit 0`
  ⇒ `CMakeLists.txt:206` 那个守卫在这条工具链上是**放行**（它仍对 MinGW 响亮拒绝）。
- `[事实]` **ASan 运行时 DLL 必须在 `PATH` 上**（成文时新测到的关键参数）：插桩出的 exe 依赖
  `<clang 根>\lib\clang\22\lib\windows\clang_rt.asan_dynamic-x86_64.dll`，而它**不会**被拷进 `bin/`
  ⇒ 不把该目录加进 `PATH` 时，插桩测试一律以 `Exit code 0xc0000135`（DLL not found）起不来。
  **这是环境问题，不是缺陷信号**（成文时第一次 `-L slow` 就是这样 4/4 红的，加上 `PATH` 后同样 4 条 4/4 绿）。
- `[事实]` **条数**：各配置的注册数**唯一权威是 `docs/NEW_DIALECT_PLAYBOOK.md` 的配置指纹表**
  （2026-10-10 重测：默认 **36**、`EXAMPLES=ON` **37**、静态 **35**、两者同时 **36**，各带条目集合指纹；
  `tools/check_doc_claims.py` 会读回该表并从构建目录重算），本文不复述。
  本节这一档（**examples OFF**）上游实测时记的是**全量 36 条**、`-LE slow` **28 条**、`-L slow` **4 条**；
  ⚠️ 同一行当时还写了「examples ON 为 33 条」，与 2026-10-10 重测的 **37** 不符 ⇒ **以指纹表为准**。
- `[事实]` `-L slow` **4 条** = `LoadCheck` / `LoadCheckSelfTest` / `DumpEquivalence` / `DumpEquivalenceSelfTest`。
- `[事实]` **结果**：`-LE slow` **27/28**（红的是 `InstallConsumerSmoke`，见下）、`-L slow` **4/4**；
  两个日志里 **ASan/UBSan 报告 0 条**（`AddressSanitizer|runtime error` 零命中）。`ExportGate|BindingGates`
  另跑一次 **4/4 绿**。
- `[事实]` 唯一那条红 **`InstallConsumerSmoke` 与 ASan 无关**：它用 **MinGW g++**（本机的
  `mingw/16.1.0-rt_v14-rev0` 的 `c++.exe` + `ld.exe`）去链**这条 MSVC ABI 安装树**里的
  `lib/cpp-pinyin.lib` ⇒ `undefined reference to '__imp__ZN10cpp_pinyin8Mandarin…'` 一族
  （`.tmp/p1s28-asan2/ctest.log:287-326`）—— **交叉工具链不匹配**，同一测试在 MinGW 档里是绿的。
- `[事实]` "ASan 在这台机器上有牙"是**探针自证**过的（`.tmp/asan_probe.cpp`，含一个堆越界用例）：
  `ok` ⇒ `sum=10 n=4 oob=0`、exit 0；`oob` ⇒ `ERROR: AddressSanitizer: heap-buffer-overflow`、exit 1，
  且**栈顶是项目帧** `…/asan_probe.cpp:13` ⇒ 上面那个"0 条报告"不是空壳跑出来的。
- `[事实]` 证据文件（只读留档，不做二次加工）：`.tmp/p1s28-asan-rwdi.log`（configure + build + `-LE slow`）、
  `.tmp/p1s28-asan-rwdi-gates.log`（成文时 `-L slow` 与 `ExportGate|BindingGates` 复跑）、
  `.tmp/asan_probe.cpp`、`.tmp/p1s28-asan2/{build.log,ctest.log}`、`.tmp/p1s28-asan/build.log`。

**仍存的边界（未验证就是未验证 —— 别把上面读成"sanitizer 已覆盖全部"）**：

- `[事实]` **板上不能**：旧台账（原 `REPO_LAYOUT.md` 批次 ⑭）
  记全套 ASan 构建在这块板（3.8 GiB 内存）configure 通过后
  **>17 min 未完成 ⇒ 主动停止**（`free` 显示 `2810 used / 965 available`）；`:315` 记另一次窄验证里 ASan 自身报
  `CHECK failed: sanitizer_allocator_primary64.h:131`，并把"内核 4.4 拿不到影子内存"明确标为 `[推断]`/未证实。
  板侧 sanitizer 覆盖**按项目 skill 的硬约束不做**。
- `[未核实]` **另两条路成文时没留日志**：MinGW g++（`CMakeLists.txt:190-191` 记"没有 libasan"）与 clang 的
  `windows-gnu` 目标（此前观测缺 `libclang_rt.asan_dynamic.dll.a`，发行包只带 MSVC 目标的运行库）⇒
  仍算**不可用**，但这是**转述而非成文时实测**，别当成本文结论引用。
- `[事实]` **不要用 Debug 档跑 ASan**：同 clang、同三宏、`CPP_PINYIN2_SANITIZE=ON`，只把档位换成 `Debug` ⇒
  **28 条里 10 红**。其中 **9 条**是 ASan 报告，且报告栈**全是系统 CRT**（`ucrtbased.dll` / `MSVCP140D.dll` /
  `ntdll.dll`），**一条项目帧都没有**；同 clang **Debug 关 sanitizer** 的对照档只 **1** 红。
  ⇒ `[推断]` 归因"ASan × debug-CRT 不匹配"，**机制成文时未证实**（如实按推断写，勿升级成定论）。
- `[未证实]` **Windows 上没有 LeakSanitizer**（泄漏检测目前只写在 Linux 腿的 `detect_leaks=1`，§1.2）
  ⇒ 本机路径**不给**泄漏检测这一维。
- `[事实]` 板/CI 的覆盖状态**不变**：板不做、CI 腿是否跑过未证实（§1.2）。本机这条路**替代不了** CI 腿
  （环境不同、且本机禁 push）。
- `[历史]` 旧台账（2026-10-02 的更正记录）当时写"**唯一残留**是'sanitizer 下的套件真绿'
  仍标**未验证（待 CI）**（本机无 `libasan`…）" —— 该记录**按原样保留**；其中"本机"这半句在 2026-10-06
  被上文的窄路限定掉了（"真绿"现在**有一份本机观测**，仍**不等于** CI 腿已验）。

### 1.2 ② **门禁化**靠什么验？—— 本机有手工窄路（§1.1） + CI 一条腿，而那条腿的触发 `[未证实]`

> **2026-10-06 更正**：标题从"靠什么验"改为"**门禁化**靠什么验"。本机现在**能**跑出 ASan 观测（§1.1），
> 但那是**手工复现**（环境、运行时 DLL 的 `PATH`、档位都自己配），**替代不了**下面这条腿的 runner 侧接线。
> **2026-10-06 第二次更正（同批）**：标题原后半句作"**仍只剩 CI 一条腿**"，与本文 §0 总表（本机**能**）和上面的
> §1.1 自相矛盾 ⇒ 已改为现在的措辞；"只剩 CI"只对**门禁化**这条腿成立（见 `docs/REQUIREMENTS.md` §5.3 同一处更正）。

- `[事实]` 执行者：`ci.yml` 的 job `linux-sanitizers`（`.github/workflows/ci.yml:65-86`）：
  `ubuntu-latest`（`:66`）、`ASAN_OPTIONS: detect_leaks=1` / `UBSAN_OPTIONS: print_stacktrace=1`
  （`:68-69`）、`RelWithDebInfo` + `-DCPP_PINYIN2_SANITIZE=ON`（`:79-82`）、跑与 `linux` 腿**同一套** ctest（`:86`）。
- `[事实]` 该 job 自己的备注把边界写死：`:63-64` "**What is NOT verified** is this job's wiring on the
  runner itself, and the runner's GCC version need not match the one measured."
- **触发链的出处（任务要求点名的自述，逐字）**：`.github/workflows/ci.yml:11-12`：
  > `# Consequence to keep in mind: until test_runner.yml is on the default branch and registered, nothing`
  > `# triggers this workflow. The jobs themselves are unchanged.`
  `[事实]` 该工作流**只**声明 `on: workflow_call:`（`ci.yml:13-14`）—— 自己没有任何触发器。
- **第二处自述**：`.github/workflows/test_runner.yml:20-23`（**2026-10-27 注：源文件里这段已改写成"已跑过"，
  下面的引用保留为审计当天的原文**）：
  > `# NOT WIRED YET, and deliberately stated rather than implied: this file's runner wiring is unverified`
  > `# until it runs on GitHub (see the header of test_csharp.yml for how the individual commands were`
  > `# verified locally), and no publishing lives here -- a PyPI or NuGet upload must be a separate,`
  > `# manually dispatched workflow.`
  `[事实]` `test_runner.yml` 确实持有 push/pull_request/tag 触发器（`:25-31`），并在 `:38-40` 用
  `uses: ./.github/workflows/ci.yml` 调它 —— **代码上的接线在**。审计当天"它在 GitHub 上真的跑起来过"无法验证；
  **2026-10-23 已有一条全绿运行记录**（`f5c44bd`，run `39877724768`，见 §0.1），这一条因此已经闭合。
- ⇒ `[未证实]` **`linux-sanitizers` 是否曾经真正执行过、是否真绿。** 本文不把它当已验门禁引用。
  **〔2026-10-27 闭合〕** `[事实]` 它执行过且是绿的：2026-10-23 的那次全绿运行（`f5c44bd`，run `39877724768`）
  的 19 个 job 里就有 `linux-sanitizers`（§0.1 的清单）。下面两句随之作废，保留原文以便对照。
  `[推断]` 在"禁止 push"的前提下，它**不可能**在本机侧被观察运行。
  **〔2026-10-21 分工说明〕** 别把下面这条读成"CI 跑过了"：本机侧**能跑的部分**已实测 —— MinGW g++ 与
  MSVC 19.51 各自 configure/build/`ctest` **49/49**，20 项门禁全绿（当时是 20 条；**2026-10-27 起 22 条**，
  见本段末——这一句原写"21 条"，是加入 `verify_build_dependent` 之后的数字，后来又加
  `tools/check_parsable.py` 时没有同步，正好与它下面那段自相矛盾）。但那是**本机三条腿**（MinGW g++ / MSVC / Clang，各 50/50）的证据，GitHub 上五个 job
  的真实运行记录**仍然一条都没有**，sanitizer 腿依旧 `[未证实]`。
  **〔2026-10-27 三条腿重测（第 42–43 轮）〕** 用例总数现在是 **49**（不是这句里的 50，那是 2026-10-21 的口径）；
  门禁从 20 条变成 **22 条**：`verify_build_dependent`（它守的两条生成期规则此前**没有任何自动调用者**），`tools/check_parsable.py`（5 个 workflow、2 个 `.sh`、4 个 JSON 与 `CMakePresets.json` 此前**没有任何门禁解析过**）。
  三条腿逐一重跑：`default`（MinGW g++）**49/49**、`msvc`（VS2026 / clang-cl 环境外的 cl 14.51）**49/49**、
  `clang`（22.1.4）**49/49**——但 clang 这一条**是在修掉 `InstallConsumerSmoke` 的编译器传递缺陷之后**才绿的
  （修前 48/49，见 `tests/install/check_install_consumer.cmake` 的注释）。也就是说：**这句话在
  2026-10-21 到 2026-10-27 之间，只有在前两条腿上成立**。
  **〔2026-10-27 另有两条配置〕** 除三条编译器腿之外，两种构建类型也实测过：
  `CMAKE_BUILD_TYPE=Debug`（同一份源码，`assert()` 全部保持生效）**49/49**；`-DCPP_PINYIN2_WERROR=ON`
  + `BUILD_EXAMPLES=ON`（**CI 的 linux 腿用的就是这组**）**50/50**，构建零警告。前一条是 D45 那类
  "Debug 与 Release 不一致"的反向证据，后一条说明本地 GCC 与 CI 的严格告警集合没有分歧。
  **四套配置组合至此全部实测**（第 65–66 轮），读数与 `docs/NEW_DIALECT_PLAYBOOK.md` §7 的指纹表一致：
  默认 **49** / +examples **50** / 静态 **48** / 静态+examples **49**——最后这套是后补的，
  它证明「静态少一条」与「examples 多一条」**互不抵消**（48+1=49，而默认 49 的成因不同，
  指纹也不同：`a131c1e6c36e0485` ≠ `93b0e403a39ffa01`）。
- `[事实]` CI 中也**没有**第二条 sanitizer 腿：`mingw`（`ci.yml:157`）、`mingw32`（`:215`）、
  `macos`（`:89`）、`windows`（`:125`）、`linux`（`:22`）五个 job 都不设 `CPP_PINYIN2_SANITIZE`。

### 1.3 ③ 若改动什么会破？

1. `[推断]` **新增一个 C++ 目标却忘了链聚合目标** ⇒ 该目标静默不被插桩，而所有现存门禁仍然全绿。
   聚合目标的动机正是这条：`CMakeLists.txt:222-225` 记"加 sanitizer 集曾要逐处编辑 6 组链接行"。
   同类先例已有一次真实修补：examples 曾是"唯一不吃项目警告集的 C++ 代码"（`examples/CMakeLists.txt:7-9`）。
2. `[推断]` **把 C 目标改去链 `cpp-pinyin_dev_flags`** ⇒ 吃进 C++ 专有 flag（`CMakeLists.txt:227-229`
   点名了这条边界），编译期噪音甚至失败。
3. `[推断]` **删掉 `-fno-sanitize-recover=all`** ⇒ UBSan 变成"只打印不判红"，绿运行不再代表任何东西
   （理由原文：`CMakeLists.txt:186-187` "A gate that only prints is not a gate"）。
4. `[推断]` **把自检换回 `check_cxx_compiler_flag`** ⇒ 回到已实测的假阴性（`CMakeLists.txt:193-199`）：
   在运行库齐备的 Linux 上拒绝可用工具链。
5. `[事实]` **触发链本身**：删/改 `test_runner.yml` 的 `uses:`，或让它不被默认分支注册 ⇒
   `ci.yml` 全族（含 sanitizer 腿）无触发器。风险等级：这是**当前唯一装置**，且它的运行状态本来就未证实。

---

## 2. 线程（多线程并发使用同一 / 不同 `Engine`）

### 2.1 ① 现在能不能验？—— 能，但只覆盖契约里被支持的那一半

- `[事实]` **契约原文有两半**。唯一权威是 `include/cpp-pinyin2/core/Engine.h` 的**类注释 `THREADS:` 段**（2026-10-17 逐字读回，**按符号引用、不再抄行号**；逐字原文共 4 行，含 `///` 前缀）：

  > THREADS: after construction completes, concurrent READ-only calls (`query` / `readings` /
  > `toSimplified` / `isPolyphonic`) are safe on a shared instance; `loadUserDict` / `loadTraditionalDict`
  > / `setDisambiguationCallback` MUTATE tables readers touch and must not overlap a read from another
  > thread -- a data race today, measured (concurrent loading corrupts the heap).

  同文本在 C 侧 `include/cpp-pinyin2/ext/c_api.h` 的 "THREAD SAFETY" 段另有一份。
  〔**2026-10-17 更正**：本处原先引的是 `Engine.h:23-26` 的斜体块 `**Thread safety** -- identical to the built-in facades…` 并写成"逐字引用"，但该措辞在今天的 `Engine.h` 里**不存在**（是门面类还在时的旧文本被抄成了现行引用）；且当时门面头 `Mandarin.h` 已删、`Dialect.h` 也不含线程段 ⇒ "契约在 `Engine.h` / `Dialect.h` / `c_api.h` 三处"这一半同样不成立。已按今天的头逐字改回，行号改符号。〕
- `[事实]` **可执行用例存在**（三组，各自独立）——**一律按符号引用**（本段原先写的行号 2026-10-17 实测已漂；`TEST_CASE` 行号：`test_c_api.cpp` 562、`test_bug_regression.cpp` 340、`test_comprehensive.cpp` 194、`test_engine_dialect.cpp` 435）：
  - `tests/cpp/test_c_api.cpp` 的 `TEST_CASE("C ABI: a shared handle survives concurrent readers", "[c_api][thread]")`
    —— 4 线程 × 40 轮，走 `cppp2_query`/`cppp2_readings` 的 marshalling 层。
  - `tests/cpp/test_bug_regression.cpp` 的 `TEST_CASE("concurrent read-only calls on a shared engine agree with single-threaded results", "[api][thread_safety]")`
    —— 4 线程 × 150 轮 × 5 个只读 API，普通话 + 粤语共享实例。
  - `tests/cpp/test_comprehensive.cpp` 的 `TEST_CASE_METHOD(Fixture, "Thread safety smoke test", "[thread][concurrent]")`
    —— 4 线程 × 100 轮，轻量冒烟。
  - 另有跨实例并发：`tests/cpp/test_engine_dialect.cpp` 的 `TEST_CASE("no shared state between instances, across threads", "[engine][thread]")`（见 §3）。
- `[事实]` **它不能证明什么，测试文件自己写明了**（`tests/cpp/test_bug_regression.cpp`，`TEST_CASE(...)` 上方那段注释，**符号锚点**）：
  "The unsupported half is deliberately NOT exercised … a test racing on purpose would be flaky by
  construction — it would report the contract, not check it. **What this test cannot do is prove
  race-freedom (no TSan on this toolchain)**; it catches the realistic regression of a read path starting
  to WRITE shared state (a cache, a lazy index)."
- `[事实]` **全仓没有任何 TSan 装置**：`git grep -n "thread_sanitize\|TSan\|tsan" -- src include tests CMakeLists.txt .github`
  只命中 1 处，即上面那句注释（`tests/cpp/test_bug_regression.cpp:340`）。
- `[事实]` 旧台账记录同一口径（"契约仍不加锁（按设计）… 仍未加锁：`load*` /
  `setDisambiguationCallback` 与并发读重叠依然是未定义行为（契约已写在 `Mandarin.h:25-32`）"）。
  —— **2026-10-10 复核**：这一句是从旧记录抄来的，两处都不成立 —— ① 它引的头（连同 `Mandarin` 类）**已从公开面删除**；② 当年它自己就注明了行号已漂到 `Mandarin.h:30-35`。契约现在是 `include/cpp-pinyin2/core/Engine.h` 的类注释 **Thread safety** 段（成文时实测 :35-40），本行按符号定位。

### 2.2 ② 靠什么验？—— 点名装置

- `[事实]` **执行装置**：主套件 `CppPinyinTests`（`tests/CMakeLists.txt:146`，
  `add_test(NAME CppPinyinTests COMMAND CppPinyinTests "~[corpus]~[thread]~[thread_safety]~[concurrent]~[isolation]")`），
  加上 **2026-10-04 新增的两条独立条目**（`:120-123`）：`ThreadSafety`（`"[thread],[thread_safety],[concurrent]"`，
  实测选中 5 个 case）与 `InstanceIsolation`（`"[isolation]"`，1 个 case），两条都带 `thread` 标签
  ⇒ 同日在默认配置实测 `ctest -L thread` **2 条全过**；主套件把这两组标签排除，同一批 case 不会跑两遍。
  所有线程用例都在这个 Catch2 二进制的 `tests/cpp/*.cpp` 里（源文件清单：`tests/CMakeLists.txt:20-36`）。
- `[史实·已闭合]` **本文初稿（2026-10-04 早些时候）实测到的是「没有一条 CTest 条目名、也没有一个 CTest
  标签直接指向"线程"」** —— 当时想单跑这些 case 只能靠 Catch2 标签（`ctest -R CppPinyinTests -- --list-tests`
  之类的路径），`ctest -L` 选不到。用户当日拍板后按上面两条条目补齐 ⇒ **该缺口已闭合**。本条保留为史实：
  它也是"文档里的缺口会被下一个人当成现状"的现成例子（另：本文不再罗列全部注册名的行号——那张清单会烂，
  以 `tools/check_gate_inventory.py` 与 `ctest -N` 为准）。
- `[事实]` 线程链接不是自动的：`tests/CMakeLists.txt:13-14` 显式 `find_package(Threads REQUIRED)`，
  理由写在 `:9-12`（glibc < 2.34 上 `pthread_create` 未定义引用）。
- `[事实]` `CppPinyinTests` 条目带过滤 `~[corpus]`：`tests/CMakeLists.txt:92-100` 说明语料用例被拆到
  `CorpusAccuracy` 单独跑，**因此 `ctest -R CppPinyinTests` 不再覆盖语料**（原文自述）。
  对本文的意义：线程用例在 `~[corpus]` 这一侧，**不受该拆分影响**。
- `[未证实]` 这些用例在 CI 上是否真的跑过 —— 与 §1.2 同一个原因（触发链自述未接线）。

### 2.3 ③ 若改动什么会破？

1. `[推断]` **在只读路径里引入写入**：任何"首次查询时建缓存/惰性索引/懒加载"的优化，都会穿过
   用例而**不被静态检查发现** —— 这正是 `test_bug_regression.cpp:341` 声明它能抓的唯一一类回归。
2. `[推断]` **给 `load*` 与查询之间加锁并宣称"线程安全了"**：契约文本（`Engine.h` 与 `Mandarin.h` 的
   "Thread safety" 段、`c_api.h` 的 "THREAD SAFETY" 段）会与实现不一致；而当前**没有任何门禁比对
   "契约文本 ↔ 实现"**。
3. `[推断]` **改动 `Registry.h` 的加锁**：`add` 取 `unique_lock`、`resolve`/`ids` 取 `shared_lock`
   （`src/Engine.cpp` 的 `Registry::add` 取 `unique_lock`、`Registry::resolve` 取 `shared_lock`），且 creator 在**锁外**调用（`resolve` 里 "Called with the lock released" 那条注释 + 紧随其后的 `return creator();`；原文引的 `:64,75,86` 与 `:81-82` 四个行号在 2026-10-27 重读时已全部漂掉：`:64` 是空行、`:65` 才是 `Registry::add`），
   理由是"creator 允许回调注册"。把 creator 挪进锁内会引入死锁（推断：同一把 `shared_mutex` 上
   重入 `unique_lock`）。
4. `[推断]` **删掉 `find_package(Threads)`**（`tests/CMakeLists.txt:14`）：在 glibc < 2.34 的 Linux 上
   测试二进制**链接失败**，而不是行为变化（`:9-12` 记录了这条）。

---

## 3. 多实例（同进程多个 `Engine`：异方言 / 异词典 / 异配置）

### 3.1 ① 现在能不能验？—— 能，而且有正负对照

> **2026-10-05（S3，Registry 形态收敛）追加**：**进程级单例与自注册宏都已删除**。`cpp_pinyin::v2::Registry`
> 的构造函数**公开**且构造即注册内置两方言（`include/cpp-pinyin2/ext/Registry.h`、`src/Engine.cpp:41-56`），
> `Engine::find` 与 `DialectRegistry::instance()` 一并消失；id 的唯一入口是
> `Engine::open(registry, root, id)`，且它**只查调用方传进来的那张表**。因此本小节里凡出现
> "进程级单例 / `instance()` / `CPP_PINYIN_REGISTER_DIALECT` / 自注册在链接里" 的段落（原 `:43-47`、
> `:185-192`、`:199`、`:214-218`、`:225-247`、`:252`、`:259-261`）都是 **S3 之前的形态记录**，不再描述今天的树；
> 新形态的把守装置是 CTest `HostOwnedRegistry` / `HostOwnedRegistryBare`（`tests/CMakeLists.txt:499-507`，
> 一组调用方自持注册表的正/负对照；行号 2026-10-27 重读——原文写的 451-454 已漂到 AC-4 那段注释上）＋ `test_engine_dialect.cpp` 的 `registry semantics`
> （两个 Registry 互不可见、`ids()` 有序、空 id/空 creator 与重复 add 的失败路径；详见 §3.2）。
> **另一处随之消失的事实**：原来"唯一的非 const 文件级对象是 `src/Engine.cpp:56` 的 `static
> DialectRegistry registry;`"（见 `:199`）—— **今天一个也没有**；库侧 ID 查表用的那张不可变表是
> C 层自己的函数局部 `static const`（`src/c_api_lifecycle.cpp:12-15`，`cppp2_registry == NULL` 时兜底，
> 理由写在同文件 `:7-11`）。

- `[事实]` **契约在类型层面**：`Engine` 的词典根是构造参数、**没有 setter**
  （`include/cpp-pinyin2/core/Engine.h:48-49` 四个构造 + `:21-22` 注释 "The dictionary root is a
  constructor argument and there is no setter for it -- deliberately (D12 / FR-4.5). Two `Engine`
  objects with two different roots, or two different dialects, coexist in one process and do not interfere."）。
- `[事实]` **可执行用例 1（多实例 × 线程，含注册表并发查表）**：
  `tests/cpp/test_engine_dialect.cpp:465` `TEST_CASE("no shared state between instances, across threads", "[engine][thread]")`。
  4 线程（`:487`）× 40 轮（`:488`），每个线程自己构造 `mandarin`/`cantonese`/探针方言三个引擎（`:502-505`；
  这四个行号 2026-10-27 重读，原文的 412/434/435/447-450 已整体漂掉），
  与**单线程基线**逐字节比对（`:455-463`），并检查长活实例事后未损（`:476-479`）。
  文件头 `:16-18` 把意图写明："**Nothing is process-global.** Every case builds its own engines from its own roots"。
- `[事实]` **可执行用例 2（用户词典不串味）**：`tests/cpp/test_engine_dialect.cpp:482`
  `TEST_CASE("instances do not share loaded user dictionaries", "[engine][isolation]")` ——
  `a` 载入用户词典后结果变（`:504-507`），而 `b` 的查询结果不变（`:508`）。
- `[事实]` **可执行用例 3（异方言 + 异配置隔离）**：同文件内的能力位用例，用 `FlagTone` 回显
  "哪个开关真的到达了转换器"（`:63-75`），并验证**关掉能力位后配置无效**（`:366-373`）。
- `[事实]` **可执行用例 4（注册表语义）**：`tests/cpp/test_engine_dialect.cpp:377`
  `TEST_CASE("registry semantics", "[engine][registry]")`；`:397-399` 断言"重复 add 无 replace 必须失败"、
  `:403-406` 断言"已 resolve 的描述是快照，后续 replace 不改变它"、`:408-409` 断言空 id / 空 creator 失败。
- `[事实]` **进程级单例的真身与范围**：`include/cpp-pinyin2/ext/Registry.h:19-33` 的类注释
  （"Process-wide, READ-ONLY table of dialect id -> creator. … it stores **creators only**. It never
  stores a dictionary path, never stores an `Engine`, and never caches loaded dictionaries."）；
  `:37` 的 `static DialectRegistry &instance();`；`src/Engine.cpp:55-58` 的函数局部 static 实现
  （线程安全初始化）；`src/Engine.cpp:45-50` 只注册 `"mandarin"` 与 `"cantonese"`。
- `[事实]` **库不读环境变量**（成文时亲自数的）：`git grep -n "getenv\|setenv\|putenv" -- src include tools examples`
  ⇒ **0 命中，exit 1**。全仓命中 4 处，全在第三方 vendored Catch2
  （`vendor/catch_amalgamated.cpp:4294,4297`、`vendor/catch_amalgamated.hpp:243,9551`）。
  `[事实]` 另有第三方写入点在测试侧（Catch2 的 `getenv` 包装）。
  ⚠️ **纠正一处常见误读**：CI 里的 `ASAN_OPTIONS`/`UBSAN_OPTIONS`（`ci.yml:68-69`）是**运行时读**，
  但读它们的是 sanitizer 运行库、不是本库 —— 与"库不读环境变量"不冲突。
- `[事实]` **唯一的非 const 文件级对象**是 `src/Engine.cpp:56` 的 `static DialectRegistry registry;`
  `[推断]`（依据：`git grep -n "std::mutex\|std::shared_mutex\|std::atomic\|std::once_flag" -- src include`
  只命中 `src/Engine.cpp:37` 一行）。
- ⚠️ **两条文档口径需要更正**（照实记录，不删史实）：
  - `docs/DECISIONS.md` §2.1:131` 写 "`src/`+`include/` 内 `std::mutex`/`std::shared_mutex`/
    `std::atomic` **0 处**"。**实测是 1 处**：`src/Engine.cpp:44` `mutable std::shared_mutex mutex;`。
    `[推断]` 该行是 P1 实施**之前**的快照，同文档 `:25-26` 自己已经承认后来加了这把锁。
  - 同一行 `:131` 与旧台账提到 "`std::mutex`" 的旧口径 —— 以 `src/Engine.cpp:44` 为准。

### 3.2 ② 靠什么验？—— 点名装置 + **两条真约束的当前把守状态**

> **本小节的行号已于 2026-10-06 在 HEAD `0d883e7` 上逐条重读**（§3.1、§3.3 的段落仍是 2026-10-04 的快照；
> 各装置的**名字**是稳定的，行号不是）。

**装置清单**（全部真实存在）：

| 装置 | 位置 | 覆盖什么 | 条件 |
|---|---|---|---|
| CTest `CppPinyinTests` | `tests/CMakeLists.txt:129` | §3.1 的用例 1–4（都在同一个二进制里）；把 `thread`/`thread_safety`/`concurrent`/`isolation` 四组标签排除，改由下面两条独立条目选中（避免同一批 case 跑两遍） | `CPP_PINYIN2_BUILD_TESTS=ON` |
| CTest `ThreadSafety` | `tests/CMakeLists.txt:160` | 只读并发契约：`[thread]`/`[thread_safety]`/`[concurrent]` | 同上（标签 `thread`） |
| CTest `InstanceIsolation` | `tests/CMakeLists.txt:162` | 多实例：`[isolation]` ⇒ 实例间不共享已装载的用户词典 | 同上（与 `ThreadSafety` 共用 `thread` 标签） |
| CTest `HostOwnedRegistry` | `tests/CMakeLists.txt:508-509` | **正向**：调用方把探针 id `add` 进**自己声明的** `Registry` ⇒ 该 id 必须 resolve，且 `Engine::open` 必须给出可用引擎、`engine.dialect().id` 必须就是它 | 同上（标签 `registry`；程序 `tests/registry/host_owned_registry_main.cpp`） |
| CTest `HostOwnedRegistryBare` | `tests/CMakeLists.txt:510-511` | **负对照**：**同一个二进制**、只差"调用方没 add" ⇒ 该 id 必须 resolve 不了，`Engine::open` 必须失败**且消息点名该 id** | 同上 |
| CTest `DialectSetGate` | `tests/CMakeLists.txt:328-330` | `data/dialects/` 目录集 == 运行时注册的 id 集 == 冻结清单 | 同上（标签 `dialect`） |
| CTest `CApiSmoke` | `tests/CMakeLists.txt:228` | C 侧 `cppp2_open(NULL, "no-such-dialect", …)` 必须失败、`code == CPPP2_ERR_NOT_READY`、detail 点名该 id（`tests/c_api_smoke.c:141-150`） | 同上（标签 `abi`） |
| 静/动态链接 | `CMakeLists.txt:104-106` | `BUILD_SHARED_LIBS=OFF` 时定义 `CPP_PINYIN_STATIC` | — |

**两条真约束：写成可执行断言了吗？**

- **(a) "一个 id 只在调用方交进来的那张表里可达"** —— **覆盖到了，且带负对照**。
  `[事实]` 正/负两侧是**同一份代码**，只差一次 `add`：`tests/registry/host_owned_registry_main.cpp:57-78`
  （`:59` `add` 探针、`:66` `resolve`、`:72-78` 负例自述 "the negative control is broken, so the positive
  case proves nothing"）。
  `[事实]` `Engine::open` 那条路在同一程序里也被压成断言：`:83` 打开、`:84-94` 正向要求引擎可用且方言 id
  相符、`:96-110` 负向要求"必须失败 + 必须有理由 + 理由必须含该 id 字符串"（该文件 `:104-105` 写明理由：
  否则调用方分不清"打错字"与"词典没装起来"）；`:114-121` 再断言"未知 id 不 resolve，而内置 `mandarin` 必须可达"。
  `[事实]` `Registry()` / `~Registry()` 进冻结清单是有原因的：调用方自持 ⇒ 它必须能在外部构造与析构
  （`tests/abi/expected_plugin_surface.txt:9-13` 原文）。
  `[边界]` 该程序**不装载**探针方言的词典（`:38-41` 自述 "The tables are never loaded"）⇒ 它证的是
  **id → 方言描述 → 引擎可用**这一段，**不是**"第三方方言真能查到词"；后者由 `examples/plugin-dialect`
  在**安装门禁**里真跑（安装到 scratch 前缀再运行：`tests/install/InstallConsumerSmoke`，由 `ctest` 每次构建都跑）。
- **(b) 旧那条"静态库下注册 TU 可能不被链入 ⇒ id 不注册"** —— **随形态收敛整体消失，原因留档**。
  `[史实]` 2026-10-05（S3）之前，注册发生在某个 TU 的静态初始化表里（`CPP_PINYIN_REGISTER_DIALECT`），
  链接器有权丢掉整个无人引用的 TU ⇒ 当时用"两个只差一个 TU 的可执行文件"把守，并因此带一个**未闭合的
  推断**：那对目标跟随 `BUILD_SHARED_LIBS` 默认值（ON），要真压到静态路径得另配一条静态侧判据 ——
  那条判据**始终没有单列出来**。
  `[事实]` 今天注册只发生在**运行时**：`Registry` 的构造即注册内置两方言、没有 `instance()`、没有
  `builtins()`、没有注册宏（`include/cpp-pinyin2/ext/Registry.h:27-38`）；第三方方言由调用方自己 `add`。
  `tests/registry/host_owned_registry_main.cpp:6-8` 把这个变化写成一句话："a registry is now owned by the
  caller, so 'who registered this id' is answered by the object the caller passes to `Engine::open`, **not by
  the link map**" ⇒ 旧约束**不可能再以旧形式成立**（没有静态初始化期注册这回事了），代价对应的新约束
  是"未注册的 id 不可达"，而它**有负对照**（上一段）。
  `[事实]` 旧装置的两个可执行文件与 `the linked-in probe (no longer in the tree)`（`b88826b` 删除）都不在了：今天只剩
  `host_owned_registry_main.cpp` 一个文件、两个模式（`tests/CMakeLists.txt:434-454` 的注释逐字记了这次换面）。

### 3.3 ③ 若改动什么会破？

1. `[推断]` **引入任何"全局默认词典根/全局配置"**：会同时打破 `Engine.h:26-28` 的类型级承诺与
   §3.1 全部 4 组用例。今天 id → 方言的**唯一**路径是调用方自己传进 `Engine::open` 的那个 `Registry`
   （`include/cpp-pinyin2/ext/Registry.h:19-38`：只存 creator、不存词典路径、也不存 `Engine`，且**没有进程级
   表**）⇒ 连"悄悄给个默认方言"的退路都已不存在：`HostOwnedRegistryBare` 会把"未注册的 id 却能用"
   当场判红（§3.2），错误消息还必须点名 id。
2. `[推断]` **把注册表从"只存 creator"扩成"也缓存词典/`Engine`"** ⇒ 回归 `Registry.h:19-25` 的
   "no 'default dictionary' to be read or written globally"。
3. `[推断]` **给 `data/dialects/` 加第三种语言目录**：`DialectSetGate`（`tests/CMakeLists.txt:328-330`）
   会红 —— 这是**有装置**的（检查器自述在
   `tests/data/dialect_set/check_dialect_set.cmake:3-8`，同文件 `:55-60` 起是它的自证段）。
   `[事实]` 该检查器**不读**环境变量，也不依赖 CWD（`:23-26` 用 `-D` 传参）。
4. `[推断]` **给 `Registry` 加回"进程级 / 默认"语义**（恢复 `instance()` / `builtins()` / 静态注册，
   或让未注册的 id 回落到内置）⇒ 三处同时红：① §3.2 的 `HostOwnedRegistryBare` 负对照；
   ② `Registry()` / `~Registry()` / `add(` / `resolve(` / `ids(` 等声明**按序逐字冻结**在
   `tests/abi/expected_plugin_surface.txt:14-29`（`:5-7` 说明顺序即布局、顺序才是被比对的东西）
   ⇒ 与清单不同批提交就破 `BindingGates`；③ `docs/REQUIREMENTS.md` §5.3 的计数行会一起失真
   —— 那一行现在**真被** `tools/check_doc_claims.py` 校验（§5 第 6 条）。
5. `[推断]` **删除测试里按 CWD 找 `dict/` 的假设**：多个用例用
   `std::filesystem::current_path() / "dict"`（`test_bug_regression.cpp:344-345`、
   `test_engine_dialect.cpp:414`），CTest 侧靠 `WORKING_DIRECTORY` 兜住
   （`tests/CMakeLists.txt:206` 等）—— 这是一个**残余的隐式全局输入**（"测试依赖 CWD"），
   换 CWD 就跑红。`docs/DECISIONS.md` §2.1:131` 也把它列为待消灭项。

---

## 4. ABI 兼容（C ABI 冻结面 + C++ 插件面 + 版本 / so-version 策略）

### 4.1 ① 现在能不能验？—— 源级与符号级：能（有边界条件）；二进制兼容：**不可验（没有对象）**

- `[事实]` **本线未发布，因此不存在"老版本"可对打**：`include/cpp-pinyin2/ext/c_api.h` 的 "ABI STABILITY" 段
  （2026-10-06 重读：这一段已**不再写函数个数**，改为指向冻结清单，避免与清单漂移）
  > `* ABI STABILITY (revised 2026-10-01 -- read this before shipping a binding): this line has NOT been`
  > `* released, so there is no compatibility promise to keep yet and everything in this header may still`
  > `* change. The promise starts with the first release: from then on the functions declared in this`
  > `* header keep their names, argument types and meaning. The frozen list is`
  > `* tests/abi/expected_c_api_symbols.txt and the COUNT lives there; no number is repeated here.`
- `[事实]` 权威成文（唯一权威）：`docs/DECISIONS.md` §2.1:926`
  `#### 6.8.5.1 版本与兼容契约（**本节是这一段的唯一权威**，2026-10-03 成文）`。
  三条要点在 `:930-942`，将来要真承诺二进制兼容要"同时做三件"在 `:944-946`。
- `[未证实]` 该节的"唯一权威"地位只有它自己与旧台账
  声明；`[推断]` 没有机器门禁在比对"哪些文档复述了版本策略"。`docs/DECISIONS.md` §2.1
  是**入库且 frozen 清单里没有它**，因此它自己可以被静默改动而不触发任何门禁 —— 这是本文的推断，不是缺陷排期。

### 4.2 ② 靠什么验？—— 三个冻结面 + 各自的装置

**冻结面 1：公开头清单**

- `[事实]`（2026-10-06 重读；2026-10-17 改为指向权威）清单：`tests/abi/expected_public_headers.txt` ——
  **头数以该清单的非注释行为准**（清单本身是这一个数的机器真值源，`tools/check_bindings.py` 按这些行**双向**比对）。
  文件行数与行号随注释增删漂移，故此处**不复述**；三个冻结面的定量读数**只许一处登记**，那处是
  `docs/REQUIREMENTS.md` §5.3 的 ① 行，本文件下方的「冻结面口径」行由 `tools/check_doc_claims.py` 逐轮与真值源核对。
  `[注]` 本小节其余行号仍是 2026-10-04 快照；三条清单的**形状**在下面三条里重读过。
- `[事实]` 执行者之一 `BindingGates`：`tests/CMakeLists.txt:573-574`
  （`tools/check_bindings.py`，无 flag = 仓内可验证子集），比对逻辑在 `tools/check_bindings.py` 的 `check_public_header_surface`。
- `[事实]` 执行者之二 `HeaderSelfCheck`：`tests/CMakeLists.txt:441-442`，把**每个** `include/cpp-pinyin2/**/*.h`
  单独编译两次（`:308-341`，glob 在 `:323`，空 glob 会 `FATAL_ERROR`，`:324-329`）。
- ⚠️ **两个装置的覆盖差**：`HeaderSelfCheck` 的 glob 只保证"存在于此的头都能自包含"，
  **不比对清单**；`BindingGates` 才比对清单，但**它需要 Python3**（`tests/CMakeLists.txt:573-574`，
  找不到 Python 时 `:468-470` 是 `FATAL_ERROR`，不是跳过）。

**冻结面 2：C ABI 符号清单**

- `[事实]` 清单：`tests/abi/expected_c_api_symbols.txt`（文件头自述 **"THE COUNT IS THESE LINES"**，2026-10-06
  起**行数即答案、正文不写数字**；符号是注释块之后的一段连续行）—— **已删 2 个**
  （`cppp_is_ready`/`cppp_last_error`），当前符号 **19**，与 `docs/REQUIREMENTS.md` §5.3 的登记值同批改
  **冻结面口径（本行由 tools/check_doc_claims.py 逐轮核对）**：**15 公开头 + 23 C ABI + 18 插件面声明**。
  （那是三个数的唯一登记处）；**历史注记（2026-10-06 当时）**："另有 N7A 的 `cppp2_apply_tone_sandhi`（+1）与
  N29 的两项**尚未落地** ⇒ 目标 19"。<!-- doc-claims: historical -->
  这三项**现已全部落地**（`cppp2_apply_tone_sandhi`、`cppp2_style_list` / `cppp2_style_list_free`），
  现状读数以本条上方的**冻结面口径**行为准，此处**不写第二份数字**。
  ⚠️ **本条曾写"31 行 / `:14-31`"，板上取证实测为 30 行、符号块 `:15-30`**：行号随注释增删漂移，故不再复述行号，
  唯一定量读数就是符号行数本身。
- `[事实]` 执行者 `ExportGate`：`tests/CMakeLists.txt:283-284`（2026-10-27 重读改准；此前引过 `:228-212` 与 `:250-257`，前者是反向区间且不存在，后者已漂到别的用例），检查器
  `tests/abi/check_exports.cmake`（双向比对 + 内部符号泄漏检测在 `:78-105`，工具选择在 `:202-216`）。
- `[事实]` **只在共享构建注册**：`tests/CMakeLists.txt:282-284`（`if (BUILD_SHARED_LIBS)` 把它整个包住）；静态构建下 configure 期
  `message(STATUS ...)` 明确说"不注册"（`:215-216`）—— **不是静默跳过**。
- `[事实]` **静态构建下的把守改到了链接期**：`tests/c_api_smoke.c:406-411` 的注释逐字记录
  "Measured 2026-10-03: `cppp_load_traditional_dict` was the one frozen symbol no test referenced … its
  implementation could be removed with every entry still green. Referencing it here makes that a link error."
  实调用在 `:242-243`。
- `[事实]` `ExportGateSelfTest`（`tests/CMakeLists.txt:292-296`）用四个合成 dump 证明检查器**会红**
  （`tests/abi/check_exports.cmake:147-190`：缺一个符号 / 多一个符号 / 内部符号泄漏 / 正确）。
- `[事实]` **静态构建下的全套测试，2026-10-27 第一次跑**：`-DBUILD_SHARED_LIBS=OFF -DCPP_PINYIN2_BUILD_TESTS=ON`
  ⇒ `ctest` **48/48 通过**（49 减掉未注册的 `ExportGate`，原因见上条，不是少测了）。在此之前静态构建只被
  manylinux 步骤 10 以 `-DCPP_PINYIN2_BUILD_TESTS=OFF` 构建过——**它的测试从来没在任何地方跑过**，而
  `tests/c_api_smoke.c:406-411` 说明静态路径的符号把守就靠这条链接期用例。

**冻结面 3：C++ 插件面声明清单**

- `[事实]` 清单：`tests/abi/expected_plugin_surface.txt` —— **条数以该清单的非注释行为准**（旧文曾写「29 行 / 13 行注释 / 6+10 条」，
随该面换面后已失真，故不再复述）；文件头 `:5-7` 说明**顺序即布局、必须按序比对**。
  `[事实]` 该面的成员在 2026-10-05（S3）换过一次面：`static DialectRegistry &instance()` 与
  `CPP_PINYIN_REGISTER_DIALECT` 出列，`Registry()` / `~Registry()` 入列，**总数不变**（`:9-13` 原文
  "Still 16 declarations, but not the same sixteen"）。⇒ 引用这个清单时必须说**哪一天**的那一版。
- `[事实]` 执行者仍是 `BindingGates`：比对逻辑与"清单太短就拒绝通过"的守卫在
  `tools/check_bindings.py` 的 `check_plugin_surface`（"清单不足以描述面 ⇒ 失败"）。
- `[事实]` 该面**曾经没有 CTest 用例**，理由写在 `tests/CMakeLists.txt:539-544`：
  "The plugin surface and the header set were owned by `tools/check_bindings.py`, which nothing in this
  repository called except **two CI workflows whose trigger workflow has never run on GitHub**. A gate
  that only runs in CI is a gate that has never been observed to run, so it is wired here."
  —— 这句话与 §1.2 的触发链自述**互相印证**，是当时找到的第二处"CI 未接线"记录。
  **〔2026-10-27 补注〕**上面这段引文**已经过期**：那条触发链在 2026-10-23 跑绿（run `39877724768`），
  `tests/CMakeLists.txt` 的同一段现在紧跟一句"that trigger chain has since run green … so 'never run' is
  history"（该文件 `:553-555`）。这里保留原文是因为它是"为什么把门禁接进 CTest 而不是只靠 CI"的理由，
  **不是因为那句话今天还成立**——门禁不该依赖"CI 被观测到跑过"这件事本身。

**版本 / so-version 策略（现状 + 权威行）**

| 项 | 现状（`[事实]`） | 权威在哪一行 |
|---|---|---|
| 版本号唯一源 | `project(cpp-pinyin2 VERSION 2.0.0 ...)` | `CMakeLists.txt:6`（同文件 `:2-5` 自述"the single authority"） |
| C ABI 报的版本 | `CPP_PINYIN2_VERSION_STRING="${PROJECT_VERSION}"` → `cppp2_version()` | `CMakeLists.txt:303`；实现 `src/c_api_lifecycle.cpp:73`；兜底 `src/c_api_internal.h:49-50`（三处行号 2026-10-27 重读，原文写的 168-169 / 9 / 32-33 全部已漂） |
| 包版本兼容等级 | `COMPATIBILITY SameMajorVersion`（**2026-10-06 由 `AnyNewerVersion` 改判**） | `CMakeLists.txt:417-421`（指令在 `:420`） |
| 该等级的**语义边界** | 表达"同 major 的**包/API** 请求算满足" ≠ 承诺兼容（更非二进制兼容）；它同时**挡住跨 major**：1.x 请求不再被 2.0 满足 | `docs/DECISIONS.md` §2.1:963-973`；**可执行判据** `tests/install/check_install_consumer.cmake:163-182` |
| so-version（`SOVERSION`） | **`SOVERSION 2`（2026-10-18 决定 D2，仅共享构建）** ⇒ 产物为 `libcpp-pinyin2.so.2` 加无版本符号链接；**仍不承诺二进制兼容**（`SameMajorVersion` 只管包/API 一级）；PE（Windows）产物**没有 soname**，这一格记 "n/a (PE)" | `CMakeLists.txt:282-286`（`SOVERSION` 在 `:285`）；此前"全仓 0 命中"的记录见 `docs/DECISIONS.md` §2.1:793`（D24 已被本决定取代）；门禁 `tests/abi/check_soname.cmake` |
| 版本宏（`CPP_PINYIN_VERSION_MAJOR/MINOR/PATCH`、`kVersion`） | **不落地（已结案）**；树里只有构建期宏 `CPP_PINYIN2_VERSION_STRING`，**无版本分解宏** | `docs/DECISIONS.md` §2.1:948-953`；原文计划 `docs/DECISIONS.md`（已被标未采纳） |
| `D2` / `D3` 的裁定 | **`D2` 已改判（2026-10-06）：`AnyNewerVersion` → `SameMajorVersion`**（用户口径：只要求 API 兼容、不为不存在的消费者做冗余；1.x 请求不得被 2.0 满足）；D3 = **不做版本宏** | `CMakeLists.txt:345-349` + `docs/DECISIONS.md` §2.1:963-973`（入库且自称唯一权威）；旧裁定记录见 l1 接口稳定台账第 209–210 行（**该文件未跟踪**，已被本次改判取代） |〔**2026-10-09 清除**：那份台账文档已删，取回命令见本文开头〕

- `[事实]` **成文时亲自复数的版本宏命中**：
  `git grep -n "CPP_PINYIN_VERSION_\|kVersion" -- src include tools tests examples CMakeLists.txt`
  ⇒ 3 行，全在构建期路径：`CMakeLists.txt:303`、`src/c_api_internal.h:49,50`。`src/c_api_lifecycle.cpp:73` 用它。
  ⇒ **`include/` 下 0 命中**，与 `tests/abi/expected_public_headers.txt` 的现状一致。
- `[事实]` **`cppp2_version()` 的值有门禁**：`tests/install/check_install_consumer.cmake` 校验安装树
  `.pc` 与 `ConfigVersion` 等于 `project(... VERSION)`（登记在 `tests/CMakeLists.txt:288-302`，
  该文件 `:110` 注释说明它还会索要一个**更新的**版本号以验版本比较）；
  `tools/check_bindings.py` 的 `check_version_chain` 比对 CMake / `version.props` / wheel / nuspec / tag。
- `[事实]` **`D2`/`D3` 的权威落点是分裂的**：入库的那句话在
  `docs/DECISIONS.md` §2.1:926-953`（自称唯一权威）；而"哪个选项被裁定成 (b)/(a)"的
  决策台账只在旧台账（**2026-10-09 起已跟踪**）。〔**2026-10-09 清除**：那份台账文档已删，取回命令见本文开头〕  <!-- anchor-history: same ledger, with its deletion already noted -->
  `[事实]` 另一处入库记录是旧台账：`L1-D2` **"待拍板（现按推荐维持现状 + 文档写清，可回退）"**
  —— **与"已结案/保持现状"的口径不完全一致**，成文时照实记录，不替任何一方作结论。

### 4.3 ③ 若改动什么会破？

1. `[事实/推断]` **共享构建下增删 `cppp_*` 符号** ⇒ `ExportGate` 红（双向都红，
   `tests/abi/check_exports.cmake:94-105`）。
2. `[推断]` **静态构建下删掉某个 C ABI 函数的定义** ⇒ 只有 `CApiSmoke` 的**链接期**能抓到
   （`tests/c_api_smoke.c:410` 的原话："its implementation could be removed with every entry still green"
   是**修之前**的状态）；若新增的函数没有被任何测试引用，**这个洞会重新打开**。
3. `[推断]` **给公开头加一个新的 helper 但忘了 `CPP_PINYIN_EXPORT`** ⇒ 会因为
   `-fvisibility=hidden`（`CMakeLists.txt:139-142`）而变成内部符号，`ExportGate` 的泄漏检测
   （`check_exports.cmake:82-86` 只查 `EngineImpl`/`PhraseGate` 两个名字）**不一定报**。
4. `[事实/推断]` **改 `PinyinGlobal.h` 的导出方案**：`include/cpp-pinyin2/core/PinyinGlobal.h:11-20`
   把 MinGW 放到 `__declspec(dllexport)` 分支，`:5-10` 记录了原因与后果 ——
   "Measured 2026-10-01: … GNU ld applied its default 'export every global symbol' rule and the DLL
   exported **250+ symbols** -- including internal ones (`EngineImpl`, `PhraseGate`)"。
   ⇒ 把 MinGW 移回 `__attribute__((visibility))` 分支会**重演这次泄漏**（`ExportGate` 会抓，但只在共享构建）。
5. `[推断]` **`Dialect` 聚合体加成员或换序** ⇒ `BindingGates` 按序比对会红
   （`tests/abi/expected_plugin_surface.txt:5-7` + `tools/check_bindings.py` 的 `check_plugin_surface`）。
6. `[推断]` **改 `cppp2_query_opts` 的字段布局**（插入/重排/改类型，或把新选项放进旧的尾部 padding）⇒
   `struct_size` 前缀守卫会**静默失灵**而不是报错。守卫的位置：前缀判定在
   `src/c_api_lifecycle.cpp:174-177`（`prefix` = 声明值 ≥ `sizeof(size_t)` 且 < 新 `sizeof`），
   逐字段 gated 在 `:178-194`；可执行证据在 `tests/c_api_smoke.c` 的哨兵值块
   （哨兵值法：`on_error = 777` / `include_candidates = 888`，声明外字段必须**保持不动**）。
   `[事实]` **这两处不一致的成文口径已闭合（S12，2026-10-06）**：`include/cpp-pinyin2/ext/c_api.h` 文件头
   的第 6 条原写 "**the struct does not grow: new options arrive as a new struct type plus a new entry
   point**"，与同文件 `cppp2_query_opts` 上方那段 APPENDED 规则正面冲突；该条已按后者重写为"新选项
   **APPENDED 在整个旧 `sizeof` 之后**，绝不落在旧声明留下的 padding 里"。
   〔**2026-10-10 行号锚点改为名字锚点**：原条目按 `:21-24` / `:225-244` 引用，而成文时把那段 45 行的
   "HOW THIS STRUCT GROWS" 压成头文件里的 "THE GROWTH RULE"（规则留头文件）+ §6.2.1（事故与 padding
   算术）两处 ⇒ **行号锚点整批失效**。结论不变、证据补一句：初版把 `include_reading` 追在旧声明的
   padding（28..31）上，实测用 `0x5A` 填充后"什么都没请求"的调用方拿到了结构化结果；现行
   `reserved_0` 认领 28..31，下一个需认领 36..39（`sizeof` 40、字段止于 36，布局探针实测）。〕
   权威仍是 `ext/c_api.h` 的 "THE GROWTH RULE" 段与 §6.2.1 ①，以及把它钉住的用例
   `tests/cpp/test_c_api.cpp` 的 `OldQueryOptsV1`
   （`static_assert(sizeof(OldQueryOptsV1) == 32)` + `CHECK(offsetof(cppp2_query_opts, include_reading) >= 32)`）。
   ⇒ 引用"结构体怎么长"时**必须指到规则段本身**（名字锚点优先），只写"追加在末尾即可"是错的。
7. `[推断]` **把 `BUILD_SHARED_LIBS` 默认值改成 OFF** ⇒ `ExportGate` 静默不注册
   （`tests/CMakeLists.txt:283-284`，2026-10-27 重读改准；此前引的 `:227-217` 是反向区间、`:250-257` 已漂），C ABI 面只剩链接期把守；`CMakeLists.txt:131-132` 的注释本身就
   把"默认是共享"当作前提。
8. `[推断]` **改 `COMPATIBILITY`**：改成 `ExactVersion` 而不做 `SOVERSION` + ABI 版本入口 ⇒
   按 `docs/DECISIONS.md` §2.1:944-946` 的说法属"做半套"；
   `[事实]` `tests/install/check_install_consumer.cmake:110` 的注释还指出：正是因为
   `AnyNewerVersion`，"要一个比装机版更新的版本"才会被接受 —— 自证的负例必须用 `999.0.0` 级别的版本号才有效。

---

### 4.4 C4/N27（2026-10-06）的两处**破坏**，与它们为什么值得

`[事实]` 这一批同时做了一件**源级破坏**和一件**行为破坏**，都挂在 P1 缺陷"同一句查询四层得到不同字节"名下：

1. **源级**：`MandarinConfig` 与 `CantoneseConfig` **已删除**，三配置合一为 `EngineConfig`（`core/Types.h`）；两个门面（`Mandarin`/`Cantonese`）现在收 `EngineConfig`。多数调用点一字不改即可编译——`style` 换成了值类型 `Style`，`cfg.style = MandarinStyle::TONE3;` 这类拼写靠隐式转换原样成立。
2. **行为**：`style` 的默认从 `0`（NORMAL）变为**哨兵 `-1`＝"方言自己的默认"**（普通话 TONE、粤语 TONE3），**在 C++/C/Python/C# 四层同时生效**。于是"空配置"四处同义；此前 C++ 门面给 `zhōng guó`，而 `Engine`/C ABI/两壳给 `zhong guo`。**升级后不显式指定 style 的调用点会看到声调符号——这就是修复本身，不是副作用。**
3. **为什么不是坏账**：本仓当前**没有外部消费者**（用户裁定："现在没有任何消费者，你不要为兼容性做出不必要的冗余"）；且 `-1` 永不会被当成风格使用——`resolveStyle` 对"方言未声明任何风格"落回 `0`，**自定义方言行为不变**（`tests/cpp/test_default_style.cpp` 有专门的判据）。
4. **判据（不是文档）**：`tests/cpp/test_default_style.cpp`（哨兵 + 求解表 + 双方言默认在表内）、`tests/c_api_smoke.c`（空 `cppp2_query_opts` ⇒ `nǐ hǎo`）、`python/tests/test_shell.py::test_the_default_style_is_the_dialects_own`、`python/tests/test_contract.py`（两壳的 `DEFAULT` ↔ `Types.h` 的 `int alias = -1;`）。
5. `[未完成]` **文档 prose 尚未收敛**：`README.md`/`docs/API_zh.md`/`docs/API_en.md` 只做了机械改名（旧类型名只剩"此前有三个配置类型…"这类**变更叙述**里的提及，实测 API_zh 两处、API_en 一处，均非残留的 API 用法；真正的叙述性改写仍留给 C1），"三配置合一 + 新默认"的叙述性改写留给 C1（**2026-10-16 复核：仍成立** —— 该改写要等 C1 删掉两个门面头文件时才顺手做，届时本节条目应随之转成完成态）——那一批要删掉两个门面头文件，本来就要重写这几节。

### 4.5 大版本演进政策（v2 / v3 / v4…；本节是这一主题的唯一权威）

**适用范围**：`cpp-pinyin` 2.0 起（1.x 不适用：它使用 `Pinyin::` 命名空间，与本政策的名字面没有交集）。

**网络口径的一句话**：每个大版本只承诺向下兼容**前一个大版本**；公开名带版本命名空间，因此"用错代"
永远是编译错误，不存在多个版本争一个名字的情况。下表是全部条文。

| 规则 | 内容 |
|---|---|
| R1 | 公开名的唯一写法是 `cpp_pinyin::v{N}::…`，N = 包主版本（唯一真相源：根 `CMakeLists.txt`）。**不提供任何不限定的根名**：`cpp_pinyin` 只装载版本命名空间，自身不含任何声明。**主版本号 = 代号**（唯一真相源同前）⇒ 库内**无任何全局态**（实测：无单例、无进程级注册表、无环境变量读取 ⇒ 两代可同进程共存 ✓），因此 `cppp_version()` 是**唯一**的运行时代际探针：消费者拿到一个已加载的库，只能靠它确认「这是第几代」✓。 |
| R2 | 每版只承诺向下兼容**前一个大版本**的名字与语义（除该版 CHANGELOG 列出的变更）。窗口外的版本（含 1.x）不在承诺内。 |
| R3 | 兼容的实际含义是可机检的一件事：把 `cpp_pinyin::vN::` 改成 `cpp_pinyin::v{N+1}::`（一次机械替换）即可编译。 |
| R4 | 每版一套安装面：头目录 `include/cpp-pinyin{N}/`、库名 `libcpp-pinyin{N}`、CMake 包与别名 `cpp-pinyin{N}[::cpp-pinyin{N}]`、`cpp-pinyin{N}.pc`、词典目录 `share/cpp-pinyin{N}/dict`。并发安装不得有同名文件（vcpkg 的硬规则）。 |
| R5 | 每版一套导出宏前缀 `CPP_PINYIN{N}_*`。预处理层没有命名空间，前缀是唯一手段。 |
| R6 | 版本命名空间**永不内联**（`inline namespace` 会把成员再导出到 `cpp_pinyin`，等价于偷偷造出根名。2026-10-14 实测：两代内联同时可见会报 "call of overloaded … is ambiguous"）。 |
| R7 | **C ABI 的每一个有链接性的函数符号都带代令牌**（条数以 `tests/abi/expected_c_api_symbols.txt` 为准，2026-10-23 为 23）：`cppp_open` → `cppp2_open`（v3 是 `cppp3_open`）。符号面**仍然只有一处权威**：`tests/abi/expected_c_api_symbols.txt`；改名与清单更新**必须同一提交**（`check_bindings` 会比对，不同批就红）。**C 面上每个名字都带令牌**：函数符号（`cppp_open` → `cppp2_open`，v3 是 `cppp3_open`）、结构体/typedef（`cppp2_engine`、`cppp2_query_opts`、`cppp2_syllable`…）、枚举常量与宏（`CPPP2_OK`、`CPPP2_ERR_*`、`CPPP2_SOURCE_*`、`CPPP2_TONE_KIND_*`）。**没有链接性的类型为什么也要带**：不带时，两代的 `ext/c_api.h` 放进同一 TU 会报 7 个结构体重定义 + 15 个枚举常量冲突（**2026-10-16 实测证伪**：`tools/verify_build_dependent.py --only g3` 报 `NOT VERIFIED`），而本政策承诺「两代的头可同时 include」（见本节共存表）⇒ 那条承诺对 C 面曾经是**假的**，同批已修（`2d33e25`（压缩前提交，克隆内不可达））。改名规则因此收敛成一句：**C 面上凡是能产生名字的东西都带代令牌**。库内部对自己的调用不属于消费面，跟着实现改名即可。**消费者必须知道的三件事**：① **v2 的 C 消费方在 v3 时代必须重编**：符号名变了 ⇒ 旧二进制解析不到新符号 ⇒ **链接期/加载期**失败，而**不是**运行期静默用到另一代（这正是把禁令升级为令牌的目的）；② 两代 C 库现在可以同进程共存且各调各的；③ 托管面的擦除风险仍在，见 R15（两个 Python distribution 可共存并互相覆盖同一导入目录；两个同 id 的 NuGet 包会被静默合一）。 |
| R8 | C++ 侧无此限制：两代可以同进程共存（命名空间不同、类型不同、库内无全局状态）。 |
| R9 | **设 `SOVERSION`，取包主版本**（`CMakeLists.txt:282-286`，`SOVERSION` 在 `:285`，仅共享构建）：产物为 `libcpp-pinyin2.so.${PROJECT_VERSION_MAJOR}` 加一条无版本的符号链接。**它只表达"代"，不是 ABI 承诺**——同一代内的 2.x→2.y 仍要求重编（`docs/CONTRACT.md` §1），`SameMajorVersion` 管的是包/API 那一级。soname 必须与头目录/宏前缀/库名/命名空间令牌/C 符号令牌**同批**变化（R4/R5/R7），由 `tools/check_version_tokens.py` 与 `tests/abi/check_soname.cmake` 双向把守；Windows 没有 soname（PE 产物），那一格在 §4.2 的表里写"n/a"而不是留空。 |
| R10 | 仓库内部（`src/**` 与公共头内部）不得出现根名写法；版本命名空间内部直接用同命名空间的名字。 |
| R11 | 仓库内测试与示例一律写全限定名，不得依赖任何便捷别名。 |
| R12 | 文档、示例与迁移指南只承诺 R2 的窗口，不得出现对更早版本的兼容承诺。 |
| R13 | **数据/词典格式自报家门**：每个方言目录一行 `format.txt`（内容 = 数据格式代；2.0 线写 `2`），引擎在**读任何表之前**校验它。判定表：**存在且等于支持值 ⇒ 加载**；**缺失 ⇒ 按 v1 遗产纯文本格式接受**（用户自建目录不必改）；**大于支持值或非数字 ⇒ 响亮失败**（`Code::Load`，消息含目录、发现值与支持上限）；**目录里有 `dialect.json`（安装树产物的标志）却缺 `format.txt` ⇒ 也响亮失败**。用户词典 `user_dict.txt` **不加版本头**（格式冻结；库侧对用户文件是**刻意容忍**的，见 `src/dict/DictUtil.cpp:221-225`，因此不兼容的变化只能走新 API 或新文件名）；未来的二进制表**不得复用文本表名**（`*.cpdict` 用不同文件名 ⇒ 纯文本引擎报"表缺失"，而不是把二进制当文本半读）。**格式代的唯一权威是引擎侧的 `kSupportedDictFormat` 常量**。 |
| R14 | **头文件里禁止 `using namespace cpp_pinyin::vN;`**：它会把一代的名字**再导出**到包含者的 TU，等于根名以"第三方头"的形式复活，而本仓的任何门禁都扫不到下游。文档只推荐 TU 局部别名 `namespace cp = cpp_pinyin::vN;`；`using namespace` 仅限 `.cpp`。 |
| R15 | **托管面（Python/C#）也带代令牌**：Python 导入名 `cpp_pinyin{N}`、分发名 `cpp-pinyin{N}`；C# `AssemblyName`/`RootNamespace`/`PackageId` 均带令牌（`CppPinyin{N}`）。理由与 C++ 侧同一条：**每代用户可见的名字都带令牌** ⇒ 两代可共存且不可能静默串味。（现状 2026-10-19 实测：分发名 `cpp-pinyin2`、导入名 `cpp_pinyin2`、`AssemblyName`/`RootNamespace` = `CppPinyin2` **均已带**；**只有 `PackageId=CppPinyin` 未带**——两代若都发这个 id，NuGet 会静默合一。）**2026-10-20：这条已是可执行断言** —— `tools/check_bindings.py` 的 `check_csharp_identity` 冻结这三个属性的值（唯一带 `<PackageId>` 的 csproj 必须等于 `CppPinyin` / `CppPinyin2` / `CppPinyin2`），改值必须同批改该表并留下评审记录；此前无任何门禁看这三个属性（此前无任何门禁看这三个属性） |
| R16 | **公开的别名/回调类型必须带代身份**：`using X = std::function<…>` 这类 RHS **只含标准库类型**的公开名，在两代之间会是**同一个类型**（`is_same_v` 为真），于是"写错代"不会报错。这类别名必须改成版本命名空间**内定义的类型**（构造成员保留"lambda 可直传"）。G3 的"逐个名字断言跨代不同类型"探针负责把它量出来。 |

**升级一个版本时要做什么**（对调用方，逐条可执行）：

1. 把 `#include <cpp-pinyin{N}/…>` 改成 `#include <cpp-pinyin{N+1}/…>`；
2. 把 `cpp_pinyin::vN::` 机械替换成 `cpp_pinyin::v{N+1}::`；
3. `find_package(cpp-pinyin{N})` / `-lcpp-pinyin{N}` / `CPP_PINYIN{N}_*` 宏 / 词典目录同步换代令牌；
4. 读该版 CHANGELOG 的 Breaking 段（R2 只覆盖"名字与语义"，行为变更仍以 CHANGELOG 为准）。
5. **C ABI 消费方（R7）**：20 个**有链接性的函数符号**每代都带令牌（`cppp_open` ⇒ `cppp3_open`）⇒ **必须重编**；
   旧二进制在新库上会在**链接期/加载期**失败 ✓（这正是不要"运行期静默用到另一代"的手段 ✓）。**类型、枚举常量与宏同样带令牌**
   （`cppp_engine` / `CPPP_OK` 等，与 R7 同一条规则）—— 不带时两代的 `ext/c_api.h` 放进同一 TU 会报 7 个结构体重定义 +
   15 个枚举冲突，而本节承诺"两代的头可同时 include"；按第 3 条同步换掉它们的令牌即可。
6. **Python 消费方（R15）**：**导入名**与**分发名**都带令牌 ⇒ `import cpp_pinyin` ⇒ `import cpp_pinyin3`，
   `pip install cpp-pinyin2` ⇒ `pip install cpp-pinyin3`。**两个 distribution 可共存** ✓（但它们会覆盖同一个导入目录里各自的文件 ✓）。
7. **C# 消费方（R15）**：`AssemblyName` / `RootNamespace` / `PackageId` 三处都带令牌 ⇒ 包引用从 `CppPinyin2` 改 `CppPinyin3`、
   命名空间从 `CppPinyin2` 改 `CppPinyin3` ✓。**两个同 id 的 NuGet 包会被静默合一** ✗，所以**不要**同时引用两代 ✓。
8. **1.x 用户**：本政策**不适用**（1.x 用 `Pinyin::` ✓，名字面没有交集 ✓）—— 升级路线见 `docs/STABILITY.md` §4.5 ✓。

**维护者加一代时要改什么**：安装面令牌、宏前缀、命名空间 token、**托管面令牌（R15：Python 导入名与分发名、C# 三个 msbuild 属性）**、迁移指南（只写 N-1→N）；
数据面新增（或换代）时再改**数据格式代**（R13：`format.txt` 与引擎侧的 `kSupportedDictFormat`，两者必须同批）；
**不要**动旧代的命名空间体（它永远是 `vN`），也**不要**为旧代补任何"同名兼容名"（R12）；
**不要**在任何公共头里写 `using namespace cpp_pinyin::vN;`（R14），也**不要**新增 RHS 只含标准库类型的公开别名（R16）。

**数据侧的第二个版本号 `min_library`：它与换代的关系（2026-10-20 补）**

`data/dialects/*/dialect.json` 的 `min_library` 是**手写的第二份库版本**（产地是词典仓，本仓只消费），它同时
被两条规则约束，**第一条在 `tools/check_version_tokens.py` 里逐轮核对（`min_library ≤ 包版本`），第二条是人的规则、机器只记录不校验**：

1. **不得超过包版本**（`min_library ≤ project(... VERSION ...)`）—— 超过等于宣称"本库读不了自己的数据"；
2. **只在数据真的需要更新的库时才抬**：抬它等于承诺"低于此版本的库拒绝加载这份数据"（D4 的运行期表现是
   `Code::DictIncompatible`，见 `core/Types.h` 与 `ext/c_api.h` 的注释）。

**因此 `min_library` 与 `format.txt` 是两件事，不要一起改**：`format.txt` 说的是**数据格式代**（R13：引擎在读任何
表之前校验，值大于支持值即响亮失败），`min_library` 说的是**库版本要求**。而**换代（N → N+1）时默认不动
`min_library`** —— 新库读旧数据本来就是常态；只有当新数据确实用到新库才有的能力时才抬，并在该版 CHANGELOG
的 Breaking 段写明"旧库会被 `DictIncompatible` 拒绝"。

## 5. 横向观察（四条都不能靠"文档写了"成立）

> **2026-10-09 追加：全量 ctest 的 3 条"失败"是这个 shell 的环境，不是代码 —— 判据可复现。**
> 在把 `TEMP`/`TMP` 留作默认值的 shell 里跑 `ctest`，会看到 `CppPinyinTests`、`ThreadSafety`、
> `InstanceIsolation` 三条红，症状是 `REQUIRE(out.is_open())` 失败、
> `filesystem error: cannot create directories: Permission denied [C:\Users\…\Temp]`、
> `std::get: wrong index for variant`（根因同一条：**C++ 子进程写不了系统临时目录**，夹具建不起来）。
> **判据**：同一棵树、同一条命令，只把 `TEMP`/`TMP` 指到仓内一个可写目录，三条立即转绿。
> 本机实测：`TEMP=<仓内目录>` ⇒ `ctest --test-dir <build> -j 4` **全部通过、exit 0**；用例数与 `slow` 标签数**不在此处重复** —— 它们的单一权威是上文「注册用例数」那一条（本行只记录"这套命令在本机跑得过"）。
> 默认 `TEMP` ⇒ 29/32、exit 8。默认那个目录里 PowerShell 自己写得进去（`Out-File` 成功）、
> **C++ 子进程写不进去** —— 所以不是仓库缺陷，而是"跑测的人必须先知道的一条前置"。
> 复现探针 13 行（`std::filesystem::temp_directory_path` + `create_directories` + `ofstream`）当场证过；
> 按上面两行环境变量设置即可，**不必改任何测试代码**。



1. `[2026-10-09 的记录]` **当时四个面里没有任何一个的子断言在 CI 被观察运行过**（§1.2 的 `ci.yml` 触发段自述、
   §4.2 引的 `tests/CMakeLists.txt` 那句 "whose trigger workflow has never run on GitHub"）。当时本机能验的
   三分之四（线程、多实例、ABI 的源级/符号级）全部落在本机 CTest 上。
   **2026-10-27 更新：这条已不成立。** `test_runner.yml` 那条链自 2026-10-22 起真在 GitHub 上跑，
   2026-10-23 全绿（运行记录、job 清单见本文件 §0.1），这些 ctest 子断言因此都在 CI 里被观察运行过。
   `tests/CMakeLists.txt` 与 `ci.yml` 里那几句"从未跑过"的自述**同批过期**，§0.1 已把接线现状改准。
2. `[事实]` **"能不能验"取决于配置，不只取决于代码**：`ExportGate` 只共享构建注册
   （`tests/CMakeLists.txt:283-284`，2026-10-06 复核改准）；`InstallConsumerSmoke` 需要 `CPP_PINYIN2_INSTALL`
   （`:271`）；`BindingGates`/`LayerIncludes`/`DocClaimsSelfTest` 需要 Python3（`:421-422`，
   缺则 `FATAL_ERROR` `:468-470`）；`MinimalUsage` 只在 examples 打开时存在
   （`examples/CMakeLists.txt:48-59`）。⇒ 引用任何一个"门禁在"的说法，必须同时说清是**哪套配置**。
3. `[事实]` **每条门禁几乎都带自证**：`ExportGateSelfTest`（`tests/CMakeLists.txt:292`）、
   `DialectSetGateSelfTest`（`:244`）、`BindingGatesSelfTest`（`:462`）、`InstallConsumerSmoke*SelfTest`
   （`:278,295`）、`LoadCheckSelfTest`（`:383`）、`CorpusGateSelfTest`（`:134`）、
   `DumpEquivalenceSelfTest`（`tools/CMakeLists.txt:56`）。
   **2026-10-27 起这条不再只是推断，而是规定**（原文写的是"惯例推断，不是文档规定"）：
   任何新增或修复的**守卫分支**——拒绝型 `return false`、`Code::Load` / `Code::NotReady` / `Code::Internal`、
   边界与上界检查——必须同时留下**负对照**，即"把守卫删掉或改回旧值，某个测试必须变红"的可复现证据，
   并且该证据要能被别人照做（写进提交信息或紧邻的注释）。

   **为什么从"惯例"升级为"规定"**：2026-10-27 的源码逐模块审读（审计报告 §8.17）在四处发现"守卫在、
   证据无"，其中**两处是此前修过的缺陷，只剩注释记得**——词组桶的 8 位计数上界（`src/dict/PhraseIndex.cpp`，
   守门曾误写成 `0xFFFF`）、词典清单里 `Engine::open` 够不到的两条守门（`src/dict/DictManifest.cpp`）。
   另外两处是：`Code::Internal` 从未被任何测试触发过（唯一生产点是 `catch (...)`），以及移动后状态的
   `loadTraditionalDict` 在七个入口里唯一没有断言。⇒ **"修过"与"钉住"是两件事**：只靠注释记录的修复，
   下一次重构就会悄悄复活。

   **负对照的三种可接受形态**：① 临时改坏守卫、跑测试看它红、再还原（本项目最先用的形态，见
   `tools/check_doc_claims.py` 的 `gate-total` 轴）；② 新增一个**专用自检条目**，用与生产不同的输入打靶
   （如 `DictSelfCheck` 直接调 `checkDictManifest`，因为公开 API 到不了那两条守门）；③ 让守卫的**两侧边界**
   都进测试（正好 255 必须过、再多一个必须拒），因为只测拒绝一侧时，索引提前拒绝也会让测试"通过"。

4. `[事实]` **"可执行的稳定性断言"与"文档里的稳定性陈述"之间的那道差，成文时被消掉了一条**：
   旧台账里的 L6 行原把"多实例/线程用例进 CTest 并计入真实测试数"记为**未完成**；
   2026-10-04 补了 `ThreadSafety` / `InstanceIsolation` 两条独立条目（`tests/CMakeLists.txt:120-123`）
   ⇒ 这两组用例现在**既是独立条目、又计入真实测试数**，"按 case 计还是按条目计"的口径争议随之消失
   （注册表的那一对本来就是独立条目：`:426,431`）。
5. `[事实]` **多个"稳定性"相关事实只以文字记录存在，而那批记录已不在树里**：
   旧台账（L1–L6 台账、D2/D3 决策、AC 逐条核验）在 2026-10-22 随过程记录一并删除（取回命令见本文开头）；
   它 2026-10-09 起曾被跟踪，此前是未跟踪文件。因此引用这类内容时必须写明"来源是过程记录"，
   不能当作入库权威。
6. `[事实]` **补充门禁（互补，不是替代）：断言开启构建也要跑一遍套件**（2026-10-06 在 HEAD `0d883e7` 实测）。
   本仓库**既有门禁全部在 Release 下跑**，`NDEBUG` 把 `assert` 编掉 ⇒ "不变量被违反"这一类缺陷对它们
   **完全隐形**（这正是当年"打包进去的 DLL 一 abort、而门禁全绿"的成因）。补跑办法只是换一个
   `CMAKE_BUILD_TYPE`：

   ```
   cmake -S . -B <builddir> -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCPP_PINYIN2_BUILD_TESTS=ON
   cmake --build <builddir>
   ctest --test-dir <builddir> -LE slow        # 本次实测：100% tests passed, 0 tests failed out of 28
   ```

   - **必须显式给 `-DCMAKE_BUILD_TYPE`**：本仓库没有默认值，不给会落进未定义配置（活断言/优化等级
     混搭），读数不再可比。
   - **它替代不了 Release 口径**：性能数字、以及只在共享构建注册的 `ExportGate` 等，仍按 Release 取；
     两者互补。
   - **它不是新注册的 CTest 用例**：`tools/CMakeLists.txt` 的冻结常量一字未动，这条以**文档化配方**
     （而不是新门禁）的形式固化。
7. `[事实]` **C# 绑定侧的门禁路径**：本机 `dotnet test` **必然在启动阶段中止**
   （`System.ComponentModel.Win32Exception (5): 拒绝访问 … SetParentProcessExitCallback`；成因未定位，
   反向证据是 CI runner 上能过）。**现行权威跑法**是仓库自带 runner：

   ```
   python csharp/tools/run_tests.py --native-dir <build树>/bin      # 绝对路径；相对路径本机实测假红
   ```

   `[事实]` 测试工程在缺原生库时**故意失败而不是跳过**（`csharp/CppPinyin.Tests/CppPinyin.Tests.csproj:42`）
   ⇒ "没库也能跑绿"这条路不存在。该缺陷的起因、三条对照与 runner 的负例自证**已登记在
   `docs/DECISIONS.md` §2.1 的 V30 与 V43**（本条只写指针，不复写其结论与数字）。
   `[事实]` **2026-10-06 起这条腿接进了交付前清单**：本机 checklist 的第 **7/7** 步跑上表那条命令
   （`--native-dir` 传的是步 1 构建树的**绝对**路径），且**在场即必过**——`dotnet` 不在 PATH、或原生库
   不存在，都直接判失败，**不接受"跳过"**：此前这条腿只被 `.github/workflows/test_csharp.yml` 驱动，
   而那条 workflow 要 push 才生效 ⇒ 本地门禁根本不覆盖它（会自己跳过的那条腿会静默腐烂）。

---

## 6. 成文时未能核实 / 明确留给下一个人的事

1. `[未证实]` CI（尤其 `linux-sanitizers`）**是否真的在 GitHub 上跑过、结果如何**（§1.2）。 〔**2026-10-27 更新：这一问已有答案** —— §0.1 记 `test_runner.yml` 于 2026-10-23 全绿（19 个 job，运行 `39877724768`）；仍没跑过的是 `release_prep.yml`，见 §0.1 第二段。下面"查不了 Actions 历史"是成文时的限制。〕
   成文时禁止联网，无法查 Actions 历史。
2. `[未证实]` 开发板（firefly.local / 内核 4.4）上 ASan 失败的确切原因 ——
   旧台账自己就把"内核 4.4 拿不到影子内存预留"标为**推断、未证实**；成文时未连板。
3. `[未证实]` 旧台账里的 `L1-D2`（"待拍板"）与该台账 2026-10-09 那份清除记录里的"保持现状"哪个是当前裁定
   —— 两处都是文字记录，且后者当时未跟踪。**本文只记录分歧**。
4. `[未核实]` 旧台账里引用的 HEAD（该台账 2026-10-09 已清除，取回命令见本文开头；它引的提交号在本仓历史里**已不可解析**）与成文时实测
   HEAD（那个提交号同样已不可解析）不同 ⇒ 该台账的**行号与提交号已过期**；本文所有行号均由成文时重读得来，未转抄它。
5. `[未核实]` 工作区里被修改的 tracked 文件与本文引用的行号可能处于"已改未提交"状态；本文读到的是
   **磁盘上的当前内容**，不是某个已提交版本的内容。
   `[事实]` 本条不是理论问题：**起草期间工作区就变过一次** —— 开始时 `git status` 显示
   旧台账、`docs/PERFORMANCE.md`、`tools/CMakeLists.txt`、板上夹具的 README（本机保留）、
   `board_baseline.sh`（本机保留）5 个文件被修改；那些改动随后被提交（那次提交的哈希在本仓历史里已不可解析）；
   `tools/CMakeLists.txt` 虽被重写过（mtime 晚于本文引用它的时间），**内容与行号逐行复核未变**
   （仍 67 行，`:47,53,56,57` 同前）。⇒ 引用本文的提交号是必需的（成文时它指向的那棵树），本文描述的是那一刻磁盘上的状态。
6. ~~`[未核实]` 本机当前是否真的装着一个没有 libasan 的 MinGW（`CMakeLists.txt:190-191` 的记录）；
   成文时禁止构建，无法实测本地 sanitizer 路径。~~
   **2026-10-06 更新（部分已实测）**：本地 sanitizer 路径**已在本机跑通一次**（clang/MSVC 目标窄路，
   配方与数字的唯一处见 §1.1）⇒ 本条原来那句"本机不可验"**不再成立**；但 MinGW 那条路**成文时未重测**，
   而且"本机跑通"**不证明** §6 第 1 条（CI 腿）与第 2 条（板）任何事。**仍未验证**：Debug 档 10 红的
   **机制**（§1.1 里标 `[推断]`）、Windows 无 LeakSanitizer 这一维。
