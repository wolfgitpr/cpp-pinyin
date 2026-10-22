"""Behavioural gates for the Python shell (docs/PLUGIN_INTERFACE_AND_BINDINGS.md section 6.8.7).

Expectations come from the project's own gates rather than from memory: the install gate asserts
中国人民 -> "zhong1 guo2 ren2 min2", and tests/catch2/test_engine_dialect.cpp asserts the
Cantonese reading "nei5 hou2" for 你好.

Run from python/:   python -m pytest tests -q
"""

from __future__ import annotations

import cpp_pinyin2
import ctypes
import os
import sys
import threading
from pathlib import Path

import pytest

from cpp_pinyin2 import (
    MAX_TEXT_BYTES,
    Engine,
    OnError,
    Style,
    dialect_ids,
    to_text,
    version,
)


@pytest.fixture()
def mandarin():
    engine = Engine("mandarin")
    try:
        yield engine
    finally:
        engine.close()


@pytest.fixture()
def cantonese():
    engine = Engine("cantonese")
    try:
        yield engine
    finally:
        engine.close()


def working_set_bytes():
    """Resident memory in bytes, or None where this process cannot ask.

    On Windows this goes through K32GetProcessMemoryInfo: psapi's older GetProcessMemoryInfo
    returns 0 on current Windows, which silently turned an earlier probe's leak check into a skip.
    """
    if sys.platform == "win32":

        class ProcessMemoryCounters(ctypes.Structure):
            _fields_ = [
                ("cb", ctypes.c_uint32),
                ("PageFaultCount", ctypes.c_uint32),
                ("PeakWorkingSetSize", ctypes.c_size_t),
                ("WorkingSetSize", ctypes.c_size_t),
                ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                ("PagefileUsage", ctypes.c_size_t),
                ("PeakPagefileUsage", ctypes.c_size_t),
            ]

        counters = ProcessMemoryCounters()
        counters.cb = ctypes.sizeof(ProcessMemoryCounters)
        kernel32 = ctypes.windll.kernel32
        query = getattr(kernel32, "K32GetProcessMemoryInfo", None)
        if query is None:
            return None
        query.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ProcessMemoryCounters),
            ctypes.c_uint32,
        ]
        query.restype = ctypes.c_int32
        if not query(kernel32.GetCurrentProcess(), ctypes.byref(counters), counters.cb):
            return None
        return counters.WorkingSetSize

    if sys.platform.startswith("linux"):
        try:
            fields = Path("/proc/self/statm").read_text(encoding="ascii").split()
        except OSError:
            return None
        return int(fields[1]) * os.sysconf("SC_PAGE_SIZE")

    return None


def test_dialect_ids_come_from_the_library():
    assert set(dialect_ids()) == {"mandarin", "cantonese"}


@pytest.mark.parametrize(
    ("text", "style", "expected"),
    [
        ("中国人民", Style.TONE3, "zhong1 guo2 ren2 min2"),
        ("你好世界", Style.TONE3, "ni3 hao3 shi4 jie4"),
        ("中国", Style.NORMAL, "zhong guo"),
        ("中国", Style.FIRST_LETTER, "z g"),
    ],
)
def test_mandarin_styles(mandarin, text, style, expected):
    assert to_text(mandarin.query(text, style=style)) == expected


def test_cantonese(cantonese):
    assert to_text(cantonese.query("你好", style=Style.TONE3)) == "nei5 hou2"


def test_spans_are_spans_not_characters(mandarin):
    from cpp_pinyin2 import _ffi

    spans = mandarin.query("abc你", style=Style.TONE3)
    assert [span.text for span in spans] == ["abc", "你"]
    # The failure is a CLASSIFICATION (a cpp_pinyin2::v2::Code), not a boolean: a span the library
    # cannot read is "something it cannot use", i.e. CPPP2_ERR_INVALID_ARG.
    assert spans[0].failure_kind == _ffi.INVALID_ARG
    assert spans[0].rendered == "abc"
    assert spans[1].failure_kind == _ffi.OK


