"""A broken phrase gate must fail the rebuild instead of being silently ignored (Q4).

`phrases.builtinStart` says which characters can OPEN a phrase window, and it is loaded from
`phrases_map.txt`. It used to be filled in `init()` only: `rebuildPhrases` re-read `phrases_dict.txt`
every time and never refreshed the gate, so the gate could keep describing a different pair of files
than the table it guards -- and a stale gate fails silently, because `test()` returning false just
means "no phrase starts here".

The observable here is the LOAD RESULT, and that is deliberate: measurement showed the reading of
银行 stays `yín háng` in both states, so a reading comparison would not distinguish them. What does
distinguish them is that the old code reported success for a rebuild whose gate it had failed to
reload:

  * old code (`PhraseLayers.cpp` from 885de33): `load_user_dict` returns normally;
  * fixed code: it raises.

The reading is still asserted, for the other half of the promise: a failed rebuild must not leave the
engine half-built -- 银行 must still read `yín háng` because the previous layer is kept (the swap
discipline from 1d387b1).
"""

from __future__ import annotations

import shutil
from pathlib import Path

import pytest

import cpp_pinyin2
from cpp_pinyin2 import CppPinyinError, Engine


def _shipped_dictionary_root() -> Path:
    root = Path(cpp_pinyin2.__file__).parent / "data" / "dict"
    assert (root / "mandarin" / "phrases_map.txt").is_file(), (
        f"the package no longer ships a phrase gate at {root}"
    )
    return root


def _readings(engine: Engine, words: tuple[str, ...]) -> dict[str, list[str]]:
    return {word: [s.rendered for s in engine.query(word)] for word in words}


def test_a_gate_that_cannot_be_reloaded_fails_the_load_and_keeps_the_layer(tmp_path: Path) -> None:
    root = tmp_path / "dict"
    shutil.copytree(_shipped_dictionary_root(), root)

    engine = Engine("mandarin", dict_dir=root)
    try:
        words = ("\u9280\u884c",)
        before = _readings(engine, words)

        # Corrupt the gate AFTER construction: the next rebuild has to notice it cannot be reloaded.
        (root / "mandarin" / "phrases_map.txt").write_text("", encoding="utf-8")

        user_dict = tmp_path / "user_dict.txt"
        user_dict.write_text("\u4f60\u597d:ni3 hao3\n", encoding="utf-8")
        with pytest.raises(CppPinyinError):
            engine.load_user_dict(user_dict)

        assert _readings(engine, words) == before, (
            "a failed rebuild must keep the layer it had, not leave a half-built engine"
        )
    finally:
        engine.close()
