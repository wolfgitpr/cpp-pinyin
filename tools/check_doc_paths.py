#!/usr/bin/env python3
"""Refuse a document that sends the reader to a path this repository does not have.

WHY THIS EXISTS. `check_links` follows markdown LINKS and `audit_doc_anchors` resolves `path:line` anchors. A
bare `include/cpp-pinyin2/c_api.h` sitting in backticks inside a sentence is neither, so a document can name a
file that was renamed two generations ago while every other gate stays green. Measured on 2026-10-22 before
this tool existed: 663 path mentions across `docs/`, 28 distinct ones that resolve to nothing.

WHAT IT DOES NOT JUDGE. Whether the sentence around the path is TRUE -- a path can exist and still be cited for
something it does not do (`check_doc_claims` and the anchor audit are for that). This is the cheapest possible
question: does the name still point at a file.

WHAT IS ALLOWED, and why each allowance is written down rather than inferred:

  * `cross-repo` -- the path belongs to the dictionary repository (`pinyin-makedict`), which is checked out
    beside this one and is where the generators, the corpus scripts and the dialect-study documents live. The
    list is explicit: guessing "looks like it is not ours" is how a silently wrong citation is born.
  * `historical` -- the file declares, in its header, that its line numbers and paths describe a tree that has
    moved (`doc-anchors: historical` / `doc-claims: historical`). History may name dead paths; that is what it
    is for.
  * `retirement index` -- `docs/HISTORY.md` section 5 IS the table of retired paths, so it must be allowed to
    name them.

USAGE
    python tools/check_doc_paths.py            # 0 = every path resolves or is allowed, 1 = findings, 2 = setup
    python tools/check_doc_paths.py --self-test
"""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys

#: Paths that belong to the sibling repository. Each entry says which one, because "not ours" needs a reason.
CROSS_REPO = {
    "tools/check_drift.ps1": "pinyin-makedict",
    "data/sources/": "pinyin-makedict",
    "gen/": "pinyin-makedict",
    "tests/test_conformance.py": "pinyin-makedict",
    "tools/compare_dict_dirs.py": "pinyin-makedict",
    "tools/check_binary.py": "pinyin-makedict",
    "tools/run_versions.py": "pinyin-makedict",
    "tools/corpus/": "pinyin-makedict",
    "data/overrides/man_overrides.py": "pinyin-makedict",
    "data/overrides/can_overrides.py": "pinyin-makedict",
    "data/sources/fanjian2.txt": "pinyin-makedict",
    "data/tc_origin_vocab.txt": "pinyin-makedict",
    "docs/DIALECT_EXTENSION_RECORD.md": "pinyin-makedict",
    "docs/DIALECT_EXTENSION_DESIGN.md": "pinyin-makedict",
    "docs/DIALECT_ACCURACY_PROTOCOL.md": "pinyin-makedict",
    "docs/DIALECT_ROLLOUT_RESEARCH.md": "pinyin-makedict",
    "docs/MULTILINGUAL_SUPPORT_RESEARCH.md": "pinyin-makedict",
    "docs/NEXT_SESSION_MULTILINGUAL.md": "pinyin-makedict",
    "tests/test_conformance.py": "pinyin-makedict",
    "docs/TOOLING.md": "pinyin-makedict",
    "docs/REPO_SCOPE.md": "pinyin-makedict",
    "common.py": "pinyin-makedict",
    "mandarin.py": "pinyin-makedict",
    "cantonese.py": "pinyin-makedict",
}

#: Documents whose header opts them out: they describe a tree that has moved.
HISTORICAL_MARKERS = ("doc-anchors: historical", "doc-claims: historical")

#: The retirement index may name retired paths -- that is its whole job.
RETIREMENT_INDEX = "docs/HISTORY.md"

#: The archive index names archived paths on purpose -- it is the page that says where they went.
ARCHIVE_INDEX = "docs/TESTDATA_ARCHIVE.md"

