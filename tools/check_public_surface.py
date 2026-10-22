#!/usr/bin/env python3
"""The API pair's public-surface index must agree with the public headers, in both directions.

WHY THIS EXISTS. The documentation review found that `docs/API_zh.md` / `docs/API_en.md` were using
`Syllable` in ordinary prose while never defining it, and that five other public types had no entry either --
`Error` / `ErrorException` (the payload of the only error channel), `ToneKind` / `ReadingSource`, `StyleDecl`
(a field of the public `Dialect` aggregate) and `ToneConverter` (the base class a plugin author subclasses,
which the C++ section of the same document called "not part of the public API"). Every one of those was found
by a human reading a table. Nothing mechanical could have found them, because nothing knew what the documents
were SUPPOSED to cover.

WHAT IT CHECKS, both directions:
  * every type a public header declares has a row in the index -- so a new public type cannot ship
    undocumented, and
  * every name in the index is really declared in a public header -- so a deleted or renamed type leaves a
    dangling row.

WHAT IT DOES NOT DO: it does not judge the `audience` column (`consumer` / `plugin` / `internal`) or the prose
in the other columns. Those are the judgement calls the index exists to make visible, and the gate's job is to
keep the LIST complete, not to have an opinion about it.

The index lives in `docs/API_zh.md` and `docs/API_en.md` and is parsed identically from both, so the pair
cannot drift apart without failing here.

USAGE
    python tools/check_public_surface.py              # exit 1 on any disagreement
    python tools/check_public_surface.py --self-test  # prove the check can go red, then exit
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

ROOT = Path(__file__).resolve().parent.parent

DOCUMENTS = ("docs/API_zh.md", "docs/API_en.md")
PUBLIC_ROOT = "include/cpp-pinyin2"

#: A type declaration in a public header, at NAMESPACE level. `class Impl;` nested in a class body is not a
#: public TYPE, so lines that are bare forward declarations indented deeper than one space are skipped --
#: `check_layers.py` learned this the hard way, reporting the public `Registry` as a leak because a nested
#: `class Impl;` counted as a declaration.
DECL_RX = re.compile(r"^\s*(?:struct|class|enum class|enum)\s+(?:CPP_PINYIN2_EXPORT\s+)?(\w+)")
NESTED_FORWARD_RX = re.compile(r"^\s{2,}(?:struct|class)\s+\w+\s*;")

#: `| \`Name\` | \`audience\` | \`file\` | purpose |` -- the index rows.
ROW_RX = re.compile(r"^\|\s*`(\w+)`\s*\|\s*`(\w+)`\s*\|\s*`([\w./]+)`\s*\|")

#: Types a public header declares that deliberately get NO index row, each with the reason. Kept small on
#: purpose: an entry here is a statement that a reader never needs the name.
EXCLUDED = {
    "Impl": "a nested implementation class (`Engine::Impl`), never named in a public signature",
}


def declared_types(root: Path) -> dict:
    """name -> the header that DEFINES it (falling back to the first header that names it)."""
    found: dict = {}
    defined: set = set()
    for path in sorted((root / PUBLIC_ROOT).rglob("*.h")):
        origin = path.relative_to(root / PUBLIC_ROOT).as_posix()
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            if NESTED_FORWARD_RX.match(line):
                continue
            match = DECL_RX.match(line)
            if not match:
                continue
            name = match.group(1)
            opens_body = "{" in line
            # A type can be forward-declared in several headers; the definition is where it lives. Measured:
            # `core/Engine.h` forward-declares `Registry`, whose definition is in `ext/Registry.h`, so a naive
            # first-wins rule reported the index as pointing at the wrong file.
            if name not in found or (opens_body and name not in defined):
                found[name] = origin
                if opens_body:
                    defined.add(name)
    return found


def index_rows(root: Path, document: str) -> List[Tuple[str, str, str]]:
    lines = (root / document).read_text(encoding="utf-8", errors="replace").splitlines()
    rows: List[Tuple[str, str, str]] = []
    inside = False
    for line in lines:
        if line.startswith("## ") and ("索引" in line or "index" in line.lower()):
            inside = True
            continue
        if inside and line.startswith("## "):
            break
        if inside:
            match = ROW_RX.match(line)
            if match:
                rows.append((match.group(1), match.group(2), match.group(3)))
    return rows


MEMBER_FILE = "tests/abi/expected_public_members.txt"

#: The types whose public member declarations are frozen, with the declaration each one starts at.
#:
#: WHY FIVE AND NOT ONE (2026-10-20, audit round 2, G03). V2 §7 item 11 promised the member-signature
#: freeze for `Engine`/`Result`/`Syllable`/`ToneConverter`/`Outcome`; what landed scanned `Engine`
#: only, and the file said so ("Requiring a parameter list also drops data members, which `Engine` has
#: none of"). The gap was not theoretical: `Syllable::spelling` is cited in tests/ exactly zero times,
#: so renaming it -- the field that IS a syllable's identity -- changed the public surface with every
#: gate green. `Outcome` gets two sections because the `void` specialization is a separate class with
#: its own members (`value()`,`error()` differ in what they return).
MEMBER_SURFACE = (
    ("Engine", "core/Engine.h", "class CPP_PINYIN2_EXPORT Engine"),
    ("Result", "core/Types.h", "class CPP_PINYIN2_EXPORT Result"),
    ("Syllable", "core/Syllable.h", "struct CPP_PINYIN2_EXPORT Syllable"),
    ("ToneConverter", "core/ToneConverter.h", "class CPP_PINYIN2_EXPORT ToneConverter"),
    ("Outcome<T>", "core/Types.h", "class Outcome {"),
    ("Outcome<void>", "core/Types.h", "class Outcome<void> {"),
    # Added 2026-10-20 (audit round 2 §12.6 C1): `Segment` is `Result`'s element type -- a consumer
    # writes `for (const Segment &row : result)` and reads `row.text/reading/candidates/failure_kind/
    # rendered` -- and `EngineConfig` is the source of the C option table's fields. Neither was scanned
    # by anything: the freeze covered five types, and these two are the ones a rename here would reach
    # furthest (176 and 119 member references in the tree respectively, all of them test-side).
    ("Segment", "core/Types.h", "struct CPP_PINYIN2_EXPORT Segment"),
    ("EngineConfig", "core/Types.h", "struct CPP_PINYIN2_EXPORT EngineConfig"),
)


def public_members(root: Path, header: str, needle: str) -> List[str]:
    """The public member declarations of the type `needle` opens, normalised, in declaration order.

    Functions AND data members, because for a `struct` the data members are the surface: `Syllable`'s
    `spelling` is what a consumer reads, and a freeze that only counted `name(` missed it. `private:`
    and `protected:` end the scan -- what a subclass or the implementation can touch is not here.
    """
    path = root / PUBLIC_ROOT / header
    if not path.is_file():
        return []
    text = path.read_text(encoding="utf-8", errors="replace")
    at = text.find(needle)
    if at < 0:
        return []
    body = text[at + len(needle):]
    if not needle.rstrip().endswith("{"):   # `Outcome`/`Outcome<void>` needles carry their own brace
        brace = body.find("{")
        if brace < 0:
            return []
        body = body[brace + 1:]
    closing = body.find("\n    };")
    if closing < 0:
        closing = body.find("\n};")
    if closing >= 0:
        body = body[:closing]

    declarations: List[str] = []
    current: List[str] = []
    depth = 0
    for raw in body.splitlines():
        line = raw.split("//", 1)[0].strip()  # trailing comments are not part of the declaration
        if not line:
            continue
        if line.startswith(("private:", "protected:")):
            break
        if line.startswith("public:") or line.startswith("template"):
            continue
        current.append(line)
        depth += line.count("{") - line.count("}")
        joined = " ".join(current).rstrip()
        # A statement is complete only at brace depth 0. Without the depth test, the `;` INSIDE an inline
        # body ended the declaration early -- `value() &&` froze as far as its `throw` and the rest of the
        # body became a "new member" (measured 2026-10-20, first run of --freeze).
        if depth <= 0 and (joined.endswith(";") or joined.endswith("}")):
            normalised = " ".join(joined.split())
            current = []
            depth = 0
            if normalised.startswith("}") or normalised.startswith("static_assert"):
                continue
            # The SIGNATURE is the surface, not the body. An inline body is cut at its opening brace:
            # rewriting `value_or_throw`'s body is not a compatibility event, while changing its
            # `&&`-qualifier or return type is -- and both survive this cut.
            #
            # ONLY A BODY IS CUT. A brace can also belong to a DEFAULT ARGUMENT, and that is signature:
            # `query(std::string_view text, const EngineConfig &cfg = EngineConfig{})` used to be frozen
            # as `query(std::string_view text, const EngineConfig &cfg =;` -- the default expression, the
            # one part a caller actually depends on, was destroyed by the cut (2026-10-20, audit round 3
            # H01; visible as `=;` in tests/abi/expected_public_members.txt before this fix).
            if "{" in normalised:
                head, _rest = normalised.split("{", 1)
                # A brace is a DEFAULT ARGUMENT when the parameter list it sits in already contains an
                # `=` -- `query(std::string_view text, const EngineConfig &cfg = EngineConfig{})`. That
                # expression is signature, so it stays whole. Everything else opening after a parameter
                # list is a body, and bodies are cut.
                paren = head.rfind("(")
                is_default = paren >= 0 and "=" in head[paren:]
                if not is_default:
                    normalised = head.strip()
                if not normalised.endswith(";"):
                    normalised += ";"
            declarations.append(normalised)
    return declarations


def frozen_member_sections(root: Path) -> Dict[str, List[str]]:
    """{label: [declaration, ...]} out of the frozen file's `# --- Label (header) ---` sections."""
    path = root / MEMBER_FILE
    if not path.is_file():
        return {}
    sections: Dict[str, List[str]] = {}
    current = ""
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        stripped = line.strip()
        if stripped.startswith("# ---"):
            current = stripped.strip("# -").split(" (")[0].strip()
            sections[current] = []
        elif stripped and not stripped.startswith("#") and current:
            sections[current].append(" ".join(stripped.split()))
    return sections


