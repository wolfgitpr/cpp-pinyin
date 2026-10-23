# check_install_consumer.cmake -- does a *consumer* project actually work against an installed tree?
#
# WHY THIS EXISTS. Nothing owned the install rules. On 2026-10-01 the first probe of them, done by
# hand, produced a `find_package(cpp-pinyin2 2.0.0)` failure that looked like a packaging defect and
# was not one: the probe had passed `-DCMAKE_PREFIX_PATH=$inst` unquoted to a native command, so CMake
# received the four characters `$inst` (CMakeCache.txt: `CMAKE_PREFIX_PATH:UNINITIALIZED=$inst`). The
# install rules themselves are the only way a downstream project consumes this library, and no test
# in the suite executed `cmake --install` at all, so a broken export, a missing dictionary or a
# version file that disagrees with the project version would have shipped silently. This gate is that
# consumer: it installs the build tree into a scratch prefix, writes a small consumer project against
# the INSTALLED tree only, configures, builds, links and runs it, and then checks the installed
# metadata carries the same version the project declares.
#
# It checks three things the in-tree tests cannot:
#   1. `cmake --install` succeeds and lays out headers, library, CMake package files, pkg-config file
#      and dictionaries under the prefix (the dictionary DESTINATION is relative, so `--prefix` must
#      actually reach it).
#   2. A consumer that knows only `find_package(cpp-pinyin2 <version>)` and the target name
#      `cpp-pinyin2::cpp-pinyin2` can build and run.
#   3. The version chain agrees: the pkg-config `Version:` and the ConfigVersion file come from the
#      same `project(... VERSION)` the project promises (plan D32).
#
# Inputs (all required):
#   -DBUILD_DIR=<dir>      build tree to install
#   -DVERSION=<x.y.z>      the version this project declares (must match the installed metadata)
#   -DWORK=<dir>           scratch directory, wiped at the start
#   -DGENERATOR=<name>     generator to reuse for the consumer (may be empty)
#   -DINSTALL_LIBDIR=<dir> where the library and the CMake package files go, i.e. the project's
#                          CMAKE_INSTALL_LIBDIR. Required, because it is NOT always `lib`: measured
#                          2026-10-02, AlmaLinux (the manylinux image) reports `lib64`, and this gate
#                          hardcoded `lib` in six places, so it failed there at every CMake version.
#                          Passing the value in is deliberate -- guessing it (globbing lib*/) would
#                          also accept an install tree that put things in the wrong place.
#   -DCPP_PINYIN_SELF_TEST=ON  instead: ask for a version that does not exist and require the consumer
#                              configure to FAIL. A gate that cannot fail is decoration.
cmake_minimum_required(VERSION 3.17)

if (NOT DEFINED BUILD_DIR OR NOT DEFINED VERSION OR NOT DEFINED WORK OR NOT DEFINED INSTALL_LIBDIR)
    message(FATAL_ERROR "usage: cmake -DBUILD_DIR=<build> -DVERSION=<x.y.z> -DWORK=<dir> "
            "-DINSTALL_LIBDIR=<lib|lib64|...> -P check_install_consumer.cmake")
endif ()

# ctest -C <cfg> exports this; a multi-config consumer build needs it, a single-config one ignores it.
set(_config_args "")
if (DEFINED ENV{CTEST_CONFIGURATION_TYPE} AND NOT "$ENV{CTEST_CONFIGURATION_TYPE}" STREQUAL "")
    set(_config_args --config "$ENV{CTEST_CONFIGURATION_TYPE}")
endif ()

set(_prefix "${WORK}/prefix")
set(_proj "${WORK}/consumer")
file(REMOVE_RECURSE "${_prefix}" "${_proj}")

