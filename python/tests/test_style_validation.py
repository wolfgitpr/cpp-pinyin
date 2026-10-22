"""A style the dialect does not declare must be refused by the shell, not rendered anyway (N20=A).

The library's pass-through is a deliberate contract (`core/Types.h`, `ext/c_api.h`): a style it cannot
apply comes back unchanged. That is the right contract for a C caller holding a number it owns, and the
wrong one for a shell, which has names -- and the engine will hand it the dialect's OWN style table, so
the shell can refuse by name instead of rendering something else.

Both tables were printed before these assertions were written (`cppp2_style_list`):

    mandarin  : 0 normal, 1 tone (default), 2 tone2, 3 first-letter, 4 bopomofo, 5 initials,
                6 finals, 7 finals-tone3, 8 tone3
    cantonese : 0 normal, 3 first-letter, 8 tone3 (default)

The first draft of this file asserted that cantonese + style=3 was a defect. That was wrong, and the
table says why: cantonese declares first-letter itself, so that call is legitimate and is now pinned as
a case that must keep working. What is genuinely wrong is a value no row claims -- and 1 (tone) is the
most tempting of those, being Mandarin's default and every dialect's historic number.
"""

from __future__ import annotations

import pytest

from cpp_pinyin2 import Engine, InvalidArgumentError, Style, to_text

TEXT = "\u4e2d\u56fd"


def test_a_style_the_dialect_does_not_declare_is_refused() -> None:
    """1 is Mandarin's default and absent from Cantonese's three rows: it must be named, not applied."""
    engine = Engine("cantonese")
    try:
        with pytest.raises(InvalidArgumentError) as caught:
            to_text(engine.query(TEXT, style=1))
        assert "1" in str(caught.value), "the refused value is named in the message"
        assert "cantonese" in str(caught.value), "and so is the dialect"
    finally:
        engine.close()


def test_an_out_of_range_style_is_refused() -> None:
    engine = Engine("mandarin")
    try:
        with pytest.raises(InvalidArgumentError):
            to_text(engine.query(TEXT, style=9))
        # A negative number is not the sentinel by accident: only -1 is, and it must keep working.
        assert to_text(engine.query(TEXT, style=Style.DEFAULT)) == to_text(engine.query(TEXT, style=Style.TONE))
    finally:
        engine.close()


def test_every_declared_style_still_works() -> None:
    """The refusal must not take a legitimate style with it: both tables' own rows keep working."""
    mandarin = Engine("mandarin")
    cantonese = Engine("cantonese")
    try:
        assert to_text(mandarin.query(TEXT, style=3)) == "z g"
        # Cantonese declares first-letter itself (alias 3) -- the value the first draft wrongly rejected.
        assert to_text(cantonese.query(TEXT, style=3)) == "z g"
        assert to_text(cantonese.query(TEXT, style=Style.TONE3)) == to_text(cantonese.query(TEXT))
    finally:
        mandarin.close()
        cantonese.close()