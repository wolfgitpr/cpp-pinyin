# 测试集工作状态卡（随时可续）

> 这张卡只记「现在在哪、下一步做什么」，不重复判据链。判据与逐条裁定见 `decisions.md`（§10.*）、
> 本轮/各波改动见 `audit_round24.md`，队列账目见 `coverage.md`。

## 一句话现状

`tests/verified/` 四张金标表的**内容**已通过 `check_corpus.py`（`problems: 0`、四表 `identity=True`），
`ctest --test-dir cmake-build-release -j 4` **15/15**（含 `CorpusAccuracy`、`BenchmarkSmoke`）；剩下来的是
**覆盖**问题——还有约 5,300 行普通话、约 7,000 行粤拼没有被人逐句读过，以及字典侧缺口（不是金标错）。

## 冻结值（`test_accuracy_v2.cpp` 的 `kFrozen[]`，2026-09-29 重定）

| 表 | 行 | pass | ALT_OK | DATA_FIX | ENGINE_BUG | DICT_GAP | 逐行指纹 |
|---|---|---|---|---|---|---|---|
| `mandarin_tone` | 9,391 | **3,063**（32.62% 严格 / 38.49% 算法）| 636 | 1,006 | 4,894 | 1,434 | `0xe1dd7b4d187d354a` |
| `mandarin_untone` | 1,138 | 1,063（93.41% / 93.57%）| 9 | 19 | 73 | 2 | `0x2dc49b692b5f23c3` |
| `cantonese_tone` | 6,354 | 4,944（77.83% / 80.84%）| 524 | 1,613 | 1,172 | 236 | `0x37463e25947f127e` |
| `cantonese_untone` | 6,354 | 5,767（90.79% / 92.70%）| 221 | 854 | 454 | 131 | `0x1d40a7f972de77b9` |

两张粤拼表各有 2 行 `EXCLUDED`；`decided = pass + ENGINE_BUG + DICT_GAP`（`identity=True`）。

**逐行指纹**是 2026-09-29 新加的判据：`<id>:<STATUS>:<P|F|X>\n`（每个数据行、文件顺序、P=该行每一位都
命中、F=有位置未命中、X=不参与任何统计的行）过 FNV-1a 64。冻结值来自
`check_corpus.py --with-probe` 打印的 `kFrozenFacet[]`，C++ 侧独立算同一串字节。加它的原因很具体：
**计数是聚合量，看不见对冲**——把一行 `OK`（通过）与一行 `ENGINE_BUG`（失败）的状态对调，`decided`、`pass`、
`ENGINE_BUG`、`identity` 全部不变，只数数的测试照样绿。实测（改的是 `.tmp` 里的副本，金标文件未动）：
对调后计数逐字相同、33,262 条断言里 **33,261 条仍然通过**，唯一失败的就是
`CHECK( t.verdictFnv == f->verdictFnv )`。详见 `tools/corpus/README.md`「The frozen numbers, and the
fingerprint behind them」。

`mandarin_tone` 是**分层抽样**的结果，不要当成「普通话准确率」直接引用。pass 会在**修对金标**时下降
（改对的格子引擎读不出 → 从假通过挪进 ENGINE_BUG/DICT_GAP），所以 pass 降低常是质量提高。

## 队列（`coverage.py` 口径，队列是上界）

cm 5,308 · mu 796 · ct 3,336 · cu 3,711 · 粤语句子已读 2,717 / 6,354。

## 在飞读者（8 个名额满）

audit20 第 07–10 批（随机抽检）、audit21「长」四组（`chang_1..4.txt`，277 处，问题唯一：
「生长/年长/长官」zhǎng 还是「长短/长久」cháng）。结论落在 `.tmp/corpus/verdicts/`。

## 下一步（按优先级）

1. 开「传」chuán/zhuàn、「重」zhòng/chóng 两类清点（脚本照 `tools/corpus/_chang_sweep.py` 改；
   已有的经验：词典「最近词条」提示是机械匹配，会造跨词假朋友——只能当线索，不能当判据）。
2. 生成 audit22 抽样载荷（`tools/corpus/sample_scan9.py` 改 SEED 与 OUT；记得把 audit21 加进 `SWEEPS`）。
3. 粤拼：ct 队列 3,336、cu 3,711，句子覆盖 2,717/6,354 —— 是最大的一块未覆盖面。
4. 继续收 audit20 剩余批次（第 11–16 批未派）。

