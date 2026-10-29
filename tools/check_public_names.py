#!/usr/bin/env python3
"""The frozen list of public names must agree with the headers, in both directions.

`tests/abi/expected_public_headers.txt` freezes the header FILES and `expected_c_api_symbols.txt` freezes
the C symbols, but nothing froze the C++ NAMES: a name appearing in an existing header is a public-surface
change no gate sees, and the generation policy (docs/STABILITY.md section 4.5) makes each name a promise the
next major version must keep or document as gone.

WHAT IT CHECKS
  * every namespace-scope name a public header declares has a row in the list, and every row names
    something really declared -- the list cannot drift in either direction;
  * the scan reports how many headers and declarations it read, and zero is RED ("found nothing" must not
    look like "found no problem");
  * only namespace scope counts: a member function, a nested alias or a private nested class is not a
    public name of the generation (`ToneConverter::ConvertFunc`, `Engine::Impl`);
  * **the list and the header tree are paired BY GENERATION**: `include/cpp-pinyinN/` checks against
    `tests/abi/expected_public_names.vN.txt`. Globbing every generation into one name set would mean a name
    that v3 DROPPED could never turn the check red -- an older generation still declares it, so the union
    still contains it. A tokenless `expected_public_names.txt` is RED with a rename hint.

USAGE
    python tools/check_public_names.py                 # exit 1 on any disagreement
    python tools/check_public_names.py --self-test     # fixtures + controls that must go red

歧义形态：**源码面** ✗ —— 每代的冻结名单与头里命名空间级声明**双向**一致，且声明的作用域必须真的在那一代的命名空间内（文件作用域声明是"根名回来"⇒ 判红）。
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path

#: The tokenless name is the one the first revision used. It is still RECOGNISED -- so that landing this
#: gate against a tree that has it produces a rename hint instead of a confusing "list is missing".
LEGACY_LIST_REL = "tests/abi/expected_public_names.txt"


def list_rel(major: int) -> str:
    """Each generation has its own frozen list: the generation token is in the file name."""
    return f"tests/abi/expected_public_names.v{major}.txt"


GENERATION_DIR = re.compile(r"^cpp-pinyin(\d+)$")

TYPE_RX = re.compile(r"^(class|struct|union|enum)(?:\s+(?:class|struct))?\s+"
                     r"(?:CPP_PINYIN\d+_EXPORT\s+)?(\w+)")
ALIAS_RX = re.compile(r"^using\s+(\w+)\s*=")
USING_DECL_RX = re.compile(r"^using\s+[\w:]+::(\w+)\s*;")
TYPEDEF_RX = re.compile(r"^typedef\b.*?(\w+)\s*;$", re.S)
FUNC_RX = re.compile(r"^(?:\[\[nodiscard\]\]\s*)?(?:CPP_PINYIN\d+_NODISCARD\s*)?(?:inline\s+)?"
                     r"[\w:<>,*&\s]+\b(\w+)\s*\(")
#: Statements at namespace level that are known NOT to introduce a name of this generation.
KNOWN_NONAME_RX = (
    (re.compile(r"^template\b"), "template head (the declaration follows)"),
    (re.compile(r"^namespace\s+\w"), "nested namespace"),
    (re.compile(r"^using\s+namespace\b"), "using-directive"),
    (re.compile(r"^static_assert\b"), "static_assert"),
    (re.compile(r"^(public|private|protected)\s*:"), "access specifier"),
    (re.compile(r"^extern\s+\"C\""), 'extern "C" block'),
    (re.compile(r"^};?$"), "closing brace"),
)
VERSION_NS_RX = re.compile(r"^\s*namespace\s+cpp_pinyin::v\d+\s*$", re.M)
VERSION_NS_LINE = re.compile(r"^\s*namespace\s+cpp_pinyin::v(\d+)\b")


class ScanError(RuntimeError):
    pass


def strip_comments(body: str, name: str) -> list[tuple[int, str]]:
    """(line number, code) with comments removed, strings preserved, one left-to-right pass.

    Two lessons are folded in here. Cutting `//` only (the first revision) made every `*`-continued
    line of a block comment look like code. Scanning for `/*` BEFORE cutting `//` (prepare_Pab.py, and
    this file's own second revision) lets a `/*` inside a `//` comment open a block that never closes,
    silently swallowing the rest of the file -- 34 real lines were lost that way in
    tests/cpp/test_comprehensive.cpp. An unterminated block comment is now a hard error.
    """
    rows: list[tuple[int, str]] = []
    in_block = False
    for number, line in enumerate(body.splitlines(), 1):
        chars: list[str] = []
        i = 0
        while i < len(line):
            if in_block:
                end = line.find("*/", i)
                if end < 0:
                    i = len(line)
                    break
                i = end + 2
                in_block = False
                continue
            two = line[i:i + 2]
            if two == "/*":
                in_block = True
                i += 2
                continue
            if two == "//":
                break
            if line[i] in "\"'":
                quote = line[i]
                chars.append(quote)
                i += 1
                while i < len(line):
                    chars.append(line[i])
                    if line[i] == "\\":
                        i += 2
                        continue
                    if line[i] == quote:
                        i += 1
                        break
                    i += 1
                continue
            chars.append(line[i])
            i += 1
        rows.append((number, "".join(chars)))
    if in_block:
        raise ScanError(f"{name}: unterminated /* block comment -- the scan would silently swallow the "
                        f"rest of the file")
    return rows


def drop_template_head(text: str) -> str:
    """Remove a leading `template <...>` (balanced angle brackets) and return the remainder."""
    if not text.startswith("template"):
        return text
    start = text.find("<")
    if start < 0:
        return text
    depth = 0
    for index in range(start, len(text)):
        if text[index] == "<":
            depth += 1
        elif text[index] == ">":
            depth -= 1
            if depth == 0:
                return text[index + 1:].strip()
    return text


def classify(statement: str) -> tuple[str, str]:
    """('name', X) | ('known', reason) | ('unknown', text) for one namespace-level statement.

    Fail-closed on purpose: the previous revision only recognised four shapes, so a `typedef`, a macro
    call or a statement split across lines was simply invisible -- and invisible meant "adding a public
    name does not require touching the frozen list", which is the one thing this gate exists to prevent.
    """
    text = " ".join(statement.split())
    if not text:
        return "known", "empty"
    # A template head belongs to the declaration that follows it, and that declaration DOES have a name
    # (Outcome is a class template in the frozen list). Treating `^template` as a known non-name -- the
    # first version of this classifier did -- silently dropped it: measured 35 names instead of 36.
    stripped_template = drop_template_head(text)
    if stripped_template != text:
        kind, value = classify(stripped_template)
        if kind == "known" and value == "empty":
            return "known", "template head"
        return kind, value
    for pattern, reason in KNOWN_NONAME_RX:
        if pattern.match(text):
            return "known", reason
    match = TYPE_RX.match(text)
    if match:
        rest = text[match.end():]
        if text.endswith(";") and "{" not in rest:
            return "known", "forward declaration"
        return "name", match.group(2)
    match = ALIAS_RX.match(text)
    if match:
        return "name", match.group(1)
    match = TYPEDEF_RX.match(text)
    if match:
        return "name", match.group(1)
    match = USING_DECL_RX.match(text)
    if match:
        return "known", "using-declaration (re-exports a name of this generation)"
    if FUNC_RX.match(text) and "(" in text:
        return "name", FUNC_RX.match(text).group(1)
    return "unknown", text


def scan(root: Path, product: str, major: int) -> tuple[dict[str, list[str]], int, list[str]]:
    """(name -> [file:line], headers examined, findings) for the C++ surface of ONE generation.

    Scope is tracked by BRACE DEPTH anchored at the version namespace, not by indentation. The old
    "exactly four spaces" rule was defeated by a tab, by eight spaces and by a line-leading block
    comment -- all of which are still valid C++ in this tree's style, and all of which meant a new
    public function or alias could be added without the frozen list noticing.

    One generation at a time, on purpose: the caller pairs this result with `expected_public_names.vN.txt`.
    Scanning every `cpp-pinyin*` directory into one set is what made "a name v3 dropped" unfalsifiable.
    """
    product_root = root / "include" / product
    names: dict[str, list[str]] = {}
    headers = 0
    findings: list[str] = []
    if product_root.is_dir():
        for path in sorted(product_root.rglob("*.h")):
            headers += 1
            rel = path.relative_to(root).as_posix()
            body = path.read_text(encoding="utf-8-sig", errors="replace")
            depth = 0
            ns_depth: int | None = None
            pending_ns = False
            statement: list[str] = []
            statement_line = 0
            for number, code in strip_comments(body, rel):
                stripped = code.strip()
                if not stripped or stripped.startswith("#"):
                    continue
                opens_line = VERSION_NS_LINE.match(stripped)
                at_ns_level = ns_depth is not None and depth == ns_depth
                if at_ns_level and not statement:
                    statement_line = number
                if at_ns_level:
                    statement.append(stripped)
                closes = ";" in stripped or "{" in stripped or "}" in stripped
                depth += stripped.count("{") - stripped.count("}")

                if opens_line:
                    # The namespace header itself is not a declaration. Its brace may arrive on the same
                    # line or the next, and `ns_depth` is the depth INSIDE it: that anchor is what makes
                    # indentation irrelevant (tab, 8 spaces, whatever the formatter does next).
                    if "{" in stripped:
                        ns_depth = depth
                    else:
                        pending_ns = True
                    statement = []
                    continue
                if pending_ns and "{" in stripped:
                    ns_depth = depth
                    pending_ns = False
                    statement = []
                    continue
                if at_ns_level and statement and closes:
                    kind, value = classify(" ".join(statement))
                    if kind == "name":
                        names.setdefault(value, []).append(f"{rel}:{statement_line}")
                    elif kind == "unknown":
                        findings.append(f"{rel}:{statement_line}: namespace-level statement this gate "
                                        f"cannot classify: `{value[:70]}` -- classify it (a name for the "
                                        f"frozen list, or a pattern in KNOWN_NONAME_RX); unknown must not "
                                        f"mean invisible")
                    statement = []
                if ns_depth is not None and depth < ns_depth:
                    ns_depth = None                       # left the version namespace
            if pending_ns:
                findings.append(f"{rel}: the version namespace declaration never opened a brace")
    return names, headers, findings


def find_root() -> Path:
    here = Path(__file__).resolve()
    for candidate in (here.parent, *here.parents):
        if (candidate / "CMakeLists.txt").is_file() and (candidate / "include").is_dir():
            return candidate
    return Path.cwd()


def code_line(line: str) -> str:
    """Drop a `//` comment; block comments are dropped by the caller's line filter below."""
    index = line.find("//")
    return line if index < 0 else line[:index]


def frozen(root: Path, major: int) -> list[str]:
    """The frozen rows for ONE generation. Read with `utf-8-sig`: a BOM on the first line turned the
    first row into a "name" (`\ufeff# frozen`) in the first revision -- this tree has shipped BOM trouble
    before, and a gate that invents a phantom name from a byte-order mark is worse than no gate.
    """
    path = root / list_rel(major)
    if not path.is_file():
        return []
    return [line.strip() for line in path.read_text(encoding="utf-8-sig").splitlines()
            if line.strip() and not line.strip().startswith("#")]


def generations(root: Path) -> list[tuple[str, int]]:
    """[(product directory, major)] for every `include/cpp-pinyinN/` present, oldest first."""
    include_root = root / "include"
    found: list[tuple[str, int]] = []
    if not include_root.is_dir():
        return found
    for product in sorted(include_root.glob("cpp-pinyin*")):
        match = GENERATION_DIR.match(product.name)
        if product.is_dir() and match:
            found.append((product.name, int(match.group(1))))
    return sorted(found, key=lambda item: item[1])


def check(root: Path) -> list[str]:
    """Per-generation comparison. See the module docstring for why the union version was wrong."""
    findings: list[str] = []
    generations_found = generations(root)
    if not generations_found:
        return ["ROOT-LEVEL: no include/cpp-pinyinN/ directory matched -- check the scan root, not the tree"]
    legacy = root / LEGACY_LIST_REL
    if legacy.is_file():
        findings.append(f"{LEGACY_LIST_REL} has no generation token in its name -- a frozen list must say "
                        f"which generation it belongs to: rename it to "
                        f"{list_rel(generations_found[-1][1])} (the list and the header tree are paired by "
                        f"generation, so one tokenless file cannot serve two generations)")
    for product, major in generations_found:
        names, headers, scan_findings = scan(root, product, major)
        listed = frozen(root, major)
        print(f"note: generation {major} ({product}): scanned {headers} header(s); "
              f"{len(names)} distinct public name(s) declared; {len(listed)} name(s) frozen")
        if not headers:
            findings.append(f"generation {major}: no public header matched under include/{product}/ -- "
                            f"check the scan root, not the tree")
            continue
        findings += scan_findings
        versioned = [path for path in (root / "include" / product).rglob("*.h")
                     if VERSION_NS_RX.search(path.read_text(encoding="utf-8", errors="replace"))]
        if not versioned:
            findings.append(f"generation {major}: no header under include/{product}/ opens a version "
                            f"namespace -- the scan is looking in the wrong place")
        if not listed:
            findings.append(f"{list_rel(major)} is missing or empty -- every generation needs its own "
                            f"frozen list (v{major} names cannot be checked against another list)")
            continue
        for name in sorted(set(names) - set(listed)):
            findings.append(f"generation {major}: {name} is declared but not frozen "
                            f"({', '.join(names[name][:3])}) -- add it to {list_rel(major)} in the same "
                            f"commit and note it in the migration guide")
        for name in sorted(set(listed) - set(names)):
            findings.append(f"generation {major}: {name} is frozen but is not declared by any header of "
                            f"this generation -- a removed or renamed name must leave "
                            f"{list_rel(major)} in the same commit")
    return findings


GOOD_HEADER = """#pragma once

namespace cpp_pinyin::v2
{
    class Engine
    {
    public:
        void query();
        class Impl;
    };

    struct Segment { int tone = 0; };
    enum class Code { Ok = 0 };

    using Callback = int (*)(int);

    int codeText(Code code);
    inline bool isHanzi(char16_t c) { return c != 0; }
}
"""


def append_to_fixture(base: Path, addition: str) -> None:
    """Insert a namespace-level statement into the fixture header, BEFORE its closing brace.

    Appending after the brace would put the statement at file scope, where it is legitimately not a
    public name of the generation -- the control would then pass for the wrong reason, which is the
    exact failure mode these controls exist to catch.
    """
    path = base / "include" / "cpp-pinyin2" / "core" / "Types.h"
    lines = path.read_text(encoding="utf-8").splitlines()
    close = max(index for index, line in enumerate(lines) if line.strip() == "}")
    lines.insert(close, addition.rstrip("\n"))
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="")


