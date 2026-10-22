"""ctypes declarations for the cpp-pinyin2 C ABI (``include/cpp-pinyin2/ext/c_api.h``).

This file is a transcription of that header and nothing else: no engine behaviour is
reimplemented here (docs/PLUGIN_INTERFACE_AND_BINDINGS.md section 6.8.1, rule 1).

Every entry point gets explicit ``argtypes``/``restype`` -- not just the ones that return
pointers. ctypes defaults ``restype`` to ``c_int``, so a pointer-returning function declared
without it is truncated to 32 bits on a 64-bit host and silently becomes a wild pointer.

The surface is the frozen ``cppp_*`` symbols of the header; the list and its count live in
tests/abi/expected_c_api_symbols.txt, so this module repeats neither.
``cppp_create`` is deliberately absent: the entry point that took a dialect id with no registry and
reported failure as "NULL handle plus a message" was replaced by :func:`cppp2_open`, whose status
code distinguishes an unknown dialect (``CPPP2_ERR_NOT_READY``) from a dictionary that would not
load (``CPPP2_ERR_LOAD``) -- the two failures the old shape made indistinguishable.
"""

from __future__ import annotations

import ctypes

from ._errors import (
    CppPinyinError,
    DictionaryError,
    DictionaryIncompatibleError,
    InternalError,
    InvalidArgumentError,
    NotReadyError,
)

# --- status codes (the CPPP2_ERR_* enum in c_api.h) -------------------------
OK = 0
INVALID_ARG = 1
NOT_READY = 2
LOAD = 3
INTERNAL = 4
#: The data is newer than this library understands (the dialect's `format.txt` says a format
#: generation above `kSupportedDictFormat`). A code of its own because the ACTION differs: nothing
#: is wrong with the file, the library is old -- upgrade it, or point at older data.
DICT_INCOMPATIBLE = 5

_NAMES = {
    OK: "CPPP2_OK",
    INVALID_ARG: "CPPP2_ERR_INVALID_ARG",
    NOT_READY: "CPPP2_ERR_NOT_READY",
    LOAD: "CPPP2_ERR_LOAD",
    INTERNAL: "CPPP2_ERR_INTERNAL",
    DICT_INCOMPATIBLE: "CPPP2_ERR_DICT_INCOMPATIBLE",
}

# --- the two value domains of cppp2_syllable (see c_api.h's cppp2_syllable) -
TONE_KIND_UNSPECIFIED = 0
TONE_KIND_BASE = 1
TONE_KIND_SANDHI = 2

SOURCE_DICTIONARY = 0
SOURCE_PHRASE = 1
SOURCE_USER_DICT = 2
SOURCE_PLUGIN = 3


def status_name(status: int) -> str:
    return _NAMES.get(status, f"unknown CPPP_ status {status}")


def raise_for_status(status: int, where: str, detail: str = "") -> None:
    """Turn a C status into an exception. Success is an explicit ``status == OK``.

    The fallback branch matters: an unrecognised code must not fall through as success
    (that is how "returned nothing" becomes "returned nothing and nobody noticed"), and it
    must not raise ``KeyError`` from a dict lookup either.

    The same value domain covers ``cppp2_error::code``, so this is also what a ``cppp2_open``
    failure is turned into -- with one refinement in :mod:`cpp_pinyin2`, which maps
    ``NOT_READY`` (an id the registry does not know) to
    :class:`~cpp_pinyin2.UnknownDialectError`.
    """
    if status == OK:
        return
    suffix = f": {detail}" if detail else ""
    message = f"{where} failed with {status_name(status)}{suffix}"
    if status == INVALID_ARG:
        raise InvalidArgumentError(message)
    if status == NOT_READY:
        raise NotReadyError(message)
    if status == LOAD:
        raise DictionaryError(message)
    if status == INTERNAL:
        raise InternalError(message)
    if status == DICT_INCOMPATIBLE:
        raise DictionaryIncompatibleError(message)
    raise CppPinyinError(message)


# --- structures -------------------------------------------------------------


