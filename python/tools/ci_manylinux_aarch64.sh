#!/bin/bash
# Build and verify the wheel where the manylinux tag can actually be earned: inside a manylinux
# container, on a machine of the same architecture as that container.
#
# ARCHITECTURE-NEUTRAL BY CONSTRUCTION (2026-10-09). The container image, the wheel's own platform
# tag and the filename this script looks for are all derived from `uname -m`, so the same bytes run
# on `ubuntu-latest` + `manylinux_2_28_x86_64` and on `ubuntu-24.04-arm` +
# `manylinux_2_28_aarch64`. The file name still says aarch64 because that is the leg this script was
# FIRST measured on; the x86_64 leg reuses it rather than forking a second copy that can drift.
#
# PROVENANCE, PER LEG: every command below was measured on a real aarch64 board on 2026-10-02 (see
# the ledger V110) rather than guessed from a runner's documentation; the x86_64 leg is the same
# commands with one variable changed, and its first honest test is its first GitHub run.
#
# WHY THIS IS A TRACKED SCRIPT AND NOT A DOZEN INLINE STEPS. The wheel pipeline is one artifact moving
# through six questions -- build the package, test the shell, build the wheel, check its member set,
# install it with no index and no compiler, stamp it, check the stamp -- and every one of those commands
# was measured on a real aarch64 board on 2026-10-02 rather than guessed from a runner's documentation.
# Keeping them in one file means the machine that measured them and the CI job that will run them execute
# the same bytes, which is the same reason python/tools/smoke_installed.py is tracked.
#
# WHAT THIS PROVES, AND WHAT IT DOES NOT. It proves that this tree can produce an aarch64 wheel, that the
# wheel installs into an interpreter with no compiler, and that the installed package loads its library and
# answers correctly. It does not prove the GitHub runner wiring: the first run on GitHub is the first honest
# test of the label, the image pull and the runner's Docker. The job that calls this says the same thing.
#
# WHY THE STAMP IS HERE. Platform tag policy (plan section 6.5): our own build stamps plain linux_aarch64,
# because only auditwheel is allowed to claim manylinux. So auditwheel stamps the wheel here, and the
# acceptance (python/tools/check_wheel.py) is then run against the stamped artifact under its own tag --
# a stamped wheel is a compressed tag set, and the checker accepts one of its tags as the expected tag.
#
# Invoked as (the arch must match the runner's own architecture -- no emulation):
#   docker run --rm -e HOME=/tmp -v "$PWD":/work -w /work \
#     quay.io/pypa/manylinux_2_28_x86_64 /bin/bash python/tools/ci_manylinux_aarch64.sh   # ubuntu-latest
#   docker run --rm -e HOME=/tmp -v "$PWD":/work -w /work \
#     quay.io/pypa/manylinux_2_28_aarch64 /bin/bash python/tools/ci_manylinux_aarch64.sh  # ubuntu-24.04-arm
set -u
set -o pipefail

WORK="${WORK:-/work}"
PY="${PY:-/opt/python/cp311-cp311/bin/python}"

# The architecture is read from the machine, not assumed: `linux_aarch64` and `linux_x86_64` are the
# only two this repository ships, and a typo must fail here rather than produce a wheel that claims
# the wrong platform. `docker/setup-qemu-action` is deliberately NOT used anywhere -- the point of
# the per-architecture runners is that the build runs on native hardware (plan section 6.5).
MACHINE="$(uname -m)"
case "$MACHINE" in
  aarch64|arm64) WHEEL_ARCH="linux_aarch64" ;;
  x86_64|amd64)  WHEEL_ARCH="linux_x86_64" ;;
  *) echo "FATAL: unsupported machine '$MACHINE' (expected aarch64 or x86_64)"; exit 2 ;;
esac
PLAT="${PLAT:-manylinux_2_28_${MACHINE}}"
FAILED=""

note()  { echo; echo "### $*"; }
check() { if [ "$2" = "1" ]; then echo "ASSERT OK   $1 ($3)"; else echo "ASSERT FAIL $1 ($3)"; FAILED="$FAILED | $1"; fi; }
fails() { grep -ci FAIL "$1" 2>/dev/null || true; }

cd "$WORK/python" || exit 1

note "0 facts"
uname -m
$PY -V
gcc --version | head -n 1

