"""Build script for the `cpp-pinyin2` Python wheel.

The wheel is not pure Python even though it contains no extension module: it carries a native
library that is loaded through ctypes at runtime. That distinction is the reason this file exists.

Measured recipe (the wheel was built this way on 2026-10-23):

    python -m pip wheel . --no-build-isolation --no-deps -w dist

``python -m build`` is deliberately not used. ``pip wheel`` has no ``--plat-name`` option, so the
platform tag comes from :func:`platform_tag` below -- and it must never be ``any``: a
``py3-none-any`` wheel installs on every platform and then fails at import time on all but the one
it was built for.
"""

from __future__ import annotations

import os
import re
import sysconfig
from pathlib import Path
from typing import Tuple

from setuptools import setup

try:
    # setuptools >= 70.1 ships its own bdist_wheel, so the separate `wheel` distribution is not
    # required (measured: setuptools 70.2.0 with no `wheel` package installed at all).
    from setuptools.command.bdist_wheel import bdist_wheel
except ImportError:  # pragma: no cover - depends on the ambient toolchain
    from wheel.bdist_wheel import bdist_wheel

HERE = Path(__file__).resolve().parent
VERSION_FILE = HERE / "cpp_pinyin2" / "_version.py"
_VERSION_RE = re.compile(r'^__version__\s*=\s*"([^"]+)"', re.MULTILINE)

PLATFORM_TAG_ENV = "CPP_PINYIN_PLATFORM_TAG"


def dynamic_version() -> str:
    """The version written by ``python/tools/build_native.py``, which read it from the single source."""
    if not VERSION_FILE.is_file():
        raise SystemExit(
            f"FATAL: {VERSION_FILE} is missing. Run `python python/tools/build_native.py` first: "
            "the version has exactly one source (the root CMakeLists.txt `project(... VERSION)`) "
            "and it is written into that file during the native build."
        )
    match = _VERSION_RE.search(VERSION_FILE.read_text(encoding="utf-8"))
    if not match:
        raise SystemExit(f"FATAL: no __version__ assignment found in {VERSION_FILE}")
    return match.group(1)


def platform_tag() -> str:
    """The wheel's platform tag.

    Windows and macOS report a usable tag straight from sysconfig (``win_amd64``,
    ``macosx_10_14_x86_64``). Linux reports glibc-plus-architecture (``linux_x86_64``), which is not
    a distributable tag -- the manylinux policy tag has to come from auditwheel or from the build
    script, so an explicit override is accepted here. A bare local Linux build therefore stays
    honestly unprefixed instead of claiming a manylinux level nobody verified.
    """
    explicit = os.environ.get(PLATFORM_TAG_ENV, "").strip()
    if explicit:
        return explicit
    return sysconfig.get_platform().replace("-", "_").replace(".", "_")


class BDistWheel(bdist_wheel):
    """Force ``py3-none-<platform>`` and keep the real platform.

    Both overrides are required and they are not interchangeable:

    * ``finalize_options`` sets ``root_is_pure = True``, because no CPython ABI is involved (the
      native library is opened with ctypes, never linked against the interpreter);
    * ``get_tag`` supplies the platform. With ``root_is_pure`` set and no ``--plat-name``
      available through ``pip wheel``, wheel would report ``py3-none-any`` -- the one wheel that
      installs everywhere and works almost nowhere.
    """

    def finalize_options(self) -> None:
        super().finalize_options()
        self.root_is_pure = True

    def get_tag(self) -> Tuple[str, str, str]:
        super().get_tag()  # keep wheel's own validation and plumbing in the loop
        # An explicit --plat-name WINS over the build host's tag (2026-10-20). Before this, the
        # option was accepted by the command line and then silently ignored here: a cross-build, or a
        # release that pins the manylinux level, got the host's sysconfig tag anyway -- with no error, and
        # the only trace was the wheel's FILE NAME, which nobody compares against their intent.
        if self.plat_name:
            return ("py3", "none", self.plat_name.replace("-", "_").replace(".", "_"))
        return ("py3", "none", platform_tag())


def long_description() -> str:
    """The repository README, so an index does not render this project as a blank page.

    Measured before this was added: the built wheel's METADATA carried the header fields and an empty body,
    and an empty body is what an index shows as no description at all. The README is read from the repository
    root and is deliberately optional -- a build from an unpacked sdist has no README beside setup.py, and a
    packaging script that dies there would be a worse failure than a short description.
    """
    readme = Path(__file__).resolve().parents[1] / "README.md"
    if readme.is_file():
        return readme.read_text(encoding="utf-8")
    return (
        "Chinese (Mandarin + Cantonese) pinyin conversion driven by a native C++ engine. "
        "See the repository for the full description."
    )


# The two licence files live at the repository root, and setuptools only honours a `../` entry in
# `license_files` when the build runs from the repository root -- the manylinux script does `cd python` first,
# and the wheel it produced had no <dist-info>/LICENSE at all (measured on the first CI run, 2026-10-22, job
# "Python wheel (linux aarch64)"). Staging copies next to setup.py fixes that without a second authority: the
# copies are build artefacts, gitignored, and regenerated on every build.
for _licence in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
    _source = Path(__file__).resolve().parents[1] / _licence
    _target = Path(__file__).resolve().parent / _licence
    if _source.is_file():
        _target.write_bytes(_source.read_bytes())