def test_ignore_drops_failed_spans_instead_of_marking_them(mandarin):
    from cpp_pinyin2 import _ffi

    kept = mandarin.query("abc你", style=Style.TONE3, on_error=OnError.DROP)
    assert [span.text for span in kept] == ["你"]
    assert all(span.failure_kind == _ffi.OK for span in kept)


def test_the_reading_structure_is_built_only_when_it_is_asked_for(mandarin):
    """``include_reading`` is a switch, not a property of the data (c_api.h, S9a2).

    "Not requested" and "this span has no reading" are two different states, and the second one
    is told by ``failure_kind`` alone -- never by "is ``syllables`` filled".
    """
    plain = mandarin.query("你好", style=Style.TONE3)[0]
    assert plain.syllables == ()
    assert plain.rendered == "ni3"

    asked = mandarin.query("你好", style=Style.TONE3, include_reading=True)[0]
    assert len(asked.syllables) == 1
    reading = asked.syllables[0]
    assert reading.initial == "n"
    assert reading.fin == "i"
    assert reading.tone == 3
    assert reading.source in (0, 1, 2, 3)


def test_a_failed_span_carries_no_reading_even_when_requested(mandarin):
    from cpp_pinyin2 import _ffi

    span = mandarin.query("abc你", style=Style.TONE3, include_reading=True)[0]
    assert span.failure_kind == _ffi.INVALID_ARG
    assert span.syllables == (), "a failed span must not enter the syllables array"


def test_render_syllable_reproduces_the_spans_own_rendering(mandarin):
    """The shell owns no style: the string comes back from the library's own renderer."""
    segments = mandarin.query("你好世界", style=Style.TONE3, include_reading=True)
    for span in segments:
        assert mandarin.render_syllable(span.syllables[0], style=Style.TONE3) == span.rendered


def test_render_syllable_refuses_a_multi_syllable_structure(mandarin):
    from cpp_pinyin2 import InvalidArgumentError, Syllable

    multi = Syllable(initial="", fin="zhi4 fa2", tone=0, tone_kind=0, source=0)
    with pytest.raises(InvalidArgumentError):
        mandarin.render_syllable(multi)


def test_a_reading_the_projection_cannot_rebuild_still_renders(mandarin):
    """The identity field (``spelling``), seen from the shell.

    ``hm`` (the interjection) has no final at all, so the ``(initial, fin)`` projection comes back
    empty-handed. Before the identity field existed this entry could only answer
    ``InvalidArgumentError`` -- which reads as "your structure is malformed" for a reading the library
    had merely taken apart lossily.
    """
    from cpp_pinyin2 import InvalidArgumentError, Syllable

    span = mandarin.query("噷", style=Style.TONE3, include_reading=True)[0]
    reading = span.syllables[0]
    assert reading.spelling == "hm", reading
    assert reading.fin == "", "the projection really has nothing to rebuild from"
    assert mandarin.render_syllable(reading, style=Style.TONE3) == span.rendered

    # Drop the identity and the SAME structure is refused: that is the library having nothing to
    # render, not the shell refusing a reading it could not invert.
    halves = Syllable(
        initial=reading.initial,
        fin=reading.fin,
        tone=reading.tone,
        tone_kind=reading.tone_kind,
        source=reading.source,
    )
    assert halves.spelling == ""
    with pytest.raises(InvalidArgumentError):
        mandarin.render_syllable(halves, style=Style.TONE3)


def test_code_text_is_the_librarys_own_wording():
    from cpp_pinyin2 import _ffi, code_text

    assert code_text(_ffi.OK)
    assert code_text(_ffi.INVALID_ARG) != code_text(_ffi.OK)
    # A value outside the value domain is ANSWERED, not refused (c_api.h: "a value outside the
    # domain above is reported as unknown rather than aborting"). The library's sentence for that
    # case is the literal "unknown code" (src/Outcome.cpp, the single authority for wording): it
    # does NOT echo the number. Asserting that it did would be this shell writing the wording
    # itself -- the one thing this entry point exists to prevent.
    assert code_text(99) == "unknown code"


def test_empty_input_is_not_an_error(mandarin):
    assert mandarin.query("") == []


def test_candidates_only_when_requested(mandarin):
    wanted = mandarin.query("行", style=Style.TONE3, include_candidates=True)[0]
    plain = mandarin.query("行", style=Style.TONE3)[0]
    assert wanted.candidates, "a polyphonic character must list candidates when asked"
    assert plain.candidates == ()


