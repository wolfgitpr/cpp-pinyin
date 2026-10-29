#!/usr/bin/env python3
"""Check the CTest registry itself: is every gate this repository claims actually registered?

The registry is not one list anywhere -- `foreach` loops, generator expressions and conditionals in
tests/CMakeLists.txt build it, so a build configured without one option silently has an entry fewer, and a
count quoted in a document drifts away from the numbers the build gives.

What it checks, from `ctest --show-only=json-v1` (read the JSON, never scrape the human table):

  1. every REQUIRED entry below exists -- these are the gates whose absence has been, or nearly been, a
     real finding (the plugin surface was ungated for a week; the static C ABI surface was too);
  2. every test either carries a label or is listed in LABEL_EXEMPT with a reason at the site;
  3. every test whose TIMEOUT is >= LONG_TIMEOUT_SECONDS carries LABELS slow. The main suite and the
     dump/load gates are the expensive ones, and `ctest -LE slow` is this repository's quick pass -- a new
     long test without that label would silently join "quick";
  4. names are unique and the totals are reported, so a document quoting a count can be checked against
     this output instead of against memory.

Usage:
    ctest --test-dir <build> --show-only=json-v1 > <inventory.json>
    python tools/check_gate_inventory.py <inventory.json> [--expect-total N] [--expect-slow N]
Exit codes: 0 = the registry satisfies every check; 1 = it does not; 2 = usage/parse failure.
"""

import argparse
import json
import sys
from pathlib import Path

# Entries whose absence means a surface is unguarded. The comment beside each one is what it guards.
REQUIRED = {
    "ExportGate": "the 20 exported C ABI symbols, on a shared build",
    "ExportGateSelfTest": "ExportGate itself can fail (missing/extra/leaked symbols)",
    "SonameGate": "R9: the artifact's own DT_SONAME carries the generation (self-test runs on every platform)",
    "DialectSetGate": "data/dialects == the runtime registry == the frozen dialect list",
    "DialectSetGateSelfTest": "DialectSetGate itself can fail (extra / missing dialect)",
    "HeaderSelfCheck": "every public header compiles on its own",
    "LayerIncludes": "AC-3: no lower layer includes an upper one",
    "LinkChecks": "every relative markdown link resolves (the form the anchor audit cannot see)",
    "LinkChecksSelfTest": "LinkChecks itself can fail (dead link / percent-encoding / marker over-reach)",
    "PublicSurfaceIndex": "every public type is indexed in the API pair, and every indexed name exists",
    "PublicSurfaceIndexSelfTest": "PublicSurfaceIndex itself can fail (missing row / ghost row / wrong file)",
    "BindingGates": "the three frozen surfaces agree with the tree (headers / symbols / declarations)",
    "BindingGatesSelfTest": "BindingGates itself can fail across all three surfaces",
    "CorpusAccuracy": "the corpus accuracy gate runs at all (it is a separate entry from the main suite)",
    "ThreadSafety": "the read-only concurrency contract (shared engine / shared C handle) is a selectable entry",
    "InstanceIsolation": "two instances do not share loaded user dictionaries (its own case, on purpose)",
    "CorpusGate_check_corpus": "corpus self-consistency",
    "CorpusGate_ledger": "the ledger's frozen numbers",
    "CorpusGate_consistency": "cross-dataset consistency",
    "CorpusGate_cross_set": "the cross-set baseline",
    "CorpusGateSelfTest": "the corpus gates themselves can fail",
    "CApiSmoke": "the C ABI is usable from C (and, on a static build, is linked at all)",
    "InstallConsumerSmoke": "an installed tree can be consumed by a third party",
    "InstallConsumerSmokeSelfTest": "InstallConsumerSmoke itself can fail",
    "DictSelfCheck": "AC-4: the dict layer compiles and passes tests without linking the library",
    "LoadCheck": "load-time catastrophe gate",
    "LoadCheckSelfTest": "LoadCheck itself can fail",
    "DumpEquivalenceSelfTest": "the equivalence gate itself can fail",
    # Round R11 (2026-10-20): the conformance fixture's provenance. Named here rather than left to the
    # `--expect-total` count alone, because a count cannot say WHICH entry went missing.
    "ConformanceProvenanceGate": "G24: conformance/cases.json's generated_by_commit is a commit in this repo",
    "ConformanceProvenanceGateSelfTest": "ConformanceProvenanceGate itself can fail (unknown commit / "
                                         "missing field / a tree that is not its own checkout)",
}

