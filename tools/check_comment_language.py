#!/usr/bin/env python3
"""Refuse a comment line that a reader of this repository's code cannot read.

Why: the comment migration was done by hand over many rounds, and by hand it can come back — a new Chinese
sentence in a comment is exactly as easy to write as an English one. The rule this enforces is the one the
migration converged on, and it is deliberately narrow so that everything kept on purpose stays legal:

  * a comment line counts only if it has NO Latin letters at all, at least four CJK characters, and its own
    text is not a quotation of an authoritative document. One or two characters is a term; a line with pinyin
    next to it is an example; a quoted contract sentence is quoted.

Exit codes: 0 = nothing to report, 1 = at least one line found (each printed with its location), 2 = setup
failure. `tools/self_test_common.py` is not used here: the checker must work when it is the only file present,
so the fixture construction is inline.
"""
from __future__ import annotations

import argparse
import pathlib
import re
import sys
import tempfile

CJK = re.compile(r"[\u4e00-\u9fff]")
LATIN = re.compile(r"[A-Za-z]")

# Files whose comments are quotations of an authoritative document, kept verbatim on purpose. The list is
# short and each entry says which document it quotes; an unlisted addition is a decision, not an oversight.
QUOTING_FILES = {
    "core/Syllable.h":
        "the four API-contract sentences, marked 'do not reword' in the header itself",
}


def is_quoting_file(rel: str) -> bool:
    """Whether a repository-relative path names a file whose comments quote a document.

    Keyed by the path INSIDE a generation directory, not by the whole path. Measured 2026-10-16 on the
    v3-day rehearsal tree: with the full path as the key, `include/cpp-pinyin3/core/Syllable.h` was not
    exempt, so the roll-out to a new generation turned a quoted Chinese sentence into a finding -- a red
    gate on a non-defect, on exactly the day nobody wants to be arguing about one. The quoted sentence is
    a property of the file, and the file moves generation with the tree.
    """
    if rel in QUOTING_FILES:
        return True
    parts = rel.split("/")
    for index in range(1, len(parts) - 1):
        if "/".join(parts[index:]) in QUOTING_FILES:
            return True
    return False

SKIP_PARTS = {"obj", "bin", "__pycache__", ".git", ".tmp", "build", "node_modules"}

DEFAULT_GLOBS = ("src/**/*.cpp", "src/**/*.h", "include/**/*.h", "tests/**/*.cpp", "tests/**/*.h",
                 "python/**/*.py", "csharp/**/*.cs", "tools/**/*.py")


def scan(root: pathlib.Path, globs=DEFAULT_GLOBS) -> tuple[list[tuple[str, int, str]], int]:
    """(findings, files_examined). A finding is (relative path, line number, the line)."""
    findings: list[tuple[str, int, str]] = []
    examined = 0
    for pattern in globs:
        for path in sorted(root.glob(pattern)):
            if any(part in SKIP_PARTS for part in path.parts):
                continue
            examined += 1
            rel = path.relative_to(root).as_posix()
            if is_quoting_file(rel):
                continue
            try:
                body = path.read_text(encoding="utf-8-sig", errors="replace")
            except OSError:
                continue
            for number, line in enumerate(body.splitlines(), 1):
                stripped = line.strip()
                if not stripped or not CJK.search(line):
                    continue
                if not stripped.startswith(("//", "#", "*", "/*", "///")):
                    continue
                if LATIN.search(line):
                    continue
                if len(CJK.findall(line)) < 4:
                    continue
                findings.append((rel, number, stripped))
    return findings, examined


def self_test() -> int:
    """Cases that prove both directions: a Chinese-only comment is found, and every kept-by-rule shape is not."""
    cases = [
        ("a Chinese-only comment is reported", "// 这是一行中文散文注释。\n", 1),
        ("a one-character term is not reported", "// 你好\na = 1\n", 0),
        ("a line with pinyin is not reported", "// 你好 -> ni3 hao3\n", 0),
        ("an English comment is not reported", "// the style carries no readable tone\n", 0),
        ("a quoted string in CODE is not reported", 'const char* s = u8"你好世界";\n', 0),
        ("a three-character term is not reported", "// 初始化\n", 0),
        ("a Chinese-only comment with four characters is reported", "// 不得引入七九\n", 1),
    ]
    failures = 0
    for note, body, expected in cases:
        with tempfile.TemporaryDirectory() as raw:
            root = pathlib.Path(raw)
            (root / "src").mkdir()
            (root / "src" / "sample.cpp").write_text(body, encoding="utf-8")
            findings, examined = scan(root, globs=("src/**/*.cpp",))
            if len(findings) != expected:
                print(f"  FAIL  {note}: expected {expected} finding(s), got {len(findings)} {findings}")
                failures += 1
            elif examined != 1:
                print(f"  FAIL  {note}: the fixture was not examined (examined={examined}) -- a pass would mean "
                      f"nothing")
                failures += 1
            else:
                print(f"  ok    {note}")

    # the quoting-file exemption must be real: the same body is reported unless the file is on the list
    with tempfile.TemporaryDirectory() as raw:
        root = pathlib.Path(raw)
        (root / "include" / "cpp-pinyin2" / "core").mkdir(parents=True)
        (root / "include" / "cpp-pinyin2" / "core" / "Syllable.h").write_text("// 不得引入七九\n", encoding="utf-8")
        findings, _ = scan(root, globs=("include/**/*.h",))
        if findings:
            print(f"  FAIL  a quoting file is exempt from the rule: got {findings}")
            failures += 1
        else:
            print("  ok    a quoting file is exempt from the rule")

    print(f"\nSELF-TEST: {len(cases) + 1 - failures}/{len(cases) + 1} case(s), {failures} failure(s)")
    return 1 if failures else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=".", help="repository root (default: the working directory)")
    parser.add_argument("--self-test", action="store_true", help="run the built-in cases and exit")
    parser.add_argument("--verbose", action="store_true", help="list every file examined")
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test()

    root = pathlib.Path(args.root).resolve()
    findings, examined = scan(root)
    if examined == 0:
        print(f"ERROR: no source file matched under {root}, so this check cannot say anything; it refuses to "
              f"pass. (A run that examined nothing is not a clean run.)")
        return 2

    if args.verbose:
        for pattern in DEFAULT_GLOBS:
            for path in sorted(root.glob(pattern)):
                if not any(part in SKIP_PARTS for part in path.parts):
                    print(f"  examined {path.relative_to(root).as_posix()}")

    print(f"comment language: {examined} file(s) examined, {len(findings)} problem(s)")
    for rel, number, text in findings:
        print(f"  UNREADABLE  {rel}:{number}")
        print(f"      {text[:104]}")
        print(f"      this comment has no Latin letters -- write it in English, or put it in QUOTING_FILES "
              f"with the document it quotes")
    if findings:
        return 1
    print("comment language: no comment line is Chinese-only (the quoting files are exempt, by name)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
