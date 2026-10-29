#!/usr/bin/env python3
"""Audit `path:NN` anchors in the docs: does the referenced file exist, and does that line exist?

`path:NN` is partly checkable, and this tool checks exactly that part. What it deliberately does NOT do is
judge whether the cited line means what the sentence claims -- mechanising that produced 465 "broken"
anchors out of 784, almost all of them sentences that cite several files at once. A rule that a line must
mention every file its sentence names is wrong, and a 59%-broken report gets ignored, which is worse than
no report. So: existence only, and the wording says so.

References are classified rather than lumped together:

  * `missing-file` / `past-eof` -- inside this repository, and actually broken;
  * `historical-doc-anchors` -- the whole document is opted out, see MARKER_HISTORICAL below: its header
    says its line numbers describe a tree that has moved;
  * `external` -- the path belongs to another checkout (`pinyin-makedict/...`), or is a bare filename with
    no directory (`TOOLING.md`), which this repository may not contain and cannot adjudicate.
"""

import argparse
import re
import subprocess
import os
import sys
import tempfile
from pathlib import Path

ANCHOR_RX = re.compile(
    r"(?<![\w./-])((?:[\w.-]+/)*[\w.-]+\.(?:md|txt|json|cpp|h|c|cmake|py|sh|yml|yaml)):(\d+)(?:-(\d+))?")

# Names that belong to other checkouts or to files this repository does not ship. Matched on the first
# path component.
EXTERNAL_ROOTS = ("pinyin-makedict",)


def classify(target_rel):
    parts = target_rel.split("/")
    if parts[0] in EXTERNAL_ROOTS:
        return "external"
    if len(parts) == 1:
        # A bare filename: no directory to resolve against, so this could be another project's file. Do
        # not call it missing; report it as unadjudicated.
        return "external"
    return "in-repo"


def tracked_by_basename(root):
    """basename -> tracked path, for reporting anchors whose file was moved by a refactor.

    Measured on this tree: the P2 split moved `src/DictUtil.cpp` to `src/dict/`, `include/cpp-pinyin2/
    Types.h` to `core/` and `tests/catch2/*` to `tests/cpp/`. A doc that cites the old path is not citing
    a missing file, it is citing a file that MOVED, and saying so turns an unhelpful "missing-file" into
    an actionable "now at ...". The listing comes from the git index rather than a filesystem walk so that
    build trees and installed prefixes cannot be mistaken for the source location.
    """
    try:
        out = subprocess.run(["git", "ls-files"], cwd=root, capture_output=True, text=True)
    except FileNotFoundError:
        # No git on PATH (measured: the `mingw` job, 2026-10-22, where the MSYS2 environment has no git).
        # Losing this table only costs the "this file moved to ..." hint; every anchor is still adjudicated,
        # so a missing git must not turn the gate into a crash.
        return {}
    if out.returncode != 0:
        return {}
    table = {}
    for line in out.stdout.split("\n"):
        line = line.strip()
        if line:
            table.setdefault(Path(line).name, line)
    return table


def default_sibling() -> str:
    r"""Where the dictionary repository probably is, without naming anyone's absolute path.

    Order: the environment variable (CI and the board set it), then the three layouts this project is actually
    checked out in around here (a plain sibling, one level up, or `<parent>/<parent>/python/<name>`). An empty
    answer is not a failure: anchors that need the sibling then classify as `external`, which is what they are when
    the other repository is not on this machine -- and it beats a hardcoded `D:\...` that only works for its author.
    """
    env = os.environ.get("CPP_PINYIN_DICT_ROOT")
    if env:
        return env
    root = Path(__file__).resolve().parent.parent
    for candidate in (
        root.parent / "pinyin-makedict",
        root.parent.parent / "pinyin-makedict",
        root.parent.parent / "python" / "pinyin-makedict",
    ):
        if candidate.is_dir():
            return str(candidate)
    return ""


def sibling_by_basename(sibling):
    """basename -> path inside the sibling checkout, if that checkout is present.

    Reason this exists: four anchors in these docs cite files that live in the dictionary repository
    (`tools/run_versions.py`, `tools/compare_dict_dirs.py`, `data/overrides/man_overrides.py`,
    `binary/FORMAT.md`). Calling those "missing" was wrong -- this repository deliberately does not keep
    dialect/tooling records, so the cross-repo reference is the correct thing for the doc to do.
    """
    if sibling is None or not Path(sibling).is_dir():
        return {}
    table = {}
    for path in Path(sibling).rglob("*"):
        if path.is_file() and ".git" not in path.parts and "out" not in path.parts:
            table.setdefault(path.name, str(path))
    return table


