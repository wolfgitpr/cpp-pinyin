#!/usr/bin/env python3
"""Smoke-test an INSTALLED cpp_pinyin2, i.e. the wheel, from outside the source tree.

WHY IT IS A FILE AND NOT A SNIPPET IN THE WORKFLOW. The workflow used to hold these assertions inline,
where they cannot be run locally without retyping them, so they would be prose that CI happens to
execute. Keeping them here means the development machine and CI run the same bytes, and the local run
is the one that found the first real disagreement: an earlier draft asserted
``cpp_pinyin2.cppp2_version()``, which does not exist (the C ABI's ``cppp2_version()`` is compared against
``__version__`` inside the pytest suite, not exposed as a module attribute). The workflow would have
failed on its first run for a reason that had nothing to do with the package.

Run it from the repository root and with the installed package on the path, NOT from ``python/``: the
source package directory would shadow the installed one and the test would pass while proving nothing.

    python -m venv /tmp/v && /tmp/v/bin/python -m pip install --no-index --no-deps <wheel>
    /tmp/v/bin/python python/tools/smoke_installed.py --wheel <wheel>

The expected version is read from the wheel's own file name rather than hardcoded, so bumping the
version does not require editing this file (one source of truth, applied to the test as well).
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Optional, Sequence


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--wheel", type=Path, help="the wheel that was installed; its file name gives the expected version")
    options = parser.parse_args(argv)

    import cpp_pinyin2
    from cpp_pinyin2 import Engine, Style

    print("module file :", cpp_pinyin2.__file__)
    print("version     :", cpp_pinyin2.__version__)
    print("dialects    :", sorted(cpp_pinyin2.dialect_ids()))

    if options.wheel is not None:
        match = re.match(r"cpp_pinyin2-([0-9][^-]*)-", Path(options.wheel).name)
        if not match:
            print(f"FAIL: cannot read a version out of {Path(options.wheel).name}")
            return 1
        expected = match.group(1)
        if cpp_pinyin2.__version__ != expected:
            print(f"FAIL: the wheel is named {expected} but the installed package reports {cpp_pinyin2.__version__}")
            return 1
        print("version matches the wheel name:", expected)

    dialects = sorted(cpp_pinyin2.dialect_ids())
    if dialects != ["cantonese", "mandarin"]:
        print(f"FAIL: dialects are {dialects}")
        return 1

    spans = Engine("mandarin").query("你好世界", style=Style.TONE3)
    if [span.text for span in spans] != list("你好世界"):
        print(f"FAIL: mandarin spans are {[span.text for span in spans]}")
        return 1
    readings = " ".join(span.rendered for span in spans)
    if readings != "ni3 hao3 shi4 jie4":
        print(f"FAIL: mandarin readings are {readings}")
        return 1
    print("mandarin    :", readings)

    # The structural side of the same call, through the two entry points that only exist on the
    # 20-symbol surface: the default builds no structure, and asking for it yields exactly one
    # reading per span -- which the library's own renderer reproduces byte for byte.
    if any(span.syllables for span in spans):
        print("FAIL: syllables were built without include_reading")
        return 1
    structured = Engine("mandarin")
    try:
        rows = structured.query("你好世界", style=Style.TONE3, include_reading=True)
        rendered = [structured.render_syllable(row.syllables[0], style=Style.TONE3) for row in rows]
    finally:
        structured.close()
    if rendered != [span.rendered for span in spans]:
        print(f"FAIL: render_syllable produced {rendered}")
        return 1
    print("syllables   :", " ".join(f"{row.syllables[0].initial}-{row.syllables[0].fin}" for row in rows))

    cantonese = Engine("cantonese").query("你好世界", style=Style.TONE3)
    if len(cantonese) != 4:
        print(f"FAIL: cantonese spans are {len(cantonese)}")
        return 1
    print("cantonese   :", " ".join(span.rendered for span in cantonese))

    print("SMOKE OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
