# cpp-pinyin2 API Reference (English)

> Version: the single source is `project(... VERSION)` in the root `CMakeLists.txt`; this document does not repeat the number · C++17 · Zero external dependencies

---

## Table of Contents

0. [Public surface index (automatic check)](#public-surface-index-automatic-check)
1. [Quick Start](#1-quick-start)
2. [Namespace](#2-namespace)
3. [Core Types](#3-core-types)
4. [Engine API](#4-engine-api-obtaining-a-dialect-and-querying)
5. [Dialect descriptions and the registry](#5-dialect-descriptions-and-the-registry-dialect--registry)
6. [Tone Converters](#6-tone-converters)
7. [UTF Utilities](#7-utf-utilities)
8. [Dictionary File Format](#8-dictionary-file-format)
9. [Mandarin Tone Sandhi](#9-mandarin-tone-sandhi-tonesandhih)
10. [C ABI](#10-c-abi)
11. [Parameter order across four faces](#11-the-same-operation-on-four-faces-parameter-order-added-2026-10-20)

---

## Public surface index (automatic check)

This table is what `tools/check_public_surface.py` checks: **every type declared in a public header must
have a row here**, and every name here must be found in a public header. The audience column is the part
the gate cannot judge and a human must write: `consumer` = a third party can use it directly; `plugin` =
a dialect author needs it; `internal` = exported scaffolding the API pair says is not for callers.

| Type | Audience | Declared in | Purpose |
|---|---|---|---|
| `Engine` | `consumer` | `core/Engine.h` | the query facade: open by registry id or by description |
| `Registry` | `consumer` | `ext/Registry.h` | read-only id -> creator table, held by the caller |
| `Dialect` | `plugin` | `ext/Dialect.h` | a complete description of one dialect |
| `StyleDecl` | `plugin` | `core/Types.h` | one supported style: id + frozen name + sample tone |
| `DisambiguationCallback` | `consumer` | `core/Types.h` | the polyphone callback: asks the caller which reading to use, empty string means "no opinion"; default-constructed means "none installed" |
| `ToneConverter` | `plugin` | `core/ToneConverter.h` | the base class a custom dialect derives from |
| `Syllable` | `consumer` | `core/Syllable.h` | one structured reading: spelling is the identity, tone is dialect-internal |
| `ToneKind` | `consumer` | `core/Syllable.h` | where the reading came from / whether a rule rewrote it |
| `ReadingSource` | `consumer` | `core/Syllable.h` | which table layer the reading was read from |
| `Segment` | `consumer` | `core/Types.h` | one rendered position: text + selected reading + candidates |
| `Result` | `consumer` | `core/Types.h` | a whole query: the segments plus the joined rendering |
| `Outcome` | `consumer` | `core/Types.h` | a value OR an error, never both; the single error channel |
| `Error` | `consumer` | `core/Types.h` | code + detail |
| `ErrorException` | `consumer` | `core/Types.h` | what value_or_throw() throws; what() is the detail |
| `Code` | `consumer` | `core/Types.h` | the error-code enumeration |
| `ErrorHandling` | `consumer` | `core/Types.h` | keep or drop characters that fail to convert |
| `EngineConfig` | `consumer` | `core/Types.h` | per-call options (style / reading / shaping) |
| `ToneSandhiOptions` | `consumer` | `core/ToneSandhi.h` | options for the Mandarin tone-sandhi pass |
| `ToneSandhiPass` | `consumer` | `core/ToneSandhi.h` | which tone-sandhi pass a dialect declares (`None`/`Mandarin`); `cppp2_apply_tone_sandhi` asks it before doing anything |
| `ManTone` | `internal` | `dialects/ManTone.h` | the built-in Mandarin converter (9 styles). **Installed and part of the frozen public name surface** (it is in the frozen public-header and member lists), but its audience is `internal`: usable, not recommended, no upgrade promise |
| `CanTone` | `internal` | `dialects/CanTone.h` | the built-in Cantonese converter (3 styles). **Installed and part of the frozen public name surface**, same standing as `ManTone`: usable, not recommended, no upgrade promise |
| `MandarinStyle` | `internal` | `core/Types.h` | the nine Mandarin style ids (table in section 3) |
| `CantoneseStyle` | `internal` | `core/Types.h` | the three Cantonese style ids (table in section 3) |
| `Style` | `internal` | `core/Types.h` | the numeric style id both per-dialect enums alias |

---

## 1. Quick Start

```cpp
#include <cpp-pinyin2/core/Engine.h>
#include <cpp-pinyin2/ext/Registry.h>

cpp_pinyin::v2::Registry registry;              // the dialect registry outlives the engine

// Mandarin
cpp_pinyin::v2::Engine m = cpp_pinyin::v2::Engine::open(registry, "path/to/dict", "mandarin").value();
auto res = m.query("你好世界", {.style = cpp_pinyin::v2::MandarinStyle::TONE3});
std::cout << res.value().join();  // "ni3 hao3 shi4 jie4"

// Cantonese
cpp_pinyin::v2::Engine c = cpp_pinyin::v2::Engine::open(registry, "path/to/dict", "cantonese").value();
auto jres = c.query("你好世界", {.style = cpp_pinyin::v2::CantoneseStyle::TONE3});
std::cout << jres.value().join();  // "nei5 hou2 sai3 gaai3"
```

---

## 2. Namespace

> **Cross-generation compatibility and evolution policy** (v2 / v3 / v4...): see [`docs/STABILITY.md`](STABILITY.md) section 4.5 -- this document covers API usage; the compatibility promises, the generation rules and which surfaces must carry the token live there (single authority).

All public interfaces live in `namespace cpp_pinyin`.

---

## 3. Core Types

File: `Types.h`

### Segment

```cpp
struct Segment {
    std::string text;                    // Input run (UTF-8); runs of ASCII letters/digits stay together
    Syllable reading;                    // Structure of the reading PICKED here (filled only when
                                         // includeReading=true; see below)
    std::vector<std::string> candidates; // Every reading of *this* character (dictionary order,
                                         // de-duplicated after style conversion)
    Code failure_kind = Code::Ok;        // Non-Ok when conversion failed (the old `failed` flag)
    std::string rendered;                // The row's one rendered string; equals text when failed
};
```

`reading` is the *selected* reading's structure (a failed row, or a row whose string a callback
wrote itself, keeps the default-constructed value: "the structure describes nothing"). It is
**opt-in** (`includeReading`, default `false`): the structure is not a rendering input, so the
default path must not pay a lookup and a copy per row for it -- the same rule `candidates` follows.
The switch moves only that column: `text` / `rendered` / `candidates` / `failure_kind` are
byte-identical either way. `candidates` is still strings: 7 readings in the corpora are not
invertible (`yo`, `yō`, `ǹ`, `ḿ`, `hm`, ...), so a pure structure cannot be rendered back to the
stored spelling -- see the comment on `Segment`.

Three verified rules for `candidates` (full-corpus scan of 3.6M rows, see
`docs/REQUIREMENTS.md` K9):

- Han rows: `candidates` equals `Engine::readings(character, cfg)` for the same style/options --
  `Engine` is the one facade, and Cantonese goes through the same path;
- with `includeCandidates=false` Han rows get an empty list -- **failed rows are the exception**
  and always carry `{character}`;
- a phrase/user-dictionary hit takes its `reading` from the phrase table, so it may differ from
  `candidates[0]` (`银行` reads `hang2` while `candidates[0]` is `xing2`), or not be in
  `candidates` at all.

### Syllable / ToneKind / ReadingSource

File: `Syllable.h` (`tone`'s authoritative contract is `docs/CONTRACT.md` section 3; its four sentences are
pinned by `tests/cpp/test_syllable_contract.cpp`)

```cpp
enum class ToneKind { unspecified = 0, base = 1, sandhi = 2 };   // where the reading came from, and whether a rule rewrote it
enum class ReadingSource { dictionary = 0, phrase = 1, user_dict = 2, plugin = 3 };

struct Syllable {
    std::string initial;                 // initial; "" for a zero-initial syllable
    std::string fin;                     // normalised final: tone stripped, u-umlaut written as v
    std::string spelling;                // the table's ORIGINAL spelling (UTF-8, byte for byte) -- the identity of this struct
    int tone = 0;                        // dialect-internal tone category: Cantonese 1..6 / Mandarin 0..5
    ToneKind tone_kind = ToneKind::unspecified;
    ReadingSource source = ReadingSource::dictionary;
};
```

- **`spelling` is the identity; `initial`/`fin` are only a projection of it**, and that projection is **not
  reversible**: Mandarin `hm`/`ǹ`/`ḿ`/`yo` and any non-syllable string cannot be reconstructed from the two
  fields, so a consumer that has to render again must use `spelling` rather than infer it.
- **`tone` is a dialect-internal number, NOT a value comparable across dialects**: the same integer means a
  different category in each (Mandarin 0..5, Cantonese 1..6), so never compare, order or average values read
  from two dialects. Its only authority is the dictionary's original notation, and it **must be filled even in
  NORMAL style** (NORMAL strips tone marks for rendering only).
- **`tone_kind` describes the table entry that was read**, not the rendering; when the data cannot be
  annotated all values may be `unspecified`, and **the API must not change a single output byte** because of
  it (`sandhi` is reserved and nothing produces it today).
- `ReadingSource` lists the four sources the engine actually consults, in priority order -- it is **not** a
  list of render styles. **Known gap, stated so a caller does not have to discover it:** only the first two
  have a producer today, because the phrase layer and the user dictionary are merged into one table in
  `src/dict/PhraseLayers.cpp` and a row's layer cannot be told apart afterwards -- so a reading from a user
  dictionary is reported as `phrase`, and **`user_dict` / `plugin` are never produced** (not implemented,
  not merely "not observed yet").

### Result

```cpp
class Result {
public:
    using value_type = Segment;                                 // a row is a Segment, NOT a Syllable
    using const_iterator = std::vector<Segment>::const_iterator;

    Result() = default;
    explicit Result(std::vector<Segment> syllables);

    bool empty() const noexcept;                                // no rows at all
    std::size_t size() const noexcept;                          // number of rows
    const_iterator begin() const noexcept;
    const_iterator end() const noexcept;
    const_iterator cbegin() const noexcept;                     // the explicit read-only form of begin()
    const_iterator cend() const noexcept;                       // the explicit read-only form of end()

    const Segment& at(std::size_t i) const;
    const Segment& operator[](std::size_t i) const;
    Segment& at(std::size_t i);              // mutable: for post-passes rewriting `rendered`; throws std::out_of_range
    Segment& operator[](std::size_t i);      // mutable; no bounds check (as in std::vector)

    std::string join(std::string_view delimiter = " ") const;   // joins each row's `rendered` with this delimiter
    std::vector<std::string> toVector() const;                  // each row's `rendered`, copied out as a string
};
```

> `Result` has **no** `readings()`: that member belongs to the host `Engine` --
> `Outcome<std::vector<std::string>> Engine::readings(std::string_view hanzi, const EngineConfig &cfg = {}) const;`
> (`Engine.h`). `Result` holds rows that are **already converted**; `readings` asks **which readings one
> character itself has**.

### ErrorHandling

```cpp
enum class ErrorHandling {
    Default = 0,  // Keep original characters on conversion failure
    Drop = 1    // Drop failed entries
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
    const Error &error() const noexcept;   // also its what()
};
```

- Failure has exactly one channel: `Outcome<T>` carries an `Error` (`value_or_throw()` throws
  `ErrorException`); a `catch` can still branch on `code`, and `what()` is the error's `detail`.
- `Outcome` holds a value OR an error, never both -- **test success first**, do not read `value()` upfront.

### StyleDecl

File: `Types.h`

```cpp
struct StyleDecl {
    std::string name;               // canonical, lower-case, hyphens between words: "tone2" / "first-letter"
    int alias = 0;                  // the number this name is an alias for -- what EngineConfig::style carries
    bool isDefault = false;         // the dialect's own default: what a caller gets when it asks for no style
};
```

- It is how a dialect **declares which styles it supports**: `Dialect::styles` is a `std::vector<StyleDecl>`,
  `findStyle` looks a style up by **`name`**, and the number the engine carries is **`alias`** -- it must be
  inside `[0, ToneConverter::MAX_STYLE)`, because it indexes `ToneConverter::m_converts`.
- `name` is **the dialect's own word for that style** (canonical, lower-case, hyphens between words); the C
  face's own table uses the same field names (`ext/c_api.h`'s `cppp2_style`: `name` / `alias` / `is_default`),
  so renaming one is a change to the public contract.

### ToneConverter (the base class a plugin author derives from)

File: `ToneConverter.h`

```cpp
class ToneConverter {
public:
    using ConvertFunc   = std::u16string(const std::u16string &pinyin, bool v_to_u, bool neutral_tone_with_5);
    using NormalizeFunc = std::u16string(const std::u16string &pinyin);
    static constexpr int MAX_STYLE = 9;      // styles 0..8

    virtual ~ToneConverter();
    std::u16string convert(std::u16string str, int style, bool v_to_u = false, bool neutral_tone_with_5 = false);
};
```

> **`ToneConverter` is exported**: the built-in `ManTone` / `CanTone` are internal, but the base class
> `ToneConverter` is the extension point a custom dialect subclasses (the built-ins no longer have public
> classes of their own either -- see section 5).

- `NormalizeFunc` is a **function pointer rather than a virtual, deliberately**: the engine holds an *object
  copy* of the converter, and a value copy slices to this base subobject, so a virtual would **silently** fall
  back to the base implementation instead of the dialect's.
- `MAX_STYLE = 9`: a dialect may declare at most nine styles; more is a declaration error.

### MandarinStyle

| Value | Name | Example |
|-------|------|---------|
| 0 | `NORMAL` | `zhong guo` (no tone) |
| 1 | `TONE` (default) | `zhōng guó` (Unicode diacritics) |
| 2 | `TONE2` | `zho1ng guo2` (number after vowel) |
| 3 | `FIRST_LETTER` | `z g` (first letter only) |
| 4 | `BOPOMOFO` | `ㄓㄨㄥ ㄍㄨㄛˊ` (Zhuyin) |
| 5 | `INITIALS` | `zh g` (声母; empty for zero-initial syllables) |
| 6 | `FINALS` | `ong uo` (韵母; `gui` → `uei`, `yu` → `v`) |
| 7 | `FINALS_TONE3` | `ong1 uo2` (final + tone digit) |
| 8 | `TONE3` | `zhong1 guo2` (number after syllable) |

> The numbering is this library's own; it is **not** pypinyin's `Style` numbering.
> `INITIALS`/`FINALS`/`FINALS_TONE3` are aligned case-by-case with pypinyin 0.55.0
> (`strict=True`, `v_to_u=False`): 113 of 115 reference syllables are byte-identical, the two
> exceptions being the exotic `ê` interjection spellings (no special case here).

### CantoneseStyle

| Value | Name | Example |
|-------|------|---------|
| 0 | `NORMAL` | `zung gwok` (no tone) |
| 3 | `FIRST_LETTER` | `z g` |
| 8 | `TONE3` (default) | `zung1 gwok3` |

### EngineConfig

```cpp
struct EngineConfig {
    Style style;                         // defaults to Style{} == -1 == "the dialect's own default"
    ErrorHandling onError = ErrorHandling::Default;
    bool includeCandidates = true;
    bool includeReading = false;         // true: fill Segment::reading's structure on every row
    bool vToU = false;                   // true: a reading spelled with ü keeps ü; false: it becomes v
    bool markNeutralAs5 = false;         // Mark neutral tone as 5
};
```

> **One config, one default in all four layers (C4/N27, 2026-10-06).** There used to be three config
> types -- `MandarinConfig` defaulting to `TONE`, `CantoneseConfig` to `TONE3`, and `EngineConfig` to
> `0` (`NORMAL`) -- so the SAME sentence could come out as different bytes through the facades,
> `Engine`, the C ABI and both shells. There is now one `EngineConfig`, and `style` defaults to the
> **sentinel `Style{}` == `-1` == "whatever this dialect defaults to"**, resolved by the library in
> exactly ONE place (`resolveStyle`, `core/Types.h`): Mandarin `TONE`, Cantonese `TONE3`. A dialect
> that declares no styles at all falls back to `0`, so custom dialects are unaffected. Both binding
> shells spell it the same way (`Style.DEFAULT = -1`), which is what makes "I did not ask for a
> style" mean one thing in all four layers.
>
> `style` is the value type `Style`, implicitly constructible from `int` and from both style enums,
> so `{.style = cpp_pinyin::v2::MandarinStyle::TONE3}` keeps working. **An explicit style always wins,
> including an explicit `0`**: `0` is the real style `NORMAL`, not "unset".
>
> `vToU`/`markNeutralAs5` are MASKED by the dialect's capability flags (`Dialect::supportsVToU` /
> `supportsNeutralTone5`): unsupported switches are ignored, never reinterpreted.

> `vToU` only affects readings that spell `ü` itself. Tone-marked `ǖǘǚǜ` always reduce to `v`
> (existing contract, pinned by `test_golden_pinyin.cpp` "Mandarin TONE3 v_to_u"). Example:
> `女:nǚ,nü` → with `vToU=true` the reading is `nv3` and the candidates are `{nv3, nü}`.

### Callback types

```cpp
// There is no streaming query. The result-returning overloads are the only query shape; the
// "streaming" overload that used to exist was removed (D33) because it computed the whole result
// before calling back once per syllable and silently skipped `Dialect::postProcess`.

// A class defined inside the version namespace, not a std::function alias: an alias whose right-hand
// side names only standard-library types denotes the SAME type in every generation, so the other
// generation's callback would compile and only misbehave at runtime. Per generation it is a distinct
// type, so passing the wrong one is a compile error. A lambda, a function pointer or a std::function
// all construct it; a default-constructed one means "none installed".
class DisambiguationCallback {
public:
    using Body = std::function<std::string(std::string_view text, std::size_t position,
                                           std::string_view character,
                                           const std::vector<std::string> &candidates)>;
    DisambiguationCallback() = default;                       // empty = not installed
    template <class Callable> DisambiguationCallback(Callable &&body);
    explicit operator bool() const noexcept;                  // is one installed
    std::string operator()(std::string_view, std::size_t, std::string_view,
                           const std::vector<std::string> &) const;
};
```

---

## 4. Engine API: obtaining a dialect and querying

`cpp_pinyin::v2::Engine` is the only public entry point; the signatures and their exact semantics live in `include/cpp-pinyin2/core/Engine.h` (not copied here -- a third copy is a third thing to drift).

- **From the registry**: `Engine::open(const Registry&, const path& dictRoot, std::string_view dialectId)` returns `Outcome<Engine>`. On failure `error().code` is `Code::NotReady` (unknown id), `Code::Load` (dictionary failed to load) or `Code::DictIncompatible` (the dictionary's `dialect.json` asks for a newer library via `min_library` -- upgrade the library, the data is not broken), and `error().detail` names the directory and the files.
- **From a description** (bypasses the registry; for dialects that are not registered): `Engine(const Dialect&, const path& dictDir[, path subdir])`. It has no `Outcome` to return, so a failed construction says nothing at the time; **querying** such an engine answers `Code::NotReady`, with the reason in `error().detail`.
- **Query**: `Outcome<Result> query(std::string_view text, const EngineConfig& = {}) const` (the other overload takes a vector of ALREADY-SPLIT characters, one per element -- so writing `query({"x"})` is ambiguous at compile time, deliberately: name the unit, `query("x")` vs `query(std::vector<std::string>{"x"})`; `Result` is in section 3).
- **Other members**: `readings` / `toSimplified` / `isPolyphonic` / `dialect()` / `loadUserDict` / `loadTraditionalDict` / `setDisambiguationCallback`. `readings` is `Outcome<std::vector<std::string>>` (a moved-from or not-ready engine answers `Code::NotReady`; empty input stays a legal empty result).
  > Meaning (the authority is the `isPolyphonic` comment in `include/cpp-pinyin2/core/Engine.h`): **the first character starts a phrase in the corpus**; the test is `builtinStart || userStart`, so either layer counts, and only the first character is examined.

> Note -- the failure channel today: `query` / `readings` / `toSimplified` / `loadUserDict` /
> `loadTraditionalDict` all return `Outcome`; a moved-from or not-ready engine answers
> `Code::NotReady` (`toSimplified` answers a LEGAL empty string for empty input, which is not a
> failure), and a failed load answers `Code::Load` with the library's own detail -- something the C
> ABI cannot deliver (`cppp2_load_user_dict` has no `err` out-parameter). `isPolyphonic` still
> returns a bare `bool`: it is a predicate whose only unusable state is the moved-from one, which
> `dialect().id.empty()` already answers -- the same test the C ABI applies.

## 5. Dialect descriptions and the registry (`Dialect` / `Registry`)

The built-in dialects no longer have public classes of their own: the `Mandarin` and `Cantonese` classes and the headers `include/cpp-pinyin2/dialects/Mandarin.h` and `dialects/Cantonese.h` are all gone. A dialect is now a **description**, and the registry assembles one by id.

- A `Dialect` carries at least `id`, and its default style is the `styles[]` entry with `isDefault` set (read by `defaultStyleOf`); that entry is what `EngineConfig{}` (the default-style sentinel) means -- see section 3.
- `Registry::add(std::string id, DialectCreator creator, bool replace = false)` registers one dialect factory; this plugin surface is a **frozen** public contract, watched by a gate (`tools/check_bindings.py`).
- Three ways in: `Engine::open` for a registered dialect, a **description construction** for an unregistered one (section 4, second bullet), and `docs/Custom Chinese Dialect.md` if you want to write a dialect of your own.

## 6. Tone Converters

File: `ToneConverter.h` / `ManTone.h` / `CanTone.h`

Internal classes used by the Mandarin and Cantonese engines to handle tone style conversion. **These two
built-in implementations are not part of the public API; the base class they share, `ToneConverter`, is an
exported class that a custom dialect subclasses** (see the `ToneConverter` entry in section 3).

- `ManTone`: 9 styles (NORMAL, TONE, TONE2, FIRST_LETTER, BOPOMOFO, INITIALS, FINALS, FINALS_TONE3, TONE3)
- `CanTone`: 3 styles (NORMAL, FIRST_LETTER, TONE3)

For custom dialect support, see [Custom Chinese Dialect](Custom%20Chinese%20Dialect.md).

---

## 7. UTF Utilities

File: `U16Str.h`

```cpp
std::string u16strToUtf8str(const char16_t &ch16);      // Single BMP char
std::string u16strToUtf8str(const std::u16string &u16);  // Full string
std::u16string utf8strToU16str(const std::string &utf8);  // With surrogate pair support
```

- `utf8strToU16str` handles 1-to-4-byte UTF-8 sequences
- 4-byte sequences (U+10000+) encoded as UTF-16 surrogate pairs
- **Malformed UTF-8 becomes U+FFFD — one replacement per malformed code point, never an exception.**
  1.x threw `std::invalid_argument`; 2.0 makes the conversion total on purpose, `src/U16Str.cpp` carries the
  reason and `tests/cpp/test_boundary_utf8.cpp` pins it. A 1.x caller that used the throw as validation must
  now check for U+FFFD. (Until 2026-10-27 this bullet described the 1.x behaviour.)

---

## 8. Dictionary File Format

Each language directory (`mandarin/`, `cantonese/`) contains:

| File | Format | Purpose |
|------|--------|---------|
| `word.txt` | `char:reading1,reading2,...` | Single-character reading mapping |
| `phrases_map.txt` | `char:` | Polyphonic character marker |
| `phrases_dict.txt` | `phrase:rd1 rd2 rd3 rd4` | Multi-character disambiguation |
| `user_dict.txt` | Same as `phrases_dict.txt` | Custom user entries |
| `trans_word.txt` | `trad:sim` | Traditional-to-Simplified map |

---

## 9. Mandarin Tone Sandhi (ToneSandhi.h)

An **optional caller-side post-pass** -- it is never run inside `query()`, so query behaviour
and performance stay unchanged. It rewrites `rendered` and writes the new tone into the C++ side's `Segment`
(the rule's own output). `text`, `candidates`, `failure_kind` and the row count are untouched.

```cpp
#include <cpp-pinyin2/core/ToneSandhi.h>

cpp_pinyin::v2::Registry registry;                     // the dialect registry outlives the engine
cpp_pinyin::v2::Engine man = cpp_pinyin::v2::Engine::open(registry, "path/to/dict", "mandarin").value();

auto res = man.query("你好世界", {.style = cpp_pinyin::v2::MandarinStyle::TONE3});

cpp_pinyin::v2::applyMandarinToneSandhi(res.value());   // "ni2 hao3 shi4 jie4"
```

```cpp
struct ToneSandhiOptions {
    bool thirdTone = true;  // third-tone runs: all but the last become 2nd tone
    bool bu = true;         // 不 + 4th tone -> bú
    bool yi = true;         // 一 + 4th tone -> yí; 一 + 一/digit/end -> yī; otherwise -> yì
};

std::size_t applyMandarinToneSandhi(Result& result, const ToneSandhiOptions& options = {});
// returns the number of rows rewritten
```

- only applies to `TONE3` / `FINALS_TONE3` readings (they end in a tone digit); other styles
  cannot recover the tone and are returned unchanged
- runs are broken by rows with `failure_kind != Ok` (punctuation, ASCII)
- **tone sandhi is a property of a DIALECT, not a shared core concept**: it is declared by
  `Dialect::toneSandhi` (`ToneSandhiPass`), and only Mandarin declares `Mandarin` today (Cantonese stays `None`:
  jyutping readings look the same, but the 不/一 rules do not apply). That is why the C entry
  `cppp2_apply_tone_sandhi(const cppp2_engine *engine, result, opts, &changed)` **requires the engine**: for a
  dialect that declares no pass it returns `CPPP2_ERR_INVALID_ARG` and leaves every row untouched (the Python
  shell raises `CppPinyinError`, the C# shell `CppPinyinInvalidArgumentException`). A new dialect's pass arrives
  as a NEW symbol, `cppp2_apply_<dialect>_sandhi` -- this signature never changes again
- two known divergences from pypinyin's `tone_sandhi` (this library implements the rules as
  stated): pypinyin's third-tone pass only fires when the run ends its segment (`老鼠` is left
  alone), and it lacks the "一 + digit/一 → first tone" exception

---

## 10. C ABI

`include/cpp-pinyin2/ext/c_api.h` is the single entry point for non-C++ hosts (C / C# / Python): all
`extern "C"` functions (list authority: `tests/abi/expected_c_api_symbols.txt`), an opaque handle,
UTF-8 in and out, library-allocated and library-freed strings. It wraps the same engine as the C++
API, and the two are **field-for-field identical** -- the `[c_api]` gate runs the same inputs through
both doors (1,259 assertions, including concurrent reads and error paths).

```c
#include <cpp-pinyin2/ext/c_api.h>

cppp2_error err;                     /* the error channel: code is what you branch on, detail says why */
cppp2_engine *h = cppp2_open(NULL, "mandarin", "/path/to/dict", &err);  /* registry may be NULL; dict_dir is mandatory */
if (h == NULL) {
    fprintf(stderr, "%s\n", err.detail ? err.detail : cppp2_code_text(err.code));
    cppp2_string_free(err.detail);   /* detail belongs to the caller */
    return 1;
}

cppp2_query_opts opts;               /* must be initialised: it carries struct_size */
cppp2_query_opts_init(&opts);
opts.style = 1;                     /* same values as MandarinStyle: 1 = TONE */

cppp2_result *res = NULL;
if (cppp2_query(h, "中国银行", &opts, &res) == CPPP2_OK) {
    for (size_t i = 0; i < res->count; ++i)
        printf("%s %s\n", res->segments[i].text, res->segments[i].rendered);
    cppp2_result_free(res);
}
cppp2_destroy(h);
```

Python (ctypes; measured with Python 3.13 against a MinGW-built DLL):

```python
import ctypes as C, os
os.add_dll_directory(r"C:\...\mingw64\bin")     # Windows: ctypes does not search PATH
lib = C.CDLL(r"...\libcpp-pinyin2.dll")

class CpppError(C.Structure):                   # field for field the cppp2_error of the header
    _fields_ = [("code", C.c_int32), ("detail", C.POINTER(C.c_char))]

lib.cppp2_open.restype = C.c_void_p              # a pointer-returning function needs restype/argtypes
lib.cppp2_open.argtypes = [C.c_void_p, C.c_char_p, C.c_char_p, C.POINTER(CpppError)]
h = lib.cppp2_open(None, b"mandarin", b"D:/path/to/dict", None)  # the error channel may be NULL
```

**Contract highlights** (the full contract is [CONTRACT.md](CONTRACT.md) §2 for the error model and §7 for what each face can do):

| Rule | Detail |
|---|---|
| Return values | `0` = `CPPP2_OK`, otherwise see the error enum at the end of `c_api.h`; **no exception crosses the boundary** |
| String ownership | free library-allocated memory only with `cppp2_string_free` / `cppp2_string_list_free` / `cppp2_result_free` |
| `dict_dir` | mandatory, no implicit default path; a directory without the dictionary files makes `cppp2_open` return NULL with a reason |
| Dialects | `cppp2_dialect_ids()` lists registered ids; an unknown id **fails loudly** (never silently falls back to Mandarin) |
| Versioned structs | `cppp2_query_opts` starts with `struct_size` and only covered fields are read, so a shorter older declaration keeps working; **how a new option is added** (where it goes, and the trailing-padding trap): the rule is `ext/c_api.h`'s "THE GROWTH RULE", the incident and the arithmetic are in `docs/DECISIONS.md` §2.1 |
| Threads | read-only functions may share one handle across threads; `cppp2_load_user_dict` / `cppp2_load_traditional_dict` mutate tables and **must not overlap reads** |
| Registering a dialect | C++ only (`Dialect` + `Registry`, held by the caller); C consumers use the dialects the library itself knows |
| First character only | `cppp2_to_simplified` / `cppp2_is_polyphonic` act on the **first character** (same as the C++ API, not a whole-string conversion) |

`cppp2_version()` returns the **package version** -- the number `find_package(cpp-pinyin2 <ver>)`
resolves. It is written down once, in the root `CMakeLists.txt`'s `project(... VERSION)` (D32/D34).
As of 2026-10-01 this document's header carries no version of its own. `v2.x` elsewhere in this document
is an internal refactoring phase label, not the package version. <!-- doc-claims: historical -->

**`cppp2_dict_dir_candidate()` (added 2026-10-18, the 20th ABI symbol; **the frozen face holds 22 as of 2026-10-21** -- the two `load_*_dict_ex` entries joined that day, so this is the ordinal it took when it was added)**: where *this build* installed the
dictionary tables, derived from the **library's own module location** (`<module dir>/../share/cpp-pinyin2/dict`).
Static string, never freed; `NULL` in a static build (there "this module" would be the host executable, which
is exactly the guess this library refuses to make). It is **not a default** (`cppp2_open` still requires
`dict_dir`), **not an environment override** (reading `CPP_PINYIN2_DICT_DIR` is the shell's job) and **not a
check** (call `cppp2_open` and let it name the missing file). It exists for hosts that link the library with no
CMake package and no `.pc` file -- a Qt application, a Unity/UE plugin -- so they need not hard-code a path.
**C++ hosts call `cpp_pinyin::v2::dictDirCandidate()`** (added 2026-10-20): same cache, same
answer; the difference is that it returns a `std::string` copy you may keep, while the C face returns a
`const char *` into that cache (empty / `NULL` in a static build). Until then this question had a C face and
both shells and no C++ face -- while the hosts it exists for are precisely the ones without a CMake package.
Decision and boundaries: the process record that carried this ruling lived under `tests/data/testdata/` and was
removed in the 2026-10-22 cleanup; the boundary that still binds is the sentence above.

Gate: the CTest entry `CApiSmoke`, a C99 translation unit compiled by the **C compiler**, touching the
struct fields the way an FFI consumer does.

---

## 11. The same operation on four faces: parameter order (added 2026-10-20)

**Why this is its own section**: the same query operation does **not** carry the same parameter order on the four
faces, and order is exactly what binding code swaps without noticing (this repository has already shipped a
P/Invoke declaration that spelled the handle as `IntPtr`). The table below was **transcribed from the public
headers and the sources**, not from older prose; its job is to make "bind by name" checkable -- if any face
reorders its parameters, this table should look wrong.

| Operation | C++ `Engine::` | C ABI `cppp2_` | Python `Engine.` | C# `Engine.` |
|---|---|---|---|---|
| Query text | `query(text, cfg = {})` | `cppp2_query(engine, text, opts, out_result)` | `query(text, *, style, include_candidates, include_reading, v_to_u, mark_neutral_as_5, on_error)` | `Query(text, style, includeCandidates, vToU, markNeutralAs5, onError, includeReading)` |
| Query characters | `query(chars, cfg = {})` (overload, `std::vector<std::string>`) | no counterpart | none | none |
| All readings | `readings(hanzi, cfg = {})` | `cppp2_readings(engine, hanzi, opts, out_readings, out_count)` | `readings(hanzi, *, style, v_to_u, mark_neutral_as_5)` | `Readings(hanzi, style, vToU, markNeutralAs5)` |
| Traditional -> simplified | `toSimplified(hanzi)` | `cppp2_to_simplified(engine, text, out_text)` | `to_simplified(text)` | `ToSimplified(text)` |
| Polyphonic? | `isPolyphonic(hanzi)` | `cppp2_is_polyphonic(engine, hanzi, out_is_polyphonic)` | `is_polyphonic(hanzi)` | `IsPolyphonic(hanzi)` |
| A dialect's style table | no public method (read through `Dialect`) | `cppp2_style_list(engine, out_styles, out_count)` | shell-internal only (`declaredAliases` validation) | shell-internal only (`DeclaredAliases` validation) |

**How to read it**:

1. **Only the C ABI is position-bound** -- C has no named arguments. Its order is frozen in
   `include/cpp-pinyin2/ext/c_api.h`; reordering it is an ABI break (`tests/abi/expected_c_api_symbols.txt`
   freezes names only, so order is held by the C99 `CApiSmoke` case and by this table).
2. **Both shells bind by name**: Python cuts positional calls with `*`, C# relies on defaults and named
   arguments. That makes the following difference **harmless -- do not "fix" it**: `on_error` sits after
   `mark_neutral_as_5` in Python's parameter list and before `includeReading` in C#'s. A named caller cannot
   observe it, and aligning them would only re-sign the surface on one face.
3. **There is a fourth order**: the fields of `cppp2_query_opts` run `style, on_error, include_candidates,
   v_to_u, mark_neutral_as_5, include_reading`. Also read by name, and guarded by `struct_size` (only fields the
   caller declared are read -- see section 10 and the "WHICH OPTIONS EACH ENTRY HONOURS" table in `c_api.h`).
4. **`cppp2_open` is the one entry whose return value is NOT a status**: it returns the handle and reports the
   failure through `cppp2_error *err` (`c_api.h`); every other symbol returns an `int32_t` status and hands its
   result back through an out-parameter (`cppp2_query`'s `out_result` is that shape). Parking the "is the return
   value data?" ambiguity in ONE symbol is why `open` looks different from every other method on both shells.

Gate: this table is **checked by a human** (`check_public_surface` freezes the member declarations of the public
types -- the authoritative list is `tests/abi/expected_public_members.txt`, and its size is deliberately not
repeated here, because a second copy of that number is how this sentence went stale -- but there is no
cross-language parameter-order comparison). When a face gains or reorders a parameter, update
this section -- otherwise the section becomes another piece of stale evidence.
