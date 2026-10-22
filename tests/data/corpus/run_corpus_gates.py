"""run_corpus_gates.py -- run the tracked corpus gates and decide pass/fail.

The gates in this directory are the ones that need nothing but files under version control
(tests/data/verified, data/dialects, tests/data/corpus/*baseline*.txt, tests/data/corpus/scan_overrides.jsonl); the pieces
of the review pipeline that live in the untracked .tmp/corpus working area are deliberately not part
of this: the engine-side numbers (pass counts, identity, kFrozen[]) are checked live by the C++
[corpus] test instead (tests/cpp/test_accuracy_v2.cpp).

Each gate prints one machine-checkable summary line, and this runner requires the line to say what a
healthy corpus says. That is stronger than trusting an exit status alone: a script that crashes
after printing a cheerful line, or one whose check silently stops running, is caught by the required
patterns; every gate is also required to exit 0.

Usage:
    python tests/data/corpus/run_corpus_gates.py              # all gates
    python tests/data/corpus/run_corpus_gates.py --gate ledger
    python tests/data/corpus/run_corpus_gates.py --self-test  # proves the validator can fail
Exit status 0 only when every gate ran, printed its expected markers and exited 0.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

# (name, script, required markers with a minimum count, patterns that must not appear)
GATES = [
    ("check_corpus", "tests/data/corpus/check_corpus.py", [("problems: 0", 1)], [r"problems: [1-9]"]),
    ("ledger", "tests/data/corpus/ledger_check.py", [("unexpected=0", 1)], [r"unexpected=[1-9]"]),
    ("consistency", "tests/data/corpus/consistency_scan.py", [("new: 0", 1)], [r"new: [1-9]"]),
    ("cross_set", "tests/data/corpus/cross_set.py", [("new: 0", 2)], [r"new: [1-9]"]),
    ("projection", "tests/data/corpus/cu_rowcheck.py",
     [("unmatched: 0", 1), ("disagreeing: 0", 1)], [r"unmatched: [1-9]", r"disagreeing: [1-9]"]),
]


def validate(name, spec, returncode, out):
    """Return a list of failure reasons (empty means the gate passed)."""
    required, forbidden = spec
    why = []
    if returncode != 0:
        why.append("%s: exit status %d" % (name, returncode))
    for pat, least in required:
        n = len(re.findall(re.escape(pat), out))
        if n < least:
            why.append("%s: expected %d x %r, found %d" % (name, least, pat, n))
    for pat in forbidden:
        m = re.search(pat, out)
        if m:
            why.append("%s: forbidden %r in %r" % (name, pat, m.group(0)))
    return why


def run_gate(name, script, spec):
    path = os.path.join(ROOT, script)
    if not os.path.exists(path):
        return ["%s: missing script %s" % (name, script)], ""
    env = dict(os.environ, PYTHONIOENCODING="utf-8")
    proc = subprocess.run([sys.executable, path], cwd=ROOT, env=env,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = proc.stdout.decode("utf-8", "replace")
    return validate(name, spec, proc.returncode, out), out


def self_test():
    """Prove the validator fails on bad output, so a green run means something."""
    ok_spec = ([("marker: 0", 1)], [r"marker: [1-9]"])
    cases = [("clean pass", ok_spec, 0, "marker: 0\n", 0),
             ("bad count", ok_spec, 0, "marker: 3\n", 1),
             ("bad exit", ok_spec, 1, "marker: 0\n", 1),
             ("silent gate", ok_spec, 0, "nothing to say\n", 1)]
    bad = []
    for label, spec, rc, out, want_failures in cases:
        got = len(validate("selftest", spec, rc, out))
        if (got > 0) != (want_failures > 0):
            bad.append("%s: expected %s, got %d failures" %
                       (label, "failure" if want_failures else "pass", got))
    print("self-test cases=%d unexpected=%d" % (len(cases), len(bad)))
    for b in bad:
        print("  ", b)
    return 1 if bad else 0


def main():
    if "--self-test" in sys.argv:
        return self_test()
    only = None
    if "--gate" in sys.argv:
        only = sys.argv[sys.argv.index("--gate") + 1]
    selected = [g for g in GATES if only in (None, g[0])]
    if not selected:
        print("unknown gate %r; known: %s" % (only, ", ".join(g[0] for g in GATES)))
        return 1
    failures = []
    for name, script, required, forbidden in selected:
        why, out = run_gate(name, script, (required, forbidden))
        summary = [ln.strip() for ln in out.splitlines()
                   if re.search(r"rows=|problems:|records=|conflicts:|spans|matched:|mode:", ln)]
        print("%-12s %s" % (name, "PASS" if not why else "FAIL"))
        for ln in summary:
            print("    " + ln)
        failures.extend(why)
    print()
    print("gates run=%d failed=%d" % (len(selected), len(failures)))
    for f in failures:
        print("  ", f)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
