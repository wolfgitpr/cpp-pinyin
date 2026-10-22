# 发布执行手册（Runbook）—— 四个渠道，一步一条命令

**这份文件管"怎么发"，`docs/RELEASE_CHECKLIST.md` 管"发之前验收什么"。** 两者分工是 2026-10-21 拆出来的：
清单越写越厚（验收、证据、历史读数），发版时真正要照着敲的那几步却被埋在中间。

**三条硬纪律**（与 `RELEASE_CHECKLIST.md` 6.7.12 一致，不是这里的发明）：

1. **发布动作永不自动化。** 没有任何 workflow 会 `twine upload` 或 `dotnet nuget push`；`release_prep.yml`
   只构建、验收、上传 artifact。上传是人的动作，本文件只写命令。
2. **禁止 push 到远端仓库**（本项目的既定纪律），所以"推 tag"这一步由你执行，本文件不代劳。
3. **版本号唯一真相源**是根 `CMakeLists.txt` 的 `project(... VERSION ...)`；别处都是投影，发版前先让
   `python tools/check_version_tokens.py` 自己说 PASS。

---

## 0. 四条渠道共用的前置

| 步骤 | 命令 | 判据 |
|---|---|---|
| 0.1 CI 至少在 GitHub 上真跑过一次 | 触发任一 workflow | `RELEASE_CHECKLIST.md` §0b；**今天仍是未完成项** |
| 0.2 版本链一致 | `python tools/check_version_tokens.py` | PASS（0 finding） |
| 0.3 三腿构建与全量测试 | `cmake --preset default\|msvc\|clang` → `--build` → `ctest` | 各自 49/49 |
| 0.4 门禁全家桶 | `python tools/run_all_gates.py --with-build` | 20/20，含各自 self-test |

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
| 上架前自查 | `python -m twine check dist/*.whl` | **本机没装 twine，从未跑过**；要 `pip install twine`（联网）后才算验过 |
| 上传 | `python -m twine upload dist/*.whl` | 人的动作 |

> Linux 轮子要在 manylinux 容器里产出（`python/tools/ci_manylinux_aarch64.sh` 是板上那套配方），**macOS 轮子至今没在任何机器上产出过**（缺机器），别在发版当天现造。

## 2. NuGet

| 步骤 | 命令 | 判据 |
|---|---|---|
| 2.1 原生库 + 版本信息 | `python csharp/tools/emit_build_info.py --native <built lib> --commit <sha>` | 写出 `version.props` / `BuildInfo.g.cs`（生成物，不入库） |
| 2.2 单 RID 打包 | `dotnet pack csharp/CppPinyin/CppPinyin.csproj -c Release -p:CppPinyinNativeDir=<dir> -p:CppPinyinRid=win-x64 -o dist/win-x64` | 产出 `CppPinyin.<ver>.nupkg` |
| 2.3 逐腿验收 | `python tools/check_bindings.py --nupkg dist/win-x64/*.nupkg --tag v<ver> --rid win-x64` | PASS（含 `lib/`+`ref/`、词典、`THIRD_PARTY_NOTICES.md`） |
| 2.4 合池 | `python csharp/tools/merge_nupkgs.py --self-test` → 合成 → `check_bindings.py --nupkg`（**不带 `--rid`**） | PASS |
| 2.5 上传 | `dotnet nuget push dist/pooled/CppPinyin.<ver>.nupkg --source https://api.nuget.org/v3/index.json --api-key <key>` | 人的动作 |

> 三个 RID（`win-x64` / `linux-x64` / `osx-arm64`）的包由 CI 的 `release_prep.yml` 产出；**本机只产过 `win-x64`**，另外两个腿一次没跑过。

## 3. vcpkg

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

- 每条命令都在本机跑过至少一次（§1.6 `twine` 除外，**已在表里点名**）；
- 表里出现的数字要能自己核对：`49/49` 指 `ctest` 的条目数，`20/20` 指 `run_all_gates` 的**门禁条数**。
  注意后者**不**在 `check_doc_claims` 的核对范围内（它核的是 ctest 注册数与自检用例数，不是门禁条数）——
  这一行原先写"19 门禁也在核对范围内"，2026-10-22 复核时实测不成立且数字已过期，故改为如实说明。
