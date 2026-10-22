# cpp-pinyin2

A C++17 library for converting Chinese characters to Pinyin (Mandarin) and Jyutping (Cantonese). Zero external dependencies, fast, and accurate.

**Version: `project(... VERSION)` in the root `CMakeLists.txt` is the only copy** | **License: Apache-2.0 (code) · CC BY-SA (dictionary data, see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md))** | **C++17** | **CMake >= 3.17**

**What changed, and where the measured numbers live: [CHANGELOG.md](CHANGELOG.md).** The changelog describes user-visible changes and points at the documents that own the numbers; it does not quote them, so the two cannot drift apart.

**New here? [`docs/QUICKSTART.md`](docs/QUICKSTART.md)** is the shortest path for all three surfaces (C++ / Python / C#): what to install, five lines of code, the expected output, and the traps people hit first. The section below is the same material in the repository's own voice, with the full examples.

## Features

- Mandarin Pinyin and Cantonese Jyutping conversion
- Polyphonic character disambiguation via phrase dictionary (sliding window, max length 4)
- Traditional to Simplified Chinese conversion
- Custom user dictionaries and fan-jian (Traditional/Simplified) dictionaries
- Custom dialect support through `ToneConverter` inheritance
- Multiple tone styles (Unicode marks, numbered, plain)
- Initials (声母) and finals (韵母) styles, aligned with python-pinyin
- Optional Mandarin tone-sandhi post-pass (三声连读 / 不 / 一) for TONE3 output
- Unicode range: \[0x4E00, 0x9FFF\] (CJK Unified Ideographs basic block)
- Performance and accuracy figures are deliberately **not** duplicated here — numbers drift out of sync when copied.
  The single authority is [`docs/PERFORMANCE.md`](docs/PERFORMANCE.md) for measured values, and `ctest` for what is
  actually enforced on every build. (The figures once printed here — throughput, no-tone accuracy on a lyrics set, and
  with-tone accuracy on `CPP_Dataset` — are kept, with their dated comparison against re-measurement, in that document.)

## Install

### CMake

No vcpkg port is provided: this repository ships no portfile, and there is no `cpp-pinyin2` port in the
registry. Build from source and install with CMake as below.

```cmake
find_package(cpp-pinyin2)
target_link_libraries(${PROJECT_NAME} PRIVATE cpp-pinyin2::cpp-pinyin2)

# Copy dictionary files to the output directory
#
# The variable is `CPP_PINYIN2_DICT_DIR`, which cpp-pinyin2Config.cmake sets to the installed
# dictionary location (`share/cpp-pinyin2/dict`, see the migration table below). The 1.x line put
# them under `lib/cmake/cpp-pinyin2/dict` instead, which is why an older recipe may say that.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
    "${CPP_PINYIN2_DICT_DIR}"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/dict"
)
```

### CMake FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(
    cpp-pinyin2
    GIT_REPOSITORY https://github.com/wolfgitpr/cpp-pinyin.git
    GIT_TAG        <tag-or-commit>   # no release tag exists for this line yet: use a commit hash,
                                     # or the tag of an older release
)
FetchContent_MakeAvailable(cpp-pinyin2)

target_link_libraries(${PROJECT_NAME} PRIVATE cpp-pinyin2::cpp-pinyin2)

