# Changelog

All notable changes to this project are documented here. The format follows the spirit of
[Keep a Changelog](https://keepachangelog.com/), and the project uses [Semantic Versioning](https://semver.org/).

**Where the numbers live.** This file says *what* changed for a user of the library. The measured values, the
commands that produced them and the honest boundaries ("unverified until CI") live in one place each:
`docs/DECISIONS.md` §2.1 (the decision ledger) and
`docs/REQUIREMENTS.md` (frozen acceptance). Numbers are quoted there and are deliberately **not** copied here,
so they cannot drift apart.

## [Unreleased] — 2.0.0（未发布）

> **2.0.0 尚未打 tag** ⇒ 本仓是「待发布」而非「已发布」。
> 下面这一节就是 2.0.0 的完整说明，**发布当天只做一件事：把本节标题改成 `## [2.0.0] - YYYY-MM-DD`**，
> 不要在这一天之前写上一个日期（2026-10-16 曾提前写死过一天，属记录错误，2026-10-18 改回本形态）。
> 发布清单（要发一版时该做什么的唯一落点）在 docs/RELEASE_CHECKLIST.md（原 PLUGIN 文档第 6.7.11 节）；本文件的版本标题是其中的**第 ⑦ 处版本投影**，**由 `tools/check_version_tokens.py` 的第 10 轴自动核对**（2026-10-20 起）。

Version 2.0.0 is a rewrite rather than a release with additions. It turns the library into something you
instantiate, gives it a C ABI, and builds Python and C# shells on that ABI. The public C++ and C surfaces are
frozen and gated (see `docs/DECISIONS.md` §2.1); the changes marked **Breaking** below are the
ones a 1.x user will notice.

### Breaking

- **`Engine::toSimplified`, `loadUserDict` and `loadTraditionalDict` answer an `Outcome` instead of a
  bare value.** `toSimplified` returns `Outcome<std::string>` and the two loaders return
  `Outcome<void>`: a moved-from or not-ready engine used to answer an empty string / `false`, which
  a caller could not tell from "the input was empty" / "the table was fine but empty". The C face
  could always say `CPPP2_ERR_NOT_READY`, and the loaders' `detail` (the library's own reason) was
  unreachable from C++ -- both gaps close here. `isPolyphonic` deliberately KEEPS returning `bool`:
  it is a predicate, and rewriting it as `Outcome<bool>` would have let `if (x)` / `x ? a : b` /
  `Catch2 CHECK` compile with a new meaning, silently (`Outcome` has an explicit `operator bool`).
  `Outcome<void>` is an explicit specialization, so no new public name was added.
- **Every name on the C ABI carries the generation token.** Every exported function moved to `cppp2_*`
  (`cppp_open` became `cppp2_open`), and so did the types and enumerators they take and return
  (`cppp2_engine`, `cppp2_query_opts`, `CPPP2_OK`, `CPPP2_ERR_*`, `CPPP2_SOURCE_*`, `CPPP2_TONE_KIND_*`).
  Both halves had to move together: with the types left alone, the two generations' `ext/c_api.h` could not
  be included in one translation unit (measured: seven struct redefinitions and fifteen enumerator
  conflicts), which is exactly the coexistence this release promises. A C consumer recompiles; the symbol
  set keeps its single authority, `tests/abi/expected_c_api_symbols.txt`.
- **`DisambiguationCallback` is a type of its own, not a `std::function` alias.** Per generation it is a
  distinct class, so handing an engine the *other* generation's callback is now a compile error instead of
  code that compiles and then behaves differently. Constructing it from a lambda, a function pointer or a
  `std::function` still works, and a default-constructed callback still means "none installed" -- the call
  sites in this repository did not change.
- **`cppp2_apply_tone_sandhi` takes the engine handle.** Tone sandhi is a property of a dialect, so the C entry
  now asks `Dialect::toneSandhi` instead of applying the Mandarin rules to whatever result it was handed; a
  dialect that declares none (Cantonese) answers `CPPP2_ERR_INVALID_ARG` and leaves the rows untouched. The
  change lands BEFORE the first 2.x release, which is exactly why it costs nothing: nothing was ever shipped
  against the three-argument form. Both shells were updated in the same commit, and a second dialect's pass
  will arrive as its own symbol (`cppp2_apply_<dialect>_sandhi`) rather than as another signature change.
- **Managed identities carry the token**: the Python **import** package is `cpp_pinyin2` (the distribution
  was already `cpp-pinyin2`, so the two had disagreed), and the C# assembly and root namespace are
  `CppPinyin2`. A Python consumer changes its import line; a C# consumer changes its `using`.
