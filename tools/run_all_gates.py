#!/usr/bin/env python3
"""Run every repository gate, print each verdict, and fail if any of them fails.

Why this exists: the gates are separate tools that take different arguments, so checking the tree meant
recalling each invocation -- and a gate nobody remembers to run is a gate that does not guard anything. This
runs them all in one command with one exit code, which is what a pre-commit check or a CI step can call.

What it does NOT do: build. Anything that needs a configured build tree (the ctest inventory, the canonical
build the doc-claims gate measures against) is reported as SKIPPED with the exact configure line needed, not as
a pass -- a run that quietly drops half its gates is the failure mode this tool exists to remove.

Usage:
    python tools/run_all_gates.py                 # the gates that need no build
    python tools/run_all_gates.py --with-build    # also the ones that need .tmp/mk-default (see BUILD_HINT)
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD_HINT = ("configure with: cmake -S . -B .tmp/mk-default -G Ninja "
              "-DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON "
              "-DCPP_PINYIN2_BUILD_TESTS=ON -DCPP_PINYIN2_INSTALL=ON -DCPP_PINYIN2_BUILD_EXAMPLES=OFF")

# (name, argv, needs_a_build_tree, what a failure means)
GATES = [
    ("check_layers", ["tools/check_layers.py"], False,
     "a lower layer includes an upper one, or a public name leaks from src"),
    ("check_bindings", ["tools/check_bindings.py"], False,
     "the frozen surfaces (headers / C symbols / plugin declarations) disagree with the tree"),
    ("check_links", ["tools/check_links.py"], False,
     "a relative markdown link does not resolve"),
    ("check_doc_refs", ["tools/check_doc_refs.py"], False,
     "a docs/<file>.md section reference does not resolve, a decision number is unregistered, "
     "or a retired path is cited without its pointer"),
    # Round R11 (2026-10-20): `conformance/cases.json` names the commit it was generated from, and that field
    # rotted twice with the whole suite green -- every other gate reads the fixture's CASES, none read the
    # provenance. `--require` is here for the reason check_format's entry spells out: outside a git checkout
    # the only honest verdict is SKIP, and a run that skips silently is a run that stops meaning anything.
    ("check_conformance_provenance", ["tools/check_conformance_provenance.py", "--require"], False,
     "conformance/cases.json's generated_by_commit is missing, malformed, or names a commit this repository "
     "does not contain -- regenerate it with CApiSmoke --emit-conformance"),
    ("check_public_surface", ["tools/check_public_surface.py"], False,
     "a public type is missing from the API pair, or the index names something that does not exist"),
    # 2026-10-22: check_links follows markdown LINKS and audit_doc_anchors resolves path:line; a bare
    # include/.../c_api.h in backticks inside a sentence is neither, so a renamed file could be cited for a
    # generation with every gate green. Measured before this entry existed: 663 path mentions, 28 dead.
    ("check_doc_paths", ["tools/check_doc_paths.py"], False,
     "a document names a repository path that does not exist"),
    ("audit_doc_anchors", ["tools/audit_doc_anchors.py", "--limit", "0", "--include-code"], False,
     "a doc anchor whose target is in this repository does not resolve"),
    ("check_doc_claims", ["tools/check_doc_claims.py", "--ctest-inventory",
                          ".tmp/mk-default-inventory.json"], True,
     "a document states a number the tree does not measure"),
    ("check_gate_inventory", ["tools/check_gate_inventory.py",
                              ".tmp/mk-default-inventory.json",
                              "--expect-total", "49", "--expect-slow", "4"], True,
     "a required gate is unregistered, unlabelled, or long without an excludable label"),
    ("check_comment_language", ["tools/check_comment_language.py"], False,
     "a comment line is Chinese-only, so a reader of the code cannot read it"),
    # Round 60: the five generation gates landed in tools/ (3501d42) but were registered nowhere, so nothing ran them -- the
    # orphan census caught exactly that, four of them as "tracked orphans". Wiring them in is the fourth part of landing:
    # a file in the tree, a passing self-test, a clean real-tree run, and SOMETHING THAT ACTUALLY RUNS IT.
    ("check_version_tokens", ["tools/check_version_tokens.py"], False,
     "a generation token disagrees with the package's major version, or a bare build name carries no token at all"),
    ("check_public_names", ["tools/check_public_names.py"], False,
     "the frozen list tests/abi/expected_public_names.vN.txt and the headers disagree, in either direction"),
    ("check_root_names", ["tools/check_root_names.py"], False,
     "a root-style reference (cpp_pinyin::X) appears where only cpp_pinyin::vN::X is allowed"),
    ("check_source_list", ["tools/check_source_list.py"], False,
     "a source file is in the tree but not in CMakeLists.txt's list (or the other way round), so the build "
     "and the tree disagree about what the library consists of"),
    # --allow-single-generation belongs to THIS entry (it used to sit above check_source_list, which takes no
    # such flag): this repository ships one generation (2.x), and the explicit flag is how that state is
    # recorded instead of being indistinguishable from "the scan found nothing".
    ("check_generation_compat", ["tools/check_generation_compat.py", "--allow-single-generation"], False,
     "the two-generation window rule is broken -- a generation claims compatibility with more than the one below it"),
    ("check_doc_code_tokens", ["tools/check_doc_code_tokens.py"], False,
     "a code block teaches a build name with no generation digit, so a reader copies a name that does not exist"),
    # Round R3 (2026-10-18): `.clang-format` had existed from the start with NOTHING running it, which is
    # why the tree had drifted from its own config (69 of 79 files, 40 lines over the configured 120
    # columns). Registering it here is the whole point of the batch: a config nobody runs is a claim.
    ("check_format", ["tools/check_format.py", "--require"], False,
     "a first-party C/C++ file no longer matches .clang-format, or clang-format is missing "
     "(--require turns the skip into a failure: a gate that prints SKIP and exits 0 has not run)"),
    # Round R5 (2026-10-18): the Python shell's suite existed and passed, but NOTHING ran it, so it rotted
    # in silence -- its contract test still matched the 1.x prefix `cppp_` against a 2.x header and only
    # failed because the other side of the comparison was non-empty. It needs the staged native payload
    # (python/tools/build_native.py), which is why it is marked as needing a build; without pytest or the
    # payload it reports SKIP rather than pretending to have checked anything.
    ("check_python_shell", ["tools/check_python_shell.py"], True,
     "the Python shell's own test suite failed -- run python -m pytest python/tests for the detail"),
    # Round 21 (2026-10-21): the C# suite had the same history as the Python one and worse consequences -- six
    # conformance tests were red from 2026-10-20 to 2026-10-21 because nothing ran them, and the hand-run that found
    # them was itself invalid (five-day-old native library). Registered WITHOUT `--require`: on a machine with no
    # .NET SDK the honest verdict is SKIP, and the gate prints which piece is missing. A STALE library is refused
    # rather than used -- that is the whole lesson of the run that found the six.
    ("check_csharp_shell", ["tools/check_csharp_shell.py"], True,
     "the C# shell's own test suite failed -- run python csharp/tools/run_tests.py --native-dir <built bin>"),
]


def self_tests_pass() -> tuple[bool, str]:
    """Each gate's own `--self-test` must pass before its verdict about the tree means anything.

    TEMP and TMP are removed from the child environment on purpose. The gate suite exports TEMP inside the
    repository's `.tmp/`, and a self-test that builds its fixtures with `tempfile` then builds them under a
    directory the tools SKIP as a build artefact -- so the fixtures become invisible and a case asserting
    "this must fail" passes because there was nothing to fail on. Measured 2026-10-10: that made
    the archive-reference gate reported 4/4 by hand and 2 failures under this runner, with TEMP the only
    difference (that gate retired on 2026-10-23 with the archive index it policed).
    """
    problems = []
    environment = {key: value for key, value in os.environ.items() if key not in ("TEMP", "TMP")}
    for name, argv, _needs_build, _why in GATES:
        # THE REGISTRY'S ARGUMENTS MUST SURVIVE (2026-10-20, audit round 2 §12.6 C3). This used to build
        # `argv[:1] + ["--self-test"]`, which dropped every flag the entry declares -- most importantly
        # `check_format`'s `--require`, so on a machine without clang-format the format gate's self-test
        # printed SKIP, exited 0, and this loop reported "all passed". The flags go before `--self-test`
        # because each gate parses them positionally-independently but the trailing flag is what switches
        # the mode; a gate that does not know a flag would have failed loudly here, which is the point.
        completed = subprocess.run([sys.executable, "-B"] + argv + ["--self-test"],
                                   cwd=ROOT, capture_output=True, text=True, env=environment)
        if completed.returncode != 0:
            problems.append(name)
    return (not problems, ", ".join(problems))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--with-build", action="store_true",
                        help="also run the gates that need a configured build tree")
    args = parser.parse_args()

    ok, broken = self_tests_pass()
    print(f"self-tests: {'all passed' if ok else 'FAILED for: ' + broken}")
    if not ok:
        print("  a gate whose own self-test fails cannot vouch for the tree; fix that first")
        return 1
    print()

    failures = []
    skipped = []
    for name, argv, needs_build, why in GATES:
        if needs_build and not args.with_build:
            skipped.append(name)
            print(f"  SKIP  {name} (needs a build tree; pass --with-build)")
            continue
        completed = subprocess.run([sys.executable, "-B"] + argv, cwd=ROOT, capture_output=True, text=True)
        output = (completed.stdout + completed.stderr).strip().splitlines()

        def verdict_of(lines: list[str]) -> str:
            """The line that states the outcome.

            Not simply the last line: `audit_doc_anchors` ends with a note about anchors it could not
            adjudicate, so reading the tail reported half a sentence instead of its result -- which is exactly
            the kind of quiet wrong answer this runner is supposed to remove.
            """
            for line in reversed(lines):
                stripped = line.strip()
                if stripped.startswith("RESULT:") or "adjudicated in this repository" in stripped \
                        or stripped.startswith(("PASS", "AC-3 OK")):
                    return stripped
            return lines[-1].strip() if lines else "(no output)"

        verdict = verdict_of(output)[:96]
        status = "ok  " if completed.returncode == 0 else "FAIL"
        print(f"  {status}  {name:<22} {verdict}")
        if completed.returncode != 0:
            failures.append((name, why))
            for line in output[:-1][-4:]:
                print(f"          {line[:96]}")

    print()
    if skipped:
        print(f"skipped: {', '.join(skipped)}")
        print(f"  {BUILD_HINT}")
    if failures:
        for name, why in failures:
            print(f"RESULT: FAIL -- {name}: {why}")
        return 1
    if skipped:
        print(f"RESULT: the {len(GATES) - len(skipped)} build-free gates pass; "
              f"{len(skipped)} were not run")
        return 0
    print(f"RESULT: all {len(GATES)} gates pass, and each gate's own self-test passes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
