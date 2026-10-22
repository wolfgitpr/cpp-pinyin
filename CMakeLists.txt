cmake_minimum_required(VERSION 3.17)
# 2.0.0 is the v2 development line -- this branch has never been released, so the number is the
# contract for its first release, not a bump of the old 1.0.x line. This line is the single authority
# for the version: every other document points here instead of repeating the number (D32/D34, and
# docs/BACKLOG.md ISS-015 for what went wrong when three copies disagreed).
project(cpp-pinyin2 VERSION 2.0.0 LANGUAGES CXX)

# Expose dictionary source directory for FetchContent consumers
set(CPP_PINYIN2_DICT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data/dialects" CACHE PATH
    "Path to cpp-pinyin2 dictionary files (for FetchContent users)")

set(CMAKE_CXX_STANDARD 17)
# Require the standard instead of silently decaying to a lower one (GCC/Clang would
# otherwise fall back with a warning; MSVC would just pick its default).
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ----------------------------------
# Build Options
# ----------------------------------
option(BUILD_SHARED_LIBS "Build using shared libraries" ON)
# This is the project's test switch, and the only one consulted. Python 3 is required when it is ON,
# because the corpus gates are Python scripts (see tests/CMakeLists.txt). The CTest module's generic
# `BUILD_TESTING` is deliberately NOT included: it used to define a second switch that nothing read,
# so `-DBUILD_TESTING=OFF` looked like it did something and did not.
option(CPP_PINYIN2_BUILD_TESTS "Build test cases (requires a Python 3 interpreter)" OFF)
option(CPP_PINYIN2_BUILD_EXAMPLES "Build example programs" OFF)
# Sanitizers are a CI concern, not a user-facing build mode, and they are OFF by default. The reason
# this repository had no sanitizer option at all before 2026-10-02 is written in docs/BACKLOG.md: the
# development machine is MinGW, which has no libasan. That reason does not apply to the Linux runners,
# which is where this option is meant to be turned on (ci.yml, job linux-sanitizers).
option(CPP_PINYIN2_SANITIZE "Build the library, tools and tests with ASan and UBSan (GCC/Clang)" OFF)
# ON only for a top-level build: when this project is embedded (FetchContent / add_subdirectory), the
# parent's `install` must not suddenly install this library, its headers, its dictionaries and a .pc
# file. `PROJECT_IS_TOP_LEVEL` would say this in one word but needs CMake 3.21, and the floor declared
# here is 3.17.
set(_cpp_pinyin_top_level OFF)
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_cpp_pinyin_top_level ON)
endif ()
option(CPP_PINYIN2_INSTALL "Install library" ${_cpp_pinyin_top_level})