#: Names that documents cite BECAUSE they are gone -- "the header was deleted" is a true sentence that a
#: path-existence checker would otherwise flag. Each entry states why it is expected to be absent, so adding one
#: is a decision rather than an oversight.
RETIRED_NAMES = {
    "include/cpp-pinyin2/dialects/Mandarin.h": "deleted with the per-dialect classes (docs/API_*.md say so)",
    "include/cpp-pinyin2/dialects/Cantonese.h": "deleted with the per-dialect classes (docs/API_*.md say so)",
    "tests/cpp/test_batch_accuracy.cpp": "deleted 2026-09-29, replaced by tests/cpp/test_accuracy_v2.cpp",
    "tests/Common.cpp": "deleted; the shared helpers live in the per-suite translation units",
    "tests/registry/linked_in.cpp": "deleted; the linked-in probe is no longer in the tree",
    # Removed 2026-10-23 as process records whose work had landed. Dated documents still cite them, and a dated
    # citation is a true sentence about the day it was written -- what the repository must not do is imply the
    # file is still there, which is why the pointers were reworded where they were load-bearing.
    "docs/audit-report.md": "deleted 2026-10-23; the four-domain audit's fixes are the commits, and its open "
                            "items moved to docs/DECISIONS.md",
    "docs/REPRODUCE_CLAIMS.md": "deleted 2026-10-23; it was a snapshot whose rows named tools that never shipped",
    "docs/REPO_LAYOUT.md": "deleted 2026-10-23; the restructure it planned landed, and the surviving record "
                           "of what moved is git history",
    "docs/HISTORY.md": "deleted 2026-10-23 with the other process records; the facts a live document still "
                       "needs are stated in that document, and the narration is in git history",
    "docs/BACKLOG.md": "deleted 2026-10-23; open items moved to docs/DECISIONS.md sections 3 and 8",
    "docs/PLUGIN_INTERFACE_AND_BINDINGS.md": "deleted 2026-10-23; its rulings migrated to docs/DECISIONS.md "
                                             "section 2.1 and its fixture registry to conformance/README.md",
    "docs/TESTDATA_ARCHIVE.md": "deleted 2026-10-23 with the dated review records it indexed; the frozen accuracy table lives in docs/REQUIREMENTS.md",
    "tests/data/testdata/": "the dated review records were removed 2026-10-23; the results they carried are the accuracy table in docs/REQUIREMENTS.md",
}

#: Paths owned by third parties that documents legitimately cite as reference material.
EXTERNAL = ("include/tree_sitter/api.h",)

#: Paths a BUILD produces. They are correctly absent from git and correctly cited by the documents that say
#: "read this after configuring": the version stamp the two shells generate, and nothing else so far. Listing
#: them is the point -- everything else a document names must be in `git ls-files`, or a reader on a fresh
#: clone meets a path that exists only on the machine that wrote the sentence. That is exactly how the board
#: harness (`tools/board/*.sh`) stayed cited after it moved out of the repository on 2026-10-23.
GENERATED = {
    "csharp/CppPinyin/version.props": "generated from CMakeLists.txt at configure time",
    "python/cpp_pinyin2/_version.py": "generated by build_native.py from CMakeLists.txt",
}

ROOTS = ("include", "src", "tests", "tools", "python", "csharp", "examples", "packaging", "conformance", "data")
PATH_RX = re.compile(
    r"(?<![\w/.-])((?:%s)/[A-Za-z0-9_./-]*?\.(?:hpp|cpp|cs|cmake|props|csproj|ps1|h|c|py|txt|json|yml|yaml|md|sh|in))"
    r"(?![\w])" % "|".join(ROOTS)
)


def tracked(root: pathlib.Path) -> set[str]:
    out = subprocess.run(["git", "ls-files"], cwd=root, capture_output=True, text=True, encoding="utf-8")
    return set(out.stdout.splitlines())


def allowed(path: str) -> str | None:
    for prefix, repo in CROSS_REPO.items():
        if path == prefix or path.startswith(prefix):
            return f"cross-repo ({repo})"
    return None


def scan(root: pathlib.Path) -> list[tuple[str, int, str, str]]:
    have = tracked(root)
    findings: list[tuple[str, int, str, str]] = []
    docs = sorted((root / "docs").rglob("*.md"))
    if not docs:
        raise SystemExit("check_doc_paths: no documents under docs/ -- refusing to report on nothing")
    for doc in docs:
        rel = doc.relative_to(root).as_posix()
        text = doc.read_text(encoding="utf-8", errors="replace")
        head = "\n".join(text.splitlines()[:40])
        if rel in (RETIREMENT_INDEX, ARCHIVE_INDEX):
            continue
        if any(marker in head for marker in HISTORICAL_MARKERS):
            continue
        for match in PATH_RX.finditer(text):
            path = match.group(1)
            if path in have or path in GENERATED:
                continue
            if path in EXTERNAL or path in RETIRED_NAMES or allowed(path):
                continue
            line = text[: match.start()].count("\n") + 1
            # Distinguish the two shapes, because they are fixed differently: a path that is nowhere is a typo
            # or a retirement, while a path that is on THIS disk but not in git is the worse one -- it works for
            # whoever wrote the sentence and for nobody else.
            if (root / path).exists():
                findings.append((rel, line, path,
                                 "exists in this working tree but is not tracked, so a fresh clone has no such "
                                 "file; untrack it from the document too, or add it to GENERATED if a build "
                                 "produces it"))
            else:
                findings.append((rel, line, path, "no such tracked file or directory entry"))
    return findings


