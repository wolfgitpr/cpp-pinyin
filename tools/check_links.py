#!/usr/bin/env python3
"""Every relative markdown link must land on a file that exists.

WHY THIS EXISTS. The documentation review produced five gates, and between them they left one form of
cross-reference unchecked: the anchor audit reads backticked ``path:NN`` citations, so a plain
`[text](relative/path)` link was invisible to every instrument. Measured 2026-10-10 on the tracked tree:
**28 relative links in 53 markdown files**, of which two were dead -- `docs/API_en.md` and `docs/API_zh.md`
linked to `./docs/Custom%20Chinese%20Dialect.md`, a path that resolves to `docs/docs/...` because those files
live in `docs/` while the link was written as if they sat at the root. A human found those two by reading a
table; nothing would have found the next one.

WHAT IT CHECKS: every markdown link with a relative target, resolved against the linking file's own
directory, must name an existing path. Schemes (`https:`), absolute (`/...`) and in-page (`#...`) targets are
out of scope -- they are not this repository's to verify.

TWO MEASURED DETAILS IT HAS TO GET RIGHT, both found by running it against the tree before trusting it:
  * percent-encoding -- this repository writes `Custom%20Chinese%20Dialect.md`; without decoding, five links
    reported as missing and the gate would have been noise from its first run.
  * `doc-anchors: historical` -- the same declaration the anchor audit honours. A dated record may describe a
    path as it was; that is a record, not a link to today. A document that declares itself historical is
    skipped, and so is a line carrying a trailing `<!-- anchor-history: reason -->` comment. The marker has
    to be a trailing comment on the link's own line: an exemption that a standalone comment line could grant
    would be granted by accident, which is exactly what happened twice to the anchor audit.

USAGE
    python tools/check_links.py              # exit 1 if any relative link is dead
    python tools/check_links.py --verbose    # list every link and where it resolves
    python tools/check_links.py --self-test  # prove the check can go red, then exit
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import urllib.parse
from pathlib import Path
from typing import List, Optional, Sequence, Tuple

ROOT = Path(__file__).resolve().parent.parent

LINK = re.compile(r"\[(?P<text>[^\]]*)\]\((?P<target>[^)\s]+)(?:\s+\"[^\"]*\")?\)")

#: A scheme-qualified target (`https:`, `mailto:`), a protocol-relative one, or an absolute path is not this
#: repository's to resolve.
EXTERNAL = re.compile(r"^(?:[a-zA-Z][a-zA-Z0-9+.-]*:|//|/)")

MARKER_DOCUMENT = "doc-anchors: historical"
MARKER_LINE = "anchor-history"

SKIP_PARTS = {".git", ".tmp", ".tmp-firefly-disk", "build", "bin", "obj", "__pycache__", "node_modules"}


def tracked_markdown(root: Path) -> List[str]:
    # NO PATHSPEC GLOB: `git ls-files "*.md"` asks git to interpret a pattern, and on the msys2 job that list came
    # back with one file in it while the index held three (measured 2026-10-22, `LinkChecksSelfTest`). Listing
    # everything with `-z` and filtering by suffix here takes the question away from git, and `-z` also removes the
    # quoting rules that make a path with a space arrive as `"docs/with space.md"`.
    completed = subprocess.run(["git", "ls-files", "-z"], cwd=root, capture_output=True)
    if completed.returncode != 0:
        raise SystemExit("ERROR: `git ls-files` failed, refusing to report a clean audit of nothing")
    names = [name for name in completed.stdout.decode("utf-8", "replace").split("\0") if name]
    return [name for name in names
            if name.endswith(".md") and not any(part in SKIP_PARTS for part in Path(name).parts)]


def declares_history(root: Path, name: str) -> bool:
    """A document that declares its line numbers to be movement history is a record, not a set of links."""
    body = (root / name).read_text(encoding="utf-8", errors="replace")
    for line in body.splitlines():
        if MARKER_DOCUMENT in line:
            stripped = line.lstrip()
            if stripped.startswith(">") or stripped.startswith("<!--"):
                return True
    return False


def resolve(linker: str, target: str) -> Path:
    """Resolve a relative link target against the linking file's directory, without touching the filesystem."""
    path_part = urllib.parse.unquote(target.partition("#")[0])
    parts: List[str] = []
    for piece in (Path(linker).parent / path_part).parts:
        if piece == "..":
            if parts:
                parts.pop()
        elif piece != ".":
            parts.append(piece)
    return Path(*parts) if parts else Path(".")


