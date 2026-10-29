"""Python shell for cpp-pinyin2: a ctypes binding over the C ABI in ``c_api.h``.

What this is not: a second implementation. Splitting, reading selection and style conversion
all happen inside the library; this package marshals bytes in and copies results out
(`docs/DECISIONS.md` §2.1).

Invariants kept here (same section):

* one native handle per object, held in a slot that ``close()`` empties -- ``cppp2_destroy``
  only ``delete``s the engine, so a second call on a non-NULL pointer is a use-after-free,
  and the C header's "NULL is a no-op" covers NULL only;
* every non-``OK`` status raises, with a fallback for codes this shell has never seen;
* results are copied out before ``cppp2_result_free`` runs, in a ``finally``;
* U+0000 in text or in a path is refused before crossing the boundary, because the C ABI
  takes NUL-terminated strings and would silently truncate;
* only default (GIL) CPython builds are supported: a free-threaded build loses the
  "a CDLL call runs under the GIL" premise that the library's lock-free read path relies on.
"""

from __future__ import annotations

import ctypes
import os
from dataclasses import dataclass, field
from enum import IntEnum
from pathlib import Path
from typing import Iterable, List, Optional, Tuple, Union

from . import _ffi
from ._errors import (
    CppPinyinError,
    DictionaryError,
    DictionaryIncompatibleError,
    InternalError,
    InvalidArgumentError,
    NotReadyError,
    UnknownDialectError,
)
from ._native import load_library

try:
    from ._version import __commit__, __version__
except ImportError as exc:  # pragma: no cover - packaging guard
    raise CppPinyinError(
        "cpp_pinyin2/_version.py is missing: this package was not assembled by "
        "python/tools/build_native.py. The version has exactly one source (the root "
        "CMakeLists.txt `project(... VERSION)`), and falling back to a default would "
        "produce a package that reports a version nobody built."
    ) from exc

__all__ = [
    "CppPinyinError",
    "DictionaryError",
    "DictionaryIncompatibleError",
    "Engine",
    "InternalError",
    "InvalidArgumentError",
    "MAX_TEXT_BYTES",
    "MAX_USER_DICT_KEY",
    "NotReadyError",
    "OnError",
    "Segment",
    "Style",
    "Syllable",
    "UnknownDialectError",
    "__commit__",
    "__version__",
    "code_text",
    "to_text",
    "dialect_ids",
    "version",
]

#: Largest accepted input, checked on the UTF-8 encoding of the four text entry points. The
#: library has no limit of its own (its sliding window is linear, not quadratic); this exists
#: to refuse memory-exhaustion-sized inputs at the boundary.
MAX_TEXT_BYTES = 16 * 1024 * 1024

#: Longest user-dictionary key the library can index. `encodePhrasesKey` packs 16 bits per
#: character into a uint64, so a longer key has its leading characters shifted out and loads
#: under the wrong key without any error.
MAX_USER_DICT_KEY = 4

_LIB: Optional[ctypes.CDLL] = None


def _library() -> ctypes.CDLL:
    """The process-wide CDLL, loaded once."""
    global _LIB
    if _LIB is None:
        _LIB = load_library()
    return _LIB


class Style(IntEnum):
    """The library's own style numbering -- NOT pypinyin's.

    ``NORMAL`` (0) is the only value both built-in dialects share; ``FIRST_LETTER`` (3) and
    ``TONE3`` (8) exist for both; the rest are Mandarin-only. A style a dialect does not
    support returns the input unchanged, which is the library's documented contract, and this
    shell does not second-guess it.

    ``DEFAULT`` (-1) is not a style: it is the sentinel "whatever this dialect defaults to",
    and it is what every entry point below asks for when the caller names no style. The
    library resolves it (`core/Types.h` -> ``resolveStyle``), so an unspecified style means
    Mandarin ``TONE`` and Cantonese ``TONE3`` in ALL FOUR layers -- Python, C#, C++ and the C
    ABI alike. It used to mean ``NORMAL`` here and ``style = 0`` in the C ABI, which made this
    shell and the C ABI the two layers that disagreed with the C++ facades about the same
    sentence (C4/N27 fixed that by giving "unset" a value of its own).
    """

    DEFAULT = -1
    NORMAL = 0
    TONE = 1
    TONE2 = 2
    FIRST_LETTER = 3
    BOPOMOFO = 4
    INITIALS = 5
    FINALS = 6
    FINALS_TONE3 = 7
    TONE3 = 8


class OnError(IntEnum):
    """``ErrorHandling``: ``DEFAULT`` marks a span it could not read, ``DROP`` drops it."""

    DEFAULT = 0
    DROP = 1


