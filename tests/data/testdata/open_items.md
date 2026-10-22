# Open items in the v2 verified sets

Everything here is a known, deliberate exception. It is listed so that no future pass re-litigates it by accident,
and so that a reader of the accuracy numbers knows what is settled and what is not.

## Kept although a naive sweep would change them

| item | verified | why it stays |
| --- | --- | --- |
| 李一氓 | meng2 (23 of 23 rows) | the corpus is internally consistent and a dictionary entry names the person; the modern name Li Yimang is only an analogy |
| 德累斯顿 | lei3 (13 of 13 rows, audit4=fix:6:lei3) | same reason; Dresden's Chinese name is a convention, not a derivation |
| 当时 | dang1,shi2 (109 rows) and dang4,shi2 (6 rows) | phrases_dict lists only dang4,shi2, but dang1,shi2 is equally standard, so the split is two senses and not an error |
| 分子 | fen1,zi3 (20) and fen4,zi3 (13) | phrases_dict lists only fen4,zi; fen1,zi3 (molecule) is standard, so both are kept |
| 地方 | di4,fang5 (33) and di4,fang1 (9) | both senses are standard: place (neutral) and local (full) |
| 东西 · 当年 · 太阳 · 女人 · 左边 · 右边 · 小姐 · 立即-type neutral pairs | split readings | neutral-tone variants of the same word, both permitted |

## Held for lack of evidence

| item | rows | state |
| --- | --- | --- |
| 西藏 as cang2 | 2 | one of the three was 东西+藏 (correct); the other two are unclear, so nothing was changed |
| 夫人 ren2 | 1 | the surrounding sentence is unclear, and 夫人 furen is the dictionary form |
| 丈夫 fu1 | 1 | the classical sense "a man" does read zhang4,fu1, so the row may be right |
| 澄清 | 2 cheng2 vs 1 deng4 | phrases_dict says deng4,qing1 while user_dict says cheng2 qing1; both are standard, and the two dictionaries disagree, so the entry cannot decide |

## Repository defects that are not verified defects

`res/dict/mandarin/user_dict.txt` contradicts the main table in five places: 中都:zhong1 dou1, 上都:shang4 dou1,
国都:guo2 dou1 (the main table is zhong1,du1 / shang4,du1 / guo2,du1), 澄清:cheng2 qing1 against deng4,qing1, and
今朝:jin2 zhao1 against jin1,zhao1. None of these affected the verified, because the corpus uses these strings across
word boundaries; they should be fixed on the engine side, separately from any verified change.

## How the verified is verified

1. `python tools/corpus/check_corpus.py` -- every row decided, no problems (structural mode on the tracked files; add
   `--with-probe` to also recompute the engine side from the untracked `.tmp/corpus/rows_*.jsonl` dumps and print
   `identity=True` for all four files).
2. `python tools/corpus/ledger_check.py` -- every override record is in effect in the current verified, the two accepted
   stale Cantonese records in `tools/corpus/ledger_stale.txt` excepted (`unexpected=0`).
3. `ctest --test-dir cmake-build-release` -- 15 of 15, of which seven are verified tests: `CorpusAccuracy` (the frozen
   per-file counts in `tests/catch2/test_accuracy_v2.cpp` matching the files) plus `CorpusGate_check_corpus`,
   `CorpusGate_ledger`, `CorpusGate_consistency`, `CorpusGate_cross_set`, `CorpusGate_projection` and `CorpusGateSelfTest`.
   `python tools/corpus/run_corpus_gates.py` runs the five gates in one go.

## Characters whose readings are split by sense, held after the duplicate-sentence pass (rounds 20-32)

The duplicate-sentence check found 205 repeated sentences in the tone set, 32 of which carried different readings; 11
cells were genuinely wrong and are fixed (waves 78, 80, 81). The rest are held for these reasons.

| character | readings | why it is held |
| --- | --- | --- |
| 隗 | kui2 and wei3 | both are standard surname readings, so a sentence using it as a name does not decide which |
| 垛 | duo4 (a stack: 草垛, 堆垛, 麦垛) and duo3 (垛口, 城垛, 垛积 duo3ji1) | both standard; a whole-character rule would be wrong |
| 镐 | hao4 (镐京 Hao4jing1, and the personal name 张镐) and gao3 (a pickaxe) | the corpus reads the same name both ways and no entry settles it |
| 蜇 | zhe1 (to sting) and zhe2 (海蜇, jellyfish) | sense decides, and the sentences are short |
| 锔 | ju1 (to mend crockery) and ju2 | rare either way |
| 缊 | yun1 and yun4 | rare either way |
| 子 (as a suffix) | zi5 and zi3 | neutral-tone convention, not an error |

One correction to the method is recorded here as well. Wave 79 was landed and then reverted (commit 1bb05b9): its
rule used two-character windows that were mostly punctuation-adjacent, so it grouped different words together and
came within one cell of "fixing" 垛积 duo3ji1 in the wrong direction. Two lessons: a pattern key must be a word, not
a character plus punctuation, and when two duplicate sentences disagree the row that gets changed must be named
explicitly, because a (row, character, old value) triple that does not match simply matches nothing.

## 面 min2 / min6 is an accepted variant, not an error (round 176)

A cell-level census (`tools/corpus/census_readings.py`) counts 面 79 times in the tone set: `min6` 54, plain `min2` 13,
widened `min2/min6` 12. Many of the plain-`min2` cells sit under an ENGINE_BUG status (that label says the engine
could not produce the reading, not that the annotation is wrong). 面 also carries min2 in other compounds --
畀面 bei2 min2, 賞面 soeng2 min2, 場面 coeng4 min2, 層面 cang4 min2/min6 -- so min2 is not specific to one
paraphrase template.

Six rows (ct:1335, ct:1384, ct:2249, ct:3398, ct:3418, ct:3422) carry `was=NN:min2` notes from earlier waves that
rewrote min2 to min6. Judgement: min6 is the standard reading of 面 and min2 is a colloquial variant the corpus
accepts, so the plain-min2 rows are held rather than corrected, and the earlier min2 -> min6 rewrites are kept
because min6 is also correct. The two spellings therefore coexist on purpose: do not unify them in either
direction without a fresh adjudication, and in particular do not read a lone `min2` as a defect signal.

### 淡 daam6 / taam5 (round 180)

The same census counts 淡 9 times: `daam6` 5, plain `taam5` 2, widened `daam6/taam5` 2. Both readings are
attested -- daam6 is the standard reading of 淡 and taam5 the 又讀 -- the set widens two rows explicitly, and one
earlier wave narrowed 清淡 to taam5 while 淡淡 daam6 was left alone. So 淡 joins 面: both readings are accepted,
neither side is narrowed, and a lone value on either side is not a defect signal.
