"""Shared plumbing for the Python shell's tests.

The package is imported from the source tree (``python/cpp_pinyin2/``), not from an installed
wheel: these tests exercise the shell, while the packaging is checked by
``python/tools/check_wheel.py``.

This is a conftest rather than a note in a README because ``import cpp_pinyin2`` otherwise depends
on how pytest happens to be invoked: ``python -m pytest`` from ``python/`` works by accident (the
cwd lands on sys.path), a bare ``pytest`` from the repository root does not.
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Optional

import pytest

PYTHON_ROOT = Path(__file__).resolve().parents[1]
if str(PYTHON_ROOT) not in sys.path:
    sys.path.insert(0, str(PYTHON_ROOT))


def _find_repo_root() -> Optional[Path]:
    """The checkout root, recognised by files whose job is to sit at the root.

    This used to test for ``include/cpp-pinyin2/ext/c_api.h`` as well. That was the mistake the layout
    batch had to remove twice over: the moment the batch regroups the public headers, the probe stops
    recognising the checkout, and every test that skips outside a checkout -- the five contract tests
    in ``test_contract.py`` -- starts skipping *inside* one too. Skipped is not failed, so CI would
    have stayed green while those tests quietly stopped existing. CMakeLists.txt and the packaging
    script are root furniture; a header move cannot turn them off.
    """
    for candidate in (PYTHON_ROOT, *PYTHON_ROOT.parents):
        if ((candidate / "CMakeLists.txt").is_file()
                and (candidate / "python" / "tools" / "build_native.py").is_file()):
            return candidate
    return None


REPO_ROOT = _find_repo_root()


@pytest.fixture(scope="session")
def repo_root() -> Path:
    """The checkout root.

    Tests that need the single version source or the C headers are skipped rather than failed when
    the suite runs outside a checkout: the skip is environment-shaped (the file is genuinely
    absent), never capability-shaped, and inside the repository it never happens.
    """
    if REPO_ROOT is None:
        pytest.skip(
            "not a checkout: CMakeLists.txt and python/tools/build_native.py are absent, "
            "so this run is against an installed wheel"
        )
    return REPO_ROOT
