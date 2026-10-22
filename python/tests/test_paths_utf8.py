"""The C face reads its paths as UTF-8 -- the pin for decision Q1.

`std::filesystem::path(const char*)` decodes with the C locale, which on Windows is the ANSI code
page. Every other `const char*` this ABI accepts is UTF-8 (the query text, the dictionary payload,
the dialect id), so a path was the one argument whose meaning depended on the machine's locale: a
dictionary root outside that code page became a *different* path and the engine could not find it.

The name below is chosen so that it cannot survive a conversion to the ANSI code page on a host
whose code page is CP936 or Windows-1252: `Ω`, `Ж` and `дик` are absent from both.

MEASURED (this host, 2026-10-06): reverting the fix (`u8path` back to `path`) does NOT make these
cases fail here, so on this machine the pin is a regression guard, not a red proof: this
host''s active code page is 65001 -- UTF-8 -- measured with `chcp`, .NET''s default encoding and
Python''s `getpreferredencoding()`, so `path(const char*)` already decoded this name as UTF-8 and
the two spellings are indistinguishable. On a host without a UTF-8 code page they are not. The
fix is still the right one (it stops the meaning of a path from depending on the locale, which is
what the ruling asked for), but nobody should read this file as evidence that it repaired an
observed failure here. A host without a UTF-8 code page is where the two spellings diverge.
"""

from __future__ import annotations

import shutil
from pathlib import Path

import pytest

import cpp_pinyin2
from cpp_pinyin2 import CppPinyinError, Engine

NON_ANSI = "Ω-Ж-дик"


def _shipped_dictionary_root() -> Path:
    """The dictionary root the wheel ships, used as the source of a byte-identical copy.

    The root is the directory that *contains* the dialects (`mandarin/`, `cantonese/`), each with
    its own `dialect.json` and tables -- passing a dialect directory itself makes the engine look
    for `mandarin/mandarin/` and fail with DictionaryError, which is how this helper was fixed.
    """
    root = Path(cpp_pinyin2.__file__).parent / "data" / "dict"
    assert (root / "mandarin" / "dialect.json").is_file(), (
        f"the package no longer ships a dictionary root at {root}"
    )
    return root


def test_an_engine_opens_from_a_root_outside_the_ansi_code_page(tmp_path: Path) -> None:
    """A real dictionary root, reached through a name the ANSI code page cannot represent."""
    root = tmp_path / NON_ANSI
    shutil.copytree(_shipped_dictionary_root(), root)

    engine = Engine("mandarin", dict_dir=root)
    engine.close()


def test_a_missing_root_outside_the_ansi_code_page_is_still_reported(tmp_path: Path) -> None:
    """The same conversion must not turn a missing directory into an existing one.

    This is the control for the case above: it proves the spec of the name is what matters, not
    "anything non-ASCII is accepted". Both halves go through the same `char*` -> path conversion.
    """
    missing = tmp_path / NON_ANSI / "no-such-dictionary"
    with pytest.raises(CppPinyinError):
        Engine("mandarin", dict_dir=missing)