SKIP_DIRS = {".git", ".tmp", ".venv", "venv", "build", "out", "dict", "__pycache__", "node_modules"}

# Paths the repository RETIRED as a whole, with the replacement a reader should follow. A citation of one of
# these is not "somebody forgot to update a link": the directory is gone by a recorded decision, and the
# documents already name the replacement. Kept out of the `broken` count for that reason -- while still being
# listed, so the disposition is visible instead of assumed. Distinct from the two markers below, which need
# per-document or per-line opt-in; this one is a property of the retired path itself.
RETIRED_PATHS = {
    # 2026-10-20: a `docs/plans/` entry was REMOVED from this table, not renamed: the directory was not retired
    # at the time, and matching it here silently absorbed every anchor into it. On 2026-10-22 the directory
    # really was retired, together with `docs/history/`, and neither needs an entry any more -- no file lives
    # there to cite.
    # The dictionary repository's binary line (`binary/`, and the two tools that built it) was deleted on
    # 2026-10-18 -- its `TOOLING.md` section 13 records where each piece went. Four ledgers in THIS repository
    # cite those files as the place a number was measured ("`binary/STATUS.md:131` already records this"),  [anchor-history: the dictionary repo retired that file on 2026-10-18, this line is its obituary]
    # which is a record of provenance, not a pointer into today's tree. Same character as the working-ledger
    # directory above: the path is gone by a recorded decision, and the citing sentence is about that path --
    # rewriting it would erase where the measurement came from.
    "binary/": "the dictionary repository's binary line was deleted on 2026-10-18; its TOOLING.md section 13 "
               "records where each piece went",
    "tools/check_index.py": "deleted with the dictionary repository's binary line on 2026-10-18 "
                            "(TOOLING.md section 13)",
    "tools/check_binary.py": "deleted with the dictionary repository's binary line on 2026-10-18 "
                             "(TOOLING.md section 13)",
}

# A document that declares its own anchors to be movement-history, not claims about today's tree.
# `docs/DECISIONS.md` and `docs/DECISIONS.md` §2.1 both say so in their header, and the
# repository's rule is that such records are NOT rewritten when a directory moves. Measured 2026-10-09,
# those two documents carried 37 of the 56 in-tracked broken anchors, so a flat "391 broken" mixed
# "nobody should touch this" with "somebody should look at this" -- the failure mode the doc-claims gate
# already solved with its own `doc-claims: historical` marker. Same idea, different question.

MARKER_HISTORICAL = "doc-anchors: historical"

# A LINE-level marker, for the case the document-level one cannot express: a live document that writes one
# sentence ABOUT an anchor that is now dead ("the台账 this was written against has since been deleted",
# "that path was observed at the time", "this was where the header lived before it moved"). Without a
# marker those sentences are indistinguishable from a stale citation, and there were 16 of them: the
# document-level exemption cannot be used because the rest of such a document IS about today's tree.
#
# Writing the marker is the point: the alternative -- "just don't write the old path" -- is not always
# available, because the whole sentence is about that path. Fixed 2026-10-10 after the same note I wrote
# about this defect created two new ones (a backticked `path:NN` inside a sentence explaining that the
# anchor was dead IS an anchor).
MARKER_LINE_HISTORY = "anchor-history"


def declare_marker_line(body: str):
    """The 1-based line that DECLARES the document-level marker, or None.

    A declaration is a marker on its own line, introduced as a blockquote (`> ...`) or an HTML comment --
    which is how all three documents in this repository write it. A marker merely MENTIONED inside prose or
    a table cell is not a declaration: measured twice, both times a note ABOUT the exemption silently took a
    whole document out of adjudication (`docs/BACKLOG.md` lost 61 anchors that way). Printing where the
    marker sits made the first fix visible; requiring the declaration form removes the footgun.
    """
    for number, line in enumerate(body.splitlines(), 1):
        if MARKER_HISTORICAL not in line:
            continue
        stripped = line.lstrip()
        if stripped.startswith(">") or stripped.startswith("<!--"):
            return number
    return None


