"""Row-level projection check: cantonese_untone.txt is the strip(TONE3) projection of cantonese_tone.txt.

For every cantonese_untone row we find the cantonese_tone row carrying the same sentence and
compare the untone reading against the tone-stripped tone reading.  A disagreement is a real
lead (unlike the span-level cross_set check, where a collision across different sentences is
meaningless); so is a sentence that has no twin at all.  Exit status is non-zero when the
projection is broken, so this can be used as a gate.

Tools: tests/data/corpus/cu_rowcheck.py
"""
import collections
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
CT = ROOT / "tests" / "data" / "verified" / "cantonese_tone.txt"
CU = ROOT / "tests" / "data" / "verified" / "cantonese_untone.txt"

TONE = re.compile(r"[1-6]$")


def strip_tones(reading):
    out = []
    for syl in reading.split():
        if "/" in syl:  # widened cell a/b -> keep both, order-insensitive
            parts = [TONE.sub("", p) for p in syl.split("/")]
            out.append("/".join(sorted(set(parts))))
        else:
            out.append(TONE.sub("", syl))
    return out


def load(path):
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#!meta"):
            continue
        parts = line.split("\t")
        if len(parts) < 4:
            continue
        rid = re.search(r"id=(cu|ct):(\d+)", parts[3])
        rows.append({
            "sent": parts[0],
            "reading": parts[1],
            "status": parts[2],
            "note": parts[3],
            "id": int(rid.group(2)) if rid else None,
        })
    return rows


def main():
    ct_rows = load(CT)
    cu_rows = load(CU)
    by_sent = collections.defaultdict(list)
    for r in ct_rows:
        by_sent[r["sent"]].append(r)

    matched = 0
    unmatched = []
    diffs = []
    for r in cu_rows:
        cands = by_sent.get(r["sent"])
        if not cands:
            unmatched.append(r)
            continue
        matched += 1
        cu_syl = strip_tones(r["reading"])
        agree = False
        for c in cands:
            ct_syl = strip_tones(c["reading"])
            if len(ct_syl) == len(cu_syl) and all(
                set(a.split("/")) == set(b.split("/")) for a, b in zip(cu_syl, ct_syl)
            ):
                agree = True
                break
        if not agree:
            diffs.append({
                "cu_id": r["id"],
                "ct_ids": [c["id"] for c in cands],
                "sent": r["sent"],
                "cu": r["reading"],
                "ct": cands[0]["reading"],
            })

    print("cu rows: %d   ct rows: %d" % (len(cu_rows), len(ct_rows)))
    print("sentence matched: %d   unmatched: %d   disagreeing: %d"
          % (matched, len(unmatched), len(diffs)))
    for r in unmatched[:20]:
        print(json.dumps({"cu_id": r["id"], "sent": r["sent"]}, ensure_ascii=True))
    for d in diffs[:40]:
        print(json.dumps(d, ensure_ascii=True))
    return 1 if (unmatched or diffs) else 0


if __name__ == "__main__":
    sys.exit(main())