class QueryOpts(ctypes.Structure):
    """``cppp2_query_opts``: caller-owned, the ONE struct that grows.

    ``struct_size`` must be written *before* ``cppp2_query_opts_init`` is called, because init
    only writes fields inside the size the caller declared.

    ``include_reading`` is APPENDED after every field that existed before it (c_api.h's "HOW
    THIS STRUCT GROWS"): a shell built against the older, shorter declaration keeps reading and
    writing the fields it declared at the same offsets, and simply does not see the new one.
    Inserting a field here instead of appending it would move ``v_to_u``/``mark_neutral_as_5``
    under any pre-existing caller's feet.

    "APPENDED" MEANS AFTER THE OLD ``sizeof``, not merely after the old last member (S9c), so
    ``reserved_0`` -- which claims the four bytes of TRAILING PADDING the first, 32-byte
    declaration of this struct ended in -- comes first: this shell is 40 bytes, ``reserved_0``
    is at 28 and ``include_reading`` at 32. Put at 28 instead, the option lands inside the bytes
    an older caller's ``struct_size = 32`` already covers, and the library reads that caller's
    uninitialised stack word as "build the structure". The member has no meaning to set: the
    library neither reads nor writes it.
    """

    _fields_ = [
        ("struct_size", ctypes.c_size_t),
        ("style", ctypes.c_int32),
        ("on_error", ctypes.c_int32),
        ("include_candidates", ctypes.c_int32),
        ("v_to_u", ctypes.c_int32),
        ("mark_neutral_as_5", ctypes.c_int32),
        # RESERVED -- the claim on the padding described above. Never an option.
        ("reserved_0", ctypes.c_int32),
        ("include_reading", ctypes.c_int32),
        # RESERVED -- same claim, one declaration later: 36..39 is the trailing padding of a
        # 40-byte struct, and claiming it is what keeps the next option out of bytes an older
        # caller's ``struct_size = 40`` already covers (S9c). Never an option.
        ("reserved_1", ctypes.c_int32),
    ]


class Syllable(ctypes.Structure):
    """``cppp2_syllable``: a STRUCTURED reading (data, never a rendering).

    ``initial`` is "" for a zero-initial syllable; ``fin`` is the canonical final. The pair is a
    projection of the table reading, so it does not always rebuild the spelling the table stores --
    ``spelling`` is that spelling, verbatim, and is what ``cppp2_render_syllable`` renders when a
    caller hands the structure back. No ``struct_size``: the library makes no cross-version
    compatibility promise at all (rule 6 is deleted), so the layout is free to follow the header.
    """

    _fields_ = [
        ("initial", ctypes.c_char_p),
        ("fin", ctypes.c_char_p),
        ("spelling", ctypes.c_char_p),
        ("tone", ctypes.c_int32),
        ("tone_kind", ctypes.c_int32),
        ("source", ctypes.c_int32),
        ("reserved_0", ctypes.c_int32),  # claims the trailing hole the C compiler used to insert
    ]


class Segment(ctypes.Structure):
    """``cppp2_segment``: one input span and what the engine read for it.

    The field order is the header's. ``failure_kind`` used to be followed by four bytes of compiler
    padding on a 64-bit host; that hole is now claimed by ``reserved_0`` (S9c), so a future field can
    be appended without changing ``sizeof`` -- and an older consumer's array stride keeps working.
    The order must still stay identical to c_api.h rather than merely "the same fields".

    ``rendered`` is the ONE rendered string of this span (it equals ``text`` when the span
    failed). ``failure_kind`` is a status code -- numerically a ``cpp_pinyin2::v2::Code`` -- and
    ``syllables``/``syllable_count`` are the structural side, EMPTY both when the caller did not
    ask for it (``include_reading == 0``) and when the span failed. Read ``failure_kind``, never
    "is the structure filled", to decide whether a span failed.
    """

    _fields_ = [
        ("text", ctypes.c_char_p),
        ("rendered", ctypes.c_char_p),
        ("failure_kind", ctypes.c_int32),
        ("reserved_0", ctypes.c_int32),  # claims the hole before the pointer, so no padding is implicit
        ("syllables", ctypes.POINTER(Syllable)),
        ("syllable_count", ctypes.c_size_t),
        ("candidate_count", ctypes.c_size_t),
        ("candidates", ctypes.POINTER(ctypes.c_char_p)),
    ]


class Result(ctypes.Structure):
    """``cppp2_result``. Everything reachable from it dies with ``cppp2_result_free``."""

    _fields_ = [
        ("count", ctypes.c_size_t),
        ("segments", ctypes.POINTER(Segment)),
    ]


class Error(ctypes.Structure):
    """``cppp2_error``: the entry points' optional error channel.

    ``detail`` is ``POINTER(c_char)`` rather than ``c_char_p`` on purpose. A ``c_char_p`` field
    reads back as a Python ``bytes`` object, and the address of the library's allocation -- the
    one thing ``cppp2_string_free`` needs -- would be gone: passing those bytes back would hand
    the library a pointer into CPython's own buffer. Owned by the CALLER, NULL on success, and
    released by :func:`take_detail` (or by nothing, if the caller never looks at it -- the
    library writes both fields before returning, so a caller never pre-initialises the struct).
    """

    _fields_ = [
        ("code", ctypes.c_int32),
        ("reserved_0", ctypes.c_int32),  # claims the hole before `detail`
        ("detail", ctypes.POINTER(ctypes.c_char)),
    ]