setup(
    name="cpp-pinyin2",
    version=dynamic_version(),
    description=(
        "Chinese (Mandarin + Cantonese) pinyin conversion driven by a native C++ engine"
    ),
    long_description=long_description(),
    long_description_content_type="text/markdown",
    # The wheel ships THREE licences' worth of material, so it says so (2026-10-21, user decision C): the code is
    # Apache-2.0 (LICENSE), the Mandarin tables are CC-CEDICT under CC BY-SA 4.0 and the Cantonese tables are
    # CC-Canto under CC BY-SA 3.0 -- each dialect's own License.txt says which, and they differ. Before this the
    # field named Apache-2.0 alone, which understated both data sets; the C# package had the same bug (and, before
    # round 33, said MIT outright). An SPDX expression rather than a classifier, so there is one spelling of the
    # fact, and THIRD_PARTY_NOTICES.md ships next to it because an expression tells a reader WHAT applies and the
    # notices tell them TO WHAT. Both land in <dist-info>/licenses/; the per-dialect License.txt files already
    # travel inside the package, so an installed copy carries the primary texts too.
    license="Apache-2.0 AND CC-BY-SA-3.0 AND CC-BY-SA-4.0",
    license_files=["LICENSE", "THIRD_PARTY_NOTICES.md"],
    # The single authority for the Python floor; python/tools/check_wheel.py reads THIS line rather than
    # carrying a copy of the number, and the wheel's METADATA is what setuptools derives from it.
    #
    # 3.9, not 3.8 (user decision, 2026-10-10). The floor was lowered 3.9 -> 3.8 in round 28 on the
    # strength of the aarch64 board, but every measurement since has bottomed out at 3.9 for an
    # environmental reason: the manylinux image ships no cp38 and setup-python no longer offers 3.8
    # either, so 3.8 could never be exercised as a floor -- only asserted. A declared floor nobody can
    # test is the kind of claim this repository refuses elsewhere, and 3.9 is measured on the board and
    # in CI (the python_versions job installs one wheel into cp39..cp313). Raising it drops support for
    # 3.8 rather than promising it.
    python_requires=">=3.9",
    # PyPI's own surface (2026-10-21, release-readiness objective): an index listing with no classifiers and no
    # project links is a package a reader cannot evaluate -- they cannot tell what it targets, what it is for,
    # or where the source lives. These are the facts the repository already states in README/QUICKSTART.
    #
    # NO `License :: OSI Approved :: ...` CLASSIFIER on purpose: `license="Apache-2.0"` above is the one
    # spelling of that fact (PEP 639 deprecates the classifier pair for exactly this reason, and the comment
    # above records the round-33 decision to have one). Adding it back would recreate the duplication.
    classifiers=[
        "Development Status :: 5 - Production/Stable",
        "Intended Audience :: Developers",
        # No "Operating System :: MacOS" classifier until a macOS leg builds and tests this package:
        # release_prep.yml deliberately produces no macOS wheel (see its comment), and QUICKSTART says
        # macOS is unmeasured. A classifier is a claim on PyPI, not a wish.
        "Operating System :: Microsoft :: Windows",
        "Operating System :: POSIX :: Linux",
        "Programming Language :: C++",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3 :: Only",
        "Topic :: Text Processing :: Linguistic",
    ],
    # No `py.typed` in the package, so no `Typing :: Typed` classifier: that marker means "this distribution
    # ships type information", and there is no py.typed to back it up.
    #
    # The repository URL appears in README and here; CHANGELOG and
    # docs/QUICKSTART are the two links a user of a wheel asks for first. Not derived from anything on purpose:
    # a setup.py that read the URL out of another file would fail at build time on a wheel machine, which is a
    # worse failure than a duplicated constant.
    project_urls={
        "Homepage": "https://github.com/wolfgitpr/cpp-pinyin",
        "Documentation": "https://github.com/wolfgitpr/cpp-pinyin/blob/v2/docs/QUICKSTART.md",
        "Changelog": "https://github.com/wolfgitpr/cpp-pinyin/blob/v2/CHANGELOG.md",
        "Source": "https://github.com/wolfgitpr/cpp-pinyin",
        "Tracker": "https://github.com/wolfgitpr/cpp-pinyin/issues",
    },
    packages=["cpp_pinyin2"],
    package_data={
        "cpp_pinyin2": [
            "_native/*",
            # Nested dictionaries. A bare "*.txt" matches nothing inside data/dict/<dialect>/
            # and reports no error while matching nothing (measured).
            #
            # The ".json" pattern exists because of 2026-10-02: a dialect manifest joined the dictionaries
            # and build_native.py staged it, while this list still said "*.txt", so the wheel shipped every
            # dialect without its manifest and NOTHING FAILED -- the previous wheel had passed the same
            # checks, because those checks did not know the file existed either. Two patterns, one per
            # extension, rather than a directory-wide glob: check_wheel.py pins the exact member set
            # against the source tree and fails on missing AND on extra, so the list of what belongs in a
            # dialect directory is worth keeping explicit and readable next to it.
            "data/dict/**/*.txt",
            "data/dict/**/*.json",
        ]
    },
    include_package_data=False,
    zip_safe=False,
    cmdclass={"bdist_wheel": BDistWheel},
) if __name__ == "__main__" else None
