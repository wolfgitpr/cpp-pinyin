"""Contract gates: what the shell must stay in step with, checked mechanically.

Each test here couples a hand-written copy to its authoritative source in the repository, so the
copy cannot drift quietly:

* the status codes and style numbering in ``_ffi``/``Style`` against ``c_api.h`` and ``Types.h``;
* the entry points the shell declares against the header's declarations and against the export
  gate's own expectation file (a three-way check: header == baseline == shell);
* the field ORDER of every struct the ABI passes across the boundary, against the header's own
  text -- field order is layout, and an inserted field moves the ones after it;
* the version chain against the single source in ``CMakeLists.txt``;
* the two mutating dictionary loaders' docstrings, so the "not thread-safe" warning cannot be
  deleted without a test going red (section 6.8.7 row 6c).

References to the plan doc follow `docs/DECISIONS.md` §2.1
"""

from __future__ import annotations

import ctypes
import re
from pathlib import Path

import pytest

import cpp_pinyin2
from cpp_pinyin2 import Engine, Style, version
from cpp_pinyin2 import _ffi

# --- parsers, one per authoritative file ------------------------------------


def parse_c_api_functions(header_text: str) -> set:
    """The function names the header declares.

    Every declaration in c_api.h starts with the export macro on the same line as the name, so
    anchoring on the macro avoids matching the names that appear in prose comments.

    The prefix carries the generation token (`cppp2_`), and it is matched literally: until 2026-10-18 this
    pattern still said `cppp_`, so it parsed NOTHING out of a 2.x header and the assertion below only failed
    because the other side of the comparison was non-empty. The `assert names` at the end is the guard --
    a parser that finds nothing must never read as agreement.
    """
    names = set()
    for line in header_text.splitlines():
        if "CPP_PINYIN2_EXPORT" not in line:
            continue
        match = re.search(r"\b(cppp2_[a-z0-9_]+)\s*\(", line)
        if match:
            names.add(match.group(1))
    assert names, "parsed no exported function out of c_api.h -- the pattern is stale, not the header"
    return names


def parse_expected_symbols(text: str) -> set:
    return {
        line.strip()
        for line in text.splitlines()
        if line.strip() and not line.strip().startswith("#")
    }


def parse_status_codes(header_text: str) -> dict:
    """The enum values of c_api.h's status block, keyed by the shell's own constant names."""
    codes = {}
    for name, value in re.findall(r"\bCPPP2_(\w+)\s*=\s*(\d+)", header_text):
        codes[name] = int(value)
    return codes


def parse_style_enum(header_text: str, enum_name: str) -> dict:
    """{member: value} for one `enum class <enum_name>` block in Types.h."""
    match = re.search(rf"enum class {enum_name}\b[^{{]*{{(.*?)}}", header_text, re.DOTALL)
    assert match, f"enum class {enum_name} not found in Types.h"
    return {
        name: int(value)
        for name, value in re.findall(r"(\w+)\s*=\s*(\d+)", match.group(1))
    }


def parse_struct_fields(header_text: str, tag: str) -> list:
    """The field names of `typedef struct <tag> { ... }`, in declaration order.

    Comments are stripped first: several fields carry a trailing `/* ... */` or a `// ...` and a
    naive split on ';' would fold that prose into the next field's name. The field name is the
    last identifier of each declaration, which holds for every shape in this header including the
    pointer-to-const-pointer field `const char *const *candidates`.
    """
    match = re.search(rf"typedef struct {tag}\b[^{{]*{{(.*?)}}", header_text, re.DOTALL)
    assert match, f"typedef struct {tag} not found in c_api.h"
    body = re.sub(r"/\*.*?\*/", " ", match.group(1), flags=re.DOTALL)
    body = re.sub(r"//[^\n]*", " ", body)
    fields = []
    for statement in body.split(";"):
        identifiers = re.findall(r"[A-Za-z_]\w*", statement)
        if identifiers:
            fields.append(identifiers[-1])
    return fields


# --- tests ------------------------------------------------------------------


