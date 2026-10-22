"""G9 `check_doc_code_tokens`: the names taught to users in code blocks.

Round 34 measured a hole with no gate behind it: the runbook's mechanical retag does not touch `docs/**` or
`README.md`, so after a generation change the documentation keeps teaching `CPP_PINYIN2_DICT_DIR` -- a name
no generation has -- and G1 never looks there (its token areas are code and build files).

Scope, chosen to avoid flooding:
  * only FENCED code blocks (``` ... ```) inside `*.md` are judged. Those are what a reader copies.
  * prose is never judged: a sentence may legitimately quote a historical name (this repository's rule is
    that history is not rewritten).
  * inside a block, a `CPP_PINYIN_*` name WITHOUT a generation digit is a finding -- the same criterion the
    build-face attacker proposed for build names ("only `CPP_PINYIN_` and no number means a missed rename").

The dependency this exposes: the current documentation DOES teach the bare name, so this gate cannot land
before the rename it argues for. That is measured here rather than assumed.

Two exemption forms, both named rather than loose (this repository's rule: an exemption is named with a
reason, and the pattern is never loosened, because loosening removes protection from real files too):
  * a line marker, for a line that quotes history in prose;
  * a block marker (round 241), for an excerpt that is historical as a WHOLE -- a line marker cannot be used
    inside a fence without printing an HTML comment in a sample a reader copies, and the reason for each such
    exemption is written in a note beside the block.

歧义形态：**源码面** ✗（文档面）—— 读者照抄的围栏代码块里必须写带令牌的名字，否则照抄即错。
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

#: A build name with no generation digit. There is deliberately NO leading-identifier guard: the most common
#: real-world spelling is `-DCPP_PINYIN_DICT_DIR=...`, where the preceding character is the letter `D` of the
#: cmake flag, so both `\b` and `(?<![A-Za-z0-9_])` fail on it. My self-test caught this twice -- the first
#: "fix" replaced `\b` with an equivalent lookbehind and changed nothing. The trailing `\b` stays, so a
#: genuine prefix (`CPP_PINYIN_DICT` inside `CPP_PINYIN_DICT_DIR`) is still not matched on its own.
BARE = re.compile(r"CPP_PINYIN_(?!\d)([A-Z][A-Z0-9_]*)\b")
FENCE = re.compile(r"^\s*(?:```|~~~)")
#: A code block that quotes history on purpose: the repository's rule is that history is not rewritten, so
#: a marked line is exempt. Measured need (round 35): the real docs contain historical names inside fenced
#: blocks (docs/DECISIONS.md, and the layout plan before it was deleted), and without a marker the gate could not be satisfied
#: even after the rename it argues for.
HISTORICAL = re.compile(r"<!--\s*doc-tokens:\s*historical\s*-->")
#: Round 241: the block-level form. It must stand alone on its own line immediately before the fence it
#: exempts, so it can never be mistaken for content inside a sample.
BLOCK_MARKER = re.compile(r"^\s*<!--\s*doc-tokens:\s*historical-block\s*-->\s*$")
#: Round 244 (corrected in the same round): BARE's group(1) is the part AFTER the `CPP_PINYIN_` prefix, so the set must hold
#: the bare tails -- the full spellings below are the ones a reader sees, and the tails are what the scan compares. The first
#: version held the full names, matched nothing, and left the count at 14; the negative self-test case ("a name that merely
#: starts like an exempted one") is what proves the exemption is still narrow.
#: Measured (round 239): the dictionary repo's `tools/check_drift.ps1` takes `-CppPinyinRoot` as a parameter (:18), falls back
#: to `$env:CPP_PINYIN_ROOT` (:30), and reads `$env:CPP_PINYIN_DICT` (:33) -- that script's variables, not this repository's
#: build names, so the "no generation digit" rule does not apply. The block they sit in is the LIVE delivery checklist:
#: exempting the whole block would make it stop noticing a genuine misspelling later.
CROSS_REPO_NAMES = {"ROOT", "DICT"}          # CPP_PINYIN_ROOT / CPP_PINYIN_DICT


def scan_docs(root: Path) -> tuple[list[str], int, int]:
    findings: list[str] = []
    files = 0
    blocks = 0
    for path in sorted(root.rglob("*.md")):
        if any(part in {"build", ".git", ".tmp", "obj", "bin", "node_modules"} for part in path.parts):
            continue
        files += 1
        inside = False
        pending_block_exempt = False
        exempt_block = False
        for number, line in enumerate(path.read_text(encoding="utf-8-sig", errors="replace").splitlines(),
                                       start=1):
            if not inside and BLOCK_MARKER.match(line):
                pending_block_exempt = True
                continue
            if FENCE.match(line):
                inside = not inside
                if inside:
                    blocks += 1
                    exempt_block = pending_block_exempt
                else:
                    exempt_block = False
                pending_block_exempt = False
                continue
            # Round 241: the marker must stand IMMEDIATELY before the fence it exempts. Any other line clears the
            # pending state, so a marker followed by prose cannot leak its exemption onto a later block. The
            # negative self-test case "a block marker followed by prose, then a fence" is what caught the first
            # version keeping the state alive; the docstring had said "immediately" all along.
            pending_block_exempt = False
            if not inside or exempt_block or HISTORICAL.search(line):
                continue
            for match in BARE.finditer(line):
                if match.group(1) in CROSS_REPO_NAMES:
                    continue                      # another repository's script variable; reason above
                rel = path.relative_to(root).as_posix()
                findings.append(f"{rel}:{number}: a code block teaches `CPP_PINYIN_{match.group(1)}` -- a "
                                f"build name with no generation digit is a missed rename (see R5; the rename itself is D23)")
    return findings, files, blocks


def self_test(tmp: Path) -> int:
    def tree(label: str, body: str) -> Path:
        root = tmp / label
        (root / "docs").mkdir(parents=True, exist_ok=True)
        (root / "docs" / "GUIDE.md").write_text(body, encoding="utf-8", newline="")
        return root

    cases = [
        ("a code block teaching the bare name", "```\ncmake -DCPP_PINYIN_DICT_DIR=x\n```\n", True),
        ("the same name in prose only", "The variable CPP_PINYIN_DICT_DIR was introduced in 1.0.\n", False),
        ("a code block teaching the tokenized name", "```\ncmake -DCPP_PINYIN2_DICT_DIR=x\n```\n", False),
        ("a historical name inside a fenced block", "```\n# 1.0 used CPP_PINYIN_DICT_DIR\n```\n", True),
        ("no code block at all", "# Title\n\nSome prose.\n", False),
        # round 241: the block form must exempt the whole block, and only when it stands before the fence
        ("a block exempted as a whole",
         "<!-- doc-tokens: historical-block -->\n```\ncmake -DCPP_PINYIN_DICT_DIR=x\n```\n", False),
        ("the same block without the marker", "```\ncmake -DCPP_PINYIN_DICT_DIR=x\n```\n", True),
        ("a block marker followed by prose, then a fence",
         "<!-- doc-tokens: historical-block -->\nprose\n```\ncmake -DCPP_PINYIN_DICT_DIR=x\n```\n", True),
        ("a block marker after the closing fence must not reach the next block",
         "```\ncmake -DCPP_PINYIN2_DICT_DIR=x\n```\n<!-- doc-tokens: historical-block -->\nprose\n", False),
        # round 244: the cross-repo names are named exemptions, and the exemption must not be wider than the two names
        ("the dictionary repo's script variable",
         "```\npowershell -File tools/check_drift.ps1   # needs CPP_PINYIN_ROOT\n```\n", False),
        ("a neighbouring name must still be red", "```\ncmake -DCPP_PINYIN_DICT_DIR=x\n```\n", True),
        ("a name that merely starts like an exempted one", "```\ncmake -DCPP_PINYIN_ROOT_DIR=x\n```\n", True),
    ]
    failures = 0
    for label, body, expect_red in cases:
        findings, _files, _blocks = scan_docs(tree(label.replace(" ", "-"), body))
        red = bool(findings)
        ok = red == expect_red
        suffix = "" if ok else f" (expected {'red' if expect_red else 'green'})"
        print(f"  {'PASS' if ok else 'FAIL'} self-test: {label} -> {'red' if red else 'green'}{suffix}")
        failures += 0 if ok else 1
    return failures, len(cases)


def main() -> int:
    parser = argparse.ArgumentParser(description="generation tokens taught in documentation code blocks")
    # parents[1], NOT parents[2]: this file lives in `<repo>/tools/`, so parents[2] is the DIRECTORY ABOVE the
    # repository. Measured on the firefly board (2026-10-21): the default root was /tmp, the gate scanned whatever
    # else happened to sit there and reported ANOTHER project's documents as this repository's findings -- while on
    # a parent directory that holds only this repo makes the wrong root look right. The same off-by-one was
    # already fixed in check_generation_compat.py, whose comment records it (audit round 2, 2026-10-20).
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        import tempfile
        failures, case_count = self_test(Path(tempfile.mkdtemp(prefix="g9-selftest-")))
        print(f"\nSELFTEST {'FAIL' if failures else 'PASS'} -- {case_count} case(s) reported above")
        return 1 if failures else 0
    findings, files, blocks = scan_docs(args.root)
    for finding in findings:
        print("FAIL: " + finding)
    print(f"check_doc_code_tokens: {'FAIL' if findings else 'PASS'} ({len(findings)} finding(s) in "
          f"{files} markdown file(s), {blocks} code block(s))")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
