# cpp-pinyin2 快速入门 / Quick Start

中文在前，English below. 两种语言的内容**一一对应**，改动时两边一起改。

**这份文档的定位**：最短可用路径 —— 装什么、五行代码、预期输出、以及最常踩的三个坑。
更细的内容不在这里重复，末尾有"接下来读什么"。

---

## 0. 先看这三点 / Before you start

1. **三种形态任选其一**：C++ 库（CMake 安装）、Python 包、C# 包。
   它们共用同一个引擎（同一个 C ABI），所以**说话方式一致**：注册表 / 打开 / 配置 / 查询。
2. **平台状态**（截至 2026-10-09，详见 `docs/DECISIONS.md` §2.1 `docs/RELEASE_CHECKLIST.md`（原 §6.7.11））：
   | 平台 | 状态 |
   |---|---|
   | Windows x64 | ✅ 已实测（含现成 wheel / nupkg） |
   | Linux x86_64 / aarch64 | ✅ 能构建，脚本已在板上跑过；**CI 尚未在 GitHub 上跑过** |
   | macOS | ⚠️ **未实测**——本仓没有 Mac；部署目标（10.15）与验收脚本已进树 |
3. **词典要能被找到**：库本身**不含**默认词典路径（`Engine::open` / `cppp2_open` 都要求显式路径）。
   谁能替决定，优先级是固定的：

   | 层 | 怎么给 | 谁读它 |
   |---|---|---|
   | ① 显式参数 | `Engine::open(registry, dictDir, id)` / `cppp2_open(..., dict_dir, ...)` / `Engine(*registry.resolve("mandarin"), dictDir)`（描述构造，需自备 `Dialect`） | 库本体（唯一认可的方式） |
   | ② 环境变量 | `CPP_PINYIN2_DICT_DIR=/path/to/dict` | **只有包装层与示例**（Python `Engine.default_dict_dir()`、C# `DefaultDictDirectory()`、`examples/mandarin_example`） |
   | ③ 包内副本 | wheel 的 `cpp_pinyin2/data/dict`、nupkg 输出目录的 `data/dict` | 包装层的兜底 |

   C++ 用 `find_package` 时靠 `CPP_PINYIN2_DICT_DIR`（由 `cpp-pinyin2Config.cmake` 设到
   `share/cpp-pinyin2/dict`）；自己从源码构建时要显式给出词典目录。
   **② 的值不做预先校验**：设错了要让库报出"哪个目录、哪个文件"，而不是悄悄退回 ③ —— 否则部署错误
   只在别人机器上暴露。

   **不想让宿主带着数据目录走**（单文件 exe / AppImage / Unity、UE 插件 / 移动端）就打开
   `-DCPP_PINYIN2_EMBED_DICTS=ON`（或 `=mandarin` 只嵌一个方言）：`tools/embed_dialects.py` 会把
   `data/dialects` 编进库里，此后任何"路径不存在"的词典请求都由内置副本回答（**磁盘上的真文件优先**，
   嵌入副本绝不覆盖它）。直接链库、又没有 CMake 包/pc 文件的宿主，还可以调
   `cppp2_dict_dir_candidate()` 问"本次构建把词典装到哪"。

---

## 1. C++ / CMake

```bash
# 从源码（仓库根目录）—— 当前唯一可用的一条
cmake --preset default && cmake --build --preset default
```

> **本仓不提供 vcpkg 端口**（2026-10-23 移除）：仓库里没有 portfile，registry 里也没有 `cpp-pinyin2`，
> 所以 `vcpkg install cpp-pinyin2` 不成立。想用就按下面的 CMake 路径从源码装。

`CMakeLists.txt`：

```cmake
find_package(cpp-pinyin2)
target_link_libraries(你的目标 PRIVATE cpp-pinyin2::cpp-pinyin2)

# 词典拷到可执行文件旁边（本库不猜词典位置）
add_custom_command(TARGET 你的目标 POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
    "${CPP_PINYIN2_DICT_DIR}" "$<TARGET_FILE_DIR:你的目标>/dict")
```

五行代码：

