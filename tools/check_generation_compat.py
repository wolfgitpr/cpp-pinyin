"""G8 `check_generation_compat`: the compatibility promise, made checkable.

R2/R3 promise that generation N ships the names of generation N-1 (a caller migrates by rewriting the token
once). The plan's runbook step 4 ASSUMED a gate checked that ("v3 must contain v2's 36 names, removals must
be recorded") -- no such gate existed. This is it.

How it judges
  * it reads only the FROZEN LISTS (`tests/abi/expected_public_names.vN.txt`), which G2 already pins to each
    generation's headers. That keeps G8 free of parsing, and it means "the promise" is measured against what
    the repository declares, not against a second interpretation of the headers.
  * for every pair of CONSECUTIVE generations present (2 and 3, 3 and 4, ...), every name of the older list
    must appear in the newer one.
  * a removal is allowed only if it is written down in `tests/abi/removed_names.vN.txt` (one name per line,
    optional `# reason`), where N is the generation that dropped it. That file is the machine-readable half
    of the migration guide.
  * a removal entry for a name the newer generation still has is STALE and also fails: otherwise the file
    would silently accumulate permission slips.

Self-test fixtures cover the baseline, a dropped name, a recorded drop, a stale record, non-consecutive
generations, and a missing list.

歧义形态：**源码面** ✗（承诺面）—— 逐对检查"这一代是否仍提供上一代的名字"，删名必须在 removed_names 里记录在案。
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

LIST_GLOB = "tests/abi/expected_public_names.v*.txt"


def list_path(root: Path, generation: int) -> Path:
    return root / "tests" / "abi" / f"expected_public_names.v{generation}.txt"


def removals_path(root: Path, generation: int) -> Path:
    return root / "tests" / "abi" / f"removed_names.v{generation}.txt"


def read_rows(path: Path) -> list[str]:
    rows: list[str] = []
    for line in path.read_text(encoding="utf-8-sig", errors="replace").splitlines():
        text = line.split("#", 1)[0].strip()
        if text:
            rows.append(text)
    return rows


def generations(root: Path) -> list[int]:
    found: list[int] = []
    for path in root.glob(LIST_GLOB):
        stem = path.name[len("expected_public_names.v"):-len(".txt")]
        if stem.isdigit():
            found.append(int(stem))
    return sorted(found)


def compare(root: Path, allow_single: bool = False) -> list[str]:
    findings: list[str] = []
    found = generations(root)
    if not found:
        # ZERO lists is not the same state as ONE list (2026-10-20, audit round 2 §12.7). The old code
        # printed "nothing to compare yet" for both and PASSed, so a wrong default root -- it used to be
        # `parents[2]`, which resolves to D:\projects on Windows, i.e. NOT this repository -- looked
        # exactly like a healthy single-generation repository.
        findings.append(f"no generation name list found under {root} -- the check measured NOTHING, which "
                        f"is not the same as 'the tree is consistent'. Pass --root <repository> if this is "
                        f"the wrong directory")
        return findings
    if len(found) < 2:
        print(f"note: {len(found)} generation list(s) present ({found[0]} only) -- NOT MEASURED: the "
              f"two-generation window rule cannot be evaluated until a second generation exists")
        if not allow_single:
            findings.append(f"only generation {found[0]} is present, so the window rule was NOT MEASURED -- "
                            f"pass --allow-single-generation to accept that state explicitly")
        return findings
    for older, newer in zip(found, found[1:]):
        if newer != older + 1:
            # The window rule (G1) forbids this; G8 states it too rather than comparing across the gap.
            findings.append(f"generations {older} and {newer} are not consecutive -- the compatibility "
                            f"promise is defined for consecutive generations only (see R2)")
            continue
        older_rows = read_rows(list_path(root, older))
        newer_rows = set(read_rows(list_path(root, newer)))
        recorded = set(read_rows(removals_path(root, newer))) if removals_path(root, newer).is_file() else set()
        missing = [name for name in older_rows if name not in newer_rows]
        unrecorded = [name for name in missing if name not in recorded]
        print(f"note: generation {older} -> {newer}: {len(older_rows)} name(s) in the older list, "
              f"{len(newer_rows)} in the newer, {len(missing)} no longer present, "
              f"{len(recorded)} recorded as removed")
        if unrecorded:
            findings.append(f"generation {newer} no longer provides {len(unrecorded)} name(s) that "
                            f"generation {older} has, and they are not recorded in "
                            f"tests/abi/removed_names.v{newer}.txt: " + ", ".join(unrecorded[:6])
                            + (" ..." if len(unrecorded) > 6 else ""))
        stale = sorted(name for name in recorded if name in newer_rows)
        if stale:
            findings.append(f"tests/abi/removed_names.v{newer}.txt records {len(stale)} name(s) that "
                            f"generation {newer} still provides: " + ", ".join(stale[:6])
                            + " -- a stale permission slip is worse than none")
    return findings


def write_generation(root: Path, generation: int, names: list[str], removed: list[str] | None = None) -> None:
    (root / "tests" / "abi").mkdir(parents=True, exist_ok=True)
    list_path(root, generation).write_text("# frozen v%d\n" % generation + "\n".join(names) + "\n",
                                           encoding="utf-8", newline="")
    if removed is not None:
        removals_path(root, generation).write_text("\n".join(removed) + "\n", encoding="utf-8", newline="")


def self_test(tmp: Path) -> int:
    base_names = ["Alpha", "Beta", "Gamma", "Delta"]

    def fresh(label: str) -> Path:
        tree = tmp / label
        if tree.exists():
            import shutil
            shutil.rmtree(tree, ignore_errors=True)
        return tree

    cases: list[tuple[str, Path, bool, str]] = []

    tree = fresh("c1-two-identical")
    write_generation(tree, 2, base_names)
    write_generation(tree, 3, base_names)
    cases.append(("two generations with identical names", tree, False, ""))

    tree = fresh("c2-dropped")
    write_generation(tree, 2, base_names)
    write_generation(tree, 3, base_names[:-1])
    cases.append(("a name dropped without a record", tree, True, "no longer provides"))

    tree = fresh("c3-recorded")
    write_generation(tree, 2, base_names)
    write_generation(tree, 3, base_names[:-1], removed=["Delta  # superseded by Gamma"])
    cases.append(("the same drop, recorded", tree, False, ""))

    tree = fresh("c4-stale-record")
    write_generation(tree, 2, base_names)
    write_generation(tree, 3, base_names, removed=["Beta"])
    cases.append(("a stale removal record", tree, True, "still provides"))

    tree = fresh("c5-gap")
    write_generation(tree, 2, base_names)
    write_generation(tree, 4, base_names)
    cases.append(("a gap between generations", tree, True, "not consecutive"))

    tree = fresh("c6-single")
    write_generation(tree, 2, base_names)
    # §12.7: one generation is NOT green any more -- the window rule was never applied, and a bare PASS made
    # that indistinguishable from a healthy two-generation tree. It is red unless the caller opts in.
    cases.append(("a single generation without the opt-in is red, not green", tree, True, "NOT MEASURED"))
    cases.append(("no generation list at all is red, not green", fresh("c6-empty"), True, "measured NOTHING"))

    failures = 0
    for label, tree, expect_red, needle in cases:
        findings = compare(tree)
        red = bool(findings)
        ok = red == expect_red and (not needle or any(needle in finding for finding in findings))
        want = "red" if expect_red else "green"
        got = "red" if red else "green"
        suffix = "" if ok else f" (expected {want}, got {got})"
        print(f"  {'PASS' if ok else 'FAIL'} self-test: {label} -> {got}{suffix}")
        failures += 0 if ok else 1
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description="generation-to-generation name compatibility (R2/R3)")
    # parents[1], not [2]: this file lives in tools/, so [0] is tools/ and [1] is the repository root.
    # [2] is the DIRECTORY HOLDING the repository (D:\projects here) -- measured 2026-10-20: the gate
    # then scanned a directory with no generation lists and reported a bare PASS (audit round 2 §12.7).
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--allow-single-generation", action="store_true",
                        help="accept a tree that ships one generation (the check then reports NOT MEASURED "
                             "instead of a bare PASS); without it the state is reported, not scored green")
    args = parser.parse_args()
    if args.self_test:
        import tempfile
        scratch = Path(tempfile.mkdtemp(prefix="g8-selftest-"))
        failures = self_test(scratch)
        # The opt-in has to actually change the verdict, or the flag would be decoration.
        single_tree = scratch / "c6-single"
        if single_tree.is_dir():
            opted_in = compare(single_tree, allow_single=True)
            ok = not opted_in
            print(f"  {'PASS' if ok else 'FAIL'} self-test: the opt-in turns the single-generation state "
                  f"green{'' if ok else ' -> ' + str(opted_in)}")
            failures += 0 if ok else 1
        print(f"\nSELFTEST {'FAIL' if failures else 'PASS'} -- 7 case(s) reported above")
        return 1 if failures else 0
    findings = compare(args.root, allow_single=args.allow_single_generation)
    for finding in findings:
        print("FAIL: " + finding)
    if findings:
        print(f"check_generation_compat: FAIL ({len(findings)} finding(s))")
        return 1
    if len(generations(args.root)) < 2:
        # A PASS here would be a lie of the kind this repository keeps paying for: the rule was never applied.
        print("check_generation_compat: NOT MEASURED (one generation in the tree; the window rule needs two)")
        return 0
    print("check_generation_compat: PASS (0 finding(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main())
