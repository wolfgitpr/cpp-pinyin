# check_gate_requires_libdir.cmake -- two controls on the install gate's INSTALL_LIBDIR input.
#
# WHY. The first version of this leg used ctest's WILL_FAIL together with PASS_REGULAR_EXPRESSION,
# expecting "it failed, and for that reason". Measured 2026-10-02 on this machine: ctest printed
# "Required regular expression found" and still reported ***Failed*** -- the two properties do not
# compose the way that reads, so the leg proved nothing about the reason. These two controls do:
#
#   A. no -DINSTALL_LIBDIR at all -> must fail, and must name the input in its message.
#   B. -DINSTALL_LIBDIR=bogus-lib -> must fail, and must name the bogus path it went looking for.
#
# B is the one that proves the value is USED and not merely required: with the old hardcoded `lib/`,
# the message would have said lib/cmake/... whatever this input contained.
#
# Inputs (all required):
#   -DBUILD_DIR=<dir>   build tree the gate installs
#   -DVERSION=<x.y.z>   the project version the gate pins
#   -DWORK=<dir>        scratch directory for the gate
#   -DGATE=<path>       the gate under test (install/check_install_consumer.cmake)
cmake_minimum_required(VERSION 3.17)

foreach (_need IN ITEMS BUILD_DIR VERSION WORK GATE)
    if (NOT DEFINED ${_need})
        message(FATAL_ERROR "usage: cmake -DBUILD_DIR=<build> -DVERSION=<x.y.z> -DWORK=<dir> "
                "-DGATE=<path> -P check_gate_requires_libdir.cmake")
    endif ()
endforeach ()
if (NOT EXISTS "${GATE}")
    message(FATAL_ERROR "the gate under test does not exist at ${GATE}")
endif ()

function (_expect_rejection _what _pattern _extra_args)
    file(REMOVE_RECURSE "${WORK}")
    execute_process(
        COMMAND ${CMAKE_COMMAND} "-DBUILD_DIR=${BUILD_DIR}" "-DVERSION=${VERSION}"
                "-DWORK=${WORK}" ${_extra_args} -P "${GATE}"
        RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    if (_rc EQUAL 0)
        message(FATAL_ERROR "${_what}: the gate ACCEPTED a call it was supposed to reject (exit 0)")
    endif ()
    if (NOT "${_out}${_err}" MATCHES "${_pattern}")
        message(FATAL_ERROR "${_what}: the gate failed (exit ${_rc}) but not for the expected reason; "
                "looked for '${_pattern}'.\n--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
    endif ()
    message(STATUS "${_what}: rejected with exit ${_rc} and said why")
endfunction ()

_expect_rejection("A: no INSTALL_LIBDIR given" "INSTALL_LIBDIR" "")
_expect_rejection("B: INSTALL_LIBDIR=bogus-lib" "bogus-lib/cmake" "-DINSTALL_LIBDIR=bogus-lib")

message(STATUS "the install gate requires INSTALL_LIBDIR, and uses the value it is given")