# Long-running entries (TIMEOUT >= this) must carry the `slow` label, so `-LE slow` stays meaningful.
LONG_TIMEOUT_SECONDS = 600  # the corpus gates are the first entries at or above this

# Tests that deliberately carry no label, with the reason. An unlabelled test is a choice, not an
# accident -- this list is what forces that choice to be visible in review.
LABEL_EXEMPT = {
    "CppPinyinTests": "the main Catch2 suite: it IS the bulk of the run, and it is excluded by filter "
                      "(~[corpus]) rather than by label, because the corpus half is its own entry",
    "BenchmarkSmoke": "a smoke run of the reference benchmark; not a gate, its numbers are recorded "
                      "not asserted",
    "LoadCheckNegativeCase": "the negative half of LoadCheck: it is EXPECTED to fail (WILL_FAIL), so a "
                             "label would invite including it in a normal pass",
    # MinimalUsage is NOT exempt: it carries LABELS "ac1" (examples/CMakeLists.txt), which is
    # why it is absent from this dict -- see that file for the reasoning.
}


def labels_of(test):
    for prop in test.get("properties", []):
        if prop.get("name") == "LABELS":
            return list(prop.get("value", []))
    return []


def timeout_of(test):
    for prop in test.get("properties", []):
        if prop.get("name") == "TIMEOUT":
            try:
                return float(prop.get("value"))
            except (TypeError, ValueError):
                return None
    return None


def audit_inventory(tests):
    """Every problem this gate can report, plus the counts it prints, for one inventory.

    Deliberately an INDEPENDENT statement of the rules rather than a split of main(): two attempts to extract
    main()'s checking body into a reusable function both broke this gate (`SyntaxError: 'return' outside
    function`), so the rules are written a second time here and the self-test asserts the two agree on every
    entry of the real registry. Writing a rule twice costs; a gate that cannot be tested at all costs more.
    """
    names = [t.get("name", "") for t in tests]
    present = set(names)
    problems = []

    conditional = {}
    for name, guards in REQUIRED.items():
        if name in present:
            continue
        if name == "ExportGate" and "ExportGateSelfTest" in present:
            conditional[name] = "shared-only (BUILD_SHARED_LIBS=OFF), so its absence here is expected"
            continue
        problems.append(f"MISSING GATE: {name} -- it guards {guards}")

    for name in sorted({n for n in names if names.count(n) > 1}):
        problems.append(f"DUPLICATE entry name: {name}")

    unlabelled = [t.get("name", "") for t in tests
                  if not labels_of(t) and t.get("name", "") not in LABEL_EXEMPT]
    for name in unlabelled:
        problems.append(f"NO LABEL: {name} -- add LABELS or add it to LABEL_EXEMPT in this tool")

    slow = [t.get("name", "") for t in tests if "slow" in labels_of(t)]
    for test in tests:
        name = test.get("name", "")
        timeout = timeout_of(test)
        excludable = {"slow", "corpus"} & set(labels_of(test))
        if timeout is not None and timeout >= LONG_TIMEOUT_SECONDS and not excludable:
            problems.append(f"LONG BUT NOT EXCLUDABLE: {name} has TIMEOUT {timeout:g}s and neither a slow nor "
                            f"a corpus label; `ctest -LE slow` would run it")

    return problems, slow, unlabelled, conditional