def test_ffi_declares_exactly_the_c_api_surface(repo_root: Path):
    header = (repo_root / "include" / "cpp-pinyin2" / "ext" / "c_api.h").read_text(encoding="utf-8")
    baseline = (repo_root / "tests" / "abi" / "expected_c_api_symbols.txt").read_text(
        encoding="utf-8"
    )
    shell_source = Path(_ffi.__file__).read_text(encoding="utf-8")

    in_header = parse_c_api_functions(header)
    in_baseline = parse_expected_symbols(baseline)
    in_shell = set(re.findall(r"\blib\.(cppp2_[a-z0-9_]+)", shell_source))

    # NO literal count here. tests/abi/expected_c_api_symbols.txt IS the count, and the assertion below
    # compares the two as SETS. A number repeated in this file would be a third copy of a fact that
    # already lived in the header's prose and in the baseline file, and all three had to be found and
    # edited together every time the C ABI changed.
    assert in_header == in_baseline, (
        "the export gate's expectation and the header disagree: "
        f"only in header {in_header - in_baseline}, only in baseline {in_baseline - in_header}"
    )
    assert in_shell == in_header, (
        "the shell and the C ABI disagree: "
        f"undeclared in the shell {in_header - in_shell}, unknown to the header {in_shell - in_header}"
    )


def test_the_removed_entry_point_is_gone_from_both_sides(repo_root: Path):
    """``cppp_create`` was replaced by ``cppp2_open`` (S9a1) and must not survive anywhere.

    A shell that still bound it would fail at the first call with an AttributeError from ctypes,
    which is a runtime error in the host language with no C++ diagnostic; and a header that still
    declared it would keep the old "NULL handle plus a message" contract alive next to the new one.
    """
    header = (repo_root / "include" / "cpp-pinyin2" / "ext" / "c_api.h").read_text(encoding="utf-8")
    shell_source = Path(_ffi.__file__).read_text(encoding="utf-8")
    assert "cppp_create" not in header
    assert not re.search(r"\blib\.cppp_create\b", shell_source)
    assert "cppp2_open" in shell_source


def test_shell_struct_field_order_matches_the_header(repo_root: Path):
    """Field order IS layout, in both directions: what the shell reads and what the library writes.

    A field inserted mid-struct would keep every test above green (the names are all present) while
    moving every field after it -- and ctypes lays a Structure out with the C compiler's own rules,
    so nothing else in the shell would notice either.
    """
    header = (repo_root / "include" / "cpp-pinyin2" / "ext" / "c_api.h").read_text(encoding="utf-8")
    pairs = (
        ("cppp2_query_opts", _ffi.QueryOpts),
        ("cppp2_error", _ffi.Error),
        ("cppp2_syllable", _ffi.Syllable),
        ("cppp2_segment", _ffi.Segment),
        ("cppp2_result", _ffi.Result),
    )
    for tag, structure in pairs:
        assert [name for name, _ in structure._fields_] == parse_struct_fields(header, tag), tag

    # The ABI's growth rule, stated as the one property that makes an older caller safe: the field
    # added last (S9a2's include_reading) sits after every field that existed before it.
    #
    # Order alone is not enough, and S9c is why: the FIRST declaration of this struct ended in four
    # bytes of TRAILING PADDING (its last member was `mark_neutral_as_5`), so `include_reading`
    # first landed at offset 28 -- inside the bytes an older caller's own `struct_size` already
    # covers -- and the library read that caller's uninitialised stack word as an option. The
    # appended option must therefore be outside every older `sizeof`; `reserved_0` is the member
    # that claims that padding, and the boundary below is derived from the v1 fields themselves
    # rather than written down, exactly as tests/cpp/test_c_api.cpp's `OldQueryOptsV1` does.
    names = [name for name, _ in _ffi.QueryOpts._fields_]
    assert "reserved_0" in names, names
    # The option `include_reading` sits immediately after the padding claim, and everything after it
    # is another CLAIM, never an option: `reserved_1` (2026-10-19) claims the 4 bytes a 40-byte
    # struct ends in, so the next option appends at 40 instead of landing inside bytes an older
    # caller's `struct_size = 40` already covers (S9c, one declaration later).
    assert names[names.index("reserved_0") + 1] == "include_reading", names
    tail = names[names.index("include_reading") + 1:]
    assert all(name.startswith("reserved_") for name in tail), (
        f"the fields after include_reading must be padding claims, got {tail}")

    class _OldQueryOptsV1(ctypes.Structure):
        _fields_ = _ffi.QueryOpts._fields_[:6]

    appended = _ffi.QueryOpts.include_reading.offset
    assert appended >= ctypes.sizeof(_OldQueryOptsV1), (
        "the appended option reached back inside an older declaration's bytes: "
        f"offset {appended} < {ctypes.sizeof(_OldQueryOptsV1)}"
    )


