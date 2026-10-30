# Third-party notices

This repository is released under **Apache-2.0** (`LICENSE`). That covers the **code**. Two other
things travel with the packages and are **not** under Apache-2.0; this file is the single place that
says what they are and where their terms live. **It now travels inside the packages too** (2026-10-21):
the Python wheel carries it in `<dist-info>/`, and the NuGet package at its root.

| What | Where it ends up | License | Authority (this file only points) |
|---|---|---|---|
| Mandarin dictionary tables | installed `share/cpp-pinyin2/dict/mandarin/`; inside the Python wheel as `cpp_pinyin2/data/dict/mandarin/`; inside the C# package as `contentFiles/any/any/data/dict/mandarin/` | **CC BY-SA 4.0** (https://creativecommons.org/licenses/by-sa/4.0/) | `data/dialects/mandarin/License.txt` |
| Cantonese dictionary tables | same three places, `cantonese/` | **CC BY-SA 3.0** (http://creativecommons.org/licenses/by-sa/3.0/) | `data/dialects/cantonese/License.txt` |
| Catch2 **3.5.2** (amalgamated) | test executables only — **never installed**, not linked into the library | **BSL-1.0** | `vendor/LICENSE.txt` |

Both dialect directories ship their own `License.txt` next to the tables, and every packaging path
copies `*.txt` from that directory (`CMakeLists.txt` installs `data/dialects/` wholesale; `python/setup.py`
lists `data/dict/**/*.txt` in `package_data`; `csharp/CppPinyin/CppPinyin.csproj` packs
`$(CppPinyinDictDir)\**\*.txt`), so the attribution travels with the data instead of relying on this file.

## Dictionary provenance

- The **mandarin** tables derive from [CC-CEDICT](https://cc-cedict.org/wiki/) (MDBG), a continuation of
  CEDICT (© 1997, 1998 Paul Andrew Denisowski).
- The **cantonese** tables derive from CC-Canto and the CC-CEDICT Cantonese readings
  (© 2015-16 Pleco Software Incorporated).
- Both are regenerated from a pinned dependency set by the dictionary repository `pinyin-makedict`; which
  upstream snapshot produced which table is owned by that repository, not repeated here.

**What share-alike means for a redistributor:** the attribution and share-alike obligations apply to the
dictionary tables (the data), not to your own program that links the library. Keep the `License.txt`
files in whatever you redistribute — do not strip them while pruning package size.

## Test data (not installed)

Corpora under `tests/data/` are used by the gates and are never installed or packaged. Their sources are
listed in `README.md` ("Open-source references": `cpp_dataset`, `opencpop`, `M4Singer`) and in the corpus
ledgers under `tests/data/`.

## Algorithm provenance

The tone-conversion rules are derived from [`zh_CN`](https://github.com/ZiQiangWang/zh_CN); the v2
implementation is a rewrite, and the per-source citation lives beside the code
(`src/toneUtil/ManToneUtil.cpp`).

If something you redistribute is missing from this list, treat it as a defect and open an issue.