def take_detail(err: "Error", lib: ctypes.CDLL) -> str:
    """Decode ``err.detail`` and release it -- the struct's ownership rule, applied once.

    Safe to call on success (``detail`` is NULL, nothing is freed, "" comes back), so a caller
    does not have to branch before it can clean up.
    """
    detail = decode(err.detail)
    if err.detail:
        lib.cppp2_string_free(err.detail)
        err.detail = None
    return detail


# --- declarations -----------------------------------------------------------

_STR = ctypes.c_char_p


class Style(ctypes.Structure):
    """``cppp2_style``: one row of a dialect's own style table -- the same three field names as the
    C++ ``StyleDecl`` (name / alias / isDefault).

    ``is_default`` is 1 for the dialect's own default, else 0: the C ABI carries no ``bool``. The rows
    belong to the library -- release them with ``cppp2_style_list_free``."""

    _fields_ = [
        ("name", _STR),
        ("alias", ctypes.c_int32),
        ("is_default", ctypes.c_int32),
    ]

class ToneSandhiOpts(ctypes.Structure):
    """``cppp2_tone_sandhi_opts``: caller-owned, three switches, and the library's default for all
    three is ON (1).

    ``struct_size`` is written by the CALLER before the call, exactly like ``cppp2_query_opts``: it is
    how the library knows which fields the caller's own header actually declares, so a shell built
    against an older ``c_api.h`` still gets a defined answer instead of a field it never set.

    This is MANDARIN only -- see ``Engine.tone_sandhi``."""

    _fields_ = [
        ("struct_size", ctypes.c_size_t),
        ("third_tone", ctypes.c_int32),
        ("bu", ctypes.c_int32),
        ("yi", ctypes.c_int32),
        # Claims the trailing padding, as c_api.h:357-359 states: the three switches end at 20 while
        # sizeof is 24, and an unpinned four-byte hole is where a later field would silently land.
        ("reserved_0", ctypes.c_int32),
    ]