def test_status_codes_match_the_header(repo_root: Path):
    """The shell's whole status domain must equal the header's -- not a subset of it.

    2026-10-19: this test used to compare exactly five codes, so a sixth in the header was invisible
    and `CPPP2_ERR_DICT_INCOMPATIBLE` fell through to "unknown CPPP_ status 5" in every binding.
    The set is now built from the header, so a new code in `c_api.h` is red here until the shell
    carries it (and a shell-only code is red too, since the dicts must match).
    """
    header = (repo_root / "include" / "cpp-pinyin2" / "ext" / "c_api.h").read_text(encoding="utf-8")
    codes = parse_status_codes(header)
    shell = {
        "OK": _ffi.OK,
        "INVALID_ARG": _ffi.INVALID_ARG,
        "NOT_READY": _ffi.NOT_READY,
        "LOAD": _ffi.LOAD,
        "INTERNAL": _ffi.INTERNAL,
        "DICT_INCOMPATIBLE": _ffi.DICT_INCOMPATIBLE,
    }
    names = {
        _ffi.OK: "CPPP2_OK",
        _ffi.INVALID_ARG: "CPPP2_ERR_INVALID_ARG",
        _ffi.NOT_READY: "CPPP2_ERR_NOT_READY",
        _ffi.LOAD: "CPPP2_ERR_LOAD",
        _ffi.INTERNAL: "CPPP2_ERR_INTERNAL",
        _ffi.DICT_INCOMPATIBLE: "CPPP2_ERR_DICT_INCOMPATIBLE",
    }
    # `parse_status_codes` keys are the header's own spelling (`ERR_DICT_INCOMPATIBLE` etc.); the
    # shell puts the common `ERR_` prefix back, so compare the numeric domain, not the key names.
    expected_numbers = set(codes.values())
    assert set(shell.values()) == expected_numbers, (
        f"shell status domain {sorted(shell.values())} != header {sorted(expected_numbers)}")
    assert _ffi._NAMES == names


def test_unknown_status_is_not_success():
    with pytest.raises(cpp_pinyin2.CppPinyinError, match="unknown CPPP_ status 99"):
        _ffi.raise_for_status(99, "cppp_test")
    assert _ffi.raise_for_status(_ffi.OK, "cppp_test") is None


def test_style_enum_matches_types_h(repo_root: Path):
    types = (repo_root / "include" / "cpp-pinyin2" / "core" / "Types.h").read_text(encoding="utf-8")
    header_pairs = {
        **parse_style_enum(types, "MandarinStyle"),
        **parse_style_enum(types, "CantoneseStyle"),
    }
    # `DEFAULT` is deliberately NOT in `header_pairs`: it is not a style, it is the sentinel that
    # `cpp_pinyin2::v2::Style{}` carries in Types.h -- "whatever the dialect defaults to". It gets its own
    # two assertions below, so a *style* appearing on either side still shows up as drift while the
    # sentinel stays the one member allowed to be negative.
    shell_pairs = {name: member.value for name, member in Style.__members__.items()
                   if name != "DEFAULT"}
    assert shell_pairs == header_pairs, (
        "the shell's Style numbering drifted from Types.h: "
        f"only in shell {set(shell_pairs.items()) - set(header_pairs.items())}, "
        f"only in header {set(header_pairs.items()) - set(shell_pairs.items())}"
    )
    assert int(Style.DEFAULT) == -1, "the shell's sentinel must be -1"
    assert "int alias = -1;" in types, (
        "the C++ sentinel (`Style::alias`, Types.h) must still be -1 -- the two layers default to the "
        "same 'unset' value, which is what C4/N27 made true"
    )


