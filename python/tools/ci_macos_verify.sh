#!/bin/bash
# Verify a macOS cpp-pinyin2 wheel the way the repository's other legs are verified: build it, ask the
# binary what it will need at load time, install it with no index and no compiler, and answer a query.
#
# WHY THIS IS A TRACKED SCRIPT AND NOT A DOZEN INLINE STEPS. Exactly the reason
# python/tools/ci_manylinux_aarch64.sh is tracked: the machine that measured the commands and the CI job
# that will run them then execute the same bytes. The Linux legs had a board to be measured on; this one
# has no macOS machine in this project, so **this script is unverified until someone runs it on a Mac**,
# and its first honest execution is the first evidence. That is stated here rather than implied, and
# docs/RELEASE_CHECKLIST.md section 6.7.11 says the same thing.
#
# WIRED (2026-10-27): it had no caller at all until then. The ci.yml macos job now builds a wheel and
# hands it to this script, so "unverified" means "the step exists and has never produced a result", not
# "nothing is going to run it". The header above predates that wiring and is kept on purpose: it is the
# reason the script refuses to guess.
#
# WHAT IT CHECKS AND WHY EACH ONE IS HERE:
#   1. the architecture the binary actually contains, read from `lipo -archs`, against the architecture
#      the wheel's own tag claims -- a universal2 tag on a thin arm64 library is the failure this catches;
#   2. LC_LOAD_DYLIB: nothing may be loaded from a Homebrew path and libstdc++ may not appear at all
#      (macOS ships libc++, never libstdc++), and the library's own install name must be @rpath-based;
#   3. LC_RPATH: a bundled library is found through a self-relative @loader_path, not an absolute path;
#   4. the wheel installs into a compiler-less, index-less venv and answers a query (smoke_installed.py);
#   5. the packaged member set still matches the source tree it was built from (check_wheel.py).
#
# USAGE (on a Mac, from the repository root):
#     python python/tools/build_native.py                 # stages _native/ + data/dict/
#     cd python && python -m pip wheel . --no-build-isolation --no-deps -w dist && cd ..
#     bash python/tools/ci_macos_verify.sh python/dist/cpp_pinyin2-*.whl
#
# The deployment target and the architecture flags are the build's business, not this script's: see the
# APPLE branch of CMakeLists.txt (per-architecture floor) for the deployment target; universal2 is not built.
set -u
set -o pipefail

WHEEL="${1:-}"
WORK="$(cd "$(dirname "$0")/../.." && pwd)"
PY="${PY:-python3}"
FAILED=""

note()  { echo; echo "### $*"; }
check() { if [ "$2" = "1" ]; then echo "ASSERT OK   $1 ($3)"; else echo "ASSERT FAIL $1 ($3)"; FAILED="$FAILED | $1"; fi; }

if [ "$(uname -s)" != "Darwin" ]; then
    echo "FATAL: this script needs macOS (uname -s = $(uname -s)). It asks otool/lipo questions; there is no"
    echo "       way to answer them from another platform, and guessing them is what this file refuses to do."
    exit 2
fi

if [ -z "$WHEEL" ] || [ ! -f "$WHEEL" ]; then
    echo "FATAL: pass the wheel to verify, e.g. bash python/tools/ci_macos_verify.sh python/dist/cpp_pinyin2-*.whl"
    exit 2
fi
WHEEL="$(cd "$(dirname "$WHEEL")" && pwd)/$(basename "$WHEEL")"

note "0 facts"
uname -a
sw_vers 2>/dev/null || true
$PY -V
xcode-select -p 2>/dev/null || true
echo "wheel: $WHEEL"

# ---------------------------------------------------------------------------------------------------
# Unpack just far enough to look at the binary. Not into the venv, and not into the source tree.
# ---------------------------------------------------------------------------------------------------
STAGE="$(mktemp -d "${TMPDIR:-/tmp}/cpp-pinyin2-macos-verify.XXXXXX")"
cleanup() { rm -rf "$STAGE"; }
trap cleanup EXIT

