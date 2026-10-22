# 发布清单（发布一版的唯一落点）

**这是「要发一版时该做什么」的唯一落点**——原在 `docs/DECISIONS.md` §2.1，
2026-10-19 抽出成独立文件：它是**动作清单**（逐条勾选），而 PLUGIN 那份是**方案与台账**。放在一起
的后果是可查性——1243 行的台账里翻 178 行清单，谁也不会每次发版都去翻。

**发布动作永不自动化**（原 §6.7.12 的裁定仍在）：下面每一条都要有人真的做，并留下读数。

章节号 `6.7.11` 原样保留，旧引用改指本文档即可。

---

### 6.7.11 发布清单（2026-10-09 成文；**发布动作永不自动化**，见 6.7.12）

本节是"要发一版时该做什么"的唯一落点。**它不是流程建议，而是一串可核对的判据**；每条都指到已存在的门禁或已实测的读数。

**0. 前提：三件事不是本项目能单方面完成的**（dispatcher、仓库设置、商店账号），本节只把它们列成入口，不假装已就绪。

**0b. 发版前置（2026-10-20 新增）：CI 必须先在 GitHub 上真跑过一次。** 本仓的 `.github/workflows/` 至今
**一次都没有真跑过**（`ci.yml` / `release_prep.yml` 里多处自注 `UNVERIFIED`；本清单 §3 与 §6 的多行读数标着
"CI 未跑"）。在那之前，凡"CI 会拦住"的说法都只是**配置层推断**，不是证据。⇒ 发版动作的第 0 步是**先完整
跑一轮 CI 并把结论写回本节**，再开始下面任何一条。

**1. 版本号：唯一真相源是 `CMakeLists.txt` 的 `project(cpp-pinyin2 VERSION x.y.z)`**，其余**六处**必须是它的投影
（本节此前在同一屏里并存「其余五处」「版本链六处」两种数法；2026-10-20 统一为**七处** = ①–⑦，因为 ⑦ 是后加的一处，不在原来那六处之内）：

| # | 位置 | 由什么守住 |
|---|---|---|
| ① | `cppp2_version()`（C ABI 运行时） | `python/tests/test_contract.py::test_version_chain_three_of_six` 三方比对 |
| ② | `python/cpp_pinyin2/_version.py`（生成物） | 同上（`build_native.py` 从 CMakeLists 解析，解析不到即 `SystemExit`） |
| ③ | C# `version.props` | 同上门禁 + `tools/check_bindings.py` |
| ④ | wheel `METADATA` 的 `Version:` | `tools/check_bindings.py --wheel`（`check_wheel.py` 另行断言 `Requires-Python: >=3.9`） |
| ⑤ | `nuspec` | `tools/check_bindings.py --nupkg` |
| ⑥ | **git tag** | `tools/check_bindings.py --tag <tag>`（裸 `x.y.z` 与 `vx.y.z` 都接受；不符即红） |
| ⑦ | **CHANGELOG.md 的版本标题** | **有门禁**（2026-10-20 起）：`tools/check_version_tokens.py` 的第十轴比对该文件**最新**一个 `## ...` 标题里的版本与 package 版本；历史小节按记录保留、不改写，门禁只看第一个标题。**同一条的第二个判断（2026-10-20 新增，审计 H49）**：**打了 tag 之后**，那个标题必须是**已发布形态** `## [x.y.z] — YYYY-MM-DD`——此前 `[Unreleased] — x.y.z` 与已发布形态**都算绿**，于是"发布日只改标题"这一步无门禁可证已做 |

**2. 发布前的构建与验收（全是已有命令，没有新动作）**

# Python：装配 → 套件 → wheel → 成员集 → C ABI/头/方言契约 → 无编译器无索引装机
```bash
# cwd = 仓库根；下面每一行都从仓库根开始（没有任何一行的正确性依赖上一行 cd 到哪了）
python python/tools/build_native.py --build-dir build-python --config Release
python -m pytest python/tests -q
python -m pip wheel ./python --no-build-isolation --no-deps -w python/dist
python python/tools/check_wheel.py python/dist/*.whl
python tools/check_bindings.py --wheel python/dist/*.whl --tag "$TAG"
python python/tools/smoke_installed.py --wheel python/dist/*.whl
```

Linux 两条腿**必须在各自架构的 manylinux 容器内**跑同一条脚本（架构由 `uname -m` 自判，禁用 QEMU 仿真）：
`python/tools/ci_manylinux_aarch64.sh` —— 它把"我们自己盖的 `linux_<arch>`"与"`auditwheel` 盖章后的
`manylinux_2_28_<arch>`"**两份都留下**，因为只有后者可以声称 manylinux（6.5 第 1b 条）。

**3. 待 CI 首次运行才成立的两条（如实标注，不得写成"已支持"）**