note "1 toolchain inside the container"
if ! command -v ninja >/dev/null 2>&1; then
  $PY -m pip install --quiet --no-cache-dir ninja > /tmp/pip-ninja.txt 2>&1 || true
  export PATH="$($PY -c 'import sysconfig; print(sysconfig.get_paths()["scripts"])'):$PATH"
fi
if command -v ninja >/dev/null 2>&1; then echo "ninja $(ninja --version)"; else echo "ninja absent: CMake falls back to its default generator"; fi
$PY -m pip install --quiet --no-cache-dir --upgrade pip setuptools wheel > /tmp/pip-tools.txt 2>&1

note "2 stage the package (writes _version.py, builds the shared library, copies the dictionaries)"
$PY tools/build_native.py > /tmp/build-native.txt 2>&1
build_rc=$?
echo "build_native.py exit: $build_rc"
check "_version.py was staged" "$([ -f cpp_pinyin2/_version.py ] && echo 1 || echo 0)" "cpp_pinyin2/_version.py"
check "the native library was built" \
      "$([ -n "$(ls -1 cpp_pinyin2/_native/*.so 2>/dev/null)" ] && echo 1 || echo 0)" \
      "$(ls -1 cpp_pinyin2/_native/ 2>/dev/null | tr '\n' ' ')"
if [ "$build_rc" != 0 ]; then tail -n 40 /tmp/build-native.txt; fi

note "3 the shell suite, before anything is packaged"
$PY -m pip install --quiet --no-cache-dir pytest > /tmp/pip-pytest.txt 2>&1
$PY -m pytest tests -q > /tmp/pytest.txt 2>&1
echo "pytest exit: $?"
summary="$(grep -E '[0-9]+ (passed|failed|error)' /tmp/pytest.txt | tail -n 1)"
echo "summary: $summary"
# The failure word is tested first on purpose: "1 failed, 91 passed" contains both.
case "$summary" in
  *failed*|*error*) ok=0 ;;
  *" passed"*)      ok=1 ;;
  *)                ok=0 ;;
esac
check "pytest reported passes and nothing failed" "$ok" "$summary"

note "4 build the wheel"
$PY -m pip wheel . --no-build-isolation --no-deps -w dist > /tmp/pip-wheel.txt 2>&1
echo "pip wheel exit: $?"
wheel="$(ls -1 dist/*${WHEEL_ARCH}.whl 2>/dev/null | head -n 1)"
check "a wheel was produced" "$([ -n "$wheel" ] && echo 1 || echo 0)" "${wheel:-none}"
if [ -z "$wheel" ]; then echo "RESULT: FAILED -> no wheel"; tail -n 40 /tmp/pip-wheel.txt; exit 1; fi
sha256sum "$wheel"

note "5 the checker's own self-test, on this architecture"
$PY tools/check_wheel.py --self-test > /tmp/self-test.txt 2>&1
self_rc=$?
tail -n 3 /tmp/self-test.txt
check "the checker's self-test exits 0" "$([ "$self_rc" = 0 ] && echo 1 || echo 0)" "exit=$self_rc"

note "6 the wheel we just built, against the source tree"
$PY tools/check_wheel.py "$wheel" > /tmp/check-wheel.txt 2>&1
check_rc=$?
echo "exit: $check_rc"
cat /tmp/check-wheel.txt
# BOTH the exit code AND the absence of FAIL lines. Counting FAIL lines alone reported a crashed checker as a
# pass: a traceback carries no "FAIL" line, so the count is 0 and this assertion said ASSERT OK (2026-10-27).
check "the wheel is accepted" "$([ "$check_rc" = 0 ] && [ "$(fails /tmp/check-wheel.txt)" = 0 ] && echo 1 || echo 0)" "exit=$check_rc, $(fails /tmp/check-wheel.txt) FAIL line(s)"

note "7 install it with no index and no compiler, then ask it questions"
$PY -m venv /tmp/venv
/tmp/venv/bin/pip install --quiet --no-index --no-deps "$WORK/python/$wheel" > /tmp/venv-install.txt 2>&1
echo "venv install exit: $?"
cd /tmp || exit 1
/tmp/venv/bin/python "$WORK/python/tools/smoke_installed.py" --wheel "$WORK/python/$wheel" > /tmp/smoke.txt 2>&1
echo "smoke exit: $?"
tail -n 6 /tmp/smoke.txt
check "the installed package answered" "$(grep -q 'SMOKE OK' /tmp/smoke.txt && echo 1 || echo 0)" "$(grep 'SMOKE OK' /tmp/smoke.txt | tail -n 1)"
cd "$WORK/python" || exit 1

note "8 the wheel against the binding contract (D20's name, the C ABI, the dialect set, the version chain)"
# This step is why the leg exists in this shape. check_bindings.py --wheel had only ever been measured on
# Windows, and CI runs it on Linux too -- so the first honest measurement of it on Linux belongs here, in the
# same file the runner executes rather than in a note claiming it would probably work.
# Version-chain place 5 is the generated `version.props`; this leg never built the C# shell, so it has to
# be generated before the chain can be checked (measured 2026-10-22: "version.props is missing").
$PY "$WORK/csharp/tools/emit_build_info.py" --version-props-only
$PY "$WORK/tools/check_bindings.py" --wheel "$WORK/python/$wheel" > /tmp/check-bindings.txt 2>&1
bind_rc=$?
echo "exit: $bind_rc"
tail -n 10 /tmp/check-bindings.txt
check "the binding gate accepts the wheel" "$([ "$bind_rc" = 0 ] && echo 1 || echo 0)" "exit=$bind_rc"

note "9 auditwheel, the third party the plan defers to"
auditwheel_bin="$(command -v auditwheel || true)"
if [ -z "$auditwheel_bin" ]; then
  $PY -m pip install --quiet --no-cache-dir auditwheel > /tmp/pip-auditwheel.txt 2>&1 || true
  auditwheel_bin="$($PY -c 'import sysconfig, os; print(os.path.join(sysconfig.get_paths()["scripts"], "auditwheel"))')"
fi
echo "auditwheel: $auditwheel_bin"
"$auditwheel_bin" --version 2>&1 | tail -n 1
"$auditwheel_bin" show "$wheel" > /tmp/auditwheel-show.txt 2>&1
echo "auditwheel show exit: $?"
tail -n 12 /tmp/auditwheel-show.txt
rm -rf repaired && mkdir -p repaired
"$auditwheel_bin" repair --plat "$PLAT" -w repaired "$wheel" > /tmp/auditwheel-repair.txt 2>&1
echo "auditwheel repair exit: $?"
tail -n 6 /tmp/auditwheel-repair.txt
stamped="$(ls -1 repaired/*.whl 2>/dev/null | head -n 1)"
check "auditwheel stamped a wheel" "$([ -n "$stamped" ] && echo 1 || echo 0)" "${stamped:-none}"
if [ -n "$stamped" ]; then
  sha256sum "$stamped"
  stamped_tag="$(basename "$stamped" | awk -F- '{print $NF}' | sed 's/\.whl$//' | sed 's/\..*//')"
  echo "checking the stamped wheel under its first tag: $stamped_tag"
  CPP_PINYIN_PLATFORM_TAG="$stamped_tag" $PY tools/check_wheel.py "$stamped" > /tmp/check-stamped.txt 2>&1
  stamped_rc=$?
  echo "exit: $stamped_rc"
  cat /tmp/check-stamped.txt
  # Same rule as step 6: the exit code decides, the FAIL count only describes (2026-10-27).
  check "the stamped wheel is accepted" "$([ "$stamped_rc" = 0 ] && [ "$(fails /tmp/check-stamped.txt)" = 0 ] && echo 1 || echo 0)" "exit=$stamped_rc, $(fails /tmp/check-stamped.txt) FAIL line(s)"
fi

note "10 a STATIC consumer through pkg-config (the only reader of Libs.private)"
# WHY THIS STEP EXISTS (2026-10-27): the .pc carries `Libs.private: -ldl` on Linux, computed from
# ${CMAKE_DL_LIBS}. That variable is EMPTY on Windows and macOS, so no developer machine can exercise the
# non-empty branch, and nothing in tests/ reads the line. A static consumer on glibc < 2.34 needs it. This
# step builds the library STATIC, installs it, and asks `pkg-config --static` -- the one command that reads
# Libs.private -- so the flag is measured rather than asserted in a commit message.
if command -v pkg-config >/dev/null 2>&1; then
  STATIC_BUILD=/tmp/static-build
  STATIC_PREFIX=/tmp/static-prefix
  rm -rf "$STATIC_BUILD" "$STATIC_PREFIX"
  # NO `-G Ninja` HERE. Step 1 installs ninja when pip can, and this image sometimes cannot (no wheel for
  # the architecture, no network): forcing the generator then made configure fail, the .pc was never written,
  # and the two checks below reported "Libs.private contributes -ldl" as FALSE -- an environment gap wearing
  # the verdict of a code defect (measured on CI, manylinux aarch64, 2026-10-29). CMake picks its own default
  # generator when none is named, and the question here is pkg-config, not the generator.
  if command -v ninja >/dev/null 2>&1; then STATIC_GEN="-G Ninja"; else STATIC_GEN=""; fi
  # shellcheck disable=SC2086  # word-splitting is the point: an empty STATIC_GEN must vanish, not become ""
  cmake -S "$WORK" -B "$STATIC_BUILD" $STATIC_GEN -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
        -DCPP_PINYIN2_BUILD_TESTS=OFF -DCPP_PINYIN2_BUILD_EXAMPLES=OFF -DCPP_PINYIN2_INSTALL=ON \
        > /tmp/static.txt 2>&1
  static_configure_rc=$?
  echo "configure exit: $static_configure_rc"
  # A failed configure is a failure of THIS step, not a missing flag: without this line the empty .pc read
  # as "Libs.private is wrong".
  check "the static configure succeeds" "$([ "$static_configure_rc" = 0 ] && echo 1 || echo 0)" "exit=$static_configure_rc"
  cmake --build "$STATIC_BUILD" >> /tmp/static.txt 2>&1
  echo "build exit: $?"
  cmake --install "$STATIC_BUILD" --prefix "$STATIC_PREFIX" >> /tmp/static.txt 2>&1
  echo "install exit: $?"
  pc_file="$(find "$STATIC_PREFIX" -name cpp-pinyin2.pc | head -n 1)"
  echo "pc file: $pc_file"
  pc_dir="$(dirname "$pc_file")"
  static_libs="$(PKG_CONFIG_PATH="$pc_dir" pkg-config --static --libs cpp-pinyin2 2>/dev/null)"
  echo "pkg-config --static --libs: $static_libs"
  # POSIX `case` rather than grep: -ldl may sit anywhere in the flag list, and the log shows the whole line.
  case " $static_libs " in
    *" -ldl "*) have_dl=1 ;;
    *)          have_dl=0 ;;
  esac
  check "Libs.private contributes -ldl" "$have_dl" "$static_libs"
  cat > /tmp/static-consumer.c <<'C_EOF'
