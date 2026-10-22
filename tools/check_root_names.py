#!/usr/bin/env python3
"""No unqualified `cpp_pinyin::X` anywhere, and no `using namespace` in a header.

WHY THIS EXISTS. The generation policy (`docs/STABILITY.md` section 4.5, R10/R11) says the product
namespace holds version namespaces only and that the repository -- including its documents -- always
spells the qualified name. For compiled code the COMPILER enforces that: after the rename, a root-style
reference simply does not resolve. Nothing enforces it for the files no compiler reads:

  * `docs/**` and the root `README.md`, where a stale `cpp_pinyin::Engine` is a lie a reader copies;
  * `python/**` and `csharp/**`, whose comments and docstrings quote the C++ spelling;
  * `tools/**`, whose messages talk about the library.

WHAT IT CHECKS
  * a root-style reference (`cpp_pinyin::X` where X is not `v<digits>::…` and not `detail::…`) in code or
    in prose is a finding;
  * the spellings the `::` rule cannot see -- `namespace cpp_pinyin {`, `namespace cp = cpp_pinyin;`,
    `using namespace cpp_pinyin;` -- are findings of their own: the first one is the abolished form
    itself, and none of them contain a `::`;
  * `using namespace cpp_pinyin::vN;` is a finding ONLY in a header (R14: in a header it re-exports a
    generation into every translation unit that includes it); in a `.cpp` it is an advisory note.

WHAT IT DOES NOT DO. It does not judge WHICH generation is current (`check_version_tokens` owns that) and
it does not freeze names (`check_public_names` owns that). It also does not scan build directories or
`.tmp`: those are copies, and judging a copy is how a gate reports its own shadow.

EXEMPTIONS. `tests/versioning/**` exists to put two generations side by side, so it is allowed to spell
either one; everything else names the current generation.

歧义形态：**源码面** ✗ —— 全树不得出现不限定的根名写法（编译器看不到的那一半由本门禁负责）。
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

#: Areas scanned, in the order they are reported. `docs` and the root files are here on purpose: they
#: are the half of the repository the compiler never reads.
AREAS = ("include", "src", "tests", "examples", "tools", "python", "csharp", "docs")
ROOT_FILES = ("README.md", "CHANGELOG.md", "CMakeLists.txt")
CODE_SUFFIXES = {".h", ".hpp", ".hh", ".hxx", ".c", ".cpp", ".cc", ".cs", ".py", ".cmake", ".in", ".txt"}
PROSE_SUFFIXES = {".md"}
SKIP_PARTS = {"build", ".tmp", "__pycache__", ".pytest_cache"}

#: R10/R11: a root-style reference in prose is a finding. Round 285: D31 (a user ruling already in the ledger) requires the
#: PLACEHOLDER spellings to be accepted, because a policy document must be able to name the rule it states -- and the landing
#: sandbox showed the family is wider than the two examples the ruling lists (`vN`, `v{N}`), also covering `v{N+1}`, `v<N>`.
#: So the exemption is written by SHAPE: `v` followed by a placeholder (`N`, `{...}`, `<...>`) and then `::` or a boundary.
#: A real generation (`v\d+`) stays green as before; a name that merely STARTS like a placeholder (`cpp_pinyin::vNaked::`)
#: still fails, and the self-test keeps a case for exactly that. First attempt used `v\{N\}\b`, where the `\b` between `}` and
#: `:` cannot hold -- both are non-word characters -- so the brace form was not actually exempt; the self-test caught it.
ROOT_USE = re.compile(r"cpp_pinyin::(?!v\d+\b|v(?:N|\{[^}]*\}|<[^>]*>)(?!\w)|detail\b)")
#: The bare-namespace form needs a lookahead, not just a regex: `namespace cpp_pinyin` is a declaration
#: only when a `{` opens it -- on the same line or on the next code line. Without that, honest prose
#: ("the library lives in namespace cpp_pinyin") was reported as the abolished form, which is the kind
#: of false positive that gets a gate switched off. Measured by the self-test.
BARE_NAMESPACE = re.compile(r"\bnamespace\s+cpp_pinyin\s*(?:\{\s*)?$")
#: These two are unambiguous code, so no lookahead is needed.
ZERO_COLON = (
    (re.compile(r"=\s*cpp_pinyin\s*;"), "a namespace alias must name a generation"),
    (re.compile(r"\busing\s+namespace\s+cpp_pinyin\s*;"), "a using-directive must name a generation"),
)
BARE_NAMESPACE_WHY = "`namespace cpp_pinyin { ... }` is the abolished form"
USING_GENERATION = re.compile(r"\busing\s+namespace\s+cpp_pinyin::v\d+\s*;")
EXEMPT_PREFIXES = ("tests/versioning/",)
#: Round 290 (user-approved): a POLICY document has to be able to NAME the forms it forbids -- that is what a rule list is.
#: Measured in the landing sandbox: 4 findings, all in `docs/STABILITY.md`, all of that one kind (a bare
#: `cpp_pinyin::` prefix, the banned `using namespace cpp_pinyin;`, and the abolished form quoted as a counter-example).
#: The marker is inline and exempts THAT LINE ONLY, the same shape as G9's line marker (`<!-- doc-tokens: historical -->`),
#: because the lines in question are table rows and prose -- a line of its own would not survive a Markdown table. Exempted
#: lines are reported as notes, not silently dropped: an exemption a reader cannot see is how a gate stops being trusted.
#: A whole-file exemption was rejected (it would let genuinely stale names hide in this very file).
QUOTED_FORM_MARKER = re.compile(r"<!--\s*root-names:\s*quoted-form\s*-->")


class Result:
    def __init__(self) -> None:
        self.findings: list[str] = []
        self.notes: list[str] = []

    def fail(self, message: str) -> None:
        self.findings.append(message)

    def note(self, message: str) -> None:
        self.notes.append(message)


def strip_comments(body: str, suffix: str, name: str,
                   mask_strings: bool = False) -> list[tuple[int, str]]:
    """(line number, text without comments), one left-to-right pass, unterminated block = hard error.

    The same three lessons as the other tools in this suite: cutting only `//` leaves `*`-continued block
    comment lines looking like code; scanning for `/*` before cutting `//` lets a `/*` inside a line
    comment swallow the rest of the file; and an unterminated block comment must never be silent.

    `mask_strings` blanks the CONTENT of quoted literals in a code file. A `cpp_pinyin::Engine` inside a
    string is data -- a fixture, a message, a test's expected text -- not a reference to a name; judging it
    made this gate report its own self-test fixtures when the tools were landed (measured 2026-10-15: 15
    of 21 remaining findings in the landing sandbox were the gates' own strings). Documents are a
    different matter and are never masked: in a `.md` file the same text IS a claim a reader copies.
    """
    if suffix == ".py":
        rows: list[tuple[int, str]] = []
        in_doc = False
        quote = ""
        for number, line in enumerate(body.splitlines(), 1):
            stripped = line.lstrip()
            if in_doc:
                if quote in line:
                    rest = line.split(quote, 1)[1]
                    in_doc = False
                    rows.append((number, rest))
                    continue
                rows.append((number, ""))
                continue
            if stripped.startswith(('"""', "'''")):
                quote = stripped[:3]
                rest = stripped[3:]
                end = rest.find(quote)
                if end >= 0:
                    rows.append((number, rest[end + 3:]))
                    continue
                in_doc = True
                rows.append((number, ""))
                continue
            cut = line.find("#")
            code = line if cut < 0 else line[:cut]
            if mask_strings:
                code = blank_quoted(code, ("'", '"'))
            rows.append((number, code))
        return rows
    if suffix in PROSE_SUFFIXES or suffix == ".txt":
        return [(number, line) for number, line in enumerate(body.splitlines(), 1)]
    if suffix in (".cmake", ".in"):
        return [(number, line.split("#", 1)[0]) for number, line in enumerate(body.splitlines(), 1)]
    rows = []
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
                    chars.append(" " if mask_strings and line[i] != quote else line[i])
                    if line[i] == "\\":
                        chars.append(" " if mask_strings else line[i + 1:i + 2])
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
        raise ValueError(f"{name}: unterminated /* block comment -- the scan would silently swallow the "
                         f"rest of the file")
    return rows


def blank_quoted(line: str, quotes: tuple[str, ...]) -> str:
    """Replace the inside of quoted literals with spaces, keeping the line's shape and the quotes.

    Not a full lexer: raw strings (`R"(...)"`) and nested quoting are not modelled. The rule it serves is
    "a name inside a string in a code file is data", and the self-test pins that down.
    """
    out: list[str] = []
    index = 0
    while index < len(line):
        char = line[index]
        if char not in quotes:
            out.append(char)
            index += 1
            continue
        out.append(char)
        index += 1
        while index < len(line):
            if line[index] == "\\":
                out.append("  ")
                index += 2
                continue
            if line[index] == char:
                out.append(char)
                index += 1
                break
            out.append(" ")
            index += 1
    return "".join(out)


def classify(path_rel: str, body: str, suffix: str,
             counts: dict[str, int] | None = None) -> tuple[list[str], list[str]]:
    """(findings, advisories) for one file. `counts` accumulates per-rule tallies for the summary."""
    if path_rel.startswith(EXEMPT_PREFIXES):
        return [], []
    tally = counts if counts is not None else {}
    findings: list[str] = []
    advisories: list[str] = []
    is_header = suffix in (".h", ".hpp", ".hh", ".hxx")
    is_prose = suffix in PROSE_SUFFIXES
    rows = strip_comments(body, suffix, path_rel, mask_strings=not is_prose)
    for index, (number, code) in enumerate(rows):
        # Round 290 (user-approved intent: "let a policy document name the forms it forbids"). The first version exempted the
        # root-style rule only, on my own assumption; that kept `docs/STABILITY.md` red, and the banned  [anchor-history: a deleted document, quoted as the example that motivated the rule]
        # `using namespace cpp_pinyin;` it quotes IS one of the forms the document forbids -- so the marker now exempts the
        # whole line and the report says how many findings were let through and why. The negative self-test that used to assert
        # "a banned using-directive is still a finding" was asserting my assumption, not the intent, and is flipped below.
        # Keeping it line-scoped is what stops this from being a whole-file hole.
        if QUOTED_FORM_MARKER.search(code):
            exempted = 0
            if ROOT_USE.search(code):
                exempted += 1
            for pattern, _why in ZERO_COLON:
                if pattern.search(code):
                    exempted += 1
            if exempted:
                advisories.append(f"{path_rel}:{number}: {exempted} finding(s) exempted as a quoted form "
                                  f"(policy document naming what it forbids)")
            continue
        if ROOT_USE.search(code):
            tally["root-style reference"] = tally.get("root-style reference", 0) + 1
            findings.append(f"{path_rel}:{number}: root-style reference `{code.strip()[:70]}` -- use "
                            f"`cpp_pinyin::v<N>::…` (R10/R11); the compiler cannot catch this in a "
                            f"document, a comment or a binding")
        for pattern, why in ZERO_COLON:
            if pattern.search(code):
                tally["zero-colon spelling"] = tally.get("zero-colon spelling", 0) + 1
                findings.append(f"{path_rel}:{number}: {why} -- `{code.strip()[:70]}`")
        if BARE_NAMESPACE.search(code.rstrip()):
            same_line = code.rstrip().endswith("{")
            following = next((text.strip() for _, text in rows[index + 1:] if text.strip()), "")
            if same_line or following.startswith("{"):
                tally["abolished namespace form"] = tally.get("abolished namespace form", 0) + 1
                findings.append(f"{path_rel}:{number}: {BARE_NAMESPACE_WHY} -- "
                                f"`{code.strip()[:70]}`")
        if USING_GENERATION.search(code):
            if is_header:
                tally["using-directive in a header"] = tally.get("using-directive in a header", 0) + 1
                findings.append(f"{path_rel}:{number}: `using namespace cpp_pinyin::v<N>;` in a HEADER "
                                f"re-exports a generation into every translation unit that includes it "
                                f"(R14) -- alias it in the .cpp instead")
            else:
                advisories.append(f"{path_rel}:{number}: using-directive (allowed in a .cpp, R14)")
    return findings, advisories


def walk(root: Path) -> list[Path]:
    files: list[Path] = []
    for area in AREAS:
        base = root / area
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if path.is_file() and path.suffix.lower() in CODE_SUFFIXES | PROSE_SUFFIXES \
                    and not (set(path.parts) & SKIP_PARTS):
                files.append(path)
    for name in ROOT_FILES:
        path = root / name
        if path.is_file():
            files.append(path)
    return files


def check(root: Path) -> Result:
    result = Result()
    files = walk(root)
    if not files:
        result.fail("ROOT-LEVEL: no file matched -- check the scan root, not the tree")
        return result
    result.note(f"scanned {len(files)} file(s) under {', '.join(AREAS)} plus {', '.join(ROOT_FILES)}")
    counts: dict[str, int] = {}
    for path in files:
        rel = path.relative_to(root).as_posix()
        body = path.read_text(encoding="utf-8-sig", errors="replace")
        findings, advisories = classify(rel, body, path.suffix.lower(), counts)
        result.findings += findings
        for advisory in advisories:
            result.note(advisory)
    if counts:
        breakdown = ", ".join(f"{rule} {count}" for rule, count in sorted(counts.items()))
        result.note(f"by rule: {breakdown}")
    return result


def self_test() -> int:
    import shutil
    import tempfile

    cases: list[str] = []
    failures: list[str] = []

    def case(name: str, ok: bool, detail: str = "") -> None:
        print(f"{'PASS' if ok else 'FAIL'} self-test: {name}")
        cases.append(name)
        if not ok:
            failures.append(f"{name}: {detail}")

    def write(base: Path, rel: str, body: str) -> None:
        path = base / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(body, encoding="utf-8", newline="")

    with tempfile.TemporaryDirectory() as tmp:
        base = Path(tmp)
        write(base, "docs/clean.md", "Use `cpp_pinyin::v2::Engine`.\n")
        clean = check(base).findings
        case("a correctly qualified reference is green", not clean, f"findings={clean}")

        write(base, "docs/root.md", "Write `cpp_pinyin::Engine e;`.\n")
        found = check(base).findings
        case("a root-style reference in a DOCUMENT is a finding",
             any("docs/root.md" in item for item in found), f"findings={found}")

        # Round 285 (D31): a policy document must be able to NAME the rule it states, so the placeholder spellings are
        # exempt. Both forms the ruling lists, and both must stay green while the case above stays red.
        write(base, "docs/placeholder.md",
              "The rule: `cpp_pinyin::vN::Engine`, and the brace form `cpp_pinyin::v{N}::Engine`.\n")
        found = check(base).findings
        case("D31: the placeholder spellings `vN::` and `v{N}::` in a document are NOT findings",
             not any("docs/placeholder.md" in item for item in found), f"findings={found}")

        write(base, "docs/placeholder-real.md", "`cpp_pinyin::vNaked::Engine` is still a root-style name.\n")
        found = check(base).findings
        case("D31 does not open a hole: `cpp_pinyin::vNaked::` is still a finding",
             any("docs/placeholder-real.md" in item for item in found), f"findings={found}")

        # The landing sandbox showed the family is wider than the ruling's two examples; the exemption is by shape now.
        write(base, "docs/placeholder-wide.md",
              "Replace `cpp_pinyin::vN::` with `cpp_pinyin::v{N+1}::`, or write `cpp_pinyin::v<N>::`.\n")
        found = check(base).findings
        case("D31 by shape: `v{N+1}::` and `v<N>::` are placeholders too, not findings",
             not any("docs/placeholder-wide.md" in item for item in found), f"findings={found}")

        # Round 290 (user-approved): the quoted-form marker, for a policy document naming the forms it forbids.
        write(base, "docs/quoted-form.md",
              "| R1 | the only spelling is `cpp_pinyin::v{N}::` <!-- root-names: quoted-form --> |\n")
        found = check(base).findings
        case("a marked line may name the root form it forbids",
             not any("docs/quoted-form.md" in item for item in found), f"findings={found}")

        write(base, "docs/quoted-form-bare.md", "| R1 | write `cpp_pinyin::Engine` |\n")
        found = check(base).findings
        case("the same line WITHOUT the marker is still a finding",
             any("docs/quoted-form-bare.md" in item for item in found), f"findings={found}")

        write(base, "docs/quoted-form-next.md",
              "| R1 | `cpp_pinyin::Engine` <!-- root-names: quoted-form --> |\n"
              "| R2 | and `cpp_pinyin::Dialect` on the NEXT line is not covered |\n")
        found = check(base).findings
        case("the marker covers its OWN line only, not the next one",
             sum("docs/quoted-form-next.md" in item for item in found) == 1, f"findings={found}")

        write(base, "docs/quoted-form-other.md",
              "① `using namespace cpp_pinyin;` <!-- root-names: quoted-form -->\n")
        found = check(base).findings
        case("the marker also covers a banned using-directive on a marked line (the document is naming it)",
             not any("docs/quoted-form-other.md" in item for item in found), f"findings={found}")

        write(base, "docs/quoted-form-unmarked-other.md",
              "① `using namespace cpp_pinyin;` on an unmarked line is still a finding\n")
        found = check(base).findings
        case("the same using-directive WITHOUT the marker is still a finding",
             any("docs/quoted-form-unmarked-other.md" in item for item in found), f"findings={found}")

        write(base, "docs/prose.md", "The library lives in namespace cpp_pinyin and nothing else.\n")
        found = check(base).findings
        case("prose that mentions the product namespace is NOT a finding",
             not any("docs/prose.md" in item for item in found), f"findings={found}")

        write(base, "docs/abolished.md", "namespace cpp_pinyin {\n    using Engine = v2::Engine;\n}\n")
        found = check(base).findings
        case("the abolished `namespace cpp_pinyin {` form is a finding",
             any("app_pinyin {" in item or "abolished" in item for item in found), f"findings={found}")

        write(base, "src/alias.cpp", "namespace cp = cpp_pinyin;\n")
        found = check(base).findings
        case("a namespace alias with no generation is a finding",
             any("src/alias.cpp" in item for item in found), f"findings={found}")

        write(base, "include/cpp-pinyin2/core/Types.h",
              "#pragma once\n#include <cpp_pinyin.h>\nusing namespace cpp_pinyin::v2;\n")
        result = check(base)
        case("a using-directive in a HEADER is a finding",
             any("using namespace" in item for item in result.findings), f"findings={result.findings}")

        write(base, "include/cpp-pinyin2/core/Types.h", "#pragma once\n")
        write(base, "src/uses.cpp", "using namespace cpp_pinyin::v2;\n")
        result = check(base)
        case("a using-directive in a .cpp is only a note",
             not any("uses.cpp" in item for item in result.findings)
             and any("uses.cpp" in note for note in result.notes),
             f"findings={result.findings} notes={result.notes}")

        write(base, "tests/versioning/two_generations.cpp",
              "cpp_pinyin::v2::Engine *a = nullptr;\ncpp_pinyin::v3::Engine *b = nullptr;\n")
        write(base, "src/clean.cpp", "cpp_pinyin::v2::Engine *ok = nullptr;\n")
        found = check(base).findings
        case("the versioning fixture may name two generations",
             not any("tests/versioning" in item for item in found), f"findings={found}")

        # A name inside a string in a code file is data; in a document the same text is a claim. This
        # pair of cases is the whole reason `mask_strings` exists, and it is what the gates tripped over
        # in the landing sandbox: they reported their own fixture strings.
        write(base, "src/quoted.cpp", 'const char *k = "cpp_pinyin::Engine";\n')
        found = check(base).findings
        case("a root name inside a string in a code file is NOT a finding",
             not any("quoted.cpp" in item for item in found), f"findings={found}")
        write(base, "docs/quoted.md", "The old spelling was `cpp_pinyin::Engine`.\n")
        found = check(base).findings
        case("a root name inside a document IS a finding",
             any("docs/quoted.md" in item for item in found), f"findings={found}")

        # The lock-up regression, and the unterminated-block hard error. Both are the same defects the
        # sibling tools were fixed for; a gate that scans text has to keep proving them.
        write(base, "src/locked.cpp", "// paths look like res/dict/*/word.txt\ncpp_pinyin::Engine *e;\n")
        found = check(base).findings
        case("a `/*` inside a line comment does not swallow the file",
             any("locked.cpp:2" in item for item in found), f"findings={found}")

        write(base, "src/broken.cpp", "cpp_pinyin::v2::Engine *e;\n/* never closed\n")
        try:
            check(base)
            case("an unterminated /* raises instead of passing", False, "no exception")
        except ValueError:
            case("an unterminated /* raises instead of passing", True)
        (base / "src" / "broken.cpp").unlink()

        # Excluded trees must not be judged: a build copy carries the old spelling on purpose.
        write(base, "build/copy.md", "cpp_pinyin::Engine from the old build\n")
        write(base, "src/clean2.cpp", "cpp_pinyin::v2::Engine *ok = nullptr;\n")
        found = check(base).findings
        case("build directories are skipped, not judged",
             not any("build/" in item for item in found), f"findings={found}")

        # A scan root with nothing to scan must be RED: "found nothing" is not "found no problem".
        empty = Path(tmp) / "empty-root"
        (empty / "build").mkdir(parents=True)
        (empty / "build" / "copy.md").write_text("cpp_pinyin::Engine\n", encoding="utf-8", newline="")
        found = check(empty).findings
        case("an empty scan scope is red",
             any("no file matched" in item for item in found), f"findings={found}")

    print(f"SELFTEST {'PASS' if not failures else 'FAIL'} -- {len(cases)} case(s) reported above")
    for failure in failures:
        print("  " + failure)
    return 1 if failures else 0


def find_root() -> Path:
    """The repository root, by looking for the markers that define one.

    `Path(__file__).parent.parent` is wrong for this tool in its draft home (`.tmp/v5-artifacts/`), where
    it resolves to `.tmp` -- a directory the scan SKIPS, so the gate reported 1 finding on a tree with
    hundreds. Same defect the sibling tools were fixed for; the fix is the same walk-up.
    """
    here = Path(__file__).resolve()
    for candidate in (here.parent, *here.parents):
        if (candidate / "CMakeLists.txt").is_file() and (candidate / "include").is_dir():
            return candidate
    return Path.cwd()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default=str(find_root()),
                        help="repository root (default: the checkout this tool lives in)")
    parser.add_argument("--self-test", action="store_true", help="prove each rule can fail, then exit")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    root = Path(args.root).resolve()
    result = check(root)
    for note in result.notes:
        print(f"note: {note}")
    for finding in result.findings:
        print(f"FAIL: {finding}")
    print(f"check_root_names: {'PASS' if not result.findings else 'FAIL'} "
          f"({len(result.findings)} finding(s))")
    return 1 if result.findings else 0


if __name__ == "__main__":
    sys.exit(main())
