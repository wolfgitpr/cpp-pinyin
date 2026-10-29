# 发布执行手册（Runbook）—— 三个渠道，一步一条命令

**这份文件管"怎么发"，`docs/RELEASE_CHECKLIST.md` 管"发之前验收什么"。** 两者分工是 2026-10-21 拆出来的：
清单越写越厚（验收、证据、历史读数），发版时真正要照着敲的那几步却被埋在中间。

**三条硬纪律**（与 `RELEASE_CHECKLIST.md` 6.7.12 一致，不是这里的发明）：

1. **发布动作永不自动化。** 没有任何 workflow 会 `twine upload` 或 `dotnet nuget push`；`release_prep.yml`
   只构建、验收、上传 artifact。上传是人的动作，本文件只写命令。
2. **禁止 push 到远端仓库**（本项目的既定纪律），所以"推 tag"这一步由你执行，本文件不代劳。
3. **版本号唯一真相源**是根 `CMakeLists.txt` 的 `project(... VERSION ...)`；别处都是投影，发版前先让
   `python tools/check_version_tokens.py` 自己说 PASS。

---

## 0. 三条渠道共用的前置

| 步骤 | 命令 | 判据 |
|---|---|---|
| 0.1 要发的这棵树跑过 CI | `test_runner` 全绿 + `workflow_dispatch` 一次 `release_prep` | `RELEASE_CHECKLIST.md` §0b 的三条：**套件链已跑过；`release_prep` 仍未跑** |
| 0.2 版本链一致 | `python tools/check_version_tokens.py` | PASS（0 finding） |
| 0.3 三腿构建与全量测试 | `cmake --preset default\|msvc\|clang` → `--build` → `ctest` | 各自 49/49 |
| 0.4 门禁全家桶 | `python tools/run_all_gates.py --with-build` | 20/20，含各自 self-test |

### 0.5 `release_prep` 的第一次 dispatch：填什么、会出什么、哪里是首次见分晓

`release_prep.yml` 只有 `workflow_dispatch` 一个触发口，`inputs` 只有一个 `tag`。第一次跑之前照这张表核一遍。

**填什么**

| 输入 | 填法 | 不填的后果 |
|---|---|---|
| `tag` | 要发的 tag，`v2.0.0` 与 `2.0.0` 都接受（`check_bindings.py --tag` 两种写法都认）。**2026-10-27 起是必填项**，留空 GitHub 直接不让你发起 | 现在**不填发不起来**。改成必填之前，留空会跳过版本链第 1b 位（tag == `CMakeLists.txt` 的 `PROJECT_VERSION`），对 Windows 轮子与每个 nupkg 都生效；而产物**字节不变**——tag 从不写进任何包，只用来比对——所以空跑会产出一批"能用、但恰好没验过这次发布最该验的那一项"的工件 |

**会出七个 artifact**（名字就是 `download-artifact` 拿到的名字，发布时按这个对）

| artifact | 来源 job | 在这条流水线里已经被验过什么 |
|---|---|---|
| `wheel-linux-x86_64` / `wheel-linux-aarch64` | `linux`（manylinux 容器内） | `build_native` → 外壳套件 → wheel → `check_wheel` 成员集 → C ABI/头/方言契约 → 无编译器无索引装机 → auditwheel 打 manylinux 标 → 再验一次 stamped 副本 |
| `wheel-windows-win_amd64` | `windows` | 同上，只是不打 manylinux 标 |
| `nupkg-win-x64` / `nupkg-linux-x64` / `nupkg-osx-arm64` | `csharp`（3 腿矩阵） | 每腿各自 `dotnet pack` 后**先单独过门禁**（`check_bindings.py --nupkg --rid <rid>`），坏在谁的腿上就红在谁的名字上 |
| `nupkg-pooled` | `csharp_pool` | 先证明合并工具的"拒绝"真的会响（`--self-test`），再合并，再用**不带 `--rid`**的形式过门禁 |

**三处"第一次跑才有答案"**（都在本会话里新写或新接线，之前零执行记录）

1. `python/tools/ci_manylinux_aarch64.sh` **步骤 10**（2026-10-27 新增）：静态构建 + `pkg-config --static` 实链，
   断言 `Libs.private` 真的贡献了 `-ldl`。在此之前这条修复**没有任何门禁验证过**。
   **2026-10-27 本地预演**：把步骤 10 的 configure / build / install 三条命令原样在本机跑了一遍
   （`-DBUILD_SHARED_LIBS=OFF -DCPP_PINYIN2_INSTALL=ON`）——configure 0、build 0（25 个目标、
   `libcpp-pinyin2.a`）、install 0；生成的 `.pc` 带 `Cflags: … -DCPP_PINYIN2_STATIC` 与 `Libs:`，而
   `Libs.private:` 在本机**为空**（Windows 上 `CMAKE_DL_LIBS` 就是空，符合设计）；再用这份安装树链接一个
   C 消费者并**运行**，打印 `2.0.0`。⇒ 容器里仍未验的只剩两件：**`pkg-config` 能否读到这份 `.pc`**、以及
   **Linux 上 `Libs.private` 是否真的吐出 `-ldl`**。