def test_readings_and_polyphony(mandarin):
    readings = mandarin.readings("行", style=Style.TONE3)
    assert readings and all(isinstance(reading, str) for reading in readings)
    assert mandarin.is_polyphonic("行") is True
    assert mandarin.is_polyphonic("世") is False


def test_to_simplified_only_looks_at_the_first_character(mandarin):
    assert mandarin.to_simplified("學") == "学"
    assert mandarin.to_simplified("學習") == "学"


def test_tone_convert_moves_a_stored_reading_between_styles(mandarin):
    """The C face of the dialect's own converter, fed the STORED form (what ``readings()`` returns)."""
    assert mandarin.tone_convert("ni3", Style.TONE) == "nǐ"
    assert mandarin.tone_convert("hao3", Style.TONE2) == "ha3o"
    assert mandarin.tone_convert("zhong1", Style.NORMAL) == "zhong"
    assert mandarin.tone_convert("zhong1", Style.INITIALS) == "zh"


def test_tone_convert_refuses_a_reading_that_is_not_the_stored_form(mandarin):
    """Letters plus one tone digit, or nothing.

    Measured 2026-10-21: the converter does not reject a marked or toneless string, it mangles it -- ``"ni"`` came
    back ``"nii"``, ``"nǐ"`` came back ``"nii3"``. A refusal is the only honest answer for those, so the shape is
    checked at the boundary rather than trusted.
    """
    for bad in ("ni", "nǐ", "nǐ3", "hao3hao3"):
        with pytest.raises(ValueError):
            mandarin.tone_convert(bad, Style.TONE)


def test_tone_convert_applies_the_dialect_default_when_no_style_is_given(mandarin):
    """No style means the dialect's DECLARED default, not "leave it alone".

    This test used to compare `tone_convert("ni3")` with `tone_convert("ni3", Style.DEFAULT)` -- the same call
    twice, since `None` becomes `Style.DEFAULT` (-1) -- so it passed while the default path returned the input
    unchanged with success (found 2026-10-22 by a read-only consistency audit, fixed in `cppp2_tone_convert`).
    The assertion is on the VALUE now, because an identity between two calls cannot tell those apart.
    """
    assert mandarin.tone_convert("ni3") == "nǐ"
    assert mandarin.tone_convert("ni3") == mandarin.tone_convert("ni3", Style.TONE)
    assert mandarin.tone_convert("zhong1") == "zhōng"


def test_tone_convert_returns_a_style_the_dialect_does_not_declare_unchanged(cantonese):
    """Cantonese declares normal / first-letter / tone3; asking it for TONE2 is "no conversion", not an error.

    The unchanged answer must be the CALLER's string, not the library's normalised form -- that distinction is
    what makes this observable at all ("jyut6" must not come back "jyu̱t").
    """
    assert cantonese.tone_convert("jyut6", Style.TONE2) == "jyut6"
    assert cantonese.tone_convert("jyut6", Style.TONE3) == "jyut6"


def test_tone_convert_refuses_a_separator(mandarin):
    """One reading goes in. Splitting on separators is the query path's job, not this one's."""
    with pytest.raises(ValueError):
        mandarin.tone_convert("ni3 hao3", Style.TONE)


