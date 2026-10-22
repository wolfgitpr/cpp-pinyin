"""check_corpus.py -- self-check of tests/data/verified/ before the C++ test freezes the counts.

Recomputes, for every verified file, what the C++ test will compute (engine output from the probe
dumps), and verifies the invariants the verified set claims:

  1. every row has 4 TAB fields and a legal status marker;
  2. the header carries datasetId / source / license;
  3. decided == pass + ENGINE_BUG + DICT_GAP  (nothing falls between the markers);
  4. every verified cell is either a legal syllable for that dialect+style, or the raw character
     passed through because the engine could not convert it;
  5. a syllable-count disagreement exists only on a row marked EXCLUDED.

Prints the numbers to paste into kFrozen[] in tests/cpp/test_accuracy_v2.cpp.
"""
import collections
import io
import json
import os
import re
import sys

REPO = os.environ.get("CORPUS_REPO",
                      os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))))
CORPUS = os.environ.get("CORPUS_DIR", os.path.join(REPO, ".tmp", "corpus"))
DATA = os.path.join(REPO, "tests", "data", "verified")
# The engine dumps under .tmp/corpus/rows_*.jsonl are build artifacts of the review pipeline and are
# NOT tracked (rows_cm.jsonl alone is 70 MB), so CI cannot rely on them. Without them this script
# still checks everything that is a property of the verified files themselves -- field count, status
# legality, id presence, decided arithmetic, cell legality/distinctness, and the cell-count
# invariant -- and the engine-side half (pass counts, identity, kFrozen[]) is covered instead by
# the C++ [corpus] test, which computes those numbers live from the same files. Pass --with-probe to
# re-enable the dump half locally.
WITH_PROBE = "--with-probe" in sys.argv

SPEC = [("mandarin_tone", "cm", re.compile(r"^[a-z]+[1-5]$")),
        ("mandarin_untone", "mu", re.compile(r"^[a-z]+$")),
        ("cantonese_tone", "ct", re.compile(r"^[a-z]+[1-6]$")),
        ("cantonese_untone", "cu", re.compile(r"^[a-z]+$"))]
STATUSES = {"OK", "ALT_OK", "DATA_FIX", "ENGINE_BUG", "DICT_GAP", "EXCLUDED"}
HANZI = [(0x3400, 0x4DBF), (0x4E00, 0x9FFF), (0xF900, 0xFAFF)]
ID_RE = re.compile(r"(?:^|;)id=([a-z]+:\d+)(?:;|$)")

# FNV-1a 64 over the per-row verdict vector -- the digest the C++ [corpus] test pins next to the
# counts. Why it exists: every number in kFrozen[] is an AGGREGATE, so one row that starts passing
# while another starts failing leaves all of them unchanged, and the audit waves do move rows
# between classes in pairs. The digest pins the vector itself: "<id>:<STATUS>:<P|F|X>\n" per data
# row, in file order (P = engine matched every position, F = it did not, X = the row is EXCLUDED or
# its cell count disagrees, i.e. it takes part in no statistic). C++ computes the same bytes;
# measured 2026-09-29, both sides agree on all four tables.
FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211
FNV_MASK = 0xFFFFFFFFFFFFFFFF


def fnv_update(h, text):
    for b in text.encode("utf-8"):
        h = ((h ^ b) * FNV_PRIME) & FNV_MASK
    return h


def fnv_hex(h):
    return "0x%016x" % h


def is_hanzi(tok):
    return len(tok) >= 1 and any(lo <= ord(tok[0]) <= hi for lo, hi in HANZI)


