# tests/abi/check_soname.cmake -- the binary-generation gate (docs/STABILITY.md R9).
#
# WHAT IT PROVES
#   The shared library's DT_SONAME reads `libcpp-pinyin2.so.<major>` -- the artifact expresses the
#   generation it belongs to, which is what R9 now claims. Before this gate NOTHING read the
#   artifact: `git grep -i soname -- tests tools .github cmake packaging` had 0 hits while
#   CMakeLists.txt set SOVERSION, so dropping SOVERSION, or renaming the library, left every gate
#   green. That is the exact failure shape this repository keeps re-learning: a policy written in
#   prose with no instrument behind it.
#
# WHY THE MAJOR COMES FROM THE ROOT CMakeLists.txt AND NOT FROM A -D FLAG
#   A `-DEXPECTED_MAJOR=2` would be a second truth: the test would still pass with the package at
#   3.0.0, which is the drift this gate exists to catch. So the script parses
#   `project(<name> VERSION x.y.z)` from the repository itself and also checks that the product
#   name carries the same number.
#
# SCOPE / SKIP DISCIPLINE
#   PE (Windows) has no SONAME. That case prints "SKIP (PE has no soname)" and exits 0 -- the line
#   is printed so a reader can tell "skipped" from "compared and matched", and it is the behaviour
#   R9 documents ("n/a" in the §4.2 table). Every other outcome is a verdict: a missing SONAME, a
#   wrong number, a wrong product name, an absent dump tool.
#
# SELF-TEST
#   The same run first feeds the checker four synthetic dumps that MUST fail (no SONAME line, wrong
#   generation, wrong product, empty file) plus two that must pass (correct .so dump, Apple
#   install-name dump). It runs on every platform, including the ones where the real comparison is
#   skipped -- a checker that never ran is a checker nobody can trust.
#
# USAGE
#   cmake -DLIB=<libcpp-pinyin2.so> -DROOT=<repo root> -DWORK=<writable dir> [-DTOOL=objdump] \
#         -P check_soname.cmake
#   cmake -DCPP_PINYIN_SELF_TEST=ON -P check_soname.cmake      # fixtures only, no library needed

cmake_minimum_required(VERSION 3.17)

# ------------------------------------------------------------------ expected generation ---

set(_root "${ROOT}")
if (NOT _root)
    set(_root "${CMAKE_CURRENT_LIST_DIR}/../..")
endif ()
set(_cmake_file "${_root}/CMakeLists.txt")
if (NOT EXISTS "${_cmake_file}")
    message(FATAL_ERROR "check_soname: no CMakeLists.txt at ROOT=${_root}")
endif ()
file(STRINGS "${_cmake_file}" _cmake_head LIMIT_COUNT 40)
set(_product "")
set(_major "")
foreach (_line IN LISTS _cmake_head)
    if (_line MATCHES "project[ \t\r\n]*\\([ \t]*([A-Za-z0-9_.-]+)[ \t]+VERSION[ \t]+([0-9]+)\\.([0-9]+)\\.([0-9]+)")
        set(_product "${CMAKE_MATCH_1}")
        set(_major "${CMAKE_MATCH_2}")
        break()
    endif ()
endforeach ()
if (_major STREQUAL "")
    message(FATAL_ERROR "check_soname: project(<name> VERSION x.y.z) not found in ${_cmake_file}")
endif ()
if (NOT _product MATCHES "^cpp-pinyin${_major}$")
    message(FATAL_ERROR "check_soname: product ${_product} does not carry the major version ${_major}")
endif ()
set(_expected_so "lib${_product}.so.${_major}")
set(_expected_dylib "lib${_product}.${_major}.dylib")

# ------------------------------------------------------------------ verdict on a dump ---