#: The file kinds whose COMMENTS also carry `path:NN` anchors (2026-10-20). Until now the
#: gate only read Markdown under `docs/`, so a `path:NN` in a C++ or Python comment was nobody's business --
#: measured before wiring: 18 such anchors existed, 4 of them already wrong. These are the extensions the
#: tree actually uses; a new source language has to be added here, which is the point of a written list.
CODE_GLOBS = (
    "src/**/*.cpp",
    "include/**/*.h",
    "src/**/*.h",
    "tools/**/*.py",
    "tools/**/*.cpp",
    "tests/**/*.cpp",
    "python/**/*.py",
    "csharp/**/*.cs",
    ".github/**/*.yml",
    "examples/**/*.cpp",
)


def audit(root, sibling=None, docs_glob="docs/**/*.md", adjudicate_bare=False):
    """Scan `docs_glob` under root for `path:NN` anchors.

    2026-10-04: the glob became an argument so the SAME implementation can audit pinyin-makedict, which
    keeps its line-number anchors in README.md and binary/*.md rather than under docs/ -- writing a
    second auditor there would have added the very duplication this sweep exists to remove. The default
    is unchanged, so this repository's numbers are unaffected.

    2026-10-09: a document whose header carries MARKER_HISTORICAL is reported as one
    `historical-doc-anchors` record instead of having every anchor adjudicated: its lines describe a tree
    that no longer exists, by policy, and counting them as breakage makes the signal unreadable.
    """
    records = []
    moved = tracked_by_basename(root)
    elsewhere = sibling_by_basename(sibling)
    for path in sorted(root.glob(docs_glob)):
        if any(part in SKIP_DIRS for part in path.relative_to(root).parts):
            continue
        rel_doc = path.relative_to(root).as_posix()
        body = path.read_text(encoding="utf-8", errors="replace")
        declared_line = declare_marker_line(body)
        if declared_line:
            skipped = len(ANCHOR_RX.findall(body))
            # Only a DECLARATION counts (blockquote or HTML comment, see `declare_marker_line`), and where it
            # sits is still printed: a declaration far from the header is worth a reviewer's eye even though
            # it is honoured. Measured twice that a mere MENTION used to exempt a whole document -- the
            # footgun this form removes.
            where = declared_line
            hint = "" if where <= 40 else f" -- NOTE: the declaration sits at line {where}, not in the header"
            records.append((rel_doc, 0, "-", 0, "historical-doc-anchors",
                            f"{skipped} anchors not adjudicated: the document declares its line numbers "
                            f"to be movement-history ({MARKER_HISTORICAL}) at line {where}{hint}"))
            continue
        for line_no, line in enumerate(body.splitlines(), 1):
            for m in ANCHOR_RX.finditer(line):
                target_rel, first = m.group(1), int(m.group(2))
                if MARKER_LINE_HISTORY in line:
                    records.append((rel_doc, line_no, target_rel, first, "line-history", ""))
                    continue
                kind = classify(target_rel)
                if adjudicate_bare and kind == "external" and "/" not in target_rel:
                    # --adjudicate-bare-names (opt-in): a bare filename is usually a cross-repo reference,
                    # but the dictionary repository cites its OWN scripts that way (README.md writes
                    # `cantonese.py:126`, `FORMAT.md:193`). Resolve it against this repository's tracked
                    # files and adjudicate the line like any other anchor. Off by default on purpose:
                    # turning it on moves anchors out of the "external" bucket, and the counts then belong to
                    # whichever repository is under audit -- 477 resolve / 69 broken is the DICTIONARY
                    # repository's frozen baseline, while THIS repository's is 293 resolve / 0 broken. The two
                    # numbers were once confused in this comment, which is why both are named here.
                    resolved = moved.get(target_rel)
                    if resolved:
                        target_rel = resolved
                        kind = "bare-name"
                    elif elsewhere.get(target_rel):
                        # the other half of the same case: the dictionary repo cites THIS repo's sources by
                        # bare name (binary/FORMAT.md -> `DictUtil.cpp:115`), which is a legitimate
                        # cross-repo reference rather than a missing file.
                        records.append((rel_doc, line_no, target_rel, first, "present-elsewhere",
                                        f"a file with that name lives in the sibling repository at "
                                        f"{elsewhere[target_rel]}"))
                        continue
                if kind == "external":
                    records.append((rel_doc, line_no, target_rel, first, "external", ""))
                    continue
                retired = next((prefix for prefix in RETIRED_PATHS if target_rel.startswith(prefix)), None)
                if retired:
                    records.append((rel_doc, line_no, target_rel, first, "retired-path", RETIRED_PATHS[retired]))
                    continue
                target = root / target_rel
                if not target.is_file():
                    now_at = moved.get(Path(target_rel).name)
                    if now_at:
                        note = f"a tracked file with that name is now at {now_at}"
                        records.append((rel_doc, line_no, target_rel, first, "missing-file", note))
                        continue
                    in_sibling = elsewhere.get(Path(target_rel).name)
                    if in_sibling:
                        note = f"this file lives in the sibling repository at {in_sibling}"
                        records.append((rel_doc, line_no, target_rel, first, "present-elsewhere", note))
                        continue
                    records.append((rel_doc, line_no, target_rel, first, "missing-file",
                                    "neither this repository nor the sibling has a file with that name"))
                    continue
                total = len(target.read_text(encoding="utf-8", errors="replace").splitlines())
                if first > total:
                    records.append((rel_doc, line_no, target_rel, first, "past-eof",
                                    f"the file has {total} lines"))
                    continue
                records.append((rel_doc, line_no, target_rel, first, "resolves", ""))
    return records