@dataclass(frozen=True)
class Syllable:
    """One STRUCTURED reading -- ``cppp2_syllable``: data, never a rendering.

    ``initial`` is "" for a zero-initial syllable; ``fin`` is the canonical final (ü written as
    ``v``, ``uei``/``iou``/``uen`` spelled out, ``zhi`` -> ``i``) -- the two strings the
    ``INITIALS`` and ``FINALS`` styles render. The pair is a PROJECTION of the table reading, not
    a re-encoding of it, so it does not always rebuild the spelling the table stores -- ``spelling``
    is that spelling, verbatim, and is what :meth:`Engine.render_syllable` renders when it is set.

    ``tone`` is a dialect-internal tone-category number and is NOT comparable across dialects
    (Mandarin 0..5, Cantonese 1..6): the same integer means a different category in each. It is
    filled for every style, including the ones whose string carries no tone. ``tone_kind`` and
    ``source`` are the two value domains of the C header (``CPPP2_TONE_KIND_*`` / ``CPPP2_SOURCE_*``);
    the shell does not rename them, so a caller can compare them against that header directly.
    """

    initial: str
    fin: str
    tone: int
    tone_kind: int
    source: int
    #: The table's own reading bytes -- this row's IDENTITY. ``initial``/``fin`` are its projection.
    #: Declared LAST because it is optional: a caller holding only the two halves can leave it empty
    #: and rendering falls back to the projection, which is exactly what a NULL ``spelling`` means in
    #: the C struct. A reading that came out of :meth:`Engine.query` always carries it.
    spelling: str = ""


@dataclass(frozen=True)
class Segment:
    """One input span and what the engine read for it -- ``cppp2_segment``.

    ``text`` is a span of the input, not a character: a run of ASCII letters is one span.
    ``rendered`` is the ONE rendered string of this span -- it equals ``text`` when the span
    failed, which is why the shell does not call it a "reading": ``Segment.syllables`` is where
    the reading as data lives.

    ``failure_kind`` is the failure classification (numerically a ``cpp_pinyin2::v2::Code``, 0 = OK;
    :func:`code_text` is the single authority for its wording). It -- never "is ``syllables``
    filled" -- is the failure test: ``syllables`` is empty both when the span failed and when
    the query did not ask for the structure (``include_reading=False``).

    ``candidates`` is empty unless the query asked for candidates -- and it is not necessarily a
    superset of ``rendered``: a phrase-table hit can carry a reading the character table does
    not list.
    """

    text: str
    rendered: str
    failure_kind: int
    candidates: Tuple[str, ...] = field(default=())
    syllables: Tuple[Syllable, ...] = field(default=())


def code_text(code: int) -> str:
    """The library's own wording for a status code (``cppp2_code_text``).

    A shell that wants to say what went wrong asks here instead of writing its own sentence, so
    the wording has one authority -- the library. Accepts a ``failure_kind`` from a
    :class:`Segment` as well: it is the same value domain.
    """
    return _ffi.decode(_library().cppp2_code_text(int(code)))


def to_text(segments: Iterable["Segment"]) -> str:
    """``query()``'s segments joined into one space-separated string -- the shape ``Result::join()`` has in C++.

    A projection of the result, not a second way to ask the engine: the frozen C++ face puts the string form on
    the RESULT (``Result::join`` in ``include/cpp-pinyin2/core/Types.h``), and this mirrors it. A failed span
    contributes its original text (``rendered`` equals ``text`` there), so the input stays readable in the output.

    >>> to_text(engine.query("中国"))
    'zhōng guó'
    """
    return " ".join(segment.rendered for segment in segments)


def _encode_text(value: str, what: str) -> bytes:
    """Validate and encode an argument that crosses the boundary as UTF-8 text."""
    if not isinstance(value, str):
        raise TypeError(f"{what} must be str, not {type(value).__name__}")
    if "\x00" in value:
        raise ValueError(
            f"{what} contains U+0000: the C ABI takes NUL-terminated UTF-8, so an embedded "
            f"NUL would be silently truncated instead of rejected"
        )
    encoded = value.encode("utf-8")  # a lone surrogate raises UnicodeEncodeError, by design
    if len(encoded) > MAX_TEXT_BYTES:
        raise ValueError(
            f"{what} is {len(encoded)} bytes, over MAX_TEXT_BYTES ({MAX_TEXT_BYTES})"
        )
    return encoded