- **runner 接线**：`.github/workflows/release_prep.yml` 的两个 Linux job 与 Windows job **一次都还没在 GitHub 上跑过**；标签、镜像拉取、runner 的 Docker 都未验证。命令本身有实测出处：Linux 侧是板测（ledger V110），Windows 侧是 2026-10-09 从 （压缩前提交，克隆内不可达） 重建（见下节读数）。
- **裸 `check_wheel.py` 今天没有 macOS 那一半**（2026-10-09 实测档位）：非 Linux 轮子上它**只留一句 note**——"no ELF shared objects in this wheel, so the glibc surface was not measured"（Windows 轮子上实测如此），而 PE/DLL 成员按"原生库成员但非 ELF/Mach-O，故未测动态面"记；**Mach-O 成员根本不会被解析**。要补这一半，前置是四项、且都是未验证面：① 一个走遍 `LC_LOAD_DYLIB`/`LC_RPATH` 的 Mach-O 读取器（薄二进制与 `FAT_MAGIC` 通用二进制两种形态）；② 6.7.10 那两行 macOS 设置（`MACOSX_DEPLOYMENT_TARGET` / `-arch`）；③ 一条"装载时不依赖 libstdc++（macOS 只有 libc++）+ 必须有 `@loader_path` 相对 rpath"的判据；④ 一台能跑它的 macOS 机器（6.7.9 记"无机器、无 osxcross"）。**没有第 ④ 项，前三项只能写成未验证**——本轮试过写①②③，在本机既跑不了真 dylib 又只能靠合成夹具自证，故**整体未入库**，只把踩到的形态学坑留在这里见 3d。

**3c. macOS 的部署目标与验收脚本已进树（2026-10-09）**：部分回应上一条列的第 ②③ 项——① `CMakeLists.txt`
的 `if (APPLE)` 分支把 `CMAKE_OSX_DEPLOYMENT_TARGET` 定为 **10.15**（libc++ 自 10.15 才有 `<filesystem>`，
`std::filesystem` 在 10.13 只有 `<experimental/filesystem>`），对更低值**硬拒**（`FATAL_ERROR` 而非警告），
命令行 `-D` 仍可抬高；放在树里而非某个 workflow，是为了让 Python 轮子 / C# 包 / 下游 `find_package` 拿到
**同一个答案**（同一数值多处写，正是本项目禁止的那类漂移）。② `python/tools/ci_macos_verify.sh` 在 Mac 上对
一个 macOS 轮子问五件事：`lipo -archs` 是否与轮子标签一致、`LC_LOAD_DYLIB` 里**不许有 Homebrew/`/usr/local`
路径也不许出现 libstdc++**、install name 是否 `@rpath` 形、`LC_RPATH` 是否含 `@loader_path` 自相对项、
以及无索引无编译器的 venv 装机 + `smoke_installed.py`（附 `check_wheel.py` 成员集比对）。
**仍缺第 ④ 项（一台 Mac），所以这两项都还没在 macOS 上跑过一次**：CMake 分支只在本机验了"非 Apple 路径
不受影响"（Windows 上 `--fresh` 重配置 + 全量重建跑通；用例数随配置变化的原因在 `examples/CMakeLists.txt:48`，
见 §4），脚本只过了 `sh -n`/`bash -n`。
**第一次上 Mac 才是第一次真凭据**，此句在两份文件、一处脚本头部都写着。

**3d. 一个可复用的教训（2026-10-09，写 Mach-O 读取器时踩的）**：**不要用"合成夹具全绿"当作"读取器正确"的证据**——本轮合成夹具一度在自认正确时也全绿，而在真相被钉住后又全红，来回若干轮，根因从一开始就在**夹具自己**的两条规则上：`cmdsize` 是命令自身的固定长（LC_LOAD_DYLIB 24、LC_RPATH 12），**不含路径字节**；而路径字符串位于**整个命令块之后**，其偏移相对**命令块起点**（即 `sizeofcmds`），**不是**相对该命令起点。把这三件事搞混的读取器会读到命令头被当成路径（实测表现为 `dependencies [\x1c]`）。另附本机两条工具坑：**PowerShell 会吞掉命令里 `<IIII` 这类格式字面量**（inline 命令实测把 `len(body)` 从 72 降到 48），凡是要按字节断言的探针，**一律落成仓库内脚本再跑**；以及 `git check-ignore <目录>` 对目录本身返回"未匹配"，**要判目录是否被忽略须给一个目录内文件路径**（`python/repaired/` 实测如此，规则本身无误）。