note "1 the wheel's own claims, and the member we are about to interrogate"
$PY - "$WHEEL" "$STAGE" <<'PYTHON'
import pathlib, sys, zipfile

wheel, stage = sys.argv[1], pathlib.Path(sys.argv[2])
print("file name     :", wheel.rsplit("/", 1)[-1])
parts = wheel.rsplit("/", 1)[-1][: -len(".whl")].split("-")
if len(parts) != 5:
    sys.exit("FATAL: expected <distribution>-<version>-<python>-<abi>-<platform>.whl")
print("platform tags :", parts[4])

with zipfile.ZipFile(wheel) as archive:
    meta = next((n for n in archive.namelist() if n.endswith(".dist-info/WHEEL")), None)
    print("WHEEL file    :",
          archive.read(meta).decode("utf-8", "replace").strip().replace("\n", " | ")
          if meta else "MISSING from the wheel")
    natives = [n for n in archive.namelist()
               if n.endswith((".dylib", ".so", ".dll")) or ".so." in n.rsplit("/", 1)[-1]]
    print("native members:", natives)
    for member in natives:
        # Written flat into the staging directory: the checks below ask `otool` questions about files, and
        # nothing here needs the package's directory layout.
        leaf = member.rsplit("/", 1)[-1]
        (stage / leaf).write_bytes(archive.read(member))
        print("unpacked      :", stage / leaf)
PYTHON
libs="$(find "$STAGE" -maxdepth 1 -type f \( -name '*.dylib' -o -name '*.so' \) 2>/dev/null)"
check "the wheel carries a native library" "$([ -n "$libs" ] && echo 1 || echo 0)" "${libs:-none}"
if [ -z "$libs" ]; then
    echo "RESULT: FAILED ->$FAILED"
    exit 1
fi

note "2 the architecture the binary really contains"
tag_arch=""
case "$(basename "$WHEEL")" in
    *universal2*) tag_arch="universal2" ;;
    *arm64*)      tag_arch="arm64" ;;
    *x86_64*)     tag_arch="x86_64" ;;
esac
# THE CASE ABOVE HAS NO DEFAULT ARM, and that used to be a silent hole: a wheel whose platform tag named
# no architecture this script knows left `tag_arch` empty, and the loop below then skipped its arch check
# without printing anything -- "verified" and "never looked at" reading the same in the log. That is the
# shape this repository keeps finding, so it is a failure now: a tag naming no architecture cannot be
# verified, and the three tags macOS actually produces (`macosx_X_Y_{arm64,x86_64,universal2}`) all match
# a pattern above (2026-10-27).
check "the platform tag names an architecture this script can verify" \
      "$([ -n "$tag_arch" ] && echo 1 || echo 0)" "$(basename "$WHEEL")"
for lib in $libs; do
    archs="$(lipo -archs "$lib" 2>/dev/null | tr ' ' '\n' | sort | tr '\n' ' ')"
    echo "  $(basename "$lib"): $archs"
    if [ "$tag_arch" = "universal2" ]; then
        has_x86="$(echo "$archs" | grep -c x86_64)"
        has_arm="$(echo "$archs" | grep -c arm64)"
        check "the universal2 tag is backed by both architectures in $(basename "$lib")" \
              "$([ "$has_x86" -ge 1 ] && [ "$has_arm" -ge 1 ] && echo 1 || echo 0)" "$archs"
    elif [ -n "$tag_arch" ]; then
        check "the $(basename "$lib") architecture matches the wheel tag ($tag_arch)" \
              "$(echo "$archs" | grep -c "$tag_arch")" "$archs"
    fi
done

