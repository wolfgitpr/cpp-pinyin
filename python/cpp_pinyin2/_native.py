"""Locate and load the native library that ships inside this package.

The file name is never hardcoded (decision D20=A): MinGW produces ``libcpp-pinyin2.dll``,
MSVC produces ``cpp-pinyin2.dll``, Linux ``libcpp-pinyin2.so``, macOS ``libcpp-pinyin2.dylib``.
The build script copies whatever it built into ``cpp_pinyin2/_native/`` and records the actual
name in ``_native/name.txt``, so the loader re-derives it instead of guessing.
"""

from __future__ import annotations

import ctypes
from pathlib import Path
from typing import List

from ._errors import CppPinyinError

NAME_FILE = "name.txt"
_SUFFIXES = (".dll", ".so", ".dylib")


def native_dir() -> Path:
    """``cpp_pinyin2/_native`` as installed (next to this file)."""
    return Path(__file__).resolve().parent / "_native"


def library_path() -> Path:
    """The recorded native library, or a diagnosable error -- never a guess."""
    directory = native_dir()
    if not directory.is_dir():
        raise CppPinyinError(
            f"native library directory is missing: {directory}. This package was not "
            f"assembled by python/tools/build_native.py."
        )

    recorded = directory / NAME_FILE
    if recorded.is_file():
        name = recorded.read_text(encoding="utf-8").strip()
        if not name:
            raise CppPinyinError(f"{recorded} is empty: it must record the built library's name.")
        path = directory / name
        if not path.is_file():
            # A recorded name that is not there means a broken package. Scanning for
            # something else would hide exactly the failure the record exists to catch.
            raise CppPinyinError(
                f"{recorded} records {name!r}, but {path} does not exist. "
                f"The package is inconsistent; rebuild it with python/tools/build_native.py."
            )
        return path

    candidates: List[Path] = sorted(
        f for f in directory.iterdir() if f.is_file() and f.suffix.lower() in _SUFFIXES
    )
    if len(candidates) == 1:
        return candidates[0]
    raise CppPinyinError(
        f"{recorded} is missing and {directory} holds {len(candidates)} library-looking "
        f"files ({', '.join(c.name for c in candidates) or 'none'}), so the name cannot be "
        f"re-derived. Rebuild with python/tools/build_native.py."
    )


def load_library() -> ctypes.CDLL:
    """Load the recorded library and declare the ABI on it."""
    from . import _ffi

    path = library_path()
    try:
        lib = ctypes.CDLL(str(path))
    except OSError as exc:
        # On Windows a missing *dependency* is reported as if the library itself were
        # missing: ctypes does not search PATH for a DLL's dependencies. That is why the
        # build links the MinGW runtime statically (docs/PLUGIN_INTERFACE_AND_BINDINGS.md
        # section 6.5, item 6); this message is what the user gets if that ever regresses.
        raise CppPinyinError(
            f"could not load {path}: {exc}. If this is Windows, the library may have an "
            f"unresolved dependency (ctypes does not search PATH for dependencies, so a "
            f"non-statically-linked runtime shows up as 'the file is missing')."
        ) from exc
    _ffi.declare(lib)
    return lib
