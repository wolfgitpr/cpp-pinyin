"""Exception hierarchy for the cpp-pinyin2 Python shell.

Every non-``CPPP2_OK`` status becomes one of these. A shell must never turn a failure into an
empty result or a partial one (`docs/DECISIONS.md` §2.1).
The mapping lives in :func:`cpp_pinyin2._ffi.raise_for_status` and ends with a fallback branch,
so an unknown status raises rather than passing as success.

The names are the C ABI's value domain (``CPPP2_OK`` / ``CPPP2_ERR_*``, numerically
``cpp_pinyin2::v2::Code`` 0..5); the sentence for a code is not written here -- it is
:func:`cpp_pinyin2.code_text`, which asks the library.
"""

from __future__ import annotations


class CppPinyinError(Exception):
    """Base class for every error this package raises."""


class InvalidArgumentError(CppPinyinError, ValueError):
    """``CPPP2_ERR_INVALID_ARG`` -- a required argument was NULL or empty.

    Also what a span's own ``failure_kind`` carries when the library cannot read that span, and
    what ``cppp2_render_syllable`` returns for a structure that does not denote one syllable.
    """


class NotReadyError(CppPinyinError):
    """``CPPP2_ERR_NOT_READY`` -- the handle exists but is not usable.

    Not reachable through :class:`cpp_pinyin2.Engine` today: ``cppp2_open`` publishes only ready
    handles (an id it cannot resolve is reported as a failure of the call, not as a broken
    handle). Mapped anyway -- a shell that cannot express this status would have to guess what
    the library meant.
    """


class DictionaryError(CppPinyinError):
    """``CPPP2_ERR_LOAD`` -- the path was readable but the table did not load."""


class DictionaryIncompatibleError(CppPinyinError):
    """``CPPP2_ERR_DICT_INCOMPATIBLE`` -- the shipped data is NEWER than this library understands.

    The engine reads the dialect's ``format.txt`` before any table and refuses a value above its
    own ``kSupportedDictFormat`` instead of half-reading the data. The action differs from
    :class:`DictionaryError` (nothing is wrong with the file -- the library is old), which is why
    the C ABI gives it a code of its own and the header says a host can act on it.
    """


class InternalError(CppPinyinError):
    """``CPPP2_ERR_INTERNAL`` -- an exception was caught at the C boundary, or a marshal failed."""


class UnknownDialectError(NotReadyError):
    """``CPPP2_ERR_NOT_READY`` from ``cppp2_open``: the id is not in the registry.

    Reachable since the ABI's entry point reports an unknown id as a status of its own, instead
    of a NULL handle plus a message: the library never resolves an unknown id to another dialect,
    and ``cppp2_open``'s ``detail`` names the id (the shell carries that detail into its message).
    It is a :class:`NotReadyError` because it IS that status -- the refined name is what lets a
    caller catch this case without catching every not-ready failure.
    """