note "3 what the library loads at run time (LC_LOAD_DYLIB)"
for lib in $libs; do
    echo "  --- $(basename "$lib")"
    otool -L "$lib" | tail -n +2 | sed 's/^/    /'
    if otool -L "$lib" | tail -n +2 | grep -qi "/opt/homebrew/\|/usr/local/"; then
        check "$(basename "$lib") does not load a Homebrew/usr-local library" 0 \
              "$(otool -L "$lib" | grep -i "/opt/homebrew/\|/usr/local/")"
    else
        check "$(basename "$lib") does not load a Homebrew/usr-local library" 1 "no such path in LC_LOAD_DYLIB"
    fi
    if otool -L "$lib" | tail -n +2 | grep -qi "libstdc++"; then
        check "$(basename "$lib") does not need libstdc++ (macOS ships libc++)" 0 \
              "$(otool -L "$lib" | grep -i libstdc++)"
    else
        check "$(basename "$lib") does not need libstdc++ (macOS ships libc++)" 1 "no libstdc++ in LC_LOAD_DYLIB"
    fi
    install_name="$(otool -D "$lib" | tail -n +2 | head -n 1)"
    echo "    install name: ${install_name:-<none>}"
    case "$install_name" in
        @rpath/*|@loader_path/*|"") check "the install name is @rpath-based" 1 "${install_name:-<none>}" ;;
        *) check "the install name is @rpath-based" 0 "$install_name" ;;
    esac
done

note "4 how it finds its siblings at run time (LC_RPATH) -- REPORTED, not required"
# This used to be a `check`, and it could never pass: the build sets no INSTALL_RPATH at all
# (`git grep -i rpath` over the CMake files returns nothing), so the dylib has no LC_RPATH to find. It also
# does not NEED one: the wheel ships a single self-contained dylib that ctypes loads by absolute path, with no
# sibling libraries to locate. Turning a design fact into a failing assertion is how a verification script
# starts lying, so it reports what it sees and says why that is acceptable.
for lib in $libs; do
    rpaths="$(otool -l "$lib" | awk '/LC_RPATH/{found=1} found && $1=="path"{print $2; found=0}')"
    echo "  --- $(basename "$lib"): LC_RPATH = ${rpaths:-<none>} (no sibling dylibs to find; see the note above)"
done

note "5 the wheel installs into a compiler-less, index-less venv and answers a query"
VENV="$STAGE/venv"
$PY -m venv "$VENV"
"$VENV/bin/python" -m pip install --quiet --no-index --no-deps "$WHEEL" > "$STAGE/pip.txt" 2>&1
echo "pip install exit: $?"
tail -n 3 "$STAGE/pip.txt"
"$VENV/bin/python" -c "import cpp_pinyin2, sys; print('import ok:', cpp_pinyin2.__version__, cpp_pinyin2.Engine.default_dict_dir())" \
    > "$STAGE/import.txt" 2>&1
import_rc=$?
echo "import exit: $import_rc"
cat "$STAGE/import.txt"
check "the installed package imports" "$([ "$import_rc" = 0 ] && echo 1 || echo 0)" "see above"
if [ "$import_rc" = 0 ]; then
    "$VENV/bin/python" "$WORK/python/tools/smoke_installed.py" --wheel "$WHEEL" > "$STAGE/smoke.txt" 2>&1
    smoke_rc=$?
    echo "smoke exit: $smoke_rc"
    tail -n 8 "$STAGE/smoke.txt"
    check "the installed package answers a query" "$([ "$smoke_rc" = 0 ] && echo 1 || echo 0)" "see above"
fi

note "6 the packaged members still match the source tree"
$PY "$WORK/python/tools/check_wheel.py" "$WHEEL" > "$STAGE/check-wheel.txt" 2>&1
check_rc=$?
tail -n 5 "$STAGE/check-wheel.txt"
check "check_wheel.py accepts the wheel" "$([ "$check_rc" = 0 ] && echo 1 || echo 0)" "exit $check_rc"
# check_wheel.py expects this host's platform tag; on a macOS host that is what it reads from the wheel
# name, so a mismatch here means the wheel was built for another macOS architecture than this machine --
# worth seeing, not worth failing over, which is why it is reported rather than asserted.
if [ "$check_rc" != 0 ]; then
    echo "  note: check_wheel.py also compares the platform tag against this host; on a wheel built for"
    echo "        another architecture that comparison is expected to fail while the member set is fine."
fi

note "RESULT"
if [ -z "$FAILED" ]; then
    echo "RESULT: OK"
    exit 0
fi
echo "RESULT: FAILED ->$FAILED"
exit 1
