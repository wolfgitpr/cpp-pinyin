#!/usr/bin/env python3
"""Cross-repository check: the dictionary repo's GENERATED tables against this repo's FROZEN copies.

Why this exists. `data/dialects/<id>/` is consumed by this library and produced by pinyin-makedict, and
the two are checked against each other by that repository's `tools/check_drift.ps1` -- which lives in
the other repository and therefore cannot run as part of this one's gate. What can run here is the part
that does not need the generators: given a generated tree, are the tables this library ships the ones
that tree produces?

Scope, stated so it is not mistaken for more than it is:
  * it compares TABLE NAMES and ROW COUNTS, not bytes. Byte equality is what the frozen dump gate
    (`DumpEquivalence` against `CPP_PINYIN2_FROZEN_DUMP_SHA256`) covers for this repository's own data,
    and what the other repository's drift script covers for the producer side;
  * it needs `out/dict` from the other repository. That output is a generated artefact and is not
    committed, so this is a TOOL, not a CTest entry: a gate that silently skips when a sibling
    checkout is missing is worse than no gate.

Usage:
    python tools/pinyin_makedict_output.py <makedict-out-dict-dir> [--dialects <this-repo's data/dialects>]
    python tools/pinyin_makedict_output.py --self-test     # prove it can fail, on fixtures only

Exit codes: 0 = the frozen copies match the generated tree on every table this tool compares;
1 = drift; 2 = usage or a missing tree (never silently "ok").
"""

import argparse
import json
import sys
import tempfile
from pathlib import Path

# The tables a dialect directory must have, in the order they are reported. Names come from
# data/dialects/<id>/dialect.json's required_tables; the list is duplicated here on purpose so that a
# dialect.json that loses an entry is caught by the comparison rather than by the missing entry.
REQUIRED_TABLES = ("word.txt", "phrases_dict.txt", "phrases_map.txt", "trans_word.txt")


def row_count(path):
    """Rows, counting a trailing newline as the end of the last row rather than as an empty row."""
    data = path.read_bytes()
    if not data:
        return 0
    return data.count(b"\n") + (0 if data.endswith(b"\n") else 1)


def compare(dict_root, dialects_root):
    findings = []
    rows = []
    dialects = sorted(p.name for p in dialects_root.iterdir() if p.is_dir())
    if not dialects:
        findings.append(f"note: no dialect directories under {dialects_root}")
    for dialect in dialects:
        generated = dict_root / dialect
        if not generated.is_dir():
            findings.append(f"note: {dialect} exists in this repository but not in the generated tree")
            continue
        # REQUIRED_TABLES duplicates this dialect's dialect.json on purpose (see the comment above), so
        # the duplication has to be CHECKED rather than trusted: a manifest that gains or loses an entry
        # would otherwise leave this tool comparing the wrong set of tables -- the same kind of silent
        # narrowing that a hardcoded dialect list caused on the producer side until 2026-10-04.
        manifest = dialects_root / dialect / "dialect.json"
        if manifest.is_file():
            declared = None
            try:
                declared = json.loads(manifest.read_text(encoding="utf-8")).get("required_tables")
            except (OSError, ValueError):
                declared = None
            if isinstance(declared, list) and declared:
                extra = sorted(set(declared) - set(REQUIRED_TABLES))
                gone = sorted(set(REQUIRED_TABLES) - set(declared))
                if extra or gone:
                    findings.append(
                        f"MANIFEST MISMATCH: {dialect}/dialect.json declares what this tool does not "
                        f"know: only in the manifest {extra or '[]'}, only in this tool {gone or '[]'}")
        for table in REQUIRED_TABLES:
            gen = generated / table
            frozen = dialects_root / dialect / table
            if not gen.is_file():
                findings.append(f"note: {dialect}/{table} is not generated (frozen copy exists: "
                                f"{frozen.is_file()})")
                continue
            if not frozen.is_file():
                findings.append(f"MISSING FROZEN: this repository has no {dialect}/{table} although the "
                                f"generated tree produces it")
                continue
            a, b = row_count(gen), row_count(frozen)
            verdict = "same" if a == b else f"DRIFT ({b} frozen vs {a} generated, {b - a:+d})"
            rows.append((f"{dialect}/{table}", a, b, verdict))
    return findings, rows