**3b. 多版本的 CI 半边（2026-10-09 新增，仍待首次运行）**：`test_python.yml` 新增 job `python_versions`
（`needs: python`，矩阵 **cp39 / cp310 / cp311 / cp312 / cp313**）——把 x86_64 那一个 `py3-none-linux_x86_64`
轮子**逐个装进五个解释器**再 `smoke_installed.py`（`--no-index --no-deps`）。在此之前，该文件里每个 job
都只跑一个现代解释器（其头部原话承认过这一点）。**下限已于 2026-10-10 由用户拍板从 3.8 上调为 3.9**
（原话："下限3.9"）：3.8 从未被当作下限实测过（`setup-python` 不再提供它，manylinux 镜像也不带 cp38），
而"声明一个测不了的底线"正是本仓在别处拒绝的形态；现在**声明的下限 == 矩阵里最老的那个解释器**，
即"3.9 起的'一个轮子多解释器'既有声明也有机械化检查点"。改动落在 `python/setup.py` 的 `python_requires`
（唯一真相源），`check_wheel.py` 会解析它并拒绝 METADATA 里写着旧下限的轮子——**实测**：改完拿旧轮子跑，
`check_wheel.py` 当场红（"does not declare 'Requires-Python: >=3.9'"），重建后转绿。

**4-bis. 2026-10-23 本机实测读数（Windows x64，从 `0b4f46b` 重建）**——`2.0.0` 待发布状态的最新一组数：

| 环节 | 读数 |
|---|---|
| `cmake --build .tmp/build-python --config Release` → `build_native.py` | exit 0；staged dll + 中粤各 7 个词典文件；`__commit__ = "0b4f46b"`（等于当轮 tip，见 §"物料必须从要打标签的那棵树构建"） |
| wheel | `cpp_pinyin2-2.0.0-py3-none-win_amd64.whl`，**约 1.11 MB**（1,139,023～1,139,026 字节之间，同一棵树重建也会有几字节的压缩差；权威值看轮子自己的 `RECORD`） |
| nupkg | `CppPinyin.2.0.0.nupkg`，**约 1.25 MB**（1,282,732～1,282,758 字节，同上） |
| `check_wheel.py` | `PASS  every packaged file matches its source byte for byte; nothing missing, nothing extra` |
| `check_bindings.py --wheel` / `--nupkg` | 两个都 `PASS`，且**签名基线 23 条**与头文件一致（`tests/abi/expected_c_api_signatures.v2.txt`，本轮新增） |
| `ctest --preset default` | `100% tests passed, 0 tests failed out of 50` |
| `python -m pytest python/tests` | `125 passed` |
| `csharp/tools/run_tests.py --native-dir <abs>` | `net7.0` 76/76、`net10.0` 76/76 |
| `tools/run_all_gates.py --with-build` | `all 20 gates pass`；`check_bindings --self-test` 77/77 |

> **新的一步（本轮起必需）**：任何改动 `c_api.h` 声明的提交，都要在同一提交里跑
> `python tools/check_bindings.py --emit-signatures` 重新生成签名基线。门禁只认"类型序列"，
> **改参数名不会变红**（那是刻意留的自由），改类型/顺序/个数都会。

**4. 2026-10-09 本机实测读数（Windows x64，从 `50be016`（压缩前提交，克隆内不可达） 重建）**——发布链路在本机的证据：

| 环节 | 读数 |
|---|---|
| `build_native.py` | exit 0；staged `libcpp-pinyin.dll`（sha256 `aed598c1fd450057…`）、中粤各 7 个词典文件；`version=2.0.0 commit=50be016` |
| wheel | `cpp_pinyin2-2.0.0-py3-none-win_amd64.whl`，**1,094,773 字节**〔2026-10-21 更正：包名一直是 `cpp_pinyin2`（`setup.py` 的 `name=`），原来这里少写了一个 `2`；字节数保持当时读数未动〕 |
| `check_wheel.py` | `PASS  every packaged file matches its source byte for byte; nothing missing, nothing extra` |
| `check_bindings.py --wheel` | `PASS`（插件面 18 声明、导出 19 符号、C# 壳逐字段转写 6 结构 + 19 入口） |
| 干净 venv（`--no-index --no-deps`） | 安装 exit 0；`smoke_installed.py` **SMOKE OK**（`version: 2.0.0`、`dialects: ['cantonese','mandarin']`、mandarin `ni3 hao3 shi4 jie4`、cantonese `nei5 hou2 sai3 gaai3`） |

**本轮（`if (APPLE)` 分支入库后，Windows x64）的回归读数**——改的是 Apple 专用分支，所以要证的是"非 Apple 路径没被碰坏"：

