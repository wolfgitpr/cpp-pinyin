"""Run the C# test suite without VSTest, and prove the runner itself before trusting it.

Why this exists (round 34): `dotnet test` and `dotnet vstest` both abort in this environment before a single
test runs, with

    Unhandled exception. System.ComponentModel.Win32Exception (5): Access is denied.
      at System.Diagnostics.ProcessManager.OpenProcess(...)
      at Microsoft.VisualStudio.TestPlatform.TestHost.DefaultEngineInvoker.SetParentProcessExitCallback(...)

i.e. VSTest's testhost cannot open its parent process handle. It reproduces with a clean environment, with the
workspace TMP/TEMP override this project uses for the C++ suite, and through `dotnet vstest` directly, so it is
an environment property rather than anything in the repository. The tests themselves never changed, and the same
assemblies passed here and on the board before. This script runs them through xunit's own execution engine
instead, using csharp/tools/minirunner, which is not the VSTest host protocol -- the only part that is broken.

WHY it happens here and not in CI is still a suspicion, and it is written down as one: the call that fails is
OpenProcess on the parent process, so the process that starts the run is the prime suspect. The agent that
found this runs commands under a confinement layer which also refuses named pipes and, in its read-only mode,
the full language -- plausible, unproven. One experiment would settle it and needs a plain terminal, not this
workspace: run `dotnet test` outside the agent. Until that is done, treat the cause as unestablished and the
runner as the workaround that works, which is what the CI job's successful `dotnet test` and this script's 54
passing tests on both target frameworks both say.

Measured here: every target framework reports the same 54 tests with no failures, and `--self-test` reports the
control's single deliberate failure. Two traps are coded around, both hit while building this:

  * the merge order matters. CppPinyin.Tests' output carries xunit.runner.utility.netcoreapp10.dll at assembly
    version 2.9.0.0 while the runner needs 2.9.3.0, and the two files have byte-identical sizes (255184), so a
    size check says "same file" while the loader says FileNotFoundException. The runner's copy therefore goes in
    LAST, and the precheck below reads assembly identities rather than sizes.
  * xunit's discoverer looks for xunit.execution.dotnet.dll next to the test assembly, not next to the runner, so
    both outputs have to end up in one directory.

Usage:
    python csharp/tools/run_tests.py                    # both target frameworks
    python csharp/tools/run_tests.py --tfm net10.0      # one of them
    python csharp/tools/run_tests.py --self-test        # run the control, whose failure must be reported
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
from pathlib import Path
from typing import List, Optional, Sequence, Tuple

ROOT = Path(__file__).resolve().parents[2]
MINIRUNNER = ROOT / "csharp" / "tools" / "minirunner"
TESTS_PROJECT = ROOT / "csharp" / "CppPinyin.Tests" / "CppPinyin.Tests.csproj"
SCRATCH = ROOT / ".tmp" / "minirunner"
SUMMARY = re.compile(r"total (\d+), failed (\d+)")
TFMS = ("net7.0", "net10.0")
REQUIRED = ("Runner.dll", "Runner.deps.json", "xunit.execution.dotnet.dll")


def run(command: List[str], env: Optional[dict] = None) -> Tuple[int, str]:
    completed = subprocess.run(command, cwd=str(ROOT), env=env, capture_output=True, text=True)
    return completed.returncode, (completed.stdout or "") + (completed.stderr or "")


def merge(test_bin: Path, runner_bin: Path, target: Path) -> List[str]:
    """Copy both outputs into one directory, runner LAST, and return what the run still needs."""
    if target.exists():
        shutil.rmtree(str(target))
    target.mkdir(parents=True)
    for source in (test_bin, runner_bin):
        for item in sorted(source.iterdir()):
            destination = target / item.name
            if item.is_dir():
                shutil.copytree(str(item), str(destination), dirs_exist_ok=True)
            else:
                shutil.copy2(str(item), str(destination))
    missing = [name for name in REQUIRED if not (target / name).is_file()]
    if not list(target.glob("xunit.runner.utility*.dll")):
        missing.append("xunit.runner.utility*.dll")
    return missing


def run_one(label: str, test_bin: Path, runner_bin: Path, assembly: str, env: dict,
            expect: Optional[Tuple[int, int]] = None) -> bool:
    target = SCRATCH / label
    missing = merge(test_bin, runner_bin, target)
    if missing:
        print("FAIL  {0}: merged directory is incomplete, missing {1}".format(label, ", ".join(missing)))
        return False
    code, output = run(["dotnet", str(target / "Runner.dll"), str(target / assembly)], env)
    for line in output.splitlines():
        if line.startswith(("FAIL ", "SKIP ")):
            print("      " + line)
    match = SUMMARY.search(output)
    # Print the runner's own output when the run is red. It carries `info.ExceptionMessage` on the line after each
    # FAIL (Program.cs:37-38), and discarding it made every failure undiagnosable from this script -- the 2026-10-21
    # conformance failures had to be re-run by hand to be read at all. Success stays quiet.
    if match is not None and int(match.group(2)) > 0:
        print("      --- runner output ---")
        for line in output.splitlines():
            print("      " + line)
        print("      --- end ---")
    if not match:
        print("FAIL  {0}: no summary line (exit {1})".format(label, code))
        for line in [l for l in output.splitlines() if l.strip()][-4:]:
            print("      " + line)
        return False
    total, failed = int(match.group(1)), int(match.group(2))
    if expect is None:
        ok = failed == 0 and total > 0 and code == 0
    else:
        # An inverted expectation: the control must report its deliberate failure and a non-zero exit.
        ok = (total, failed) == expect and code != 0
    print("{0} {1}: total {2}, failed {3}, exit {4}{5}".format(
        "PASS " if ok else "FAIL ", label, total, failed, code,
        "" if expect is None else " (expected total {0}, failed {1})".format(expect[0], expect[1])))
    return ok


def stale_reason(native_dir: pathlib.Path) -> Optional[str]:
    """Why `native_dir` is older than the sources, or None when it is not.

    A test run against a stale library reports the OLD behaviour with the NEW test names, which is the most
    expensive kind of false signal: it looks like a defect in the shell. Measured 2026-10-21 -- a five-day-old DLL
    made `cppp2_apply_tone_sandhi` return INTERNAL and the suite looked broken while a fresh build was green.
    """
    newest = 0.0
    newest_path = None
    for root_name in ("src", "include"):
        for path in (ROOT / root_name).rglob("*"):
            if path.is_file() and path.suffix in {".cpp", ".h", ".hpp", ".c"}:
                mtime = path.stat().st_mtime
                if mtime > newest:
                    newest, newest_path = mtime, path
    libraries = [p for p in native_dir.glob("*cpp-pinyin2*") if p.is_file()]
    if not libraries or newest_path is None:
        return None
    newest_library = max(libraries, key=lambda p: p.stat().st_mtime)
    if newest_library.stat().st_mtime < newest:
        return "{0} is older than {1} -- the native library predates the sources".format(
            newest_library.relative_to(ROOT) if newest_library.is_relative_to(ROOT) else newest_library,
            newest_path.relative_to(ROOT),
        )
    return None


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--tfm", action="append", choices=TFMS, help="target framework to run (repeatable)")
    parser.add_argument("--native-dir", type=Path,
                        help="directory holding the built native library (default: .tmp/build-p3a-shared/bin)")
    parser.add_argument("--self-test", action="store_true", help="run the control project, which must fail")
    options = parser.parse_args(argv)

    native_dir = options.native_dir or (ROOT / ".tmp" / "build-p3a-shared" / "bin")
    if not native_dir.is_dir():
        print("FAIL  no native library directory at {0}: the tests need the built library and do not skip".format(native_dir))
        return 1
    if options.native_dir is None and stale_reason(native_dir) is not None:
        print("FAIL  " + stale_reason(native_dir))
        print("      The default directory is a build output; it is only as trustworthy as its timestamp.")
        print("      Rebuild it, or pass --native-dir <dir> to say which library you mean.")
        return 1

    env = dict(os.environ)
    env["CPPP_NATIVE_DIR"] = str(native_dir)
    env["DOTNET_CLI_UI_LANGUAGE"] = "en-US"

    targets = [("ctrl", MINIRUNNER / "ctrl" / "Ctrl.csproj", "Ctrl.dll")] if options.self_test else [
        (tfm, TESTS_PROJECT, "CppPinyin.Tests.dll") for tfm in (options.tfm or list(TFMS))
    ]
    # The runner is built once, on its own, before anything else: an earlier version of this script built only the
    # test projects and then died with FileNotFoundError on minirunner/bin/Release/<tfm>.
    for project in [MINIRUNNER / "Runner.csproj"] + [project for _, project, _ in targets]:
        code, output = run(["dotnet", "build", str(project), "-c", "Release"], env)
        if code != 0:
            print("FAIL  {0} did not build".format(project.name))
            # Never swallow a build failure's reason: the previous version discarded this output and reported only
            # that something did not build.
            for line in [l for l in output.splitlines() if l.strip()][-6:]:
                print("      " + line)
            return 1

    results = []
    for label, project, assembly in targets:
        tfm = "net10.0" if options.self_test else label
        test_bin = project.parent / "bin" / "Release" / tfm
        if not test_bin.is_dir():
            print("FAIL  no output at {0}: build it first, or pass --native-dir if the native library is missing".format(test_bin))
            return 1
        runner_bin = MINIRUNNER / "bin" / "Release" / tfm
        expect = (2, 1) if options.self_test else None
        results.append(run_one(label, test_bin, runner_bin, assembly, env, expect))

    if options.self_test:
        ok = all(results)
        print("self-test: {0}".format("PASS" if ok else "FAIL"))
        return 0 if ok else 1
    if all(results):
        print("PASS  {0} target framework(s), no failures".format(len(results)))
        return 0
    print("FAIL  {0} of {1} target framework(s) failed".format(results.count(False), len(results)))
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
