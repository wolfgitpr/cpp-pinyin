#!/usr/bin/env python3
"""Binding-side gates that run on this machine (PLUGIN doc section 6.8.7, row 4).

Every check exists because something actually went wrong. The count is deliberately NOT written out as a
number here: it said FOUR while six kinds of check ran. What is checked instead is the shape -- items
numbered from 1 with no gaps, every check_* function here invoked (self-test case "every defined check is invoked").

1. XML HYGIENE: no "--" in an XML comment (MSBuild refuses the project with MSB4025, or NuGet calls the
   config invalid); the prose's dashes cost four round-trips before this became a check.
2. VERSION CHAIN: CMakeLists.txt vs version.props, nuspec (6.8.5 place 6), supplied wheel METADATA (place 4)
   and tag (place 1b -- unguarded, ci.yml triggers on branches only); regeneration is emit_build_info.py's job.
   Scope note (2026-10-20, audit H45): the COMPLETE list of version projections is the seven-row table under
   `**1. 版本号…**` in `docs/RELEASE_CHECKLIST.md`; this file covers only the places a machine check can run
   here, and the other two (`cppp2_version()` at runtime, the CHANGELOG heading) are guarded by
   `python/tests/test_contract.py` and `tools/check_version_tokens.py`. Counting the projections by reading
   this docstring would undercount -- it used to be quoted as "six places" while the table said seven.
3. PACKAGING PRESENCE (nupkg supplied): native asset, managed assembly and dictionary copy all present, and
   packaged dialects must match data/dialects; the first version packed no runtimes/ entry while pack exited 0.
4. WORKFLOW WIRING: test_runner.yml owns the only push/pull_request triggers and its all-blocking-tests job is
   the single required check -- silently broken by a missing workflow_call, a job absent from needs, or no `if: always()`.
5. PUBLIC HEADER SURFACE: include/cpp-pinyin2 is the installed tree, so its set is frozen in tests/abi/
   expected_public_headers.txt and only installed headers may be included (src/ fails consumers only).
6. EXPORTED DECLARATIONS: c_api.h's symbols are frozen in tests/abi/expected_c_api_symbols.txt; the built
   library is check_exports.cmake's side, this the header side -- exported C++ types are the plugin face.

No flag runs the tree-verifiable subset (hygiene + version chain); --nupkg <file> adds 3, --tag v2.0.0 adds 1b,
--print-version prints the single version source, and --self-test proves each check can go red on a fixture
tree: a gate that has never been seen failing is a gate nobody should trust.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import re
import subprocess
import shutil
import sys
import zipfile
from pathlib import Path
from typing import List, Optional, Sequence, Tuple

ROOT = Path(__file__).resolve().parents[1]
XML_GLOBS = ("*.csproj", "*.props", "*.targets", "*.config", "nuget.config")


class Report:
    """Collects results so --self-test can assert on them instead of parsing stdout."""

    def __init__(self) -> None:
        self.failures: List[str] = []
        self.notes: List[str] = []

    def fail(self, message: str) -> None:
        self.failures.append(message)

    def note(self, message: str) -> None:
        self.notes.append(message)

    @property
    def ok(self) -> bool:
        return not self.failures


def comment_regions(text: str) -> List[Tuple[int, str]]:
    """Every ``<!-- ... -->`` region, with the line it starts on."""
    regions = []
    for match in re.finditer(r"<!--(.*?)-->", text, re.DOTALL):
        regions.append((text.count("\n", 0, match.start()) + 1, match.group(1)))
    # An unterminated comment is its own problem; report it rather than silently skipping the tail.
    opened = text.count("<!--")
    if opened != len(regions):
        regions.append((text.count("\n", 0, text.rfind("<!--")) + 1, text[text.rfind("<!--") :]))
    return regions


def check_xml_hygiene(root: Path, report: Report, exclude_parts: Sequence[str] = (".tmp", "bin", "obj")) -> None:
    files = sorted({p for pattern in XML_GLOBS for p in root.rglob(pattern) if p.is_file()})
    files = [p for p in files if not any(part in exclude_parts for part in p.parts)]
    if not files:
        report.fail("no XML project/config files found under " + str(root) + ": the check would pass vacuously")
        return
    for path in files:
        text = path.read_text(encoding="utf-8")
        for line, body in comment_regions(text):
            if "--" in body:
                where = body[: body.find("--")].count("\n") + line
                report.fail(
                    f"{path.relative_to(root)}:{where}: a double hyphen inside an XML comment makes MSBuild "
                    f"refuse the file (MSB4025) and NuGet call it invalid; only the <!-- and --> delimiters may contain it"
                )
    report.note(f"xml hygiene: {len(files)} file(s) scanned")


def cmake_version(root: Path) -> Optional[str]:
    cmake = root / "CMakeLists.txt"
    if not cmake.is_file():
        return None
    match = re.search(r"project\s*\(\s*cpp-pinyin2\s+VERSION\s+(\d+\.\d+\.\d+)", cmake.read_text(encoding="utf-8"))
    return match.group(1) if match else None


def nuspec_version(nupkg: Path) -> Optional[str]:
    """The package's own version, never a dependency's.

    Taking the first ``<version>...</version>`` in the nuspec looked right while the package had no
    dependencies; a reviewer built the two orders and showed the dependency winning in one of them and the
    self version being missed in the other. The dependencies are removed before the search, and the
    attribute form NuGet actually writes is accepted as well.
    """
    with zipfile.ZipFile(nupkg) as archive:
        names = [n for n in archive.namelist() if n.lower().endswith(".nuspec")]
        if not names:
            return None
        text = archive.read(names[0]).decode("utf-8")
    metadata = re.search(r"<metadata\b[^>]*>(.*?)</metadata>", text, re.S)
    scope = re.sub(r"<dependencies\b.*?</dependencies>", "", metadata.group(1) if metadata else text, flags=re.S)
    match = re.search(r"<version>([^<]+)</version>", scope)
    if match:
        return match.group(1).strip()
    match = re.search(r"<package\b[^>]*\bversion=\"([^\"]+)\"", text)
    return match.group(1).strip() if match else None


def wheel_version(wheel: Path) -> Optional[str]:
    with zipfile.ZipFile(wheel) as archive:
        names = [n for n in archive.namelist() if n.endswith(".dist-info/METADATA")]
        if not names:
            return None
        for line in archive.read(names[0]).decode("utf-8").splitlines():
            if line.startswith("Version:"):
                return line.split(":", 1)[1].strip()
    return None


#: The C# package's three MSBuild identity properties. `PackageId` is the ONE place in this tree that
#: deliberately does NOT carry the generation token (R15, docs/STABILITY.md section 4.5): it is the id
#: NuGet consumers put in their `PackageReference`, so renaming it breaks THEM for no ABI reason.
#: Pinned to exact values here instead of living only as policy prose: a rule nobody can run is a rule
#: that drifts, and until this check existed NO gate looked at these three properties at all (2026-10-20,
#: audit round 3 H28). Changing one is still allowed -- as a reviewed edit of this table together with
#: the csproj and the package docs, in one commit.
CSHARP_IDENTITY = {"AssemblyName": "CppPinyin2", "RootNamespace": "CppPinyin2", "PackageId": "CppPinyin"}


def check_csharp_identity(root: Path, report: Report) -> None:
    """The ONE csproj that declares a `<PackageId>` must carry the frozen identity values.

    Scoped by `<PackageId>` on purpose: `csharp/CppPinyin.ConsumerSmoke` and `csharp/tools/minirunner`
    are test/fixture projects with identities of their own, and freezing THEIR AssemblyName would be
    inventing a rule nobody wants. The package is the project a consumer installs by id, and that id is
    the thing R15 talks about.

    Finding NO such project is a failure, not a note: the real tree has exactly one, and a check that
    quietly finds nothing is the "green run that measured nothing" this file keeps warning about.
    """
    packages = []
    for path in sorted((root / "csharp").rglob("*.csproj")):
        text = path.read_text(encoding="utf-8", errors="replace")
        declared = {}
        for key in CSHARP_IDENTITY:
            match = re.search(rf"<{key}>([^<]+)</{key}>", text)
            if match:
                declared[key] = match.group(1).strip()
        if "PackageId" in declared:
            packages.append((path, declared))
    if not packages:
        report.fail("no .csproj declares a <PackageId>, so the package identity (`PackageId` / "
                    "`AssemblyName` / `RootNamespace`) is unchecked -- that must not pass silently")
        return
    if len(packages) > 1:
        names = ", ".join(p.relative_to(root).as_posix() for p, _ in packages)
        report.fail(f"more than one project declares a <PackageId> ({names}); this check freezes ONE "
                    f"shipping package and cannot tell which of them is it")
        return
    path, declared = packages[0]
    relative = path.relative_to(root).as_posix()
    for key, want in CSHARP_IDENTITY.items():
        if key not in declared:
            report.fail(f"{relative} declares no <{key}>; the frozen identity needs all three")
        elif declared[key] != want:
            report.fail(f"{relative} sets <{key}>{declared[key]}</{key}> but the frozen identity is "
                        f"'{want}' (R15 exception, docs/STABILITY.md section 4.5)")
    report.note(f"C# package identity: {relative} carries PackageId={declared.get('PackageId')}, "
                f"AssemblyName={declared.get('AssemblyName')}, RootNamespace={declared.get('RootNamespace')}")


def check_version_chain(root: Path, nupkg: Optional[Path], wheel: Optional[Path], report: Report,                        tag: Optional[str] = None) -> None:
    expected = cmake_version(root)
    if expected is None:
        report.fail("CMakeLists.txt has no 'project(cpp-pinyin2 VERSION x.y.z)' line")
        return
    report.note(f"version: CMakeLists.txt says {expected}")

    props = root / "csharp" / "CppPinyin" / "version.props"
    if props.is_file():
        match = re.search(r"<Version>([^<]+)</Version>", props.read_text(encoding="utf-8"))
        if not match:
            report.fail("csharp/CppPinyin/version.props has no <Version> element")
        elif match.group(1).strip() != expected:
            report.fail(f"version.props says {match.group(1).strip()} but CMakeLists.txt says {expected} (section 6.8.5 place 5)")
    else:
        # ABSENT IS A FAILURE (2026-10-20, audit round 2 §12.6 C2). This used to be a `note`, and the file
        # it guards is in .gitignore -- so every run that did not first execute `csharp/tools/emit_build_info.py`
        # (the C++ CI's ctest leg does not) passed the version chain with place 5 unverified. Worse, the
        # runs that DO generate it compare a file that was itself derived from CMakeLists.txt, which makes
        # place 5 an identity check: it can only ever agree. A chain whose link is allowed to be missing is
        # not a chain, so this is now red and names the command that writes the file.
        report.fail("csharp/CppPinyin/version.props is missing, so version-chain place 5 cannot be checked "
                    "-- generate it with `python csharp/tools/emit_build_info.py` (it is gitignored, so a "
                    "fresh clone has to run that before this gate means anything)")

    if nupkg is not None:
        found = nuspec_version(nupkg)
        if found != expected:
            report.fail(f"nupkg nuspec says {found} but CMakeLists.txt says {expected} (section 6.8.5 place 6)")
        else:
            report.note(f"nupkg nuspec agrees: {found}")

    if wheel is not None:
        found = wheel_version(wheel)
        if found != expected:
            report.fail(f"wheel METADATA says {found} but CMakeLists.txt says {expected} (section 6.8.5 place 4)")
        else:
            report.note(f"wheel METADATA agrees: {found}")

    if tag is not None:
        accepted = (f"v{expected}", expected)
        if tag not in accepted:
            report.fail(
                f"git tag says {tag} but CMakeLists.txt says {expected} (section 6.8.5 place 1b): "
                f"a release tag may be {accepted[0]} or {accepted[1]} and nothing else"
            )
        else:
            report.note(f"git tag agrees: {tag}")


DICT_MEMBER_PREFIX = "contentFiles/any/any/data/dict/"

# A native asset has to be the right kind of file for the runtime identifier it sits under. Without this,
# a reviewer put a .so under runtimes/win-x64/native/ and the package passed: "there is a file in the native
# folder" was the whole question being asked.
RID_SUFFIX = {"win": ".dll", "linux": ".so", "osx": ".dylib", "darwin": ".dylib"}


PROJECT_TO_PACK = Path("csharp") / "CppPinyin" / "CppPinyin.csproj"


def managed_assembly_name(root: Path) -> str:
    """The managed assembly name the package must carry, read from the project that packs it.

    Hardcoding this was a real defect (2026-10-21): the project sets `<AssemblyName>CppPinyin2</AssemblyName>`
    while `<PackageId>` stays `CppPinyin`, so a CORRECTLY packed .nupkg failed this gate, and the self-test's
    fixtures had the same hardcoded name -- meaning no test could see the two drift apart. Reading the project
    keeps ONE copy of the fact.

    The fallbacks are MSBuild's own order rather than a guess: `AssemblyName` defaults to the project file's
    stem, and `PackageId` defaults to `AssemblyName`. If the project ever becomes unreadable, say so loudly --
    silently falling back to a name this gate made up is how the original bug behaved.
    """
    project = root / PROJECT_TO_PACK
    try:
        source = project.read_text(encoding="utf-8")
    except OSError:
        return "CppPinyin"  # keep the gate usable without the project; the note below records it
    for element in ("AssemblyName", "PackageId"):
        match = re.search(rf"<{element}>\s*([^<\s]+)\s*</{element}>", source)
        if match:
            return match.group(1)
    return project.stem


def check_packaging(root: Path, nupkg: Path, report: Report, rid: Optional[str] = None) -> None:
    with zipfile.ZipFile(nupkg) as archive:
        names = archive.namelist()
        native = [n for n in names if n.startswith("runtimes/") and "/native/" in n]
        native_size = {n: archive.getinfo(n).file_size for n in native}
        assembly = managed_assembly_name(root)
        report.note(f"managed assembly name (from {PROJECT_TO_PACK.as_posix()}): {assembly}")
        managed = [n for n in names if re.match(rf"lib/[^/]+/{re.escape(assembly)}\.dll$", n)]
        reference = [n for n in names if re.match(rf"ref/[^/]+/{re.escape(assembly)}\.dll$", n)]
        reference_xml = [n for n in names if re.match(rf"ref/[^/]+/{re.escape(assembly)}\.xml$", n)]
        # Every member of the dictionary tree, not "*.txt": selecting by extension is how a manifest with a
        # different one came to be invisible to this check while both packages really did lack it.
        dicts = [n for n in names if n.startswith(DICT_MEMBER_PREFIX)]

    on_disk = {d.name for d in (root / "data" / "dialects").iterdir() if d.is_dir()} \
        if (root / "data" / "dialects").is_dir() else set()

    if not native:
        report.fail("the nupkg has no runtimes/<rid>/native/ entry: it would install and then fail to load the library")
    else:
        report.note("nupkg native assets: " + ", ".join(native))
    for member in native:
        # member_rid, not rid: this loop used to bind `rid`, which is now also the parameter naming the
        # runtime identifier the package was packed for. While they shared a name, --rid compared the
        # package's own rid with itself and every value of --rid passed -- the gate ran and proved nothing.
        member_rid = member.split("/")[1]
        expected = RID_SUFFIX.get(member_rid.split("-")[0])
        if expected and not member.endswith(expected):
            report.fail(f"the nupkg's {member} is under a {member_rid} runtime but is not a {expected} file: "
                        f"a consumer on that rid would install a library it cannot load")
        if native_size.get(member, 0) == 0:
            report.fail(f"the nupkg's {member} is empty: presence alone is not a library")
    # The runtime identifier is a pack-time parameter (CppPinyinRid), so the package cannot state which rid
    # it is: the workflow knows and passes it in. Without this, a package whose native asset is for another
    # platform passed every check in this file -- the extension rule only asks whether the file suits the
    # directory it happens to sit in, never whether that directory is the one the build targeted.
    if rid is not None:
        carried = sorted({member.split("/")[1] for member in native})
        if rid.split("-")[0] not in RID_SUFFIX:
            report.fail(f"--rid {rid!r} is not a runtime identifier this check knows: expected one of "
                        f"{sorted({k + '-*' for k in RID_SUFFIX})}")
        elif carried != [rid]:
            report.fail(f"the package carries native assets for {carried}, but it was asked to be checked "
                        f"as {rid}: a package is packed per runtime identifier, so this is either the wrong "
                        f"build or the wrong check")
    # The licence expression names three licences; the file that says WHICH material falls under WHICH must be in
    # the package, or the expression is a summary with no way to check it (2026-10-21).
    if "THIRD_PARTY_NOTICES.md" not in names:
        report.fail("the nupkg carries no THIRD_PARTY_NOTICES.md: its licence expression names three licences and "
                    "nothing in the package says which material falls under which")
    else:
        report.note("nupkg carries THIRD_PARTY_NOTICES.md next to the licence expression")

    if not managed:
        report.fail(f"the nupkg has no lib/<tfm>/{assembly}.dll: a PackageReference consumer has nothing to load")
    else:
        report.note("nupkg managed assemblies: " + ", ".join(managed))
    # ref/ is required (decision D40 = B) and NOT because NuGet asks for it: measured, pack with NU5131
    # allowed through really does place these files in the package, and a PackageReference consumer then
    # resolves compile -> ref/<tfm>/CppPinyin.dll and runtime -> lib/<tfm>/CppPinyin.dll (read out of a
    # real project.assets.json). That warning is suppressed in the project file as a false positive, so
    # this check is what keeps the requirement honest.
    if not reference:
        report.fail(f"the nupkg has no ref/<tfm>/{assembly}.dll: consumers would compile against the "
                    f"runtime assembly instead of the reference assembly (D40 = B asks for ref/)")
    elif not reference_xml:
        report.fail(f"the nupkg has ref/<tfm>/{assembly}.dll but no ref/<tfm>/{assembly}.xml: a reference "
                    f"group expects the documentation file next to it")
    else:
        report.note("nupkg reference assemblies: " + ", ".join(reference))
    # "some ref/ exists" was the old question, and a package with three lib/ frameworks and a ref/ for one
    # of them passed it. D40 = B is about a reference group: every framework that gets a lib/ assembly needs
    # a ref/ one, or that framework's consumers compile against the runtime assembly.
    lib_tfms = {n.split("/")[1] for n in managed}
    ref_tfms = {n.split("/")[1] for n in reference}
    if reference and (lib_tfms - ref_tfms):
        report.fail(f"the nupkg ships lib/ for {sorted(lib_tfms)} but ref/ for {sorted(ref_tfms)} only: "
                    f"{sorted(lib_tfms - ref_tfms)} would compile against the runtime assembly")
    if not dicts:
        # An empty dictionary face used to be accepted here with a note ("callers must pass DictDirectory"),
        # and a reviewer fed this function a package with no dictionary members at all: both comparisons
        # below were skipped and it passed. That note is about the C++ entry point, not about what a package
        # should contain, and this repository does have dialects to ship -- so the two cases are now told
        # apart: nothing on disk to ship is a configuration, nothing packed while the tree has data is a
        # packaging regression that installs and then cannot answer a query.
        if on_disk:
            report.fail("the nupkg carries no dictionary copy at all under "
                        f"{DICT_MEMBER_PREFIX}: the tree has {sorted(on_disk)} to ship, so this package "
                        "installs and cannot answer a single query")
        else:
            report.note("the nupkg carries no dictionary copy (contentFiles): callers must pass DictDirectory")

    # parts[4] is "dict" in contentFiles/any/any/data/dict/<dialect>/<file>; it used to be compared with
    # "any", which is true of no member, so this comparison never ran and the note below never printed.
    # Members are required to sit at exactly <dialect>/<file>: a reviewer showed that one level of extra
    # nesting makes this function report "packaged dictionaries ['cn'] do not match data/dialects", which
    # points at the dialect names when the problem is the depth.
    strays = sorted(n for n in dicts if len(n[len(DICT_MEMBER_PREFIX):].split("/")) != 2)
    if strays:
        report.fail(f"{len(strays)} dictionary member(s) are not <dialect>/<file>, starting with "
                    f"{strays[0]!r}: the packaged dictionary tree is not the shape this check compares with")
    packaged = {n.split("/")[5] for n in dicts if len(n.split("/")) > 6 and n.split("/")[4] == "dict"}
    if packaged:
        if packaged != on_disk:
            report.fail(
                f"the nupkg's packaged dictionaries {sorted(packaged)} do not match data/dialects {sorted(on_disk)}: "
                "a package must not ship fewer dialects than the engine has"
            )
        else:
            report.note(f"packaged dialects match data/dialects: {sorted(packaged)}")

    # The dialect ids agreeing is not enough, and for a while it was not even asked. On 2026-10-02 both
    # packages carried every dialect's four tables and license while the manifest declaring those roles was
    # staged by build_native.py and never packed, and nothing failed. So each packaged dialect is compared
    # with the tree's own files, missing and extra both -- the rule check_wheel.py already applies to the
    # wheel as a whole, applied here to the one artifact that has no other member-level checker.
    for dialect in sorted(packaged):
        source = root / "data" / "dialects" / dialect
        on_disk = {entry.name for entry in source.iterdir() if entry.is_file()} if source.is_dir() else set()
        in_package = {parts[6] for parts in (n.split("/") for n in dicts)
                      if len(parts) > 6 and parts[5] == dialect}
        missing = sorted(on_disk - in_package)
        extra = sorted(in_package - on_disk)
        if missing or extra:
            report.fail(
                f"the packaged copy of {dialect} does not match data/dialects/{dialect}: "
                f"missing {missing or 'none'}, extra {extra or 'none'}"
            )
        else:
            report.note(f"packaged {dialect}: {len(in_package)} files match data/dialects/{dialect}")


def workflow_jobs(text: str) -> List[Tuple[str, str]]:
    """Split a workflow's text into (job id, job body) pairs, using indentation only.

    Deliberately not a YAML parser: this script has no third-party dependencies, and the questions being
    asked here (which jobs call a reusable workflow, what the aggregate job needs) do not need a real
    one. A job id is a two-space-indented key inside the jobs block; anything deeper belongs to a job.
    """
    _, separator, jobs_text = text.partition("\njobs:")
    if not separator:
        return []
    blocks: List[Tuple[str, str]] = []
    current: Optional[List[str]] = None
    for line in jobs_text.splitlines():
        match = re.match(r"^  ([A-Za-z0-9_-]+):\s*$", line)
        if match:
            if current is not None:
                blocks.append((current[0], "\n".join(current[1:])))
            current = [match.group(1)]
        elif current is not None:
            current.append(line)
    if current is not None:
        blocks.append((current[0], "\n".join(current[1:])))
    return blocks


def check_workflow_wiring(root: Path, report: Report) -> None:
    """Check the calls, the aggregate's needs, and the aggregate's if: always (see the header)."""
    workflow_dir = root / ".github" / "workflows"
    files = sorted(workflow_dir.glob("*.yml"))
    if not files:
        report.fail(f"workflow wiring: no workflows found under {workflow_dir} (expected at least the scheduler)")
        return

    for workflow in files:
        text = effective_lines(workflow.read_text(encoding="utf-8"))
        on_block, separator, _ = text.partition("\njobs:")
        if not separator:
            report.fail(f"workflow wiring: {workflow.name} has no jobs block")
            continue
        trigger_lines = [line for line in on_block.splitlines() if re.match(r"^  \S", line)]
        if not trigger_lines:
            report.fail(f"workflow wiring: {workflow.name} declares no trigger at all")

        targets = sorted(set(re.findall(r"uses:\s*\./\.github/workflows/([\w.\-]+)", text)))
        for target in targets:
            called = workflow_dir / target
            if not called.is_file():
                report.fail(f"workflow wiring: {workflow.name} calls {target}, which does not exist")
                continue
            called_text = called.read_text(encoding="utf-8")
            head, separator, _ = called_text.partition("\njobs:")
            if not separator or "workflow_call:" not in head:
                report.fail(
                    f"workflow wiring: {workflow.name} calls {target}, but {target} does not declare "
                    f"workflow_call in its on block"
                )

        blocks = workflow_jobs(text)
        job_ids = {job_id for job_id, _ in blocks}
        callers = {job for job, body in blocks if "./.github/workflows/" in body}
        for job, body in blocks:
            needs: Optional[set] = None
            inline = re.search(r"^    needs:\s*\[(.*?)\]\s*$", body, re.M)
            if inline:
                needs = {item.strip() for item in inline.group(1).split(",") if item.strip()}
            elif re.search(r"^    needs:\s*$", body, re.M):
                listed = re.findall(r"^      - (\S+)\s*$", body, re.M)
                needs = set(listed)
            if needs is None:
                continue
            missing = sorted(callers - needs)
            if missing:
                report.fail(
                    f"workflow wiring: {workflow.name} job '{job}' aggregates other jobs but does not need "
                    f"{missing}, so the required check would report green without them"
                )
            # A job that needs a job which is not in this file cannot run at all, and it is also how a
            # commented-out job used to hide: a reviewer commented the `csharp:` job out while the aggregate
            # still listed it, and this check stayed green because it only asked whether every caller was
            # needed, never whether every needed job existed.
            ghosts = sorted(name for name in needs if name not in job_ids)
            if ghosts:
                report.fail(
                    f"workflow wiring: {workflow.name} job '{job}' needs {ghosts}, which "
                    f"{'is' if len(ghosts) == 1 else 'are'} not a job of this file: a renamed or "
                    f"commented-out job leaves the aggregate green while running nothing"
                )
            if not re.search(r"^    if:\s*always\(\)", body, re.M):
                report.fail(
                    f"workflow wiring: {workflow.name} job '{job}' needs other jobs but does not use "
                    f"'if: always()', so it skips when one of them fails and never reports a verdict"
                )
        report.note(f"workflow wiring checked: {workflow.name} ({len(blocks)} job(s), {len(targets)} called workflow(s))")


# What a language workflow exists to do, as (workflow file, fragment) pairs. The fragments are command
# names rather than whole steps, so the wording around them stays free. This table exists because the Python
# job built a wheel, installed it and checked it member by member, and never once asked check_bindings.py
# about it -- the check existed and the C# job ran it for the other artifact. A check in the repository and
# a check wired into CI are different things, and only the second one runs when nobody is looking.
REQUIRED_ARTIFACT_STEPS = (
    ("test_python.yml", "python/tools/build_native.py"),
    ("test_python.yml", "python/tools/check_wheel.py"),
    ("test_python.yml", "python/tools/smoke_installed.py"),
    ("test_python.yml", "tools/check_bindings.py --wheel"),
    ("test_csharp.yml", "tools/check_bindings.py --nupkg"),
    ("test_csharp.yml", "CppPinyin.ConsumerSmoke"),
)


def effective_lines(text: str) -> str:
    """A workflow's text without its comments, keeping every line boundary intact.

    Comments are why this exists. A step that has been commented out is still a string in the file, and a
    check that asks "is this command in the file" answers yes: a reviewer commented out every required
    command and the artifact gate stayed green, and did the same to a job in the wiring check. Comments
    inside a ``run:`` block (shell comments) go too, which is right: that line does not run either.
    """
    return "\n".join(line for line in text.splitlines() if not line.lstrip().startswith("#"))


def fragment_runs(text: str, fragment: str) -> bool:
    """True when the fragment appears as a whole token on a line that is not a comment.

    The token boundary matters as much as the comment rule. "tools/check_bindings.py --wheel" is a prefix of
    "tools/check_bindings.py --wheel-DISABLED", so the first negative control written for this gate changed
    the file and left the searched-for property untouched. Both shapes were measured -- one by me, one by a
    reviewer -- so both are fixtures now.
    """
    pattern = re.compile(re.escape(fragment) + r"(?![\w.\-])")
    return any(pattern.search(line) for line in effective_lines(text).splitlines())


def check_artifact_wiring(root: Path, report: Report) -> None:
    """Every artifact step the language workflows are supposed to run is still there, and still runs.

    Deleting one during a refactor leaves the job green and the artifact unchecked, which is the same shape
    as the gates the rest of this file guards: a verdict nobody can fail is not a verdict. What counts as
    "there" is a non-comment line holding the command as a whole token, because the weaker test -- the string
    appears somewhere in the file -- is satisfied by the header comment that documents the command.
    """
    workflow_dir = root / ".github" / "workflows"
    seen = set()
    for name, fragment in REQUIRED_ARTIFACT_STEPS:
        seen.add(name)
        path = workflow_dir / name
        if not path.is_file():
            report.fail(f"artifact wiring: {name} is missing, so {fragment!r} runs nowhere")
        elif not fragment_runs(path.read_text(encoding="utf-8"), fragment):
            report.fail(
                f"artifact wiring: {name} does not run {fragment!r} as an executable command -- the step is "
                f"gone, commented out, or renamed, while the job it belongs to still reports green"
            )
    # Deliberately "checked", not "present": this line counts the table's entries, and it used to say they
    # were present even on the runs where the failures above said otherwise. A check that reassures while
    # failing is the shape this whole file is written against.
    report.note(f"artifact wiring: checked {len(REQUIRED_ARTIFACT_STEPS)} required step(s) across "
                f"{len(seen)} workflow(s)")


def supported_dict_format(root: Path) -> Optional[int]:
    """The data format this tree's engine accepts, read from its ONE definition (`kSupportedDictFormat`).

    Parsed instead of hard-coded so the check cannot drift from the engine: a literal here would be a third
    copy of a number that already has two owners (src/Engine.cpp and the shipped data).
    """
    engine = root / "src" / "Engine.cpp"
    if not engine.is_file():
        return None
    match = re.search(r"constexpr int kSupportedDictFormat = (\d+);",
                      engine.read_text(encoding="utf-8", errors="replace"))
    return int(match.group(1)) if match else None


def check_dialect_surface(root: Path, wheel: Optional[Path], report: Report) -> None:
    """Only Mandarin and Cantonese carry data in this repository, and the build must promise the same two.

    Scope matters here: this check looks at the DATA surface and at the declared whitelist, never at prose.
    The plugin documentation uses Hokkien and Teochew as examples on purpose (docs/Custom Chinese Dialect.md
    is the guide for adding one), so a text-wide hunt for language names would be nothing but false
    positives. What can be verified mechanically is which dialects have data in the tree, which ones the
    packaging script claims to ship, and what each dialect's own manifest says its files are for.
    """
    data = root / "data" / "dialects"
    script = root / "python" / "tools" / "build_native.py"
    if not data.is_dir():
        report.fail("no data/dialects directory: the two dialects have no data at all")
        return
    found = sorted(entry.name for entry in data.iterdir() if entry.is_dir())

    declared: Optional[List[str]] = None
    if script.is_file():
        match = re.search(r'^DIALECTS\s*=\s*\(([^)]*)\)', script.read_text(encoding="utf-8"), re.M)
        if match:
            declared = sorted(re.findall(r'"([^"]+)"', match.group(1)))
    if declared is None:
        report.fail(
            "python/tools/build_native.py declares no DIALECTS whitelist: the set of dialects this "
            "repository ships is then not stated anywhere a check can read"
        )
    elif declared != found:
        report.fail(
            f"data/dialects holds {found} while build_native.py's whitelist declares {declared}: the data and "
            f"the declaration must agree, and this repository tracks Mandarin and Cantonese only"
        )
    else:
        report.note("dialect surface: data/dialects and the build whitelist agree on " + ", ".join(found))

    loose = sorted(entry.name for entry in data.iterdir() if not entry.is_dir())
    if loose:
        report.fail("data/dialects holds loose files that belong to no dialect: " + ", ".join(loose))

    # The manifests. Each dialect states its own file roles, so "this dialect's data is complete" stops being
    # a claim about a directory listing and becomes something a check reads -- and a file that no role claims
    # is an error rather than a quiet passenger. The three role sets are frozen HERE because the engine loads
    # exactly these four tables (src/EngineImpl.cpp:43-46) and ships exactly these assets; a manifest that
    # declared a table the engine never reads would be a declaration without a reader, which is the shape
    # this repository keeps having to remove. Adding a table therefore means changing the loader, the
    # packaging whitelist and this set in one commit.
    expected_tables = {"word.txt", "phrases_dict.txt", "phrases_map.txt", "trans_word.txt"}
    expected_optional = {"user_dict.txt"}
    expected_assets = {"License.txt", "format.txt"}

    packaged_tables: Optional[set] = None
    if script.is_file():
        match = re.search(r'^REQUIRED_TABLES\s*=\s*\(([^)]*)\)', script.read_text(encoding="utf-8"), re.M)
        if match:
            packaged_tables = set(re.findall(r'"([^"]+)"', match.group(1)))
    if packaged_tables != expected_tables:
        report.fail(
            "python/tools/build_native.py's REQUIRED_TABLES does not match the tables the engine loads: "
            f"{sorted(packaged_tables or [])} vs {sorted(expected_tables)}"
        )

    for name in found:
        manifest = data / name / "dialect.json"
        if not manifest.is_file():
            report.fail(
                f"data/dialects/{name} has no dialect.json: the roles of its files are then stated nowhere, "
                "so a half-copied dictionary cannot be told from a complete one"
            )
            continue
        try:
            declared_manifest = json.loads(manifest.read_text(encoding="utf-8"))
        except ValueError as error:
            report.fail(f"data/dialects/{name}/dialect.json is not valid JSON: {error}")
            continue
        if not isinstance(declared_manifest, dict):
            report.fail(f"data/dialects/{name}/dialect.json is not a JSON object")
            continue

        problems: List[str] = []
        if declared_manifest.get("id") != name:
            problems.append(f"id is {declared_manifest.get('id')!r}, not the directory name {name!r}")
        for key, expected in (("required_tables", expected_tables),
                              ("optional_tables", expected_optional),
                              ("assets", expected_assets)):
            declared_roles = declared_manifest.get(key)
            if not isinstance(declared_roles, list):
                problems.append(f"{key} is missing or not a list")
            elif set(declared_roles) != expected:
                problems.append(f"{key} is {sorted(declared_roles)}, not {sorted(expected)}")

        # THE MARKER'S VALUE, NOT JUST ITS PRESENCE (2026-10-20 audit round 2, cross-repo E-series). The
        # number in format.txt is written by the generator in pinyin-makedict (`common.DATA_FORMAT_VERSION`)
        # and compared here by `kSupportedDictFormat` (src/Engine.cpp). Those are two literals in two
        # repositories, so this is the only place that can notice the shipped data and this tree's engine
        # disagreeing -- e.g. data regenerated for format 3 while the engine still accepts 2, which would
        # fail for every consumer and for no one in either repository's own test suite.
        marker = data / name / "format.txt"
        if marker.is_file():
            shipped = marker.read_text(encoding="utf-8", errors="replace").strip()
            supported = supported_dict_format(root)
            # An EMPTY marker is out of scope here on purpose: whether a directory may carry a marker that
            # says nothing is the engine's question (src/Engine.cpp treats it as "not a number") and the
            # asset-role check above already asserts that the file exists. This check is about the VALUE.
            if shipped and supported is None:
                problems.append(
                    "src/Engine.cpp declares no parseable `constexpr int kSupportedDictFormat`, so the "
                    "shipped format.txt cannot be checked against the engine that will read it")
            elif shipped and shipped != str(supported):
                problems.append(
                    f"format.txt says {shipped!r} but this tree's engine accepts {supported} "
                    f"(src/Engine.cpp kSupportedDictFormat)")

        # D4 (docs/DECISIONS.md section 4.1, min_library): the engine compares this against its own
        # PROJECT_VERSION before reading a table, so a value it cannot parse is a starting failure rather than
        # a document about the old days. Every dialect that ships must answer the question.
        declared_min = declared_manifest.get("min_library")
        if not isinstance(declared_min, str) or not re.fullmatch(r"\d+\.\d+\.\d+", declared_min):
            problems.append(
                f"min_library is {declared_min!r}, not a version like \"2.0.0\": the engine refuses data written "
                "for a newer library, and needs a value it can compare against its own version")

        present = {entry.name for entry in (data / name).iterdir() if entry.is_file()}
        allowed = expected_tables | expected_optional | expected_assets | {"dialect.json"}
        unlisted = sorted(present - allowed)
        missing = sorted((expected_tables | expected_assets) - present)
        if unlisted:
            problems.append("holds files that belong to no declared role: " + ", ".join(unlisted))
        if missing:
            problems.append("is missing files it declares: " + ", ".join(missing))

        if problems:
            report.fail(f"data/dialects/{name}/dialect.json disagrees with the directory: " + "; ".join(problems))
        else:
            report.note(f"dialect {name}: manifest and directory agree ({len(expected_tables)} tables + "
                        f"{len(expected_optional)} optional + {len(expected_assets)} asset)")

    if wheel is not None and wheel.is_file():
        with zipfile.ZipFile(wheel) as archive:
            packaged = sorted({
                parts[3]
                for parts in (name.split("/") for name in archive.namelist())
                if len(parts) > 3 and parts[:3] == ["cpp_pinyin2", "data", "dict"]
            })
        if packaged and packaged != found:
            report.fail(f"the wheel packages {packaged} while the tree holds {found}")
        elif packaged:
            report.note("dialects packaged in the wheel: " + ", ".join(packaged))


PLUGIN_SURFACE_HEADERS = ("cpp-pinyin2/ext/Dialect.h", "cpp-pinyin2/ext/Registry.h")


def plugin_surface(root: Path) -> List[str]:
    """The declarations a plugin author writes code against, in file order.

    The third frozen surface, and until 2026-10-02 the only one nothing read. The C ABI has
    tests/abi/expected_c_api_symbols.txt, the header SET has tests/abi/expected_public_headers.txt, and the
    C++ surface a plugin actually compiles against had no list at all: measured on the real tree, adding a
    member to `Dialect` changed no gate and the whole suite still passed. That is the surface Refinement A
    promised to keep stable, so it is stated in one place a check can read.

    Order is part of it, not an accident of the extractor: `Dialect` is an aggregate, so reordering its
    members changes the layout every plugin was compiled against while every other check stays green.

    Declaration level only, and deliberately: this is the set of things a plugin can name, not the ABI of
    any particular compiler. Layout beyond member order is not something this repository can promise.
    """
    surface: List[str] = []
    for relative in PLUGIN_SURFACE_HEADERS:
        path = root / "include" / relative
        if not path.is_file():
            continue
        # Not effective_lines() here: that helper drops every '#' line, which is right for the YAML and
        # shell it was written for and wrong here -- it silently swallowed `#define
        # CPP_PINYIN_REGISTER_DIALECT`, a load-bearing part of the plugin surface. The first version of
        # this extractor produced 15 declarations and looked complete.
        in_private = False
        for raw in path.read_text(encoding="utf-8").splitlines():
            if raw.strip().startswith("//"):
                continue
            line = " ".join(raw.split())
            if not line:
                continue
            if line.startswith("private:"):
                in_private = True
                continue
            if line.startswith("public:"):
                in_private = False
                continue
            # The macro rule comes first on purpose: `in_private` is sticky (there is no second
            # `public:` to reset it), and the macro lives after the class, so checking it second meant
            # the registration macro -- the one line every plugin is written around -- was skipped.
            if line.startswith("#define CPP_PINYIN_REGISTER_DIALECT"):
                surface.append(f"{relative}: #define CPP_PINYIN_REGISTER_DIALECT(id, creator)")
                continue
            if in_private:
                continue
            # A trailing comment is not part of the declaration, and leaving it in made the extraction
            # blind to declarations written that way: a probe line ending in `// PROBE` was invisible, so
            # a mutated tree still passed. Comments inside the declaration are kept as written.
            code = line.split("//", 1)[0].strip() if "//" in line else line
            if not code.endswith(";"):
                continue
            if code.startswith(("#", "}", "using namespace")):
                continue
            surface.append(f"{relative}: {code}")
    return surface


def check_plugin_surface(root: Path, report: Report) -> None:
    """Compares the plugin surface with the frozen list, both ways and in order."""
    baseline = root / "tests" / "abi" / "expected_plugin_surface.txt"
    found = plugin_surface(root)
    if not found:
        report.fail("no plugin surface found: either include/cpp-pinyin2/ext/ is gone or the extractor "
                    "matched nothing. Both mean this check cannot say anything, so it refuses to pass")
        return
    if not baseline.is_file():
        report.fail(
            "tests/abi/expected_plugin_surface.txt is missing: the C++ surface a plugin compiles against "
            "is then stated nowhere a check can read, and a member can be added to `Dialect` unnoticed"
        )
        return
    expected = [line.strip() for line in baseline.read_text(encoding="utf-8").splitlines()
                if line.strip() and not line.lstrip().startswith("#")]
    if len(expected) < 8:
        report.fail(f"tests/abi/expected_plugin_surface.txt lists only {len(expected)} declaration(s): a "
                    f"list this short cannot describe the plugin surface, so this check refuses to pass")
        return
    added = [line for line in found if line not in expected]
    removed = [line for line in expected if line not in found]
    if added:
        report.fail("plugin surface the frozen list does not know: " + "; ".join(added) +
                    " -- a plugin surface change is a contract change, so update "
                    "tests/abi/expected_plugin_surface.txt in the same commit and record why")
    if removed:
        report.fail("frozen plugin declarations that are gone: " + "; ".join(removed) +
                    " -- every plugin compiled against them stops building")
    if not added and not removed and expected != found:
        report.fail("the plugin surface is unchanged as a set but reordered: `Dialect` is an aggregate, so "
                    "member order is layout, and plugins compiled against the old order read the wrong "
                    "fields rather than failing to build")
    if not added and not removed and expected == found:
        report.note(f"plugin surface matches the frozen list ({len(expected)} declarations)")


def check_public_header_surface(root: Path, report: Report) -> None:
    """The installed headers are a contract: both their set and their inward reach are checked.

    Two failure modes, both of which are invisible in this checkout and expensive outside it:

      * A header added, renamed or moved changes what every consumer includes. The frozen list keeps
        that deliberate -- the same reason the C ABI has tests/abi/expected_c_api_symbols.txt.
      * A public header that includes something under src/ compiles here (the build has that directory
        on its include path) and cannot compile from an installed tree, where src/ was never installed.

    The second is checked as "must not name src/ and must not reach upwards": every include in the
    public tree has to be a canonical ``<cpp-pinyin2/...>`` reference to a file that ships with it.
    """
    include_dir = root / "include" / "cpp-pinyin2"
    baseline = root / "tests" / "abi" / "expected_public_headers.txt"
    if not include_dir.is_dir():
        report.fail("no include/cpp-pinyin2 directory: the public headers are the installed surface")
        return

    headers = sorted(path.relative_to(root / "include").as_posix() for path in include_dir.rglob("*.h"))
    if not baseline.is_file():
        report.fail(
            "tests/abi/expected_public_headers.txt is missing: the set of public headers is then stated "
            "nowhere a check can read, and a new one can join the installed tree unnoticed"
        )
    else:
        expected = [line.strip() for line in baseline.read_text(encoding="utf-8").splitlines()
                    if line.strip() and not line.lstrip().startswith("#")]
        extra = sorted(set(headers) - set(expected))
        missing = sorted(set(expected) - set(headers))
        if extra:
            report.fail(
                "public headers the frozen list does not know " + ", ".join(extra) +
                " -- adding a public header is a contract change, so update "
                "tests/abi/expected_public_headers.txt in the same commit"
            )
        if missing:
            report.fail("frozen public headers that are gone: " + ", ".join(missing) +
                        " -- removing a public header breaks every consumer that includes it")
        if not extra and not missing:
            report.note(f"public header set matches the frozen list ({len(expected)} headers)")

    offenders: List[str] = []
    for path in sorted(include_dir.rglob("*.h")):
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
            text = line.strip()
            if not text.startswith("#include"):
                continue
            parts = text.split(None, 1)
            if len(parts) < 2:
                continue
            target = parts[1].replace("\\", "/")
            if "src/" in target or '"..' in target or "<.." in target:
                offenders.append(f"{path.relative_to(root).as_posix()}:{number} {parts[1]}")
    if offenders:
        report.fail(
            "public headers reach outside the installed tree (an installed header cannot include a file "
            "that is not installed, and this only fails for consumers): " + "; ".join(offenders)
        )
    else:
        report.note("public headers include only headers that ship with them")


def check_exported_declarations(root: Path, report: Report) -> None:
    """The header side of the ABI: every exported C declaration is in the frozen symbol list, and back.

    c_api.h states the position this enforces: the PACKAGE VERSION is not an ABI version and the visible
    SYMBOL SET is the contract (that header's "ABI STABILITY" and "visible SYMBOL SET" paragraphs).
    tests/abi/check_exports.cmake already compares the built
    library's exports against tests/abi/expected_c_api_symbols.txt, so the library side is covered; the
    hole this closes is the header side, where a declaration can be added a release before anyone notices
    that the list, the ABI document and the shells' baselines still describe the old surface.

    Only declarations that carry the export macro and look like functions are compared. The exported C++
    types (`Dialect`, `DialectRegistry`) are the plugin face and are held by the frozen header list
    instead -- see check_public_header_surface.
    """
    ext_dir = root / "include" / "cpp-pinyin2" / "ext"
    baseline = root / "tests" / "abi" / "expected_c_api_symbols.txt"
    if not baseline.is_file():
        report.fail("tests/abi/expected_c_api_symbols.txt is missing: the exported symbol set is then "
                    "stated nowhere a check can read")
        return

    # Every declaration in c_api.h starts with the export macro on the same line as the name (the same
    # rule tests/test_contract.py relies on), so a line scan is enough and does not need a C parser.
    declared: List[str] = []
    if ext_dir.is_dir():
        for path in sorted(ext_dir.rglob("*.h")):
            for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
                text = line.strip()
                if not text.startswith("CPP_PINYIN2_EXPORT"):
                    continue
                tail = text[len("CPP_PINYIN2_EXPORT"):]
                if "(" not in tail:
                    continue  # an exported type rather than a function
                words = tail.split("(", 1)[0].split()
                if words:
                    declared.append(words[-1].lstrip("*&"))
                    declared[-1] = f"{declared[-1]} at {path.relative_to(root).as_posix()}:{number}"

    expected: List[str] = []
    if baseline.is_file():
        for line in baseline.read_text(encoding="utf-8").splitlines():
            text = line.split("#", 1)[0].split("//", 1)[0].strip()
            if text:
                expected.append(text.split()[0].lstrip("*&"))

    declared_names = sorted({entry.split(" ", 1)[0] for entry in declared})
    missing = sorted(set(declared_names) - set(expected))
    ghosts = sorted(set(expected) - set(declared_names))
    if missing:
        report.fail(
            "exported declarations the frozen symbol list does not know: " + ", ".join(missing) +
            " -- a new C symbol changes the ABI for every binding, so the symbol list, the ABI document "
            "and the shells' baselines are updated in the same commit"
        )
    if ghosts:
        report.fail("frozen symbols no public header declares: " + ", ".join(ghosts) +
                    " -- the library may still export them, but no caller can be compiled against them")
    if not declared_names:
        report.fail("no exported declarations found under include/cpp-pinyin2/ext: either the scan is "
                    "looking in the wrong place or the C surface is gone, and a check that finds nothing "
                    "must not pass")
    elif not missing and not ghosts:
        report.note(f"exported declarations match the frozen symbol list ({len(expected)} symbols): " +
                    ", ".join(declared_names[:3]) + ", ...")


# ---------------------------------------------------------------------------------------------------------
# SIGNATURES (not just names). See the module docstring of the section above: the symbol list freezes the
# NAMES, and this freezes what a caller can compile against them.
# ---------------------------------------------------------------------------------------------------------

def normalize_parameter(parameter: str) -> str:
    """One parameter with its NAME removed and its spelling made canonical.

    `const char *text` and `const char* text` are the same declaration, so both become `const char*`. A
    parameter whose remaining text is empty after dropping the name would be a bare type with no name --
    legal in C, and kept as-is.
    """
    collapsed = " ".join(parameter.split())
    collapsed = collapsed.replace("( *", "(*").replace("* )", "*)")
    # Drop a trailing identifier -- but KEEP the pointers that are glued to it. `const char *text` splits into
    # ["const","char","*text"]: dropping the last token outright would drop the `*` with it and turn
    # `const char*` into `const char`, which is the one difference this check exists to catch.
    parts = collapsed.split(" ")
    if len(parts) > 1 and parts[-1] not in ("const", "void"):
        last = parts[-1]
        identifier = last.lstrip("*&")
        if identifier and not identifier.endswith("*"):
            pointers = last[: len(last) - len(identifier)]
            parts = parts[:-1]
            if pointers:
                parts[-1] = parts[-1] + pointers
    collapsed = " ".join(parts)
    # Canonicalise pointer spacing only: `const char *` -> `const char*`, `const char *const*` unchanged.
    collapsed = collapsed.replace(" *", "*")
    return collapsed


def parse_exported_signatures(root: Path) -> List[str]:
    """Every exported C declaration as `return name(type, type, ...)`, in sorted order.

    A declaration may span lines (one of them does), so the scan accumulates from the export macro to the
    terminating `;`, tracking parenthesis depth -- a `;` inside a parameter list is not the end.
    """
    ext_dir = root / "include" / "cpp-pinyin2" / "ext"
    signatures: List[str] = []
    if not ext_dir.is_dir():
        return signatures
    for path in sorted(ext_dir.rglob("*.h")):
        lines = path.read_text(encoding="utf-8").splitlines()
        index = 0
        while index < len(lines):
            stripped = lines[index].strip()
            if not stripped.startswith("CPP_PINYIN2_EXPORT"):
                index += 1
                continue
            declaration = ""
            depth = 0
            while index < len(lines):
                piece = lines[index].split("/*", 1)[0].split("//", 1)[0].strip()
                declaration += " " + piece
                depth += piece.count("(") - piece.count(")")
                index += 1
                if depth <= 0 and ";" in piece:
                    break
            declaration = declaration.split("CPP_PINYIN2_EXPORT", 1)[1].split(";", 1)[0].strip()
            if "(" not in declaration:
                continue  # an exported type rather than a function
            head, tail = declaration.split("(", 1)
            parameters, _ = tail.rsplit(")", 1) if ")" in tail else (tail, "")
            words = head.split()
            if not words:
                continue
            # The return type's pointers are glued to the function name (`const char *cppp2_code_text`), so
            # stripping them off the name without putting them back on the type would erase the difference
            # between `const char*` and `const char` -- see the note in normalize_parameter.
            name_token = words[-1]
            name = name_token.lstrip("*&")
            return_pointers = name_token[: len(name_token) - len(name)] if name else ""
            return_type = (" ".join(words[:-1]) + return_pointers).replace(" *", "*")
            normalized = [normalize_parameter(part) for part in _split_top_level(parameters)]
            normalized = [part for part in normalized if part and part != "void"]
            signatures.append(f"{return_type} {name}({', '.join(normalized)})")
    return sorted(signatures)


def _split_top_level(parameters: str) -> List[str]:
    """Split a parameter list on commas that are not inside parentheses."""
    parts: List[str] = []
    depth = 0
    current = ""
    for character in parameters:
        if character == "(":
            depth += 1
        elif character == ")":
            depth -= 1
        if character == "," and depth == 0:
            parts.append(current)
            current = ""
        else:
            current += character
    if current.strip():
        parts.append(current)
    return parts


SIGNATURE_BASELINE = Path("tests") / "abi" / "expected_c_api_signatures.v2.txt"


def write_signature_baseline(root: Path) -> int:
    """Write the baseline from the headers. The ONLY way this file is produced."""
    signatures = parse_exported_signatures(root)
    if not signatures:
        print("no exported declarations found -- refusing to write an empty baseline")
        return 1
    target = root / SIGNATURE_BASELINE
    body = (
        "# The C ABI's SIGNATURES: return type, name, parameter TYPES (no parameter names).\n"
        "#\n"
        "# Generated by `python tools/check_bindings.py --emit-signatures`; compared on every run.\n"
        "# A parameter NAME is deliberately absent: renaming one is invisible to a caller. A type, an\n"
        "# order or a count is not, and changing one fails the gate -- which is the point, because the\n"
        "# symbol list next door only freezes names.\n"
        "#\n"
    )
    body += "\n".join(signatures) + "\n"
    # LF only, and via open() rather than Path.write_text: `newline=` on write_text needs 3.10, and the board
    # runs 3.8 (the same reason the self-test spells its own writer out).
    target.parent.mkdir(parents=True, exist_ok=True)
    with open(target, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(body)
    print(f"wrote {target.relative_to(root).as_posix()}: {len(signatures)} signature(s)")
    return 0


def check_exported_signatures(root: Path, report: Report) -> None:
    """The TYPE side of the frozen C surface: signatures must match the baseline exactly."""
    baseline = root / SIGNATURE_BASELINE
    declared = parse_exported_signatures(root)
    if not declared:
        report.fail("no exported declarations found under include/cpp-pinyin2/ext: a signature check that "
                    "finds nothing must not pass")
        return
    if not baseline.is_file():
        report.fail(f"{SIGNATURE_BASELINE.as_posix()} is missing: the C ABI's signature set is then stated "
                    "nowhere a check can read (regenerate with --emit-signatures)")
        return

    expected = [line for line in baseline.read_text(encoding="utf-8").splitlines()
                if line.strip() and not line.lstrip().startswith("#")]
    by_name = {entry.split("(", 1)[0].split()[-1]: entry for entry in expected}
    declared_by_name = {entry.split("(", 1)[0].split()[-1]: entry for entry in declared}

    added = sorted(set(declared_by_name) - set(by_name))
    removed = sorted(set(by_name) - set(declared_by_name))
    changed = sorted(name for name in set(by_name) & set(declared_by_name)
                     if by_name[name] != declared_by_name[name])

    if added:
        report.fail("exported declarations the signature baseline does not know: " + ", ".join(added) +
                    " -- a new C entry point changes the ABI for every binding; add it and the shells' "
                    "baselines in the same commit")
    if removed:
        report.fail("signature baselines for symbols no header declares: " + ", ".join(removed))
    if changed:
        detail = "; ".join(f"{name}: {by_name[name]} -> {declared_by_name[name]}" for name in changed)
        report.fail("the C ABI's SIGNATURES changed, which the symbol list next door cannot see: " + detail +
                    " -- this is the half of the contract a caller compiles against")
    if not (added or removed or changed):
        report.note(f"the C ABI's signatures match the baseline ({len(expected)}): " +
                    ", ".join(sorted(by_name)[:2]) + ", ...")


# The C# shell's transcription of the C ABI: (struct in c_api.h, struct in NativeMethods.cs). Spelled out
# rather than discovered, because a struct that is silently MISSING from the shell is exactly the drift
# this check exists to catch, and a discovered pair list would quietly shrink along with it.
CSHARP_STRUCT_PAIRS = (
    ("cppp2_error", "ErrorRaw"),
    ("cppp2_query_opts", "QueryOptions"),
    ("cppp2_syllable", "SyllableRaw"),
    ("cppp2_segment", "SegmentRaw"),
    ("cppp2_style", "StyleRaw"),
    ("cppp2_result", "ResultRaw"),
    # Added 2026-10-20 (audit round 2 §12.2 API P1): the three switches end at 20 while `sizeof` is 24, and
    # both shells transcribed only the three switches -- the two structs then agreed by PADDING, not by
    # declaration. A future field appended into that hole would have moved every member after it with this
    # table still green, which is the drift the table exists to catch.
    ("cppp2_tone_sandhi_opts", "ToneSandhiOptsRaw"),
)

#: The Python shell's transcription of the same C structs: (struct in c_api.h, class in `_ffi.py`).
#: Same reasoning as CSHARP_STRUCT_PAIRS -- spelled out, never discovered, so a class deleted from the
#: shell cannot silently shrink the comparison. `cppp2_registry`/`cppp2_engine` are opaque handles in
#: both directions (the Python side models them as `ctypes.c_void_p`), so they are not pairs.
PYTHON_STRUCT_PAIRS = (
    ("cppp2_error", "Error"),
    ("cppp2_query_opts", "QueryOpts"),
    ("cppp2_syllable", "Syllable"),
    ("cppp2_segment", "Segment"),
    ("cppp2_style", "Style"),
    ("cppp2_result", "Result"),
    ("cppp2_tone_sandhi_opts", "ToneSandhiOpts"),
)


def c_struct_fields(text: str, name: str) -> Optional[List[str]]:
    """The field names of `typedef struct <name> { ... } <name>;`, in declaration order.

    Comments are removed before splitting on `;`, so a documented field is still seen, and a preprocessor
    line inside the body is dropped. Each declaration contributes its LAST identifier, which is the field
    name for every shape in this header (`const char *spelling`, `size_t count`, `int32_t reserved_0`).
    Returns None when the struct is absent: the caller decides whether that is a failure, because for it
    "absent" and "present but empty" are different findings.
    """
    match = re.search(r"typedef\s+struct\s+" + re.escape(name) + r"\s*\{", text)
    if not match:
        return None
    body = text[match.end():]
    end = re.search(r"\}\s*" + re.escape(name) + r"\s*;", body)
    if not end:
        return None
    body = re.sub(r"/\*.*?\*/", " ", body[:end.start()], flags=re.S)
    body = re.sub(r"//[^\n]*", " ", body)
    fields: List[str] = []
    for declaration in body.split(";"):
        declaration = declaration.strip()
        if not declaration or declaration.startswith("#"):
            continue
        words = re.findall(r"[A-Za-z_][A-Za-z0-9_]*", declaration)
        if words:
            fields.append(words[-1])
    return fields


def csharp_struct_fields(text: str, name: str) -> Optional[List[str]]:
    """The field names of `internal struct <name> { ... }` in NativeMethods.cs, in declaration order.

    Only `internal <type> <name>;` lines count, which is the shape every field in that file has; a
    `<summary>` block is skipped by its `///` prefix rather than by trying to parse XML.
    """
    match = re.search(r"internal\s+struct\s+" + re.escape(name) + r"\s*\{", text)
    if not match:
        return None
    body = text[match.end():]
    body = re.sub(r"/\*.*?\*/", " ", body, flags=re.S)
    end = body.find("}")
    if end >= 0:
        body = body[:end]
    fields: List[str] = []
    for line in body.splitlines():
        line = line.strip()
        if not line or line.startswith("//"):
            continue
        field = re.match(r"internal\s+[\w\.<>\[\]]+\s+(\w+)\s*;", line)
        if field:
            fields.append(field.group(1))
    return fields


def python_struct_fields(text: str, name: str) -> Optional[List[str]]:
    """The `_fields_` names of `class <name>(ctypes.Structure)` in `_ffi.py`, in declaration order.

    Each entry is a `("field", ctypes.type),` tuple; comments and blank lines are skipped, and a tuple
    spread over several lines is joined before the name is taken. Returns None when the class is absent,
    because for the caller "absent" and "present but empty" are different findings -- the same rule the
    C# side follows.
    """
    match = re.search(r"^class\s+" + re.escape(name) + r"\s*\(ctypes\.Structure\):", text, flags=re.M)
    if not match:
        return None
    body = text[match.end():]
    nxt = re.search(r"^class\s+\w+", body, flags=re.M)
    if nxt:
        body = body[:nxt.start()]
    fields_match = re.search(r"_fields_\s*=\s*\[(.*?)\]", body, flags=re.S)
    if not fields_match:
        return []
    body = re.sub(r"#.*", "", fields_match.group(1))
    names = re.findall(r"\(\s*[\"'](\w+)[\"']\s*,", body)
    return names


def check_csharp_baseline(root: Path, report: Report) -> None:
    """The C# shell's STATIC transcription of the C ABI: the entry points and the struct field order.

    c_api.h is the authority for both. The layouts are also asserted at runtime
    (csharp/CppPinyin.Tests/SmokeTests.cs compares Marshal.SizeOf and Marshal.OffsetOf), and that is the
    stronger statement -- but it only holds where the suite can run, and the suite needs a test host that
    parents a child process, which is not every developer machine and is not this sandbox. A drift that
    lands without anyone running them is silent in both shells, so this layer runs with the repository's
    other binding gates: no native build, no dotnet, no runtime.

    The symbol check closes the half the header-side check cannot see: a C symbol that the library
    exports, the frozen list knows about, and no C# caller can reach.
    """
    native = root / "csharp" / "CppPinyin" / "NativeMethods.cs"
    header = root / "include" / "cpp-pinyin2" / "ext" / "c_api.h"
    baseline = root / "tests" / "abi" / "expected_c_api_symbols.txt"
    for path, why in ((native, "the C# shell then declares nothing, and this check would compare two empty "
                               "sets and pass"),
                      (header, "the C ABI's own declarations are the authority this check reads"),
                      (baseline, "the frozen symbol set is then stated nowhere a check can read")):
        if not path.is_file():
            report.fail(f"{path.relative_to(root).as_posix()} is missing: {why}")
            return

    text = native.read_text(encoding="utf-8")
    entry_points = sorted(set(re.findall(r"internal\s+static\s+extern\s+[\w\*\[\]<>\.]+\s+(\w+)\s*\(", text)))
    expected = []
    for line in baseline.read_text(encoding="utf-8").splitlines():
        text_without_comment = line.split("#", 1)[0].split("//", 1)[0].strip()
        if text_without_comment:
            expected.append(text_without_comment.split()[0].lstrip("*&"))

    unreachable = sorted(set(expected) - set(entry_points))
    unknown = sorted(set(entry_points) - set(expected))
    if unreachable:
        report.fail("frozen C symbols the C# shell never declares (a binding gap nobody notices until a "
                    "caller needs it): " + ", ".join(unreachable))
    if unknown:
        report.fail("entry points in NativeMethods.cs that the frozen symbol list does not know: " +
                    ", ".join(unknown) + " -- the shell is transcribed from c_api.h, so this is either a "
                    "typo or a symbol list that lagged behind the header")
    if not entry_points:
        report.fail("no `internal static extern` entry points found in NativeMethods.cs: either the scan is "
                    "looking in the wrong place or the shell's imports changed shape, and a check that finds "
                    "nothing must not pass")

    header_text = header.read_text(encoding="utf-8")
    layout_problems: List[str] = []
    for c_name, cs_name in CSHARP_STRUCT_PAIRS:
        in_header = c_struct_fields(header_text, c_name)
        in_shell = csharp_struct_fields(text, cs_name)
        if in_header is None:
            layout_problems.append(f"{c_name} is not declared in c_api.h")
            continue
        if in_shell is None:
            layout_problems.append(f"{cs_name} is not declared in NativeMethods.cs")
            continue
        if not in_header or not in_shell:
            layout_problems.append(f"{c_name}/{cs_name} parsed to no fields at all")
            continue
        if in_header != in_shell:
            layout_problems.append(f"{cs_name} does not transcribe {c_name}: header {in_header}, "
                                   f"shell {in_shell}")
    if layout_problems:
        report.fail("the C# shell's struct layout drifted from c_api.h: " + "; ".join(layout_problems) +
                    " -- with LayoutKind.Sequential the field ORDER is the layout, so a reordering or a "
                    "missing field moves every member after it while every NAME a grep test looks for is "
                    "still there")
    elif entry_points and not unreachable and not unknown:
        report.note(f"C# shell transcribes {len(CSHARP_STRUCT_PAIRS)} structs field-for-field and declares "
                    f"all {len(entry_points)} entry points")


def check_python_baseline(root: Path, report: Report) -> None:
    """The Python shell's STATIC transcription of the C structs, field for field, in order.

    WHY THIS EXISTS (2026-10-20, audit round 2 §12.2 API P1). The C# side had a field-for-field check and
    the Python side had NONE: `_ffi.py` carries seven `ctypes.Structure` mirrors of `c_api.h`, and not one
    of them was compared with the header by any gate. A missing or reordered field there does not fail
    loudly -- ctypes lays the struct out from `_fields_`, so the call succeeds and reads the wrong bytes,
    which surfaces as a nonsense `style` or a truncated `spelling` far from the cause. The runtime tests
    cannot be the only guard either: they need a built library, so a drift that lands without anyone
    running them is silent.

    The comparison is the same shape as the C# one and shares its authority: `c_api.h` in declaration order.
    """
    ffi = root / "python" / "cpp_pinyin2" / "_ffi.py"
    header = root / "include" / "cpp-pinyin2" / "ext" / "c_api.h"
    for path, why in ((ffi, "the Python shell then mirrors nothing, and this check would compare two empty "
                            "lists and pass"),
                      (header, "the C ABI's own declarations are the authority this check reads")):
        if not path.is_file():
            report.fail(f"{path.relative_to(root).as_posix()} is missing: {why}")
            return

    header_text = header.read_text(encoding="utf-8")
    ffi_text = ffi.read_text(encoding="utf-8")
    problems: List[str] = []
    compared = 0
    for c_name, py_name in PYTHON_STRUCT_PAIRS:
        in_header = c_struct_fields(header_text, c_name)
        in_shell = python_struct_fields(ffi_text, py_name)
        if in_header is None:
            problems.append(f"{c_name} is not declared in c_api.h")
            continue
        if in_shell is None:
            problems.append(f"class {py_name} is not declared in _ffi.py")
            continue
        if not in_header or not in_shell:
            problems.append(f"{c_name}/{py_name} parsed to no fields at all")
            continue
        compared += 1
        if in_header != in_shell:
            problems.append(f"{py_name} does not mirror {c_name}: header {in_header}, shell {in_shell}")
    if problems:
        report.fail("the Python shell's ctypes mirrors drifted from c_api.h: " + "; ".join(problems) +
                    " -- ctypes lays the struct out from `_fields_`, so a missing or reordered field does "
                    "not fail: the call succeeds and reads the wrong bytes")
    elif compared:
        report.note(f"Python shell mirrors {compared} C struct(s) field-for-field, in declaration order")


def check_wheel_members(root: Path, wheel: Path, report: Report) -> None:
    """Ask the wheel the member-level question that only the nupkg was being asked.

    The dialect NAMES in a wheel agreeing with the tree is a much weaker statement than it looks. A
    reviewer built four wheels that passed this gate: every dictionary file empty, one table missing, an
    extra file, and no dictionary face at all -- each of which installs and then fails. The comparison
    itself lives in python/tools/check_wheel.py, which CI runs as its own step, and this calls the same
    function rather than growing a second copy: the CI step asserts that the script is invoked, and
    "invoked" is not "compared correctly".
    """
    import importlib.util

    script = root / "python" / "tools" / "check_wheel.py"
    if not script.is_file():
        report.fail("python/tools/check_wheel.py is missing, so nothing compares the wheel's members with "
                    "the tree any more")
        return
    spec = importlib.util.spec_from_file_location("cpp_pinyin2_check_wheel", script)
    module = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(module)
        problems = module.member_problems(wheel, root)
    except SystemExit as exc:      # the shared helper says so, loudly, when the package dir is not staged
        report.fail(f"wheel members could not be compared: {exc}")
        return
    for problem in problems[:5]:
        report.fail(f"wheel members: {problem}")
    if len(problems) > 5:
        report.fail(f"wheel members: and {len(problems) - 5} more")
    if not problems:
        report.note("wheel members: every packaged file matches the tree byte for byte")


SLUG_RE = re.compile(r"github\.com/([A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+)")

#: Where the release surface states the repository, and how to read the statement out of each file. Each entry
#: is (path, regex-with-one-group). Regexes rather than parsers on purpose: these are single-line facts inn
#: several formats, and a YAML/XML/TOML/Python parser per format would be more machinery than the fact
#: is worth -- but each pattern is anchored on the FIELD name, not on prose, so a mention of another repository
#: (pinyin-makedict, csharp-pinyin) in a README sentence is not picked up.
RELEASE_URL_FIELDS = (
    ("README.md", r"GIT_REPOSITORY\s+https://github\.com/([A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+?)\.git"),
    ("csharp/CppPinyin/CppPinyin.csproj", r"<PackageProjectUrl>https://github\.com/([A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+)</PackageProjectUrl>"),
    ("csharp/CppPinyin/CppPinyin.csproj", r"<RepositoryUrl>https://github\.com/([A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+)</RepositoryUrl>"),
    ("python/setup.py", r'"Homepage":\s*"https://github\.com/([A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+)"'),
    ("python/setup.py", r'"Source":\s*"https://github\.com/([A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+)"'),
)

TFM_RE = re.compile(r"<TargetFrameworks?>([^<]+)</TargetFrameworks?>")


def origin_slug(root: Path) -> Optional[str]:
    """`owner/repo` from the git remote, or None when this is not a git checkout (a tarball, a CI sandbox)."""
    try:
        out = subprocess.run(["git", "-C", str(root), "remote", "get-url", "origin"],
                             capture_output=True, text=True, encoding="utf-8", timeout=10)
    except (OSError, subprocess.SubprocessError):
        return None
    if out.returncode != 0:
        return None
    match = SLUG_RE.search(out.stdout.strip().replace(":", "/", 1))
    # `removesuffix` is 3.9+: Ubuntu 20.04 ships python 3.8, where this raised AttributeError and took the whole
    # BindingGates test with it (measured on the firefly board, 2026-10-21).
    if not match:
        return None
    name = match.group(1)
    return name[:-4] if name.endswith(".git") else name


# Optional "- " in front: a matrix `include:` entry puts the first key on the dash line and the rest below it,
# so `- rid: win-x64` and `  rid: win-x64` are the same fact written two ways (2026-10-21).
RID_LINE = re.compile(r"^\s*(?:-\s*)?rid:\s*([A-Za-z0-9_.-]+)\s*$", re.MULTILINE)


def workflow_rids(path: Path) -> set:
    """Every `rid:` value in a workflow's matrix, as a set. Line-anchored on purpose: a RID quoted in a comment
    or a step name is documentation, and a documentation mention is not a release matrix."""
    try:
        source = path.read_text(encoding="utf-8")
    except OSError:
        return set()
    return {match.group(1) for match in RID_LINE.finditer(source) if not match.group(1).startswith("$")}


CC_IN_LICENCE_TEXT = re.compile(r"creativecommons\.org/licenses/by-sa/(\d+\.\d+)", re.IGNORECASE)
CC_IN_EXPRESSION = re.compile(r"CC-BY-SA-(\d+\.\d+)", re.IGNORECASE)


def shipped_cc_versions(root: Path) -> set:
    """The Creative Commons versions the DICTIONARY FILES declare, read from their own license texts.

    Each dialect ships a `License.txt` next to its tables (that is what THIRD_PARTY_NOTICES.md points at), so the
    data is its own authority here; this function never invents a version.
    """
    versions = set()
    for licence in sorted((root / "data" / "dialects").glob("*/License.txt")):
        try:
            source = licence.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        versions.update(CC_IN_LICENCE_TEXT.findall(source))
    return versions


def check_package_licence(root: Path, report: Report) -> None:
    """The nupkg's SPDX expression must cover the code AND every licence the shipped dictionaries carry.

    WHERE THIS READS FROM: `CppPinyin.csproj`'s `<PackageLicenseExpression>`, which is what the nupkg is built
    with. It used to read a vcpkg port manifest instead -- that was the only SPDX expression in the tree at the
    time -- and when the port was removed (2026-10-23) the guarantee came along rather than going with it: the
    nupkg declares the same three licences, and understating them is invisible to every other check, because
    the packaging gate looks at members and the version gates at numbers.

    A MISSING csproj is not a failure here -- `check_csharp_identity` owns that -- so this reports and returns.
    """
    project = root / "csharp" / "CppPinyin" / "CppPinyin.csproj"
    try:
        source = project.read_text(encoding="utf-8")
    except OSError:
        report.note("package licence: not applicable here (no csproj in this tree)")
        return
    declared = re.search(r"<PackageLicenseExpression>([^<]+)</PackageLicenseExpression>", source)
    if not declared:
        report.fail("package licence: the csproj declares no <PackageLicenseExpression>")
        return
    expression = declared.group(1).strip()
    if "Apache-2.0" not in expression:
        report.fail(f"package licence: {expression!r} does not name Apache-2.0, which is what LICENSE grants for "
                    f"the code")
    shipped = shipped_cc_versions(root)
    if not shipped:
        report.note("package licence: no dictionary licence text found, so only the code licence was checked")
        return
    listed = set(CC_IN_EXPRESSION.findall(expression))
    missing = sorted(shipped - listed)
    extra = sorted(listed - shipped)
    if missing:
        report.fail(f"package licence: {expression!r} omits CC-BY-SA-{', CC-BY-SA-'.join(missing)}, which the "
                    f"shipped dictionary tables declare in their own License.txt: the package would tell a "
                    f"consumer something its files do not grant")
    if extra:
        report.fail(f"package licence: {expression!r} names CC-BY-SA-{', CC-BY-SA-'.join(extra)}, but no shipped "
                    f"dictionary declares that version")
    if not missing and not extra:
        report.note(f"package licence: {expression} covers the code and every shipped dictionary licence")


BLOB_PATH = re.compile(r"github\.com/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+/blob/([A-Za-z0-9_.\-/]+?)/")


def local_branches(root: Path) -> set:
    """Branch names in this checkout, or an empty set when git is not available."""
    try:
        out = subprocess.run(["git", "-C", str(root), "for-each-ref", "--format=%(refname:short)", "refs/heads"],
                             capture_output=True, text=True, encoding="utf-8", timeout=10)
    except (OSError, subprocess.SubprocessError):
        return set()
    if out.returncode != 0:
        return set()
    return {line.strip() for line in out.stdout.splitlines() if line.strip()}


RELEASE_TOOLS = ("build_native.py", "check_wheel.py", "check_bindings.py", "smoke_installed.py",
                 "merge_nupkgs.py", "emit_build_info.py", "stamp_sha512.py")

#: The two pages that show a human which commands to paste. They may say it in different prose; they may not
#: disagree about the command itself.
RELEASE_PAGES = ("docs/RELEASE_CHECKLIST.md", "docs/RELEASE_RUNBOOK.md")


def tool_commands(page: Path) -> dict:
    """`{tool: {command lines naming it}}` for one page, from every fenced code block."""
    try:
        source = page.read_text(encoding="utf-8")
    except OSError:
        return {}
    found = {}
    for block in re.findall(r"```[A-Za-z]*\n(.*?)```", source, flags=re.S):
        for line in block.splitlines():
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                continue
            for tool in RELEASE_TOOLS:
                if tool in stripped:
                    found.setdefault(tool, set()).add(" ".join(stripped.split()))
    return found


def check_release_commands(root: Path, report: Report) -> None:
    """The two release pages must not show two recipes for the same tool.

    A page a person reads at 2am and a page they paste from must be the same page. When the checklist and the
    runbook disagreed (2026-10-21) nothing failed: each page was internally consistent, and the disagreement only
    exists BETWEEN them.
    """
    pages = [root / relative for relative in RELEASE_PAGES]
    if not all(page.is_file() for page in pages):
        report.note("release commands: not applicable here (both release pages are not present)")
        return
    collected = [tool_commands(page) for page in pages]
    if not any(collected):
        report.note("release commands: neither page carries a fenced command block")
        return
    disagreements = []
    for tool in RELEASE_TOOLS:
        left = collected[0].get(tool, set())
        right = collected[1].get(tool, set())
        if left and right and left != right:
            disagreements.append((tool, sorted(left), sorted(right)))
    if disagreements:
        for tool, left, right in disagreements:
            report.fail(f"release commands: {tool} is spelled differently in the two release pages: "
                        f"{RELEASE_PAGES[0]} says {left}, {RELEASE_PAGES[1]} says {right}")
    else:
        shared = sorted(set(collected[0]) & set(collected[1]))
        report.note(f"release commands: the two release pages agree about "
                    f"{', '.join(shared) if shared else 'nothing they both show'}")


def check_release_url_paths(root: Path, report: Report) -> None:
    """The branch segment of every release-facing link must be the same one, and it must exist here.

    The release files point into the repository by branch name, not by tag: a listing that survives is one whose
    links survive a rename of `v2`. Nothing read those paths before (2026-10-21), so a renamed branch would break
    every package page silently -- the links keep working for the person who renamed it and stop working for
    everyone who already installed.
    """
    branches_by_file = {}
    for relative, _pattern in RELEASE_URL_FIELDS:
        try:
            source = (root / relative).read_text(encoding="utf-8")
        except OSError:
            continue
        found = {m.group(1) for m in BLOB_PATH.finditer(source)}
        if found:
            branches_by_file.setdefault(relative, set()).update(found)
    if not branches_by_file:
        report.note("release paths: no branch-scoped links in the release surface, so nothing to compare")
        return
    named = set().union(*branches_by_file.values())
    if len(named) > 1:
        detail = "; ".join(f"{', '.join(sorted(b))} <- {f}" for f, b in sorted(branches_by_file.items()))
        report.fail(f"release paths: the release files link into more than one branch ({detail}): one of them is "
                    f"stale, and both look equally authoritative to a reader")
        return
    branch = next(iter(named))
    known = local_branches(root)
    if not known:
        report.note(f"release paths: every link points into branch {branch!r}, and there is no git here to confirm "
                    f"the branch exists")
        return
    if branch not in known:
        report.fail(f"release paths: the release files link into branch {branch!r}, which does not exist in this "
                    f"checkout {sorted(known)}: every package page would point at a 404")
    else:
        report.note(f"release paths: every link points into branch {branch!r}, which exists here")


def check_rid_matrix(root: Path, report: Report) -> None:
    """The RID set is written in two workflows and the native-extension rule in one table; they must agree.

    `release_prep.yml` decides what is SHIPPED, `test_csharp.yml` decides what is TESTED. A package for a runtime
    nobody tested, or a test for a runtime that is never packed, is invisible to every other check in this file:
    `check_packaging` only asks whether the native asset matches the directory it was packed into.
    """
    release = workflow_rids(root / ".github" / "workflows" / "release_prep.yml")
    tested = workflow_rids(root / ".github" / "workflows" / "test_csharp.yml")
    if not release and not tested:
        report.note("runtime identifiers: not applicable here (no release or test matrix in this tree)")
        return
    if not release:
        report.fail("runtime identifiers: the release workflow declares no rid matrix, so the packages it "
                    "builds cannot be tied to a runtime")
        return
    unknown = sorted(rid for rid in release | tested if rid.split("-")[0] not in RID_SUFFIX)
    if unknown:
        report.fail(f"runtime identifiers: {unknown} have no native-extension rule in RID_SUFFIX "
                    f"{sorted(RID_SUFFIX)}, so nothing can tell whether the packaged library is loadable there")
    only_release = sorted(release - tested)
    only_test = sorted(tested - release)
    if only_release:
        report.fail(f"runtime identifiers: release_prep ships {only_release} but test_csharp never tests them: "
                    f"a package for a runtime no leg has run")
    if only_test:
        report.fail(f"runtime identifiers: test_csharp tests {only_test} but release_prep never ships them: a "
                    f"leg whose result no consumer will ever receive")
    if not only_release and not only_test and not unknown:
        report.note(f"runtime identifiers: {sorted(release)} are both shipped and tested, and each has an "
                    f"extension rule")


def check_release_surface(root: Path, report: Report) -> None:
    """The release surface states its own identity four times; this is the check that they agree.

    1. The repository slug: README's FetchContent, the csproj's two URL properties and
       setup.py's project_urls must all name one repository, and that one must be the remote when git is around.
    2. The target frameworks: every other C# project may target a SUBSET of what the package targets, never a
       framework the package does not publish -- a smoke project on a TFM the package lacks passes in CI and
       then fails for a consumer.
    """
    # A synthetic tree (the self-test's sandboxes, a tarball of two files) has no release surface at all. That is
    # "not applicable", not "passing": say which, so a run that skipped the check is not mistaken for a run that
    # proved it. A tree that has SOME of the surface but not all of it still fails below -- deleting the README
    # while keeping the project is exactly the regression worth catching.
    present = [root / relative for relative, _pattern in RELEASE_URL_FIELDS]
    if not any(path.exists() for path in present) and not (root / "csharp" / "CppPinyin" / "CppPinyin.csproj").exists():
        report.note("release surface: not applicable here (no README, csproj or setup.py in this tree)")
        return

    canonical = origin_slug(root)
    if canonical is None:
        report.note("release surface: no git remote here, so the slug is only checked for agreement between files")
    seen = {}
    for relative, pattern in RELEASE_URL_FIELDS:
        path = root / relative
        try:
            source = path.read_text(encoding="utf-8")
        except OSError:
            report.fail(f"release surface: {relative} is missing, so its repository URL cannot be checked")
            continue
        match = re.search(pattern, source)
        if not match:
            report.fail(f"release surface: {relative} has no repository URL this check can read "
                        f"(pattern {pattern!r}): a release file that stops naming its source is a listing "
                        f"nobody can trace back")
            continue
        seen.setdefault(match.group(1), []).append(relative)
    if len(seen) > 1:
        detail = "; ".join(f"{slug} <- {', '.join(sorted(set(files)))}" for slug, files in sorted(seen.items()))
        report.fail(f"release surface: the repository is named more than one way: {detail}")
    elif seen:
        slug = next(iter(seen))
        if canonical is not None and slug != canonical:
            report.fail(f"release surface: the files say {slug!r} but the git remote says {canonical!r}: a "
                        f"package that points at a repository that is not its own")
        else:
            report.note(f"release surface: every release file names {slug}")

    package = root / "csharp" / "CppPinyin" / "CppPinyin.csproj"
    try:
        package_tfms = TFM_RE.search(package.read_text(encoding="utf-8"))
    except OSError:
        package_tfms = None
    if package_tfms is None:
        report.fail("release surface: the package project declares no TargetFrameworks, so there is nothing to "
                    "compare the other projects against")
        return
    published = {t.strip() for t in package_tfms.group(1).split(";") if t.strip()}
    for relative in ("csharp/CppPinyin.Tests/CppPinyin.Tests.csproj",
                     "csharp/CppPinyin.ConsumerSmoke/CppPinyin.ConsumerSmoke.csproj"):
        try:
            found = TFM_RE.search((root / relative).read_text(encoding="utf-8"))
        except OSError:
            # Absent is not drift: a checkout carrying only the package project still has a consistent surface,
            # and failing here would turn every minimal fixture (and any trimmed tarball) red for a file it
            # never had. What IS checked is agreement among the projects that exist.
            continue
        if found is None:
            report.fail(f"release surface: {relative} declares no TargetFrameworks")
            continue
        wanted = {t.strip() for t in found.group(1).split(";") if t.strip()}
        extra = wanted - published
        if extra:
            report.fail(f"release surface: {relative} targets {sorted(extra)}, which the package does not "
                        f"publish {sorted(published)}: CI would test a framework no consumer can install")
    report.note(f"release surface: target frameworks {sorted(published)} cover every other C# project")


def run(root: Path, nupkg: Optional[Path], wheel: Optional[Path], rid: Optional[str] = None,
        exclude_parts: Sequence[str] = (".tmp", "bin", "obj"),
        tag: Optional[str] = None, workflows: bool = True) -> Report:
    report = Report()
    check_xml_hygiene(root, report, exclude_parts)
    check_source_shape(root, report)   # the script own shape: no check defined-but-never-called
    if workflows:
        check_workflow_wiring(root, report)
        check_artifact_wiring(root, report)
    check_version_chain(root, nupkg, wheel, report, tag)
    check_csharp_identity(root, report)
    check_release_surface(root, report)
    check_rid_matrix(root, report)
    check_package_licence(root, report)
    check_release_url_paths(root, report)
    check_release_commands(root, report)
    check_dialect_surface(root, wheel, report)
    if wheel is not None:
        check_wheel_members(root, wheel, report)
    check_public_header_surface(root, report)
    check_plugin_surface(root, report)
    check_exported_declarations(root, report)
    check_exported_signatures(root, report)
    check_csharp_baseline(root, report)
    check_python_baseline(root, report)
    if nupkg is not None:
        if not nupkg.is_file():
            report.fail("nupkg not found: " + str(nupkg))
        else:
            check_packaging(root, nupkg, report, rid)
    return report


def print_report(report: Report) -> None:
    for note in report.notes:
        print("  note: " + note)
    for failure in report.failures:
        print("  FAIL: " + failure)
    print("PASS" if report.ok else f"FAILED: {len(report.failures)} problem(s)")


def self_test() -> int:
    """Prove each check can go red, on a fixture tree built for the purpose.

    The sandbox lives under .tmp (so an interrupted run leaves only gitignored scratch), which means
    the real run's exclusion rules would filter out every file in it -- the first version of this
    self-test did exactly that and the "no XML files found" guard fired on the CLEAN fixture. So the
    sandbox is scanned with no exclusions at all, which is also the honest setting: a fixture that has
    to be excluded to be seen is not testing what the real tree gets tested with.
    """
    sandbox = ROOT / ".tmp" / "r16-gate-selftest"
    if sandbox.exists():
        shutil.rmtree(sandbox)

    def sandbox_run(nupkg: Optional[Path] = None, tag: Optional[str] = None,
                    rid: Optional[str] = None) -> Report:
        # workflows=False: the sandbox holds no .github/workflows, and the wiring check has its own
        # fixtures below. Without this the "no workflows found" guard would fire on every other control.
        return run(sandbox, nupkg, None, rid=rid, exclude_parts=(), tag=tag, workflows=False)

    (sandbox / "csharp" / "CppPinyin").mkdir(parents=True)
    (sandbox / "data" / "dialects" / "mandarin").mkdir(parents=True)
    (sandbox / "data" / "dialects" / "cantonese").mkdir(parents=True)

    def write_lf(path: Path, text: str) -> None:
        """LF-only write, spelled with open(..., newline="\\n").

        Path.write_text(newline=...) only exists from 3.10. This self-test runs on whatever interpreter the
        developer or CI has, and the board runs 3.8, where the keyword raises TypeError and turns the whole
        self-test red for a reason that has nothing to do with the checks being tested.
        """
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)

    (sandbox / "CMakeLists.txt").write_text("project(cpp-pinyin2 VERSION 2.0.0)\n", encoding="utf-8")
    (sandbox / "python" / "tools").mkdir(parents=True)
    write_lf(sandbox / "python" / "tools" / "build_native.py",
             'DIALECTS = ("mandarin", "cantonese")\n'
             'REQUIRED_TABLES = ("word.txt", "phrases_dict.txt", "phrases_map.txt", "trans_word.txt")\n')

    def write_manifest(dialect: str, **overrides: object) -> Path:
        """Write `dialect`'s manifest; the controls below patch single fields through `overrides`."""
        body = {
            "id": dialect,
            "min_library": "2.0.0",
            "required_tables": ["word.txt", "phrases_dict.txt", "phrases_map.txt", "trans_word.txt"],
            "optional_tables": ["user_dict.txt"],
            "assets": ["License.txt", "format.txt"],
        }
        body.update(overrides)
        path = sandbox / "data" / "dialects" / dialect / "dialect.json"
        write_lf(path, json.dumps(body, indent=2, sort_keys=True) + "\n")
        return path

    for _dialect in ("mandarin", "cantonese"):
        for _name in ("word.txt", "phrases_dict.txt", "phrases_map.txt", "trans_word.txt", "License.txt", "format.txt"):
            (sandbox / "data" / "dialects" / _dialect / _name).write_text("", encoding="utf-8")
        write_manifest(_dialect)
    # One dialect carries the optional table, the other does not: the manifest must accept both.
    (sandbox / "data" / "dialects" / "mandarin" / "user_dict.txt").write_text("", encoding="utf-8")

    (sandbox / "include" / "cpp-pinyin2" / "core").mkdir(parents=True)
    (sandbox / "include" / "cpp-pinyin2" / "core" / "Types.h").write_text("#pragma once\n", encoding="utf-8")
    (sandbox / "include" / "cpp-pinyin2" / "core" / "Engine.h").write_text(
        "#pragma once\n#include <cpp-pinyin2/core/Types.h>\n", encoding="utf-8")
    (sandbox / "tests" / "abi").mkdir(parents=True)
    write_lf(sandbox / "tests" / "abi" / "expected_public_headers.txt",
             "# frozen\ncpp-pinyin2/core/Engine.h\ncpp-pinyin2/core/Types.h\ncpp-pinyin2/ext/Dialect.h\n"
             "cpp-pinyin2/ext/Registry.h\ncpp-pinyin2/ext/c_api.h\n")
    (sandbox / "include" / "cpp-pinyin2" / "ext").mkdir(parents=True)
    # The C# baseline check reads three files, so the fixture carries all three: a header with structs to
    # transcribe, the shell that transcribes them, and the frozen symbol list. Two fields per struct are
    # enough -- the check compares ORDER, not widths, and the widths are SmokeTests.cs's runtime job.
    write_lf(sandbox / "include" / "cpp-pinyin2" / "ext" / "c_api.h",
             "#pragma once\nCPP_PINYIN2_EXPORT const char *demo_symbol(void);\n"
             "typedef struct cppp2_error {\n    int code;\n    const char *detail;\n} cppp2_error;\n"
             "typedef struct cppp2_query_opts {\n    size_t struct_size;\n    int style;\n} cppp2_query_opts;\n"
             "typedef struct cppp2_syllable {\n    const char *initial;\n    int tone;\n} cppp2_syllable;\n" "typedef struct cppp2_style {\n    const char *name;\n    int32_t alias;\n    int32_t is_default;\n} cppp2_style;\n"
             "typedef struct cppp2_segment {\n    const char *text;\n    int failure_kind;\n} cppp2_segment;\n"
             "typedef struct cppp2_result {\n    size_t count;\n    void *segments;\n} cppp2_result;\n"
             "typedef struct cppp2_tone_sandhi_opts {\n    size_t struct_size;\n    int32_t third_tone;\n"
             "    int32_t bu;\n    int32_t yi;\n    int32_t reserved_0;\n} cppp2_tone_sandhi_opts;\n")
    write_lf(sandbox / "tests" / "abi" / "expected_c_api_symbols.txt", "# frozen\ndemo_symbol\n")
    # The signature baseline for the same one export. Staged with the symbol list because the two halves of the
    # C ABI contract are checked together: without this file the clean fixture reported "signature set stated
    # nowhere a check can read" and every should-pass case went red for a reason unrelated to what it tested.
    write_lf(sandbox / "tests" / "abi" / "expected_c_api_signatures.v2.txt",
             "const char* demo_symbol()\n")
    write_lf(sandbox / "csharp" / "CppPinyin" / "NativeMethods.cs",
             "internal static class NativeMethods\n{\n"
             "    internal static extern IntPtr demo_symbol();\n\n"
             "    internal struct ErrorRaw\n    {\n        internal int code;\n        internal IntPtr detail;\n    }\n"
             "    internal struct QueryOptions\n    {\n        internal UIntPtr struct_size;\n        internal int style;\n    }\n"
             "    internal struct SyllableRaw\n    {\n        internal IntPtr initial;\n        internal int tone;\n    }\n" "    internal struct StyleRaw\n    {\n        internal IntPtr name;\n        internal int alias;\n        internal int is_default;\n    }\n"
             "    internal struct SegmentRaw\n    {\n        internal IntPtr text;\n        internal int failure_kind;\n    }\n"
             "    internal struct ResultRaw\n    {\n        internal UIntPtr count;\n        internal IntPtr segments;\n    }\n"
             "    internal struct ToneSandhiOptsRaw\n    {\n        internal UIntPtr struct_size;\n"
             "        internal int third_tone;\n        internal int bu;\n        internal int yi;\n"
             "        internal int reserved_0;\n    }\n"
             "}\n")

    # The Python mirrors get the same fixture treatment as the C# ones: every struct in the sandbox
    # c_api.h has one class here, named as `PYTHON_STRUCT_PAIRS` expects, or `check_python_baseline`
    # fails the clean-tree control for a reason that has nothing to do with what that control tests.
    (sandbox / "python" / "cpp_pinyin2").mkdir(parents=True, exist_ok=True)
    write_lf(sandbox / "python" / "cpp_pinyin2" / "_ffi.py",
             "import ctypes\n\n\n"
             "class Error(ctypes.Structure):\n    _fields_ = [\n        (\"code\", ctypes.c_int32),\n"
             "        (\"detail\", ctypes.c_char_p),\n    ]\n\n\n"
             "class QueryOpts(ctypes.Structure):\n    _fields_ = [\n        (\"struct_size\", ctypes.c_size_t),\n"
             "        (\"style\", ctypes.c_int32),\n    ]\n\n\n"
             "class Syllable(ctypes.Structure):\n    _fields_ = [\n        (\"initial\", ctypes.c_char_p),\n"
             "        (\"tone\", ctypes.c_int32),\n    ]\n\n\n"
             "class Segment(ctypes.Structure):\n    _fields_ = [\n        (\"text\", ctypes.c_char_p),\n"
             "        (\"failure_kind\", ctypes.c_int32),\n    ]\n\n\n"
             "class Style(ctypes.Structure):\n    _fields_ = [\n        (\"name\", ctypes.c_char_p),\n"
             "        (\"alias\", ctypes.c_int32),\n        (\"is_default\", ctypes.c_int32),\n    ]\n\n\n"
             "class Result(ctypes.Structure):\n    _fields_ = [\n        (\"count\", ctypes.c_size_t),\n"
             "        (\"segments\", ctypes.c_void_p),\n    ]\n\n\n"
             "class ToneSandhiOpts(ctypes.Structure):\n    _fields_ = [\n        (\"struct_size\", ctypes.c_size_t),\n"
             "        (\"third_tone\", ctypes.c_int32),\n        (\"bu\", ctypes.c_int32),\n"
             "        (\"yi\", ctypes.c_int32),\n        (\"reserved_0\", ctypes.c_int32),\n    ]\n")

    # The plugin surface is staged here, with the other frozen lists, and NOT next to its fixtures: the
    # first version wrote these at the end of the self test, so every "passes" fixture that ran earlier saw
    # no list at all and nine of them failed for a reason that had nothing to do with what they test.
    plugin_dir = sandbox / "include" / "cpp-pinyin2" / "ext"
    plugin_dir.mkdir(parents=True, exist_ok=True)
    write_lf(plugin_dir / "Dialect.h",
             "#pragma once\n"
             "#include <cpp-pinyin2/core/ToneConverter.h>\n"
             "namespace Pinyin {\n"
             "    struct Dialect {\n"
             "        std::string id;\n"
             "        std::string dictSubDir;\n"
             "        ToneConverter tone;\n"
             "        bool supportsVToU = false;\n"
             "        bool supportsNeutralTone5 = false;   // a trailing comment on purpose: the extractor\n"
             "                                             // used to drop declarations written this way\n"
             "    };\n"
             "}\n")
    write_lf(plugin_dir / "Registry.h",
             "#pragma once\n"
             "#include <cpp-pinyin2/ext/Dialect.h>\n"
             "namespace Pinyin {\n"
             "    using DialectCreator = std::function<std::shared_ptr<const Dialect>()>;\n"
             "    class DialectRegistry {\n"
             "    public:\n"
             "        static DialectRegistry &instance();\n"
             "        bool add(std::string id, DialectCreator creator, bool replace = false);\n"
             "        [[nodiscard]] std::shared_ptr<const Dialect> resolve(std::string_view id) const;\n"
             "        [[nodiscard]] std::vector<std::string> ids() const;\n"
             "        DialectRegistry(const DialectRegistry &) = delete;\n"
             "    private:\n"
             "        struct Impl;\n"
             "    };\n"
             "}\n"
             "#define CPP_PINYIN_REGISTER_DIALECT(id, creator) \\\n"
             "    namespace { const bool x = ::Pinyin::DialectRegistry::instance().add(id, creator); }\n")
    surface_list = sandbox / "tests" / "abi" / "expected_plugin_surface.txt"
    surface_list.parent.mkdir(parents=True, exist_ok=True)
    write_lf(surface_list, "\n".join(plugin_surface(sandbox)) + "\n")

    (sandbox / "csharp" / "CppPinyin" / "version.props").write_text(
        "<Project><PropertyGroup><Version>2.0.0</Version></PropertyGroup></Project>\n", encoding="utf-8")
    (sandbox / "csharp" / "CppPinyin" / "Clean.csproj").write_text(
        "<!-- a clean comment with an em dash, not a double hyphen -->\n<Project />\n", encoding="utf-8")
    # The package identity fixture (H28): the real tree has exactly one csproj carrying these three
    # properties, and `check_csharp_identity` fails when it finds none, so the sandbox must stage one or
    # every "clean tree passes" case would go red for an unrelated reason. It also carries the licence
    # expression, because `check_package_licence` reads that from this file (since the vcpkg port, which used
    # to hold the only SPDX expression in the tree, was removed on 2026-10-23).
    package_project = sandbox / "csharp" / "CppPinyin" / "CppPinyin.csproj"
    package_project.write_text(
        "<Project>\n  <PropertyGroup>\n    <AssemblyName>CppPinyin2</AssemblyName>\n"
        "    <RootNamespace>CppPinyin2</RootNamespace>\n    <PackageId>CppPinyin</PackageId>\n"
        "    <PackageProjectUrl>https://github.com/wolfgitpr/cpp-pinyin</PackageProjectUrl>\n"
        "    <RepositoryUrl>https://github.com/wolfgitpr/cpp-pinyin</RepositoryUrl>\n"
        "    <TargetFrameworks>netstandard2.0;net7.0;net10.0</TargetFrameworks>\n"
        "    <PackageLicenseExpression>Apache-2.0 AND CC-BY-SA-3.0 AND CC-BY-SA-4.0</PackageLicenseExpression>\n"
        "  </PropertyGroup>\n</Project>\n", encoding="utf-8")

    # The release surface (2026-10-21): the same repository slug in every file the ecosystems read. Staged so
    # `check_release_surface` has something to agree about; the drift case stages its own tree. The vcpkg
    # manifest used to be one of those files -- the port was removed on 2026-10-23.
    (sandbox / "README.md").write_text(
        "GIT_REPOSITORY https://github.com/wolfgitpr/cpp-pinyin.git\n", encoding="utf-8")
    write_lf(sandbox / "python" / "setup.py",
             'setup(project_urls={"Homepage": "https://github.com/wolfgitpr/cpp-pinyin",\n'
             '                     "Source": "https://github.com/wolfgitpr/cpp-pinyin"})\n')

    results = []

    # 1. clean fixture must pass. Why it failed is part of the label: a should-pass fixture that goes red
    #    without saying what it saw costs a debugging round every time -- which is exactly what happened
    #    here when the plugin surface check was added and the sandbox had no list staged yet.
    # The engine's own data-format constant, staged with the fixtures rather than in the drift control:
    # `format.txt` IS present in the sandbox data from the start, so the marker check needs an engine to
    # compare it against before the first clean run (measured: staging it later turned "clean tree passes"
    # red for a reason that had nothing to do with the clean tree).
    (sandbox / "src").mkdir(parents=True, exist_ok=True)
    write_lf(sandbox / "src" / "Engine.cpp", "namespace {\nconstexpr int kSupportedDictFormat = 2;\n}\n")
    clean = sandbox_run()
    if clean.ok:
        results.append(("clean tree passes", True))
    else:
        first = clean.failures[0][:200] if clean.failures else "(no failure text reported)"
        results.append((f"clean tree passes -- it did not; first failure: {first}", False))

    # 1a. the package identity (H28): `PackageId` is the one property that deliberately keeps the
    # pre-2.0 name, and until this case existed nothing could tell that from a typo.
    good_identity = package_project.read_text(encoding="utf-8")
    package_project.write_text(good_identity.replace("<PackageId>CppPinyin</PackageId>",
                                                     "<PackageId>CppPinyin2</PackageId>"), encoding="utf-8")
    retagged = sandbox_run()
    results.append(("a PackageId that does not match the frozen value fails",
                    not retagged.ok and any("PackageId" in failure for failure in retagged.failures)))
    package_project.write_text(good_identity, encoding="utf-8")
    restored_identity = sandbox_run()
    results.append(("restoring the frozen PackageId restores the clean verdict", restored_identity.ok))

    # 1b. the dialect surface: data and the declared whitelist must agree, and only these two dialects
    (sandbox / "data" / "dialects" / "hokkien").mkdir(parents=True)
    third = sandbox_run()
    results.append(("a third dialect with data in data/dialects fails",
                    not third.ok and any("hokkien" in failure for failure in third.failures)))
    shutil.rmtree(sandbox / "data" / "dialects" / "hokkien")

    whitelist = sandbox / "python" / "tools" / "build_native.py"
    tables_line = 'REQUIRED_TABLES = ("word.txt", "phrases_dict.txt", "phrases_map.txt", "trans_word.txt")\n'
    write_lf(whitelist, 'DIALECTS = ("mandarin",)\n' + tables_line)
    skewed = sandbox_run()
    results.append(("a whitelist that disagrees with data/dialects fails",
                    not skewed.ok and any("whitelist" in failure for failure in skewed.failures)))
    write_lf(whitelist, 'DIALECTS = ("mandarin", "cantonese")\n' + tables_line)
    restored = sandbox_run()
    results.append(("restoring the whitelist restores the clean verdict", restored.ok))

    # 1b-bis. the shipped data-format marker against the engine's own constant. The number is written by the
    # generator in the OTHER repository, so nothing else in this tree can notice the two disagreeing -- data
    # regenerated for format 3 while the engine still accepts 2 fails for every consumer and for no one here.
    mandarin_marker = sandbox / "data" / "dialects" / "mandarin" / "format.txt"
    write_lf(mandarin_marker, "3\n")
    drifted_marker = sandbox_run()
    results.append(("a format.txt the engine's kSupportedDictFormat does not accept fails",
                    not drifted_marker.ok and "format.txt" in " ".join(drifted_marker.failures)))
    write_lf(mandarin_marker, "2\n")
    matching_marker = sandbox_run()
    results.append(("a format.txt equal to the engine's constant passes", matching_marker.ok))
    write_lf(sandbox / "src" / "Engine.cpp", "// no constant here\n")
    silent_engine = sandbox_run()
    results.append(("an engine whose constant cannot be parsed is a failure, not a pass",
                    not silent_engine.ok and "kSupportedDictFormat" in " ".join(silent_engine.failures)))
    write_lf(sandbox / "src" / "Engine.cpp", "namespace {\nconstexpr int kSupportedDictFormat = 2;\n}\n")

    # 1c. the manifests: a dialect that declares nothing, or disagrees with its own directory, must fail
    cantonese_manifest = sandbox / "data" / "dialects" / "cantonese" / "dialect.json"
    original_manifest = cantonese_manifest.read_text(encoding="utf-8")
    cantonese_manifest.unlink()
    unmanifested = sandbox_run()
    results.append(("a dialect without a dialect.json fails",
                    not unmanifested.ok and "dialect.json" in " ".join(unmanifested.failures)))
    write_lf(cantonese_manifest, original_manifest)

    write_manifest("mandarin",
                   required_tables=["word.txt", "phrases_dict.txt", "phrases_map.txt",
                                    "trans_word.txt", "extra.txt"])
    extra_table = sandbox_run()
    results.append(("a manifest declaring a table the engine never reads fails",
                    not extra_table.ok and "required_tables" in " ".join(extra_table.failures)))
    write_manifest("mandarin", id="hokkien")
    wrong_id = sandbox_run()
    results.append(("a manifest whose id disagrees with its directory fails",
                    not wrong_id.ok and "id is" in " ".join(wrong_id.failures)))
    write_manifest("mandarin")

    stray = sandbox / "data" / "dialects" / "cantonese" / "stray.txt"
    stray.write_text("", encoding="utf-8")
    unlisted = sandbox_run()
    results.append(("a file in a dialect directory that belongs to no role fails",
                    not unlisted.ok and "belong to no declared role" in " ".join(unlisted.failures)))
    stray.unlink()

    write_lf(whitelist, 'DIALECTS = ("mandarin", "cantonese")\nREQUIRED_TABLES = ("word.txt",)\n')
    wrong_tables = sandbox_run()
    results.append(("a tables whitelist that disagrees with the engine's loader fails",
                    not wrong_tables.ok and "REQUIRED_TABLES" in " ".join(wrong_tables.failures)))
    write_lf(whitelist, 'DIALECTS = ("mandarin", "cantonese")\n' + tables_line)

    restored_manifests = sandbox_run()
    results.append(("restoring the manifests restores the clean verdict", restored_manifests.ok))

    # 1d. the public header surface: the frozen list, and headers that reach outside the installed tree
    stray_header = sandbox / "include" / "cpp-pinyin2" / "ext" / "NewThing.h"
    stray_header.parent.mkdir(parents=True, exist_ok=True)
    stray_header.write_text("#pragma once\n", encoding="utf-8")
    unlisted_header = sandbox_run()
    results.append(("a public header the frozen list does not know fails",
                    not unlisted_header.ok and "NewThing.h" in " ".join(unlisted_header.failures)))
    stray_header.unlink()

    engine_header = sandbox / "include" / "cpp-pinyin2" / "core" / "Engine.h"
    original_engine = engine_header.read_text(encoding="utf-8")
    write_lf(engine_header, "#pragma once\n#include \"../src/EngineImpl.h\"\n")
    reaching_out = sandbox_run()
    results.append(("a public header that includes an internal file fails",
                    not reaching_out.ok and "outside the installed tree" in " ".join(reaching_out.failures)))
    write_lf(engine_header, original_engine)

    surface_list = sandbox / "tests" / "abi" / "expected_public_headers.txt"
    # Both directions of the comparison get their own control: adding a file the list does not know is
    # the NewThing.h case above, and a list entry with no file is this one. Written the other way round
    # first (shortening the list, which reports an EXTRA header, not a missing one) and the control that
    # was supposed to prove the gate fails proved nothing -- it is the control that has to be tested.
    write_lf(surface_list,
             "# frozen\ncpp-pinyin2/core/Engine.h\ncpp-pinyin2/core/Types.h\ncpp-pinyin2/core/Ghost.h\n"
             "cpp-pinyin2/ext/Dialect.h\ncpp-pinyin2/ext/Registry.h\ncpp-pinyin2/ext/c_api.h\n")
    shrunk_surface = sandbox_run()
    results.append(("a frozen public header that is not in the tree fails",
                    not shrunk_surface.ok and "are gone" in " ".join(shrunk_surface.failures)))
    write_lf(surface_list,
             "# frozen\ncpp-pinyin2/core/Engine.h\ncpp-pinyin2/core/Types.h\ncpp-pinyin2/ext/Dialect.h\n"
             "cpp-pinyin2/ext/Registry.h\ncpp-pinyin2/ext/c_api.h\n")

    restored_surface = sandbox_run()
    results.append(("restoring the public header surface restores the clean verdict", restored_surface.ok))

    # 1e. the header side of the ABI: the exported declarations and the frozen symbol list
    c_api = sandbox / "include" / "cpp-pinyin2" / "ext" / "c_api.h"
    original_c_api = c_api.read_text(encoding="utf-8")
    write_lf(c_api, original_c_api + "CPP_PINYIN2_EXPORT const char *demo_extra(void);\n")
    new_export = sandbox_run()
    results.append(("an exported declaration no frozen symbol covers fails",
                    not new_export.ok and "demo_extra" in " ".join(new_export.failures)))
    write_lf(c_api, original_c_api)

    symbol_list = sandbox / "tests" / "abi" / "expected_c_api_symbols.txt"
    write_lf(symbol_list, "# frozen\ndemo_symbol\ndemo_ghost\n")
    ghost_symbol = sandbox_run()
    results.append(("a frozen symbol no header declares fails",
                    not ghost_symbol.ok and "demo_ghost" in " ".join(ghost_symbol.failures)))
    write_lf(symbol_list, "# frozen\ndemo_symbol\n")

    restored_exports = sandbox_run()
    results.append(("restoring the exported declarations restores the clean verdict", restored_exports.ok))

    # 1e-bis. the TYPE side of the C ABI contract. The symbol list freezes names only, so these two cases are
    # what proves the signature check fills a hole rather than duplicating one: a WIDENED PARAMETER must go red,
    # and a RENAMED parameter must stay green (a name is invisible to every caller).
    #
    # Both cases edit the staged header IN PLACE (one function's parameter list) rather than replacing the
    # file: the same fixture also carries the struct declarations that the C# layout check reads, and the first
    # version of this case wrote a header without them, so it measured that check instead of this one.
    signature_list = sandbox / "tests" / "abi" / "expected_c_api_signatures.v2.txt"
    native_for_signature = sandbox / "csharp" / "CppPinyin" / "NativeMethods.cs"
    native_before_signature = native_for_signature.read_text(encoding="utf-8")
    write_lf(native_for_signature,
             native_before_signature.replace("internal static extern IntPtr demo_symbol();",
                                             "internal static extern IntPtr demo_symbol(int code);"))
    write_lf(signature_list, "const char* demo_symbol(int32_t)\n")

    write_lf(c_api, original_c_api.replace("demo_symbol(void)", "demo_symbol(int32_t other_name)"))
    renamed_parameter = sandbox_run()
    results.append(("a renamed parameter keeps the signature check green"
                    + ("" if renamed_parameter.ok else
                       " -- it did not; first failure: "
                       + (renamed_parameter.failures[0][:200] if renamed_parameter.failures else "(none)")),
                    renamed_parameter.ok))

    write_lf(c_api, original_c_api.replace("demo_symbol(void)", "demo_symbol(int64_t code)"))
    widened_parameter = sandbox_run()
    results.append(("a widened parameter type fails the signature check",
                    not widened_parameter.ok
                    and any("SIGNATURES changed" in failure for failure in widened_parameter.failures)))

    write_lf(c_api, original_c_api)
    write_lf(signature_list, "const char* demo_symbol()\n")
    write_lf(native_for_signature, native_before_signature)

    # 1f. the C# shell's static transcription of the C ABI (field order and the entry-point set)
    native_cs = sandbox / "csharp" / "CppPinyin" / "NativeMethods.cs"
    original_native = native_cs.read_text(encoding="utf-8")
    write_lf(native_cs, original_native.replace("internal int tone;", "internal int PITCH;"))
    drifted_layout = sandbox_run()
    results.append(("a C# struct field that does not transcribe the header fails",
                    not drifted_layout.ok
                    and "does not transcribe cppp2_syllable" in " ".join(drifted_layout.failures)))
    write_lf(native_cs, original_native)

    write_lf(native_cs, original_native.replace("    internal static extern IntPtr demo_symbol();\n", ""))
    never_declared = sandbox_run()
    results.append(("a frozen C symbol the C# shell never declares fails",
                    not never_declared.ok and "never declares" in " ".join(never_declared.failures)))
    write_lf(native_cs, original_native)

    restored_csharp = sandbox_run()
    results.append(("restoring the C# shell's transcription restores the clean verdict", restored_csharp.ok))

    # 2. a violating comment must fail (and name the file)
    bad = sandbox / "csharp" / "CppPinyin" / "Bad.csproj"
    bad.write_text("<!-- uses a double hyphen -- like the prose does -->\n<Project />\n", encoding="utf-8")
    hygiene = sandbox_run()
    results.append(("double hyphen in a comment fails", not hygiene.ok and "Bad.csproj" in " ".join(hygiene.failures)))
    bad.unlink()

    # 3. a version mismatch must fail
    (sandbox / "csharp" / "CppPinyin" / "version.props").write_text(
        "<Project><PropertyGroup><Version>9.9.9</Version></PropertyGroup></Project>\n", encoding="utf-8")
    mismatch = sandbox_run()
    results.append(("version mismatch fails", not mismatch.ok and any("version.props" in f for f in mismatch.failures)))
    (sandbox / "csharp" / "CppPinyin" / "version.props").write_text(
        "<Project><PropertyGroup><Version>2.0.0</Version></PropertyGroup></Project>\n", encoding="utf-8")

    # 4. a nupkg without the native asset must fail
    NUSPEC = "<package><metadata><id>CppPinyin</id><version>2.0.0</version></metadata></package>"

    def mirror_dicts(archive: zipfile.ZipFile, *, skip_manifests: bool = False) -> None:
        """Copy the sandbox tree's dialect directories into the package member for member.

        Writing one table per dialect would look complete and prove much less: the dictionary check is
        precisely what catches a package carrying the tables but not the manifest that says what they are,
        which is the regression of 2026-10-02 that this fixture set exists for.
        """
        for item in sorted((sandbox / "data" / "dialects").rglob("*")):
            if item.is_file() and not (skip_manifests and item.name == "dialect.json"):
                archive.writestr(f"{DICT_MEMBER_PREFIX}{item.parent.name}/{item.name}", "x")

    def package(name: str, *, native: Optional[str] = "libcpp-pinyin2.dll",
                native_data: bytes = b"not really a library", dicts: bool = True,
                skip_manifests: bool = False, libs: Sequence[str] = ("net7.0",),
                refs: Sequence[str] = ("net7.0",), xml: bool = True,
                notices: bool = True,
                assembly: str = managed_assembly_name(ROOT)) -> Path:
        """A package with the shape under test, written into the sandbox.

        `assembly` defaults to the name the packing project declares, NOT to a literal: the fixtures and the
        gate used to hardcode the same string, so a project that renamed its assembly kept both green while the
        real package stopped matching (2026-10-21). Override it only to build the negative case below.
        """
        path = sandbox / name
        with zipfile.ZipFile(path, "w") as archive:
            archive.writestr("CppPinyin.nuspec", NUSPEC)
            if notices:
                archive.writestr("THIRD_PARTY_NOTICES.md", "which material falls under which licence")
            for tfm in libs:
                archive.writestr(f"lib/{tfm}/{assembly}.dll", b"not really an assembly")
            for tfm in refs:
                archive.writestr(f"ref/{tfm}/{assembly}.dll", b"not really a reference assembly")
                if xml:
                    archive.writestr(f"ref/{tfm}/{assembly}.xml", "<doc/>")
            if native is not None:
                archive.writestr(f"runtimes/win-x64/native/{native}", native_data)
            if dicts:
                mirror_dicts(archive, skip_manifests=skip_manifests)
        return path

    hollow_report = sandbox_run(package("hollow.nupkg", native=None))
    results.append(("a nupkg with no native asset fails", not hollow_report.ok and any("runtimes" in f for f in hollow_report.failures)))

    # 5. a good nupkg must pass (built here, so the check is not only ever seen failing)
    good_report = sandbox_run(package("good.nupkg"))
    results.append(("a complete nupkg passes", good_report.ok))

    # 5a. the same package without the manifests: the real 2026-10-02 regression, kept as a fixture
    nomans_report = sandbox_run(package("nomans.nupkg", skip_manifests=True))
    results.append(("a nupkg whose dictionary copy omits a manifest fails",
                    not nomans_report.ok and any("dialect.json" in f for f in nomans_report.failures)))

    # 5b. the reference assembly, and its documentation file, are required (D40 = B)
    nonotices_report = sandbox_run(package("nonotices.nupkg", notices=False))
    results.append(("a nupkg without THIRD_PARTY_NOTICES.md fails",
                    not nonotices_report.ok
                    and any("no THIRD_PARTY_NOTICES.md" in f for f in nonotices_report.failures)))

    noref_report = sandbox_run(package("noref.nupkg", refs=()))
    results.append(("a nupkg with no ref/ assembly fails", not noref_report.ok and any("ref/" in f for f in noref_report.failures)))

    # 5b''. the two RID matrices must agree, and every rid needs an extension rule
    wf = sandbox / ".github" / "workflows"
    wf.mkdir(parents=True, exist_ok=True)
    (wf / "release_prep.yml").write_text("        include:\n          - rid: win-x64\n          - rid: linux-x64\n",
                                          encoding="utf-8")
    (wf / "test_csharp.yml").write_text("        include:\n          - rid: win-x64\n", encoding="utf-8")
    rid_report = Report()
    check_rid_matrix(sandbox, rid_report)
    results.append(("a shipped runtime that no test leg covers fails",
                    not rid_report.ok and any("never tests them" in f for f in rid_report.failures)))

    (wf / "test_csharp.yml").write_text(
        "        include:\n          - rid: win-x64\n          - rid: linux-x64\n          - rid: wasm-unknown\n",
        encoding="utf-8")
    unknown_report = Report()
    check_rid_matrix(sandbox, unknown_report)
    results.append(("a runtime identifier with no native-extension rule fails",
                    not unknown_report.ok and any("no native-extension rule" in f for f in unknown_report.failures)))

    (wf / "test_csharp.yml").write_text(
        "        include:\n          - rid: win-x64\n          - rid: linux-x64\n", encoding="utf-8")
    agree_report = Report()
    check_rid_matrix(sandbox, agree_report)
    results.append(("matching rid matrices pass", agree_report.ok))

    # 5g. the two release pages may not show two recipes for one tool
    pages = sandbox / "pages"
    (pages / "docs").mkdir(parents=True, exist_ok=True)
    (pages / "docs" / "RELEASE_CHECKLIST.md").write_text(
        "```bash\npython tools/check_wheel.py dist/*.whl --strict\n```\n", encoding="utf-8")
    (pages / "docs" / "RELEASE_RUNBOOK.md").write_text(
        "```bash\npython tools/check_wheel.py dist/*.whl --strict\n```\n", encoding="utf-8")
    same_recipe = Report()
    check_release_commands(pages, same_recipe)
    results.append(("two pages showing the same command pass", same_recipe.ok))

    (pages / "docs" / "RELEASE_RUNBOOK.md").write_text(
        "```bash\npython tools/check_wheel.py dist/*.whl\n```\n", encoding="utf-8")
    two_recipes = Report()
    check_release_commands(pages, two_recipes)
    results.append(("two pages disagreeing about one command fail",
                    not two_recipes.ok and any("spelled differently" in f for f in two_recipes.failures)))

    # 5f. the branch a listing links into must be one branch, and it must exist
    links = sandbox / "links"
    (links / "python").mkdir(parents=True, exist_ok=True)
    (links / "python" / "setup.py").write_text(
        'setup(project_urls={"Changelog": "https://github.com/wolfgitpr/cpp-pinyin/blob/v2/CHANGELOG.md"})\n',
        encoding="utf-8")
    one_branch = Report()
    check_release_url_paths(links, one_branch)
    results.append(("one branch across the release links is accepted", one_branch.ok))

    (links / "python" / "setup.py").write_text(
        'setup(project_urls={"Changelog": "https://github.com/wolfgitpr/cpp-pinyin/blob/v2/CHANGELOG.md",\n'
        '                     "Docs": "https://github.com/wolfgitpr/cpp-pinyin/blob/main/docs/QUICKSTART.md"})\n',
        encoding="utf-8")
    two_branches = Report()
    check_release_url_paths(links, two_branches)
    results.append(("release links into two different branches fail",
                    not two_branches.ok and any("more than one branch" in f for f in two_branches.failures)))

    # 5e. the nupkg's licence expression must not understate the licences of the data it installs
    # Its OWN tree: writing licence texts into the shared sandbox changed what the controls after this case saw,
    # which is how a self-test starts reporting on a tree it did not build (measured: 58/70).
    licence_tree = sandbox / "licence"
    (licence_tree / "csharp" / "CppPinyin").mkdir(parents=True, exist_ok=True)
    (licence_tree / "data" / "dialects" / "mandarin").mkdir(parents=True, exist_ok=True)
    (licence_tree / "data" / "dialects" / "cantonese").mkdir(parents=True, exist_ok=True)
    (licence_tree / "data" / "dialects" / "mandarin" / "License.txt").write_text(
        "# https://creativecommons.org/licenses/by-sa/4.0/\n", encoding="utf-8")
    (licence_tree / "data" / "dialects" / "cantonese" / "License.txt").write_text(
        "Creative Commons Attribution-Share Alike 3.0 (http://creativecommons.org/licenses/by-sa/3.0/)\n",
        encoding="utf-8")
    licence_ok = Report()
    (licence_tree / "csharp" / "CppPinyin" / "CppPinyin.csproj").write_text(
        "<Project><PropertyGroup>"
        "<PackageLicenseExpression>Apache-2.0 AND CC-BY-SA-3.0 AND CC-BY-SA-4.0</PackageLicenseExpression>"
        "</PropertyGroup></Project>\n", encoding="utf-8")
    check_package_licence(licence_tree, licence_ok)
    results.append(("a licence expression covering code and both dictionaries passes", licence_ok.ok))

    understated = Report()
    (licence_tree / "csharp" / "CppPinyin" / "CppPinyin.csproj").write_text(
        "<Project><PropertyGroup>"
        "<PackageLicenseExpression>Apache-2.0 AND CC-BY-SA-4.0</PackageLicenseExpression>"
        "</PropertyGroup></Project>\n", encoding="utf-8")
    check_package_licence(licence_tree, understated)
    results.append(("a licence expression that omits a shipped dictionary licence fails",
                    not understated.ok and any("omits CC-BY-SA-3.0" in f for f in understated.failures)))

    # 5c. the release surface: a repository URL that names a repository other than the remote is a package
    # pointing at someone else's code, and it is invisible to every other check in this file.
    surface = sandbox / "surface"
    (surface / "csharp" / "CppPinyin").mkdir(parents=True, exist_ok=True)
    (surface / "python").mkdir(parents=True, exist_ok=True)
    (surface / "README.md").write_text(
        "GIT_REPOSITORY https://github.com/wolfgitpr/cpp-pinyin.git\n", encoding="utf-8")
    (surface / "csharp/CppPinyin/CppPinyin.csproj").write_text(
        "<Project>\n  <PackageProjectUrl>https://github.com/wolfgitpr/cpp-pinyin</PackageProjectUrl>\n"
        "  <RepositoryUrl>https://github.com/wolfgitpr/cpp-pinyin</RepositoryUrl>\n"
        "  <TargetFrameworks>net7.0;net10.0</TargetFrameworks>\n</Project>\n", encoding="utf-8")
    (surface / "python/setup.py").write_text(
        'setup(project_urls={"Homepage": "https://github.com/wolfgitpr/cpp-pinyin",\n'
        '                     "Source": "https://github.com/wolfgitpr/cpp-pinyin"})\n', encoding="utf-8")
    agreeing = Report()
    check_release_surface(surface, agreeing)
    results.append(("a release surface that names one repository passes", agreeing.ok))

    (surface / "README.md").write_text(
        "GIT_REPOSITORY https://github.com/wolfgitpr/cpp-pinyin2.git\n", encoding="utf-8")
    drifted = Report()
    check_release_surface(surface, drifted)
    results.append(("a repository URL that disagrees with the other release files fails",
                    not drifted.ok and any("named more than one way" in f for f in drifted.failures)))
    (surface / "README.md").write_text(
        "GIT_REPOSITORY https://github.com/wolfgitpr/cpp-pinyin.git\n", encoding="utf-8")

    # 5d. a test project targeting a framework the package does not publish: green in CI, broken for consumers
    (surface / "csharp/CppPinyin.Tests").mkdir(parents=True, exist_ok=True)
    (surface / "csharp/CppPinyin.Tests/CppPinyin.Tests.csproj").write_text(
        "<Project>\n  <TargetFrameworks>net6.0</TargetFrameworks>\n</Project>\n", encoding="utf-8")
    extra_tfm = Report()
    check_release_surface(surface, extra_tfm)
    results.append(("a test project on a framework the package does not publish fails",
                    not extra_tfm.ok and any("does not publish" in f for f in extra_tfm.failures)))

    # 5b'. the assembly name must be the one the project PACKS, not a name this gate happens to remember.
    # Before 2026-10-21 both the gate and these fixtures said "CppPinyin.dll" and the project said
    # "CppPinyin2": the real package failed the gate, and no self-test could see why. This case is the
    # anti-regression -- a package carrying the old literal must now fail, naming the expected assembly.
    wrongname_report = sandbox_run(package("wrongname.nupkg", assembly="CppPinyin"))
    results.append(("a nupkg whose assembly name is not the one the project declares fails",
                    not wrongname_report.ok
                    and any(managed_assembly_name(ROOT) in f for f in wrongname_report.failures)))

    refdll_report = sandbox_run(package("refdll.nupkg", xml=False))
    results.append(("a nupkg whose ref/ has no documentation file fails",
                    not refdll_report.ok and any("documentation" in f for f in refdll_report.failures)))

    # 5c. Four shapes the read-only review round showed were accepted: a package with no dictionary copy at
    #     all (both comparisons were skipped), a native asset that is not the right kind of file for its rid,
    #     an empty native asset, a ref/ group covering one framework while lib/ covers several. All four
    #     install and then fail in a consumer's hands -- keep them red.
    nodict_report = sandbox_run(package("nodict.nupkg", dicts=False))
    results.append(("a nupkg with no dictionary copy at all fails",
                    not nodict_report.ok and any("no dictionary copy at all" in f for f in nodict_report.failures)))

    wrongext_report = sandbox_run(package("wrongext.nupkg", native="libcpp-pinyin2.so"))
    results.append(("a native asset that is not a .dll under win-x64 fails",
                    not wrongext_report.ok and any("not a .dll file" in f for f in wrongext_report.failures)))

    emptynative_report = sandbox_run(package("emptynative.nupkg", native_data=b""))
    results.append(("an empty native asset fails",
                    not emptynative_report.ok and any("is empty" in f for f in emptynative_report.failures)))

    refgap_report = sandbox_run(package("refgap.nupkg", libs=("net7.0", "net10.0"), refs=("net7.0",)))
    results.append(("a ref/ group that misses a lib/ framework fails",
                    not refgap_report.ok and any("compile against the runtime assembly" in f for f in refgap_report.failures)))

    # 6. the tag (version chain place 1b). Nothing else looks at it: ci.yml triggers on branches only,
    #    so a release tag can disagree with the single version source and every existing gate stays
    #    green. Both directions are checked here, because a tag check that only ever says yes is the
    #    shape this gate is meant to catch.
    bad_tag = sandbox_run(tag="v2.1.0")
    results.append(("a mismatched tag fails", not bad_tag.ok and any("place 1b" in f for f in bad_tag.failures)))
    good_tag = sandbox_run(tag="v2.0.0")
    results.append(("the matching tag passes", good_tag.ok))

    # 4. Workflow wiring. These get their own fixtures rather than the shared sandbox, because the
    #    sandbox deliberately contains no .github/workflows at all.
    def wiring_tree(name: str, *, callable_target: bool = True, needs: str = "[cpp, csharp]",
                    always: bool = True, ghost: bool = False) -> Path:
        tree = Path(sandbox) / "wiring" / name
        wf = tree / ".github" / "workflows"
        wf.mkdir(parents=True, exist_ok=True)
        trigger = "workflow_call" if callable_target else "push"
        gate = "    if: always()\n" if always else ""
        # Commented out, not deleted: this is the reviewer's counterexample, where the aggregate still
        # needs a job that no longer runs and the wiring check used to call that consistent.
        csharp_job = ("  # csharp:\n  #   uses: ./.github/workflows/lang.yml\n" if ghost
                      else "  csharp:\n    uses: ./.github/workflows/lang.yml\n")
        # Written with LF through open(..., newline="\n"). Path.write_text(newline=...) exists only from
        # Python 3.10, and this repository's declared floor is 3.9; the board caught that pattern in
        # python/tools/build_native.py, and it was in this self test as well.
        for file_name, body in (
            ("lang.yml",
             f"name: lang\non:\n  {trigger}:\n\njobs:\n  build:\n    runs-on: ubuntu-latest\n"
             f"    steps:\n      - run: echo hi\n"),
            ("runner.yml",
             f"name: runner\non:\n  push:\n\njobs:\n"
             f"  cpp:\n    uses: ./.github/workflows/lang.yml\n"
             f"{csharp_job}"
             f"  gate:\n{gate}    needs: {needs}\n    runs-on: ubuntu-latest\n"
             f"    steps:\n      - run: echo ok\n"),
        ):
            with (wf / file_name).open("w", encoding="utf-8", newline="\n") as handle:
                handle.write(body)
        return tree

    def wiring_fails(tree: Path) -> bool:
        report = Report()
        check_workflow_wiring(tree, report)
        return not report.ok

    good = wiring_tree("good")
    ok_report = Report()
    check_workflow_wiring(good, ok_report)
    results.append(("a correctly wired scheduler passes", ok_report.ok))

    results.append(("a called workflow without workflow_call fails",
                    wiring_fails(wiring_tree("not-callable", callable_target=False))))
    results.append(("an aggregate that does not need every caller fails",
                    wiring_fails(wiring_tree("missing-need", needs="[cpp]"))))
    results.append(("an aggregate without if: always() fails",
                    wiring_fails(wiring_tree("no-always", always=False))))

    # 4b. The artifact steps themselves: a different failure from the wiring above, and the one that was
    #     really present -- a job that aggregates correctly while the check it exists for has been deleted.
    #     Fixtures must be real workflows, not bare scalars: two reviewers showed a commented-out step passed
    #     the old string-only fixture, and a fixture the check cannot fail against proves nothing.
    def artifact_tree(name: str, *, drop: Optional[str] = None, comment_out: Optional[str] = None,
                      disable: Optional[str] = None, omit_file: Optional[str] = None) -> Path:
        tree = Path(sandbox) / "artifacts" / name
        folder = tree / ".github" / "workflows"
        folder.mkdir(parents=True, exist_ok=True)
        for workflow in ("test_python.yml", "test_csharp.yml"):
            if workflow == omit_file:
                continue
            steps = []
            for owner, fragment in REQUIRED_ARTIFACT_STEPS:
                if owner != workflow:
                    continue
                if fragment == drop:
                    continue
                if fragment == disable:
                    fragment = fragment + "-DISABLED"
                if fragment == comment_out:
                    steps.append(f"      # {fragment}   <-- commented out during a refactor")
                else:
                    steps.append(f"      - run: {fragment}")
            body = ("name: fixture\n"
                    "on:\n"
                    "  push:\n"
                    "\n"
                    "jobs:\n"
                    "  build:\n"
                    "    runs-on: ubuntu-latest\n"
                    "    steps:\n" + "\n".join(steps) + "\n")
            with (folder / workflow).open("w", encoding="utf-8", newline="\n") as handle:
                handle.write(body)
        return tree

    good_artifacts = Report()
    check_artifact_wiring(artifact_tree("good"), good_artifacts)
    results.append(("a workflow set with every artifact step passes", good_artifacts.ok))

    dropped = Report()
    check_artifact_wiring(artifact_tree("dropped", drop="tools/check_bindings.py --wheel"), dropped)
    results.append(("a workflow that dropped an artifact step fails",
                    not dropped.ok and any("check_bindings.py --wheel" in f for f in dropped.failures)))

    commented = Report()
    check_artifact_wiring(artifact_tree("commented", comment_out="tools/check_bindings.py --wheel"), commented)
    results.append(("a commented-out artifact step fails, instead of being covered by the header comment "
                    "that documents it",
                    not commented.ok and any("check_bindings.py --wheel" in f for f in commented.failures)))

    disabled = Report()
    check_artifact_wiring(artifact_tree("disabled", disable="tools/check_bindings.py --wheel"), disabled)
    results.append(("a renamed flag does not count as the step, though it contains the fragment",
                    not disabled.ok and any("check_bindings.py --wheel" in f for f in disabled.failures)))

    absent = Report()
    check_artifact_wiring(Path(sandbox) / "artifacts" / "empty", absent)
    results.append(("a missing language workflow fails", not absent.ok))

    results.append(("a job that needs a job which was commented out fails",
                    wiring_fails(wiring_tree("ghost-need", ghost=True))))

    # 4c. The wheel's members: a reviewer built four wheels this gate accepted (every dictionary file empty,
    #     one table missing, an extra file, none of them there at all) while it compared by dialect NAME only.
    #     The comparison lives in python/tools/check_wheel.py, run by CI as its own step; the sandbox copies
    #     that script and a staged package layout, so the shared function is exercised here, not described.
    wheel_tree = Path(sandbox) / "wheelcheck"
    wheel_src = Path(__file__).resolve().parents[1] / "python" / "tools" / "check_wheel.py"
    (wheel_tree / "python" / "tools").mkdir(parents=True, exist_ok=True)
    (wheel_tree / "python" / "tools" / "check_wheel.py").write_bytes(wheel_src.read_bytes())
    staged: Dict[str, Path] = {}
    for dialect in ("mandarin", "cantonese"):
        for member_name in ("word.txt", "dialect.json"):
            target = wheel_tree / "python" / "cpp_pinyin2" / "data" / "dict" / dialect / member_name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(f"{dialect}:{member_name}".encode("ascii"))
            staged[f"cpp_pinyin2/data/dict/{dialect}/{member_name}"] = target
    for member_name, payload in (("_native/libcpp-pinyin2.dll", b"not really a library"),
                                 ("shell.py", b"# a staged module\n")):
        target = wheel_tree / "python" / "cpp_pinyin2" / member_name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
        staged[f"cpp_pinyin2/{member_name}"] = target

    def wheel_fixture(name: str, *, drop: Optional[str] = None,
                      overwrite: Optional[Tuple[str, bytes]] = None) -> Path:
        path = Path(sandbox) / name
        with zipfile.ZipFile(path, "w") as archive:
            archive.writestr("cpp_pinyin2-2.0.0.dist-info/METADATA", "Version: 2.0.0\n")
            # The licence material the real wheel carries: `member_problems` now asserts it, and a fixture that
            # omits it is not a wheel this project would ship.
            for licence_member in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
                archive.writestr(f"cpp_pinyin2-2.0.0.dist-info/{licence_member}", "licence text")
            for member_name, source in sorted(staged.items()):
                if member_name != drop:
                    archive.writestr(member_name, source.read_bytes())
            if overwrite is not None:
                archive.writestr(overwrite[0], overwrite[1])
        return path

    wheel_ok = Report()
    check_wheel_members(wheel_tree, wheel_fixture("wheel-ok.whl"), wheel_ok)
    results.append(("a wheel whose members match the tree passes", wheel_ok.ok))

    wheel_missing = Report()
    check_wheel_members(wheel_tree, wheel_fixture("wheel-missing.whl",
                                                  drop="cpp_pinyin2/data/dict/mandarin/word.txt"), wheel_missing)
    results.append(("a wheel missing a dictionary member fails, which the dialect-name check accepted",
                    not wheel_missing.ok and any("missing from the wheel" in f for f in wheel_missing.failures)))

    wheel_stale = Report()
    check_wheel_members(wheel_tree,
                        wheel_fixture("wheel-stale.whl", overwrite=("cpp_pinyin2/data/dict/mandarin/word.txt",
                                                                   b"stale contents")), wheel_stale)
    results.append(("a wheel whose member bytes differ from the tree fails",
                    not wheel_stale.ok and any("packaged bytes differ" in f for f in wheel_stale.failures)))

    # 6e. The runtime identifier the package was packed for. CppPinyinRid is a pack-time parameter, so the
    #     package itself cannot say which rid it is; the workflow knows and passes it in (--rid). Without
    #     that argument, a package whose native asset was built for another platform passed everything here:
    #     the extension rule only asks whether the file suits the directory it happens to sit in.
    rid_match = sandbox_run(package("ridmatch.nupkg"), rid="win-x64")
    results.append(("a package checked as the rid it was packed for passes", rid_match.ok))

    rid_other = sandbox_run(package("ridother.nupkg"), rid="linux-x64")
    results.append(("a package asked to be checked as a rid it does not carry fails",
                    not rid_other.ok and any("was asked to be checked as" in f for f in rid_other.failures)))

    rid_unknown = sandbox_run(package("ridunknown.nupkg"), rid="win64")
    results.append(("an unknown rid is refused instead of silently skipping the comparison",
                    not rid_unknown.ok and any("not a runtime identifier this check knows" in f
                                                for f in rid_unknown.failures)))

    # 8. The plugin surface. Measured on the real tree before this check existed: adding a member to
    #    `Dialect` changed no gate, and the whole suite still passed. The headers and the frozen list are
    #    staged with the other frozen lists above; every fixture below mutates exactly one thing.
    plugin_clean = sandbox_run()
    results.append(("a plugin surface that matches its frozen list passes", plugin_clean.ok))
    results.append(("the extractor sees the registration macro, which the first version swallowed",
                    any("CPP_PINYIN_REGISTER_DIALECT" in line
                        for line in plugin_surface(sandbox))))

    plugin_dialect = plugin_dir / "Dialect.h"
    plugin_before = plugin_dialect.read_text(encoding="utf-8")
    write_lf(plugin_dialect, plugin_before.replace(
        "        bool supportsVToU = false;\n",
        "        bool supportsVToU = false;\n        bool experimental = false;\n"))
    plugin_added = sandbox_run()
    results.append(("a member added to Dialect fails, which nothing caught before this check existed",
                    not plugin_added.ok and any("plugin surface the frozen list does not know" in f
                                                for f in plugin_added.failures)))
    write_lf(plugin_dialect, plugin_before)

    write_lf(plugin_dialect, plugin_before.replace("        std::string id;\n", ""))
    plugin_removed = sandbox_run()
    # The Python mirror check (audit round 2 §12.2 API P1) gets its own control: without one, a gate that
    # compares seven ctypes classes would be a gate nobody ever saw go red.
    ffi_path = sandbox / "python" / "cpp_pinyin2" / "_ffi.py"
    ffi_before = ffi_path.read_text(encoding="utf-8")
    write_lf(ffi_path, ffi_before.replace('        ("reserved_0", ctypes.c_int32),\n', ""))
    ffi_drift = sandbox_run()
    results.append(("a field removed from a Python ctypes mirror fails",
                    not ffi_drift.ok and any("ctypes mirrors drifted" in f for f in ffi_drift.failures)))
    write_lf(ffi_path, ffi_before)

    results.append(("a member removed from Dialect fails",
                    not plugin_removed.ok and any("frozen plugin declarations that are gone" in f
                                                  for f in plugin_removed.failures)))
    write_lf(plugin_dialect, plugin_before)

    plugin_reordered = plugin_before.replace(
        "        std::string id;\n        std::string dictSubDir;\n",
        "        std::string dictSubDir;\n        std::string id;\n")
    if plugin_reordered == plugin_before:
        raise SystemExit("SELF-TEST FAULT: the reorder fixture changed nothing, so it would pass for the "
                         "wrong reason")
    write_lf(plugin_dialect, plugin_reordered)
    results.append(("reordering Dialect members fails: it is an aggregate, so order is layout",
                    not sandbox_run().ok))
    write_lf(plugin_dialect, plugin_before)

    write_lf(surface_list, "cpp-pinyin2/ext/Dialect.h: std::string id;\n")
    results.append(("a frozen list too short to describe the surface is refused",
                    not sandbox_run().ok))
    write_lf(surface_list, "\n".join(plugin_surface(sandbox)) + "\n")

    print("self-test:")
    for name, passed in results:
        print(f"  {'PASS' if passed else 'FAIL'}  {name}")
    failed = [name for name, passed in results if not passed]
    print(f"self-test: {len(results) - len(failed)}/{len(results)}")
    shutil.rmtree(sandbox, ignore_errors=True)
    return 0 if not failed else 1


def check_source_shape(root: Path, report: Report) -> None:
    """The script own shape: a check that is defined but never called does not run.

    This is the failure the module docstring used to hide -- it announced FOUR CHECKS while six kinds of
    check ran, and nothing connected the prose to the code. Two things are checked here: the numbered
    items in the docstring are 1..N with no gaps, and every check_* function defined in this file has a
    call site somewhere in the file. It is deliberately count-free, because a number in prose nobody can
    check is worse than no number at all."""
    source = Path(__file__).read_text(encoding="utf-8")
    items = [line for line in __doc__.splitlines() if re.match(r"^\d+\. ", line)]
    got = [line[:(len(str(i)) + 2)] for i, line in enumerate(items, 1)]
    expected = [str(i) + ". " for i in range(1, len(items) + 1)]
    if got != expected:
        report.fail("docstring items are not numbered 1..N without gaps: " + repr(got))
    else:
        report.note("docstring: " + str(len(items)) + " numbered item(s), 1.." + str(len(items)))

    defined = [name for name, value in sorted(globals().items())
               if name.startswith("check_") and callable(value)]
    # A call site is an INDENTED `name(` line; the definition is `def name(` at column 0. Searching for
    # the name before the definition was the first version of this, and it reported every function as
    # uncalled -- which the sandbox runs turned into 45/55 in --self-test.
    lines = source.splitlines()
    uncalled = [name for name in defined
                if not any(line.strip().startswith(name + "(") for line in lines if line[:1].isspace())]
    if uncalled:
        report.fail("defined but never called: " + ", ".join(uncalled))
    else:
        report.note("every one of the " + str(len(defined)) + " check_* function(s) defined here has a call site")


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--nupkg", type=Path, help="a built CppPinyin nupkg to inspect")
    parser.add_argument("--wheel", type=Path, help="a built cpp_pinyin2 wheel to inspect (version chain place 4)")
    parser.add_argument("--rid", help="the runtime identifier the nupkg was packed for (matrix.rid in CI); "
                                      "without it nothing ties the native asset to the build that made it")
    parser.add_argument("--tag", help="the git tag to check against the single version source (version chain place 1b)")
    parser.add_argument("--self-test", action="store_true", help="prove the checks can fail, then exit")
    parser.add_argument("--emit-signatures", action="store_true",
                        help="write tests/abi/expected_c_api_signatures.v2.txt from the headers, then exit")
    parser.add_argument("--print-version", action="store_true",
                        help="print the single version source and exit; CI uses this to pin a consumer")
    options = parser.parse_args(argv)

    if options.self_test:
        return self_test()

    if options.emit_signatures:
        return write_signature_baseline(ROOT)

    if options.print_version:
        version = cmake_version(ROOT)
        if version is None:
            print("FAIL: cannot read the version out of CMakeLists.txt")
            return 1
        print(version)
        return 0

    report = run(ROOT, options.nupkg, options.wheel, rid=options.rid, tag=options.tag)
    print_report(report)
    return 0 if report.ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