def check_member_surface(root: Path) -> Tuple[List[str], List[str]]:
    """Freeze the public member declarations of the seven types the surface freezes today."""
    problems: List[str] = []
    notes: List[str] = []
    frozen = frozen_member_sections(root)
    if not frozen:
        return [f"ERROR: {MEMBER_FILE} is missing or has no sections; regenerate it with "
                f"`--freeze` and review the diff"], notes

    for label, header, needle in MEMBER_SURFACE:
        found = public_members(root, header, needle)
        if not found:
            problems.append(f"ERROR: no public members parsed out of {PUBLIC_ROOT}/{header} for `{label}` "
                            f"-- refusing to pass vacuously (the parser or the declaration changed)")
            continue
        expect = frozen.get(label)
        if expect is None:
            problems.append(f"{MEMBER_FILE}: no section for `{label}` -- the type is in the freeze set but "
                            f"not in the file, so nothing about it is frozen")
            continue
        # ORDER, not set membership (2026-10-20, audit round 2 §12.6 C1). The previous revision compared
        # `sorted(set(...))`, which made a REORDERED header look identical to the frozen one -- and a
        # reorder of public data members changes both the aggregate-initialisation order and the layout a
        # consumer compiles against. Measured before the fix: swapping `EngineConfig::vToU` and
        # `markNeutralAs5` left every gate green. Sets also silently swallowed duplicates.
        if found == expect:
            notes.append(f"{PUBLIC_ROOT}/{header}: {label} has {len(found)} public member(s), "
                         f"frozen in declaration order")
            continue
        missing = [e for e in expect if e not in found]
        added = [e for e in found if e not in expect]
        for entry in missing:
            problems.append(f"{PUBLIC_ROOT}/{header}: the frozen member `{entry}` of `{label}` is GONE or "
                            f"changed declaration (see {MEMBER_FILE})")
        for entry in added:
            problems.append(f"{PUBLIC_ROOT}/{header}: `{entry}` is not among the frozen members of `{label}` "
                            f"-- a new or changed public declaration is a surface decision, not a detail")
        if not missing and not added:
            first = next(i for i in range(min(len(expect), len(found))) if expect[i] != found[i])
            problems.append(
                f"{PUBLIC_ROOT}/{header}: `{label}` declares `{found[first]}` at position {first + 1} where "
                f"{MEMBER_FILE} froze `{expect[first]}` -- REORDERING public members changes the aggregate "
                f"initialisation order and the layout a consumer compiles against, so it is a surface "
                f"decision too, not a detail")
            continue
    return problems, notes