| 环节 | 读数 |
|---|---|
| `cmake -S . -B .tmp/mk-default --fresh`（Release / Ninja / MinGW / **tests+examples+install 全开**） | exit 0，`Configuring done (4.1s)` / `Generating done (0.1s)` |
| `cmake --build .tmp/mk-default --config Release` | exit 0（全部目标建成） |
| `ctest --test-dir .tmp/mk-default`（**未设 `TEMP`**） | **3 项红**：`CppPinyinTests` / `ThreadSafety` / `InstanceIsolation` |
| 同上，`TEMP`/`TMP` 指到仓内可写目录 | **全部通过**（examples 开启时 50 条） |
| 恢复仓内规范配置（同上但 `-DCPP_PINYIN2_BUILD_EXAMPLES=OFF`）后再跑 | **49/49 通过**；`check_doc_claims --ctest-inventory .tmp/mk-default-inventory.json` **回到绿** |

**用例数必须带配置**：`examples/CMakeLists.txt` 的 `MinimalUsage` 只在 `CPP_PINYIN2_BUILD_EXAMPLES` 与
`CPP_PINYIN2_BUILD_TESTS` **同时**开启时才注册。规范配置是 examples 关闭的 `.tmp/mk-default`（**49 条**）；
examples 开启时是 **50 条**。`tools/check_doc_claims.py` 以 `.tmp/mk-default` 为准，四套配置的实测读数见
`docs/NEW_DIALECT_PLAYBOOK.md` §7 的指纹表 —— 引用这个数而不写明配置，下一次复核就会把"配置差异"读成"门禁漂移"。

**`TEMP`/`TMP` 必须先指到仓内可写目录**，否则 `CppPinyinTests` / `ThreadSafety` / `InstanceIsolation` 三项会红；
设好之后一并转绿。这不是回归，是环境。

**5. 多版本 Python 支持**：本包是 **`py3-none-<platform>`**（ctypes + 非 Limited-API 的 C ABI），
**新增一个 Python 版本的成本是 +0 个 wheel**（6.3 路线 A）。声明的下限是 `Requires-Python: >=3.9`
（**下限 3.9**：manylinux 镜像不带 cp38，`setup-python` 也不再提供 3.8 ⇒ 3.8 只能被"声明"、
从未被当作下限实测过）；
**板上实测口径**：套件在 **cp39 / cp310 / cp311 / cp312 / cp313 各 91 passed**，且同一个 wheel 在这五个
解释器里都能装能冒烟（2026-10-02，见 6.7.9 与 ledger V110）。**该口径自 2026-10-09 起有机械化检查点**：
`test_python.yml` 的 `python_versions` job 逐个装（见 3b）。⇒ **现在"声明的下限"与"矩阵里最老的那个解释器"
是同一个数（3.9）**，声明与证据不再有缝。

**下限现在只有一处权威（2026-10-09 收敛）**：`python/setup.py` 的 `python_requires` —— 它是 setuptools 写进
wheel METADATA 的唯一来源。`python/tools/check_wheel.py` **不再自带那份常量**（原来写的是
`Requires-Python: >=3.8`），改为**解析 `setup.py` 得出期望值**再比对 METADATA（不 import：`setup.py` 一
import 就会执行 `setup()`），并新增两条自检：METADATA 里写别的下限**必须被拒**、树里如实写就**不许报**。
动机是实测过的漂移：第 28 轮把下限由 3.9 降到 3.8 时，这个常量必须**手工跟着改**，而漏改不会有任何提示，
只有"轮子里写着一个没人选过的下限"这种事后才发现。**这次上调当场演示了这条门禁是活的**：改完
`setup.py` 后拿**未重建的旧轮子**跑 `check_wheel.py`，它立刻报
`METADATA does not declare 'Requires-Python: >=3.9'`，重建后转绿。
并继续标注未实测），不是技术缺口 —— **2026-10-10 用户已就此拍板：取"改成可实测的 3.9"那一支**
（见本节 §5 开头）。

**6. 跨平台支撑现在的总账（2026-10-09 收口核对；一行一个可核对的单项）**

