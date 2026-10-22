# The contract (2.x)

This is the in-repo home of the rules the public headers used to cite from the working ledger under
提交历史. It used to say that ledger was untracked, and that this file therefore "ships with the
repository and with the package, so a clone of this tree always carries its own authority"; since the
2026-10-09 documentation cleanup the **ledger is tracked too** (the only part of it that is not is the
`doc-claims`/anchor exclusion, which is a reading rule, not a shipping one) -- and it is still
**excluded from `git archive`**, so the package keeps carrying this file as its authority rather than the
ledger. Decision N1 (A), taken in the interface plan's section 4.1 and since retired in the 2026-10-09
documentation cut, is what created this file .

**How to read the status markers.** Each section says whether the rule is *live* in this tree or
*planned*. A rule that is planned is not a promise about the code you are reading -- it is the
target the plan is driving to, and relying on it before it lands is a mistake. `STATUS: live` means
it was checked against the tree; `STATUS: planned` means it is scheduled and must be re-checked
before anyone relies on it.

## 1. Stability policy

- **The C ABI only grows inside 2.x.** Symbols may be added; shapes may gain a trailing member only
  through the documented `struct_size` growth rule; nothing is removed or reordered within 2.x.
- **Output structs never gain a member inside 2.x.** `cppp2_result`, `cppp2_segment`, `cppp2_syllable`,
  `cppp2_style` and `cppp2_error` are filled BY the library into memory the caller walks with its own
  `sizeof`; they carry no `struct_size`, so appending a field would change their stride under every
  already-compiled binding with nothing able to detect it. A new output field therefore waits for 3.0
  (where the generation token changes anyway) or arrives as a new function. The two `*_opts` structs are
  the only ones that grow, and only through the `struct_size` rule -- with the `reserved_*` member
  claiming the declaration's trailing padding first (S9c: an append that landed in that padding was read
  as the caller's uninitialised bytes).
- **Source compatibility for C++** is the goal; **binary compatibility for C++ is not promised.**
  The user's ruling is explicit: API compatibility only, no binary compatibility. Rebuild against
  the new headers.
- **Breaking changes happen only in a major version**, and the package config enforces it:
  `write_basic_package_version_file(... COMPATIBILITY SameMajorVersion)` means
  `find_package(cpp-pinyin2 1.x)` is refused by a 2.x install instead of silently configuring and
  then failing to compile. One `find_package` behaviour, one place that decides it.
- **The public surface is four frozen lists, and they are the only authority:**
  `tests/abi/expected_public_headers.txt`, `tests/abi/expected_c_api_symbols.txt`,
  `tests/abi/expected_plugin_surface.txt` and `tests/abi/expected_public_names.v2.txt` (the C++
  names of this generation). `docs/REQUIREMENTS.md` carries the human-readable count
  and is machine-checked against those files. A change to any of them is made in the same
  commit as the change it describes; there is no "list follows later".
- **Names containing `detail`, `impl`, `internal` are never part of the surface**, no matter which
  header they live in.
- **Escape hatch:** pin a commit or a tag. That is what is supported for a consumer who cannot
  follow the 2.x line.

*STATUS: live.* The compatibility level is set by the
`write_basic_package_version_file(... COMPATIBILITY SameMajorVersion)` call (root `CMakeLists.txt`,
in the install section) and is exercised by the install gate
(`tests/install/check_install_consumer.cmake`), which configures a 1.x probe and requires it to be
refused. The four lists are at the paths above.

- **Naming:** the library lives in the `cpp_pinyin` namespace, and `cpp_pinyin::v2::Engine` is the
  daily spelling of its entry type. Public headers are included as `<cpp-pinyin2/...>`; each header
  is self-contained, so a translation unit can include the one it needs.
## 2. Error model

- Every fallible operation returns `Outcome<T>` (`core/Types.h`); there is exactly one error
  channel, and no layer writes its own reason prose: `codeText()` is the single source of message
  text (`include/cpp-pinyin2/core/Types.h`), and `cppp2_code_text` mirrors it on the C face.
  Two spellings are not `Outcome` **on purpose**: `isPolyphonic` is a predicate (its only unusable
  state is a moved-from engine, which `dialect().id.empty()` answers -- the same test the C ABI
  applies), and `dialect()` is a reference accessor that cannot fail. Since 2026-10-19 the five
  fallible members -- `query` (both overloads), `readings`, `toSimplified`, `loadUserDict`,
  `loadTraditionalDict` -- all carry a `Code`, so "the C++ face is quieter than the C face" is no
  longer true of any of them. `Outcome<void>` is the spelling for "succeeded, nothing to return".