def check(root: Path) -> Tuple[List[str], List[str]]:
    """(problems, notes). Problems are failures; notes describe what was checked."""
    declared = declared_types(root)
    if not declared:
        return [f"ERROR: no types found under {PUBLIC_ROOT}, refusing to pass vacuously"], []

    problems: List[str] = []
    notes: List[str] = []
    for document in DOCUMENTS:
        rows = index_rows(root, document)
        if not rows:
            problems.append(f"{document}: no index table found (expected a `## ... index ...` section "
                            f"with `| `Name` | `audience` | `file` |` rows)")
            continue
        indexed = {name for name, _audience, _origin in rows}
        for name in sorted(set(declared) - indexed - set(EXCLUDED)):
            problems.append(f"{document}: {name} is declared in {PUBLIC_ROOT}/{declared[name]} but has no "
                            f"row in the public-surface index")
        for name, audience, origin in rows:
            if name not in declared:
                problems.append(f"{document}: the index lists {name}, but no public header declares it "
                                f"(row points at {origin})")
            elif declared[name] != origin:
                problems.append(f"{document}: {name} is declared in {declared[name]}, but the index says "
                                f"{origin}")
            if audience not in ("consumer", "plugin", "internal"):
                problems.append(f"{document}: {name} has audience `{audience}`, which is not one of "
                                f"consumer / plugin / internal")
        notes.append(f"{document}: {len(rows)} indexed rows against {len(declared)} declared types")
    member_problems, member_notes = check_member_surface(root)
    problems += member_problems
    notes += member_notes
    for name, reason in sorted(EXCLUDED.items()):
        if name in declared:
            notes.append(f"excluded by policy: {name} ({reason})")
    return problems, notes