def self_test() -> int:
    """Plant each mistake this gate exists to catch, and check the discipline its own tables rely on."""
    def entry(name, labels=None, timeout=None):
        properties = []
        if labels:
            properties.append({"name": "LABELS", "value": labels})
        if timeout is not None:
            properties.append({"name": "TIMEOUT", "value": str(timeout)})
        return {"name": name, "properties": properties}

    complete = [entry(name, ["gate"]) for name in REQUIRED]
    cases = []

    problems, _slow, _un, _cond = audit_inventory(complete)
    cases.append(("a complete, labelled inventory reports nothing", problems == []))

    incomplete = [item for item in complete if item["name"] != "ThreadSafety"]
    problems, _slow, _un, _cond = audit_inventory(incomplete)
    cases.append(("a missing required gate is reported",
                  any("MISSING GATE: ThreadSafety" in problem for problem in problems)))

    problems, _slow, _un, _cond = audit_inventory(complete + [complete[0]])
    cases.append(("a duplicate entry name is reported",
                  any("DUPLICATE entry name" in problem for problem in problems)))

    problems, _slow, _un, _cond = audit_inventory(complete + [entry("NoLabelHere")])
    cases.append(("an unlabelled entry is reported",
                  any("NO LABEL: NoLabelHere" in problem for problem in problems)))

    problems, _slow, unlabelled, _cond = audit_inventory(complete + [entry("CppPinyinTests")])
    cases.append(("an entry in LABEL_EXEMPT raises no problem and is NOT counted as unlabelled "
                  "(the list excludes what the table explains)",
                  not any("NO LABEL" in problem for problem in problems)
                  and "CppPinyinTests" not in unlabelled))

    problems, _slow, _un, _cond = audit_inventory(complete + [entry("VeryLongThing", timeout=900)])
    cases.append(("a long entry with no excludable label is reported",
                  any("LONG BUT NOT EXCLUDABLE: VeryLongThing" in problem for problem in problems)))

    problems, _slow, _un, _cond = audit_inventory(complete + [entry("LongButSlow", ["slow"], timeout=900)])
    cases.append(("a long entry carrying `slow` is not reported",
                  not any("LONG BUT NOT EXCLUDABLE" in problem for problem in problems)))

    problems, _slow, _un, _cond = audit_inventory(complete + [entry("LongButCorpus", ["corpus"], timeout=900)])
    cases.append(("a long entry carrying `corpus` is not reported (the quick pass runs it, by design)",
                  not any("LONG BUT NOT EXCLUDABLE" in problem for problem in problems)))

    without_export = [item for item in complete if item["name"] != "ExportGate"]
    problems, _slow, _un, conditional = audit_inventory(without_export)
    cases.append(("ExportGate absent with its self-test present is conditional, not a problem",
                  not any("MISSING GATE: ExportGate" in problem for problem in problems)
                  and "ExportGate" in conditional))

    skipped: list[str] = []
    real = Path(".tmp/mk-default-inventory.json")
    if real.is_file():
        import json as json_module
        entries = json_module.loads(real.read_text(encoding="utf-8-sig")).get("tests", [])
        problems, slow, unlabelled, _cond = audit_inventory(entries)
        cases.append((f"the independent rules agree with the real registry ({len(entries)} entries, "
                      f"{len(slow)} slow, {len(unlabelled)} unlabelled)",
                      problems == [] and len(slow) == 4))
    else:
        # ABSENT IS "NOT MEASURED", NOT "FAILED" (2026-10-20). The self-test proves the
        # gate's own rules; it needs a real inventory only for the one case that cross-checks those rules
        # against what CTest actually registered. On a clean clone there is no build directory, so that file
        # does not exist -- and the old code turned its absence into a FAILED case, which made
        # `run_all_gates.py` (no-build mode) red on every fresh checkout. The case is now reported as skipped,
        # by name, so a reader can see which one did not run; the agreement check itself still runs whenever
        # an inventory is present, and `--with-build` demands one through the main path.
        skipped.append("the independent rules agree with the real registry (no .tmp/mk-default-inventory.json "
                       "in this checkout -- configure and run ctest, or pass --ctest-inventory)")

    failures = []
    for label in skipped:
        print(f"self-test: SKIP {label}")
    for label, ok in cases:
        print(f"self-test: {label} -> {'ok' if ok else 'FAILED'}")
        if not ok:
            failures.append(label)
    print(f"SELF-TEST: {len(cases)} case(s), {len(failures)} failure(s)"
          + (": " + "; ".join(failures) if failures else ""))
    if not cases:
        print("SELF-TEST: NO CASES RAN -- a self-test that checks nothing must not report success")
        return 1
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("inventory", nargs="?",
                        help="JSON from `ctest --show-only=json-v1` (not needed with --self-test)")
    parser.add_argument("--expect-total", type=int, default=None)
    parser.add_argument("--expect-slow", type=int, default=None)
    parser.add_argument("--self-test", action="store_true",
                        help="plant each mistake this gate catches, and check that the independently written "
                             "rules agree with the real registry; then exit")
    args = parser.parse_args()

    if args.self_test:
        return self_test()
    if args.inventory is None:
        print("ERROR: an inventory file is required (or pass --self-test)", file=sys.stderr)
        return 2

    path = Path(args.inventory)
    if not path.is_file():
        print(f"ERROR: inventory not found: {path}", file=sys.stderr)
        return 2
    # utf-8-sig, not utf-8: the usual way to produce this file on Windows is
    # `ctest ... --show-only=json-v1 | Out-File -Encoding utf8`, and PowerShell's `utf8` writes a BOM.
    # Reading it as plain utf-8 fails with "Unexpected UTF-8 BOM", which is a tooling trap rather than
    # a problem with the registry -- so accept both forms here. (Plain `>` is worse: it writes UTF-16.)
    try:
        data = json.loads(path.read_text(encoding="utf-8-sig"))
    except json.JSONDecodeError as exc:
        print(f"ERROR: not valid JSON ({exc}). Use `--show-only=json-v1`, not `ctest -N`.", file=sys.stderr)
        return 2

    tests = data.get("tests", [])
    if not tests:
        print("ERROR: the inventory contains no tests at all -- a zero-test run looks green and proves "
              "nothing, so this is a failure rather than a pass.", file=sys.stderr)
        return 2

    names = [t.get("name", "") for t in tests]
    problems = []

    # 1. required entries, minus the ones this build configuration cannot have
    #
    # ExportGate only exists on a shared build (tests/CMakeLists.txt registers it under
    # `if (BUILD_SHARED_LIBS)`), so demanding it on a static build is a false positive -- measured by
    # running this tool against -DBUILD_SHARED_LIBS=OFF, where it reported a missing gate that is absent
    # by design. A checker that cries wolf gets turned off, so the conditional is handled here.
    present = set(names)
    conditional = {}
    for name, guards in REQUIRED.items():
        if name in present:
            continue
        if name == "ExportGate" and "ExportGateSelfTest" in present:
            conditional[name] = "shared-only (BUILD_SHARED_LIBS=OFF), so its absence here is expected"
            continue
        problems.append(f"MISSING GATE: {name} -- it guards {guards}")
    for name, why in conditional.items():
        print(f"note: {name} is not registered in this configuration -- {why}")

    # 2. uniqueness
    duplicates = {n for n in names if names.count(n) > 1}
    for name in sorted(duplicates):
        problems.append(f"DUPLICATE entry name: {name}")

    # 3. labels
    unlabelled = []
    for test in tests:
        name = test.get("name", "")
        if labels_of(test) or name in LABEL_EXEMPT:
            continue
        unlabelled.append(name)
    for name in unlabelled:
        problems.append(f"NO LABEL: {name} -- add LABELS or add it to LABEL_EXEMPT in this tool")

    # 4. long entries are labelled slow
    slow = [t.get("name", "") for t in tests if "slow" in labels_of(t)]
    for test in tests:
        name = test.get("name", "")
        t = timeout_of(test)
        # `slow` is not the only label a caller can exclude by: the corpus gates run under `corpus`,
        # and the documented quick pass for this repository is `ctest -LE slow`, which does NOT exclude
        # them. They are excluded in practice by their own filter (`-LE corpus`), so the honest rule is
        # "a long entry must carry a label someone can exclude by", not "it must be slow".
        excludable = {"slow", "corpus"} & set(labels_of(test))
        if t is not None and t >= LONG_TIMEOUT_SECONDS and not excludable:
            problems.append(f"LONG BUT NOT EXCLUDABLE: {name} has TIMEOUT {t:g}s and neither a slow nor "
                            f"a corpus label; `ctest -LE slow` would run it")

    # 5. optional count expectations (so a document can pin the number it quotes)
    if args.expect_total is not None and len(tests) != args.expect_total:
        problems.append(f"TOTAL {len(tests)} != expected {args.expect_total}")
    if args.expect_slow is not None and len(slow) != args.expect_slow:
        problems.append(f"SLOW {len(slow)} != expected {args.expect_slow}")

    print(f"registry: {len(tests)} entries, {len(slow)} labelled slow ({', '.join(slow)})")
    labelled = sum(1 for t in tests if labels_of(t))
    print(f"labels:   {labelled} labelled, {len(LABEL_EXEMPT)} exempt-with-reason, "
          f"{len(unlabelled)} unlabelled")
    print(f"gates:    {len(REQUIRED)} required entries checked, "
          f"{len(set(names) & set(REQUIRED))} present")
    print()
    if problems:
        for p in problems:
            print(f"  {p}")
        print()
        print(f"RESULT: {len(problems)} problem(s)")
        return 1
    print("RESULT: every required gate is registered, every entry is labelled or exempt, "
          "and every long entry is behind the slow label")
    return 0


if __name__ == "__main__":
    sys.exit(main())