2. `ci.yml` 的 macOS 两步（2026-10-27 新增）：构建 macOS wheel 并交给 `ci_macos_verify.sh`。**注意**：这两步在
   `ci.yml` 里，不在 `release_prep` 里——所以 dispatch 一次 `release_prep` **不会**覆盖 macOS 二进制面；
   要覆盖得让 `test_runner` 在同一个 tag 上跑一轮。
3. 池化包的**跨机器合并**：三个单 RID 包由三台不同 runner 产出，这是第一次真合并（此前的证据只有"同一台机器
   打的两个包"）。**合并要求 `lib/`、`ref/`、`contentFiles/`、`README.md`、nuspec 在三个包里逐字节相同**。
   2026-10-27 本机实测：同一 commit 连打两次**逐字节相同**（构建是确定性的）；commit 夹在两次打包之间会让四个
   托管程序集共 142 字节不同（commit 被写进了程序集）。⇒ 三条腿共享同一 commit，剩下的变量是 **SDK patch**；
   若某条腿解析到不同 patch，`csharp_pool` 会**响亮失败**并点名不一致的成员，不会静默出错。

**时间预算**：`linux` 90 分钟/arch、`windows` 60、`csharp` 60/leg、`csharp_pool` 30。四类 job 并行，
`csharp_pool` 要等三条 `csharp` 腿。

---

## 1. PyPI

命令块与 `RELEASE_CHECKLIST.md` §2 **逐字相同**（那份是从 CI 的 `release_prep.yml` / `test_python.yml` 抄来的，
本机跑过）。这里不另写一套"更好看"的变体——两页两套配方正是本轮修掉的病。

```bash
# cwd = 仓库根；下面每一行都从仓库根开始（没有任何一行的正确性依赖上一行 cd 到哪了）
python python/tools/build_native.py --build-dir build-python --config Release
python -m pytest python/tests -q
python -m pip wheel ./python --no-build-isolation --no-deps -w python/dist
python python/tools/check_wheel.py python/dist/*.whl
python tools/check_bindings.py --wheel python/dist/*.whl --tag "$TAG"
python python/tools/smoke_installed.py --wheel python/dist/*.whl
```

| 步骤 | 判据 | 本机状态 |
|---|---|---|
| 装配 + 套件 | `pytest` 全绿 | 已实测 |
| 构建轮子 | 产出 `cpp_pinyin2-<ver>-py3-none-<platform>.whl` | 已实测 |
| 成员集与许可证材料 | `check_wheel.py` PASS，且 wheel 内有 `dist-info/LICENSE` 与 `dist-info/THIRD_PARTY_NOTICES.md` | 已实测（2026-10-21 起断言） |
| 绑定面 | `check_bindings.py --wheel` PASS | 已实测 |
| 干净装机 | `smoke_installed.py` 输出 SMOKE OK（`--no-index --no-deps` 的 venv） | 已实测 |
| 上架前自查 | `python -m twine check dist/*.whl` | **2026-10-27 首次跑过**：`twine 7.0.0`，`cpp_pinyin2-2.0.0-py3-none-win_amd64.whl` ⇒ **PASSED**（此前"本机没装 twine，从未跑过"的记载随之作废） |
| 上传 | `python -m twine upload dist/*.whl` | 人的动作 |

> Linux 轮子要在 manylinux 容器里产出（`python/tools/ci_manylinux_aarch64.sh` 是板上那套配方）。**macOS 轮子 CI 每轮都在造**（`test_python.yml` 的 macos 腿会构建它、做成员集与绑定契约检查、再装进无索引无编译器的 venv 冒烟），但 `release_prep` **故意不产出** macOS 轮子，而且那个轮子的 Mach-O 面（`lipo`/`@rpath`）没有门禁 ⇒ **别把 CI 工件当发布物件**，也**别在发版当天现造**。

