# Dialect-set gate: this repository ships exactly the dialects listed in tests/data/dialect_set/expected_dialects.txt.
#
# Why it exists (measured 2026-10-01): copying a third language's dictionary directory into data/dialects/ --
# the dictionary repository produces out/dict/<lang>/ with exactly the same file layout -- left all 18
# CTest entries green, because every other gate names its dialects explicitly or only checks "at least
# two are registered". The hard rule (`docs/DECISIONS.md` §2.1 6.7.4 rule 2b: data/dialects,
# package copies, runtime registry and fixture dialects are all exactly {mandarin, cantonese}) had no
# implementation. This gate is that implementation.
#
# Two legs, compared against the SAME frozen file:
#   1. the directory names under data/dialects/          (the data that ships)
#   2. the ids the library actually registers       (via `CApiSmoke <dict_dir> --print-dialect-ids`)
#
# Usage:
#   cmake -DREPO_DICT_DIR=<dir> -DEXPECTED=<file> -DSMOKE=<exe> [-DDICT_DIR=<dir>] -DWORK=<dir>
#         [-DCPP_PINYIN_SELF_TEST=ON] -P check_dialect_set.cmake
#
# CPP_PINYIN_SELF_TEST=ON proves the gate CAN fail: it synthesises three trees (exactly the expected set,
# one extra directory, one missing directory) and requires the first to pass and the other two to fail.
# It deliberately does NOT read the real data/dialects -- a self-test that needed real state would report the
# state's problems, not the gate's. (Same discipline as the ExportGate self-test.)

if (NOT DEFINED REPO_DICT_DIR OR NOT DEFINED EXPECTED OR NOT DEFINED WORK)
    message(FATAL_ERROR "check_dialect_set: -DREPO_DICT_DIR, -DEXPECTED and -DWORK are required")
endif ()
file(MAKE_DIRECTORY "${WORK}")

function(_dialect_dirs_from root out_var)
    set(_dirs "")
    if (IS_DIRECTORY "${root}")
        file(GLOB _entries LIST_DIRECTORIES true RELATIVE "${root}" "${root}/*")
        foreach (_e IN LISTS _entries)
            if (IS_DIRECTORY "${root}/${_e}")
                list(APPEND _dirs "${_e}")
            endif ()
        endforeach ()
    endif ()
    list(SORT _dirs)
    set(${out_var} "${_dirs}" PARENT_SCOPE)
endfunction()

file(STRINGS "${EXPECTED}" _expected_lines)
set(_expected "")
foreach (_line IN LISTS _expected_lines)
    string(STRIP "${_line}" _line)
    if (NOT _line STREQUAL "")
        list(APPEND _expected "${_line}")
    endif ()
endforeach ()
list(SORT _expected)
if (NOT _expected)
    message(FATAL_ERROR "check_dialect_set: the expected list is empty -- refusing to compare against nothing")
endif ()

if (CPP_PINYIN_SELF_TEST)
    # -----------------------------------------------------------------------------------------
    # Self-test: synthetic trees only, so a failure here means the comparison is broken.
    # -----------------------------------------------------------------------------------------
    file(REMOVE_RECURSE "${WORK}/synth")
    set(_last "")
    foreach (_id IN LISTS _expected)
        set(_last "${_id}")
    endforeach ()

    foreach (_id IN LISTS _expected)
        file(MAKE_DIRECTORY "${WORK}/synth/exact/${_id}")
        file(MAKE_DIRECTORY "${WORK}/synth/extra/${_id}")
        if (NOT _id STREQUAL _last)
            file(MAKE_DIRECTORY "${WORK}/synth/missing/${_id}")
        endif ()
    endforeach ()
    file(MAKE_DIRECTORY "${WORK}/synth/extra/example")   # the case that motivated this gate
    # A stray file next to the directories must be ignored: the gate compares language directories,
    # not "everything under data/dialects".
    file(WRITE "${WORK}/synth/exact/README.txt" "not a dialect\n")

    _dialect_dirs_from("${WORK}/synth/exact" _exact_dirs)
    if (NOT _exact_dirs STREQUAL _expected)
        message(FATAL_ERROR "DialectSetGateSelfTest: the 'exact' tree should have matched, got [${_exact_dirs}]")
    endif ()

    _dialect_dirs_from("${WORK}/synth/extra" _extra_dirs)
    if (_extra_dirs STREQUAL _expected)
        message(FATAL_ERROR "DialectSetGateSelfTest: an extra directory was NOT detected -- the gate cannot fail")
    endif ()

    _dialect_dirs_from("${WORK}/synth/missing" _missing_dirs)
    if (_missing_dirs STREQUAL _expected)
        message(FATAL_ERROR "DialectSetGateSelfTest: a missing directory was NOT detected -- the gate cannot fail")
    endif ()

    message(STATUS "DialectSetGateSelfTest: exact=[${_expected}] accepted; extra=[${_extra_dirs}] and "
            "missing=[${_missing_dirs}] both rejected")
    return ()
endif ()

# ---------------------------------------------------------------------------------------------
# Leg 1: the directories under data/dialects/
# ---------------------------------------------------------------------------------------------
_dialect_dirs_from("${REPO_DICT_DIR}" _repo_dirs)
if (NOT _repo_dirs STREQUAL _expected)
    message(FATAL_ERROR "DialectSetGate: data/dialects has [${_repo_dirs}], expected [${_expected}]. "
            "A third language must stay in the dictionary repository; adding it here needs an explicit "
            "decision (plan 6.7.4 rule 2b).")
endif ()

# ---------------------------------------------------------------------------------------------
# Leg 2: the ids the library actually registers
# ---------------------------------------------------------------------------------------------
if (NOT DEFINED SMOKE)
    message(STATUS "DialectSetGate: data/dialects is [${_expected}] (runtime registry not checked: -DSMOKE not given)")
    return ()
endif ()
if (NOT EXISTS "${SMOKE}")
    message(FATAL_ERROR "DialectSetGate: -DSMOKE=${SMOKE} does not exist")
endif ()

execute_process(COMMAND "${SMOKE}" "${DICT_DIR}" --print-dialect-ids
        OUTPUT_FILE "${WORK}/runtime_dialect_ids.txt"
        RESULT_VARIABLE _rc)
if (NOT _rc EQUAL 0)
    message(FATAL_ERROR "DialectSetGate: the smoke binary could not list the runtime dialects (exit ${_rc})")
endif ()
file(STRINGS "${WORK}/runtime_dialect_ids.txt" _id_lines)
set(_registry "")
foreach (_line IN LISTS _id_lines)
    string(STRIP "${_line}" _line)
    if (NOT _line STREQUAL "")
        list(APPEND _registry "${_line}")
    endif ()
endforeach ()
list(SORT _registry)
if (NOT _registry STREQUAL _expected)
    message(FATAL_ERROR "DialectSetGate: the runtime registry is [${_registry}], expected [${_expected}]. "
            "Registering a built-in dialect is a code change: it must be accompanied by its dictionary "
            "assets and by an explicit decision (plan 6.7.4 rule 2b).")
endif ()

message(STATUS "DialectSetGate: data/dialects and the runtime registry are both [${_expected}]")