def self_test() -> int:
    """Prove the check can go red, and that it refuses to pass on an empty input set."""
    import shutil
    import tempfile

    failures: List[str] = []

    ran = 0

    def expect(label: str, ok: bool, detail: object = "") -> None:
        nonlocal ran
        ran += 1
        print(f"{'PASS' if ok else 'FAIL'}  self-test: {label}" + ("" if ok else f"  -> {detail}"))
        if not ok:
            failures.append(label)

    #: The fixture's public headers and the frozen member sections that go with them. Written into every
    #: fixture tree so that an INDEX case never fails because of the member surface (and vice versa).
    FIXTURE_HEADERS = {
        "core/Engine.h": "class CPP_PINYIN2_EXPORT Engine {\npublic:\n    Engine();\n};\n",
        "core/Syllable.h": "struct CPP_PINYIN2_EXPORT Syllable {\n    std::u16string spelling;\n};\n",
        "core/ToneConverter.h": ("class CPP_PINYIN2_EXPORT ToneConverter {\npublic:\n"
                                 "    virtual ~ToneConverter() {}\n};\n"),
    }
    FIXTURE_SECTIONS = {
        "Engine": ("core/Engine.h", ["Engine();"]),
        "Result": ("core/Types.h", ["Result();", "size_t size() const;"]),
        "Syllable": ("core/Syllable.h", ["std::u16string spelling;"]),
        "ToneConverter": ("core/ToneConverter.h", ["virtual ~ToneConverter();"]),
        "Outcome<T>": ("core/Types.h", ["bool has_value() const;"]),
        "Outcome<void>": ("core/Types.h", ["bool has_value() const;"]),
        "Segment": ("core/Types.h", ["std::string text;", "Code failure_kind;"]),
        "EngineConfig": ("core/Types.h", ["Style style;", "bool vToU = false;",
                                          "bool markNeutralAs5 = false;"]),
    }

    def build(root: Path, index_names: Sequence[str], declared_names: Sequence[str] = ("Alpha", "Beta"),
              origin: str = "core/Types.h") -> None:
        core = root / "include" / "cpp-pinyin2" / "core"
        core.mkdir(parents=True, exist_ok=True)
        (root / "docs").mkdir(parents=True, exist_ok=True)
        (core / "Types.h").write_text(
            "".join(f"struct {name} {{}};\n" for name in declared_names)
            + "class CPP_PINYIN2_EXPORT Result {\npublic:\n    Result();\n    size_t size() const;\n};\n"
            + "struct CPP_PINYIN2_EXPORT Segment {\n    std::string text;\n    Code failure_kind;\n};\n"
            + "struct CPP_PINYIN2_EXPORT EngineConfig {\n    Style style;\n    bool vToU = false;\n"
            + "    bool markNeutralAs5 = false;\n};\n"
            + "template <class T>\nclass Outcome {\npublic:\n    bool has_value() const;\n};\n"
            + "template <>\nclass Outcome<void> {\npublic:\n    bool has_value() const;\n};\n",
            encoding="utf-8")
        for relative, body in FIXTURE_HEADERS.items():
            (root / "include" / "cpp-pinyin2" / relative).write_text(body, encoding="utf-8")
        (root / "tests" / "abi").mkdir(parents=True, exist_ok=True)
        write_frozen(root)
        for document, audience in zip(DOCUMENTS, ("consumer", "consumer")):
            body = ["## Public surface index (automatic check)", "", "| Type | Audience | Declared in | Why |",
                    "|---|---|---|---|"]
            body += [f"| `{name}` | `{audience}` | `{origin}` | fixture |" for name in index_names]
            # Every type the member surface needs is also a DECLARED public type in the fixture, so each
            # needs an index row or every "should pass" case fails for an unrelated reason.
            for name, header in (("Engine", "core/Engine.h"), ("Result", "core/Types.h"),
                                 ("Syllable", "core/Syllable.h"), ("ToneConverter", "core/ToneConverter.h"),
                                 ("Outcome", "core/Types.h"), ("Segment", "core/Types.h"),
                                 ("EngineConfig", "core/Types.h")):
                body.append(f"| `{name}` | `consumer` | `{header}` | fixture |")
            (root / document).write_text("\n".join(body) + "\n", encoding="utf-8")

    def write_frozen(root: Path, overrides: Dict[str, List[str]] | None = None) -> None:
        out = ["# fixture"]
        for label, (header, entries) in FIXTURE_SECTIONS.items():
            rendered = (overrides or {}).get(label, entries)
            out.append(f"# --- {label} ({header}) ---")
            out.extend(rendered)
        (root / MEMBER_FILE).write_text("\n".join(out) + "\n", encoding="utf-8")

    with tempfile.TemporaryDirectory() as raw:
        root = Path(raw)

        def run(expect_failure: bool, label: str) -> None:
            problems, notes = check(root)
            expect(label, bool(problems) == expect_failure, problems + notes)

        build(root, ["Alpha", "Beta"])
        run(False, "a complete index passes")

        build(root, ["Alpha"])
        run(True, "a declared type missing from the index fails")

        build(root, ["Alpha", "Beta", "Ghost"])
        run(True, "an indexed name that is not declared fails")

        build(root, ["Alpha", "Beta"], origin="core/Wrong.h")
        run(True, "a row pointing at the wrong file fails")

        build(root, ["Alpha"], declared_names=("Alpha", "Impl"))
        run(False, "the excluded nested class needs no row")

        # the exclusion must not be a blanket: a REAL missing type is still caught in the same fixture
        build(root, ["Alpha"], declared_names=("Alpha", "Impl", "Real"))
        run(True, "but a real missing type still fails alongside the exclusion")

        # empty input must abort rather than pass
        with tempfile.TemporaryDirectory() as empty:
            root = Path(empty)
            (root / "include" / "cpp-pinyin2").mkdir(parents=True)
            run(True, "no public types aborts instead of passing")

        # --- the member surface of the five promised types (plan D11=A, V2 §7 item 11, G03) ---
        build(root, ["Alpha", "Beta"])
        run(False, "the frozen member list passes when nothing moved")

        write_frozen(root, {"Engine": ["Engine();", "Engine(bool);"]})
        run(True, "a member that is gone from the header fails")

        write_frozen(root)
        (root / "include" / "cpp-pinyin2" / "core" / "Engine.h").write_text(
            "class CPP_PINYIN2_EXPORT Engine {\npublic:\n    Engine();\n    int added() const;\n};\n",
            encoding="utf-8")
        run(True, "a newly added public member fails until it is frozen on purpose")

        # The case the freeze existed for and did not cover until 2026-10-20: a DATA member of a type
        # nobody scans. Renaming `Syllable::spelling` was invisible to every gate on the tree.
        build(root, ["Alpha", "Beta"])
        (root / "include" / "cpp-pinyin2" / "core" / "Syllable.h").write_text(
            "struct CPP_PINYIN2_EXPORT Syllable {\n    std::u16string pivot;\n};\n", encoding="utf-8")
        run(True, "a renamed data member of Syllable fails")

        # §12.6 C1: a REORDER compared equal under the old set-based comparison. Swap two members of
        # `EngineConfig` -- same set, different order -- and the gate must go red with the ORDER message.
        build(root, ["Alpha", "Beta"])
        types_h = root / "include" / "cpp-pinyin2" / "core" / "Types.h"
        body = types_h.read_text(encoding="utf-8")
        types_h.write_text(body.replace(
            "    bool vToU = false;\n    bool markNeutralAs5 = false;",
            "    bool markNeutralAs5 = false;\n    bool vToU = false;"), encoding="utf-8")
        problems, _notes = check(root)
        expect("reordering two frozen members fails", any("REORDERING" in p for p in problems), problems)

        write_frozen(root)
        (root / MEMBER_FILE).unlink()
        run(True, "a missing frozen list is an error, not a pass")

        # A section dropped from the file is a hole in the freeze, not a smaller one.
        write_frozen(root)
        body = (root / MEMBER_FILE).read_text(encoding="utf-8")
        (root / MEMBER_FILE).write_text(
            body.replace("# --- Outcome<void> (core/Types.h) ---\nbool has_value() const;\n", ""),
            encoding="utf-8")
        run(True, "a type with no section in the frozen file fails")

    # The parser rule itself (2026-10-20, audit round 3 H01): a DEFAULT ARGUMENT is signature and must
    # survive the body cut, while an inline BODY must still be cut. Both halves are asserted because the
    # first version of the fix got one and broke the other -- it kept `= EngineConfig{}` but froze
    # `normalise(...) const { return s; }` verbatim, turning a body into surface.
    with tempfile.TemporaryDirectory() as parser_tmp:
        probe_dir = Path(parser_tmp) / PUBLIC_ROOT / "core"
        probe_dir.mkdir(parents=True)
        (probe_dir / "Probe.h").write_text(
            "struct CPP_PINYIN2_EXPORT Probe {\n"
            "    Outcome<Result> query(std::string_view t, const EngineConfig &cfg = EngineConfig{}) const;\n"
            "    std::u16string normalise(const std::u16string &s) const { return s; }\n"
            "};\n", encoding="utf-8")
        parsed = public_members(Path(parser_tmp), "core/Probe.h", "struct CPP_PINYIN2_EXPORT Probe")
        expect("a default argument survives the body cut",
               any("= EngineConfig{}" in declaration for declaration in parsed), parsed)
        expect("an inline body is still cut to its signature",
               any(declaration.endswith("normalise(const std::u16string &s) const;") for declaration in parsed),
               parsed)

    print(f"self-test: {ran} case(s), {len(failures)} failure(s)"
          + (": " + "; ".join(failures) if failures else ""))
    if not ran:
        print("self-test: NO CASES RAN -- a self-test that checks nothing must not report success")
        return 1
    return 1 if failures else 0


