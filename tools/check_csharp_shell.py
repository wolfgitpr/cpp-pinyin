#!/usr/bin/env python3
"""Run the C# shell's own test suite, and say SKIP when it cannot be run.

The C# suite existed and passed while nothing in this repository ran it, so it rotted in silence. It joins
the gate run with two rules:

  * a STALE native library is refused, not used -- a fresh binary is the only thing whose verdict means
    anything (`csharp/tools/run_tests.py` carries the same check);
  * "cannot be run" is said out loud. `dotnet` missing, no built library, or a library older than the
    headers: print what is missing and the exact command that fixes it. With `--require` each of those
    becomes an error, for callers (CI) that must not accept a silent skip.

Exit codes: 0 pass (or SKIP), 1 the suite failed, 2 required-but-unanswerable or usage.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RUNNER = ROOT / "csharp" / "tools" / "run_tests.py"
#: Directories that may hold a built native library, best first: the staged payload is the one the Python shell
#: uses, so a tree that ran build_native.py needs no second build.
NATIVE_CANDIDATES = (
    ROOT / "python" / "cpp_pinyin2" / "_native",
    ROOT / ".tmp" / "build-default" / "bin",
    ROOT / ".tmp" / "build-p3a-shared" / "bin",
)
SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".c"}
SKIP, RUN = "skip", "run"


def newest_source() -> Path | None:
    newest: tuple[float, Path] | None = None
    for directory in ("src", "include"):
        for path in (ROOT / directory).rglob("*"):
            if path.is_file() and path.suffix in SOURCE_SUFFIXES:
                stamp = path.stat().st_mtime
                if newest is None or stamp > newest[0]:
                    newest = (stamp, path)
    return newest[1] if newest else None


def library_in(directory: Path) -> Path | None:
    if not directory or not directory.is_dir():
        return None
    libraries = [p for p in directory.glob("*cpp-pinyin2*") if p.is_file()]
    return max(libraries, key=lambda p: p.stat().st_mtime) if libraries else None


def decide(dotnet: str | None, native_dir: Path | None, source: Path | None) -> tuple[str, str]:
    """(verdict, reason). The whole gate's judgement lives here so the self-test can drive it."""
    if not dotnet:
        return SKIP, ("dotnet is not on PATH, so the C# suite cannot be built or run "
                      "(install the .NET SDK, or accept the skip)")
    library = library_in(native_dir) if native_dir else None
    if native_dir is None:
        return SKIP, ("no built native library found (looked in "
                      + ", ".join(str(p.relative_to(ROOT)) for p in NATIVE_CANDIDATES)
                      + "); stage one: python python/tools/build_native.py --build-dir .tmp/build-default")
    if library is None:
        return SKIP, f"{native_dir} holds no cpp-pinyin2 library; build it or point --native-dir elsewhere"
    if source is not None and library.stat().st_mtime < source.stat().st_mtime:
        return SKIP, (f"{library.relative_to(ROOT) if library.is_relative_to(ROOT) else library} is older than "
                      f"{source.relative_to(ROOT)} -- a suite run against a stale library reports the OLD behaviour "
                      f"with the NEW test names (measured 2026-10-21: that is exactly how a binding defect was "
                      f"diagnosed that did not exist). Rebuild first")
    return RUN, str(library)


def self_test() -> int:
    """Four controls against `decide()`: the gate must refuse for the right reasons and run for the right ones."""
    fresh = ROOT / "src" / "Engine.cpp"
    cases = [
        ("no dotnet", decide(None, NATIVE_CANDIDATES[0], fresh), SKIP),
        ("no native directory", decide("dotnet", None, fresh), SKIP),
        ("a directory with no library", decide("dotnet", ROOT / "docs", fresh), SKIP),
        ("a library older than the sources", decide("dotnet", ROOT / ".tmp", fresh), SKIP),
    ]
    failures = 0
    for label, (verdict, reason), want in cases:
        ok = verdict == want and bool(reason)
        print(f"self-test: {label} -> {verdict} {'ok' if ok else 'FAIL'}")
        failures += 0 if ok else 1
    real = library_in(NATIVE_CANDIDATES[0])
    if real is not None:
        verdict, _ = decide("dotnet", NATIVE_CANDIDATES[0], None)
        ok = verdict == RUN
        print(f"self-test: a real staged library -> {verdict} {'ok' if ok else 'FAIL'}")
        failures += 0 if ok else 1
    print(f"SELFTEST {'PASS' if not failures else 'FAIL'} -- {len(cases) + (1 if real else 0)} case(s)")
    return 1 if failures else 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="run the C# shell's test suite")
    parser.add_argument("--native-dir", type=Path, help="directory holding the built native library")
    parser.add_argument("--require", action="store_true", help="a skip becomes exit 2")
    parser.add_argument("--self-test", action="store_true", help="prove the decision logic refuses correctly")
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test()

    dotnet = shutil.which("dotnet")
    native_dir = args.native_dir
    if native_dir is None:
        native_dir = next((p for p in NATIVE_CANDIDATES if library_in(p) is not None), None)
    verdict, reason = decide(dotnet, native_dir, newest_source())
    if verdict == SKIP:
        print(f"check_csharp_shell: SKIP -- {reason}")
        return 2 if args.require else 0
    if not RUNNER.is_file():
        print("check_csharp_shell: FAIL -- csharp/tools/run_tests.py is missing")
        return 1

    completed = subprocess.run(
        [sys.executable, str(RUNNER), "--native-dir", str(native_dir or "")],
        cwd=ROOT, capture_output=True, text=True)
    output = (completed.stdout + completed.stderr).strip().splitlines()
    if completed.returncode == 0:
        print(f"check_csharp_shell: PASS ({output[-1] if output else 'suite passed'}; library {reason})")
        return 0
    print("check_csharp_shell: FAIL -- the C# shell's suite did not pass")
    for line in output[-20:]:
        print(f"  {line}")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