| 项 | 状态 | 最硬的凭据 | 权威落点 |
|---|---|---|---|
| Windows x64 轮子 | **已实测** | 2026-10-09 从 `50be016`（压缩前提交，克隆内不可达） 重建：`check_wheel.py` PASS（逐字节）、`check_bindings.py --wheel` PASS、干净 venv `--no-index --no-deps` 装机 + `SMOKE OK`；ctest 32/32 | 本节 §4 |
| Linux x86_64 轮子（`linux_x86_64` + auditwheel 盖章） | **脚本已板测 / CI 未跑** | 同一条脚本的 aarch64 腿在真板上 `RESULT: OK`（含盖章与盖章后复检，ledger V110）；x86_64 腿只换了 `uname -m` | 6.7.12（原 6.7.11 顺延）、`python/tools/ci_manylinux_aarch64.sh` |
| Linux aarch64 轮子 | **板测已实测 / CI 未跑** | 板上 `RESULT: OK`；盖章后标签为压缩标签集（auditwheel 判 2_26 更宽） | 6.7.9 / 6.7.10 行内 |
| Linux 原生构建 + **全量 ctest 50/50**（当时注册 50 条；现为 49 条） | **已实测** | 2026-10-21 在 firefly 板（aarch64 / glibc 2.31 / gcc 9.4 / cmake 3.21.3 / python 3.8.10 / binutils 2.34，快照 `4772b3f`（压缩前提交，克隆内不可达））：configure、build、`ctest` **50/50**、install 全过；README 的前置是词典仓检出在 `../pinyin-makedict` 且先跑一次 `emit_build_info.py` | 提交历史 |
| macOS 轮子 | **未实测（缺机器）** | 只有：部署目标进树（`CMakeLists.txt` 的 `if (APPLE)`，硬拒 < 10.15）、验收脚本进树（`ci_macos_verify.sh`，五项问句）、以及"非 Apple 路径不受影响"的本机回归 | §3c |
| 一个轮子覆盖多解释器 | **板测已实测 + CI 检查点已建（未跑）** | 板上 cp39/cp310/cp311/cp312/cp313 各 91 passed 且同一 wheel 都能装能冒烟；CI 侧 `python_versions` job | §3b、§5 |
| Python 下限这个数字 | **单一真相源已收敛** | `setup.py` 的 `python_requires` 是唯一权威，`check_wheel.py` 解析它并比对 METADATA，两条自检一红一绿（2026-10-09） | §5 末段、`python/tools/check_wheel.py` |
| 版本链六处一致（发版前提） | **①②③机械化为用例，④⑤⑥在 `check_bindings.py`，数据下限在 `check_version_tokens` 的第七轴，⑥ 的 tag 检项待 CI 传参** | `python/tests/test_contract.py::test_version_chain_three_of_six`；`check_bindings.py --self-test 77/77` | 本节第 1 条 |
| 发布动作自动化 | **故意不做** | 只做"预备"（构建 + 验收 + 上传 artifact），不发 PyPI/NuGet | 6.7.12 第三条、`release_prep.yml` 头部 |
| C# / NuGet 侧 | **矩阵工作流已在（3 平台）；合池工具 + 池化 job 均在，本机两个 RID 实跑过，CI 未跑** | `merge_nupkgs.py --self-test` 6/6；同源两包 → 34 成员 / rids `linux-x64`+`win-x64` / `check_bindings.py` PASS（不带 `--rid`） | 第 7 条、`csharp/tools/merge_nupkgs.py`、`release_prep.yml` 的 `csharp`+`csharp_pool` |

**6b. 2026-10-21 的本机复测（补在上面那张 2026-10-09 总账之后，不覆盖它）**

那一版总账写的是当时的读数；下面这一行是**按月重跑**的结果，两者不一致时以新的为准：

| 项 | 2026-10-21 本机实测 | 命令/证据 |
|---|---|---|
| 门禁 | **20/20**（含每条自检） | `python tools/run_all_gates.py --with-build` |
| C++ 测试 | **49/49** | `ctest --preset default` |
| Python 壳 | **124 passed** | `python -m pytest python/tests -q` |
| C# 壳 | **net7.0 76/76 + net10.0 76/76** | `python csharp/tools/run_tests.py --native-dir <bin>` |
| 词典仓 | **24 passed, 1 skipped**；跨仓漂移 **0** | `pytest .` + `tools/check_drift.ps1 -CppPinyinRoot ../cpp-pinyin` |
| 冻结面 | **15 公开头 / 23 C ABI / 18 插件面 / 76 签名** | `tools/check_bindings.py` |
| 产物来源 | wheel 与 nupkg 重建自压缩后的 `v2` 树；**具体提交只写在产物内**的 `python/cpp_pinyin2/_version.py`（`__commit__`） | 文档里不写哈希：写完这一行，`HEAD` 就已经不是它了——历史压缩也会让旧产物里的哈希失效，所以重建与压缩必须连着做 |
| 复验结果 | `check_wheel.py` **PASS**（逐字节）；`check_bindings.py --wheel` **PASS**；`check_bindings.py --nupkg` **PASS**（8+8 词典文件） | 三个命令都在本机跑过 |

> 下表是 **2026-10-21** 的读数，保留为历史；上方 2026-10-23 的块是最新一次实测。