def test_concurrent_reads_match_the_single_thread_baseline(mandarin):
    baseline = [to_text(mandarin.query("你好世界", style=Style.TONE3)) for _ in range(40)]
    runs = []

    def worker():
        runs.append([to_text(mandarin.query("你好世界", style=Style.TONE3)) for _ in range(40)])

    threads = [threading.Thread(target=worker) for _ in range(4)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    assert len(runs) == 4
    assert all(run == baseline for run in runs)


@pytest.mark.parametrize("bad", ["a\x00b", "\x00"])
def test_nul_in_text_is_refused(mandarin, bad):
    with pytest.raises(ValueError):
        mandarin.query(bad)


def test_nul_in_arguments_is_refused():
    with pytest.raises(ValueError):
        Engine("man\x00darin")
    with pytest.raises(ValueError):
        Engine("mandarin", "bad\x00path")


def test_lone_surrogate_propagates_without_replacement(mandarin):
    assert issubclass(UnicodeEncodeError, UnicodeError)
    with pytest.raises(UnicodeEncodeError):
        mandarin.query("\ud800")


def test_oversized_text_is_refused(mandarin):
    with pytest.raises(ValueError):
        mandarin.query("a" * (MAX_TEXT_BYTES + 1))


def test_non_str_text_is_a_type_error(mandarin):
    with pytest.raises(TypeError):
        mandarin.query(b"not text")


def test_missing_dict_dir_is_diagnosable():
    with pytest.raises(Exception, match="does not exist"):
        Engine("mandarin", "no/such/dir")


def test_default_dict_dir_prefers_the_environment_override(monkeypatch, tmp_path):
    """D1 (docs/DECISIONS.md section 4.1): override first, packaged copy second.

    An EMPTY value must count as unset: `export CPP_PINYIN2_DICT_DIR=` is a shell accident, and treating it
    as "the current directory" would make every query depend on where the process happens to run.
    """
    packaged = Engine.default_dict_dir()

    monkeypatch.setenv(Engine.DICT_DIR_ENV, str(tmp_path))
    assert Engine.default_dict_dir() == tmp_path

    monkeypatch.setenv(Engine.DICT_DIR_ENV, "")
    assert Engine.default_dict_dir() == packaged


def test_a_wrong_environment_override_is_reported_not_silently_replaced(monkeypatch, tmp_path):
    """The override is NOT validated before use: the library says which directory it looked in.

    Falling back to the packaged copy here would hide a broken deployment -- the process would work on the
    developer's machine and fail on the container that set the variable wrong.
    """
    missing = tmp_path / "not-here"
    monkeypatch.setenv(Engine.DICT_DIR_ENV, str(missing))
    with pytest.raises(Exception, match="does not exist"):
        Engine("mandarin")


def test_the_native_library_reports_its_own_install_candidate():
    """D2: the candidate is resolved from the library's own module, not from anything the shell guesses."""
    candidate = Engine.dict_dir_candidate()
    if candidate is None:
        pytest.skip("static build: no module anchor, which is the documented answer")
    assert candidate.as_posix().endswith("share/cpp-pinyin2/dict")


def test_unknown_dialect_names_the_id_it_was_given():
    from cpp_pinyin2 import CppPinyinError, NotReadyError, UnknownDialectError

    # cppp2_open reports an unresolvable id as CPPP2_ERR_NOT_READY (with the id in `detail`), not as
    # a NULL handle plus a message: the two failures are distinguishable at this ABI level.
    with pytest.raises(UnknownDialectError, match="example") as caught:
        Engine("example")
    assert isinstance(caught.value, NotReadyError)
    assert isinstance(caught.value, CppPinyinError)


def test_close_is_idempotent_and_every_entry_point_after_it_raises():
    engine = Engine("cantonese")
    engine.close()
    engine.close()  # a second close is a no-op, not an error
    engine.close()  # ... and so is a third
    after_close = {
        "query": lambda: engine.query("你好"),
        "query_text": lambda: to_text(engine.query("你好")),
        "readings": lambda: engine.readings("你"),
        "to_simplified": lambda: engine.to_simplified("學"),
        "is_polyphonic": lambda: engine.is_polyphonic("你"),
        "load_user_dict": lambda: engine.load_user_dict("whatever.txt"),
        "load_traditional_dict": lambda: engine.load_traditional_dict("whatever.txt"),
    }
    for name, call in after_close.items():
        try:
            call()
        except ValueError as exc:
            assert "closed" in str(exc), f"{name}: unexpected ValueError {exc}"
        else:
            pytest.fail(f"{name} did not raise after close()")


def test_engine_works_as_a_context_manager():
    with Engine("mandarin") as engine:
        # An explicit style, because what this test is about is the context manager. The default is
        # pinned once, on purpose, in test_the_default_style_is_the_dialects_own.
        assert to_text(engine.query("中国", style=Style.NORMAL)) == "zhong guo"
    with pytest.raises(ValueError):
        engine.query("中国")


def test_the_default_style_is_the_dialects_own(mandarin):
    """C4/N27: an unspecified style means the dialect's own default (Mandarin TONE), in every layer.

    This is the Python half of the same criterion C++ and the C ABI pin in tests/cpp/
    test_default_style.cpp and tests/c_api_smoke.c. It used to be NORMAL here, which is what made
    this shell disagree with the C++ facades about ``query_text("中国")``.
    """
    assert int(Style.DEFAULT) == -1, "the sentinel is a number of its own, not a style"
    assert to_text(mandarin.query("中国")) == "zhōng guó", "unspecified -> the dialect's own default"
    assert to_text(mandarin.query("中国", style=Style.DEFAULT)) == "zhōng guó"
    assert to_text(mandarin.query("中国", style=Style.NORMAL)) == "zhong guo"

    # readings() is the same promise through a different entry point -- and it was the one C#
    # entry still hard-coding NORMAL, fixed in 84d0a65. Measured before it was written: the
    # sentinel answers ['zhōng', 'zhòng'] here, which equals style=Style.TONE and differs from
    # style=Style.NORMAL, because src/Mandarin.cpp marks "tone" as the styles[] default (isDefault).
    assert mandarin.readings("中") == mandarin.readings("中", style=Style.TONE)
    assert mandarin.readings("中") != mandarin.readings("中", style=Style.NORMAL)


def test_dropped_engines_do_not_leak():
    before = working_set_bytes()
    for _ in range(50):
        engine = Engine("mandarin")
        del engine
    after = working_set_bytes()
    if before is None or after is None:
        pytest.skip(f"cannot read this process's working set on {sys.platform}")
    growth = after - before
    assert growth < 8 * 1024 * 1024, f"50 dropped engines grew the working set by {growth} bytes"


def test_user_dict_without_entries_is_refused(mandarin, tmp_path):
    empty = tmp_path / "empty.txt"
    empty.write_text("", encoding="utf-8")
    with pytest.raises(ValueError, match="no .key:value. entries"):
        mandarin.load_user_dict(empty)


def test_user_dict_of_skipped_lines_is_refused(mandarin, tmp_path):
    # splitLine in src/DictUtil.cpp drops lines without a separator (there is no comment syntax),
    # so a file of prose parses to zero entries -- and the library reports success for that.
    prose = tmp_path / "prose.txt"
    prose.write_text("just prose\n# this is skipped, not a comment\n", encoding="utf-8")
    with pytest.raises(ValueError, match="no .key:value. entries"):
        mandarin.load_user_dict(prose)


def test_user_dict_key_longer_than_four_is_refused(mandarin, tmp_path):
    bad = tmp_path / "longkey.txt"
    bad.write_text("一二三四五:yi1 er4 san1 si4 wu3\n", encoding="utf-8")
    with pytest.raises(ValueError, match="MAX_USER_DICT_KEY"):
        mandarin.load_user_dict(bad)


def test_user_dict_that_does_not_exist_is_refused(mandarin, tmp_path):
    with pytest.raises(Exception, match="does not exist"):
        mandarin.load_user_dict(tmp_path / "nope.txt")


def test_bundled_user_dict_loads_and_leaves_the_engine_usable(mandarin):
    mandarin.load_user_dict(Engine.default_dict_dir() / "mandarin" / "user_dict.txt")
    # Explicit style: this test is about the user dict not breaking the engine, not about the default.
    assert to_text(mandarin.query("中国", style=Style.NORMAL)) == "zhong guo"


def test_traditional_dict_merges_without_breaking_conversion(mandarin):
    mandarin.load_traditional_dict(
        Engine.default_dict_dir() / "mandarin" / "trans_word.txt", clear_existing=False
    )
    assert mandarin.to_simplified("學") == "学"


def test_query_opts_init_respects_a_shrunk_struct_size():
    """Rule 6 of section 6.8.1, seen from the caller's side: a caller that declares less must not be
    written past what it declared (decision D19 / commit e68832d).

    ``include_reading`` is covered by the same loop on purpose: the ABI's growth rule is that a new
    option is APPENDED and existing fields keep their offsets, so a caller whose declaration stops
    before it must keep its own zero/default there -- an inserted field would also move the fields
    after it, which is exactly the failure this test exists to catch.
    """
    from cpp_pinyin2 import _ffi, _library

    size_of_size = ctypes.sizeof(ctypes.c_size_t)
    size_of_int = ctypes.sizeof(ctypes.c_int32)
    declared = size_of_size + size_of_int  # struct_size + style, nothing beyond

    options = _ffi.QueryOpts()
    beyond = ("on_error", "include_candidates", "v_to_u", "mark_neutral_as_5", "include_reading")
    for field in ("style",) + beyond:
        setattr(options, field, 12345)
    options.struct_size = declared
    _library().cppp2_query_opts_init(ctypes.byref(options))

    assert options.style != 12345, "a field inside the declared size must be initialised"
    for field in beyond:
        assert getattr(options, field) == 12345, f"init wrote past struct_size into {field}"

    # The appended field's offset is after every field that existed before it, which is what keeps
    # an older, shorter caller's layout intact. Anything after it is a `reserved_*` CLAIM on trailing
    # padding (reserved_1, 2026-10-19), never an option.
    offsets = [name for name, _ in _ffi.QueryOpts._fields_]
    tail = offsets[offsets.index("include_reading") + 1:]
    assert all(name.startswith("reserved_") for name in tail), (
        f"only padding claims may follow include_reading, got {tail}")
    assert offsets[:6] == [
        "struct_size",
        "style",
        "on_error",
        "include_candidates",
        "v_to_u",
        "mark_neutral_as_5",
    ], offsets


def test_version_matches_the_library():
    """`version()` reads `cppp2_version()`; `__version__` is written from the single source by build_native.

    The assertion is the INVARIANT the two names are documented to satisfy (`cpp_pinyin2/__init__.py`:
    "Must equal :data:`__version__`"), not a copy of the number: a literal here would be a second home for
    the version, and it would have to be edited on every release -- the same drift the single-source rule
    exists to prevent. test_contract.py pins __version__ to the CMakeLists.txt source.
    """
    assert version() == cpp_pinyin2.__version__


def test_tone_sandhi_is_the_library_rules_not_the_shell():
    """The post-processing step, one sentence per rule -- the same input and the same expectations as
    the C++ case (tests/cpp/test_tone_sandhi.cpp) and the C case (tests/cpp/test_c_api.cpp)."""
    with cpp_pinyin2.Engine() as engine:
        style = cpp_pinyin2.Style.TONE3

        assert rendered(engine.tone_sandhi("你好", style=style)) == ["ni2", "hao3"]
        assert rendered(engine.tone_sandhi("不是", style=style)) == ["bu2", "shi4"]
        assert rendered(engine.tone_sandhi("一定", style=style)) == ["yi2", "ding4"]
        assert rendered(engine.tone_sandhi("小心", style=style)) == ["xiao3", "xin1"]

        # The post-processing is what changed it: a plain query still renders the table readings.
        assert rendered(engine.query("你好", style=style)) == ["ni3", "hao3"]

        # Punctuation is a boundary; the run after it still applies.
        assert rendered(engine.tone_sandhi("一，你好", style=style)) == ["yi1", "，", "ni2", "hao3"]

        # The switches are the caller's value, not the library default: all off means nothing is rewritten.
        assert rendered(
            engine.tone_sandhi("你好", style=style, third_tone=False, bu=False, yi=False)
        ) == ["ni3", "hao3"]


def test_tone_sandhi_is_refused_when_the_dialect_declares_none():
    """Tone sandhi belongs to the DIALECT (D10). Cantonese declares none, so the C entry -- and therefore this
    shell -- refuses instead of rewriting jyutping with 不/一 rules that do not apply there. Parity with the C
    case in tests/cpp/test_c_api.cpp: the refusal is the feature, because a jyutping reading has exactly the
    shape the Mandarin rules accept, so nothing in the data could have stopped them."""
    with cpp_pinyin2.Engine(dialect_id="cantonese") as engine:
        with pytest.raises(cpp_pinyin2.CppPinyinError):
            engine.tone_sandhi("你好", style=cpp_pinyin2.Style(8))


def rendered(spans):
    """The rendered string of every span, in order."""
    return [span.rendered for span in spans]
