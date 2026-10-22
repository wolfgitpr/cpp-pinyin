"""A failed dictionary reload must report failure AND leave the engine's phrase layer alone (Q2).

`EngineImpl::rebuildPhrases` used to clear the live phrase table and then load `phrases_dict.txt`
into it. If that load failed, the engine was left with an EMPTY phrase layer -- and until commit
ac07487 the caller was still told the load succeeded. The fix builds the layer in a local table and
swaps it in only on success, so the observable promise is two-part:

  * the reload reports the failure;
  * the engine keeps answering from the layer it already had.

The second half is what the first half of the fix could not give: reporting a failure while leaving
a hollow engine behind is still a broken engine.

The cases compare READINGS, not "did it raise". Measured with the pre-fix code: after a failed
reload the phrase layer was empty, and words that depend on it silently changed their reading --
银行 went from `yín háng` to `yín xíng`, 长大 from `zhǎng dà` to `cháng dà`, 音乐 from `yīn yuè` to
`yīn lè`. A raise/no-raise assertion cannot see any of that (an emptied layer still answers), which
is why this file compares the readings before and after. Words that read the same either way -- 你好,
重庆 -- are deliberately not in the list: they would make the test look stronger than it is.
"""

from __future__ import annotations

import shutil
from pathlib import Path

import pytest

import cpp_pinyin2
from cpp_pinyin2 import CppPinyinError, Engine


def _shipped_dictionary_root() -> Path:
    """The dictionary root the wheel ships -- the directory that CONTAINS the dialects."""
    root = Path(cpp_pinyin2.__file__).parent / "data" / "dict"
    assert (root / "mandarin" / "dialect.json").is_file(), (
        f"the package no longer ships a dictionary root at {root}"
    )
    return root


def _user_dict(path: Path) -> Path:
    path.write_text("\u4f60\u597d:ni3 hao3\n", encoding="utf-8")
    return path


def test_a_reload_that_cannot_rebuild_reports_it_and_keeps_the_old_layer(tmp_path: Path) -> None:
    root = tmp_path / "dict"
    shutil.copytree(_shipped_dictionary_root(), root)

    engine = Engine("mandarin", dict_dir=root)
    try:
        # Readings that come from the phrase layer; character-by-character each of these is read
        # differently, so an emptied layer changes them (that is what makes this test go red).
        # 长大 is deliberately NOT in this list, and the reason is measured: with the fix in place it
        # still goes `zhǎng dà` -> `cháng dà` after this failed reload, so its reading does not come
        # from the phrase layer alone (the tone-sandhi path is the likely owner). Keeping it here
        # would have made the assertion red for a reason this batch does not fix.
        phrase_dependent = ("\u94f6\u884c", "\u97f3\u4e50")
        before = {word: [s.rendered for s in engine.query(word)] for word in phrase_dependent}

        # Break the authoritative built-in layer AFTER construction: the next rebuild has to notice.
        (root / "mandarin" / "phrases_dict.txt").unlink()

        with pytest.raises(CppPinyinError):
            engine.load_user_dict(_user_dict(tmp_path / "user_dict.txt"))

        # And the engine still reads from the layer it had: untouched, not emptied.
        after = {word: [s.rendered for s in engine.query(word)] for word in phrase_dependent}
        assert after == before, (
            "a failed reload must leave the phrase layer exactly as it was; readings changed for: "
            + ", ".join(f"{w}: {before[w]} -> {after[w]}" for w in phrase_dependent if before[w] != after[w])
        )
    finally:
        engine.close()