| 项 | 2026-10-21 本机实测 | 命令/证据 |
|---|---|---|
| Windows x64 轮子（从当时的 HEAD 重建） | `check_wheel.py` **PASS**（成员逐字节）、`check_bindings.py --wheel` **PASS**；METADATA 新增 9 条 classifiers 与 5 条 `Project-URL` | `python -m pip wheel . --no-build-isolation --no-deps` ⇒ 见 `python/dist/` |
| 单个 RID `win-x64` 的 nupkg | `check_bindings.py --nupkg --tag v2.0.0` **PASS**；nuspec 新增 `projectUrl`/`repository url`/`tags`/`releaseNotes`；许可证表达式见 上面的包许可证一行；原 vcpkg 那条（同源数据） | `emit_build_info.py` → `dotnet pack -p:CppPinyinRid=win-x64` |
| C++ 编译腿 | MinGW g++ / MSVC 19.51 / Clang 22.1.4 **三条腿各 49/49**（20 门禁全绿） | `cmake --preset default\|msvc\|clang` ⇒ 提交历史 |
| 包的许可证表达式 | 从两项补成三项（`CC-BY-SA-3.0` 是粤语表，之前漏了），并由 `check_bindings.py` 的 `check_package_licence` 与各表 `License.txt` 逐版本比对（2026-10-23 起读 `CppPinyin.csproj` 的 `<PackageLicenseExpression>`——原先读的是已删除的 vcpkg 清单） | 第 8 节 |
**6c. 复跑读数前必须知道的两个假信号（2026-10-21 实测，都是当场踩到的）**

| 现象 | 真因 | 正确做法 |
|---|---|---|
| C# 测试报 `cppp2_apply_tone_sandhi failed with CPPP2_ERR_INTERNAL` | `csharp/tools/run_tests.py` 的**默认** native 目录里是 2026-10-16 的旧 DLL，而源码到 10-21 还在改 | 用新构建：`--native-dir <刚构建的 bin>`；**脚本现在会自己拒绝陈旧库**（比源文件旧即 FAIL 并提示重建），这条检查本身也是本轮加的 |
| `ctest --preset default` 有 4 条失败：`InstallConsumerSmoke`、两条它的自检、`HeaderSelfCheck` | 任何一次 `build_native.py --build-dir .tmp/build-default` 都会把**同一个 cache** 重配成 `BUILD_TESTS=OFF` / `INSTALL=OFF`，安装目标随之消失 | 先 `cmake --preset default` 恢复配置，再 `ctest --preset default`（本轮实测：恢复后 **50/50 全过**） |

**还有一条不是假信号，而是教训**：C# 一致性测试从 **2026-10-20** 起就有 6 条红（守卫更新了、逐例执行器没跟上），
一直没人发现 —— 因为 **CI 从未在 GitHub 上跑过**（§0b）。这类"本机也没人跑"的窗口期，正是 §0b 那条前置存在的理由。

**7. C# / NuGet 侧的发布预备（2026-10-09 补齐口径与工具）**

**7.1 现状（已存在，勿重复造）**：`.github/workflows/test_csharp.yml` 已有 3 平台矩阵
`win-x64`（windows-latest）/ `linux-x64`（ubuntu-latest）/ `osx-arm64`（macos-latest）——各自构建原生库 →
生成 `version.props`/`BuildInfo.g.cs` → `dotnet test`（net7.0 与 net10.0 各 54/54）→
`dotnet pack -p:CppPinyinRid=<该腿的 rid>` → `check_bindings.py --nupkg --rid <该腿的 rid>`

> ⚠️ **`CppPinyinNativeDir` 必须传绝对路径**（2026-10-23 实测）：csproj 里的`$(CppPinyinNativeDir)\*cpp-pinyin2.dll` 是**相对项目文件目录**（`csharp/CppPinyin/`）解析的，传 `python\cpp_pinyin2\_native` 会拼成 `csharp/CppPinyin/python/...`，于是拿到那句 `no cpp-pinyin2 native library in ...` —— 而库其实好好地躺在你指的地方。（带 tag 时另传
`--tag`）→ 上传 artifact。**只有 Windows 腿在开发机上实测过**；Darwin 与 Linux 腿从未运行（文件头部自述）。

**7.2 一个包只带一个 RID —— 这是设计，不是缺陷**：`CppPinyinRid` 故意无默认值
（`CppPinyin.csproj`：猜一个 rid 会产出"装得上、加载不了"的包），而 `check_bindings.py --rid` 会断言
**包内 rid 集合恰等于被问的那一个**（原文："a package is packed per runtime identifier"）。所以发布有两种
口径，必须显式选一种：**发布 N 个单 RID 包**（让消费方自己挑对），或**发布 1 个多 RID 池化包**。

**7.3 池化工具（2026-10-09 新增）**：`csharp/tools/merge_nupkgs.py` —— 把若干个单 RID nupkg 合成一个。
- **它只搬字节**：不改原生库、不改托管程序集、不重写 nuspec（标签纪律："任何人不许对产物做二次修补"）；
- **它拒绝**：共享面（`lib/`、`ref/`、`contentFiles/`、`README.md`、nuspec）字节不一致的输入（那意味着输入
  来自不同构建/版本，合起来会发出两个互相打架的半边）、没有 `runtimes/<rid>/native/` 的输入、同一个 rid 出现
  两次、以及已存在的输出路径（除非 `--force`）；