- **`Dialect::defaultStyle` is gone; the default is the `styles[].isDefault` entry.** The field said the
  same thing as the table flag from a second place -- `resolveStyle` read the field while `cppp2_style_list`
  read the flag -- so a plugin could publish one default and render another, with nothing comparing the two.
  `styles` is now the only authority and `cpp_pinyin::v2::defaultStyleOf` reads it (a new public name).
  Migration: delete `d.defaultStyle = 8;` and mark that entry `true` in `d.styles`, which the built-ins were
  already doing. The frozen plugin surface loses one declaration (`tests/abi/expected_plugin_surface.txt` is the authority for how many it holds today). See
  `docs/UPGRADING_A_GENERATION.md` (2026-10-18) for the full note.

- The engine is **instance-level**: no global dictionary path, no process-wide state. You construct an engine
  with the dictionaries you want, and the API no longer offers a global setter. Rationale: `docs/DECISIONS.md` §2.1, decision D12.
- The **fake streaming query** is gone. It never streamed; `readings()`, `join()` and `toVector()` cover the
  use, and a query that pretends to be incremental was a contract nobody could rely on.
- **Host layout changed**: dictionary data is separated from product data (the "data faces"), and the public
  headers are grouped into `core/`, `dialects/` and `ext/` in three layers. Include paths under
  `include/cpp-pinyin2/` therefore moved. See `docs/STABILITY.md` R1-R9.
- One test switch and one style authority: `-DBUILD_TESTING=OFF` used to look like it did something, and
  several warning sets disagreed. Both now have a single source.
- The version is **2.0.0** with exactly one source of truth (the root `CMakeLists.txt`), which
  `find_package(cpp-pinyin2 2.0.0 REQUIRED)` and every binding now agree on. Earlier the three copies
  disagreed.
- **The two shells now take the dialect's own default style**, and it is a value of its own
  (`Style.DEFAULT` / `-1`), resolved in one place. Measured through the Python shell:
  `query_text("中国")` answers `zhōng guó` and `readings("中")` answers the `TONE` pair,
  while `style=NORMAL` still answers `zhong guo`; Cantonese answers `nei5 hou2` for `你好`.
  A 1.x caller of the C++ facade sees no change here -- the facades already answered this way
  and the shells disagreed with them; the shells caught up, which changes their visible bytes.
- **`isPolyphonic` means "starts a phrase", not "has several readings of its own".** A
  single-reading character that opens a phrase answers `true`. The name invited the other
  reading, so the meaning is now stated where the function is declared and pinned by the
  conformance fixture rather than left to the reader.
- **`uAsV` is spelled `vToU` on every surface** -- C++ member, C field (`cppp2_query_opts.v_to_u`),
  conformance key and the Python shell's public keyword (`v_to_u`). The Python keyword is user
  code, so this is a rename that breaks callers rather than a cosmetic one.
- **The C ABI is one frozen set of symbols, and it is not what an early 2.0 reader saw** (the list in `tests/abi/expected_c_api_symbols.txt` is the count; this note deliberately does not repeat it). The
  `cppp_is_ready` / `cppp_last_error` pair is gone -- a failed `cppp2_open` reports through its
  error out-parameter instead, which is the only failure a handle can carry -- while
  `cppp2_apply_tone_sandhi` and `cppp2_style_list` / `cppp2_style_list_free` arrived.
- **`utf8strToU16str` is a total function: malformed UTF-8 becomes U+FFFD instead of throwing.** 1.x threw
  `std::invalid_argument` ("Invalid UTF-8 sequence") on input its decoder rejected; 2.0 maps each malformed
  code point to exactly one U+FFFD and never throws. The reason it is a total function rather than a
  cleanup: the engine emits one result row per input code point and the accuracy corpora align positionally,
  so every caller had to catch and re-map the exception anyway. `src/U16Str.cpp` states it and
  `tests/cpp/test_boundary_utf8.cpp` pins it. A 1.x caller that used the throw as its input validation must
  check for U+FFFD instead. Until 2026-10-27 `README.md` and both API pages still documented the 1.x
  behaviour.

### Added

- **Tone-style conversion on every surface** (`Engine::toneConvert`, `cppp2_tone_convert`,
  `Engine.tone_convert`, `Engine.ToneConvert`): a reading in the dialect's stored notation comes back in any
  style that dialect declares, and a style it does NOT declare returns the reading untouched rather than an
  error. The input shape is checked before the converter sees it, because `ToneConverter::convert` assumes the
  stored form and silently mangles anything else -- measured: `"ni"` came back `"nii"`, `"nǐ3"` came back
  `"n3i"`. This is the C ABI's 23rd symbol; the frozen list moved with it in the same commit.
- **Dialect plugin surface** (C++): the public headers under `include/cpp-pinyin2/` let a new language be
  added as data plus a plugin directory, without touching the core. Mandarin and Cantonese are the two that
  ship; the interface is the one that was measured to be stable enough to freeze.
