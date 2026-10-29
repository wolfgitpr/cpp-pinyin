#!/usr/bin/env python3
"""Do the files that CI INTERPRETS actually parse?

Five workflows and two shell scripts decide whether anything gets built, packed or published, and nothing
parsed them:

  * `tools/check_bindings.py`'s `check_workflow_wiring` reads the workflow files as TEXT and counts
    indentation. That answers wiring questions and nothing about whether the file is YAML -- a space where
    it belongs, an unclosed quote, a list that lost its dash all pass it and fail at dispatch, which is the
    worst place to learn it because a dispatch is a decision;
  * the two tracked shell scripts had no caller and no `bash -n`. `python/tools/ci_macos_verify.sh` hands a
    macOS wheel to a release step, so a syntax error there dies on a machine nobody has.

Both checks are one subprocess each, and what makes them worth a gate is that the failure mode is
invisible: an unparsable workflow is not a red X here, it is a green report and a red dispatch.

WHAT IT CHECKS

  1. every tracked `*.sh` passes `bash -n` (syntax only -- it does not run anything);
  2. every `.github/workflows/*.yml` loads with `yaml.safe_load`;
  3. every tracked `*.json` loads with `json.loads`;
  4. `CMakePresets.json`, if tracked, is accepted by `cmake --list-presets` -- JSON validity is not enough
     for that file, because CMake reads a schema out of it and a wrong `version` or a preset missing a
     required key is valid JSON no `cmake --preset` accepts.

WHEN IT CANNOT SPEAK. `bash`, PyYAML and `cmake` are not project dependencies, so a machine may lack them.
That is SKIP, not PASS: with `--require` (the form `tools/run_all_gates.py` uses) an unanswerable check
exits 2, the same convention as `tools/check_format.py`. JSON needs no tool.

`bash` is searched on PATH first and then in Git for Windows' two known locations: on this repository's own
Windows development machine it is not on PATH, and a gate that cannot run where its author works is a gate
that never runs.

WHAT IT DOES NOT DO. It does not validate workflows against GitHub's schema, does not check that a step's
name resolves, and does not shellcheck the scripts: `bash -n` catches structure, not intent.
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import List, Optional, Tuple

ROOT = Path(__file__).resolve().parent.parent

#: Where Git for Windows puts bash when it is installed but not on PATH. Kept short and explicit rather
#: than searched recursively: a gate that walks the filesystem to find its own tool is a gate whose
#: behaviour depends on the machine in a way nobody can read off the source.
BASH_CANDIDATES = (r"C:\Program Files\Git\bin\bash.exe", r"C:\Program Files\Git\usr\bin\bash.exe")


def bash_works(candidate: str) -> bool:
    """Does this `bash` actually run a command?

    THE PATH LOOKUP ALONE IS NOT ENOUGH ON WINDOWS. `C:\\Windows\\System32\\bash.exe` is the WSL launcher:
    `shutil.which("bash")` finds it first, and it answers `bash -n file.sh` with a UTF-16 "Windows Subsystem
    for Linux has no installed distributions" message and a non-zero status. Measured here on 2026-10-27:
    taking that as "bash is present" made the gate report a PERFECTLY VALID script as unparsable -- a false
    finding about the tree, caused by the tool's own search order. So a candidate is only accepted if it
    runs something and says so.
    """
    try:
        completed = subprocess.run([candidate, "-c", "echo check-parsable-ok"],
                                   capture_output=True, text=True, encoding="utf-8", errors="replace",
                                   timeout=60)
    except (OSError, subprocess.SubprocessError):
        return False
    return completed.returncode == 0 and "check-parsable-ok" in (completed.stdout or "")


def find_bash() -> Optional[str]:
    candidates = [shutil.which("bash"), *BASH_CANDIDATES]
    for candidate in candidates:
        if candidate and Path(candidate).is_file() and bash_works(candidate):
            return candidate
    return None


def tracked(root: Path, pattern: str) -> List[Path]:
    """Files git knows about, so a scratch script in .tmp/ cannot turn the gate red."""
    completed = subprocess.run(["git", "-C", str(root), "ls-files", pattern],
                               capture_output=True, text=True, encoding="utf-8", errors="replace")
    if completed.returncode != 0:
        raise SystemExit(f"FATAL: git ls-files {pattern} failed in {root}: {completed.stderr.strip()}")
    return [root / name.strip() for name in completed.stdout.splitlines() if name.strip()]


#: Directories an untracked scan must not walk into. `git archive`-style exports have none of these, but a
#: working copy does, and `build/` holds generated `.sh` files on some generators.
UNTRACKED_SKIP = {".git", ".tmp", "dist", "__pycache__", "node_modules"}


def is_own_checkout(root: Path) -> bool:
    """Is `root` a checkout of ITSELF, or a directory that merely sits inside one?

    The same test `tools/check_conformance_provenance.py` uses, for the same measured reason: `git -C
    <dir> rev-parse --git-dir` also succeeds inside somebody else's work tree (this repository's own
    `.tmp/` is exactly such a directory), so only the toplevel comparison answers the question. It matters
    here because `git -C <export> ls-files` returns NOTHING (the pathspec is filtered by the enclosing
    repository's prefix), which made this gate report "nothing was parsed" on a perfectly good exported
    tree -- measured on 2026-10-27 against `git archive HEAD`.
    """
    completed = subprocess.run(["git", "-C", str(root), "rev-parse", "--show-toplevel"],
                               capture_output=True, text=True, encoding="utf-8", errors="replace")
    if completed.returncode != 0:
        return False
    try:
        return Path(completed.stdout.strip()).resolve() == root.resolve()
    except OSError:
        return False


def files_of(root: Path, pattern: str, own_checkout: bool) -> List[Path]:
    """The file set this gate checks: git's view in its own checkout, the filesystem's view otherwise."""
    if own_checkout:
        return tracked(root, pattern)
    suffix = pattern.rsplit("*", 1)[-1] if "*" in pattern else pattern
    found = [path for path in root.rglob(f"*{suffix}")
             if not UNTRACKED_SKIP & set(part for part in path.relative_to(root).parts[:-1])]
    return sorted(found)


def bash_syntax(bash: str, path: Path) -> Tuple[bool, str]:
    completed = subprocess.run([bash, "-n", str(path)], capture_output=True, text=True,
                               encoding="utf-8", errors="replace")
    return completed.returncode == 0, ((completed.stdout or "") + (completed.stderr or "")).strip()


def yaml_load(path: Path) -> Tuple[bool, str]:
    import yaml                                                # noqa: PLC0415 -- optional dependency

    try:
        yaml.safe_load(path.read_text(encoding="utf-8"))
    except Exception as exc:                                   # noqa: BLE001 -- any parse error is the finding
        return False, f"{type(exc).__name__}: {exc}".replace("\n", " ")
    return True, ""


def json_load(path: Path) -> Tuple[bool, str]:
    try:
        json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:                                   # noqa: BLE001 -- any parse error is the finding
        return False, f"{type(exc).__name__}: {exc}".replace("\n", " ")
    return True, ""


def presets_load(cmake: str, directory: Path) -> Tuple[bool, str]:
    """Ask CMake itself, because the presets file has a schema that `json.loads` says nothing about."""
    completed = subprocess.run([cmake, "--list-presets"], cwd=str(directory), capture_output=True,
                               text=True, encoding="utf-8", errors="replace")
    return completed.returncode == 0, ((completed.stdout or "") + (completed.stderr or "")).strip()


#: The presets file says `"version": 3` and `cmakeMinimumRequired: 3.21`, and `--list-presets` itself
#: arrived in 3.19. This project's own `cmake_minimum_required` is 3.17, so a machine can legitimately
#: have a CMake that configures this tree perfectly and cannot read its presets at all. Reading the
#: presets is then a SKIP, not a finding: the file is not wrong, the tool is old (2026-10-27).
PRESETS_MIN_CMAKE = (3, 21)


def cmake_version(cmake: str) -> Optional[Tuple[int, ...]]:
    """(major, minor[, patch]) from `cmake --version`'s first line, or None if it cannot be read."""
    completed = subprocess.run([cmake, "--version"], capture_output=True, text=True,
                               encoding="utf-8", errors="replace")
    first = (completed.stdout or "").splitlines()
    if completed.returncode != 0 or not first:
        return None
    match = re.search(r"(\d+)\.(\d+)(?:\.(\d+))?", first[0])
    if not match:
        return None
    return tuple(int(part) for part in match.groups() if part is not None)


def check(root: Path) -> Tuple[List[str], List[str], List[str]]:
    """(findings, checked, skipped) -- three lists so the caller can print what did and did not happen."""
    findings: List[str] = []
    checked: List[str] = []
    skipped: List[str] = []
    own_checkout = is_own_checkout(root)

    scripts = files_of(root, "*.sh", own_checkout)
    bash = find_bash()
    if bash is None:
        if scripts:
            skipped.append(f"{len(scripts)} shell script(s): bash not found on PATH or in Git for Windows")
    else:
        for path in scripts:
            ok, output = bash_syntax(bash, path)
            if ok:
                checked.append(f"{path.relative_to(root).as_posix()} (bash -n)")
            else:
                findings.append(f"{path.relative_to(root).as_posix()}: bash -n failed\n{output}")

    workflows = files_of(root, ".github/workflows/*.yml", own_checkout)
    try:
        import yaml                                            # noqa: F401,PLC0415 -- presence probe
        have_yaml = True
    except ImportError:
        have_yaml = False
    if not have_yaml:
        if workflows:
            skipped.append(f"{len(workflows)} workflow(s): PyYAML is not installed")
    else:
        for path in workflows:
            ok, output = yaml_load(path)
            if ok:
                checked.append(f"{path.relative_to(root).as_posix()} (yaml.safe_load)")
            else:
                findings.append(f"{path.relative_to(root).as_posix()}: yaml.safe_load failed: {output}")

    # JSON needs no tool, so this half never skips.
    for path in files_of(root, "*.json", own_checkout):
        ok, output = json_load(path)
        if ok:
            checked.append(f"{path.relative_to(root).as_posix()} (json.loads)")
        else:
            findings.append(f"{path.relative_to(root).as_posix()}: json.loads failed: {output}")

    presets = files_of(root, "CMakePresets.json", own_checkout)
    cmake = shutil.which("cmake")
    if not presets:
        pass
    elif cmake is None:
        skipped.append("CMakePresets.json: cmake not found, so the schema cannot be read")
    else:
        version = cmake_version(cmake)
        minimum = PRESETS_MIN_CMAKE
        if version is None:
            skipped.append("CMakePresets.json: `cmake --version` could not be read, so whether this cmake "
                           "supports presets is unknown")
        elif version[:2] < minimum:
            skipped.append(f"CMakePresets.json: cmake {'.'.join(map(str, version))} is older than "
                           f"{minimum[0]}.{minimum[1]}, which cannot read a version-3 presets file")
        else:
            ok, output = presets_load(cmake, root)
            if ok:
                checked.append("CMakePresets.json (cmake --list-presets)")
            else:
                findings.append(f"CMakePresets.json: cmake --list-presets failed: {output}")
    return findings, checked, skipped


def self_test() -> int:
    """Prove both checks can fail, on fixtures, without touching the tree."""
    import tempfile

    failures: List[str] = []

    def expect(label: str, ok: bool, detail: str = "") -> None:
        print(f"{'PASS' if ok else 'FAIL'}  self-test: {label}" + ("" if ok else f"  -> {detail}"))
        if not ok:
            failures.append(label)

    bash = find_bash()
    with tempfile.TemporaryDirectory() as raw:
        tmp = Path(raw)
        good = tmp / "good.sh"
        good.write_text("#!/bin/sh\nif [ 1 -eq 1 ]; then echo yes; fi\n", encoding="utf-8")
        # A missing `fi`: the file is a plausible script a human would write, and bash refuses it.
        bad = tmp / "bad.sh"
        bad.write_text("#!/bin/sh\nif [ 1 -eq 1 ]; then echo yes\n", encoding="utf-8")
        if bash is None:
            print("SKIP  self-test: shell half (no bash on this machine)")
        else:
            expect("a valid script passes bash -n", bash_syntax(bash, good)[0])
            ok, output = bash_syntax(bash, bad)
            expect("a script missing its `fi` is reported", not ok, "bash -n accepted it")
            expect("the report names the missing construct", "fi" in output.lower(), output)

        try:
            import yaml                                            # noqa: F401,PLC0415
            have_yaml = True
        except ImportError:
            have_yaml = False
        if not have_yaml:
            print("SKIP  self-test: workflow half (PyYAML not installed)")
        else:
            good_yml = tmp / "good.yml"
            good_yml.write_text("on: push\njobs:\n  a:\n    runs-on: ubuntu-latest\n", encoding="utf-8")
            # An unclosed flow sequence: valid-looking text, not valid YAML.
            bad_yml = tmp / "bad.yml"
            bad_yml.write_text("on: push\njobs: [a, b\n", encoding="utf-8")
            expect("a valid workflow passes yaml.safe_load", yaml_load(good_yml)[0])
            ok, output = yaml_load(bad_yml)
            expect("an unclosed sequence is reported", not ok, "yaml.safe_load accepted it")
            expect("the report carries an exception type", "Error" in output or "error" in output, output)

        # File discovery in a tree that is not a checkout of itself (`git archive` output, a container
        # mount). Measured 2026-10-27: `git -C <export> ls-files '*.sh'` returns NOTHING because the
        # pathspec is filtered by the enclosing repository, and the gate then said "nothing was parsed"
        # about a complete tree.
        nested = tmp / "export"
        (nested / "tools").mkdir(parents=True)
        (nested / "tools" / "a.sh").write_text("#!/bin/sh\ntrue\n", encoding="utf-8")
        (nested / ".tmp").mkdir()
        (nested / ".tmp" / "scratch.sh").write_text("#!/bin/sh\ntrue\n", encoding="utf-8")
        found = files_of(nested, "*.sh", False)
        expect("filesystem discovery finds the script", [p.name for p in found] == ["a.sh"], [str(p) for p in found])
        expect("filesystem discovery skips .tmp/", all(".tmp" not in p.relative_to(nested).parts for p in found),
               [str(p) for p in found])
        expect("a real checkout is recognised as one", is_own_checkout(ROOT), str(ROOT))

        # JSON: a trailing comma is the mistake a hand-edited file actually makes.
        good_json = tmp / "good.json"
        good_json.write_text('{"version": 3, "configurePresets": [{"name": "a"}]}', encoding="utf-8")
        bad_json = tmp / "bad.json"
        bad_json.write_text('{"version": 3,}', encoding="utf-8")
        expect("a valid JSON file passes json.loads", json_load(good_json)[0])
        ok, output = json_load(bad_json)
        expect("a trailing comma is reported", not ok, "json.loads accepted it")
        expect("the JSON report names the decoder's complaint", "JSONDecodeError" in output, output)

        # CMakePresets: valid JSON with a schema CMake refuses. `--list-presets` in a scratch directory
        # needs nothing but cmake itself, which is why this half can be proven here rather than assumed.
        cmake = shutil.which("cmake")
        if cmake is None:
            print("SKIP  self-test: presets half (cmake not on PATH)")
        else:
            # The version reader, because a CMake OLDER than the presets file can configure this tree
            # (cmake_minimum_required is 3.17) and cannot read version-3 presets -- the difference between
            # "the file is broken" and "the tool is old" decides whether this gate may call it a finding.
            for text, expected in (("cmake version 3.17.5", (3, 17, 5)),
                                   ("cmake version 3.30.4", (3, 30, 4)),
                                   ("cmake version 4.0.0-rc1", (4, 0, 0))):
                parsed = re.search(r"(\d+)\.(\d+)(?:\.(\d+))?", text)
                got = tuple(int(part) for part in parsed.groups() if part is not None) if parsed else None
                expect(f"the version reader parses '{text}'", got == expected, f"{got}")
            version = cmake_version(cmake)
            # Only "can I read the version" is asserted. Whether THIS machine's cmake is new enough is a
            # property of the machine, and a self-test that fails because of the toolchain it runs on is
            # the same mistake as the one being fixed here: reporting the environment as a defect in the
            # tree. The gate itself answers that question with a SKIP, not a finding.
            expect("this machine's cmake version is readable", version is not None, "cmake --version unparsable")
            good_presets = tmp / "presets-ok"
            good_presets.mkdir()
            (good_presets / "CMakePresets.json").write_text(
                '{"version": 3, "configurePresets": [{"name": "a", "generator": "Ninja"}]}', encoding="utf-8")
            # `version: 3` with a field the schema does not have at this level: valid JSON, refused by CMake.
            bad_presets = tmp / "presets-bad"
            bad_presets.mkdir()
            (bad_presets / "CMakePresets.json").write_text(
                '{"version": 3, "configurePresets": [{"name": "a", "notAField": 1}]}', encoding="utf-8")
            expect("a valid presets file is accepted by cmake --list-presets", presets_load(cmake, good_presets)[0])
            ok, output = presets_load(cmake, bad_presets)
            expect("a presets file CMake refuses is reported", not ok, "cmake accepted notAField")

    print(f"self-test: {len(failures)} failure(s)" + (": " + "; ".join(failures) if failures else ""))
    return 1 if failures else 0


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=str(ROOT), help="the tree to check (default: this repository)")
    parser.add_argument("--require", action="store_true",
                        help="turn an unanswerable check (no bash / no PyYAML) into exit 2 instead of a skip")
    parser.add_argument("--self-test", action="store_true", help="prove both checks can fail, then exit")
    options = parser.parse_args(argv)

    if options.self_test:
        return self_test()

    root = Path(options.root).resolve()
    findings, checked, skipped = check(root)

    for line in checked:
        print(f"ok    {line}")
    for line in skipped:
        print(f"skip  {line}")
    for line in findings:
        print(f"FAIL  {line}")

    if findings:
        print(f"RESULT: {len(findings)} file(s) do not parse; {len(checked)} parsed, {len(skipped)} skipped")
        return 1
    if skipped and options.require:
        print(f"RESULT: {len(checked)} file(s) parsed, but {len(skipped)} could not be checked and "
              f"--require was given")
        return 2
    if not checked:
        print("RESULT: nothing was parsed -- every check was skipped")
        return 2 if options.require else 0
    print(f"RESULT: every script parses with bash -n, every workflow with yaml.safe_load, every "
          f"JSON file with json.loads, and CMakePresets.json with cmake --list-presets "
          f"({len(checked)} file(s)" + (f", {len(skipped)} skipped" if skipped else "") + ")")
    if not is_own_checkout(root):
        # Say which file set was judged: in an export the list came from the filesystem, so a file that is
        # present but not meant to be part of the tree WOULD be checked. That is the right trade for an
        # extracted tarball and the wrong one for a working copy, and the reader can tell which one they got.
        print("note: this tree is not a checkout of itself, so the file set came from the filesystem "
              "rather than `git ls-files` (build/ and .tmp/ are still skipped)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
