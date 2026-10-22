"""The wheel's platform tag: never `any`, and an explicit `--plat-name` is honoured.

Why this file exists (audit H47, 2026-10-20). The wheel is `py3-none-<platform>` on purpose: the native
library is opened with ctypes, so no CPython ABI is involved, but the artifact is still platform-specific.
Two things were worth pinning:

* `plat_name` was ACCEPTED on the command line and then silently ignored -- `get_tag()` always rebuilt the
  tag from `sysconfig`, so a cross-build or a release that pins the manylinux level got the build host's
  tag with no error anywhere (the only trace is the wheel's file name, which nobody compares with intent);
* the "not any" rule lived only in `python/tools/check_wheel.py`, i.e. in a tool you must remember to run
  against a built artifact -- these cases fail at the source instead.
"""

import importlib.util
import pathlib

import pytest

SETUP = pathlib.Path(__file__).resolve().parents[1] / "setup.py"


def load_setup():
    """Import setup.py as a module. The `setup()` call at its tail is guarded by __name__, so this is safe."""
    spec = importlib.util.spec_from_file_location("cpp_pinyin2_setup_under_test", SETUP)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.fixture(scope="module")
def setup_module():
    try:
        return load_setup()
    except ImportError as error:  # setuptools missing: the packaging path is not installed at all
        pytest.skip(f"cannot import setup.py here: {error}")


def make_command(setup_module):
    from setuptools.dist import Distribution

    command = setup_module.BDistWheel(Distribution({"name": "probe", "version": "0.0.0"}))
    command.finalize_options()
    return command


def test_the_python_and_abi_tags_are_fixed(setup_module) -> None:
    python_tag, abi_tag, _ = make_command(setup_module).get_tag()
    assert (python_tag, abi_tag) == ("py3", "none")


def test_the_platform_tag_is_never_any(setup_module) -> None:
    """`any` is the one wheel that installs everywhere and works almost nowhere."""
    assert make_command(setup_module).get_tag()[2] != "any"


def test_an_explicit_plat_name_wins_over_the_host_tag(setup_module) -> None:
    command = make_command(setup_module)
    command.plat_name = "manylinux_2_28_x86_64"
    assert command.get_tag()[2] == "manylinux_2_28_x86_64"


def test_a_dashed_plat_name_is_normalised(setup_module) -> None:
    command = make_command(setup_module)
    command.plat_name = "macosx-11.0-arm64"
    assert command.get_tag()[2] == "macosx_11_0_arm64"


def test_without_an_override_the_host_tag_is_used(setup_module) -> None:
    command = make_command(setup_module)
    command.plat_name = None
    assert command.get_tag()[2] == setup_module.platform_tag()
