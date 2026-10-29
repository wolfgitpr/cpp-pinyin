#!/usr/bin/env python3
"""Run the Python shell's own test suite, and say SKIP when it cannot be run.

The suite existed and passed but NOTHING ran it, so it rotted in silence: one test still matched the 1.x
`cppp_` prefix against a 2.x header, so its "the shell declares exactly the C ABI" assertion compared the
empty set with the baseline and read as agreement. A pattern that parses nothing must never pass; the test
now asserts it parsed something, and this gate is what runs it.

What "cannot be run" means, and why it is a SKIP rather than a failure:
  * `pytest` is not installed -> SKIP (print how to install it);
  * the native payload is not staged under `python/cpp_pinyin2/_native/` -> SKIP (print the exact
    `python/tools/build_native.py` command). The payload is gitignored by design: it is a build product,
    and shipping one from a previous commit is how a test starts vouching for code nobody built.

Exit codes: 0 passed (or SKIP), 1 the suite failed, 2 usage.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PYTHON_PACKAGE = ROOT / "python"
PAYLOAD = PYTHON_PACKAGE / "cpp_pinyin2" / "_native" / "name.txt"


def say_skip(reason: str, hint: str) -> int:
    print(f"check_python_shell: SKIP -- {reason}")
    print(f"check_python_shell: {hint}")
    return 0


def run_suite(pytest_args: list[str], cwd: Path) -> tuple[int, str]:
    environment = dict(os.environ)
    environment["PYTHONPATH"] = str(PYTHON_PACKAGE)
    environment.pop("PYTEST_ADDOPTS", None)
    completed = subprocess.run(
        [sys.executable, "-m", "pytest", *pytest_args],
        cwd=cwd,
        capture_output=True,
        text=True,
        env=environment,
    )
    return completed.returncode, completed.stdout + completed.stderr


def self_test() -> int:
    """Prove both directions on a fixture: a passing test must pass, a failing one must fail."""
    with tempfile.TemporaryDirectory(dir=ROOT / ".tmp") as scratch:
        space = Path(scratch)
        (space / "test_ok.py").write_text("def test_ok():\n    assert 1 + 1 == 2\n", encoding="utf-8")
        (space / "test_bad.py").write_text("def test_bad():\n    assert 1 + 1 == 3\n", encoding="utf-8")
        passing, _ = run_suite(["-q", "test_ok.py"], space)
        failing, _ = run_suite(["-q", "test_bad.py"], space)
        failures = 0
        for label, got, want in (("a passing test", passing, 0), ("a failing test", failing, 1)):
            ok = (got == 0) if want == 0 else (got != 0)
            print(f"self-test: {label} -> exit {got}, want {'0' if want == 0 else 'non-zero'} "
                  f"{'ok' if ok else 'FAIL'}")
            failures += 0 if ok else 1
        return 1 if failures else 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="run the Python shell's test suite")
    parser.add_argument("--self-test", action="store_true", help="prove this runner can see a failure")
    args = parser.parse_args(argv)

    try:
        import pytest  # noqa: F401  (presence check only)
    except ImportError:
        return say_skip("pytest is not installed for this interpreter", "install it: pip install pytest")
    if shutil.which(sys.executable) is None:  # pragma: no cover - defensive
        return say_skip("no usable interpreter", "run this with the interpreter that owns pytest")

    if args.self_test:
        return self_test()

    if not PAYLOAD.is_file():
        return say_skip(
            "the native payload is not staged (python/cpp_pinyin2/_native/name.txt is missing)",
            "stage it: python python/tools/build_native.py --build-dir .tmp/mk-default",
        )

    code, output = run_suite(["-q", "python/tests"], ROOT)
    tail = [line for line in output.strip().splitlines() if line.strip()]
    if code == 0:
        print(f"check_python_shell: PASS ({tail[-1] if tail else 'suite passed'})")
        return 0
    print("check_python_shell: FAIL -- the Python shell's suite did not pass")
    for line in tail[-15:]:
        print(f"  {line}")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