## 未结事项

- **「萧铣」的其余出现处**：第 43 波只改了 `cm:43211`（xian3→xi3），而 audit20 第 11 批的读者把另一处
  「萧铣 xiǎn」当作正确读法核过 —— 说明语料里可能还有同类行没改。下一步先把全语料「铣」的位置列出来
  （Python 直写文件，别过 PowerShell），凡是「萧铣」一律 xi3，其余（铣床/铣工 xǐ、铣铁 xiǎn）按语境定。
- **TOTAL 口径不统一**：`_reader_prompt.md` 是 3 个数版本，audit21/22 载荷是 2 个数版本，读者已两次提出。
  〔**2026-10-10 补**：`audit21/`、`audit22/` 的读者载荷属于**已归档**的批次目录（检索点 =
  归档前提交 `dff33b2`）；本行的结论（TOTAL 口径要统一）与归档无关，路径映射见 `docs/TESTDATA_ARCHIVE.md`。〕
  统一成 `TOTAL <处数> <判错处数>`，并在载荷头写明「下游按两个数解析」。

- **T-09 许可**：语料来源与再分发许可问题仍未结（见 `REQUIREMENTS.md`）。
- 字典缺陷（引擎侧，非金标错）：`閤 gé`、`缪 miao4`、`恶 ě`、`长乐府`、`嚟:lai4`、`吓:haa5`、
  `騾:leoi4,lo4`、`被:pei5,bei6,pei2`、`攒:zǎn,cuán` 等 —— 改字典是**另一件工作**，别混进金标波次。
- 文档偏见必须随数字一起走：第 1–7 波无逐行标记（「无记录」≠「绝没读过」）；金标错误天然偏向
  「错算对」；DICT_GAP 类已封闭核查过 2,326 处、无金标错。

## 复现（每波固定五步）

```powershell
Copy-Item tools\corpus\*.py .tmp\corpus\ -Force
$py = "C:\Users\server\AppData\Local\Programs\Python\Python310\python.exe"
& $py .tmp\corpus\assemble.py; & $py .tmp\corpus\check_corpus.py
$env:PATH = "C:\Users\server\AppData\Local\Programs\CLion\bin\mingw\bin;" + $env:PATH
cmake --build cmake-build-release; ctest --test-dir cmake-build-release -j 4
```

冻结值不再由 `_freeze13.py` 生成（那个脚本读的是改名前的 `.tmp/gold/`，已失效）：**现在用
`python tools/corpus/check_corpus.py --with-probe`**，它打印可整块粘进 `kFrozen[]` 的计数行，以及
`kFrozenFacet[]` 的逐行指纹。没有 `--with-probe` 时 `pass` 恒为 0（那一栏要引擎侧才判），别把结构模式的
读数当成准确率。

改动写进 `.tmp/corpus/scan_overrides.jsonl`（**不提交**），金标表由 `assemble.py` 重放覆盖记录生成；
**永不 `git push`**，分支 `v2`。

## Class-sweep ledger (rounds ~60-140)