# Copy dictionary files to the output directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
    "${CPP_PINYIN2_DICT_DIR}"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/dict"
)
```

### Build from source

```bash
mkdir build && cd build
cmake .. -DCPP_PINYIN2_BUILD_TESTS=ON
cmake --build .
```

A checked-in `CMakePresets.json` reproduces the development configuration (it differs from the CI one in two
options on purpose: examples OFF, WERROR not forced), and keeps the
build tree under `.tmp/` so that nothing grows in the repository root:

```bash
cmake --preset default
cmake --build --preset default
ctest --preset default
```

The root of this repository is a working directory as much as a source tree: `.tmp/` holds scratch builds and
research output, `.tmp-firefly-disk/` holds board cleanup scripts, and both are git-ignored. Anything an IDE
wants to generate — build directories above all — belongs under `.tmp/` as well, which is what the preset is
for. Only what `git ls-files` tracks at the top level is project content.

For static linking, define `CPP_PINYIN2_STATIC` before including headers.

`-DCPP_PINYIN2_BUILD_TESTS=ON` builds the C++ suite (`ctest` runs it). The two language shells have their own
tests. Neither is a single command on a fresh checkout: both need files that are generated and not tracked.

> **Windows hosts: give the suite a writable `TEMP`.** Some C++ tests write a scratch file under
> `std::filesystem::temp_directory_path()`. On this development host, binaries built by the MinGW toolchain
> **cannot create files under the default `%TEMP%`** (`C:\Users\<user>\AppData\Local\Temp\`) while Python
> can -- measured with a 16-line standalone probe compiled by the same `g++`: `ofstream.is_open()` is `false`
> there and `true` when `TEMP`/`TMP` point at a writable directory (the build tree works). The suite then fails
> three tests with `REQUIRE( out.is_open() )` in `tests/cpp/test_user_dict.cpp`, which reads as a repository
> failure but is not one: **this is an environment fact, not a repo fact**, exactly like the missing-`pypinyin`
> note in the dictionary repository's README. Point `TEMP`/`TMP` somewhere writable before `ctest` and the
> suite is green (exit 0). The **registered test count is a single authority** — `docs/STABILITY.md` §0 — and is
> deliberately not quoted here: the number in this line once said 33/33 long after the suite had 36 and then 41.

```bash
python python/tools/build_native.py       # stages the native library, the dictionaries, and _version.py
(cd python && python -m pytest tests -q)  # the Python shell
```

```bash
# the C# shell. type into the placeholders the built library under build/ and the directory holding it;
# emit_build_info.py writes version.props and BuildInfo.g.cs, which are not tracked either.
python csharp/tools/emit_build_info.py --native <built cpp-pinyin2 library>
python csharp/tools/run_tests.py --native-dir <directory holding it>
```

The order that is actually executed for both shells lives in [`.github/workflows/`](./.github/workflows).
`csharp/tools/run_tests.py` is not a convenience wrapper: on some hardened hosts `dotnet test` aborts before
the first test runs, with `Win32Exception (5): Access denied` raised by VSTest's testhost, and this runner
drives xunit's own engine over the same assemblies instead, proving itself with `--self-test` first. CI runs
`dotnet test` on three operating systems; whether that job passes is for the job's own result to say, not for
this note. Measurements and the two traps this runner codes around:
[`docs/DECISIONS.md` §2.1](./`docs/DECISIONS.md` §2.1) V30.

## Quick Start

### Mandarin Pinyin

```cpp
#include <cpp-pinyin2/core/Engine.h>
#include <cpp-pinyin2/ext/Registry.h>
#include <iostream>

cpp_pinyin::v2::Registry registry;                         // 方言注册表，比引擎活得久
auto opened = cpp_pinyin::v2::Engine::open(registry, "dict", "mandarin");
cpp_pinyin::v2::EngineConfig cfg;
cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;          // "zhong1 guo2 ren2 min2"
std::cout << opened.value().query("中国人民", cfg).value().join() << "\n";
```

这一版只展示 API 主干（注册表 + 打开 + 配置 + 查询）。**完整示例**——逐字结果、候选读音、**打开失败的错误详情**——见
`examples/mandarin_example.cpp`（可构建、随门禁编译）与 `docs/API_en.md` / `docs/API_zh.md`
（此处不再复制一份，避免同一段代码三处维护）。

### Cantonese Jyutping

```cpp
#include <cpp-pinyin2/core/Engine.h>
#include <cpp-pinyin2/ext/Registry.h>
#include <iostream>

cpp_pinyin::v2::Registry registry;                         // 方言注册表，比引擎活得久
auto opened = cpp_pinyin::v2::Engine::open(registry, "dict", "cantonese");
cpp_pinyin::v2::EngineConfig cfg;
cfg.style = cpp_pinyin::v2::CantoneseStyle::TONE3;         // "zung1 gwok3 jan4 man4"
std::cout << opened.value().query("中國人民", cfg).value().join() << "\n";
```

与上面的普通话例子**同形**：注册表 + 打开（失败即带原因返回）+ 查询。完整示例与错误处理见
`examples/`（随门禁构建）与 `docs/API_en.md` / `docs/API_zh.md`。

### Loading custom dictionaries

```cpp
// 沿用上面打开的引擎 m（方言 id 决定它读哪套词典）
m.loadUserDict("path/to/my_dict.txt");           // 自定义词典：词 -> 读音
m.loadTraditionalDict("path/to/my_fanjian.txt"); // 简繁映射
```

## Consuming from another project

**CMake.** The installed package exports one target and one variable, and **there is no implicit
dictionary path** — `dict_dir` is always an argument (`include/cpp-pinyin2/ext/c_api.h`, rule 7):

```cmake
find_package(cpp-pinyin2 CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE cpp-pinyin2::cpp-pinyin2)

