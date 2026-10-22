cmake_minimum_required(VERSION 3.17)
project(cpp-pinyin2-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# ---- Vendor Catch2 (amalgamated .hpp + .cpp, no network needed) ----
set(CATCH2_VENDOR_DIR "${CMAKE_SOURCE_DIR}/vendor")

# test_comprehensive.cpp uses std::thread. On glibc < 2.34 (e.g. Ubuntu 20.04, glibc 2.31)
# the pthread symbols still live in libpthread, which g++ does not pull in implicitly, so
# without this the test executable fails to link with "undefined reference to pthread_create".
# On platforms where threads are part of libc this resolves to a no-op.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# ---- Catch2 test executable ----
# This list is explicit, and the guard below makes a missing entry a configure ERROR: dropping a test used to
# shrink the suite silently, because `file(GLOB)` over literal paths matches nothing without complaint (that is how
# test_batch_accuracy.cpp vanished with the legacy corpora on 2026-09-29 without the build saying a word).
set(_catch2_src
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/test_golden_pinyin.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/test_golden_jyutping.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/test_bug_regression.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/test_new_api.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/test_accuracy_v2.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/test_boundary_utf8.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/test_candidates.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/test_style_convert.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/test_tone_sandhi.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/test_user_dict.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/test_comprehensive.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/test_engine_dialect.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/test_dict_format_marker.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/test_embedded_dicts.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/test_c_api.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/test_minimal_usage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/test_syllable_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/test_syllable_producer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/test_outcome.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/test_style_table.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/test_default_style.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/test_seven_tone_dialect.cpp
    ${CATCH2_VENDOR_DIR}/catch_amalgamated.cpp
)

# A missing test file must be a configure error, not a quietly smaller suite. `file(GLOB)` over literal paths is
# what used to make this silent; `set()` plus this loop is what makes it loud (see the note above).
foreach(_required_src IN LISTS _catch2_src)
    if(NOT EXISTS "${_required_src}")
        message(FATAL_ERROR
            "a test source listed in tests/CMakeLists.txt does not exist: ${_required_src}\n"
            "Deleting a test on purpose means deleting its line here too -- otherwise the suite silently shrinks.")
    endif()
endforeach()

add_executable(CppPinyinTests ${_catch2_src})

# The S5 producer/renderer (`src/toneUtil/ReadingCodec.{h,cpp}`) is a library internal: it is not one
# of the installed headers (`tests/abi/expected_public_headers.txt` is that list's authority and counts 15 as
# of 2026-10-21; this comment said 16 until 2026-10-10 and then 14 -- which is why it no longer repeats a number), and `ToneNotation`/`parseReading`/`surfaceOf` are not exported from the
# shared library. test_syllable_producer.cpp therefore compiles its translation unit directly -- the
# same arrangement DictSelfCheck uses for the dict layer. ManToneUtil.cpp comes along because
# `tone3ToTone` is only exported inside the library (it is what ManTone::normalizeReading calls), and
# `-I src` is what makes `#include "toneUtil/ReadingCodec.h"` resolve. Dropping any of the three lines
# below breaks the test at link time rather than silently shrinking what the sweep covers.
target_sources(CppPinyinTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/toneUtil/ReadingCodec.cpp
    ${CMAKE_SOURCE_DIR}/src/toneUtil/ManToneUtil.cpp)
target_include_directories(CppPinyinTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/toneUtil)

# The vendored Catch2 amalgamation is third-party code and is not policed: it triggers
# -Wdouble-promotion / -Wredundant-decls under our strict set (measured). Disable warnings
# for that one translation unit rather than narrowing the set for our own sources.
set_source_files_properties(${CATCH2_VENDOR_DIR}/catch_amalgamated.cpp
        PROPERTIES COMPILE_OPTIONS "-w")   # VENDOR_NOT_POLICED
target_include_directories(CppPinyinTests PRIVATE ${CATCH2_VENDOR_DIR})
target_link_libraries(CppPinyinTests PRIVATE
    cpp-pinyin2::cpp-pinyin2
    Threads::Threads)

# AC-1's six-line sample is enforced by COUNTING the lines in examples/minimal_usage.cpp, so the test
# has to be able to open that file. A relative path back to the source tree would depend on the working
# directory; a compile definition does not. The same file is a real target (examples/, target
# MinimalUsage) -- that is what makes it checkable instead of asserted.
target_compile_definitions(CppPinyinTests PRIVATE
    CPP_PINYIN_MINIMAL_USAGE_SOURCE="${CMAKE_SOURCE_DIR}/examples/minimal_usage.cpp")

# Same idiom, same reason, for the 2.0 face's tone contract: the four authoritative sentences live in
# the header's own comment (the 2.0 plan's section E, adopted verbatim; that ledger was retired in the
# 2026-10-09 documentation cut -- docs/HISTORY.md), so
# tests/cpp/test_syllable_contract.cpp reads that header and fails when a rule goes away. The test
# runs from the build tree, hence a definition rather than a relative path.
target_compile_definitions(CppPinyinTests PRIVATE
    CPP_PINYIN_SYLLABLE_HEADER_SOURCE="${CMAKE_SOURCE_DIR}/include/cpp-pinyin2/core/Syllable.h")

# Copy dict and the verified tables for the Catch2 tests.
#
# These used to be plain POST_BUILD commands of the test executable, which only run when that
# executable *relinks*. Editing a data file therefore left the stale copy in the build directory in
# place and the suite kept measuring the old corpus -- proved when the D18 notation fix changed
# cpp_dataset.txt and the accuracy numbers did not move at all.
#
# The second attempt (an OUTPUT/stamp custom command with DEPENDS on the data files) was correct in
# intent and still did not hold: the stamp is compared against the DATA FILE TIMESTAMPS, so any edit
# whose timestamp was not newer than the stamp left the build tree holding the old corpus while the
# suite stayed green. Found 2026-09-29, while wiring the corpus gates into CTest: the frozen
# mandarin_untone counts had been stale since wave 105 and the only reason the C++ test kept passing
# was the old copy of that file in the build tree. The copies are therefore UNCONDITIONAL now --
# copy_directory costs about 15 ms for the 5 MB of assets, which is cheaper than trusting a stamp.
#
# tests/data/verified/source/ is deliberately NOT copied: nothing reads it from the build tree. The
# DumpEquivalence gate takes it straight from the source tree (tools/CMakeLists.txt).
#
# Two destinations, because the assets are read from two different working directories:
#   bin/    -- when the test executable is run directly (its runtime directory)
#   tests/  -- when it is run through ctest (ctest uses the CMake binary dir as cwd)
set(_asset_dirs ${CMAKE_BINARY_DIR}/bin ${CMAKE_BINARY_DIR}/tests)
set(_copy_commands "")
foreach (_dir IN LISTS _asset_dirs)
    list(APPEND _copy_commands
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/data/verified ${_dir}/verified
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/data/dialects ${_dir}/dict)
endforeach ()

add_custom_target(CopyTestAssets ${_copy_commands}
    COMMENT "Copying test assets (dict, verified tables) into bin/ and tests/")
add_dependencies(CppPinyinTests CopyTestAssets)

# Register with CTest.
#
# The corpus case is held OUT of this entry and runs as CorpusAccuracy below, so that one `ctest`
# run executes it exactly once. Measured 2026-09-29: that case is 33,258 of the binary's 34,550
# assertions (96%) and 0.58 s of its 4.48 s; before the split ctest ran it twice -- once inside
# CppPinyinTests and once as CorpusAccuracy. The filter lives in THIS entry, not in the test case,
# so running the bare binary by hand (`./CppPinyinTests`, what ci.yml used to do) still runs
# everything. Consequence to remember: `ctest -R CppPinyinTests` alone no longer covers the corpus;
# use the full `ctest` run, or `ctest -R "CppPinyinTests|CorpusAccuracy"`.
#
# 2026-10-04: the concurrency cases below get the same treatment, so this filter now excludes their
# tags too -- `ctest -R CppPinyinTests` no longer covers them either. Use the `thread` label (both
# entries below) or a full `ctest` run. Measured with `--list-tests` on that date: 132 cases before
# the change, 126 after, and the 6 removed are exactly the two new entries' cases.
add_test(NAME CppPinyinTests COMMAND CppPinyinTests "~[corpus]~[thread]~[thread_safety]~[concurrent]~[isolation]")

# The verified tables run as their own test: their per-set counts are frozen exactly, so a
# regression names the table it broke instead of hiding inside the whole suite (D16 discipline).
add_test(NAME CorpusAccuracy COMMAND CppPinyinTests "[corpus]")
set_tests_properties(CorpusAccuracy PROPERTIES LABELS "corpus")

# Thread safety and instance isolation get their own entries for the same reason as the corpus split:
# a failure should name the contract it broke. This also closes a gap `docs/STABILITY.md` recorded on
# 2026-10-04 -- those cases were reachable ONLY through Catch2 tag names, so no CTest label could
# select them and "the thread / multi-instance cases are covered" stayed a claim about the suite.
# As above, the filter lives in the entry, so the bare binary (what ci.yml runs) still runs everything.
#   Measured 2026-10-04 (`--list-tests`): [thread]/[thread_safety]/[concurrent] = 5 cases;
#   [isolation] = 1 case; the exclusions on the entry above remove exactly those 6.
add_test(NAME ThreadSafety COMMAND CppPinyinTests "[thread],[thread_safety],[concurrent]")
set_tests_properties(ThreadSafety PROPERTIES LABELS "thread")
add_test(NAME InstanceIsolation COMMAND CppPinyinTests "[isolation]")
set_tests_properties(InstanceIsolation PROPERTIES LABELS "thread")

# ---- Corpus gates (data-side regression) ----------------------------------------------------------
# CorpusAccuracy above proves the ENGINE still reproduces the frozen counts. It cannot prove the
# verified files are still internally consistent: a hand-edit that keeps the counts intact (say,
# widening a cell, or dropping a ledger record) would sail through it. These gates cover that side,
# and they read version-controlled inputs only -- tests/data/verified, data/dialects and the baselines/ledger
# under tests/data/corpus -- so they run anywhere the repo is checked out. The review pipeline itself
# lives in the untracked .tmp/corpus area and is deliberately not reachable from here.
#
# One test per gate, so a failure names the broken invariant instead of "the corpus gates failed".
#
# Python is REQUIRED here. It used to be optional, with a configure-time note when it was missing --
# but that note silently removed six CTest entries, so a suite that looked complete checked less (the
# same "declaration without verification" shape that let a third dialect into data/dialects unnoticed).
# Configure with -DCPP_PINYIN2_BUILD_TESTS=OFF if you do not want the tests at all.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(_corpus_gates check_corpus ledger consistency cross_set projection)
foreach (_gate IN LISTS _corpus_gates)
    add_test(NAME CorpusGate_${_gate}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/data/corpus/run_corpus_gates.py --gate ${_gate})
    set_tests_properties(CorpusGate_${_gate} PROPERTIES
        LABELS "corpus"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 600)
endforeach ()
# Proves the runner can fail: without it, a gate whose check silently stopped running would
# look exactly like a passing gate.
add_test(NAME CorpusGateSelfTest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/data/corpus/run_corpus_gates.py --self-test)
set_tests_properties(CorpusGateSelfTest PROPERTIES LABELS "corpus")

# ---- Benchmark executable ----
add_executable(Benchmark ${CMAKE_CURRENT_SOURCE_DIR}/benchmark.cpp)
target_link_libraries(Benchmark PRIVATE cpp-pinyin2::cpp-pinyin2)
add_dependencies(Benchmark CopyTestAssets)

# Smoke-run the benchmark, but do NOT assert timings. Measured 2026-09-29 on five to eight identical
# runs of this binary: a case's min moves up to 4.55% and the dictionary-loading cases move 11-19%
# (BACKLOG.md section 1.1 measured max/min = 1.39 inside ONE run of LoadCheck), so a timing gate
# would fail on noise every few runs. What this entry does catch is runtime bit-rot that the compiler
# cannot see: a changed dictionary layout, an API change that still compiles, missing test assets.
# Measured 2026-09-29: `Benchmark.exe` exits 0 in ~3 s (the iterations are sized so every case
# measures for ~50 ms, which is what makes its numbers comparable between runs -- see the comment in
# benchmark.cpp); with --brief its log is one line per case (21 cases) instead of the ~70-line table
# the interactive run prints.
#
# It also leaves an artifact behind now: --json writes a report the next run can be diffed against
# (`Benchmark --compare <path>`), and --brief keeps this entry's log to one line per case. That is
# where the benchmark's CI value lives, since a timing assertion is still off the table.
add_test(NAME BenchmarkSmoke
    COMMAND Benchmark --brief --json ${CMAKE_BINARY_DIR}/benchmark-report.json)
set_tests_properties(BenchmarkSmoke PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ---- C ABI smoke test (compiled as C, not C++) -----------------------------------------------
# The C ABI (include/cpp-pinyin2/ext/c_api.h) exists for C, C# and Python consumers, so the header has
# to be usable from a C translation unit -- which the C++ header self-check below cannot prove. This
# target is the proof: the C compiler compiles tests/c_api_smoke.c, links the same library, and
# walks results through the struct fields the way a P/Invoke or ctypes binding does.
# Its warning set is NOT cpp-pinyin2_warnings: that set contains C++-only flags
# (-Wnon-virtual-dtor, -Wsuggest-override, -Wzero-as-null-pointer-constant, -Wextra-semi) which GCC
# only warns about being ignored for C -- it would put that noise in every build log.
enable_language(C)
# tests/conformance_emit.c is the second job of this program: `--emit-conformance` writes
# conformance/cases.json for the shell test suites (plan doc 6.7.6). It rides along in the existing
# C ABI program because that program already links the library and already has a flag-driven mode.
add_executable(CApiSmoke ${CMAKE_CURRENT_SOURCE_DIR}/c_api_smoke.c
                         ${CMAKE_CURRENT_SOURCE_DIR}/conformance_emit.c)
target_link_libraries(CApiSmoke PRIVATE cpp-pinyin2::cpp-pinyin2)
# The library's own CPP_PINYIN2_VERSION_STRING is PRIVATE, so this target gets the same value from the
# same source (${cpp-pinyin2_VERSION} == PROJECT_VERSION). That is what lets c_api_smoke compare
# cppp2_version() against it without inventing a second place the version is written down.
target_compile_definitions(CApiSmoke PRIVATE CPP_PINYIN2_VERSION_STRING="${cpp-pinyin2_VERSION}")
# C target: it must NOT link cpp-pinyin2_dev_flags (that set carries C++-only warning flags), but it should
# still be instrumented, so the sanitizers are linked directly. The C API is a frozen surface -- a smoke
# test that is not built with the sanitizers would leave that surface unchecked.
target_link_libraries(CApiSmoke PRIVATE cpp-pinyin2_sanitizers)
add_dependencies(CApiSmoke CopyTestAssets)
set_target_properties(CApiSmoke PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
if (MSVC)
    target_compile_options(CApiSmoke PRIVATE /W4)
else ()
    target_compile_options(CApiSmoke PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wcast-qual)
endif ()
# dictDir is passed explicitly: the C ABI has no default dictionary path on purpose, so the test
# must not depend on one either. WORKING_DIRECTORY is bin/ for the same reason BenchmarkSmoke uses it.
add_test(NAME CApiSmoke COMMAND CApiSmoke "${CMAKE_BINARY_DIR}/bin/dict")
set_tests_properties(CApiSmoke PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ---- the static archive must be linkable INTO a shared object (2026-10-20 audit, G10) -------------
# Only the static configuration has anything to prove: a non-PIC archive links into an executable (what
# the static leg of CI builds) and fails only when a shared object pulls it in. Building this target is
# the assertion -- there is no runtime check, and ctest is not involved on purpose (a build that cannot
# produce the library is already a red build). `tests/cpp/pic_probe.cpp` says why it calls a symbol.
if (NOT BUILD_SHARED_LIBS)
    add_library(PicProbe SHARED cpp/pic_probe.cpp)
    target_link_libraries(PicProbe PRIVATE cpp-pinyin2::cpp-pinyin2 cpp-pinyin2_sanitizers)
endif ()

# ---- C ABI export surface (P2 acceptance item 3 / D10, plan doc section 6.7.8) ------------------
# CApiSmoke proves the C ABI WORKS; it cannot prove the surface a binding binds to is still the same
# surface. That is a property of the binary's dynamic symbol table, so the check reads the built
# shared library with nm/objdump and compares the exported cppp2_* set against a frozen list -- in
# both directions (a missing symbol breaks a ctypes/P-Invoke load at runtime; an extra one is an
# unreviewed ABI addition). It also fails if internal C++ symbols (EngineImpl, PhraseGate) start
# leaking, which is exactly what -fvisibility=hidden in the root CMakeLists.txt prevents.
#
# SCOPE: it inspects a SHARED library, so it is registered only for BUILD_SHARED_LIBS=ON builds --
# which is the project default (CMakeLists.txt:20 sets `option(BUILD_SHARED_LIBS ... ON)`), so this gate
# runs by default. A
# -DBUILD_SHARED_LIBS=OFF build has no dynamic symbol table to read; it says so at configure time
# instead of registering a test that would quietly pass.
# NOTE: the library target is named literally, not via ${PROJECT_NAME}: this directory declares its
# own project() (line 2), so PROJECT_NAME here is cpp-pinyin2-tests -- using it produced
# "$<TARGET_FILE:cpp-pinyin2-tests> / No target" at generate time (measured).
set(_export_gate_args
        "-DLIB=$<TARGET_FILE:cpp-pinyin2>"
        "-DEXPECTED=${CMAKE_SOURCE_DIR}/tests/abi/expected_c_api_symbols.txt"
        "-DWORK=${CMAKE_BINARY_DIR}/tests/abi")
if (BUILD_SHARED_LIBS)
    add_test(NAME ExportGate
        COMMAND ${CMAKE_COMMAND} ${_export_gate_args} -P ${CMAKE_CURRENT_SOURCE_DIR}/abi/check_exports.cmake)
    set_tests_properties(ExportGate PROPERTIES LABELS "abi")
else ()
    message(STATUS "BUILD_SHARED_LIBS=OFF -- ExportGate is not registered "
            "(no shared library to inspect; configure with -DBUILD_SHARED_LIBS=ON to run it)")
endif ()
# Proves the checker can fail: four synthetic dumps (exact list / one missing / one extra / one
# internal leak). Without it, a checker that silently stopped comparing would look like a pass.
add_test(NAME ExportGateSelfTest
    COMMAND ${CMAKE_COMMAND} -DCPP_PINYIN_SELF_TEST=ON
            "-DEXPECTED=${CMAKE_SOURCE_DIR}/tests/abi/expected_c_api_symbols.txt"
            "-DWORK=${CMAKE_BINARY_DIR}/tests/abi"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/abi/check_exports.cmake)
set_tests_properties(ExportGateSelfTest PROPERTIES LABELS "abi")

# ---- soname / binary generation (docs/STABILITY.md R9) ----------------------------------------
# WHAT IT PROVES: the artifact's DT_SONAME reads libcpp-pinyin2.so.<major>, so the generation
# claim R9 makes is readable from the binary itself. Before 2026-10-19 no gate read the artifact at
# all (0 hits for `soname` under tests/ tools/ .github/ cmake/ packaging/).
# WHY UNCONDITIONAL: the entry always runs the checker's self-test first (six synthetic dumps:
# correct ELF / no SONAME / previous generation / wrong product / empty file / Apple install name),
# and that half is platform-independent. Only the comparison against the real file is skipped --
# loudly, with "SKIP (PE has no soname)" -- on PE or on a static build. A gate that is registered
# only on Linux is a gate this machine never runs.
add_test(NAME SonameGate
    COMMAND ${CMAKE_COMMAND}
            "-DLIB=$<TARGET_FILE:cpp-pinyin2>"
            "-DROOT=${CMAKE_SOURCE_DIR}"
            "-DWORK=${CMAKE_BINARY_DIR}/tests/abi"
            "-DEXPECT_SHARED=${BUILD_SHARED_LIBS}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/abi/check_soname.cmake)
set_tests_properties(SonameGate PROPERTIES LABELS "abi")

# ---- Dialect set (this repository ships exactly mandarin and cantonese) -----------------------
# Measured 2026-10-01: copying a third language's dictionary directory into data/dialects/ (the dictionary
# repository produces out/dict/<lang>/ with the same layout) left every other CTest entry green --
# nothing owned the rule "data/dialects == runtime registry == {mandarin, cantonese}" (plan: 6.7.4 rule 2b,
# 6.8.7 #7). This gate owns it, against a single frozen file.
set(_dialect_gate_args
        "-DREPO_DICT_DIR=${CMAKE_SOURCE_DIR}/data/dialects"
        "-DEXPECTED=${CMAKE_SOURCE_DIR}/tests/data/dialect_set/expected_dialects.txt"
        "-DSMOKE=$<TARGET_FILE:CApiSmoke>"
        "-DDICT_DIR=${CMAKE_BINARY_DIR}/bin/dict"
        "-DWORK=${CMAKE_BINARY_DIR}/tests/data/dialect_set")
add_test(NAME DialectSetGate
    COMMAND ${CMAKE_COMMAND} ${_dialect_gate_args}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/data/dialect_set/check_dialect_set.cmake)
set_tests_properties(DialectSetGate PROPERTIES LABELS "dialect")
# Proves it can fail: synthesises an exact tree, one with an extra dialect directory, and one with a
# directory missing. Without this, a gate that stopped comparing would look like a pass.
add_test(NAME DialectSetGateSelfTest
    COMMAND ${CMAKE_COMMAND} -DCPP_PINYIN_SELF_TEST=ON ${_dialect_gate_args}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/data/dialect_set/check_dialect_set.cmake)
set_tests_properties(DialectSetGateSelfTest PROPERTIES LABELS "dialect")

# ---- Installed-tree consumer smoke ------------------------------------------------------------
# WHY: nothing owned the install rules. A downstream project consumes this library through
# `cmake --install` + find_package, and until 2026-10-01 no test executed either one. The probe that
# found this gap looked like a packaging defect and was a quoting bug in the probe itself --
# `-DCMAKE_PREFIX_PATH=$x` passed unquoted to a native command reached CMake literally, as
# CMakeCache.txt recorded (`CMAKE_PREFIX_PATH:UNINITIALIZED=$inst`). That is exactly why the check
# belongs in a gate with a self-test instead of in a shell history.
if (NOT cpp-pinyin2_VERSION)
    message(FATAL_ERROR "the root project() version is not visible in this directory, so the install "
            "smoke gate could not pin the version chain (see tests/CMakeLists.txt)")
endif ()
# The install layout is not the same everywhere: CMake's GNUInstallDirs says `lib` on Debian/Ubuntu and
# `lib64` on Fedora/RHEL/AlmaLinux (measured 2026-10-02 in the manylinux image). The gate takes the value
# as an input instead of assuming one, so include the module here too -- tests/ declares its own
# project() and must not depend on the root directory having included it.
include(GNUInstallDirs)
set(_install_gate_args
        "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
        "-DVERSION=${cpp-pinyin2_VERSION}"
        "-DGENERATOR=${CMAKE_GENERATOR}"
        "-DINSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}"
        "-DCPP_PINYIN2_EMBEDDED=${_cpp_pinyin_embedded}"
        "-DBUILD_SHARED=${BUILD_SHARED_LIBS}"
        "-DWORK=${CMAKE_BINARY_DIR}/tests/install")
if (CPP_PINYIN2_INSTALL)
    add_test(NAME InstallConsumerSmoke
        COMMAND ${CMAKE_COMMAND} ${_install_gate_args}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/install/check_install_consumer.cmake)
    # Its own budget, because `ctest --timeout 300` (added so a hang reports itself) is too small for the
    # heaviest entry in the suite: it installs the tree and then configures AND builds an out-of-tree
    # consumer, which on a Windows runner takes longer than 300 s (measured 2026-10-22, job `windows`:
    # "InstallConsumerSmoke (Timeout)"). A property wins over `--timeout`.
    set_tests_properties(InstallConsumerSmoke PROPERTIES LABELS "install" RUN_SERIAL TRUE TIMEOUT 900)
    # Proves it can fail: the same consumer asks for version 0.0.1, which no installed metadata
    # offers. Without this, a ConfigVersion file that stopped comparing versions would look fine.
    add_test(NAME InstallConsumerSmokeSelfTest
        COMMAND ${CMAKE_COMMAND} -DCPP_PINYIN_SELF_TEST=ON
                "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
                "-DVERSION=${cpp-pinyin2_VERSION}"
                "-DGENERATOR=${CMAKE_GENERATOR}"
                "-DINSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}"
                "-DWORK=${CMAKE_BINARY_DIR}/tests/install-selftest"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/install/check_install_consumer.cmake)
    set_tests_properties(InstallConsumerSmokeSelfTest PROPERTIES LABELS "install" RUN_SERIAL TRUE)
    # And this proves the input is really REQUIRED, and really used: the wrapper runs the gate twice --
    # once with no -DINSTALL_LIBDIR and once with a bogus one -- and insists each call is rejected for
    # the right reason. Without this leg, dropping the argument would silently restore the hardcoded
    # `lib/`: green on Ubuntu, red on every RPM-family distribution, which is the bug being fixed here.
    # NOTE: this deliberately does NOT use ctest's WILL_FAIL + PASS_REGULAR_EXPRESSION pair. Measured
    # 2026-10-02: ctest reported "Required regular expression found" and ***Failed*** -- the two do not
    # compose, so that form would have proved nothing about the reason. The wrapper asserts the exit
    # code and the message itself.
    add_test(NAME InstallConsumerSmokeRequiresLibdirSelfTest
        COMMAND ${CMAKE_COMMAND}
                "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
                "-DVERSION=${cpp-pinyin2_VERSION}"
                "-DWORK=${CMAKE_BINARY_DIR}/tests/install-nolibdir"
                "-DGATE=${CMAKE_CURRENT_SOURCE_DIR}/install/check_install_consumer.cmake"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/install/check_gate_requires_libdir.cmake)
    set_tests_properties(InstallConsumerSmokeRequiresLibdirSelfTest PROPERTIES LABELS "install" RUN_SERIAL TRUE)
else ()
    message(STATUS "CPP_PINYIN2_INSTALL=OFF -- InstallConsumerSmoke is not registered (there are no "
            "install rules to exercise; configure with -DCPP_PINYIN2_INSTALL=ON to run it)")
endif ()

# ---- Public header self-containment ---------------------------------------------------------
# Every header under include/cpp-pinyin2 must compile **on its own**: included first, twice (which
# also exercises the include guard), with nothing else in the translation unit. A header that
# forgets <string>/<vector>/<cstdint> therefore fails THIS build instead of the user's build.
# The requirement was documented but had no automated guard (docs/BACKLOG.md,
# "公共头自包含"), and a manual sweep in the audit had to be re-done by hand to be trusted.
# EXCLUDE_FROM_ALL keeps the extra TUs (one per public header -- 15 of them) out of normal builds; the
# CTest entry builds them.
set(_selfcheck_targets "")
# NOTE: CMAKE_SOURCE_DIR, not PROJECT_SOURCE_DIR -- this file declares its own project()
# (line 2), so PROJECT_SOURCE_DIR would be tests/ and the glob would silently match nothing
# (measured: the guard compiled zero translation units and reported success).
# GLOB_RECURSE for the same reason, one level down: the public headers are grouped into
# core/ dialects/ ext/ subdirectories, and a non-recursive pattern would once again match nothing
# while this target went on "succeeding". The FATAL_ERROR below is the second half of that guard --
# a zero-match glob is a build error here, never a green build with nothing compiled into it.
file(GLOB_RECURSE _public_headers CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/include/cpp-pinyin2/*.h)
if (NOT _public_headers)
    message(FATAL_ERROR
        "HeaderSelfCheck: the glob under include/cpp-pinyin2/ matched no header at all. "
        "The public headers are grouped into subdirectories; fix this pattern instead of letting "
        "the guard compile zero translation units and report success.")
endif ()
foreach (_hdr IN LISTS _public_headers)
    get_filename_component(_hdr_name ${_hdr} NAME_WE)
    # Include the header the way a user does -- relative to the include root, so the same pattern
    # works whether the header sits at the top level or inside core/ dialects/ ext/.
    file(RELATIVE_PATH _hdr_rel ${CMAKE_SOURCE_DIR}/include ${_hdr})
    set(_selfcheck_tu ${CMAKE_CURRENT_BINARY_DIR}/selfcheck/${_hdr_name}.cpp)
    file(WRITE ${_selfcheck_tu}
        "#include <${_hdr_rel}>\n#include <${_hdr_rel}>\n")
    add_library(SelfCheck_${_hdr_name} OBJECT EXCLUDE_FROM_ALL ${_selfcheck_tu})
    target_link_libraries(SelfCheck_${_hdr_name} PRIVATE cpp-pinyin2::cpp-pinyin2)
    list(APPEND _selfcheck_targets SelfCheck_${_hdr_name})
endforeach ()
add_custom_target(HeaderSelfCheck DEPENDS ${_selfcheck_targets}
    COMMENT "Compiling every public header stand-alone (${_selfcheck_targets})")
add_test(NAME HeaderSelfCheck
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target HeaderSelfCheck)
# RUN_SERIAL is not tidiness here: this test BUILDS in the shared build directory, so on a stale tree it can
# relink the library while other tests are running it, and on Windows a running DLL cannot be overwritten.
# Reproduced 2/2 on 2026-10-02 by refreshing the mtime of all 446 tracked files and running the suite with
# -j 8 -- the link failed with "cannot open output file bin\libcpp-pinyin2.dll: Permission denied", which
# names the mechanism outright. The two earlier sightings (once after a reconfigure, once after a git
# reset --hard) were the same event: both leave the tree looking stale. Serialising removes the overlap,
# which is the whole cause; the cost is one 3.8 s test that no longer overlaps.
set_tests_properties(HeaderSelfCheck PROPERTIES RUN_SERIAL TRUE)

# AC-4 (docs/DECISIONS.md §6): "dict can be compiled and tested without tone/engine". The target
# below compiles src/dict/*.cpp plus a driver that uses the builder directly, and it deliberately
# does NOT link cpp-pinyin2 -- so a reintroduced dependency on the tone or engine layers breaks this
# build instead of hiding inside the library (AC-3). No Catch2 here either: a library-free driver
# needs no framework, and the point is precisely that nothing else is pulled in.
file(GLOB _dict_src CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/dict/*.cpp)
# DictUtil.cpp is the dict layer's file I/O implementation; it lives in src/dict/, so it
# U16Str.cpp (utf8 -> utf16) is the same story. Both are dict-layer prerequisites, not tone/engine code. Moving
# DictUtil.{h,cpp} into src/dict/ is the follow-up that would make this line unnecessary -- and it is
# also what AC-3's directory rule asks for.
add_executable(DictSelfCheck ${CMAKE_CURRENT_SOURCE_DIR}/dict/selfcheck_main.cpp
    ${CMAKE_SOURCE_DIR}/src/dict/DictUtil.cpp ${CMAKE_SOURCE_DIR}/src/U16Str.cpp ${_dict_src})
target_include_directories(DictSelfCheck PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
# U16Str.cpp defines utf8strToU16str as CPP_PINYIN2_EXPORT; compiling it without the export define would
# make every user of it expect a dllimport and fail at link time. Same define as the library.
target_compile_definitions(DictSelfCheck PRIVATE CPP_PINYIN2_LIBRARY)
add_test(NAME DictSelfCheck COMMAND DictSelfCheck)
# Same reason as HeaderSelfCheck above: this test BUILDS inside the shared build directory, so it
# must not overlap tests that are running the library it sits next to.
set_tests_properties(DictSelfCheck PROPERTIES RUN_SERIAL TRUE)

# ---- Load-time regression gate ----------------------------------------------------------------
# Not a timing gate: the Dictionary Loading section swings ~12% run to run (measured: Mandarin
# min 21.4 / max 23.9 ms over 5 runs), so the gate compares a MIN-OF-N against a threshold with a
# ~20x margin (500 ms vs ~24 ms) and can only fire on a catastrophic regression. The negative case
# proves the gate can fail; the self-test covers the min/threshold logic without a dictionary.
add_test(NAME LoadCheck COMMAND LoadCheck "${CMAKE_BINARY_DIR}/bin/dict")
set_tests_properties(LoadCheck PROPERTIES LABELS "slow" TIMEOUT 1800)
add_test(NAME LoadCheckSelfTest COMMAND LoadCheck --self-test)
add_test(NAME LoadCheckNegativeCase COMMAND LoadCheck "${CMAKE_BINARY_DIR}/bin/dict" --n 1 --threshold-ms 0)
set_tests_properties(LoadCheckNegativeCase PROPERTIES WILL_FAIL TRUE)

# Same strict set as the library (defined once in the root CMakeLists.txt).
target_link_libraries(CppPinyinTests PRIVATE cpp-pinyin2_dev_flags)
target_link_libraries(Benchmark PRIVATE cpp-pinyin2_dev_flags)
foreach (_warn_target IN LISTS _selfcheck_targets)
    target_link_libraries(${_warn_target} PRIVATE cpp-pinyin2_dev_flags)
endforeach ()

# "The id -> dialect path is the registry the caller hands in", as a program with a negative control.
#
# Until 2026-10-05 this was a pair of executables differing by one translation unit, because
# registration used to happen in a static-initialization list of a TU that the linker was free to
# drop (CPP_PINYIN_REGISTER_DIALECT). That macro and the process-level registry behind it are gone:
# a registry is now owned by the caller, so the only question left is whether the SAME call behaves
# differently depending on what the caller registered. One binary answers it under two modes:
#
#   HostOwnedRegistry      (the caller registered the probe)  -> the id must RESOLVE and open
#   HostOwnedRegistryBare  (the caller registered nothing)    -> the id must NOT resolve, and the
#                                                                error must name the id
#
# The bare one is the negative control for the first: without it, a "does the id resolve?" assertion
# that passed for any other reason (a built-in fallback, a leftover process-level table) would look
# like proof.
add_executable(HostOwnedRegistry ${CMAKE_CURRENT_SOURCE_DIR}/registry/host_owned_registry_main.cpp)
target_link_libraries(HostOwnedRegistry PRIVATE cpp-pinyin2::cpp-pinyin2 cpp-pinyin2_dev_flags)
add_test(NAME HostOwnedRegistry COMMAND HostOwnedRegistry --expect-registered)
set_tests_properties(HostOwnedRegistry PROPERTIES LABELS "registry")
add_test(NAME HostOwnedRegistryBare COMMAND HostOwnedRegistry --expect-absent)
set_tests_properties(HostOwnedRegistryBare PROPERTIES LABELS "registry")

# AC-3 as a machine gate (docs/DECISIONS.md): no lower layer may include an upper one. Before this
# test existed the rule was only ever checked by hand with grep, and every report had to carry that
# caveat. Failing loudly when Python is missing is deliberate: a silently skipped gate is worse than
# no gate, because it looks green.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME LayerIncludes COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_layers.py)
    set_tests_properties(LayerIncludes PROPERTIES RUN_SERIAL TRUE)

    # The documents' own numbers, checked against the tree. This session found four cases of a number in
    # prose that nothing could contradict (a gate that was never registered, a ledger quoting a stale
    # count, a tautological drift record, an argument printed but not passed), so the checker is now a
    # gate too. Only the self-test is registered here: it needs no build directory, because it writes three
    # synthetic fixtures under the system temp dir (none copied from this repository), first proves each is
    # recognised as the claim it targets, and then requires the gate's own numbers quoted verbatim to pass
    # while the same phrasing with one number changed, and a wrong "public headers + C ABI + plugin
    # declarations" line, are both reported. The real-document pass needs a CTest inventory, so it is run by
    # the pre-delivery checklist (the local, untracked board harness), which has one.
    # No --ctest-inventory: the self-test needs no build, and passing one made the entry depend on a build
    # directory that exists only after a configure -- the same species of dependency the board replay caught.
    add_test(NAME DocClaimsSelfTest
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_doc_claims.py --self-test)
    set_tests_properties(DocClaimsSelfTest PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        LABELS "doc")

    # The frozen surfaces as a machine gate (measured 2026-10-03): of the three frozen lists --
    # public headers (16 on that day), C ABI symbols (16) and the C++ plugin surface (16) -- only the C ABI one
    # had a CTest entry (ExportGate, and only in shared builds). The plugin surface and the header
    # set were owned by tools/check_bindings.py, which nothing in this repository called except two
    # CI workflows whose trigger workflow has never run on GitHub. A gate that only runs in CI is a
    # gate that has never been observed to run, so it is wired here.
    #
    # No flags on purpose: with no --nupkg/--wheel/--tag this is the repository-side subset (frozen
    # lists, header hygiene, XML comments, workflow/artifact wiring, single version source, dialect
    # surface). The artifact- and tag-shaped checks stay in CI, where a package and a tag actually
    # exist -- passing them here would fail for reasons that have nothing to do with this tree.
    #
    # Both directions are covered: BindingGates runs the checks, BindingGatesSelfTest proves the
    # checker can fail (dozens of fixtures, most of them negative; the tool prints its own N/N). The counts live in the three
    # frozen lists -- read from there, never restated here: a restated count drifts, and this comment
    # kept saying sixteen/sixteen/sixteen long after the surfaces had moved on.
    # `BindingGates` checks the version chain, and place 5 of that chain is
    # `csharp/CppPinyin/version.props` -- a GENERATED, gitignored file. Nothing generated it before `ctest`
    # ran, so a clean tree (measured: the `mingw` CI job, 2026-10-22) failed the gate for a file the release
    # flow produces elsewhere. Generating it here costs nothing (it needs only the version) and keeps the
    # check honest: the chain is verified rather than skipped.
    if (NOT EXISTS "${CMAKE_SOURCE_DIR}/csharp/CppPinyin/version.props")
        execute_process(
            COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/csharp/tools/emit_build_info.py"
                    --version-props-only
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE _version_props_result
            OUTPUT_QUIET ERROR_QUIET)
        if (NOT _version_props_result EQUAL 0)
            message(WARNING "could not pre-generate csharp/CppPinyin/version.props; "
                            "the BindingGates entry will report it missing")
        endif ()
    endif ()

    add_test(NAME BindingGates
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_bindings.py)
    set_tests_properties(BindingGates PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        LABELS "abi"
        TIMEOUT 300)
    add_test(NAME BindingGatesSelfTest
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_bindings.py --self-test)
    set_tests_properties(BindingGatesSelfTest PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        LABELS "abi"
        TIMEOUT 300)

    # `CMakeLists.txt` no longer globs its sources (audit round 2 G31), so something has to prove the
    # written-down list still equals the tree. No flags, no build tree, no fixtures.
    add_test(NAME SourceList
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_source_list.py)
    set_tests_properties(SourceList PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "docs" TIMEOUT 120)

    # The four gates that had NO entry point anywhere in CI or CTest (2026-10-20, audit round 2 §12.7): a
    # gate reachable only by remembering to type `tools/run_all_gates.py` is a gate that, in practice, only
    # the person who wrote it runs. They are cheap (no build, no network) and each is wired with the flags
    # its own docs prescribe:
    #   * check_gate_inventory wants a `--ctest-inventory` when one exists; without it the tool itself says
    #     it is comparing against nothing, so the entry passes none and relies on the tool's own guard.
    #   * check_comment_language / audit_doc_anchors scan the tree in place. `--include-code` (2026-10-20, G34)
    #     makes the anchor sweep cover source COMMENTS too, not just docs/*.md.
    # check_gate_inventory is deliberately NOT here: it refuses to run without a `--ctest-inventory`, and
    # that file is produced BY `ctest --show-only=json-v1` -- a test inside the run that would generate it is
    # the wrong shape. CI generates the inventory and then runs `run_all_gates.py --with-build`, which is
    # what executes this gate there -- ci.yml deliberately does NOT add a second standalone step.

    add_test(NAME CommentLanguage
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_comment_language.py)
    set_tests_properties(CommentLanguage PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "docs" TIMEOUT 300)

    add_test(NAME DocAnchors
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/audit_doc_anchors.py --limit 0 --include-code)
    # TIMEOUT 300, not 600: check_gate_inventory refuses any entry whose TIMEOUT is long enough to matter
    # unless it also carries `slow`/`corpus`, and this anchor sweep measures well under a second on the whole
    # tree -- a long timeout here would be a claim about cost that the measurement does not support.
    set_tests_properties(DocAnchors PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "docs" TIMEOUT 300)

    # The five generation gates (docs/STABILITY.md section 4.5) used to be reachable only by remembering
    # to run `tools/run_all_gates.py --with-build`; a gate nobody runs is a gate that does not guard. They
    # are registered here without their self-tests on purpose: those five self-tests write fixture trees,
    # and the directory holding them would have to be skipped by the tree scan (or the gate reports its
    # own fixtures as findings) and NOT skipped when the self-test scans them (or it scans nothing and
    # reports red). Both directions were measured; no single location satisfies both. The self-tests still
    # run in `tools/run_all_gates.py`, which runs every self-test on every call.
    foreach(_generation_tool IN ITEMS check_version_tokens check_public_names check_root_names
                                     check_generation_compat check_doc_code_tokens)
        # check_generation_compat gets an explicit opt-in: this repository ships ONE generation, so the
        # window rule cannot be evaluated here, and the gate reports NOT MEASURED rather than a bare PASS.
        # Every other tool in the list takes no arguments.
        if (_generation_tool STREQUAL "check_generation_compat")
            add_test(NAME GenerationGate_${_generation_tool}
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/${_generation_tool}.py
                        --allow-single-generation)
        else ()
            add_test(NAME GenerationGate_${_generation_tool}
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/${_generation_tool}.py)
        endif ()
        set_tests_properties(GenerationGate_${_generation_tool} PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            LABELS "generation"
            TIMEOUT 300)
    endforeach()

    # The cross-reference form every other gate walked past. The anchor audit reads backticked `path:NN`
    # citations, so `[text](relative/path)` was invisible to all five instruments -- measured 2026-10-10,
    # two dead links (in API_en.md / API_zh.md, written as if those files sat at the repository root)
    # survived until a human read the table. LinkChecks resolves every relative link against its own file's
    # directory; LinkChecksSelfTest proves it can fail, including the percent-encoding case this repository
    # actually uses (`Custom%20Chinese%20Dialect.md`) and the marker cases where a mere mention must NOT
    # exempt anything.
    add_test(NAME LinkChecks
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_links.py)
    set_tests_properties(LinkChecks PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        LABELS "docs"
        TIMEOUT 120)
    add_test(NAME LinkChecksSelfTest
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_links.py --self-test)

    # The references a reader actually follows from CODE. Every documentation gate above reads the
    # documents; none read the citations a public header makes into them. Measured 2026-10-20 (audit
    # round 2, G15/G16/G17): `Types.h` cited a CONTRACT section that does not exist, `ManTone.h` cited
    # a TEST_RECORD section that had been archived, and `include/` + `src/` carried ~70 uses of
    # decision numbers like `(S7)`/`(D4)` whose ledgers were retired. DocRefsGate enforces three
    # things: a section reference resolves, every decision number is registered in
    # `docs/DECISIONS.md` (and a public header may only cite one whose rule ships with the
    # package), and a citation of the retired test-data path carries its archive pointer. The self-test plants
    # one violation per rule and requires each to go red.
    add_test(NAME DocRefsGate
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_doc_refs.py)
    set_tests_properties(DocRefsGate PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        LABELS "docs"
        TIMEOUT 120)
    add_test(NAME DocRefsGateSelfTest
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_doc_refs.py --self-test)
    set_tests_properties(DocRefsGateSelfTest PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        LABELS "docs"
        TIMEOUT 120)

    # A tracked fixture that names its own provenance, and the provenance was never checked. Measured
    # 2026-10-20 (audit round 2, G24): `conformance/cases.json` said `generated_by_commit: 03b28db`, a commit
    # `git cat-file -e` cannot resolve in this repository AT ALL -- and every gate stayed green, because they
    # all read the fixture's CASES and none read the field that says what tree those cases came from. The R9
    # repair was a human running `git cat-file -t` by hand; this entry is what makes the repair stick.
    # ConformanceProvenanceGate asserts the value is a commit in THIS repository (`cat-file -e <v>^{commit}`,
    # so a blob with the same prefix does not pass); ConformanceProvenanceGateSelfTest proves the four
    # verdicts in a sandbox -- own commit passes, unknown commit is reported with the exact regeneration
    # command, a missing field is reported, and a directory that is not its own checkout SKIPs (exit 2 under
    # --require). No --require here: a source tarball has no `.git` and reporting SKIP is the honest answer
    # there; the full gate runner passes --require, where an unverifiable provenance must not look green.
    add_test(NAME ConformanceProvenanceGate
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_conformance_provenance.py)
    set_tests_properties(ConformanceProvenanceGate PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        LABELS "docs"
        TIMEOUT 120)
    add_test(NAME ConformanceProvenanceGateSelfTest
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_conformance_provenance.py --self-test)
    set_tests_properties(ConformanceProvenanceGateSelfTest PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        LABELS "docs"
        TIMEOUT 120)

    # The API pair's completeness. The five documentation gates all check that what the documents SAY is
    # true; none asked whether they cover the public surface at all. Measured 2026-10-10, they did not:
    # `Syllable` was used in prose while never being defined, and `Error` / `ErrorException` / `ToneKind` /
    # `ReadingSource` / `StyleDecl` / `ToneConverter` had no entry either. The index table in the API pair is
    # now the declaration of intent, and this gate holds it to the headers in both directions.
    add_test(NAME PublicSurfaceIndex
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_public_surface.py)
    set_tests_properties(PublicSurfaceIndex PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        LABELS "docs"
        TIMEOUT 120)
    add_test(NAME PublicSurfaceIndexSelfTest
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_public_surface.py --self-test)
    set_tests_properties(PublicSurfaceIndexSelfTest PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        LABELS "docs"
        TIMEOUT 120)
else()
    message(FATAL_ERROR "Python3 not found: the AC-3 include gate (tools/check_layers.py) would not run")
endif()

# Labels for the entries that had none, found by tools/check_gate_inventory.py. That tool FAILS
# on an unlabelled entry, so the decision is made when the entry is added rather than later.
# Until this existed, `ctest -L abi` silently missed CApiSmoke and HeaderSelfCheck, which are
# two of the guards of the frozen surfaces. These calls sit at TOP LEVEL (after the endif):
# CApiSmoke/HeaderSelfCheck/DictSelfCheck are created above the Python3 block, so a properties
# call inside that block would not see them. DumpEquivalenceSelfTest is NOT here: it is
# created in tools/CMakeLists.txt, and labelling it from this file made CMake stop with
# "Can not find test to add properties to" -- so its label lives there instead.
set_tests_properties(CApiSmoke PROPERTIES LABELS "abi")   # it exercises the frozen C ABI surface from C
set_tests_properties(HeaderSelfCheck PROPERTIES LABELS "abi")   # one of the three frozen-surface guards (the public headers)
set_tests_properties(DictSelfCheck PROPERTIES LABELS "ac4")   # the AC-4 machine proof: the dict layer builds without the library
set_tests_properties(LoadCheckSelfTest PROPERTIES LABELS "slow")   # self-test of a slow entry; run it with the entry it guards
set_tests_properties(LayerIncludes PROPERTIES LABELS "ac3")   # the AC-3 include gate
set_tests_properties(LinkChecksSelfTest PROPERTIES LABELS "docs")   # the fast doc gate's self-test; run it with the entry it guards