def test_version_chain_three_of_six(repo_root: Path):
    """① CMake source -> ② cppp2_version() -> ③ _version.py.

    The other three places (④ wheel METADATA, ⑤ C# version.props, ⑥ nuspec) are produced by the
    packaging jobs; BindingVersionGate covers all six once those exist.
    """
    text = (repo_root / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(
        r"project\(\s*cpp-pinyin2\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)", text
    )
    assert match, "the single version source project(... VERSION) is missing from CMakeLists.txt"
    source_version = match.group(1)
    assert source_version == version() == cpp_pinyin2.__version__, (
        "the version chain must agree end to end:  CMakeLists.txt project(... VERSION) -> cppp2_version() "
        "-> _version.py"
    )
    # A well-formed three-part version, so the chain above cannot pass by all three sides being EMPTY. The
    # literal "2.0.0" used to be part of that assertion, which made this file a second home for the version
    # (and contradicted the test immediately below it, whose name says the literal lives in CMakeLists only).
    assert re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", source_version), (
        f"the version in CMakeLists.txt is not a three-part version: {source_version!r}"
    )


def test_version_literal_appears_nowhere_else_it_must_not(repo_root: Path):
    """Decision D32/D34: the version literal lives in CMakeLists.txt only.

    README and the API docs carry pointers instead, so this checks the two document entry points that used to
    carry a literal.

    Until 2026-10-10 this function asserted only that README has no `GIT_TAG v1.` -- which is not the rule in
    its own name, and the rule was in fact broken in the tree it was supposedly guarding (two API-doc sentences
    spelled the version out).

    The needle is the package's OWN version, taken from the single source rather than matched as "any x.y.z":
    that is what keeps the check quiet where it should be. Measured against the three documents, a generic
    `\\d+\\.\\d+\\.\\d+` scan fires on nine lines, eight of them legitimate (a third-party `pypinyin 0.55.0`, the
    1.x tag in the migration table, a version-looking file name); scanning for the derived version fires on the
    two real violations and nothing else.

    A line that records the version as HISTORY carries the repository's history marker, the same one
    tools/check_doc_claims.py recognises. Silence is not an exemption; the marker shows up in review.
    """
    readme = (repo_root / "README.md").read_text(encoding="utf-8")
    assert "GIT_TAG v1." not in readme, "the FetchContent example must use a placeholder tag"

    source = (repo_root / "CMakeLists.txt").read_text(encoding="utf-8")
    found = re.search(r"project\(\s*cpp-pinyin2\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)", source)
    assert found, "the single version source is missing, so this check cannot know what to look for"
    version = found.group(1)

    offenders = []
    for name in ("README.md", "docs/API_en.md", "docs/API_zh.md"):
        path = repo_root / name
        if not path.is_file():
            continue
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if version in line and "doc-claims: historical" not in line:
                offenders.append(f"{name}:{number}: {line.strip()[:96]}")

    assert not offenders, (
        f"the package version {version} is spelled out in a document entry point, which makes that document a "
        f"second home for it. Either point at the single source (`project(... VERSION)` in the root "
        f"CMakeLists.txt) or mark the line as history with `<!-- doc-claims: historical -->`:\n  "
        + "\n  ".join(offenders)
    )


@pytest.mark.parametrize("loader", ["load_user_dict", "load_traditional_dict"])
def test_mutating_loaders_document_that_they_are_not_thread_safe(loader: str):
    doc = getattr(Engine, loader).__doc__
    assert doc is not None
    assert "must not" in doc and "not thread-safe" in doc, (
        f"{loader}'s docstring must keep the explicit 'must not ... not thread-safe' warning"
    )
