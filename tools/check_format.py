#!/usr/bin/env python3
"""Format gate: every FIRST-PARTY C/C++ file matches `.clang-format`.

`.clang-format` had been in this repository since the beginning with nothing running it, so the tree
drifted from its own config. A config nobody runs is a claim, not a policy; this gate is what turns it
into one.

Scope. `git ls-files` for `.c`/`.cc`/`.cpp`/`.h`/`.hpp`, minus anything under `vendor/`: third-party code
is stored verbatim, and reformatting it would turn every future update into a merge with our changes hidden
inside it. `vendor/.clang-format` says `DisableFormat: true`, so editors and this gate agree about that
boundary.

Modes.
    (default)     verify; exit 1 listing the files that need formatting
    --fix         run `clang-format -i` on exactly the same set
    --self-test   prove the checker sees a bad file and accepts a clean one (writes only under .tmp)

Exit codes: 0 clean (or clang-format absent -- reported as SKIP, not as a pass), 1 drift found, 2 usage.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STYLE = ROOT / ".clang-format"
SOURCE_SUFFIXES = (".c", ".cc", ".cpp", ".h", ".hpp")
EXCLUDED_DIRS = ("vendor/",)


def tracked_sources() -> list[str]:
    """The files this gate owns, relative to the repository root, in git's order."""
    completed = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, capture_output=True, check=True)
    names = [name for name in completed.stdout.decode("utf-8").split("\0") if name]
    return [
        name
        for name in names
        if name.endswith(SOURCE_SUFFIXES) and not name.startswith(EXCLUDED_DIRS)
    ]


def clang_format() -> str | None:
    return shutil.which("clang-format") or shutil.which("clang-format.exe")


def offender(executable: str, names: list[str], style: str) -> list[str]:
    """The subset of `names` that `clang-format` would rewrite."""
    if not names:
        return []
    completed = subprocess.run(
        [executable, f"--style={style}", "--dry-run", "--Werror", *names],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    if completed.returncode == 0:
        return []
    # `--dry-run` names each file it would change (`<path>:<line>:<col>: warning: ...`). Compare by
    # resolved path so absolute arguments and repo-relative ones both match; fall back to the basename
    # for the case where clang-format printed a name we cannot resolve.
    reported: set[str] = set()
    for line in (completed.stdout + completed.stderr).splitlines():
        head = line.split(":", 1)[0].strip()
        if not head:
            continue
        reported.add(head)
        try:
            reported.add(str((ROOT / head).resolve()))
        except OSError:
            pass
    reported_names = {Path(item).name for item in reported}
    found = [
        name
        for name in names
        if name in reported or str((ROOT / name).resolve()) in reported or Path(name).name in reported_names
    ]
    # Anything reported but not attributable is still drift: say so rather than report clean.
    if not found:
        sys.stderr.write(completed.stdout + completed.stderr)
        return names
    return found


def self_test(require: bool = False) -> int:
    executable = clang_format()
    if executable is None:
        # --require must reach the self-test too (2026-10-20). The full gate runner
        # passes `--require`, and its self-test traversal used to drop it -- but even with the flag passed
        # the self-test ignored it and returned 0, so "no clang-format" still printed SKIP and looked like
        # a pass. With --require the absence is a failure here exactly as it is on the main path: a gate
        # that cannot run must not have a self-test that reports it ran.
        if require:
            print("self-test: FAIL -- clang-format is not installed and --require was given, so the "
                  "checker's own controls never ran")
            return 2
        print("self-test: SKIP (clang-format is not installed)")
        return 0
    # Absolute, deliberately: `--style=file:<path>` is resolved against the INPUT FILE's directory when
    # the path is relative (measured 2026-10-18 -- a probe under .tmp/ silently got LLVM's default style
    # and "failed" a file that is formatted exactly as this repository asks). An absolute path cannot
    # surprise anyone that way.
    style = f"file:{STYLE}"
    # The "clean" fixture is DERIVED from clang-format instead of written by hand: a hardcoded snippet is
    # only clean until the config changes (the first version of this test used `int main() {...}` and
    # LLVM's AllowShortFunctionsOnASingleLine collapsed it). Asking the tool what it wants makes the test
    # survive style evolution, which is the point of a self-test.
    seed = "int  main(){return 0;}\n"
    clean_text = subprocess.run(
        [executable, f"--style={style}"], input=seed, capture_output=True, text=True, check=True
    ).stdout
    # `.tmp/` is gitignored (`.gitignore:54`), so a clean checkout does not have it and
    # TemporaryDirectory(dir=...) raises FileNotFoundError before the self-test can prove anything. Create it.
    (ROOT / ".tmp").mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=ROOT / ".tmp") as scratch:
        space = Path(scratch)
        bad = space / "bad.cpp"
        good = space / "good.cpp"
        bad.write_text(seed, encoding="utf-8")
        good.write_text(clean_text, encoding="utf-8")
        cases = [("a badly formatted file", [str(bad)], 1), ("a clang-format-shaped file", [str(good)], 0)]
        failures = 0
        for label, names, want in cases:
            got = len(offender(executable, names, style))
            ok = got == want
            print(f"self-test: {label} -> {got} offender(s), want {want} {'ok' if ok else 'FAIL'}")
            failures += 0 if ok else 1
        return 1 if failures else 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="verify the tree matches .clang-format")
    parser.add_argument("--fix", action="store_true", help="format the files instead of reporting them")
    parser.add_argument("--self-test", action="store_true", help="prove this checker can see a violation")
    parser.add_argument("--require", action="store_true",
                        help="exit 2 instead of skipping when clang-format is absent")
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test(require=args.require)

    executable = clang_format()
    if executable is None:
        if args.require:
            # WHY THIS EXIT CODE EXISTS (2026-10-20 audit, G08). Without it a machine that has no LLVM
            # prints SKIP and exits 0, so "the gate passed" and "the gate never ran" look identical --
            # and on this repository that is exactly what happened: two public headers written in
            # 2026-10-19 were never formatted and nothing noticed until a machine WITH clang-format ran
            # the gate once. `--require` is the strict form: a caller that cannot tolerate a silent skip
            # (the full gate runner, CI) asks for it and gets a failure instead of a green line.
            print("check_format: FAIL -- clang-format is not on PATH and --require was given")
            print("check_format: install LLVM (or set CLANG_FORMAT) so the gate can actually read the tree")
            return 2
        # A SKIP, not a pass: the gate cannot speak about a tree it could not read.
        print("check_format: SKIP -- clang-format is not on PATH (install LLVM, or use the IDE's copy)")
        return 0

    names = tracked_sources()
    print(f"check_format: {len(names)} first-party source file(s) against {STYLE.name}")
    if args.fix:
        subprocess.run([executable, f"--style=file:{STYLE}", "-i", *names], cwd=ROOT, check=True)
        print("check_format: formatted in place")
        return 0

    drifted = offender(executable, names, f"file:{STYLE}")
    if not drifted:
        print("check_format: PASS")
        return 0
    print(f"check_format: FAIL ({len(drifted)} file(s) do not match .clang-format)")
    for name in drifted[:20]:
        print(f"  {name}")
    if len(drifted) > 20:
        print(f"  ... and {len(drifted) - 20} more")
    print("fix with: python tools/check_format.py --fix")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
