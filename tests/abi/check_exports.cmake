# tests/abi/check_exports.cmake -- the C ABI surface gate (P2 acceptance item 3, plan doc 6.7.8).
#
# WHAT IT PROVES
#   1. The shared library exports EXACTLY the cppp_* functions in expected_c_api_symbols.txt. Both
#      directions are failures: a missing symbol breaks a binding at load time (inside ctypes or
#      P/Invoke, where the only diagnostic is "entry point not found"), and an extra one is an
#      unreviewed change to a documented surface.
#   2. No internal C++ symbol leaks out (EngineImpl, PhraseGate). That is what -fvisibility=hidden /
#      -Wl,--exclude-all-symbols in the root CMakeLists.txt are for; this is the test that notices
#      when they stop working -- e.g. someone adds a helper to a public header without the
#      CPP_PINYIN2_EXPORT macro. (Measured 2026-10-01: on MinGW/PE the linker exported EVERY global
#      C++ symbol, internal ones included -- 250+ of them -- until --exclude-all-symbols was added.
#      That is exactly the leak this gate exists to catch, and it caught it on its first run.)
#
# WHY A SCRIPT, NOT A C++ TEST: the subject is the BINARY's dynamic symbol table, which a linked
# test process cannot portably inspect about itself. This script reads it with nm/objdump instead.
#
# USAGE
#   cmake -DLIB=<libcpp-pinyin2.dll|.so|.dylib> -DEXPECTED=<expected_c_api_symbols.txt> \
#         -DWORK=<writable dir for the dump> [-DTOOL=<objdump|nm>] -P check_exports.cmake
#   cmake -DCPP_PINYIN_SELF_TEST=ON -DEXPECTED=<same file> -DWORK=<same dir> -P check_exports.cmake
#
# The self-test is not decoration: it feeds the checker synthetic dumps that MUST fail (a missing
# symbol, an unreviewed addition, an internal leak) plus one that must pass. Without it, a checker
# that silently stopped comparing would look exactly like a passing gate.

cmake_minimum_required(VERSION 3.17)

# ------------------------------------------------------------------ expected list ---

if (NOT DEFINED EXPECTED)
    message(FATAL_ERROR "check_exports: -DEXPECTED=<expected_c_api_symbols.txt> is required")
endif ()
if (NOT EXISTS "${EXPECTED}")
    message(FATAL_ERROR "check_exports: expected-symbol list not found: ${EXPECTED}")
endif ()

file(STRINGS "${EXPECTED}" _expected_lines)
set(_expected "")
foreach (_line IN LISTS _expected_lines)
    string(STRIP "${_line}" _line)
    if (_line STREQUAL "" OR _line MATCHES "^#")
        continue()
    endif ()
    list(APPEND _expected "${_line}")
endforeach ()
list(REMOVE_DUPLICATES _expected)
list(SORT _expected)
list(LENGTH _expected _expected_count)
if (_expected_count EQUAL 0)
    message(FATAL_ERROR "check_exports: ${EXPECTED} lists no symbols (a gate with an empty "
            "expectation would always pass)")
endif ()

# ------------------------------------------------------------------ checker core ---
#
# Reads a dump FILE and compares the exported cppp_* names against the expected list. A file, not a
# string: file(STRINGS) does the line splitting. Measured 2026-10-01 -- splitting the same 165 KB
# dump by hand with string(REPLACE "\n" ";") silently produced ONE element (the control case on a
# two-line literal worked), so the gate reported "exported=0" against a library that exported all 16
# symbols then (the list has 19 as of 2026-10-10; the count is whatever
# tests/abi/expected_c_api_symbols.txt holds, and that file is the authority).
# Do not go back to hand-splitting: read the dump from a file.