- `Error` carries `{code, detail}`: the code is the machine-readable classification, `detail` is
  context for a human. `Code` values 0..4 are frozen; 5 (`DictIncompatible`) was appended in
  2026-10-18 and the rule stays "append only, never renumber". The 1.x value `Unsupported` is
  deliberately gone: it had no producer and no consumer, so a binding that still names it maps it to
  `Internal` rather than to a code this library never returns.
- **Exceptions.** A fallible call that fails throws nothing by itself -- the failure is the returned
  `Outcome`. If the caller asks for the wrong side of it, that is a programming error, not a library
  failure: `value()` on a failure throws `ErrorException` (which carries the `Error`), and `error()`
  on a success throws `std::bad_variant_access`, exactly as `std::variant` does. `Outcome<void>` has
  no alternative to get wrong, so its `error()` returns a default `Error` (`Code::Ok`) instead of
  throwing. There is deliberately no `Code` for "wrong question": the domain is frozen and a
  caller's bug is not a library outcome.
- **Empty input is a legal empty result, not an error.** A moved-from engine and a failed dictionary
  load are `Code::NotReady`; the C face documents the same split at `include/cpp-pinyin2/ext/c_api.h`.
- *STATUS: live.* The plan's decision N17 (A) gave this channel to `Engine::readings`
  (`Outcome<std::vector<std::string>>`) and fixed the code for each of the three "returns empty" paths:
  a moved-from facade and an engine that never became ready answer `Code::NotReady`, while empty (or
  undecodable) input stays a legal empty result, as the bullet above requires. The C face already
  returned a status code, so the two faces now agree. Landed in batch B4.

## 3. Structured readings (the `Syllable` rules)

**The four rules are stated once, in `include/cpp-pinyin2/core/Syllable.h`** (the block introducing the
structure). That wording is what `tests/cpp/test_syllable_contract.cpp` pins, so it is the authority: any
second copy necessarily drifts from it. This section used to restate the four rules in English while the
header called itself "verbatim from docs/CONTRACT.md section 3 (authoritative wording)" -- each pointing at
the other, with slightly different text (the English version had dropped the `markNeutralAs5` 0/5 nuance and
the entering-tone `-p/-t/-k` detail). 2026-10-20 (audit round 2): the copy here is gone, and the rules are
summarised in one line instead --

1. `tone` comes from the word list, never parsed out of a rendered string.
2. `tone_kind` describes the table entry and changes no output byte.
3. The two neutral-tone encodings are the same thing at this layer.
4. Dialect numbers are used as the data gives them (no private 7/8/9 renumbering).

Read them as an index, not as the wording: **the header is normative.**

`spelling` (decision N5) is the word list's original spelling, filled whenever readings or
candidates were asked for. `candidates` stays a vector of strings, and its de-duplication key is
fixed to the rendered string -- changing that key would change `candidate_count` and the frozen
summary.

*STATUS: live for rules 1-4 and for `spelling`; the "verbatim" source of the four rules is the
header block itself.*

## 4. Threading

- Concurrent reads (`query`, `readings`, `to_simplified`, `is_polyphonic`) on one constructed instance
  are safe -- they take no lock, because nothing they read is mutated by them.