#: Symbol anchors: `` `path::literal` `` -- the claim is "that file contains this literal". Added 2026-10-20
#:  after measuring that 16 of 17 line-number anchors in ONE table pointed at unrelated
#: code while this gate reported every anchor resolving: existence is checkable, meaning is not, and a name
#: survives the refactors that move lines. Kept as a SEPARATE rule (own records, own line in the summary)
#: rather than folded into ANCHOR_RX, so the frozen resolve/broken counts this tool has been quoted by do not
#: move while a defect is being fixed.
SYMBOL_RX = re.compile(
    r"(?<![\w./-])((?:[\w.-]+/)*[\w.-]+\.(?:md|txt|json|cpp|h|c|cmake|py|sh|yml|yaml))::([^\s`]+)")


def audit_symbols(root, docs_glob="docs/**/*.md"):
    """Resolve `` `path::literal` `` anchors: the file must exist and contain that literal."""
    records = []
    moved = tracked_by_basename(root)
    for path in sorted(root.glob(docs_glob)):
        if any(part in SKIP_DIRS for part in path.relative_to(root).parts):
            continue
        rel_doc = path.relative_to(root).as_posix()
        body = path.read_text(encoding="utf-8", errors="replace")
        if declare_marker_line(body):
            continue
        for line_no, line in enumerate(body.splitlines(), 1):
            for match in SYMBOL_RX.finditer(line):
                target_rel, literal = match.group(1), match.group(2)
                kind = classify(target_rel)
                if kind == "external":
                    records.append((rel_doc, line_no, target_rel, literal, "symbol-external", ""))
                    continue
                retired = next((prefix for prefix in RETIRED_PATHS if target_rel.startswith(prefix)), None)
                if retired:
                    records.append((rel_doc, line_no, target_rel, literal, "symbol-retired",
                                    RETIRED_PATHS[retired]))
                    continue
                target = root / target_rel
                if not target.is_file():
                    now_at = moved.get(Path(target_rel).name)
                    note = f"the file is missing{'; a tracked file with that name is now at ' + now_at if now_at else ''}"
                    records.append((rel_doc, line_no, target_rel, literal, "symbol-missing", note))
                    continue
                if literal in target.read_text(encoding="utf-8", errors="replace"):
                    records.append((rel_doc, line_no, target_rel, literal, "symbol-resolves", ""))
                else:
                    records.append((rel_doc, line_no, target_rel, literal, "symbol-absent",
                                    "the file exists but does not contain that literal"))
    return records