function (_run_and_report _what)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    if (NOT _rc EQUAL 0)
        # Show the tail only: a failing `cmake --install` prints one line per file.
        string(REGEX REPLACE "\n" ";" _lines "${_out}")
        list(LENGTH _lines _n)
        math(EXPR _from "${_n} - 12")
        if (_from LESS 0)
            set(_from 0)
        endif ()
        string(REGEX REPLACE "\n" ";" _errlines "${_err}")
        list(LENGTH _errlines _en)
        math(EXPR _efrom "${_en} - 12")
        if (_efrom LESS 0)
            set(_efrom 0)
        endif ()
        message(FATAL_ERROR "${_what} failed (exit ${_rc}).
--- stdout (tail) ---\n${_out}\n--- stderr (tail) ---\n${_err}")
    endif ()
    set(_LAST_OUT "${_out}" PARENT_SCOPE)
endfunction ()

# ---- 1. install ------------------------------------------------------------------------------
# --config matters for multi-config generators (the Windows CI job runs ctest -C Release against a
# Visual Studio tree): without it `cmake --install` would install the wrong configuration.
_run_and_report("cmake --install" ${CMAKE_COMMAND} --install "${BUILD_DIR}" --prefix "${_prefix}"
        ${_config_args})

set(_must_exist
    "include/cpp-pinyin2/ext/c_api.h"
    "${INSTALL_LIBDIR}/cmake/cpp-pinyin2/cpp-pinyin2Config.cmake"
    "${INSTALL_LIBDIR}/cmake/cpp-pinyin2/cpp-pinyin2ConfigVersion.cmake"
    "${INSTALL_LIBDIR}/cmake/cpp-pinyin2/cpp-pinyin2Targets.cmake"
    "${INSTALL_LIBDIR}/pkgconfig/cpp-pinyin2.pc"
    "share/cpp-pinyin2/dict/mandarin/word.txt"
    "share/cpp-pinyin2/dict/cantonese/word.txt")
foreach (_rel IN LISTS _must_exist)
    if (NOT EXISTS "${_prefix}/${_rel}")
        message(FATAL_ERROR "installed tree is missing ${_rel} (prefix ${_prefix})")
    endif ()
endforeach ()

# ---- 2. the version chain ---------------------------------------------------------------------
file(READ "${_prefix}/${INSTALL_LIBDIR}/pkgconfig/cpp-pinyin2.pc" _pc)

# Q14: the dictionary location must be reachable from BOTH package descriptions, and must point at a
# directory that actually holds the data. The `.pc` side has exported `dictdir=` all along; the CMake
# side gained `CPP_PINYIN_DICT_DIR` in the same batch as this check. `dialect.json` is the file the
# loaders read first, so requiring it makes an empty-or-wrong path fail here rather than at runtime.
string(REGEX MATCH "dictdir=([^\n\r]*)" _pc_dict "${_pc}")
if (NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "cpp-pinyin2.pc does not export dictdir=:\n${_pc}")
endif ()
set(_pc_dict_dir "${CMAKE_MATCH_1}")
# `${prefix}` in a .pc file is a literal for pkg-config to expand (prefix=${pcfiledir}/../..), so it
# is still literal here -- resolve it against the installed pc file's own location.
get_filename_component(_pc_dir "${_prefix}/${INSTALL_LIBDIR}/pkgconfig" ABSOLUTE)
get_filename_component(_pc_prefix "${_pc_dir}/../.." ABSOLUTE)
string(REPLACE [[${prefix}]] "${_pc_prefix}" _pc_dict_dir "${_pc_dict_dir}")
if (NOT IS_DIRECTORY "${_pc_dict_dir}")
    message(FATAL_ERROR "cpp-pinyin2.pc's dictdir is not a directory: ${_pc_dict_dir}")
endif ()
if (NOT EXISTS "${_pc_dict_dir}/mandarin/dialect.json")
    message(FATAL_ERROR "cpp-pinyin2.pc's dictdir holds no mandarin/dialect.json: ${_pc_dict_dir}")
endif ()
message(STATUS "pkg-config dictdir points at the installed data: ${_pc_dict_dir}")
if (NOT _pc MATCHES "Version: ${VERSION}")
    message(FATAL_ERROR "cpp-pinyin2.pc does not carry Version: ${VERSION}.\n--- pc file ---\n${_pc}")
endif ()
# The Cflags line must say what the CMake target says (2026-10-20 audit, G11): the library requires
# C++17 and, in a static install, the consumer needs `CPP_PINYIN2_STATIC` too (it selects the
# no-dllimport branch of core/PinyinGlobal.h). Neither is inherited through the .pc the way it is
# through the exported CMake target, so a consumer that only uses pkg-config is the one who finds out.
# `BUILD_SHARED` is what tests/CMakeLists.txt passes; `BUILD_SHARED_LIBS` is unset inside a `-P` script
# (a `-P` run has no cache), so testing the latter read as "static" in every configuration. Absent means
# "not told", and the shared expectation is the lenient one: this leg must not invent a failure.
set(_pc_expect_static OFF)
if (DEFINED BUILD_SHARED AND NOT BUILD_SHARED)
    set(_pc_expect_static ON)
endif ()
if (NOT _pc MATCHES "Cflags:.*-std=c\\+\\+17")
    message(FATAL_ERROR "cpp-pinyin2.pc's Cflags does not name -std=c++17.\n--- pc file ---\n${_pc}")
endif ()
if (_pc_expect_static AND NOT _pc MATCHES "Cflags:.*-DCPP_PINYIN2_STATIC")
    message(FATAL_ERROR
            "cpp-pinyin2.pc's Cflags does not name -DCPP_PINYIN2_STATIC in a static install.\n--- pc file ---\n${_pc}")
endif ()
file(READ "${_prefix}/${INSTALL_LIBDIR}/cmake/cpp-pinyin2/cpp-pinyin2ConfigVersion.cmake" _cv)
if (NOT _cv MATCHES "PACKAGE_VERSION \"${VERSION}\"")
    message(FATAL_ERROR "cpp-pinyin2ConfigVersion.cmake does not declare PACKAGE_VERSION \"${VERSION}\"")
endif ()

# ---- 3. a consumer of the installed tree ------------------------------------------------------
set(_wanted "${VERSION}")
set(_expect_failure OFF)
if (CPP_PINYIN_SELF_TEST)
    # Ask for a version NEWER than the installed one. Every compatibility mode refuses that, so a live
    # ConfigVersion refuses it; if the configure still succeeds, that file is comparing nothing and this
    # gate proves nothing. (Measured 2026-10-01, when the root declared AnyNewerVersion: this self-test
    # first asked for 0.0.1, which was legitimately ACCEPTED, so the self-test failed instead of the
    # gate. The policy itself -- a cross-major 1.x request must be refused -- is asserted in step 3b
    # below, which runs in every gate run rather than only under CPP_PINYIN_SELF_TEST.)
    set(_wanted "999.0.0")
    set(_expect_failure ON)
endif ()

file(WRITE "${_proj}/CMakeLists.txt" "cmake_minimum_required(VERSION 3.17)
project(consumer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(cpp-pinyin2 ${_wanted} REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE cpp-pinyin2::cpp-pinyin2)
add_executable(capi capi.cpp)
target_link_libraries(capi PRIVATE cpp-pinyin2::cpp-pinyin2)
")

# A second consumer, using the C ABI rather than the C++ facade: D2/D4 of
# docs/DECISIONS.md section 4.1 are about what a host that links this library directly
# can ask it, and the only honest place to prove that is against an INSTALLED tree.
file(WRITE "${_proj}/capi.cpp" "#include <cpp-pinyin2/ext/c_api.h>
#include <cstdio>
#include <cstring>
int main(int argc, char **argv) {
    if (argc < 3) { std::fprintf(stderr, \"usage: capi <dict_root> probe|incompatible\\n\"); return 2; }
    if (std::strcmp(argv[2], \"probe\") == 0) {
        const char *candidate = cppp2_dict_dir_candidate();
        std::printf(\"%s\\n\", candidate != nullptr ? candidate : \"(null)\");
        return 0;
    }
    cppp2_error err{};
    cppp2_engine *handle = cppp2_open(nullptr, \"mandarin\", argv[1], &err);
    if (std::strcmp(argv[2], \"incompatible\") == 0) {
        if (handle != nullptr) { cppp2_destroy(handle); std::fprintf(stderr, \"opened a dictionary it should have refused\\n\"); return 1; }
        if (err.code != CPPP2_ERR_DICT_INCOMPATIBLE) {
            std::fprintf(stderr, \"code %d, expected %d: %s\\n\", err.code, CPPP2_ERR_DICT_INCOMPATIBLE,
                         err.detail != nullptr ? err.detail : \"(no detail)\");
            return 1;
        }
        std::printf(\"%s\\n\", err.detail != nullptr ? err.detail : \"\");
        return 0;
    }
    if (handle == nullptr) { std::fprintf(stderr, \"open failed: %s\\n\", err.detail != nullptr ? err.detail : \"\"); return 1; }
    cppp2_destroy(handle);
    std::printf(\"opened\\n\");
    return 0;
}
")
file(WRITE "${_proj}/main.cpp" "#include <cpp-pinyin2/core/Engine.h>
#include <cpp-pinyin2/ext/Registry.h>
#include <filesystem>
#include <iostream>
int main(int argc, char **argv) {
    if (argc < 2) { std::cerr << \"usage: consumer <dict_dir>\\n\"; return 2; }
    const std::filesystem::path dictDir(argv[1]);
    cpp_pinyin::v2::Registry registry;
    auto opened = cpp_pinyin::v2::Engine::open(registry, dictDir, \"mandarin\");
    if (!opened) { std::cerr << \"not ready: \" << opened.error().detail << \"\\n\"; return 1; }
    cpp_pinyin::v2::Engine man = std::move(opened.value());
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
    std::cout << man.query(u8\"\\u4e2d\\u56fd\\u4eba\\u6c11\", cfg).value().join() << \"\\n\";
    return 0;
}
")

set(_gen_args "")
if (DEFINED GENERATOR AND NOT GENERATOR STREQUAL "")
    set(_gen_args -G "${GENERATOR}")
endif ()

# The consumer MUST be configured for the SAME configuration as the library it links (2026-10-21, MSVC leg).
# Without this, a single-config generator leaves `CMAKE_BUILD_TYPE` empty and the consumer is built with MSVC's
# DEBUG runtime (`/MDd`) against a RELEASE `cpp-pinyin2.dll` (`/MD`): two CRT heaps in one process, and the
# `std::string` / `Registry` objects that cross the module boundary come back mangled. Measured on MSVC 19.51:
# the C++ consumer printed "not ready: " with an EMPTY detail while the C face opened the very same dictionary
# successfully -- MinGW hid the defect because g++ has no separate debug CRT, which is why this only surfaced
# when the MSVC build was actually run. `CTEST_CONFIGURATION_TYPE` is what `ctest -C <cfg>` exports; an unset
# value (single-config parent) falls back to Release, matching how this project configures.
set(_type_args "")
if (DEFINED ENV{CTEST_CONFIGURATION_TYPE} AND NOT "$ENV{CTEST_CONFIGURATION_TYPE}" STREQUAL "")
    set(_type_args "-DCMAKE_BUILD_TYPE=$ENV{CTEST_CONFIGURATION_TYPE}")
else ()
    set(_type_args "-DCMAKE_BUILD_TYPE=Release")
endif ()

# NOTE: no --config here: that is a build/install flag, not a configure flag.
execute_process(
    COMMAND ${CMAKE_COMMAND} -S "${_proj}" -B "${_proj}/build" ${_gen_args} ${_type_args}
            "-DCMAKE_PREFIX_PATH=${_prefix}"
    RESULT_VARIABLE _cfg_rc OUTPUT_VARIABLE _cfg_out ERROR_VARIABLE _cfg_err)
if (_expect_failure)
    if (_cfg_rc EQUAL 0)
        message(FATAL_ERROR "self-test: find_package accepted the nonexistent version ${_wanted}")
    endif ()
    message(STATUS "self-test OK: find_package refused version ${_wanted} (exit ${_cfg_rc})")
    return ()
endif ()
if (NOT _cfg_rc EQUAL 0)
    message(FATAL_ERROR "consumer configure failed (exit ${_cfg_rc}).\n${_cfg_out}\n${_cfg_err}")
endif ()

# ---- 3b. the version policy, asserted rather than described ------------------------------------
# COMPATIBILITY SameMajorVersion (CMakeLists.txt): a request from the 1.x line must NOT be satisfied by
# this 2.0 install. It is not a formality -- 1.x's flat headers are not in this tree, so accepting the
# request turns "your requirement cannot be met" into a compile error inside the consumer's own code.
# Under the previous AnyNewerVersion this configure SUCCEEDED.
file(MAKE_DIRECTORY "${_proj}/cross-major")
file(WRITE "${_proj}/cross-major/CMakeLists.txt" "cmake_minimum_required(VERSION 3.17)
project(crossmajor LANGUAGES CXX)
find_package(cpp-pinyin2 1.0.0 REQUIRED)
")
execute_process(
    COMMAND ${CMAKE_COMMAND} -S "${_proj}/cross-major" -B "${_proj}/cross-major/build" ${_gen_args}
            "-DCMAKE_PREFIX_PATH=${_prefix}"
    RESULT_VARIABLE _x_rc OUTPUT_VARIABLE _x_out ERROR_VARIABLE _x_err)
if (_x_rc EQUAL 0)
    message(FATAL_ERROR "find_package(cpp-pinyin2 1.0.0) was ACCEPTED by an installed ${VERSION} tree. "
            "The version policy no longer refuses a cross-major request; the 1.x headers do not exist "
            "in this tree, so the consumer would fail to COMPILE instead of failing to configure.")
endif ()
message(STATUS "version policy OK: a 1.0.0 request is refused by ${VERSION}")

_run_and_report("consumer build" ${CMAKE_COMMAND} --build "${_proj}/build" ${_config_args})

# ---- 4. run it against the installed dictionaries ---------------------------------------------
# The library may be shared: Windows finds it next to the executable by PATH, the others by their
# library search path. Set all three; the ones that do not apply are ignored.
#
# The executable is located by candidate list rather than by CMAKE_EXECUTABLE_SUFFIX: in `cmake -P`
# script mode there is no project() and that variable is empty, so the suffix would be missing on
# Windows (measured 2026-10-01 -- the first version of this gate wrote "consumer" and did not exist).
set(_exe "")
foreach (_cand IN ITEMS
        "${_proj}/build/consumer" "${_proj}/build/consumer.exe"
        "${_proj}/build/Release/consumer.exe" "${_proj}/build/Release/consumer"
        "${_proj}/build/Debug/consumer.exe")
    # FIRST MATCH WINS, not last: the lists are ordered Release-first and put `build/Debug/...` last,
    # so a last-wins loop prefers a DEBUG executable whenever one exists -- and a Debug consumer
    # (MSVC `/MDd`) against a Release `cpp-pinyin2.dll` (`/MD`) is the CRT split documented above:
    # the C++ consumer prints "not ready: " with an EMPTY detail and then does not return. Measured
    # 2026-10-23 on the Windows runner, where the script ran `build/Debug/capi.exe`.
    if (NOT _exe AND EXISTS "${_cand}")
        set(_exe "${_cand}")
    endif ()
endforeach ()
if (NOT _exe)
    message(FATAL_ERROR "the consumer executable was not found under ${_proj}/build after a "
            "successful build")
endif ()
set(_env "PATH=${_prefix}/bin;$ENV{PATH}")
set(_env "${_env};LD_LIBRARY_PATH=${_prefix}/${INSTALL_LIBDIR};$ENV{LD_LIBRARY_PATH}")
set(_env "${_env};DYLD_LIBRARY_PATH=${_prefix}/${INSTALL_LIBDIR};$ENV{DYLD_LIBRARY_PATH}")
# The phase banners below exist because a TIMEOUT is invisible to this script: the consumer's configure/build
# output is captured into variables and printed only when the build FAILS, so a run that stalls prints the last
# banner and nothing after it -- which is what a reader of the Windows job needed (2026-10-22).
message(STATUS "install consumer smoke: phase 4b (running the out-of-tree consumer: ${_exe})")
# TIMEOUT, because a process that never returns is invisible to this script: `OUTPUT_VARIABLE` swallows its
# output, and ctest's own limit only arrives minutes later as a bare "(Timeout)". Measured 2026-10-22 on the
# Windows runner: the run step held for the full 1200 s ctest budget and printed nothing after the banner.
# An EMPTY INPUT FILE, because a child that reads stdin never returns: `execute_process` hands it no stdin, but
# no EOF either, and on the Windows runner this step held until the timeout (measured 2026-10-22). A child that
# does not read input cannot notice the difference.
set(_stdin "${CMAKE_CURRENT_BINARY_DIR}/empty-stdin.txt")
file(WRITE "${_stdin}" "")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "${_env}" "${_exe}" "${_prefix}/share/cpp-pinyin2/dict"
    INPUT_FILE "${_stdin}"
    # 45, not 300: the runner outside gives each test its own budget, and a script timeout that sits ABOVE it never
    # fires -- ctest kills the test first and the diagnostic branch below never runs (measured 2026-10-23, the
    # probe printed nothing). The budget has to be lower than whoever is watching.
    TIMEOUT 45
    RESULT_VARIABLE _run_rc OUTPUT_VARIABLE _run_out ERROR_VARIABLE _run_err)
string(STRIP "${_run_out}" _run_out)

# WHEN THE FIRST ATTEMPT DOES NOT RETURN, ASK A SECOND WAY. A process that never returns says nothing about WHY,
# and each guess costs a CI round. So the failure path re-runs the same executable (a) from the prefix's own bin
# directory and (b) without the `cmake -E env` wrapper, each with a short budget, and reports all three outcomes
# together. If either fallback works the culprit is the launch form, not the consumer; if neither does, the
# evidence says so and the next round does not have to re-guess it. Measured 2026-10-23: the Windows runner stalls
# at this step with an install tree that holds no DLL and no EXE at all.
if (NOT _run_rc EQUAL 0)
    file(GLOB _bin_contents "${_prefix}/bin/*")
    get_filename_component(_exe_dir "${_exe}" DIRECTORY)
    file(GLOB _exe_siblings "${_exe_dir}/*")
    message(STATUS "install consumer smoke: attempt 1 exited '${_run_rc}'")
    message(STATUS "install consumer smoke: prefix/bin holds: ${_bin_contents}")
    message(STATUS "install consumer smoke: the consumer's own directory holds: ${_exe_siblings}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "${_env}" "${_exe}" "${_prefix}/share/cpp-pinyin2/dict"
        WORKING_DIRECTORY "${_prefix}/bin"
        INPUT_FILE "${_stdin}"
        TIMEOUT 30
        RESULT_VARIABLE _run2_rc OUTPUT_VARIABLE _run2_out ERROR_VARIABLE _run2_err)
    message(STATUS "install consumer smoke: attempt 2 (cwd = prefix/bin) exited '${_run2_rc}' "
            "stdout '${_run2_out}' stderr '${_run2_err}'")
    execute_process(
        COMMAND "${_exe}" "${_prefix}/share/cpp-pinyin2/dict"
        INPUT_FILE "${_stdin}"
        TIMEOUT 30
        RESULT_VARIABLE _run3_rc OUTPUT_VARIABLE _run3_out ERROR_VARIABLE _run3_err)
    message(STATUS "install consumer smoke: attempt 3 (no env wrapper) exited '${_run3_rc}' "
            "stdout '${_run3_out}' stderr '${_run3_err}'")
endif ()

if (NOT _run_rc EQUAL 0)
    message(FATAL_ERROR "installed consumer exited ${_run_rc} (prefix ${_prefix}, executable ${_exe}, "
            "environment ${_env}): stdout '${_run_out}' stderr '${_run_err}'")
endif ()
if (NOT _run_out STREQUAL "zhong1 guo2 ren2 min2")
    message(FATAL_ERROR "installed consumer printed '${_run_out}', expected 'zhong1 guo2 ren2 min2'")
endif ()

message(STATUS "install consumer smoke OK: ${_prefix}, version ${VERSION}, output '${_run_out}'")

# ---- 5. an out-of-tree dialect, built the way a third party builds one -------------------------
# Phase 3 proves a consumer can link the library. This proves the PLUGIN story separately, because it is
# a different contract: a translation unit that registers its own dialect, compiled against the
# INSTALLED headers only, with find_package pointing at the installed package. It cannot be checked
# in-tree -- there every header is already on the include path and every target already exists, which is
# exactly the state that hides a missing install rule. The example lives in examples/plugin-dialect and
# is not part of this build for the same reason.
#
# What it must demonstrate, all of it asserted below: the dialect resolves by id (so registration ran),
# the engine loads the installed dictionaries through a dictionary root that is an argument, and the
# dialect's own tone converter reaches the result (the example compares against the built-in output,
# transformed the same way, and fails if the two agree).
set(_plugin_src "${CMAKE_CURRENT_LIST_DIR}/../../examples/plugin-dialect")
if (NOT EXISTS "${_plugin_src}/CMakeLists.txt")
    message(FATAL_ERROR "the out-of-tree dialect example is missing at ${_plugin_src}")
endif ()
set(_plugin_cfg_args "-DCMAKE_PREFIX_PATH=${_prefix}")
if (EXISTS "${_prefix}/${INSTALL_LIBDIR}/cmake/cpp-pinyin2")
    # Naming the package directory directly is what a user does when the package is not on the prefix
    # path; both spellings are accepted, and the gate passes both so either one breaking is caught.
    list(APPEND _plugin_cfg_args "-Dcpp-pinyin2_DIR=${_prefix}/${INSTALL_LIBDIR}/cmake/cpp-pinyin2")
endif ()
_run_and_report("plugin dialect configure" ${CMAKE_COMMAND} -S "${_plugin_src}"
        -B "${_proj}/plugin-build" ${_gen_args} ${_type_args} ${_plugin_cfg_args})
_run_and_report("plugin dialect build" ${CMAKE_COMMAND} --build "${_proj}/plugin-build" ${_config_args})

# Same candidate-list reasoning as phase 4: `cmake -P` has no project(), so CMAKE_EXECUTABLE_SUFFIX is
# empty and a name built from it would be wrong on Windows.
set(_plugin_exe "")
foreach (_cand IN ITEMS
        "${_proj}/plugin-build/plugin_dialect" "${_proj}/plugin-build/plugin_dialect.exe"
        "${_proj}/plugin-build/Release/plugin_dialect.exe" "${_proj}/plugin-build/Release/plugin_dialect"
        "${_proj}/plugin-build/Debug/plugin_dialect.exe")
    # FIRST MATCH WINS, not last: the lists are ordered Release-first and put `build/Debug/...` last,
    # so a last-wins loop prefers a DEBUG executable whenever one exists -- and a Debug consumer
    # (MSVC `/MDd`) against a Release `cpp-pinyin2.dll` (`/MD`) is the CRT split documented above:
    # the C++ consumer prints "not ready: " with an EMPTY detail and then does not return. Measured
    # 2026-10-23 on the Windows runner, where the script ran `build/Debug/capi.exe`.
    if (NOT _plugin_exe AND EXISTS "${_cand}")
        set(_plugin_exe "${_cand}")
    endif ()
endforeach ()
if (NOT _plugin_exe)
    message(FATAL_ERROR "the out-of-tree dialect example did not produce an executable under "
            "${_proj}/plugin-build after a successful build")
endif ()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "${_env}" "${_plugin_exe}" "${_prefix}/share/cpp-pinyin2/dict"
    RESULT_VARIABLE _plugin_rc OUTPUT_VARIABLE _plugin_out ERROR_VARIABLE _plugin_err)
if (NOT _plugin_rc EQUAL 0)
    message(FATAL_ERROR "the out-of-tree dialect example exited ${_plugin_rc}: ${_plugin_out}${_plugin_err}")
endif ()
if (NOT _plugin_out MATCHES "RESULT: OK")
    message(FATAL_ERROR "the out-of-tree dialect example ran but did not report success: ${_plugin_out}")
endif ()

# The negative leg: the same binary against a dictionary root that does not contain this dialect's
# tables must fail rather than fall back to something. Without this, a registry that silently answered
# with a built-in dialect would make the run above pass.
#
# EXCEPTION, and it is the point of the feature rather than a hole: a build with CPP_PINYIN2_EMBED_DICTS
# carries the tables itself, so an empty root is LEGAL there and the example must still succeed. Asserting
# failure in that configuration would be asserting that embedding is broken.
string(REGEX REPLACE "[\r\n]+" " | " _plugin_one_line "${_plugin_out}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "${_env}" "${_plugin_exe}" "${_proj}/no-such-dict-root"
    RESULT_VARIABLE _plugin_bad_rc OUTPUT_VARIABLE _plugin_bad_out ERROR_VARIABLE _plugin_bad_err)
if (CPP_PINYIN2_EMBEDDED)
    if (NOT _plugin_bad_rc EQUAL 0)
        message(FATAL_ERROR "this build embeds the dictionaries, so it should have opened against a root with no "
                "tables, but it failed (exit ${_plugin_bad_rc}): ${_plugin_bad_out}${_plugin_bad_err}")
    endif ()
    message(STATUS "out-of-tree dialect OK: ${_plugin_one_line} (embedded build: the tables come from the library)")
else ()
    if (_plugin_bad_rc EQUAL 0)
        message(FATAL_ERROR "the out-of-tree dialect example succeeded with a dictionary root that has no "
                "tables (exit 0): ${_plugin_bad_out}${_plugin_bad_err}")
    endif ()
    message(STATUS "out-of-tree dialect OK: ${_plugin_one_line} (and it refuses an empty dictionary root)")
endif ()

# ---- 6. what the C ABI can tell an installed host (D2 / D4) -------------------------------------
# Two questions, both asked of the INSTALLED tree by a separate consumer that uses only `cppp2_*`:
#   * where did this build put the tables? (`cppp2_dict_dir_candidate`) -- the answer a host with no CMake
#     package and no .pc file otherwise has to hard-code;
#   * what happens when the tables belong to a newer library? (`CPPP2_ERR_DICT_INCOMPATIBLE`).
# Neither can be checked in-tree: in-tree the module directory is the build tree's `bin/`, not an install prefix.
set(_capi_exe "")
foreach (_cand IN ITEMS
        "${_proj}/build/capi" "${_proj}/build/capi.exe"
        "${_proj}/build/Release/capi.exe" "${_proj}/build/Release/capi"
        "${_proj}/build/Debug/capi.exe")
    # FIRST MATCH WINS, not last: the lists are ordered Release-first and put `build/Debug/...` last,
    # so a last-wins loop prefers a DEBUG executable whenever one exists -- and a Debug consumer
    # (MSVC `/MDd`) against a Release `cpp-pinyin2.dll` (`/MD`) is the CRT split documented above:
    # the C++ consumer prints "not ready: " with an EMPTY detail and then does not return. Measured
    # 2026-10-23 on the Windows runner, where the script ran `build/Debug/capi.exe`.
    if (NOT _capi_exe AND EXISTS "${_cand}")
        set(_capi_exe "${_cand}")
    endif ()
endforeach ()
if (NOT _capi_exe)
    message(FATAL_ERROR "the C ABI consumer executable was not found under ${_proj}/build")
endif ()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "${_env}" "${_capi_exe}" "" probe
    RESULT_VARIABLE _probe_rc OUTPUT_VARIABLE _probe_out ERROR_VARIABLE _probe_err)
string(STRIP "${_probe_out}" _probe_out)
if (NOT _probe_rc EQUAL 0)
    message(FATAL_ERROR "cppp2_dict_dir_candidate probe exited ${_probe_rc}: ${_probe_out}${_probe_err}")
endif ()
if (BUILD_SHARED)
    file(TO_CMAKE_PATH "${_prefix}/share/cpp-pinyin2/dict" _expected_candidate)
    if (NOT _probe_out STREQUAL "${_expected_candidate}")
        message(FATAL_ERROR "cppp2_dict_dir_candidate answered '${_probe_out}', expected the installed data "
                "directory '${_expected_candidate}'")
    endif ()
    message(STATUS "cppp2_dict_dir_candidate points at the installed data: ${_probe_out}")
else ()
    # A static library is code inside the host, so "this module" would be the host executable -- the guess
    # the locality rule forbids. NULL is the documented answer, and asserting it keeps it that way.
    if (NOT _probe_out STREQUAL "(null)")
        message(FATAL_ERROR "a STATIC build answered '${_probe_out}' for cppp2_dict_dir_candidate; it must "
                "say (null): there is no module anchor to resolve")
    endif ()
    message(STATUS "cppp2_dict_dir_candidate answers (null) in a static build, as documented")
endif ()

# The doctored copy: same tables, a manifest that asks for a library this build is not. The path EXISTS, so
# the disk wins over any embedded copy -- which is what makes this assertion meaningful in every configuration.
file(REMOVE_RECURSE "${_proj}/bad-dict")
file(COPY "${_prefix}/share/cpp-pinyin2/dict/" DESTINATION "${_proj}/bad-dict")
file(WRITE "${_proj}/bad-dict/mandarin/dialect.json" "{\n    \"id\": \"mandarin\",\n    \"min_library\": \"99.0.0\"\n}\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "${_env}" "${_capi_exe}" "${_proj}/bad-dict" incompatible
    RESULT_VARIABLE _bad_rc OUTPUT_VARIABLE _bad_out ERROR_VARIABLE _bad_err)
string(STRIP "${_bad_out}" _bad_out)
if (NOT _bad_rc EQUAL 0)
    message(FATAL_ERROR "a dictionary that asks for library 99.0.0 was not refused with "
            "CPPP2_ERR_DICT_INCOMPATIBLE (exit ${_bad_rc}): ${_bad_out}${_bad_err}")
endif ()
if (NOT _bad_out MATCHES "requires library")
    message(FATAL_ERROR "the refusal did not name the requirement: '${_bad_out}'")
endif ()

message(STATUS "installed C ABI OK: candidate + incompatible-dictionary refusal both asserted")