```cpp
#include <cpp-pinyin2/core/Engine.h>
#include <cpp-pinyin2/ext/Registry.h>
#include <iostream>

cpp_pinyin::v2::Registry registry;                          // 方言注册表，必须比引擎活得久
auto opened = cpp_pinyin::v2::Engine::open(registry, "dict", "mandarin");
cpp_pinyin::v2::EngineConfig cfg;
cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;           // 声调数字在音节末尾
std::cout << opened.value().query("中国人民", cfg).value().join() << "\n";
```

预期输出：`zhong1 guo2 ren2 min2`

**注意**：`open()` 与 `query()` 返回的是 `Outcome`（单错误通道），**不要忽略**——失败时里面没有值，
`error().code` 与 `error().detail` 是唯一原因来源。粤语把 `"mandarin"` 换成 `"cantonese"`、
把风格换成 `cpp_pinyin::v2::CantoneseStyle::TONE3`。

---

## 2. Python

```bash
# 仓库里自带构建脚本（会写出 _version.py、构建原生库、把词典拷进包内）
python python/tools/build_native.py
(cd python && python -m pytest tests -q)     # 可选：跑壳的测试
```

包**不在 PyPI**（截至 2026-10-09 未发布），用两种方式之一：

```bash
# a) 从源码构建 wheel 后安装（实测配方）
cd python && python -m pip wheel . --no-build-isolation --no-deps -w dist
python -m pip install dist/cpp_pinyin2-<版本>.whl    # 文件名带平台标签，见 dist/ 里的实际名

# b) 直接把 python/ 加进 PYTHONPATH（开发时最快）
set PYTHONPATH=<你的检出路径>\python
```

五行代码（**发行名 `cpp-pinyin2` 与导入名 `cpp_pinyin2` 不同**，见 `CHANGELOG.md` 的 Breaking 段）：

```python
from cpp_pinyin2 import Engine, Style, to_text

engine = Engine(dialect_id="mandarin")                  # dict_dir 默认用包内那份
print(to_text(engine.query("你好世界", style=Style.TONE3)))  # ni3 hao3 shi4 jie4

for span in engine.query("你好世界", style=Style.TONE3, include_candidates=True):
    print(span.text, span.rendered)
```

预期输出：

```
ni3 hao3 shi4 jie4
你 ni3
好 hao3
世 shi4
界 jie4
```

（`span.text` 逐行就是输入的字，因为"你好世界"里没有 ASCII 串。**候选列表没有抄在这里**：它随词典变，
以 `engine.readings("好")` 为准。）

**三个必须知道的坑**：

- **`close()` 之后不能再查询**（会抛 `ValueError`）；进程里长期复用一个引擎，别每次新建。
- **`span.text` 是"跨段"不是"字符"**：连续 ASCII 字母/数字是一个 span。要逐字对齐，自己按
  `len(span.text)` 摊平；Σ`span.text` 恒等于输入。
- **失败要看 `failure_kind`，不是"`syllables` 空不空"**：`failure_kind` 是 int，`0` = 成功
  （`code_text()` 给措辞）；不请求结构时 `syllables` 本来就是空的。

**变调是显式的一步**（不是风格的副作用）：`engine.tone_sandhi("你好")` → `["ni2", "hao3"]`，
`engine.tone_sandhi("不是")` → `["bu2", "shi4"]`。C++ 侧是 `cpp_pinyin::v2::applyMandarinToneSandhi(result, opts)`
（`core/ToneSandhi.h`），C# 侧是 `engine.ToneSandhi(...)`。粤语没有这套规则。

---

## 3. C# / .NET

```bash
# 先构建原生库，再把它的路径告诉壳（生成 version.props / BuildInfo.g.cs，二者不入库）
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCPP_PINYIN2_BUILD_TESTS=OFF
cmake --build build
python csharp/tools/emit_build_info.py --native build/bin/libcpp-pinyin2.dll
```

```csharp
using CppPinyin;

using (var engine = new Engine("mandarin"))         // 包内自带词典；可传第二个参数覆盖
{
    Console.WriteLine(engine.QueryText("你好世界", Style.Tone3));   // ni3 hao3 shi4 jie4

    foreach (var span in engine.Query("你好世界", Style.Tone3, includeCandidates: true))
    {
        Console.WriteLine($"{span.Text} {span.Rendered}");
    }
}
```