def freeze(root: Path) -> int:
    """Rewrite the frozen member file from the tree. The diff is the review."""
    lines = ["# Public member declarations frozen at 2.0.0 -- every type the public surface freezes.",
             "#",
             "# `2.0.0` is a PROVENANCE anchor (when this freeze was cut), NOT a copy of the current",
             "# PROJECT_VERSION: bumping the version must NOT bump it, or the sentence stops being true about",
             "# the release it describes. Deliberate, and the reason the version gate does not police this",
             "# line (audit round 3, H26). The declarations below are what it is checked for.",
             "#",
             "# Regenerate with `python tools/check_public_surface.py --freeze` and READ the diff: every",
             "# changed line here is a change to the surface a C++ consumer compiles against.",
             "# Format: `# --- <label> (<header>) ---` then one declaration per line, whitespace-normalised.",
             "# `private:`/`protected:` sections are not part of it, and neither are comments."]
    for label, header, needle in MEMBER_SURFACE:
        found = public_members(root, header, needle)
        if not found:
            print(f"FAIL  no public members parsed for `{label}` out of {PUBLIC_ROOT}/{header}")
            return 1
        lines.append("")
        lines.append(f"# --- {label} ({header}) ---")
        lines.extend(found)
    target = root / MEMBER_FILE
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {MEMBER_FILE}: "
          + ", ".join(f"{label} {len(public_members(root, header, needle))}"
                      for label, header, needle in MEMBER_SURFACE))
    return 0


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--self-test", action="store_true", help="prove the check can go red, then exit")
    parser.add_argument("--freeze", action="store_true",
                        help=f"rewrite {MEMBER_FILE} from the tree (review the diff)")
    options = parser.parse_args(argv)

    if options.self_test:
        return self_test()
    if options.freeze:
        return freeze(ROOT)

    problems, notes = check(ROOT)
    for problem in problems:
        print(f"FAIL  {problem}")
    for note in notes:
        print(f"note  {note}")
    print("PASS" if not problems else "FAILED")
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