- **vcpkg port** (`packaging/vcpkg/ports/cpp-pinyin2`): ready to be proposed to the registry, under its own
  package name so the existing `cpp-pinyin` 1.0.2 port keeps working for its consumers; both generations can
  be installed side by side. The install path was verified offline against a local vcpkg tree; what remained
  was the release tag and the registry PR. **The port was then removed from this repository on 2026-10-23**
  : it never reached a registry, and `packaging/vcpkg/` no longer exists.
- **C ABI** (`cppp2_*`): every public operation is reachable from C, and the exported symbol list is gated, so
  an accidental addition or removal fails the build instead of surprising a consumer.
- **Python binding**: a `ctypes` shell over the C ABI, packaged as a `py3-none-<platform>` wheel — the shared
  library travels inside it, so **Python users need no compiler**. Prebuilt wheels are produced for the
  platforms listed in `.github/workflows/test_python.yml`, including Linux aarch64 built and stamped in a
  manylinux container.
- **C# binding**: P/Invoke over the same ABI, packaged as one `nupkg` that carries the native runtime assets
  for each RID.
- **Cross-language conformance fixture**: one generated case file that the C++ core and each shell replay, so
  "the shells agree with the core" is a test rather than a claim.
- **Install-consumer gate**: a test installs the library into a throwaway prefix, builds a consumer with
  `find_package`, and fails the suite if packaging regresses (including the `lib` vs `lib64` question, which
  it now takes as a required input instead of assuming).
- **Optional sanitizer configuration** (`-DCPP_PINYIN2_SANITIZE=ON`) and a CI job that runs the suite with
  AddressSanitizer and UndefinedBehaviorSanitizer. The configuration refuses to configure, loudly, on a
  toolchain that cannot link them.
- **Data gates**: a dialect-set gate that keeps "only Mandarin and Cantonese" true for the shipped tree, and
  strict per-position accuracy gates over the verified corpora, with the failing lines shown rather than
  summarised.
- **Examples** for both languages, and workspace-scoped temporary files for the test suite, so runs do not
  scatter artifacts into the system temp directory.
- **License disclosure** ([`THIRD_PARTY_NOTICES.md`](./THIRD_PARTY_NOTICES.md)): the code is Apache-2.0,
  but the dictionary tables every package installs are CC BY-SA (4.0 Mandarin, 3.0 Cantonese) and the
  vendored Catch2 is BSL-1.0. The README's badge and License section said "Apache 2.0" for the whole
  package until this release; a redistributor (vcpkg, a distribution, a product embedding the data) now has
  one page that says what is under which license and which `License.txt` files must be kept.
- **The spelling is part of a syllable**: `cppp2_syllable` carries `spelling` -- the table's own
  reading bytes, the row's identity -- alongside the `initial`/`fin` projection the renderer
  cannot always rebuild from it. The rendering itself still appears exactly once, at the segment
  level, so the two strings cannot drift apart.
- **One sentinel for "whatever this dialect defaults to"**: `style = -1` (`Style.DEFAULT`) is a
  value of its own rather than a magic zero, so a caller can ask for the dialect's default
  explicitly.

### Fixed

- **The Cantonese dictionary is byte-identical to 1.x again, including the `吒:咤` fan-to-simplified
  mapping.** A v2-only edit had dropped that one line from `data/dialects/cantonese/trans_word.txt`; it was
  measured to change what the engine returns for `吒` (its own two readings instead of the four `咤` readings
  1.x reported) -- a behaviour change neither repository's gates could see, because the Cantonese corpora
  never contain those two characters. The line is back, and the generator in `pinyin-makedict` was fixed to
  emit both halves of the pair itself, so the shipping data needs no repo-specific edit at all. Net effect
  for a consumer: none, which is the point.
- **`cmake --install` no longer assumes the install libdir is `lib`.** On distributions where CMake picks
  `lib64` (Fedora/RHEL family) the package config, the `pkg-config` file and the consumer gate all pointed at
  the wrong directory. All three now follow `CMAKE_INSTALL_LIBDIR`.
- **Mandarin phrase readings corrected** where the verified corpus disagreed with the table, and the corpus
  gates now score every position strictly instead of letting a line pass on a coincidental match.
- **Header self-containment** is enforced: every public header is compiled stand-alone, and the guard fails
  the build if it stops compiling anything at all.

### Notes for 1.x users

- The migration is a rewrite of the public surface, not a rename; expect to recompile and to adjust includes.
  The frozen surfaces are enumerated in `docs/DECISIONS.md` §2.1, and `docs/REQUIREMENTS.md`
  records what "done" meant for each requirement.
- Windows, Linux and macOS are built in CI; the macOS leg and any CI-only fact are **not** verified on the
  development machine and are marked as such in the documents above.
