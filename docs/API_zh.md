# cpp-pinyin2 API 文档 (中文)

> 版本：唯一源是根 `CMakeLists.txt` 的 `project(... VERSION)`，本文件不复述数字 · C++17 · 零外部依赖

---

## 目录

0. [公开面索引（自动核对）](#公开面索引自动核对)
1. [快速开始](#1-快速开始)
2. [命名空间](#2-命名空间)
3. [核心类型](#3-核心类型)
4. [引擎 API](#4-引擎-apiengine取方言与查询)
5. [方言描述与注册表](#5-方言描述与注册表dialect--registry)
6. [声调转换器](#6-声调转换器)
7. [UTF 工具](#7-utf-工具)
8. [词典文件格式](#8-词典文件格式)
9. [普通话变调](#9-普通话变调tonesandhih)
10. [C ABI](#10-c-abi)
11. [四面的参数序](#11-同一操作在四个面上的参数序2026-10-20-新增)

---

## 公开面索引（自动核对）

本表是 `tools/check_public_surface.py` 的核对对象：**每个公共头里声明的类型都必须在这里出现一行**，
反之表里的每个名字都必须在公共头里找得到。「受众」列是该门禁判不了的部分，由人写：
`consumer` = 第三方可直接用；`plugin` = 写方言的人要用；`internal` = 导出脚手架，API 文档明说不对调用者开放。

| 类型 | 受众 | 声明于 | 用途 |
|---|---|---|---|
| `Engine` | `consumer` | `core/Engine.h` | 查询门面：按注册 id 或按描述打开 |
| `Registry` | `consumer` | `ext/Registry.h` | 只读的 id → 构造器表，由调用方持有 |
| `Dialect` | `plugin` | `ext/Dialect.h` | 一个方言的完整描述 |
| `StyleDecl` | `plugin` | `core/Types.h` | 一种受支持风格：编号 + 冻结名 + 示例调类 |
| `DisambiguationCallback` | `consumer` | `core/Types.h` | 多音字消歧回调：问调用方该字读什么，返回空串表示不表态；默认构造即「未安装」 |
| `ToneConverter` | `plugin` | `core/ToneConverter.h` | 自定义方言继承的基类 |
| `Syllable` | `consumer` | `core/Syllable.h` | 一条结构化读音：spelling 是身份，tone 是方言内编号 |
| `ToneKind` | `consumer` | `core/Syllable.h` | 读数来源 / 是否被规则改写过 |
| `ReadingSource` | `consumer` | `core/Syllable.h` | 读数从哪一层表读到的 |
| `Segment` | `consumer` | `core/Types.h` | 一个渲染位置：文本 + 选中读音 + 候选 |
| `Result` | `consumer` | `core/Types.h` | 一次查询的整体：分段 + 拼接后的渲染 |
| `Outcome` | `consumer` | `core/Types.h` | 值 **或** 错，不会同时；唯一错误通道 |
| `Error` | `consumer` | `core/Types.h` | code + detail |
| `ErrorException` | `consumer` | `core/Types.h` | value_or_throw() 抛出的东西；what() 即 detail |
| `Code` | `consumer` | `core/Types.h` | 错误码枚举 |
| `ErrorHandling` | `consumer` | `core/Types.h` | 转换失败时保留还是丢弃字符 |
| `EngineConfig` | `consumer` | `core/Types.h` | 单次调用选项（风格 / 是否带读音 / 成形） |
| `ToneSandhiOptions` | `consumer` | `core/ToneSandhi.h` | 普通话变调趟的选项 |
| `ToneSandhiPass` | `consumer` | `core/ToneSandhi.h` | 方言声明的是哪一趟变调（`None`/`Mandarin`）；`cppp2_apply_tone_sandhi` 据此决定能不能调 |
| `ManTone` | `internal` | `dialects/ManTone.h` | 内置普通话转换器（9 风格）。**已安装、已进冻结公共名面**（`test/abi` 的公共头与成员清单里有它），但定位是 `internal` 档：可用、不推荐、升级不承诺 |
| `CanTone` | `internal` | `dialects/CanTone.h` | 内置粤语转换器（3 风格）。**已安装、已进冻结公共名面**，同 `ManTone`：可用、不推荐、升级不承诺 |
| `MandarinStyle` | `internal` | `core/Types.h` | 九个普通话风格编号（§3 有表） |
| `CantoneseStyle` | `internal` | `core/Types.h` | 三个粤语风格编号（§3 有表） |
| `Style` | `internal` | `core/Types.h` | 两个按方言枚举共用的数值风格编号 |

---

## 1. 快速开始

```cpp
#include <cpp-pinyin2/core/Engine.h>
#include <cpp-pinyin2/ext/Registry.h>

cpp_pinyin::v2::Registry registry;              // 方言注册表，比引擎活得久

// 普通话
cpp_pinyin::v2::Engine m = cpp_pinyin::v2::Engine::open(registry, "path/to/dict", "mandarin").value();
auto res = m.query("你好世界", {.style = cpp_pinyin::v2::MandarinStyle::TONE3});
std::cout << res.value().join();  // "ni3 hao3 shi4 jie4"

// 粤语
cpp_pinyin::v2::Engine c = cpp_pinyin::v2::Engine::open(registry, "path/to/dict", "cantonese").value();
auto jres = c.query("你好世界", {.style = cpp_pinyin::v2::CantoneseStyle::TONE3});
std::cout << jres.value().join();  // "nei5 hou2 sai3 gaai3"
```

---

## 2. 命名空间

> **跨代兼容与演进政策**（v2 / v3 / v4…）：见 [`docs/STABILITY.md`](STABILITY.md) §4.5 —— 本文只讲 API 用法，兼容承诺、换代规则与"哪些面必须带代令牌"在那里成文（唯一权威）。


所有公开接口位于 `namespace cpp_pinyin`。

---

## 3. 核心类型

文件：`Types.h`

### Segment

```cpp
struct Segment {
    std::string text;                    // 输入片段 (UTF-8)；连续 ASCII 字母/数字是一个片段
    Syllable reading;                    // 该位置**选中**读音的结构（仅 includeReading=true 时填）
    std::vector<std::string> candidates; // 该字自身的全部读音（词典行序，转换后去重保首次）
    Code failure_kind = Code::Ok;        // 转换失败时为非 Ok（旧 failed 标志的等价物）
    std::string rendered;                // 该位置唯一的渲染字符串；失败行等于 text
};
```

`reading` 是**选中**读音的结构（失败的、以及回调自己写了字符串的行留默认构造值，即"结构
什么都不描述"）；它是**按需**的（`includeReading`，默认 `false`）：结构不参与渲染，默认路径
不该为每行付这次查询与拷贝 —— 与 `candidates` 同一条理由。开关只影响这一列，
`text`/`rendered`/`candidates`/`failure_kind` 两侧逐字节相同。`candidates` 仍是字符串，因为
语料里 7 个不可逆读音（`yo`/`yō`/`ǹ`/`ḿ`/`hm` 等）无法由结构反推原文串，见 `Segment` 头注释。

`candidates` 的三条实测契约（全语料 3.6M 行扫描，见 `docs/REQUIREMENTS.md` K9）：

- 汉字行：`candidates` 恒等于 `Engine::readings(character, cfg)`（同样的 style/参数；`Engine` 是
  唯一门面，粤语走的是同一条路径）；
- `includeCandidates=false` 时汉字行为空；**失败行例外**，恒为 `{character}`；
- 词组/用户词典命中行的 `reading` 来自词组表，可以 ≠ `candidates[0]`（如 `银行` 的「行」读
  `hang2` 而候选首项是 `xing2`），甚至不在 `candidates` 内（用户词典可以收录字表没有的读音）。

### Syllable / ToneKind / ReadingSource

文件：`Syllable.h`（`tone` 的权威契约在 `docs/CONTRACT.md` §3，四处措辞由
`tests/cpp/test_syllable_contract.cpp` 钉住）

```cpp
enum class ToneKind { unspecified = 0, base = 1, sandhi = 2 };   // 读数来自哪里、是否被规则改写过
enum class ReadingSource { dictionary = 0, phrase = 1, user_dict = 2, plugin = 3 };

struct Syllable {
    std::string initial;                 // 声母；零声母为 ""
    std::string fin;                     // 规范韵母：去调、ü 记作 v
    std::string spelling;                // 词表原始拼写（UTF-8，逐字节）——**本结构体的身份**
    int tone = 0;                        // 方言内调类编号：粤语 1..6 / 普通话 0..5
    ToneKind tone_kind = ToneKind::unspecified;
    ReadingSource source = ReadingSource::dictionary;
};
```

- **`spelling` 是身份，`initial`/`fin` 只是它的投影**，而投影**不可逆**：普通话的 `hm`/`ǹ`/`ḿ`/`yo` 等
  读音与非音节串都拼不回去 ⇒ 需要再渲染一次的消费者请用 `spelling`，不要反推。
- **`tone` 是方言内编号，不是跨方言可比量**：同一个整数在两种方言里含义不同（普通话 0..5、粤语 1..6）
  ⇒ 不要把两种方言的 `tone` 拿来比较、排序或求平均。它的唯一权威来源是**词表原始注音**，且 **NORMAL
  风格下也必须填**（NORMAL 去调只影响渲染）。
- **`tone_kind` 描述读到的表项本身**，不是渲染过程；数据无法标注变调时允许全 `unspecified`，
  且 **API 不得据此改变任何输出字节**（`sandhi` 目前没有任何数据会产生）。
- `ReadingSource` 是引擎**实际查询的四个来源**（按优先级），**不是**渲染风格清单。
  **已知缺口，写在这里省得使用者自己撞**：今天只有前两个值有生产者 —— 词组层与用户词典在
  `src/dict/PhraseLayers.cpp` 里已被合并成一张表，合并后无法分辨行来自哪一层，所以用户词典来的读数
  报成 `phrase`，**`user_dict` / `plugin` 从不产生**（是"未实现"，不是"尚未观察到"）。

### Result

```cpp
class Result {
public:
    using value_type = Segment;                                 // 每行是 Segment，不是 Syllable
    using const_iterator = std::vector<Segment>::const_iterator;

    Result() = default;
    explicit Result(std::vector<Segment> syllables);

    bool empty() const noexcept;                                // 没有任何行
    std::size_t size() const noexcept;                          // 行数
    const_iterator begin() const noexcept;
    const_iterator end() const noexcept;
    const_iterator cbegin() const noexcept;                     // 与 begin() 同义的显式只读版
    const_iterator cend() const noexcept;                       // 与 end() 同义的显式只读版

    const Segment& at(std::size_t i) const;
    const Segment& operator[](std::size_t i) const;
    Segment& at(std::size_t i);              // 可写：供变调等后处理改写 rendered；越界抛 std::out_of_range
    Segment& operator[](std::size_t i);      // 可写；越界不检查（同 std::vector）

    std::string join(std::string_view delimiter = " ") const;   // 用 delimiter 连接各行的 rendered
    std::vector<std::string> toVector() const;                  // 各行的 rendered，逐个取成 string
};
```

> `Result` **没有** `readings()`：那是宿主 `Engine` 的成员 ——
> `Outcome<std::vector<std::string>> Engine::readings(std::string_view hanzi, const EngineConfig &cfg = {}) const;`
> （`Engine.h`）。`Result` 装的是**已经转换好的行**，`readings` 问的是**一个字本身有哪些读音**。

### ErrorHandling

```cpp
enum class ErrorHandling {
    Default = 0,  // 保留转换失败的字符
    Drop = 1    // 忽略转换失败的字符
};
```

### Error / ErrorException

```cpp
struct Error {
    Code code = Code::Ok;
    std::string detail;
};

class ErrorException : public std::runtime_error {
public:
    explicit ErrorException(Error error);
    const Error &error() const noexcept;   // 也等于 what()
};
```

- 失败只有一个通道：`Outcome<T>` 承载 `Error`（`value_or_throw()` 抛 `ErrorException`）；
  捕获后仍可按 `code` 分支，`what()` 就是 `detail`。
- `Outcome` 是"要么值、要么错，不会同时"的载体 —— **使用前先判成功**，不要先取 `value()`。

### StyleDecl

文件：`Types.h`

```cpp
struct StyleDecl {
    std::string name;               // 规范小写、词间连字符："tone2" / "first-letter" / "finals-tone3"
    int alias = 0;                  // 这个 name 是哪个编号的别名，即 EngineConfig::style 承载的值
    bool isDefault = false;         // 方言自己的默认风格：调用者不指定风格时拿到的那个
};
```

- 它是**方言声明自己支持哪些风格**的载体：`Dialect::styles` 是 `std::vector<StyleDecl>`，
  `findStyle` 按 **`name`** 查表；引擎真正携带的编号是 **`alias`** —— 它必须落在
  `[0, ToneConverter::MAX_STYLE)` 内，因为它要索引 `ToneConverter::m_converts`。
- `name` 是**方言自己的风格名**（规范小写、词间连字符）；C 面同一张表的字段与它同名
  （`ext/c_api.h` 的 `cppp2_style`：`name` / `alias` / `is_default`），改名即改公开契约。

### ToneConverter（插件作者要继承的基类）

文件：`ToneConverter.h`

```cpp
class ToneConverter {
public:
    using ConvertFunc  = std::u16string(const std::u16string &pinyin, bool v_to_u, bool neutral_tone_with_5);
    using NormalizeFunc = std::u16string(const std::u16string &pinyin);
    static constexpr int MAX_STYLE = 9;      // 风格 0..8

    virtual ~ToneConverter();
    std::u16string convert(std::u16string str, int style, bool v_to_u = false, bool neutral_tone_with_5 = false);
};
```

> ⚠️ **本节原先写着"普通话和粤语引擎内部使用的类，不属于公开 API"——那半句是错的**（2026-10-10 更正）：
> 内置的 `ManTone` / `CanTone` 确实是内部实现，但**基类 `ToneConverter` 是导出类**，正是自定义方言要继承的
> 那个扩展点（内置方言也不再是公开类了，见 §5）。

- `NormalizeFunc` 是**函数指针而不是虚函数**，这是刻意的：引擎持有转换器的**对象副本**，值拷贝会切到基类
  子对象，虚函数会**静默**退回基类实现而不是方言的实现。
- `MAX_STYLE = 9`：一个方言最多声明 9 种风格，超过即声明错误。

### MandarinStyle

| 值 | 名称 | 示例 |
|----|------|------|
| 0 | `NORMAL` | `zhong guo`（无音调） |
| 1 | `TONE`（默认） | `zhōng guó`（Unicode 声调符号） |
| 2 | `TONE2` | `zho1ng guo2`（元音后数字） |
| 3 | `FIRST_LETTER` | `z g`（首字母） |
| 4 | `BOPOMOFO` | `ㄓㄨㄥ ㄍㄨㄛˊ`（注音符号） |
| 5 | `INITIALS` | `zh g`（声母；零声母音节为空串） |
| 6 | `FINALS` | `ong uo`（韵母；`gui`→`uei`、`yu`→`v`） |
| 7 | `FINALS_TONE3` | `ong1 uo2`（韵母 + 末尾声调数字） |
| 8 | `TONE3` | `zhong1 guo2`（音节后数字） |

> 数值是本库自己的历史编号，与 pypinyin 的 `Style` 数值不同名同值，不要混用。
> `INITIALS`/`FINALS`/`FINALS_TONE3` 与 pypinyin 0.55.0（`strict=True`、`v_to_u=False`）
> 逐例对齐：115 例对照表中 113 例逐字节相同，差异仅 `ê̄`/`ế` 两个叹词拼写（本库不做特判）。

### CantoneseStyle

| 值 | 名称 | 示例 |
|----|------|------|
| 0 | `NORMAL` | `zung gwok`（无音调） |
| 3 | `FIRST_LETTER` | `z g` |
| 8 | `TONE3`（默认） | `zung1 gwok3` |

### EngineConfig

```cpp
struct EngineConfig {
    Style style;                         // 默认 Style{} == -1 == "方言自己的默认"（见下）
    ErrorHandling onError = ErrorHandling::Default;
    bool includeCandidates = true;
    bool includeReading = false;          // true: 每行填 Segment::reading 的结构（按需）
    bool vToU = false;                   // true: 写作 ü 的读音保留 ü；false: 写作 v
    bool markNeutralAs5 = false;         // 轻声音标 5
};
```

> **一个配置，四层一个默认（C4/N27，2026-10-06）**。此前有三个配置类型——`MandarinConfig`
> 默认 `TONE`、`CantoneseConfig` 默认 `TONE3`、而 `EngineConfig` 默认 `0`（`NORMAL`）——于是
> **同一句话**在门面、`Engine`、C ABI 与两个壳里可能得到不同字节。现在只剩这一个 `EngineConfig`，
> `style` 的默认是**哨兵 `Style{}` == `-1` ==「方言自己的默认」**，由库在**唯一一处**求解
> （`core/Types.h` 的 `resolveStyle`）：普通话 `TONE`、粤语 `TONE3`；**方言若没有声明任何风格，
> 落回 `0`** —— 自定义方言行为不变。两个绑定壳同理（`Style.DEFAULT = -1`），所以"不指定风格"
> 在四层同义。
>
> `style` 是**值类型** `Style`，可从 `int` 与两个风格枚举**隐式**构造 ⇒
> `{.style = cpp_pinyin::v2::MandarinStyle::TONE3}` 这类写法照旧成立。**显式指定永远优先，包括显式写
> `0`**：`0` 是真实风格 `NORMAL`，不是"未指定"。
>
> `vToU`/`markNeutralAs5` 被方言能力位（`Dialect::supportsVToU` / `supportsNeutralTone5`）**掩蔽**：
> 方言不支持时该开关被忽略，而不是被重新解释。

> `vToU` 只影响**词典里写作 `ü` 本身**的读音；带声调符号的 `ǖǘǚǜ` 一律归到 `v`
> （既有契约，见 `tests/cpp/test_golden_pinyin.cpp` 的 "Mandarin TONE3 v_to_u"）。
> 例如 `女:nǚ,nü` → `vToU=true` 时读音 `nv3`、候选 `{nv3, nü}`。

### 回调类型

```cpp
// 没有"流式"查询。唯一的查询形状是返回 Result 的重载；那个"流式"重载已于 D33 删除
// （它先算完整结果再回调，且静默绕过 `Dialect::postProcess`）。

// 版内定义的类，不是 std::function 别名：别名的右侧只由标准库类型构成 ⇒ 两代是同一种类型，
// 传错代的回调能编过、只在运行时表现为行为不对。类在版本命名空间内定义 ⇒ 换代后是不同类型，
// 传错直接编译报错。lambda / 函数指针 / std::function 都能直接构造；默认构造即"未安装"。
class DisambiguationCallback {
public:
    using Body = std::function<std::string(std::string_view text, std::size_t position,
                                           std::string_view character,
                                           const std::vector<std::string> &candidates)>;
    DisambiguationCallback() = default;                       // 空 = 未安装（setDisambiguationCallback({})）
    template <class Callable> DisambiguationCallback(Callable &&body);
    explicit operator bool() const noexcept;                   // 装了没有
    std::string operator()(std::string_view, std::size_t, std::string_view,
                           const std::vector<std::string> &) const;
};
```

---

## 4. 引擎 API（`Engine`）：取方言与查询

`cpp_pinyin::v2::Engine` 是唯一的公开入口；签名与逐条语义以 `include/cpp-pinyin2/core/Engine.h` 为准（此处不复制副本，避免第三份 API 描述漂移）。

- **从注册表取方言**：`Engine::open(const Registry&, const path& dictRoot, std::string_view dialectId)` 返回 `Outcome<Engine>`。失败时 `error().code` 为 `Code::NotReady`（未知 id）、`Code::Load`（词典装载失败）或 `Code::DictIncompatible`（词典的 `dialect.json` 声明了比本库更新的 `min_library` ⇒ 该升库，而不是数据坏了），`error().detail` 带上是哪个目录、哪个文件。
- **按描述直接构造**（不看注册表，适合未注册的方言）：`Engine(const Dialect&, const path& dictDir[, path subdir])`。它没有 `Outcome` 可返回 ⇒ 构造失败当场不发声；对这样的对象**查询**会回 `Code::NotReady`，原因在 `error().detail`。
- **查询**：`Outcome<Result> query(std::string_view text, const EngineConfig& = {}) const`（另有接收字符向量的重载：那个重载把输入当作**已经切好的字符表**，每个元素必须恰好是一个字符；`Result` 见 §3）。两个重载的输入单位不同，所以 `query({"x"})` 这种花括号写法**在编译期就是二义的**（g++ 会列出两个候选）——这是有意的，写成 `query("x")` 或 `query(std::vector<std::string>{"x"})` 说明你要哪一种。
- **其余成员**：`readings` / `toSimplified` / `isPolyphonic` / `dialect()` / `loadUserDict` / `loadTraditionalDict` / `setDisambiguationCallback`。其中 `readings` 是 `Outcome<std::vector<std::string>>`（移出态与未就绪回 `Code::NotReady`；空输入是**合法空**）。
  > 语义（单一真相源在 `include/cpp-pinyin2/core/Engine.h` 的 `isPolyphonic` 注释）：**＝该字在语料中作为"词组首字"出现**（跨词组），**不等于**"这个字自己有几个读音"；判据是**两层词表任一**以它开头（`builtinStart || userStart`），**只看首字符**。

> ⚠️ 失败通道（现状）：`query` / `readings` / `toSimplified` / `loadUserDict` / `loadTraditionalDict` 都返回 `Outcome`，移出态与未就绪一律 `Code::NotReady`（`toSimplified` 在空输入时是**合法空串**，不是失败）；`load*` 的加载失败是 `Code::Load`，`detail` 带库自己的原因——这一点 C ABI 给不了（`cppp2_load_user_dict` 没有 `err` 出参）。`isPolyphonic` 仍返回**裸 `bool`**：它是谓词，只在"移出态"这一种状态下失效，调用前 `if (!dialect().id.empty())` 即可判；C ABI 用 `dialect().id.empty()` 挡在同一处。

## 5. 方言描述与注册表（`Dialect` / `Registry`）

内置方言**不再有各自的公开类**：`Mandarin`、`Cantonese` 两个类与 `include/cpp-pinyin2/dialects/Mandarin.h`、`dialects/Cantonese.h` 两个头**均已删除**。方言现在只是**描述**，由注册表按 id 装配。

- `Dialect` 至少带 `id`；默认风格是 `styles[]` 里 `isDefault` 为真的那一项（由 `defaultStyleOf` 读取），它决定 `EngineConfig{}`（默认风格哨兵）的含义，见 §3。
- `Registry::add(std::string id, DialectCreator creator, bool replace = false)` 注册某个方言的构造器；这层插件面是**冻结**的公开契约，由门禁（`tools/check_bindings.py`）看守。
- 三选一：注册过的方言用 `Engine::open`；未注册的用**描述构造**（§4 第二条）；要自己写一个方言，见 `docs/Custom Chinese Dialect.md`。

## 6. 声调转换器

文件：`ToneConverter.h` / `ManTone.h` / `CanTone.h`

普通话和粤语引擎内部使用的类，负责声调风格转换。**这两个内置实现不是公开 API；但它们共同的基类 `ToneConverter` 是导出类，自定义方言要继承它**（见 §3 的 `ToneConverter` 一节）。

- `ManTone`：9 种风格（NORMAL, TONE, TONE2, FIRST_LETTER, BOPOMOFO, INITIALS, FINALS, FINALS_TONE3, TONE3）
- `CanTone`：3 种风格（NORMAL, FIRST_LETTER, TONE3）

自定义方言支持请参见 [Custom Chinese Dialect](Custom%20Chinese%20Dialect.md)。

---

## 7. UTF 工具

文件：`U16Str.h`

```cpp
std::string u16strToUtf8str(const char16_t &ch16);      // 单 BMP 字符
std::string u16strToUtf8str(const std::u16string &u16);  // 完整字符串
std::u16string utf8strToU16str(const std::string &utf8);  // 含 surrogate pair 支持
```

- `utf8strToU16str` 支持从 1 到 4 字节的 UTF-8 序列
- 4 字节序列（U+10000 以上）映射为 UTF-16 surrogate pair
- 无效 UTF-8 序列抛出 `std::invalid_argument`

---

## 8. 词典文件格式

每个语言目录（`mandarin/`、`cantonese/`）包含：

| 文件 | 格式 | 用途 |
|------|------|------|
| `word.txt` | `字:拼音1,拼音2,...` | 单字拼音映射 |
| `phrases_map.txt` | `字:` | 标记多音字 |
| `phrases_dict.txt` | `词组:拼音1 拼音2 ...` | 多字词组消歧 |
| `user_dict.txt` | 同 `phrases_dict.txt` | 用户自定义词典 |
| `trans_word.txt` | `繁:简` | 繁简转换表 |

---

## 9. 普通话变调（ToneSandhi.h）

这是**可选的调用方后处理**，不在 `query()` 内自动执行（保持 `query()` 的等价性与性能不变）。
它改写 `rendered`，并把新声调写进 C++ 侧的 `Segment::reading.tone`（那是规则自己的输出）；不动 `text` / `candidates` / `failure_kind`，也不改变行数。
**C 面不同（有意如此）**：`cppp2_apply_tone_sandhi` 只刷新每行的 `rendered`，`cppp2_segment::syllables[k].tone` 保持词表原值——那个结构是调用方对词典数据的只读视图（`c_api.h` 定义 `tone` 为表自带数据、永不从渲染串解析）。需要变调后声调的 C 调用方从 `rendered` 里读。

```cpp
#include <cpp-pinyin2/core/ToneSandhi.h>

cpp_pinyin::v2::Registry registry;                     // 方言注册表，比引擎活得久
cpp_pinyin::v2::Engine man = cpp_pinyin::v2::Engine::open(registry, "path/to/dict", "mandarin").value();

auto res = man.query("你好世界", {.style = cpp_pinyin::v2::MandarinStyle::TONE3});

cpp_pinyin::v2::applyMandarinToneSandhi(res.value());   // "ni2 hao3 shi4 jie4"
```

```cpp
struct ToneSandhiOptions {
    bool thirdTone = true;  // 三声连读：前 N-1 个变二声
    bool bu = true;         // 不 + 四声 -> bú
    bool yi = true;         // 一 + 四声 -> yí；一 + 一/数字/句末 -> yī；其余 -> yì
};

std::size_t applyMandarinToneSandhi(Result& result, const ToneSandhiOptions& options = {});
// 返回值：被改写的行数
```

- 仅对 `TONE3` / `FINALS_TONE3`（末尾为声调数字）生效；其它风格无法还原声调，原样返回
- 只在 **`failure_kind == Ok` 的连续行**内连读，失败行（标点/ASCII）是边界
- **变调是方言属性，不是所有方言共享的概念**：它由 `Dialect::toneSandhi`（`ToneSandhiPass`）声明，目前只有
  普通话声明 `Mandarin`，粤语保持 `None`（粤语拼音形状相同，但 不/一 规则不适用）。因此 C 面
  `cppp2_apply_tone_sandhi(const cppp2_engine *engine, result, opts, &changed)` **要求传引擎**：对未声明变调的
  方言它返回 `CPPP2_ERR_INVALID_ARG` 且**一行都不改**（Python 壳抛 `CppPinyinError`，C# 壳抛
  `CppPinyinInvalidArgumentException`）。将来新增方言 = **新符号** `cppp2_apply_<dialect>_sandhi`，不改这个签名。
- 与 pypinyin 的 `tone_sandhi` 有两处已知差异（本库按规则本体实现）：
  pypinyin 的三声处理仅在该分段末尾成串时生效（`老鼠` 不生效），且未实现
  "一 + 数字/一 → 一声" 例外

---

## 10. C ABI

`include/cpp-pinyin2/ext/c_api.h` 是给**非 C++ 宿主**（C / C# / Python）的唯一入口：全部 `extern "C"`
函数（清单唯一权威：`tests/abi/expected_c_api_symbols.txt`）、不透明句柄、UTF-8 进出、库分配 / 库释放。它与 C++ API 共用同一个引擎，行为**逐字段一致**
（`[c_api]` 门禁把两扇门拿同一批输入做全量对比，1,259 条断言，含并发读与错误路径）。

```c
#include <cpp-pinyin2/ext/c_api.h>

cppp2_error err;                     /* 错误通道：code 是可分支的状态码，detail 是本次失败的原因串 */
cppp2_engine *h = cppp2_open(NULL, "mandarin", "/path/to/dict", &err);  /* 注册表可空；dict_dir 必传 */
if (h == NULL) {
    fprintf(stderr, "%s\n", err.detail ? err.detail : cppp2_code_text(err.code));
    cppp2_string_free(err.detail);   /* detail 归调用方释放 */
    return 1;
}

cppp2_query_opts opts;               /* 必须初始化：它带 struct_size */
cppp2_query_opts_init(&opts);
opts.style = 1;                     /* 与 MandarinStyle 同值：1 = TONE */

cppp2_result *res = NULL;
if (cppp2_query(h, "中国银行", &opts, &res) == CPPP2_OK) {
    for (size_t i = 0; i < res->count; ++i)
        printf("%s %s\n", res->segments[i].text, res->segments[i].rendered);
    cppp2_result_free(res);
}
cppp2_destroy(h);
```

Python（ctypes；已实测 Python 3.13 + 本机 MinGW 构建的 DLL）：

```python
import ctypes as C, os
os.add_dll_directory(r"C:\...\mingw64\bin")     # Windows：ctypes 不查 PATH 找依赖
lib = C.CDLL(r"...\libcpp-pinyin2.dll")

class CpppError(C.Structure):                   # 与头里的 cppp2_error 逐字段一致
    _fields_ = [("code", C.c_int32), ("detail", C.POINTER(C.c_char))]

lib.cppp2_open.restype = C.c_void_p              # 指针返回函数必须显式声明 restype/argtypes
lib.cppp2_open.argtypes = [C.c_void_p, C.c_char_p, C.c_char_p, C.POINTER(CpppError)]
h = lib.cppp2_open(None, b"mandarin", b"D:/path/to/dict", None)  # 错误通道可传 NULL
```

**契约要点**（完整契约见 [CONTRACT.md](CONTRACT.md) §2 错误模型与 §7 各面能力）：

| 规则 | 说明 |
|---|---|
| 返回值 | `0` = `CPPP2_OK`，其余见表尾错误码枚举；**异常不跨边界**，一律转成返回码 |
| 字符串所有权 | 库分配的只能用 `cppp2_string_free` / `cppp2_string_list_free` / `cppp2_result_free`，不要用宿主自己的 `free` |
| `dict_dir` | 必传，没有隐式默认路径；目录里缺少词典文件时 `cppp2_open` 返回 NULL 并给出原因 |
| 方言 | `cppp2_dialect_ids()` 列已注册 id；未知 id **明确失败**（不静默退回普通话） |
| 版本化结构体 | `cppp2_query_opts` 首字段是 `struct_size`，库只读"覆盖到"的字段 ⇒ 更短的旧声明照常工作；**新选项怎么加**（追加位置与尾部 padding 陷阱）：规则在 `ext/c_api.h` 的 "THE GROWTH RULE"，事故与算术在 `docs/DECISIONS.md` §2.1 ① |
| 线程 | 只读函数可多线程共用同一句柄；`cppp2_load_user_dict` / `cppp2_load_traditional_dict` 会改表，**不得与读取重叠** |
| 注册新方言 | 只有 C++ 侧能注册（`Dialect` + 调用方自持的 `Registry`）；C 侧用库自带的方言 |
| 首字符语义 | `cppp2_to_simplified` / `cppp2_is_polyphonic` 只看**首个字符**（与 C++ API 一致，不是整串转换） |

`cppp2_version()` 返回构建时的**包版本**，也就是 `find_package(cpp-pinyin2 <ver>)` 解析到的那个数；
数字只在根 `CMakeLists.txt` 的 `project(... VERSION)` 里写一次（D32/D34）。本文档抬头自 2026-10-01
起不再自带版本号；它曾自称 2.0.0 而包版本是 1.0.2。<!-- doc-claims: historical -->
本文档其余小节里出现的 `v2.x` 是**内部重构分期标签**，不是包版本。

**`cppp2_dict_dir_candidate()`（2026-10-18 新增，第 20 个 ABI 符号；**2026-10-21 起冻结面是 22 个，2026-10-22 加入 `cppp2_tone_convert` 后为 23 个**——那天新增了 `cppp2_load_user_dict_ex` 与 `cppp2_load_traditional_dict_ex`，本句说的是它当年入列时的序号）**：返回**本次构建把词典装到哪**，
按**库自身模块位置**推导（`<模块目录>/../share/cpp-pinyin2/dict`），静态串、不需要释放；静态链接时返回
`NULL`（那时"本模块"就是宿主可执行文件，属于本库明确不做的猜测）。它**不是默认路径**（`cppp2_open` 仍要求
`dict_dir`）、**不读环境变量**（那是包装层的事）、**不做校验**（目录可能不存在或装的是别的表——交给
`cppp2_open` 报出"哪个目录、哪个文件"）。用途：直接链库、又没有 CMake 包/pc 文件的宿主（Qt 应用、
Unity/UE 插件）不必把路径硬编死。决策与边界见提交历史。
**C++ 宿主用 `cpp_pinyin::v2::dictDirCandidate()`**（2026-10-20 补，审计 round 3 的 H05）：同一份缓存、同一答案，
差别只在于它返回 `std::string` 拷贝（可长期持有），C 面返回指向缓存的 `const char *`（静态构建里是空串 / `NULL`）。
此前这条查询只有 C 面和两个壳有，而它面向的正是"没有 CMake 包/pc"的宿主 —— C++ 宿主不该为了问这一句去 include C 头。

门禁：CTest `CApiSmoke`——**用 C 编译器**编译的 C99 用例，按 FFI 消费方的方式访问结构体字段。

---

## 11. 同一操作在四个面上的参数序（2026-10-20 新增）

**为什么单独一节**：同一个查询操作，四个面用的**参数顺序并不相同**，而顺序正是绑定代码最容易悄悄换掉的东西
（本仓就出过 P/Invoke 把句柄类型写成 `IntPtr` 的缺陷）。下表**逐个从公共头与源码抄下来**，不是从旧文档转抄；
它的用途是让"按名绑定"这条规则有处可核对——任何一面改了序，在这里看起来就该是错的。

| 操作 | C++ `Engine::` | C ABI `cppp2_` | Python `Engine.` | C# `Engine.` |
|---|---|---|---|---|
| 查询文本 | `query(text, cfg = {})` | `cppp2_query(engine, text, opts, out_result)` | `query(text, *, style, include_candidates, include_reading, v_to_u, mark_neutral_as_5, on_error)` | `Query(text, style, includeCandidates, vToU, markNeutralAs5, onError, includeReading)` |
| 按字符查询 | `query(chars, cfg = {})`（重载，参数是 `std::vector<std::string>`） | 无对应入口 | 无 | 无 |
| 全部读数 | `readings(hanzi, cfg = {})` | `cppp2_readings(engine, hanzi, opts, out_readings, out_count)` | `readings(hanzi, *, style, v_to_u, mark_neutral_as_5)` | `Readings(hanzi, style, vToU, markNeutralAs5)` |
| 繁→简 | `toSimplified(hanzi)` | `cppp2_to_simplified(engine, text, out_text)` | `to_simplified(text)` | `ToSimplified(text)` |
| 是否多音 | `isPolyphonic(hanzi)` | `cppp2_is_polyphonic(engine, hanzi, out_is_polyphonic)` | `is_polyphonic(hanzi)` | `IsPolyphonic(hanzi)` |
| 方言的风格表 | 无公共方法（经 `Dialect` 读） | `cppp2_style_list(engine, out_styles, out_count)` | 仅壳内部调用（校验 `declaredAliases`） | 仅壳内部调用（校验 `DeclaredAliases`） |

**怎么读这张表**：

1. **只有 C ABI 是位置绑定**——C 没有命名参数。它的顺序冻结在 `include/cpp-pinyin2/ext/c_api.h`，改序等于 ABI
   断裂（`tests/abi/expected_c_api_symbols.txt` 只冻结名字，顺序靠 `CApiSmoke` 的 C99 用例与这份表约束）。
2. **两个壳都按名绑定**：Python 用 `*` 切断位置传参，C# 靠默认值与命名实参。所以下面这处差异是**无害**的，
   别去"修"它：`on_error` 在 Python 的形参表里排在 `mark_neutral_as_5` 之后，在 C# 里排在 `includeReading`
   之前——按名调用的人不可能感知，而强行统一只会让某一面改一次签名。
3. **选项的顺序有第四种**：`cppp2_query_opts` 结构体里是 `style, on_error, include_candidates, v_to_u,
   mark_neutral_as_5, include_reading`。同样是按名读取，并且受 `struct_size` 保护（只读调用方声明过的字段，
   见第 10 节与 `c_api.h` 的「WHICH OPTIONS EACH ENTRY HONOURS」表）。
4. **`cppp2_open` 是唯一"返回值不是状态"的入口**：它返回句柄，错误经 `cppp2_error *err` 交回（`c_api.h`）；
   其余符号都返回 `int32_t` 状态、结果经出参交回（`cppp2_query` 的 `out_result` 即这一形态）。把"返回值是不是数据"
   的歧义关在**一个**符号里，正是两个壳的 `open` 与其它方法在错误处理上长得不一样的原因。

门禁：这份表是**人工核对**的（`check_public_surface` 冻结公共类型的成员声明——清单的权威是
`tests/abi/expected_public_members.txt`，此处**不重复数量**，免得成为漂移的第二副本——但没有跨语言的参数序比对）；
任何一面新增/重排参数时，同步改这里，否则这一节就成了另一种过期的证据。
