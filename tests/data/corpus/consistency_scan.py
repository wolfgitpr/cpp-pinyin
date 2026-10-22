"""Consistency guard for the v2 verified sets.

Every 2-4 character span that is also a phrases_dict key is collected, and spans whose readings differ across
occurrences (ignoring a neutral-tone mark) are listed. The file tests/data/corpus/consistency_baseline.txt holds the
conflicts that have been examined and accepted: cross-word spans (为了 inside 成为了), words with two standard
readings (当时 dang1/dang4, 分子 fen1/fen4, 地方 di/neutral di/fang1) and the items listed in
tests/data/testdata/open_items.md. This script fails when a conflict appears that is not in the baseline, which is how a
future edit is caught if it makes one word inconsistent.

Usage:  python tests/data/corpus/consistency_scan.py       (exit 0 = only known conflicts, 1 = something new)
"""
import io as _io, collections, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
PHRASES = os.path.join(ROOT, "data", "dialects", "mandarin", "phrases_dict.txt")
CORPUS = os.path.join(ROOT, "tests", "data", "verified", "mandarin_tone.txt")
BASE = os.path.join(ROOT, "tests", "data", "corpus", "consistency_baseline.txt")


def scan():
    phr = set()
    for line in _io.open(PHRASES, encoding="utf-8"):
        w = line.split(":")[0]
        if 2 <= len(w) <= 4:
            phr.add(w)
    conf = collections.defaultdict(lambda: collections.defaultdict(int))
    for line in _io.open(CORPUS, encoding="utf-8"):
        f = line.rstrip("\n").split("\t")
        if len(f) < 3 or not f[1].strip():
            continue
        sent, cells = f[0], f[1].split()
        if len(cells) != len(sent):
            continue
        for L in (2, 3, 4):
            for i in range(len(sent) - L + 1):
                span = sent[i:i + L]
                if span not in phr:
                    continue
                val = tuple(cells[i:i + L])
                if any("/" in v for v in val):
                    continue
                conf[span][val] += 1
    out = {}
    for k, v in conf.items():
        if len(v) < 2:
            continue
        if len(set(tuple(x.replace("5", "4") for x in t) for t in v)) == 1:
            continue
        out[k] = v
    return out


def main():
    known = set()
    if os.path.exists(BASE):
        for line in _io.open(BASE, encoding="utf-8"):
            if "::" in line:
                known.add(line.split("::")[0].strip())
    real = scan()
    new = [k for k in real if k not in known]
    gone = [k for k in known if k not in real]
    print("conflicts: %d | baseline: %d | new: %d | no longer present: %d" % (len(real), len(known), len(new), len(gone)))
    for k in new:
        print("  NEW %s :: %s" % (k, " | ".join(",".join(t) for t in real[k])))
    return 1 if new else 0


if __name__ == "__main__":
    sys.exit(main())