def declare(lib: ctypes.CDLL) -> None:
    """Declare all 20 entry points of the ABI on ``lib``.

    ``cppp2_open``'s first argument is the nullable ``const cppp2_registry *``: the shell has no
    C++ host to hand it one, so it passes NULL and gets the library's own two built-ins -- which
    is exactly what the header says NULL means.
    """
    lib.cppp2_version.restype = _STR
    lib.cppp2_version.argtypes = []

    # D2: the native build's installed dictionary root, or NULL when the build has no anchor (static).
    # Not part of `Engine.default_dict_dir()` -- a wheel carries its own copy, and that copy wins there.
    lib.cppp2_dict_dir_candidate.restype = _STR
    lib.cppp2_dict_dir_candidate.argtypes = []

    lib.cppp2_string_free.restype = None
    # c_void_p, not c_char_p: the free functions are also handed the raw char* of `cppp2_error.detail`
    # and the strlen-free `char**` of `cppp2_string_list_free`. Pointers are the only thing all three
    # call sites have in common, and ctypes converts any pointer instance (including c_char_p) to it.
    lib.cppp2_string_free.argtypes = [ctypes.c_void_p]

    lib.cppp2_string_list_free.restype = None
    lib.cppp2_string_list_free.argtypes = [ctypes.POINTER(_STR), ctypes.c_size_t]
    lib.cppp2_style_list.restype = ctypes.c_int32
    lib.cppp2_style_list.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.POINTER(Style)),
        ctypes.POINTER(ctypes.c_size_t),
    ]

    lib.cppp2_style_list_free.restype = None
    lib.cppp2_style_list_free.argtypes = [ctypes.POINTER(Style), ctypes.c_size_t]


    lib.cppp2_code_text.restype = _STR
    lib.cppp2_code_text.argtypes = [ctypes.c_int32]

    lib.cppp2_open.restype = ctypes.c_void_p
    lib.cppp2_open.argtypes = [
        ctypes.c_void_p,
        _STR,
        _STR,
        ctypes.POINTER(Error),
    ]

    lib.cppp2_destroy.restype = None
    lib.cppp2_destroy.argtypes = [ctypes.c_void_p]

    lib.cppp2_dialect_ids.restype = ctypes.c_int32
    lib.cppp2_dialect_ids.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.POINTER(_STR)),
        ctypes.POINTER(ctypes.c_size_t),
    ]

    lib.cppp2_query_opts_init.restype = None
    lib.cppp2_query_opts_init.argtypes = [ctypes.POINTER(QueryOpts)]

    lib.cppp2_query.restype = ctypes.c_int32
    lib.cppp2_query.argtypes = [
        ctypes.c_void_p,
        _STR,
        ctypes.POINTER(QueryOpts),
        ctypes.POINTER(ctypes.POINTER(Result)),
    ]

    lib.cppp2_result_free.restype = None
    lib.cppp2_result_free.argtypes = [ctypes.POINTER(Result)]

    lib.cppp2_apply_tone_sandhi.restype = ctypes.c_int32
    lib.cppp2_apply_tone_sandhi.argtypes = [
        # The engine handle comes first, and it is NOT decoration: tone sandhi is a dialect property, so the
        # library asks the dialect whether it declares a pass instead of applying the Mandarin rules to any
        # result. A dialect without one answers CPPP2_ERR_INVALID_ARG and leaves the rows alone.
        ctypes.c_void_p,
        ctypes.POINTER(Result),
        ctypes.POINTER(ToneSandhiOpts),
        # May be NULL ("do not report the count"); the return value is a STATUS, like every other
        # entry point here -- the count the C++ function returns travels through this out-parameter.
        ctypes.POINTER(ctypes.c_size_t),
    ]

    lib.cppp2_render_syllable.restype = ctypes.c_int32
    lib.cppp2_render_syllable.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(Syllable),
        ctypes.POINTER(QueryOpts),
        ctypes.POINTER(_STR),
    ]

    lib.cppp2_readings.restype = ctypes.c_int32
    lib.cppp2_readings.argtypes = [
        ctypes.c_void_p,
        _STR,
        ctypes.POINTER(QueryOpts),
        ctypes.POINTER(ctypes.POINTER(_STR)),
        ctypes.POINTER(ctypes.c_size_t),
    ]

    lib.cppp2_to_simplified.restype = ctypes.c_int32
    lib.cppp2_to_simplified.argtypes = [ctypes.c_void_p, _STR, ctypes.POINTER(_STR)]

    lib.cppp2_tone_convert.restype = ctypes.c_int32
    lib.cppp2_tone_convert.argtypes = [ctypes.c_void_p, _STR, ctypes.c_int32, ctypes.POINTER(_STR)]

    lib.cppp2_is_polyphonic.restype = ctypes.c_int32
    lib.cppp2_is_polyphonic.argtypes = [
        ctypes.c_void_p,
        _STR,
        ctypes.POINTER(ctypes.c_int32),
    ]

    lib.cppp2_load_user_dict.restype = ctypes.c_int32
    lib.cppp2_load_user_dict.argtypes = [ctypes.c_void_p, _STR, ctypes.c_int32]

    lib.cppp2_load_traditional_dict.restype = ctypes.c_int32
    lib.cppp2_load_traditional_dict.argtypes = [ctypes.c_void_p, _STR, ctypes.c_int32]

    # The `_ex` spellings (2026-10-20, audit H04): the same two loads plus the error channel. Declared here
    # because tests/test_contract.py::test_ffi_declares_exactly_the_c_api_surface compares this module's
    # `lib.cppp2_*` set against the frozen list as SETS -- a symbol the C face has and this module lacks is
    # exactly the gap that test exists to catch. The public Python methods keep calling the short names:
    # their failures already carry `path` from a pre-load check, and switching them is a separate decision.
    lib.cppp2_load_user_dict_ex.restype = ctypes.c_int32
    lib.cppp2_load_user_dict_ex.argtypes = [ctypes.c_void_p, _STR, ctypes.c_int32, ctypes.POINTER(Error)]

    lib.cppp2_load_traditional_dict_ex.restype = ctypes.c_int32
    lib.cppp2_load_traditional_dict_ex.argtypes = [ctypes.c_void_p, _STR, ctypes.c_int32, ctypes.POINTER(Error)]


def decode(value) -> str:
    """Decode a string the library handed out (``bytes``, a pointer) or a NULL (``None``).

    Three shapes reach here and all three mean "a NUL-terminated UTF-8 string": ``c_char_p``
    fields come back as ``bytes``, ``POINTER(c_char)`` fields (and the ``c_char_p`` instances
    used as out-parameters) come back as pointers, and an out-parameter that failed comes back
    as ``None`` or a NULL pointer.
    """
    if value is None:
        return ""
    if isinstance(value, bytes):
        return value.decode("utf-8")
    if not value:
        return ""
    return ctypes.string_at(value).decode("utf-8")