def self_check_counts(record_count, counts, resolving, broken):
    """Does the summary line describe the records it printed? A summary that disagrees with its own detail
    is worse than no summary.

    This closes a question that stayed open across several rounds: twice I read a `broken` number that did not
    match the detail lines, and both times it was a TRUNCATION I introduced while filtering output -- the
    summary line is 124 characters and the tail of it got cut. The tool itself was consistent the whole time
    (measured 2026-10-10: 446 detail records, 446 counted, resolve/broken agree). Kept as a check rather than
    a note, because "the summary lies" is exactly the kind of thing that should not need re-deriving.
    """
    problems = []
    if sum(counts.values()) != record_count:
        problems.append(f"the summary counts {sum(counts.values())} anchors but {record_count} records "
                        f"were printed")
    stated_resolving = sum(counts.get(key, 0) for key in
                           ("resolves", "self-doc", "present-elsewhere", "line-history", "retired-path"))
    stated_broken = counts.get("missing-file", 0) + counts.get("past-eof", 0)
    if (stated_resolving, stated_broken) != (resolving, broken):
        problems.append(f"the adjudicated line says {resolving} resolve / {broken} broken, but the status "
                        f"counts say {stated_resolving} / {stated_broken}")
    known = {"resolves", "self-doc", "present-elsewhere", "line-history", "retired-path",
             "missing-file", "past-eof", "external", "historical-doc-anchors"}
    unknown = set(counts) - known
    if unknown:
        problems.append(f"unknown status name(s) in the counts: {sorted(unknown)}")
    return problems