**注意**：`Engine` 实现了 `IDisposable`——用 `using`；原生库由包内 `runtimes/<rid>/native/` 里的文件
决定，**不要在任何地方硬编码库名**（MinGW 产出 `libcpp-pinyin2.dll`，MSVC 产出 `cpp-pinyin2.dll`）。

---

## 4. 常见问题 / Troubleshooting

| 症状 | 原因与处理 |
|---|---|
| C++ 测试 3 条红（`test_user_dict.cpp` 的 `REQUIRE( out.is_open() )`） | **环境事实不是仓库缺陷**：MinGW 产物在默认 `%TEMP%` 下建不了文件。把 `TEMP`/`TMP` 指到可写目录（构建树即可）即绿 |
| `import cpp_pinyin2` 报"native library directory is missing" | 包没被 `python/tools/build_native.py` 装配过；跑一次它（它会写 `_native/` 与 `_version.py`，两者都不入库） |
| `cpp_pinyin2` 装在 Python 3.8 上装不上？ | 这是**有意的**：声明的下限是 `>=3.9`（唯一权威是 `python/setup.py`），且 3.9–3.13 实测过（板上每个解释器都装了同一个 wheel），3.8 从未被当作下限实测过 |
| 想加方言 / 改读音 | 见 `docs/NEW_DIALECT_PLAYBOOK.md`；自定义词典用 `loadUserDict` / `load_user_dict`（**非线程安全，见函数文档**） |
| `query()` 拿到的顺序和 pypinyin 不一致 | 候选按词典顺序（本仓口径），pypinyin 另有一套；差异清单见 `docs/API_zh.md` |

---

## 5. 接下来读什么 / Where next

| 想做的事 | 看 |
|---|---|
| 逐字 API 细节、错误处理、候选语义 | `docs/API_zh.md` / `docs/API_en.md` |
| 契约与不变量（对齐、失败码、线程安全） | `docs/CONTRACT.md`、`docs/STABILITY.md` |
| 打包 / 发布 / 轮子矩阵 / 版本链 | `docs/DECISIONS.md` §2.1 `docs/RELEASE_CHECKLIST.md`（原 §6.7.11） |
| 完全可跑的示例 | `examples/mandarin_example.cpp`、`examples/minimal_usage.cpp` |

---

# Quick Start (English)

Chinese above, English below. The two halves correspond one to one.

## 0. Before you start

1. **Three surfaces, one engine**: the C++ library (installed with CMake), the Python package and the C# package all
   drive the same engine over the same C ABI, so they speak the same way: registry / open / config / query.
2. **Platform status** (as of 2026-10-09; details in `docs/DECISIONS.md` §2.1 `docs/RELEASE_CHECKLIST.md`（原 §6.7.11）):
   Windows x64 is measured end to end; Linux x86_64/aarch64 build and have run on real hardware (the CI legs
   themselves have not run on GitHub yet); **macOS is not measured** -- there is no Mac in this project, though
   the deployment target (10.15) and a verification script are in the tree.
3. **The dictionaries have to be findable**: the library ships no implicit dictionary path. Through
   `find_package` it is `CPP_PINYIN2_DICT_DIR` (set by `cpp-pinyin2Config.cmake` to `share/cpp-pinyin2/dict`);
   the Python and C# packages carry a copy; a source build has to
   be told where they are.

## 1. C++ / CMake

```bash
cmake --preset default && cmake --build --preset default
# No vcpkg port is provided (removed 2026-10-23): there is no portfile here and no cpp-pinyin2 port in the
# registry, so `vcpkg install cpp-pinyin2` does not apply. Install from source with CMake as above.
```

```cmake
find_package(cpp-pinyin2)
target_link_libraries(your_target PRIVATE cpp-pinyin2::cpp-pinyin2)
add_custom_command(TARGET your_target POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
    "${CPP_PINYIN2_DICT_DIR}" "$<TARGET_FILE_DIR:your_target>/dict")
```