# The dictionaries this package installed; copy or point at them yourself -- the engine
# never guesses a location. Exported by cpp-pinyin2Config.cmake.
add_custom_command(TARGET your_app POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CPP_PINYIN2_DICT_DIR}" "$<TARGET_FILE_DIR:your_app>/dict")
```

The package refuses a **cross-major** request on purpose (`COMPATIBILITY SameMajorVersion`), so a
1.x `find_package(cpp-pinyin2 1.x)` does not silently resolve to a 2.x install.

**Per-character alignment.** Downstream morphology/TTS code usually needs "which reading belongs to
which input character". One row of a `Result` covers **one input span**, and the spans are a
partition of your input — the contract is that **concatenating every row's `text` in order reproduces
the input byte string** (enforced by `tests/cpp/test_c_api.cpp`, `CHECK(joined == one.text)`).
So a byte cursor over the input is the whole recipe:

```cpp
std::string input = "银行ABC";
auto rows = engine.query(input).value();     // one row per span, spans partition `input`

std::size_t byteOffset = 0;                  // stays aligned: Σ row.text == input
for (const cpp_pinyin::v2::Segment &row : rows) {
    // This row's reading is `row.rendered` (bytes), not `row.candidates[0]`:
    // a phrase hit may pick a reading that the character's own candidate list
    // does not even contain (docs/REQUIREMENTS.md §1.2 K9).
    use(byteOffset, row.text.size(), row.rendered);

    // Two deliberate exceptions to "one row == one character":
    //  * a run of ASCII letters/digits is ONE span, so a 3-byte Hanzi is never
    //    the unit of `text.size()` -- always advance by the byte count;
    //  * a row the library could not read keeps `rendered == text` and carries
    //    `row.failure_kind != Code::Ok` (punctuation, digits, unknown characters).
    byteOffset += row.text.size();
}
```

If you control phrase-level disambiguation yourself, `Engine::setDisambiguationCallback` hands you
the **UTF-8 byte offset** of the character inside your text (`core/Types.h`), which is the same
alignment expressed from the callback side.

## Migrating from 1.x (and from an earlier 2.0 revision)

**Nothing in the 1.x surface survives as an alias**, so a 1.x call site has to be rewritten — the
names below have **zero** hits under `include/` and `src/` on this branch.

| 1.x (tag `1.0.2`) | this branch | notes |
|---|---|---|
| `namespace Pinyin` | `namespace cpp_pinyin` | every public type |
| `Pinyin::Pinyin` / `Pinyin::Jyutping` | `cpp_pinyin::v2::Engine::open(registry, dictRoot, "mandarin"/"cantonese")` | one class instead of two; the dialect is an argument |
| `Pinyin::setDictionaryPath(root)` / `dictionaryPath()` (process-global) | the `dictRoot` argument of `open` / the description constructors | **no process-global path at all** — two engines with two roots coexist (`core/Engine.h:21-22`) |
| `hanziToPinyin(hans, style, error, candidates, v_to_u, neutral_tone_with_five)` → `PinyinResVector` | `query(text, cfg)` → `Outcome<Result>` | `cfg` carries style/`includeCandidates`/`vToU`/`markNeutralAs5`; a failure is an `Outcome` with no value plus `error().code`/`detail` |
| row fields `character` / `reading` / `failed` | `Segment::text` / `rendered` / `failure_kind` | the mapping is recorded in `core/Types.h` (the `Segment` block header) |
| `ManTone::Style` / `CanTone::Style` | `MandarinStyle` / `CantoneseStyle` | **the numbers are unchanged** (`NORMAL=0`, `TONE=1`, `TONE2=2`, `TONE3=8`, …) — only the enum's name and namespace moved |
| `include/cpp-pinyin2/Pinyin.h` etc. | `include/cpp-pinyin2/core/…`, `ext/…`, `dialects/…` | 14 public headers, frozen list in `tests/abi/expected_public_headers.txt` |
| tone sandhi as a side effect of a style | `cpp_pinyin::v2::applyMandarinToneSandhi(result, opts)` | an explicit post-pass (`core/ToneSandhi.h`) |

**If you were consuming the Python shell of an earlier 2.0 revision** (`Segment.failed` /
`Segment.reading`), two field names changed on 2026-10-07:

| earlier 2.0 shell | now |
|---|---|
| `Segment.failed` (bool) | `Segment.failure_kind` (int; `0` = OK, `code_text()` is the wording authority) |
| `Segment.reading` (str) | `Segment.rendered` (str) — the structured reading is `Segment.syllables` |
| — | `Segment.text` is a **span**, not a character: a run of ASCII letters is one span |

The shell's class/constructor names did **not** change: `Engine(dialect_id=..., dict_dir=...)`,
`engine.query(text, style=..., include_candidates=...)`, `engine.readings(...)`.

**Build/packaging changes** that a port or CI job has to absorb:

| 1.x | this branch |
|---|---|
| dictionaries installed under `lib/cmake/cpp-pinyin2/dict` | `share/cpp-pinyin2/dict` (`CMAKE_INSTALL_DATADIR`), exported as `CPP_PINYIN2_DICT_DIR` |
| `-DCPP_PINYIN_VCPKG_DICT_DIR=…` | **removed** (no equivalent option) |
| `-DCPP_PINYIN_BUILD_STATIC` | `-DBUILD_SHARED_LIBS=OFF` (which defines `CPP_PINYIN2_STATIC` for consumers) |
| a 1.x `find_package(cpp-pinyin2 1.x)` request | refused on purpose: `COMPATIBILITY SameMajorVersion` ⇒ request `2.x` |

## API Reference

All public types live in the `cpp_pinyin` namespace.

### Character classification (core/G2pglobal.h)

```cpp
bool isLetter(char16_t c);
bool isHanzi(char16_t c);       // 0x3400..0x4DBF, 0x4E00..0x9FFF, 0xF900..0xFAFF
bool isKana(char16_t c);        // Hiragana + Katakana
bool isDigit(char16_t c);       // 0-9
bool isSpace(char16_t c);       // ' ' only
bool isSpecialKana(char16_t c); // Small kana (ャ, ュ, ョ, etc.)
```

### Segment (core/Types.h)

```cpp
struct Segment {
    std::string text;       // Original input run (UTF-8); ASCII letter/digit runs stay together
    Syllable reading;       // Structure of the reading PICKED for this position (initial/fin/tone/
                            // tone_kind/source). A failed row -- or one whose string a plugin
                            // callback wrote -- keeps the default value: it describes nothing.
    // Every reading of *this* character (dictionary order, de-duplicated after style
    // conversion). Empty when `includeCandidates == false` -- except on failed rows, which
    // always carry `{text}`. A phrase/user-dictionary hit may pick a reading that is
    // not `candidates[0]`, or is not in `candidates` at all (see docs/REQUIREMENTS.md K9).
    // Still strings, not structures: 7 readings in the shipped corpora are not invertible
    // (`yo`, `yō`, `ǹ`, `ḿ`, `hm`, ...), so a structure cannot be rendered back to the stored
    // spelling -- moving this column needs the renderer entry point first.
    std::vector<std::string> candidates;
    Code failure_kind = Code::Ok;  // Non-Ok when conversion failed (the old `failed` flag)
    std::string rendered;   // The row's one rendered string; equals `text` when failed
};
```

### Result (core/Types.h)

A `Result` is a read-only sequence of the `Segment` rows above (authority:
`include/cpp-pinyin2/core/Types.h`). There is **no `readings()` member** — the rows carry the
readings, so ask the row:

```cpp
class Result {
public:
    using value_type = Segment;
    using const_iterator = std::vector<Segment>::const_iterator;