def self_test_counts() -> int:
    """Every case this gate's rules depend on, planted and checked.

    Why it is worth having: the two rules here have each been got wrong once, and both times the wrong version
    looked right.
      * the marker must be DECLARED (blockquote or HTML comment) -- a mention inside prose or a table cell
        silently exempted a whole document, measured in `docs/BACKLOG.md`, 61 anchors;
      * a TRAILING marker comment applies to its own line only; treating it as a declaration takes the whole
        document out of adjudication, which is the same failure wearing a different hat.
    The cases below plant both mistakes and the shapes that must stay legal.
    """
    marker = MARKER_HISTORICAL
    cases = [
        ("a blockquote declaration is found",         declare_marker_line(f"> {marker}: the paths below are historical.\n") == 1),
        ("an HTML-comment declaration is found",
         declare_marker_line(f"<!-- {marker} -->\ntext\n") == 1),
        ("a declaration on a later line reports that line",
         declare_marker_line(f"title\n\n> {marker}\n") == 3),
        ("a mention inside prose is NOT a declaration",
         declare_marker_line(f"see the {marker} convention for details\n") is None),
        ("a mention inside a table cell is NOT a declaration",
         declare_marker_line(f"| note | uses {marker} here |\n") is None),
        ("a mention indented but not quoted is NOT a declaration",
         declare_marker_line(f"    {marker} in a code block\n") is None),
        ("no marker at all means no declaration",
         declare_marker_line("ordinary text\n") is None),
        ("the marker in an HTML comment must be at the line start after strip",
         declare_marker_line(f"text <!-- {marker} -->\n") is None),
    ]
    # symbol anchors (`path::literal`), added 2026-10-20 (H19): one literal that IS in the file and one that
    # is not. The second is the whole point -- a name that drifts out of the file must turn this red.
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / "docs").mkdir()
        (root / "src").mkdir()
        (root / "src" / "thing.txt").write_text("alpha\nbeta\n", encoding="utf-8")
        (root / "docs" / "note.md").write_text(
            "`src/thing.txt::alpha` and `src/thing.txt::gamma`\n", encoding="utf-8")
        seen = {(record[2], record[3]): record[4] for record in audit_symbols(root)}
        cases.append(("a symbol anchor that is present resolves",
                      seen.get(("src/thing.txt", "alpha")) == "symbol-resolves"))
        cases.append(("a symbol anchor whose literal is gone is broken",
                      seen.get(("src/thing.txt", "gamma")) == "symbol-absent"))

    failures = []
    for label, ok in cases:
        print(f"self-test: {label} -> {'ok' if ok else 'FAILED'}")
        if not ok:
            failures.append(label)
    print(f"SELF-TEST: {len(cases)} case(s), {len(failures)} failure(s)"
          + (": " + "; ".join(failures) if failures else ""))
    if not cases:
        print("SELF-TEST: NO CASES RAN -- a self-test that checks nothing must not report success")
        return 1
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=".")
    parser.add_argument("--limit", type=int, default=60)
    parser.add_argument("--sibling", default=default_sibling(),
                        help="the dictionary repository, for anchors that cite its files")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--self-test", action="store_true",
                        help="check that this gate's own rules hold on planted cases, then exit")
    parser.add_argument("--include-external", action="store_true",
                        help="also list the anchors that cannot be adjudicated from this repository")
    parser.add_argument("--adjudicate-bare-names", action="store_true",
                        help="opt-in: resolve a bare filename anchor (no directory) against this "
                             "repository's tracked files and check its line, instead of reporting it as "
                             "external. Needed to audit the dictionary repo, whose docs cite their own "
                             "scripts by bare name. Default OFF: enabling it changes the classification "
                             "counts. Whose counts: the dictionary repository's frozen baseline is "
                             "477 resolve / 69 broken; THIS repository's is 293 resolve / 0 broken")
    parser.add_argument("--include-code", action="store_true",
                        help="also adjudicate the anchors in source COMMENTS (CODE_GLOBS), not just the "
                             "Markdown under docs/. The gate passes this; by hand it is optional so a "
                             "reader can still ask 'what do the docs alone say'")
    parser.add_argument("--docs-glob", default="docs/**/*.md",
                        help="documents to scan, relative to --root. The default keeps this repository's "
                             "numbers; the dictionary repo passes '**/*.md' because its anchors live in "
                             "README.md and binary/*.md rather than under docs/")
    args = parser.parse_args()

    if args.self_test:
        return self_test_counts()

    root = Path(args.root).resolve()
    if args.docs_glob.startswith("docs") and not (root / "docs").is_dir():
        print(f"ERROR: no docs/ under {root}", file=sys.stderr)
        return 2
    if not any(root.glob(args.docs_glob)):
        print(f"ERROR: --docs-glob {args.docs_glob!r} matched no document under {root} "
              f"(refusing to report a clean audit of nothing)", file=sys.stderr)
        return 2

    records = audit(root, args.sibling, args.docs_glob, args.adjudicate_bare_names)
    if args.include_code:
        # One pass per glob rather than one merged glob: `Path.glob` takes a single pattern, and the
        # per-glob call also keeps the "an empty scan is an error" rule meaningful for each kind.
        scanned = 1
        for glob in CODE_GLOBS:
            if not any(root.glob(glob)):
                continue
            records = records + audit(root, args.sibling, glob, args.adjudicate_bare_names)
            scanned += 1
        print(f"also adjudicated the anchors in source comments ({scanned - 1} of {len(CODE_GLOBS)} "
              f"code glob(s) matched something under {root})")
    symbol_records = audit_symbols(root, args.docs_glob)
    symbol_bad = [r for r in symbol_records if r[4] in ("symbol-absent", "symbol-missing")]
    counts = {}
    for *_, status, _note in records:
        counts[status] = counts.get(status, 0) + 1
    broken = [r for r in records if r[4] in ("missing-file", "past-eof")]
    elsewhere = [r for r in records if r[4] == "present-elsewhere"]

    if args.verbose:
        for rel_doc, line_no, target_rel, first, status, note in records:
            print(f"  {status:<13} {rel_doc}:{line_no} -> {target_rel}:{first} {note}")

    print("anchors by status: " + ", ".join(f"{k}={v}" for k, v in sorted(counts.items())))
    resolving = (counts.get("resolves", 0) + counts.get("self-doc", 0)
                 + counts.get("present-elsewhere", 0) + counts.get("line-history", 0)
                 + counts.get("retired-path", 0))
    print(f"adjudicated in this repository: {resolving} resolve, {len(broken)} broken")
    print(f"symbol anchors (`path::literal`): "
          f"{sum(1 for r in symbol_records if r[4] == 'symbol-resolves')} resolve, {len(symbol_bad)} broken")
    count_problems = self_check_counts(len(records), counts, resolving, len(broken))
    for problem in count_problems:
        print(f"  INCONSISTENT SUMMARY: {problem}")
    print()

    if counts.get("retired-path"):
        print("refused by policy (the cited path was retired as a whole, and the documents name its successor):")
        for rel_doc, line_no, target_rel, first, status, note in records:
            if status == "retired-path":
                print(f"  {rel_doc}:{line_no} -> {target_rel}:{first}  ({note})")
        print("  These are not stale links: the directory is gone by a recorded decision. Counted separately")
        print("  so a reader can still see every one of them.")
        print()

    if counts.get("line-history"):
        print("not adjudicated by policy (individual lines that write ABOUT a since-dead anchor):")
        for rel_doc, line_no, target_rel, first, status, _note in records:
            if status == "line-history":
                print(f"  {rel_doc}:{line_no} -> {target_rel}:{first}")
        # The marker is line-scoped while anchors are inline, so a line that cites a dead path AND a live one
        # hides the live one too. The over-reach is measured by re-reading just those lines (bounded: it is
        # the number of marked lines) and classifying the anchors the marker swallowed. Printed rather than
        # claimed away, and measured 2026-10-10 at 10 of 35: the reviewer can see the size.
        swallowed_live, swallowed_dead = [], []
        for rel_doc, line_no, _target, _first, _status, _note in records:
            if _status != "line-history":
                continue
            text = (root / rel_doc).read_text(encoding="utf-8", errors="replace").splitlines()
            if line_no - 1 >= len(text):
                continue
            for match in ANCHOR_RX.finditer(text[line_no - 1]):
                target_rel = match.group(1)
                if classify(target_rel) != "in-repo":
                    continue
                target = root / target_rel
                if target.is_file() and int(match.group(2)) <= len(
                        target.read_text(encoding="utf-8", errors="replace").splitlines()):
                    swallowed_live.append(f"{rel_doc}:{line_no} -> {match.group(0)}")
                elif not target.is_file():
                    swallowed_dead.append(f"{rel_doc}:{line_no} -> {match.group(0)}")
        if swallowed_live:
            print(f"  NOTE: {len(swallowed_live)} anchor(s) on MARKED lines would still resolve, so the "
                  f"marker covers them too.")
            print("        Keep the marker off lines whose other anchors are live claims. They are:")
            for entry in swallowed_live:
                print(f"          {entry}")
        print(f"  {len(swallowed_dead)} more anchor(s) on the same lines point at a file that is gone, "
              f"which is what the marker states.")
        print(f"  These carry `{MARKER_LINE_HISTORY}`, the line-level marker: the sentence is about a path "
              f"that no longer")
        print("  exists (a deleted ledger, a moved header, a since-renamed file). Use it instead of writing "
              "the old")
        print("  path in a sentence that would otherwise read as a current citation.")
        print()

    if counts.get("historical-doc-anchors"):
        print("not adjudicated by policy (documents that declare their own line numbers to be history):")
        for rel_doc, _line_no, _target, _first, status, note in records:
            if status == "historical-doc-anchors":
                print(f"  {rel_doc}: {note}")
        print("  These are not missing evidence: the repository's rule is that dated records are NOT")
        print(f"  rewritten when a directory moves. Add `{MARKER_HISTORICAL}` to a document's header to opt")
        print("  a document in; the marker says 'do not rewrite this', not 'this is unimportant'.")
        print()

    if args.include_external and counts.get("external"):
        print("not adjudicated (another checkout, or a bare filename with no directory):")
        for rel_doc, line_no, target_rel, first, status, _ in records:
            if status == "external":
                print(f"  external     {rel_doc}:{line_no} -> {target_rel}:{first}")
        print()

    if symbol_bad:
        for rel_doc, line_no, target_rel, literal, status, note in symbol_bad[:args.limit]:
            print(f"  BROKEN [{status}] {rel_doc}:{line_no} -> {target_rel}::{literal}")
            if note:
                print(f"         {note}")
        print()

    if broken and (not args.sibling or not Path(args.sibling).is_dir()):
        # A red run must state its own preconditions: most anchors that fail here point into the dictionary
        # repository, which cannot be consulted when it is not checked out (see --sibling).
        print("NOTE: the dictionary repository is not checked out (no --sibling path resolved here);")
        print(f"      {len(broken)} anchor(s) do not resolve here, and the cross-repository ones cannot be judged")
        print(f"      without it -- point --sibling or CPP_PINYIN_DICT_ROOT at that checkout before editing docs.")
        print()

    if broken or symbol_bad:
        for rel_doc, line_no, target_rel, first, status, note in broken[:args.limit]:
            print(f"  BROKEN [{status}] {rel_doc}:{line_no} -> {target_rel}:{first}")
            if note:
                print(f"         {note}")
        print()
        print("Fix the number, or replace the anchor with a symbol (a name that still exists). Line numbers")
        print("rot; names do not. Do NOT read this exit code as a verdict on whether the cited line says")
        print("what the sentence claims -- it only says the file and the line are there.")
        return 1

    print("RESULT: every in-repository line-number anchor resolves to an existing line")
    print("        (existence only; the meaning of the cited line is not checked, and must not be quoted")
    print("         as if it were)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