def self_test() -> int:
    """Prove this checker can go red, and that each allowance actually allows."""
    import tempfile

    cases = 0
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        (root / "docs").mkdir()
        (root / "src").mkdir()
        (root / "src" / "real.cpp").write_text("// x\n", encoding="utf-8")
        subprocess.run(["git", "init", "-q"], cwd=root, check=True)
        subprocess.run(["git", "add", "-A"], cwd=root, check=True)

        def check(name: str, body: str, want: int) -> None:
            nonlocal cases, failures
            (root / "docs" / "doc.md").write_text(body, encoding="utf-8")
            subprocess.run(["git", "add", "-A"], cwd=root, check=True, capture_output=True)
            got = len(scan(root))
            cases += 1
            if got != want:
                failures += 1
                print(f"FAIL self-test: {name} -- expected {want} finding(s), got {got}")
            else:
                print(f"PASS self-test: {name}")

        check("a path that exists resolves", "see `src/real.cpp`\n", 0)
        check("a path that does not exist is a finding", "see `src/gone.cpp`\n", 1)

        # The case that made this gate miss the board harness for months: a file that IS on this disk but was
        # never committed. The old rule accepted it, so the sentence worked for its author and for nobody else.
        # Written inline rather than through check(), because check() stages everything with `git add -A` and
        # staging the file is exactly what would hide it again (learned from the failing first version).
        (root / "src" / "local_only.cpp").write_text("// never committed\n", encoding="utf-8")
        (root / "docs" / "doc.md").write_text("see `src/local_only.cpp`\n", encoding="utf-8")
        subprocess.run(["git", "add", "docs"], cwd=root, check=True, capture_output=True)
        got = len(scan(root))
        cases += 1
        if got != 1:
            failures += 1
            print(f"FAIL self-test: a path that exists only on this disk is a finding -- got {got} finding(s)")
        else:
            print("PASS self-test: a path that exists only on this disk is a finding")
        (root / "src" / "local_only.cpp").unlink()

        check("a cross-repo path is allowed", "see `tools/check_drift.ps1`\n", 0)
        check("a historical document may name a dead path",
              "doc-anchors: historical\n\nsee `src/gone.cpp`\n", 0)
        (root / "docs" / "HISTORY.md").write_text("see `src/gone.cpp`\n", encoding="utf-8")
        subprocess.run(["git", "add", "-A"], cwd=root, check=True, capture_output=True)
        # the retirement index writes its own findings into doc.md; point doc.md at something real first
        (root / "docs" / "doc.md").write_text("see `src/real.cpp`\n", encoding="utf-8")
        got = len(scan(root))
        cases += 1
        if got != 0:
            failures += 1
            print(f"FAIL self-test: the retirement index may name retired paths -- got {got} finding(s)")
        else:
            print("PASS self-test: the retirement index may name retired paths")

    print(f"self-test: {cases} case(s), {failures} failure(s)")
    return 0 if failures == 0 else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=".", help="repository root (default: the current directory)")
    ap.add_argument("--self-test", action="store_true", help="prove this checker can see a violation")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    root = pathlib.Path(args.root).resolve()
    if not (root / "docs").is_dir():
        print(f"check_doc_paths: {root}/docs is missing -- nothing to scan")
        return 2
    findings = scan(root)
    for rel, line, path, why in findings:
        print(f"FAIL  {rel}:{line} -> {path} ({why})")
    if findings:
        print(f"\n{len(findings)} path mention(s) do not resolve. Fix the path, or -- if it belongs to the "
              f"sibling repository -- add it to CROSS_REPO in this file with the reason.")
        return 1
    docs = len(list((root / "docs").rglob("*.md")))
    print(f"check_doc_paths: PASS ({docs} document(s); every repository path they name resolves or is allowed)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
