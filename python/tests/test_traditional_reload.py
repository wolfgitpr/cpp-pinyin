"""A failed traditional-dictionary load must not destroy the mapping in use (Q5).

`EngineImpl::loadTraditionalDict` zeroed the live conversion table and then loaded into it, so a
failed load left the traditional mapping destroyed while `isReady()` still said true -- every later
conversion then silently disagreed with the dictionary the caller had asked for. It now loads into a
local copy and swaps on success only.

The observable is a reading that goes THROUGH the conversion table: 銀行 is traditional input, and
its reading only comes out as `yín háng` because the table maps it to the simplified 银行 first.
漢字 and `to_simplified` are checked for the same reason.

THIS IS A GUARD, NOT A RED PROOF, and the reason is measured. With src/EngineImpl.cpp taken from
f000374 -- the clear-then-load version, `m_transTable.fill(0)` present -- every observable in this
file is unchanged after a failed load: the readings and `to_simplified` both still answer correctly.
The failed load never reaches the C++ loader at all, because the Python face validates the path AND
the content before calling down (a nonexistent path raises "dictionary file does not exist", a file
without `key:value` entries raises "no `key:value` entries"), so the zeroing cannot be triggered from
here. An earlier version of this docstring claimed the pre-fix code changed a reading; that claim was
wrong and is corrected here.

Getting a real red case for Q5 therefore needs a C or C++ level caller, i.e. a separate item, not a
change to this file.
"""

from __future__ import annotations

import pytest

from cpp_pinyin2 import CppPinyinError, Engine

# Traditional spellings whose reading is produced via the conversion table.
TRADITIONAL = ("\u9280\u884c", "\u6f22\u5b57")


def _readings(engine: Engine, words: tuple[str, ...]) -> dict[str, list[str]]:
    return {word: [s.rendered for s in engine.query(word)] for word in words}


def test_a_failed_traditional_load_leaves_the_mapping_alone(tmp_path) -> None:
    engine = Engine("mandarin")
    try:
        before = _readings(engine, TRADITIONAL)

        # A path that cannot be loaded: the load has to fail, and the mapping has to survive it.
        with pytest.raises(CppPinyinError):
            engine.load_traditional_dict(str(tmp_path / "no_such_table.txt"))

        assert _readings(engine, TRADITIONAL) == before, (
            "a failed traditional-dict load must leave the conversion table as it was"
        )

        # And the engine is still a working engine, not a half-cleared one.
        assert engine.to_simplified("\u6f22\u5b57") == "\u6c49"
    finally:
        engine.close()
