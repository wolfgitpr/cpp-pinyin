#!/usr/bin/env python3
"""Does `conformance/cases.json` name a commit THIS repository actually contains?

WHY THIS EXISTS. The fixture's `generated_by_commit` field is the only thing that ties the emitted
conformance cases to a state of this tree, and it rotted twice without a single gate noticing:

  * the field said `03b28db`, which `git cat-file -e` cannot resolve **in this repository at all** -- not a
    short hash whose object is missing, but a value from somewhere else entirely. A consumer that wants "what
    did the fixture look like when it was generated" has nothing to check out.
  * it was repaired once by hand to `e33f6f6` (round R9), and the repair was again witnessed by a human
    running `git cat-file -t`, not by anything the suite runs. The next regeneration can rot it again and
    every other gate stays green, because they all read the fixture's CASES and none read its PROVENANCE.

What this checks (one rule, plus the two ways it can refuse to speak):

  1. `conformance/cases.json` must exist, and its `generated_by_commit` must be a 7..40-character lowercase
     hex string -- the shape `git rev-parse --short` and the emitter's own `[<commit>]` argument produce.
     A missing file, a missing field, a non-string or a malformed value is a FAILURE, not a skip: those are
     readable from the tree itself and never depend on git.
  2. `git -C <root> cat-file -e <value>^{commit}` must succeed, i.e. the value must name a commit object --
     `^{commit}` and not a bare `-e` on purpose: a blob or a tree with the same prefix is not provenance.
  3. When the tree is not a checkout of ITSELF, the provenance question cannot be answered and this tool
     says SKIP rather than PASS. "Itself" is load-bearing: `git rev-parse --git-dir` alone also succeeds in
     a directory that merely SITS INSIDE somebody else's work tree (this repository's own `.tmp/` is such a
     directory), so the rule compares `rev-parse --show-toplevel` with `--root`. That distinction is what
     makes `--self-test`'s non-git control non-vacuous -- see the note in `self_test()`.
     With `--require` (the form `tools/run_all_gates.py` uses) a skip becomes exit 2, the same convention as
     `tools/check_format.py`: a caller that cannot tolerate a silent skip asks for the strict form, because
     a gate that prints SKIP and exits 0 has not run. Exit codes: 0 pass (or skip), 1 finding, 2 required
     but unanswerable.

WHAT IT DOES NOT DO. It does not check that the fixture's CASES match that commit -- that would need a
second build of an older tree, and R9 already showed the cheap half (regenerate into `.tmp/` and diff line by
line) is the human step. It does not verify `cases.sha256`, which `python/tests/test_conformance.py` owns.
And it requires the named commit to be REACHABLE from some ref, not merely present: `cat-file -e` answers
"does this repository contain the object", which is TRUE for an object left behind by a history rewrite and FALSE
for every fresh clone of the same branch -- measured 2026-10-21, when the fixture named a pre-squash commit that no
clone contains and the gate passed here while CI would have failed. Reachability is the property that makes the
provenance checkable by the person who clones this repository.

USAGE
    python tools/check_conformance_provenance.py               # exit 1 on a finding
    python tools/check_conformance_provenance.py --require     # exit 2 instead of skipping outside git
    python tools/check_conformance_provenance.py --self-test    # four sandbox controls, then exit
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import List, Optional, Tuple

ROOT = Path(__file__).resolve().parent.parent

#: The fixture and the field this gate owns. Both are spelled once: the failure message, the reader's
#: repair command and the self-test fixtures all come from these two names.
FIXTURE = "conformance/cases.json"
FIELD = "generated_by_commit"

#: An abbreviated-or-full lowercase hex commit id. The window is 7..40 because `--emit-conformance` writes
#: whatever it is handed; 7 is git's own default abbreviation floor and 40 is SHA-1's width. Uppercase or a
#: 4-character value is a typo, and a typo must be reported as a malformed FIELD rather than as an unknown
#: object -- the two need different repairs.
COMMIT_RX = re.compile(r"^[0-9a-f]{7,40}$")

#: The one command that puts the field back into a state this gate can pass. Printed with every finding: a
#: gate that only says "wrong" leaves the reader to invent a generator, and an invented one writes a fixture
#: of the wrong shape (this repository has had exactly that happen to it).
REGENERATE = "CApiSmoke <build>/bin/dict --emit-conformance conformance/cases.json <commit>"

PREFIX = "check_conformance_provenance"


def git(root: Path, *arguments: str) -> Optional[subprocess.CompletedProcess]:
    """Run one git command against `root`, or None when git itself is missing.

    `git -C <root>` rather than `cwd=root` so that the command line printed on failure is the one a reader
    can paste. Text mode with `errors="replace"`: a repository path with undecodable bytes must produce a
    finding, not a UnicodeDecodeError traceback.
    """
    try:
        return subprocess.run(["git", "-C", str(root), *arguments], capture_output=True, text=True,
                              encoding="utf-8", errors="replace")
    except OSError:
        return None


def checkout_root(root: Path) -> Tuple[Optional[Path], str]:
    """(toplevel, why-not) for `root`: is this tree its own git checkout?

    Two questions, deliberately in this order:

      * `rev-parse --git-dir` -- is there git at all? (Fails outside a repository, and when git is absent.)
      * `rev-parse --show-toplevel` -- is the checkout THIS directory's, or an ancestor's? A directory that
        merely sits inside another work tree answers the first question yes and this one with a different
        path, and treating that as "a checkout of this tree" would let the gate report a PASS (or a FAIL!)
        about a repository the caller never named.
    """
    probe = git(root, "rev-parse", "--git-dir")
    if probe is None:
        return None, "git is not on PATH, so commits cannot be resolved here"
    if probe.returncode != 0:
        return None, f"`git -C {root} rev-parse --git-dir` fails, so this tree is not a git checkout"
    probe = git(root, "rev-parse", "--show-toplevel")
    if probe is None or probe.returncode != 0:
        # A bare repository has no work tree, and the fixture cannot be walked from one anyway.
        return None, (f"`git -C {root} rev-parse --show-toplevel` fails, so this checkout has no work tree "
                      f"of its own")
    toplevel = Path(probe.stdout.strip())
    try:
        same = toplevel.resolve() == Path(root).resolve()
    except OSError:
        same = False
    if not same:
        return None, (f"the nearest checkout is {toplevel}, not {root} -- this tree is not a repository of "
                      f"its own, so the commits it names cannot be looked up here")
    return toplevel, ""


def named_commit(root: Path) -> Tuple[Optional[str], Optional[str]]:
    """(value, problem) for the fixture's `generated_by_commit`, read from the tree alone.

    Every branch here is a FAILURE rather than a skip, because none of them needs git: whether a tracked
    file exists and whether a field holds a commit-shaped string are questions the tree answers by itself.
    """
    path = root / FIXTURE
    if not path.is_file():
        return None, f"{FIXTURE} is missing"
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        return None, f"{FIXTURE} is not readable JSON ({exc})"
    if not isinstance(document, dict):
        return None, f"{FIXTURE} does not hold a JSON object"
    value = document.get(FIELD)
    if value is None:
        return None, f"{FIXTURE} carries no `{FIELD}` field, so nothing ties the cases to a tree"
    if not isinstance(value, str):
        # `null` or a number reads like a field that exists; say which, so the repair is the obvious one.
        return None, f"{FIXTURE}'s `{FIELD}` is a {type(value).__name__}, not a string"
    if not COMMIT_RX.match(value):
        return None, (f"{FIXTURE}'s `{FIELD}` is {value!r}, which is not a 7..40-character lowercase hex "
                      f"commit")
    return value, None


def verdict(root: Path, require: bool) -> Tuple[int, List[str]]:
    """(exit code, lines) for one tree -- the ONE copy of the rule, used by main() and by `--self-test`.

    A self-test that re-implements the check proves the wrong thing (the lesson `tools/check_doc_claims.py`
    wrote down after its first coverage table passed on a fixture its scan could not read).
    """
    commit, problem = named_commit(root)
    if problem is not None:
        return 1, [f"{PREFIX}: {problem}",
                   f"{PREFIX}: regenerate the fixture with: {REGENERATE}",
                   f"{PREFIX}: FAIL -- the fixture does not name a commit this gate can resolve"]

    toplevel, why_not = checkout_root(root)
    if toplevel is None:
        if require:
            return 2, [f"{PREFIX}: {why_not}",
                       f"{PREFIX}: FAIL -- --require was given, and an unanswerable provenance question "
                       f"is not a pass",
                       f"{PREFIX}: run this gate inside the repository's own git checkout, or drop "
                       f"--require to accept the skip"]
        # A SKIP, not a pass: the gate cannot speak about commits in a tree it cannot look commits up in.
        return 0, [f"{PREFIX}: SKIP -- {why_not} (SKIP, not a pass: nothing was verified)"]

    probe = git(root, "cat-file", "-e", f"{commit}^{{commit}}")
    if probe is None or probe.returncode != 0:
        # The `^{commit}` peel is not decoration: `cat-file -e <hash>` is happy with a blob or a tree whose
        # id shares the prefix, and neither of those is a place the cases were generated from.
        return 1, [f"{PREFIX}: {FIXTURE} says it was generated by {commit}, which {toplevel} does not "
                   f"contain as a commit",
                   f"{PREFIX}: regenerate the fixture with: {REGENERATE}",
                   f"{PREFIX}: FAIL -- the provenance names a commit this repository does not have"]
    # Reachable from SOME ref, not merely present. `rev-list <commit> --not --all` lists the commits reachable from
    # it that no ref reaches: empty means a branch or tag can walk to it, which is what makes the provenance
    # checkable in a fresh clone. An object orphaned by a history rewrite is present here and nowhere else.
    unreachable = git(root, "rev-list", commit, "--not", "--all")
    if unreachable is not None and unreachable.returncode == 0 and unreachable.stdout.strip():
        return 1, [f"{PREFIX}: {FIXTURE} says it was generated by {commit}, which exists here but no branch or tag "
                   f"reaches it (a history rewrite left it behind)",
                   f"{PREFIX}: a fresh clone and CI do not have that object, so the provenance cannot be checked "
                   f"by anyone else",
                   f"{PREFIX}: regenerate the fixture at a reachable commit: {REGENERATE}",
                   f"{PREFIX}: FAIL -- the provenance names a commit no clone of this branch contains"]
    return 0, [f"{PREFIX}: PASS -- `{FIELD}` {commit} is a commit in this repository, reachable from a ref"]


def self_test() -> int:
    """Six controls, one sandbox, all of them driving `verdict()` rather than a copy of it.

      (a) a fixture naming the sandbox's own commit                     -> PASS (exit 0)
      (b) a fixture naming a hash no repository contains                -> finding (exit 1)
      (c) a fixture with the FIELD removed                              -> finding (exit 1)
      (d) a directory that is not a checkout of itself                  -> SKIP, and exit 2 with --require

    WHY THE SANDBOX LIVES UNDER `.tmp/`: that directory is git-ignored, so the sandbox never shows up as an
    untracked tree in `git status`, and none of the tree-scanning gates (check_doc_refs, audit_doc_anchors,
    check_format) ever reads it. `.tmp/` is also INSIDE this repository's work tree, which is exactly why
    control (d) is worth having: the same property that keeps the sandbox invisible to other gates makes
    `rev-parse --git-dir` succeed in it, so a tool that stopped at that probe would "pass" control (d)
    while proving nothing. The rule under test compares `--show-toplevel` with the root for that reason.
    """
    scratch = ROOT / ".tmp"
    scratch.mkdir(parents=True, exist_ok=True)
    cases: List[Tuple[str, bool]] = []
    # ignore_cleanup_errors: git marks loose objects read-only, and Windows refuses to delete those without a
    # chmod a TemporaryDirectory does not do. A self-test that fails on its own housekeeping is a false red.
    # `ignore_cleanup_errors` is 3.10+; Ubuntu 20.04's python 3.8 raised TypeError here and the self-test never
    # ran (measured on the firefly board, 2026-10-21). The flag only stops Windows from failing cleanup on the
    # read-only loose objects git leaves behind, so on a Python without it the sandbox is simply opened without it.
    try:
        _sandbox = tempfile.TemporaryDirectory(dir=scratch, prefix="provenance-", ignore_cleanup_errors=True)
    except TypeError:
        _sandbox = tempfile.TemporaryDirectory(dir=scratch, prefix="provenance-")
    with _sandbox as directory:
        space = Path(directory)

        def run(*arguments: str, cwd: Path) -> str:
            """One git command, loudly: a sandbox this test cannot build must not look like a passing case."""
            completed = subprocess.run(["git", "-C", str(cwd), *arguments], capture_output=True, text=True)
            if completed.returncode != 0:
                raise SystemExit(f"{PREFIX} --self-test: cannot build the sandbox: `git {' '.join(arguments)}`"
                                 f" exited {completed.returncode}: {completed.stdout}{completed.stderr}")
            return completed.stdout.strip()

        def fixture(root: Path, value: Optional[str]) -> Path:
            """Write `root`'s fixture with `value` under FIELD (`None` writes no field at all), return root.

            The fixture sits INSIDE the checkout for controls (a)..(c) on purpose: `--root` is both where the
            JSON is read from and which repository the hash is looked up in, so a fixture outside the sandbox
            repository would exercise the "not a checkout" branch instead of the branch under test.
            """
            payload = {"schema": 1, "cases_count": 0}
            if value is not None:
                payload[FIELD] = value
            (root / "conformance").mkdir(parents=True, exist_ok=True)
            (root / FIXTURE).write_text(json.dumps(payload, indent=2), encoding="utf-8")
            return root

        repository = space / "repo"
        repository.mkdir()
        # `git init` then one commit with an EXPLICIT identity and no gpg signing: the sandbox must not
        # depend on the machine's git config, and `--allow-empty` keeps it from needing a staged file (this
        # repository's own rules forbid `git add -A`, and a self-test has no business staging anything).
        run("init", "--quiet", cwd=repository)
        run("-c", "user.name=t", "-c", "user.email=t@t", "-c", "commit.gpgsign=false",
            "commit", "--allow-empty", "--quiet", "-m", "sandbox commit: the fixture's provenance", cwd=repository)
        head = run("rev-parse", "HEAD", cwd=repository)
        short = head[:7]

        # (e) an orphaned commit: made, then dropped by a reset, so the object exists and no ref reaches it.
        # The SAME identity flags as the first commit: without them a machine with no global git identity cannot
        # build the sandbox at all ("*** Please tell me who you are", measured on the firefly board 2026-10-21).
        run("-c", "user.name=t", "-c", "user.email=t@t", "-c", "commit.gpgsign=false",
            "commit", "--allow-empty", "--quiet", "-m", "sandbox commit: to be orphaned", cwd=repository)
        orphan = run("rev-parse", "HEAD", cwd=repository)
        run("reset", "--hard", "--quiet", "HEAD~1", cwd=repository)
        code_orphan, lines_orphan = verdict(fixture(repository, orphan[:7]), require=True)
        cases.append(("(e) a commit that exists but no ref reaches is refused",
                      code_orphan == 1 and any("no branch or tag" in line for line in lines_orphan)))
        cases.append(("(f) restoring the reachable fixture passes again",
                      verdict(fixture(repository, short), require=True)[0] == 0))

        code, lines = verdict(fixture(repository, short), require=True)
        cases.append((f"(a) a fixture naming the sandbox's own commit {short} passes",
                      code == 0 and any("PASS" in line for line in lines)))

        bogus = "f" * 40
        code, lines = verdict(fixture(repository, bogus), require=True)
        cases.append((f"(b) a fixture naming an unknown commit {bogus[:7]} is reported",
                      code == 1 and any(REGENERATE in line for line in lines)))

        code, lines = verdict(fixture(repository, None), require=True)
        cases.append(("(c) a fixture with no generated_by_commit field is reported",
                      code == 1 and any(FIELD in line for line in lines)))

        # The hash is well formed and, in a directory that is not its own checkout, unresolvable -- so a SKIP
        # here can only come from the checkout probe, which is what this control exists to prove.
        plain = fixture(space / "not-a-checkout", head)
        code_skip, lines_skip = verdict(plain, require=False)
        code_required, lines_required = verdict(plain, require=True)
        cases.append(("(d) a directory that is not its own checkout SKIPs, and fails with --require",
                      code_skip == 0 and any("SKIP" in line for line in lines_skip)
                      and code_required == 2))

    failures = 0
    for label, ok in cases:
        print(f"{'PASS' if ok else 'FAIL'} self-test: {label}")
        failures += 0 if ok else 1
    if failures or not cases:
        print(f"SELFTEST FAIL -- {len(cases)} case(s), {failures} failure(s)")
        return 1
    print(f"SELFTEST PASS -- {len(cases)} case(s)")
    return 0


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(
        description="require conformance/cases.json's generated_by_commit to be a commit in this repository")
    parser.add_argument("--root", default=str(ROOT), help="where the tree is read from")
    parser.add_argument("--require", action="store_true",
                        help="exit 2 instead of skipping when this tree is not its own git checkout")
    parser.add_argument("--self-test", action="store_true",
                        help="prove all four verdicts (pass / unknown commit / missing field / not a "
                             "checkout) in a sandbox, then exit")
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test()

    code, lines = verdict(Path(args.root).resolve(), args.require)
    for line in lines:
        print(line)
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
