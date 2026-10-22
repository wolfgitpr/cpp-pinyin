# Coverage of the reading pass

mandarin_tone.txt: all 9391 rows read word by word. Eight readers covered roughly 84 percent in blocks (block 1 lines
1-463, block 2 1175-2003, block 3 2349-3196, block 4 3523-4164, block 5 4697-5639 and 5685-5848, block 6 5871-7029,
block 7 7045-8008, block 8 8219-9083), and the parent pass read the remainder, including the gaps each block left
behind (464-1174, 2004-2348, 3197-3522, 4165-4696, 5640-5684 and 5849-5870, 7030-7044, 8009-8218, 9084-9391).

cantonese_tone.txt: all 6354 rows read cell by cell (rounds 176-245 of the same session, 40-row slices up to line 4797
and 80-row slices after that). Every cell was adjudicated against the standard Cantonese reading and every was=/auditNN=
trail was re-derived from its origin. Result: no genuine verified error found in the whole pass. Every repair trail
(audit-injected readings, wrong-character fixes, misplaced fix trails) turned out to be a correct repair of a wrong
value, not a defect.

mandarin_untone.txt: all 1138 rows read cell by cell (rounds 246-256, 80-row slices, final slice to line 1139 inclusive).
The audit surface of this table is clean (11 audit cells, none disagreeing). It is a toneless table, so only
cross-syllable polyphones can be checked there; those were adjudicated one by one (le/liao, de/dei, zhe/zhuo, zhong/chong,
zhuan/zhuan, bo/bao, huan/hai, hang/xing, zhang/chang, si/shi, mo/mu, and the pairs left widened as ALT_OK). Result: no
genuine verified error found.

cantonese_untone.txt: no independent reading pass, by decision -- it is by construction the strip(TONE3) projection of
cantonese_tone.txt, whose 6354 rows were read cell by cell above, so its readings are covered by transitivity rather
than by a second pass over the same content. The projection itself is verified at row level (tools/corpus/cu_rowcheck.py):
all 6354 sentences match a cantonese_tone row and every tone-stripped reading agrees cell for cell (0 unmatched,
0 disagreeing), and the table carries zero audit/was= trails, so there is no repair surface to re-derive. A separate
8-reader pass over cantonese_untone.txt was started and then stopped once this was pointed out; the 1040 rows the parent
had read in that pass had raised no issue. cross_set.py adds an independent check against the tone table
(160 shared exclusive spans, 0 differences).

The untone tables are held to the same verified rules and are checked by cross_set.py against each other and against the
tone table, which reports zero new differences; their open questions are the sense splits listed in open_items.md.

Final state at the end of this pass (all four tables): check_corpus problems 0 (identity True everywhere when run with
--with-probe), ledger_check 883 records with 2 accepted stale records in tools/corpus/ledger_stale.txt and 1 further
record satisfied by a widened cell (`unexpected=0`), consistency_scan 63 conflicts against the 64 baseline with 0 new,
cross_set cm/mu 142 spans / 8 differences / 0 new and ct/cu 160 spans / 0 differences / 0 new, ctest 15/15 -- the five
data-side gates now run as `CorpusGate_*` tests next to `CorpusAccuracy` (tools/corpus/run_corpus_gates.py runs them by hand).

That integration earned its keep immediately: rebuilding the asset copies exposed a stale freeze -- the
mandarin_untone row of `kFrozen[]` still held the pre-wave-105 numbers (pass 1066 / DATA_FIX 24 / ENGINE_BUG 70) while
the committed verified measured 1063 / 19 / 73. The C++ test had been passing only because the build tree still contained
an older copy of that file, which is exactly the failure mode the copy step was supposed to prevent; the copies are
unconditional now (tests/CMakeLists.txt) and the frozen row is corrected.

Tools: check_corpus.py (structural by default, --with-probe for the engine side), ledger_check.py, consistency_scan.py
(baseline 64), cross_set.py (baseline 9), cu_rowcheck.py (projection gate, exit 0), run_corpus_gates.py (runs all five,
--self-test proves it can fail), apply_suspects.py.

Names (renamed 2026-09-29): the tables live in `tests/verified/` (`mandarin_tone.txt` was
`tests/testData2/mandarin_ctx.txt`, dataset id `mandarin_ctx` -> `mandarin_tone`), the corpora they were derived from
in `tests/verified/source/` (was `tests/testData/`), and the tooling in `tools/corpus/` (was `tools/gold/`). The
full mapping, and why the dated records keep the old names, is in tools/corpus/README.md.