    bool empty() const noexcept;
    std::size_t size() const noexcept;

    const Segment& at(std::size_t i) const;    // throws std::out_of_range
    const Segment& operator[](std::size_t i) const noexcept;
    Segment& at(std::size_t i);               // mutable: for post-passes such as tone sandhi
    Segment& operator[](std::size_t i) noexcept;

    std::string join(const std::string &delimiter = " ") const; // all rows' `rendered`, joined
    std::vector<std::string> toVector() const;                  // the same, as a vector
};
```

`join()` joins **`Segment::rendered`**, i.e. the reading each row actually got — not its candidates.

### Styles and configuration (core/Types.h)

#### EngineConfig

```cpp
struct EngineConfig {
    Style style;                            // Style{} asks for the dialect default; an explicit 0 is NORMAL
    ErrorHandling onError = ErrorHandling::Default;
    bool includeCandidates = true;
    bool includeReading = false;            // fills Segment::reading; off by default (hot path)
    bool vToU = false;                      // ignored, not reinterpreted, where unsupported
    bool markNeutralAs5 = false;            // same masking rule
};
```

#### Engine

`cpp_pinyin::v2::Engine` 是唯一的公开入口：用 `cpp_pinyin::v2::Engine::open(registry, dictRoot, dialectId)` 从注册表
取方言，或用 `Engine(const Dialect&, dictDir[, subdir])` 直接按描述构造（不看注册表，适合未注册的方言）。

两处失败都走**同一条错误通道**：`open` 与查询都返回 `Outcome`，失败时 `error().code`（`Code::NotReady` /
`Code::Load`）配 `error().detail`（详情里带上是哪个文件、哪个目录）。**完整签名与逐条语义以
`include/cpp-pinyin2/core/Engine.h` 为准**（这里不复制一遍，避免同一份 API 三处维护），逐条可编译示例见
`docs/API_zh.md` / `docs/API_en.md`。

> **描述构造没有 `Outcome` 可返回**，所以它失败时既不会抛、也不会留一个"看似成功"的半成品：对该对象
> **查询**会明确回 `Code::NotReady`，`error().detail` 给出原因（是哪个目录、哪个文件）；`open` 则干脆不交出
> 这样的引擎——它在返回前就检查完，把同一句话放进 `Code::Load` 的 `detail`。
> 旧的 `isReady()` / `lastError()` 公开访问器**已删除**（S11a 收尾）：就绪与否对使用者只有上面这两条错误
> 通道可读；引擎内部那个等价判断仍在，但已是私有实现，不再属于公开接口。

#### MandarinStyle

| Style | Value | Example |
|-------|-------|---------|
| `NORMAL` | 0 | `zhong guo` (no tone) |
| `TONE` | 1 | `zhōng guó` (Unicode diacritics) |
| `TONE2` | 2 | `zho1ng guo2` (number after the vowel) |
| `FIRST_LETTER` | 3 | `z g` (first letter only) |
| `BOPOMOFO` | 4 | `ㄓㄨㄥ ㄍㄨㄛˊ` (Zhuyin/Bopomofo) |
| `INITIALS` | 5 | `zh g` (声母; empty for zero-initial syllables) |
| `FINALS` | 6 | `ong uo` (韵母; `gui` → `uei`, `yu` → `v`) |
| `FINALS_TONE3` | 7 | `ong1 uo2` (final + tone digit) |
| `TONE3` | 8 | `zhong1 guo2` (number after the syllable) |

The numbering is this library's own; it is *not* pypinyin's `Style` numbering.
`INITIALS`/`FINALS`/`FINALS_TONE3` follow pypinyin 0.55.0 (`strict=True`, `v_to_u=False`)
and are verified case-by-case against it .

**Mandarin-specific config**:
- `vToU`: When `true`, a reading that spells `ü` itself keeps the `ü`; when `false` (default) it becomes `v`. Note that tone-marked `ǚ/ǘ/ǖ/ǜ` always reduce to `v` — only the literal `ü` spelling is affected (pinned by `test_golden_pinyin.cpp`).

### Cantonese style (core/Types.h)

#### CantoneseStyle

| Style | Value | Example |
|-------|-------|---------|
| `NORMAL` | 0 | `zung gwok` (no tone) |
| `FIRST_LETTER` | 3 | `z g` (first letter only) |
| `TONE3` | 8 | `zung1 gwok3` (number after the syllable) |

Cantonese has no `INITIALS`/`FINALS` styles yet (the rules above are Mandarin-specific).

### Tone sandhi (core/ToneSandhi.h)

Optional post-pass for Mandarin, applied by the caller after `query()` — it never re-runs
segmentation, phrase matching or disambiguation and only rewrites `rendered` (mirroring the new tone into `reading.tone`):

```cpp
#include <cpp-pinyin2/core/ToneSandhi.h>

