"""Ledger self-check for the v2 verified sets.

For every override record, take the LAST record for each (dataset, row id, cell index) and verify that the
current verified actually carries that record's target value. A record that is not in effect means either the row
was later changed again (fine, the later record should then be in effect) or the record was silently skipped --
which happens when a later record's "old" value no longer matches the file, e.g. after another wave touched the
same cell. That is exactly how 娘家, 菲舍尔 and 唠叨 stayed only partly corrected until wave 70.

Two rules keep the check usable as a CI gate rather than a tripwire on accepted practice:

* a cell the verified deliberately WIDENED to a/b satisfies a narrower record ("2026-09-29 variant doctrine": a
  lone value on either side of a widened cell is an accepted variant, so the record's target is still present);
* records listed in tests/data/corpus/ledger_stale.txt are accepted as known-stale -- that file carries the reason and
  is expected to stay tiny. Anything else that is not in effect is a failure.

Usage:  python tests/data/corpus/ledger_check.py [overrides.jsonl] [corpus_dir]
Exit status is 0 when every record is in effect or explainable, 1 otherwise.
Prints one machine-checkable summary line:
    records=N distinct cells=M not in effect=K allowlisted=A unexpected=U
"""
import io as _io
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
OVERRIDES = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "tests", "data", "corpus", "scan_overrides.jsonl")
CORPUS_DIR = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "tests", "data", "verified")
STALE_ALLOW = os.path.join(ROOT, "tests", "data", "corpus", "ledger_stale.txt")
FILES = {"cm": "mandarin_tone.txt", "mu": "mandarin_untone.txt",
         "ct": "cantonese_tone.txt", "cu": "cantonese_untone.txt"}
# the ledger mixes short codes with the long dataset names used by the reader payloads
ALIAS = {"mandarin_tone": "cm", "mandarin_untone": "mu",
         "cantonese_tone": "ct", "cantonese_untone": "cu"}


def load(ds):
    path = os.path.join(CORPUS_DIR, FILES[ds])
    out = {}
    for line in _io.open(path, encoding="utf-8"):
        f = line.rstrip("\n").split("\t")
        if len(f) < 4:
            continue
        rid = f[3].split("id=")[-1].split(";")[0] if "id=" in f[3] else "?"
        out[rid] = (f[0], f[1].split() if f[1].strip() else [])
    return out


def load_allowlist():
    out = set()
    if not os.path.exists(STALE_ALLOW):
        return out
    for line in _io.open(STALE_ALLOW, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        f = line.split()
        if len(f) >= 3:
            out.add((f[0], f[1], int(f[2])))
    return out


def main():
    last, total = {}, 0
    for line in _io.open(OVERRIDES, encoding="utf-8"):
        line = line.strip()
        if not line:
            continue
        r = json.loads(line)
        ds = ALIAS.get(r.get("ds"), r.get("ds"))
        if ds not in FILES:
            continue
        total += 1
        last[(ds, r["id"], r["i"])] = r
    verified = {ds: load(ds) for ds in FILES}
    allowed = load_allowlist()
    bad, known, widened = [], [], []
    for (ds, rid, i), r in last.items():
        if rid not in verified[ds]:
            bad.append((ds, rid, i, r, "row missing"))
            continue
        sent, cells = verified[ds][rid]
        if i >= len(cells):
            bad.append((ds, rid, i, r, "index out of range"))
            continue
        want = r.get("to", "")
        got = cells[i]
        ok = (got == want) or ("/" in want and sorted(want.split("/")) == sorted(got.split("/")))
        if not ok and "/" in got and want and "/" not in want and want in got.split("/"):
            # the verified widened the cell; the record's target is still among the accepted readings
            widened.append((ds, rid, i, r, got))
            continue
        if not ok:
            if (ds, rid, i) in allowed:
                known.append((ds, rid, i, r, got))
            else:
                bad.append((ds, rid, i, r, "verified has " + got))
    print("records=%d distinct cells=%d not in effect=%d allowlisted=%d unexpected=%d"
          % (total, len(last), len(bad) + len(known), len(known), len(bad)))
    print("  satisfied by widening=%d" % len(widened))
    for ds, rid, i, r, why in bad[:40]:
        print("  UNEXPECTED %s %s i=%d wave=%s %s -> %s | %s"
              % (ds, rid, i, r.get("wave"), r.get("old", "?"), r.get("to", r.get("action")), why))
    for ds, rid, i, r, why in known[:40]:
        print("  allowlisted %s %s i=%d wave=%s %s -> %s | %s"
              % (ds, rid, i, r.get("wave"), r.get("old", "?"), r.get("to", r.get("action")), why))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