- Loading **is** a mutable entry point: `Engine::loadUserDict` / `loadTraditionalDict` (and their C,
  Python and C# counterparts) replace (the default) or merge the user layer / traditional table on a
  live instance. A load **must not overlap a read on another thread** -- that overlap is a data race,
  and it has been observed to corrupt the heap. Loading at construction time stays the recommended shape.
- **Callbacks run on the calling thread and are never serialised.** The disambiguation callback and
  `Dialect::postProcess` are invoked in place, so when an instance is shared across threads the
  callback must be thread-safe itself and must not capture mutable state. **The headers now say so
  where a consumer reads it** -- `Engine::setDisambiguationCallback` and `Dialect::postProcess` -- so
  this bullet is a summary here and the normative wording lives with the declarations.

*STATUS: live for all three bullets. The third one used to be a requirement stated only in this file,
with the headers deferring to a batch B7 that never landed; 2026-10-20 (audit round 2) put the wording
into both headers, so this document is now a pointer for it rather than its only home.*

## 5. Style identity

- A style's **identity is its name, scoped by dialect**; the small integer is an **alias**, and the
  index of a style in a list is not a number at all (sparse aliases, such as Cantonese's 0/3/8, are
  therefore expressible).
- **Name resolution happens once, at an entry point**, never in the hot path. The C face accepts
  numbers only; the C++ façade accepts its typed enumerations.
- The C face mirrors the style list as data (`cppp2_style_list`) so a binding can
  present the same names without a private table. (The C symbol count has changed since this sentence
  was first written; `tests/abi/expected_c_api_symbols.txt` is today's authority, not a number here.)
- **`Dialect::styles` is the single authority, and it is per dialect** -- there is no global alias table.
  Mandarin and Cantonese agree on 0/3/8 because each declares it (`include/cpp-pinyin2/core/Types.h:51-58` for Cantonese);
  the dialect headers are *aliases* of that one authority, not second definitions
  (`include/cpp-pinyin2/dialects/ManTone.h:11` says so in as many words). There is **no normalization of style names**
  anywhere: `tolower`/`toupper` have 0 hits under `src/` and `include/`. That is a statement about
  *names*; a dialect normalizing its **external readings** is a different thing and does exist
  (`ToneConverter::normalizeReading`, and `include/cpp-pinyin2/ext/Dialect.h:41` names both roles in one sentence).
- **A default-constructed `ToneConverter` is legal**, and an empty style slot is a documented
  pass-through, not an inconsistency: `include/cpp-pinyin2/ext/Dialect.h:41-43` -- "every style slot is empty, so every
  style passes the reading through unchanged". A dialect that declares three styles and fills three
  slots is therefore well formed even though the shared number domain is larger.

*STATUS: live. Verified in this tree: `cppp2_style_list`/`cppp2_style_list_free` exist, and the three
frozen surfaces measure **15 public headers + 23 C ABI symbols + 18 plugin-surface declarations**. Those
numbers are machine-checked every round by `tools/check_doc_claims.py`; their one human-readable registry
is `docs/REQUIREMENTS.md` §5.3 row ①, and its machine truth is the three list files under `tests/abi/`
(`expected_public_headers.txt`, `expected_c_api_symbols.txt`, `expected_plugin_surface.txt`). (An earlier
wording of this sentence named the list files as "the one authority", which contradicted REQUIREMENTS §5.3
-- one reader-facing registry PLUS one machine truth source is the arrangement; the correction itself is
not part of the contract and lives in the review record, not here.) `StyleDecl` lives in
`include/cpp-pinyin2/core/Types.h`.*

## 6. The dialect contract

- A dialect is opened over a **dictionary directory**; `cppp2_open` takes that path and a dialect id,
  and nothing else.
- The tables are `word.txt`, `phrases_dict.txt`, `phrases_map.txt`, `trans_word.txt`, plus an
  optional `user_dict.txt`, alongside a `dialect.json` that declares the dialect.
- Row syntax is `key:value`, with `:` and `,` as the separators; the format is the same for the
  C, C++ and both binding faces because there is one loader.
- A dialect must supply its reading normaliser (`normalizeReading`); it is never optional.
- An **empty table is a load failure**, not an empty dialect.

*STATUS: live (the loader is one implementation; the C face reaches it through `cppp2_open`).*

## 7. What each face can actually do

- **C++**: the full surface -- dialects, styles by name or alias, readings with `spelling`,
  structured results, derivation, callbacks.
- **C ABI**: dialects, styles by number, queries, readings, dictionary loading, the style list.
  Structured C++ types (`Syllable`, `Segment`) are not visible; the C face mirrors what it needs.
- **Python**: one-to-one with the C ABI, plus the conveniences the package documents.
- **No input-length cap, and the cost is the caller's business.** A query allocates roughly **200 bytes of
  result per input byte of Chinese** (one `Segment` per character + one extra copy on the C face); a 1 MiB
  input peaks around 100 MB. The measurements behind that factor are in `docs/PERFORMANCE.md`, the decision
  not to impose a cap is in `include/cpp-pinyin2/ext/c_api.h`.
- **C#**: one-to-one with the C ABI. The suite DECLARES net7.0 and net10.0 and `test_csharp.yml` is set up
  to run both, but that workflow has never executed on GitHub -- the local runs are the only evidence so far.
- **A third-party dialect cannot be registered from C, Python or C#.** Registration is a C++
  facility (`include/cpp-pinyin2/ext/Registry.h`); the bindings can only use dialects that the library already knows.
  This is a hard fact of the current design, not an oversight, and it is stated here so nobody has
  to discover it by trying.

## 8. Per-entry defaults

Every layer uses the same sentinel for "no style was asked for": `-1`, resolved in exactly one place,
`resolveStyle` (`include/cpp-pinyin2/core/Types.h`), which returns the dialect's own default (the `styles[].isDefault` entry, read by `defaultStyleOf`) and falls back to 0
when a dialect declares no styles at all. The historical note is in `include/cpp-pinyin2/core/Types.h` above
`EngineConfig`: before C4/N27 there were three config types whose default styles disagreed (Mandarin
TONE, Cantonese TONE3, `EngineConfig` NORMAL), so the same sentence rendered to different bytes
depending on which layer asked.

| Entry | `style` when unspecified | `include_candidates` |
|---|---|---|
| C++ `EngineConfig` | `Style{}` = `-1` (`include/cpp-pinyin2/core/Types.h`) | `true` (`include/cpp-pinyin2/core/Types.h`) |
| C ABI `cppp2_query_opts_init` | `-1` (`src/c_api_lifecycle.cpp`) | `1` = true (`src/c_api_lifecycle.cpp`) |
| Python shell, all four entries | `Style.DEFAULT` = `-1` (`python/cpp_pinyin2/__init__.py`) | `False` (`_make_opts`; `query` spells the same default) |
| C# shell, all four entries | `Style.Default` = `-1` (`csharp/CppPinyin/Style.cs`) | `false` (`csharp/CppPinyin/Engine.cs`) |

Two statements here are load-bearing, so they are pinned by tests rather than trusted:

- **The sentinel reaches the dialect default.** Python pins `to_text(engine.query(...))` (`python/tests/test_shell.py`)
  and `readings`; C# pins `Readings` (`csharp/CppPinyin.Tests/DefaultStyleTests.cs`). Both assert
  equality with the dialect's own style AND inequality with `NORMAL`, because a shell that quietly
  passed 0 would still render something plausible.
- **The sentinel is not a style number.** `int(Style.DEFAULT) == -1` is asserted directly, so the
  sentinel cannot be folded into the numbering by accident.

**The one deliberate divergence** is `include_candidates`: `true` on the library side, `false` in both
shells. That is not an oversight to be tidied away -- the frozen conformance fixtures count candidates,
so changing either side moves bytes under the fingerprint gate; unifying them is its own item with its
own re-pin, and until then this table is where the difference is recorded.
### 8.1 Traps: a style the dialect does not declare

The library **passes an unknown style through** and says so: `include/cpp-pinyin2/core/Types.h`, `include/cpp-pinyin2/ext/c_api.h` and
`src/ToneConverter.cpp` document that a style a dialect cannot apply comes back unchanged. That is the
right contract for a C caller holding a number it owns, and the wrong default for a shell, so N20=A
puts the refusal in the shells:

| Layer | Behaviour for a style the dialect does not declare |
|---|---|
| Library (C++, C ABI) | pass-through: the value is applied where it can be, and otherwise comes back unchanged |
| Python shell | `InvalidArgumentError`, naming the value, the dialect and the styles it declares |
| C# shell | `CppPinyinInvalidArgumentException`, same wording |

**The C face cannot fail on a style number at all**, and that is structural rather than a promise:
`include/cpp-pinyin2/ext/c_api.h` has no name parameter anywhere on its surface, so a name conflict is not expressible there.
The only implementation that could produce one is one that looked names up on behalf of C numbers --
which is exactly what the refusal rule above forbids the library to do, and what the shells do instead,
against the dialect's own table.

Both shells read the dialect's **own** style table (`cppp2_style_list`) instead of keeping a copy, and
`-1` remains legal everywhere -- it is the sentinel "the dialect's own default". Measured tables
(`cppp2_style_list`, this tree):

    mandarin  : 0 normal, 1 tone (default), 2 tone2, 3 first-letter, 4 bopomofo, 5 initials,
                6 finals, 7 finals-tone3, 8 tone3
    cantonese : 0 normal, 3 first-letter, 8 tone3 (default)

Read that table before assuming a number is a defect: an early draft of this batch called
`cantonese + 3` one, and the table refuted it -- Cantonese declares first-letter itself, so the call
is legitimate and is pinned as a case that must keep working (both shells). Numbering that belongs to
another library is not this library's numbering either: pypinyin's `INITIALS = 3` is this tree's
`first-letter = 3`, and pypinyin's `FINALS_TONE3 = 9` is out of range here (the largest alias is 8).