cpp_pinyin::v2::Registry registry;
cpp_pinyin::v2::Engine man = cpp_pinyin::v2::Engine::open(registry, "path/to/dict", "mandarin").value();
auto res = man.query("你好世界", {.style = cpp_pinyin::v2::MandarinStyle::TONE3});
cpp_pinyin::v2::applyMandarinToneSandhi(res.value());   // "ni2 hao3 shi4 jie4"
```

- third-tone chains (`你好` → `ní hǎo`, `老鼠` → `láo shǔ`), `不` + 4th tone (`不是` → `bú shì`),
  `一` + 4th tone (`一定` → `yí dìng`), `一` + 1st/2nd/3rd tone (`一天` → `yì tiān`),
  `一` at the end / before a digit → `yī`
- works on `TONE3` / `FINALS_TONE3` readings (a trailing digit); other styles have no
  recoverable tone and are left untouched
- row count, `text`, `candidates` and `failure_kind` are never modified
- **the pass belongs to the dialect**: `Dialect::toneSandhi` (`ToneSandhiPass`) says whether a dialect has
  one, and `applyMandarinToneSandhi` is the only pass declared today. Cantonese declares none — jyutping
  readings happen to look the same, but the 不/一 rules do not apply there, so the C entry
  `cppp2_apply_tone_sandhi(engine, result, opts, &changed)` takes the engine and **refuses** with
  `CPPP2_ERR_INVALID_ARG` instead of rewriting them. A second dialect's pass arrives as a new symbol
  (`cppp2_apply_<dialect>_sandhi`), never as a change to this one
- individual rules can be turned off with `ToneSandhiOptions`

### U16Str utilities (core/U16Str.h)

```cpp
std::string u16strToUtf8str(const char16_t &ch16);   // Single BMP char
std::string u16strToUtf8str(const std::u16string &u16); // Full string
std::u16string utf8strToU16str(const std::string &utf8); // With surrogate pair support
```

- `utf8strToU16str` handles 1-to-4-byte UTF-8 sequences
- 4-byte sequences (U+10000+) encoded as UTF-16 surrogate pairs
- Invalid UTF-8 sequences throw `std::invalid_argument`

Normative source for those three, and for the reverse direction: `include/cpp-pinyin2/core/U16Str.h`.
(`docs/API_en.md` repeats the three bullets; that is deliberate -- a reader of either page should not have
to hop. Nothing in the two copies is a number, so they cannot drift apart in the way counts do.)

## Custom Dialect Support

**Adding a dialect does not require touching this library** — you supply dictionary files, a
`ToneConverter` subclass, and one `Dialect`, then hand it to `Engine` (directly, or through a
`Registry` so it can be addressed by id). Authority: `include/cpp-pinyin2/ext/Dialect.h`.

```cpp
#include <cpp-pinyin2/core/Engine.h>
#include <cpp-pinyin2/ext/Dialect.h>
#include <cpp-pinyin2/ext/Registry.h>