```cpp
cpp_pinyin::v2::Registry registry;                          // outlives the engine
auto opened = cpp_pinyin::v2::Engine::open(registry, "dict", "mandarin");
cpp_pinyin::v2::EngineConfig cfg;
cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
std::cout << opened.value().query("中国人民", cfg).value().join() << "\n";   // zhong1 guo2 ren2 min2
```

`open()` and `query()` return an `Outcome`: the single error channel. Do not ignore it -- on failure it holds
no value, and `error().code` / `error().detail` are the only account of why.

## 2. Python

```bash
python python/tools/build_native.py        # writes _version.py, builds the library, stages the dictionaries
(cd python && python -m pytest tests -q)   # optional: the shell's own suite
```

```python
from cpp_pinyin2 import Engine, Style, to_text

engine = Engine(dialect_id="mandarin")
print(to_text(engine.query("你好世界", style=Style.TONE3)))       # ni3 hao3 shi4 jie4
for span in engine.query("你好世界", style=Style.TONE3, include_candidates=True):
    print(span.text, span.rendered)
```

Three traps worth knowing before you write code:

- **No querying after `close()`** (`ValueError`); reuse one engine per process instead of making one per call.
- **`span.text` is a SPAN, not a character** -- a run of ASCII letters/digits is one span. Flatten by
  `len(span.text)` for per-character work; the spans always concatenate back to the input.
- **Test failure with `failure_kind`, not with "is `syllables` empty"**: `failure_kind` is an int, `0` = OK
  (`code_text()` is the wording authority), and `syllables` is legitimately empty when structure was not asked
  for.

**Tone sandhi is an explicit step**, not a side effect of a style: `engine.tone_sandhi("你好")` yields
`["ni2", "hao3"]` and `engine.tone_sandhi("不是")` yields `["bu2", "shi4"]`. In C++ it is
`cpp_pinyin::v2::applyMandarinToneSandhi(result, opts)` (`core/ToneSandhi.h`); in C#, `engine.ToneSandhi(...)`.
Cantonese has no such rules.

## 3. C# / .NET

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCPP_PINYIN2_BUILD_TESTS=OFF
cmake --build build
python csharp/tools/emit_build_info.py --native build/bin/libcpp-pinyin2.dll
```

```csharp
using CppPinyin;

using (var engine = new Engine("mandarin"))
{
    Console.WriteLine(engine.QueryText("你好世界", Style.Tone3));   // ni3 hao3 shi4 jie4
}
```

`Engine` is `IDisposable`. The native library comes from the package's `runtimes/<rid>/native/` entry, so
never hardcode its name: MinGW produces `libcpp-pinyin2.dll`, MSVC produces `cpp-pinyin2.dll`.

## 4. Troubleshooting

| Symptom | Cause and fix |
|---|---|
| Three C++ tests red (`REQUIRE( out.is_open() )`) | Environment, not repository: MinGW binaries cannot create files under the default `%TEMP%`. Point `TEMP`/`TMP` at a writable directory and the suite is green |
| `import cpp_pinyin2` says the native directory is missing | The package was never assembled by `python/tools/build_native.py`; run it (it writes `_native/` and `_version.py`, neither tracked) |
| Python floor question | The floor is `>=3.9`, and 3.8 will not install **on purpose**: 3.9-3.13 are measured (one wheel installed in each), while 3.8 never was -- it could only ever be asserted |
| Adding a dialect or overriding readings | `docs/NEW_DIALECT_PLAYBOOK.md`, plus `loadUserDict` / `load_user_dict` (not thread safe -- see the docstrings) |
| Candidate order differs from pypinyin | Order follows this library's dictionary; `docs/API_zh.md` records the differences |

## 5. Where next

`docs/API_zh.md` / `docs/API_en.md` (per-call detail) · `docs/CONTRACT.md` and `docs/STABILITY.md`
(invariants) · `docs/DECISIONS.md` §2.1 `docs/RELEASE_CHECKLIST.md`（原 §6.7.11） (packaging and release) ·
`examples/` (runnable examples).