Method: dump every position of one character from tests/verified/mandarin_tone.txt, then judge each
position against its WORD (dictionary phrase entry first, then the corpus convention, then judgement).
An automated keyword check may only raise candidates -- four times it was too narrow and the verified was
right (番州 Panzhou, 屠岸贾 Tu'an Gu, 会稽 Kuaiji, surname 曾 Zeng).

Zero-error classes (3,065 positions, 0 confirmed errors): 传 234, 重 347, 弄 65, 坊 54, 膏 17,
靡 130, 贲 161 (the lone bi4 is 贲卦 Bi-gua, correct), 哩 93, 埔 19 (the bu4 rows are 大埔 Dabu),
阿 262 (both e1 are 东阿 Dong'e), 占 128, 堡 39 (the lone bu3 is 吴堡 Wubu), 咄 8, 剌 27, 喇 26
(the la1 rows are 喀喇沁/哈喇泊), 羯 3, 伽 16, 佗 3, 台 182 (all six tai1 are 台州/天台), 番 49,
氏 126, 龟 10 (all four qiu1 are 龟兹 Qiuci), 翟 34 (di2 is the 翟/狄 people), 贾 36 (gu3 is
屠岸贾/商大贾), 会 587 (all four kuai4 are 会稽 Kuaiji), 曾 243 (zeng1 is the surname), 强 133,
辟 33.

Error-bearing classes (86 errors in ~633 positions): 将 24/227, 长 3/277, 铣 53/109 (萧铣 and three
other personal names), 阆 2, 圩 6/20 (market-origin names read xu1; wave 52), plus three single-name
fixes from readers (塞万提斯 sai1, 郑思肖 xiao4, 聂铣 xi3).

Re-checked and deliberately kept: 李一氓 meng2 (23/23 rows agree; the reader's leading reading
Li Yimang is an analogy, which loses to the word). 倔强 does not occur in the corpus at all -- a
"missing reading" is not an error signal until the typical word is found in the corpus.

## Class-sweep ledger, update (rounds 140-166)

Zero-error classes now number 39 and cover 5,052 positions, with 94 errors found in the
error-bearing classes (将 24/227, 长 3/277, 铣 53/109, 阆 2, 圩 6/20, 舍 1/56, 乘 1/58, plus four
single-name fixes). Classes added since the first ledger: 翟 34 (di2 = the 翟/狄 people), 贾 36
(gu3 = 屠岸贾 and 商大贾), 会 587 (kuai4 = 会稽), 曾 243 (zeng1 = the surname), 强 133, 辟 33, 恶 42
(e3 = 恶心), 宿 81, 氏 126, 龟 10, 台 182, 番 49, 单 227 (shan4 = 单县/单城 names), 塞 252
(se4 includes 塞擦音 and 喉塞音), 处 227, 监 61 (jian4 = the office names 秘书监/殿中监/钦天监),
系 240 (ji4 = 系鞋带), 与 648 (yu4 = 参与/与会/与闻, all 38), 济 95 (ji3 = 济南, 胶济铁路,
济源, 人才济济).

Waves 53-55 (the last three fixes): 宿 xiu3 in 一宿 (it means a night, not a constellation),
舍 she4 in 菲舍尔 (the same name is she4 at cm:3251), 乘 cheng2 in 大乘 (the same word is cheng2
twice elsewhere; sheng4 is the chariot measure and 史乘).

Two operational lessons, both about instruments rather than data:
1. A keyword check may only raise candidates. It was too narrow nine times and the verified was right
   every time (番州, 屠岸贾, 会稽, surname 曾, 塞擦音, 喀喇沁, 监 office names, 人才济济, 胶济铁路).
   When the unmatched bucket dominates, the check itself is broken and nothing may be reported from it.
2. A missing reading is not an error until the typical word is found in the corpus. Four searches
   came back empty -- 倔强, 薄荷, 参差, the classical 予 -- and all four were therefore non-issues.

## Dictionary-driven sweep, waves 60-68 (rounds 187-205)

The consistency instrument (enumerate every 2-4 character span that is also a phrases_dict key and compare the
readings across occurrences) produced a 145-item list. Every item was adjudicated by looking up its dictionary line
rather than by counting corpus votes, and 220 cells were corrected in nine waves:

  60: 11 words (28 cells)   61: 8 words (24)   62: 9 words (25)   63: 11 words (25)   64: 10 words (17)
  65: 11 words (18)         66: 10 words (16)  67: 12 words (12)  68: 13 words (14)

Result: pass 3,030 -> 3,090, DATA_FIX 974 -> 1,017, ENGINE_BUG 4,958 -> 4,899, DICT_GAP 1,403 -> 1,402.
Twenty-nine candidate rows were examined and deliberately left alone; no accepted change has had to be reverted.

The decisive lesson is that the corpus majority proves nothing. Twenty-six items had the dictionary line against the
majority or against a 1:1 tie, for example 似的 shi4 (13 rows said si4), 芍陂 que4,pi2 (8 rows said bei1), 处在
chu3 (5 rows said chu4), 总长 zong3,zhang3, 说中 shuo1,zhong4, 金匮 jin1,gui4, 教书 jiao1,shu1, 挑檐 tiao3,yan2,
巴结 ba1,jie, 娘娘 niang2,niang, 出差 chu1,chai1, 散剂 san3,ji4, 石磨 shi2,mo4, 人参 ren2,shen1, 心地 xin1,di4,
把儿 ba4, 倾倒 qing1,dao4, 安分 an1,fen4, 年少 nian2,shao4, 中式 zhong4,shi4, 中牟 zhong1,mu4.

The second lesson is that a span is not a word. Four clusters that looked like mass errors dissolved on a
word-boundary check: 为了 x14 was 成为+了 (wei2 is correct there), 地方 x8 was the legitimate local sense di4,fang1,
目的 x8 was 总目/盲目 + the particle 的, and 上都/国都 x8 was 上/国 + 都(dou1). Conversely the same instrument found
real mass errors (起来 x11) in the same period, so each item needs its own look.

Known contradictions inside the repository's own dictionaries, left for a separate engine-side change (they do not
affect the verified): user_dict.txt has 中都:zhong1 dou1, 上都:shang4 dou1, 国都:guo2 dou1, 澄清:cheng2 qing1 (the main
table says deng4,qing1) and 今朝:jin2 zhao1 (the main table says jin1,zhao1).

## Ledger self-check (tools/corpus/ledger_check.py)

Every override record carries the value it replaced, and assemble.py keys on (dataset, row id, cell index). A record
is therefore only valid if the current verified actually holds its target value. The checker takes the LAST record per
cell and verifies that, treating different orders of a single alternative group (mei2/mei6 vs mei6/mei2) as equal.

Current result: 613 records, 579 distinct cells, 3 not in effect. All three are Cantonese records from wave 12 whose
intent was wrong and which the verified correctly never took: cu:1481 wanted 咪 as mai (it is mi in 妈咪), cu:5529 wanted
银行's 行 as hang (Cantonese is hong4) and cu:2733 wanted the deliberately widened man/min narrowed to man. There is
no record anywhere that was judged and then silently lost.

Two hazards this check exists to catch. First, when two waves touch the same cell, a later record whose "old" no
longer matches the file is skipped without complaint; that is how 娘家 (three rows), 菲舍尔 (two rows) and 唠叨 (one
row) stayed only partly corrected until wave 70. Second, the ledger mixes dataset codes -- early records say
"mandarin_tone" while the waves since 26 say "cm" -- so any consumer must accept both or it silently ignores most of
the file.

## Bucket checks versus directed checks (rounds 228-232)

Two kinds of instrument have been used against the same data and they have very different standing.

A bucket check collects every position where a character carries reading A and reports how many sit inside a list of
words that imply reading B. Its unmatched bucket mixes legitimate uses with errors, so it may only raise suspects.
That is how 相 (71 rows tagged xiang4; 53 did not match a xiang1 word list) and 供 (16 rows tagged gong4; 6 did not
match) were handled: every one of the four rows changed was read in full context first, and everything else was left.
Acting on a bucket is what produced the wave-12 errors in the Cantonese set, which the ledger check later exposed as
three stale records the verified had correctly ignored.

A directed check states an implication that must hold: "if 相 here meant xiang1, the following character would form
a xiang1 word". When the list of such words is closed and the scan returns zero, that is evidence of absence, because
the same scan demonstrably catches the errors that were just fixed. Results so far: 相 0 suspects (after three real
fixes in wave 71 -- 相关系数, 相当, 相同), 好 0 suspects, and 供 two suspects (供养) which are the legitimate
gong4 reading for offerings.

Class totals now stand at 50 zero-error classes covering 9,045 positions against roughly 134 confirmed errors
(about 1.5%), every one of which is either fixed or listed as an open item. The zero-error classes include the whole
high-frequency function-word families: 与 648, 了 1293, 着 224, 过 454, 为 (2000+ rows scanned), 说 426, 数 247,
应 197, 好 155, 相 329, 兴 130.

## The four gates a verified change must pass (from round 241)

1. python .tmp/corpus/assemble.py then check_corpus.py -- every row decided, problems 0, identity True for all four
   files. This catches a row that was never ruled on or whose annotation no longer matches its cell count.
2. python tools/corpus/ledger_check.py -- every override record must be in effect in the current verified. This catches the
   case where a record is silently skipped because a later wave touched the same cell; it really happened (娘家 3
   rows, 菲舍尔 2 rows, 唠叨 1 row, found in wave 70). Three stale Cantonese records from wave 12 remain, all of them
   changes the verified was right not to take.
3. python tools/corpus/consistency_scan.py -- no span may disagree with itself beyond the reviewed baseline of 45
   tolerated conflicts. This catches a fix that makes one word inconsistent, which is the failure mode that produced
   most of the real errors found in this stage.
4. cmake --build cmake-build-release and ctest -E "LoadCheck$" -- 8 of 8 with kFrozen matching the files, rewritten
   by .tmp/corpus/_freeze13.py after each wave.

Standing of the verified at this point: 50 zero-error classes covering 9,045 positions against roughly 134 confirmed
errors (about 1.5 per cent), of which every one is either fixed or listed in docs/testdata/open_items.md. Fifteen
instruments were built and discarded for being unusable, and no accepted change has ever had to be reverted.

## Closing state at wave 73 (round 255)

Final counts, all four files assembled and frozen: pass 3097 of 9391 strict (32.97 per cent), ALT_OK 641, DATA_FIX
1024, ENGINE_BUG 4892, DICT_GAP 1402, problems 0 and identity True. Cumulative verified corrections in waves 26 to 73:
382 cells. Zero-error classes verified word by word or by a directed implication test: 50 classes plus a set of
proper names and common words, about 9723 positions. Confirmed errors over the whole stage: roughly 140, every one
either fixed or listed in open_items.md. Of the instruments built, 16 were discarded for failing a plausibility
check and 255 accepted cells landed from the ones that passed; no accepted change has ever had to be reverted.

The strongest closing evidence is negative. The dictionary longest-match instrument, once its tone-marked pinyin was
normalised correctly, compares 7878 phrase instances across the whole corpus and its remaining tail is the
documented tolerance list plus a handful of word-boundary artefacts. The last three rounds read 46 flagged
positions and found exactly one real error (葛洲坝, 8 cells, read with the surname tone of 葛); every other one was a span such as
曾+参与, 的+哥哥, 语言+中, 分子间+作用力, 效 应+得到 that no word-boundary-free scan can separate. In other words,
within reach of every instrument built here, the verified no longer contains a findable error.

What that does not prove is stated plainly: about seventy per cent of the rows have never been read word by word,
and the fifteen items in open_items.md are open by design. The objective is therefore still not complete, and the
honest description of the current verified set is "verified as far as this project's instruments can reach", not
"完全正确".

## Instrument status at round 17 of the convergence pass, and the neutral-tone policy

New corrections since the earlier closing note: wave 74 (12 cells: 柏林 is Bo2lin2, 塞浦路斯 is Sai4pu3lu4si1, and
品相/本相/色相/面相 are all -xiang4), wave 75 (2 cells in the Cantonese pair: 蝕 is sik6, and the corpus itself shows
it twice with two different readings), wave 76 (9 cells in mandarin_untone, which had flattened 似的/地面/了当/落地/
着手/了事 to si/de/le/zhe) and wave 77 (2 cells where 达到了当时 had been read as the word 了当). Total 25 cells.

A fifth guard was added: tools/corpus/cross_set.py. The tone and untone files of one language are two styles of one
corpus, so for every 2-4 character span that is a phrases_dict key and is not inside a longer dictionary word, the
base syllables must agree across the pair. Cantonese: 160 shared spans, zero differences. Mandarin: 142 shared spans,
three tolerated differences (琢磨 and 都会, both of which have two standard readings, and 了当, where the span also
occurs across the words 到了 + 当时).

Instrument status after this pass. The cross-set check is the only one still producing real errors; every other
instrument has run dry. The dictionary longest-match check, even restricted to exclusive spans (7,954 instances,
1,602 mismatches over 292 words), now reports only two things: dictionary entries that are themselves wrong
(勃艮第, 湮灭, 拖拽, 佛罗里达 -- all recorded in engine_issues.md) and a policy difference about neutral tones.

That policy difference is worth stating once. The phrase table marks many second syllables as neutral, so it has
土地 tu3di, 起来 qi3lai, 出来 chu1lai, 明朝 ming2zhao, 大夫 dai4fu, 中的 zhong1de. The verified consistently writes the
full tone instead (tu3di4, qi3lai2, chu1lai2, ming2chao2, and di4fang1 alongside di4fang5). Both policies are
defensible and the verified applies its own consistently; the 43 entries in the consistency baseline are the audit trail.
No verified change is implied by this difference, and a future editor should not "fix" one side to match the other
without rerunning all five guards.

## Substring consistency: the instrument that finally worked, and where it does not

Rounds 33-40 added a third kind of structural check. Two rows sharing a run of eight identical characters must agree
on how those characters are read; when they do not, at least one row is wrong, so the check needs no dictionary and no
sense of taste. On the tone set it reported eight pairs at length eight, eighteen pairs at six, twenty-five at five,
and produced eleven real corrections (waves 82, 83, 84): 骠骑 is piao4qi2, 崔铣 is Cui1 Xi3, 鬲 as a bronze vessel is
li4, 万俟 the surname is Mo4qi2, 高句丽 is Gao1gou1li2, 重檐 is chong2yan2 and 长柄山蚂蟥 has chang2.

Two failures taught more than the successes. First, the same scan on cantonese_tone reports 680 conflicting pairs at
length six -- Cantonese reads many characters differently from word to word (到 as dou3 and dou2, 銀 as ngan2 and
ngan4, 母 as mou2 and mou5), so string equality of readings is not a valid implication there and the instrument must
not be run on that file. Second, on cantonese_untone the same scan found exactly one pair, and it was a genuine
error worth fixing (冠業街 is gun1 in both Cantonese sets; gwun1 does not exist because 冠 has no -w-).

Rule added to the working discipline: when a character may carry more than one reading inside one row, a correction
must be guarded by a word. Changing 重 zhong4 to chong2 only when 檐 follows, or 长 zhang3 to chang2 only when 柄
follows, is what kept waves 83 and 84 from damaging 重要 or 长大 in the same sentences; an unmatched guard simply
changes nothing. Together with "name the row that gets changed" and "a pattern key must be a word, not a character
plus punctuation", this is what the last four waves were checked against before landing.

## Convergence reached at round 48, and what "still split" does and does not mean

The pass from rounds 20 to 48 corrected 84 cells in thirteen waves. The productive instruments were the duplicate
sentence check, the shared-substring check at lengths eight, six and five, and a word-window census that compares how
the corpus reads one character across the words it appears in. The last of these produced the two largest waves:
wave 86 (every remaining 高句丽 read gou1 and 万俟 read Mo4qi2, thirty cells) and wave 88 (every transliterated 塞 that
was still sai1, fifty-eight cells, because 塞 is sai1 only in the 'to stuff' sense and sai4 in names and frontier words).

That census also settled the question of what a split means. A character whose readings divide between words is only
an error when the standard reading is unique or when the corpus majority already sits on the correct side. 塞 and 句
and 万 qualify. 子 does not: its zi3 and zi5 appear across 124 different neighbours, because 子 is zi3 as a noun and
zi5 as a suffix, and both are correct. 父, 垛, 镐, 隗, 蜇 and 锔 are the same kind of split and are listed as held in
open_items.md. Treating any of these as errors would damage the corpus, so the rule is written down: split first,
then ask whether the standard reading is unique.

One further finding belongs here. Wave 86 turned sixteen ENGINE_BUG cells into DICT_GAP cells. They had been recorded
as engine defects when the verified itself was wrong, which means a wrong verified can masquerade as an engine bug and inflate
the engine-bug count. Fifteen of the engine-side dictionary entries in engine_issues.md were verified the same way,
from the verified side, and none of them changed: the verified is right in all of those.

What remains. The five gates are green (check_corpus problems 0, ledger 3 stale records out of 661 and all three known,
consistency 45 reviewed conflicts all in the baseline (two of them, 为重 and 科长, are cross-word collisions rather than errors), cross_set zero new on both language pairs, ctest 8 of 8 with the freeze matching the files).
Two tasks outside this pass are still open: T-09, the corpus licence question, and the long-standing fact that about
70 percent of the rows have never been read word by word -- the instruments above only ever assert things about rows
that repeat or contradict, so the untouched tail rests on the earlier sweeps alone.

## The reading pipeline added at round 52

The structural instruments had run dry by round 51, so the pass returned to the original plan: read the rows. Eight
readers work in parallel, each on a block of about 1,174 lines of the tone set, and each writes
.tmp/corpus/reader_block_N.md with one table row per suspect (line, id, cell index, character, current, proposed,
reason, confidence). The prompt each reader receives spells out the decisions of rounds 20-48 -- what counts as an
error (高句丽 gou1, 万俟 Mo4qi2, 长孙晟 Sheng4, 骠骑 piao4qi2, 铣 xi3, 鬲 li4, 重檐 chong2, 长柄 chang2, transliterated
塞 sai4) and what does not (neutral-tone policy, sense-split polyphones, cross-word collisions like 了当, 科长, 为重).

Two tools support the pipeline and neither touches the data on its own. The validation step re-checks every reported
line against the actual file as a five-tuple (dataset, row id, cell index, character, current value) and drops
anything that does not match, which is what makes a mis-numbered or reversed report harmless. apply_suspects.py then
turns the survivors into a wave, taking only the confidence levels named on its command line and skipping cells that
already carry an override. Nothing is applied without running assemble.py and check_corpus.py afterwards.

## The substring dimension is exhausted (round 58)

The scan was run at lengths eight, seven, six and five. Length seven adds nothing to length eight: its six conflicting
pairs are the same held items (彲, 镐, 隗). Length five and four only re-list known polyphones (塞, 句, 万, 垛, 子, 父,
镐, 隗, 啊, 蜇). Together with the duplicate-sentence check and the cross-set check, that is five overlapping passes
over the whole tone set, and every disagreement they can still see is one of the two documented kinds: a sense-split
polyphone, or a word-boundary collision. New errors at this point have to come from reading rows, not from comparing
them, which is why the reading pipeline of round 52 is the way forward and why the five-gate total is the number to
watch from here on.


## State at round 73 of the convergence pass, with thirteen conflicts still open

The reading pass (waves 89-97) corrected 60 cells across seven waves and, more importantly, exposed the failure mode of
this whole exercise: an audit or a later correction can itself be wrong. Three independent sources did it. The corpus
file carried audit59/60/62/64/65/66/68/71/74/88 notes that rewrote correct readings (似 si4 to shi4 in 近似/类似/相似,
长 chang2 to zhang3 in 长宁/长汀, 匮 kui4 to gui4 in 匮乏, 塞 sai4 in stuffing senses) or wrote a reading onto the wrong
character (audit74 put 相 xiang4 on the preceding 本/色/品/面, an impossible syllable). Waves 91-94 of this pass then
over-applied a correct finding past the word it came from, and the consistency guard caught that too: it made 似的
shi4de5 into si4, 校正 into xiao4, 上当 into dang1, 大地 into de5, 任重 into chong2, and moved 长 to chang2 inside the
official titles 市长/县长/师长/排长/州长/所长/成长. Wave 95 was reverted (commit b2fbc94) and two genuine leftovers were
re-applied in wave 97.

Two rules came out of it, and they are the reason to keep the guard running. A pattern key must be a word, not a
character plus a neighbour: wave 79 and wave 95 both failed on this, and both times the damage was caught before it
shipped only because every wave re-runs the guards and can be reverted in one command. And when two occurrences of a
word disagree, the side to change must be named explicitly, because a (row, index, current value) triple that does not
match simply changes nothing -- that property turned two wrong waves into no-ops rather than into damage.

Still open: thirteen consistency conflicts (listed by tools/corpus/consistency_scan.py; they are the next wave), roughly
16 percent of the tone set never read by a reader (each block stopped early when asked to report), T-09 the corpus
licence question, and the engine-side dictionary defects in engine_issues.md, which are deliberately not applied to the
verified. Gates at this moment: check_corpus problems 0, ledger 843 records with 3 known stale, consistency 61 conflicts
against a reviewed baseline of 49, cross_set zero new on both language pairs, ctest 8 of 8 with the freeze matching.


## Round 95: the reading pass is closed, and what it proved

The row-by-row reading pass is finished for the tone set. Eight readers covered about 84 percent of mandarin_tone.txt in
blocks and reported 60 suspects; a parent pass then read every line the blocks had skipped, so all 9391 rows have now
been read word by word at least once. The pass became waves 89 to 103 and corrected 629 cells in total across waves 74
to 103, alongside two reverted waves (79 and 95).

Three defect classes were closed. The first and largest was the audit note: the file carried audit59/60/62/64/65/66/68/
71/74/88/94 notes that had rewritten correct readings (似 si4 to shi4 in 近似/类似/相似, 长 chang2 to zhang3 in 长宁/
长汀/长坡, 匮 kui4 to gui4 in 匮乏, 塞 sai4 in stuffing senses, 中 zhong1 in 中出现, 声 sheng1 to a sheng5 that does
not exist), or had written a reading onto the wrong character altogether (audit74 put 相 xiang4 on the preceding 本/色/
品/面, leaving an impossible syllable). The rule that follows is blunt: an auditNN=fix marker is not evidence of
correctness, and audited cells deserve to be checked first, not last. The second class was format: 22 reading cells
still held their source character instead of a syllable (閗 讐 勅 锺 尙 憍 愻 爇 儁 麹 栰 誴 廧 朮 and others); all are
converted, and the scan for cells equal to their own character now returns zero. The third class was this pass's own
over-reach, described above, caught by the consistency guard and reverted.

Two rules were earned the hard way and now belong with the discipline list. A pattern key must be a word, never a
character plus a neighbour: waves 79 and 95 both failed on that, and only the re-runnable guards limited the damage.
And when two occurrences of a word disagree, the side to change has to be named as a (row, index, current value)
triple, because a triple that does not match changes nothing - which is what turned both bad waves into no-ops instead
of corruption.

What is not done. The untone and the two Cantonese tables have not had a pass of this depth; the 64-entry consistency
baseline and the 5-entry cross-set baseline record what was adjudicated and why, and both guards report zero new
findings. T-09, the corpus licence question, is still open, and the thirteen engine-side dictionary defects listed in
engine_issues.md are still deliberately kept out of the verified. Gates at the close of the pass: check_corpus problems 0,
identity true for all four tables, ledger 843 records, consistency new 0, cross_set new 0, ctest 8 of 8.


## Round 104: where an audit note can and cannot be trusted

The censuses make the boundary concrete, so it is worth stating before anyone reads them as one rule. An auditNN=fix
note is not evidence of correctness, but the reason differs by table, and the tables are not equally affected.

mandarin_tone.txt carries 924 audited cells, all of them written before this pass. Their content is wrong often enough
that treating them as reviewed is unsafe: the readings they produced include 似 shi4 for si4 in 近似/类似/相似, 匮 gui4
for kui4 in 匮乏, 长 zhang3 for chang2 in 长宁/长汀/长坡, 了 liao3 for the particle le5, 声 sheng5 for sheng1 in 声援,
and audit74 put 相 xiang4 onto the preceding character, giving 本/色/品/面 an impossible syllable. Waves 89 to 99
corrected the ones the readers and the guards pointed at; the rest deserve a census of their own before the table is
called converged.

mandarin_untone.txt carries only 11 audited cells, and nine of them were wrong - the particle 了 rewritten as liao, the
adverb 地 as di, 着 as zhuo, 似 as shi in 类似/相似, 剥 as bao in 剥开. All nine were corrected in wave 105, so this
table's audited set is now clean. That 9 of 11 rate is the strongest single signal found in this pass.

cantonese_tone.txt carries 231 audited cells and cantonese_untone.txt none. Sampling 26 of the Cantonese cells (嚟 lei4
thirteen times, 跌 dit3 nine, 緩 wun6 two, 吓 haa5 two) found every one correct, because those notes were not inherited
from the corpus: they are the records this session's own earlier waves wrote when they corrected the Cantonese tables,
and they are listed in the override ledger. The same ledger holds 148 Cantonese tone records and 9 untone records. So
the rule has a shape: inherited audit notes are a lead, self-written ones are a record, and the two must be told apart
by their origin rather than by their tag.

Totals at this point: check_corpus problems 0 with identity true for all four tables; ledger 861 records; consistency 63
conflicts against a reviewed baseline of 64, zero new; cross_set zero new on both pairs against a baseline of 9; ctest 8
of 8. Verified totals are mandarin_tone 3063 pass, mandarin_untone 1063, cantonese_tone 4944, cantonese_untone 5767.


## Round 106: the audited surface of the tone table is consistent

All 924 audit notes in mandarin_tone.txt were re-checked against the corpus itself. For each audited cell whose
wide context (the character plus two characters either side) is read more than one way in the file, the cell's current
value was compared with the majority reading for that context. Result: 918 cells checked, 0 disagreeing. Cells whose
context is unanimous are skipped by construction, and the test only proves agreement with the corpus's own majority, not
absolute correctness - the word-level guards and the five gates still carry that weight. But it does mean the readings
written by the inherited audit notes no longer contradict the corpus anywhere, which is the property that did not hold
when this pass began, when families like 似 si4/shi4, 长 chang2/zhang3 and 匮 kui4/gui4 were split down the middle.

With this, all four tables' inherited or self-written audit surfaces have been handled: mandarin_tone consistent at 918
of 918; mandarin_untone has only 11 notes and nine of them were wrong and are fixed; cantonese_tone's 231 notes are this
session's own records and sampled correct; cantonese_untone has none. What remains open is depth rather than repair:
line-by-line reading of the untone and the two Cantonese tables, the T-09 licence question, and the engine-side
dictionary defects that are deliberately kept out of the verified.
