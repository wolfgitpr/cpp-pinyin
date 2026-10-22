"""Cross-set guard: the tone and untone files of the same language must agree on base syllables.

The Cantonese tone and untone files are two styles of one corpus and so are the two Mandarin files. For every 2-4
character span that is a phrases_dict key and is not contained in a longer dictionary word (an exclusive span), the
base syllables (letters without tone digits) must match across the pair. Importants exceptions live in
tests/data/corpus/cross_set_baseline.txt: words whose two standard readings differ by base syllable, such as 琢磨
zhuo2mo2 / zuo2mo5 and 都会 dou1hui4 / du1hui4.

Usage:  python tests/data/corpus/cross_set.py        (exit 0 = only known differences, 1 = something new)
"""
import io as _io, collections, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
DATA = os.path.join(ROOT, "tests", "data", "verified")
PHRASES = os.path.join(ROOT, "data", "dialects", "mandarin", "phrases_dict.txt")
BASE = os.path.join(ROOT, "tests", "data", "corpus", "cross_set_baseline.txt")


def base(s):
    m = re.match(r"^([a-z]+)", s)
    return m.group(1) if m else s


def load(name, phr):
    d = collections.defaultdict(collections.Counter)
    for line in _io.open(os.path.join(DATA, name), encoding="utf-8"):
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
                ext = False
                for L2 in (L + 1, L + 2):
                    for st in (i - (L2 - L), i, i - 1):
                        if st < 0 or st + L2 > len(sent):
                            continue
                        if sent[st:st + L2] in phr:
                            ext = True
                if ext:
                    continue
                val = tuple(cells[i:i + L])
                if any("/" in v for v in val):
                    continue
                d[span][val] += 1
    return d


def main():
    phr = set(l.split(":")[0] for l in _io.open(PHRASES, encoding="utf-8"))
    known = set()
    if os.path.exists(BASE):
        for line in _io.open(BASE, encoding="utf-8"):
            if "::" in line:
                known.add(line.split("::")[0].strip())
    total_new = 0
    for label, n1, n2 in (("cm/mu", "mandarin_tone.txt", "mandarin_untone.txt"),
                          ("ct/cu", "cantonese_tone.txt", "cantonese_untone.txt")):
        a, b = load(n1, phr), load(n2, phr)
        bad = []
        for w in set(a) & set(b):
            ba = set(tuple(base(x) for x in t) for t in a[w])
            bb = set(tuple(base(x) for x in t) for t in b[w])
            if ba != bb:
                bad.append(w)
        new = [w for w in bad if w not in known]
        total_new += len(new)
        print("%s shared exclusive spans: %d | differences: %d | new: %d" % (label, len(set(a) & set(b)), len(bad), len(new)))
        for w in new:
            print("   NEW %s" % w)
    return 1 if total_new else 0


if __name__ == "__main__":
    sys.exit(main())