> **Linux 渠道两个坑**（2026-10-27 静态审 `release_prep.yml` 时发现；两条都是文档缺口，不是代码缺陷）：
>
> 1. **上传的是哪个轮子**：linux job 的工件里有**两个**轮子——`python/dist/` 里 auditwheel 处理前那个（标签
>    `linux_aarch64` / `linux_x86_64`）和 `python/repaired/` 里盖章后的（`manylinux_2_28_*`）。**要传 `repaired/`
>    里那个**；`linux_*` 不是 PyPI 接受的平台标签，照第 1 节那句 `twine upload dist/*.whl` 原样敲会传错文件。
> 2. **Linux 轮子不做标签核对**：容器命令 `bash python/tools/ci_manylinux_aarch64.sh` 没有接收 `inputs.tag`，
>    脚本里的 `check_bindings.py --wheel` 也不带 `--tag` ⇒ 版本链第 1b 位（标签 == `CMakeLists.txt` 版本）
>    在 Linux 轮子上**不核对**；核对它的是 Windows 轮子与每个 nupkg。同一次 dispatch 里版本串只有一个，
>    所以这个取舍是有意的——但别以为"三个渠道都核过了"。

## 2. NuGet

| 步骤 | 命令 | 判据 |
|---|---|---|
| 2.1 原生库 + 版本信息 | `python csharp/tools/emit_build_info.py --native <built lib> --commit <sha>` | 写出 `version.props` / `BuildInfo.g.cs`（生成物，不入库） |
| 2.2 单 RID 打包 | `dotnet pack csharp/CppPinyin/CppPinyin.csproj -c Release -p:CppPinyinNativeDir=<dir> -p:CppPinyinRid=win-x64 -o dist/win-x64` | 产出 `CppPinyin.<ver>.nupkg` |
| 2.3 逐腿验收 | `python tools/check_bindings.py --nupkg dist/win-x64/*.nupkg --tag v<ver> --rid win-x64` | PASS（含 `lib/`+`ref/`、词典、`THIRD_PARTY_NOTICES.md`） |
| 2.4 合池 | `python csharp/tools/merge_nupkgs.py --self-test` → 合成 → `check_bindings.py --nupkg`（**不带 `--rid`**） | PASS |
| 2.5 上传 | `dotnet nuget push dist/pooled/CppPinyin.<ver>.nupkg --source https://api.nuget.org/v3/index.json --api-key <key>` | 人的动作 |

> 三个 RID（`win-x64` / `linux-x64` / `osx-arm64`）的包由 CI 的 `release_prep.yml` 产出；**本机只产过 `win-x64`**，另外两个腿一次没跑过。

## 3. vcpkg（**不是渠道**，本节为空，保留编号以免下游引用错位）

**这一节现在是空的，因为仓库不提供 vcpkg 端口**（2026-10-23 移除：仓库里没有 portfile，registry 里也没有
`cpp-pinyin2`）。发版当天**没有 vcpkg 相关的动作**。若将来要把它作为一个新渠道加回来，那是一个新决定，不是
把这一节的旧步骤恢复即可——旧步骤的第一条就是"tag 推上去之后才能算真哈希"，而当时那个 `SHA512 0` 占位是
vcpkg 的"接受任意哈希"，端口在那个状态下是不能发布的。

## 4. pkg-config / CMake 消费面

没有独立的发布动作——它随 `cmake --install` 一起落地：

| 步骤 | 命令 | 判据 |
|---|---|---|
| 4.1 安装到临时前缀 | `cmake --install .tmp/build-default --prefix .tmp/prefix` | 生成 `lib/pkgconfig/cpp-pinyin2.pc` |
| 4.2 读 `.pc` | `$env:PKG_CONFIG_PATH=".tmp/prefix/lib/pkgconfig"; pkg-config --cflags --libs cpp-pinyin2` | 输出含 `-I…/include`、`-L…/lib`、`-lcpp-pinyin2` |
| 4.3 消费者冒烟 | `ctest --preset default -R InstallConsumerSmoke` | PASS（这条 ctest 每次构建都在跑） |

---

## 5. 发版之后

| 步骤 | 说明 |
|---|---|
| 5.1 `CHANGELOG.md` 的发布标题 | 已发布版本必须写 `## [x.y.z] - YYYY-MM-DD`（`check_version_tokens` 的第 11 轴会认这个形态） |
| 5.2 文档回填 | 把本次的实测读数写进 `RELEASE_CHECKLIST.md` 的"本机实测"块，**不覆盖旧读数**，按日期并列 |
| 5.3 台账 | 发布当天的实际结果回填到 `docs/RELEASE_CHECKLIST.md` 的对应行 |

---

## 6. 这份手册自己怎么被验证

- 每条命令都在本机跑过至少一次（含 §1.6 的 `twine check`，2026-10-27 实测 PASSED）；
- 表里出现的数字要能自己核对：`49/49` 指 `ctest` 的条目数，`22/22` 指 `run_all_gates` 的**门禁条数**。
  门禁条数自 2026-10-27 起也在 `check_doc_claims` 的核对范围内（`gate-total` 轴），但它**只认
  `all N gates pass` 这一种写法**，写成 `N/N` 的认不出 ⇒ 写门禁条数时用前一种措辞。