def scan(root: Path) -> List[Tuple[str, int, str, str, bool]]:
    """(file, line, target, resolution, exempt) for every relative link.

    `exempt` is True for a link on a line that carries a trailing `<!-- anchor-history: ... -->` comment and
    for every link in a document that declares itself historical. Exempt rows are RETURNED rather than
    dropped, so the empty-set guard counts what was actually found: dropping them here would make a tree
    consisting only of history look like a tree with no links, and the guard would fire on a healthy tree.
    """
    rows: List[Tuple[str, int, str, str, bool]] = []
    for name in tracked_markdown(root):
        body = (root / name).read_text(encoding="utf-8", errors="replace")
        historical_document = declares_history(root, name)
        for line_no, line in enumerate(body.splitlines(), 1):
            exempt = historical_document or MARKER_LINE in line
            for match in LINK.finditer(line):
                target = match.group("target")
                if EXTERNAL.match(target) or target.startswith("#"):
                    continue
                rows.append((name, line_no, target, resolve(name, target).as_posix(), exempt))
    return rows


def check(root: Path) -> Tuple[bool, List[str]]:
    rows = scan(root)
    if not rows:
        return False, ["ERROR: no relative markdown links found, refusing to pass vacuously"]
    live = [(name, line, target, dest) for name, line, target, dest, exempt in rows if not exempt]
    dead = [(name, line, target, dest) for name, line, target, dest in live if not (root / dest).exists()]
    problems = [f"{name}:{line} -> {target} resolves to {dest}, which does not exist"
                for name, line, target, dest in dead]
    return not problems, problems + [
        f"checked {len(live)} relative markdown link(s) ({len(rows) - len(live)} exempt) "
        f"in {len(tracked_markdown(root))} file(s)"]