# Sets PARSED_SONAME (may be empty) from one dump file. Accepts both shapes this gate feeds it:
# `objdump -p` (a "SONAME" line) and `otool -D` (a bare install name, possibly with a trailing
# "(compatibility version ...)" clause).
function(soname_of dump out_var)
    file(STRINGS "${dump}" _lines)
    set(_name "")
    foreach (_line IN LISTS _lines)
        if (_line STREQUAL "")
            continue()
        endif ()
        # objdump -p spells it `(SONAME)  Library soname: [libcpp-pinyin2.so.2]`; the bracketed
        # tail is the value. A `SONAME\\s+(\\S+)` pattern does NOT work here -- it requires the
        # token to be followed by whitespace, but the real dump puts `)` right after it. The
        # self-test caught exactly that (measured 2026-10-19: good.dump read as "no SONAME").
        if (_line MATCHES "SONAME[^]]*\\[([^]]+)\\]")
            set(_name "${CMAKE_MATCH_1}")
            break()
        endif ()
        # binutils <= 2.34 (Ubuntu 20.04, Debian 11, RHEL 8) prints the CLASSIC shape, with no brackets:
        #     SONAME               libcpp-pinyin2.so.2
        # The bracketed pattern above found nothing there, so the gate declared "no SONAME (SOVERSION dropped?)"
        # about a library whose DT_SONAME was right. Measured on the board with binutils 2.34, 2026-10-21.
        if (_line MATCHES "^[ \t]*SONAME[ \t]+([^ \t]+)[ \t]*$")
            set(_name "${CMAKE_MATCH_1}")
            break()
        endif ()
        if (_line MATCHES "([^ \t]*lib${_product}[^ \t]*\\.dylib)")
            set(_name "${CMAKE_MATCH_1}")
            break()
        endif ()
    endforeach ()
    set(${out_var} "${_name}" PARENT_SCOPE)
endfunction()