def add_generation(base: Path, major: int, body: str, names: str) -> None:
    """Add a second generation directory with its own frozen list (for the pairing controls)."""
    directory = base / "include" / f"cpp-pinyin{major}" / "core"
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "Types.h").write_text(body, encoding="utf-8", newline="")
    (base / list_rel(major)).write_text("# frozen\n" + names, encoding="utf-8", newline="")


def build_fixture(base: Path, names: str) -> None:
    directory = base / "include" / "cpp-pinyin2" / "core"
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "Types.h").write_text(GOOD_HEADER, encoding="utf-8", newline="")
    (base / "tests" / "abi").mkdir(parents=True, exist_ok=True)
    (base / list_rel(2)).write_text("# frozen\n" + names, encoding="utf-8", newline="")
    for other in ("cpp-pinyin3",):
        path = base / "include" / other
        if path.is_dir():
            for item in sorted(path.rglob("*"), reverse=True):
                item.unlink() if item.is_file() else item.rmdir()
            path.rmdir()
    third = base / list_rel(3)
    if third.is_file():
        third.unlink()
    tokenless = base / LEGACY_LIST_REL
    if tokenless.is_file():
        tokenless.unlink()


#: One line per case, in the shape tools/check_doc_claims.py measures: it counts `^(PASS|FAIL) self-test:`
#: lines from the tool's own output (`measure_selftests`). A summary-only self-test cannot be documented
#: -- docs/STABILITY.md §0 claims a case count per gate -- so landing this gate without these lines would
#: make that section either wrong or silently unmeasurable.
_SELF_TEST_FAILURES: list[str] = []
_SELF_TEST_CASES: list[str] = []