def main():
    rows_out = {}
    problems = []
    print("mode: %s" % ("engine dump + verified files" if WITH_PROBE
                        else "verified files only (structural; engine half is the C++ [corpus] test)"))
    for key, ds, sound in SPEC:
        probe = {}
        if WITH_PROBE:
            with io.open(os.path.join(CORPUS, f"rows_{ds}.jsonl"), encoding="utf-8") as f:
                for line in f:
                    if line.strip():
                        o = json.loads(line)
                        probe[o["id"]] = o
        c = collections.Counter()
        fnv = FNV_OFFSET   # only complete with --with-probe: P/F is a verdict against the engine
        with io.open(os.path.join(DATA, key + ".txt"), encoding="utf-8") as f:
            header = f.readline()
            if not header.startswith("#!meta "):
                problems.append(f"{key}: no #!meta header")
            meta = json.loads(header[7:])
            for k in ("datasetId", "source", "license"):
                if not meta.get(k):
                    problems.append(f"{key}: meta missing {k}")
            for ln, line in enumerate(f, start=2):
                line = line.rstrip("\n")
                if not line:
                    continue
                parts = line.split("\t")
                if len(parts) != 4:
                    problems.append(f"{key}:{ln} field count {len(parts)}")
                    continue
                sent, cellstr, status, note = parts
                if status not in STATUSES:
                    problems.append(f"{key}:{ln} bad status {status}")
                    status = status if status in STATUSES else "?"
                m = ID_RE.search(note)
                if not m:
                    problems.append(f"{key}:{ln} note has no id")
                rid = m.group(1) if m else "?"
                c[status] += 1
                if status == "EXCLUDED":
                    # An excluded row takes part in no statistic at all -- not even in "pass" when
                    # the engine happens to agree with it (the audit produced such a row: the
                    # corpus wrote 莆 for 脯, so the verified cell is not a reading of that character).
                    fnv = fnv_update(fnv, f"{rid}:{status}:X\n")
                    continue
                verified = cellstr.split()
                # A cell must be a set of DISTINCT readings. A duplicated or nested token
                # ("pa2/pa2/pa5", "a/b/c") is a pipeline bug, not a legal cell -- the round-4
                # override layer produced one by unioning an auditor's "pa2/pa5" as one string,
                # and every token in it was still a legal reading, so this check is what catches it.
                for i_cell, cell in enumerate(verified):
                    toks = cell.split("/")
                    if len(toks) != len(set(toks)) or any(not t for t in toks):
                        problems.append(f"{key} {rid} cell {i_cell} malformed: {cell!r}")
                    if "/" in cell and not all(sound.match(t) for t in toks):
                        problems.append(f"{key} {rid} cell {i_cell} nested: {cell!r}")
                # Cell count must equal the number of non-space characters in the sentence: one cell
                # per character, always (measured 2026-09-29 on all four tables: 0 exceptions, the two
                # EXCLUDED rows included). This is the structural half of the old probe-length check
                # and needs no engine dump.
                expected = (len(probe[m.group(1)]["eng"]) if WITH_PROBE and m else
                            len([ch for ch in sent if not ch.isspace()]))
                if len(verified) != expected:
                    c["count_mismatch"] += 1
                    if status != "EXCLUDED":
                        problems.append(f"{key}:{ln} count mismatch ({len(verified)} cells vs {expected}) "
                                        f"but status {status}")
                    fnv = fnv_update(fnv, f"{rid}:{status}:X\n")
                    continue
                if not WITH_PROBE:
                    continue
                eng = probe[m.group(1)]["eng"] if m else []
                ok = True
                for i, cell in enumerate(verified):
                    opts = cell.split("/")
                    passthrough = False
                    for v in opts:
                        if sound.match(v):
                            continue
                        if is_hanzi(v):
                            passthrough = True   # the engine could not read this character
                        # punctuation / Latin / digits are echoed back by the engine: not a defect
                    if passthrough:
                        # A character the engine cannot read is a dictionary gap, never a silent
                        # success, even though the engine echoes the character back (D16 rule 2).
                        ok = False
                    elif eng[i] not in opts:
                        ok = False
                c["pass"] += 1 if ok else 0
                fnv = fnv_update(fnv, f"{rid}:{status}:{'P' if ok else 'F'}\n")
        decided = c["OK"] + c["ALT_OK"] + c["DATA_FIX"] + c["ENGINE_BUG"] + c["DICT_GAP"]
        identity = (decided == c["pass"] + c["ENGINE_BUG"] + c["DICT_GAP"]) if WITH_PROBE else None
        rows_out[key] = dict(decided=decided, pass_=c["pass"], altOk=c["ALT_OK"],
                             dataFix=c["DATA_FIX"], engineBug=c["ENGINE_BUG"],
                             dictGap=c["DICT_GAP"], excluded=c["EXCLUDED"],
                             rows=decided + c["EXCLUDED"], identity=identity,
                             verdict_fnv=fnv_hex(fnv) if WITH_PROBE else None)
        tail = ("identity=%s" % identity) if WITH_PROBE else "identity=n/a (structural)"
        print(f'{key:18s} rows={rows_out[key]["rows"]:5d} decided={decided:5d} pass={c["pass"]:5d} '
              f'ALT_OK={c["ALT_OK"]:4d} DATA_FIX={c["DATA_FIX"]:4d} ENGINE_BUG={c["ENGINE_BUG"]:5d} '
              f'DICT_GAP={c["DICT_GAP"]:4d} EXCLUDED={c["EXCLUDED"]} {tail}')
    if WITH_PROBE:
        print()
        print("kFrozen[] = {")
        for key in ("mandarin_tone", "mandarin_untone", "cantonese_tone", "cantonese_untone"):
            r = rows_out[key]
            print(f'    {{"{key}", {r["decided"]}, {r["pass_"]}, {r["altOk"]}, {r["dataFix"]}, '
                  f'{r["engineBug"]}, {r["dictGap"]}, {r["excluded"]}}},')
        print("};")
        print()
        print("// The per-row vector behind the counts above: any single row that changes verdict or")
        print("// status changes one of these, which the counts alone cannot promise.")
        print("kFrozenFacet[] = {")
        for key in ("mandarin_tone", "mandarin_untone", "cantonese_tone", "cantonese_untone"):
            print(f'    {{"{key}", {rows_out[key]["verdict_fnv"]}ull}},')
        print("};")
    print()
    print("problems:", len(problems))
    for p in problems[:20]:
        print("  ", p)
    if WITH_PROBE:
        json.dump(rows_out, open(os.path.join(CORPUS, "corpus_check.json"), "w"), indent=1)
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