def _encode_path(value: Union[str, os.PathLike], what: str) -> bytes:
    """Validate and encode a filesystem path argument."""
    raw = os.fspath(value)
    if isinstance(raw, bytes):
        raw = raw.decode("utf-8")
    if not isinstance(raw, str):
        raise TypeError(f"{what} must be str or os.PathLike, not {type(value).__name__}")
    if "\x00" in raw:
        raise ValueError(f"{what} contains U+0000, which the C ABI would silently truncate")
    return raw.encode("utf-8")


def _check_dict_file(path: Path) -> None:
    """Refuse a user dictionary the library would accept while loading nothing useful.

    Transport-layer only (section 6.8.1, rule 10) -- this is not a second parser. Two
    measured library behaviours motivate it:

    * the loader behind ``cppp2_load_user_dict`` returns success unconditionally
      (``src/dict/DictUtil.cpp``, the end of ``loadUserDictLayer``), so a file that parses to zero
      entries loads "fine";
    * a key longer than :data:`MAX_USER_DICT_KEY` is silently truncated by
      ``encodePhrasesKey``.

    Lines without ``:`` are skipped here as well, matching ``splitLine`` in
    ``src/dict/DictUtil.cpp``, which ignores them rather than failing: a stray line is not
    evidence of a malformed file.
    """
    entries = 0
    with path.open("r", encoding="utf-8") as handle:
        for lineno, line in enumerate(handle, 1):
            stripped = line.strip()
            if not stripped:
                continue
            separator = stripped.find(":")
            if separator <= 0:
                continue
            key = stripped[:separator]
            if len(key) > MAX_USER_DICT_KEY:
                raise ValueError(
                    f"{path}:{lineno}: key {key!r} is {len(key)} characters, over "
                    f"MAX_USER_DICT_KEY ({MAX_USER_DICT_KEY}); the library would truncate it "
                    f"to the last {MAX_USER_DICT_KEY} characters and index the wrong key"
                )
            entries += 1
    if entries == 0:
        raise ValueError(
            f"{path}: no `key:value` entries. The library reports success for a zero-entry "
            f"file on this path, which would leave the engine silently unchanged"
        )


def _make_opts(
    style: Union[Style, int],
    *,
    on_error: Union[OnError, int] = OnError.DEFAULT,
    include_candidates: bool = False,
    v_to_u: bool = False,
    mark_neutral_as_5: bool = False,
    include_reading: bool = False,
) -> "_ffi.QueryOpts":
    """Build a caller-owned ``cppp2_query_opts`` for one call.

    ``struct_size`` is written BEFORE ``cppp2_query_opts_init`` (the documented order, decision D19).
    init writes exactly the fields inside the declared size; a zeroed ``struct_size`` is read as
    "the whole struct" and every field is written, so a zeroed size is not a way to reach the
    library's defaults -- declare the real size.
    """
    opts = _ffi.QueryOpts()
    opts.struct_size = ctypes.sizeof(_ffi.QueryOpts)
    _library().cppp2_query_opts_init(ctypes.byref(opts))
    opts.style = int(style)
    opts.on_error = int(on_error)
    opts.include_candidates = 1 if include_candidates else 0
    opts.v_to_u = 1 if v_to_u else 0
    opts.mark_neutral_as_5 = 1 if mark_neutral_as_5 else 0
    opts.include_reading = 1 if include_reading else 0
    return opts


def _checked_opts(handle, dialect_id, style, **kw):
    """``_make_opts``, after refusing a style this dialect does not declare (N20=A).

    The library's pass-through is a deliberate contract: a style it cannot apply comes back
    unchanged. A shell is in a different position -- it has names, and the engine will hand it the
    dialect's OWN style table -- so it refuses instead of rendering something else. Measured on this
    shell before the check existed: cantonese with style=3 rendered "z g" (Mandarin FIRST_LETTER over
    Cantonese readings), and style=9 / style=-2 both silently rendered a dialect default.
    """
    value = int(style)
    if value != -1:  # -1 is the sentinel Style.DEFAULT: "the dialect's own default"
        lib = _library()
        rows = ctypes.POINTER(_ffi.Style)()
        count = ctypes.c_size_t(0)
        status = lib.cppp2_style_list(handle, ctypes.byref(rows), ctypes.byref(count))
        _ffi.raise_for_status(status, "cppp2_style_list")
        try:
            aliases = {rows[i].alias: rows[i].name.decode("utf-8") for i in range(count.value)}
        finally:
            lib.cppp2_style_list_free(rows, count)
        if value not in aliases:
            declared = ", ".join(f"{alias} ({name})" for alias, name in sorted(aliases.items()))
            raise InvalidArgumentError(
                f"{value} is not a style of dialect {dialect_id!r}; it declares {declared} -- "
                f"or pass Style.DEFAULT for the dialect's own default"
            )
    return _make_opts(style, **kw)



