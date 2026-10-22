# `tests/data/corpus/` — the gates over `tests/data/verified/`, and why they are shaped this way

`tests/data/verified/` is the verified test set. Every row carries its own judging rule and a status marker,
and the frozen accuracy table in `docs/REQUIREMENTS.md` records what each corpus measures. The dated review
records these scripts once cross-referenced were removed with the other process documents.

**What this directory holds now (2026-10-23):** the five gate scripts, the corpus gate runner, the three
committed inputs they need, and this README. The other 63 files that used to live here — the one-off review
pipeline (`assemble.py`, `sample_scan1..9.py`, `audit_apply*.py`, `build_audit*.py`, `word_*.py`, `twin_*.py`,
their `.jsonl` payloads) — were **removed**. They were run-once analysis against a scratch tree that was never
versioned (`.tmp/corpus/`, 70 MB of `rows_*.jsonl` for one dataset), so without that scratch they could not be
re-run, and every one of them was superseded by the frozen numbers the gates now pin. The waves they carried
out, were recorded in dated review files that have since been removed; the numbers they produced are theions.md`; those
records still cite the scripts by name, which is correct for a dated record and is why they were left alone.

## Layout

```
tests/data/verified/mandarin_tone.txt       cm  TONE3, tone digits      9391 rows
tests/data/verified/mandarin_untone.txt     mu  Pinyin, no tones        1138 rows
tests/data/verified/cantonese_tone.txt      ct  Jyutping, tone digits   6354 rows
tests/data/verified/cantonese_untone.txt    cu  Jyutping, no tones      6354 rows  (= ct minus tone)
tests/data/verified/source/*.txt            the four corpora the tables above were derived from
tests/data/corpus/                          the gates over those tables (this directory)
```

The short codes `cm/mu/ct/cu` are the row-id prefixes (`id=cm:1234`) and the ledger keys; the `datasetId` in
each `#!meta` header is the spelled-out name above. `tests/data/verified/source/` is kept for two reasons:
every `#!meta` names it as `derivedFrom` (provenance), and the `DumpEquivalence` gate reads the dictionary dump
over those unchanged files, so its frozen SHA-256 still means what it meant before the 2026-09-29 rename.

## The frozen numbers, and the fingerprint behind them

`check_corpus.py --with-probe` prints the two blocks the C++ `[corpus]` test asserts, per table: `kFrozen[]`
(decided / pass / ALT_OK / DATA_FIX / ENGINE_BUG / DICT_GAP / EXCLUDED) and `kFrozenFacet[]`, an FNV-1a 64
digest of the per-row verdict vector — `<id>:<STATUS>:<P|F|X>\n` for every data row, in file order, where `P`
means the engine matched every position of the row, `F` that it did not, and `X` that the row takes part in no
statistic (EXCLUDED, or its cell count disagrees with the engine's output).

Why a digest on top of exact counts: **counts are aggregates and cannot see a compensating change.** Take an
`OK` row that the engine passes and an `ENGINE_BUG` row that it fails and swap the two statuses: the multiset
`{status, pass}` is unchanged, so `decided`, `pass`, `engineBug`, `identity` and every other frozen number stay
exactly as they were, and a counts-only test stays green. The engine fix that closes one defect while opening
another is exactly that shape. The digest pins the vector, so the swap fails the test.

Measured 2026-09-29 on a patched copy of `mandarin_untone.txt` (that one row pair swapped, working from a
scratch copy, tracked files untouched): the counts printed identically (`1063/1138`, `ALT_OK=9`, `DATA_FIX=19`,
`ENGINE_BUG=73`, `DICT_GAP=2`), 33,261 of 33,262 assertions still passed, and the single failure was
`CHECK( t.verdictFnv == f->verdictFnv )` with `0x5da48873fec4fec7 != 0x2dc49b692b5f23c3`. The four frozen
values were produced by the Python side and independently reproduced by the live C++ run, so the two
implementations agree byte for byte on all four tables.

## Gates

They run in CTest as `CorpusGate_*` (`ctest --test-dir <build> -R CorpusGate`, label `corpus`), or all at once
by hand:

```sh
python tests/data/corpus/run_corpus_gates.py             # every gate
python tests/data/corpus/run_corpus_gates.py --gate ledger
python tests/data/corpus/run_corpus_gates.py --self-test # proves the validator can fail
```

Note for `ctest`: on Windows/MinGW the test executables need the MinGW runtime DLLs on `PATH`
(`...\CLion\bin\mingw\bin`); without them every C++ test exits `0xc0000135` (`STATUS_DLL_NOT_FOUND`) and looks
like a mass regression. The Python gates are unaffected.

| gate | file | what it protects |
|---|---|---|
| `check_corpus` | `check_corpus.py` | 4 fields per row, legal status, `id=` present, `decided` arithmetic, cell legality and distinctness, one cell per non-space character |
| `ledger` | `ledger_check.py` | every override record is still in effect in the verified, plus the accepted staleness list |
| `consistency` | `consistency_scan.py` | no intra-corpus contradiction beyond `consistency_baseline.txt` |
| `cross_set` | `cross_set.py` | tone/untone twins agree on base syllables beyond `cross_set_baseline.txt` |
| `projection` | `cu_rowcheck.py` | `cantonese_untone.txt` is exactly the tone-stripped projection of `cantonese_tone.txt` (row by row, all 6354 sentences paired) |

They read version-controlled inputs only — `tests/data/verified/`, `data/dialects/`, and the three files
committed here — so they run on any checkout. Two of those inputs form the ledger:
`scan_overrides.jsonl`, the record of every accepted correction (883 records), and `ledger_stale.txt`, which
names the records that are *intentionally* not in effect and why. The ledger gate fails on any staleness not
listed there (verified by hand: a fabricated record whose target no such row can carry makes it exit 1 with
`unexpected=1`), and the projection gate was verified the same way by tampering with one cell of the untone
file and watching it fail.

`check_corpus.py` derives the repository root from its own location (`CORPUS_REPO` overrides it), so a copy
placed at a different depth has to be pointed at the repository. Without `--with-probe` it checks only what is
a property of the verified files themselves, because the engine dumps it can otherwise use (`rows_*.jsonl`)
live in the untracked scratch tree; the engine side — pass counts, `identity`, `kFrozen[]`, the per-row
fingerprint — is checked live by the C++ `[corpus]` test, so nothing is lost in that mode.