function(cppp_exports_verdict dump_file out_verdict out_detail)
    file(STRINGS "${dump_file}" _lines)

    set(_found "")
    set(_leaks "")
    foreach (_line IN LISTS _lines)
        string(STRIP "${_line}" _line)
        if (_line STREQUAL "")
            continue()
        endif ()

        # The symbol name is the last whitespace-separated token of the line; Mach-O adds a leading
        # underscore that the C ABI does not have.
        string(REGEX REPLACE "^.*[ \t]" "" _token "${_line}")
        string(REGEX REPLACE "^_" "" _token "${_token}")
        if (_token MATCHES "^cppp2_[A-Za-z0-9_]+$")
            list(APPEND _found "${_token}")
        endif ()

        foreach (_internal IN ITEMS EngineImpl PhraseGate)
            if (_line MATCHES "${_internal}")
                list(APPEND _leaks "${_internal}")
            endif ()
        endforeach ()
    endforeach ()

    list(REMOVE_DUPLICATES _found)
    list(SORT _found)
    list(REMOVE_DUPLICATES _leaks)
    list(SORT _leaks)

    set(_missing "")
    foreach (_sym IN LISTS _expected)
        if (NOT _sym IN_LIST _found)
            list(APPEND _missing "${_sym}")
        endif ()
    endforeach ()
    set(_extra "")
    foreach (_sym IN LISTS _found)
        if (NOT _sym IN_LIST _expected)
            list(APPEND _extra "${_sym}")
        endif ()
    endforeach ()

    list(LENGTH _found _found_count)
    list(LENGTH _missing _missing_count)
    list(LENGTH _extra _extra_count)
    list(LENGTH _leaks _leaks_count)

    set(_detail "exported=${_found_count} expected=${_expected_count}")
    if (_missing_count GREATER 0)
        set(_detail "${_detail} missing=[${_missing}]")
    endif ()
    if (_extra_count GREATER 0)
        set(_detail "${_detail} unexpected=[${_extra}]")
    endif ()
    if (_leaks_count GREATER 0)
        set(_detail "${_detail} leaked=[${_leaks}]")
    endif ()

    if (_missing_count GREATER 0 OR _extra_count GREATER 0 OR _leaks_count GREATER 0)
        set(${out_verdict} "FAIL" PARENT_SCOPE)
    else ()
        set(${out_verdict} "OK" PARENT_SCOPE)
    endif ()
    set(${out_detail} "${_detail}" PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------ work directory ---

if (DEFINED WORK AND NOT WORK STREQUAL "")
    set(_work "${WORK}")
else ()
    set(_work "${CMAKE_CURRENT_BINARY_DIR}")
endif ()
if (NOT EXISTS "${_work}")
    file(MAKE_DIRECTORY "${_work}")
endif ()
if (NOT IS_DIRECTORY "${_work}")
    message(FATAL_ERROR "check_exports: -DWORK=${_work} is not a directory")
endif ()

# ------------------------------------------------------------------ self-test mode ---

if (CPP_PINYIN_SELF_TEST)
    # NOTE: the synthetic dumps are joined into ONE string per case BEFORE the cases are collected
    # into a list. Collecting them while they are still `;`-separated lists would let CMake split
    # each case into 16 list items -- measured 2026-10-01: the 'ok' case then saw a one-symbol dump
    # and reported FAIL, i.e. the self-test failed on correct input.
    set(_dump_ok "${_expected}")
    set(_dump_missing "${_expected}")
    list(REMOVE_ITEM _dump_missing "cppp2_query")
    set(_dump_extra "${_expected}")
    list(APPEND _dump_extra "cppp2_experimental_new_thing")
    set(_dump_leak "${_expected}")
    list(APPEND _dump_leak "_ZN6Pinyin11EngineImpl8assembleEv")

    foreach (_var IN ITEMS _dump_ok _dump_missing _dump_extra _dump_leak)
        string(REPLACE ";" "\n" ${_var} "${${_var}}")
    endforeach ()

    # Synthetic dumps go through the same file+file(STRINGS) path the real check uses, so the
    # self-test covers the plumbing too -- not just the comparison.
    set(_cases "ok|OK|${_dump_ok}"
            "missing_symbol|FAIL|${_dump_missing}"
            "unreviewed_addition|FAIL|${_dump_extra}"
            "internal_leak|FAIL|${_dump_leak}")
    list(LENGTH _cases _cases_count)

    foreach (_case IN LISTS _cases)
        string(REPLACE "|" ";" _parts "${_case}")
        list(GET _parts 0 _name)
        list(GET _parts 1 _want)
        list(GET _parts 2 _text)
        set(_file "${_work}/selftest_${_name}.txt")
        file(WRITE "${_file}" "${_text}\n")
        cppp_exports_verdict("${_file}" _got _detail)
        file(REMOVE "${_file}")
        if (NOT _got STREQUAL _want)
            message(FATAL_ERROR "ExportGateSelfTest: case '${_name}' expected ${_want} but the "
                    "checker said ${_got} (${_detail}) -- the gate no longer detects what it claims")
        endif ()
        message(STATUS "ExportGateSelfTest: case '${_name}' -> ${_got} (${_detail}) [expected]")
    endforeach ()
    message(STATUS "ExportGateSelfTest: all ${_cases_count} cases behaved as expected -- this gate "
            "can fail")
    return()
endif ()

# ------------------------------------------------------------------ real run ---

if (NOT DEFINED LIB)
    message(FATAL_ERROR "check_exports: -DLIB=<shared library> is required")
endif ()
if (NOT EXISTS "${LIB}")
    message(FATAL_ERROR "check_exports: shared library not found: ${LIB} "
            "(this gate only applies to BUILD_SHARED_LIBS=ON builds)")
endif ()

get_filename_component(_lib_ext "${LIB}" EXT)
if (_lib_ext STREQUAL ".dll")
    # PE: objdump -p prints the export table; the names are in the "[Ordinal/Name Pointer] Table"
    # section (the "Export Address Table" above it lists RVAs only).
    set(_tool_kind objdump)
    set(_tool_args -p)
elseif (_lib_ext STREQUAL ".dylib")
    # Mach-O: nm -g on the library lists external defined symbols.
    set(_tool_kind nm)
    set(_tool_args -g --defined-only)
else ()
    # ELF: -D is the dynamic symbol table -- the only one a dlopen/ctypes consumer can see.
    set(_tool_kind nm)
    set(_tool_args -D --defined-only)
endif ()

if (DEFINED TOOL AND NOT TOOL STREQUAL "")
    set(_tool "${TOOL}")
else ()
    find_program(_tool ${_tool_kind})
endif ()
if (NOT _tool)
    message(FATAL_ERROR "check_exports: ${_tool_kind} not found; pass -DTOOL=<path> to point at one")
endif ()

set(_dump_file "${_work}/export_dump.txt")
execute_process(COMMAND "${_tool}" ${_tool_args} "${LIB}"
        RESULT_VARIABLE _rc
        OUTPUT_FILE "${_dump_file}"
        ERROR_VARIABLE _stderr)
if (NOT _rc EQUAL 0)
    message(FATAL_ERROR "check_exports: '${_tool} ${_tool_args} ${LIB}' failed with ${_rc}: "
            "${_stderr}")
endif ()
file(SIZE "${_dump_file}" _dump_size)
if (_dump_size EQUAL 0)
    message(FATAL_ERROR "check_exports: '${_tool} ${_tool_args}' wrote an empty dump -- refusing to "
            "report success on a read that produced nothing")
endif ()

cppp_exports_verdict("${_dump_file}" _verdict _detail)
message(STATUS "ExportGate: ${LIB}")
message(STATUS "ExportGate: tool=${_tool} ${_tool_args}; dump=${_dump_size} bytes; ${_detail}")
if (_verdict STREQUAL "OK")
    message(STATUS "ExportGate: OK -- the C ABI surface matches ${EXPECTED}")
else ()
    message(FATAL_ERROR "ExportGate: the C ABI surface changed. If this is intentional, update "
            "${EXPECTED} and docs/PLUGIN_INTERFACE_AND_BINDINGS.md section 6.7 in the same commit.")
endif ()