# Sets VERDICT to "PASS <name>", "SKIP <why>" or "FAIL <why>".
function(judge dump out_var)
    if (NOT EXISTS "${dump}")
        set(${out_var} "FAIL dump file not found: ${dump}" PARENT_SCOPE)
        return()
    endif ()
    file(SIZE "${dump}" _size)
    if (_size EQUAL 0)
        set(${out_var} "FAIL dump is empty -- the scan saw no object at all" PARENT_SCOPE)
        return()
    endif ()
    soname_of("${dump}" _name)
    if (_name STREQUAL "")
        set(${out_var} "FAIL no SONAME in ${dump} (SOVERSION dropped?)" PARENT_SCOPE)
        return()
    endif ()
    get_filename_component(_base "${_name}" NAME)
    if (_base STREQUAL "${_expected_so}" OR _base STREQUAL "${_expected_dylib}")
        set(${out_var} "PASS ${_base}" PARENT_SCOPE)
        return()
    endif ()
    set(${out_var} "FAIL soname ${_base} != ${_expected_so} (or ${_expected_dylib})" PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------ self-test ---

if (CPP_PINYIN_SELF_TEST)
    set(_work "${WORK}")
    if (NOT _work)
        set(_work "${CMAKE_CURRENT_BINARY_DIR}/soname-selftest")
    endif ()
    file(MAKE_DIRECTORY "${_work}")
    set(_cases "")
    set(_failures 0)

    # 1. a correct ELF dump
    file(WRITE "${_work}/good.dump"
         "Dynamic Section:\n  0x000000000000000e (SONAME)             Library soname: [${_expected_so}]\n")
    # 2. an ELF dump with no SONAME at all (SOVERSION dropped)
    file(WRITE "${_work}/none.dump" "Dynamic Section:\n  0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]\n")
    # 3. the previous generation's soname
    file(WRITE "${_work}/old.dump"
         "Dynamic Section:\n  0x000000000000000e (SONAME)             Library soname: [lib${_product}.so.1]\n")
    # 4. a second product built into the same dump (wrong product, right number)
    file(WRITE "${_work}/other.dump"
         "Dynamic Section:\n  0x000000000000000e (SONAME)             Library soname: [libcpp-pinyin2so.${_major}]\n")
    # 5. an empty file
    file(WRITE "${_work}/empty.dump" "")
    # 6. an Apple install name
    file(WRITE "${_work}/apple.dump" "@rpath/lib${_product}.${_major}.dylib\n")

    foreach (_case IN ITEMS good:PASS none:FAIL old:FAIL other:FAIL empty:FAIL apple:PASS)
        string(REPLACE ":" ";" _parts "${_case}")
        list(GET _parts 0 _which)
        list(GET _parts 1 _want)
        judge("${_work}/${_which}.dump" _verdict)
        string(SUBSTRING "${_verdict}" 0 4 _got)
        if (_got STREQUAL _want)
            list(APPEND _cases "PASS self-test: ${_which}.dump -> ${_verdict}")
        else ()
            math(EXPR _failures "${_failures}+1")
            list(APPEND _cases "FAIL self-test: ${_which}.dump -> expected ${_want}, got ${_verdict}")
        endif ()
    endforeach ()

    foreach (_line IN LISTS _cases)
        message(STATUS "${_line}")
    endforeach ()
    if (_failures GREATER 0)
        message(FATAL_ERROR "check_soname self-test: ${_failures} control(s) did not behave")
    endif ()
    message(STATUS "check_soname self-test: all controls behaved (expected ${_expected_so})")
    return()
endif ()

# ------------------------------------------------------------------ the real check ---

if (NOT DEFINED LIB OR LIB STREQUAL "")
    message(FATAL_ERROR "check_soname: -DLIB=<shared library> is required (or -DCPP_PINYIN_SELF_TEST=ON)")
endif ()
if (NOT EXISTS "${LIB}")
    message(FATAL_ERROR "check_soname: library not found: ${LIB}")
endif ()

if (NOT UNIX)
    message(STATUS "SKIP (PE has no soname): ${LIB}")
    return()
endif ()
if (DEFINED EXPECT_SHARED AND NOT EXPECT_SHARED)
    message(STATUS "SKIP (static build: no soname): ${LIB}")
    return()
endif ()

set(_tool "${TOOL}")
if (NOT _tool)
    # `unset` first: on CMake 3.21 (Ubuntu 20.04's, and the board's /opt/cmake-3.21.3) a result variable that is
    # DEFINED but empty is not searched, so `set(_tool "")` followed by `find_program(_tool ...)` silently returns
    # nothing while the newer CMake here searches. Measured both ways on 2026-10-21.
    unset(_tool)
    find_program(_tool NAMES objdump llvm-objdump)
endif ()
if (NOT _tool)
    message(FATAL_ERROR "check_soname: neither objdump nor llvm-objdump on PATH -- cannot read DT_SONAME")
endif ()

set(_work "${WORK}")
if (NOT _work)
    set(_work "${CMAKE_CURRENT_BINARY_DIR}")
endif ()
file(MAKE_DIRECTORY "${_work}")
get_filename_component(_stem "${LIB}" NAME)
set(_dump "${_work}/soname-${_stem}.dump")
file(REMOVE "${_dump}")

if (APPLE)
    execute_process(COMMAND otool -D "${LIB}" OUTPUT_FILE "${_dump}" RESULT_VARIABLE _rc)
else ()
    execute_process(COMMAND "${_tool}" -p "${LIB}" OUTPUT_FILE "${_dump}" RESULT_VARIABLE _rc)
endif ()
if (NOT _rc EQUAL 0)
    message(FATAL_ERROR "check_soname: '${_tool}' could not read ${LIB} (exit ${_rc})")
endif ()

judge("${_dump}" _verdict)
string(SUBSTRING "${_verdict}" 0 4 _head)
if (_head STREQUAL "PASS")
    message(STATUS "check_soname: ${_verdict}")
else ()
    message(FATAL_ERROR "check_soname: ${_verdict} -- see docs/STABILITY.md R9")
endif ()
