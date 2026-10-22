# Custom Chinese Dialect

cpp-pinyin2 ships with Mandarin and Cantonese support out of the box. You can add your own dialect by providing dictionary files, a tone converter, and a thin wrapper class. This guide walks through each step.

## How the Extension Mechanism Works

The library has two layers:

1. **Dictionary files** in one directory per language hold character-to-pinyin mappings. Your dialects live
   wherever you keep them; this repository's own two live under `data/dialects/`.
2. **Internal engine** (`EngineImpl`) loads dictionaries and performs conversion. It accepts an external `ToneConverter` for style dispatch.

Instead of subclassing, the built-in dialects (`Mandarin`, `Cantonese`) compose an `EngineImpl` with their tone converter. Your dialect follows the same pattern.

## Step 1: Create Dictionary Files

Use [pinyin-makedict](https://github.com/wolfgitpr/pinyin-makedict) to generate your dictionary, and place the
output in a directory of your own -- **outside this repository**, for instance `D:\my-dialects\hokkien\`. The
folder name is the subdirectory the engine loads, and the path you pass to your wrapper is the directory above
it. Putting a new dialect under `data/dialects/` is not the supported route: that directory is the list of
dialects this repository itself ships, and CI fails when a third one appears there.

For example, to add Hokkien: `D:\my-dialects\hokkien\`, passed as `D:\my-dialects`.

The folder needs the files listed below. `dialect.json` is what makes the list authoritative: it names the required tables, the optional ones and the assets, so a missing required table is an error rather than a partially loaded dictionary. The four `required_tables` are required; `user_dict.txt` is optional (the engine starts with an empty user dictionary when it is absent), and `License.txt` is an asset, not a table:

| File | Purpose |
|------|---------|
| `word.txt` | Single-character pinyin lookup. One entry per line. |
| `phrases_dict.txt` | Phrase-level pinyin. Maps multi-character words to their pinyin sequence. |
| `phrases_map.txt` | Polyphonic character markers. Flags characters with multiple readings. |
| `user_dict.txt` | User-defined overrides. Create manually (can be empty). Loadable at runtime via `loadUserDict()`. |
| `trans_word.txt` | Traditional-to-simplified character mapping. |

## Step 2: Create a ToneConverter Subclass

`ToneConverter` is the base class. It holds an array of function pointers, one per output style. Your subclass defines which styles exist and provides the conversion functions.

```cpp
// HokTone.h  -- your own header, outside this repository (the tree from Step 1)
#pragma once

#include <cpp-pinyin2/core/ToneConverter.h>

namespace cpp_pinyin::v2 {

class HokTone final : public ToneConverter {
public:
    enum Style {
        NORMAL = 0,
        TONE3 = 8
    };

    HokTone() {
        m_converts[static_cast<int>(NORMAL)] = tone3ToNormal;
        m_converts[static_cast<int>(TONE3)] = nullptr; // passthrough raw pinyin
    }

    static std::u16string tone3ToNormal(
        const std::u16string &pinyin,
        bool v_to_u,
        bool neutral_tone_with_five);
};

} // namespace cpp_pinyin
```

A subclass defines its own style constants: `ToneConverter` used to carry a `Style` enum (`NORMAL` / `TONE`) that nothing referenced and that duplicated the library's own numbering, and it was deleted on 2026-10-01 (D27). An **in-tree** dialect does not redeclare the values at all — `ManTone::Style` / `CanTone::Style` are `using`-aliases of `MandarinStyle` / `CantoneseStyle` in `Types.h`, the single authority for the numbering; an **out-of-tree** dialect keeps its own enum, as the example above does, because it cannot extend those enums.

Each conversion function takes a tone-marked pinyin string (UTF-16) and returns the reformatted version. Look at [ManTone.h](../include/cpp-pinyin2/dialects/ManTone.h) for a full example with nine styles, or [CanTone.h](../include/cpp-pinyin2/dialects/CanTone.h) for a minimal setup that registers two conversions (`NORMAL`, `FIRST_LETTER`) and leaves `TONE3` as a passthrough — three styles in total.

### External readings: register `m_normalize`

A dialect dictionary (`word.txt`, `phrases_dict.txt`, `user_dict.txt`) may write readings in a
notation that differs from what the engine stores internally. `ToneConverter::normalizeReading`
is the hook that bridges the two, and it must be registered **per dialect**:

```cpp
class HokTone final : public cpp_pinyin::v2::ToneConverter {
public:
    HokTone() {
        m_converts[static_cast<int>(NORMAL)] = tone3ToNormal;
        // Dialect dictionaries are stored as e.g. "zhi4" -> keep them verbatim:
        // (the base class already does this -- only override when you need a rewrite)
        m_normalize = &identityLike; // or simply do not touch m_normalize at all
    }
    static std::u16string identityLike(const std::u16string &pinyin);
};
```

Mandarin does need a rewrite and registers `&ManTone::normalizeReading` (`tone3ToTone`):
`user_dict.txt` says `仄声:ze4 sheng1`, the engine stores `zè shēng`. Cantonese leaves the hook
alone because its dictionaries already use `jyut6`-style tone digits.

Two rules follow from how the engine stores the converter:

* **It is a function pointer, not a virtual.** The engine holds a `ToneConverter` *object* copied
  from your subclass, and that copy slices to the base subobject — a virtual `normalizeReading`
  would silently call the base implementation. Same contract as `m_converts`.
* **It must be passed at construction.** `EngineImpl`'s constructor takes the converter and loads
  every dictionary itself, so there is no setter: swapping the converter afterwards would leave
  already-loaded user dictionaries normalized by the old hook.

## Step 3: Describe the Dialect

No wrapper class is needed, and you do not have to fork this repository. Describe your dialect once
and hand it to `cpp_pinyin::v2::Engine::open`:

```cpp
// my_dialect.cpp -- lives in YOUR project, not in this repository
#include <cpp-pinyin2/ext/Dialect.h>
#include <cpp-pinyin2/ext/Registry.h>

#include "HokTone.h"  // Step 2's header -- yours, so a quoted include

#include <memory>

namespace {

std::shared_ptr<const cpp_pinyin::v2::Dialect> makeHokkien() {
    auto dialect = std::make_shared<cpp_pinyin::v2::Dialect>();
    dialect->id = "hokkien";
    dialect->dictSubDir = "hokkien";  // optional; defaults to `id`
    dialect->tone = cpp_pinyin::v2::HokTone{};  // the converter from Step 2
    return dialect;
}

} // namespace
```

There is no registration macro. A registry belongs to whoever declares it, and the constructor
already carries the two built-in dialects:

```cpp
cpp_pinyin::v2::Registry registry;                                  // built-ins on board
registry.add("hokkien", makeHokkien);                           // false if the id is taken

// `open` answers with an `Outcome`, never with an `Engine`: name the type and check it
// before touching a value (Step 4 shows the whole error channel).
cpp_pinyin::v2::Outcome<cpp_pinyin::v2::Engine> opened =
    cpp_pinyin::v2::Engine::open(registry, dictRoot, "hokkien");
if (!opened.has_value()) { /* unknown id, or a dictionary that would not load */ }
cpp_pinyin::v2::Engine g2p = std::move(opened).value();              // Engine is move-only
```

That is the whole mechanism: `add()` puts the creator in **your** table, `Engine::open` resolves the
id in **the table you passed**. A second registry shares nothing with this one, so a dialect is
reachable exactly where you registered it — nothing about it is process-global.

Facts worth knowing before you fill in the fields:

* **A `Dialect` carries no dictionary path.** The path is a constructor argument of `Engine`, so two
  engines of the same dialect can read two different roots at once. That is the whole reason this
  interface exists in this shape — see `docs/REQUIREMENTS.md` FR-4.5.
* `dictSubDir` is the folder under the dictionary root that holds your tables. Leave it empty to use
  the id as the folder name.
* `supportsVToU` / `supportsNeutralTone5` declare which of the two `EngineConfig` switches mean
  something for your dialect. A dialect that does not set them **ignores** the switch instead of
  having its output silently reinterpreted — this is how the built-in Cantonese behaves.
* `postProcess` is an optional pass over a finished `Result`. Since D33 it applies to **every**
  `query` overload there is, because the "streaming" overload that used to bypass it was deleted. Use
  it for caller-side fixes. Do **not** use it to re-implement reading selection: the disambiguation window is frozen and
  a second implementation of it will disagree with the engine on exactly the polyphones you care
  about.
* `tone` is copied into the engine by value, which slices it to the `ToneConverter` base. That is
  intentional and harmless: every dialect behaviour lives in the base's function-pointer table
  (`m_converts`, `m_normalize`), never in a virtual. Do not turn it into a virtual.

## Step 4: Usage

```cpp
#include <cpp-pinyin2/core/Engine.h>
#include <cpp-pinyin2/ext/Registry.h>
#include <filesystem>
#include <iostream>

// The rest of my_dialect.cpp from Step 3 (`makeHokkien` is in the unnamed namespace above it),
// plus the header from Step 2 that defines `HokTone`.
int main() {
    // By registered id -- the description from Step 3, in a registry this program owns.
    cpp_pinyin::v2::Registry registry;
    registry.add("hokkien", makeHokkien);

    // `open` answers with an `Outcome<Engine>`: a value, or a `Code` plus its `detail`. There is
    // no conversion from it to `Engine`, and this is the *only* channel it uses -- the reason
    // travels inside the failure, not in a separate accessor.
    auto opened =
        cpp_pinyin::v2::Engine::open(registry, std::filesystem::current_path() / "dict", "hokkien");
    if (!opened.has_value()) {
        std::cerr << cpp_pinyin::v2::codeText(opened.error().code) << ": "
                  << opened.error().detail << std::endl;
        return 1;
    }
    cpp_pinyin::v2::Engine g2p = std::move(opened).value();  // Engine is move-only

    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = static_cast<int>(cpp_pinyin::v2::HokTone::Style::NORMAL);
    cfg.includeCandidates = true;

    const auto res = g2p.query("你好世界", cfg);  // the same `Outcome`; a ready engine has rows
    for (const auto &item : res.value())
        std::cout << item.text << " -> " << item.rendered << std::endl;

    // Load an additional user dictionary at runtime (this instance only).
    g2p.loadUserDict("path/to/custom_dict.txt");
    return 0;
}
```

The description-first constructor is also available when you do not want to register anything, or
when you want to be immune to a later registration under the same id:

```cpp
cpp_pinyin::v2::Dialect hokkien = *makeHokkien();                 // a plain value
cpp_pinyin::v2::Engine other(hokkien, dictRoot);                  // description-first: registry untouched
cpp_pinyin::v2::Engine third(hokkien, dictRoot, "hokkien_alt");   // custom dictionary subdirectory
```

Two failure modes are loud by design: `open` never hands back a half-built engine. An unknown
dialect id is an `Outcome` with no value, `error().code == Code::NotReady` and `error().detail`
naming the id -- there is no fallback to Mandarin; a dictionary that fails to parse is
`Code::Load`, with the offending files and the directory in `error().detail`. The two description
constructors have no `Outcome` to return, so they fail without saying so at construction and answer on the
query instead: querying such an engine is `Code::NotReady`, with `error().detail` naming the directory and
the files it could not load.

### The registry surface

`cpp-pinyin2/ext/Registry.h` is small, and every line of it is yours to use. The whole public surface is
frozen by a gate (`tests/abi/expected_plugin_surface.txt`, checked by `tools/check_bindings.py`), and
this is what each piece means for a plugin author:

* `Registry()` — the constructor. It registers the two built-in dialects into *this object*, so a
  registry you declare is immediately useful. There is no singleton, no `builtins()` factory and no
  process-level table: two registries share nothing.
* `Registry::add(std::string id, DialectCreator creator, bool replace = false)` — registers your
  dialect. Returns **false** when the id is already taken and `replace` is false; it does **not**
  overwrite, so check the return value.
* `Registry::resolve(id)` — how `Engine::open` turns an id into a description. Returns `nullptr` for an
  unknown id; `Engine::open` then answers with an `Outcome` that has no value, `Code::NotReady` and a
  `detail` naming the id. There is deliberately **no fallback** to Mandarin.
* `Registry::ids()` — every registered id, **sorted** (ascending byte order, not registration order),
  for diagnostics and for tests that want to assert what a registry actually contains.
* `DialectCreator` — a callable returning a description. **It is invoked on every `resolve`**, and each
  call must return a freshly built, immutable description. That is what makes an already constructed
  engine immune to a later registration under the same id.
* The copy/move constructors and assignments of `Registry` are **deleted** on purpose: a registry is a
  table with a mutex in it, not a value. Hold one object (or a reference to one) and keep registering
  into *that* one — a copy would silently become a second, empty-of-your-dialect table.

Four traps that cost real debugging time, in the order people hit them:

1. **Reusing a built-in id is refused, not merged.** `add("mandarin", ...)` returns **false** (pass
   `replace = true` if you really mean to shadow it). Pick a new id; the built-in ids are reserved by
   the dialect-set gate. The return value is the whole signal — nothing is printed for you.
2. **A registry that is not the one you passed is not consulted.** `Engine::open(registry, root, id)`
   resolves the id in *that* registry only. Register into a local registry that goes out of scope, or
   into a different one, and the open fails with `unknown dialect: your-id`. Registration is ordinary
   code you run, not something the linker does for you.
3. **Registration is not reachable from C.** The C ABI has no registration entry point on purpose (see
   the comment in `include/cpp-pinyin2/ext/c_api.h`); a C, Python or C# host can only *select* among the
   dialects the library itself knows (C enumerates them with `cppp2_dialect_ids`). A third-party id
   exists only in the C++ code that registered it, in the registry it registered it into.
4. **The dictionary root still has to be right.** Registration only supplies the description; the
   dictionary files are found relative to the root you pass to `Engine::open`, using `dictSubDir` when
   it is set and the dialect id otherwise.

### Threading and lifetime

* After an engine is constructed, concurrent **read-only** calls (`query` / `readings` /
  `toSimplified` / `isPolyphonic`) are safe on a shared instance.
* `loadUserDict` / `loadTraditionalDict` / `setDisambiguationCallback` mutate tables that readers
  touch. Overlapping them with a read from another thread is a data race today (measured: it
  corrupts the heap). Finish loading before you share the engine.
* Engines share nothing: separate roots, separate dialect descriptions, separate user dictionaries.
  `tests/cpp/test_engine_dialect.cpp` builds four threads' worth of engines at once and compares
  every query against a single-threaded baseline.

If you want a typed facade of your own, the pattern is mechanical: hold an `Engine`, forward to it, and
keep the failure channel intact -- a wrapper that swallows `Outcome` and returns a bare value gives that up.
One thing worth copying from the C ABI (`src/c_api_query.cpp`): it tests `dialect().id.empty()` before it
touches a handle, because a moved-from engine has no impl. Since 2026-10-19 only ONE value-returning method
has no error channel: `isPolyphonic`, a predicate. `query` (both overloads), `readings`, `toSimplified` and
the two loaders all answer `Outcome` and say `Code::NotReady` for a moved-from object -- so the old advice
("prefer `query` wherever a moved-from object is possible") now applies only where you want the predicate.

### Why not `EngineImpl`

`EngineImpl` (the class the old revision of this document told you to wrap) lives in `src/`, is
neither installed nor exported, and therefore cannot be used from outside this repository at all —
the only way to reach it was to fork the repository and add a facade pair. `Engine` is the supported
entry point; `EngineImpl` remains an implementation detail and is not covered by any compatibility
promise.

## Reference Implementations

| Dialect | ToneConverter | Entry point | Dictionary Folder |
|---------|--------------|-------------|-------------------|
| Mandarin | [ManTone](../include/cpp-pinyin2/dialects/ManTone.h) | `Engine::open(registry, root, "mandarin")` | `data/dialects/mandarin/` |
| Cantonese | [CanTone](../include/cpp-pinyin2/dialects/CanTone.h) | `Engine::open(registry, root, "cantonese")` | `data/dialects/cantonese/` |

> **2026-10-10 更正**：本表第三列原先链接到 `dialects/Mandarin.h` / `dialects/Cantonese.h` ——
> 那两个头与它们声明的门面类**均已删除**，链接是死的。这里改成**现行入口**
> （[Registry.h](../include/cpp-pinyin2/ext/Registry.h) 的注册 id + `Engine::open`）。

ManTone registers all nine styles in `MandarinStyle` (`NORMAL`, `TONE`, `TONE2`, `FIRST_LETTER`, `BOPOMOFO`, `INITIALS`, `FINALS`, `FINALS_TONE3`, `TONE3`). CanTone supports three (NORMAL, FIRST_LETTER, TONE3). Your dialect can support as many as needed, up to `MAX_STYLE` (9).