#include <cpp-pinyin2/ext/c_api.h>
#include <stdio.h>
int main(void) {
    printf("%s\n", cppp2_version());
    return 0;
}
C_EOF
  # Word-splitting is the point here: the flags have to reach the compiler as separate arguments.
  # shellcheck disable=SC2046
  gcc /tmp/static-consumer.c -o /tmp/static-consumer \
      $(PKG_CONFIG_PATH="$pc_dir" pkg-config --static --cflags --libs cpp-pinyin2) >> /tmp/static.txt 2>&1
  static_link_rc=$?
  echo "link exit: $static_link_rc"
  check "a consumer links through pkg-config --static" \
        "$([ "$static_link_rc" = 0 ] && echo 1 || echo 0)" "see /tmp/static.txt"
  if [ "$static_link_rc" != 0 ]; then
    tail -n 25 /tmp/static.txt
  else
    /tmp/static-consumer
  fi
else
  # NOT A SILENT SKIP (the reasoning check_format's `--require` spells out): this step exists only to
  # measure Libs.private, so an image without the tool means the -ldl claim stays unmeasured -- and a step
  # that prints a reason and exits 0 has not run. The manylinux images ship pkgconfig; if one ever does
  # not, this records the failure and the person reading it can either install it or drop the step
  # deliberately, with the reason written down.
  check "pkg-config is available for the Libs.private measurement" 0 \
        "absent from this image: Libs.private stays unmeasured"
fi

note "RESULT"
if [ -z "$FAILED" ]; then
  echo "RESULT: OK"
  exit 0
fi
echo "RESULT: FAILED ->$FAILED"
exit 1
