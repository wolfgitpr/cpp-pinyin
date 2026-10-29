"""源文件清单与树一致。

`CMakeLists.txt` used to let `file(GLOB_RECURSE ...)` decide what gets compiled. The problem is not
style: a new file joins the library **without review**, and a deleted header **keeps** building from a
stale copy until the next reconfigure -- and for a library whose contract is a frozen public surface, the
file set is part of the contract.

The list now lives in `CMakeLists.txt` as `CPP_PINYIN2_SOURCES` / `CPP_PINYIN2_HEADERS`, and this script
makes "list == tree" true inside the gate: a new file left unregistered, or an entry for a deleted file,
goes red.

USAGE
    python tools/check_source_list.py              # 退出 1 表示清单与树不一致
    python tools/check_source_list.py --self-test  # 证明两个方向都能红
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path
from typing import List, Sequence, Tuple

ROOT = Path(__file__).resolve().parents[1]
CMAKE = "CMakeLists.txt"


def listed(cmake_text: str) -> Tuple[List[str], List[str]]:
    """(sources, headers) out of the two `set(...)` blocks, in the order written."""
    def block(name: str) -> List[str]:
        match = re.search(r"set\(" + name + r"\n(.*?)\)\n", cmake_text, flags=re.S)
        if not match:
            return []
        return [line.strip() for line in match.group(1).splitlines() if line.strip()]

    return block("CPP_PINYIN2_SOURCES"), block("CPP_PINYIN2_HEADERS")


def tree(root: Path) -> Tuple[List[str], List[str]]:
    sources = sorted(p.relative_to(root).as_posix() for p in (root / "src").rglob("*.cpp"))
    headers = sorted(p.relative_to(root).as_posix()
                     for p in list((root / "include").rglob("*.h")) + list((root / "src").rglob("*.h")))
    return sources, headers


def compare(root: Path) -> List[str]:
    cmake = root / CMAKE
    if not cmake.is_file():
        return [f"{CMAKE} is missing, so the source list cannot be read"]
    got_sources, got_headers = listed(cmake.read_text(encoding="utf-8"))
    if not got_sources or not got_headers:
        # A vacuous comparison: an empty block would "match" an empty tree and report success.
        return [f"{CMAKE}: no CPP_PINYIN2_SOURCES / CPP_PINYIN2_HEADERS block found -- refusing to compare "
                f"nothing against nothing"]
    want_sources, want_headers = tree(root)
    problems: List[str] = []
    for label, got, want in (("CPP_PINYIN2_SOURCES", got_sources, want_sources),
                             ("CPP_PINYIN2_HEADERS", got_headers, want_headers)):
        for missing in sorted(set(want) - set(got)):
            problems.append(f"{label}: `{missing}` is in the tree but not in {CMAKE} -- the build would not "
                            f"compile it, and a gate that cannot see a file cannot check it")
        for ghost in sorted(set(got) - set(want)):
            problems.append(f"{label}: `{ghost}` is listed in {CMAKE} but not in the tree -- a rename left "
                            f"the list behind")
    return problems


def self_test() -> int:
    cases: List[Tuple[str, bool]] = []
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / "src" / "dict").mkdir(parents=True)
        (root / "include" / "cpp-pinyin2" / "core").mkdir(parents=True)
        (root / "src" / "A.cpp").write_text("int a();\n", encoding="utf-8")
        (root / "src" / "dict" / "B.cpp").write_text("int b();\n", encoding="utf-8")
        (root / "src" / "A.h").write_text("#pragma once\n", encoding="utf-8")
        (root / "include" / "cpp-pinyin2" / "core" / "C.h").write_text("#pragma once\n", encoding="utf-8")

        def write(sources: Sequence[str], headers: Sequence[str]) -> None:
            body = ("set(CPP_PINYIN2_SOURCES\n" + "".join(f"    {s}\n" for s in sources) + ")\n"
                    "set(CPP_PINYIN2_HEADERS\n" + "".join(f"    {h}\n" for h in headers) + ")\n")
            (root / CMAKE).write_text(body, encoding="utf-8")

        write(["src/A.cpp", "src/dict/B.cpp"], ["src/A.h", "include/cpp-pinyin2/core/C.h"])
        cases.append(("a matching list passes", not compare(root)))

        write(["src/A.cpp"], ["src/A.h", "include/cpp-pinyin2/core/C.h"])
        cases.append(("a source in the tree but not in the list fails", bool(compare(root))))

        write(["src/A.cpp", "src/dict/B.cpp"], ["src/A.h"])
        cases.append(("a header in the tree but not in the list fails", bool(compare(root))))

        write(["src/A.cpp", "src/dict/B.cpp", "src/Gone.cpp"], ["src/A.h", "include/cpp-pinyin2/core/C.h"])
        cases.append(("a listed source that no longer exists fails", bool(compare(root))))

        (root / CMAKE).write_text("project(x)\n", encoding="utf-8")
        cases.append(("an empty CMakeLists without the blocks fails", bool(compare(root))))

    failures = 0
    for label, ok in cases:
        print(f"  {'PASS' if ok else 'FAIL'} self-test: {label}")
        failures += 0 if ok else 1
    print(f"\nSELFTEST {'FAIL' if failures else 'PASS'} -- {len(cases)} case(s) reported above")
    return 1 if failures else 0


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="the source list in CMakeLists.txt must equal the tree")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    problems = compare(ROOT)
    for problem in problems:
        print("FAIL: " + problem)
    print(f"check_source_list: {'FAIL' if problems else 'PASS'} ({len(problems)} problem(s))")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