def report(findings, rows):
    print(f"{'table':<34} {'generated':>10} {'frozen':>8}  verdict")
    print("-" * 78)
    for name, a, b, verdict in rows:
        print(f"{name:<34} {a:>10} {b:>8}  {verdict}")
    print("-" * 78)
    drift = [r for r in rows if not r[3] == "same"]
    print(f"{len(rows) - len(drift)}/{len(rows)} table(s) identical in row count, {len(drift)} drifted")
    if findings:
        print()
        for line in findings:
            print(f"  {line}")
    hard = [f for f in findings if f.startswith(("MISSING FROZEN", "MANIFEST MISMATCH"))]
    if drift or hard:
        print()
        print("This tool compares row counts only. A count difference is a signal to run the producer's")
        print("own byte-level check (pinyin-makedict tools/check_drift.ps1) and attribute it, not proof")
        print("of a content difference on its own.")
        return 1
    print("OK: every generated table has the same row count as the frozen copy this library ships")
    return 0


def self_test():
    """Fixtures only: a matching tree must pass, and both a row-count change and a missing table must
    be caught. A checker that cannot fail is decoration."""
    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        base = Path(tmp)
        gen, frozen = base / "gen", base / "frozen"
        for root in (gen, frozen):
            for dialect in ("mandarin", "cantonese"):
                (root / dialect).mkdir(parents=True)
                for table in REQUIRED_TABLES:
                    (root / dialect / table).write_text("a:1\nb:2\nc:3\n", encoding="utf-8")

        findings, rows = compare(gen, frozen)
        clean = not findings and all(v == "same" for *_, v in rows)
        print(f"self-test 1/4: matching trees accepted -> {'ok' if clean else 'FAILED'} "
              f"({len(rows)} rows compared)")
        ok = ok and clean

        (gen / "mandarin" / "word.txt").write_text("a:1\nb:2\nc:3\nd:4\n", encoding="utf-8")
        findings, rows = compare(gen, frozen)
        caught = any("DRIFT" in v for *_, v in rows)
        print(f"self-test 2/4: a row-count change caught -> {'ok' if caught else 'FAILED'}")
        ok = ok and caught
        (gen / "mandarin" / "word.txt").write_text("a:1\nb:2\nc:3\n", encoding="utf-8")

        (gen / "cantonese" / "trans_word.txt").unlink()
        findings, rows = compare(gen, frozen)
        caught = any("not generated" in f for f in findings)
        print(f"self-test 3/4: an absent generated table reported -> {'ok' if caught else 'FAILED'}")
        ok = ok and caught
        (gen / "cantonese" / "trans_word.txt").write_text("a:1\nb:2\nc:3\n", encoding="utf-8")

        # 4/4: REQUIRED_TABLES duplicates the dialect's own manifest on purpose, so a manifest that
        # gains an entry must be caught too -- otherwise this tool would quietly compare a set nobody
        # else believes in. Fixture: the frozen side declares one table more than this tool knows.
        (frozen / "cantonese" / "dialect.json").write_text(
            '{"id": "cantonese", "required_tables": ["word.txt", "phrases_dict.txt", '
            '"phrases_map.txt", "trans_word.txt", "extra.txt"]}\n', encoding="utf-8")
        findings, rows = compare(gen, frozen)
        caught = any(f.startswith("MANIFEST MISMATCH") for f in findings)
        print(f"self-test 4/4: a manifest disagreeing with this tool's list caught -> "
              f"{'ok' if caught else 'FAILED'}")
        ok = ok and caught

    print("SELF-TEST:", "ok" if ok else "FAILED")
    return 0 if ok else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("dict_root", nargs="?", help="pinyin-makedict's out/dict directory")
    parser.add_argument("--dialects", default=None,
                        help="this repository's data/dialects (default: next to this script)")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    if not args.dict_root:
        parser.error("give the generated dictionary root, or pass --self-test")
    dict_root = Path(args.dict_root)
    dialects_root = Path(args.dialects) if args.dialects else \
        Path(__file__).resolve().parent.parent / "data" / "dialects"

    if not dict_root.is_dir():
        print(f"ERROR: generated dictionary root not found: {dict_root}", file=sys.stderr)
        print("       (pinyin-makedict's out/dict is a generated artefact and is not committed; "
              "run its mandarin.py/cantonese.py first)", file=sys.stderr)
        return 2
    if not dialects_root.is_dir():
        print(f"ERROR: this repository's dialect data not found: {dialects_root}", file=sys.stderr)
        return 2

    findings, rows = compare(dict_root, dialects_root)
    return report(findings, rows)


if __name__ == "__main__":
    sys.exit(main())