def report_case(name: str, ok: bool, detail: str = "") -> None:
    print(f"{'PASS' if ok else 'FAIL'} self-test: {name}")
    _SELF_TEST_CASES.append(name)
    if not ok:
        _SELF_TEST_FAILURES.append(f"{name}: {detail}")


def self_test() -> int:
    expected = "Engine\nSegment\nCode\nCallback\ncodeText\nisHanzi\n"
    with tempfile.TemporaryDirectory() as tmp:
        base = Path(tmp)
        controls = [
            ("a name declared but not frozen",
             lambda: build_fixture(base, expected.replace("isHanzi\n", "")),
             "isHanzi is declared but not frozen"),
            ("a frozen name that no header declares",
             lambda: build_fixture(base, expected + "Ghost\n"),
             "Ghost is frozen but is not declared"),
            ("an empty frozen list",
             lambda: build_fixture(base, ""),
             "is missing or empty"),
            ("an empty scan scope",
             lambda: (base / "include" / "cpp-pinyin2" / "core" / "Types.h").unlink(),
             "no public header matched"),
            # The seven below close the bypasses the adversarial review found on 2026-10-14. Every one of
            # them passed the previous revision, whose scope rule was "the line starts with exactly four
            # spaces": a tab, eight spaces, a `typedef` or a statement split across lines was invisible,
            # and invisible meant a new public name could be added without touching the frozen list.
            ("a tab-indented declaration",
             lambda: append_to_fixture(base, "\tint tabbed(int code);\n"),
             "tabbed is declared but not frozen"),
            ("an eight-space-indented alias",
             lambda: append_to_fixture(base, "        using EightSpaced = int;\n"),
             "EightSpaced"),
            ("a typedef",
             lambda: append_to_fixture(base, "    typedef int TypedefGhost;\n"),
             "TypedefGhost"),
            ("an alias split across two lines",
             lambda: append_to_fixture(base, "    using SplitGhost\n        = int;\n"),
             "SplitGhost"),
            ("`enum struct` (which the old regex read as a name called `struct`)",
             lambda: append_to_fixture(base, "    enum struct EnumStructGhost { A };\n"),
             "EnumStructGhost"),
            ("a namespace-level statement the classifier does not know",
             lambda: append_to_fixture(base, "    MACRO_DECLARE(MacroGhost);\n"),
             "cannot classify"),
            ("a `/*` inside a line comment must not swallow the rest of the file",
             lambda: append_to_fixture(base, "    // paths look like res/dict/*/word.txt\n"
                                             "    int AfterTheComment(int code);\n"),
             "AfterTheComment"),
            # This one is the reason the list is paired with its generation. `isHanzi` is declared by v2
            # and NOT by v3, and v3's list still keeps it: with the old "scan every cpp-pinyin* into one
            # name set" behaviour this was GREEN (the older generation's header supplied the name), which
            # made "v3 dropped a name" -- the very thing the migration guide promises -- unfalsifiable.
            ("a later generation that dropped a name its older sibling still declares",
             lambda: add_generation(base, 3,
                                    GOOD_HEADER.replace("cpp_pinyin::v2", "cpp_pinyin::v3")
                                    .replace("    inline bool isHanzi(char16_t c) { return c != 0; }\n",
                                             ""),
                                    expected),
             "generation 3: isHanzi is frozen but is not declared"),
            ("a later generation whose list keeps a name no generation declares",
             lambda: add_generation(base, 3,
                                    GOOD_HEADER.replace("cpp_pinyin::v2", "cpp_pinyin::v3"),
                                    expected + "Gone\n"),
             "generation 3: Gone is frozen but is not declared"),
            ("a frozen list without a generation token",
             lambda: (base / LEGACY_LIST_REL).write_text(expected, encoding="utf-8", newline=""),
             "has no generation token in its name"),
        ]

        build_fixture(base, expected)
        findings = check(base)
        report_case("positive fixture is green", not findings, f"expected no findings, got {findings}")

        # A byte-order mark on the list's first line must be read as a BOM, not as a name.
        list_path = base / list_rel(2)
        list_path.write_text("\ufeff" + list_path.read_text(encoding="utf-8-sig"),
                             encoding="utf-8", newline="")
        findings = check(base)
        report_case("a BOM on the frozen list is tolerated", not findings, f"findings={findings}")

        # The classifier must keep member functions, nested aliases and nested classes out of the list:
        # if it let them in, the positive fixture above would have demanded rows for query/Impl.
        scanned, _, scan_findings = scan(base, "cpp-pinyin2", 2)
        nested_leaked = [nested for nested in ("query", "Impl") if nested in scanned]
        report_case("classifier keeps nested names out of the frozen surface", not nested_leaked,
                    f"leaked={nested_leaked}")
        report_case("the positive fixture has no unclassifiable statement", not scan_findings,
                    f"findings={scan_findings}")

        for name, mutate, want in controls:
            build_fixture(base, expected)
            mutate()
            findings = check(base)
            hit = any(want in finding for finding in findings)
            report_case(f"control goes red: {name}", hit, f"wanted {want!r}, got {findings}")

    print(f"SELFTEST {'PASS' if not _SELF_TEST_FAILURES else 'FAIL'} -- "
          f"{len(_SELF_TEST_CASES)} case(s) reported above")
    for failure in _SELF_TEST_FAILURES:
        print("  " + failure)
    return 1 if _SELF_TEST_FAILURES else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default=str(find_root()))
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    findings = check(Path(args.root).resolve())
    for finding in findings:
        print(f"FAIL: {finding}")
    print(f"check_public_names: {'PASS' if not findings else 'FAIL'} ({len(findings)} finding(s))")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
