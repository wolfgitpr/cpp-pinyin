#!/usr/bin/env python3
"""Every generation token in the tree must carry the package's major version -- and nothing else.

WHY THIS EXISTS. The 2.x line identifies itself on TEN axes at once, and this file is where nine of them are
compared (the tenth, the soname, is a property of the built binary and lives in
`tests/abi/check_soname.cmake`): the header directory, the macro prefix, the package/library/pkg-config name, the
C++ version namespace, the version-namespace token inside every declaration, the CHANGELOG's newest heading, the
DATA floor (`dialect.json`'s `min_library`) and the compile-time macros in
`core/Version.h`. Different batches edit them, and nothing compared them against each other, so the failure
mode is silent: a header that keeps `namespace cpp_pinyin::v2` while the package says 3 ships a generation
with two names.

(The count is written as a word on purpose: the docstring used to say "five axes" for a year after the tool had
grown to nine -- and then said nine after it had grown to ten, which is the same class of drift this tool exists
to catch, in its own header text.)

WHAT IT CHECKS
  1. the major version and product name come from the root CMakeLists.txt `project(... VERSION ...)`,
     and the product name must be `cpp-pinyin<major>`;
  2. every header under `include/<product>/` carries exactly one `namespace cpp_pinyin::v<major>`,
     except the files on EXEMPT (which declare no C++ names at all) -- both directions, so a new
     header that forgets the namespace is red and a stale exemption is red too;
  3. the header set equals `tests/abi/expected_public_headers.txt` (the frozen surface);
  4. `inline namespace` is forbidden (it re-exports a generation into the product namespace, i.e. it
     invents an unqualified name that the policy says must not exist);
  5. nothing is declared directly in `namespace cpp_pinyin`, and no header body writes a root-style
     `cpp_pinyin::X` reference in code;
  6. the include paths, macro prefixes, pkg-config / CMake-config file names and the install directory
     all agree with the same major;
  7. scanning zero headers is RED ("found nothing" must never look like "found no problem");
  8. the DATA floor (`min_library` in every `data/dialects/*/dialect.json`) is not newer than the package;
  10. the compile-time macros in `core/Version.h` match the package (a consumer writes `#if` against them);
  11. the CHANGELOG's newest `## ...` heading names the package version -- the file a user reads FIRST, and
      the one nothing compared before 2026-10-20 (audit round 2, G31).

USAGE
    python tools/check_version_tokens.py              # exit 1 on any disagreement
    python tools/check_version_tokens.py --self-test  # build fixtures, prove each check can fail

歧义形态（政策定义见 docs/STABILITY.md §4.5）：**源码面** ✗ 与 **链接面** ✗ —— 名字（头目录/库文件名/包/pc/词典目录/宏/命名空间/C 符号）必须带同名代令牌，写错代就报错，绝不允许静默串到另一代。
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

def find_root() -> Path:
    """Walk up until a directory looks like this repository (root CMakeLists.txt + include/).

    `parent.parent` is only right once this file lives in `tools/`; as a draft under `.tmp/` it
    pointed at `.tmp` and the gate reported "no CMakeLists.txt" -- a wrong-root verdict that looks
    exactly like a wrong tree (measured, 2026-10-14).
    """
    here = Path(__file__).resolve()
    for candidate in (here.parent, *here.parents):
        if (candidate / "CMakeLists.txt").is_file() and (candidate / "include").is_dir():
            return candidate
    return Path.cwd()


ROOT = find_root()

#: Headers that declare no C++ name of their own, with the reason. A file that starts declaring
#: something stops being exempt and must open the version namespace in the same edit.
EXEMPT = {
    "core/Global.h": "macros only (warning/cpp-standard switches)",
    "core/PinyinGlobal.h": "macros only (export spellings)",
    "core/Version.h": "macros only (compile-time version tokens; axis 9 compares them with project(VERSION))",
    "ext/c_api.h": 'extern "C" declarations only; the C face has no C++ namespace',
}

#: A header declaring a public name. The C++ face allows more than `.h`: `install(DIRECTORY include/...)`
#: does not filter by extension, so `.hpp`/`.hh`/`.hxx`/`.inl` under include/ reach the user's tree and
#: must be scanned (an `.hpp` header used to be invisible to this gate AND to the frozen list).
PUBLIC_SUFFIXES = (".h", ".hpp", ".hh", ".hxx", ".inl")

#: Namespace patterns run on COMMENT-FREE text. Both rules matter, and both were defeated by comments
#: before 2026-10-14: `namespace cpp_pinyin   // router` + `{` slipped past the old line-anchored regex,
#: and a `/* the generation marker: namespace cpp_pinyin::v2 */` comment satisfied the old version regex
#: while the file declared a global `struct Ghost` at file scope -- i.e. both gates green with a new
#: unqualified public name in a frozen header.
VERSION_NS = re.compile(r"^\s*namespace\s+cpp_pinyin::v(\d+)\b(?!\s*::)", re.M)
ROOT_NS = re.compile(r"\bnamespace\s+cpp_pinyin\b(?!\s*::)")
INLINE_NS = re.compile(r"\binline\s+namespace\b")
NAMESPACE_ANY = re.compile(r"\bnamespace\s+[A-Za-z_]")
PROJECT = re.compile(r"project\s*\(\s*([A-Za-z0-9_.\-]+)\s+VERSION\s+(\d+)\.(\d+)\.(\d+)")

#: x.y.z. Axis 7 (the data floor) is NOT a name, so it cannot be checked by the
#: token patterns above; they are compared against `project(... VERSION ...)` here instead.
VERSION_TEXT = re.compile(r"(\d+)\.(\d+)\.(\d+)")
DIALECT_MIN_LIBRARY = re.compile(r'"min_library"\s*:\s*"([^"]+)"')
#: The compile-time half of the version, in `include/<product>/core/Version.h`.
VERSION_HEADER_NUMBERS = re.compile(r"#define\s+CPP_PINYIN2_VERSION_(MAJOR|MINOR|PATCH)\s+(\d+)")
VERSION_HEADER_STRING = re.compile(r'#define\s+CPP_PINYIN2_VERSION_STRING\s+"([^"]+)"')
#: The FIRST `## ...` heading of CHANGELOG.md and the version it names. Deliberately loose about the
#: bracket form: the file may write `## [Unreleased] — 2.0.0（未发布）` before the tag exists or
#: `## [2.0.0] — 2026-10-20` after, and both must be readable. Only the first heading is matched.
CHANGELOG_VERSION = re.compile(r"^##\s+[^\n]*?(\d+\.\d+\.\d+)", re.M)
#: The RELEASED form of that heading: `## [2.0.0] — 2026-10-20`. 2026-10-20 audit, H49.
#: Why it exists: the loose pattern above accepts BOTH `## [Unreleased] — 2.0.0` (before the tag) and
#: `## [2.0.0] — 2026-10-20` (after it), which made "the release-day heading edit was made" unprovable --
#: a tagged release could ship a heading that still said Unreleased, and every gate stayed green.
CHANGELOG_RELEASED_HEADING = re.compile(r"^##\s+\[\d+\.\d+\.\d+\]\s*[—–-]\s*\d{4}-\d{2}-\d{2}\b", re.M)
#: Any include directive, and the plain `<path>` shape. A macro-composed or concatenated include can
#: hide a cross-generation path, so an include that mentions the product but is not the plain shape is
#: reported rather than ignored.
INCLUDE_DIRECTIVE = re.compile(r"^\s*#\s*include\s*(.+?)\s*$")
INCLUDE_PLAIN = re.compile(r'^[<"]([^>"]+)[>"]$')
MACRO_TOKEN = re.compile(r"\bCPP_PINYIN_?(\d+)_")
#: A reference through the product namespace rather than the version namespace. The lookahead must
#: accept a bare `v<digits>` too: `namespace cpp_pinyin::v2` is a namespace HEADER, not a root use, and
#: demanding a trailing `::` here produced exactly that false positive (caught by --self-test).
ROOT_USE = re.compile(r"cpp_pinyin::(?!v\d+\b|detail\b)")
DECL_LINE = re.compile(r"^    (?!/|/\*|\*)\S")

#: Faces that must carry the generation token, with the pattern that finds a foreign generation in them.
#: The old gate only checked that the two `.in` files EXIST -- a `cpp-pinyin2.pc.in` whose body said
#: `Name: cpp-pinyin3` passed (its claim of "six axes agree" was not implemented).
PACKAGE_FACE = {
    "pkg-config": re.compile(r"cpp-pinyin(\d+)"),
    "cmake-config": re.compile(r"cpp-pinyin(\d+)"),
    "cmake-install": re.compile(r"(?:include|share|cmake)/cpp-pinyin(\d+)"),
    "cmake-install-var": re.compile(r"\$\{PROJECT_NAME\}"),
}

#: Areas whose code carries generation tokens. tests/data and .tmp are fixtures/history, not tokens.
TOKEN_AREAS = ("include", "src", "tools", "examples", "python", "csharp", "tests")


@dataclass
class Result:
    findings: list[str] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)

    def fail(self, message: str) -> None:
        self.findings.append(message)

    def note(self, message: str) -> None:
        self.notes.append(message)


def tag_names(root: Path) -> set[str] | None:
    """The tag names of the checkout AT `root`, or None when `root` is not a checkout.

    `git -C <dir>` walks UP to the enclosing repository, so a fixture directory that happens to sit inside
    a checkout (the self-test's temp dir can, when `TMP` points into the repo) would silently report the
    OUTER repository's tags. Comparing `--show-toplevel` with `root` pins the answer to this tree.
    """
    try:
        top = subprocess.run(["git", "-C", str(root), "rev-parse", "--show-toplevel"],
                             capture_output=True, text=True, timeout=15)
        if top.returncode != 0 or Path(top.stdout.strip()).resolve() != root.resolve():
            return None
        listed = subprocess.run(["git", "-C", str(root), "tag", "--list"],
                                capture_output=True, text=True, timeout=15)
    except (OSError, subprocess.SubprocessError):
        return None
    return set(listed.stdout.split()) if listed.returncode == 0 else None


def released_heading_problem(heading: str | None, version: str, tagged: bool) -> str | None:
    """Why a TAGGED release's CHANGELOG heading is wrong, or None. Pure, so the self-test can drive it.

    Axis 11 (2026-10-20 audit, H49). Before this, the release-day step "rename `[Unreleased] — 2.0.0` to
    `[2.0.0] — DATE`" had no gate: both spellings were green, so the step could be skipped forever.
    """
    if not tagged or not heading:
        return None
    if CHANGELOG_RELEASED_HEADING.match(heading):
        return None
    return (f"tag {version} exists, but CHANGELOG.md's first heading is {heading.strip()!r} -- a released "
            f"version must read `## [{version}] — YYYY-MM-DD` (the release-day edit), not the pre-release "
            f"`[Unreleased]` form")


def code_part(line: str, suffix: str) -> str:
    marker = "#" if suffix in (".py", ".cmake", ".txt", ".in") else "//"
    index = line.find(marker)
    return line if index < 0 else line[:index]


def code_lines(body: str, suffix: str):
    """(line number, code text) with comments removed, block comments included.

    Cutting only at `//` made every `*`-continued line of a `/* ... */` comment look like code: on the
    real tree that produced 11 false findings in ext/c_api.h (measured 2026-10-14), which is the kind
    of noise that gets a gate switched off.
    """
    if suffix in (".py", ".cmake", ".txt", ".in"):
        for number, line in enumerate(body.splitlines(), 1):
            yield number, code_part(line, suffix)
        return
    in_block = False
    for number, line in enumerate(body.splitlines(), 1):
        text = line
        if in_block:
            end = text.find("*/")
            if end < 0:
                continue
            in_block = False
            text = text[end + 2:]
        while True:
            start = text.find("/*")
            if start < 0:
                break
            end = text.find("*/", start + 2)
            if end < 0:
                in_block = True
                text = text[:start]
                break
            text = text[:start] + " " + text[end + 2:]
        yield number, code_part(text, suffix)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def package_identity(root: Path, result: Result) -> tuple[str, tuple[int, int, int]]:
    cmake = root / "CMakeLists.txt"
    if not cmake.is_file():
        result.fail(f"no CMakeLists.txt under {root}")
        return "", (0, 0, 0)
    match = PROJECT.search(read(cmake))
    if not match:
        result.fail("CMakeLists.txt has no `project(<name> VERSION x.y.z)`")
        return "", (0, 0, 0)
    name, major = match.group(1), int(match.group(2))
    version = (major, int(match.group(3)), int(match.group(4)))
    if name != f"cpp-pinyin{major}":
        result.fail(f"product name {name!r} does not carry the major version {major} "
                    f"(expected cpp-pinyin{major})")
    return name, version


def frozen_headers(root: Path, result: Result) -> list[str]:
    path = root / "tests" / "abi" / "expected_public_headers.txt"
    if not path.is_file():
        result.fail("tests/abi/expected_public_headers.txt is missing")
        return []
    rows = [line.strip() for line in read(path).splitlines()
            if line.strip() and not line.strip().startswith("#")]
    return rows


def blank_quoted(line: str) -> str:
    """Blank the inside of `"` / `'` literals, keeping quotes and line shape.

    A macro NAME inside a string is data, not a use: the gate's own self-test fixtures spell a foreign
    generation's prefix, and scanning them made the gate report itself once it was landed in the tree
    (measured 2026-10-15 in the landing sandbox). Include DIRECTIVES are checked separately and are not
    masked, because there the quoted form is exactly what carries a cross-generation path.
    """
    out: list[str] = []
    index = 0
    while index < len(line):
        char = line[index]
        if char not in "\"'":
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


def generation_dirs(root: Path) -> list[tuple[Path, int]]:
    """Every `include/cpp-pinyinN/` present, oldest first. A tree may hold several generations: the
    day v3 ships, v2's directory stays where it is, and every check below has to be judged against the
    generation a FILE LIVES IN rather than against the package's current major."""
    include_root = root / "include"
    found: list[tuple[Path, int]] = []
    if not include_root.is_dir():
        return found
    for path in sorted(include_root.glob("cpp-pinyin*")):
        match = re.match(r"^cpp-pinyin(\d+)$", path.name)
        if path.is_dir() and match:
            found.append((path, int(match.group(1))))
    return sorted(found, key=lambda item: item[1])


def check_headers(root: Path, product: str, major: int, result: Result) -> None:
    generations = generation_dirs(root)
    if not generations:
        result.fail(f"ROOT-LEVEL: no include/cpp-pinyinN/ directory at all -- check the scan, not the tree")
        return

    # THE WINDOW, CHECKED. The policy promises that a major release supports exactly one version back, so
    # a tree may carry at most two generations and they must be consecutive. Without this rule the promise
    # is text: nothing would notice v4 shipping while v2 was still installed, which is precisely the
    # multi-version ambiguity the whole design exists to prevent. A NEWER generation is allowed only as
    # the immediate successor, because that is the state every migration passes through.
    numbers = sorted(generation for _, generation in generations)
    if len(numbers) > 2:
        result.fail(f"generations present {numbers} -- at most TWO may ship (the current one and the "
                    f"previous); an older generation was not removed, so a consumer can link against a "
                    f"version this release does not support")
    elif len(numbers) == 2 and numbers[1] - numbers[0] != 1:
        result.fail(f"generations present {numbers} -- two generations must be consecutive (N-1 and N); a "
                    f"gap means the window skipped a version and the migration guide cannot describe it")
    if major not in numbers:
        result.fail(f"no include/cpp-pinyin{major}/ -- the package claims generation {major} but that "
                    f"generation is not installed")
    newer = [number for number in numbers if number > major]
    if newer:
        drop = newer[0] - 2
        tail = f", and remove generation {drop} if it is still present" if drop in numbers else ""
        result.note(f"generation(s) {newer} are present while the package is {major} -- a development or "
                    f"rehearsal state; the release that ships generation {newer[0]} must bump "
                    f"`project(VERSION {newer[0]}.0.0)`{tail}")

    found: list[str] = []
    for include_root, generation in generations:
        headers = sorted(path for path in include_root.rglob("*")
                         if path.is_file() and path.suffix.lower() in PUBLIC_SUFFIXES)
        result.note(f"generation {generation}: scanned {len(headers)} header(s) under "
                    f"include/{include_root.name}/ (extensions {', '.join(PUBLIC_SUFFIXES)})")
        if not headers:
            result.fail(f"ROOT-LEVEL: no headers found under include/{include_root.name}/ -- check the "
                        f"scan, not the tree")
        for path in headers:
            rel_in_include = path.relative_to(root / "include").as_posix()
            rel = path.relative_to(include_root).as_posix()
            found.append(rel_in_include)
            body = read(path)
            # Everything below runs on comment-free text: a comment used to be able to hide a namespace
            # header (between the name and `{`) and to fake one (a doc comment naming cpp_pinyin::v2).
            code_body = "\n".join(code for _, code in code_lines(body, path.suffix))
            opened = [int(m.group(1)) for m in VERSION_NS.finditer(code_body)]
            versions = sorted(set(opened))
            inline = len(INLINE_NS.findall(code_body))
            root_ns = len(ROOT_NS.findall(code_body))
            where = f"{rel_in_include} (generation {generation})"

            if len(opened) != 1 and rel not in EXEMPT:
                result.fail(f"{where}: opens `namespace cpp_pinyin::v{generation}` {len(opened)} time(s) -- "
                            f"exactly one namespace header per public header (a set() used to hide a second "
                            f"opening, and a comment used to supply a phantom one)")
            if len(versions) > 1:
                result.fail(f"{where}: {len(versions)} version namespaces {versions} -- a header belongs to "
                            f"exactly one generation")
            if versions and versions != [generation]:
                result.fail(f"{where}: declares namespace v{versions[0]} but lives in generation "
                            f"{generation} -- a header's namespace and its directory must agree")
            if not versions and rel not in EXEMPT:
                result.fail(f"{where}: declares no `namespace cpp_pinyin::v{generation}` and is not on "
                            f"EXEMPT (a new header must open its generation's namespace, or be added to "
                            f"EXEMPT with a reason)")
            if not versions and rel in EXEMPT:
                result.note(f"exempt (declares no C++ name): {rel_in_include} -- {EXEMPT[rel]}")
            if rel in EXEMPT and NAMESPACE_ANY.search(code_body):
                result.fail(f"{where}: is on EXEMPT ({EXEMPT[rel]}) but declares a namespace -- an exemption "
                            f"that survives the file growing a namespace is a stale exemption")
            if inline:
                result.fail(f"{where}: {inline} `inline namespace` -- inline re-exports the generation into "
                            f"cpp_pinyin, which is exactly the unqualified name that must not exist")
            if root_ns:
                result.fail(f"{where}: declares something directly in `namespace cpp_pinyin` "
                            f"-- the product namespace must hold version namespaces only")
            for number, code in code_lines(body, path.suffix):
                if ROOT_USE.search(code):
                    result.fail(f"{rel_in_include}:{number}: root-style reference `{code.strip()[:70]}` "
                                f"-- inside the library use the version-qualified name")
                for match in MACRO_TOKEN.finditer(blank_quoted(code)):
                    if int(match.group(1)) != generation:
                        result.fail(f"{rel_in_include}:{number}: CPP_PINYIN{match.group(1)}_* inside "
                                    f"generation {generation} -- a file carries its own generation's token")
                directive = INCLUDE_DIRECTIVE.match(code)
                if not directive:
                    continue
                argument = directive.group(1)
                plain = INCLUDE_PLAIN.match(argument)
                if not plain:
                    if "cpp-pinyin" in argument or "CPP_PINYIN" in argument:
                        result.fail(f"{rel_in_include}:{number}: include argument `{argument[:60]}` is not a "
                                    f"plain <path> -- a macro or concatenation would hide a cross-generation "
                                    f"path")
                    continue
                include = plain.group(1)
                segments = include.replace("\\", "/").split("/")
                foreign = sorted({int(seg.group(1)) for seg in
                                  (re.match(r"^cpp-pinyin(\d+)$", segment) for segment in segments)
                                  if seg} - {generation})
                if foreign:
                    result.fail(f"{rel_in_include}: includes <{include}> -- that path belongs to generation "
                                f"{foreign[0]}, but this file is generation {generation}")
                if "cpp-pinyin" in include and any(part in (".", "..") for part in segments):
                    result.fail(f"{rel_in_include}: includes <{include}> -- a relative path into a generation "
                                f"directory is how two generations end up in one build by accident")

    for rel in EXEMPT:
        for include_root, generation in generations:
            if (include_root / rel).is_file():
                break
        else:
            # Per PRESENT generation, not per package: a tree that has just dropped an old generation
            # (runbook step 5, the v4 roll-out) legitimately has no file for it any more. Measuring with
            # the 2026-10-20 v4 rehearsal: the package-anchored version of this check reddened on exactly
            # the release the window policy asks for.
            result.fail(f"EXEMPT lists {rel} but no present generation has that file -- a stale exemption "
                        f"hides a missing version namespace")

    frozen = frozen_headers(root, result)
    if frozen:
        missing = sorted(set(frozen) - set(found))
        extra = sorted(set(found) - set(frozen))
        if missing:
            result.fail(f"frozen header list names files that do not exist: {missing}")
        if extra:
            result.fail(f"headers exist that the frozen list does not name: {extra} "
                        f"-- adding a public header is a deliberate edit of tests/abi/expected_public_headers.txt")


def check_tokens(root: Path, product: str, major: int, result: Result) -> None:
    # The package face is checked by CONTENT, not by existence. Existence alone was the old behaviour and
    # it let `cpp-pinyin2.pc.in` say `Name: cpp-pinyin3` while this gate stayed green -- a claim of
    # "the package face carries the generation token" that nothing implemented.
    for name in (f"{product}.pc.in", f"{product}Config.cmake.in"):
        path = root / name
        if not path.is_file():
            result.fail(f"{name} is missing -- the package face must carry the generation token")
            continue
        text = read(path)
        wrong = sorted({int(m.group(1)) for m in re.finditer(r"cpp-pinyin(\d+)", text)} - {major})
        wrong += sorted({int(m.group(1)) for m in re.finditer(r"CPP_PINYIN_?(\d+)_", text)} - {major})
        if wrong:
            result.fail(f"{name} names generation(s) {wrong} -- this package is generation {major}; a file "
                        f"name that carries a token while its body does not is worse than no token")
        if f"cpp-pinyin{major}" not in text:
            result.fail(f"{name} does not name cpp-pinyin{major} at all")

    cmake = read(root / "CMakeLists.txt")
    tokens = {int(m.group(1)) for m in MACRO_TOKEN.finditer(cmake)}
    wrong = sorted(token for token in tokens if token != major)
    if wrong:
        result.fail(f"CMakeLists.txt carries macro prefixes for generation(s) {wrong}, package is {major}")
    for what, pattern in (("public header directory", re.compile(r"include/cpp-pinyin(\d+)")),
                          ("dictionary install directory", re.compile(r"share/cpp-pinyin(\d+)")),
                          ("CMake package directory", re.compile(r"cmake/cpp-pinyin(\d+)")),
                          ("pkg-config file name", re.compile(r"cpp-pinyin(\d+)\.pc"))):
        matches = pattern.findall(cmake)
        found = sorted({int(value) for value in matches} - {major})
        if found:
            result.fail(f"CMakeLists.txt installs a {what} for generation(s) {found}, package is {major}")
        elif not matches:
            # "0 matches" must be visible, not silent: this axis may be spelled through a variable
            # (${PROJECT_NAME}), and then the product-name check above is what guards it.
            result.note(f"no literal `{what}` token in CMakeLists.txt (spelled via a variable?)")

    checked = 0
    for area in TOKEN_AREAS:
        base = root / area
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if not path.is_file() or path.suffix.lower() not in \
                    (".h", ".hpp", ".cpp", ".cc", ".c", ".py", ".cs", ".cmake", ".in"):
                continue
            if any(part in {"build", ".tmp", "data", "__pycache__"} for part in path.parts):
                continue
            rel = path.relative_to(root).as_posix()
            # The expected token depends on WHERE the file lives. Library code is the current generation,
            # but a file under `include/cpp-pinyinN/` belongs to generation N for as long as that
            # generation ships -- and it must keep N's macros even after the package moves on. The old
            # rule demanded the current major everywhere, which would have turned this gate red on the
            # day v3 shipped, i.e. the gate would have blocked the very release it exists to protect.
            expected = major
            for segment in path.parts:
                match = re.match(r"^cpp-pinyin(\d+)$", segment)
                if match:
                    expected = int(match.group(1))
                    break
            for number, code in code_lines(read(path), path.suffix):
                checked += 1
                for match in MACRO_TOKEN.finditer(blank_quoted(code)):
                    if int(match.group(1)) != expected:
                        result.fail(f"{rel}:{number}: CPP_PINYIN{match.group(1)}_* in a file that belongs "
                                    f"to generation {expected} -- a file carries its own generation's token")
    result.note(f"macro-prefix scan covered {checked} code line(s)")


def check_version_axes(root: Path, version: tuple[int, int, int], result: Result) -> None:
    """Axis 7: the DATA floor must agree with `project(... VERSION ...)`.

    Why this one is here rather than in the name patterns: `min_library` carries no generation TOKEN -- it is a
    JSON string inside the shipped dictionaries -- so no token pattern can see it, and before 2026-10-19 nothing
    compared it with anything. It was only shape-checked (`check_bindings.py` accepted "9.9.9"), and the
    generator repository does not produce the file at all, so a typo would make every consumer refuse the
    shipped data. Measured hazard, not hypothetical.

    There used to be a second axis here: the vcpkg port manifest's `version`. The port was removed from this
    repository on 2026-10-23, so the axis went with it.

    A MISSING `min_library` is not a failure: hand-built dialect directories stay usable (that is the
    documented rule in docs/STABILITY.md R13), so only a present-and-newer value is red.
    """
    expected = ".".join(str(part) for part in version)
    manifests = sorted((root / "data" / "dialects").glob("*/dialect.json"))
    if not manifests:
        result.fail("no data/dialects/*/dialect.json found -- the data-side axis cannot be checked")
    for manifest in manifests:
        rel = manifest.relative_to(root).as_posix()
        match = DIALECT_MIN_LIBRARY.search(read(manifest))
        if not match:
            result.note(f"{rel}: no min_library (legal: hand-built directories need no floor)")
            continue
        declared = match.group(1)
        parsed = VERSION_TEXT.fullmatch(declared)
        if not parsed:
            result.fail(f"{rel}: min_library {declared!r} is not x.y.z")
            continue
        need = tuple(int(part) for part in parsed.groups())
        if need > version:
            result.fail(f"{rel}: min_library {declared} is newer than the package version {expected} "
                        f"-- every consumer would refuse this dictionary")
    # Axis 9: the compile-time macros a consumer writes `#if` against.
    header = root / "include" / f"cpp-pinyin{version[0]}" / "core" / "Version.h"
    rel = header.relative_to(root).as_posix()
    if not header.is_file():
        result.fail(f"{rel} is missing -- no consumer can branch on the version at compile time")
        return
    body = read(header)
    seen: dict[str, int] = {}
    for name, number in VERSION_HEADER_NUMBERS.findall(body):
        seen[name] = int(number)
    if len(seen) != 3:
        result.fail(f"{rel}: expected MAJOR/MINOR/PATCH, found {sorted(seen)}")
        return
    declared = (seen["MAJOR"], seen["MINOR"], seen["PATCH"])
    if declared != version:
        result.fail(f"{rel}: CPP_PINYIN2_VERSION_* say {'.'.join(map(str, declared))} but the "
                    f"package is {expected} -- a half-bump is the exact drift this axis exists for")
    string_match = VERSION_HEADER_STRING.search(body)
    if not string_match:
        result.fail(f"{rel}: CPP_PINYIN2_VERSION_STRING is missing")
    elif string_match.group(1) != expected:
        result.fail(f"{rel}: CPP_PINYIN2_VERSION_STRING {string_match.group(1)!r} != {expected!r}")

    # Axis 10: the CHANGELOG's newest version heading (2026-10-20 audit, G31). The changelog is the FIRST
    # document a user reads before upgrading, and nothing compared its heading with the package: a release
    # could ship a CHANGELOG whose top section still named the previous version, and every gate stayed green.
    # Only the FIRST heading is checked on purpose -- everything below it is history, and requiring the old
    # sections to move would rewrite the record.
    changelog = root / "CHANGELOG.md"
    if not changelog.is_file():
        result.fail("CHANGELOG.md is missing -- it is the one document a user reads before upgrading")
        return
    newest = CHANGELOG_VERSION.search(read(changelog))
    if newest is None:
        result.fail("CHANGELOG.md has no `## [...] <x.y.z>` heading, so nothing states this release's version")
    elif newest.group(1) != expected:
        result.fail(f"CHANGELOG.md's newest heading says {newest.group(1)} but the package is {expected} "
                    f"-- users read the changelog first, so a stale heading is the drift they meet")

    # Axis 10, second half: once a tag exists the heading must be in the RELEASED form. The tag list is the only
    # in-tree evidence that "release day" happened, so this is the one place the release-day edit can be
    # proven rather than assumed (2026-10-20 audit, H49).
    body = read(changelog)
    first_heading = next((line for line in body.splitlines() if line.startswith("## ")), None)
    tags = tag_names(root)
    if tags is None:
        result.note("CHANGELOG released-form check SKIPPED: this tree is not a git checkout (or git cannot "
                    "answer), so whether a tag exists is unknown -- do not read this as 'the heading is fine'")
    else:
        problem = released_heading_problem(first_heading, expected, expected in tags or f"v{expected}" in tags)
        if problem:
            result.fail(problem)


def check(root: Path) -> Result:
    result = Result()
    product, version = package_identity(root, result)
    if not product:
        return result
    major = version[0]
    result.note(f"package {product}, version {'.'.join(str(part) for part in version)}")
    check_headers(root, product, major, result)
    check_tokens(root, product, major, result)
    check_version_axes(root, version, result)
    return result


# --- fixtures ---------------------------------------------------------------------------------------

GOOD_CMAKE = "cmake_minimum_required(VERSION 3.16)\nproject(cpp-pinyin2 VERSION 2.0.0 LANGUAGES CXX)\n"
GOOD_HEADER = """#pragma once

#include <cpp-pinyin2/core/Global.h>

namespace cpp_pinyin::v2
{
    class Engine {};
    int codeText(int code);
}  // namespace cpp_pinyin::v2
"""
EXEMPT_HEADER = "#pragma once\n\n#define CPP_PINYIN2_NODISCARD [[nodiscard]]\n"


def add_generation_fixture(base: Path, namespace: str = "cpp_pinyin::v3",
                           macro: str = "CPP_PINYIN3_", suffix: str = "3") -> None:
    """A second generation in the fixture: the same files retagged, plus its rows in the frozen list.

    The frozen header list is PATH-keyed (`cpp-pinyin2/core/Types.h`), so it spans generations and grows
    by one row per header per generation. The frozen NAME list is name-keyed, so it cannot span
    generations at all -- that asymmetry is why `check_public_names` splits its list per generation
    (tests/abi/expected_public_names.vN.txt) while this gate keeps one path list.
    """
    source = base / "include" / "cpp-pinyin2"
    target = base / "include" / f"cpp-pinyin{suffix}"
    rows = []
    for path in sorted(source.rglob("*.h")):
        rel = path.relative_to(source)
        body = path.read_text(encoding="utf-8").replace("cpp_pinyin::v2", namespace) \
                                              .replace("CPP_PINYIN2_", macro) \
                                              .replace("cpp-pinyin2", f"cpp-pinyin{suffix}")
        (target / rel).parent.mkdir(parents=True, exist_ok=True)
        (target / rel).write_text(body, encoding="utf-8", newline="")
        rows.append(f"cpp-pinyin{suffix}/{rel.as_posix()}")
    frozen = base / "tests" / "abi" / "expected_public_headers.txt"
    existing = frozen.read_text(encoding="utf-8").rstrip("\n").splitlines()
    frozen.write_text("\n".join(existing + rows) + "\n", encoding="utf-8", newline="")


def build_fixture(base: Path) -> None:
    # Clean first. Without this, a file written by one control survives into every later control, and a
    # finding about THAT file can satisfy a later control's expected substring -- measured 2026-10-15:
    # the "non-.h extension" control passed while the extension handling was reverted, because a
    # leftover `core/Extra.h` from the previous control produced the very message it was looking for.
    # That is the "controls mask each other" failure mode, in my own instrument.
    include = base / "include"
    if include.exists():
        shutil.rmtree(include)
    abi = base / "tests" / "abi"
    if abi.exists():
        shutil.rmtree(abi)
    # Same reasoning for the two version-carrying NON-name files: a control that leaves "9.9.9" behind
    # makes the next control's green meaningless.
    for stale in ("data", "packaging"):
        path = base / stale
        if path.exists():
            shutil.rmtree(path)
    include = base / "include" / "cpp-pinyin2" / "core"
    include.mkdir(parents=True, exist_ok=True)
    (base / "include" / "cpp-pinyin2" / "ext").mkdir(parents=True, exist_ok=True)
    (base / "tests" / "abi").mkdir(parents=True, exist_ok=True)
    (base / "src").mkdir(parents=True, exist_ok=True)
    (base / "CMakeLists.txt").write_text(GOOD_CMAKE, encoding="utf-8", newline="")
    # Axis 10's subject. Staged here, with the other version-carrying files, so that a control which
    # rewrites the heading cannot leak into the next one (same lesson as the `data`/`packaging` cleanup).
    (base / "CHANGELOG.md").write_text(
        "# Changelog\n\nAll notable changes to this project are documented here.\n\n"
        "## [Unreleased] — 2.0.0（未发布）\n", encoding="utf-8", newline="")
    (include / "Types.h").write_text(GOOD_HEADER, encoding="utf-8", newline="")
    (include / "Global.h").write_text(EXEMPT_HEADER, encoding="utf-8", newline="")
    (include / "Version.h").write_text(
        "#pragma once\n\n#define CPP_PINYIN2_VERSION_MAJOR 2\n#define CPP_PINYIN2_VERSION_MINOR 0\n"
        "#define CPP_PINYIN2_VERSION_PATCH 0\n#define CPP_PINYIN2_VERSION_STRING \"2.0.0\"\n",
        encoding="utf-8", newline="")
    (include / "PinyinGlobal.h").write_text(
        "#pragma once\n\n#include <cpp-pinyin2/core/Global.h>\n\n"
        "#ifndef CPP_PINYIN2_EXPORT\n#  define CPP_PINYIN2_EXPORT\n#endif\n",
        encoding="utf-8", newline="")
    (base / "include" / "cpp-pinyin2" / "ext" / "c_api.h").write_text(
        '#pragma once\nextern "C" {\nint cppp2_version(void);\n}\n', encoding="utf-8", newline="")
    (base / "tests" / "abi" / "expected_public_headers.txt").write_text(
        "# frozen\ncpp-pinyin2/core/Global.h\ncpp-pinyin2/core/PinyinGlobal.h\n"
        "cpp-pinyin2/core/Types.h\ncpp-pinyin2/core/Version.h\ncpp-pinyin2/ext/c_api.h\n",
        encoding="utf-8", newline="")
    (base / "cpp-pinyin2.pc.in").write_text("Name: cpp-pinyin2\n", encoding="utf-8", newline="")
    # Realistic fixture: the real cpp-pinyin2Config.cmake.in names the package (it includes
    # cpp-pinyin2Targets.cmake). A fixture of `@PACKAGE_INIT@` alone made the new "the package face must
    # name its generation" rule look broken when the FIXTURE was the thing that was unrepresentative.
    (base / "cpp-pinyin2Config.cmake.in").write_text(
        '@PACKAGE_INIT@\ninclude("${CMAKE_CURRENT_LIST_DIR}/cpp-pinyin2Targets.cmake")\n',
        encoding="utf-8", newline="")
    (base / "src" / "Engine.cpp").write_text("namespace cpp_pinyin::v2 {}\n", encoding="utf-8", newline="")
    (base / "data" / "dialects" / "mandarin").mkdir(parents=True, exist_ok=True)
    (base / "data" / "dialects" / "mandarin" / "dialect.json").write_text(
        '{\n    "id":  "mandarin",\n    "min_library":  "2.0.0"\n}\n', encoding="utf-8", newline="")


def self_test() -> int:
    """Every check gets a control that MUST make it red, and the positive fixture must be green.

    A gate whose controls were never shown to fire is a gate that only ever agrees with itself; the
    repository has paid for that lesson more than once (V38/V39, recorded in the since-deleted layout plan).
    """
    failures: list[str] = []
    with tempfile.TemporaryDirectory() as tmp:
        base = Path(tmp)
        types_h = base / "include" / "cpp-pinyin2" / "core" / "Types.h"

        def mutate_types(text: str) -> None:
            types_h.write_text(text, encoding="utf-8", newline="")

        controls = [
            ("namespace carries the wrong generation",
             lambda: mutate_types(GOOD_HEADER.replace("cpp_pinyin::v2", "cpp_pinyin::v3")),
             "declares namespace v3"),
            ("inline namespace",
             lambda: mutate_types(GOOD_HEADER.replace("namespace cpp_pinyin::v2",
                                                     "inline namespace cpp_pinyin::v2")),
             "inline namespace"),
            ("something declared in the product namespace",
             lambda: mutate_types(GOOD_HEADER + "\nnamespace cpp_pinyin\n{\n    using E = v2::Engine;\n}\n"),
             "directly in `namespace cpp_pinyin`"),
            ("root-style reference in header code",
             lambda: mutate_types(GOOD_HEADER.replace("int codeText(int code);",
                                                     "int codeText(int code);\n    cpp_pinyin::Engine *probe;")),
             "root-style reference"),
            ("include path from another generation",
             lambda: mutate_types(GOOD_HEADER.replace("<cpp-pinyin2/core/Global.h>",
                                                      "<cpp-pinyin3/core/Global.h>")),
             "belongs to generation 3"),
            ("declaring header without a version namespace",
             lambda: mutate_types("#pragma once\n\nnamespace cpp_pinyin\n{\n}\n"),
             "is not on EXEMPT"),
            ("a public header missing from the frozen list",
             lambda: (base / "include" / "cpp-pinyin2" / "core" / "Extra.h")
             .write_text(GOOD_HEADER.replace("Engine", "Extra"), encoding="utf-8", newline=""),
             "core/Extra.h"),
            ("macro prefix drift in the build",
             lambda: (base / "CMakeLists.txt")
             .write_text(GOOD_CMAKE + 'set(X "CPP_PINYIN3_EXPORT")\n', encoding="utf-8", newline=""),
             "generation(s) [3]"),
            ("empty scan scope",
             lambda: [p.unlink() for p in sorted((base / "include" / "cpp-pinyin2").rglob("*.h"))],
             "no headers found"),
            ("the data floor is newer than the package",
             lambda: (base / "data" / "dialects" / "mandarin" / "dialect.json")
             .write_text('{"min_library": "9.9.9"}', encoding="utf-8", newline=""),
             "newer than the package version"),
            ("the compile-time version macros drifted from the package",
             lambda: (base / "include" / "cpp-pinyin2" / "core" / "Version.h").write_text(
                 "#pragma once\n#define CPP_PINYIN2_VERSION_MAJOR 3\n#define CPP_PINYIN2_VERSION_MINOR 0\n"
                 "#define CPP_PINYIN2_VERSION_PATCH 0\n#define CPP_PINYIN2_VERSION_STRING \"3.0.0\"\n",
                 encoding="utf-8", newline=""),
             "a half-bump is the exact drift"),
            ("the CHANGELOG's newest heading names another version",
             lambda: (base / "CHANGELOG.md").write_text(
                 "# Changelog\n\n## [Unreleased] — 2.1.0（未发布）\n", encoding="utf-8", newline=""),
             "newest heading says 2.1.0"),
            # The nine below close the bypasses an adversarial review found on 2026-10-14. Each one passed
            # the previous revision of this gate; they are the reason the namespace patterns now run on
            # comment-free text and count openings instead of collecting them into a set.
            ("a comment between the namespace name and its brace",
             lambda: mutate_types(GOOD_HEADER + "\nnamespace cpp_pinyin   // router\n{\n    using E = v2::Engine;\n}\n"),
             "declares something directly in"),
            ("a macro that expands to the root namespace",
             lambda: mutate_types(GOOD_HEADER + "\n#define CPP_PINYIN_ROOT namespace cpp_pinyin\n"),
             "declares something directly in"),
            ("an inline namespace with a comment in the middle",
             lambda: mutate_types(GOOD_HEADER + "\ninline /* v2 */ namespace cpp_pinyin\n{\n    class Extra;\n}\n"),
             "inline namespace"),
            ("a comment that fakes the version namespace",
             # The comment must be a MULTI-LINE block whose inner line starts at column 0 with the
             # namespace spelling: that is exactly what the old line-anchored regex accepted as a real
             # namespace header. Anything with text before `namespace` on the line proves nothing.
             lambda: mutate_types("#pragma once\n\n/*\nnamespace cpp_pinyin::v2\n*/\nstruct Ghost { int x; };\n"),
             "declares no `namespace cpp_pinyin::v2`"),
            ("the version namespace opened twice",
             lambda: mutate_types(GOOD_HEADER + "\nnamespace cpp_pinyin::v2\n{\n}\n"),
             "opens `namespace cpp_pinyin::v2` 2 time(s)"),
            ("a public header with a non-.h extension",
             lambda: (base / "include" / "cpp-pinyin2" / "core" / "Extra.hpp")
             .write_text(GOOD_HEADER, encoding="utf-8", newline=""),
             "core/Extra.hpp"),
            ("a relative include into another generation",
             lambda: mutate_types(GOOD_HEADER.replace("<cpp-pinyin2/core/Global.h>",
                                                      "<./cpp-pinyin3/core/Global.h>")),
             "belongs to generation 3"),
            ("a macro-shaped include argument",
             lambda: mutate_types(GOOD_HEADER.replace("<cpp-pinyin2/core/Global.h>",
                                                      "CPP_PINYIN_HDR(2, core/Global.h)")),
             "is not a plain <path>"),
            ("a package face whose body names another generation",
             lambda: (base / "cpp-pinyin2.pc.in").write_text("Name: cpp-pinyin3\n", encoding="utf-8", newline=""),
             "names generation(s) [3]"),
            ("an exempt file that grew a namespace",
             lambda: (base / "include" / "cpp-pinyin2" / "core" / "PinyinGlobal.h")
             .write_text("namespace cpp_pinyin::v2\n{\n}\n", encoding="utf-8", newline=""),
             "is on EXEMPT"),
            # Multi-generation controls: a file that lives in generation 3 must carry v3's tokens, and a
            # header whose namespace disagrees with its directory is exactly the "wrong generation"
            # mistake this gate exists to catch -- it used to be invisible, because every check was
            # anchored at the PACKAGE's major rather than at the generation the file lives in.
            ("a later generation whose header opens the earlier namespace",
             lambda: add_generation_fixture(base, namespace="cpp_pinyin::v2"),
             "must agree"),
            ("a later generation whose header uses the earlier macro prefix",
             lambda: add_generation_fixture(base, macro="CPP_PINYIN2_"),
             "carries its own generation's token"),
            # The window, from the other side: three generations, or two that are not consecutive, mean the
            # release does not match what the policy promises. Both are realistic: v4 shipping while v2 is
            # still installed, and a tree that jumped (v2 then v4) with no migration guide possible.
            ("a third generation still in the tree",
             lambda: (add_generation_fixture(base, suffix="1", namespace="cpp_pinyin::v1",
                                             macro="CPP_PINYIN1_"),
                      add_generation_fixture(base, suffix="3")),
             "at most TWO may ship"),
            ("two generations that are not consecutive",
             lambda: add_generation_fixture(base, suffix="4", namespace="cpp_pinyin::v4",
                                            macro="CPP_PINYIN4_"),
             "must be consecutive"),
        ]

        build_fixture(base)
        positive = check(base).findings
        report_case("positive fixture is green", not positive,
                    f"expected no findings, got {positive}")

        # The v3 rehearsal in miniature: two generations side by side, each judged against its own token.
        # This is the case that FAILED before the generation-aware rewrite (the frozen header list looked
        # like it named files that did not exist, because only the current generation was scanned).
        build_fixture(base)
        add_generation_fixture(base)
        both = check(base).findings
        report_case("two generations coexisting are green", not both, f"findings={both}")

        # A foreign generation's macro prefix inside a STRING is data. Without this rule the gate reports
        # its own self-test fixtures the moment it is landed -- measured in the landing sandbox.
        build_fixture(base)
        (base / "include" / "cpp-pinyin2" / "core" / "Types.h").write_text(
            GOOD_HEADER + 'inline const char *kName = "CPP_PINYIN3_EXPORT";\n',
            encoding="utf-8", newline="")
        quoted = check(base).findings
        report_case("a foreign macro prefix inside a string literal is not a finding", not quoted,
                    f"findings={quoted}")

        # Axis 11 (H49): the released-form rule. Three pure cases pin the decision, then one case pins the
        # WIRING -- the lookup is replaced for that one so the case neither needs git installed nor a
        # fixture that is a checkout (the repo's own temp dir can sit inside a checkout, which is exactly
        # why tag_names() compares --show-toplevel with the root it was given).
        released_form = "## [2.0.0] — 2026-10-16"
        unreleased_form = "## [Unreleased] — 2.0.0（未发布）"
        report_case("a tagged release with the released heading is fine",
                    released_heading_problem(released_form, "2.0.0", True) is None)
        report_case("an untagged tree may keep the Unreleased heading",
                    released_heading_problem(unreleased_form, "2.0.0", False) is None)
        report_case("a tagged release that still says Unreleased is a finding",
                    released_heading_problem(unreleased_form, "2.0.0", True) is not None)

        real_lookup = globals()["tag_names"]
        try:
            globals()["tag_names"] = lambda _root: {"2.0.0"}
            build_fixture(base)
            tagged = check(base).findings
            report_case("control goes red: a tagged release whose CHANGELOG still says Unreleased",
                        any("must read `## [2.0.0]" in finding for finding in tagged),
                        f"findings={tagged}")
            build_fixture(base)
            (base / "CHANGELOG.md").write_text(
                "# Changelog\n\nAll notable changes to this project are documented here.\n\n"
                f"{released_form}\n", encoding="utf-8", newline="")
            after = check(base).findings
            report_case("a tagged release with the released heading stays green", not after,
                        f"findings={after}")
        finally:
            globals()["tag_names"] = real_lookup

        for name, mutate, expected in controls:
            build_fixture(base)
            mutate()
            findings = check(base).findings
            hit = any(expected in finding for finding in findings)
            report_case(f"control goes red: {name}", hit,
                        f"expected {expected!r} in {findings}")

    return finish_self_test()


#: One line per case, in the shape tools/check_doc_claims.py measures: it counts
#: `^(PASS|FAIL) self-test:` lines from the tool's own output (`measure_selftests`), so a summary-only
#: self-test cannot be documented and its count drifts unnoticed. `check_bindings` and friends print in
#: this form, and a gate that lands without it makes "every gate reports its case count" false.
_SELF_TEST_FAILURES: list[str] = []
_SELF_TEST_CASES: list[str] = []


def report_case(name: str, ok: bool, detail: str = "") -> None:
    print(f"{'PASS' if ok else 'FAIL'} self-test: {name}")
    _SELF_TEST_CASES.append(name)
    if not ok:
        _SELF_TEST_FAILURES.append(f"{name}: {detail}")


def finish_self_test() -> int:
    failures = _SELF_TEST_FAILURES
    print(f"SELFTEST {'PASS' if not failures else 'FAIL'} -- "
          f"{len(_SELF_TEST_CASES)} case(s) reported above")
    for failure in failures:
        print("  " + failure)
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default=str(ROOT), help="repository root (default: this checkout)")
    parser.add_argument("--self-test", action="store_true", help="prove each check can fail, then exit")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    result = check(Path(args.root).resolve())
    for note in result.notes:
        print(f"note: {note}")
    for finding in result.findings:
        print(f"FAIL: {finding}")
    print(f"check_version_tokens: {'PASS' if not result.findings else 'FAIL'} "
          f"({len(result.findings)} finding(s))")
    return 1 if result.findings else 0


if __name__ == "__main__":
    sys.exit(main())