def self_test() -> int:
    """Prove the check can go red, and that it refuses to pass on an empty input set."""
    import tempfile

    failures: List[str] = []

    ran = 0

    def expect(label: str, ok: bool, detail: object = "") -> None:
        nonlocal ran
        ran += 1
        print(f"{'PASS' if ok else 'FAIL'}  self-test: {label}" + ("" if ok else f"  -> {detail}"))
        if not ok:
            failures.append(label)

    with tempfile.TemporaryDirectory() as raw:
        root = Path(raw)
        (root / "docs").mkdir()
        (root / "docs" / "with space.md").write_text("# target\n", encoding="utf-8")

        def init() -> None:
            """Stage the fixture, and SAY SO when the staging did not happen.

            The cases below assert verdicts about a tree that `git ls-files` must be able to see. On the msys2 job
            the staging silently produced a listing without `docs/b.md`, so the case that expects a dead link
            reported "the link passes" and named nothing useful (measured 2026-10-22). A fixture that cannot
            prepare itself must fail with the reason, not hand out a verdict about a tree it never built.
            """
            subprocess.run(["git", "init", "-q"], cwd=root, capture_output=True)
            added = subprocess.run(["git", "add", "-A"], cwd=root, capture_output=True, text=True)
            if added.returncode != 0:
                raise SystemExit(f"check_links self-test: `git add -A` failed in the fixture: "
                                 f"{added.stderr.strip() or added.stdout.strip()}")
            listed = subprocess.run(["git", "ls-files"], cwd=root, capture_output=True, text=True)
            if listed.returncode != 0:
                raise SystemExit(f"check_links self-test: `git ls-files` failed in the fixture: "
                                 f"{listed.stderr.strip()}")
            staged = {line for line in listed.stdout.splitlines() if line}
            present = {path.relative_to(root).as_posix()
                       for path in root.rglob("*.md") if ".git" not in path.parts}
            missing = sorted(present - staged)
            if missing:
                raise SystemExit(f"check_links self-test: the fixture has {missing} on disk but not in the "
                                 f"index -- the cases below would judge a tree the checker cannot see")

        (root / "a.md").write_text("see [t](docs/with%20space.md)\n", encoding="utf-8")
        init()
        ok, detail = check(root)
        expect("a percent-encoded link that exists passes", ok, detail)

        (root / "a.md").write_text("see [t](./docs/with%20space.md)\n", encoding="utf-8")
        init()
        ok, detail = check(root)
        expect("`./` prefixed link from the root passes", ok, detail)

        (root / "docs" / "b.md").write_text("see [t](./docs/with%20space.md)\n", encoding="utf-8")
        init()
        ok, detail = check(root)
        expect("the same link written INSIDE docs/ fails (it resolves to docs/docs)", not ok, detail)

        (root / "docs" / "b.md").unlink()
        (root / "a.md").write_text("see [t](../outside.md)\n", encoding="utf-8")
        init()
        ok, detail = check(root)
        expect("a link that escapes the repository fails", not ok, detail)

        (root / "a.md").write_text("see [t](https://example.com/x) and [j](#section)\n", encoding="utf-8")
        init()
        ok, detail = check(root)
        expect("external and in-page targets are refused, so an empty set aborts", not ok, detail)

        (root / "a.md").write_text("see [t](docs/gone.md)\n", encoding="utf-8")
        (root / "h.md").write_text("> `doc-anchors: historical` -- describes the old layout\n"
                                   "see [t](docs/gone.md)\n", encoding="utf-8")
        init()
        ok, detail = check(root)
        expect("a history-declaring document is skipped, the live one is not", not ok, detail)

        (root / "h.md").write_text("a note that MENTIONS `doc-anchors: historical` inline\n"
                                   "see [t](docs/gone.md)\n", encoding="utf-8")
        init()
        ok, detail = check(root)
        expect("merely mentioning the marker does not exempt the document", not ok, detail)

        (root / "a.md").write_text("see [t](docs/gone.md)  <!-- anchor-history: that batch is archived -->\n",
                                   encoding="utf-8")
        (root / "h.md").unlink()
        init()
        ok, detail = check(root)
        expect("a TRAILING anchor-history comment exempts that line", ok, detail)

        (root / "a.md").write_text("<!-- anchor-history: a standalone comment -->\nsee [t](docs/gone.md)\n",
                                   encoding="utf-8")
        init()
        ok, detail = check(root)
        expect("a STANDALONE comment on the line above does NOT exempt the link", not ok, detail)

    print(f"self-test: {ran} case(s), {len(failures)} failure(s)"
          + (": " + "; ".join(failures) if failures else ""))
    if not ran:
        print("self-test: NO CASES RAN -- a self-test that checks nothing must not report success")
        return 1
    return 1 if failures else 0


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--verbose", action="store_true", help="list every link and where it resolves")
    parser.add_argument("--self-test", action="store_true", help="prove the check can go red, then exit")
    options = parser.parse_args(argv)

    if options.self_test:
        return self_test()

    if options.verbose:
        for name, line_no, target, dest, exempt in scan(ROOT):
            state = "skip" if exempt else ("ok  " if (ROOT / dest).exists() else "DEAD")
            print(f"  {state} {name}:{line_no} -> {target}  =>  {dest}")

    ok, messages = check(ROOT)
    for message in messages:
        print(("FAIL  " if not ok and message.startswith(("docs/", "tools/", "README", "python/", "csharp/"))
               else "note  ") + message)
    print("PASS" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