class Engine:
    """One dialect over one dictionary root, backed by one ``cppp2_engine *``.

    Use it as a context manager, or call :meth:`close`. After ``close()`` every method raises
    ``ValueError`` -- including a second ``close()``, which is a no-op instead.

    Reads (``query``, ``readings``, ``to_simplified``, ``is_polyphonic``) are safe from
    several threads at once on the same engine. ``load_user_dict`` and
    ``load_traditional_dict`` mutate tables those reads walk: see their docstrings.
    """

    __slots__ = ("_handle", "_dialect_id", "_dict_dir")

    #: The environment variable a deployment sets to point at its own dictionary root. It is read HERE, in
    #: the shell, and never inside the library: the C++/C surface deliberately has no implicit path
    #: (`include/cpp-pinyin2/ext/c_api.h`), and a shell is the layer that decides what to fall back on.
    DICT_DIR_ENV = "CPP_PINYIN2_DICT_DIR"

    def __init__(
        self,
        dialect_id: str = "mandarin",
        dict_dir: Optional[Union[str, os.PathLike]] = None,
    ) -> None:
        lib = _library()
        # Validate BOTH arguments before touching the filesystem. pathlib swallows the
        # ValueError a NUL byte raises and reports the path as simply not existing, so
        # checking the directory first makes "bad\x00path" look like a missing directory
        # instead of an invalid argument -- the wrong diagnosis, from the right guard.
        dialect_bytes = _encode_path(dialect_id, "dialect_id")
        root = Path(os.fspath(dict_dir)) if dict_dir is not None else self.default_dict_dir()
        root_bytes = _encode_path(root, "dict_dir")
        if not root.is_dir():
            raise CppPinyinError(f"dictionary root does not exist: {root}")

        error = _ffi.Error()
        handle = lib.cppp2_open(None, dialect_bytes, root_bytes, ctypes.byref(error))
        if not handle:
            # The status code is what distinguishes the two failures the old cppp_create collapsed
            # into "NULL handle plus a message": NOT_READY is an id the registry does not know
            # (the library never resolves it to another dialect), LOAD is a dictionary that would
            # not load. The library's own detail is passed on -- naming the failing files is its
            # job, not this shell's.
            detail = _ffi.take_detail(error, lib)
            where = f"cppp2_open for dialect {dialect_id!r} with dictionary root {root}"
            if error.code == _ffi.NOT_READY:
                suffix = f": {detail}" if detail else ""
                raise UnknownDialectError(
                    f"{where} failed with {_ffi.status_name(error.code)}{suffix}"
                )
            _ffi.raise_for_status(error.code, where, detail)
        # On success the contract says detail is NULL and nothing is freed; calling it anyway keeps
        # the ownership rule in one place instead of "free only when the call failed".
        _ffi.take_detail(error, lib)
        self._handle: Optional[int] = handle
        self._dialect_id = dialect_id
        self._dict_dir = root

    # -- introspection ------------------------------------------------------

    @staticmethod
    def default_dict_dir() -> Path:
        """The dictionary root used when no ``dict_dir`` is given.

        Precedence (D1 of docs/DECISIONS.md section 4.1): the
        ``CPP_PINYIN2_DICT_DIR`` environment variable -- a deployment override for containers, read-only
        images or a relocated application directory -- then the copy shipped inside this package. An empty
        value counts as unset: an exported-but-empty variable is a shell accident, not an instruction.

        The result is NOT checked for existence here. A caller that overrides the variable with a wrong
        directory must see the library's own diagnosis ("which directory, which file") instead of being
        silently handed the packaged copy, which would hide a broken deployment.

        The packaged path is deliberately a plain __file__-relative path rather than
        importlib.resources.files(). Measured on real interpreters: resources.files() exists only from
        Python 3.9, and it was the single thing pinning this package's floor there -- on 3.8 the whole
        suite failed with "module 'importlib.resources' has no attribute 'files'". With this line the same
        91 tests pass on 3.8.10 as on 3.9 through 3.13. The package is a regular package installed as
        ordinary files, which is the case this has to handle; nothing here claims to serve a zipimport or
        namespace-package layout.
        """
        override = os.environ.get(Engine.DICT_DIR_ENV, "")
        if override:
            return Path(override)
        return Path(__file__).resolve().parent / "data" / "dict"

    @staticmethod
    def dict_dir_candidate() -> Optional[Path]:
        """The dictionary root the NATIVE library was installed to, or ``None`` when it has no anchor.

        D2's candidate: the library reports its own module's neighbour
        (``<module dir>/../share/cpp-pinyin2/dict``), which is what lets a relocated application work without
        hard-coding a path. It is deliberately NOT consulted by :meth:`default_dict_dir`: a wheel carries its
        own copy and that copy is the right one for a wheel consumer. Use it to diagnose "the native library
        was installed somewhere other than this package".

        ``None`` means "no anchor" -- a static build, where the library is code inside the host, or a
        platform whose module lookup failed. The value is not checked for existence: it is a candidate.
        """
        pointer = _library().cppp2_dict_dir_candidate()
        return Path(_ffi.decode(pointer)) if pointer else None

    @property
    def dialect_id(self) -> str:
        """The dialect id this engine was created with."""
        return self._dialect_id

    @property
    def dict_dir(self) -> Path:
        """The dictionary root this engine was created with."""
        return self._dict_dir

    # -- lifecycle ----------------------------------------------------------

    def _require_handle(self) -> int:
        handle = self._handle
        if handle is None:
            raise ValueError("this Engine is closed")
        return handle

    def close(self) -> None:
        """Release the native handle. Idempotent: the slot is emptied, so the second call,
        and every call after it, never reaches ``cppp2_destroy`` a second time."""
        handle = self._handle
        if handle is not None:
            self._handle = None
            _library().cppp2_destroy(handle)

    def __enter__(self) -> "Engine":
        return self

    def __exit__(self, *exc_info) -> None:
        self.close()

    def __del__(self) -> None:
        # Best effort: at interpreter shutdown the ctypes module may already be gone, and an
        # exception here is noise the user cannot act on.
        try:
            self.close()
        except Exception:
            pass

    # -- queries ------------------------------------------------------------

    def query(
        self,
        text: str,
        *,
        style: Union[Style, int] = Style.DEFAULT,
        include_candidates: bool = False,
        include_reading: bool = False,
        v_to_u: bool = False,
        mark_neutral_as_5: bool = False,
        on_error: Union[OnError, int] = OnError.DEFAULT,
    ) -> List[Segment]:
        """Convert ``text`` into one :class:`Segment` per input span.

        With ``on_error=OnError.DROP`` a span that has no reading is dropped instead of
        marked: no segment comes back with a non-zero ``failure_kind`` and ``len(result)`` is
        *not* the number of input spans. An empty list is not an error.

        ``include_reading=True`` asks for the structural side (:class:`Syllable` rows): every
        non-failed span then carries exactly one, and a FAILED span carries none -- a failure has
        no structure to describe, and a failed span deliberately does not enter the array. The
        default is off, and "off" is not "this span has no reading": ``rendered`` carries the
        reading on every span either way.

        Two measured details worth knowing before relying on the defaults:

        * ``include_candidates=False`` still yields one candidate on a *failed* span -- the span's
          own text. The flag governs spans that converted successfully. (Measured 2026-10-01 while
          building the conformance fixture: ``query("abc你")`` returns ``"abc"`` with a non-zero
          ``failure_kind`` and ``candidates == ("abc",)``, then ``"你"`` with none.)
        * ``v_to_u`` reads backwards from its name. ü is written ``v`` *by default*, and setting
          ``v_to_u=True`` switches the output to ``ü`` -- ``Types.h`` states it ("ü 记作 v，除非
          vToU=true"). Cantonese ignores the switch entirely, so the same call on both dialects is
          only a fair comparison with it left alone.
        """
        return self._run_query(
            text,
            sandhi=None,
            style=style,
            on_error=on_error,
            include_candidates=include_candidates,
            include_reading=include_reading,
            v_to_u=v_to_u,
            mark_neutral_as_5=mark_neutral_as_5,
        )

    def _run_query(self, text, *, sandhi,
        style: Union[Style, int] = Style.DEFAULT,
        include_candidates: bool = False,
        include_reading: bool = False,
        v_to_u: bool = False,
        mark_neutral_as_5: bool = False,
        on_error: Union[OnError, int] = OnError.DEFAULT,
    ) -> List[Segment]:
        """One conversion, and optionally the Mandarin tone sandhi that has to run on the
        result while it is still alive (see :meth:`tone_sandhi`). Every parameter keeps the
        meaning documented on :meth:`query`; `sandhi` is None or (third_tone, bu, yi)."""
        lib = _library()
        handle = self._require_handle()
        raw = _encode_text(text, "text")
        opts = _checked_opts(self._handle, self._dialect_id,
            style,
            on_error=on_error,
            include_candidates=include_candidates,
            include_reading=include_reading,
            v_to_u=v_to_u,
            mark_neutral_as_5=mark_neutral_as_5,
        )

        out = ctypes.POINTER(_ffi.Result)()
        status = lib.cppp2_query(handle, raw, ctypes.byref(opts), ctypes.byref(out))
        _ffi.raise_for_status(status, "cppp2_query")
        if sandhi is not None:
            third_tone, bu, yi = sandhi
            sandhi_opts = _ffi.ToneSandhiOpts()
            # struct_size first, before the call: the library reads it to learn what this caller owns.
            sandhi_opts.struct_size = ctypes.sizeof(_ffi.ToneSandhiOpts)
            sandhi_opts.third_tone = 1 if third_tone else 0
            sandhi_opts.bu = 1 if bu else 0
            sandhi_opts.yi = 1 if yi else 0
            # On the LIVE result, before the copy-out below and before the finally-frees it.
            changed = ctypes.c_size_t()
            status = lib.cppp2_apply_tone_sandhi(handle, out, ctypes.byref(sandhi_opts), ctypes.byref(changed))
        try:
            # INSIDE the try on purpose: a refused pass still hands back a live result the caller
            # owns, and the `finally` below is what frees it. Raising from before the `try` leaked
            # the whole result -- and `cppp2_apply_tone_sandhi` refuses outright on a dialect with
            # no sandhi pass (Cantonese), so that was the common path, not a corner (F21).
            _ffi.raise_for_status(status, "cppp2_apply_tone_sandhi")
            result = out.contents
            count = int(result.count)
            items: List[Segment] = []
            for index in range(count):
                span = result.segments[index]
                candidates: Tuple[str, ...] = ()
                if span.candidate_count:
                    # The pointer is NULL when candidates were not requested; count is what
                    # says whether it may be dereferenced.
                    candidates = tuple(
                        _ffi.decode(span.candidates[i])
                        for i in range(int(span.candidate_count))
                    )
                syllables: Tuple[Syllable, ...] = ()
                if span.syllable_count:
                    # Same rule: NULL unless the query asked for the structure, and a failed span
                    # never enters the array -- so count, not failure_kind, decides dereferencing.
                    syllables = tuple(
                        Syllable(
                            initial=_ffi.decode(span.syllables[i].initial),
                            fin=_ffi.decode(span.syllables[i].fin),
                            spelling=_ffi.decode(span.syllables[i].spelling),
                            tone=int(span.syllables[i].tone),
                            tone_kind=int(span.syllables[i].tone_kind),
                            source=int(span.syllables[i].source),
                        )
                        for i in range(int(span.syllable_count))
                    )
                items.append(
                    Segment(
                        text=_ffi.decode(span.text),
                        rendered=_ffi.decode(span.rendered),
                        failure_kind=int(span.failure_kind),
                        candidates=candidates,
                        syllables=syllables,
                    )
                )
            return items
        finally:
            lib.cppp2_result_free(out)

    def tone_sandhi(
        self,
        text: str,
        *,
        style: Union[Style, int] = Style.DEFAULT,
        include_candidates: bool = False,
        include_reading: bool = False,
        v_to_u: bool = False,
        mark_neutral_as_5: bool = False,
        on_error: Union[OnError, int] = OnError.DEFAULT,
        third_tone: bool = True,
        bu: bool = True,
        yi: bool = True,
    ) -> List[Segment]:
        """Convert ``text``, then apply Mandarin tone sandhi to the result.

        The rules are NOT implemented here: this is ``query`` followed by the C entry point
        ``cppp2_apply_tone_sandhi``, which is the C face of ``core/ToneSandhi.h`` -- the one place the
        three rules (third-tone runs, bu, yi) and their two deliberate differences from pypinyin's
        ``contrib.tone_sandhi`` are written down. The three switches default to ON, the library's own
        default, and are always sent explicitly: passing NULL would test the C default rather than the
        value the caller asked for.

        SCOPED BY THE DIALECT. The pass is whatever this engine\'s dialect declares (Dialect::toneSandhi in
        C++); Mandarin is the only one today. Cantonese declares none -- jyutping readings are letters plus a
        tone digit too, so no shape test could tell them apart -- and asking for sandhi on a Cantonese engine
        raises CppPinyinError (CPPP2_ERR_INVALID_ARG) instead of rewriting the rows.

        What changes is ``rendered``, and only where the reading is spelled as letters plus a tone
        digit; the span count never changes. Everything else is documented on :meth:`query`.
        """
        return self._run_query(
            text,
            sandhi=(third_tone, bu, yi),
            style=style,
            on_error=on_error,
            include_candidates=include_candidates,
            include_reading=include_reading,
            v_to_u=v_to_u,
            mark_neutral_as_5=mark_neutral_as_5,
        )

    def render_syllable(
        self,
        reading: Syllable,
        *,
        style: Union[Style, int] = Style.DEFAULT,
        v_to_u: bool = False,
        mark_neutral_as_5: bool = False,
    ) -> str:
        """Render ONE structured reading in ``style`` -- ``cppp2_render_syllable``.

        This is why the shell does not implement a style itself: ``engine.render_syllable(s)`` is
        the string this engine's own dialect prints for ``s``, including the dialect's tone
        notation. Passing a reading from another dialect's engine would render the wrong bytes,
        which is why the dialect comes from this handle rather than from the reading.

        A style the dialect does not support is the library's normal contract: the reading comes
        back unchanged, not as an error. An empty string is a legitimate answer too (``INITIALS`` of
        a zero-initial syllable is ``""``), and is returned as such. An
        :class:`InvalidArgumentError` means the structure does not denote one syllable (a separator
        inside ``initial``/``fin``) or it carries neither a ``spelling`` nor a projection that can
        rebuild one -- passing a reading that came out of :meth:`query` always has the spelling.
        """
        lib = _library()
        handle = self._require_handle()
        if not isinstance(reading, Syllable):
            raise TypeError(f"reading must be a Syllable, not {type(reading).__name__}")
        for value, what in (
            (reading.initial, "initial"),
            (reading.fin, "fin"),
            (reading.spelling, "spelling"),
        ):
            if "\x00" in value:
                raise ValueError(f"{what} contains U+0000, which the C ABI would silently truncate")

        raw = _ffi.Syllable()
        # ctypes keeps the encoded bytes alive inside the struct, so the pointers stay valid for
        # as long as `raw` is referenced -- which is this whole call.
        raw.initial = reading.initial.encode("utf-8")
        raw.fin = reading.fin.encode("utf-8")
        raw.spelling = reading.spelling.encode("utf-8")
        raw.tone = int(reading.tone)
        raw.tone_kind = int(reading.tone_kind)
        raw.source = int(reading.source)

        opts = _checked_opts(self._handle, self._dialect_id, style, v_to_u=v_to_u, mark_neutral_as_5=mark_neutral_as_5)
        out = ctypes.c_char_p()
        status = lib.cppp2_render_syllable(
            handle, ctypes.byref(raw), ctypes.byref(opts), ctypes.byref(out)
        )
        _ffi.raise_for_status(status, "cppp2_render_syllable")
        try:
            return _ffi.decode(out)
        finally:
            lib.cppp2_string_free(out)

    def tone_convert(self, reading: str, style: "Style" = None) -> str:
        """``reading`` rendered in ``style`` -- the dialect's own converter, through the C ABI.

        Any notation this dialect reads goes in (``"ni3"``, ``"nǐ"``, ``"ni"``); a style the dialect does not
        implement comes back unchanged, which is the library's contract rather than an error. No v-to-u and no
        neutral-5: this converts styles, and changing the spelling would make it look like a different reading.
        """
        lib = _library()
        handle = self._require_handle()
        raw = _encode_text(reading, "reading")
        out = ctypes.c_char_p()
        status = lib.cppp2_tone_convert(handle, raw, int(style if style is not None else Style.DEFAULT),
                                        ctypes.byref(out))
        _ffi.raise_for_status(status, "cppp2_tone_convert")
        try:
            return _ffi.decode(out)
        finally:
            lib.cppp2_string_free(out)
    def readings(
        self,
        hanzi: str,
        *,
        style: Union[Style, int] = Style.DEFAULT,
        v_to_u: bool = False,
        mark_neutral_as_5: bool = False,
    ) -> List[str]:
        """Every reading of ``hanzi``, converted with ``style`` and with the two switches.

        The order is the library's (``core/Types.h`` -> ``Segment::candidates``): the dialect's own
        dictionary order, de-duplicated by the CONVERTED string keeping the first occurrence. The
        switches are therefore visible in this call's *count* as well as its spellings: ``的`` in
        TONE3 has no reading ending in a tone digit, and ``mark_neutral_as_5=True`` turns its first
        reading from ``de`` into ``de5`` (the tone its table row does not spell out), while ``女``
        answers ``['nv3', 'nü']`` instead of ``['nv3', 'nv']`` with ``v_to_u=True``.

        ``v_to_u`` / ``mark_neutral_as_5`` mean what they mean on :meth:`query` (they are masked by
        the dialect's capability flags, so Cantonese ignores them). They are accepted here because
        the C entry point honours them -- an older shell that did not pass them silently answered a
        different question than the C ABI did for the same arguments.

        The library reads one character (the first one); passing a longer string is not a
        sentence-level call.
        """
        lib = _library()
        handle = self._require_handle()
        raw = _encode_text(hanzi, "hanzi")
        opts = _checked_opts(
            self._handle,
            self._dialect_id,
            style,
            v_to_u=v_to_u,
            mark_neutral_as_5=mark_neutral_as_5,
        )

        out_list = ctypes.POINTER(ctypes.c_char_p)()
        out_count = ctypes.c_size_t()
        status = lib.cppp2_readings(
            handle, raw, ctypes.byref(opts), ctypes.byref(out_list), ctypes.byref(out_count)
        )
        _ffi.raise_for_status(status, "cppp2_readings")
        try:
            return [_ffi.decode(out_list[i]) for i in range(int(out_count.value))]
        finally:
            lib.cppp2_string_list_free(out_list, out_count)

    def to_simplified(self, text: str) -> str:
        """Traditional to simplified, for the FIRST character of ``text``."""
        lib = _library()
        handle = self._require_handle()
        raw = _encode_text(text, "text")
        out = ctypes.c_char_p()
        status = lib.cppp2_to_simplified(handle, raw, ctypes.byref(out))
        _ffi.raise_for_status(status, "cppp2_to_simplified")
        try:
            return _ffi.decode(out)
        finally:
            lib.cppp2_string_free(out)

    def is_polyphonic(self, hanzi: str) -> bool:
        """True when the FIRST character of ``hanzi`` starts a phrase in this dialect's corpus.

        Not "has several readings of its own": a single-reading character that opens a phrase
        answers True. The authority is the ``Engine::isPolyphonic`` comment in the C++ header.
        """
        lib = _library()
        handle = self._require_handle()
        raw = _encode_text(hanzi, "hanzi")
        out = ctypes.c_int32()
        status = lib.cppp2_is_polyphonic(handle, raw, ctypes.byref(out))
        _ffi.raise_for_status(status, "cppp2_is_polyphonic")
        return bool(out.value)

    # -- dictionaries -------------------------------------------------------

    def load_user_dict(
        self, path: Union[str, os.PathLike], clear_existing: bool = True
    ) -> None:
        """Load a user dictionary (``word:reading[,reading...]``) into this engine.

        The path is pre-checked for entries and for keys longer than
        :data:`MAX_USER_DICT_KEY`, because the library would otherwise load a zero-entry file
        successfully and truncate an over-long key.

        This MUTATES the engine: it must not run concurrently with any read (``query``,
        ``readings``, ``to_simplified``, ``is_polyphonic``) on the same engine -- that is a
        data race, not thread-safe, and the C++ contract says so as well.
        """
        self._load_dict("cppp2_load_user_dict", path, clear_existing)

    def load_traditional_dict(
        self, path: Union[str, os.PathLike], clear_existing: bool = True
    ) -> None:
        """Load a traditional-to-simplified table into this engine.

        This MUTATES the engine: it must not run concurrently with any read on the same
        engine (a data race, not thread-safe), and the same path pre-check as
        :meth:`load_user_dict` applies.
        """
        self._load_dict("cppp2_load_traditional_dict", path, clear_existing)

    def _load_dict(self, entry_point: str, path: Union[str, os.PathLike], clear: bool) -> None:
        lib = _library()
        handle = self._require_handle()
        target = Path(os.fspath(path))
        if not target.is_file():
            raise CppPinyinError(f"dictionary file does not exist: {target}")
        _check_dict_file(target)
        call = getattr(lib, entry_point)
        status = call(handle, _encode_path(target, "path"), 1 if clear else 0)
        _ffi.raise_for_status(status, entry_point, str(target))


def dialect_ids() -> List[str]:
    """The dialects the library has registered, in registry order.

    Read from the library rather than hardcoded, so a shell cannot drift from the engine set.
    The registry argument is NULL: this shell has no C++ host to hand it one, and NULL is the
    header's own way of saying "the library's built-ins".
    """
    lib = _library()
    out_list = ctypes.POINTER(ctypes.c_char_p)()
    out_count = ctypes.c_size_t()
    status = lib.cppp2_dialect_ids(None, ctypes.byref(out_list), ctypes.byref(out_count))
    _ffi.raise_for_status(status, "cppp2_dialect_ids")
    try:
        return [_ffi.decode(out_list[i]) for i in range(int(out_count.value))]
    finally:
        lib.cppp2_string_list_free(out_list, out_count)


def version() -> str:
    """The library's version, as reported by ``cppp2_version()``.

    Must equal :data:`__version__`: both come from the single source in ``CMakeLists.txt``.
    """
    return _ffi.decode(_library().cppp2_version())
