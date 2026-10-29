#!/usr/bin/env python3
"""Do the cross-references this repository writes actually land?

Three kinds of reference rot a reader meets and no other gate sees:

  * **section references that point nowhere** -- a header cites `docs/CONTRACT.md` "section 修订层 B"
    and CONTRACT has no such section, or cites a section of a document that has since moved;
  * **decision numbers nothing defines** -- `(S7)`, `(S9c)`, `(D4)`, `(R14)` scattered through `include/`
    and `src/`, where the ledger that defined them is not in the tracked tree and a consumer reading a
    public header has no way to resolve it;
  * **paths to directories that no longer exist** (rule retired once the last one was gone).

WHAT IT CHECKS (three rules, each with a control in `--self-test`):

  1. `docs/<file>.md` **section <n>** must resolve: the target file must have a heading whose number starts
     with <n> (or, for a non-numeric token such as `修订层 B`, a heading that contains it);
  2. Every `(TOKEN)` decision number in code must be registered in `docs/DECISIONS.md`, and a token
     registered as `retired` may not appear under `include/`: a public header must point at a document that
     ships with the package, not at a ledger that was deleted;
  3. (retired) citing `docs/testdata/` used to require a pointer word.

WHAT IT DOES NOT DO. It does not judge whether the cited section says what the sentence claims -- that is
not mechanically checkable (see `audit_doc_anchors.py`, which learned the same lesson on line numbers). It
does not check bare `section 6.8.4` references that name no file: guessing the target would produce false
findings.

USAGE
    python tools/check_doc_refs.py                  # exit 1 on any finding
    python tools/check_doc_refs.py --verbose        # also print what was scanned
    python tools/check_doc_refs.py --self-test      # prove each rule can go red, then exit
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Dict, List, Tuple

ROOT = Path(__file__).resolve().parent.parent

REGISTRY = "docs/DECISIONS.md"
HISTORICAL = "doc-anchors: historical"

#: Files whose references are checked. Everything tracked, minus the vendor tree (third-party code is
#: not ours to rewrite), the scratch tree and the corpus data. The archived ledgers used to be skipped
#: here as well; they were retired on 2026-10-22, so there is nothing left to skip.
SKIP_PREFIXES = ("vendor/", ".tmp/", "tests/data/")

#: Where `(TOKEN)` decision numbers are looked for. `include/` is held to the stricter rule.
CODE_DIRS = ("include/", "src/", "tests/", "tools/", "csharp/", "python/")
TEXT_SUFFIXES = {".h", ".hpp", ".c", ".cpp", ".cs", ".py", ".yml", ".yaml", ".txt", ".md", ".cmake"}

#: Directories exempt from rules 1 and 3 because they quote history constantly -- including, by design,
#: the dead references this check exists to find. It is EMPTY now: `docs/plans/` and `docs/history/`
#: were retired on 2026-10-22 and replaced by two live records whose anchors are checked like anyone
#: else's. The constant stays so rule 1 and rule 3 keep one place to state the exemption.
LEDGER_DIRS = ()


#: Files exempt from rule 2 because their TEXT IS the fixture: the self-test below spells (S1)/(S2)/(S99)
#: on purpose, and a rule that scanned them would report its own controls as defects.
TOKEN_EXEMPT_FILES = {"tools/check_doc_refs.py"}

#: `docs/X.md` section 4.5  /  `docs/X.md` section 修订层 B  /  docs/X.md section 6-9
SECTION_RX = re.compile(
    r"docs/([A-Za-z0-9_.\- ]+\.md)`?\s*(?:section|§)\s*([0-9]+(?:\.[0-9]+)*|[^\s,;:)\]]{1,24})")
#: THE SAME REFERENCE WITHOUT THE `docs/` PREFIX. Added 2026-10-27, after a script justified itself with
#: `PLUGIN_INTERFACE_AND_BINDINGS.md section 6.7.11` -- a document that has never existed in this
#: repository -- and this gate, written for exactly that failure mode, said nothing at all: SECTION_RX
#: above only looks inside `docs/`.
#:
#: A bare name is resolved against `docs/` first and then the repository root (`CHANGELOG.md` lives
#: there), and only a name that resolves to a real file is judged. A name that resolves to nothing is
#: ignored on purpose: prose is allowed to mention files outside this tree, and turning every such
#: mention into a finding would teach people to switch the gate off.
BARE_SECTION_RX = re.compile(
    r"(?<![\w/.-])([A-Za-z0-9_.\-]+\.md)`?\s*(?:section|§)\s*([0-9]+(?:\.[0-9]+)*|[^\s,;:)\]]{1,24})")
#: A decision number: one to three letters, an optional dash, one or two digits, an optional letter
#: (`S7`, `D4`, `N4b`, `S11a`, `C-13`, `T-01`). The prefix set is deliberately finite: it is the one
#: the registry covers, and encodings such as `(UTF-8)` must not be mistaken for a decision number.
#:
#: `F`/`E`/`I`/`L`/`M`/`Q` are ABSENT on purpose (2026-10-20): they are per-round
#: audit labels that mean nothing outside the ledger that minted them, so requiring every one of them to
#: be registered would turn each round's working notes into registry debt. The decision and the escape
#: hatch (a label that must be cited across documents gets a registered prefix instead) are written in
#: `docs/DECISIONS.md`, section "前缀清单：哪些立册、哪些不立册" -- one place, not this comment.
#: Two shapes, because a number that resolves to nothing is equally useless in either:
#:   (D4)                         the parenthesised form this rule was written for
#:   D4 (docs/DECISIONS.md ...)   the same number in front of its source, which the first shape MISSED
#: The second shape is not hypothetical: on 2026-10-22 twelve citations of the form `D4 (docs/HISTORY.md)`
#: were green while pointing at a file that never held a D4, because this regex only looked inside parens.
TOKEN_RX = re.compile(
    r"\(((?:S|D|R|N|C|T|G|K|A|P)-?[0-9]{1,2}[a-z]?)\)"
    r"|\b((?:S|D|R|N|C|T|G|K|A|P)-?[0-9]{1,2}[a-z]?)\s+\((?:docs/)?[A-Za-z_]+\.md"
)
NOT_A_TOKEN = {"UTF-8", "UTF-16", "UTF-32"}
HEADING_RX = re.compile(r"^#{1,6}\s+(.*)$", re.M)
#: `**2. 发布前的构建与验收**` -- a numbered section written as a bold paragraph instead of a heading.
#: The release checklist numbers everything this way, so a heading-only reader sees a document with no
#: sections at all.
BOLD_SECTION_RX = re.compile(r"^\*\*([0-9]+(?:\.[0-9]+)*\.?\s+[^*]{1,80})\*\*", re.M)
REGISTRY_RX = re.compile(r"^\|\s*`([A-Z][A-Za-z]*-?[0-9]{1,2}[a-z]?)`\s*\|\s*(live|retired)\s*\|", re.M)


def tracked_text_files(root: Path) -> List[str]:
    completed = subprocess.run(["git", "ls-files"], cwd=root, capture_output=True, text=True)
    if completed.returncode != 0:
        raise SystemExit(f"check_doc_refs: git ls-files failed in {root}: {completed.stderr.strip()}")
    out = []
    for rel in completed.stdout.splitlines():
        if rel.startswith(SKIP_PREFIXES):
            continue
        if Path(rel).suffix.lower() in TEXT_SUFFIXES:
            out.append(rel)
    return out


def headings_of(path: Path) -> List[str]:
    """Every section identifier the document carries.

    Markdown headings, plus the bold-paragraph numbering style that `docs/RELEASE_CHECKLIST.md` uses
    throughout (`**2. 发布前的构建与验收**`). Without the second shape, every legitimate reference into
    that document reads as dead -- measured 2026-10-27, when the bare-reference rule below was added and
    immediately reported the runbook's `RELEASE_CHECKLIST.md section 2` as missing.
    """
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return []
    found = [m.group(1).strip() for m in HEADING_RX.finditer(text)]
    found.extend(m.group(1).strip() for m in BOLD_SECTION_RX.finditer(text))
    return found


def section_is_present(headings: List[str], cited: str) -> bool:
    """Does any heading of the target document carry this section identifier?

    Numeric identifiers match a heading whose leading number is `cited` or a deeper one (`4.5` matches
    `4.5.1`); non-numeric ones (`修订层 B`) match a heading that contains the token verbatim.
    """
    if cited and cited[0].isdigit():
        for heading in headings:
            lead = re.match(r"^§?\s*([0-9]+(?:\.[0-9]+)*)", heading)
            if lead and (lead.group(1) == cited or lead.group(1).startswith(cited + ".")):
                return True
        return False
    return any(cited in heading for heading in headings)


def load_registry(root: Path) -> Dict[str, str]:
    path = root / REGISTRY
    if not path.is_file():
        raise SystemExit(f"check_doc_refs: {REGISTRY} is missing -- it is the table this check reads")
    return {token: status for token, status in REGISTRY_RX.findall(path.read_text(encoding="utf-8"))}


def check(root: Path, files: List[str]) -> Tuple[List[str], List[str], int]:
    """(findings, notes, counts) for the three rules."""
    findings: List[str] = []
    notes: List[str] = []
    registry = load_registry(root)
    if not registry:
        raise SystemExit(f"check_doc_refs: {REGISTRY} has no `| token | live/retired |` rows -- "
                         "refusing to pass on an empty registry")
    checked = {"sections": 0, "tokens": 0, "retired_dirs": 0}
    heading_cache: Dict[str, List[str]] = {}

    for rel in files:
        text = (root / rel).read_text(encoding="utf-8", errors="replace")
        historical = HISTORICAL in text
        is_ledger = rel.startswith(LEDGER_DIRS)

        if not is_ledger and not historical:
            for match in SECTION_RX.finditer(text):
                target, cited = match.group(1), match.group(2)
                checked["sections"] += 1
                target_path = root / "docs" / target
                if not target_path.is_file():
                    findings.append(f"{rel}: cites `docs/{target}` section {cited}, but that file does not exist")
                    continue
                if target not in heading_cache:
                    heading_cache[target] = headings_of(target_path)
                if not section_is_present(heading_cache[target], cited):
                    findings.append(f"{rel}: cites `docs/{target}` section {cited}, which that file has no "
                                    f"heading for (headings: {len(heading_cache[target])})")

            for match in BARE_SECTION_RX.finditer(text):
                name, cited = match.group(1), match.group(2)
                resolved = next((p for p in (root / "docs" / name, root / name) if p.is_file()), None)
                if resolved is None:
                    continue        # names nothing in this tree: prose about somebody else's file
                checked["sections"] += 1
                key = str(resolved)
                if key not in heading_cache:
                    heading_cache[key] = headings_of(resolved)
                if not section_is_present(heading_cache[key], cited):
                    findings.append(
                        f"{rel}: cites `{name}` section {cited}, which has no such heading "
                        f"(resolved to {resolved.relative_to(root).as_posix()})")

        if rel.startswith(CODE_DIRS) and rel not in TOKEN_EXEMPT_FILES:
            for match in TOKEN_RX.finditer(text):
                token = match.group(1) or match.group(2)
                if token in NOT_A_TOKEN:
                    continue
                checked["tokens"] += 1
                status = registry.get(token)
                if status is None:
                    findings.append(f"{rel}: cites `({token})`, which {REGISTRY} does not register")
                elif rel.startswith("include/") and status != "live":
                    findings.append(
                        f"{rel}: cites `({token})`, which is registered as `retired` -- a public header "
                        f"must point at a section of a shipped document instead")

    notes.append(f"scanned {len(files)} tracked text file(s); {checked['sections']} section "
                 f"reference(s), {checked['tokens']} decision-number(s)")
    notes.append(f"registry: {len(registry)} token(s), "
                 f"{sum(1 for s in registry.values() if s == 'live')} live")
    return findings, notes, len(files)


def self_test() -> int:
    """Plant one violation per rule and require the check to go red, then clean them and require green."""
    failures: List[str] = []
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / "docs").mkdir()
        (root / "include").mkdir()
        (root / "src").mkdir()
        (root / REGISTRY).write_text(
            "| `S1` | live | rule one | `docs/CONTRACT.md` |\n"
            "| `S2` | retired | old | none |\n", encoding="utf-8")
        (root / "docs" / "CONTRACT.md").write_text("# 1. Alpha\n\n## 2. Beta\n", encoding="utf-8")
        (root / "docs" / "OLD.md").write_text("# 11. Gamma\n", encoding="utf-8")

        good = root / "src" / "good.cpp"
        good.write_text("// `docs/CONTRACT.md` section 2, and (S1), and docs/testdata/x with "
                        "TESTDATA_ARCHIVE pointer\n", encoding="utf-8")
        findings, _, _ = check(root, ["src/good.cpp"])
        if findings:
            failures.append(f"clean file must pass, got {findings}")

        bad_section = root / "src" / "bad_section.cpp"
        bad_section.write_text("// `docs/CONTRACT.md` section 9 -- does not exist\n", encoding="utf-8")
        findings, _, _ = check(root, ["src/bad_section.cpp"])
        if not findings:
            failures.append("a section reference that does not resolve must fail")

        bad_token = root / "src" / "bad_token.cpp"
        bad_token.write_text("// (S99) is not in the registry\n", encoding="utf-8")
        findings, _, _ = check(root, ["src/bad_token.cpp"])
        if not findings:
            failures.append("an unregistered decision number must fail")

        retired_in_header = root / "include" / "x.h"
        retired_in_header.write_text("// (S2) is retired, so a public header may not cite it\n",
                                     encoding="utf-8")
        findings, _, _ = check(root, ["include/x.h"])
        if not findings:
            failures.append("a retired token in include/ must fail")

        # Rule 1's other half (2026-10-27): the same reference written WITHOUT the `docs/` prefix.
        bare_ok = root / "src" / "bare_ok.cpp"
        bare_ok.write_text("// see CONTRACT.md section 2\n", encoding="utf-8")
        findings, _, _ = check(root, ["src/bare_ok.cpp"])
        if findings:
            failures.append(f"a bare reference that resolves must pass, got {findings}")

        bare_bad = root / "src" / "bare_bad.cpp"
        bare_bad.write_text("// see CONTRACT.md section 9\n", encoding="utf-8")
        findings, _, _ = check(root, ["src/bare_bad.cpp"])
        if not findings:
            failures.append("a bare reference to a section that does not exist must fail")

        bare_unknown = root / "src" / "bare_unknown.cpp"
        bare_unknown.write_text("// see SOMEBODY_ELSES.md section 3\n", encoding="utf-8")
        findings, _, _ = check(root, ["src/bare_unknown.cpp"])
        if findings:
            failures.append(f"a bare reference to a file outside this tree must be ignored, got {findings}")

        # The spec's other half: the clean file must stay green after the offenders existed.
        findings, _, _ = check(root, ["src/good.cpp"])
        if findings:
            failures.append(f"clean file must still pass, got {findings}")

    if failures:
        for line in failures:
            print(f"check_doc_refs --self-test FAIL: {line}")
        return 1
    print("check_doc_refs --self-test: PASS (10 controls: 5 green, 5 red)")
    return 0


def main(argv: List[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0] if __doc__ else "")
    parser.add_argument("--root", default=str(ROOT), help="where the tree is read from")
    parser.add_argument("--verbose", action="store_true", help="print what was scanned")
    parser.add_argument("--self-test", action="store_true", help="prove each rule can go red, then exit")
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test()

    root = Path(args.root).resolve()
    files = tracked_text_files(root)
    if len(files) < 10:
        print(f"check_doc_refs: ERROR only {len(files)} file(s) found under {root} -- refusing to pass "
              "on a tree this check could not read")
        return 1

    findings, notes, _ = check(root, files)
    if args.verbose:
        for note in notes:
            print(f"check_doc_refs: {note}")
    if findings:
        for line in findings:
            print(f"check_doc_refs: {line}")
        print(f"check_doc_refs: FAIL ({len(findings)} finding(s))")
        return 1
    print("check_doc_refs: PASS (every section reference resolves, every decision number is "
          "registered, every retired path carries its pointer)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