// 1. Your own ToneConverter subclass. It carries its OWN style numbering: an out-of-tree
//    dialect cannot extend the built-in enums, which is why the numbering lives here.
class MyTone final : public cpp_pinyin::v2::ToneConverter {
public:
    enum Style { NORMAL = 0, TONE3 = 8 };
    MyTone() { m_converts[static_cast<int>(TONE3)] = &myTone3; }   // static ConvertFunc, defined by you
};

// 2. One description. The registry stores a CREATOR, so it can hand out a fresh immutable
//    description each time the dialect is resolved.
auto makeDialect = [] {
    auto d = std::make_shared<cpp_pinyin::v2::Dialect>();
    d->id = "wuu";                    // the id `Engine::open(registry, root, "wuu")` resolves
    d->dictSubDir = "wuu";            // subdirectory of the dictionary root; empty means "use id"
    d->tone = MyTone{};               // your converter, held BY VALUE
    d->supportsVToU = true;           // gates EngineConfig::vToU
    d->supportsNeutralTone5 = true;   // gates EngineConfig::markNeutralAs5
    d->styles = {{"normal", 0, false}, {"tone3", 8, true}};  // {name, alias, isDefault}
    return d;
};

// 3. A registry the caller owns -- registering the built-ins, then yours next to them.
cpp_pinyin::v2::Registry registry;
if (!registry.add("wuu", makeDialect))
    /* the id was already taken and `replace` was false, or the id/creator was empty */;

