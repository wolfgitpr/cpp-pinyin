#!/usr/bin/env python3
"""Merge per-RID CppPinyin nupkgs into ONE package that carries every runtime identifier.

WHY THIS EXISTS. `dotnet pack -p:CppPinyinRid=<rid>` is required to name the rid, and `CppPinyinRid` is
deliberately not defaulted (csharp/CppPinyin/CppPinyin.csproj: a guessed rid ships a package that installs
cleanly and then fails to load the library). The consequence is that each pack carries exactly ONE
`runtimes/<rid>/native/` entry -- which `tools/check_bindings.py --rid` asserts on purpose ("a package is
packed per runtime identifier"). So a release either publishes N packages and asks every consumer to pick
the right one by hand, or publishes ONE package carrying all the rids. This tool builds that one.

WHAT IT REFUSES TO DO, and why each refusal is here rather than in a comment:
  * inputs whose `lib/`, `ref/`, `contentFiles/` or README bytes differ -- those are supposed to be the
    same package built once per platform, so a difference means the inputs came from different builds (or
    different versions) and merging them would ship two halves that disagree;
  * inputs that carry no `runtimes/<rid>/native/` entry at all;
  * an output path that already exists (pass --force to overwrite), because silently replacing a package
    someone just built and checked is how a "verified" artifact stops matching what gets published.

WHAT IT DOES NOT DO: it never touches the native libraries, the assemblies or the nuspec's content. It
copies bytes and rewrites the zip's index -- no repacking of a library, no renaming (the tags policy in the
plugin guide forbids secondary surgery on the artifacts: "anyone who does it may not modify the product").

USAGE
    python csharp/tools/merge_nupkgs.py -o dist/CppPinyin.<version>.nupkg \
        dist/win-x64/CppPinyin.<version>.nupkg dist/linux-x64/CppPinyin.<version>.nupkg ...

Verify the result with the repository's own gate (omit --rid: a pooled package is exactly the case that
must NOT be asked for a single rid):
    python tools/check_bindings.py --nupkg dist/CppPinyin.<version>.nupkg
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
import zipfile
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Set, Tuple

#: Directory prefixes whose bytes must be identical in every input: the managed assembly a consumer loads,
#: the reference assembly it compiles against, and the dictionaries (data/dialects is the single source of
#: truth, so every package built from one commit carries the same copy).
SHARED_PREFIXES = ("lib/", "ref/", "contentFiles/")

#: The one member whose content describes the whole package: identical inputs only.
NUSPEC_RE = re.compile(r"^[^/]+\.nuspec$")

RUNTIME_RE = re.compile(r"^runtimes/([^/]+)/native/(.+)$")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_package(path: Path) -> Dict[str, bytes]:
    with zipfile.ZipFile(path) as archive:
        return {name: archive.read(name) for name in archive.namelist() if not name.endswith("/")}


class Package:
    """One input package, with the facts this tool needs already extracted."""

    def __init__(self, path: Path) -> None:
        self.path = path
        self.members = read_package(path)
        specs = [name for name in self.members if NUSPEC_RE.match(name) and "/" not in name]
        if len(specs) != 1:
            raise SystemExit(f"FATAL: {path} holds {len(specs)} root .nuspec files ({specs}); expected exactly 1")
        self.nuspec = specs[0]
        self.rids: Set[str] = set()
        for name in self.members:
            match = RUNTIME_RE.match(name)
            if match:
                self.rids.add(match.group(1))
        if not self.rids:
            raise SystemExit(
                f"FATAL: {path} carries no runtimes/<rid>/native/ entry, so it contributes nothing to a "
                "pooled package (was it packed without -p:CppPinyinRid=...?)"
            )

    def shared_fingerprints(self) -> Dict[str, str]:
        """member -> sha256 for the members every input must agree on."""
        # `self.nuspec`, not a hard-coded file name: `__init__` already requires exactly one root .nuspec and
        # records it. The literal it replaces meant a package whose id (and therefore nuspec name) changed would
        # have its nuspec silently dropped from the comparison -- the one member that describes the whole package.
        return {
            name: sha256(blob)
            for name, blob in self.members.items()
            if name.startswith(SHARED_PREFIXES)
            or name == "README.md"
            or name == self.nuspec
        }


def compare_shared(packages: Sequence[Package]) -> List[str]:
    """Differences among the members whose bytes must be identical across inputs."""
    problems: List[str] = []
    # Every per-RID pack of one version has the SAME file name (CppPinyin.2.0.0.nupkg), so a message that
    # named only the file could not tell the two inputs apart -- measured on the first real merge attempt,
    # where every line read "differs between CppPinyin.2.0.0.nupkg and CppPinyin.2.0.0.nupkg". The label
    # therefore carries the parent directory whenever two inputs share a name.
    names = [package.path.name for package in packages]
    labels = [
        package.path.name if names.count(package.path.name) == 1
        else f"{package.path.parent.name}/{package.path.name}"
        for package in packages
    ]
    baseline = packages[0].shared_fingerprints()
    for index, package in enumerate(packages[1:], start=1):
        current = package.shared_fingerprints()
        for name in sorted(set(baseline) | set(current)):
            left, right = baseline.get(name), current.get(name)
            if left == right:
                continue
            if left is None or right is None:
                holder = labels[0] if left else labels[index]
                missing = labels[index] if left else labels[0]
                problems.append(f"{name}: present in {holder} but not in {missing}")
            else:
                problems.append(f"{name}: bytes differ between {labels[0]} and {labels[index]}")
    return problems


def merge(packages: Sequence[Package], output: Path,
          expect_rids: Optional[Sequence[str]] = None) -> Dict[str, object]:
    """Write `output` as one package holding every input's runtimes/, and report what went in.

    `expect_rids`, when given, is the set the caller believes the inputs carry -- the release job's matrix.
    A pool that silently merges FEWER rids than the matrix produced is not caught by anything downstream:
    `check_bindings.py --nupkg` is run without `--rid` on purpose (a pooled package carries several), so a
    two-rid pool passes every gate and ships. This is the only place that can refuse it (2026-10-27).
    """
    total_rids: Set[str] = set()
    for package in packages:
        overlap = total_rids & package.rids
        if overlap:
            raise SystemExit(
                f"FATAL: {package.path.name} carries {sorted(overlap)}, which an earlier input already "
                "carried: two copies of the same rid would be merged silently, and only one of them would win"
            )
        total_rids |= package.rids

    if expect_rids is not None:
        expected = set(expect_rids)
        if expected != total_rids:
            missing = sorted(expected - total_rids)
            unexpected = sorted(total_rids - expected)
            raise SystemExit(
                "FATAL: the inputs do not carry the expected runtime identifiers"
                + (f"; missing {missing}" if missing else "")
                + (f"; unexpected {unexpected}" if unexpected else "")
                + ". A pooled package that is short a rid still passes check_bindings (it is run without "
                  "--rid on purpose), so this check is the only thing between a partial release and the feed."
            )

    first = packages[0]
    # The output directory is created rather than demanded: a release job writes into a fresh dist/ path, and
    # "the directory you asked me to write into does not exist" is not a defect in the package
    # (measured: the first real merge died on FileNotFoundError for its own output path).
    output.parent.mkdir(parents=True, exist_ok=True)
    # The same members every input has (managed assemblies, dictionaries, README) plus the first input's
    # OPC plumbing; the runtime trees are added per input below. Writing the plumbing verbatim is the point:
    # this tool must not reformat anything it does not have to.
    written = 0
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as out:
        for name, blob in first.members.items():
            if RUNTIME_RE.match(name):
                # The runtime trees are written by the per-input loop below, for EVERY input including
                # this first one. Writing them here as well put the first input's native library into the
                # package TWICE -- a duplicate zip member, measured on a real merged package on 2026-10-27
                # (33 members, 1 duplicated name) -- and nothing caught it: check_bindings reads members
                # into a dict, where the second copy silently wins.
                continue
            out.writestr(name, blob)
            written += 1
        for package in packages:
            for name, blob in package.members.items():
                match = RUNTIME_RE.match(name)
                if match:
                    out.writestr(name, blob)
                    written += 1
    return {"rids": sorted(total_rids), "members": written}


def self_test() -> int:
    """Prove the two refusals can fire: a matching pair merges, a disagreeing pair is refused."""
    failures: List[str] = []

    def expect(label: str, ok: bool, detail: str = "") -> None:
        print(f"{'PASS' if ok else 'FAIL'}  self-test: {label}" + ("" if ok else f"  -> {detail}"))
        if not ok:
            failures.append(label)

    import tempfile

    with tempfile.TemporaryDirectory() as raw:
        tmp = Path(raw)

        def synthetic(rid: str, payload: bytes, native_suffix: str) -> Path:
            """A minimal package shaped like the real one: one nuspec, one shared member, one runtime."""
            path = tmp / f"CppPinyin.1.0.0-{rid}.nupkg"
            with zipfile.ZipFile(path, "w") as archive:
                archive.writestr("CppPinyin.nuspec", b"<package><metadata><id>CppPinyin</id></metadata></package>")
                archive.writestr("lib/net7.0/CppPinyin.dll", b"managed")
                archive.writestr("contentFiles/any/any/data/dict/mandarin/word.txt", b"a:1")
                archive.writestr(f"runtimes/{rid}/native/libcpp-pinyin2{native_suffix}", payload)
            return path

        win = synthetic("win-x64", b"windows-library", ".dll")
        linux = synthetic("linux-x64", b"linux-library", ".so")
        merged = tmp / "pooled.nupkg"
        report = merge([Package(win), Package(linux)], merged)
        expect("two rids merge into one package", report["rids"] == ["linux-x64", "win-x64"], report)
        with zipfile.ZipFile(merged) as archive:
            listing = archive.namelist()
        names = set(listing)
        # A SET hides exactly the failure this assertion is for: `zipfile` lets the same name in twice,
        # the package still opens, `check_bindings` still passes (it reads into a dict), and only the
        # size and some strict readers show it. Compare the list against its set.
        expect("the pooled package has no duplicate member names",
               len(listing) == len(names), sorted({n for n in listing if listing.count(n) > 1}))
        expect("the pooled package keeps both runtime trees",
               {"runtimes/win-x64/native/libcpp-pinyin2.dll",
                "runtimes/linux-x64/native/libcpp-pinyin2.so"} <= names, sorted(names))
        expect("the pooled package keeps lib/ and contentFiles/ exactly once",
               len([n for n in names if n.startswith("lib/")]) == 1
               and len([n for n in names if n.startswith("contentFiles/")]) == 1, sorted(names))
        expect("shared members are byte-identical after the merge",
               compare_shared([Package(win), Package(linux)]) == [])

        stale = synthetic("osx-arm64", b"darwin-library", ".dylib")
        # Damage the shared half of the third package: the merge must refuse it rather than pick a winner.
        with zipfile.ZipFile(stale, "a") as archive:
            archive.writestr("lib/net7.0/CppPinyin.dll", b"managed-but-different")
        problems = compare_shared([Package(win), Package(stale)])
        expect("a differing lib/ member is reported", any("lib/net7.0" in p for p in problems), problems)

        duplicate = synthetic("win-x64", b"other-windows-library", ".dll")
        try:
            merge([Package(win), Package(duplicate)], tmp / "dup.nupkg")
        except SystemExit as error:
            expect("the same rid twice is refused", "already" in str(error), str(error))
        else:
            expect("the same rid twice is refused", False, "merge accepted a duplicate rid")

        # The expected-set check (2026-10-27). The release job knows which rids its matrix just built; this
        # is the only place that can tell a TWO-rid pool from a three-rid one, because the gate that runs
        # next is deliberately given no --rid.
        expect("an expected set that matches merges",
               merge([Package(win), Package(linux)], tmp / "ok.nupkg",
                     ["linux-x64", "win-x64"])["rids"] == ["linux-x64", "win-x64"])
        try:
            merge([Package(win), Package(linux)], tmp / "short.nupkg",
                  ["linux-x64", "osx-arm64", "win-x64"])
        except SystemExit as error:
            expect("a pool missing an expected rid is refused",
                   "osx-arm64" in str(error) and "missing" in str(error), str(error))
        else:
            expect("a pool missing an expected rid is refused", False,
                   "merge wrote a pool that is short one rid")
        try:
            merge([Package(win), Package(linux)], tmp / "extra.nupkg", ["win-x64"])
        except SystemExit as error:
            expect("a pool carrying an unexpected rid is refused",
                   "linux-x64" in str(error) and "unexpected" in str(error), str(error))
        else:
            expect("a pool carrying an unexpected rid is refused", False,
                   "merge accepted more rids than the matrix named")

    print(f"self-test: {len(failures)} failure(s)" + (": " + "; ".join(failures) if failures else ""))
    return 1 if failures else 0


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("nupkgs", nargs="*", help="per-RID nupkgs to merge (at least one)")
    parser.add_argument("-o", "--output", help="the pooled nupkg to write")
    parser.add_argument("--force", action="store_true", help="overwrite the output if it exists")
    parser.add_argument("--expect-rids",
                        help="comma-separated rids the inputs must carry EXACTLY (the release matrix; a "
                             "pool that is short one still passes the other gates, so this is the guard)")
    parser.add_argument("--self-test", action="store_true", help="prove the refusals fire, then exit")
    options = parser.parse_args(argv)

    if options.self_test:
        return self_test()
    if not options.nupkgs or not options.output:
        parser.error("at least one input nupkg and -o/--output are required (or --self-test)")

    output = Path(options.output).resolve()
    if output.exists() and not options.force:
        raise SystemExit(f"FATAL: {output} already exists; pass --force to replace it")

    packages = [Package(Path(name).resolve()) for name in options.nupkgs]
    for package in packages:
        print(f"input {package.path.name}: rid(s) {sorted(package.rids)}, {len(package.members)} members")

    problems = compare_shared(packages)
    if problems:
        raise SystemExit(
            "FATAL: the inputs disagree on members that must be identical. Those files are the same package "
            "built once per platform, so a difference means they came from different builds or versions:\n  "
            + "\n  ".join(problems)
        )

    expect_rids = None
    if options.expect_rids:
        expect_rids = [rid.strip() for rid in options.expect_rids.split(",") if rid.strip()]
        if not expect_rids:
            parser.error("--expect-rids was given but names no rid")

    report = merge(packages, output, expect_rids)
    size = output.stat().st_size
    print(f"\nwrote {output} ({size} bytes, {report['members']} members)")
    print("runtime identifiers: " + ", ".join(report["rids"]))
    print(
        "\nverify with: python tools/check_bindings.py --nupkg "
        + str(output)
        + "\n(no --rid: a pooled package is the one case that must not be asked for a single rid)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