- **它不重写 `[Content_Types].xml` / `_rels/.rels` / `psmdcp`**：这三个是 OPC 管线文件，直接沿用第一个输入——
  这是有意的"不重写"取向；代价是**这些文件必须逐字节相同**，否则被拒（本工具不猜）。
- **验收要"不给 `--rid`"**：池化包正是那个**必须不被问单一 rid** 的情形（工具结尾会把这条命令打出来）。

**7.4 本机实跑记录（2026-10-09，Windows x64）**：

| 步骤 | 读数 |
|---|---|
| `dotnet pack -p:CppPinyinRid=win-x64`（原生库 = `.tmp/build-pypi/bin/libcpp-pinyin.dll`） | exit 0，产出 `CppPinyin.2.0.0.nupkg` |
| `dotnet pack -p:CppPinyinRid=linux-x64`（**原生文件是把 Windows 库改名成 `.so`，只为验证合包流水线，不是可发布产物**） | exit 0 |
| `merge_nupkgs.py --self-test` | **0 failure**（6 条控制：两次合并成功/两条运行时树都在/`lib`+`contentFiles` 各一次/共享面一致/不一致被报/同 rid 被拒） |
| 用**旧提交**的 win 包 + 新 linux 包合池 | **被正确拒绝**：11 个共享成员"bytes differ"（两个包来自不同构建）——这条比成功更有价值，它证明门是活的 |
| 同源两包合池 | exit 0 → **2,649,574 字节 / 34 成员 / rids = `linux-x64, win-x64`** |
| `check_bindings.py --nupkg <池化包>`（**不给 `--rid`**） | **PASS** |
| 同上但 `--rid win-x64` | **按预期红**（"the package carries native assets for ['linux-x64','win-x64'] …"） |

**7.5 CI 落地（2026-10-10；用户拍板"c#按你建议的来"，即采用池化路线）**：`release_prep.yml` 新增两个 job：

- **`csharp`**（矩阵 `win-x64`/`linux-x64`/`osx-arm64`，三个平台各自原生构建）：跑的是 `test_csharp.yml`
  **同一条 configure 行**（"从套件已经跑过的路径出产物，而不是另造一条会漂移的构建配方"）→
  `emit_build_info.py` → `dotnet pack -p:CppPinyinRid=<该腿>` → **按 RID 逐个过门禁**
  （`check_bindings.py --nupkg --rid <该腿>`，有 tag 时另传 `--tag`）→ 上传 `nupkg-<rid>`。
  **逐腿先过门禁是因为**：一条腿打错了东西，应当以它自己的名字失败，而不是藏进合并后的产物里。
- **`csharp_pool`**（`needs: csharp`）：下载全部 `nupkg-*` → 先跑
  `merge_nupkgs.py --self-test`（证明拒绝面是活的）→ 合成 `dist/pooled/CppPinyin.nupkg` →
  `check_bindings.py --nupkg <池化包>`。**这条门禁故意不带 `--rid`**：池化包正是那个必须不被问单一 rid 的
  情形（7.3）。
- **仍未做（如实）**：这两个 job **一次都没在 GitHub 上跑过**；C# 的 Darwin/Linux 腿在任何地方都没跑过
  （本机只有 Windows），所以"三个 RID 都合得进一个包"目前只有**两个 RID 的本机证据**（7.4：同源两包 →
  34 成员 / `linux-x64` + `win-x64` / 门禁 PASS / 带 `--rid` 时按预期红）。**NuGet 上传同样不动**（6.7.12 第三条）。

**7.6 可复现构建（2026-10-20 新增，审计 round 2 / G13）**：发版前跑一次"同一提交、两个不同目录"的
构建，两个产物的 SHA-256 必须相同：