# ----------------------------------
# CMake Settings
# ----------------------------------
if (NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif ()

if (NOT DEFINED CMAKE_LIBRARY_OUTPUT_DIRECTORY)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
endif ()

if (NOT DEFINED CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
endif ()

# ----------------------------------
# Reproducible artefacts (2026-10-20 audit round 2, G13)
#
# MEASURED BEFORE THIS BLOCK EXISTED: two Release builds of the same commit in two directories
# (.tmp/repro-a and .tmp/repro-b) produced `libcpp-pinyin2.a` with DIFFERENT SHA-256 -- while all 23
# object files inside them were byte-identical (compared one by one). So the nondeterminism was in the
# PACKAGING, not in the code:
#   * `ar` stamps every archive member with its mtime and uid unless told otherwise (`D` modifier);
#   * the PE linker writes the build time into the DLL header (MinGW; MSVC has `/Brepro` for the same).
# A distribution that rebuilds this library cannot reproduce a shipped binary from the same sources and
# compiler without these, which is exactly what "reproducible build" promises (and what makes a
# third-party rebuild verifiable rather than merely plausible).
#
# -ffile-prefix-map is the third piece: it keeps absolute build paths out of `__FILE__` and DWARF, so a
# debug build from /home/a and /home/b produces the same bytes. The paths are mapped to `.` rather than
# omitted: the relative part ("src/Engine.cpp") still identifies the file, which is what a debugger needs.
# ----------------------------------
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # CMAKE_CURRENT_SOURCE_DIR, not CMAKE_SOURCE_DIR: under add_subdirectory the latter is the CONSUMER's
    # root, and mapping their tree to "." would rewrite their `__FILE__` paths, not ours.
    add_compile_options(-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=. -ffile-prefix-map=${CMAKE_BINARY_DIR}=.)
    # These two are directory-scoped VARIABLES with no target-level equivalent, so they would also change the
    # archive rules of a consumer that pulls this project in via add_subdirectory. Only set them when we own
    # the build: a library that silently rewrites its host's toolchain rules is worse than one that is not
    # reproducible inside someone else's tree.
    if (_cpp_pinyin_top_level)
        set(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_AR> qcD <TARGET> <LINK_FLAGS> <OBJECTS>")
        set(CMAKE_CXX_ARCHIVE_APPEND "<CMAKE_AR> qD <TARGET> <LINK_FLAGS> <OBJECTS>")
        if (NOT APPLE)
            # `ar qcD` on its own IS deterministic; the plain `ranlib` that CMake runs afterwards is NOT -- it
            # rewrites the symbol index with the current time in the member header, which is where the last
            # difference between two builds came from (measured 2026-10-20: `ar qcD` twice -> identical,
            # then `ranlib` -> different, `ranlib -D` -> identical again). Apple's ranlib has no `-D`, and on
            # Apple the archive is not the shipped artefact anyway (the .dylib is).
            #
            # APPLE, WHY NOTHING IS ADDED (2026-10-20, audit round 2 P3): the two ways to spend effort here
            # would both be wrong. Substituting `libtool -static` for the archive step changes the file
            # format a consumer links against, to fix a directory that is not shipped; and Apple's own
            # `.dylib` determinism comes from the linker's `-no_uuid`/`ZERO_AR_DATE`, not from the archive.
            # If Apple artefacts ever become a published thing, the check belongs in the release job where a
            # real Mach-O can be compared -- not in a rule that no machine here can test.
            set(CMAKE_CXX_ARCHIVE_FINISH "<CMAKE_RANLIB> -D <TARGET>")
        endif ()
    endif ()
endif ()
if (MINGW)
    # `--no-insert-timestamp` is a PE-linker option; passing it on ELF/Mach-O is an error, hence the guard.
    add_link_options(-Wl,--no-insert-timestamp)
endif ()
if (MSVC)
    # /Brepro: deterministic debug info, PE headers with a fixed timestamp and no /manifest resources that
    # embed paths. MEASURED 2026-10-21 on MSVC 19.51 (VS2026), Ninja + vcvars64: accepted, build clean.
    add_compile_options(/Brepro)
    add_link_options(/Brepro)
    # /pathmap is MSVC's counterpart of -ffile-prefix-map: without it `__FILE__` and the PDB carry the
    # absolute build directory, so two machines produce different bytes for the same sources. The `.` target
    # keeps the relative part ("src/Engine.cpp"), which is what a debugger needs -- same reasoning as the
    # GNU/Clang branch above.
    #
    # /experimental:deterministic IS REQUIRED, NOT OPTIONAL (measured 2026-10-21, MSVC 19.51): on its own,
    # /pathmap does NOT fail the build -- cl prints `warning D9007: "/pathmap:" requires
    # "/experimental:deterministic"; option ignored` once per translation unit (59 of them in one build) and
    # then does nothing, so the "no absolute paths" property was silently absent on MSVC. The comment here
    # used to claim the opposite ("would fail the build loudly rather than silently have no effect"); the
    # build proved it wrong. Keep the pair together: /pathmap without this switch is decoration.
    add_compile_options("/experimental:deterministic" "/pathmap:${CMAKE_CURRENT_SOURCE_DIR}=.")
endif ()


if (MSVC)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /manifest:no")
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} /manifest:no")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /manifest:no")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8")
    # Global.h's CPP_PINYIN2_NODISCARD keys off `__cplusplus >= 201703L`, and MSVC reports __cplusplus
    # as 199711L unless this switch is given -- which would silently drop every [[nodiscard]] on the
    # C ABI. MEASURED 2026-10-21 on MSVC 19.51 / VS2026, `/std:c++17`, preprocessing `__cplusplus`:
    # without this switch it expands to 199711L, with it to 201703L -- so the switch is load-bearing,
    # not belt-and-braces. (The macro is unconditional now anyway, see Global.h; this keeps the value a
    # consumer sees honest.)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zc:__cplusplus")

    if (NOT DEFINED CMAKE_DEBUG_POSTFIX)
        set(CMAKE_DEBUG_POSTFIX "d")
    endif ()
endif ()

if (CPP_PINYIN2_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)
endif ()
# ----------------------------------
# Project sources and targets
# ----------------------------------
# Both built-in dialects are always compiled
#
# (Until 2026-10-01 there were CPP_PINYIN_DIALECT_MANDARIN / _CANTONESE options that filtered their
# sources out of the library. They were deleted -- see docs/DECISIONS.md D28: nothing in
# tests/ or CI ever set them, one OFF combination cannot even build its tests, and DialectSetGate now
# treats a missing dialect as a failure, so the switch only offered a way to break a rule that this
# repository enforces and to produce a build nobody verifies.)
#
# The dialect-specific sources are cleanly separable anyway: the core carries no dialect branches (the
# names appear in comments only -- evidence: a hardcoded Mandarin normalizer once mangled Cantonese
# user dictionaries, `jyut6` -> `jyu6t6`, which is exactly why that hook is per-dialect now). A NEW
# dialect does not need conditional compilation either: it enters through the public plugin path -- a
# `Dialect` description plus a `ToneConverter` -- which is why third parties no longer have to fork
# this repository. Boundary rules: `docs/DECISIONS.md` §2.1;
# how-to: docs/Custom Chinese Dialect.md.
#
#   Mandarin : src/Mandarin.cpp  src/toneUtil/ManTone.cpp  src/toneUtil/ManToneUtil.cpp
#   Cantonese: src/Cantonese.cpp src/toneUtil/CanTone.cpp
# ----------------------------------

# EXPLICIT, NOT GLOB_RECURSE (2026-10-20, audit round 2 G31). A glob decides the build's content from
# whatever happens to be on disk: a stray file silently joins the library, and a deleted header keeps
# building from a stale copy until someone reconfigures (`CONFIGURE_DEPENDS` only mitigates that on
# generators that honour it). For a project whose point is a FROZEN public surface, the file set is part
# of the contract, so it is written down. `tools/check_source_list.py` compares this list with the tree:
# adding a file and forgetting the list is red, not silent.
set(CPP_PINYIN2_SOURCES
    src/Cantonese.cpp
    src/DictLocation.cpp
    src/Engine.cpp
    src/EngineImpl.cpp
    src/EngineImplLoad.cpp
    src/G2pglobal.cpp
    src/Mandarin.cpp
    src/Outcome.cpp
    src/Result.cpp
    src/ToneConverter.cpp
    src/ToneSandhi.cpp
    src/U16Str.cpp
    src/c_api_lifecycle.cpp
    src/c_api_query.cpp
    src/dict/DictManifest.cpp
    src/dict/DictUtil.cpp
    src/dict/EmbeddedTables.cpp
    src/dict/PhraseIndex.cpp
    src/dict/PhraseLayers.cpp
    src/toneUtil/CanTone.cpp
    src/toneUtil/ManTone.cpp
    src/toneUtil/ManToneUtil.cpp
    src/toneUtil/ReadingCodec.cpp)
set(CPP_PINYIN2_HEADERS
    include/cpp-pinyin2/core/Engine.h
    include/cpp-pinyin2/core/G2pglobal.h
    include/cpp-pinyin2/core/Global.h
    include/cpp-pinyin2/core/PinyinGlobal.h
    include/cpp-pinyin2/core/Syllable.h
    include/cpp-pinyin2/core/ToneConverter.h
    include/cpp-pinyin2/core/ToneSandhi.h
    include/cpp-pinyin2/core/Types.h
    include/cpp-pinyin2/core/U16Str.h
    include/cpp-pinyin2/core/Version.h
    include/cpp-pinyin2/dialects/CanTone.h
    include/cpp-pinyin2/dialects/ManTone.h
    include/cpp-pinyin2/ext/Dialect.h
    include/cpp-pinyin2/ext/Registry.h
    include/cpp-pinyin2/ext/c_api.h
    src/BuiltinDialects.h
    src/Capabilities.h
    src/EngineImpl.h
  src/Utf8Conv.h
    src/c_api_internal.h
    src/dict/DictManifest.h
    src/dict/DictUtil.h
    src/dict/EmbeddedTables.h
    src/dict/PhraseIndex.h
    src/dict/PhraseLayers.h
    src/toneUtil/ManToneUtil.h
    src/toneUtil/ReadingCodec.h)
set(_src ${CPP_PINYIN2_SOURCES} ${CPP_PINYIN2_HEADERS})

# ----------------------------------
# Embedded dictionaries (D3 of docs/DECISIONS.md section 4.1)
#
# OFF (default): the tables are only on disk, installed to <datadir>/cpp-pinyin2/dict.
# ON           : every dialect under data/dialects is compiled into the library.
# <list>       : only those dialects, e.g. -DCPP_PINYIN2_EMBED_DICTS="mandarin".
#
# An embedded build answers a table request by its last two components when the requested path does not exist,
# so the host may pass any root -- including one that is not there. A path that DOES exist always wins: an
# embedded copy must never shadow a real resource directory.
# ----------------------------------
set(CPP_PINYIN2_EMBED_DICTS "OFF" CACHE STRING
        "Embed the dictionary tables into the library: OFF, ON, or a ';'-separated list of dialect names")
set(_cpp_pinyin_embedded "")
if (NOT "${CPP_PINYIN2_EMBED_DICTS}" STREQUAL "OFF" AND NOT "${CPP_PINYIN2_EMBED_DICTS}" STREQUAL "")
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    if ("${CPP_PINYIN2_EMBED_DICTS}" STREQUAL "ON")
        set(_embed_args --all)
    else ()
        string(REPLACE ";" "," _embed_names "${CPP_PINYIN2_EMBED_DICTS}")
        set(_embed_args --dialects "${_embed_names}")
    endif ()
    set(_embed_generated "${CMAKE_CURRENT_BINARY_DIR}/embedded_dialects.cpp")
    execute_process(
            COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_dialects.py"
                    --out "${_embed_generated}" ${_embed_args}
            RESULT_VARIABLE _embed_result
            OUTPUT_VARIABLE _embed_output
            ERROR_VARIABLE _embed_error)
    if (NOT _embed_result EQUAL 0)
        message(FATAL_ERROR "embed_dialects.py failed (exit ${_embed_result}): ${_embed_output}${_embed_error}")
    endif ()
    message(STATUS "${_embed_output}")
    list(APPEND _src "${_embed_generated}")
    # The generated file lives in the build tree and includes the internal header by bare name, like its
    # sibling stub does; give that ONE translation unit the directory without widening the library's includes.
    set_source_files_properties("${_embed_generated}" PROPERTIES
            INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/src/dict")
    # Exactly one definition of findEmbeddedTable() may be linked: the generated one replaces the stub.
    list(FILTER _src EXCLUDE REGEX "/src/dict/EmbeddedTables\\.cpp$")
    set(_cpp_pinyin_embedded 1)
endif ()

add_library(${PROJECT_NAME})
# SONAME (D2, 2026-10-18). The shared library now produces `libcpp-pinyin2.so.2` (plus the unversioned
# symlink). This is NOT a binary-compatibility promise -- the policy is unchanged (docs/CONTRACT.md
# section 1: same-major package/API compatibility, rebuild for anything else). It exists because D24's
# reason for skipping it ("no external binary consumer before the first release") stops holding the moment
# vcpkg or a distribution ships the .so: a package without a soname cannot express an ABI level, and every
# "which .so is this?" question lands on the consumer. One number, PROJECT_VERSION_MAJOR, so a major bump
# moves the soname together with the header/target/symbol tokens (STABILITY R4).
if (BUILD_SHARED_LIBS)
    set_target_properties(${PROJECT_NAME} PROPERTIES
            VERSION ${PROJECT_VERSION}
            SOVERSION ${PROJECT_VERSION_MAJOR})
endif ()
if (NOT BUILD_SHARED_LIBS)
    target_compile_definitions(${PROJECT_NAME} PUBLIC CPP_PINYIN2_STATIC)
    # A static install tree must stay linkable INTO a consumer's shared object. Without PIC the archive
    # carries absolute-address relocations and `g++ -shared` on that archive fails at link time with
    # "relocation R_X86_64_PC32 ... can not be used when making a shared object" -- a defect the
    # consumer meets, not this build (2026-10-20 audit, G10). The in-tree probe below builds the exact
    # case so a regression is caught here instead.
    set_target_properties(${PROJECT_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif ()
target_compile_definitions(${PROJECT_NAME} PRIVATE CPP_PINYIN2_LIBRARY)

# The version the C ABI reports (cppp2_version). It is PROJECT_VERSION on purpose: that is the
# number a consumer resolves with find_package(cpp-pinyin2 <ver>). The API docs used to carry a "2.0.0"
# header of their own (docs/BACKLOG.md ISS-015); as of 2026-10-01 they point here instead, so this is
# the only place in the repository that spells a version number out (plan: `docs/DECISIONS.md` §2.1).
target_compile_definitions(${PROJECT_NAME} PRIVATE
        CPP_PINYIN2_VERSION_STRING="${PROJECT_VERSION}")

# Tells the library, the tests and the host which build this is. PUBLIC for the same reason
# CPP_PINYIN2_STATIC is: "this library carries its own tables" is a property a consumer may branch on, and a
# header-visible answer cannot drift from the library the way a separate config macro would.
if (_cpp_pinyin_embedded)
    target_compile_definitions(${PROJECT_NAME} PUBLIC CPP_PINYIN2_EMBED_DICTS=1)
endif ()

# The install layout of the dictionary tables, recorded so that a host linking this library directly can ask
# where they went: cppp2_dict_dir_candidate() resolves it against the library's own module directory (D2 of
# docs/DECISIONS.md section 4.1). Relative on purpose -- an absolute path would bake in
# the configure-time prefix, which `cmake --install --prefix` then contradicts (the same defect the tables'
# DESTINATION below was fixed for). GNUInstallDirs is included here because the block above includes it only
# when CPP_PINYIN2_INSTALL is ON, and this record must exist in every build that can be shipped.
include(GNUInstallDirs)
target_compile_definitions(${PROJECT_NAME} PRIVATE
        CPP_PINYIN2_DICT_SUBDIR="${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/dict")

# ----------------------------------
# Symbol visibility (D10 / plan doc section 6.7.8, measured 2026-10-01)
#
# The shared library has two kinds of consumers: C++ (find_package(cpp-pinyin2) + link) and the
# C ABI (Python ctypes, C# P/Invoke -- P3). Both must see exactly what the headers mark with
# CPP_PINYIN2_EXPORT and nothing else, so everything unmarked is hidden:
#   GCC/Clang:  -fvisibility=hidden (+ -fvisibility-inlines-hidden). The public surface already
#               carries __attribute__((visibility("default"))) via CPP_PINYIN2_EXPORT -- checked:
#               all 15 public classes/structs and the 9 free functions in include/cpp-pinyin2/*.h
#               have it; only the private `class Impl;` forward declarations do not.
#   MSVC/MinGW: no visibility flag -- a PE is not ELF. What matters there is that CPP_PINYIN2_EXPORT
#               maps to __declspec(dllexport), which include/cpp-pinyin2/core/PinyinGlobal.h now does for
#               __MINGW32__ as well (measured 2026-10-01: before that fix MinGW had no explicit
#               exports, so GNU ld exported every global symbol -- 250+, internals included).
# Applied to shared builds only: with -DBUILD_SHARED_LIBS=OFF there is no dynamic symbol table at all.
# The default IS shared (CMakeLists.txt:16), so these flags are on unless someone opts out.
# NOTE: -fvisibility only does something on ELF/Mach-O. On MinGW the equivalent fix lives in
# PinyinGlobal.h (__declspec for __MINGW32__), and its effect is verified on this machine; the ELF
# side of -fvisibility=hidden is not verified here -- no Linux toolchain locally (plan doc 6.7.9).
# No version script: the same .so/.dylib also serves C++ consumers, and a `global: cppp_*; local: *`
# list would hide exactly the symbols they link against. The gate for the C surface is
# tests/abi/check_exports.cmake (CTest entry ExportGate, shared builds).
# ----------------------------------
if (BUILD_SHARED_LIBS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE
            -fvisibility=hidden -fvisibility-inlines-hidden)
endif ()

# ${CMAKE_DL_LIBS} is `dl` on Linux and EMPTY on Windows/macOS. It must be here: `src/DictLocation.cpp` calls
# `dladdr` to locate the module directory, and on glibc < 2.34 (`dl` not yet merged into libc) a shared library that
# does not name it cannot be linked by ANY consumer -- measured on Ubuntu 20.04 / gcc 9.4, where our own test
# executables failed with "undefined reference to `dladdr'" while the same tree linked fine on newer glibc. PRIVATE
# because no public header exposes a `dl` symbol.
target_link_libraries(${PROJECT_NAME} PRIVATE $<BUILD_INTERFACE:cpp-pinyin2_dev_flags> ${CMAKE_DL_LIBS})

# ----------------------------------
# Warnings
#
# The library is warning-free under the strict set below (verified for both
# GCC and MSVC-style flag names); PRIVATE so consumers never inherit them.
#   GCC:   17 flags, -Wall -Wextra -Wpedantic ... -Wnull-dereference
#   Clang: the same set minus these 5 GCC-only flags:
#          -Wmisleading-indentation -Wextra-semi -Wlogical-op -Wsuggest-override -Wzero-as-null-pointer-constant
#   MSVC:  /W4
#
# WHY -Werror IS AN OPTION AND NOT THE DEFAULT (2026-10-20 audit, G09). The old comment here said "no
# -Werror so a newer compiler cannot break someone's build", which is true for CONSUMERS -- but the
# flags are PRIVATE and never reach them, so the sentence was protecting nobody while leaving 17 strict
# flags unable to fail anything: a new warning could only be noticed by a human reading the log. The
# option keeps both properties: OFF by default (a consumer building this tree is not held hostage by
# their compiler's new warnings) and ON in CI, where the warnings are ours to fix.
# ----------------------------------
# The strict set lives on an INTERFACE target so that everything we compile -- the library, the
# tools, the tests and the benchmark -- is held to it, instead of only the library target.
# (Measured 2026-09-23: it used to cover the library alone, so tools/tests were compiled at default
# warning level under docs/BACKLOG.md)
add_library(cpp-pinyin2_warnings INTERFACE)
option(CPP_PINYIN2_WERROR
       "Promote warnings to errors for the targets this build compiles (never inherited by consumers)" OFF)
if (CPP_PINYIN2_WERROR)
    if (MSVC)
        target_compile_options(cpp-pinyin2_warnings INTERFACE /WX)
    else ()
        target_compile_options(cpp-pinyin2_warnings INTERFACE -Werror)
    endif ()
endif ()
if (MSVC)
    target_compile_options(cpp-pinyin2_warnings INTERFACE /W4)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(cpp-pinyin2_warnings INTERFACE
            -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual
            -Wcast-qual -Wdouble-promotion -Wformat=2 -Wundef -Wredundant-decls
            -Wmisleading-indentation -Wextra-semi -Wlogical-op -Wsuggest-override
            -Wzero-as-null-pointer-constant -Wnull-dereference)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(cpp-pinyin2_warnings INTERFACE
            -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual
            -Wcast-qual -Wdouble-promotion -Wformat=2 -Wundef -Wredundant-decls
            -Wnull-dereference)
endif ()

# ----------------------------------
# Sanitizers
#
# Same shape as the warning set above: one INTERFACE target, linked PRIVATE by the library, the tools and
# the tests, so the flags never reach the exported target and a consumer of the installed package cannot
# inherit them. The target always exists (so the link lines are unconditional), but it carries flags only
# when the option is on.
#
# Two deliberate choices:
#   * -fno-sanitize-recover=all -- without it UBSan prints a runtime error and carries on, so a green run
#     would mean nothing. A gate that only prints is not a gate.
#   * the capability check -- if the compiler cannot build with these flags, configuring FAILS, rather
#     than quietly producing an uninstrumented build that then passes and claims to be sanitized.
#     Measured 2026-10-02: MinGW GCC 16 has no libasan at all (the linker cannot find -lasan), so
#     CPP_PINYIN2_SANITIZE=ON there is exactly the case this check exists for.
#
# WHY check_cxx_source_compiles AND NOT check_cxx_compiler_flag. The latter was tried first and produced a
# FALSE NEGATIVE: on ubuntu:24.04 with GCC 13.3, where libasan.so.8 and libubsan.so.1 are installed and an
# explicit `g++ -fsanitize=address,undefined` link succeeds, it reported failure -- CMake's own log shows
# the try-compile ending in "undefined reference to `__asan_init'", because the flag never reached the
# link step. A guard that rejects a working toolchain is as wrong as one that accepts a broken one, so the
# check here sets both CMAKE_REQUIRED_FLAGS (compile) and CMAKE_REQUIRED_LINK_OPTIONS (link), which is the
# combination measured to succeed in that same image.
# ----------------------------------
add_library(cpp-pinyin2_sanitizers INTERFACE)
if (CPP_PINYIN2_SANITIZE)
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_FLAGS "-fsanitize=address,undefined")
    set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=address,undefined")
    check_cxx_source_compiles("int main() { return 0; }" CPP_PINYIN_HAVE_SANITIZERS)
    unset(CMAKE_REQUIRED_FLAGS)
    unset(CMAKE_REQUIRED_LINK_OPTIONS)
    if (NOT CPP_PINYIN_HAVE_SANITIZERS)
        message(FATAL_ERROR
                "CPP_PINYIN2_SANITIZE=ON, but this compiler cannot build with -fsanitize=address,undefined. "
                "Build without the option, or use a toolchain that has the sanitizer runtimes.")
    endif ()
    target_compile_options(cpp-pinyin2_sanitizers INTERFACE
            -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
    target_link_options(cpp-pinyin2_sanitizers INTERFACE -fsanitize=address,undefined)
endif ()

# ----------------------------------
# One name for "the flags every C++ target in this project builds with".
#
# WHY IT EXISTS. Without it, every set of flags is a separate link line at every call site: adding the
# sanitizer set meant editing the library, both tools and three test targets, and the next set would mean
# editing them all again -- the drift this repository has been chasing all along. Now a new set is one line
# here.
#
# C++ ONLY, deliberately. cpp-pinyin2_warnings carries C++-only flags, so a C target must not link this.
# The C API smoke test (tests/c_api_smoke.c) is the case in point: it sets its own C warning flags and adds
# cpp-pinyin2_sanitizers directly, so the C surface is instrumented too without being fed C++ flags.
# ----------------------------------
add_library(cpp-pinyin2_dev_flags INTERFACE)
target_link_libraries(cpp-pinyin2_dev_flags INTERFACE cpp-pinyin2_warnings cpp-pinyin2_sanitizers)

target_sources(${PROJECT_NAME} PRIVATE ${_src})
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

# Consumers must inherit C++17 through the exported target, otherwise a downstream project that
# defaults to C++14 (MSVC still does) fails on the headers. INTERFACE_COMPILE_FEATURES is what
# the generated cpp-pinyin2Targets.cmake publishes; the PRIVATE standard below only covers this
# build.
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)

if (CPP_PINYIN2_BUILD_TESTS)
    # enable_testing() has to happen in the ROOT scope: `include(CTest)` inside tests/ only
    # generates tests/CTestTestfile.cmake, so `ctest` run from the build root used to report
    # "No tests were found" and every registered test was unreachable (measured 2026-09-28).
    enable_testing()
    add_subdirectory(tests)
    add_subdirectory(tools)
    # NOTE: the dict/verified-table copies for the tests are NOT here. As POST_BUILD commands of this
    # library target they only refreshed when the library relinked, so a data-only edit left the
    # stale copy behind while the suite happily measured it (see tests/CMakeLists.txt).
endif ()

if (CPP_PINYIN2_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif ()

# ----------------------------------
# Add platform-specific settings
# ----------------------------------
if (WIN32)
    set(RC_DESCRIPTION "A lightweight Chinese/Cantonese to Pinyin library.")
    set(RC_COPYRIGHT "Copyright (C) 2023-2024 wolfgitpr")
    include("cmake/winrc.cmake")
endif ()

if (APPLE)
    # The floor is 10.14, and it is a requirement rather than a preference: C++17's std::filesystem is
    # only <experimental/filesystem> on 10.13, and this tree uses std::filesystem throughout
    # (PLUGIN_INTERFACE_AND_BINDINGS.md section 6.5 item 2, which also records that the plan's macOS wheel
    # is meant to be universal2 with an explicit deployment target, which is also not implemented).
    #
    # It lives here, in the tree, rather than only in a workflow: every consumer of this project -- the
    # Python wheel, the C# package, a downstream `find_package` -- then inherits one answer instead of each
    # workflow remembering it. That matters because a deployment target that is too high does not fail
    # locally: the binary links and runs on the machine that built it and refuses to start on an older
    # system, which is the same class of silent breakage as building a Linux wheel on a new glibc.
    #
    # CMake only sets MACOSX_DEPLOYMENT_TARGET itself when CMAKE_OSX_DEPLOYMENT_TARGET is non-empty, and
    # the cache is what makes that value survive into targets created by add_subdirectory. An explicit
    # -DCMAKE_OSX_DEPLOYMENT_TARGET=... on the command line still wins, so an experiment can raise it
    # without touching this file; lowering it below 10.14 is what the guard below refuses.
    if (NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET STREQUAL "")
        # The floor depends on the ARCHITECTURE, and applying one value to both was wrong: 10.14 is what
        # C++17 std::filesystem needs on x86_64, but arm64 does not exist before macOS 11. A single 10.14
        # either gets silently raised by the toolchain (so the declared value is not the artifact's) or
        # produces a dylib that cannot load. Measured on neither leg -- this is the value the toolchain
        # documents, and the message below prints the one actually chosen.
        if (CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
            set(_cpp_pinyin2_osx_floor "11.0")
        else()
            set(_cpp_pinyin2_osx_floor "10.14")
        endif()
        set(CMAKE_OSX_DEPLOYMENT_TARGET "${_cpp_pinyin2_osx_floor}" CACHE STRING
            "Minimum macOS version (x86_64: 10.14 for std::filesystem; arm64: 11.0, the architecture floor)" FORCE)
        message(STATUS "macOS deployment target: ${CMAKE_OSX_DEPLOYMENT_TARGET} (architectures: ${CMAKE_OSX_ARCHITECTURES})")
    endif ()

    if (CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS 10.14)
        # Refused rather than warned about: a wheel tagged for an older macOS than the library can run on
        # is exactly the wheel this setting exists to prevent, and a warning in a 12,000-line CI log is not
        # a gate.
        message(FATAL_ERROR
                "CMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET} is below 10.14, but this "
                "project needs C++17 std::filesystem (PLUGIN doc section 6.5 item 2). Use 10.14 or newer.")
    endif ()
    message(STATUS "macOS deployment target: ${CMAKE_OSX_DEPLOYMENT_TARGET}")

    # No MACOSX_DEPLOYMENT_TARGET environment variable is set here: CMake has already turned the cache
    # value into that compile definition, and setting it again would create a second place where the floor
    # is written.
endif ()

# ----------------------------------
# Link libraries
# ----------------------------------
target_include_directories(${PROJECT_NAME} PRIVATE include src src/toneUtil)
target_include_directories(${PROJECT_NAME} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# ----------------------------------
# Copy dictionary files
# ----------------------------------
# Moved to tests/CMakeLists.txt as a dependency-tracked command (see the note in the tests block
# above): a POST_BUILD copy of this target does not run when only data/dialects changes.

# ----------------------------------
# Install settings
# ----------------------------------
if (CPP_PINYIN2_INSTALL)
    target_include_directories(${PROJECT_NAME} PUBLIC
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )

    install(TARGETS ${PROJECT_NAME}
            EXPORT ${PROJECT_NAME}Targets
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
            LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    )

    install(DIRECTORY include/${PROJECT_NAME}
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    )

    # Generate and install package config and version files
    #
    # COMPATIBILITY SameMajorVersion, NOT AnyNewerVersion (changed with the 2.0 interface work). The
    # package promises API compatibility inside a major version and never binary compatibility; 2.0
    # moved EVERY public header (the 1.x flat `Pinyin.h`/`ManTone.h`/`CanTone.h` layout became `core/`,
    # `dialects/`, `ext/`). Under AnyNewerVersion a request from the 1.x line was satisfied by a 2.0
    # install: the configure succeeded and the compile then failed on headers that no longer exist --
    # a mismatch that reads as a packaging defect. tests/install/check_install_consumer.cmake now
    # asserts that a cross-major request is refused.
    write_basic_package_version_file(
            "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
            VERSION ${PROJECT_VERSION}
            COMPATIBILITY SameMajorVersion
    )

    configure_package_config_file(
            ${CMAKE_CURRENT_LIST_DIR}/${PROJECT_NAME}Config.cmake.in
            "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
            INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
            NO_CHECK_REQUIRED_COMPONENTS_MACRO
    )

    install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
            DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
    )

    install(EXPORT ${PROJECT_NAME}Targets
            FILE ${PROJECT_NAME}Targets.cmake
            NAMESPACE ${PROJECT_NAME}::
            DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
    )

    # DESTINATION must stay RELATIVE: an absolute one (previously
    # "${CMAKE_INSTALL_PREFIX}/share/${PROJECT_NAME}") is baked in at configure time and ignores
    # `cmake --install --prefix`, so the dictionaries escaped the requested prefix while the .pc
    # file advertised ${prefix}/share/... . Measured before the fix: `cmake --install --prefix
    # <tmp>` produced "file(INSTALL DESTINATION "/share/cpp-pinyin2")".
    # Source layout and installed layout deliberately differ in one word: the repository keeps its data
    # under data/dialects/ (each dialect next to its dialect.json), while the installed tree keeps the
    # long-standing share/cpp-pinyin2/dict/ name that the .pc file, the install-consumer test and the
    # documentation all refer to. The trailing slash on DIRECTORY copies the contents, so the installed
    # tree is share/cpp-pinyin2/dict/<dialect>/<table> exactly as before this batch.
    install(
            DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data/dialects/
            DESTINATION "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/dict"
    )
endif ()

# ----------------------------------
# Package config (.pc file) generation
#
# Only meaningful when installing: CMAKE_INSTALL_LIBDIR comes from GNUInstallDirs,
# which is included above only in the CPP_PINYIN2_INSTALL branch. Keeping these
# inside the guard avoids an install rule with an unset destination directory.
# ----------------------------------
if (CPP_PINYIN2_INSTALL)
    set(version ${PROJECT_VERSION})

    # Cflags has to say what the CMake target says (2026-10-20 audit, G11). Two gaps were measured:
    # `CPP_PINYIN2_STATIC` is a PUBLIC compile definition (it selects the no-dllimport branch of
    # PinyinGlobal.h), so a pkg-config consumer of a STATIC install that does not get it compiles
    # against the dllimport branch -- harmless on ELF where both spell the same, wrong on MSVC/MinGW;
    # and the library needs C++17, which pkg-config consumers were never told. Neither is inherited
    # from the .pc the way they are from the CMake target, so both are written out here.
    set(CPP_PINYIN2_PC_EXTRA_CFLAGS "")
    if (NOT BUILD_SHARED_LIBS)
        set(CPP_PINYIN2_PC_EXTRA_CFLAGS " -DCPP_PINYIN2_STATIC")
    endif ()

    # Configure the .pc file
    configure_file(
            ${CMAKE_CURRENT_LIST_DIR}/cpp-pinyin2.pc.in
            ${CMAKE_BINARY_DIR}/cpp-pinyin2.pc
            @ONLY
    )

    # Install the .pc file
    install(
            FILES ${CMAKE_BINARY_DIR}/cpp-pinyin2.pc
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
    )
endif ()