auto opened = cpp_pinyin::v2::Engine::open(registry, "path/to/dict", "wuu");  // reports WHY on failure
```

A dialect that is **not** registered needs no creator: keep the description and hand it straight to
the constructor — `cpp_pinyin::v2::Engine engine(*makeDialect(), "path/to/dict");`.

Two properties are deliberate and load-bearing (`ext/Dialect.h:20-32`):

- **`tone` is held by value, not by pointer**, because `EngineImpl` copies the converter it is given.
  A value copy slices to the `ToneConverter` base subobject — which is harmless because every dialect
  behaviour lives in that base's **function pointer table** (`m_converts`, `m_normalize`) and never in
  a virtual. Do not "improve" this into a virtual.
- **A `Dialect` carries no dictionary path and no loaded state.** The root is an `Engine` constructor
  argument, so two instances of the same dialect can read two different directories at once.

`Dialect::postProcess` is the supported place for a caller-side post-pass over a finished `Result`
(`ext/Dialect.h:68-72`); the engine's own disambiguation window is frozen and must not be
reimplemented there.

**The executable form of everything above is [`examples/plugin-dialect/plugin_dialect.cpp`](./examples/plugin-dialect/plugin_dialect.cpp)**
— one translation unit outside the library, reached through the installed package, registered into a
registry the caller owns, running next to a built-in dialect over the same dictionary root. It is
built by the gates, so it cannot rot.

[pinyin-makedict](https://github.com/wolfgitpr/pinyin-makedict) is the companion tool for creating custom dialect dictionaries.

See [Custom Chinese Dialect](./docs/Custom%20Chinese%20Dialect.md) for details.

## Dictionary Structure

Dictionary files live under `data/dialects/` with separate directories per language. Each dialect states the
role of every file it ships in its own `dialect.json`, so a missing table is an error rather than a partial
dictionary that still loads:

```
data/dialects/
  mandarin/
    dialect.json      # file roles: required tables, optional tables, assets
    word.txt
    phrases_map.txt
    phrases_dict.txt
    user_dict.txt
    trans_word.txt
    License.txt
  cantonese/
    dialect.json
    word.txt
    phrases_map.txt
    phrases_dict.txt
    user_dict.txt
    trans_word.txt
    License.txt
```

These two are the dialects this repository ships and claims; CI fails if a third one appears here. Your own
dialect's dictionaries live in a directory of your choice outside the repository and are passed to the engine
-- see [Custom Chinese Dialect](./docs/Custom%20Chinese%20Dialect.md).

## Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_SHARED_LIBS` | ON | Build as shared library (dynamic linking) |
| `CPP_PINYIN2_BUILD_TESTS` | OFF | Build the test suite |
| `CPP_PINYIN2_INSTALL` | ON | Generate install targets |
| `CPP_PINYIN2_EMBED_DICTS` | OFF | Embed the dictionary tables into the library: `ON`, or a `;`-separated list of dialect names. Generated by `tools/embed_dialects.py`; a build that embeds answers table requests from itself when the requested path does not exist, which is what a single-file deployment needs |

For static linking in your own project, define `CPP_PINYIN2_STATIC` before including any cpp-pinyin2 headers.

## Contributing

Contributions are welcome. [`CONTRIBUTING.md`](CONTRIBUTING.md) lists the gates to run before opening a pull request, and [`SECURITY.md`](SECURITY.md) is the private channel for a vulnerability.

## Documentation

Three lists. **Start here** if you are using the library; **reference** when you need one specific
answer; **maintainer ledgers** record how this tree got where it is — useful when you are *changing*
the repository, not when you are using it. [`docs/README.md`](./docs/README.md) is the full index,
including which document owns which fact.

### Start here

| Document | Contents |
|---|---|
| [`docs/QUICKSTART.md`](./docs/QUICKSTART.md) | **起手最快的一份**：三种壳（C++ / Python / C#）各自的最短可用路径、预期输出与常见坑（中英对照） |
| [`docs/API_en.md`](./docs/API_en.md) | Full C++ API reference (English) |
| [`docs/API_zh.md`](./docs/API_zh.md) | 完整 C++ API 文档（中文） |
| [`docs/Custom Chinese Dialect.md`](./docs/Custom%20Chinese%20Dialect.md) | Adding a custom dialect |
| [`docs/PERFORMANCE.md`](./docs/PERFORMANCE.md) | 性能简报：**性能数字的唯一真相源**、测量协议、准入判据 |