```powershell
foreach ($d in @('.tmp\repro-a', '.tmp\repro-b')) {
  cmake -S . -B $d -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF `
        -DCPP_PINYIN2_BUILD_TESTS=OFF -DCPP_PINYIN2_INSTALL=OFF
  cmake --build $d --target cpp-pinyin2
}
(Get-FileHash .tmp\repro-a\lib\libcpp-pinyin2.a).Hash -eq (Get-FileHash .tmp\repro-b\lib\libcpp-pinyin2.a).Hash
```

为什么值得单独列一条：这个性质**不是**自动成立的。加护栏之前实测过：两个目录的 `.a` 哈希不同，
而里面 23 个 `.o` **逐字节相同**——不确定性全在打包层（`ar` 给每个成员盖 mtime/uid、`ranlib` 重写符号
索引时盖当前时间、PE 链接器把构建时间写进 DLL 头）。`CMakeLists.txt` 的「Reproducible artefacts」
一段用 `-ffile-prefix-map`、`ar qcD`/`ranlib -D`、`--no-insert-timestamp`（MSVC 是 `/Brepro`）消掉这
三处；**实测结果：共享库与静态库两次构建均逐字节相同**。

**关于 `SOURCE_DATE_EPOCH`**：本库**没有**任何地方读它，也不需要——`git grep '__DATE__\|__TIME__\|__TIMESTAMP__'`
在 `src/` `include/` `tools/` `tests/` 里是 **0 命中**（2026-10-20 实测），所以构建里没有"编译时刻进产物"的
第一方来源；唯一的时间戳来自链接器写进 PE 头的那一个，已被 `--no-insert-timestamp`（MinGW）/ `/Brepro`（MSVC）
消掉。等哪天有人往产物里嵌时间，这条注释就是该被推翻的地方。
**这条没有进 ctest**，如实说明原因：它要两次完整 configure+build（约 40 s 起），而 ctest 的其余条目
都在一次构建之内；挂上去会让"每次 `ctest` 都跑"的成本翻倍，换来的却是一个**只在发版时才有意义**的
结论。~~MSVC 与 macOS 两条腿的 `/Brepro`/`ranlib` 差异在本机**无法验证**（没有 cl.exe、没有 Apple
工具链），首次真实验证在 CI。~~ **〔2026-10-21 更正〕** MSVC 这条腿**已经本机实测**：MSVC 19.51 / VS2026
（`cmake --preset msvc`，Ninja + vcvars64）构建 + 全量 `ctest` **50/50**（当时的条目数），且一次构建的 `D9007` 从 **59 条降为 0**
——原来 `/pathmap` 单独使用会被静默忽略。
**仍无法验证**的只剩 macOS/Apple 工具链那半句，以及「两次构建字节一致」本身（仍只在发版时做）。


---

**8. 四个渠道的就绪状态（2026-10-21 新增：一页齐平，别再靠翻四处）**

写这一节的原因：发版面分散在四个生态系统里，各自的"还差什么"以前只写在各自的文件里，发版时要翻四处才
拼得出来。下表每一行的**本机实测**都是真跑过的，**未实测**的一律写明，不写成"已支持"。

| 渠道 | 产物（本机实测的那个） | 本机实测 | 发版还差什么 | 阻塞 |
|---|---|---|---|---|
| **PyPI** | `python/dist/cpp_pinyin2-2.0.0-py3-none-win_amd64.whl`（2026-10-23 重建自压缩后的 `v2` 树） | `python/tools/check_wheel.py` **PASS**（成员逐字节）；`check_bindings.py --wheel` **PASS**；`Requires-Python: >=3.9`；classifiers 与 5 条 project_urls；许可证表达式改为三项，`THIRD_PARTY_NOTICES.md` 随包进 `<dist-info>/`（2026-10-21 补） | manylinux / macOS 轮子（要 CI）；`twine check` **本机没跑**（本机没装 `twine`）；上传是**人的动作** | CI 首次运行；上传按 6.7.12 第三条 |
| **NuGet** | `.tmp/pkgs/CppPinyin.2.0.0.nupkg`（单 RID `win-x64`，本机打；2026-10-23 重建自压缩后的 `v2` 树） | `check_bindings.py --nupkg` **PASS**（含 8+8 词典文件、`runtimes/win-x64/native`、三 TFM 的 `lib/` 与 `ref/`）；nuspec 的 `projectUrl`/`repository`/`tags`/`releaseNotes`；三项许可证表达式 + 随包 `THIRD_PARTY_NOTICES.md`（2026-10-21 补） | 另外两个 RID 的包与池化（要 CI 的三个平台腿）；上传是人的动作 | CI 首次运行（7.5） |
| **pkg-config** | 安装树里的 `lib/pkgconfig/cpp-pinyin2.pc` | 2026-10-21 读安装树逐字段核对：`prefix` 相对、`Version: 2.0.0`、`Cflags`/`Libs`/`dictdir` 齐全；由 `InstallConsumerSmoke` 这条 ctest 每次构建都验 | 无（随 CMake install 一起发） | 无 |

三个渠道各自的**发版命令**在 [`RELEASE_RUNBOOK.md`](./RELEASE_RUNBOOK.md)——这份清单管"发之前验收什么"，那份管"发布当天怎么敲"。vcpkg 不是渠道：仓库不提供端口（2026-10-23 移除）。

三个渠道共用的两条纪律：**发布动作永不自动化**（6.7.12），**版本号唯一真相源是 `CMakeLists.txt`**（第 1 条）。
渠道越多，"某一处写着旧版本"的机会越多——这正是 `tools/check_version_tokens.py` 与
`tools/check_bindings.py` 的发布面一致性检查存在的原因。