### Reference

| Document | Contents |
|---|---|
| [`docs/CONTRACT.md`](./docs/CONTRACT.md) | 契约与不变量（对齐、失败码、线程安全边界、C ABI 增长规则） |
| [`docs/STABILITY.md`](./docs/STABILITY.md) | **跨代兼容政策 §4.5（R1–R16 的唯一权威）** + 长期稳定性评估（sanitizer / 线程 / 多实例 / ABI） |
| [`docs/UPGRADING_A_GENERATION.md`](./docs/UPGRADING_A_GENERATION.md) | **换代/迁移清单**：发布前与落地时两张表（替换半径、冻结清单、版本链、数据面 `format.txt`、默认风格迁移） |

### Maintainer ledgers (internal)

| Document | Contents |
|---|---|
| [`docs/DECISIONS.md`](./docs/DECISIONS.md) | **决策号唯一登记处 + 现行裁定**：各系列记号的含义、D 号分段、算法冻结区 K1–K8、验收线、性能与测量纪律、明确不做清单 |
| [`docs/DECISIONS.md` §2.1](./`docs/DECISIONS.md` §2.1) | 打包与发布：轮子/nupkg 矩阵、版本链、插件/打包决策台账 |
| [`docs/REQUIREMENTS.md`](./docs/REQUIREMENTS.md) | 需求契约：跨轮硬约束索引（算法冻结区、准确率门禁、跨平台） |
| [`docs/CONTRACT.md`](./docs/CONTRACT.md) | 用户面契约：C ABI 增长规则、失败值域、结构化读数开关 |
| [`docs/STABILITY.md`](./docs/STABILITY.md) | 稳定性政策 R1–R16、跨代兼容窗口、注册用例数的唯一权威 |
| [`docs/NEW_DIALECT_PLAYBOOK.md`](./docs/NEW_DIALECT_PLAYBOOK.md) | 新语种拓展 playbook（本仓只做中粤） |
| [`docs/UPGRADING_A_GENERATION.md`](./docs/UPGRADING_A_GENERATION.md) | 换代流程与故障对照表（判词 → 原因 → 修法） |
| [`docs/PERFORMANCE.md`](./docs/PERFORMANCE.md) | 性能测量协议与基线读数 |
| [`docs/RELEASE_CHECKLIST.md`](./docs/RELEASE_CHECKLIST.md) · [`docs/RELEASE_RUNBOOK.md`](./docs/RELEASE_RUNBOOK.md) | 发布一版的落点与当天要敲的命令 |

## Open-source references

- [zh_CN](https://github.com/ZiQiangWang/zh_CN): Core algorithm source, further tailored to this project's dictionaries.
- [opencpop](http://wenet.org.cn/opencpop/): Test data source.
- [M4Singer](https://github.com/M4Singer/M4Singer): Test data source.
- [cc-edict](https://cc-cedict.org/wiki/): Dictionary source.
- [pinyin (kfcd)](https://github.com/kfcd/pinyin): Fan-jian dictionary source.
- [cpp_dataset](https://github.com/kakaobrain/g2pm/tree/master/data): CPP_Dataset source.

## Related projects

- [pinyin](https://github.com/hotoo/pinyin): Chinese to Pinyin, Node.js/JavaScript.
- [python-pinyin](https://github.com/mozillazg/python-pinyin): Chinese to Pinyin, Python.
- [go-pinyin](https://github.com/mozillazg/go-pinyin): Chinese to Pinyin, Go.
- [rust-pinyin](https://github.com/mozillazg/rust-pinyin): Chinese to Pinyin, Rust.
- [csharp-pinyin](https://github.com/wolfgitpr/csharp-pinyin): Chinese to Pinyin, C#.

## License

**The code is Apache-2.0** — see [`LICENSE`](./LICENSE).

**The dictionary tables are not**: Mandarin ships under **CC BY-SA 4.0** and Cantonese under
**CC BY-SA 3.0**, and Catch2 (tests only, never installed) under BSL-1.0. Every package carries the
per-directory `License.txt` files next to the tables; the one-page summary of what is under which
license, where it ends up, and what a redistributor has to keep is
[`THIRD_PARTY_NOTICES.md`](./THIRD_PARTY_NOTICES.md).

If your product embeds this library, the share-alike obligation applies to the dictionary data, not to
your own source code.
